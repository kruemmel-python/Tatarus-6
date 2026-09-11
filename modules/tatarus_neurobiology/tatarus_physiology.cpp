#include "tatarus_physiology.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <istream>
#include <limits>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace tatarus::neuro::physiology {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGasConstant = 8.31446261815324;
constexpr double kFaraday = 96485.33212;
constexpr double kTemperatureK = 310.15;
constexpr double kEcsBaselineNa = 145.0;
constexpr double kEcsBaselineK = 3.5;
constexpr double kEcsBaselineCa = 1.2;
constexpr double kEcsBaselineCl = 130.0;
constexpr double kIcsBaselineNa = 12.0;
constexpr double kIcsBaselineK = 140.0;
constexpr double kIcsBaselineCa = 0.00010;
constexpr double kIcsBaselineCl = 7.0;

double clampFinite(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

double approach(double value, double target, double dtMs, double tauMs) {
    const double alpha = 1.0 - std::exp(-dtMs / std::max(1e-6, tauMs));
    return value + alpha * (target - value);
}

double logistic(double value) {
    return 1.0 / (1.0 + std::exp(-std::clamp(value, -40.0, 40.0)));
}

double nernstMv(double outside, double inside, double charge) {
    const double ratio = std::max(1e-12, outside) / std::max(1e-12, inside);
    return 1000.0 * kGasConstant * kTemperatureK / (charge * kFaraday)
        * std::log(ratio);
}

double distance(const SpatialPointView& a, const SpatialPointView& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double seededUnit(std::uint64_t seed, std::size_t index, std::uint64_t channel) {
    std::uint64_t value = seed
        ^ (0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(index) + 1ULL))
        ^ (0xBF58476D1CE4E5B9ULL * (channel + 1ULL));
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return static_cast<double>(value >> 11U) / 9007199254740992.0;
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) throw std::runtime_error("Physiologie-Snapshot konnte nicht geschrieben werden");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("Physiologie-Snapshot ist abgeschnitten");
}

