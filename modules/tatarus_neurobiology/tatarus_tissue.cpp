#include "tatarus_tissue.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace tatarus::neuro::biology {
namespace {

constexpr std::size_t kSegmentsPerNeuron = 7;
constexpr std::size_t kNeuronsPerAstrocyte = 12;
constexpr std::size_t kNeuronsPerOligodendrocyte = 16;
constexpr std::size_t kNeuronsPerMicroglia = 10;
constexpr double kUnmyelinatedVelocityUmPerMs = 150.0;
constexpr double kMaxMyelinatedVelocityUmPerMs = 920.0;
constexpr double kMinimumMyelinCoverage = 0.03;
constexpr double kMaximumMyelinCoverage = 0.96;
constexpr double kBrainRadiusXUm = 420.0;
constexpr double kBrainRadiusYUm = 330.0;
constexpr double kBrainRadiusZUm = 280.0;
constexpr double kInterhemisphericFissureUm = 24.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kBaselineEcsFraction = 0.20;
constexpr double kMinimumEcsFraction = 0.08;
constexpr double kBaselineSynapticFraction = 0.10;
constexpr double kNonSynapticSolidFraction = 0.70;
constexpr double kEffectiveTissueDensityPgPerUm3 = 1.04;
constexpr std::array<std::int32_t, kSegmentsPerNeuron> kLocalParent{
    -1, 0, 0, 1, 1, 2, 2};
constexpr std::array<double, kSegmentsPerNeuron> kSegmentLengthUm{
    26.0, 34.0, 38.0, 42.0, 44.0, 48.0, 50.0};

struct LegacyBiologicalMetricsV1 {
    int dendriteSegments = 0;
    int astrocytes = 0;
    int capillaries = 0;
    std::uint64_t dendriticSpikes = 0;
    double meanDendriticCalcium = 0.0;
    double meanAstrocyteCalcium = 0.0;
    double meanOxygen = 1.0;
    double meanGlucose = 1.0;
    double meanBloodFlow = 1.0;
    double meanAxonLengthUm = 0.0;
    double meanLocalEnergy = 1.0;
};

struct LegacyBiologicalMetricsV2 {
    int dendriteSegments = 0;
    int astrocytes = 0;
    int capillaries = 0;
    int oligodendrocytes = 0;
    std::uint64_t dendriticSpikes = 0;
    std::uint64_t myelinRemodelingUpdates = 0;
    double meanDendriticCalcium = 0.0;
    double meanAstrocyteCalcium = 0.0;
    double meanOxygen = 1.0;
    double meanGlucose = 1.0;
    double meanBloodFlow = 1.0;
    double meanAxonLengthUm = 0.0;
    double meanLocalEnergy = 1.0;
    double meanMyelinCoverage = 0.0;
    double meanConductionVelocityUmPerMs = 0.0;
    double meanEffectiveDelayMs = 0.0;
    double meanOligodendrocyteReserve = 1.0;
};

// BIO3/BIO4 snapshots predate tissue mechanics. Keep their exact POD layout.
struct LegacyBiologicalMetricsV4 {
    int dendriteSegments = 0;
    int astrocytes = 0;
    int capillaries = 0;
    int oligodendrocytes = 0;
    std::uint64_t dendriticSpikes = 0;
    std::uint64_t myelinRemodelingUpdates = 0;
    double meanDendriticCalcium = 0.0;
    double meanAstrocyteCalcium = 0.0;
    double meanOxygen = 1.0;
    double meanGlucose = 1.0;
    double meanBloodFlow = 1.0;
    double meanAxonLengthUm = 0.0;
    double meanLocalEnergy = 1.0;
    double meanMyelinCoverage = 0.0;
    double meanConductionVelocityUmPerMs = 0.0;
    double meanEffectiveDelayMs = 0.0;
    double meanOligodendrocyteReserve = 1.0;
    int microglia = 0;
    std::uint64_t microglialSurveillanceUpdates = 0;
    std::uint64_t microglialPruningEvents = 0;
    std::uint64_t microglialRepairEvents = 0;
    std::uint64_t microglialDamageSignals = 0;
    double meanMicroglialActivation = 0.0;
    double meanComplementTag = 0.0;
    double meanMicroglialRepairCapacity = 1.0;
    double meanInflammatoryTone = 0.0;
};

template <typename T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
}

template <typename T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
}

