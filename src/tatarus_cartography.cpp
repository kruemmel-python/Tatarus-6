#include "tatarus_cartography.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tatarus::detail {
namespace {

constexpr char kMapMagic[8] = {'T', 'C', 'M', 'A', 'P', '0', '1', '\0'};

struct VoxelHash {
    std::size_t operator()(const VoxelIndex& value) const noexcept {
        std::size_t hash = std::hash<std::int32_t>{}(value.x);
        const auto mix = [&hash](std::int32_t component) {
            const auto next = std::hash<std::int32_t>{}(component);
            hash ^= next + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
        };
        mix(value.y);
        mix(value.z);
        return hash;
    }
};

struct CellRecord {
    double logOdds = 0.0;
    std::uint32_t observations = 0;
    std::uint32_t freeObservations = 0;
    std::uint32_t occupiedObservations = 0;
    TimestampNs lastObservedNs = 0;
    std::string semanticLabel;
    double semanticConfidence = 0.0;
};

struct EnvironmentRecord {
    std::unordered_map<VoxelIndex, CellRecord, VoxelHash> cells;
    std::uint64_t integratedScans = 0;
    std::uint64_t integratedRays = 0;
    Pose3D lastPose;
    TimestampNs lastTimestampNs = 0;
};

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) throw std::runtime_error("TATARUS cartography snapshot write failed");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("TATARUS cartography snapshot is truncated");
}

void writeString(std::ostream& output, const std::string& value) {
    const auto size = static_cast<std::uint64_t>(value.size());
    writePod(output, size);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("TATARUS cartography label write failed");
}

std::string readString(std::istream& input) {
    std::uint64_t size = 0;
    readPod(input, size);
    if (size > 4096U) throw std::runtime_error("Cartography snapshot label is implausibly large");
    std::string value(static_cast<std::size_t>(size), '\0');
    input.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!input) throw std::runtime_error("TATARUS cartography label is truncated");
    return value;
}

bool finiteVector(const std::array<double, 3>& value) {
    return std::all_of(value.begin(), value.end(), [](double v) { return std::isfinite(v); });
}

bool finiteQuaternion(const std::array<double, 4>& value) {
    return std::all_of(value.begin(), value.end(), [](double v) { return std::isfinite(v); });
}

double clamp01(double value) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

std::array<double, 3> normalized(const std::array<double, 3>& value) {
    const double length = std::sqrt(
        value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!std::isfinite(length) || length < 1.0e-12) return {};
    return {value[0] / length, value[1] / length, value[2] / length};
}

std::array<double, 3> rotateByQuaternion(
    const std::array<double, 3>& vector,
    const std::array<double, 4>& quaternion) {
    const double norm = std::sqrt(
        quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1]
        + quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
    if (!std::isfinite(norm) || norm < 1.0e-12) return vector;
    const double x = quaternion[0] / norm;
    const double y = quaternion[1] / norm;
    const double z = quaternion[2] / norm;
    const double w = quaternion[3] / norm;
    const std::array<double, 3> q{x, y, z};
    const std::array<double, 3> cross{
        q[1] * vector[2] - q[2] * vector[1],
        q[2] * vector[0] - q[0] * vector[2],
        q[0] * vector[1] - q[1] * vector[0]};
    const std::array<double, 3> secondCross{
        q[1] * cross[2] - q[2] * cross[1],
        q[2] * cross[0] - q[0] * cross[2],
        q[0] * cross[1] - q[1] * cross[0]};
    return {
        vector[0] + 2.0 * (w * cross[0] + secondCross[0]),
        vector[1] + 2.0 * (w * cross[1] + secondCross[1]),
        vector[2] + 2.0 * (w * cross[2] + secondCross[2])};
}

std::int32_t voxelCoordinate(double position, double voxelSize) {
    const double coordinate = std::floor(position / voxelSize);
    if (coordinate < static_cast<double>(std::numeric_limits<std::int32_t>::min())
        || coordinate > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw std::out_of_range("Scanner position exceeds cartography index range");
    }
    return static_cast<std::int32_t>(coordinate);
}

