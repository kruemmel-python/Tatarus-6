#include "tatarus/cortex.hpp"
#include "tatarus/imaginatio_cortex.hpp"
#include "tatarus/robot_mind.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace tatarus;
using namespace tatarus::cortex;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Fn>
void requireThrows(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

Experience frame(std::uint64_t step) {
    Experience experience;
    experience.timestampNs = step * 50'000'000ULL;
    experience.vision = {
        (step % 9U == 0U) ? 1.0 : 0.15,
        (step % 7U == 0U) ? 0.8 : 0.1,
        0.2,
        0.05,
    };
    experience.audio = {0.1, (step % 11U == 0U) ? 0.7 : 0.0};
    experience.touch = {0.0, 0.1, 0.95, 0.0};
    experience.spatialContext = {
        static_cast<double>(step % 5U) / 4.0,
        static_cast<double>(step % 3U) / 2.0,
    };
    experience.environment.light = 0.7;
    experience.environment.temperature = 22.0;
    experience.environment.novelty = (step % 13U == 0U) ? 0.9 : 0.05;
    experience.body.battery = 0.93;
    experience.body.powerDraw = 0.2;
    experience.reward = (step % 17U == 0U) ? 0.2 : 0.0;
    return experience;
}

class FakeModelClient final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "observe-only synthetic test response";
        return response;
    }
    std::uint64_t calls = 0;
};

class FakeRoverAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "request a bounded fresh scan before committing to motion";
        response.strategies.push_back(CortexStrategy{
            .id = "ROVER_FRESH_SCAN",
            .kind = CortexStrategyKind::RequestScan,
            .rationale = "Acquire fresh geometry without choosing a motor direction.",
            .estimatedRisk = 0.05,
            .estimatedBenefit = 0.80,
            .confidence = 0.90,
            .requiredCapabilities = {"SCAN"},
        });
        return response;
    }
    std::uint64_t calls = 0;
};

class DelayedAdvisor final : public ICortexModelClient {
public:
    explicit DelayedAdvisor(
        CortexStrategyKind strategyKind = CortexStrategyKind::Observe,
        std::string capabilityName = "STATE_ANALYSIS")
        : kind(strategyKind), capability(std::move(capabilityName)) {}

    CortexResponse complete(const CortexRequest& request) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "delayed bounded response";
        response.strategies.push_back(CortexStrategy{
            .id = "DELAYED_STRATEGY",
            .kind = kind,
            .rationale = "Exercise asynchronous relevance revalidation.",
            .estimatedRisk = 0.05,
            .estimatedBenefit = 0.80,
            .confidence = 0.90,
            .requiredCapabilities = {capability},
        });
        return response;
    }

private:
    CortexStrategyKind kind;
    std::string capability;
};

class FakeImaginationAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        sawImagination = request.imagination.available;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "compose two already learned TATARUS symbols";
        response.strategies.push_back(CortexStrategy{
            .id = "IMAGINATION_COMPOSE",
            .kind = CortexStrategyKind::Compose,
            .rationale = "Use only persistent symbol engrams already exposed by TATARUS.",
            .estimatedRisk = 0.05,
            .estimatedBenefit = 0.80,
            .confidence = 0.92,
            .requiredCapabilities = {"COMPOSE"},
        });
        response.imagination = ImaginationDirective{
            .mode = "COMPOSE",
            .symbols = {"HOUSE", "TREE"},
            .conceptText = "shelter scene",
        };
        return response;
    }
    std::uint64_t calls = 0;
    bool sawImagination = false;
};

class FakeUnifiedHybridAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;

        if (request.task == CortexTaskKind::Plan) {
            sawRoverPlan = request.spatial.mapAvailable;
            response.summary = "rover branch requests fresh sensing";
            response.strategies.push_back(CortexStrategy{
                .id = "HYBRID_SCAN",
                .kind = CortexStrategyKind::RequestScan,
                .rationale = "Use the bounded scan capability.",
                .estimatedRisk = 0.05,
                .estimatedBenefit = 0.70,
                .confidence = 0.90,
                .requiredCapabilities = {"SCAN"},
            });
            return response;
        }

        if (request.task == CortexTaskKind::HybridPlan) {
            sawSpatialInHybrid = request.spatial.mapAvailable;
            sawImaginationInHybrid = request.imagination.available;
            response.summary = "same cortex selects bounded internal composition";
            response.strategies.push_back(CortexStrategy{
                .id = "HYBRID_IMAGINE",
                .kind = CortexStrategyKind::UseImagination,
                .rationale = "Use the organism's existing visual-symbol memory before further action.",
                .estimatedRisk = 0.02,
                .estimatedBenefit = 0.85,
                .confidence = 0.94,
                .requiredCapabilities = {"USE_IMAGINATION", "COMPOSE"},
            });
            response.imagination = ImaginationDirective{
                .mode = "COMPOSE",
                .symbols = {"HOUSE", "TREE"},
                .conceptText = "possible shelter arrangement",
            };
            return response;
        }

        response.summary = "no-op";
        return response;
    }

    std::uint64_t calls = 0;
    bool sawRoverPlan = false;
    bool sawSpatialInHybrid = false;
    bool sawImaginationInHybrid = false;
};

class FakeMovementAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "bounded frontier strategy for teacher transfer";
        response.strategies.push_back(CortexStrategy{
            .id = "TEACH_EXPLORE",
            .kind = CortexStrategyKind::ExploreFrontier,
            .rationale = "Use the host-exposed frontier capability; motor direction remains neural.",
            .estimatedRisk = 0.10,
            .estimatedBenefit = 0.80,
            .confidence = 0.90,
            .requiredCapabilities = {"EXPLORE_FRONTIER"},
        });
        return response;
    }
    std::uint64_t calls = 0;
};

class FakeDreamAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        sawDreamTask = request.task == CortexTaskKind::Dream;
        sawRem = request.physiology.sleepPhase == SleepPhase::Rem;
        sawOnlyImaginationCapabilities = true;
        for (const auto& capability : request.availableCapabilities) {
            if (capability != "USE_IMAGINATION"
                && capability != "VISUAL_RECALL"
                && capability != "SYMBOL_RECALL"
                && capability != "COMPOSE"
                && capability != "FREE_IMAGINATION") {
                sawOnlyImaginationCapabilities = false;
            }
        }

        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "REM dream recombines only existing TATARUS symbol memory";
        response.strategies.push_back(CortexStrategy{
            .id = "REM_COMPOSE",
            .kind = CortexStrategyKind::Compose,
            .rationale = "Compose only already known persistent symbols.",
            .estimatedRisk = 0.01,
            .estimatedBenefit = 0.70,
            .confidence = 0.92,
            .requiredCapabilities = {"COMPOSE"},
        });
        response.imagination = ImaginationDirective{
            .mode = "COMPOSE",
            .symbols = {"HOUSE", "TREE"},
            .conceptText = "REM associative composition",
        };
        return response;
    }

    std::uint64_t calls = 0;
    bool sawDreamTask = false;
    bool sawRem = false;
    bool sawOnlyImaginationCapabilities = false;
};