template <typename T>
void writeVector(std::ostream& output, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t size = values.size();
    writePod(output, size);
    if (!values.empty()) {
        output.write(
            reinterpret_cast<const char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
}

template <typename T>
void readVector(
    std::istream& input,
    std::vector<T>& values,
    std::uint64_t maximum) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint64_t size = 0;
    readPod(input, size);
    if (!input || size > maximum) {
        throw std::runtime_error("Ungültige biologische Snapshotdimension");
    }
    values.resize(static_cast<std::size_t>(size));
    if (!values.empty()) {
        input.read(
            reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
}

}  // namespace

SpatialSubstrate::SpatialSubstrate(
    std::size_t neuronCount,
    const std::vector<PopulationRole>& roles,
    std::uint64_t seed,
    double restingMv,
    double dtMs) {
    rebuild(neuronCount, roles, seed, restingMv, dtMs);
}

void SpatialSubstrate::rebuild(
    std::size_t neuronCount,
    const std::vector<PopulationRole>& roles,
    std::uint64_t seed,
    double restingMv,
    double dtMs) {
    seed_ = seed ^ 0xB10B10A57A7A5ULL;
    restingMv_ = restingMv;
    dtMs_ = dtMs;
    morphologies_.clear();
    segments_.clear();
    bindings_.clear();
    astrocytes_.clear();
    capillaries_.clear();
    oligodendrocytes_.clear();
    myelin_.clear();
    microglia_.clear();
    maintenance_.clear();
    mechanics_ = {};
    mechanics_.baselineVolumeUm3 = 4.0 / 3.0 * kPi
        * kBrainRadiusXUm * kBrainRadiusYUm * kBrainRadiusZUm;
    mechanics_.currentVolumeUm3 = mechanics_.baselineVolumeUm3;

    roles_ = roles;
    if (roles_.size() != neuronCount) {
        roles_.assign(neuronCount, PopulationRole::Excitatory);
    }
    hemispheres_.resize(neuronCount);
    regions_.resize(neuronCount);
    layers_.resize(neuronCount);

    morphologies_.resize(neuronCount);
    segments_.reserve(neuronCount * kSegmentsPerNeuron);

    std::mt19937_64 rng(seed_);
    std::uniform_real_distribution<double> jitter(-1.0, 1.0);
    const auto unitSample = [this](std::uint64_t value) {
        constexpr double denominator = 1.0 / 9007199254740992.0;
        return static_cast<double>(mix64(seed_ ^ value) >> 11) * denominator;
    };

    std::array<std::size_t, 6> roleOrdinals{};
    std::array<std::size_t, 6> roleTotals{};
    for (const auto role : roles_) {
        ++roleTotals[static_cast<std::size_t>(role)];
    }
    const auto assignLayer = [](PopulationRole role, std::size_t ordinal) {
        if (role == PopulationRole::Modulatory) return CorticalLayer::Subcortical;
        const std::size_t slot = ordinal % 4;
        if (role == PopulationRole::Sensory) {
            constexpr std::array<CorticalLayer, 4> pattern{
                CorticalLayer::Layer4, CorticalLayer::Layer4,
                CorticalLayer::Layer23, CorticalLayer::Layer6};
            return pattern[slot];
        }
        if (role == PopulationRole::Motor) {
            constexpr std::array<CorticalLayer, 4> pattern{
                CorticalLayer::Layer5, CorticalLayer::Layer5,
                CorticalLayer::Layer23, CorticalLayer::Layer6};
            return pattern[slot];
        }
        constexpr std::array<CorticalLayer, 4> pattern{
            CorticalLayer::Layer23, CorticalLayer::Layer4,
            CorticalLayer::Layer5, CorticalLayer::Layer6};
        return pattern[slot];
    };
    const auto assignRegion = [](PopulationRole role, std::size_t ordinal,
                                 std::size_t total) {
        switch (role) {
            case PopulationRole::Sensory: return BrainRegion::Sensory;
            case PopulationRole::Excitatory: return BrainRegion::Association;
            case PopulationRole::Context:
                return ordinal < (total + 1) / 2
                    ? BrainRegion::Memory : BrainRegion::Prospection;
            case PopulationRole::Motor: return BrainRegion::Motor;
            case PopulationRole::Modulatory: return BrainRegion::Homeostatic;
            case PopulationRole::Inhibitory: {
                constexpr std::array<BrainRegion, 5> targets{
                    BrainRegion::Sensory, BrainRegion::Association,
                    BrainRegion::Memory, BrainRegion::Prospection,
                    BrainRegion::Motor};
                return targets[ordinal % targets.size()];
            }
        }
        return BrainRegion::Association;
    };
    const auto regionCenterY = [](BrainRegion region) {
        switch (region) {
            case BrainRegion::Sensory: return -0.55 * kBrainRadiusYUm;
            case BrainRegion::Association: return -0.10 * kBrainRadiusYUm;
            case BrainRegion::Memory: return 0.13 * kBrainRadiusYUm;
            case BrainRegion::Prospection: return 0.35 * kBrainRadiusYUm;
            case BrainRegion::Motor: return 0.62 * kBrainRadiusYUm;
            case BrainRegion::Homeostatic: return 0.02 * kBrainRadiusYUm;
        }
        return 0.0;
    };
    const auto layerRadius = [](CorticalLayer layer, double sample) {
        switch (layer) {
            case CorticalLayer::Layer23: return 0.88 + 0.10 * sample;
            case CorticalLayer::Layer4: return 0.75 + 0.09 * sample;
            case CorticalLayer::Layer5: return 0.62 + 0.10 * sample;
            case CorticalLayer::Layer6: return 0.47 + 0.11 * sample;
            case CorticalLayer::Subcortical: return 0.20 + 0.18 * sample;
        }
        return 0.8;
    };

    for (std::size_t neuron = 0; neuron < neuronCount; ++neuron) {
        // Region, hemisphere and cortical depth are part of the causal
        // substrate. They therefore influence both soma coordinates and the
        // connectivity kernel rather than being renderer-only labels.
        const auto role = roles_[neuron];
        const auto ordinal = roleOrdinals[static_cast<std::size_t>(role)]++;
        hemispheres_[neuron] = (ordinal + static_cast<std::size_t>(role)) % 2 == 0
            ? BrainHemisphere::Left : BrainHemisphere::Right;
        regions_[neuron] = assignRegion(
            role, ordinal, roleTotals[static_cast<std::size_t>(role)]);
        layers_[neuron] = assignLayer(role, ordinal);
        const std::uint64_t base = static_cast<std::uint64_t>(neuron) * 4ULL;
        const double radius = layerRadius(layers_[neuron], unitSample(base + 1ULL));
        const double cosTheta = 2.0 * unitSample(base + 2ULL) - 1.0;
        const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        const double azimuth = 6.283185307179586 * unitSample(base + 3ULL);
        const double sideSign = hemispheres_[neuron] == BrainHemisphere::Left
            ? -1.0 : 1.0;
        double x = sideSign * (
            kInterhemisphericFissureUm
            + 0.96 * kBrainRadiusXUm * radius
                * std::abs(sinTheta * std::cos(azimuth)));
        double y = 0.44 * kBrainRadiusYUm * radius
            * sinTheta * std::sin(azimuth)
            + regionCenterY(regions_[neuron]);
        double z = kBrainRadiusZUm * radius * cosTheta;
        if (layers_[neuron] == CorticalLayer::Subcortical) z -= 55.0;
        const double normalized = std::sqrt(
            std::pow((std::abs(x) - kInterhemisphericFissureUm)
                / kBrainRadiusXUm, 2.0)
            + std::pow(y / kBrainRadiusYUm, 2.0)
            + std::pow(z / kBrainRadiusZUm, 2.0));
        if (normalized > 0.98) {
            const double scale = 0.98 / normalized;
            x = sideSign * (kInterhemisphericFissureUm
                + (std::abs(x) - kInterhemisphericFissureUm) * scale);
            y *= scale;
            z *= scale;
        }
        auto& morphology = morphologies_[neuron];
        morphology.soma = {
            x + 5.0 * jitter(rng),
            y + 5.0 * jitter(rng),
            z + 4.0 * jitter(rng)};
        morphology.firstSegment = static_cast<std::uint32_t>(segments_.size());
        morphology.segmentCount = static_cast<std::uint16_t>(kSegmentsPerNeuron);

        const double baseAngle = 6.283185307179586
            * static_cast<double>(mix64(seed_ + neuron) & 0xffffULL) / 65535.0;
        for (std::size_t local = 0; local < kSegmentsPerNeuron; ++local) {
            Segment segment;
            segment.neuron = static_cast<std::uint32_t>(neuron);
            segment.parent = kLocalParent[local] < 0
                ? -1
                : static_cast<std::int32_t>(morphology.firstSegment)
                    + kLocalParent[local];
            segment.membraneMv = restingMv_ + 0.6 * jitter(rng);
            segment.localEnergy = std::clamp(0.93 + 0.05 * jitter(rng), 0.75, 1.0);
            segment.branchOrder = static_cast<std::uint8_t>(
                local == 0 ? 0 : (local <= 2 ? 1 : 2));

            const Vec3 origin = segment.parent < 0
                ? morphology.soma
                : segments_[static_cast<std::size_t>(segment.parent)].position;
            const double branchSign = (local % 2 == 0) ? 1.0 : -1.0;
            const double angle = baseAngle
                + branchSign * (0.62 + 0.37 * static_cast<double>(local));
            const double elevation = (local < 3 ? 0.32 : 0.58)
                * ((local % 3 == 0) ? 1.0 : -0.6);
            const double length = kSegmentLengthUm[local];
            segment.position = {
                origin.x + std::cos(angle) * std::cos(elevation) * length,
                origin.y + std::sin(angle) * std::cos(elevation) * length,
                origin.z + std::sin(elevation) * length};
            segments_.push_back(segment);
        }
    }

    const std::size_t astrocyteCount = std::max<std::size_t>(
        1,
        (neuronCount + kNeuronsPerAstrocyte - 1) / kNeuronsPerAstrocyte);
    astrocytes_.resize(astrocyteCount);
    capillaries_.resize(astrocyteCount);
    for (std::size_t domain = 0; domain < astrocyteCount; ++domain) {
        const std::size_t first = domain * kNeuronsPerAstrocyte;
        const std::size_t last = std::min(neuronCount, first + kNeuronsPerAstrocyte);
        Vec3 center{};
        for (std::size_t neuron = first; neuron < last; ++neuron) {
            center.x += morphologies_[neuron].soma.x;
            center.y += morphologies_[neuron].soma.y;
            center.z += morphologies_[neuron].soma.z;
        }
        const double divisor = static_cast<double>(std::max<std::size_t>(1, last - first));
        center.x /= divisor;
        center.y /= divisor;
        center.z /= divisor;
        astrocytes_[domain].center = center;
        astrocytes_[domain].vessel = static_cast<std::uint16_t>(domain);
        capillaries_[domain].position = {center.x + 30.0, center.y - 24.0, center.z + 12.0};
    }
    for (auto& segment : segments_) {
        const std::size_t domain = std::min(
            astrocytes_.size() - 1,
            static_cast<std::size_t>(segment.neuron) / kNeuronsPerAstrocyte);
        segment.astrocyte = static_cast<std::uint16_t>(domain);
    }
    rebuildOligodendrocytes();
    rebuildMicroglia();
    updateMetrics();
}

double SpatialSubstrate::distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::uint64_t SpatialSubstrate::mix64(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

double SpatialSubstrate::connectionScale(
    std::size_t pre,
    std::size_t post) const {
    if (pre >= morphologies_.size() || post >= morphologies_.size()) return 1.0;
    const double d = distance(morphologies_[pre].soma, morphologies_[post].soma)
        * mechanics_.linearExpansion;
    const double local = std::exp(-d / 310.0);
    double regionScale = regions_[pre] == regions_[post] ? 1.55 : 0.42;
    const auto from = regions_[pre];
    const auto to = regions_[post];
    if ((from == BrainRegion::Sensory && to == BrainRegion::Association)
        || (from == BrainRegion::Association && to == BrainRegion::Memory)
        || (from == BrainRegion::Association && to == BrainRegion::Prospection)) {
        regionScale = 1.22;
    } else if ((from == BrainRegion::Memory || from == BrainRegion::Prospection)
        && to == BrainRegion::Motor) {
        regionScale = 1.05;
    } else if (from == BrainRegion::Homeostatic) {
        regionScale = 0.88;
    }
    const double hemisphereScale = hemispheres_[pre] == hemispheres_[post]
        ? 1.0 : 0.34;
    const int layerDistance = std::abs(
        static_cast<int>(layers_[pre]) - static_cast<int>(layers_[post]));
    const double layerScale = layers_[pre] == CorticalLayer::Subcortical
            || layers_[post] == CorticalLayer::Subcortical
        ? 0.90
        : layerDistance == 0 ? 1.18 : layerDistance == 1 ? 1.0 : 0.76;
    return std::clamp(
        (0.28 + 1.48 * local) * regionScale * hemisphereScale * layerScale,
        0.05,
        2.40);
}

std::uint32_t SpatialSubstrate::conductionDelaySteps(
    std::size_t pre,
    std::size_t post,
    double dtMs) const {
    if (pre >= morphologies_.size() || post >= morphologies_.size()) return 1;
    const double length = (distance(
        morphologies_[pre].soma, morphologies_[post].soma) + 35.0)
        * mechanics_.linearExpansion;
    const double delayMs = 0.35 + length / kUnmyelinatedVelocityUmPerMs;
    return static_cast<std::uint32_t>(std::clamp(
        static_cast<int>(std::llround(delayMs / std::max(0.05, dtMs))),
        1,
        32));
}

double SpatialSubstrate::conductionVelocity(const MyelinState& state) const {
    const double coverage = std::clamp(
        state.coverage * experimentalMyelinIntegrity_,
        0.0,
        kMaximumMyelinCoverage);
    const double myelinGain = std::pow(coverage, 0.72);
    const double caliberGain = std::sqrt(std::clamp(state.axonCaliber, 0.55, 1.55));
    return std::clamp(
        (kUnmyelinatedVelocityUmPerMs
            + (kMaxMyelinatedVelocityUmPerMs - kUnmyelinatedVelocityUmPerMs)
                * myelinGain)
            * caliberGain,
        90.0,
        1150.0);
}

std::uint32_t SpatialSubstrate::effectiveConductionDelaySteps(
    std::size_t synapseIndex,
    std::uint32_t fallbackDelaySteps,
    double dtMs) const {
    if (synapseIndex >= bindings_.size() || synapseIndex >= myelin_.size()) {
        return std::clamp<std::uint32_t>(fallbackDelaySteps, 1U, 32U);
    }
    const auto& binding = bindings_[synapseIndex];
    const auto& myelin = myelin_[synapseIndex];
    const double velocity = std::max(90.0, myelin.conductionVelocityUmPerMs);
    const double delayMs = 0.25
        + binding.axonLengthUm * mechanics_.linearExpansion / velocity;
    return static_cast<std::uint32_t>(std::clamp(
        static_cast<int>(std::llround(delayMs / std::max(0.05, dtMs))),
        1,
        32));
}

std::uint32_t SpatialSubstrate::selectSegment(
    std::size_t post,
    std::size_t synapseIndex,
    std::size_t pre) const {
    if (post >= morphologies_.size()) return 0;
    const auto& morphology = morphologies_[post];
    const std::uint64_t key = mix64(
        seed_
        ^ (static_cast<std::uint64_t>(pre) << 32)
        ^ static_cast<std::uint64_t>(post)
        ^ (static_cast<std::uint64_t>(synapseIndex) * 0x9e3779b97f4a7c15ULL));
    // Distal branches receive more synapses than the trunk, as in a real arbor.
    const std::array<std::uint8_t, 12> weightedLocal{
        0, 1, 2, 3, 3, 4, 4, 5, 5, 5, 6, 6};
    const auto local = weightedLocal[static_cast<std::size_t>(key % weightedLocal.size())];
    return morphology.firstSegment + std::min<std::uint32_t>(
        local,
        static_cast<std::uint32_t>(morphology.segmentCount - 1));
}

void SpatialSubstrate::rebuildOligodendrocytes() {
    const std::size_t neuronCount = morphologies_.size();
    const std::size_t count = std::max<std::size_t>(
        1,
        (neuronCount + kNeuronsPerOligodendrocyte - 1)
            / kNeuronsPerOligodendrocyte);
    oligodendrocytes_.assign(count, Oligodendrocyte{});

    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t first = index * kNeuronsPerOligodendrocyte;
        const std::size_t last = std::min(
            neuronCount, first + kNeuronsPerOligodendrocyte);
        Vec3 center{};
        for (std::size_t neuron = first; neuron < last; ++neuron) {
            center.x += morphologies_[neuron].soma.x;
            center.y += morphologies_[neuron].soma.y;
            center.z += morphologies_[neuron].soma.z;
        }
        const double divisor = static_cast<double>(
            std::max<std::size_t>(1, last - first));
        center.x /= divisor;
        center.y /= divisor;
        center.z /= divisor;

        auto& oligo = oligodendrocytes_[index];
        oligo.center = center;
        oligo.myelinReserve = 0.92;
        oligo.metabolicLoad = 0.0;
        oligo.remodelingSignal = 0.0;

        std::size_t nearestVessel = 0;
        double nearestDistance = std::numeric_limits<double>::max();
        for (std::size_t vessel = 0; vessel < capillaries_.size(); ++vessel) {
            const double candidate = distance(center, capillaries_[vessel].position);
            if (candidate < nearestDistance) {
                nearestDistance = candidate;
                nearestVessel = vessel;
            }
        }
        oligo.vessel = static_cast<std::uint16_t>(nearestVessel);
    }
}

void SpatialSubstrate::rebuildMicroglia() {
    const std::size_t neuronCount = morphologies_.size();
    const std::size_t count = std::max<std::size_t>(
        1,
        (neuronCount + kNeuronsPerMicroglia - 1) / kNeuronsPerMicroglia);
    microglia_.assign(count, Microglia{});

    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t first = index * kNeuronsPerMicroglia;
        const std::size_t last = std::min(
            neuronCount, first + kNeuronsPerMicroglia);
        Vec3 center{};
        for (std::size_t neuron = first; neuron < last; ++neuron) {
            center.x += morphologies_[neuron].soma.x;
            center.y += morphologies_[neuron].soma.y;
            center.z += morphologies_[neuron].soma.z;
        }
        const double divisor = static_cast<double>(
            std::max<std::size_t>(1, last - first));
        center.x /= divisor;
        center.y /= divisor;
        center.z /= divisor;

        auto& cell = microglia_[index];
        cell.center = center;
        cell.activation = 0.08;
        cell.inflammatoryTone = 0.0;
        cell.phagocyticLoad = 0.0;
        cell.repairCapacity = 0.92;
        cell.surveillance = 0.70;
        cell.damageLoad = 0.0;

        std::size_t nearestVessel = 0;
        double nearestDistance = std::numeric_limits<double>::max();
        for (std::size_t vessel = 0; vessel < capillaries_.size(); ++vessel) {
            const double candidate = distance(center, capillaries_[vessel].position);
            if (candidate < nearestDistance) {
                nearestDistance = candidate;
                nearestVessel = vessel;
            }
        }
        cell.vessel = static_cast<std::uint16_t>(nearestVessel);
    }
}

std::uint16_t SpatialSubstrate::nearestMicroglia(const Vec3& point) const {
    if (microglia_.empty()) return 0;
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < microglia_.size(); ++index) {
        const double candidate = distance(point, microglia_[index].center);
        if (candidate < bestDistance) {
            bestDistance = candidate;
            best = index;
        }
    }
    return static_cast<std::uint16_t>(best);
}