template <class T>
void writeVector(std::ostream& output, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t count = values.size();
    writePod(output, count);
    if (!values.empty()) {
        output.write(reinterpret_cast<const char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
    if (!output) throw std::runtime_error("Physiologie-Vektor konnte nicht geschrieben werden");
}

template <class T>
void readVector(std::istream& input, std::vector<T>& values, std::uint64_t maximum) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint64_t count = 0;
    readPod(input, count);
    if (count > maximum) throw std::runtime_error("Physiologie-Vektor ist unplausibel gross");
    values.resize(static_cast<std::size_t>(count));
    if (!values.empty()) {
        input.read(reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
    if (!input) throw std::runtime_error("Physiologie-Vektor ist abgeschnitten");
}

std::uint64_t hashBytes(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

struct PhysiologicalSubstrate::Node {
    SpatialPointView position;
    PopulationRole role = PopulationRole::Excitatory;
    NeuronSubtype subtype = NeuronSubtype::Pyramidal;
    double ecsNa = kEcsBaselineNa;
    double ecsK = kEcsBaselineK;
    double ecsCa = kEcsBaselineCa;
    double ecsCl = kEcsBaselineCl;
    double icsNa = kIcsBaselineNa;
    double icsK = kIcsBaselineK;
    double icsCa = kIcsBaselineCa;
    double icsCl = kIcsBaselineCl;
    double atp = 0.92;
    double pumpActivity = 0.0;
    double excitabilityShiftMv = 0.0;
    double channelBiasMv = 0.0;
    std::array<double, 4> modulator{0.02, 0.04, 0.08, 0.06};
    std::array<double, 4> receptor{};
    double camp = 0.5;
    double ip3 = 0.2;
    double hcnDensity = 1.0;
    double kvDensity = 1.0;
    double hcnCurrent = 0.0;
    double kvCurrent = 0.0;
    double pCreb = 0.0;
    double mrna = 0.0;
    double protein = 0.0;
    double homer1a = 0.0;
    double astrocyteSupport = 0.7;
    double waste = 0.0;
};

struct PhysiologicalSubstrate::Edge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    double coupling = 0.0;
};

struct PhysiologicalSubstrate::SynapseMolecule {
    double tag = 0.0;
    double tagPolarity = 0.0;
    double capturedProtein = 0.0;
    bool countedLtp = false;
    bool countedLtd = false;
};

PhysiologicalSubstrate::~PhysiologicalSubstrate() = default;

void PhysiologicalSubstrate::setExperimentalFactors(
    double pumpEfficiency,
    double astrocyteFunction,
    double neuromodulatorGain,
    double metabolicSupply,
    bool sleepEnabled) {
    experimentalPumpEfficiency_ = std::clamp(pumpEfficiency, 0.0, 1.5);
    experimentalAstrocyteFunction_ = std::clamp(astrocyteFunction, 0.0, 1.5);
    experimentalNeuromodulatorGain_ = std::clamp(neuromodulatorGain, 0.0, 2.0);
    experimentalMetabolicSupply_ = std::clamp(metabolicSupply, 0.0, 1.5);
    experimentalSleepEnabled_ = sleepEnabled;
    if (!experimentalSleepEnabled_ && sleepStage_ != SleepStage::Wake) {
        sleepStage_ = SleepStage::Wake;
        stageMs_ = 0.0;
        ++sleepTransitions_;
    }
    updateMetrics();
}

PhysiologicalSubstrate::PhysiologicalSubstrate(
    const NervousSystemConfig& config,
    const BiologicalSpatialState& spatial,
    const std::vector<PopulationRole>& roles,
    const std::vector<NeuronSubtype>& subtypes)
    : config_(config) {
    const std::size_t count = roles.size();
    nodes_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& node = nodes_[i];
        node.position = i < spatial.neurons.size()
            ? spatial.neurons[i].soma
            : SpatialPointView{static_cast<double>(i), 0.0, 0.0};
        node.role = roles[i];
        node.subtype = i < subtypes.size() ? subtypes[i] : NeuronSubtype::Pyramidal;
        for (std::size_t receptor = 0; receptor < node.receptor.size(); ++receptor) {
            node.receptor[receptor] = 0.65 + 0.70 * seededUnit(config.seed, i, receptor);
        }
        node.atp = 0.86 + 0.12 * seededUnit(config.seed, i, 9);
        double distal = 0.0;
        int segmentCount = 0;
        for (const auto& segment : spatial.dendrites) {
            if (segment.neuron != i) continue;
            distal += std::max(0.0, distance(node.position, segment.position));
            ++segmentCount;
        }
        distal /= std::max(1, segmentCount);
        const double gradient = std::clamp(distal / 180.0, 0.0, 1.0);
        node.hcnDensity = 1.0 + 5.0 * gradient;
        node.kvDensity = 1.0 + 4.0 * gradient;
        if (!spatial.astrocytes.empty()) {
            double nearest = std::numeric_limits<double>::max();
            for (const auto& astrocyte : spatial.astrocytes) {
                nearest = std::min(nearest, distance(node.position, astrocyte.center));
            }
            node.astrocyteSupport = std::clamp(1.10 - nearest / 600.0, 0.25, 1.0);
        }
    }

    // A sparse deterministic neighbour graph is sufficient for the local ECS
    // finite-volume approximation and keeps 65k-neuron configurations usable.
    constexpr std::size_t neighbours = 6;
    if (count <= 2048) {
        for (std::size_t a = 0; a < count; ++a) {
            std::vector<std::pair<double, std::size_t>> nearest;
            nearest.reserve(std::min(count, neighbours + 1));
            for (std::size_t b = 0; b < count; ++b) {
                if (a == b) continue;
                const double d = distance(nodes_[a].position, nodes_[b].position);
                if (nearest.size() < neighbours) {
                    nearest.emplace_back(d, b);
                    std::sort(nearest.begin(), nearest.end());
                } else if (d < nearest.back().first) {
                    nearest.back() = {d, b};
                    std::sort(nearest.begin(), nearest.end());
                }
            }
            for (const auto& [d, b] : nearest) {
                if (b <= a) continue;
                diffusionEdges_.push_back(Edge{
                    static_cast<std::uint32_t>(a),
                    static_cast<std::uint32_t>(b),
                    std::exp(-d / 220.0)});
            }
        }
    } else {
        // Large sparse substrates keep O(N) construction. TATARUS placement
        // is seed-stable, so six coprime index strides create a deterministic
        // spatial sample; actual Euclidean distance still controls coupling.
        constexpr std::array<std::size_t, neighbours> strides{1, 7, 31, 127, 509, 2039};
        diffusionEdges_.reserve(count * neighbours);
        for (std::size_t a = 0; a < count; ++a) {
            for (const auto stride : strides) {
                const std::size_t b = (a + stride) % count;
                if (a == b) continue;
                const double d = distance(nodes_[a].position, nodes_[b].position);
                diffusionEdges_.push_back(Edge{
                    static_cast<std::uint32_t>(a),
                    static_cast<std::uint32_t>(b),
                    std::exp(-d / 220.0)});
            }
        }
    }
    updateMetrics();
}

void PhysiologicalSubstrate::updateSleep(double dtMs) {
    if (throughput_) throughput_->recordReadWrite<double>(tatarus::ThroughputDomain::Physiology, 10);
    simulatedMs_ += dtMs;
    stageMs_ += dtMs;
    const double cycle = std::max(1.0, config_.circadianCycleMs);
    circadianPhaseHours_ = std::fmod(8.0 + simulatedMs_ * 24.0 / cycle, 24.0);
    const double nightDrive = 0.5 + 0.5 * std::cos(
        2.0 * kPi * (circadianPhaseHours_ - 2.0) / 24.0);
    const auto transition = [this](SleepStage next) {
        if (sleepStage_ != next) {
            sleepStage_ = next;
            stageMs_ = 0.0;
            ++sleepTransitions_;
        }
    };
    if (!experimentalSleepEnabled_) {
        if (sleepStage_ != SleepStage::Wake) transition(SleepStage::Wake);
        sleepPressure_ = std::clamp(
            sleepPressure_ + dtMs / std::max(1.0, config_.sleepPressureTauMs),
            0.0, 1.2);
    } else if (sleepStage_ == SleepStage::Wake) {
        sleepPressure_ = std::clamp(
            sleepPressure_ + dtMs / std::max(1.0, config_.sleepPressureTauMs),
            0.0, 1.2);
        if (sleepPressure_ > 0.72 && nightDrive > 0.58) transition(SleepStage::Nrem);
    } else if (sleepStage_ == SleepStage::Nrem) {
        sleepPressure_ = std::max(0.0,
            sleepPressure_ - 1.6 * dtMs / std::max(1.0, config_.sleepPressureTauMs));
        if (stageMs_ >= config_.nremMinimumMs) transition(SleepStage::Rem);
    } else {
        sleepPressure_ = std::max(0.0,
            sleepPressure_ - 0.45 * dtMs / std::max(1.0, config_.sleepPressureTauMs));
        if (stageMs_ >= config_.remMinimumMs) {
            if (sleepPressure_ < 0.18 || nightDrive < 0.35) transition(SleepStage::Wake);
            else transition(SleepStage::Nrem);
        }
    }
    const double targetEcs = sleepStage_ == SleepStage::Nrem ? 0.32
        : sleepStage_ == SleepStage::Rem ? 0.24 : 0.20;
    extracellularVolumeFraction_ = approach(
        extracellularVolumeFraction_, targetEcs, dtMs, 90'000.0);
    const double clearance = sleepStage_ == SleepStage::Nrem
        ? 0.0000028 : sleepStage_ == SleepStage::Rem ? 0.0000008 : 0.00000025;
    tissueWaste_ = std::max(0.0, tissueWaste_ - clearance * dtMs * tissueWaste_);
}

void PhysiologicalSubstrate::updateNeuromodulators(
    const SensorFrame& frame, double dtMs) {
    if (throughput_) throughput_->recordReadWrite<Node>(tatarus::ThroughputDomain::Physiology, nodes_.size());
    std::vector<std::array<double, 4>> deltas(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        const double wake = sleepStage_ == SleepStage::Wake ? 1.0 : 0.15;
        std::array<double, 4> release{};
        if (node.role == PopulationRole::Modulatory) {
            const std::size_t channel = i % 4U;
            if (channel == 0) release[0] = 0.025 * std::max(0.0, frame.reward)
                + 0.010 * std::abs(frame.reward);
            if (channel == 1) release[1] = 0.004 + 0.010 * sleepPressure_;
            if (channel == 2) release[2] = wake * (0.008 + 0.020 * std::abs(frame.novelty));
            if (channel == 3) release[3] = (sleepStage_ == SleepStage::Rem ? 0.020 : 0.006)
                + 0.020 * std::abs(frame.novelty);
        }
        for (std::size_t m = 0; m < 4; ++m) {
            const double baseline = m == 0 ? 0.02 : m == 1 ? 0.04 : m == 2 ? 0.08 : 0.06;
            node.modulator[m] = approach(
                node.modulator[m], baseline, dtMs, 18'000.0 + 7'000.0 * m)
                + experimentalNeuromodulatorGain_ * release[m] * dtMs;
            node.modulator[m] = std::clamp(node.modulator[m], 0.0, 3.0);
        }
    }
    for (const auto& edge : diffusionEdges_) {
        const auto a = static_cast<std::size_t>(edge.a);
        const auto b = static_cast<std::size_t>(edge.b);
        for (std::size_t m = 0; m < 4; ++m) {
            const double flux = 0.000035 * edge.coupling * dtMs
                * (nodes_[a].modulator[m] - nodes_[b].modulator[m]);
            deltas[a][m] -= flux;
            deltas[b][m] += flux;
        }
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        for (std::size_t m = 0; m < 4; ++m) {
            node.modulator[m] = std::clamp(node.modulator[m] + deltas[i][m], 0.0, 3.0);
        }
        const double dopamine = node.modulator[0] * node.receptor[0];
        const double serotonin = node.modulator[1] * node.receptor[1];
        const double noradrenaline = node.modulator[2] * node.receptor[2];
        const double acetylcholine = node.modulator[3] * node.receptor[3];
        const double campTarget = std::clamp(
            0.45 + 0.55 * dopamine + 0.28 * noradrenaline - 0.18 * serotonin,
            0.0, 3.0);
        const double ip3Target = std::clamp(
            0.18 + 0.40 * acetylcholine + 0.24 * serotonin,
            0.0, 3.0);
        node.camp = approach(node.camp, campTarget, dtMs, 4'000.0);
        node.ip3 = approach(node.ip3, ip3Target, dtMs, 7'000.0);
    }
}

void PhysiologicalSubstrate::updateIons(
    const SensorFrame& frame,
    const std::vector<std::uint8_t>& spikes,
    const std::vector<double>& energy,
    double dtMs) {
    if (throughput_) {
        throughput_->recordReadWrite<Node>(tatarus::ThroughputDomain::Physiology, nodes_.size());
        throughput_->recordRead<Edge>(tatarus::ThroughputDomain::Physiology, diffusionEdges_.size());
    }
    struct Delta { double na = 0.0; double k = 0.0; double ca = 0.0; double cl = 0.0; };
    std::vector<Delta> ecs(nodes_.size());
    const double ecsVolume = std::max(0.12, extracellularVolumeFraction_);
    const double icsToEcs = (1.0 - ecsVolume) / ecsVolume;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        const bool spiked = i < spikes.size() && spikes[i] != 0;
        if (spiked) {
            const double naFlux = 0.0018;
            const double kFlux = 0.0012;
            const double caFlux = 0.000004;
            node.icsNa += naFlux;
            node.ecsNa -= naFlux / icsToEcs;
            node.icsK -= kFlux;
            node.ecsK += kFlux / icsToEcs;
            node.icsCa += caFlux;
            node.ecsCa -= caFlux / icsToEcs;
            node.atp -= 0.0008;
            node.waste += 0.0005;
            tissueWaste_ += 0.0005 / std::max<std::size_t>(1, nodes_.size());
        }
        const double pumpNa = logistic((node.icsNa - 10.0) / 2.0);
        const double pumpK = logistic((node.ecsK - 2.8) / 0.6);
        node.pumpActivity = experimentalPumpEfficiency_
            * std::clamp(node.atp, 0.0, 1.0) * pumpNa * pumpK;
        const double pumpFlux = 0.0000040 * dtMs * node.pumpActivity;
        node.icsNa = std::max(2.0, node.icsNa - 3.0 * pumpFlux);
        node.ecsNa += 3.0 * pumpFlux / icsToEcs;
        node.icsK += 2.0 * pumpFlux;
        node.ecsK = std::max(1.0, node.ecsK - 2.0 * pumpFlux / icsToEcs);
        const double atpCost = pumpFlux * 0.20;
        node.atp -= atpCost;
        cumulativeAtpConsumed_ += atpCost;

        const double buffer = experimentalAstrocyteFunction_
            * 0.000030 * dtMs * node.astrocyteSupport
            * std::max(0.0, node.ecsK - kEcsBaselineK);
        node.ecsK -= buffer;
        // Potassium buffering is represented as spatial sequestration and slow
        // return, not destruction of the ion.
        node.icsK += buffer / std::max(1.0, icsToEcs);
        const double substrateEnergy = i < energy.size() ? energy[i] : frame.internalEnergy;
        const double recovery = experimentalMetabolicSupply_ * 0.000012 * dtMs
            * std::clamp(0.5 * frame.internalEnergy + 0.5 * substrateEnergy, 0.0, 1.2);
        node.atp = std::clamp(node.atp + recovery, 0.02, 1.15);
    }
    for (const auto& edge : diffusionEdges_) {
        const auto a = static_cast<std::size_t>(edge.a);
        const auto b = static_cast<std::size_t>(edge.b);
        const double rate = 0.000018 * dtMs * edge.coupling;
        const auto flux = [&](double av, double bv) { return rate * (av - bv); };
        const double na = flux(nodes_[a].ecsNa, nodes_[b].ecsNa);
        const double k = flux(nodes_[a].ecsK, nodes_[b].ecsK);
        const double ca = flux(nodes_[a].ecsCa, nodes_[b].ecsCa);
        const double cl = flux(nodes_[a].ecsCl, nodes_[b].ecsCl);
        ecs[a].na -= na; ecs[b].na += na;
        ecs[a].k -= k; ecs[b].k += k;
        ecs[a].ca -= ca; ecs[b].ca += ca;
        ecs[a].cl -= cl; ecs[b].cl += cl;
    }
    const double baselineEk = nernstMv(kEcsBaselineK, kIcsBaselineK, 1.0);
    const double baselineEna = nernstMv(kEcsBaselineNa, kIcsBaselineNa, 1.0);
    const double baselineEcl = nernstMv(kEcsBaselineCl, kIcsBaselineCl, -1.0);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        node.ecsNa = clampFinite(node.ecsNa + ecs[i].na, 80.0, 180.0, kEcsBaselineNa);
        node.ecsK = clampFinite(node.ecsK + ecs[i].k, 1.5, 18.0, kEcsBaselineK);
        node.ecsCa = clampFinite(node.ecsCa + ecs[i].ca, 0.2, 3.0, kEcsBaselineCa);
        node.ecsCl = clampFinite(node.ecsCl + ecs[i].cl, 70.0, 170.0, kEcsBaselineCl);
        node.icsNa = clampFinite(node.icsNa, 2.0, 45.0, kIcsBaselineNa);
        node.icsK = clampFinite(node.icsK, 80.0, 180.0, kIcsBaselineK);
        node.icsCa = clampFinite(node.icsCa, 0.00005, 0.02, kIcsBaselineCa);
        node.icsCl = clampFinite(node.icsCl, 2.0, 40.0, kIcsBaselineCl);
        const double ekShift = nernstMv(node.ecsK, node.icsK, 1.0) - baselineEk;
        const double enaShift = nernstMv(node.ecsNa, node.icsNa, 1.0) - baselineEna;
        const double eclShift = nernstMv(node.ecsCl, node.icsCl, -1.0) - baselineEcl;
        node.excitabilityShiftMv = std::clamp(
            0.70 * ekShift + 0.18 * enaShift + 0.12 * eclShift,
            -10.0, 15.0);
    }
}

void PhysiologicalSubstrate::updateChannelsAndGenes(
    const std::vector<std::uint8_t>& spikes,
    const std::vector<double>& somaMv,
    const std::vector<double>& dendriteMv,
    const std::vector<double>& ratesHz,
    double dtMs) {
    if (throughput_) throughput_->recordReadWrite<Node>(tatarus::ThroughputDomain::Physiology, nodes_.size());
    gammaPhase_ = std::fmod(gammaPhase_ + 2.0 * kPi * 40.0 * dtMs / 1000.0, 2.0 * kPi);
    thetaPhase_ = std::fmod(thetaPhase_ + 2.0 * kPi * 7.0 * dtMs / 1000.0, 2.0 * kPi);
    double population = 0.0;
    for (std::uint8_t spike : spikes) population += spike != 0 ? 1.0 : 0.0;
    population /= std::max<std::size_t>(1, nodes_.size());
    const double spectralAlpha = 1.0 - std::exp(-dtMs / 250.0);
    gammaX_ += spectralAlpha * (population * std::cos(gammaPhase_) - gammaX_);
    gammaY_ += spectralAlpha * (population * std::sin(gammaPhase_) - gammaY_);
    thetaX_ += spectralAlpha * (population * std::cos(thetaPhase_) - thetaX_);
    thetaY_ += spectralAlpha * (population * std::sin(thetaPhase_) - thetaY_);

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];
        const double soma = i < somaMv.size() ? somaMv[i] : config_.restingMv;
        const double dendrite = i < dendriteMv.size() ? dendriteMv[i] : config_.restingMv;
        const double hcnActivation = logistic((-72.0 - dendrite) / 5.0);
        const double kvActivation = logistic((dendrite + 38.0) / 7.0);
        node.hcnCurrent = 0.018 * node.hcnDensity * hcnActivation * (-35.0 - dendrite);
        node.kvCurrent = 0.012 * node.kvDensity * kvActivation * (-90.0 - dendrite);
        node.channelBiasMv = std::clamp(0.08 * (node.hcnCurrent + node.kvCurrent), -6.0, 3.0);

        const double calciumSignal = std::clamp(
            (node.icsCa - kIcsBaselineCa) / 0.002, 0.0, 2.0);
        const double rate = i < ratesHz.size() ? ratesHz[i] : 0.0;
        const double activity = std::clamp(rate / 30.0, 0.0, 2.0)
            + (i < spikes.size() && spikes[i] ? 0.4 : 0.0);
        const double crebTarget = std::clamp(
            0.45 * calciumSignal + 0.30 * activity + 0.25 * node.camp,
            0.0, 2.0);
        node.pCreb = approach(node.pCreb, crebTarget, dtMs, 45'000.0);
        node.mrna = approach(node.mrna, std::max(0.0, node.pCreb - 0.35), dtMs, 240'000.0);
        node.protein = approach(node.protein, node.mrna, dtMs, 600'000.0);
        const double homerTarget = sleepStage_ == SleepStage::Wake
            ? std::clamp(activity, 0.0, 1.5)
            : std::clamp(activity + sleepPressure_, 0.0, 2.0);
        node.homer1a = approach(node.homer1a, homerTarget, dtMs, 300'000.0);
        const double clearance = sleepStage_ == SleepStage::Nrem ? 0.0000035 : 0.0000003;
        node.waste = std::max(0.0, node.waste - clearance * dtMs * node.waste);
        static_cast<void>(soma);
    }
}

void PhysiologicalSubstrate::step(
    const SensorFrame& frame,
    const std::vector<std::uint8_t>& spikes,
    const std::vector<double>& somaMv,
    const std::vector<double>& dendriteMv,
    const std::vector<double>& ratesHz,
    const std::vector<double>& energy) {
    if (!config_.physiologyEnabled) return;
    const double dtMs = config_.dtMs * config_.physiologyTimeScale;
    updateSleep(dtMs);
    updateNeuromodulators(frame, dtMs);
    updateIons(frame, spikes, energy, dtMs);
    updateChannelsAndGenes(spikes, somaMv, dendriteMv, ratesHz, dtMs);
    updateMetrics();
}

double PhysiologicalSubstrate::somaBiasMv(std::size_t neuron) const {
    if (neuron >= nodes_.size() || !config_.physiologyEnabled) return 0.0;
    const auto& node = nodes_[neuron];
    double rhythm = 0.0;
    if (node.subtype == NeuronSubtype::Parvalbumin) rhythm = 1.15 * std::sin(gammaPhase_);
    if (node.subtype == NeuronSubtype::Somatostatin) rhythm = 0.75 * std::sin(thetaPhase_);
    if (node.subtype == NeuronSubtype::Vip) rhythm = 0.55 * node.modulator[3];
    if (node.subtype == NeuronSubtype::Pyramidal) rhythm = 0.22 * std::sin(thetaPhase_);
    return std::clamp(node.excitabilityShiftMv + rhythm, -12.0, 16.0);
}

double PhysiologicalSubstrate::dendriteBiasMv(std::size_t neuron) const {
    if (neuron >= nodes_.size() || !config_.physiologyEnabled) return 0.0;
    const auto& node = nodes_[neuron];
    const double modulation = 0.25 * node.camp + 0.18 * node.ip3 - 0.20;
    return std::clamp(node.channelBiasMv + modulation, -7.0, 4.0);
}

double PhysiologicalSubstrate::metabolicScale(std::size_t neuron) const {
    if (neuron >= nodes_.size() || !config_.physiologyEnabled) return 1.0;
    return std::clamp(nodes_[neuron].atp, 0.08, 1.15);
}

std::vector<PlasticityEffect> PhysiologicalSubstrate::updateSynapses(
    const std::vector<SynapseCoupling>& synapses) {
    if (throughput_) {
        throughput_->recordRead<SynapseCoupling>(tatarus::ThroughputDomain::Synapse, synapses.size());
        throughput_->recordReadWrite<SynapseMolecule>(tatarus::ThroughputDomain::Physiology, synapses.size());
    }
    if (synapseMolecules_.size() < synapses.size()) {
        synapseMolecules_.resize(synapses.size());
    }
    std::vector<PlasticityEffect> effects(synapses.size());
    if (!config_.physiologyEnabled) return effects;
    const double dtMs = config_.dtMs * config_.physiologyTimeScale;
    const double tagDecay = std::exp(-dtMs / 10'800'000.0); // 3 h
    for (std::size_t i = 0; i < synapses.size(); ++i) {
        const auto& synapse = synapses[i];
        auto& molecule = synapseMolecules_[i];
        if (!synapse.active || synapse.post >= nodes_.size()) continue;
        molecule.tag *= tagDecay;
        molecule.tagPolarity *= tagDecay;
        if (std::abs(synapse.eligibility) > 0.30) {
            const double induction = (1.0 - std::exp(-dtMs / 45'000.0))
                * std::min(1.0, std::abs(synapse.eligibility));
            molecule.tag = std::clamp(molecule.tag + induction, 0.0, 1.0);
            molecule.tagPolarity = approach(
                molecule.tagPolarity,
                synapse.eligibility > 0.0 ? 1.0 : -1.0,
                dtMs, 30'000.0);
        }
        auto& post = nodes_[synapse.post];
        const double available = std::max(0.0, post.protein);
        const double capture = (1.0 - std::exp(-dtMs / 900'000.0))
            * molecule.tag * available;
        molecule.capturedProtein = std::clamp(
            molecule.capturedProtein + capture
                - dtMs / 86'400'000.0 * molecule.capturedProtein,
            0.0, 2.0);
        post.protein = std::max(0.0, post.protein - 0.05 * capture);
        effects[i].consolidationGain = std::clamp(
            capture * molecule.tagPolarity * (synapse.inhibitory ? 0.25 : 1.0),
            -0.01, 0.01);
        if (molecule.capturedProtein > 0.05 && molecule.tagPolarity > 0.2 && !molecule.countedLtp) {
            molecule.countedLtp = true;
            ++ltpEvents_;
        }
        if (molecule.capturedProtein > 0.05 && molecule.tagPolarity < -0.2 && !molecule.countedLtd) {
            molecule.countedLtd = true;
            ++ltdEvents_;
        }
        if (sleepStage_ == SleepStage::Nrem && !synapse.inhibitory) {
            const double protectedFraction = std::clamp(
                molecule.capturedProtein + molecule.tag, 0.0, 0.95);
            const double downscale = 0.000000018 * dtMs
                * post.homer1a * (1.0 - protectedFraction);
            effects[i].weightScale = std::clamp(1.0 - downscale, 0.9990, 1.0);
        }
    }
    updateMetrics();
    return effects;
}

void PhysiologicalSubstrate::updateMetrics() {
    if (throughput_) {
        throughput_->recordRead<Node>(tatarus::ThroughputDomain::Physiology, nodes_.size());
        throughput_->recordRead<SynapseMolecule>(tatarus::ThroughputDomain::Physiology, synapseMolecules_.size());
    }
    PhysiologyMetrics result;
    result.meanExtracellularNaMm = 0.0;
    result.meanExtracellularKMm = 0.0;
    result.meanExtracellularCaMm = 0.0;
    result.meanExtracellularClMm = 0.0;
    result.meanAtp = 0.0;
    result.meanPumpActivity = 0.0;
    result.dopamine = 0.0;
    result.serotonin = 0.0;
    result.noradrenaline = 0.0;
    result.acetylcholine = 0.0;
    result.meanCamp = 0.0;
    result.meanIp3 = 0.0;
    result.meanHcnDensity = 0.0;
    result.meanKvDensity = 0.0;
    result.meanCrebActivation = 0.0;
    result.meanProteinPool = 0.0;
    result.meanSynapticTag = 0.0;
    result.available = config_.physiologyEnabled;
    result.sleepStage = sleepStage_;
    result.circadianPhaseHours = circadianPhaseHours_;
    result.sleepPressure = sleepPressure_;
    result.extracellularVolumeFraction = extracellularVolumeFraction_;
    result.glymphaticClearance = sleepStage_ == SleepStage::Nrem ? 1.0
        : sleepStage_ == SleepStage::Rem ? 0.35 : 0.10;
    result.tissueWaste = tissueWaste_;
    result.cumulativeAtpConsumed = cumulativeAtpConsumed_;
    result.heatProductionPj = cumulativeAtpConsumed_ * 50.0;
    result.entropyProductionPjPerK = result.heatProductionPj / kTemperatureK;
    result.gammaPower = 1000.0 * (gammaX_ * gammaX_ + gammaY_ * gammaY_);
    result.thetaPower = 1000.0 * (thetaX_ * thetaX_ + thetaY_ * thetaY_);
    result.longTermPotentiationEvents = ltpEvents_;
    result.longTermDepressionEvents = ltdEvents_;
    result.sleepTransitions = sleepTransitions_;
    for (const auto& node : nodes_) {
        result.meanExtracellularNaMm += node.ecsNa;
        result.meanExtracellularKMm += node.ecsK;
        result.meanExtracellularCaMm += node.ecsCa;
        result.meanExtracellularClMm += node.ecsCl;
        result.meanAtp += node.atp;
        result.meanPumpActivity += node.pumpActivity;
        result.dopamine += node.modulator[0];
        result.serotonin += node.modulator[1];
        result.noradrenaline += node.modulator[2];
        result.acetylcholine += node.modulator[3];
        result.meanCamp += node.camp;
        result.meanIp3 += node.ip3;
        result.meanHcnDensity += node.hcnDensity;
        result.meanKvDensity += node.kvDensity;
        result.meanCrebActivation += node.pCreb;
        result.meanProteinPool += node.protein;
        if (node.subtype == NeuronSubtype::Parvalbumin) ++result.parvalbuminNeurons;
        if (node.subtype == NeuronSubtype::Somatostatin) ++result.somatostatinNeurons;
        if (node.subtype == NeuronSubtype::Vip) ++result.vipNeurons;
    }
    for (const auto& molecule : synapseMolecules_) {
        result.meanSynapticTag += molecule.tag;
        if (molecule.tag > 0.05) ++result.taggedSynapses;
    }
    const double count = static_cast<double>(std::max<std::size_t>(1, nodes_.size()));
    result.meanExtracellularNaMm /= count;
    result.meanExtracellularKMm /= count;
    result.meanExtracellularCaMm /= count;
    result.meanExtracellularClMm /= count;
    result.meanAtp /= count;
    result.meanPumpActivity /= count;
    result.dopamine /= count;
    result.serotonin /= count;
    result.noradrenaline /= count;
    result.acetylcholine /= count;
    result.meanCamp /= count;
    result.meanIp3 /= count;
    result.meanHcnDensity /= count;
    result.meanKvDensity /= count;
    result.meanCrebActivation /= count;
    result.meanProteinPool /= count;
    result.meanSynapticTag /= static_cast<double>(
        std::max<std::size_t>(1, synapseMolecules_.size()));
    result.finite = std::isfinite(result.meanAtp)
        && std::isfinite(result.meanExtracellularKMm)
        && std::isfinite(result.meanCamp)
        && std::isfinite(result.meanProteinPool);
    metrics_ = result;
}

const PhysiologyMetrics& PhysiologicalSubstrate::metrics() const {
    return metrics_;
}

PhysiologicalState PhysiologicalSubstrate::inspect() const {
    if (throughput_) {
        throughput_->recordRead<Node>(tatarus::ThroughputDomain::Physiology, nodes_.size());
        throughput_->recordRead<SynapseMolecule>(tatarus::ThroughputDomain::Physiology, synapseMolecules_.size());
    }
    PhysiologicalState result;
    result.metrics = metrics_;
    result.neurons.reserve(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const auto& node = nodes_[i];
        result.neurons.push_back(PhysiologyNeuronView{
            i, node.position, node.subtype,
            node.ecsNa, node.ecsK, node.ecsCa, node.ecsCl,
            node.atp, node.pumpActivity, node.excitabilityShiftMv,
            node.modulator[0], node.modulator[1], node.modulator[2], node.modulator[3],
            node.camp, node.ip3, node.hcnDensity, node.kvDensity,
            node.pCreb, node.mrna, node.protein, node.homer1a, node.waste});
    }
    result.synapses.reserve(synapseMolecules_.size());
    for (std::size_t i = 0; i < synapseMolecules_.size(); ++i) {
        const auto& molecule = synapseMolecules_[i];
        result.synapses.push_back(PhysiologySynapseView{
            i, molecule.tag, molecule.tagPolarity, molecule.capturedProtein});
    }
    return result;
}

std::uint64_t PhysiologicalSubstrate::stateHash() const {
    // Hash semantic fields rather than raw struct storage. Raw storage contains
    // implementation-defined padding bytes and can therefore differ between two
    // otherwise identical instances, especially across compilers/ABIs.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](const auto& value) {
        hash = hashBytes(hash, &value, sizeof(value));
    };

    for (const auto& node : nodes_) {
        add(node.position.x); add(node.position.y); add(node.position.z);
        add(node.role); add(node.subtype);
        add(node.ecsNa); add(node.ecsK); add(node.ecsCa); add(node.ecsCl);
        add(node.icsNa); add(node.icsK); add(node.icsCa); add(node.icsCl);
        add(node.atp); add(node.pumpActivity); add(node.excitabilityShiftMv); add(node.channelBiasMv);
        for (const auto value : node.modulator) add(value);
        for (const auto value : node.receptor) add(value);
        add(node.camp); add(node.ip3); add(node.hcnDensity); add(node.kvDensity);
        add(node.hcnCurrent); add(node.kvCurrent);
        add(node.pCreb); add(node.mrna); add(node.protein); add(node.homer1a);
        add(node.astrocyteSupport); add(node.waste);
    }
    for (const auto& edge : diffusionEdges_) {
        add(edge.a); add(edge.b); add(edge.coupling);
    }
    for (const auto& molecule : synapseMolecules_) {
        add(molecule.tag); add(molecule.tagPolarity); add(molecule.capturedProtein);
        add(molecule.countedLtp); add(molecule.countedLtd);
    }

    add(metrics_.available); add(metrics_.finite); add(metrics_.sleepStage);
    add(metrics_.parvalbuminNeurons); add(metrics_.somatostatinNeurons); add(metrics_.vipNeurons);
    add(metrics_.taggedSynapses);
    add(metrics_.longTermPotentiationEvents); add(metrics_.longTermDepressionEvents); add(metrics_.sleepTransitions);
    add(metrics_.meanExtracellularNaMm); add(metrics_.meanExtracellularKMm);
    add(metrics_.meanExtracellularCaMm); add(metrics_.meanExtracellularClMm);
    add(metrics_.meanAtp); add(metrics_.meanPumpActivity); add(metrics_.cumulativeAtpConsumed);
    add(metrics_.heatProductionPj); add(metrics_.entropyProductionPjPerK);
    add(metrics_.dopamine); add(metrics_.serotonin); add(metrics_.noradrenaline); add(metrics_.acetylcholine);
    add(metrics_.meanCamp); add(metrics_.meanIp3); add(metrics_.meanHcnDensity); add(metrics_.meanKvDensity);
    add(metrics_.meanCrebActivation); add(metrics_.meanProteinPool); add(metrics_.meanSynapticTag);
    add(metrics_.circadianPhaseHours); add(metrics_.sleepPressure);
    add(metrics_.extracellularVolumeFraction); add(metrics_.glymphaticClearance); add(metrics_.tissueWaste);
    add(metrics_.gammaPower); add(metrics_.thetaPower);

    add(sleepStage_); add(simulatedMs_); add(stageMs_); add(sleepPressure_); add(circadianPhaseHours_);
    add(gammaPhase_); add(thetaPhase_); add(gammaX_); add(gammaY_); add(thetaX_); add(thetaY_);
    add(extracellularVolumeFraction_); add(tissueWaste_); add(cumulativeAtpConsumed_);
    add(ltpEvents_); add(ltdEvents_); add(sleepTransitions_);
    add(experimentalPumpEfficiency_); add(experimentalAstrocyteFunction_);
    add(experimentalNeuromodulatorGain_); add(experimentalMetabolicSupply_); add(experimentalSleepEnabled_);
    return hash;
}

void PhysiologicalSubstrate::save(std::ostream& output) const {
    const std::array<char, 8> magic{'P','H','Y','3','0','0','1','\0'};
    output.write(magic.data(), magic.size());
    writeVector(output, nodes_);
    writeVector(output, diffusionEdges_);
    writeVector(output, synapseMolecules_);
    writePod(output, metrics_);
    writePod(output, sleepStage_);
    writePod(output, simulatedMs_);
    writePod(output, stageMs_);
    writePod(output, sleepPressure_);
    writePod(output, circadianPhaseHours_);
    writePod(output, gammaPhase_);
    writePod(output, thetaPhase_);
    writePod(output, gammaX_); writePod(output, gammaY_);
    writePod(output, thetaX_); writePod(output, thetaY_);
    writePod(output, extracellularVolumeFraction_);
    writePod(output, tissueWaste_);
    writePod(output, cumulativeAtpConsumed_);
    writePod(output, ltpEvents_); writePod(output, ltdEvents_); writePod(output, sleepTransitions_);
}

bool PhysiologicalSubstrate::load(std::istream& input) {
    const auto start = input.tellg();
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    const std::array<char, 8> expected{'P','H','Y','3','0','0','1','\0'};
    if (!input || magic != expected) {
        input.clear();
        if (start != std::istream::pos_type(-1)) input.seekg(start);
        return false;
    }
    readVector(input, nodes_, 100'000);
    readVector(input, diffusionEdges_, 1'000'000);
    readVector(input, synapseMolecules_, 100'000'000);
    readPod(input, metrics_);
    readPod(input, sleepStage_);
    readPod(input, simulatedMs_);
    readPod(input, stageMs_);
    readPod(input, sleepPressure_);
    readPod(input, circadianPhaseHours_);
    readPod(input, gammaPhase_);
    readPod(input, thetaPhase_);
    readPod(input, gammaX_); readPod(input, gammaY_);
    readPod(input, thetaX_); readPod(input, thetaY_);
    readPod(input, extracellularVolumeFraction_);
    readPod(input, tissueWaste_);
    readPod(input, cumulativeAtpConsumed_);
    readPod(input, ltpEvents_); readPod(input, ltdEvents_); readPod(input, sleepTransitions_);
    if (nodes_.size() != static_cast<std::size_t>(config_.neuronCount())) {
        throw std::runtime_error("Physiologie-Snapshot hat falsche Neuronenzahl");
    }
    return true;
}

} // namespace tatarus::neuro::physiology