VoxelIndex voxelIndex(const std::array<double, 3>& point, double voxelSize) {
    return {
        voxelCoordinate(point[0], voxelSize),
        voxelCoordinate(point[1], voxelSize),
        voxelCoordinate(point[2], voxelSize)};
}

double probability(double logOdds) {
    return 1.0 / (1.0 + std::exp(-std::clamp(logOdds, -20.0, 20.0)));
}

OccupancyState classify(const CellRecord& cell, const CartographyConfig& config) {
    const double p = probability(cell.logOdds);
    if (p <= config.freeProbabilityThreshold) return OccupancyState::Free;
    if (p >= config.occupiedProbabilityThreshold) return OccupancyState::Occupied;
    return OccupancyState::Unknown;
}

const char* stateName(OccupancyState state) {
    switch (state) {
        case OccupancyState::Free: return "free";
        case OccupancyState::Occupied: return "occupied";
        case OccupancyState::Unknown: return "uncertain";
    }
    return "uncertain";
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(ch) << std::dec;
                } else {
                    output << static_cast<char>(ch);
                }
        }
    }
    return output.str();
}

bool indexLess(const VoxelIndex& left, const VoxelIndex& right) {
    if (left.x != right.x) return left.x < right.x;
    if (left.y != right.y) return left.y < right.y;
    return left.z < right.z;
}

std::vector<VoxelIndex> traceRay(
    const std::array<double, 3>& origin,
    const std::array<double, 3>& direction,
    double distance,
    double voxelSize) {
    const auto stepCount = std::max<std::size_t>(
        1U, static_cast<std::size_t>(std::ceil(distance / (0.45 * voxelSize))));
    std::vector<VoxelIndex> result;
    result.reserve(stepCount + 1U);
    for (std::size_t step = 0; step <= stepCount; ++step) {
        const double along = distance * static_cast<double>(step)
            / static_cast<double>(stepCount);
        const VoxelIndex index = voxelIndex({
            origin[0] + direction[0] * along,
            origin[1] + direction[1] * along,
            origin[2] + direction[2] * along}, voxelSize);
        if (result.empty() || !(result.back() == index)) result.push_back(index);
    }
    return result;
}

std::size_t proximityBin(const std::array<double, 3>& direction) {
    std::size_t axis = 0;
    if (std::abs(direction[1]) > std::abs(direction[axis])) axis = 1;
    if (std::abs(direction[2]) > std::abs(direction[axis])) axis = 2;
    return axis * 2U + (direction[axis] < 0.0 ? 1U : 0U);
}

} // namespace

class EnvironmentCartographer::Impl {
public:
    explicit Impl(CartographyConfig value) : config(std::move(value)) {
        if (!std::isfinite(config.voxelSizeMeters) || config.voxelSizeMeters <= 0.0) {
            throw std::invalid_argument("voxelSizeMeters must be finite and positive");
        }
        if (!std::isfinite(config.maximumRangeMeters) || config.maximumRangeMeters <= 0.0) {
            throw std::invalid_argument("maximumRangeMeters must be finite and positive");
        }
        if (config.maximumVoxelsPerEnvironment == 0) {
            throw std::invalid_argument("maximumVoxelsPerEnvironment must be positive");
        }
        if (!(config.freeProbabilityThreshold > 0.0
                && config.freeProbabilityThreshold < 0.5
                && config.occupiedProbabilityThreshold > 0.5
                && config.occupiedProbabilityThreshold < 1.0)) {
            throw std::invalid_argument("cartography probability thresholds are invalid");
        }
        if (!(config.freeEvidence > 0.0) || !(config.occupiedEvidence > 0.0)) {
            throw std::invalid_argument("cartography evidence gains must be positive");
        }
    }

    MapVoxel publicVoxel(const VoxelIndex& index, const CellRecord& cell) const {
        return MapVoxel{
            .index = index,
            .centerMeters = {
                (static_cast<double>(index.x) + 0.5) * config.voxelSizeMeters,
                (static_cast<double>(index.y) + 0.5) * config.voxelSizeMeters,
                (static_cast<double>(index.z) + 0.5) * config.voxelSizeMeters},
            .state = classify(cell, config),
            .occupancyProbability = probability(cell.logOdds),
            .observations = cell.observations,
            .freeObservations = cell.freeObservations,
            .occupiedObservations = cell.occupiedObservations,
            .lastObservedNs = cell.lastObservedNs,
            .semanticLabel = cell.semanticLabel,
            .semanticConfidence = cell.semanticConfidence};
    }

