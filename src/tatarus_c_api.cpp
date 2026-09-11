#define TATARUS_SDK_BUILD
#include "tatarus/c_api.h"
#include "tatarus/imaginatio.hpp"
#include "tatarus/robot_mind.hpp"
#include "tatarus/organism.hpp"
#ifdef TATARUS_HAS_CORTEX
#include "tatarus/cortex.hpp"
#include "tatarus/imaginatio_cortex.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
tatarus::RobotMindConfig makeRobotMindConfig(
    std::uint64_t seed,
    std::size_t neuronCount) {
    tatarus::RobotMindConfig config;
    config.seed = seed;
    config.neuronCount = neuronCount;
    return config;
}

tatarus::organism::OrganismConfig makeSyntheticOrganismConfig(
    std::uint64_t seed,
    std::size_t neuronCount) {
    tatarus::organism::OrganismConfig config;
    config.mind = makeRobotMindConfig(seed ? seed : 7411U, neuronCount);
    return config;
}

tatarus::VisualImaginationConfig makeOrganismImaginationConfig() {
    tatarus::VisualImaginationConfig config;
    config.width = 512;
    config.height = 512;
    config.exposureObservations = 4;
    // Keep a complete 512x512 corpus addressable after one batch lesson while
    // retaining a finite memory ceiling for exact prototypes and motor traces.
    config.maximumEngrams = 1024;
    // A 512x512 RGB lesson contains roughly eight thousand serial motor
    // actions. Sampling retinal/neural feedback at the supported upper bound
    // keeps the interactive C-ABI/UI path responsive while preserving the
    // first/last feedback points and the complete causal motor trace.
    config.neuralFeedbackStride = 256;
    config.paintPatchSide = 8;
    config.physiologicalExpressionGain = 0.12;
    return config;
}
} // namespace

struct tatarus_mind {
    explicit tatarus_mind(std::uint64_t seed, std::size_t neuronCount = 96)
        : mind(makeRobotMindConfig(seed, neuronCount)) {}
    tatarus::RobotMind mind;
    std::uint64_t navigationContext = 0;
};

struct tatarus_organism {
    explicit tatarus_organism(std::uint64_t seed, std::size_t neuronCount = 96)
        : organism(makeSyntheticOrganismConfig(seed, neuronCount)),
          imaginationConfig(makeOrganismImaginationConfig()),
          imaginatio(organism.mind(), imaginationConfig) {}
    tatarus::organism::SyntheticOrganism organism;
    tatarus::VisualImaginationConfig imaginationConfig;
    tatarus::VisualImagination imaginatio;
    std::uint64_t navigationContext = 0;
#ifdef TATARUS_HAS_CORTEX
    tatarus::cortex::CortexConfig cortexConfig{};
    std::shared_ptr<tatarus::cortex::LmStudioClient> cortexModel;
    std::unique_ptr<tatarus::cortex::CortexOrchestrator> cortex;
    tatarus::cortex::ImaginatioCortexAdapter cortexImaginatioAdapter;
    std::filesystem::path cortexConfigPath;
    bool cortexLmConnected = false;
    std::string cortexModelName;
    std::vector<std::string> cortexModels;
    std::string cortexProbeError;
    std::uint64_t cortexLastExecutedRequestId = 0;
    bool cortexPendingUsesImagination = false;
    std::string cortexLastExecution;
#endif
};


namespace {
thread_local std::string g_lastError;

int fail(const std::exception& error) {
    g_lastError = error.what();
    return 0;
}

int failMessage(const char* message) {
    g_lastError = message;
    return 0;
}

std::uint64_t copyJson(
    const std::string& value,
    char* buffer,
    std::uint64_t capacity) {
    if (value.size() >= std::numeric_limits<std::uint64_t>::max()) {
        g_lastError = "JSON snapshot is too large";
        return 0;
    }
    const auto required = static_cast<std::uint64_t>(value.size() + 1U);
    if (!buffer || capacity == 0) {
        g_lastError.clear();
        return required;
    }
    if (capacity < required) {
        g_lastError = "JSON snapshot buffer is too small";
        return required;
    }
    std::memcpy(buffer, value.c_str(), static_cast<std::size_t>(required));
    g_lastError.clear();
    return required;
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4U) & 0xFU] << hex[c & 0xFU];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

#ifdef TATARUS_HAS_CORTEX
const char* cortexExecutionModeName(tatarus::cortex::CortexExecutionMode value) noexcept {
    using E = tatarus::cortex::CortexExecutionMode;
    switch (value) {
        case E::Off: return "OFF";
        case E::Live: return "LIVE";
        case E::Record: return "RECORD";
        case E::Replay: return "REPLAY";
    }
    return "UNKNOWN";
}

const char* cortexSleepPhaseName(tatarus::SleepPhase value) noexcept {
    switch (value) {
        case tatarus::SleepPhase::Wake: return "WAKE";
        case tatarus::SleepPhase::Nrem: return "NREM";
        case tatarus::SleepPhase::Rem: return "REM";
    }
    return "UNKNOWN";
}

void appendJsonStringArray(std::ostringstream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) out << ',';
        out << '"' << jsonEscape(values[index]) << '"';
    }
    out << ']';
}

std::vector<std::string> cortexImaginationCapabilities() {
    return tatarus::cortex::ImaginatioCortexAdapter::capabilities();
}

std::uint64_t cortexCurrentStep(const tatarus_organism& handle) {
    const auto telemetry = handle.organism.telemetry();
    return telemetry.available ? telemetry.stepCount : handle.organism.mind().metrics().experiences;
}

std::string cortexStatusJson(const tatarus_organism& handle) {
    using namespace tatarus::cortex;
    std::ostringstream out;
    out << std::boolalpha;
    out.precision(8);
    out << "{\"schema\":\"tatarus-cortex-ui-v1\",\"available\":true";
    const bool configured = static_cast<bool>(handle.cortex);
    out << ",\"configured\":" << configured;
    out << ",\"lm_studio\":{";
    out << "\"connected\":" << handle.cortexLmConnected;
    out << ",\"model\":\"" << jsonEscape(handle.cortexModelName) << "\"";
    out << ",\"models\":";
    appendJsonStringArray(out, handle.cortexModels);
    out << ",\"error\":\"" << jsonEscape(handle.cortexProbeError) << "\"}";
    if (!configured) {
        out << ",\"mode\":\"DISABLED\",\"execution_mode\":\"OFF\"";
        out << ",\"autonomous_ready\":false";
        out << ",\"last_execution\":\"" << jsonEscape(handle.cortexLastExecution) << "\"";
        out << '}';
        return out.str();
    }

    out << ",\"mode\":\"" << toString(handle.cortexConfig.mode) << "\"";
    out << ",\"execution_mode\":\"" << cortexExecutionModeName(handle.cortexConfig.executionMode) << "\"";
    out << ",\"config_path\":\"" << jsonEscape(handle.cortexConfigPath.string()) << "\"";

    const auto status = handle.cortex->status();
    const auto executive = handle.cortex->executiveStatus();
    const auto meta = handle.cortex->metacognitionStatus();
    const auto learning = handle.cortex->learningStatus();
    const auto imagination = handle.cortexImaginatioAdapter.imaginationState(handle.imaginatio);

    out << ",\"sleep_phase\":\"" << cortexSleepPhaseName(status.lastSleepPhase) << "\"";
    out << ",\"idle\":" << handle.cortex->idle();
    out << ",\"last_error\":\"" << jsonEscape(status.lastError) << "\"";
    out << ",\"last_execution\":\"" << jsonEscape(handle.cortexLastExecution) << "\"";

    out << ",\"metrics\":{";
    out << "\"requests\":" << status.metrics.requestsSubmitted;
    out << ",\"responses\":" << status.metrics.responsesReceived;
    out << ",\"transport_errors\":" << status.metrics.transportErrors;
    out << ",\"stale\":" << status.metrics.staleResponses;
    out << ",\"accepted\":" << status.metrics.arbiterAccepted;
    out << ",\"rejected\":" << status.metrics.arbiterRejected;
    out << ",\"imagination_directives\":" << status.metrics.imaginationDirectivesPrepared;
    out << ",\"executive_cycles\":" << status.metrics.executiveCycles;
    out << ",\"plans_adopted\":" << status.metrics.plansAdopted;
    out << ",\"top_down_prepared\":" << status.metrics.topDownCuesPrepared;
    out << ",\"top_down_applied\":" << status.metrics.topDownCuesApplied;
    out << ",\"metacognitive_suppressions\":" << status.metrics.metacognitiveSuppressions;
    out << '}';

    out << ",\"last_trigger\":\"" << toString(status.lastTrigger) << "\"";
    if (status.lastRequest) {
        const auto& request = *status.lastRequest;
        out << ",\"last_request\":{";
        out << "\"id\":\"" << request.requestId << "\"";
        out << ",\"task\":\"" << toString(request.task) << "\"";
        out << ",\"goal\":\"" << jsonEscape(request.goal) << "\"";
        out << ",\"fingerprint\":\"" << request.stateFingerprint << "\"}";
    } else {
        out << ",\"last_request\":null";
    }
    if (status.lastResponse) {
        const auto& response = *status.lastResponse;
        out << ",\"last_response\":{";
        out << "\"summary\":\"" << jsonEscape(response.summary) << "\"";
        out << ",\"strategy_count\":" << response.strategies.size();
        out << ",\"has_imagination\":" << response.imagination.has_value();
        out << ",\"has_plan\":" << response.plan.has_value() << '}';
    } else {
        out << ",\"last_response\":null";
    }
    if (status.lastDecision) {
        const auto& decision = *status.lastDecision;
        out << ",\"last_decision\":{";
        out << "\"kind\":\"" << toString(decision.kind) << "\"";
        out << ",\"score\":" << decision.score;
        out << ",\"reason\":\"" << jsonEscape(decision.reason) << "\"";
        if (decision.selectedStrategy) {
            out << ",\"strategy\":{";
            out << "\"id\":\"" << jsonEscape(decision.selectedStrategy->id) << "\"";
            out << ",\"kind\":\"" << toString(decision.selectedStrategy->kind) << "\"";
            out << ",\"rationale\":\"" << jsonEscape(decision.selectedStrategy->rationale) << "\"}";
        } else {
            out << ",\"strategy\":null";
        }
        out << '}';
    } else {
        out << ",\"last_decision\":null";
    }

    out << ",\"executive\":{";
    out << "\"next_goal_id\":\"" << executive.nextGoalId << "\"";
    std::optional<CortexGoal> activeGoal;
    for (const auto& goal : executive.goals) {
        if (goal.status == CortexGoalStatus::Active) { activeGoal = goal; break; }
    }
    if (!activeGoal) {
        for (const auto& goal : executive.goals) {
            if (goal.status == CortexGoalStatus::Pending) { activeGoal = goal; break; }
        }
    }
    out << ",\"active_goal\":";
    if (activeGoal) {
        out << "{\"id\":\"" << activeGoal->id << "\",\"text\":\""
            << jsonEscape(activeGoal->text) << "\",\"priority\":" << activeGoal->priority
            << ",\"status\":\"" << toString(activeGoal->status) << "\"}";
    } else {
        out << "null";
    }
    out << ",\"goals\":[";
    for (std::size_t i = 0; i < executive.goals.size(); ++i) {
        if (i) out << ',';
        const auto& goal = executive.goals[i];
        out << "{\"id\":\"" << goal.id << "\",\"text\":\"" << jsonEscape(goal.text)
            << "\",\"priority\":" << goal.priority << ",\"status\":\"" << toString(goal.status) << "\"}";
    }
    out << ']';
    out << ",\"working_memory\":[";
    for (std::size_t i = 0; i < executive.workingMemory.size(); ++i) {
        if (i) out << ',';
        const auto& item = executive.workingMemory[i];
        out << "{\"key\":\"" << jsonEscape(item.key) << "\",\"value\":\"" << jsonEscape(item.value)
            << "\",\"salience\":" << item.salience << '}';
    }
    out << ']';
    out << ",\"plan\":";
    if (executive.activePlan) {
        out << "{\"id\":\"" << jsonEscape(executive.activePlan->id) << "\",\"active_step\":" << executive.activePlanStep << ",\"steps\":[";
        for (std::size_t i = 0; i < executive.activePlan->steps.size(); ++i) {
            if (i) out << ',';
            const auto& step = executive.activePlan->steps[i];
            out << "{\"id\":\"" << jsonEscape(step.id) << "\",\"kind\":\"" << toString(step.kind)
                << "\",\"objective\":\"" << jsonEscape(step.objective) << "\",\"status\":\"" << toString(step.status) << "\"}";
        }
        out << "]}";
    } else {
        out << "null";
    }
    out << '}';

    out << ",\"metacognition\":{";
    out << "\"state\":\"" << toString(meta.state) << "\"";
    out << ",\"reliability\":" << meta.aggregateReliability;
    out << ",\"transport_reliability\":" << meta.transportReliability;
    out << ",\"acceptance_rate\":" << meta.decisionAcceptanceRate;
    out << ",\"assisted_success_rate\":" << meta.assistedSuccessRate;
    out << ",\"stale_rate\":" << meta.staleRate << '}';

    out << ",\"learning\":{";
    out << "\"episodes_started\":" << learning.episodesStarted;
    out << ",\"episodes_completed\":" << learning.episodesCompleted;
    out << ",\"cortex_assisted\":" << learning.cortexAssistedEpisodes;
    out << ",\"autonomous\":" << learning.autonomousEpisodes;
    out << ",\"competencies\":" << learning.competencies.size() << '}';

    out << ",\"grounding\":{";
    out << "\"visual_engrams\":" << imagination.visualEngrams;
    out << ",\"symbol_engrams\":" << imagination.symbolEngrams;
    out << ",\"known_concepts\":"; appendJsonStringArray(out, imagination.knownConcepts);
    out << ",\"known_symbols\":"; appendJsonStringArray(out, imagination.knownSymbols);
    out << ",\"known_categories\":"; appendJsonStringArray(out, imagination.knownCategories);
    out << '}';

    std::optional<ImaginatioDirective> translatedDirective;
    if (status.lastResponse && status.lastDecision) {
        translatedDirective = handle.cortexImaginatioAdapter.translate(
            *status.lastDecision, *status.lastResponse, handle.imaginatio,
            status.lastRequest ? status.lastRequest->goal : std::string_view{});
    }
    const bool executable = status.lastRequest && translatedDirective
        && status.lastRequest->requestId != handle.cortexLastExecutedRequestId
        && translatedDirective->executable;
    out << ",\"imagination_executable\":" << executable;
    out << ",\"imagination_directive\":";
    if (translatedDirective) {
        out << "{\"kind\":\"" << toString(translatedDirective->kind) << "\"";
        out << ",\"source_strategy\":\"" << toString(translatedDirective->sourceStrategy) << "\"";
        out << ",\"strategy_id\":\"" << jsonEscape(translatedDirective->strategyId) << "\"";
        out << ",\"concept\":\"" << jsonEscape(translatedDirective->conceptText) << "\"";
        out << ",\"symbols\":";
        appendJsonStringArray(out, translatedDirective->symbols);
        out << ",\"executable\":" << translatedDirective->executable;
        out << ",\"reason\":\"" << jsonEscape(translatedDirective->reason) << "\"}";
    } else {
        out << "null";
    }
    out << '}';
    return out.str();
}
#endif