void waitForWorker(CortexOrchestrator& cortex) {
    for (int i = 0; i < 2000 && !cortex.idle(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(cortex.idle(), "cortex worker did not become idle");
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open snapshot file: " + path.string());
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    require(size >= 0, "invalid snapshot file size");
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    require(static_cast<bool>(input) || input.eof(), "cannot read snapshot file");
    return bytes;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open text file: " + path.string());
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string manifestValue(const std::filesystem::path& manifest, const std::string& key) {
    const std::string text = readText(manifest);
    const std::string needle = key + "=";
    const auto start = text.find(needle);
    require(start != std::string::npos, "manifest key missing: " + key);
    const auto valueStart = start + needle.size();
    const auto end = text.find('\n', valueStart);
    return text.substr(valueStart, end == std::string::npos ? std::string::npos : end - valueStart);
}

void comparePersistentState(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    require(
        manifestValue(left / "manifest.txt", "core_state_hash")
            == manifestValue(right / "manifest.txt", "core_state_hash"),
        "observe-only changed the nervous-system state hash");
    require(
        manifestValue(left / "manifest.txt", "biological_state_hash")
            == manifestValue(right / "manifest.txt", "biological_state_hash"),
        "observe-only changed the biological state hash");
    require(
        readBytes(left / "sdk_state.bin") == readBytes(right / "sdk_state.bin"),
        "observe-only changed persisted RobotMind SDK state");
}

void contractTest() {
    CortexRequest request;
    request.requestId = 42;
    request.organismStep = 1234;
    request.stateFingerprint = 18'446'744'073'709'551'000ULL;
    request.task = CortexTaskKind::ObserveState;
    request.trigger = CortexTriggerReason::Explicit;
    request.goal = "inspect state";
    request.neural.novelty = 0.8;
    request.neural.predictionError = 0.4;
    request.neural.activeRepresentations.push_back({71, 0.9, 0.6});
    request.spatial.targetDistance = 4.25;
    request.spatial.targetBearing = -0.5;
    request.availableCapabilities = {"STATE_ANALYSIS"};

    const std::string serialized = serializeCortexRequest(request);
    require(serialized.find("\"request_id\":\"42\"") != std::string::npos,
            "request_id must serialize as lossless string");
    require(serialized.find("\"state_fingerprint\":\"18446744073709551000\"") != std::string::npos,
            "state_fingerprint must serialize losslessly");
    require(serialized.find("\"active_representations\":[{\"id\":\"71\"") != std::string::npos,
            "active Cortex representations are missing from the request contract");
    require(serialized.find("\"target_distance\":4.25") != std::string::npos
            && serialized.find("\"target_bearing\":-0.5") != std::string::npos,
            "target distance or bearing is missing from the spatial request contract");

    const std::string valid = R"JSON({
      "request_id":"42",
      "source_fingerprint":"18446744073709551000",
      "strategies":[{
        "id":"S1",
        "kind":"OBSERVE",
        "rationale":"State is novel.",
        "estimated_risk":0.1,
        "estimated_benefit":0.2,
        "confidence":0.8,
        "required_capabilities":["STATE_ANALYSIS"]
      }],
      "information_requests":[],
      "imagination":null,
      "summary":"Novel but stable."
    })JSON";
    const auto response = parseCortexResponse(valid, request);
    require(response.requestId == request.requestId, "parsed response request id mismatch");
    require(response.sourceFingerprint == request.stateFingerprint, "parsed fingerprint mismatch");
    require(response.strategies.size() == 1U, "valid strategy missing");
    require(response.strategies.front().kind == CortexStrategyKind::Observe,
            "strategy enum parse failed");

    requireThrows([&] { (void)parseCortexResponse("{", request); },
                  "malformed JSON was accepted");
    requireThrows([&] {
        (void)parseCortexResponse(R"JSON({"request_id":"41","source_fingerprint":"18446744073709551000","strategies":[],"information_requests":[],"imagination":null,"summary":"x"})JSON", request);
    }, "mismatched request id was accepted");
    requireThrows([&] {
        (void)parseCortexResponse(R"JSON({"request_id":"42","source_fingerprint":"18446744073709551000","strategies":[{"id":"x","kind":"MOVE_MOTOR","rationale":"x","estimated_risk":0,"estimated_benefit":0,"confidence":1,"required_capabilities":[]}],"information_requests":[],"imagination":null,"summary":"x"})JSON", request);
    }, "unknown strategy kind was accepted");
    requireThrows([&] {
        (void)parseCortexResponse(R"JSON({"request_id":"42","source_fingerprint":"18446744073709551000","strategies":[{"id":"x","kind":"OBSERVE","rationale":"x","estimated_risk":1.5,"estimated_benefit":0,"confidence":1,"required_capabilities":[]}],"information_requests":[],"imagination":null,"summary":"x"})JSON", request);
    }, "out-of-range probability was accepted");    requireThrows([&] {
        (void)parseCortexResponse(R"JSON({"request_id":"42","source_fingerprint":"18446744073709551000","strategies":[],"information_requests":[],"imagination":{"mode":"DRAW_PIXELS","symbols":[],"concept":"x"},"summary":"x"})JSON", request);
    }, "unknown imagination directive mode was accepted");
}


void configLoadTest(const std::filesystem::path& outputDirectory) {
    const auto goodPath = outputDirectory / "cortex_config_good.json";
    {
        std::ofstream out(goodPath, std::ios::trunc);
        out << R"JSON({
          "enabled": true,
          "mode": "observe_only",
          "execution_mode": "live",
          "provider": {
            "type": "lm_studio",
            "base_url": "http://127.0.0.1:1234/v1",
            "model": "AUTO",
            "timeout_ms": 3210,
            "temperature": 0.25,
            "top_p": 0.8,
            "max_output_tokens": 192
          },
          "worker": {"queue_capacity": 2},
          "freshness": {"maximum_response_age_steps": 177},
          "trigger": {
            "novelty": 0.71,
            "prediction_error": 0.46,
            "low_motor_confidence": 0.34,
            "cooldown_steps": 99
          },
          "arbiter": {
            "minimum_strategy_confidence": 0.31,
            "maximum_strategy_risk": 0.79,
            "minimum_accept_score": 0.22,
            "critical_visceral_distress": 0.88,
            "critical_brain_atp": 0.17
          },
          "sandbox": {"maximum_branches": 6},
          "learning": {
            "enabled": true,
            "competence_gate_enabled": true,
            "minimum_teaching_successes": 3,
            "minimum_autonomous_trials": 2,
            "autonomous_success_threshold": 0.8,
            "outcome_success_threshold": 0.6
          },
          "top_down": {
            "enabled": true,
            "maximum_recall_strength": 0.33,
            "maximum_goal_bias_strength": 0.19,
            "semantic_channels": 10,
            "semantic_gain": 0.14
          },
          "dream": {
            "enabled": true,
            "minimum_rem_interval_steps": 123,
            "maximum_dream_symbols": 7
          },
          "limits": {
            "max_strategies": 3,
            "max_information_requests": 3,
            "max_summary_bytes": 900,
            "max_rationale_bytes": 400,
            "max_prompt_bytes": 20000
          },
          "safety_contract": {
            "direct_reward": false,
            "direct_motor_control": false,
            "physiology_mutation": false,
            "neuron_access": false,
            "synapse_access": false
          }
        })JSON";
    }
    const auto config = loadCortexConfig(goodPath);
    require(config.enabled, "config loader lost enabled=true");
    require(config.mode == CortexMode::ObserveOnly, "config loader parsed wrong mode");
    require(config.provider.timeoutMs == 3210U, "config loader parsed wrong timeout");
    require(config.trigger.cooldownSteps == 99U, "config loader parsed wrong cooldown");
    require(config.freshness.maximumResponseAgeSteps == 177U,
            "config loader parsed wrong response freshness bound");
    require(config.arbiter.minimumStrategyConfidence == 0.31,
            "config loader parsed wrong arbiter confidence");
    require(config.arbiter.criticalBrainAtp == 0.17,
            "config loader parsed wrong arbiter ATP threshold");
    require(config.limits.maxPromptBytes == 20000U, "config loader parsed wrong prompt limit");
    require(config.sandbox.maximumBranches == 6U, "config loader parsed wrong sandbox branch limit");
    require(config.learning.enabled && config.learning.competenceGateEnabled
            && config.learning.minimumTeachingSuccesses == 3U
            && config.learning.minimumAutonomousTrials == 2U
            && config.learning.autonomousSuccessThreshold == 0.8
            && config.learning.outcomeSuccessThreshold == 0.6,
            "config loader parsed wrong Phase-9 learning contract");
    require(config.topDown.enabled
            && config.topDown.maximumRecallStrength == 0.33
            && config.topDown.maximumGoalBiasStrength == 0.19
            && config.topDown.semanticChannels == 10U
            && config.topDown.semanticGain == 0.14,
            "config loader parsed wrong Phase-10 top-down contract");
    require(config.dream.enabled
            && config.dream.minimumRemIntervalSteps == 123U
            && config.dream.maximumDreamSymbols == 7U,
            "config loader parsed wrong Phase-11 dream contract");

    const auto badPath = outputDirectory / "cortex_config_bad.json";
    {
        std::ofstream out(badPath, std::ios::trunc);
        out << R"JSON({"enabled":true,"mode":"observe_only","safety_contract":{"direct_motor_control":true}})JSON";
    }
    requireThrows([&] { (void)loadCortexConfig(badPath); },
                  "config loader accepted direct motor control in phases 0-3");
}