void SpatialSubstrate::initializeMaintenanceState(
    std::size_t synapseIndex,
    std::size_t pre,
    std::size_t post) {
    if (maintenance_.size() <= synapseIndex) {
        maintenance_.resize(synapseIndex + 1);
    }
    auto& state = maintenance_[synapseIndex];
    state.preNeuron = static_cast<std::uint32_t>(pre);
    state.postNeuron = static_cast<std::uint32_t>(post);

    Vec3 midpoint{};
    if (pre < morphologies_.size() && synapseIndex < bindings_.size()) {
        const auto& source = morphologies_[pre].soma;
        const auto segmentIndex = bindings_[synapseIndex].segment;
        const auto target = segmentIndex < segments_.size()
            ? segments_[segmentIndex].position
            : (post < morphologies_.size() ? morphologies_[post].soma : source);
        midpoint = {
            0.5 * (source.x + target.x),
            0.5 * (source.y + target.y),
            0.5 * (source.z + target.z)};
    }
    state.microglia = nearestMicroglia(midpoint);

    const std::uint64_t key = mix64(
        seed_
        ^ (static_cast<std::uint64_t>(synapseIndex) * 0xA24BAED4963EE407ULL)
        ^ (static_cast<std::uint64_t>(pre) << 32)
        ^ static_cast<std::uint64_t>(post));
    const double unit = static_cast<double>(key & 0xffffULL) / 65535.0;
    state.functionalTrace = 0.08 + 0.12 * unit;
    state.complementTag = 0.025 + 0.025 * (1.0 - unit);
    state.damageSignal = 0.0;
    state.repairSignal = 0.0;
    state.surveillanceScore = 0.15;
}

std::uint16_t SpatialSubstrate::nearestOligodendrocyte(
    const Vec3& point) const {
    if (oligodendrocytes_.empty()) return 0;
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < oligodendrocytes_.size(); ++index) {
        const double candidate = distance(point, oligodendrocytes_[index].center);
        if (candidate < bestDistance) {
            bestDistance = candidate;
            best = index;
        }
    }
    return static_cast<std::uint16_t>(best);
}

void SpatialSubstrate::initializeMyelinState(
    std::size_t synapseIndex,
    std::size_t pre,
    std::size_t post) {
    if (myelin_.size() <= synapseIndex) myelin_.resize(synapseIndex + 1);
    auto& state = myelin_[synapseIndex];
    state.preNeuron = static_cast<std::uint32_t>(pre);
    state.postNeuron = static_cast<std::uint32_t>(post);

    const std::uint64_t key = mix64(
        seed_
        ^ (static_cast<std::uint64_t>(synapseIndex) * 0xD6E8FEB86659FD93ULL)
        ^ (static_cast<std::uint64_t>(pre) << 32)
        ^ static_cast<std::uint64_t>(post));
    const double unitA = static_cast<double>(key & 0xffffULL) / 65535.0;
    const double unitB = static_cast<double>((key >> 16) & 0xffffULL) / 65535.0;
    state.coverage = 0.08 + 0.10 * unitA;
    state.axonCaliber = 0.78 + 0.44 * unitB;
    state.activityTrace = 0.0;
    state.arrivalTrace = 0.0;
    state.coincidenceTrace = 0.0;
    state.remodelingRate = 0.0;

    Vec3 midpoint{};
    if (pre < morphologies_.size() && synapseIndex < bindings_.size()) {
        const auto& source = morphologies_[pre].soma;
        const auto segmentIndex = bindings_[synapseIndex].segment;
        const auto target = segmentIndex < segments_.size()
            ? segments_[segmentIndex].position
            : (post < morphologies_.size() ? morphologies_[post].soma : source);
        midpoint = {
            0.5 * (source.x + target.x),
            0.5 * (source.y + target.y),
            0.5 * (source.z + target.z)};
    }
    state.oligodendrocyte = nearestOligodendrocyte(midpoint);
    state.conductionVelocityUmPerMs = conductionVelocity(state);
}

void SpatialSubstrate::synchronizeSynapse(
    std::size_t synapseIndex,
    std::size_t pre,
    std::size_t post) {
    if (bindings_.size() <= synapseIndex) bindings_.resize(synapseIndex + 1);
    auto& binding = bindings_[synapseIndex];
    binding.segment = selectSegment(post, synapseIndex, pre);
    if (pre < morphologies_.size() && binding.segment < segments_.size()) {
        binding.axonLengthUm = distance(
            morphologies_[pre].soma,
            segments_[binding.segment].position);
    } else {
        binding.axonLengthUm = 0.0;
    }

    if (oligodendrocytes_.empty()) rebuildOligodendrocytes();
    if (microglia_.empty()) rebuildMicroglia();
    const bool needsInitialization =
        synapseIndex >= myelin_.size()
        || myelin_[synapseIndex].preNeuron != pre
        || myelin_[synapseIndex].postNeuron != post;
    if (needsInitialization) {
        initializeMyelinState(synapseIndex, pre, post);
    } else {
        auto& state = myelin_[synapseIndex];
        Vec3 midpoint{};
        if (pre < morphologies_.size() && binding.segment < segments_.size()) {
            const auto& source = morphologies_[pre].soma;
            const auto& target = segments_[binding.segment].position;
            midpoint = {
                0.5 * (source.x + target.x),
                0.5 * (source.y + target.y),
                0.5 * (source.z + target.z)};
        }
        state.oligodendrocyte = nearestOligodendrocyte(midpoint);
        state.conductionVelocityUmPerMs = conductionVelocity(state);
    }

    const bool maintenanceNeedsInitialization =
        synapseIndex >= maintenance_.size()
        || maintenance_[synapseIndex].preNeuron != pre
        || maintenance_[synapseIndex].postNeuron != post;
    if (maintenanceNeedsInitialization) {
        initializeMaintenanceState(synapseIndex, pre, post);
    } else {
        auto& state = maintenance_[synapseIndex];
        Vec3 midpoint{};
        if (pre < morphologies_.size() && binding.segment < segments_.size()) {
            const auto& source = morphologies_[pre].soma;
            const auto& target = segments_[binding.segment].position;
            midpoint = {
                0.5 * (source.x + target.x),
                0.5 * (source.y + target.y),
                0.5 * (source.z + target.z)};
        }
        state.microglia = nearestMicroglia(midpoint);
    }
}

void SpatialSubstrate::truncateBindings(std::size_t synapseCount) {
    if (bindings_.size() > synapseCount) bindings_.resize(synapseCount);
    if (myelin_.size() > synapseCount) myelin_.resize(synapseCount);
    if (maintenance_.size() > synapseCount) maintenance_.resize(synapseCount);
}

void SpatialSubstrate::recordAxonUse(
    std::size_t synapseIndex,
    double amplitude) {
    if (synapseIndex >= myelin_.size()) return;
    if (throughput_) throughput_->recordReadWrite<MyelinState>(tatarus::ThroughputDomain::Oligodendrocyte);
    auto& state = myelin_[synapseIndex];
    const double signal = std::clamp(std::abs(amplitude), 0.0, 2.0);
    state.activityTrace = std::clamp(
        state.activityTrace + 0.22 + 0.55 * signal,
        0.0,
        80.0);
    if (state.oligodendrocyte < oligodendrocytes_.size()) {
        if (throughput_) throughput_->recordReadWrite<Oligodendrocyte>(tatarus::ThroughputDomain::Oligodendrocyte);
        auto& oligo = oligodendrocytes_[state.oligodendrocyte];
        oligo.metabolicLoad = std::clamp(
            oligo.metabolicLoad + 0.012 * (0.4 + signal),
            0.0,
            8.0);
    }
    if (synapseIndex < maintenance_.size()) {
        if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
        auto& maintenance = maintenance_[synapseIndex];
        maintenance.functionalTrace = std::clamp(
            maintenance.functionalTrace + 0.12 + 0.28 * signal,
            0.0,
            20.0);
        maintenance.complementTag = std::max(
            0.0, maintenance.complementTag - 0.018 * (0.4 + signal));
    }
}

void SpatialSubstrate::recordAxonArrival(
    std::size_t synapseIndex,
    double amplitude) {
    if (synapseIndex >= myelin_.size()) return;
    if (throughput_) throughput_->recordReadWrite<MyelinState>(tatarus::ThroughputDomain::Oligodendrocyte);
    auto& state = myelin_[synapseIndex];
    state.arrivalTrace = std::clamp(
        state.arrivalTrace + 0.18 + 0.65 * std::clamp(std::abs(amplitude), 0.0, 2.0),
        0.0,
        12.0);
    if (synapseIndex < maintenance_.size()) {
        if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
        auto& maintenance = maintenance_[synapseIndex];
        maintenance.functionalTrace = std::clamp(
            maintenance.functionalTrace + 0.05, 0.0, 20.0);
    }
}

void SpatialSubstrate::reportSynapseDamage(
    std::size_t synapseIndex,
    double severity) {
    if (synapseIndex >= maintenance_.size()) return;
    if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
    auto& state = maintenance_[synapseIndex];
    const double signal = std::clamp(severity, 0.0, 2.0);
    state.damageSignal = std::clamp(
        state.damageSignal + 0.85 * signal, 0.0, 3.0);
    state.repairSignal = std::clamp(
        state.repairSignal + 0.60 * signal, 0.0, 3.0);
    state.complementTag = std::clamp(
        state.complementTag + 0.18 * signal, 0.0, 1.5);
    if (state.microglia < microglia_.size()) {
        if (throughput_) throughput_->recordReadWrite<Microglia>(tatarus::ThroughputDomain::Microglia);
        auto& cell = microglia_[state.microglia];
        cell.damageLoad = std::clamp(
            cell.damageLoad + 0.20 * signal, 0.0, 5.0);
        cell.activation = std::clamp(
            cell.activation + 0.08 * signal, 0.0, 1.5);
    }
    ++metrics_.microglialDamageSignals;
}

