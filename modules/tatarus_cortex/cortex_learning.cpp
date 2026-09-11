#include "tatarus/cortex_learning.hpp"

#include "cortex_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace tatarus::cortex {
namespace {

[[nodiscard]] std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

[[nodiscard]] std::uint64_t quantize01(double value, std::uint64_t bins = 8U) noexcept {
    if (!std::isfinite(value)) return 0U;
    const double clamped = std::clamp(value, 0.0, 1.0);
    return static_cast<std::uint64_t>(std::llround(clamped * static_cast<double>(bins - 1U)));
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T>
void readPod(std::istream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("truncated Cortex learning state");
}

struct CompetenceState {
    LearnedCortexCompetence publicState;
    std::uint64_t cortexTrials = 0;
};

[[nodiscard]] double ratio(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    return denominator == 0U ? 0.0
        : static_cast<double>(numerator) / static_cast<double>(denominator);
}

void refresh(CompetenceState& state, const CortexLearningConfig& config) {
    auto& out = state.publicState;
    out.cortexSuccessRate = ratio(out.cortexSuccesses, state.cortexTrials);
    out.autonomousSuccessRate = ratio(out.autonomousSuccesses, out.autonomousTrials);
    const auto total = state.cortexTrials + out.autonomousTrials;
    out.dependency = total == 0U ? 1.0
        : static_cast<double>(state.cortexTrials) / static_cast<double>(total);
    out.cortexOptional = out.autonomousTrials >= config.minimumAutonomousTrials
        && out.autonomousSuccessRate >= config.autonomousSuccessThreshold;
}

} // namespace

std::uint64_t cortexContextClass(const CortexRequest& request) noexcept {
    // Competence classes intentionally ignore short-lived motor confidence and
    // instantaneous prediction error. Those signals trigger consultation, but
    // should not split the same environmental task into a new competence class
    // merely because a restored/revisited state has a different transient readout.
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix(hash, static_cast<std::uint64_t>(request.task));
    hash = mix(hash, request.spatial.environmentId);
    hash = mix(hash, static_cast<std::uint64_t>(request.spatial.mapAvailable));
    hash = mix(hash, static_cast<std::uint64_t>(request.spatial.knownRouteAvailable));
    hash = mix(hash, static_cast<std::uint64_t>(request.spatial.loopDetected));
    hash = mix(hash, quantize01(request.neural.novelty));
    hash = mix(hash, quantize01(request.neural.sequenceFamiliarity));
    hash = mix(hash, quantize01(request.spatial.frontierRatio));
    hash = mix(hash, quantize01(request.spatial.knownRouteConfidence));
    hash = mix(hash, quantize01(request.physiology.visceralDistress));
    hash = mix(hash, quantize01(request.physiology.atp));
    return hash == 0U ? 1U : hash;
}

const char* toString(CortexConsultationDecision value) noexcept {
    switch (value) {
        case CortexConsultationDecision::Consult: return "CONSULT";
        case CortexConsultationDecision::SuppressForAutonomyProbe: return "SUPPRESS_FOR_AUTONOMY_PROBE";
    }
    return "CONSULT";
}

class CortexTeacherTransfer::Impl {
public:
    explicit Impl(CortexLearningConfig value) : config(value) {
        if (!std::isfinite(config.autonomousSuccessThreshold)
            || config.autonomousSuccessThreshold < 0.0
            || config.autonomousSuccessThreshold > 1.0
            || !std::isfinite(config.outcomeSuccessThreshold)
            || config.outcomeSuccessThreshold < 0.0
            || config.outcomeSuccessThreshold > 1.0) {
            throw std::invalid_argument("Cortex learning thresholds must be in [0,1]");
        }
        if (config.minimumTeachingSuccesses == 0U || config.minimumAutonomousTrials == 0U) {
            throw std::invalid_argument("Cortex learning trial thresholds must be greater than zero");
        }
    }

