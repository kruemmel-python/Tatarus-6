#include "tatarus/cortex.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace tatarus;
using namespace tatarus::cortex;

namespace {

Experience sample(std::uint64_t step) {
    Experience experience;
    experience.timestampNs = step * 50'000'000ULL;
    experience.vision = {0.2, 0.7, 0.1, 0.0};
    experience.environment.novelty = 0.8;
    experience.body.battery = 0.95;
    return experience;
}

class DemoCortex final : public ICortexModelClient {
public:
    CortexResponse complete(const CortexRequest& request) override {
        ++calls;
        CortexResponse response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        if (request.task == CortexTaskKind::Dream) {
            response.strategies.push_back(CortexStrategy{
                .id = "DREAM_COMPOSE",
                .kind = CortexStrategyKind::Compose,
                .rationale = "Recombine existing TATARUS symbols only.",
                .estimatedRisk = 0.01,
                .estimatedBenefit = 0.7,
                .confidence = 0.9,
                .requiredCapabilities = {"COMPOSE"},
            });
            response.imagination = ImaginationDirective{
                .mode = "COMPOSE",
                .symbols = {"HOUSE", "TREE"},
                .conceptText = "REM association",
            };
        } else {
            response.strategies.push_back(CortexStrategy{
                .id = "EXPLORE",
                .kind = CortexStrategyKind::ExploreFrontier,
                .rationale = "Use the host frontier capability; direction remains neural.",
                .estimatedRisk = 0.1,
                .estimatedBenefit = 0.8,
                .confidence = 0.9,
                .requiredCapabilities = {"EXPLORE_FRONTIER"},
            });
        }
        return response;
    }
    std::uint64_t calls = 0;
};

void wait(CortexOrchestrator& cortex) {
    for (int i = 0; i < 2000 && !cortex.idle(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

int main() {
    RobotMindConfig mindConfig;
    mindConfig.enableIdentity = false;
    mindConfig.enableCartography = false;
    RobotMind mind(mindConfig);
    for (std::uint64_t step = 1; step <= 8; ++step) (void)mind.observe(sample(step));

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Hybrid;
    config.topDown.enabled = true;
    config.dream.enabled = true;
    config.dream.minimumRemIntervalSteps = 1;
    auto model = std::make_shared<DemoCortex>();
    CortexOrchestrator cortex(config, model);

    CortexSpatialState spatial;
    spatial.environmentId = 1;
    spatial.mapAvailable = true;
    spatial.localNovelty = 0.8;
    spatial.frontierRatio = 0.8;

    cortex.requestExplicitAdvice(
        mind, nullptr, spatial,
        {"EXPLORE_FRONTIER"},
        "reach the target without direct motor control");
    wait(cortex);
    cortex.poll(mind, nullptr, &spatial);
    const auto cue = cortex.prepareTopDownCue();
    std::cout << "Phase 10 cue ready: " << (cue.ready ? "YES" : "NO") << '\n';
    const auto applied = cortex.applyTopDownCue(mind, sample(9), nullptr, &spatial);
    std::cout << "Phase 10 applied: " << (applied.has_value() ? "YES" : "NO") << '\n';

    RobotMindConfig sleepConfig = mindConfig;
    sleepConfig.seed = 0xD11EAU;
    sleepConfig.sleepTiming.circadianCycleMs = 200.0;
    sleepConfig.sleepTiming.sleepPressureTauMs = 45.0;
    sleepConfig.sleepTiming.nremMinimumMs = 20.0;
    sleepConfig.sleepTiming.remMinimumMs = 30.0;
    RobotMind sleepMind(sleepConfig);
    while (sleepMind.physiology().sleepPhase == SleepPhase::Wake) sleepMind.rest(1);
    CortexImaginationState imagination;
    imagination.available = true;
    imagination.visualEngrams = 2;
    imagination.symbolEngrams = 2;
    imagination.knownSymbols = {"HOUSE", "TREE"};

    const auto callsBeforeNrem = model->calls;
    cortex.sleepCycle(sleepMind, nullptr, imagination, {"COMPOSE"}, "sleep consolidation");
    std::cout << "NREM additional model calls: " << (model->calls - callsBeforeNrem) << '\n';

    while (sleepMind.physiology().sleepPhase != SleepPhase::Rem) sleepMind.rest(1);
    cortex.sleepCycle(sleepMind, nullptr, imagination, {"SCAN", "COMPOSE"}, "REM dream");
    wait(cortex);
    cortex.poll(sleepMind, nullptr, nullptr, &imagination);
    const auto status = cortex.status();
    std::cout << "REM dream requests: " << status.metrics.remDreamRequests << '\n';
    std::cout << "Dream routed to IMAGINATIO: "
              << (status.lastDecision.has_value()
                  && status.lastDecision->kind == CortexDecisionKind::SendToImagination
                  ? "YES" : "NO") << '\n';
    return 0;
}
