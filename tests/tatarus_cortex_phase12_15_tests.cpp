#include "tatarus/cortex.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace tatarus;
using namespace tatarus::cortex;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Experience frame(std::uint64_t step) {
    Experience e;
    e.timestampNs = step * 50'000'000ULL;
    e.vision = {0.2, 0.8, 0.1, 0.0};
    e.touch = {0.0, 0.0, 0.95};
    e.spatialContext = {0.2, 0.7};
    e.environment.light = 0.8;
    e.environment.temperature = 22.0;
    e.environment.novelty = 0.8;
    e.body.battery = 0.95;
    return e;
}

void seedMind(RobotMind& mind) {
    for (std::uint64_t i = 1; i <= 12; ++i) (void)mind.observe(frame(i));
}

void waitForWorker(CortexOrchestrator& cortex) {
    for (int i = 0; i < 3000 && !cortex.idle(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(cortex.idle(), "Cortex worker did not become idle");
}

class ExecutivePlanner final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        sawExecutive = request.executive.available;
        sawWorkingMemory = !request.executive.workingMemory.empty();
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "bounded two-step executive plan";
        response.strategies.push_back(CortexStrategy{
            .id = "PLAN_SCAN",
            .kind = CortexStrategyKind::RequestScan,
            .rationale = "Acquire current geometry before recall.",
            .estimatedRisk = 0.02,
            .estimatedBenefit = 0.90,
            .confidence = 0.95,
            .requiredCapabilities = {"SCAN"},
        });
        response.plan = CortexPlanProposal{
            .id = "GOAL_PLAN_1",
            .steps = {
                CortexPlanStep{
                    .id = "STEP_SCAN",
                    .kind = CortexStrategyKind::RequestScan,
                    .objective = "Acquire fresh local geometry",
                    .requiredCapabilities = {"SCAN"},
                },
                CortexPlanStep{
                    .id = "STEP_RECALL",
                    .kind = CortexStrategyKind::RecallRoute,
                    .objective = "Recall the safest known route",
                    .requiredCapabilities = {"RECALL_ROUTE"},
                },
            },
        };
        return response;
    }
    std::uint64_t calls = 0;
    bool sawExecutive = false;
    bool sawWorkingMemory = false;
};

class ScanAdvisor final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "fresh scan";
        response.strategies.push_back(CortexStrategy{
            .id = "SCAN_ONLY",
            .kind = CortexStrategyKind::RequestScan,
            .rationale = "Use only host scan capability.",
            .estimatedRisk = 0.01,
            .estimatedBenefit = 0.8,
            .confidence = 0.95,
            .requiredCapabilities = {"SCAN"},
        });
        return response;
    }
    std::uint64_t calls = 0;
};

void phase12ExecutiveMemoryTest(const std::filesystem::path& output) {
    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Hybrid;
    config.topDown.enabled = false;
    auto planner = std::make_shared<ExecutivePlanner>();
    CortexOrchestrator cortex(config, planner);

    RobotMindConfig mindConfig;
    mindConfig.seed = 0x1212U;
    mindConfig.enableIdentity = false;
    RobotMind mind(mindConfig);
    seedMind(mind);

    const auto goalId = cortex.pushGoal(
        "Reach the target while preserving physiological reserve", 0.9, mind.metrics().experiences);
    require(goalId != 0U, "Phase 12 did not create a persistent executive goal");
    cortex.remember(
        "mission_constraint", "avoid unnecessary physiological cost", 0.95,
        mind.metrics().experiences, 1234U);

    const auto before = mind.stateJson();
    CortexSpatialState spatial;
    spatial.environmentId = 12;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.6;

    cortex.requestExecutivePlan(
        mind, nullptr, &spatial, nullptr, {"SCAN", "RECALL_ROUTE"});
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial);

    require(mind.stateJson() == before, "Phase 12 executive planning mutated RobotMind");
    require(planner->sawExecutive && planner->sawWorkingMemory,
            "Phase 12 did not expose bounded executive state/working memory to the Cortex");
    const auto snapshot = cortex.executiveStatus();
    require(!snapshot.goals.empty() && snapshot.goals.front().id == goalId,
            "Phase 12 goal was not retained in executive state");

    const auto statePath = output / "phase12_executive.bin";
    cortex.saveExecutiveState(statePath);
    CortexConfig disabled;
    disabled.enabled = false;
    CortexOrchestrator restored(disabled);
    restored.loadExecutiveState(statePath);
    require(restored.executiveStatus().goals.size() == snapshot.goals.size(),
            "Phase 12 executive snapshot did not restore goals");
}

void phase13GroundedPlanTest() {
    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Hybrid;
    auto planner = std::make_shared<ExecutivePlanner>();
    CortexOrchestrator cortex(config, planner);

    RobotMindConfig mindConfig;
    mindConfig.seed = 0x1313U;
    mindConfig.enableIdentity = false;
    RobotMind mind(mindConfig);
    seedMind(mind);
    (void)cortex.pushGoal("Reach target", 1.0, mind.metrics().experiences);
    cortex.remember("known_rule", "scan before route recall", 0.9, mind.metrics().experiences);

    CortexSpatialState spatial;
    spatial.environmentId = 13;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.5;
    cortex.requestExecutivePlan(
        mind, nullptr, &spatial, nullptr, {"SCAN", "RECALL_ROUTE"});
    waitForWorker(cortex);
    cortex.poll(mind, nullptr, &spatial);

    auto step = cortex.activePlanStep();
    require(step.has_value() && step->id == "STEP_SCAN",
            "Phase 13 did not adopt the validated first long-horizon plan step");

    const auto teaching = cortex.beginTeachingAction(77);
    require(teaching.has_value(), "Phase 13 could not bind accepted strategy to a real action");
    mind.beginAction(ActionEvent{.id = 77, .label = "scan", .intensity = 0.2});
    ActionOutcome outcome{.id = 77, .reward = 0.25, .success = 1.0, .novelty = 0.4};
    mind.endAction(outcome);
    cortex.completeTeachingAction(*teaching, outcome, mind, nullptr, &spatial, nullptr);

    step = cortex.activePlanStep();
    require(step.has_value() && step->id == "STEP_RECALL",
            "Phase 13 advanced plan without preserving ordered grounded steps");
    require(!cortex.executiveStatus().recentRealOutcomes.empty(),
            "Phase 13 did not retain the real ActionOutcome as episodic context");
}