void observeOnlyNeutralityTest(const std::filesystem::path& outputDirectory) {
    RobotMindConfig mindConfig;
    mindConfig.seed = 0xC07E5U;
    mindConfig.neuronCount = 96;
    mindConfig.enableIdentity = false;
    mindConfig.enableCartography = false;

    RobotMind baseline(mindConfig);
    RobotMind observed(mindConfig);

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::ObserveOnly;
    cortexConfig.trigger.novelty = 0.0;
    cortexConfig.trigger.predictionError = 1.0;
    cortexConfig.trigger.lowMotorConfidence = 0.0;
    cortexConfig.trigger.cooldownSteps = 1;
    auto fake = std::make_shared<FakeModelClient>();
    CortexOrchestrator cortex(cortexConfig, fake);

    for (std::uint64_t step = 1; step <= 120; ++step) {
        const auto experience = frame(step);
        const auto a = baseline.observe(experience);
        const auto b = observed.observe(experience);
        require(a.assemblyId == b.assemblyId, "observe-only changed active assembly");
        require(a.motor.selectedDirection == b.motor.selectedDirection,
                "observe-only changed motor selection");
        require(a.motor.movement == b.motor.movement,
                "observe-only changed motor movement");
        cortex.observe(observed, nullptr, "observe deterministic test");
        if (step % 10U == 0U) std::this_thread::yield();
    }

    waitForWorker(cortex);
    cortex.observe(observed, nullptr, "drain final response");
    waitForWorker(cortex);

    require(fake->calls > 0U, "observe-only test never consulted fake cortex");
    const auto status = cortex.status();
    require(status.metrics.requestsSubmitted > 0U, "observe-only did not submit requests");

    const auto left = outputDirectory / "baseline";
    const auto right = outputDirectory / "observe_only";
    std::filesystem::remove_all(left);
    std::filesystem::remove_all(right);
    baseline.saveSnapshot(left);
    observed.saveSnapshot(right);
    comparePersistentState(left, right);

    require(baseline.stateJson() == observed.stateJson(),
            "observe-only changed public persistent state JSON");
}

void disabledModeTest() {
    RobotMindConfig config;
    config.seed = 12345;
    config.enableIdentity = false;
    config.enableCartography = false;
    RobotMind mind(config);
    mind.observe(frame(1));

    CortexConfig cortexConfig;
    cortexConfig.enabled = false;
    auto fake = std::make_shared<FakeModelClient>();
    CortexOrchestrator cortex(cortexConfig, fake);
    cortex.requestExplicitAnalysis(mind, nullptr, "must remain off");
    require(fake->calls == 0U, "disabled cortex called model client");
    require(cortex.status().metrics.requestsSubmitted == 0U,
            "disabled cortex recorded a request");
}

void explicitObserveOnlyTest() {
    RobotMindConfig config;
    config.seed = 9876;
    config.enableIdentity = false;
    config.enableCartography = false;
    RobotMind mind(config);
    for (std::uint64_t i = 1; i <= 5; ++i) mind.observe(frame(i));

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::ObserveOnly;
    auto fake = std::make_shared<FakeModelClient>();
    CortexOrchestrator cortex(cortexConfig, fake);

    const std::string before = mind.stateJson();
    cortex.requestExplicitAnalysis(mind, nullptr, "describe current state");
    waitForWorker(cortex);
    cortex.observe(mind, nullptr, "drain");
    const std::string after = mind.stateJson();

    require(before == after, "explicit observe-only analysis mutated RobotMind");
    const auto status = cortex.status();
    require(status.lastResponse.has_value(), "explicit response was not surfaced");
    require(status.lastResponse->summary == "observe-only synthetic test response",
            "unexpected fake response summary");
}

void asynchronousResponseRevalidationTest() {
    RobotMindConfig mindConfig;
    mindConfig.seed = 0xF2E5U;
    mindConfig.enableIdentity = false;
    mindConfig.enableCartography = false;
    RobotMind mind(mindConfig);
    for (std::uint64_t step = 1; step <= 5; ++step) mind.observe(frame(step));

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Advisor;
    config.freshness.maximumResponseAgeSteps = 8;
    auto delayed = std::make_shared<DelayedAdvisor>();
    CortexOrchestrator cortex(config, delayed);

    cortex.requestExplicitAnalysis(mind, nullptr, "bounded live analysis");
    for (std::uint64_t step = 6; step <= 9; ++step) mind.observe(frame(step));
    waitForWorker(cortex);
    cortex.poll(mind);

    auto status = cortex.status();
    require(status.metrics.staleResponses == 0U,
            "ordinary bounded live-state drift incorrectly invalidated the response");
    require(status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::Accept,
            "bounded live response was not revalidated by the current-state arbiter");

    CortexConfig expiredConfig = config;
    expiredConfig.freshness.maximumResponseAgeSteps = 2;
    auto expiredClient = std::make_shared<DelayedAdvisor>();
    CortexOrchestrator expired(expiredConfig, expiredClient);
    expired.requestExplicitAnalysis(mind, nullptr, "response must expire");
    for (std::uint64_t step = 10; step <= 13; ++step) mind.observe(frame(step));
    waitForWorker(expired);
    expired.poll(mind);
    status = expired.status();
    require(status.metrics.staleResponses == 1U && !status.lastDecision.has_value(),
            "response beyond the configured age bound was not discarded");

    auto changedClient = std::make_shared<DelayedAdvisor>();
    CortexOrchestrator changed(config, changedClient);
    changed.requestExplicitAnalysis(mind, nullptr, "executive state must remain stable");
    require(changed.pushGoal("different active goal", 1.0, mind.metrics().experiences) != 0U,
            "failed to establish changed executive context");
    waitForWorker(changed);
    changed.poll(mind);
    status = changed.status();
    require(status.metrics.staleResponses == 1U && !status.lastDecision.has_value(),
            "response survived a material executive-context change");

    CortexSpatialState spatial;
    spatial.environmentId = 42;
    spatial.mapAvailable = true;
    spatial.frontierRatio = 0.8;
    organism::OrganismTelemetry safe;
    safe.available = true;
    safe.stepCount = 100;
    safe.interoception.visceralDistress = 0.1;
    auto movementClient = std::make_shared<DelayedAdvisor>(
        CortexStrategyKind::ExploreFrontier, "EXPLORE_FRONTIER");
    CortexOrchestrator safety(config, movementClient);
    safety.requestExplicitAdvice(
        mind, &safe, spatial, {"EXPLORE_FRONTIER"}, "explore if currently safe");
    organism::OrganismTelemetry distressed = safe;
    distressed.stepCount = 101;
    distressed.interoception.visceralDistress = 0.95;
    waitForWorker(safety);
    safety.poll(mind, &distressed, &spatial);
    status = safety.status();
    require(status.metrics.staleResponses == 0U && status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::Reject,
            "current critical physiology did not override an in-flight movement proposal");
}