    [[nodiscard]] CortexConsultationDecision consultationDecision(const CortexRequest& request) {
        std::scoped_lock lock(mutex);
        if (!config.enabled || !config.competenceGateEnabled) {
            return CortexConsultationDecision::Consult;
        }
        const auto key = cortexContextClass(request);
        auto& competence = competencies[key];
        competence.publicState.contextClass = key;
        refresh(competence, config);

        if (competence.publicState.autonomyProbeOutstanding) {
            return CortexConsultationDecision::Consult;
        }
        if (competence.publicState.cortexSuccesses < config.minimumTeachingSuccesses) {
            return CortexConsultationDecision::Consult;
        }

        const bool needsProbe = competence.publicState.autonomousTrials < config.minimumAutonomousTrials;
        const bool demonstratedAutonomy = competence.publicState.cortexOptional;
        if (needsProbe || demonstratedAutonomy) {
            competence.publicState.autonomyProbeOutstanding = true;
            ++snapshotData.consultationsSuppressed;
            return CortexConsultationDecision::SuppressForAutonomyProbe;
        }
        return CortexConsultationDecision::Consult;
    }

    [[nodiscard]] CortexTeachingHandle begin(
        const CortexRequest& request,
        const CortexDecision* decision,
        ActionId actionId,
        bool cortexUsed) {
        if (actionId == 0U) throw std::invalid_argument("teaching episode requires a real non-zero action id");
        std::scoped_lock lock(mutex);
        const auto context = cortexContextClass(request);
        auto& competence = competencies[context];
        competence.publicState.contextClass = context;

        CortexTeachingEpisode item;
        item.episodeId = nextEpisodeId++;
        item.contextClass = context;
        item.requestId = request.requestId;
        item.sourceStep = request.organismStep;
        item.sourceFingerprint = request.stateFingerprint;
        item.trigger = request.trigger;
        item.actionId = actionId;
        item.cortexUsed = cortexUsed;
        item.prePredictionConfidence = request.neural.predictionConfidence;
        item.preNovelty = request.neural.novelty;
        if (decision && decision->selectedStrategy.has_value()) {
            item.strategy = decision->selectedStrategy->kind;
            item.strategyId = decision->selectedStrategy->id;
        }
        episodes[item.episodeId] = item;
        ++snapshotData.episodesStarted;
        if (cortexUsed) {
            ++snapshotData.cortexAssistedEpisodes;
            ++competence.publicState.consultations;
            ++competence.cortexTrials;
        } else {
            ++snapshotData.autonomousEpisodes;
            competence.publicState.autonomyProbeOutstanding = true;
        }
        refresh(competence, config);
        return CortexTeachingHandle{item.episodeId, context, cortexUsed};
    }

    void complete(
        const CortexTeachingHandle& handle,
        const ActionOutcome& outcome,
        const RobotMind& postMind,
        const organism::OrganismTelemetry* postOrganism,
        const CortexSpatialState* postSpatial,
        const CortexImaginationState* postImagination) {
        if (!handle) throw std::invalid_argument("invalid Cortex teaching handle");

        const auto post = detail::buildCortexRequest(
            0U, CortexTaskKind::ObserveState, CortexTriggerReason::None,
            postMind, postOrganism, {}, {}, postSpatial, postImagination);

        std::scoped_lock lock(mutex);
        const auto it = episodes.find(handle.episodeId);
        if (it == episodes.end()) throw std::invalid_argument("unknown Cortex teaching episode");
        auto& item = it->second;
        if (item.completed) throw std::logic_error("Cortex teaching episode already completed");
        if (item.contextClass != handle.contextClass || item.cortexUsed != handle.cortexUsed) {
            throw std::logic_error("Cortex teaching handle provenance mismatch");
        }
        if (outcome.id != item.actionId) {
            throw std::invalid_argument("real ActionOutcome id does not match teaching action id");
        }

        item.completed = true;
        item.outcome = outcome;
        item.successful = outcome.success >= config.outcomeSuccessThreshold;
        item.postFingerprint = post.stateFingerprint;
        item.postPredictionConfidence = post.neural.predictionConfidence;
        item.postNovelty = post.neural.novelty;
        ++snapshotData.episodesCompleted;

        auto& competence = competencies[item.contextClass];
        if (item.cortexUsed) {
            if (item.successful) ++competence.publicState.cortexSuccesses;
        } else {
            ++competence.publicState.autonomousTrials;
            if (item.successful) ++competence.publicState.autonomousSuccesses;
            competence.publicState.autonomyProbeOutstanding = false;
        }
        refresh(competence, config);
    }

