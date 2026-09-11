#include <tatarus/sdk.hpp>

#include <iostream>

int main() {
    constexpr tatarus::EnvironmentId marsSector = 2037;
    tatarus::RobotMind rover(tatarus::RobotMindConfig{
        .seed = 7411,
        .neuronCount = 384,
        .microstepsPerObservation = 8,
        .cartography = tatarus::CartographyConfig{
            .voxelSizeMeters = 0.25,
            .maximumRangeMeters = 30.0}});

    for (std::uint64_t step = 0; step < 6; ++step) {
        tatarus::Experience experience;
        experience.timestampNs = (step + 1) * 100'000'000ULL;
        experience.body.battery = 0.85;
        experience.environment.temperature = -35.0;

        tatarus::ScannerFrame scanner;
        scanner.environmentId = marsSector;
        scanner.timestampNs = experience.timestampNs;
        scanner.pose.positionMeters = {static_cast<double>(step), 0.0, 0.0};
        scanner.readings = {
            {.direction = {1.0, 0.0, 0.0}, .distanceMeters = 5.0,
             .maxRangeMeters = 5.0, .confidence = 0.95, .hit = false},
            {.direction = {0.0, 0.0, 1.0}, .distanceMeters = 2.0,
             .maxRangeMeters = 8.0, .confidence = 0.98, .hit = true,
             .semanticLabel = step == 3 ? "water_ice_candidate" : "basalt_ridge"},
            {.direction = {0.0, 0.0, -1.0}, .distanceMeters = 3.5,
             .maxRangeMeters = 8.0, .confidence = 0.92, .hit = true,
             .semanticLabel = "rock"},
        };

        const auto result = rover.observeExplorer(experience, scanner);
        std::cout << "step=" << step
                  << " assembly=" << result.cognition.assemblyId
                  << " mapped=" << result.cartography.summary.mappedVoxels
                  << " occupied=" << result.cartography.summary.occupiedVoxels
                  << " frontier=" << result.cartography.summary.frontierVoxels
                  << " new=" << result.cartography.localNovelty << '\n';
    }

    // This JSON is the persistent, later retrievable map: every voxel contains
    // occupancy evidence and, where available, a semantic sensor finding.
    std::cout << rover.environmentMapJson(marsSector) << '\n';
    rover.saveSnapshot("mars_rover_memory");
    return 0;
}