void arbiterCapabilityAndPhysiologyTest() {
    CortexArbiter arbiter;

    CortexRequest request;
    request.requestId = 7;
    request.stateFingerprint = 7007;
    request.task = CortexTaskKind::Plan;
    request.availableCapabilities = {"STATE_ANALYSIS"};
    request.spatial.mapAvailable = true;
    request.spatial.frontierRatio = 0.8;
    request.neural.novelty = 0.9;
    request.neural.predictionError = 0.7;
    request.neural.motorConfidence = 0.2;

    CortexResponse missingCapability;
    missingCapability.requestId = request.requestId;
    missingCapability.sourceFingerprint = request.stateFingerprint;
    missingCapability.strategies.push_back(CortexStrategy{
        .id = "SCAN_WITHOUT_CAPABILITY",
        .kind = CortexStrategyKind::RequestScan,
        .rationale = "scan",
        .estimatedRisk = 0.1,
        .estimatedBenefit = 0.8,
        .confidence = 0.9,
        .requiredCapabilities = {"SCAN"},
    });
    const auto rejected = arbiter.evaluate(request, missingCapability);
    require(rejected.kind == CortexDecisionKind::Reject,
            "arbiter accepted a strategy requiring an unavailable capability");

    request.availableCapabilities = {"STATE_ANALYSIS", "EXPLORE_FRONTIER"};
    request.physiology.available = true;
    request.physiology.atp = 0.8;
    request.physiology.brainOxygen = 0.9;
    request.physiology.visceralDistress = 0.95;

    CortexResponse unsafe;
    unsafe.requestId = request.requestId;
    unsafe.sourceFingerprint = request.stateFingerprint;
    unsafe.strategies.push_back(CortexStrategy{
        .id = "EXPLORE_WHILE_DISTRESSED",
        .kind = CortexStrategyKind::ExploreFrontier,
        .rationale = "frontier looks useful",
        .estimatedRisk = 0.1,
        .estimatedBenefit = 0.95,
        .confidence = 0.99,
        .requiredCapabilities = {"EXPLORE_FRONTIER"},
    });
    const auto physiologyBlocked = arbiter.evaluate(request, unsafe);
    require(physiologyBlocked.kind == CortexDecisionKind::Reject,
            "arbiter allowed exploration through critical organism distress");

    request.availableCapabilities = {"STATE_ANALYSIS", "SCAN"};
    request.physiology.visceralDistress = 0.1;
    CortexResponse needsData;
    needsData.requestId = request.requestId;
    needsData.sourceFingerprint = request.stateFingerprint;
    needsData.informationRequests.push_back({"SCAN", "Need a fresh obstacle scan."});
    const auto info = arbiter.evaluate(request, needsData);
    require(info.kind == CortexDecisionKind::RequestMoreInformation,
            "arbiter did not surface a valid bounded information request");
}

ExplorerResult makeExplorerObservation(RobotMind& mind) {
    Experience experience;
    experience.timestampNs = 1'000'000'000ULL;
    experience.body.battery = 0.9;
    experience.environment.temperature = 18.0;
    experience.environment.novelty = 0.8;
    experience.vision = {0.2, 0.1, 0.7, 0.1};

    ScannerFrame scanner;
    scanner.environmentId = 4242;
    scanner.timestampNs = experience.timestampNs;
    scanner.pose.positionMeters = {1.0, 0.0, 1.0};
    scanner.readings = {
        {.direction = {1.0, 0.0, 0.0}, .distanceMeters = 4.0,
         .maxRangeMeters = 5.0, .confidence = 0.95, .hit = false},
        {.direction = {0.0, 0.0, 1.0}, .distanceMeters = 2.0,
         .maxRangeMeters = 5.0, .confidence = 0.95, .hit = true,
         .semanticLabel = "rock"},
    };
    return mind.observeExplorer(experience, scanner);
}

void roverAdapterAuthorityTest() {
    RobotMindConfig config;
    config.seed = 0x45A5U;
    config.neuronCount = 96;
    config.enableIdentity = false;
    config.enableCartography = true;
    RobotMind mind(config);
    const auto explorer = makeExplorerObservation(mind);

    CortexDecision decision;
    decision.kind = CortexDecisionKind::Accept;
    decision.reason = "test acceptance";
    decision.selectedStrategy = CortexStrategy{
        .id = "FOLLOW",
        .kind = CortexStrategyKind::FollowKnownRoute,
        .rationale = "follow learned route",
        .estimatedRisk = 0.1,
        .estimatedBenefit = 0.8,
        .confidence = 0.9,
        .requiredCapabilities = {"FOLLOW_KNOWN_ROUTE"},
    };

    RoverCortexAdapter adapter;
    const auto directive = adapter.translate(decision, explorer);
    require(directive.kind == RoverDirectiveKind::UseNeuralMotorChoice,
            "follow-known-route did not preserve neural motor authority");
    require(directive.neuralDirection.has_value(),
            "rover directive did not expose the TATARUS neural direction");
    require(*directive.neuralDirection == explorer.cognition.motor.selectedDirection,
            "rover adapter fabricated a direction instead of using TATARUS motor output");

    decision.selectedStrategy->kind = CortexStrategyKind::RequestScan;
    const auto scan = adapter.translate(decision, explorer);
    require(scan.kind == RoverDirectiveKind::RequestSensorScan && scan.requiresFreshScan,
            "REQUEST_SCAN did not remain a bounded sensor request");
    require(!scan.neuralDirection.has_value(),
            "sensor request unexpectedly contains a motor direction");
}

void advisorRoverIntegrationTest() {
    RobotMindConfig config;
    config.seed = 0xA4B17U;
    config.neuronCount = 96;
    config.enableIdentity = false;
    config.enableCartography = true;
    RobotMind mind(config);
    const auto explorer = makeExplorerObservation(mind);

    RoverCortexAdapter adapter;
    const auto spatial = adapter.spatialState(explorer);
    require(spatial.mapAvailable, "rover adapter did not expose mapped spatial state");

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::Advisor;
    auto fake = std::make_shared<FakeRoverAdvisor>();
    CortexOrchestrator cortex(cortexConfig, fake);

    const std::string before = mind.stateJson();
    cortex.requestExplicitAdvice(
        mind, nullptr, spatial, RoverCortexAdapter::capabilities(), "reach target safely");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial);
    const std::string after = mind.stateJson();

    require(before == after,
            "Phase-5 advisor mutated RobotMind before the host executed any real action");
    const auto status = cortex.status();
    require(status.lastDecision.has_value(), "advisor response was not evaluated by the arbiter");
    require(status.lastDecision->kind == CortexDecisionKind::Accept,
            "grounded rover strategy was not accepted by the arbiter");
    require(status.metrics.arbiterEvaluations == 1U,
            "arbiter metrics did not record the advisor evaluation");

    const auto directive = adapter.translate(*status.lastDecision, explorer);
    require(directive.kind == RoverDirectiveKind::RequestSensorScan,
            "accepted rover plan did not translate to a bounded scan request");
    require(!directive.neuralDirection.has_value(),
            "bounded scan request unexpectedly acquired a motor direction");
    require(fake->calls == 1U, "advisor integration made an unexpected number of model calls");
}


VisualCanvas makeHouseCanvas() {
    VisualCanvas canvas(32, 32);
    for (std::size_t y = 14; y < 27; ++y) {
        for (std::size_t x = 8; x < 24; ++x) canvas.setPixel(x, y, 0.8);
    }
    for (std::size_t row = 0; row < 8; ++row) {
        const std::size_t y = 13 - row;
        const std::size_t left = 15 - row;
        const std::size_t right = 16 + row;
        for (std::size_t x = left; x <= right; ++x) canvas.setPixel(x, y, 1.0);
    }
    return canvas;
}

VisualCanvas makeTreeCanvas() {
    VisualCanvas canvas(32, 32);
    for (std::size_t y = 17; y < 29; ++y) {
        for (std::size_t x = 14; x < 18; ++x) canvas.setPixel(x, y, 0.75);
    }
    for (std::size_t y = 5; y < 20; ++y) {
        const std::size_t radius = static_cast<std::size_t>(1 + (y - 5) / 3);
        const std::size_t center = 16;
        const std::size_t left = center > radius ? center - radius : 0;
        const std::size_t right = std::min<std::size_t>(31, center + radius);
        for (std::size_t x = left; x <= right; ++x) canvas.setPixel(x, y, 0.95);
    }
    return canvas;
}