    [[nodiscard]] CortexLearningSnapshot snapshot() const {
        std::scoped_lock lock(mutex);
        CortexLearningSnapshot result = snapshotData;
        result.competencies.reserve(competencies.size());
        for (const auto& [_, state] : competencies) result.competencies.push_back(state.publicState);
        std::sort(result.competencies.begin(), result.competencies.end(), [](const auto& a, const auto& b) {
            return a.contextClass < b.contextClass;
        });
        return result;
    }

    [[nodiscard]] std::optional<CortexTeachingEpisode> episode(std::uint64_t id) const {
        std::scoped_lock lock(mutex);
        const auto it = episodes.find(id);
        if (it == episodes.end()) return std::nullopt;
        return it->second;
    }

    void save(const std::filesystem::path& path) const {
        std::scoped_lock lock(mutex);
        std::filesystem::create_directories(path.parent_path().empty()
            ? std::filesystem::current_path() : path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create Cortex learning state");
        constexpr std::array<char, 8> magic{'T','C','L','R','N','0','0','1'};
        output.write(magic.data(), magic.size());
        writePod(output, nextEpisodeId);
        writePod(output, snapshotData.episodesStarted);
        writePod(output, snapshotData.episodesCompleted);
        writePod(output, snapshotData.cortexAssistedEpisodes);
        writePod(output, snapshotData.autonomousEpisodes);
        writePod(output, snapshotData.consultationsSuppressed);

        const std::uint64_t competenceCount = competencies.size();
        writePod(output, competenceCount);
        for (const auto& [key, state] : competencies) {
            writePod(output, key);
            writePod(output, state.cortexTrials);
            writePod(output, state.publicState.consultations);
            writePod(output, state.publicState.cortexSuccesses);
            writePod(output, state.publicState.autonomousTrials);
            writePod(output, state.publicState.autonomousSuccesses);
            writePod(output, state.publicState.autonomyProbeOutstanding);
        }

        const std::uint64_t episodeCount = episodes.size();
        writePod(output, episodeCount);
        for (const auto& [id, item] : episodes) {
            writePod(output, id);
            writePod(output, item.contextClass);
            writePod(output, item.requestId);
            writePod(output, item.sourceStep);
            writePod(output, item.sourceFingerprint);
            writePod(output, item.postFingerprint);
            writePod(output, item.trigger);
            writePod(output, item.strategy);
            const std::uint64_t strategySize = item.strategyId.size();
            writePod(output, strategySize);
            output.write(item.strategyId.data(), static_cast<std::streamsize>(strategySize));
            writePod(output, item.actionId);
            writePod(output, item.cortexUsed);
            writePod(output, item.completed);
            writePod(output, item.successful);
            writePod(output, item.outcome);
            writePod(output, item.prePredictionConfidence);
            writePod(output, item.postPredictionConfidence);
            writePod(output, item.preNovelty);
            writePod(output, item.postNovelty);
        }
        if (!output) throw std::runtime_error("failed to write Cortex learning state");
    }

    void load(const std::filesystem::path& path) {
        std::scoped_lock lock(mutex);
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open Cortex learning state");
        std::array<char, 8> magic{};
        input.read(magic.data(), magic.size());
        constexpr std::array<char, 8> expected{'T','C','L','R','N','0','0','1'};
        if (!input || magic != expected) throw std::runtime_error("unknown Cortex learning state format");

        CortexLearningSnapshot loadedSnapshot;
        std::uint64_t loadedNext = 1;
        readPod(input, loadedNext);
        readPod(input, loadedSnapshot.episodesStarted);
        readPod(input, loadedSnapshot.episodesCompleted);
        readPod(input, loadedSnapshot.cortexAssistedEpisodes);
        readPod(input, loadedSnapshot.autonomousEpisodes);
        readPod(input, loadedSnapshot.consultationsSuppressed);

        std::unordered_map<std::uint64_t, CompetenceState> loadedCompetencies;
        std::uint64_t competenceCount = 0;
        readPod(input, competenceCount);
        if (competenceCount > 1'000'000ULL) throw std::runtime_error("implausible Cortex competence count");
        for (std::uint64_t i = 0; i < competenceCount; ++i) {
            std::uint64_t key = 0;
            CompetenceState state;
            readPod(input, key);
            state.publicState.contextClass = key;
            readPod(input, state.cortexTrials);
            readPod(input, state.publicState.consultations);
            readPod(input, state.publicState.cortexSuccesses);
            readPod(input, state.publicState.autonomousTrials);
            readPod(input, state.publicState.autonomousSuccesses);
            readPod(input, state.publicState.autonomyProbeOutstanding);
            refresh(state, config);
            loadedCompetencies[key] = state;
        }

        std::unordered_map<std::uint64_t, CortexTeachingEpisode> loadedEpisodes;
        std::uint64_t episodeCount = 0;
        readPod(input, episodeCount);
        if (episodeCount > 10'000'000ULL) throw std::runtime_error("implausible Cortex teaching episode count");
        for (std::uint64_t i = 0; i < episodeCount; ++i) {
            CortexTeachingEpisode item;
            readPod(input, item.episodeId);
            readPod(input, item.contextClass);
            readPod(input, item.requestId);
            readPod(input, item.sourceStep);
            readPod(input, item.sourceFingerprint);
            readPod(input, item.postFingerprint);
            readPod(input, item.trigger);
            readPod(input, item.strategy);
            std::uint64_t strategySize = 0;
            readPod(input, strategySize);
            if (strategySize > 4096U) throw std::runtime_error("implausible Cortex strategy id length");
            item.strategyId.resize(static_cast<std::size_t>(strategySize));
            input.read(item.strategyId.data(), static_cast<std::streamsize>(strategySize));
            if (!input) throw std::runtime_error("truncated Cortex strategy id");
            readPod(input, item.actionId);
            readPod(input, item.cortexUsed);
            readPod(input, item.completed);
            readPod(input, item.successful);
            readPod(input, item.outcome);
            readPod(input, item.prePredictionConfidence);
            readPod(input, item.postPredictionConfidence);
            readPod(input, item.preNovelty);
            readPod(input, item.postNovelty);
            loadedEpisodes[item.episodeId] = item;
        }

        nextEpisodeId = loadedNext;
        snapshotData = loadedSnapshot;
        competencies = std::move(loadedCompetencies);
        episodes = std::move(loadedEpisodes);
    }

    CortexLearningConfig config;
    mutable std::mutex mutex;
    std::uint64_t nextEpisodeId = 1;
    CortexLearningSnapshot snapshotData;
    std::unordered_map<std::uint64_t, CompetenceState> competencies;
    std::unordered_map<std::uint64_t, CortexTeachingEpisode> episodes;
};

CortexTeacherTransfer::CortexTeacherTransfer(CortexLearningConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

CortexTeacherTransfer::~CortexTeacherTransfer() = default;
CortexTeacherTransfer::CortexTeacherTransfer(CortexTeacherTransfer&&) noexcept = default;
CortexTeacherTransfer& CortexTeacherTransfer::operator=(CortexTeacherTransfer&&) noexcept = default;

CortexConsultationDecision CortexTeacherTransfer::consultationDecision(
    const CortexRequest& request) {
    return impl_->consultationDecision(request);
}

CortexTeachingHandle CortexTeacherTransfer::beginCortexAssisted(
    const CortexRequest& request,
    const CortexDecision& decision,
    ActionId actionId) {
    if (decision.kind != CortexDecisionKind::Accept || !decision.selectedStrategy.has_value()) {
        throw std::invalid_argument("cortex-assisted teaching requires an accepted real-action strategy");
    }
    return impl_->begin(request, &decision, actionId, true);
}

CortexTeachingHandle CortexTeacherTransfer::beginAutonomous(
    const CortexRequest& request,
    ActionId actionId) {
    return impl_->begin(request, nullptr, actionId, false);
}

void CortexTeacherTransfer::completeRealOutcome(
    const CortexTeachingHandle& handle,
    const ActionOutcome& outcome,
    const RobotMind& postMind,
    const organism::OrganismTelemetry* postOrganism,
    const CortexSpatialState* postSpatial,
    const CortexImaginationState* postImagination) {
    impl_->complete(handle, outcome, postMind, postOrganism, postSpatial, postImagination);
}

CortexLearningSnapshot CortexTeacherTransfer::snapshot() const {
    return impl_->snapshot();
}

std::optional<CortexTeachingEpisode> CortexTeacherTransfer::episode(
    std::uint64_t episodeId) const {
    return impl_->episode(episodeId);
}

void CortexTeacherTransfer::saveState(const std::filesystem::path& path) const {
    impl_->save(path);
}

void CortexTeacherTransfer::loadState(const std::filesystem::path& path) {
    impl_->load(path);
}

} // namespace tatarus::cortex
