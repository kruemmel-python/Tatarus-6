#include "tatarus/cortex.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using namespace tatarus;
using namespace tatarus::cortex;

namespace {

class DemoPlanner final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        response.summary = "demo bounded executive plan";
        response.strategies.push_back(CortexStrategy{
            .id = "SCAN_FIRST",
            .kind = CortexStrategyKind::RequestScan,
            .rationale = "Acquire current geometry before committing to a route.",
            .estimatedRisk = 0.02,
            .estimatedBenefit = 0.85,
            .confidence = 0.95,
            .requiredCapabilities = {"SCAN"},
        });
        if (request.task == CortexTaskKind::ExecutivePlan) {
            response.plan = CortexPlanProposal{
                .id = "MISSION_PLAN",
                .steps = {
                    CortexPlanStep{
                        .id = "SCAN",
                        .kind = CortexStrategyKind::RequestScan,
                        .objective = "Acquire fresh geometry",
                        .requiredCapabilities = {"SCAN"},
                    },
                    CortexPlanStep{
                        .id = "RECALL",
                        .kind = CortexStrategyKind::RecallRoute,
                        .objective = "Recall a grounded route",
                        .requiredCapabilities = {"RECALL_ROUTE"},
                    },
                },
            };
        }
        return response;
    }
};

void wait(CortexOrchestrator& cortex) {
    while (!cortex.idle()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

Experience observation(std::uint64_t step) {
    Experience e;
    e.timestampNs = step * 50'000'000ULL;
    e.vision = {0.2, 0.8, 0.1, 0.0};
    e.environment.novelty = 0.8;
    e.body.battery = 0.95;
    return e;
}

} // namespace

int main() {
    RobotMindConfig mindConfig;
    mindConfig.seed = 1215;
    mindConfig.enableIdentity = false;
    RobotMind mind(mindConfig);
    for (std::uint64_t i = 1; i <= 12; ++i) (void)mind.observe(observation(i));

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Hybrid;
    auto planner = std::make_shared<DemoPlanner>();
    CortexOrchestrator cortex(config, planner);

    const auto goalId = cortex.pushGoal(
        "Reach the mission target while preserving internal reserve", 0.9,
        mind.metrics().experiences);
    cortex.remember(
        "constraint", "use only grounded capabilities", 0.95,
        mind.metrics().experiences);

    CortexSpatialState spatial;
    spatial.environmentId = 1215;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.6;

    const auto before = mind.stateJson();
    cortex.autonomousCycle(
        mind, nullptr, &spatial, nullptr, {"SCAN", "RECALL_ROUTE"});
    wait(cortex);
    cortex.poll(mind, nullptr, &spatial);

    const auto step = cortex.activePlanStep();
    std::cout << "Goal id: " << goalId << '\n';
    std::cout << "Active plan step: " << (step ? step->id : "NONE") << '\n';
    std::cout << "RobotMind unchanged by executive planning: "
              << (before == mind.stateJson() ? "YES" : "NO") << '\n';
    std::cout << "Metacognition: " << toString(cortex.metacognitionStatus().state) << '\n';
    return 0;
}