VisualImaginationConfig cortexImaginationConfig() {
    VisualImaginationConfig config;
    config.width = 32;
    config.height = 32;
    config.exposureObservations = 2;
    config.maximumEngrams = 16;
    config.neuralFeedbackStride = 8;
    config.paintPatchSide = 4;
    config.physiologicalExpressionGain = 0.0;
    config.mind.seed = 0x670067U;
    config.mind.neuronCount = 96;
    config.mind.enableIdentity = false;
    config.mind.enableCartography = false;
    return config;
}

void teachHybridSymbols(VisualImagination& imagination) {
    const auto house = makeHouseCanvas();
    const auto tree = makeTreeCanvas();
    const auto houseLearned = imagination.learnToTrace(
        house, VisualImagination::makeTeacherTrace(house, 0.01, 4), "house");
    const auto treeLearned = imagination.learnToTrace(
        tree, VisualImagination::makeTeacherTrace(tree, 0.01, 4), "tree");
    require(houseLearned.success && treeLearned.success,
            "failed to establish visual memory for cortex imagination tests");
    require(imagination.associateSymbol("HOUSE", house),
            "failed to associate HOUSE symbol");
    require(imagination.associateSymbol("TREE", tree),
            "failed to associate TREE symbol");
}

std::vector<std::string> hybridCapabilities() {
    auto result = RoverCortexAdapter::capabilities();
    const auto imagination = ImaginatioCortexAdapter::capabilities();
    for (const auto& capability : imagination) {
        if (std::find(result.begin(), result.end(), capability) == result.end()) {
            result.push_back(capability);
        }
    }
    return result;
}

void imaginationArbiterAndAdapterTest() {
    VisualImagination imagination(cortexImaginationConfig());
    teachHybridSymbols(imagination);

    ImaginatioCortexAdapter adapter;
    const auto state = adapter.imaginationState(imagination);
    require(state.available && state.visualEngrams >= 2U && state.symbolEngrams >= 2U,
            "IMAGINATIO adapter did not expose persistent memory counts");
    require(std::find(state.knownSymbols.begin(), state.knownSymbols.end(), "HOUSE")
                != state.knownSymbols.end(),
            "IMAGINATIO adapter did not expose HOUSE symbol");

    CortexRequest request;
    request.requestId = 67;
    request.stateFingerprint = 670067;
    request.task = CortexTaskKind::Imagine;
    request.neural.novelty = 0.8;
    request.neural.predictionError = 0.4;
    request.imagination = state;
    request.availableCapabilities = ImaginatioCortexAdapter::capabilities();

    CortexResponse response;
    response.requestId = request.requestId;
    response.sourceFingerprint = request.stateFingerprint;
    response.strategies.push_back(CortexStrategy{
        .id = "COMPOSE_MEMORY",
        .kind = CortexStrategyKind::Compose,
        .rationale = "Combine only the learned HOUSE and TREE symbol engrams.",
        .estimatedRisk = 0.02,
        .estimatedBenefit = 0.8,
        .confidence = 0.95,
        .requiredCapabilities = {"COMPOSE"},
    });
    response.imagination = ImaginationDirective{
        .mode = "COMPOSE",
        .symbols = {"house", "tree"},
        .conceptText = "shelter",
    };

    CortexArbiter arbiter;
    const auto decision = arbiter.evaluate(request, response);
    require(decision.kind == CortexDecisionKind::SendToImagination,
            "arbiter did not route a grounded composition to IMAGINATIO");

    const auto directive = adapter.translate(decision, response, imagination);
    require(directive.executable && directive.kind == ImaginatioDirectiveKind::Compose,
            "bounded IMAGINATIO composition directive was not resolved");
    require(directive.symbols.size() == 2U
            && directive.symbols[0] == "HOUSE" && directive.symbols[1] == "TREE",
            "adapter did not canonicalize LLM symbol names against TATARUS memory");

    require(imagination.associateSymbol("ROCK", makeHouseCanvas()),
            "failed to establish the extra known symbol for explicit grounding");
    auto overBroadResponse = response;
    overBroadResponse.imagination->symbols = {"HOUSE", "TREE", "ROCK"};
    const auto explicitlyGrounded = adapter.translate(
        decision, overBroadResponse, imagination, "compose HOUSE and TREE");
    require(explicitlyGrounded.executable
            && explicitlyGrounded.symbols == std::vector<std::string>{"HOUSE", "TREE"},
            "explicit multi-symbol request was polluted by an unrequested known symbol");

    auto badResponse = response;
    badResponse.imagination->symbols = {"HOUSE", "NONEXISTENT"};
    const auto rejectedDirective = adapter.translate(decision, badResponse, imagination);
    require(!rejectedDirective.executable,
            "adapter accepted a hallucinated symbol not present in TATARUS memory");

    const auto execution = adapter.execute(directive, imagination);
    require(execution.executed && execution.report.has_value() && execution.report->success,
            "bounded IMAGINATIO composition did not execute successfully");
}

void imaginationOrchestratorNeutralityTest() {
    VisualImagination imagination(cortexImaginationConfig());
    teachHybridSymbols(imagination);
    ImaginatioCortexAdapter adapter;
    const auto state = adapter.imaginationState(imagination);

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Imagination;
    auto fake = std::make_shared<FakeImaginationAdvisor>();
    CortexOrchestrator cortex(config, fake);

    const std::string before = imagination.mind().stateJson();
    cortex.requestExplicitImagination(
        imagination.mind(), nullptr, state,
        ImaginatioCortexAdapter::capabilities(), "imagine shelter from known memories");
    waitForWorker(cortex);
    cortex.poll(imagination.mind(), nullptr, nullptr, &state);
    const std::string after = imagination.mind().stateJson();

    require(before == after,
            "Phase-6 cortex mutated RobotMind before IMAGINATIO adapter execution");
    const auto status = cortex.status();
    require(status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::SendToImagination,
            "Phase-6 orchestrator did not produce a bounded imagination decision");
    require(status.metrics.imaginationDirectivesPrepared == 1U,
            "Phase-6 imagination metric was not recorded");
    require(fake->calls == 1U && fake->sawImagination,
            "Phase-6 model did not receive the bounded IMAGINATIO state");
}

void unifiedHybridModeTest() {
    RobotMindConfig mindConfig;
    mindConfig.seed = 0x6707U;
    mindConfig.neuronCount = 96;
    mindConfig.enableIdentity = false;
    mindConfig.enableCartography = true;
    RobotMind mind(mindConfig);

    auto imaginationConfig = cortexImaginationConfig();
    VisualImagination imagination(mind, imaginationConfig);
    teachHybridSymbols(imagination);

    const auto explorer = makeExplorerObservation(mind);
    RoverCortexAdapter roverAdapter;
    ImaginatioCortexAdapter imaginationAdapter;
    const auto spatial = roverAdapter.spatialState(explorer);
    const auto imaginationState = imaginationAdapter.imaginationState(imagination);

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Hybrid;
    auto fake = std::make_shared<FakeUnifiedHybridAdvisor>();
    CortexOrchestrator cortex(config, fake);

    // The same Cortex instance first services the rover planning path.
    cortex.requestExplicitAdvice(
        mind, nullptr, spatial, RoverCortexAdapter::capabilities(), "inspect route");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial, nullptr);
    auto status = cortex.status();
    require(status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::Accept,
            "Hybrid mode could not service the rover advisor path");
    const auto roverDirective = roverAdapter.translate(*status.lastDecision, explorer);
    require(roverDirective.kind == RoverDirectiveKind::RequestSensorScan,
            "same hybrid cortex did not preserve bounded rover scan semantics");

    // Without constructing a second model/client/orchestrator, the same Cortex
    // now receives both real-world and imagination state in one request.
    cortex.requestExplicitHybrid(
        mind, nullptr, spatial, imaginationState, hybridCapabilities(),
        "consider whether internal imagination is useful before further exploration");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial, &imaginationState);
    status = cortex.status();

    require(status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::SendToImagination,
            "HYBRID_PLAN did not route the accepted internal strategy to IMAGINATIO");
    require(status.metrics.hybridRequests == 1U,
            "hybrid request metric was not recorded");
    require(fake->calls == 2U && fake->sawRoverPlan
            && fake->sawSpatialInHybrid && fake->sawImaginationInHybrid,
            "one Cortex instance did not receive both rover and IMAGINATIO state");

    const auto response = status.lastResponse;
    require(response.has_value(), "hybrid response missing");
    const auto imaginationDirective = imaginationAdapter.translate(
        *status.lastDecision, *response, imagination);
    require(imaginationDirective.executable,
            "hybrid imagination strategy could not be resolved against TATARUS memory");
}

