#pragma once

#include "tatarus/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tatarus {

using EnvironmentId = std::uint64_t;

// Sparse 3-D occupancy memory. A rover normally keeps y close to zero while a
// drone can use all three axes; both therefore share the same map format.
enum class OccupancyState : std::uint8_t {
    Unknown,
    Free,
    Occupied
};

struct CartographyConfig {
    double voxelSizeMeters = 0.50;
    double maximumRangeMeters = 250.0;
    std::size_t maximumVoxelsPerEnvironment = 1'000'000;
    double freeEvidence = 0.70;
    double occupiedEvidence = 0.90;
    double freeProbabilityThreshold = 0.35;
    double occupiedProbabilityThreshold = 0.65;
};

struct Pose3D {
    std::array<double, 3> positionMeters{};
    // Sensor-to-world quaternion in x, y, z, w order.
    std::array<double, 4> orientationQuaternion{0.0, 0.0, 0.0, 1.0};
};

struct RangeReading {
    // Unit direction in the scanner/sensor frame. It is normalized by the SDK.
    std::array<double, 3> direction{1.0, 0.0, 0.0};
    double distanceMeters = 0.0;
    double maxRangeMeters = 0.0;
    double confidence = 1.0;
    bool hit = false;
    // Optional classifier result, for example "rock", "door" or "water_ice".
    std::string semanticLabel;
};

struct ScannerFrame {
    EnvironmentId environmentId = 1;
    TimestampNs timestampNs = 0;
    Pose3D pose;
    std::vector<RangeReading> readings;
};

struct VoxelIndex {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    friend bool operator==(const VoxelIndex&, const VoxelIndex&) = default;
};

struct MapVoxel {
    VoxelIndex index;
    std::array<double, 3> centerMeters{};
    OccupancyState state = OccupancyState::Unknown;
    double occupancyProbability = 0.5;
    std::uint32_t observations = 0;
    std::uint32_t freeObservations = 0;
    std::uint32_t occupiedObservations = 0;
    TimestampNs lastObservedNs = 0;
    std::string semanticLabel;
    double semanticConfidence = 0.0;
};

struct CartographySummary {
    bool available = false;
    EnvironmentId environmentId = 0;
    double voxelSizeMeters = 0.0;
    std::uint64_t integratedScans = 0;
    std::uint64_t integratedRays = 0;
    std::uint64_t mappedVoxels = 0;
    std::uint64_t freeVoxels = 0;
    std::uint64_t occupiedVoxels = 0;
    std::uint64_t uncertainVoxels = 0;
    std::uint64_t frontierVoxels = 0;
    VoxelIndex minimumIndex;
    VoxelIndex maximumIndex;
    Pose3D lastPose;
    TimestampNs lastTimestampNs = 0;
};

struct CartographyUpdate {
    EnvironmentId environmentId = 0;
    std::uint64_t acceptedRays = 0;
    std::uint64_t rejectedRays = 0;
    std::uint64_t touchedVoxels = 0;
    std::uint64_t newlyMappedVoxels = 0;
    std::uint64_t occupiedEndpoints = 0;
    double localNovelty = 0.0;
    double frontierRatio = 0.0;
    // Scanner-local proximity for +x, -x, +y, -y, +z, -z.
    std::array<double, 6> obstacleProximity{};
    CartographySummary summary;
};

struct ExplorerResult {
    ObserveResult cognition;
    CartographyUpdate cartography;
};

} // namespace tatarus