void SpatialSubstrate::reportNeuronDamage(
    std::size_t neuronIndex,
    double severity) {
    if (neuronIndex >= morphologies_.size() || microglia_.empty()) return;
    const double signal = std::clamp(severity, 0.0, 2.0);
    const auto cellIndex = nearestMicroglia(morphologies_[neuronIndex].soma);
    auto& cell = microglia_[cellIndex];
    cell.damageLoad = std::clamp(
        cell.damageLoad + 0.45 * signal, 0.0, 5.0);
    cell.activation = std::clamp(
        cell.activation + 0.12 * signal, 0.0, 1.5);
    ++metrics_.microglialDamageSignals;

    for (std::size_t index = 0; index < maintenance_.size(); ++index) {
        auto& state = maintenance_[index];
        if (state.preNeuron != neuronIndex && state.postNeuron != neuronIndex) {
            continue;
        }
        state.damageSignal = std::clamp(
            state.damageSignal + 0.22 * signal, 0.0, 3.0);
        state.repairSignal = std::clamp(
            state.repairSignal + 0.12 * signal, 0.0, 3.0);
    }
}

double SpatialSubstrate::microglialPruningDrive(
    std::size_t synapseIndex,
    double agePressure,
    double usageProtection,
    double weightProtection) const {
    if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
    if (synapseIndex >= maintenance_.size() || microglia_.empty()) return 0.0;
    const auto& state = maintenance_[synapseIndex];
    const auto& cell = microglia_[std::min<std::size_t>(
        state.microglia, microglia_.size() - 1)];
    const double functionalProtection = std::clamp(
        state.functionalTrace / (state.functionalTrace + 1.5), 0.0, 1.0);
    const double age = std::clamp(agePressure, 0.0, 1.0);
    const double usage = std::clamp(usageProtection, 0.0, 1.0);
    const double weight = std::clamp(weightProtection, 0.0, 1.0);
    return experimentalMicrogliaFunction_ * std::clamp(
        0.38 * state.complementTag
            + 0.18 * std::min(1.0, state.damageSignal)
            + 0.18 * age * (1.0 - weight)
            + 0.14 * (1.0 - usage)
            + 0.12 * cell.activation
            + 0.18 * std::clamp(mechanics_.pressureKPa / 2.0, 0.0, 1.0)
            - 0.22 * functionalProtection,
        0.0,
        1.5);
}

double SpatialSubstrate::microglialRepairDrive(
    std::size_t synapseIndex,
    double consolidatedStrength,
    double usageEvidence) const {
    if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
    if (synapseIndex >= maintenance_.size() || microglia_.empty()) return 0.0;
    const auto& state = maintenance_[synapseIndex];
    const auto& cell = microglia_[std::min<std::size_t>(
        state.microglia, microglia_.size() - 1)];
    const double functionalMemory = std::clamp(
        state.functionalTrace / (state.functionalTrace + 0.8), 0.0, 1.0);
    return experimentalMicrogliaFunction_ * std::clamp(
        0.42 * std::min(1.0, state.repairSignal)
            + 0.18 * std::min(1.0, state.damageSignal)
            + 0.16 * cell.repairCapacity
            + 0.12 * functionalMemory
            + 0.08 * std::clamp(consolidatedStrength, 0.0, 1.0)
            + 0.04 * std::clamp(usageEvidence, 0.0, 1.0)
            - 0.12 * cell.inflammatoryTone,
        0.0,
        1.5);
}

void SpatialSubstrate::recordMicroglialPruning(std::size_t synapseIndex) {
    if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
    if (synapseIndex >= maintenance_.size() || microglia_.empty()) return;
    auto& state = maintenance_[synapseIndex];
    auto& cell = microglia_[std::min<std::size_t>(
        state.microglia, microglia_.size() - 1)];
    cell.phagocyticLoad = std::clamp(cell.phagocyticLoad + 0.18, 0.0, 4.0);
    cell.activation = std::clamp(cell.activation + 0.025, 0.0, 1.5);
    cell.repairCapacity = std::max(0.05, cell.repairCapacity - 0.012);
    state.complementTag = std::max(0.0, state.complementTag - 0.16);
    state.repairSignal *= 0.65;
    ++metrics_.microglialPruningEvents;
}

void SpatialSubstrate::recordMicroglialRepair(std::size_t synapseIndex) {
    if (throughput_) throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia);
    if (synapseIndex >= maintenance_.size() || microglia_.empty()) return;
    auto& state = maintenance_[synapseIndex];
    auto& cell = microglia_[std::min<std::size_t>(
        state.microglia, microglia_.size() - 1)];
    cell.repairCapacity = std::max(0.05, cell.repairCapacity - 0.035);
    cell.phagocyticLoad = std::max(0.0, cell.phagocyticLoad - 0.03);
    state.damageSignal *= 0.35;
    state.repairSignal *= 0.25;
    state.complementTag *= 0.45;
    state.functionalTrace = std::max(state.functionalTrace, 0.30);
    ++metrics_.microglialRepairEvents;
}

void SpatialSubstrate::refreshMetrics() {
    updateMetrics();
}