void counterfactualSandboxIsolationTest() {
    organism::OrganismConfig config;
    config.mind.seed = 0x890089U;
    config.mind.neuronCount = 96;
    config.mind.enableIdentity = false;
    config.mind.enableCartography = true;
    organism::SyntheticOrganism real(config);
    organism::AtmosphericEnvironment earth;

    for (std::uint64_t step = 1; step <= 20; ++step) {
        (void)real.step(frame(step), earth, 0.05);
    }
    const std::string realBefore = real.organismJson() + real.mind().stateJson();

    CounterfactualScenario safe;
    safe.id = "SAFE_BRANCH";
    safe.strategyId = "FOLLOW_KNOWN_ROUTE";
    safe.origin = CounterfactualOrigin::Imagination;
    safe.actionId = 1;
    for (std::uint64_t step = 21; step <= 26; ++step) {
        CounterfactualFrame branchFrame;
        branchFrame.experience = frame(step);
        branchFrame.experience.reward = 0.12;
        branchFrame.atmosphere = earth;
        branchFrame.usePhysicalLoad = true;
        branchFrame.load.mechanicalPowerW = 8.0;
        branchFrame.load.cpuGpuPowerW = 8.0;
        safe.frames.push_back(branchFrame);
    }
    safe.terminalOutcome = ActionOutcome{.id = 1, .reward = 0.45, .success = 1.0, .novelty = 0.2};
    safe.endEpisode = true;
    safe.reachedGoal = true;

    CounterfactualScenario risky;
    risky.id = "RISKY_BRANCH";
    risky.strategyId = "EXPLORE_FRONTIER";
    risky.origin = CounterfactualOrigin::Simulation;
    risky.actionId = 2;
    auto hypoxic = earth;
    hypoxic.o2Fraction = 0.05;
    for (std::uint64_t step = 21; step <= 26; ++step) {
        CounterfactualFrame branchFrame;
        branchFrame.experience = frame(step);
        branchFrame.experience.reward = -0.10;
        branchFrame.atmosphere = hypoxic;
        branchFrame.usePhysicalLoad = true;
        branchFrame.load.mechanicalPowerW = 140.0;
        branchFrame.load.cpuGpuPowerW = 20.0;
        risky.frames.push_back(branchFrame);
    }
    risky.terminalOutcome = ActionOutcome{.id = 2, .reward = -0.40, .success = 0.0, .novelty = 0.8};
    risky.endEpisode = true;
    risky.reachedGoal = false;

    CounterfactualSandbox sandbox(CortexSandboxConfig{.maximumBranches = 4});
    const auto result = sandbox.evaluate(real, {safe, risky});
    require(result.isolationVerified, "counterfactual sandbox did not verify real-state isolation");
    require(result.realSnapshotHashBefore == result.realSnapshotHashAfter,
            "counterfactual branch leaked into the real organism snapshot");
    require(realBefore == real.organismJson() + real.mind().stateJson(),
            "counterfactual sandbox changed public real-organism state");
    require(result.branches.size() == 2U && result.branches[0].success && result.branches[1].success,
            "counterfactual sandbox failed to execute both isolated branches");
    require(result.branches[0].stepsExecuted == 6U && result.branches[1].stepsExecuted == 6U,
            "counterfactual sandbox executed unexpected branch length");
    require(result.branches[0].branchSnapshotHash != result.realSnapshotHashBefore,
            "safe branch did not develop an isolated branch state");
    require(result.branches[1].final.visceralDistress >= result.branches[0].final.visceralDistress,
            "hypoxic/high-load branch did not produce the expected higher distress signal");
    require(result.bestBranchId.has_value() && *result.bestBranchId == "SAFE_BRANCH",
            "counterfactual utility did not prefer the successful lower-cost branch");
}

CortexRequest teacherRequest(std::uint64_t requestId) {
    CortexRequest request;
    request.requestId = requestId;
    request.organismStep = 100;
    request.stateFingerprint = 0x12340000ULL + requestId;
    request.task = CortexTaskKind::Plan;
    request.trigger = CortexTriggerReason::Novelty;
    request.neural.novelty = 0.75;
    request.neural.predictionError = 0.55;
    request.neural.motorConfidence = 0.30;
    request.neural.sequenceFamiliarity = 0.25;
    request.neural.brainAtp = 0.8;
    request.physiology.available = true;
    request.physiology.atp = 0.8;
    request.physiology.visceralDistress = 0.2;
    request.spatial.environmentId = 99;
    request.spatial.mapAvailable = true;
    request.spatial.frontierRatio = 0.65;
    request.availableCapabilities = {"STATE_ANALYSIS", "EXPLORE_FRONTIER"};
    return request;
}

CortexDecision teacherDecision() {
    CortexDecision decision;
    decision.kind = CortexDecisionKind::Accept;
    decision.score = 0.7;
    decision.reason = "grounded teacher strategy";
    decision.selectedStrategy = CortexStrategy{
        .id = "TEACH_EXPLORE",
        .kind = CortexStrategyKind::ExploreFrontier,
        .rationale = "bounded strategy",
        .estimatedRisk = 0.1,
        .estimatedBenefit = 0.8,
        .confidence = 0.9,
        .requiredCapabilities = {"EXPLORE_FRONTIER"},
    };
    return decision;
}

