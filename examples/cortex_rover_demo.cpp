#include <tatarus/sdk.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    using namespace tatarus;
    using namespace tatarus::cortex;

    RobotMind rover(RobotMindConfig{
        .seed = 7411,
        .neuronCount = 384,
        .microstepsPerObservation = 8,
        .enableIdentity = false,
        .enableCartography = true,
        .cartography = CartographyConfig{
            .voxelSizeMeters = 0.25,
            .maximumRangeMeters = 30.0}});

    Experience experience;
    experience.timestampNs = 100'000'000ULL;
    experience.body.battery = 0.85;
    experience.environment.temperature = -35.0;
    experience.environment.novelty = 0.80;
    experience.vision = {0.1, 0.2, 0.8, 0.1};

    ScannerFrame scanner;
    scanner.environmentId = 2037;
    scanner.timestampNs = experience.timestampNs;
    scanner.pose.positionMeters = {0.0, 0.0, 0.0};
    scanner.readings = {
        {.direction = {1.0, 0.0, 0.0}, .distanceMeters = 5.0,
         .maxRangeMeters = 5.0, .confidence = 0.95, .hit = false},
        {.direction = {0.0, 0.0, 1.0}, .distanceMeters = 2.0,
         .maxRangeMeters = 8.0, .confidence = 0.98, .hit = true,
         .semanticLabel = "basalt_ridge"},
    };

    const ExplorerResult explorer = rover.observeExplorer(experience, scanner);
    RoverCortexAdapter adapter;
    const CortexSpatialState spatial = adapter.spatialState(explorer);

    CortexConfig config;
    config.enabled = true;
    config.mode = CortexMode::Advisor;
    CortexOrchestrator cortex(config);

    cortex.requestExplicitAdvice(
        rover,
        nullptr,
        spatial,
        RoverCortexAdapter::capabilities(),
        "Reach the target while preserving organism safety and learned route knowledge.");

    // This demo waits only because it is a command-line example. A productive
    // rover loop should continue stepping TATARUS and poll asynchronously.
    for (int i = 0; i < 5000 && !cortex.idle(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cortex.poll(rover, nullptr, &spatial);

    const CortexObservation status = cortex.status();
    if (!status.lastDecision.has_value()) {
        std::cerr << "No actionable Cortex decision: " << status.lastError << '\n';
        return 2;
    }

    const RoverDirective directive = adapter.translate(*status.lastDecision, explorer);
    std::cout << "arbiter=" << toString(status.lastDecision->kind)
              << " directive=" << toString(directive.kind) << '\n';

    if (directive.neuralDirection.has_value()) {
        std::cout << "TATARUS neural direction=" << *directive.neuralDirection << '\n';
    }

    // Deliberately no motor is executed here. The host remains responsible for
    // independent rover safety checks and for feeding the real consequence back
    // through RobotMind::beginAction/endAction.
    return 0;
}