void SpatialSubstrate::updateTissueMechanics(
    std::size_t activeSynapses,
    double proteinAvailability,
    double physiologicalEcsFraction,
    double dtMs) {
    if (mechanics_.baselineVolumeUm3 <= 0.0) {
        mechanics_.baselineVolumeUm3 = 4.0 / 3.0 * kPi
            * kBrainRadiusXUm * kBrainRadiusYUm * kBrainRadiusZUm;
    }
    if (!mechanics_.initialized) {
        mechanics_.baselineActiveSynapses = std::max<std::uint64_t>(
            1U, static_cast<std::uint64_t>(activeSynapses));
        mechanics_.activeSynapses = static_cast<std::uint64_t>(activeSynapses);
        mechanics_.currentVolumeUm3 = mechanics_.baselineVolumeUm3;
        mechanics_.linearExpansion = 1.0;
        mechanics_.initialized = true;
    }

    const double baselineSynapticVolume =
        kBaselineSynapticFraction * mechanics_.baselineVolumeUm3;
    const double synapseRatio = static_cast<double>(activeSynapses)
        / static_cast<double>(std::max<std::uint64_t>(
            1U, mechanics_.baselineActiveSynapses));
    const double synapticMaterial = baselineSynapticVolume * synapseRatio;
    const double materialDelta = synapticMaterial
        - mechanics_.previousSynapticMaterialUm3;
    if (mechanics_.previousSynapticMaterialUm3 > 0.0) {
        if (materialDelta > 0.0) {
            mechanics_.cumulativeSynthesizedUm3 += materialDelta;
            mechanics_.materialReserve = std::max(
                0.0,
                mechanics_.materialReserve
                    - materialDelta / std::max(1.0, baselineSynapticVolume) * 0.08);
        } else if (materialDelta < 0.0) {
            mechanics_.cumulativeRecycledUm3 -= materialDelta;
            mechanics_.materialReserve = std::min(
                1.2,
                mechanics_.materialReserve
                    - materialDelta / std::max(1.0, baselineSynapticVolume) * 0.035);
        }
    }
    mechanics_.previousSynapticMaterialUm3 = synapticMaterial;
    mechanics_.synapticMaterialVolumeUm3 = synapticMaterial;
    mechanics_.activeSynapses = static_cast<std::uint64_t>(activeSynapses);

    const double supply = std::clamp(
        0.35 * metrics_.meanOxygen
            + 0.30 * metrics_.meanGlucose
            + 0.20 * metrics_.meanBloodFlow
            + 0.15 * std::clamp(proteinAvailability, 0.0, 1.5),
        0.0, 1.3);
    mechanics_.materialReserve = std::clamp(
        mechanics_.materialReserve
            + std::max(0.0, dtMs) / 45'000.0
                * (supply - mechanics_.materialReserve),
        0.0, 1.2);

    const double solidVolume =
        kNonSynapticSolidFraction * mechanics_.baselineVolumeUm3
        + synapticMaterial;
    const double growthLoad = std::max(0.0, synapseRatio - 1.0);
    const double physiologicalEcs = std::clamp(
        physiologicalEcsFraction, kMinimumEcsFraction, 0.32);
    const double compressedEcs = std::clamp(
        physiologicalEcs - 0.035 * growthLoad,
        kMinimumEcsFraction, 0.32);
    mechanics_.extracellularSpaceFraction = compressedEcs;
    const double targetVolume = std::max(
        mechanics_.baselineVolumeUm3,
        solidVolume / std::max(0.55, 1.0 - compressedEcs));
    const double targetScale = std::cbrt(
        targetVolume / mechanics_.baselineVolumeUm3);
    const double alpha = dtMs <= 0.0
        ? 1.0
        : 1.0 - std::exp(-dtMs / 30'000.0);
    mechanics_.linearExpansion += alpha
        * (std::clamp(targetScale, 0.92, 1.65) - mechanics_.linearExpansion);
    mechanics_.currentVolumeUm3 = mechanics_.baselineVolumeUm3
        * std::pow(mechanics_.linearExpansion, 3.0);
    mechanics_.solidPackingFraction = std::clamp(
        solidVolume / std::max(1.0, mechanics_.currentVolumeUm3), 0.0, 1.25);
    const double packingExcess = std::max(
        0.0, mechanics_.solidPackingFraction
            - (1.0 - mechanics_.extracellularSpaceFraction));
    mechanics_.pressureKPa = std::clamp(
        28.0 * packingExcess
            + 0.35 * std::max(0.0, targetScale - mechanics_.linearExpansion),
        0.0, 8.0);
    updateMetrics();
}

bool SpatialSubstrate::canAllocateSynapticMaterial() const {
    return mechanics_.materialReserve > 0.04 && mechanics_.pressureKPa < 3.5;
}

void SpatialSubstrate::recordGrowthLimited() {
    ++mechanics_.growthLimitedEvents;
    updateMetrics();
}

void SpatialSubstrate::setExperimentalFactors(
    double astrocyteFunction,
    double oxygenSupply,
    double glucoseSupply,
    double myelinIntegrity,
    double microgliaFunction) {
    experimentalAstrocyteFunction_ = std::clamp(astrocyteFunction, 0.0, 1.5);
    experimentalOxygenSupply_ = std::clamp(oxygenSupply, 0.0, 1.5);
    experimentalGlucoseSupply_ = std::clamp(glucoseSupply, 0.0, 1.5);
    experimentalMyelinIntegrity_ = std::clamp(myelinIntegrity, 0.0, 1.0);
    experimentalMicrogliaFunction_ = std::clamp(microgliaFunction, 0.0, 1.5);
    for (auto& state : myelin_) {
        state.conductionVelocityUmPerMs = conductionVelocity(state);
    }
    updateMetrics();
}

void SpatialSubstrate::deposit(
    std::size_t synapseIndex,
    SynapticChannel channel,
    double amplitude,
    double localEnergyCost) {
    if (synapseIndex >= bindings_.size()) return;
    if (throughput_) throughput_->recordRead<SynapseBinding>(tatarus::ThroughputDomain::Synapse);
    const auto segmentIndex = bindings_[synapseIndex].segment;
    if (segmentIndex >= segments_.size()) return;
    if (throughput_) throughput_->recordReadWrite<Segment>(tatarus::ThroughputDomain::Dendrite);
    auto& segment = segments_[segmentIndex];
    switch (channel) {
        case SynapticChannel::Ampa: segment.gAmpa += amplitude; break;
        case SynapticChannel::Nmda: segment.gNmda += amplitude; break;
        case SynapticChannel::GabaA: segment.gGabaA += amplitude; break;
        case SynapticChannel::GabaB: segment.gGabaB += amplitude; break;
    }
    segment.localEnergy = std::max(0.0, segment.localEnergy - localEnergyCost);
    if (throughput_) throughput_->recordReadWrite<Astrocyte>(tatarus::ThroughputDomain::Astrocyte);
    auto& astrocyte = astrocytes_[segment.astrocyte];
    if (channel == SynapticChannel::Ampa || channel == SynapticChannel::Nmda) {
        astrocyte.glutamateLoad += 0.035 * amplitude;
    } else {
        astrocyte.potassiumLoad += 0.012 * amplitude;
    }
}

DendriteOutput SpatialSubstrate::updateNeuron(
    std::size_t neuronIndex,
    double somaMv,
    double externalDrive,
    const ElectrophysiologyParameters& p) {
    DendriteOutput output;
    output.membraneMv = restingMv_;
    if (neuronIndex >= morphologies_.size()) return output;

    const auto& morphology = morphologies_[neuronIndex];
    if (throughput_) {
        throughput_->recordRead<Morphology>(tatarus::ThroughputDomain::Dendrite);
        throughput_->recordReadWrite<Segment>(tatarus::ThroughputDomain::Dendrite, morphology.segmentCount);
    }
    const double ampaDecay = std::exp(-p.dtMs / p.tauAmpaMs);
    const double nmdaDecay = std::exp(-p.dtMs / p.tauNmdaMs);
    const double gabaADecay = std::exp(-p.dtMs / p.tauGabaAMs);
    const double gabaBDecay = std::exp(-p.dtMs / p.tauGabaBMs);
    double weightedMv = 0.0;
    double weightSum = 0.0;

    for (std::size_t local = 0; local < morphology.segmentCount; ++local) {
        auto& segment = segments_[morphology.firstSegment + local];
        segment.gAmpa *= ampaDecay;
        segment.gNmda *= nmdaDecay;
        segment.gGabaA *= gabaADecay;
        segment.gGabaB *= gabaBDecay;
        segment.calcium *= std::exp(-p.dtMs / 90.0);

        const double parentMv = segment.parent < 0
            ? somaMv
            : segments_[static_cast<std::size_t>(segment.parent)].membraneMv;
        const double magnesiumBlock = 1.0 /
            (1.0 + 0.28 * std::exp(-0.062 * segment.membraneMv));
        const double synapticDrive =
            segment.gAmpa * (p.ampaReversalMv - segment.membraneMv)
            + segment.gNmda * magnesiumBlock
                * (p.nmdaReversalMv - segment.membraneMv)
            + segment.gGabaA * (p.gabaAReversalMv - segment.membraneMv)
            + segment.gGabaB * (p.gabaBReversalMv - segment.membraneMv);
        const double branchCoupling = 0.30 / (1.0 + 0.22 * segment.branchOrder);
        const double distributedDrive = externalDrive
            * (local == 0 ? 0.52 : 0.48 / std::max<std::size_t>(1, morphology.segmentCount - 1));

        double activeBoost = 0.0;
        const double coincidence = segment.gAmpa + 1.35 * segment.gNmda;
        if (segment.activeRefractory > 0) {
            --segment.activeRefractory;
        } else if (segment.membraneMv > -61.0 && coincidence > 0.12) {
            activeBoost = 18.0 * std::tanh((coincidence - 0.12) * 2.6);
            segment.calcium = std::clamp(segment.calcium + 0.22 + 0.04 * coincidence, 0.0, 4.0);
            segment.activeRefractory = 5;
            ++metrics_.dendriticSpikes;
        }

        const double localTau = p.tauDendriteMs * (0.78 + 0.16 * segment.branchOrder);
        segment.membraneMv += p.dtMs / localTau * (
            p.restingMv - segment.membraneMv
            + synapticDrive
            + distributedDrive
            + branchCoupling * (parentMv - segment.membraneMv)
            + activeBoost);
        segment.membraneMv = std::clamp(segment.membraneMv, -95.0, 35.0);

        const double astroSupport = astrocytes_[segment.astrocyte].support;
        segment.localEnergy = std::clamp(
            segment.localEnergy + p.dtMs * 0.0012 * astroSupport,
            0.0,
            1.0);

        const double proximalWeight = 1.0 / (1.0 + 0.35 * segment.branchOrder);
        weightedMv += proximalWeight * segment.membraneMv;
        weightSum += proximalWeight;
        output.gAmpa += segment.gAmpa;
        output.gNmda += segment.gNmda;
        output.gGabaA += segment.gGabaA;
        output.gGabaB += segment.gGabaB;
        output.calcium += segment.calcium;
    }
    output.membraneMv = weightedMv / std::max(1e-9, weightSum);
    const double divisor = static_cast<double>(std::max<std::uint16_t>(1, morphology.segmentCount));
    output.gAmpa /= divisor;
    output.gNmda /= divisor;
    output.gGabaA /= divisor;
    output.gGabaB /= divisor;
    output.calcium /= divisor;
    return output;
}

void SpatialSubstrate::updateGliaAndVasculature(
    const std::vector<std::uint8_t>& spikes,
    const std::vector<double>& filteredRatesHz,
    double dtMs) {
    if (throughput_) {
        throughput_->recordRead<Morphology>(tatarus::ThroughputDomain::Neuron, morphologies_.size());
        throughput_->recordReadWrite<Astrocyte>(tatarus::ThroughputDomain::Astrocyte, astrocytes_.size());
        throughput_->recordReadWrite<Capillary>(tatarus::ThroughputDomain::Capillary, capillaries_.size());
        throughput_->recordReadWrite<Segment>(tatarus::ThroughputDomain::Dendrite, segments_.size());
        throughput_->recordReadWrite<Oligodendrocyte>(tatarus::ThroughputDomain::Oligodendrocyte, oligodendrocytes_.size());
        throughput_->recordReadWrite<MyelinState>(tatarus::ThroughputDomain::Oligodendrocyte, myelin_.size());
        throughput_->recordReadWrite<Microglia>(tatarus::ThroughputDomain::Microglia, microglia_.size());
        throughput_->recordReadWrite<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia, maintenance_.size());
    }
    std::vector<double> demand(astrocytes_.size(), 0.0);
    for (std::size_t neuron = 0; neuron < morphologies_.size(); ++neuron) {
        const std::size_t domain = std::min(
            astrocytes_.size() - 1,
            neuron / kNeuronsPerAstrocyte);
        const double spikeDemand = neuron < spikes.size() && spikes[neuron] ? 1.0 : 0.0;
        const double rateDemand = neuron < filteredRatesHz.size()
            ? std::clamp(filteredRatesHz[neuron] / 40.0, 0.0, 2.0)
            : 0.0;
        demand[domain] += 0.45 * spikeDemand + 0.08 * rateDemand;
    }

    std::vector<double> uptakeByAstrocyte(astrocytes_.size(), 1.0);
    for (std::size_t index = 0; index < astrocytes_.size(); ++index) {
        auto& astro = astrocytes_[index];
        auto& vessel = capillaries_[astro.vessel];
        astro.glutamateLoad *= std::exp(-dtMs / 55.0);
        astro.potassiumLoad *= std::exp(-dtMs / 110.0);
        astro.metabolicDemand += (dtMs / 180.0)
            * (demand[index] - astro.metabolicDemand);
        const double calciumTarget = std::clamp(
            0.55 * astro.glutamateLoad
            + 0.20 * astro.potassiumLoad
            + 0.12 * astro.metabolicDemand,
            0.0,
            3.0);
        astro.calcium += (dtMs / 260.0) * (calciumTarget - astro.calcium);

        const double flowTarget = std::clamp(
            0.78 + 0.55 * astro.calcium + 0.18 * astro.metabolicDemand,
            0.45,
            2.25);
        vessel.flow += (dtMs / 520.0) * (flowTarget - vessel.flow);
        const double consumption = 0.00022 * demand[index] * dtMs;
        const double oxygenTarget = std::clamp(
            (0.62 + 0.38 * vessel.flow) * experimentalOxygenSupply_,
            0.05, 1.10);
        const double glucoseTarget = std::clamp(
            (0.66 + 0.34 * vessel.flow) * experimentalGlucoseSupply_,
            0.05, 1.10);
        vessel.oxygen = std::clamp(
            vessel.oxygen
                + (dtMs / 700.0) * (oxygenTarget - vessel.oxygen)
                - consumption,
            0.05,
            1.10);
        vessel.glucose = std::clamp(
            vessel.glucose
                + (dtMs / 850.0) * (glucoseTarget - vessel.glucose)
                - 0.72 * consumption,
            0.05,
            1.10);
        astro.support = std::clamp(
            (0.25 + 0.40 * vessel.oxygen + 0.35 * vessel.glucose)
                * experimentalAstrocyteFunction_,
            0.0,
            1.25);

        // Uptake: active astrocytes accelerate excitatory transmitter clearance.
        uptakeByAstrocyte[index] = std::clamp(
            1.0 - experimentalAstrocyteFunction_
                * 0.004 * dtMs * (1.0 + astro.calcium),
            0.90,
            1.0);
    }
    for (auto& segment : segments_) {
        const double uptake = uptakeByAstrocyte[segment.astrocyte];
        segment.gAmpa *= uptake;
        segment.gNmda *= 0.5 * (1.0 + uptake);
    }

    // Oligodendrocytes turn repeated axonal use into a slowly changing
    // conduction substrate.  Myelin growth is deliberately resource-limited:
    // active pathways only accelerate when the local vascular/glial domain can
    // afford the maintenance cost.
    for (auto& oligo : oligodendrocytes_) {
        oligo.metabolicLoad *= std::exp(-dtMs / 900.0);
        oligo.remodelingSignal *= std::exp(-dtMs / 700.0);
        const auto vesselIndex = std::min<std::size_t>(
            oligo.vessel, capillaries_.size() - 1);
        const auto& vessel = capillaries_[vesselIndex];
        const double supplyTarget = std::clamp(
            0.12
                + 0.36 * vessel.oxygen
                + 0.31 * vessel.glucose
                + 0.11 * vessel.flow,
            0.18,
            1.18);
        oligo.myelinReserve = std::clamp(
            oligo.myelinReserve
                + (dtMs / 1600.0) * (supplyTarget - oligo.myelinReserve)
                - 0.000025 * dtMs * oligo.metabolicLoad,
            0.05,
            1.20);
    }

    const double activityDecay = std::exp(-dtMs / 3200.0);
    const double arrivalDecay = std::exp(-dtMs / 28.0);
    const double coincidenceDecay = std::exp(-dtMs / 6500.0);
    for (std::size_t index = 0; index < myelin_.size(); ++index) {
        auto& state = myelin_[index];
        state.activityTrace *= activityDecay;
        state.arrivalTrace *= arrivalDecay;
        state.coincidenceTrace *= coincidenceDecay;

        const bool postsynapticSpike =
            state.postNeuron < spikes.size() && spikes[state.postNeuron] != 0;
        if (postsynapticSpike && state.arrivalTrace > 0.02) {
            state.coincidenceTrace = std::clamp(
                state.coincidenceTrace
                    + 0.20 * std::min(2.0, state.arrivalTrace),
                0.0,
                40.0);
        }

        const double useSignal = state.activityTrace / (state.activityTrace + 8.0);
        const double timingSignal =
            state.coincidenceTrace / (state.coincidenceTrace + 3.0);
        const double adaptiveDrive = std::clamp(
            0.62 * useSignal + 0.38 * timingSignal,
            0.0,
            1.0);
        const double targetCoverage = std::clamp(
            0.055 + 0.90 * adaptiveDrive,
            kMinimumMyelinCoverage,
            kMaximumMyelinCoverage);

        auto& oligo = oligodendrocytes_[std::min<std::size_t>(
            state.oligodendrocyte, oligodendrocytes_.size() - 1)];
        const double resource = std::clamp(oligo.myelinReserve, 0.08, 1.20);
        const bool growing = targetCoverage > state.coverage;
        const double tauMs = growing
            ? 6500.0 / std::max(0.25, resource)
            : 52000.0;
        double delta = dtMs / tauMs * (targetCoverage - state.coverage);

        // Severe resource shortage suppresses growth first; existing sheaths
        // are retained much longer than they are built.
        if (delta > 0.0) {
            delta *= std::clamp((resource - 0.08) / 0.72, 0.0, 1.25);
        }
        state.coverage = std::clamp(
            state.coverage + delta,
            kMinimumMyelinCoverage,
            kMaximumMyelinCoverage);
        state.remodelingRate += (dtMs / 450.0) * (delta / std::max(0.05, dtMs) - state.remodelingRate);
        state.conductionVelocityUmPerMs = conductionVelocity(state);

        if (std::abs(delta) > 1e-12) {
            ++metrics_.myelinRemodelingUpdates;
            const double length = index < bindings_.size()
                ? bindings_[index].axonLengthUm
                : 0.0;
            const double constructionCost =
                std::max(0.0, delta) * (0.03 + length / 18000.0);
            oligo.myelinReserve = std::max(
                0.05, oligo.myelinReserve - constructionCost);
            oligo.metabolicLoad = std::clamp(
                oligo.metabolicLoad + 7.5 * constructionCost,
                0.0,
                8.0);
            oligo.remodelingSignal += std::abs(delta);
        }
    }

    // Microglia provide spatial surveillance and turn local inactivity,
    // damage and metabolic stress into slow structural-maintenance signals.
    // The cells do not directly delete synapses here.  They tag and score the
    // substrate; the nervous-system structural pass consumes those local
    // decisions at its normal remodeling cadence.
    std::vector<double> localComplement(microglia_.size(), 0.0);
    std::vector<double> localDamage(microglia_.size(), 0.0);
    std::vector<std::size_t> localCount(microglia_.size(), 0);

    const double functionalDecay = std::exp(-dtMs / 6000.0);
    const double damageDecay = std::exp(-dtMs / 14000.0);
    const double repairDecay = std::exp(-dtMs / 18000.0);
    for (std::size_t index = 0; index < maintenance_.size(); ++index) {
        auto& state = maintenance_[index];
        state.functionalTrace *= functionalDecay;
        state.damageSignal *= damageDecay;
        state.repairSignal *= repairDecay;

        const auto cellIndex = std::min<std::size_t>(
            state.microglia, microglia_.size() - 1);
        const auto& cell = microglia_[cellIndex];
        const double functionalProtection = std::clamp(
            state.functionalTrace / (state.functionalTrace + 1.2), 0.0, 1.0);
        const double inactivity = 1.0 - functionalProtection;
        const double damage = std::clamp(state.damageSignal, 0.0, 1.5);
        const double targetComplement = std::clamp(
            0.018
                + 0.30 * inactivity
                + 0.36 * std::min(1.0, damage)
                + 0.20 * cell.inflammatoryTone
                - 0.24 * functionalProtection,
            0.0,
            1.5);
        state.complementTag += (dtMs / 1800.0)
            * (targetComplement - state.complementTag);
        state.complementTag = std::clamp(state.complementTag, 0.0, 1.5);

        const double repairTarget = std::clamp(
            damage
                * (0.28 + 0.72 * functionalProtection)
                * cell.repairCapacity,
            0.0,
            1.5);
        state.repairSignal += experimentalMicrogliaFunction_ * (dtMs / 2400.0)
            * (repairTarget - state.repairSignal);
        state.repairSignal = std::clamp(state.repairSignal, 0.0, 3.0);
        state.surveillanceScore += experimentalMicrogliaFunction_ * (dtMs / 650.0)
            * (std::clamp(
                0.42 * state.complementTag
                    + 0.34 * damage
                    + 0.24 * cell.activation,
                0.0,
                1.5)
                - state.surveillanceScore);

        localComplement[cellIndex] += state.complementTag;
        localDamage[cellIndex] += damage;
        ++localCount[cellIndex];
    }

    for (std::size_t index = 0; index < microglia_.size(); ++index) {
        ++metrics_.microglialSurveillanceUpdates;
        auto& cell = microglia_[index];
        cell.damageLoad *= std::exp(-dtMs / 11000.0);
        cell.phagocyticLoad *= std::exp(-dtMs / 5200.0);
        const auto vesselIndex = std::min<std::size_t>(
            cell.vessel, capillaries_.size() - 1);
        const auto& vessel = capillaries_[vesselIndex];
        const double count = static_cast<double>(
            std::max<std::size_t>(1, localCount[index]));
        const double complement = localComplement[index] / count;
        const double damage = localDamage[index] / count;
        const double hypoxia = std::clamp(1.0 - vessel.oxygen, 0.0, 1.0);
        const double substrateStress = std::clamp(
            0.34 * cell.damageLoad
                + 0.28 * damage
                + 0.20 * complement
                + 0.18 * hypoxia,
            0.0,
            2.0);

        const double inflammatoryTarget = std::clamp(
            0.48 * substrateStress
                + 0.12 * cell.phagocyticLoad,
            0.0,
            1.5);
        cell.inflammatoryTone += (dtMs / 1900.0)
            * (inflammatoryTarget - cell.inflammatoryTone);
        cell.inflammatoryTone = std::clamp(cell.inflammatoryTone, 0.0, 1.5);

        const double activationTarget = std::clamp(
            0.06
                + 0.58 * cell.inflammatoryTone
                + 0.20 * complement
                + 0.16 * cell.damageLoad,
            0.04,
            1.5);
        cell.activation += (dtMs / 720.0)
            * (activationTarget - cell.activation);
        cell.activation = std::clamp(cell.activation, 0.04, 1.5);

        const double supply = std::clamp(
            0.42 * vessel.oxygen
                + 0.34 * vessel.glucose
                + 0.24 * std::min(1.5, vessel.flow),
            0.08,
            1.35);
        const double repairTarget = std::clamp(
            supply
                - 0.22 * cell.activation
                - 0.12 * cell.phagocyticLoad,
            0.05,
            1.15);
        cell.repairCapacity += experimentalMicrogliaFunction_ * (dtMs / 2600.0)
            * (repairTarget - cell.repairCapacity);
        cell.repairCapacity = std::clamp(cell.repairCapacity, 0.05, 1.15);

        const double surveillanceTarget = std::clamp(
            0.55
                + 0.26 * cell.activation
                + 0.14 * cell.repairCapacity
                + 0.05 * complement,
            0.35,
            1.25);
        cell.surveillance += (dtMs / 900.0)
            * (surveillanceTarget - cell.surveillance);
        cell.surveillance = std::clamp(cell.surveillance, 0.35, 1.25);
    }
    updateMetrics();
}