    CartographySummary summary(EnvironmentId environmentId) const {
        CartographySummary result;
        result.environmentId = environmentId;
        result.voxelSizeMeters = config.voxelSizeMeters;
        const auto environment = environments.find(environmentId);
        if (environment == environments.end()) return result;

        result.available = true;
        result.integratedScans = environment->second.integratedScans;
        result.integratedRays = environment->second.integratedRays;
        result.mappedVoxels = environment->second.cells.size();
        result.lastPose = environment->second.lastPose;
        result.lastTimestampNs = environment->second.lastTimestampNs;

        bool first = true;
        for (const auto& [index, cell] : environment->second.cells) {
            switch (classify(cell, config)) {
                case OccupancyState::Free: ++result.freeVoxels; break;
                case OccupancyState::Occupied: ++result.occupiedVoxels; break;
                case OccupancyState::Unknown: ++result.uncertainVoxels; break;
            }
            if (first) {
                result.minimumIndex = index;
                result.maximumIndex = index;
                first = false;
            } else {
                result.minimumIndex.x = std::min(result.minimumIndex.x, index.x);
                result.minimumIndex.y = std::min(result.minimumIndex.y, index.y);
                result.minimumIndex.z = std::min(result.minimumIndex.z, index.z);
                result.maximumIndex.x = std::max(result.maximumIndex.x, index.x);
                result.maximumIndex.y = std::max(result.maximumIndex.y, index.y);
                result.maximumIndex.z = std::max(result.maximumIndex.z, index.z);
            }
        }

        // A single-height map is a rover surface map, so vertical unknown space
        // must not turn every traversable cell into a frontier. Once scans span
        // several heights (for example on a drone), all six faces participate.
        constexpr std::array<VoxelIndex, 6> neighbors{{
            {1, 0, 0}, {-1, 0, 0}, {0, 0, 1},
            {0, 0, -1}, {0, 1, 0}, {0, -1, 0}}};
        const std::size_t neighborCount = result.minimumIndex.y == result.maximumIndex.y
            ? 4U : neighbors.size();
        for (const auto& [index, cell] : environment->second.cells) {
            if (classify(cell, config) != OccupancyState::Free) continue;
            const bool frontier = std::any_of(neighbors.begin(), neighbors.begin() + neighborCount, [&](const auto& offset) {
                return environment->second.cells.find(VoxelIndex{
                    index.x + offset.x, index.y + offset.y, index.z + offset.z})
                    == environment->second.cells.end();
            });
            if (frontier) ++result.frontierVoxels;
        }
        return result;
    }

    CartographyConfig config;
    std::unordered_map<EnvironmentId, EnvironmentRecord> environments;
};