tatarus::VisualCanvas canvasFromPixels(
    const double* pixels,
    std::uint64_t pixelCount) {
    constexpr std::size_t sourceSide = 32;
    constexpr std::size_t targetSide = 512;
    if (!pixels || pixelCount != sourceSide * sourceSide) {
        throw std::invalid_argument("IMAGINATIO requires exactly 1024 row-major pixels");
    }
    for (std::size_t index = 0; index < sourceSide * sourceSide; ++index) {
        if (!std::isfinite(pixels[index])) {
            throw std::invalid_argument("IMAGINATIO pixels must be finite");
        }
    }
    tatarus::VisualCanvas canvas(targetSide, targetSide);
    for (std::size_t y = 0; y < targetSide; ++y) {
        for (std::size_t x = 0; x < targetSide; ++x) {
            const std::size_t sourceX = x * sourceSide / targetSide;
            const std::size_t sourceY = y * sourceSide / targetSide;
            canvas.setPixel(x, y, pixels[sourceY * sourceSide + sourceX]);
        }
    }
    return canvas;
}

tatarus::VisualCanvas canvasFromRgbPixels(
    const double* pixels,
    std::uint64_t channelCount) {
    constexpr std::size_t sourceSide = 32;
    constexpr std::size_t targetSide = 512;
    constexpr std::size_t channels = sourceSide * sourceSide * 3U;
    if (!pixels || channelCount != channels) {
        throw std::invalid_argument(
            "IMAGINATIO RGB requires exactly 3072 interleaved row-major channels");
    }
    for (std::size_t index = 0; index < sourceSide * sourceSide; ++index) {
        const auto red = pixels[index * 3U];
        const auto green = pixels[index * 3U + 1U];
        const auto blue = pixels[index * 3U + 2U];
        if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) {
            throw std::invalid_argument("IMAGINATIO RGB channels must be finite");
        }
    }
    tatarus::VisualCanvas canvas(targetSide, targetSide);
    for (std::size_t y = 0; y < targetSide; ++y) {
        for (std::size_t x = 0; x < targetSide; ++x) {
            const std::size_t sourceX = x * sourceSide / targetSide;
            const std::size_t sourceY = y * sourceSide / targetSide;
            const std::size_t source = (sourceY * sourceSide + sourceX) * 3U;
            canvas.setColorPixel(x, y, pixels[source], pixels[source + 1U], pixels[source + 2U]);
        }
    }
    return canvas;
}

tatarus::VisualCanvas canvasFromRgb8(
    const std::uint8_t* pixels,
    std::uint64_t channelCount,
    std::uint64_t width,
    std::uint64_t height) {
    constexpr std::size_t side = 512;
    if (!pixels || width != side || height != side
        || channelCount != side * side * 3U) {
        throw std::invalid_argument(
            "IMAGINATIO RGB8 requires exactly 512x512 interleaved sRGB channels");
    }
    tatarus::VisualCanvas canvas(side, side);
    for (std::size_t index = 0; index < side * side; ++index) {
        canvas.setColorPixel(
            index % side, index / side,
            static_cast<double>(pixels[index * 3U]) / 255.0,
            static_cast<double>(pixels[index * 3U + 1U]) / 255.0,
            static_cast<double>(pixels[index * 3U + 2U]) / 255.0);
    }
    return canvas;
}

std::vector<std::string> commaSeparatedSymbols(const char* raw) {
    if (!raw) throw std::invalid_argument("IMAGINATIO symbols must not be null");
    std::vector<std::string> result;
    std::istringstream input(raw);
    std::string symbol;
    while (std::getline(input, symbol, ',')) {
        const auto first = symbol.find_first_not_of(" \t\r\n");
        const auto last = symbol.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) result.push_back(symbol.substr(first, last - first + 1U));
    }
    return result;
}

tatarus::SceneDescription sceneFromC(const tatarus_scene_description* raw) {
    if (!raw || !raw->objects || raw->object_count == 0U
        || raw->object_count > 1024U
        || (raw->relation_count > 0U && !raw->relations)) {
        throw std::invalid_argument("IMAGINATIO scene description is invalid");
    }
    tatarus::SceneDescription scene;
    scene.name = raw->name ? raw->name : "SCENE";
    scene.backgroundColor = {
        raw->background_red, raw->background_green, raw->background_blue,
    };
    scene.objects.reserve(static_cast<std::size_t>(raw->object_count));
    for (std::uint64_t index = 0; index < raw->object_count; ++index) {
        const auto& object = raw->objects[index];
        if (!object.instance || !object.category) {
            throw std::invalid_argument("IMAGINATIO scene object requires instance and category");
        }
        tatarus::SceneObjectCue cue;
        cue.instance = object.instance;
        cue.category = object.category;
        cue.pose = object.pose ? object.pose : "canonical";
        if ((object.present_mask & TATARUS_SCENE_HAS_X) != 0U) cue.x = object.x;
        if ((object.present_mask & TATARUS_SCENE_HAS_Y) != 0U) cue.y = object.y;
        if ((object.present_mask & TATARUS_SCENE_HAS_SCALE) != 0U) cue.scale = object.scale;
        if ((object.present_mask & TATARUS_SCENE_HAS_ROTATION) != 0U) {
            cue.rotation = object.rotation;
        }
        if ((object.present_mask & TATARUS_SCENE_HAS_DEPTH) != 0U) cue.depth = object.depth;
        scene.objects.push_back(std::move(cue));
    }
    scene.relations.reserve(static_cast<std::size_t>(raw->relation_count));
    for (std::uint64_t index = 0; index < raw->relation_count; ++index) {
        const auto& relation = raw->relations[index];
        if (!relation.subject || !relation.object
            || relation.relation_kind
                > static_cast<std::uint32_t>(tatarus::SceneRelationKind::ConnectedTo)) {
            throw std::invalid_argument("IMAGINATIO scene relation is invalid");
        }
        scene.relations.push_back(tatarus::SceneRelationCue{
            .subject = relation.subject,
            .relation = static_cast<tatarus::SceneRelationKind>(relation.relation_kind),
            .object = relation.object,
            .confidence = relation.confidence,
        });
    }
    return scene;
}

tatarus::SceneAction sceneActionFromC(const tatarus_scene_action* raw) {
    if (!raw || !raw->actor
        || raw->action_kind > static_cast<std::uint32_t>(tatarus::SceneActionKind::Custom)) {
        throw std::invalid_argument("IMAGINATIO scene action is invalid");
    }
    return tatarus::SceneAction{
        .kind = static_cast<tatarus::SceneActionKind>(raw->action_kind),
        .label = raw->label ? raw->label : "",
        .actor = raw->actor,
        .target = raw->target ? raw->target : "",
        .direction = {raw->direction_x, raw->direction_y},
        .magnitude = raw->magnitude,
        .duration = raw->duration,
    };
}

void appendAllocentricGridContext(
    tatarus::Experience& experience,
    double goalX,
    double goalZ) {
    // Entorhinal-like distributed place code derived from the already
    // available allocentric target vector.  No map coordinate, visited-cell
    // table or external planner is injected into the mind.  Multiple spatial
    // frequencies and three axes give the context population a stable,
    // heading-independent basis from which recurrent assemblies can form place
    // engrams and later be recognised again.
    constexpr double pi = 3.14159265358979323846;
    constexpr std::array<double, 4> frequencies{1.0, 2.0, 4.0, 8.0};
    constexpr double invSqrt2 = 0.70710678118654752440;
    experience.spatialContext.reserve(
        experience.spatialContext.size() + frequencies.size() * 6U);
    for (const double frequency : frequencies) {
        const std::array<double, 3> axes{
            goalX,
            goalZ,
            invSqrt2 * (goalX + goalZ),
        };
        for (const double axis : axes) {
            const double phase = pi * frequency * axis;
            experience.spatialContext.push_back(std::sin(phase));
            experience.spatialContext.push_back(std::cos(phase));
        }
    }
}

std::uint64_t episodicNavigationContext(
    std::uint64_t mapContext,
    const tatarus_observation& observation) {
    if (mapContext == 0) return 0;
    const double normalizedHeading = std::clamp(
        observation.rotation[1] / 4.5,
        -1.0,
        1.0);
    const auto heading = static_cast<std::uint64_t>(std::llround(
        (normalizedHeading + 1.0) * 1.5));
    // FNV-style combination keeps the opaque map-layout identity while giving
    // each arrival orientation its own episodic place/action memory.
    std::uint64_t context = mapContext;
    context ^= heading + 0x9e3779b97f4a7c15ULL;
    context *= 1099511628211ULL;
    return context == 0 ? 1 : context;
}