double SpatialSubstrate::energyRecoveryScale(std::size_t neuronIndex) const {
    if (astrocytes_.empty()) return 1.0;
    const std::size_t domain = std::min(
        astrocytes_.size() - 1,
        neuronIndex / kNeuronsPerAstrocyte);
    const double compressionPenalty = std::clamp(
        1.0 - 0.10 * mechanics_.pressureKPa, 0.45, 1.0);
    return astrocytes_[domain].support * compressionPenalty;
}

SpatialSubstrate::Vec3 SpatialSubstrate::scaledPoint(const Vec3& point) const {
    return {
        point.x * mechanics_.linearExpansion,
        point.y * mechanics_.linearExpansion,
        point.z * mechanics_.linearExpansion};
}

const BiologicalMetrics& SpatialSubstrate::metrics() const {
    return metrics_;
}

BiologicalSpatialState SpatialSubstrate::inspectSpatial() const {
    if (throughput_) {
        throughput_->recordRead<Morphology>(tatarus::ThroughputDomain::Neuron, morphologies_.size());
        throughput_->recordRead<Segment>(tatarus::ThroughputDomain::Dendrite, segments_.size());
        throughput_->recordRead<Astrocyte>(tatarus::ThroughputDomain::Astrocyte, astrocytes_.size());
        throughput_->recordRead<Capillary>(tatarus::ThroughputDomain::Capillary, capillaries_.size());
        throughput_->recordRead<Oligodendrocyte>(tatarus::ThroughputDomain::Oligodendrocyte, oligodendrocytes_.size());
        throughput_->recordRead<MyelinState>(tatarus::ThroughputDomain::Oligodendrocyte, myelin_.size());
        throughput_->recordRead<Microglia>(tatarus::ThroughputDomain::Microglia, microglia_.size());
        throughput_->recordRead<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia, maintenance_.size());
    }
    BiologicalSpatialState state;
    state.dtMs = dtMs_;
    const auto point = [this](const Vec3& value) {
        const auto scaled = scaledPoint(value);
        return SpatialPointView{scaled.x, scaled.y, scaled.z};
    };
    state.tissueRadii = {
        kBrainRadiusXUm * mechanics_.linearExpansion,
        kBrainRadiusYUm * mechanics_.linearExpansion,
        kBrainRadiusZUm * mechanics_.linearExpansion};
    state.interhemisphericFissureUm =
        kInterhemisphericFissureUm * mechanics_.linearExpansion;
    state.baselineVolumeUm3 = mechanics_.baselineVolumeUm3;
    state.currentVolumeUm3 = mechanics_.currentVolumeUm3;
    state.volumeRatio = mechanics_.baselineVolumeUm3 > 0.0
        ? mechanics_.currentVolumeUm3 / mechanics_.baselineVolumeUm3 : 1.0;
    state.linearExpansion = mechanics_.linearExpansion;
    const double effectiveSolidVolume =
        kNonSynapticSolidFraction * mechanics_.baselineVolumeUm3
        + mechanics_.synapticMaterialVolumeUm3;
    const double baselineSolidVolume =
        (kNonSynapticSolidFraction + kBaselineSynapticFraction)
        * mechanics_.baselineVolumeUm3;
    state.effectiveTissueMassNg = effectiveSolidVolume
        * kEffectiveTissueDensityPgPerUm3 / 1000.0;
    state.netBiomassChangeNg = (effectiveSolidVolume - baselineSolidVolume)
        * kEffectiveTissueDensityPgPerUm3 / 1000.0;
    state.solidPackingFraction = mechanics_.solidPackingFraction;
    state.extracellularSpaceFraction = mechanics_.extracellularSpaceFraction;
    state.pressureKPa = mechanics_.pressureKPa;
    state.materialReserve = mechanics_.materialReserve;

    state.neurons.reserve(morphologies_.size());
    for (std::size_t index = 0; index < morphologies_.size(); ++index) {
        const auto& morphology = morphologies_[index];
        SpatialNeuronView view;
        view.index = index;
        if (index < hemispheres_.size()) view.hemisphere = hemispheres_[index];
        if (index < regions_.size()) view.region = regions_[index];
        if (index < layers_.size()) view.layer = layers_[index];
        view.soma = point(morphology.soma);
        view.firstSegment = morphology.firstSegment;
        view.segmentCount = morphology.segmentCount;
        state.neurons.push_back(view);
    }

    state.dendrites.reserve(segments_.size());
    for (std::size_t index = 0; index < segments_.size(); ++index) {
        const auto& segment = segments_[index];
        state.dendrites.push_back(SpatialDendriteView{
            index,
            segment.neuron,
            segment.parent,
            point(segment.position),
            segment.membraneMv,
            segment.calcium,
            segment.localEnergy,
            segment.astrocyte,
            segment.branchOrder,
            segment.activeRefractory > 0});
    }

    state.axons.reserve(bindings_.size());
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        const auto& binding = bindings_[index];
        const MyelinState* myelin = index < myelin_.size()
            ? &myelin_[index]
            : nullptr;
        const SynapseMaintenance* maintenance = index < maintenance_.size()
            ? &maintenance_[index]
            : nullptr;
        const std::uint32_t pre = myelin
            ? myelin->preNeuron
            : (maintenance ? maintenance->preNeuron : 0U);
        const std::uint32_t post = myelin
            ? myelin->postNeuron
            : (maintenance ? maintenance->postNeuron : 0U);
        SpatialAxonView view;
        view.index = index;
        view.pre = pre;
        view.post = post;
        view.segment = binding.segment;
        if (pre < morphologies_.size()) {
            view.start = point(morphologies_[pre].soma);
        }
        if (binding.segment < segments_.size()) {
            view.end = point(segments_[binding.segment].position);
        } else if (post < morphologies_.size()) {
            view.end = point(morphologies_[post].soma);
        }
        view.lengthUm = binding.axonLengthUm;
        if (myelin) {
            view.myelinCoverage = myelin->coverage * experimentalMyelinIntegrity_;
            view.axonCaliber = myelin->axonCaliber;
            view.conductionVelocityUmPerMs =
                myelin->conductionVelocityUmPerMs;
            view.effectiveDelayMs = 0.25
                + binding.axonLengthUm * mechanics_.linearExpansion
                / std::max(90.0, myelin->conductionVelocityUmPerMs);
            view.oligodendrocyte = myelin->oligodendrocyte;
        }
        if (maintenance) {
            view.complementTag = maintenance->complementTag;
            view.damageSignal = maintenance->damageSignal;
            view.repairSignal = maintenance->repairSignal;
            view.microglia = maintenance->microglia;
        }
        state.axons.push_back(view);
    }

    state.astrocytes.reserve(astrocytes_.size());
    for (std::size_t index = 0; index < astrocytes_.size(); ++index) {
        const auto& astro = astrocytes_[index];
        state.astrocytes.push_back(SpatialAstrocyteView{
            index,
            point(astro.center),
            astro.calcium,
            astro.glutamateLoad,
            astro.potassiumLoad,
            astro.metabolicDemand,
            astro.support,
            astro.vessel});
    }

    state.capillaries.reserve(capillaries_.size());
    for (std::size_t index = 0; index < capillaries_.size(); ++index) {
        const auto& capillary = capillaries_[index];
        state.capillaries.push_back(SpatialCapillaryView{
            index,
            point(capillary.position),
            capillary.flow,
            capillary.oxygen,
            capillary.glucose});
    }

    state.oligodendrocytes.reserve(oligodendrocytes_.size());
    for (std::size_t index = 0; index < oligodendrocytes_.size(); ++index) {
        const auto& oligo = oligodendrocytes_[index];
        state.oligodendrocytes.push_back(SpatialOligodendrocyteView{
            index,
            point(oligo.center),
            oligo.myelinReserve,
            oligo.metabolicLoad,
            oligo.remodelingSignal,
            oligo.vessel});
    }

    state.microglia.reserve(microglia_.size());
    for (std::size_t index = 0; index < microglia_.size(); ++index) {
        const auto& cell = microglia_[index];
        state.microglia.push_back(SpatialMicrogliaView{
            index,
            point(cell.center),
            cell.activation,
            cell.inflammatoryTone,
            cell.phagocyticLoad,
            cell.repairCapacity,
            cell.surveillance,
            cell.damageLoad,
            cell.vessel});
    }
    return state;
}