EnvironmentCartographer::EnvironmentCartographer(CartographyConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

EnvironmentCartographer::~EnvironmentCartographer() = default;
EnvironmentCartographer::EnvironmentCartographer(EnvironmentCartographer&&) noexcept = default;
EnvironmentCartographer& EnvironmentCartographer::operator=(EnvironmentCartographer&&) noexcept = default;

CartographyUpdate EnvironmentCartographer::integrate(const ScannerFrame& frame) {
    if (frame.environmentId == 0) throw std::invalid_argument("environmentId must be non-zero");
    if (!finiteVector(frame.pose.positionMeters)
        || !finiteQuaternion(frame.pose.orientationQuaternion)) {
        throw std::invalid_argument("scanner pose contains a non-finite value");
    }

    auto& environment = impl_->environments[frame.environmentId];
    ++environment.integratedScans;
    environment.lastPose = frame.pose;
    environment.lastTimestampNs = frame.timestampNs;

    CartographyUpdate update;
    update.environmentId = frame.environmentId;
    std::unordered_set<VoxelIndex, VoxelHash> touched;

    for (const auto& reading : frame.readings) {
        const auto sensorDirection = normalized(reading.direction);
        const double configuredRange = reading.maxRangeMeters > 0.0
            ? std::min(reading.maxRangeMeters, impl_->config.maximumRangeMeters)
            : impl_->config.maximumRangeMeters;
        const double distance = std::clamp(reading.distanceMeters, 0.0, configuredRange);
        if (!finiteVector(reading.direction) || sensorDirection == std::array<double, 3>{}
            || !std::isfinite(reading.distanceMeters) || distance <= 0.0
            || !std::isfinite(reading.maxRangeMeters) || reading.maxRangeMeters < 0.0) {
            ++update.rejectedRays;
            continue;
        }

        const auto worldDirection = normalized(rotateByQuaternion(
            sensorDirection, frame.pose.orientationQuaternion));
        const auto ray = traceRay(
            frame.pose.positionMeters, worldDirection, distance, impl_->config.voxelSizeMeters);
        if (ray.empty()) {
            ++update.rejectedRays;
            continue;
        }

        ++update.acceptedRays;
        ++environment.integratedRays;
        const double confidence = clamp01(reading.confidence);
        if (reading.hit) {
            ++update.occupiedEndpoints;
            const double closeness = configuredRange > 0.0
                ? clamp01(1.0 - distance / configuredRange) : 0.0;
            const auto bin = proximityBin(sensorDirection);
            update.obstacleProximity[bin] = std::max(
                update.obstacleProximity[bin], closeness * confidence);
        }

        for (std::size_t i = 0; i < ray.size(); ++i) {
            const bool occupiedEndpoint = reading.hit && i + 1U == ray.size();
            auto found = environment.cells.find(ray[i]);
            if (found == environment.cells.end()) {
                if (environment.cells.size() >= impl_->config.maximumVoxelsPerEnvironment) continue;
                found = environment.cells.emplace(ray[i], CellRecord{}).first;
                ++update.newlyMappedVoxels;
            }
            touched.insert(ray[i]);
            auto& cell = found->second;
            cell.observations += cell.observations < std::numeric_limits<std::uint32_t>::max() ? 1U : 0U;
            cell.lastObservedNs = frame.timestampNs;
            if (occupiedEndpoint) {
                cell.occupiedObservations += cell.occupiedObservations < std::numeric_limits<std::uint32_t>::max() ? 1U : 0U;
                cell.logOdds = std::min(8.0,
                    cell.logOdds + impl_->config.occupiedEvidence * confidence);
                if (!reading.semanticLabel.empty()
                    && (cell.semanticLabel.empty() || confidence >= cell.semanticConfidence)) {
                    cell.semanticLabel = reading.semanticLabel.substr(0, 4096U);
                    cell.semanticConfidence = confidence;
                }
            } else {
                cell.freeObservations += cell.freeObservations < std::numeric_limits<std::uint32_t>::max() ? 1U : 0U;
                cell.logOdds = std::max(-8.0,
                    cell.logOdds - impl_->config.freeEvidence * confidence);
            }
        }
    }

    update.touchedVoxels = touched.size();
    update.localNovelty = update.touchedVoxels > 0
        ? static_cast<double>(update.newlyMappedVoxels)
            / static_cast<double>(update.touchedVoxels)
        : 0.0;
    update.summary = impl_->summary(frame.environmentId);
    update.frontierRatio = update.summary.freeVoxels > 0
        ? static_cast<double>(update.summary.frontierVoxels)
            / static_cast<double>(update.summary.freeVoxels)
        : 0.0;
    return update;
}

CartographySummary EnvironmentCartographer::summary(EnvironmentId environmentId) const {
    return impl_->summary(environmentId);
}

std::optional<MapVoxel> EnvironmentCartographer::voxelAt(
    EnvironmentId environmentId,
    const std::array<double, 3>& worldPositionMeters) const {
    if (!finiteVector(worldPositionMeters)) {
        throw std::invalid_argument("map query contains a non-finite position");
    }
    const auto environment = impl_->environments.find(environmentId);
    if (environment == impl_->environments.end()) return std::nullopt;
    const auto index = voxelIndex(worldPositionMeters, impl_->config.voxelSizeMeters);
    const auto cell = environment->second.cells.find(index);
    if (cell == environment->second.cells.end()) return std::nullopt;
    return impl_->publicVoxel(index, cell->second);
}

std::vector<MapVoxel> EnvironmentCartographer::voxels(EnvironmentId environmentId) const {
    std::vector<MapVoxel> result;
    const auto environment = impl_->environments.find(environmentId);
    if (environment == impl_->environments.end()) return result;
    result.reserve(environment->second.cells.size());
    for (const auto& [index, cell] : environment->second.cells) {
        result.push_back(impl_->publicVoxel(index, cell));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return indexLess(left.index, right.index);
    });
    return result;
}