void teacherTransferNeutralityAndCompetenceTest(const std::filesystem::path& outputDirectory) {
    RobotMindConfig config;
    config.seed = 0x990099U;
    config.neuronCount = 96;
    config.enableIdentity = false;
    config.enableCartography = false;
    RobotMind withTeacher(config);
    RobotMind baseline(config);
    for (std::uint64_t step = 1; step <= 20; ++step) {
        (void)withTeacher.observe(frame(step));
        (void)baseline.observe(frame(step));
    }

    CortexLearningConfig learningConfig;
    learningConfig.minimumTeachingSuccesses = 2;
    learningConfig.minimumAutonomousTrials = 2;
    learningConfig.autonomousSuccessThreshold = 0.75;
    learningConfig.outcomeSuccessThreshold = 0.5;
    CortexTeacherTransfer teacher(learningConfig);
    const auto decision = teacherDecision();

    for (std::uint64_t trial = 0; trial < 2; ++trial) {
        const auto request = teacherRequest(100 + trial);
        const auto handle = teacher.beginCortexAssisted(request, decision, 1);
        const ActionEvent action{.id = 1, .label = "real-teacher-action", .intensity = 1.0};
        withTeacher.beginAction(action);
        baseline.beginAction(action);
        const ActionOutcome outcome{.id = 1, .reward = 0.35, .success = 1.0, .novelty = 0.2};
        withTeacher.endAction(outcome);
        baseline.endAction(outcome);
        teacher.completeRealOutcome(handle, outcome, withTeacher);
        require(withTeacher.stateJson() == baseline.stateJson(),
                "teacher-transfer bookkeeping mutated TATARUS beyond the real ActionOutcome path");
    }

    auto probeRequest = teacherRequest(200);
    require(teacher.consultationDecision(probeRequest)
                == CortexConsultationDecision::SuppressForAutonomyProbe,
            "successful cortex teaching did not schedule a bounded autonomous probe");

    for (std::uint64_t trial = 0; trial < 2; ++trial) {
        if (trial > 0) {
            require(teacher.consultationDecision(probeRequest)
                        == CortexConsultationDecision::SuppressForAutonomyProbe,
                    "teacher did not schedule the second required autonomy probe");
        }
        const auto handle = teacher.beginAutonomous(probeRequest, 1);
        const ActionEvent action{.id = 1, .label = "real-autonomous-action", .intensity = 1.0};
        withTeacher.beginAction(action);
        baseline.beginAction(action);
        const ActionOutcome outcome{.id = 1, .reward = 0.30, .success = 1.0, .novelty = 0.1};
        withTeacher.endAction(outcome);
        baseline.endAction(outcome);
        teacher.completeRealOutcome(handle, outcome, withTeacher);
        require(withTeacher.stateJson() == baseline.stateJson(),
                "autonomy competence bookkeeping changed the real learning path");
    }

    const auto learned = teacher.snapshot();
    require(learned.episodesCompleted == 4U
            && learned.cortexAssistedEpisodes == 2U
            && learned.autonomousEpisodes == 2U,
            "teacher-transfer episode accounting is incorrect");
    require(learned.competencies.size() == 1U,
            "equivalent teacher contexts did not collapse into one competence class");
    const auto& competence = learned.competencies.front();
    require(competence.cortexSuccesses == 2U
            && competence.autonomousTrials == 2U
            && competence.autonomousSuccesses == 2U,
            "teacher-transfer competence counts are incorrect");
    require(competence.cortexOptional && competence.autonomousSuccessRate >= 0.99,
            "successful autonomous retention was not recognized");

    const auto learningPath = outputDirectory / "cortex_learning_state.bin";
    teacher.saveState(learningPath);
    CortexTeacherTransfer restored(learningConfig);
    restored.loadState(learningPath);
    const auto restoredSnapshot = restored.snapshot();
    require(restoredSnapshot.episodesCompleted == learned.episodesCompleted
            && restoredSnapshot.competencies.size() == learned.competencies.size()
            && restoredSnapshot.competencies.front().autonomousSuccesses
                == competence.autonomousSuccesses,
            "persistent Cortex competence state did not round-trip");
}

void orchestratorTeacherApiTest() {
    RobotMindConfig mindConfig;
    mindConfig.seed = 0x9911U;
    mindConfig.neuronCount = 96;
    mindConfig.enableIdentity = false;
    mindConfig.enableCartography = false;
    RobotMind mind(mindConfig);
    for (std::uint64_t step = 1; step <= 10; ++step) (void)mind.observe(frame(step));

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::Advisor;
    cortexConfig.learning.minimumTeachingSuccesses = 1;
    cortexConfig.learning.minimumAutonomousTrials = 1;
    cortexConfig.trigger.novelty = 0.0;
    cortexConfig.trigger.predictionError = 0.0;
    cortexConfig.trigger.lowMotorConfidence = 1.0;
    cortexConfig.trigger.cooldownSteps = 0;
    auto fake = std::make_shared<FakeMovementAdvisor>();
    CortexOrchestrator cortex(cortexConfig, fake);

    CortexSpatialState spatial;
    spatial.environmentId = 77;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.8;
    cortex.requestExplicitAdvice(
        mind, nullptr, spatial,
        {"STATE_ANALYSIS", "EXPLORE_FRONTIER"},
        "learn one bounded real strategy");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial);
    const auto status = cortex.status();
    require(status.lastRequest.has_value() && status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::Accept,
            "orchestrator did not retain teacher provenance for accepted advice");

    const auto preLearningSnapshot = std::filesystem::temp_directory_path()
        / "tatarus_phase9_orchestrator_probe";
    std::filesystem::remove_all(preLearningSnapshot);
    mind.saveSnapshot(preLearningSnapshot);

    const auto handle = cortex.beginTeachingAction(1);
    require(handle.has_value(), "orchestrator did not open a cortex-assisted teaching episode");
    mind.beginAction(ActionEvent{.id = 1, .label = "real-host-action", .intensity = 1.0});
    const ActionOutcome outcome{.id = 1, .reward = 0.4, .success = 1.0, .novelty = 0.2};
    mind.endAction(outcome);
    cortex.completeTeachingAction(*handle, outcome, mind, nullptr, &spatial);
    const auto learning = cortex.learningStatus();
    require(learning.episodesCompleted == 1U && learning.cortexAssistedEpisodes == 1U,
            "orchestrator teacher API did not record the real completed outcome");

    RobotMind probeMind(mindConfig);
    require(probeMind.loadSnapshot(preLearningSnapshot),
            "could not restore pre-learning state for competence-gate probe");
    cortex.advise(
        probeMind, nullptr, spatial,
        {"STATE_ANALYSIS", "EXPLORE_FRONTIER"},
        "probe whether the learned context can run without Cortex");
    const auto gated = cortex.status();
    require(gated.metrics.consultationsSuppressed == 1U
            && gated.lastSuppressedRequest.has_value()
            && fake->calls == 1U,
            "orchestrator did not suppress exactly one consultation for an autonomy probe");
    const auto autonomous = cortex.beginAutonomousTeachingAction(1);
    require(autonomous.has_value() && !autonomous->cortexUsed,
            "orchestrator did not bind the suppressed context to an autonomous teaching episode");
    std::filesystem::remove_all(preLearningSnapshot);

}

void boundedTopDownCognitionTest() {
    RobotMindConfig config;
    config.seed = 0xA1011U;
    config.neuronCount = 96;
    config.enableIdentity = false;
    config.enableCartography = false;

    RobotMind baseline(config);
    RobotMind neutral(config);
    for (std::uint64_t step = 1; step <= 20; ++step) {
        const auto sample = frame(step);
        (void)baseline.observe(sample);
        (void)neutral.observe(sample);
    }
    const auto baselineResult = baseline.observe(frame(21));
    const CognitiveCue neutralCue;
    const auto neutralResult = neutral.observeWithCognitiveCue(frame(21), neutralCue);
    require(baseline.stateJson() == neutral.stateJson(),
            "neutral CognitiveCue changed the legacy RobotMind state");
    require(baselineResult.motor.selectedDirection == neutralResult.motor.selectedDirection
            && baselineResult.motor.movement == neutralResult.motor.movement,
            "neutral CognitiveCue changed motor output");

    RobotMind mind(config);
    for (std::uint64_t step = 1; step <= 10; ++step) (void)mind.observe(frame(step));

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::Advisor;
    cortexConfig.topDown.enabled = true;
    cortexConfig.topDown.maximumRecallStrength = 0.35;
    cortexConfig.topDown.maximumGoalBiasStrength = 0.20;
    cortexConfig.topDown.semanticChannels = 12;
    cortexConfig.topDown.semanticGain = 0.15;
    auto fake = std::make_shared<FakeMovementAdvisor>();
    CortexOrchestrator cortex(cortexConfig, fake);

    CortexSpatialState spatial;
    spatial.environmentId = 5;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.7;
    cortex.requestExplicitAdvice(
        mind, nullptr, spatial,
        {"STATE_ANALYSIS", "EXPLORE_FRONTIER"},
        "reach the target while preserving organism state");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial);

    const auto prepared = cortex.prepareTopDownCue();
    require(prepared.ready, "accepted Cortex strategy did not prepare a phase-10 CognitiveCue");
    require(prepared.cue.recallStrength <= 0.35
            && prepared.cue.goalBiasStrength <= 0.20
            && prepared.cue.semanticContext.size() == 12U,
            "phase-10 CognitiveCue exceeded configured bounds");
    for (const double value : prepared.cue.semanticContext) {
        require(std::abs(value) <= 0.15 + 1.0e-12,
                "semantic CognitiveCue channel exceeded bounded gain");
    }

    const auto applied = cortex.applyTopDownCue(mind, frame(22), nullptr, &spatial);
    require(applied.has_value(), "phase-10 CognitiveCue was not applied to one real observation");
    const auto status = cortex.status();
    require(status.metrics.topDownCuesPrepared == 1U
            && status.metrics.topDownCuesApplied == 1U
            && !status.lastCognitiveCue.has_value(),
            "phase-10 top-down metrics or one-shot consumption are incorrect");
    require(!cortex.applyTopDownCue(mind, frame(23), nullptr, &spatial).has_value(),
            "phase-10 CognitiveCue was incorrectly reusable after one observation");

    CognitiveCue unsafe;
    unsafe.recallStrength = 0.75;
    requireThrows([&] { (void)mind.observeWithCognitiveCue(frame(24), unsafe); },
                  "RobotMind accepted a CognitiveCue above the hard recall limit");
}