tatarus::Experience experienceFromObservation(
    const tatarus_observation& observation,
    std::uint64_t navigationContext,
    bool detailedRobotSensors) {
    tatarus::Experience experience;
    experience.timestampNs = observation.timestamp_ns;
    for (std::size_t i = 0; i < 3; ++i) {
        experience.imu.acceleration[i] = observation.acceleration[i];
        experience.imu.rotation[i] = observation.rotation[i];
    }
    experience.environment.light = observation.light;
    experience.environment.soundLevel = observation.sound_level;
    experience.environment.proximity = std::clamp(observation.proximity[0], 0.0, 1.0);
    experience.environment.temperature = observation.temperature;
    experience.environment.novelty = observation.novelty;
    experience.body.battery = observation.battery;
    experience.body.contactLeft = 1.0 - std::clamp(observation.proximity[1], 0.0, 1.0);
    experience.body.contactRight = 1.0 - std::clamp(observation.proximity[2], 0.0, 1.0);
    experience.reward = observation.reward;
    const double goalX = std::clamp(observation.goal_direction[0], -1.0, 1.0);
    const double goalZ = std::clamp(observation.goal_direction[1], -1.0, 1.0);
    experience.spatialEpisodeContext = episodicNavigationContext(
        navigationContext, observation);
    appendAllocentricGridContext(experience, goalX, goalZ);
    experience.vision = {
        std::max(0.0, -goalZ),
        std::max(0.0, goalX),
        std::max(0.0, goalZ),
        std::max(0.0, -goalX),
    };
    if (detailedRobotSensors) {
        experience.vision.insert(experience.vision.end(), {
            std::clamp(observation.proximity[0], 0.0, 1.0),
            std::clamp(observation.proximity[1], 0.0, 1.0),
            std::clamp(observation.proximity[2], 0.0, 1.0),
            std::clamp(observation.light, 0.0, 1.0)});
        experience.audio = {
            std::clamp(observation.sound_level, 0.0, 1.0),
            std::clamp(observation.novelty, 0.0, 1.0)};
        experience.touch = {
            experience.body.contactLeft,
            experience.body.contactRight,
            std::clamp(observation.battery, 0.0, 1.0),
            std::clamp((observation.temperature - 20.0) / 40.0, -1.0, 1.0)};
    }
    return experience;
}

tatarus::ScannerFrame scannerFrameFromC(
    const tatarus_observation& observation,
    std::uint64_t environmentId,
    const tatarus_pose3d& pose,
    const tatarus_range_reading* readings,
    std::uint64_t readingCount) {
    if (environmentId == 0) throw std::invalid_argument("environment_id must be non-zero");
    if (readingCount > 1'000'000ULL) {
        throw std::invalid_argument("reading_count is implausibly large");
    }
    if (readingCount > 0 && !readings) {
        throw std::invalid_argument("readings is null while reading_count is non-zero");
    }
    tatarus::ScannerFrame frame;
    frame.environmentId = environmentId;
    frame.timestampNs = observation.timestamp_ns;
    for (std::size_t i = 0; i < 3; ++i) frame.pose.positionMeters[i] = pose.position_m[i];
    for (std::size_t i = 0; i < 4; ++i) {
        frame.pose.orientationQuaternion[i] = pose.orientation_xyzw[i];
    }
    frame.readings.reserve(static_cast<std::size_t>(readingCount));
    for (std::uint64_t index = 0; index < readingCount; ++index) {
        tatarus::RangeReading reading;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            reading.direction[axis] = readings[index].direction[axis];
        }
        reading.distanceMeters = readings[index].distance_m;
        reading.maxRangeMeters = readings[index].max_range_m;
        reading.confidence = readings[index].confidence;
        reading.hit = readings[index].hit != 0;
        if (readings[index].semantic_label) {
            const char* source = readings[index].semantic_label;
            std::size_t length = 0;
            while (length < 4096U && source[length] != '\0') ++length;
            reading.semanticLabel.assign(source, length);
        }
        frame.readings.push_back(std::move(reading));
    }
    return frame;
}

void fillProductState(const tatarus::ObserveResult& result, tatarus_state& state) {
    state.assembly_id = result.assemblyId;
    state.predicted_assembly_id = result.prediction.expectedAssemblyId;
    state.prediction_confidence = result.prediction.confidence;
    state.prediction_error = result.predictionError;
    state.learned_transitions = result.metrics.learnedTransitions;
    state.experiences = result.metrics.experiences;
    state.predictions = result.metrics.predictions;
}

void fillCartographyUpdate(
    const tatarus::CartographyUpdate& source,
    tatarus_cartography_update& target) {
    target.environment_id = source.environmentId;
    target.accepted_rays = source.acceptedRays;
    target.rejected_rays = source.rejectedRays;
    target.touched_voxels = source.touchedVoxels;
    target.newly_mapped_voxels = source.newlyMappedVoxels;
    target.occupied_endpoints = source.occupiedEndpoints;
    target.mapped_voxels = source.summary.mappedVoxels;
    target.free_voxels = source.summary.freeVoxels;
    target.occupied_voxels = source.summary.occupiedVoxels;
    target.uncertain_voxels = source.summary.uncertainVoxels;
    target.frontier_voxels = source.summary.frontierVoxels;
    target.local_novelty = source.localNovelty;
    target.frontier_ratio = source.frontierRatio;
    for (std::size_t i = 0; i < source.obstacleProximity.size(); ++i) {
        target.obstacle_proximity[i] = source.obstacleProximity[i];
    }
}

tatarus::organism::AtmosphericEnvironment atmosphereFromC(
    const tatarus_atmospheric_environment& source) {
    tatarus::organism::AtmosphericEnvironment result;
    result.totalPressureKPa = source.total_pressure_kpa;
    result.o2Fraction = source.o2_fraction;
    result.co2Fraction = source.co2_fraction;
    result.n2Fraction = source.n2_fraction;
    result.temperatureC = source.temperature_c;
    result.relativeHumidity = source.relative_humidity;
    result.dustPpm = source.dust_ppm;
    return result;
}

tatarus::organism::RobotPhysicalLoad physicalLoadFromC(
    const tatarus_robot_physical_load& source) {
    tatarus::organism::RobotPhysicalLoad result;
    result.motorCurrentA = source.motor_current_a;
    result.motorVoltageV = source.motor_voltage_v;
    result.jointTorqueNm = source.joint_torque_nm;
    result.angularVelocityRadPerS = source.angular_velocity_rad_per_s;
    result.mechanicalPowerW = source.mechanical_power_w;
    result.cpuGpuPowerW = source.cpu_gpu_power_w;
    result.batteryChargeRemainingFraction = source.battery_charge_fraction;
    result.chassisTemperatureC = source.chassis_temperature_c;
    return result;
}

tatarus_identity_decision_kind convertIdentityKind(
    tatarus::IdentityDecisionKind kind) {
    switch (kind) {
        case tatarus::IdentityDecisionKind::NewIdentity:
            return TATARUS_IDENTITY_NEW;
        case tatarus::IdentityDecisionKind::MatchedIdentity:
            return TATARUS_IDENTITY_MATCHED;
        case tatarus::IdentityDecisionKind::ProvisionalAssociation:
            return TATARUS_IDENTITY_PROVISIONAL_ASSOCIATION;
        case tatarus::IdentityDecisionKind::ProvisionalIdentity:
            return TATARUS_IDENTITY_PROVISIONAL_IDENTITY;
        case tatarus::IdentityDecisionKind::Uncertain:
            return TATARUS_IDENTITY_UNCERTAIN;
    }
    return TATARUS_IDENTITY_UNCERTAIN;
}
} // namespace