std::string EnvironmentCartographer::json(EnvironmentId environmentId) const {
    const auto mapSummary = summary(environmentId);
    const auto mapVoxels = voxels(environmentId);
    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    output << "{\"schema\":\"tatarus-environment-map-v1\""
           << ",\"environment_id\":" << environmentId
           << ",\"available\":" << (mapSummary.available ? "true" : "false")
           << ",\"voxel_size_m\":" << mapSummary.voxelSizeMeters
           << ",\"summary\":{"
           << "\"scans\":" << mapSummary.integratedScans
           << ",\"rays\":" << mapSummary.integratedRays
           << ",\"mapped\":" << mapSummary.mappedVoxels
           << ",\"free\":" << mapSummary.freeVoxels
           << ",\"occupied\":" << mapSummary.occupiedVoxels
           << ",\"uncertain\":" << mapSummary.uncertainVoxels
           << ",\"frontiers\":" << mapSummary.frontierVoxels
           << ",\"last_timestamp_ns\":" << mapSummary.lastTimestampNs
           << ",\"bounds\":{\"min\":[" << mapSummary.minimumIndex.x << ','
           << mapSummary.minimumIndex.y << ',' << mapSummary.minimumIndex.z
           << "],\"max\":[" << mapSummary.maximumIndex.x << ','
           << mapSummary.maximumIndex.y << ',' << mapSummary.maximumIndex.z << "]}"
           << ",\"last_pose_m\":[" << mapSummary.lastPose.positionMeters[0] << ','
           << mapSummary.lastPose.positionMeters[1] << ','
           << mapSummary.lastPose.positionMeters[2] << "]}"
           << ",\"voxels\":[";
    for (std::size_t i = 0; i < mapVoxels.size(); ++i) {
        const auto& voxel = mapVoxels[i];
        if (i != 0) output << ',';
        output << "{\"index\":[" << voxel.index.x << ',' << voxel.index.y << ','
               << voxel.index.z << "],\"center_m\":[" << voxel.centerMeters[0] << ','
               << voxel.centerMeters[1] << ',' << voxel.centerMeters[2] << ']'
               << ",\"state\":\"" << stateName(voxel.state) << "\""
               << ",\"occupancy\":" << voxel.occupancyProbability
               << ",\"observations\":" << voxel.observations
               << ",\"free_observations\":" << voxel.freeObservations
               << ",\"occupied_observations\":" << voxel.occupiedObservations
               << ",\"last_observed_ns\":" << voxel.lastObservedNs;
        if (!voxel.semanticLabel.empty()) {
            output << ",\"semantic_label\":\"" << jsonEscape(voxel.semanticLabel) << "\""
                   << ",\"semantic_confidence\":" << voxel.semanticConfidence;
        }
        output << '}';
    }
    output << "]}";
    return output.str();
}

void EnvironmentCartographer::clear(EnvironmentId environmentId) {
    impl_->environments.erase(environmentId);
}

void EnvironmentCartographer::clearAll() {
    impl_->environments.clear();
}