void phase14MetacognitionTest() {
    CortexMetacognitionConfig config;
    config.minimumSamples = 2;
    config.cautiousReliabilityThreshold = 0.7;
    config.degradedReliabilityThreshold = 0.4;
    CortexMetacognition monitor(config);

    monitor.recordTransport(false);
    monitor.recordTransport(false);
    monitor.recordRealOutcome(true, false);
    monitor.recordRealOutcome(true, false);
    const auto degraded = monitor.snapshot();
    require(degraded.state == CortexReliabilityState::Degraded,
            "Phase 14 did not enter DEGRADED after repeated Cortex failures");
    require(!monitor.allowAutomaticConsultation(CortexTaskKind::Plan, CortexTriggerReason::Novelty),
            "Phase 14 DEGRADED state did not suppress automatic consultation");
    require(monitor.allowAutomaticConsultation(CortexTaskKind::Plan, CortexTriggerReason::Explicit),
            "Phase 14 incorrectly blocked explicit human-requested consultation");
}

void phase15RecordReplayAndAutonomousExecutiveTest(const std::filesystem::path& output) {
    const auto tape = output / "phase15_cortex_record.bin";
    std::filesystem::remove(tape);

    RobotMindConfig mindConfig;
    mindConfig.seed = 0x1515U;
    mindConfig.enableIdentity = false;
    RobotMind recordMind(mindConfig);
    seedMind(recordMind);

    CortexSpatialState spatial;
    spatial.environmentId = 15;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.5;

    CortexConfig recordConfig;
    recordConfig.enabled = true;
    recordConfig.mode = CortexMode::Advisor;
    recordConfig.executionMode = CortexExecutionMode::Record;
    recordConfig.recordReplay.path = tape.string();
    auto advisor = std::make_shared<ScanAdvisor>();
    CortexOrchestrator recorder(recordConfig, advisor);
    recorder.requestExplicitAdvice(
        recordMind, nullptr, spatial, {"SCAN"}, "get fresh geometry");
    waitForWorker(recorder);
    recorder.poll(recordMind, nullptr, &spatial);
    require(std::filesystem::exists(tape) && std::filesystem::file_size(tape) > 16U,
            "Phase 15 record mode did not create a Cortex tape");
    require(recorder.status().metrics.recordEntries == 1U,
            "Phase 15 record metric was not incremented");

    RobotMind replayMind(mindConfig);
    seedMind(replayMind);
    CortexConfig replayConfig = recordConfig;
    replayConfig.executionMode = CortexExecutionMode::Replay;
    CortexOrchestrator replay(replayConfig);
    replay.requestExplicitAdvice(
        replayMind, nullptr, spatial, {"SCAN"}, "get fresh geometry");
    waitForWorker(replay);
    replay.poll(replayMind, nullptr, &spatial);
    const auto replayStatus = replay.status();
    require(replayStatus.metrics.replayHits == 1U
            && replayStatus.lastDecision.has_value()
            && replayStatus.lastDecision->kind == CortexDecisionKind::Accept,
            "Phase 15 replay did not reproduce the recorded Cortex decision");

    CortexConfig executiveConfig;
    executiveConfig.enabled = true;
    executiveConfig.mode = CortexMode::Hybrid;
    auto planner = std::make_shared<ExecutivePlanner>();
    CortexOrchestrator executive(executiveConfig, planner);
    RobotMind executiveMind(mindConfig);
    seedMind(executiveMind);
    const auto before = executiveMind.stateJson();
    (void)executive.pushGoal("Autonomously maintain mission progress", 0.8,
                             executiveMind.metrics().experiences);
    executive.remember("mission", "prefer bounded planning", 0.9,
                       executiveMind.metrics().experiences);
    executive.autonomousCycle(
        executiveMind, nullptr, &spatial, nullptr, {"SCAN", "RECALL_ROUTE"});
    waitForWorker(executive);
    executive.poll(executiveMind, nullptr, &spatial);
    require(executiveMind.stateJson() == before,
            "Phase 15 autonomous executive directly mutated RobotMind");
    require(executive.status().metrics.executiveCycles == 1U
            && executive.activePlanStep().has_value(),
            "Phase 15 autonomous executive did not schedule the bounded executive plan");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::temp_directory_path() / "tatarus_cortex_phase12_15";
        std::filesystem::create_directories(output);

        phase12ExecutiveMemoryTest(output);
        phase13GroundedPlanTest();
        phase14MetacognitionTest();
        phase15RecordReplayAndAutonomousExecutiveTest(output);

        std::cout << "PASS cortex_phase12_executive_memory\n"
                  << "PASS cortex_phase13_grounded_long_horizon_plan\n"
                  << "PASS cortex_phase14_metacognition\n"
                  << "PASS cortex_phase15_record_replay_autonomous_executive\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