void SpatialSubstrate::updateMetrics() {
    if (throughput_) {
        throughput_->recordRead<Segment>(tatarus::ThroughputDomain::Dendrite, segments_.size());
        throughput_->recordRead<Astrocyte>(tatarus::ThroughputDomain::Astrocyte, astrocytes_.size());
        throughput_->recordRead<Capillary>(tatarus::ThroughputDomain::Capillary, capillaries_.size());
        throughput_->recordRead<Oligodendrocyte>(tatarus::ThroughputDomain::Oligodendrocyte, oligodendrocytes_.size());
        throughput_->recordRead<MyelinState>(tatarus::ThroughputDomain::Oligodendrocyte, myelin_.size());
        throughput_->recordRead<Microglia>(tatarus::ThroughputDomain::Microglia, microglia_.size());
        throughput_->recordRead<SynapseMaintenance>(tatarus::ThroughputDomain::Microglia, maintenance_.size());
    }
    metrics_.dendriteSegments = static_cast<int>(segments_.size());
    metrics_.astrocytes = static_cast<int>(astrocytes_.size());
    metrics_.capillaries = static_cast<int>(capillaries_.size());
    metrics_.oligodendrocytes = static_cast<int>(oligodendrocytes_.size());
    metrics_.microglia = static_cast<int>(microglia_.size());
    metrics_.meanDendriticCalcium = 0.0;
    metrics_.meanAstrocyteCalcium = 0.0;
    metrics_.meanOxygen = 0.0;
    metrics_.meanGlucose = 0.0;
    metrics_.meanBloodFlow = 0.0;
    metrics_.meanAxonLengthUm = 0.0;
    metrics_.meanLocalEnergy = 0.0;
    metrics_.meanMyelinCoverage = 0.0;
    metrics_.meanConductionVelocityUmPerMs = 0.0;
    metrics_.meanEffectiveDelayMs = 0.0;
    metrics_.meanOligodendrocyteReserve = 0.0;
    metrics_.meanMicroglialActivation = 0.0;
    metrics_.meanComplementTag = 0.0;
    metrics_.meanMicroglialRepairCapacity = 0.0;
    metrics_.meanInflammatoryTone = 0.0;
    metrics_.baselineActiveSynapses = mechanics_.baselineActiveSynapses;
    metrics_.mechanicsActiveSynapses = mechanics_.activeSynapses;
    metrics_.growthLimitedEvents = mechanics_.growthLimitedEvents;
    metrics_.baselineTissueVolumeUm3 = mechanics_.baselineVolumeUm3;
    metrics_.tissueVolumeUm3 = mechanics_.currentVolumeUm3;
    metrics_.tissueVolumeRatio = mechanics_.baselineVolumeUm3 > 0.0
        ? mechanics_.currentVolumeUm3 / mechanics_.baselineVolumeUm3 : 1.0;
    metrics_.linearExpansion = mechanics_.linearExpansion;
    const double effectiveSolidVolume =
        kNonSynapticSolidFraction * mechanics_.baselineVolumeUm3
        + mechanics_.synapticMaterialVolumeUm3;
    const double baselineSolidVolume =
        (kNonSynapticSolidFraction + kBaselineSynapticFraction)
        * mechanics_.baselineVolumeUm3;
    metrics_.effectiveTissueMassNg = effectiveSolidVolume
        * kEffectiveTissueDensityPgPerUm3 / 1000.0;
    metrics_.netBiomassChangeNg = (effectiveSolidVolume - baselineSolidVolume)
        * kEffectiveTissueDensityPgPerUm3 / 1000.0;
    metrics_.synapticMaterialVolumeUm3 = mechanics_.synapticMaterialVolumeUm3;
    metrics_.solidPackingFraction = mechanics_.solidPackingFraction;
    metrics_.extracellularSpaceFraction = mechanics_.extracellularSpaceFraction;
    metrics_.tissuePressureKPa = mechanics_.pressureKPa;
    metrics_.materialReserve = mechanics_.materialReserve;
    metrics_.cumulativeMaterialSynthesizedUm3 = mechanics_.cumulativeSynthesizedUm3;
    metrics_.cumulativeMaterialRecycledUm3 = mechanics_.cumulativeRecycledUm3;

    for (const auto& segment : segments_) {
        metrics_.meanDendriticCalcium += segment.calcium;
        metrics_.meanLocalEnergy += segment.localEnergy;
    }
    if (!segments_.empty()) {
        metrics_.meanDendriticCalcium /= segments_.size();
        metrics_.meanLocalEnergy /= segments_.size();
    }
    for (const auto& astro : astrocytes_) {
        metrics_.meanAstrocyteCalcium += astro.calcium;
    }
    if (!astrocytes_.empty()) {
        metrics_.meanAstrocyteCalcium /= astrocytes_.size();
    }
    for (const auto& vessel : capillaries_) {
        metrics_.meanOxygen += vessel.oxygen;
        metrics_.meanGlucose += vessel.glucose;
        metrics_.meanBloodFlow += vessel.flow;
    }
    if (!capillaries_.empty()) {
        metrics_.meanOxygen /= capillaries_.size();
        metrics_.meanGlucose /= capillaries_.size();
        metrics_.meanBloodFlow /= capillaries_.size();
    }
    for (const auto& binding : bindings_) {
        metrics_.meanAxonLengthUm +=
            binding.axonLengthUm * mechanics_.linearExpansion;
    }
    if (!bindings_.empty()) {
        metrics_.meanAxonLengthUm /= bindings_.size();
    }
    for (const auto& oligo : oligodendrocytes_) {
        metrics_.meanOligodendrocyteReserve += oligo.myelinReserve;
    }
    if (!oligodendrocytes_.empty()) {
        metrics_.meanOligodendrocyteReserve /= oligodendrocytes_.size();
    }
    for (const auto& cell : microglia_) {
        metrics_.meanMicroglialActivation += cell.activation;
        metrics_.meanMicroglialRepairCapacity += cell.repairCapacity;
        metrics_.meanInflammatoryTone += cell.inflammatoryTone;
    }
    if (!microglia_.empty()) {
        metrics_.meanMicroglialActivation /= microglia_.size();
        metrics_.meanMicroglialRepairCapacity /= microglia_.size();
        metrics_.meanInflammatoryTone /= microglia_.size();
    }
    for (const auto& state : maintenance_) {
        metrics_.meanComplementTag += state.complementTag;
    }
    if (!maintenance_.empty()) {
        metrics_.meanComplementTag /= maintenance_.size();
    }
    for (std::size_t index = 0; index < myelin_.size(); ++index) {
        const auto& state = myelin_[index];
        metrics_.meanMyelinCoverage += state.coverage * experimentalMyelinIntegrity_;
        metrics_.meanConductionVelocityUmPerMs += state.conductionVelocityUmPerMs;
        if (index < bindings_.size()) {
            metrics_.meanEffectiveDelayMs +=
                0.25 + bindings_[index].axonLengthUm * mechanics_.linearExpansion
                    / std::max(90.0, state.conductionVelocityUmPerMs);
        }
    }
    if (!myelin_.empty()) {
        metrics_.meanMyelinCoverage /= myelin_.size();
        metrics_.meanConductionVelocityUmPerMs /= myelin_.size();
        metrics_.meanEffectiveDelayMs /= myelin_.size();
    }
}