extern "C" {

tatarus_mind* tatarus_create(uint64_t seed) {
    try {
        g_lastError.clear();
        return new tatarus_mind(seed ? seed : 7411U);
    } catch (const std::exception& error) {
        fail(error);
        return nullptr;
    }
}

tatarus_mind* tatarus_create_sized(uint64_t seed, uint32_t neuron_count) {
    try {
        g_lastError.clear();
        return new tatarus_mind(
            seed ? seed : 7411U,
            static_cast<std::size_t>(neuron_count));
    } catch (const std::exception& error) {
        fail(error);
        return nullptr;
    }
}

void tatarus_destroy(tatarus_mind* mind) {
    delete mind;
}

int tatarus_observe(
    tatarus_mind* handle,
    const tatarus_observation* observation,
    tatarus_state* state) {
    if (!handle || !observation || !state) {
        return failMessage("null pointer passed to tatarus_observe");
    }
    try {
        tatarus::Experience experience;
        experience.timestampNs = observation->timestamp_ns;
        for (std::size_t i = 0; i < 3; ++i) {
            experience.imu.acceleration[i] = observation->acceleration[i];
            experience.imu.rotation[i] = observation->rotation[i];
        }
        experience.environment.light = observation->light;
        experience.environment.soundLevel = observation->sound_level;
        experience.environment.proximity = std::clamp(observation->proximity[0], 0.0, 1.0);
        experience.environment.temperature = observation->temperature;
        experience.environment.novelty = observation->novelty;
        experience.body.battery = observation->battery;
        experience.body.contactLeft = 1.0 - std::clamp(observation->proximity[1], 0.0, 1.0);
        experience.body.contactRight = 1.0 - std::clamp(observation->proximity[2], 0.0, 1.0);
        experience.reward = observation->reward;
        const double goalX = std::clamp(observation->goal_direction[0], -1.0, 1.0);
        const double goalZ = std::clamp(observation->goal_direction[1], -1.0, 1.0);
        experience.spatialEpisodeContext = episodicNavigationContext(
            handle->navigationContext, *observation);
        appendAllocentricGridContext(experience, goalX, goalZ);
        experience.vision = {
            std::max(0.0, -goalZ), // north
            std::max(0.0, goalX),  // east
            std::max(0.0, goalZ),  // south
            std::max(0.0, -goalX), // west
        };

        const auto result = handle->mind.observe(experience);
        state->assembly_id = result.assemblyId;
        state->predicted_assembly_id = result.prediction.expectedAssemblyId;
        state->prediction_confidence = result.prediction.confidence;
        state->prediction_error = result.predictionError;
        state->learned_transitions = result.metrics.learnedTransitions;
        state->experiences = result.metrics.experiences;
        state->predictions = result.metrics.predictions;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_explore(
    tatarus_mind* handle,
    const tatarus_observation* observation,
    uint64_t environment_id,
    const tatarus_pose3d* pose,
    const tatarus_range_reading* readings,
    uint64_t reading_count,
    tatarus_state* state,
    tatarus_cartography_update* map_update) {
    if (!handle || !observation || !pose || !state || !map_update) {
        return failMessage("null pointer passed to tatarus_explore");
    }
    try {
        const auto experience = experienceFromObservation(
            *observation, handle->navigationContext, false);
        const auto scanner = scannerFrameFromC(
            *observation, environment_id, *pose, readings, reading_count);
        const auto result = handle->mind.observeExplorer(experience, scanner);
        fillProductState(result.cognition, *state);
        fillCartographyUpdate(result.cartography, *map_update);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_observe_identity(
    tatarus_mind* handle,
    const tatarus_identity_observation* observation,
    tatarus_identity_state* state) {
    if (!handle || !observation || !state) {
        return failMessage("null pointer passed to tatarus_observe_identity");
    }
    try {
        tatarus::IdentityObservation input;
        for (std::size_t i = 0; i < input.features.size(); ++i) {
            input.features[i] = observation->features[i];
        }
        input.cameraId = observation->camera_id;
        input.timestampSeconds = observation->timestamp_seconds;
        input.quality = observation->quality;

        const auto decision = handle->mind.observeIdentity(input);
        state->kind = convertIdentityKind(decision.kind);
        state->entity_id = decision.entityId;
        state->active_assembly_id = decision.activeAssemblyId;
        state->confidence = decision.confidence;
        state->hypothesis_evidence = decision.hypothesisEvidence;
        state->evidence_required = decision.evidenceRequired;
        state->baseline_accepted = decision.baselineAccepted ? 1 : 0;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_confirm_last_identity(tatarus_mind* handle, int32_t correct) {
    if (!handle) return failMessage("null pointer passed to tatarus_confirm_last_identity");
    try {
        handle->mind.confirmLastIdentity(correct != 0);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_get_biology(tatarus_mind* handle, tatarus_biology_state* state) {
    if (!handle || !state) return failMessage("null pointer passed to tatarus_get_biology");
    try {
        const auto biology = handle->mind.biology();
        state->available = biology.available ? 1 : 0;
        state->dendritic_segments = biology.dendriticSegments;
        state->astrocytes = biology.astrocytes;
        state->capillaries = biology.capillaries;
        state->oligodendrocytes = biology.oligodendrocytes;
        state->microglia = biology.microglia;
        state->dendritic_spikes = biology.dendriticSpikes;
        state->myelin_remodeling_updates = biology.myelinRemodelingUpdates;
        state->microglial_surveillance_updates = biology.microglialSurveillanceUpdates;
        state->microglial_pruning_events = biology.microglialPruningEvents;
        state->microglial_repair_events = biology.microglialRepairEvents;
        state->microglial_damage_signals = biology.microglialDamageSignals;
        state->oxygen = biology.oxygen;
        state->glucose = biology.glucose;
        state->flow = biology.flow;
        state->mean_axon_length_um = biology.meanAxonLengthUm;
        state->myelin_coverage = biology.myelinCoverage;
        state->conduction_velocity_mps = biology.conductionVelocityMps;
        state->effective_delay_ms = biology.effectiveDelayMs;
        state->oligodendrocyte_reserve = biology.oligodendrocyteReserve;
        state->microglia_activation = biology.microgliaActivation;
        state->complement_tag = biology.complementTag;
        state->repair_capacity = biology.repairCapacity;
        state->inflammatory_tone = biology.inflammatoryTone;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_get_tissue_mechanics(
    tatarus_mind* handle,
    tatarus_tissue_mechanics_state* state) {
    if (!handle || !state) {
        return failMessage("null pointer passed to tatarus_get_tissue_mechanics");
    }
    try {
        const auto biology = handle->mind.biology();
        state->available = biology.available ? 1 : 0;
        state->baseline_active_synapses = biology.baselineActiveSynapses;
        state->active_synapses = biology.mechanicsActiveSynapses;
        state->growth_limited_events = biology.growthLimitedEvents;
        state->baseline_tissue_volume_um3 = biology.baselineTissueVolumeUm3;
        state->tissue_volume_um3 = biology.tissueVolumeUm3;
        state->tissue_volume_ratio = biology.tissueVolumeRatio;
        state->linear_expansion = biology.linearExpansion;
        state->effective_tissue_mass_ng = biology.effectiveTissueMassNg;
        state->net_biomass_change_ng = biology.netBiomassChangeNg;
        state->synaptic_material_volume_um3 = biology.synapticMaterialVolumeUm3;
        state->solid_packing_fraction = biology.solidPackingFraction;
        state->extracellular_space_fraction = biology.extracellularSpaceFraction;
        state->tissue_pressure_kpa = biology.tissuePressureKPa;
        state->material_reserve = biology.materialReserve;
        state->cumulative_material_synthesized_um3 = biology.cumulativeMaterialSynthesizedUm3;
        state->cumulative_material_recycled_um3 = biology.cumulativeMaterialRecycledUm3;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_get_prospection(
    tatarus_mind* handle,
    tatarus_prospection_state* state) {
    if (!handle || !state) return failMessage("null pointer passed to tatarus_get_prospection");
    try {
        const auto p = handle->mind.prospection();
        state->available = p.available ? 1 : 0;
        state->learned_transitions = p.learnedTransitions;
        state->observed_transitions = p.observedTransitions;
        state->prediction_hits = p.predictionHits;
        state->prediction_misses = p.predictionMisses;
        state->predicted_assembly_id = p.predictedAssemblyId;
        state->last_assembly_id = p.lastAssemblyId;
        state->prediction_confidence = p.predictionConfidence;
        state->expected_delay_ms = p.expectedDelayMs;
        state->prediction_error = p.predictionError;
        state->temporal_surprise = p.temporalSurprise;
        state->sequence_familiarity = p.sequenceFamiliarity;
        state->prospective_activation = p.prospectiveActivation;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_get_physiology(
    tatarus_mind* handle,
    tatarus_physiology_state* state) {
    if (!handle || !state) return failMessage("null pointer passed to tatarus_get_physiology");
    try {
        const auto p = handle->mind.physiology();
        state->available = p.available ? 1 : 0;
        state->finite = p.finite ? 1 : 0;
        state->sleep_phase = p.sleepPhase == tatarus::SleepPhase::Nrem
            ? TATARUS_SLEEP_NREM
            : p.sleepPhase == tatarus::SleepPhase::Rem ? TATARUS_SLEEP_REM : TATARUS_SLEEP_WAKE;
        state->parvalbumin_neurons = p.parvalbuminNeurons;
        state->somatostatin_neurons = p.somatostatinNeurons;
        state->vip_neurons = p.vipNeurons;
        state->tagged_synapses = p.taggedSynapses;
        state->l_ltp_events = p.longTermPotentiationEvents;
        state->l_ltd_events = p.longTermDepressionEvents;
        state->sleep_transitions = p.sleepTransitions;
        state->extracellular_na_mm = p.extracellularNaMm;
        state->extracellular_k_mm = p.extracellularKMm;
        state->extracellular_ca_mm = p.extracellularCaMm;
        state->extracellular_cl_mm = p.extracellularClMm;
        state->atp = p.atp;
        state->pump_activity = p.pumpActivity;
        state->cumulative_atp_consumed = p.cumulativeAtpConsumed;
        state->heat_production_pj = p.heatProductionPj;
        state->entropy_production_pj_per_k = p.entropyProductionPjPerK;
        state->dopamine = p.dopamine;
        state->serotonin = p.serotonin;
        state->noradrenaline = p.noradrenaline;
        state->acetylcholine = p.acetylcholine;
        state->camp = p.camp;
        state->ip3 = p.ip3;
        state->hcn_density = p.hcnDensity;
        state->kv_density = p.kvDensity;
        state->creb_activation = p.crebActivation;
        state->protein_pool = p.proteinPool;
        state->synaptic_tag = p.synapticTag;
        state->circadian_phase_hours = p.circadianPhaseHours;
        state->sleep_pressure = p.sleepPressure;
        state->extracellular_volume_fraction = p.extracellularVolumeFraction;
        state->glymphatic_clearance = p.glymphaticClearance;
        state->tissue_waste = p.tissueWaste;
        state->gamma_power = p.gammaPower;
        state->theta_power = p.thetaPower;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_get_motor(tatarus_mind* handle, tatarus_motor_state* state) {
    if (!handle || !state) return failMessage("null pointer passed to tatarus_get_motor");
    try {
        const auto motor = handle->mind.motor();
        state->available = motor.available ? 1 : 0;
        state->selected_direction = motor.selectedDirection;
        for (std::size_t i = 0; i < motor.directionalActivity.size(); ++i) {
            state->directional_activity[i] = motor.directionalActivity[i];
        }
        state->movement = motor.movement;
        state->attention = motor.attention;
        state->vocalization = motor.vocalization;
        state->confidence = motor.confidence;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_begin_action(
    tatarus_mind* handle,
    uint64_t action_id,
    double intensity) {
    if (!handle || action_id == 0) return failMessage("invalid tatarus_begin_action arguments");
    try {
        handle->mind.beginAction(tatarus::ActionEvent{
            .id = action_id,
            .label = "motor-" + std::to_string(action_id),
            .intensity = intensity,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_end_action(
    tatarus_mind* handle,
    uint64_t action_id,
    double reward,
    double success,
    double novelty) {
    if (!handle || action_id == 0) return failMessage("invalid tatarus_end_action arguments");
    try {
        handle->mind.endAction(tatarus::ActionOutcome{
            .id = action_id,
            .reward = reward,
            .success = success,
            .novelty = novelty,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_end_episode(tatarus_mind* handle, int32_t reached_goal) {
    if (!handle) return failMessage("invalid tatarus_end_episode arguments");
    try {
        handle->mind.endEpisode(reached_goal != 0);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_set_learning_enabled(tatarus_mind* handle, int32_t enabled) {
    if (!handle) return failMessage("invalid tatarus_set_learning_enabled arguments");
    try {
        handle->mind.setLearningEnabled(enabled != 0);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_set_navigation_context(tatarus_mind* handle, uint64_t context_id) {
    if (!handle) return failMessage("invalid tatarus_set_navigation_context arguments");
    handle->navigationContext = context_id;
    g_lastError.clear();
    return 1;
}

int tatarus_set_intervention(
    tatarus_mind* handle,
    const tatarus_intervention* intervention) {
    if (!handle || !intervention) {
        return failMessage("null pointer passed to tatarus_set_intervention");
    }
    try {
        handle->mind.setExperimentalIntervention(tatarus::ExperimentalIntervention{
            .astrocyteFunction = intervention->astrocyte_function,
            .oxygenSupply = intervention->oxygen_supply,
            .glucoseSupply = intervention->glucose_supply,
            .pumpEfficiency = intervention->pump_efficiency,
            .myelinIntegrity = intervention->myelin_integrity,
            .microgliaFunction = intervention->microglia_function,
            .neuromodulatorGain = intervention->neuromodulator_gain,
            .sleepEnabled = intervention->sleep_enabled != 0,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_apply_damage(
    tatarus_mind* handle,
    double neuron_fraction,
    double synapse_fraction,
    uint64_t seed) {
    if (!handle) return failMessage("null pointer passed to tatarus_apply_damage");
    try {
        handle->mind.applyExperimentalDamage(
            neuron_fraction, synapse_fraction, seed);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

uint64_t tatarus_get_spatial_json(
    tatarus_mind* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null pointer passed to tatarus_get_spatial_json");
        return 0;
    }
    try {
        return copyJson(handle->mind.spatialJson(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

uint64_t tatarus_get_live_json(
    tatarus_mind* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null pointer passed to tatarus_get_live_json");
        return 0;
    }
    try {
        return copyJson(handle->mind.liveJson(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

uint64_t tatarus_get_physiology_json(
    tatarus_mind* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null pointer passed to tatarus_get_physiology_json");
        return 0;
    }
    try {
        return copyJson(handle->mind.physiologyJson(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

uint64_t tatarus_get_environment_map_json(
    tatarus_mind* handle,
    uint64_t environment_id,
    char* buffer,
    uint64_t capacity) {
    if (!handle || environment_id == 0) {
        failMessage("invalid arguments passed to tatarus_get_environment_map_json");
        return 0;
    }
    try {
        return copyJson(handle->mind.environmentMapJson(environment_id), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_clear_environment_map(tatarus_mind* handle, uint64_t environment_id) {
    if (!handle || environment_id == 0) {
        return failMessage("invalid arguments passed to tatarus_clear_environment_map");
    }
    handle->mind.clearEnvironmentMap(environment_id);
    g_lastError.clear();
    return 1;
}

int tatarus_save(tatarus_mind* handle, const char* directory) {
    if (!handle || !directory) return failMessage("null pointer passed to tatarus_save");
    try {
        handle->mind.saveSnapshot(directory);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_load(tatarus_mind* handle, const char* directory) {
    if (!handle || !directory) return failMessage("null pointer passed to tatarus_load");
    try {
        const bool loaded = handle->mind.loadSnapshot(directory);
        if (!loaded) g_lastError = "snapshot not found or incomplete";
        else g_lastError.clear();
        return loaded ? 1 : 0;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

const char* tatarus_last_error(void) {
    return g_lastError.c_str();
}

tatarus_organism* tatarus_organism_create(uint64_t seed) {
    try {
        g_lastError.clear();
        return new tatarus_organism(seed ? seed : 7411U);
    } catch (const std::exception& error) {
        fail(error);
        return nullptr;
    }
}

tatarus_organism* tatarus_organism_create_sized(uint64_t seed, uint32_t neuron_count) {
    try {
        if (neuron_count < 16 || neuron_count > 65536) {
            failMessage("organism neuron_count must be between 16 and 65536");
            return nullptr;
        }
        g_lastError.clear();
        return new tatarus_organism(seed ? seed : 7411U, neuron_count);
    } catch (const std::exception& error) {
        fail(error);
        return nullptr;
    }
}

void tatarus_organism_destroy(tatarus_organism* organism) {
    delete organism;
}

int tatarus_organism_step(
    tatarus_organism* handle,
    const tatarus_observation* observation,
    const tatarus_atmospheric_environment* atmosphere,
    double dt_seconds,
    tatarus_state* state) {
    if (!handle || !observation || !atmosphere) {
        return failMessage("null pointer passed to tatarus_organism_step");
    }
    try {
        tatarus::Experience experience;
        experience.timestampNs = observation->timestamp_ns;
        for (std::size_t i = 0; i < 3; ++i) {
            experience.imu.acceleration[i] = observation->acceleration[i];
            experience.imu.rotation[i] = observation->rotation[i];
        }
        experience.environment.light = observation->light;
        experience.environment.soundLevel = observation->sound_level;
        experience.environment.proximity = std::clamp(observation->proximity[0], 0.0, 1.0);
        experience.environment.temperature = observation->temperature;
        experience.environment.novelty = observation->novelty;
        experience.body.battery = observation->battery;
        experience.body.contactLeft = 1.0 - std::clamp(observation->proximity[1], 0.0, 1.0);
        experience.body.contactRight = 1.0 - std::clamp(observation->proximity[2], 0.0, 1.0);
        experience.reward = observation->reward;
        const double goalX = std::clamp(observation->goal_direction[0], -1.0, 1.0);
        const double goalZ = std::clamp(observation->goal_direction[1], -1.0, 1.0);
        experience.spatialEpisodeContext = episodicNavigationContext(
            handle->navigationContext, *observation);
        appendAllocentricGridContext(experience, goalX, goalZ);
        experience.vision = {
            std::max(0.0, -goalZ),
            std::max(0.0, goalX),
            std::max(0.0, goalZ),
            std::max(0.0, -goalX),
            std::clamp(observation->proximity[0], 0.0, 1.0),
            std::clamp(observation->proximity[1], 0.0, 1.0),
            std::clamp(observation->proximity[2], 0.0, 1.0),
            std::clamp(observation->light, 0.0, 1.0)
        };
        experience.audio = {
            std::clamp(observation->sound_level, 0.0, 1.0),
            std::clamp(observation->novelty, 0.0, 1.0)
        };
        experience.touch = {
            experience.body.contactLeft,
            experience.body.contactRight,
            std::clamp(observation->battery, 0.0, 1.0),
            std::clamp((observation->temperature - 20.0) / 40.0, -1.0, 1.0)
        };

        tatarus::organism::AtmosphericEnvironment env;
        env.totalPressureKPa = atmosphere->total_pressure_kpa;
        env.o2Fraction = atmosphere->o2_fraction;
        env.co2Fraction = atmosphere->co2_fraction;
        env.n2Fraction = atmosphere->n2_fraction;
        env.temperatureC = atmosphere->temperature_c;
        env.relativeHumidity = atmosphere->relative_humidity;
        env.dustPpm = atmosphere->dust_ppm;

        const auto result = handle->organism.step(experience, env, dt_seconds);
        if (state) {
            state->assembly_id = result.assemblyId;
            state->predicted_assembly_id = result.prediction.expectedAssemblyId;
            state->prediction_confidence = result.prediction.confidence;
            state->prediction_error = result.predictionError;
            state->learned_transitions = result.prospection.learnedTransitions;
            state->experiences = result.metrics.experiences;
            state->predictions = result.metrics.predictions;
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_get_telemetry(
    tatarus_organism* handle,
    tatarus_organism_telemetry_c* out_telemetry) {
    if (!handle || !out_telemetry) {
        return failMessage("null pointer passed to tatarus_organism_get_telemetry");
    }
    try {
        const auto t = handle->organism.telemetry();
        out_telemetry->available = t.available ? 1 : 0;
        out_telemetry->step_count = t.stepCount;
        out_telemetry->simulated_time_seconds = t.simulatedTimeSeconds;

        /* Circulation */
        out_telemetry->total_blood_volume_l = t.circulation.totalBloodVolumeL;
        out_telemetry->mean_arterial_pressure_mm_hg = t.circulation.meanArterialPressureMmHg;
        out_telemetry->systolic_pressure_mm_hg = t.circulation.systolicPressureMmHg;
        out_telemetry->diastolic_pressure_mm_hg = t.circulation.diastolicPressureMmHg;
        out_telemetry->central_venous_pressure_mm_hg = t.circulation.centralVenousPressureMmHg;
        out_telemetry->arterial_po2_mm_hg = t.circulation.arterialPo2MmHg;
        out_telemetry->arterial_pco2_mm_hg = t.circulation.arterialPco2MmHg;
        out_telemetry->arterial_oxygen_saturation = t.circulation.arterialOxygenSaturation;
        out_telemetry->arterial_ph = t.circulation.arterialPh;
        out_telemetry->plasma_glucose_mm = t.circulation.plasmaGlucoseMm;
        out_telemetry->plasma_na_mm = t.circulation.plasmaNaMm;
        out_telemetry->plasma_k_mm = t.circulation.plasmaKMm;
        out_telemetry->plasma_ca_mm = t.circulation.plasmaCaMm;
        out_telemetry->blood_temperature_c = t.circulation.bloodTemperatureC;

        /* Heart */
        out_telemetry->heart_rate_bpm = t.heart.heartRateBpm;
        out_telemetry->stroke_volume_ml = t.heart.strokeVolumeMl;
        out_telemetry->cardiac_output_l_per_min = t.heart.cardiacOutputLPerMin;
        out_telemetry->ejection_fraction_lv = t.heart.ejectionFractionLv;
        out_telemetry->left_ventricle_pressure_mm_hg = t.heart.leftVentriclePressureMmHg;
        out_telemetry->myocardial_o2_consumption_ml_per_min = t.heart.myocardialO2ConsumptionMlPerMin;
        out_telemetry->ischemic_stress_index = t.heart.ischemicStressIndex;

        /* Lung */
        out_telemetry->respiration_rate_bpm = t.lung.respirationRateBpm;
        out_telemetry->tidal_volume_l = t.lung.tidalVolumeL;
        out_telemetry->minute_ventilation_l_per_min = t.lung.minuteVentilationLPerMin;
        out_telemetry->alveolar_po2_mm_hg = t.lung.alveolarPo2MmHg;
        out_telemetry->alveolar_pco2_mm_hg = t.lung.alveolarPco2MmHg;
        out_telemetry->o2_uptake_rate_ml_per_min = t.lung.o2UptakeRateMlPerMin;

        /* Kidney */
        out_telemetry->glomerular_filtration_rate_ml_per_min = t.kidney.glomerularFiltrationRateMlPerMin;
        out_telemetry->urine_output_rate_ml_per_min = t.kidney.urineOutputRateMlPerMin;
        out_telemetry->urine_osmolarity_m_osm_per_kg = t.kidney.urineOsmolarityMOsmPerKg;
        out_telemetry->renin_secretion_rate = t.kidney.reninSecretionRate;
        out_telemetry->left_kidney_gfr_ml_per_min = t.leftKidney.glomerularFiltrationRateMlPerMin;
        out_telemetry->right_kidney_gfr_ml_per_min = t.rightKidney.glomerularFiltrationRateMlPerMin;
        out_telemetry->left_kidney_urine_output_ml_per_min = t.leftKidney.urineOutputRateMlPerMin;
        out_telemetry->right_kidney_urine_output_ml_per_min = t.rightKidney.urineOutputRateMlPerMin;
        out_telemetry->left_kidney_renal_blood_flow_ml_per_min = t.leftKidney.renalBloodFlowMlPerMin;
        out_telemetry->right_kidney_renal_blood_flow_ml_per_min = t.rightKidney.renalBloodFlowMlPerMin;

        /* Interoception */
        out_telemetry->visceral_distress = t.interoception.visceralDistress;
        out_telemetry->cardiovascular_load = t.interoception.cardiovascularLoad;
        out_telemetry->metabolic_depletion = t.interoception.metabolicDepletion;
        out_telemetry->respiratory_hypoxia = t.interoception.respiratoryHypoxia;
        out_telemetry->fluid_electrolyte_imbalance = t.interoception.fluidElectrolyteImbalance;
        out_telemetry->sympathetic_tone = t.interoception.sympatheticTone;
        out_telemetry->parasympathetic_tone = t.interoception.parasympatheticTone;

        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

uint64_t tatarus_organism_get_json(
    tatarus_organism* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null pointer passed to tatarus_organism_get_json");
        return 0;
    }
    try {
        return copyJson(handle->organism.organismJson(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_organism_step_with_load(
    tatarus_organism* handle,
    const tatarus_observation* observation,
    const tatarus_atmospheric_environment* atmosphere,
    const tatarus_robot_physical_load* load,
    double dt_seconds,
    tatarus_state* state) {
    if (!handle || !observation || !atmosphere || !load) {
        return failMessage("null pointer passed to tatarus_organism_step_with_load");
    }
    try {
        tatarus::Experience experience;
        experience.timestampNs = observation->timestamp_ns;
        for (std::size_t i = 0; i < 3; ++i) {
            experience.imu.acceleration[i] = observation->acceleration[i];
            experience.imu.rotation[i] = observation->rotation[i];
        }
        experience.environment.light = observation->light;
        experience.environment.soundLevel = observation->sound_level;
        experience.environment.proximity = std::clamp(observation->proximity[0], 0.0, 1.0);
        experience.environment.temperature = observation->temperature;
        experience.environment.novelty = observation->novelty;
        experience.body.battery = observation->battery;
        experience.body.contactLeft = 1.0 - std::clamp(observation->proximity[1], 0.0, 1.0);
        experience.body.contactRight = 1.0 - std::clamp(observation->proximity[2], 0.0, 1.0);
        experience.reward = observation->reward;
        const double goalX = std::clamp(observation->goal_direction[0], -1.0, 1.0);
        const double goalZ = std::clamp(observation->goal_direction[1], -1.0, 1.0);
        experience.spatialEpisodeContext = episodicNavigationContext(
            handle->navigationContext, *observation);
        appendAllocentricGridContext(experience, goalX, goalZ);
        experience.vision = {
            std::max(0.0, -goalZ),
            std::max(0.0, goalX),
            std::max(0.0, goalZ),
            std::max(0.0, -goalX),
            std::clamp(observation->proximity[0], 0.0, 1.0),
            std::clamp(observation->proximity[1], 0.0, 1.0),
            std::clamp(observation->proximity[2], 0.0, 1.0),
            std::clamp(observation->light, 0.0, 1.0)
        };
        experience.audio = {
            std::clamp(observation->sound_level, 0.0, 1.0),
            std::clamp(observation->novelty, 0.0, 1.0)
        };
        experience.touch = {
            experience.body.contactLeft,
            experience.body.contactRight,
            std::clamp(observation->battery, 0.0, 1.0),
            std::clamp((observation->temperature - 20.0) / 40.0, -1.0, 1.0)
        };

        tatarus::organism::AtmosphericEnvironment atmo;
        atmo.totalPressureKPa = atmosphere->total_pressure_kpa;
        atmo.o2Fraction = atmosphere->o2_fraction;
        atmo.co2Fraction = atmosphere->co2_fraction;
        atmo.n2Fraction = atmosphere->n2_fraction;
        atmo.temperatureC = atmosphere->temperature_c;
        atmo.relativeHumidity = atmosphere->relative_humidity;
        atmo.dustPpm = atmosphere->dust_ppm;

        tatarus::organism::RobotPhysicalLoad physLoad;
        physLoad.motorCurrentA = load->motor_current_a;
        physLoad.motorVoltageV = load->motor_voltage_v;
        physLoad.jointTorqueNm = load->joint_torque_nm;
        physLoad.angularVelocityRadPerS = load->angular_velocity_rad_per_s;
        physLoad.mechanicalPowerW = load->mechanical_power_w;
        physLoad.cpuGpuPowerW = load->cpu_gpu_power_w;
        physLoad.batteryChargeRemainingFraction = load->battery_charge_fraction;
        physLoad.chassisTemperatureC = load->chassis_temperature_c;

        const auto result = handle->organism.stepWithLoad(experience, atmo, physLoad, dt_seconds);
        if (state) {
            state->assembly_id = result.assemblyId;
            state->predicted_assembly_id = result.prediction.expectedAssemblyId;
            state->prediction_confidence = result.prediction.confidence;
            state->prediction_error = result.predictionError;
            state->learned_transitions = result.prospection.learnedTransitions;
            state->experiences = result.metrics.experiences;
            state->predictions = result.metrics.predictions;
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_explore_step_with_load(
    tatarus_organism* handle,
    const tatarus_observation* observation,
    uint64_t environment_id,
    const tatarus_pose3d* pose,
    const tatarus_range_reading* readings,
    uint64_t reading_count,
    const tatarus_atmospheric_environment* atmosphere,
    const tatarus_robot_physical_load* load,
    double dt_seconds,
    tatarus_state* state,
    tatarus_cartography_update* map_update) {
    if (!handle || !observation || !pose || !atmosphere || !load
        || !state || !map_update) {
        return failMessage("null pointer passed to tatarus_organism_explore_step_with_load");
    }
    try {
        const auto experience = experienceFromObservation(
            *observation, handle->navigationContext, true);
        const auto scanner = scannerFrameFromC(
            *observation, environment_id, *pose, readings, reading_count);
        const auto result = handle->organism.stepExplorerWithLoad(
            experience,
            scanner,
            atmosphereFromC(*atmosphere),
            physicalLoadFromC(*load),
            dt_seconds);
        fillProductState(result.cognition, *state);
        fillCartographyUpdate(result.cartography, *map_update);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_get_interoception(
    tatarus_organism* handle,
    tatarus_interoception_state_c* out_state) {
    if (!handle || !out_state) {
        return failMessage("null pointer passed to tatarus_organism_get_interoception");
    }
    try {
        const auto s = handle->organism.interoceptionState();
        out_state->arterial_pressure = s.arterialPressure;
        out_state->venous_pressure = s.venousPressure;
        out_state->cardiac_load = s.cardiacLoad;
        out_state->heart_rate = s.heartRate;
        out_state->arterial_o2 = s.arterialO2;
        out_state->arterial_co2 = s.arterialCO2;
        out_state->blood_ph = s.bloodPH;
        out_state->glucose = s.glucose;
        out_state->sodium = s.sodium;
        out_state->potassium = s.potassium;
        out_state->calcium = s.calcium;
        out_state->osmolarity = s.osmolarity;
        out_state->body_temperature = s.bodyTemperature;
        out_state->renal_stress = s.renalStress;
        out_state->hypoxia = s.hypoxia;
        out_state->hypercapnia = s.hypercapnia;
        out_state->visceral_distress = s.visceralDistress;
        out_state->metabolic_stress = s.metabolicStress;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_audit_conservation(
    tatarus_organism* handle,
    tatarus_conservation_audit_c* out_audit,
    double tolerance) {
    if (!handle || !out_audit) {
        return failMessage("null pointer passed to tatarus_organism_audit_conservation");
    }
    try {
        const auto audit = handle->organism.auditConservation(tolerance);
        out_audit->balanced = audit.balanced ? 1 : 0;
        out_audit->max_discrepancy = audit.maxDiscrepancy;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

uint64_t tatarus_organism_snapshot_save(
    tatarus_organism* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null pointer passed to tatarus_organism_snapshot_save");
        return 0;
    }
    try {
        const auto stateStr = handle->organism.serializeState();
        return copyJson(stateStr, buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_organism_snapshot_load(
    tatarus_organism* handle,
    const char* buffer) {
    if (!handle || !buffer) {
        return failMessage("null pointer passed to tatarus_organism_snapshot_load");
    }
    try {
        if (handle->organism.deserializeState(buffer)) {
            g_lastError.clear();
            return 1;
        }
        return failMessage("failed to deserialize organism state");
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_infuse(
    tatarus_organism* handle,
    double volume_ml,
    double na_mm,
    double k_mm,
    double glucose_mm) {
    if (!handle) return failMessage("null pointer passed to tatarus_organism_infuse");
    try {
        tatarus::organism::SoluteProfile solutes;
        solutes.naMm = na_mm;
        solutes.kMm = k_mm;
        solutes.glucoseMm = glucose_mm;
        handle->organism.infuseFluid(volume_ml, solutes);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_bleed(
    tatarus_organism* handle,
    double volume_ml) {
    if (!handle) return failMessage("null pointer passed to tatarus_organism_bleed");
    try {
        handle->organism.hemorrhage(volume_ml);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_set_renal_function(
    tatarus_organism* handle,
    double left_fraction,
    double right_fraction) {
    if (!handle) return failMessage("null pointer passed to tatarus_organism_set_renal_function");
    try {
        handle->organism.setRenalFunction(left_fraction, right_fraction);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_get_motor(
    tatarus_organism* handle,
    tatarus_motor_state* state) {
    if (!handle || !state) return failMessage("null pointer passed to tatarus_organism_get_motor");
    try {
        const auto motor = handle->organism.mind().motor();
        state->available = motor.available ? 1 : 0;
        state->selected_direction = motor.selectedDirection;
        for (std::size_t i = 0; i < motor.directionalActivity.size(); ++i) {
            state->directional_activity[i] = motor.directionalActivity[i];
        }
        state->movement = motor.movement;
        state->attention = motor.attention;
        state->vocalization = motor.vocalization;
        state->confidence = motor.confidence;
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_begin_action(
    tatarus_organism* handle,
    uint64_t action_id,
    double intensity) {
    if (!handle || action_id == 0) return failMessage("invalid tatarus_organism_begin_action arguments");
    try {
        handle->organism.mind().beginAction(tatarus::ActionEvent{
            .id = action_id,
            .label = "motor-" + std::to_string(action_id),
            .intensity = intensity,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_end_action(
    tatarus_organism* handle,
    uint64_t action_id,
    double reward,
    double success,
    double novelty) {
    if (!handle || action_id == 0) return failMessage("invalid tatarus_organism_end_action arguments");
    try {
        handle->organism.mind().endAction(tatarus::ActionOutcome{
            .id = action_id,
            .reward = reward,
            .success = success,
            .novelty = novelty,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_end_episode(
    tatarus_organism* handle,
    int32_t reached_goal) {
    if (!handle) return failMessage("invalid tatarus_organism_end_episode arguments");
    try {
        handle->organism.mind().endEpisode(reached_goal != 0);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_set_learning_enabled(
    tatarus_organism* handle,
    int32_t enabled) {
    if (!handle) return failMessage("invalid tatarus_organism_set_learning_enabled arguments");
    try {
        handle->organism.mind().setLearningEnabled(enabled != 0);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_set_navigation_context(
    tatarus_organism* handle,
    uint64_t context_id) {
    if (!handle) return failMessage("invalid tatarus_organism_set_navigation_context arguments");
    handle->navigationContext = context_id;
    g_lastError.clear();
    return 1;
}

int tatarus_organism_set_intervention(
    tatarus_organism* handle,
    const tatarus_intervention* intervention) {
    if (!handle || !intervention) return failMessage("null pointer passed to tatarus_organism_set_intervention");
    try {
        handle->organism.setExperimentalIntervention(tatarus::ExperimentalIntervention{
            .astrocyteFunction = intervention->astrocyte_function,
            .oxygenSupply = intervention->oxygen_supply,
            .glucoseSupply = intervention->glucose_supply,
            .pumpEfficiency = intervention->pump_efficiency,
            .myelinIntegrity = intervention->myelin_integrity,
            .microgliaFunction = intervention->microglia_function,
            .neuromodulatorGain = intervention->neuromodulator_gain,
            .sleepEnabled = intervention->sleep_enabled != 0,
        });
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_apply_damage(
    tatarus_organism* handle,
    double neuron_fraction,
    double synapse_fraction,
    uint64_t seed) {
    if (!handle) return failMessage("null pointer passed to tatarus_organism_apply_damage");
    try {
        handle->organism.applyExperimentalDamage(neuron_fraction, synapse_fraction, seed);
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

uint64_t tatarus_organism_get_spatial_json(
    tatarus_organism* handle, char* buffer, uint64_t capacity) {
    if (!handle) { failMessage("null pointer passed to tatarus_organism_get_spatial_json"); return 0; }
    try { return copyJson(handle->organism.mind().spatialJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

uint64_t tatarus_organism_get_live_json(
    tatarus_organism* handle, char* buffer, uint64_t capacity) {
    if (!handle) { failMessage("null pointer passed to tatarus_organism_get_live_json"); return 0; }
    try { return copyJson(handle->organism.mind().liveJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

uint64_t tatarus_organism_get_physiology_json(
    tatarus_organism* handle, char* buffer, uint64_t capacity) {
    if (!handle) { failMessage("null pointer passed to tatarus_organism_get_physiology_json"); return 0; }
    try { return copyJson(handle->organism.mind().physiologyJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

uint64_t tatarus_organism_get_throughput_json(
    tatarus_organism* handle, char* buffer, uint64_t capacity) {
    if (!handle) { failMessage("null pointer passed to tatarus_organism_get_throughput_json"); return 0; }
    try { return copyJson(handle->organism.throughputJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

uint64_t tatarus_organism_get_environment_map_json(
    tatarus_organism* handle,
    uint64_t environment_id,
    char* buffer,
    uint64_t capacity) {
    if (!handle || environment_id == 0) {
        failMessage("invalid arguments passed to tatarus_organism_get_environment_map_json");
        return 0;
    }
    try {
        return copyJson(
            handle->organism.mind().environmentMapJson(environment_id),
            buffer,
            capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_organism_clear_environment_map(
    tatarus_organism* handle,
    uint64_t environment_id) {
    if (!handle || environment_id == 0) {
        return failMessage("invalid arguments passed to tatarus_organism_clear_environment_map");
    }
    handle->organism.mind().clearEnvironmentMap(environment_id);
    g_lastError.clear();
    return 1;
}

int tatarus_organism_reset_throughput(tatarus_organism* handle) {
    if (!handle) return failMessage("null pointer passed to tatarus_organism_reset_throughput");
    try {
        handle->organism.resetThroughputCounters();
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn(
    tatarus_organism* handle,
    const double* pixels,
    uint64_t pixel_count,
    const char* label) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO learn");
    try {
        const auto reference = canvasFromPixels(pixels, pixel_count);
        const auto program = tatarus::VisualImagination::makeTeacherTrace(
            reference, 0.01, handle->imaginationConfig.paintPatchSide);
        const auto report = handle->imaginatio.learnToTrace(
            reference, program, label ? label : "");
        if (!report.success) return failMessage("IMAGINATIO trace lesson did not converge");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn_rgb(
    tatarus_organism* handle,
    const double* rgb_pixels,
    uint64_t channel_count,
    const char* label) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO RGB learn");
    try {
        const auto reference = canvasFromRgbPixels(rgb_pixels, channel_count);
        const auto program = tatarus::VisualImagination::makeTeacherTrace(
            reference, 0.0, handle->imaginationConfig.paintPatchSide);
        const auto report = handle->imaginatio.learnToTrace(
            reference, program, label ? label : "");
        if (!report.success) return failMessage("IMAGINATIO RGB trace lesson did not converge");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO RGB8 learn");
    if (patch_side == 0U || patch_side > 8U) {
        return failMessage("IMAGINATIO RGB8 patch_side must be in [1,8]");
    }
    try {
        const auto reference = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        const auto program = tatarus::VisualImagination::makeTeacherTrace(
            reference, 0.0, static_cast<std::size_t>(patch_side));
        const auto report = handle->imaginatio.learnToTrace(
            reference, program, label ? label : "");
        if (!report.success) return failMessage("IMAGINATIO RGB8 trace lesson did not converge");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn_category_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category) {
    if (!handle || !category || category[0] == '\0') {
        return failMessage("invalid organism or category passed to IMAGINATIO category learn");
    }
    if (patch_side == 0U || patch_side > 8U) {
        return failMessage("IMAGINATIO category patch_side must be in [1,8]");
    }
    try {
        const auto reference = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        const auto program = tatarus::VisualImagination::makeTeacherTrace(
            reference, 0.0, static_cast<std::size_t>(patch_side));
        const auto report = handle->imaginatio.learnToTrace(
            reference, program, label ? label : "");
        if (!report.success) return failMessage("IMAGINATIO category example did not converge");
        if (!handle->imaginatio.addCategoryExample(category, reference)) {
            return failMessage("IMAGINATIO category example was not bound to its concrete engram");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_ingest_category_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category) {
    if (!handle || !category || category[0] == '\0') {
        return failMessage("invalid organism or category passed to IMAGINATIO corpus ingestion");
    }
    if (patch_side == 0U || patch_side > 8U) {
        return failMessage("IMAGINATIO corpus patch_side must be in [1,8]");
    }
    try {
        const auto reference = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        if (!handle->imaginatio.ingestCategoryExample(
                reference, label ? label : "", category,
                static_cast<std::size_t>(patch_side))) {
            return failMessage("IMAGINATIO corpus example could not be retained");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

uint64_t tatarus_organism_imaginatio_recognize_categories_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* expected_categories,
    uint64_t maximum_matches,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null organism passed to IMAGINATIO visual recognition");
        return 0;
    }
    try {
        const auto cue = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        const auto report = handle->imaginatio.recognizeCategories(
            cue,
            expected_categories
                ? commaSeparatedSymbols(expected_categories)
                : std::vector<std::string>{},
            static_cast<std::size_t>(maximum_matches));
        std::ostringstream out;
        out << std::fixed << std::setprecision(6)
            << "{\"schema\":\"tatarus-visual-recognition-v1\""
            << ",\"success\":" << (report.success ? "true" : "false")
            << ",\"recognized\":" << (report.recognized ? "true" : "false")
            << ",\"dominant_ventral_area\":\""
            << jsonEscape(report.dominantVentralArea) << "\""
            << ",\"matches\":[";
        for (std::size_t index = 0; index < report.matches.size(); ++index) {
            if (index) out << ',';
            const auto& match = report.matches[index];
            out << "{\"category\":\"" << jsonEscape(match.category) << "\""
                << ",\"concept_similarity\":" << match.conceptSimilarity
                << ",\"episodic_similarity\":" << match.episodicSimilarity
                << ",\"bottom_up_similarity\":" << match.bottomUpSimilarity
                << ",\"top_down_boost\":" << match.topDownBoost
                << ",\"score\":" << match.score
                << ",\"confidence\":" << match.confidence << '}';
        }
        out << "]}";
        g_lastError.clear();
        return copyJson(out.str(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

uint64_t tatarus_organism_imaginatio_perceive_scene_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* expected_categories,
    int learn_objects,
    uint64_t maximum_objects,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null organism passed to IMAGINATIO scene perception");
        return 0;
    }
    try {
        const auto cue = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        const auto report = handle->imaginatio.perceiveScene(
            cue,
            expected_categories
                ? commaSeparatedSymbols(expected_categories)
                : std::vector<std::string>{},
            learn_objects != 0,
            static_cast<std::size_t>(maximum_objects));
        std::ostringstream out;
        out << std::fixed << std::setprecision(6)
            << "{\"schema\":\"tatarus-object-cortex-v1\""
            << ",\"success\":" << (report.success ? "true" : "false")
            << ",\"candidate_count\":" << report.candidateCount
            << ",\"recognized_count\":" << report.recognizedCount
            << ",\"novel_count\":" << report.novelCount
            << ",\"objects\":[";
        for (std::size_t index = 0; index < report.objects.size(); ++index) {
            if (index) out << ',';
            const auto& object = report.objects[index];
            out << "{\"object_engram_id\":" << object.objectEngramId
                << ",\"instance\":\"" << jsonEscape(object.instance) << "\""
                << ",\"category\":\"" << jsonEscape(object.category) << "\""
                << ",\"recognized_category\":"
                << (object.recognizedCategory ? "true" : "false")
                << ",\"novel_object\":" << (object.novelObject ? "true" : "false")
                << ",\"bounds\":[" << object.bounds[0] << ',' << object.bounds[1]
                << ',' << object.bounds[2] << ',' << object.bounds[3] << ']'
                << ",\"center\":[" << object.center[0] << ',' << object.center[1] << ']'
                << ",\"area_fraction\":" << object.areaFraction
                << ",\"saliency\":" << object.saliency
                << ",\"object_memory_similarity\":" << object.objectMemorySimilarity
                << ",\"dominant_ventral_area\":\""
                << jsonEscape(object.dominantVentralArea) << "\""
                << ",\"matches\":[";
            for (std::size_t matchIndex = 0; matchIndex < object.categoryMatches.size(); ++matchIndex) {
                if (matchIndex) out << ',';
                const auto& match = object.categoryMatches[matchIndex];
                out << "{\"category\":\"" << jsonEscape(match.category) << "\""
                    << ",\"bottom_up_similarity\":" << match.bottomUpSimilarity
                    << ",\"top_down_boost\":" << match.topDownBoost
                    << ",\"score\":" << match.score
                    << ",\"confidence\":" << match.confidence << '}';
            }
            out << "]}";
        }
        out << "],\"relations\":[";
        for (std::size_t index = 0; index < report.relations.size(); ++index) {
            if (index) out << ',';
            const auto& relation = report.relations[index];
            out << "{\"subject\":\"" << jsonEscape(relation.subject) << "\""
                << ",\"relation\":" << static_cast<unsigned int>(relation.relation)
                << ",\"object\":\"" << jsonEscape(relation.object) << "\""
                << ",\"confidence\":" << relation.confidence << '}';
        }
        out << "]}";
        g_lastError.clear();
        return copyJson(out.str(), buffer, capacity);
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_organism_imaginatio_recall(
    tatarus_organism* handle,
    const double* cue_pixels,
    uint64_t pixel_count) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO recall");
    try {
        const auto report = handle->imaginatio.drawFromMemory(
            canvasFromPixels(cue_pixels, pixel_count));
        if (!report.success) return failMessage("no matching visual engram could be recalled");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_recall_rgb(
    tatarus_organism* handle,
    const double* cue_rgb_pixels,
    uint64_t channel_count) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO RGB recall");
    try {
        const auto report = handle->imaginatio.drawFromMemory(
            canvasFromRgbPixels(cue_rgb_pixels, channel_count));
        if (!report.success) return failMessage("no matching RGB visual engram could be recalled");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_recall_rgb8(
    tatarus_organism* handle,
    const uint8_t* cue_rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO RGB8 recall");
    try {
        const auto report = handle->imaginatio.drawFromMemory(
            canvasFromRgb8(cue_rgb_pixels, channel_count, width, height));
        if (!report.success) return failMessage("no matching RGB8 visual engram could be recalled");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_associate_symbol(
    tatarus_organism* handle,
    const double* cue_pixels,
    uint64_t pixel_count,
    const char* symbol) {
    if (!handle || !symbol) {
        return failMessage("invalid arguments passed to IMAGINATIO symbol association");
    }
    try {
        const bool associated = handle->imaginatio.associateSymbol(
            symbol, canvasFromPixels(cue_pixels, pixel_count));
        if (!associated) return failMessage("symbol has no sufficiently similar visual engram");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_associate_symbol_rgb(
    tatarus_organism* handle,
    const double* cue_rgb_pixels,
    uint64_t channel_count,
    const char* symbol) {
    if (!handle || !symbol) {
        return failMessage("invalid arguments passed to IMAGINATIO RGB symbol association");
    }
    try {
        const bool associated = handle->imaginatio.associateSymbol(
            symbol, canvasFromRgbPixels(cue_rgb_pixels, channel_count));
        if (!associated) return failMessage("symbol has no sufficiently similar RGB visual engram");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_associate_symbol_rgb8(
    tatarus_organism* handle,
    const uint8_t* cue_rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* symbol) {
    if (!handle || !symbol) {
        return failMessage("invalid arguments passed to IMAGINATIO RGB8 symbol association");
    }
    try {
        const bool associated = handle->imaginatio.associateSymbol(
            symbol, canvasFromRgb8(cue_rgb_pixels, channel_count, width, height));
        if (!associated) return failMessage("symbol has no sufficiently similar RGB8 visual engram");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_draw_symbol(
    tatarus_organism* handle,
    const char* symbol) {
    if (!handle || !symbol) {
        return failMessage("invalid arguments passed to IMAGINATIO symbol recall");
    }
    try {
        const auto report = handle->imaginatio.drawFromSymbol(symbol);
        if (!report.success) return failMessage("symbol is not associated with a visual engram");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_draw_free(
    tatarus_organism* handle,
    const char* optional_label) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO free recall");
    try {
        const auto report = handle->imaginatio.drawFreely(
            optional_label ? optional_label : "");
        if (!report.success) return failMessage("no visual engram is available for free recall");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_draw_category(
    tatarus_organism* handle,
    const char* category,
    uint64_t variation_seed) {
    if (!handle || !category) {
        return failMessage("invalid arguments passed to IMAGINATIO category recall");
    }
    try {
        const auto report = handle->imaginatio.drawFromCategory(category, variation_seed);
        if (!report.success) {
            return failMessage("category requires at least two learned examples");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_fuse_categories(
    tatarus_organism* handle,
    const char* categories,
    uint64_t variation_seed) {
    if (!handle || !categories) {
        return failMessage("invalid arguments passed to IMAGINATIO category fusion");
    }
    try {
        const auto report = handle->imaginatio.fuseCategories(
            commaSeparatedSymbols(categories), variation_seed);
        if (!report.success) {
            return failMessage("every fused category requires at least two learned examples");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_compose(
    tatarus_organism* handle,
    const char* symbols) {
    if (!handle || !symbols) {
        return failMessage("invalid arguments passed to IMAGINATIO composition");
    }
    try {
        const auto report = handle->imaginatio.compose(commaSeparatedSymbols(symbols));
        if (!report.success) return failMessage("composition references an unknown symbol");
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn_pose_rgb8(
    tatarus_organism* handle,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category,
    const char* pose) {
    if (!handle || !category || !pose || category[0] == '\0' || pose[0] == '\0') {
        return failMessage("invalid arguments passed to IMAGINATIO pose learning");
    }
    if (patch_side == 0U || patch_side > 8U) {
        return failMessage("IMAGINATIO pose patch_side must be in [1,8]");
    }
    try {
        const auto reference = canvasFromRgb8(rgb_pixels, channel_count, width, height);
        const auto program = tatarus::VisualImagination::makeTeacherTrace(
            reference, 0.0, static_cast<std::size_t>(patch_side));
        const auto learned = handle->imaginatio.learnToTrace(
            reference, program, label ? label : "");
        if (!learned.success || !handle->imaginatio.addPoseExample(category, pose, reference)) {
            return failMessage("IMAGINATIO pose could not be bound to its category");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_observe_scene(
    tatarus_organism* handle,
    const tatarus_scene_description* scene) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO scene observation");
    try {
        if (!handle->imaginatio.observeScene(sceneFromC(scene))) {
            return failMessage("IMAGINATIO scene observation failed");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_draw_scene(
    tatarus_organism* handle,
    const tatarus_scene_description* scene,
    uint64_t variation_seed) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO scene drawing");
    try {
        const auto report = handle->imaginatio.imagineScene(
            sceneFromC(scene), variation_seed);
        if (!report.success) {
            return failMessage("IMAGINATIO relational scene could not be drawn");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_learn_transition(
    tatarus_organism* handle,
    const tatarus_scene_description* before,
    const tatarus_scene_action* action,
    const tatarus_scene_description* after) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO transition learning");
    try {
        if (!handle->imaginatio.learnSceneTransition(
                sceneFromC(before), sceneActionFromC(action), sceneFromC(after))) {
            return failMessage("IMAGINATIO scene transition learning failed");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_imagine_future(
    tatarus_organism* handle,
    const tatarus_scene_description* initial,
    const tatarus_scene_action* rawActions,
    uint64_t action_count,
    uint64_t variation_seed) {
    if (!handle || !rawActions || action_count == 0U || action_count > 256U) {
        return failMessage("invalid arguments passed to IMAGINATIO prospective imagination");
    }
    try {
        std::vector<tatarus::SceneAction> actions;
        actions.reserve(static_cast<std::size_t>(action_count));
        for (std::uint64_t index = 0; index < action_count; ++index) {
            actions.push_back(sceneActionFromC(&rawActions[index]));
        }
        const auto report = handle->imaginatio.imagineFuture(
            sceneFromC(initial), actions, variation_seed);
        if (!report.success) {
            return failMessage("IMAGINATIO has no learned effect for every prospective action");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_imaginatio_reset_canvas(tatarus_organism* handle) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO reset");
    handle->imaginatio.resetCanvas();
    g_lastError.clear();
    return 1;
}

uint64_t tatarus_organism_imaginatio_get_json(
    tatarus_organism* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null organism passed to IMAGINATIO JSON");
        return 0;
    }
    try { return copyJson(handle->imaginatio.stateJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

uint64_t tatarus_organism_imaginatio_render_native_rgb8(
    tatarus_organism* handle,
    uint64_t width,
    uint64_t height,
    uint8_t* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null organism passed to IMAGINATIO target-space painting");
        return 0;
    }
    try {
        if (width < 8U || height < 8U || width > 4096U || height > 4096U
            || width > 16'777'216ULL / height) {
            return failMessage("IMAGINATIO target-space dimensions exceed the supported 4K raster envelope");
        }
        const auto required = width * height * 3U;
        if (buffer == nullptr || capacity == 0U) {
            g_lastError.clear();
            return required;
        }
        const auto render = handle->imaginatio.renderLastTargetSpace(
            static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        if (capacity < required) {
            failMessage("IMAGINATIO target-space buffer is too small");
            return required;
        }
        std::memcpy(buffer, render.rgb8.data(), static_cast<std::size_t>(required));
        g_lastError.clear();
        return required;
    } catch (const std::exception& error) {
        fail(error);
        return 0;
    }
}

int tatarus_organism_imaginatio_remember_source_resolution(
    tatarus_organism* handle,
    uint64_t width,
    uint64_t height) {
    if (!handle) return failMessage("null organism passed to IMAGINATIO source resolution memory");
    try {
        handle->imaginatio.rememberSourceResolution(
            static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

uint64_t tatarus_organism_imaginatio_get_trace_json(
    tatarus_organism* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) {
        failMessage("null organism passed to IMAGINATIO trace JSON");
        return 0;
    }
    try { return copyJson(handle->imaginatio.traceJson(), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}

#ifdef TATARUS_HAS_CORTEX
int tatarus_organism_cortex_configure(
    tatarus_organism* handle,
    const char* config_path) {
    if (!handle || !config_path || config_path[0] == '\0') {
        return failMessage("Cortex configuration requires an organism and config path");
    }
    try {
        using namespace tatarus::cortex;
        CortexConfig config = loadCortexConfig(config_path);
        if (!config.enabled || config.mode == CortexMode::Disabled) {
            return failMessage("Cortex configuration is disabled");
        }
        std::shared_ptr<LmStudioClient> model;
        if (config.executionMode == CortexExecutionMode::Live
            || config.executionMode == CortexExecutionMode::Record) {
            model = std::make_shared<LmStudioClient>(config.provider, config.limits);
        }
        auto cortex = model
            ? std::make_unique<CortexOrchestrator>(config, model)
            : std::make_unique<CortexOrchestrator>(config);
        handle->cortexConfig = config;
        handle->cortexModel = std::move(model);
        handle->cortex = std::move(cortex);
        handle->cortexConfigPath = std::filesystem::path(config_path);
        handle->cortexLmConnected = false;
        handle->cortexModelName.clear();
        handle->cortexModels.clear();
        handle->cortexProbeError.clear();
        handle->cortexLastExecutedRequestId = 0;
        handle->cortexLastExecution = "Cortex configured; LM Studio not probed yet";
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        return fail(error);
    }
}

int tatarus_organism_cortex_disable(tatarus_organism* handle) {
    if (!handle) return failMessage("null organism passed to Cortex disable");
    handle->cortex.reset();
    handle->cortexModel.reset();
    handle->cortexLmConnected = false;
    handle->cortexModelName.clear();
    handle->cortexModels.clear();
    handle->cortexProbeError.clear();
    handle->cortexLastExecutedRequestId = 0;
    handle->cortexLastExecution = "Cortex disabled";
    g_lastError.clear();
    return 1;
}

int tatarus_organism_cortex_probe(tatarus_organism* handle) {
    if (!handle || !handle->cortex) return failMessage("Cortex is not configured");
    try {
        if (!handle->cortexModel) {
            handle->cortexLmConnected = handle->cortexConfig.executionMode == tatarus::cortex::CortexExecutionMode::Replay;
            handle->cortexModelName = handle->cortexLmConnected ? "REPLAY" : "";
            handle->cortexModels = handle->cortexLmConnected ? std::vector<std::string>{"REPLAY"} : std::vector<std::string>{};
            handle->cortexProbeError = handle->cortexLmConnected ? "" : "No live LM client for this execution mode";
            if (!handle->cortexLmConnected) return failMessage(handle->cortexProbeError.c_str());
            g_lastError.clear();
            return 1;
        }
        handle->cortexModels = handle->cortexModel->listModels();
        handle->cortexModelName = handle->cortexModel->resolvedModel();
        handle->cortexLmConnected = true;
        handle->cortexProbeError.clear();
        handle->cortexLastExecution = "LM Studio connection verified";
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) {
        handle->cortexLmConnected = false;
        handle->cortexModelName.clear();
        handle->cortexModels.clear();
        handle->cortexProbeError = error.what();
        handle->cortexLastExecution = "LM Studio probe failed";
        return fail(error);
    }
}

uint64_t tatarus_organism_cortex_push_goal(
    tatarus_organism* handle,
    const char* goal,
    double priority) {
    if (!handle || !handle->cortex || !goal || goal[0] == '\0') {
        failMessage("Cortex goal requires configured Cortex and non-empty text");
        return 0;
    }
    if (!std::isfinite(priority) || priority < 0.0 || priority > 1.0) {
        failMessage("Cortex goal priority must be in [0,1]");
        return 0;
    }
    try {
        const auto id = handle->cortex->pushGoal(goal, priority, cortexCurrentStep(*handle));
        handle->cortexLastExecution = "Executive goal added";
        g_lastError.clear();
        return id;
    } catch (const std::exception& error) { fail(error); return 0; }
}

int tatarus_organism_cortex_complete_goal(
    tatarus_organism* handle,
    uint64_t goal_id,
    int32_t success) {
    if (!handle || !handle->cortex || goal_id == 0U) {
        return failMessage("Cortex complete-goal requires configured Cortex and goal id");
    }
    try {
        if (!handle->cortex->completeGoal(goal_id, success != 0, cortexCurrentStep(*handle))) {
            return failMessage("Cortex goal id not found or already terminal");
        }
        handle->cortexLastExecution = success ? "Executive goal completed" : "Executive goal marked failed";
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_cortex_cancel_goal(
    tatarus_organism* handle,
    uint64_t goal_id) {
    if (!handle || !handle->cortex || goal_id == 0U) {
        return failMessage("Cortex cancel-goal requires configured Cortex and goal id");
    }
    try {
        if (!handle->cortex->cancelGoal(goal_id, cortexCurrentStep(*handle))) {
            return failMessage("Cortex goal id not found or already terminal");
        }
        handle->cortexLastExecution = "Executive goal cancelled";
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_cortex_remember(
    tatarus_organism* handle,
    const char* key,
    const char* value,
    double salience) {
    if (!handle || !handle->cortex || !key || key[0] == '\0' || !value) {
        return failMessage("Cortex working memory requires configured Cortex, key and value");
    }
    if (!std::isfinite(salience) || salience < 0.0 || salience > 1.0) {
        return failMessage("Cortex working-memory salience must be in [0,1]");
    }
    try {
        handle->cortex->remember(key, value, salience, cortexCurrentStep(*handle));
        handle->cortexLastExecution = "Working-memory item stored";
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_cortex_command(
    tatarus_organism* handle,
    uint32_t command,
    const char* goal) {
    if (!handle || !handle->cortex) return failMessage("Cortex is not configured");
    try {
        using namespace tatarus::cortex;
        const auto telemetry = handle->organism.telemetry();
        const auto imagination = handle->cortexImaginatioAdapter.imaginationState(handle->imaginatio);
        const auto capabilities = cortexImaginationCapabilities();
        const std::string goalText = goal ? goal : "";
        CortexSpatialState noSpatial{};
        if ((command == TATARUS_CORTEX_ANALYZE
                || command == TATARUS_CORTEX_IMAGINE
                || command == TATARUS_CORTEX_HYBRID
                || command == TATARUS_CORTEX_EXECUTIVE_PLAN)
            && !handle->cortex->idle()) {
            return failMessage("Cortex is busy processing the previous request");
        }
        switch (command) {
            case TATARUS_CORTEX_ANALYZE:
                handle->cortexPendingUsesImagination = false;
                handle->cortex->requestExplicitAnalysis(handle->organism.mind(), &telemetry, goalText);
                handle->cortexLastExecution = "Explicit Cortex analysis submitted";
                break;
            case TATARUS_CORTEX_IMAGINE:
                handle->cortexPendingUsesImagination = true;
                handle->cortex->requestExplicitImagination(
                    handle->organism.mind(), &telemetry, imagination, capabilities, goalText);
                handle->cortexLastExecution = "Explicit imagination request submitted";
                break;
            case TATARUS_CORTEX_HYBRID:
                handle->cortexPendingUsesImagination = true;
                handle->cortex->requestExplicitHybrid(
                    handle->organism.mind(), &telemetry, noSpatial, imagination, capabilities, goalText);
                handle->cortexLastExecution = "Explicit hybrid request submitted";
                break;
            case TATARUS_CORTEX_EXECUTIVE_PLAN:
                handle->cortexPendingUsesImagination = true;
                handle->cortex->requestExecutivePlan(
                    handle->organism.mind(), &telemetry, nullptr, &imagination, capabilities);
                handle->cortexLastExecution = "Executive plan request submitted";
                break;
            case TATARUS_CORTEX_AUTONOMOUS_CYCLE:
                handle->cortexPendingUsesImagination = true;
                handle->cortex->autonomousCycle(
                    handle->organism.mind(), &telemetry, nullptr, &imagination, capabilities);
                handle->cortexLastExecution = "Autonomous executive cycle scheduled";
                break;
            case TATARUS_CORTEX_POLL:
                handle->cortex->poll(
                    handle->organism.mind(), &telemetry, nullptr,
                    handle->cortexPendingUsesImagination ? &imagination : nullptr);
                break;
            case TATARUS_CORTEX_EXECUTE_IMAGINATION: {
                handle->cortex->poll(
                    handle->organism.mind(), &telemetry, nullptr,
                    handle->cortexPendingUsesImagination ? &imagination : nullptr);
                const auto status = handle->cortex->status();
                if (!status.lastRequest || !status.lastResponse || !status.lastDecision) {
                    return failMessage("No completed Cortex imagination decision is available");
                }
                if (status.lastRequest->requestId == handle->cortexLastExecutedRequestId) {
                    return failMessage("The current Cortex imagination decision was already executed");
                }
                const auto directive = handle->cortexImaginatioAdapter.translate(
                    *status.lastDecision, *status.lastResponse, handle->imaginatio,
                    status.lastRequest->goal);
                if (!directive.executable) {
                    handle->cortexLastExecution = "Imagination directive rejected: " + directive.reason;
                    return failMessage(handle->cortexLastExecution.c_str());
                }
                const auto execution = handle->cortexImaginatioAdapter.execute(directive, handle->imaginatio);
                if (!execution.executed) {
                    handle->cortexLastExecution = execution.error.empty()
                        ? "IMAGINATIO execution failed" : execution.error;
                    return failMessage(handle->cortexLastExecution.c_str());
                }
                handle->cortexLastExecutedRequestId = status.lastRequest->requestId;
                handle->cortexLastExecution = std::string("IMAGINATIO executed: ")
                    + toString(directive.kind);
                break;
            }
            default:
                return failMessage("Unknown Cortex C-ABI command");
        }
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

uint64_t tatarus_organism_cortex_get_json(
    tatarus_organism* handle,
    char* buffer,
    uint64_t capacity) {
    if (!handle) { failMessage("null organism passed to Cortex JSON"); return 0; }
    try { return copyJson(cortexStatusJson(*handle), buffer, capacity); }
    catch (const std::exception& error) { fail(error); return 0; }
}
#endif

int tatarus_organism_save(tatarus_organism* handle, const char* directory) {
    if (!handle || !directory) return failMessage("null pointer passed to tatarus_organism_save");
    try {
        handle->organism.saveSnapshot(directory);
        handle->imaginatio.saveSnapshot(
            std::filesystem::path(directory) / "imaginatio");
#ifdef TATARUS_HAS_CORTEX
        if (handle->cortex) {
            const auto cortexDirectory = std::filesystem::path(directory) / "cortex";
            std::filesystem::create_directories(cortexDirectory);
            handle->cortex->saveExecutiveState(cortexDirectory / "executive.bin");
            handle->cortex->saveLearningState(cortexDirectory / "learning.bin");
        }
#endif
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

int tatarus_organism_load(tatarus_organism* handle, const char* directory) {
    if (!handle || !directory) return failMessage("null pointer passed to tatarus_organism_load");
    try {
        if (!handle->organism.loadSnapshot(directory)) {
            return failMessage("failed to load complete organism snapshot");
        }
        const auto imaginationDirectory = std::filesystem::path(directory) / "imaginatio";
        if (std::filesystem::is_directory(imaginationDirectory)) {
            if (!handle->imaginatio.loadSnapshot(imaginationDirectory)) {
                return failMessage("failed to load IMAGINATIO state from organism snapshot");
            }
        } else {
            handle->imaginatio = tatarus::VisualImagination(
                handle->organism.mind(), handle->imaginationConfig);
        }
#ifdef TATARUS_HAS_CORTEX
        if (handle->cortex) {
            const auto cortexDirectory = std::filesystem::path(directory) / "cortex";
            const auto executivePath = cortexDirectory / "executive.bin";
            const auto learningPath = cortexDirectory / "learning.bin";
            if (std::filesystem::exists(executivePath)) {
                handle->cortex->loadExecutiveState(executivePath);
            }
            if (std::filesystem::exists(learningPath)) {
                handle->cortex->loadLearningState(learningPath);
            }
            handle->cortexLastExecutedRequestId = 0;
            handle->cortexLastExecution = "Cortex persistent state restored with organism snapshot";
        }
#endif
        g_lastError.clear();
        return 1;
    } catch (const std::exception& error) { return fail(error); }
}

} // extern "C"
