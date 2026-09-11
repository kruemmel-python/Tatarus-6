#include "tatarus/cortex.hpp"

#include <iostream>

using namespace tatarus;
using namespace tatarus::cortex;

namespace {

Experience observation(std::uint64_t step, double reward) {
    Experience value;
    value.timestampNs = step * 50'000'000ULL;
    value.vision = {0.2, 0.1, 0.7, 0.1};
    value.touch = {0.0, 0.0, 0.9, 0.0};
    value.environment.light = 0.7;
    value.environment.temperature = 18.0;
    value.environment.novelty = 0.4;
    value.body.battery = 0.9;
    value.reward = reward;
    return value;
}

} // namespace

int main() {
    organism::OrganismConfig config;
    config.mind.seed = 8909;
    config.mind.neuronCount = 96;
    config.mind.enableIdentity = false;
    organism::SyntheticOrganism real(config);
    organism::AtmosphericEnvironment earth;

    for (std::uint64_t step = 1; step <= 10; ++step) {
        (void)real.step(observation(step, 0.0), earth);
    }

    CounterfactualScenario routeA;
    routeA.id = "route-a";
    routeA.strategyId = "FOLLOW_KNOWN_ROUTE";
    routeA.actionId = 1;
    for (std::uint64_t step = 11; step <= 15; ++step) {
        CounterfactualFrame frame;
        frame.experience = observation(step, 0.10);
        frame.atmosphere = earth;
        routeA.frames.push_back(frame);
    }
    routeA.terminalOutcome = ActionOutcome{.id = 1, .reward = 0.4, .success = 1.0, .novelty = 0.2};

    CounterfactualScenario routeB = routeA;
    routeB.id = "route-b";
    routeB.strategyId = "EXPLORE_FRONTIER";
    routeB.actionId = 2;
    routeB.terminalOutcome = ActionOutcome{.id = 2, .reward = -0.2, .success = 0.0, .novelty = 0.8};
    for (auto& frame : routeB.frames) {
        frame.experience.reward = -0.05;
        frame.usePhysicalLoad = true;
        frame.load.mechanicalPowerW = 120.0;
    }

    CounterfactualSandbox sandbox;
    const auto comparison = sandbox.evaluate(real, {routeA, routeB});
    std::cout << "Sandbox isolation: " << (comparison.isolationVerified ? "PASS" : "FAIL") << '\n';
    for (const auto& branch : comparison.branches) {
        std::cout << branch.id << " utility=" << branch.utility
                  << " distress=" << branch.final.visceralDistress << '\n';
    }

    CortexLearningConfig learning;
    learning.minimumTeachingSuccesses = 1;
    learning.minimumAutonomousTrials = 1;
    CortexTeacherTransfer teacher(learning);

    CortexRequest request;
    request.requestId = 1;
    request.stateFingerprint = 1234;
    request.task = CortexTaskKind::Plan;
    request.spatial.environmentId = 7;
    request.spatial.mapAvailable = true;
    request.spatial.frontierRatio = 0.7;
    request.neural.novelty = 0.7;

    CortexDecision decision;
    decision.kind = CortexDecisionKind::Accept;
    decision.selectedStrategy = CortexStrategy{
        .id = "TEACH_ROUTE_A",
        .kind = CortexStrategyKind::FollowKnownRoute,
        .rationale = "example",
        .estimatedRisk = 0.1,
        .estimatedBenefit = 0.8,
        .confidence = 0.9,
    };

    const auto handle = teacher.beginCortexAssisted(request, decision, 1);
    real.mind().beginAction(ActionEvent{.id = 1, .label = "real-route-a", .intensity = 1.0});
    const ActionOutcome realOutcome{.id = 1, .reward = 0.4, .success = 1.0, .novelty = 0.2};
    real.mind().endAction(realOutcome); // the only real learning path
    const auto postTelemetry = real.telemetry();
    teacher.completeRealOutcome(handle, realOutcome, real.mind(), &postTelemetry);

    const auto learningState = teacher.snapshot();
    std::cout << "Teacher episodes completed: " << learningState.episodesCompleted << '\n';
    return comparison.isolationVerified ? 0 : 1;
}