void sleepDreamCortexTest() {
    RobotMindConfig config;
    config.seed = 0xD11EAU;
    config.neuronCount = 96;
    config.enableIdentity = false;
    config.enableCartography = false;
    config.sleepTiming.circadianCycleMs = 200.0;
    config.sleepTiming.sleepPressureTauMs = 45.0;
    config.sleepTiming.nremMinimumMs = 20.0;
    config.sleepTiming.remMinimumMs = 30.0;
    RobotMind mind(config);

    bool reachedNrem = false;
    for (int tick = 0; tick < 2000; ++tick) {
        if (mind.physiology().sleepPhase == SleepPhase::Nrem) {
            reachedNrem = true;
            break;
        }
        mind.rest(1);
    }
    require(reachedNrem, "accelerated RobotMind timing did not reach NREM");

    CortexConfig cortexConfig;
    cortexConfig.enabled = true;
    cortexConfig.mode = CortexMode::Hybrid;
    cortexConfig.topDown.enabled = true;
    cortexConfig.dream.enabled = true;
    cortexConfig.dream.minimumRemIntervalSteps = 1;
    cortexConfig.dream.maximumDreamSymbols = 4;
    auto fake = std::make_shared<FakeDreamAdvisor>();
    CortexOrchestrator cortex(cortexConfig, fake);

    CortexImaginationState imagination;
    imagination.available = true;
    imagination.stage = 4;
    imagination.visualEngrams = 3;
    imagination.symbolEngrams = 3;
    imagination.knownConcepts = {"SHELTER", "HOME", "FOREST", "ROAD", "EXTRA"};
    imagination.knownSymbols = {"HOUSE", "TREE", "SUN", "ROAD", "EXTRA"};
    imagination.knownCategories = {"PLACE", "OBJECT", "NATURE", "ROUTE", "EXTRA"};

    cortex.sleepCycle(
        mind, nullptr, imagination,
        {"SCAN", "COMPOSE", "FREE_IMAGINATION", "SYMBOL_RECALL"},
        "associate recent persistent memories");
    require(fake->calls == 0U, "NREM incorrectly invoked the language cortex");
    require(cortex.status().metrics.nremCycles == 1U,
            "NREM consolidation cycle was not recorded");

    cortex.requestExplicitAnalysis(mind, nullptr, "this wake request must be blocked");
    require(fake->calls == 0U && cortex.status().metrics.sleepBlockedRequests == 1U,
            "ordinary Cortex request was not blocked during NREM");

    bool reachedRem = false;
    for (int tick = 0; tick < 1000; ++tick) {
        if (mind.physiology().sleepPhase == SleepPhase::Rem) {
            reachedRem = true;
            break;
        }
        mind.rest(1);
    }
    require(reachedRem, "accelerated RobotMind timing did not transition NREM -> REM");

    cortex.sleepCycle(
        mind, nullptr, imagination,
        {"SCAN", "COMPOSE", "FREE_IMAGINATION", "SYMBOL_RECALL"},
        "REM associative recombination");
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, nullptr, &imagination);
    const auto status = cortex.status();
    require(fake->calls == 1U && fake->sawDreamTask && fake->sawRem,
            "REM did not produce exactly one DREAM request grounded in REM physiology");
    require(fake->sawOnlyImaginationCapabilities,
            "REM DREAM request leaked a real-world capability such as SCAN");
    require(status.lastRequest.has_value()
            && status.lastRequest->task == CortexTaskKind::Dream
            && status.lastRequest->trigger == CortexTriggerReason::SleepRem,
            "REM request provenance is not marked DREAM/SLEEP_REM");
    require(status.lastRequest->imagination.knownSymbols.size() <= 4U,
            "REM dream memory exposure exceeded maximum_dream_symbols");
    require(status.lastDecision.has_value()
            && status.lastDecision->kind == CortexDecisionKind::SendToImagination,
            "REM dream was not routed exclusively to bounded IMAGINATIO");
    require(!status.lastCognitiveCue.has_value(),
            "REM dream incorrectly prepared a wake top-down CognitiveCue");
    require(status.metrics.remDreamRequests == 1U
            && status.metrics.remDreamResponses == 1U,
            "REM dream request/response metrics are incorrect");

    CortexRequest illegalDream;
    illegalDream.requestId = 90;
    illegalDream.stateFingerprint = 91;
    illegalDream.task = CortexTaskKind::Dream;
    illegalDream.physiology.sleepPhase = SleepPhase::Rem;
    illegalDream.imagination = imagination;
    illegalDream.availableCapabilities = {"EXPLORE_FRONTIER", "COMPOSE"};
    CortexResponse illegalResponse;
    illegalResponse.requestId = 90;
    illegalResponse.sourceFingerprint = 91;
    illegalResponse.strategies.push_back(CortexStrategy{
        .id = "ILLEGAL_DREAM_MOVE",
        .kind = CortexStrategyKind::ExploreFrontier,
        .rationale = "must be rejected in REM",
        .estimatedRisk = 0.0,
        .estimatedBenefit = 1.0,
        .confidence = 1.0,
        .requiredCapabilities = {"EXPLORE_FRONTIER"},
    });
    CortexArbiter arbiter;
    const auto rejected = arbiter.evaluate(illegalDream, illegalResponse);
    require(rejected.kind != CortexDecisionKind::Accept,
            "DREAM arbiter accepted a real-world movement strategy");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::temp_directory_path() / "tatarus_cortex_test_output";
        std::filesystem::create_directories(output);

        contractTest();
        configLoadTest(output);
        disabledModeTest();
        explicitObserveOnlyTest();
        asynchronousResponseRevalidationTest();
        observeOnlyNeutralityTest(output);
        arbiterCapabilityAndPhysiologyTest();
        roverAdapterAuthorityTest();
        advisorRoverIntegrationTest();
        imaginationArbiterAndAdapterTest();
        imaginationOrchestratorNeutralityTest();
        unifiedHybridModeTest();
        counterfactualSandboxIsolationTest();
        teacherTransferNeutralityAndCompetenceTest(output);
        orchestratorTeacherApiTest();
        boundedTopDownCognitionTest();
        sleepDreamCortexTest();

        std::cout << "PASS cortex_contract\n"
                  << "PASS cortex_config_load\n"
                  << "PASS cortex_disabled\n"
                  << "PASS cortex_explicit_observe_only\n"
                  << "PASS cortex_asynchronous_response_revalidation\n"
                  << "PASS cortex_persistent_state_neutrality\n"
                  << "PASS cortex_arbiter_capability_and_physiology\n"
                  << "PASS cortex_rover_adapter_authority\n"
                  << "PASS cortex_advisor_rover_integration\n"
                  << "PASS cortex_imaginatio_adapter_grounding\n"
                  << "PASS cortex_imagination_orchestrator_neutrality\n"
                  << "PASS cortex_unified_hybrid_mode\n"
                  << "PASS cortex_counterfactual_sandbox_isolation\n"
                  << "PASS cortex_teacher_transfer_neutrality_and_competence\n"
                  << "PASS cortex_orchestrator_teacher_api\n"
                  << "PASS cortex_bounded_top_down_cognition\n"
                  << "PASS cortex_sleep_dream_cycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