std::uint64_t SpatialSubstrate::stateHash() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto addBytes = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    };
    const auto add = [&addBytes](const auto& value) {
        addBytes(&value, sizeof(value));
    };
    const auto addVec3 = [&add](const Vec3& value) {
        add(value.x);
        add(value.y);
        add(value.z);
    };

    add(seed_);
    add(restingMv_);
    add(dtMs_);
    for (const auto& morphology : morphologies_) {
        addVec3(morphology.soma);
        add(morphology.firstSegment);
        add(morphology.segmentCount);
    }
    for (const auto role : roles_) add(role);
    for (const auto hemisphere : hemispheres_) add(hemisphere);
    for (const auto region : regions_) add(region);
    for (const auto layer : layers_) add(layer);
    for (const auto& segment : segments_) {
        add(segment.neuron);
        add(segment.parent);
        addVec3(segment.position);
        add(segment.membraneMv);
        add(segment.calcium);
        add(segment.gAmpa);
        add(segment.gNmda);
        add(segment.gGabaA);
        add(segment.gGabaB);
        add(segment.localEnergy);
        add(segment.astrocyte);
        add(segment.branchOrder);
        add(segment.activeRefractory);
    }
    for (const auto& binding : bindings_) {
        add(binding.segment);
        add(binding.axonLengthUm);
    }
    for (const auto& astrocyte : astrocytes_) {
        addVec3(astrocyte.center);
        add(astrocyte.calcium);
        add(astrocyte.glutamateLoad);
        add(astrocyte.potassiumLoad);
        add(astrocyte.metabolicDemand);
        add(astrocyte.support);
        add(astrocyte.vessel);
    }
    for (const auto& capillary : capillaries_) {
        addVec3(capillary.position);
        add(capillary.flow);
        add(capillary.oxygen);
        add(capillary.glucose);
    }
    for (const auto& oligo : oligodendrocytes_) {
        addVec3(oligo.center);
        add(oligo.myelinReserve);
        add(oligo.metabolicLoad);
        add(oligo.remodelingSignal);
        add(oligo.vessel);
    }
    for (const auto& state : myelin_) {
        add(state.preNeuron);
        add(state.postNeuron);
        add(state.oligodendrocyte);
        add(state.coverage);
        add(state.axonCaliber);
        add(state.activityTrace);
        add(state.arrivalTrace);
        add(state.coincidenceTrace);
        add(state.remodelingRate);
        add(state.conductionVelocityUmPerMs);
    }
    for (const auto& cell : microglia_) {
        addVec3(cell.center);
        add(cell.activation);
        add(cell.inflammatoryTone);
        add(cell.phagocyticLoad);
        add(cell.repairCapacity);
        add(cell.surveillance);
        add(cell.damageLoad);
        add(cell.vessel);
    }
    for (const auto& state : maintenance_) {
        add(state.preNeuron);
        add(state.postNeuron);
        add(state.microglia);
        add(state.functionalTrace);
        add(state.complementTag);
        add(state.damageSignal);
        add(state.repairSignal);
        add(state.surveillanceScore);
    }
    add(metrics_.dendriticSpikes);
    add(metrics_.myelinRemodelingUpdates);
    add(metrics_.microglialSurveillanceUpdates);
    add(metrics_.microglialPruningEvents);
    add(metrics_.microglialRepairEvents);
    add(metrics_.microglialDamageSignals);
    add(mechanics_.baselineActiveSynapses);
    add(mechanics_.activeSynapses);
    add(mechanics_.growthLimitedEvents);
    add(mechanics_.baselineVolumeUm3);
    add(mechanics_.currentVolumeUm3);
    add(mechanics_.linearExpansion);
    add(mechanics_.synapticMaterialVolumeUm3);
    add(mechanics_.solidPackingFraction);
    add(mechanics_.extracellularSpaceFraction);
    add(mechanics_.pressureKPa);
    add(mechanics_.materialReserve);
    add(mechanics_.cumulativeSynthesizedUm3);
    add(mechanics_.cumulativeRecycledUm3);
    add(mechanics_.previousSynapticMaterialUm3);
    add(mechanics_.initialized);
    return hash;
}

void SpatialSubstrate::save(std::ostream& output) const {
    const std::array<char, 4> magic{'B', 'I', 'O', '5'};
    output.write(magic.data(), magic.size());
    writePod(output, seed_);
    writePod(output, restingMv_);
    writePod(output, dtMs_);
    writeVector(output, morphologies_);
    writeVector(output, roles_);
    writeVector(output, hemispheres_);
    writeVector(output, regions_);
    writeVector(output, layers_);
    writeVector(output, segments_);
    writeVector(output, bindings_);
    writeVector(output, astrocytes_);
    writeVector(output, capillaries_);
    writeVector(output, oligodendrocytes_);
    writeVector(output, myelin_);
    writeVector(output, microglia_);
    writeVector(output, maintenance_);
    writePod(output, metrics_);
    writePod(output, mechanics_);
}

bool SpatialSubstrate::load(
    std::istream& input,
    std::size_t expectedNeuronCount,
    double restingMv,
    double dtMs) {
    const auto start = input.tellg();
    std::array<char, 4> magic{};
    input.read(magic.data(), magic.size());
    const std::array<char, 4> bio1{'B', 'I', 'O', '1'};
    const std::array<char, 4> bio2{'B', 'I', 'O', '2'};
    const std::array<char, 4> bio3{'B', 'I', 'O', '3'};
    const std::array<char, 4> bio4{'B', 'I', 'O', '4'};
    const std::array<char, 4> bio5{'B', 'I', 'O', '5'};
    if (input.gcount() != static_cast<std::streamsize>(magic.size())
        || (magic != bio1 && magic != bio2 && magic != bio3
            && magic != bio4 && magic != bio5)) {
        input.clear();
        if (start != std::streampos(-1)) input.seekg(start);
        return false;
    }

    readPod(input, seed_);
    readPod(input, restingMv_);
    readPod(input, dtMs_);
    readVector(input, morphologies_, expectedNeuronCount + 1);
    if (magic == bio4 || magic == bio5) {
        readVector(input, roles_, expectedNeuronCount + 1);
        readVector(input, hemispheres_, expectedNeuronCount + 1);
        readVector(input, regions_, expectedNeuronCount + 1);
        readVector(input, layers_, expectedNeuronCount + 1);
    }
    readVector(input, segments_, expectedNeuronCount * 16 + 16);
    readVector(input, bindings_, 10'000'000);
    readVector(input, astrocytes_, expectedNeuronCount + 1);
    readVector(input, capillaries_, expectedNeuronCount + 1);

    metrics_ = {};
    mechanics_ = {};
    mechanics_.baselineVolumeUm3 = 4.0 / 3.0 * kPi
        * kBrainRadiusXUm * kBrainRadiusYUm * kBrainRadiusZUm;
    mechanics_.currentVolumeUm3 = mechanics_.baselineVolumeUm3;
    if (magic == bio5) {
        readVector(input, oligodendrocytes_, expectedNeuronCount + 1);
        readVector(input, myelin_, 10'000'000);
        readVector(input, microglia_, expectedNeuronCount + 1);
        readVector(input, maintenance_, 10'000'000);
        readPod(input, metrics_);
        readPod(input, mechanics_);
    } else if (magic == bio3 || magic == bio4) {
        readVector(input, oligodendrocytes_, expectedNeuronCount + 1);
        readVector(input, myelin_, 10'000'000);
        readVector(input, microglia_, expectedNeuronCount + 1);
        readVector(input, maintenance_, 10'000'000);
        LegacyBiologicalMetricsV4 legacy;
        readPod(input, legacy);
        metrics_.dendriticSpikes = legacy.dendriticSpikes;
        metrics_.myelinRemodelingUpdates = legacy.myelinRemodelingUpdates;
        metrics_.microglialSurveillanceUpdates = legacy.microglialSurveillanceUpdates;
        metrics_.microglialPruningEvents = legacy.microglialPruningEvents;
        metrics_.microglialRepairEvents = legacy.microglialRepairEvents;
        metrics_.microglialDamageSignals = legacy.microglialDamageSignals;
    } else if (magic == bio2) {
        readVector(input, oligodendrocytes_, expectedNeuronCount + 1);
        readVector(input, myelin_, 10'000'000);
        LegacyBiologicalMetricsV2 legacy;
        readPod(input, legacy);
        metrics_.dendriticSpikes = legacy.dendriticSpikes;
        metrics_.myelinRemodelingUpdates = legacy.myelinRemodelingUpdates;
        rebuildMicroglia();
        maintenance_.clear();
    } else {
        LegacyBiologicalMetricsV1 legacy;
        readPod(input, legacy);
        metrics_.dendriticSpikes = legacy.dendriticSpikes;
        rebuildOligodendrocytes();
        rebuildMicroglia();
        myelin_.clear();
        maintenance_.clear();
    }

    if (!input
        || morphologies_.size() != expectedNeuronCount
        || segments_.size() != expectedNeuronCount * kSegmentsPerNeuron
        || astrocytes_.empty()
        || capillaries_.empty()
        || oligodendrocytes_.empty()
        || microglia_.empty()
        || ((magic == bio4 || magic == bio5) && (roles_.size() != expectedNeuronCount
            || hemispheres_.size() != expectedNeuronCount
            || regions_.size() != expectedNeuronCount
            || layers_.size() != expectedNeuronCount))
        || ((magic == bio2 || magic == bio3 || magic == bio4 || magic == bio5)
            && myelin_.size() != bindings_.size())
        || ((magic == bio3 || magic == bio4 || magic == bio5)
            && maintenance_.size() != bindings_.size())) {
        throw std::runtime_error("Biologischer Snapshot ist inkonsistent");
    }
    restingMv_ = restingMv;
    dtMs_ = dtMs;
    updateMetrics();
    return true;
}

}  // namespace tatarus::neuro::biology