void EnvironmentCartographer::save(const std::filesystem::path& path) const {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create cartography snapshot");
    output.write(kMapMagic, sizeof(kMapMagic));
    writePod(output, impl_->config.voxelSizeMeters);
    const auto environmentCount = static_cast<std::uint64_t>(impl_->environments.size());
    writePod(output, environmentCount);

    std::vector<EnvironmentId> environmentIds;
    environmentIds.reserve(impl_->environments.size());
    for (const auto& [id, ignored] : impl_->environments) {
        static_cast<void>(ignored);
        environmentIds.push_back(id);
    }
    std::sort(environmentIds.begin(), environmentIds.end());
    for (const auto id : environmentIds) {
        const auto& environment = impl_->environments.at(id);
        writePod(output, id);
        writePod(output, environment.integratedScans);
        writePod(output, environment.integratedRays);
        for (const double value : environment.lastPose.positionMeters) writePod(output, value);
        for (const double value : environment.lastPose.orientationQuaternion) writePod(output, value);
        writePod(output, environment.lastTimestampNs);

        std::vector<VoxelIndex> indices;
        indices.reserve(environment.cells.size());
        for (const auto& [index, ignored] : environment.cells) {
            static_cast<void>(ignored);
            indices.push_back(index);
        }
        std::sort(indices.begin(), indices.end(), indexLess);
        const auto cellCount = static_cast<std::uint64_t>(indices.size());
        writePod(output, cellCount);
        for (const auto& index : indices) {
            const auto& cell = environment.cells.at(index);
            writePod(output, index.x);
            writePod(output, index.y);
            writePod(output, index.z);
            writePod(output, cell.logOdds);
            writePod(output, cell.observations);
            writePod(output, cell.freeObservations);
            writePod(output, cell.occupiedObservations);
            writePod(output, cell.lastObservedNs);
            writePod(output, cell.semanticConfidence);
            writeString(output, cell.semanticLabel);
        }
    }
}

bool EnvironmentCartographer::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        clearAll();
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open cartography snapshot");
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (!input || !std::equal(std::begin(magic), std::end(magic), std::begin(kMapMagic))) {
        throw std::runtime_error("Unknown TATARUS cartography snapshot format");
    }
    double voxelSize = 0.0;
    readPod(input, voxelSize);
    if (std::abs(voxelSize - impl_->config.voxelSizeMeters) > 1.0e-12) {
        throw std::runtime_error("Cartography snapshot voxel size does not match RobotMind config");
    }
    std::uint64_t environmentCount = 0;
    readPod(input, environmentCount);
    if (environmentCount > 1'000'000ULL) {
        throw std::runtime_error("Cartography snapshot has implausibly many environments");
    }
    std::unordered_map<EnvironmentId, EnvironmentRecord> restored;
    for (std::uint64_t environmentIndex = 0; environmentIndex < environmentCount; ++environmentIndex) {
        EnvironmentId id = 0;
        EnvironmentRecord environment;
        readPod(input, id);
        if (id == 0 || restored.contains(id)) {
            throw std::runtime_error("Cartography snapshot contains an invalid environment id");
        }
        readPod(input, environment.integratedScans);
        readPod(input, environment.integratedRays);
        for (double& value : environment.lastPose.positionMeters) readPod(input, value);
        for (double& value : environment.lastPose.orientationQuaternion) readPod(input, value);
        readPod(input, environment.lastTimestampNs);
        std::uint64_t cellCount = 0;
        readPod(input, cellCount);
        if (cellCount > impl_->config.maximumVoxelsPerEnvironment) {
            throw std::runtime_error("Cartography snapshot exceeds configured voxel capacity");
        }
        environment.cells.reserve(static_cast<std::size_t>(cellCount));
        for (std::uint64_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
            VoxelIndex index;
            CellRecord cell;
            readPod(input, index.x);
            readPod(input, index.y);
            readPod(input, index.z);
            readPod(input, cell.logOdds);
            readPod(input, cell.observations);
            readPod(input, cell.freeObservations);
            readPod(input, cell.occupiedObservations);
            readPod(input, cell.lastObservedNs);
            readPod(input, cell.semanticConfidence);
            cell.semanticLabel = readString(input);
            if (!std::isfinite(cell.logOdds) || !std::isfinite(cell.semanticConfidence)) {
                throw std::runtime_error("Cartography snapshot contains non-finite evidence");
            }
            environment.cells.emplace(index, std::move(cell));
        }
        restored.emplace(id, std::move(environment));
    }
    impl_->environments = std::move(restored);
    return true;
}

} // namespace tatarus::detail
