#include "tatarus/robot_mind.hpp"
#include "tatarus/visual_pathway.hpp"
#include "tatarus/ocular_system.hpp"
#include "tatarus/vestibular_system.hpp"
#include "tatarus/version.hpp"

#include "tatarus_cartography.hpp"
#include "tatarus_neural_network.hpp"
#include "tatarus_identity.hpp"
#include "tatarus_cognition.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace tatarus {
namespace {

constexpr char kStateMagic[8] = {'T','S','D','K','0','0','1','\0'};

struct PairHash {
    std::size_t operator()(const std::pair<std::uint64_t, std::uint64_t>& value) const noexcept {
        const auto h1 = std::hash<std::uint64_t>{}(value.first);
        const auto h2 = std::hash<std::uint64_t>{}(value.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
    }
};

struct TransitionKey {
    std::uint64_t source = 0;
    std::uint64_t action = 0;
    std::uint64_t target = 0;

    friend bool operator==(const TransitionKey&, const TransitionKey&) = default;
};

struct TransitionKeyHash {
    std::size_t operator()(const TransitionKey& value) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(value.source);
        const auto mix = [&h](std::uint64_t x) {
            const auto v = std::hash<std::uint64_t>{}(x);
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
        };
        mix(value.action);
        mix(value.target);
        return h;
    }
};

struct EntityTransitionKey {
    std::uint64_t source = 0;
    std::uint64_t entity = 0;
    std::uint64_t target = 0;

    friend bool operator==(const EntityTransitionKey&, const EntityTransitionKey&) = default;
};

struct EntityTransitionKeyHash {
    std::size_t operator()(const EntityTransitionKey& value) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(value.source);
        const auto mix = [&h](std::uint64_t x) {
            const auto v = std::hash<std::uint64_t>{}(x);
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
        };
        mix(value.entity);
        mix(value.target);
        return h;
    }
};

double clampUnit(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, -1.0, 1.0);
}

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

void validateCognitiveCue(const CognitiveCue& cue) {
    if (!std::isfinite(cue.recallStrength)
        || cue.recallStrength < 0.0
        || cue.recallStrength > CognitiveCueLimits::maximumRecallStrength) {
        throw std::invalid_argument("CognitiveCue recallStrength exceeds bounded phase-10 limit");
    }
    if (!std::isfinite(cue.goalBiasStrength)
        || cue.goalBiasStrength < 0.0
        || cue.goalBiasStrength > CognitiveCueLimits::maximumGoalBiasStrength) {
        throw std::invalid_argument("CognitiveCue goalBiasStrength exceeds bounded phase-10 limit");
    }
    if (cue.semanticContext.size() > CognitiveCueLimits::maximumSemanticChannels) {
        throw std::invalid_argument("CognitiveCue contains too many semantic context channels");
    }
    for (const double value : cue.goalBias) {
        if (!std::isfinite(value) || value < -1.0 || value > 1.0) {
            throw std::invalid_argument("CognitiveCue goalBias values must be finite and in [-1,1]");
        }
    }
    for (const double value : cue.semanticContext) {
        if (!std::isfinite(value)
            || std::abs(value) > CognitiveCueLimits::maximumSemanticMagnitude) {
            throw std::invalid_argument("CognitiveCue semanticContext exceeds bounded phase-10 limit");
        }
    }
}

tatarus::neuro::AttentionTarget toNeuroAttention(CognitiveAttentionTarget target) {
    using Public = CognitiveAttentionTarget;
    using Internal = tatarus::neuro::AttentionTarget;
    switch (target) {
        case Public::Balanced: return Internal::Balanced;
        case Public::Vision: return Internal::Vision;
        case Public::Audio: return Internal::Audio;
        case Public::Touch: return Internal::Touch;
        case Public::Text: return Internal::Text;
        case Public::Interoception: return Internal::Interoception;
    }
    throw std::invalid_argument("Unknown CognitiveAttentionTarget");
}

tatarus::neuro::CognitiveCommand toNeuroCommand(const CognitiveCue& cue) {
    validateCognitiveCue(cue);
    tatarus::neuro::CognitiveCommand command;
    command.attention = toNeuroAttention(cue.attention);
    command.recallCue = cue.recallCue;
    command.recallStrength = cue.recallStrength;
    command.goalBias = cue.goalBias;
    command.goalBiasStrength = cue.goalBiasStrength;
    command.semanticContext = cue.semanticContext;

    // Deliberately fixed at zero. The public CognitiveCue API exposes neither
    // direct motor intent nor reward injection.
    command.motorIntent = 0.0;
    command.intentStrength = 0.0;
    command.reward = 0.0;
    command.contextOnly = true;
    return command;
}

double signedHash(std::uint64_t value) {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    const double unit = static_cast<double>(value & 0xffffULL) / 65535.0;
    return 2.0 * unit - 1.0;
}

const char* populationRoleName(tatarus::neuro::PopulationRole role) {
    switch (role) {
        case tatarus::neuro::PopulationRole::Sensory: return "sensory";
        case tatarus::neuro::PopulationRole::Excitatory: return "excitatory";
        case tatarus::neuro::PopulationRole::Inhibitory: return "inhibitory";
        case tatarus::neuro::PopulationRole::Context: return "context";
        case tatarus::neuro::PopulationRole::Motor: return "motor";
        case tatarus::neuro::PopulationRole::Modulatory: return "modulatory";
    }
    return "excitatory";
}

const char* receptorName(tatarus::neuro::ReceptorType receptor) {
    switch (receptor) {
        case tatarus::neuro::ReceptorType::Ampa: return "ampa";
        case tatarus::neuro::ReceptorType::Nmda: return "nmda";
        case tatarus::neuro::ReceptorType::GabaA: return "gaba_a";
        case tatarus::neuro::ReceptorType::GabaB: return "gaba_b";
        case tatarus::neuro::ReceptorType::Modulatory: return "modulatory";
    }
    return "ampa";
}

const char* neuronSubtypeName(tatarus::neuro::NeuronSubtype subtype) {
    switch (subtype) {
        case tatarus::neuro::NeuronSubtype::Sensory: return "sensory";
        case tatarus::neuro::NeuronSubtype::Pyramidal: return "pyramidal";
        case tatarus::neuro::NeuronSubtype::Parvalbumin: return "pv";
        case tatarus::neuro::NeuronSubtype::Somatostatin: return "sst";
        case tatarus::neuro::NeuronSubtype::Vip: return "vip";
        case tatarus::neuro::NeuronSubtype::Neuromodulatory: return "neuromodulatory";
    }
    return "pyramidal";
}

const char* hemisphereName(tatarus::neuro::BrainHemisphere hemisphere) {
    return hemisphere == tatarus::neuro::BrainHemisphere::Left
        ? "left" : "right";
}

const char* brainRegionName(tatarus::neuro::BrainRegion region) {
    switch (region) {
        case tatarus::neuro::BrainRegion::Sensory: return "sensory";
        case tatarus::neuro::BrainRegion::Association: return "association";
        case tatarus::neuro::BrainRegion::Memory: return "memory";
        case tatarus::neuro::BrainRegion::Prospection: return "prospection";
        case tatarus::neuro::BrainRegion::Motor: return "motor";
        case tatarus::neuro::BrainRegion::Homeostatic: return "homeostatic";
    }
    return "association";
}

const char* corticalLayerName(tatarus::neuro::CorticalLayer layer) {
    switch (layer) {
        case tatarus::neuro::CorticalLayer::Layer23: return "L2/3";
        case tatarus::neuro::CorticalLayer::Layer4: return "L4";
        case tatarus::neuro::CorticalLayer::Layer5: return "L5";
        case tatarus::neuro::CorticalLayer::Layer6: return "L6";
        case tatarus::neuro::CorticalLayer::Subcortical: return "subcortical";
    }
    return "L2/3";
}

const char* sleepStageName(tatarus::neuro::SleepStage stage) {
    switch (stage) {
        case tatarus::neuro::SleepStage::Wake: return "wake";
        case tatarus::neuro::SleepStage::Nrem: return "nrem";
        case tatarus::neuro::SleepStage::Rem: return "rem";
    }
    return "wake";
}

void writeSpatialPoint(
    std::ostream& output,
    const tatarus::neuro::SpatialPointView& point) {
    output << '[' << point.x << ',' << point.y << ',' << point.z << ']';
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) throw std::runtime_error("TATARUS SDK snapshot write failed");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("TATARUS SDK snapshot is truncated");
}


tatarus::neuro::SensorFrame encodeExperience(const Experience& experience) {
    tatarus::neuro::SensorFrame frame;
    frame.visionEvents.reserve(experience.vision.size());
    for (const double value : experience.vision) frame.visionEvents.push_back(clampUnit(value));

    frame.audioSamples.reserve(experience.audio.size());
    for (const double value : experience.audio) frame.audioSamples.push_back(clampUnit(value));

    frame.touch.reserve(experience.touch.size() + 16U);
    for (const double value : experience.touch) frame.touch.push_back(clampUnit(value));

    frame.spatialContext.reserve(experience.spatialContext.size());
    for (const double value : experience.spatialContext) {
        frame.spatialContext.push_back(clampUnit(value));
    }
    frame.spatialEpisodeContext = experience.spatialEpisodeContext;

    // Somatosensory embodiment remains separate from vestibular afferents.
    frame.touch.push_back(clampUnit(2.0 * clamp01(experience.environment.light) - 1.0));
    frame.touch.push_back(clampUnit(2.0 * clamp01(experience.environment.soundLevel) - 1.0));
    frame.touch.push_back(clampUnit(2.0 * clamp01(experience.environment.proximity) - 1.0));
    frame.touch.push_back(clampUnit(experience.body.balance));
    frame.touch.push_back(clampUnit(2.0 * clamp01(experience.body.contactLeft) - 1.0));
    frame.touch.push_back(clampUnit(2.0 * clamp01(experience.body.contactRight) - 1.0));


    frame.textBytes = experience.text;
    frame.contextEvents.reserve(
        experience.context.size() + experience.spatialContext.size() + 8U);
    for (const double value : experience.spatialContext) {
        frame.contextEvents.push_back(clampUnit(value));
    }
    for (const double value : experience.context) frame.contextEvents.push_back(clampUnit(value));
    frame.contextEvents.push_back(clampUnit(experience.body.powerDraw));
    frame.contextEvents.push_back(clampUnit(experience.environment.temperature / 50.0));

    if (!experience.body.joints.empty()) {
        double p = 0.0;
        double v = 0.0;
        double t = 0.0;
        double temp = 0.0;
        for (const auto& joint : experience.body.joints) {
            p += std::abs(joint.position);
            v += std::abs(joint.velocity);
            t += std::abs(joint.torque);
            temp += std::abs(joint.temperature);
        }
        const double n = static_cast<double>(experience.body.joints.size());
        frame.contextEvents.push_back(clampUnit(p / n));
        frame.contextEvents.push_back(clampUnit(v / n));
        frame.contextEvents.push_back(clampUnit(t / n));
        frame.contextEvents.push_back(clampUnit((temp / n) / 100.0));
    }

    if (experience.entity.has_value() && experience.entity->id != 0) {
        frame.contextEvents.push_back(signedHash(experience.entity->id));
        frame.contextEvents.push_back(clampUnit(2.0 * clamp01(experience.entity->confidence) - 1.0));
    }

    if (experience.action.has_value() && experience.action->id != 0) {
        frame.contextEvents.push_back(signedHash(experience.action->id));
        frame.contextEvents.push_back(clampUnit(experience.action->intensity));
    }

    frame.temperature = experience.environment.temperature;
    frame.internalEnergy = clamp01(experience.body.battery);
    frame.reward = clampUnit(experience.reward);
    frame.novelty = clamp01(experience.environment.novelty);
    return frame;
}

BiologicalTelemetry mapBiology(const tatarus::neuro::BiologicalMetrics& source) {
    BiologicalTelemetry out;
    out.available = true;
    out.dendriticSegments = static_cast<std::uint64_t>(std::max(0, source.dendriteSegments));
    out.astrocytes = static_cast<std::uint64_t>(std::max(0, source.astrocytes));
    out.capillaries = static_cast<std::uint64_t>(std::max(0, source.capillaries));
    out.vessels = out.capillaries;
    out.oligodendrocytes = static_cast<std::uint64_t>(std::max(0, source.oligodendrocytes));
    out.microglia = static_cast<std::uint64_t>(std::max(0, source.microglia));
    out.dendriticSpikes = source.dendriticSpikes;
    out.myelinRemodelingUpdates = source.myelinRemodelingUpdates;
    out.microglialSurveillanceUpdates = source.microglialSurveillanceUpdates;
    out.microglialPruningEvents = source.microglialPruningEvents;
    out.microglialRepairEvents = source.microglialRepairEvents;
    out.microglialDamageSignals = source.microglialDamageSignals;
    out.meanDendriticCalcium = source.meanDendriticCalcium;
    out.meanAstrocyteCalcium = source.meanAstrocyteCalcium;
    out.oxygen = source.meanOxygen;
    out.glucose = source.meanGlucose;
    out.flow = source.meanBloodFlow;
    out.meanAxonLengthUm = source.meanAxonLengthUm;
    out.meanLocalEnergy = source.meanLocalEnergy;
    out.myelinCoverage = source.meanMyelinCoverage;
    // 1 um/ms == 1e-3 m/s.
    out.conductionVelocityMps = source.meanConductionVelocityUmPerMs * 1e-3;
    out.effectiveDelayMs = source.meanEffectiveDelayMs;
    out.oligodendrocyteReserve = source.meanOligodendrocyteReserve;
    out.microgliaActivation = source.meanMicroglialActivation;
    out.complementTag = source.meanComplementTag;
    out.repairCapacity = source.meanMicroglialRepairCapacity;
    out.inflammatoryTone = source.meanInflammatoryTone;
    out.baselineActiveSynapses = source.baselineActiveSynapses;
    out.mechanicsActiveSynapses = source.mechanicsActiveSynapses;
    out.growthLimitedEvents = source.growthLimitedEvents;
    out.baselineTissueVolumeUm3 = source.baselineTissueVolumeUm3;
    out.tissueVolumeUm3 = source.tissueVolumeUm3;
    out.tissueVolumeRatio = source.tissueVolumeRatio;
    out.linearExpansion = source.linearExpansion;
    out.effectiveTissueMassNg = source.effectiveTissueMassNg;
    out.netBiomassChangeNg = source.netBiomassChangeNg;
    out.synapticMaterialVolumeUm3 = source.synapticMaterialVolumeUm3;
    out.solidPackingFraction = source.solidPackingFraction;
    out.extracellularSpaceFraction = source.extracellularSpaceFraction;
    out.tissuePressureKPa = source.tissuePressureKPa;
    out.materialReserve = source.materialReserve;
    out.cumulativeMaterialSynthesizedUm3 = source.cumulativeMaterialSynthesizedUm3;
    out.cumulativeMaterialRecycledUm3 = source.cumulativeMaterialRecycledUm3;
    out.pruneEvents = source.microglialPruningEvents;
    out.repairEvents = source.microglialRepairEvents;
    return out;
}

ProspectiveTelemetry mapProspection(const tatarus::neuro::ProspectiveMetrics& source) {
    ProspectiveTelemetry out;
    out.available = true;
    out.learnedTransitions = source.learnedTransitions;
    out.observedTransitions = source.observedTransitions;
    out.predictionHits = source.predictionHits;
    out.predictionMisses = source.predictionMisses;
    out.predictedAssemblyId = source.predictedAssemblyId;
    out.lastAssemblyId = source.lastAssemblyId;
    out.predictionConfidence = source.predictionConfidence;
    out.expectedDelayMs = source.expectedDelayMs;
    out.predictionError = source.predictionError;
    out.temporalSurprise = source.temporalSurprise;
    out.sequenceFamiliarity = source.sequenceFamiliarity;
    out.prospectiveActivation = source.prospectiveActivation;
    return out;
}

PhysiologyTelemetry mapPhysiology(const tatarus::neuro::PhysiologyMetrics& source) {
    PhysiologyTelemetry out;
    out.available = source.available;
    out.finite = source.finite;
    out.sleepPhase = source.sleepStage == tatarus::neuro::SleepStage::Nrem
        ? SleepPhase::Nrem
        : source.sleepStage == tatarus::neuro::SleepStage::Rem ? SleepPhase::Rem : SleepPhase::Wake;
    out.parvalbuminNeurons = static_cast<std::uint64_t>(std::max(0, source.parvalbuminNeurons));
    out.somatostatinNeurons = static_cast<std::uint64_t>(std::max(0, source.somatostatinNeurons));
    out.vipNeurons = static_cast<std::uint64_t>(std::max(0, source.vipNeurons));
    out.taggedSynapses = static_cast<std::uint64_t>(std::max(0, source.taggedSynapses));
    out.longTermPotentiationEvents = source.longTermPotentiationEvents;
    out.longTermDepressionEvents = source.longTermDepressionEvents;
    out.sleepTransitions = source.sleepTransitions;
    out.extracellularNaMm = source.meanExtracellularNaMm;
    out.extracellularKMm = source.meanExtracellularKMm;
    out.extracellularCaMm = source.meanExtracellularCaMm;
    out.extracellularClMm = source.meanExtracellularClMm;
    out.atp = source.meanAtp;
    out.pumpActivity = source.meanPumpActivity;
    out.cumulativeAtpConsumed = source.cumulativeAtpConsumed;
    out.heatProductionPj = source.heatProductionPj;
    out.entropyProductionPjPerK = source.entropyProductionPjPerK;
    out.dopamine = source.dopamine;
    out.serotonin = source.serotonin;
    out.noradrenaline = source.noradrenaline;
    out.acetylcholine = source.acetylcholine;
    out.camp = source.meanCamp;
    out.ip3 = source.meanIp3;
    out.hcnDensity = source.meanHcnDensity;
    out.kvDensity = source.meanKvDensity;
    out.crebActivation = source.meanCrebActivation;
    out.proteinPool = source.meanProteinPool;
    out.synapticTag = source.meanSynapticTag;
    out.circadianPhaseHours = source.circadianPhaseHours;
    out.sleepPressure = source.sleepPressure;
    out.extracellularVolumeFraction = source.extracellularVolumeFraction;
    out.glymphaticClearance = source.glymphaticClearance;
    out.tissueWaste = source.tissueWaste;
    out.gammaPower = source.gammaPower;
    out.thetaPower = source.thetaPower;
    return out;
}

MotorTelemetry mapMotor(const tatarus::neuro::MotorAction& source) {
    MotorTelemetry out;
    out.available = true;
    out.directionalActivity = source.directionalActivity;
    out.selectedDirection = source.selectedDirection;
    out.movement = source.movement;
    out.attention = source.attention;
    out.vocalization = source.vocalization;
    out.confidence = source.confidence;
    return out;
}

IdentityDecisionKind convertDecision(tatarus::neuro::identity::DecisionKind kind) {
    using Source = tatarus::neuro::identity::DecisionKind;
    switch (kind) {
        case Source::NewIdentity: return IdentityDecisionKind::NewIdentity;
        case Source::MatchedIdentity: return IdentityDecisionKind::MatchedIdentity;
        case Source::ProvisionalAssociation: return IdentityDecisionKind::ProvisionalAssociation;
        case Source::ProvisionalIdentity: return IdentityDecisionKind::ProvisionalIdentity;
        case Source::Uncertain: return IdentityDecisionKind::Uncertain;
    }
    return IdentityDecisionKind::Uncertain;
}

} // namespace

class RobotMind::Impl {
public:
    explicit Impl(RobotMindConfig value)
        : config(std::move(value)),
          identity(config.seed + 16'590U),
          cartographer(config.cartography) {
        if (config.microstepsPerObservation <= 0 || config.microstepsPerObservation > 10'000) {
            throw std::invalid_argument("microstepsPerObservation must be in [1, 10000]");
        }
        if (config.neuronCount < 96 || config.neuronCount > 65'536) {
            throw std::invalid_argument("neuronCount must be in [96, 65536]");
        }
        const auto positiveFinite = [](double value) {
            return std::isfinite(value) && value > 0.0;
        };
        if (!positiveFinite(config.sleepTiming.circadianCycleMs)
            || !positiveFinite(config.sleepTiming.sleepPressureTauMs)
            || !positiveFinite(config.sleepTiming.nremMinimumMs)
            || !positiveFinite(config.sleepTiming.remMinimumMs)) {
            throw std::invalid_argument("RobotMind sleep timing constants must be finite and > 0");
        }
        createCore();
    }

    void createCore() {
        tatarus::neuro::NervousSystemConfig coreConfig;
        coreConfig.seed = config.seed;
        const auto unit = config.neuronCount / 24U;
        coreConfig.sensoryNeurons = static_cast<int>(4U * unit);
        coreConfig.excitatoryNeurons = static_cast<int>(10U * unit);
        coreConfig.inhibitoryNeurons = static_cast<int>(3U * unit);
        coreConfig.contextNeurons = static_cast<int>(4U * unit);
        coreConfig.motorNeurons = static_cast<int>(2U * unit);
        coreConfig.modulatoryNeurons = static_cast<int>(unit);
        coreConfig.excitatoryNeurons += static_cast<int>(
            config.neuronCount - 24U * unit);
        coreConfig.maximumAssemblies = 512;
        coreConfig.assemblySimilarityThreshold = 0.72;
        coreConfig.structuralIntervalMs = 750.0;
        coreConfig.circadianCycleMs = config.sleepTiming.circadianCycleMs;
        coreConfig.sleepPressureTauMs = config.sleepTiming.sleepPressureTauMs;
        coreConfig.nremMinimumMs = config.sleepTiming.nremMinimumMs;
        coreConfig.remMinimumMs = config.sleepTiming.remMinimumMs;
        core = std::make_unique<tatarus::neuro::PersistentNervousSystem>(coreConfig);
    }

    Prediction predictionFor(
        AssemblyId source,
        std::optional<ActionId> action = std::nullopt,
        std::optional<EntityId> entity = std::nullopt) const {
        Prediction prediction;
        prediction.sourceAssemblyId = source;
        if (source == 0) return prediction;

        std::unordered_map<AssemblyId, std::uint64_t> counts;
        std::uint64_t total = 0;
        bool conditioned = false;

        if (action.has_value() && *action != 0) {
            sdkThroughput.record(tatarus::ThroughputDomain::Cognition, actionTransitions.size(), sizeof(TransitionKey) + sizeof(std::uint64_t), true, false);
            for (const auto& [key, count] : actionTransitions) {
                if (key.source == source && key.action == *action) {
                    counts[key.target] += count;
                    total += count;
                }
            }
            conditioned = total > 0;
        }

        if (!conditioned && entity.has_value() && *entity != 0) {
            counts.clear();
            total = 0;
            sdkThroughput.record(tatarus::ThroughputDomain::Cognition, entityTransitions.size(), sizeof(EntityTransitionKey) + sizeof(std::uint64_t), true, false);
            for (const auto& [key, count] : entityTransitions) {
                if (key.source == source && key.entity == *entity) {
                    counts[key.target] += count;
                    total += count;
                }
            }
            conditioned = total > 0;
        }

        // Generic transition counts remain useful for alternatives and horizon
        // traversal. For the live current state, however, TATARUS intrinsic
        // prospective memory is authoritative when there is no entity/action-
        // conditioned history. That intrinsic prediction also performs dendritic
        // priming inside the nervous substrate.
        if (!conditioned) {
            counts.clear();
            total = 0;
            sdkThroughput.record(tatarus::ThroughputDomain::Cognition, transitions.size(), sizeof(std::pair<std::uint64_t, std::uint64_t>) + sizeof(std::uint64_t), true, false);
            for (const auto& [key, count] : transitions) {
                if (key.first == source) {
                    counts[key.second] += count;
                    total += count;
                }
            }
        }

        std::vector<AlternativePrediction> candidates;
        candidates.reserve(counts.size());
        for (const auto& [target, count] : counts) {
            candidates.push_back(AlternativePrediction{
                .assemblyId = target,
                .probability = total > 0
                    ? static_cast<double>(count) / static_cast<double>(total)
                    : 0.0,
                .observations = count,
            });
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
            if (left.probability != right.probability) return left.probability > right.probability;
            return left.assemblyId < right.assemblyId;
        });
        if (candidates.size() > config.maximumPredictionAlternatives) {
            candidates.resize(config.maximumPredictionAlternatives);
        }

        if (!conditioned && source == currentAssembly) {
            const auto& neural = core->prospectiveMetrics();
            if (neural.predictedAssemblyId != 0) {
                prediction.available = true;
                prediction.expectedAssemblyId = neural.predictedAssemblyId;
                prediction.confidence = clamp01(neural.predictionConfidence);
                prediction.familiarity = clamp01(neural.sequenceFamiliarity);
                prediction.alternatives = std::move(candidates);
                if (std::none_of(
                        prediction.alternatives.begin(), prediction.alternatives.end(),
                        [&](const auto& item) {
                            return item.assemblyId == neural.predictedAssemblyId;
                        })) {
                    prediction.alternatives.insert(
                        prediction.alternatives.begin(),
                        AlternativePrediction{
                            .assemblyId = neural.predictedAssemblyId,
                            .probability = prediction.confidence,
                            .observations = 0,
                        });
                    if (prediction.alternatives.size() > config.maximumPredictionAlternatives) {
                        prediction.alternatives.resize(config.maximumPredictionAlternatives);
                    }
                }
                return prediction;
            }
        }

        if (candidates.empty() || total == 0) return prediction;
        prediction.available = true;
        prediction.expectedAssemblyId = candidates.front().assemblyId;
        prediction.confidence = candidates.front().probability;
        prediction.familiarity = 1.0 - std::exp(-static_cast<double>(total) / 8.0);
        prediction.alternatives = std::move(candidates);
        return prediction;
    }

    RuntimeMetrics publicMetrics() const {
        RuntimeMetrics out;
        out.experiences = experiences;
        out.predictions = predictionCount;
        out.predictionHits = predictionHits;
        out.predictionMisses = predictionMisses;
        out.cumulativeBrier = predictionCount
            ? brierSum / static_cast<double>(predictionCount)
            : 0.0;
        out.meanPredictionError = predictionCount
            ? predictionErrorSum / static_cast<double>(predictionCount)
            : 0.0;

        const auto& prospective = core->prospectiveMetrics();
        out.learnedTransitions = prospective.learnedTransitions;
        out.observedTransitions = prospective.observedTransitions;
        out.neuralPredictionHits = prospective.predictionHits;
        out.neuralPredictionMisses = prospective.predictionMisses;
        out.neuralPredictionError = prospective.predictionError;
        out.sequenceFamiliarity = prospective.sequenceFamiliarity;

        out.contextualTransitions = transitions.size();
        out.actionConditionedTransitions = actionTransitions.size();
        out.entityConditionedTransitions = entityTransitions.size();
        out.identities = config.enableIdentity ? identity.identityCount() : 0;

        const auto& m = core->metrics();
        out.activeSynapses = static_cast<std::uint64_t>(std::max(0, m.activeSynapses));
        out.totalSpikes = m.totalSpikes;
        out.meanEnergy = m.meanEnergy;
        out.finite = m.finite;
        return out;
    }

    BiologicalTelemetry biologicalTelemetry() const {
        return mapBiology(core->biologicalMetrics());
    }

    PhysiologyTelemetry physiologyTelemetry() const {
        return mapPhysiology(core->physiologyMetrics());
    }

    ProspectiveTelemetry prospectiveTelemetry() const {
        return mapProspection(core->prospectiveMetrics());
    }

    MotorTelemetry motorTelemetry() const {
        return mapMotor(lastMotorAction);
    }

    ThroughputCounters throughputTelemetry() const {
        auto total = core->throughputCounters();
        total += sdkThroughput;
        return total;
    }

    ObserveResult observe(const Experience& experience, const CognitiveCue* cognitiveCue = nullptr) {
        sdkThroughput.recordRead<Experience>(tatarus::ThroughputDomain::Cognition);
        Experience effective = experience;

        double dtSeconds = 0.02;
        if (lastObservationTimestampNs != 0 && effective.timestampNs > lastObservationTimestampNs) {
            dtSeconds = std::clamp(
                static_cast<double>(effective.timestampNs - lastObservationTimestampNs) * 1e-9,
                1e-4, 0.25);
        }
        if (effective.timestampNs != 0) lastObservationTimestampNs = effective.timestampNs;

        lastVestibularPercept = vestibularSystem.step(effective.imu, dtSeconds);
        if (lastVestibularPercept.available) {
            effective.context.push_back(clampUnit(lastVestibularPercept.angularMotion));
            effective.context.push_back(clampUnit(lastVestibularPercept.linearMotion));
            effective.context.push_back(clampUnit(
                2.0 * lastVestibularPercept.gravityConfidence - 1.0));
        }

        if (effective.visualFrame.has_value()) {
            const auto& camera = *effective.visualFrame;
            auto ocularFrame = ocularSystem.process(
                camera.width, camera.height, camera.rgb, dtSeconds,
                lastVestibularPercept.vorEyeVelocityDegPerSecond);
            lastOcularTelemetry = ocularFrame.telemetry;
            auto percept = visualPathway.perceive(
                ocularFrame.width, ocularFrame.height, ocularFrame.retinalRgb);
            effective.vision.insert(
                effective.vision.end(),
                percept.opticNerveEvents.begin(), percept.opticNerveEvents.end());
            effective.vision.insert(
                effective.vision.end(),
                percept.objectCortexEvents.begin(), percept.objectCortexEvents.end());
            effective.context.push_back(clampUnit(
                2.0 * std::min(1.0,
                    static_cast<double>(percept.objects.size())
                        / static_cast<double>(visualPathway.config().maximumObjects))
                - 1.0));
            if (!percept.objects.empty()) {
                effective.context.push_back(clampUnit(
                    2.0 * percept.objects.front().saliency - 1.0));
            }
            effective.context.push_back(clampUnit(
                lastOcularTelemetry.gazeYawDegrees
                / ocularSystem.config().maximumGazeDegrees));
            effective.context.push_back(clampUnit(
                lastOcularTelemetry.gazePitchDegrees
                / ocularSystem.config().maximumGazeDegrees));
            effective.context.push_back(clampUnit(
                2.0 * (lastOcularTelemetry.pupilDiameterMm
                    - ocularSystem.config().minimumPupilDiameterMm)
                    / (ocularSystem.config().maximumPupilDiameterMm
                        - ocularSystem.config().minimumPupilDiameterMm) - 1.0));
            effective.context.push_back(lastOcularTelemetry.saccadeActive ? 1.0 : -1.0);
            lastVisualPercept = std::move(percept);
        }
        sdkThroughput.record(tatarus::ThroughputDomain::Cognition,
            effective.vision.size() + effective.audio.size() + effective.touch.size()
                + effective.spatialContext.size() + effective.context.size(),
            sizeof(double), true, false);
        sdkThroughput.record(tatarus::ThroughputDomain::Cognition, effective.text.size(), sizeof(std::uint8_t), true, false);
        sdkThroughput.recordRead<JointState>(tatarus::ThroughputDomain::Cognition, effective.body.joints.size());
        if (!effective.entity.has_value() && currentEntity.has_value()) effective.entity = currentEntity;
        tatarus::neuro::SensorFrame frame = encodeExperience(effective);
        frame.vestibularEvents = lastVestibularPercept.vestibularNerveEvents;

        std::optional<ActionId> conditioningAction;
        if (effective.action.has_value() && effective.action->id != 0) {
            conditioningAction = effective.action->id;
        } else if (activeAction.has_value() && activeAction->id != 0) {
            conditioningAction = activeAction->id;
        }

        const Prediction expected = pendingPrediction;
        AssemblyId observedAssembly = 0;

        std::unique_ptr<tatarus::neuro::CognitiveBridge> cognitiveBridge;
        tatarus::neuro::CognitiveCommand cognitiveCommand;
        if (cognitiveCue && !cognitiveCue->neutral()) {
            cognitiveCommand = toNeuroCommand(*cognitiveCue);
            cognitiveBridge = std::make_unique<tatarus::neuro::CognitiveBridge>(*core);
        }

        for (int step = 0; step < config.microstepsPerObservation; ++step) {
            if (cognitiveBridge) {
                lastMotorAction = cognitiveBridge->step(frame, cognitiveCommand).action;
            } else {
                lastMotorAction = core->step(frame);
            }
            const auto state = core->inspect();
            if (state.activeAssembly >= 0
                && static_cast<std::size_t>(state.activeAssembly) < state.assemblies.size()) {
                observedAssembly = state.assemblies[static_cast<std::size_t>(state.activeAssembly)].id;
            }
        }

        ++experiences;
        double currentPredictionError = 0.0;
        if (expected.available && observedAssembly != 0) {
            ++predictionCount;
            const bool hit = expected.expectedAssemblyId == observedAssembly;
            if (hit) ++predictionHits;
            else ++predictionMisses;
            const double p = clamp01(expected.confidence);
            const double y = hit ? 1.0 : 0.0;
            const double diff = p - y;
            brierSum += diff * diff;
            currentPredictionError = hit ? (1.0 - p) : p;
            predictionErrorSum += currentPredictionError;
        }

        double learnedNovelty = frame.novelty;
        if (previousAssembly != 0 && observedAssembly != 0) {
            const auto key = std::pair{previousAssembly, observedAssembly};
            const auto previousCount = transitions[key];
            std::uint64_t outgoingBefore = 0;
            sdkThroughput.record(tatarus::ThroughputDomain::Cognition, transitions.size(), sizeof(std::pair<std::uint64_t, std::uint64_t>) + sizeof(std::uint64_t), true, false);
            for (const auto& [edge, count] : transitions) {
                if (edge.first == previousAssembly) outgoingBefore += count;
            }
            if (outgoingBefore > 0) {
                const double knownProbability = static_cast<double>(previousCount) / static_cast<double>(outgoingBefore);
                learnedNovelty = std::max(learnedNovelty, 1.0 - knownProbability);
            } else {
                learnedNovelty = 1.0;
            }
            sdkThroughput.record(tatarus::ThroughputDomain::Cognition, 1, sizeof(key) + sizeof(std::uint64_t), true, true);
            ++transitions[key];
            if (conditioningAction.has_value()) {
                sdkThroughput.record(tatarus::ThroughputDomain::Cognition, 1, sizeof(TransitionKey) + sizeof(std::uint64_t), true, true);
                ++actionTransitions[TransitionKey{previousAssembly, *conditioningAction, observedAssembly}];
            }
            if (effective.entity.has_value() && effective.entity->id != 0) {
                sdkThroughput.record(tatarus::ThroughputDomain::Cognition, 1, sizeof(EntityTransitionKey) + sizeof(std::uint64_t), true, true);
                ++entityTransitions[EntityTransitionKey{previousAssembly, effective.entity->id, observedAssembly}];
            }
        }

        if (effective.entity.has_value()) {
            currentEntity = effective.entity;
        }
        currentAssembly = observedAssembly;
        if (observedAssembly != 0) previousAssembly = observedAssembly;
        currentNovelty = clamp01(learnedNovelty);
        lastPredictionError = currentPredictionError;
        pendingPrediction = predictionFor(currentAssembly, conditioningAction, currentEntity ? std::optional<EntityId>(currentEntity->id) : std::nullopt);

        ObserveResult result;
        result.assemblyId = observedAssembly;
        result.prediction = pendingPrediction;
        result.predictionError = currentPredictionError;
        result.novelty = currentNovelty;
        result.metrics = publicMetrics();
        result.biology = biologicalTelemetry();
        result.physiology = physiologyTelemetry();
        result.prospection = prospectiveTelemetry();
        result.motor = motorTelemetry();
        return result;
    }

    CartographyUpdate integrateScan(const ScannerFrame& scannerFrame) {
        if (!config.enableCartography) {
            throw std::logic_error("Cartography module is disabled");
        }
        sdkThroughput.record(
            tatarus::ThroughputDomain::SpatialMemory,
            scannerFrame.readings.size(),
            sizeof(RangeReading),
            true,
            false);
        auto update = cartographer.integrate(scannerFrame);
        sdkThroughput.record(
            tatarus::ThroughputDomain::SpatialMemory,
            static_cast<std::size_t>(update.touchedVoxels),
            sizeof(MapVoxel),
            true,
            true);
        return update;
    }

    ExplorerResult observeExplorer(
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const CognitiveCue* cognitiveCue = nullptr) {
        const CartographyUpdate mapping = integrateScan(scannerFrame);
        Experience effective = experience;

        // Grid-cell-like pose phases make the mapped location available to the
        // recurrent nervous substrate without injecting a route or motor command.
        constexpr double twoPi = 6.28318530717958647692;
        constexpr std::array<double, 3> periodsInVoxels{4.0, 16.0, 64.0};
        for (const double period : periodsInVoxels) {
            for (const double position : scannerFrame.pose.positionMeters) {
                const double phase = twoPi * position
                    / (period * config.cartography.voxelSizeMeters);
                effective.spatialContext.push_back(std::sin(phase));
                effective.spatialContext.push_back(std::cos(phase));
            }
        }
        effective.spatialContext.push_back(signedHash(scannerFrame.environmentId));

        const double mapped = static_cast<double>(mapping.summary.mappedVoxels);
        effective.context.push_back(clampUnit(2.0 * mapping.localNovelty - 1.0));
        effective.context.push_back(clampUnit(2.0 * mapping.frontierRatio - 1.0));
        effective.context.push_back(mapped > 0.0
            ? clampUnit(2.0 * static_cast<double>(mapping.summary.occupiedVoxels) / mapped - 1.0)
            : -1.0);
        for (const double proximity : mapping.obstacleProximity) {
            effective.vision.push_back(clampUnit(proximity));
        }
        effective.environment.novelty = std::max(
            effective.environment.novelty, mapping.localNovelty);

        // Environment identity scopes episodic place/action recall while still
        // allowing the distributed sensory representation to generalize.
        if (effective.spatialEpisodeContext == 0) {
            effective.spatialEpisodeContext = scannerFrame.environmentId;
        } else {
            effective.spatialEpisodeContext ^= scannerFrame.environmentId
                + 0x9e3779b97f4a7c15ULL
                + (effective.spatialEpisodeContext << 6U)
                + (effective.spatialEpisodeContext >> 2U);
        }

        // Semantic detections enter the ordinary text modality and are also
        // retained on occupied map voxels for later human or robot queries.
        constexpr std::size_t maximumSemanticBytes = 512U;
        for (const auto& reading : scannerFrame.readings) {
            if (!reading.hit || reading.semanticLabel.empty()
                || effective.text.size() >= maximumSemanticBytes) {
                continue;
            }
            if (!effective.text.empty()) effective.text.push_back(0U);
            const std::size_t available = maximumSemanticBytes - effective.text.size();
            const std::size_t count = std::min(available, reading.semanticLabel.size());
            for (std::size_t i = 0; i < count; ++i) {
                effective.text.push_back(static_cast<std::uint8_t>(reading.semanticLabel[i]));
            }
        }

        ExplorerResult result;
        result.cartography = mapping;
        result.cognition = observe(effective, cognitiveCue);
        return result;
    }

    IdentityDecision observeIdentity(const IdentityObservation& observation) {
        if (!config.enableIdentity) throw std::logic_error("Identity module is disabled");
        tatarus::neuro::identity::PersonObservation source;
        source.features.assign(observation.features.begin(), observation.features.end());
        source.cameraId = observation.cameraId;
        source.timestampSeconds = observation.timestampSeconds;
        source.quality = observation.quality;
        const auto decision = identity.observe(source);

        IdentityDecision result;
        result.kind = convertDecision(decision.kind);
        result.entityId = decision.identityId;
        result.activeAssemblyId = decision.activeAssemblyId;
        result.confidence = decision.confidence;
        result.hypothesisEvidence = decision.hypothesisEvidence;
        result.evidenceRequired = decision.evidenceRequired;
        result.baselineAccepted = decision.baselineAccepted;

        if (decision.identityId != 0) {
            const bool known = decision.kind == tatarus::neuro::identity::DecisionKind::MatchedIdentity
                || decision.kind == tatarus::neuro::identity::DecisionKind::NewIdentity
                || decision.kind == tatarus::neuro::identity::DecisionKind::ProvisionalAssociation;
            currentEntity = EntityContext{
                .id = decision.identityId,
                .confidence = decision.confidence,
                .familiarity = decision.confidence,
                .known = known,
                .newlyConsolidated = decision.kind == tatarus::neuro::identity::DecisionKind::NewIdentity,
            };
        }
        return result;
    }

    void rest(std::uint64_t ticks) {
        tatarus::neuro::SensorFrame empty;
        empty.internalEnergy = 1.0;
        for (std::uint64_t i = 0; i < ticks; ++i) core->step(empty);
    }

    void saveOwnState(const std::filesystem::path& path) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot create SDK state file");
        output.write(kStateMagic, sizeof(kStateMagic));
        writePod(output, config.seed);
        writePod(output, experiences);
        writePod(output, predictionCount);
        writePod(output, predictionHits);
        writePod(output, predictionMisses);
        writePod(output, brierSum);
        writePod(output, predictionErrorSum);
        writePod(output, previousAssembly);
        writePod(output, currentAssembly);
        writePod(output, currentNovelty);
        writePod(output, lastPredictionError);

        const bool hasEntity = currentEntity.has_value();
        writePod(output, hasEntity);
        if (hasEntity) {
            writePod(output, currentEntity->id);
            writePod(output, currentEntity->confidence);
            writePod(output, currentEntity->familiarity);
            writePod(output, currentEntity->known);
            writePod(output, currentEntity->newlyConsolidated);
        }

        const std::uint64_t transitionCount = transitions.size();
        writePod(output, transitionCount);
        for (const auto& [key, count] : transitions) {
            writePod(output, key.first);
            writePod(output, key.second);
            writePod(output, count);
        }

        const std::uint64_t actionTransitionCount = actionTransitions.size();
        writePod(output, actionTransitionCount);
        for (const auto& [key, count] : actionTransitions) {
            writePod(output, key.source);
            writePod(output, key.action);
            writePod(output, key.target);
            writePod(output, count);
        }

        const std::uint64_t entityTransitionCount = entityTransitions.size();
        writePod(output, entityTransitionCount);
        for (const auto& [key, count] : entityTransitions) {
            writePod(output, key.source);
            writePod(output, key.entity);
            writePod(output, key.target);
            writePod(output, count);
        }
    }

    void loadOwnState(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open SDK state file");
        char magic[8]{};
        input.read(magic, sizeof(magic));
        if (!input || !std::equal(std::begin(magic), std::end(magic), std::begin(kStateMagic))) {
            throw std::runtime_error("Unknown TATARUS SDK state format");
        }
        std::uint64_t storedSeed = 0;
        readPod(input, storedSeed);
        if (storedSeed != config.seed) throw std::runtime_error("Snapshot seed does not match RobotMind config");
        readPod(input, experiences);
        readPod(input, predictionCount);
        readPod(input, predictionHits);
        readPod(input, predictionMisses);
        readPod(input, brierSum);
        readPod(input, predictionErrorSum);
        readPod(input, previousAssembly);
        readPod(input, currentAssembly);
        readPod(input, currentNovelty);
        readPod(input, lastPredictionError);

        bool hasEntity = false;
        readPod(input, hasEntity);
        currentEntity.reset();
        if (hasEntity) {
            EntityContext entity;
            readPod(input, entity.id);
            readPod(input, entity.confidence);
            readPod(input, entity.familiarity);
            readPod(input, entity.known);
            readPod(input, entity.newlyConsolidated);
            currentEntity = entity;
        }

        transitions.clear();
        std::uint64_t transitionCount = 0;
        readPod(input, transitionCount);
        if (transitionCount > 10'000'000ULL) throw std::runtime_error("Snapshot contains implausibly many transitions");
        for (std::uint64_t i = 0; i < transitionCount; ++i) {
            std::uint64_t source = 0, target = 0, count = 0;
            readPod(input, source);
            readPod(input, target);
            readPod(input, count);
            transitions[{source, target}] = count;
        }

        actionTransitions.clear();
        std::uint64_t actionTransitionCount = 0;
        readPod(input, actionTransitionCount);
        if (actionTransitionCount > 10'000'000ULL) throw std::runtime_error("Snapshot contains implausibly many action transitions");
        for (std::uint64_t i = 0; i < actionTransitionCount; ++i) {
            TransitionKey key;
            std::uint64_t count = 0;
            readPod(input, key.source);
            readPod(input, key.action);
            readPod(input, key.target);
            readPod(input, count);
            actionTransitions[key] = count;
        }

        entityTransitions.clear();
        std::uint64_t entityTransitionCount = 0;
        readPod(input, entityTransitionCount);
        if (entityTransitionCount > 10'000'000ULL) throw std::runtime_error("Snapshot contains implausibly many entity transitions");
        for (std::uint64_t i = 0; i < entityTransitionCount; ++i) {
            EntityTransitionKey key;
            std::uint64_t count = 0;
            readPod(input, key.source);
            readPod(input, key.entity);
            readPod(input, key.target);
            readPod(input, count);
            entityTransitions[key] = count;
        }
        pendingPrediction = predictionFor(currentAssembly, std::nullopt, currentEntity ? std::optional<EntityId>(currentEntity->id) : std::nullopt);
    }

    void saveSnapshot(const std::filesystem::path& directory) const {
        const auto parent = directory.parent_path().empty() ? std::filesystem::current_path() : directory.parent_path();
        std::filesystem::create_directories(parent);
        const auto temp = parent / (directory.filename().string() + ".tmp");
        const auto backup = parent / (directory.filename().string() + ".bak");
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        std::filesystem::create_directories(temp);

        core->saveSnapshot(temp / "experience_core.tns");
        saveOwnState(temp / "sdk_state.bin");
        if (config.enableIdentity) identity.save(temp / "identity");
        if (config.enableCartography) cartographer.save(temp / "environment_map.tcm");

        std::ofstream manifest(temp / "manifest.txt", std::ios::trunc);
        if (!manifest) throw std::runtime_error("Cannot create snapshot manifest");
        manifest << "format=TATARUS_EMBODIED_MEMORY_IDENTITY_SDK\n";
        manifest << "version=" << TATARUS_SDK_VERSION_STRING << "\n";
        manifest << "seed=" << config.seed << "\n";
        manifest << "core_state_hash=" << core->stateHash() << "\n";
        manifest << "experiences=" << experiences << "\n";
        manifest << "contextual_transitions=" << transitions.size() << "\n";
        manifest << "neural_transitions=" << core->prospectiveMetrics().learnedTransitions << "\n";
        manifest << "biological_state_hash=" << core->stateHash() << "\n";
        manifest << "identities=" << (config.enableIdentity ? identity.identityCount() : 0) << "\n";
        manifest << "cartography=" << (config.enableCartography ? "enabled" : "disabled") << "\n";
        manifest.close();

        std::filesystem::remove_all(backup, ec);
        if (std::filesystem::exists(directory)) {
            std::filesystem::rename(directory, backup, ec);
            if (ec) throw std::runtime_error("Cannot move previous snapshot to backup: " + ec.message());
        }
        ec.clear();
        std::filesystem::rename(temp, directory, ec);
        if (ec) {
            if (std::filesystem::exists(backup)) {
                std::error_code restoreEc;
                std::filesystem::rename(backup, directory, restoreEc);
            }
            throw std::runtime_error("Cannot publish new snapshot: " + ec.message());
        }
        std::filesystem::remove_all(backup, ec);
    }

    bool loadSnapshot(const std::filesystem::path& directory) {
        if (!std::filesystem::is_directory(directory)) return false;
        const auto stateFile = directory / "sdk_state.bin";
        const auto coreFile = directory / "experience_core.tns";
        if (!std::filesystem::exists(stateFile) || !std::filesystem::exists(coreFile)) return false;
        core->loadSnapshot(coreFile);
        loadOwnState(stateFile);
        if (config.enableIdentity) {
            const auto identityBase = directory / "identity";
            if (std::filesystem::exists(identityBase.string() + ".pms")) {
                identity.load(identityBase);
            }
        }
        if (config.enableCartography) {
            // Snapshots created before the explorer extension remain valid and
            // simply begin with an empty environment map.
            cartographer.load(directory / "environment_map.tcm");
        }
        return true;
    }

    std::string spatialJson() const {
        const auto state = core->inspectSpatial();
        constexpr std::size_t maxRenderedAxons = 25'000;
        const std::size_t axonStride = std::max<std::size_t>(
            1, (state.axons.size() + maxRenderedAxons - 1) / maxRenderedAxons);
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{\"schema\":\"tatarus-spatial-v3\"";
        out << ",\"sdk_version\":\"" << TATARUS_SDK_VERSION_STRING << '"';
        out << ",\"seed\":" << config.seed;
        out << ",\"step\":" << state.step;
        out << ",\"dt_ms\":" << state.dtMs;
        out << ",\"scale\":{\"neurons\":" << state.neurons.size()
            << ",\"dendrites\":" << state.dendrites.size()
            << ",\"axons\":" << state.axons.size()
            << ",\"hemispheres\":2,\"regions\":6,\"layers\":5}";
        out << ",\"render_lod\":{\"axon_stride\":" << axonStride
            << ",\"total_axons\":" << state.axons.size() << '}';
        out << ",\"tissue_shape\":{\"type\":\"" << state.tissueShape << "\",\"center\":";
        writeSpatialPoint(out, state.tissueCenter);
        out << ",\"radii_um\":";
        writeSpatialPoint(out, state.tissueRadii);
        out << ",\"interhemispheric_fissure_um\":"
            << state.interhemisphericFissureUm
            << ",\"baseline_volume_um3\":" << state.baselineVolumeUm3
            << ",\"current_volume_um3\":" << state.currentVolumeUm3
            << ",\"volume_ratio\":" << state.volumeRatio
            << ",\"linear_expansion\":" << state.linearExpansion
            << ",\"effective_tissue_mass_ng\":" << state.effectiveTissueMassNg
            << ",\"net_biomass_change_ng\":" << state.netBiomassChangeNg
            << ",\"solid_packing_fraction\":" << state.solidPackingFraction
            << ",\"ecs_fraction\":" << state.extracellularSpaceFraction
            << ",\"pressure_kpa\":" << state.pressureKPa
            << ",\"material_reserve\":" << state.materialReserve << '}';

        out << ",\"neurons\":[";
        for (std::size_t index = 0; index < state.neurons.size(); ++index) {
            if (index) out << ',';
            const auto& neuron = state.neurons[index];
            out << "{\"id\":" << neuron.index
                << ",\"role\":\"" << populationRoleName(neuron.role) << "\""
                << ",\"subtype\":\"" << neuronSubtypeName(neuron.subtype) << "\""
                << ",\"hemisphere\":\"" << hemisphereName(neuron.hemisphere) << "\""
                << ",\"region\":\"" << brainRegionName(neuron.region) << "\""
                << ",\"layer\":\"" << corticalLayerName(neuron.layer) << "\""
                << ",\"soma\":";
            writeSpatialPoint(out, neuron.soma);
            out << ",\"first_segment\":" << neuron.firstSegment
                << ",\"segment_count\":" << neuron.segmentCount << '}';
        }
        out << ']';

        out << ",\"dendrites\":[";
        for (std::size_t index = 0; index < state.dendrites.size(); ++index) {
            if (index) out << ',';
            const auto& segment = state.dendrites[index];
            out << "{\"id\":" << segment.index
                << ",\"neuron\":" << segment.neuron
                << ",\"parent\":" << segment.parent
                << ",\"position\":";
            writeSpatialPoint(out, segment.position);
            out << ",\"branch_order\":"
                << static_cast<unsigned int>(segment.branchOrder)
                << ",\"astrocyte\":" << segment.astrocyte << '}';
        }
        out << ']';

        out << ",\"axons\":[";
        bool emittedAxon = false;
        for (std::size_t index = 0; index < state.axons.size(); ++index) {
            if (index % axonStride != 0) continue;
            if (emittedAxon) out << ',';
            emittedAxon = true;
            const auto& axon = state.axons[index];
            out << "{\"id\":" << axon.index
                << ",\"pre\":" << axon.pre
                << ",\"post\":" << axon.post
                << ",\"segment\":" << axon.segment
                << ",\"receptor\":\"" << receptorName(axon.receptor) << "\""
                << ",\"start\":";
            writeSpatialPoint(out, axon.start);
            out << ",\"end\":";
            writeSpatialPoint(out, axon.end);
            out << ",\"length_um\":" << axon.lengthUm
                << ",\"oligodendrocyte\":" << axon.oligodendrocyte
                << ",\"microglia\":" << axon.microglia
                << ",\"active\":" << (axon.active ? "true" : "false")
                << '}';
        }
        out << ']';

        out << ",\"astrocytes\":[";
        for (std::size_t index = 0; index < state.astrocytes.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.astrocytes[index];
            out << "{\"id\":" << cell.index << ",\"center\":";
            writeSpatialPoint(out, cell.center);
            out << ",\"vessel\":" << cell.vessel << '}';
        }
        out << ']';

        out << ",\"capillaries\":[";
        for (std::size_t index = 0; index < state.capillaries.size(); ++index) {
            if (index) out << ',';
            const auto& vessel = state.capillaries[index];
            out << "{\"id\":" << vessel.index << ",\"position\":";
            writeSpatialPoint(out, vessel.position);
            out << '}';
        }
        out << ']';

        out << ",\"oligodendrocytes\":[";
        for (std::size_t index = 0; index < state.oligodendrocytes.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.oligodendrocytes[index];
            out << "{\"id\":" << cell.index << ",\"center\":";
            writeSpatialPoint(out, cell.center);
            out << ",\"vessel\":" << cell.vessel << '}';
        }
        out << ']';

        out << ",\"microglia\":[";
        for (std::size_t index = 0; index < state.microglia.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.microglia[index];
            out << "{\"id\":" << cell.index << ",\"center\":";
            writeSpatialPoint(out, cell.center);
            out << ",\"vessel\":" << cell.vessel << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string physiologyJson() const {
        const auto state = core->inspectPhysiology();
        const auto& p = state.metrics;
        std::ostringstream out;
        out << std::fixed << std::setprecision(7);
        out << "{\"schema\":\"tatarus-physiology-v3\""
            << ",\"sdk_version\":\"" << TATARUS_SDK_VERSION_STRING << "\""
            << ",\"sleep_stage\":\"" << sleepStageName(p.sleepStage) << "\""
            << ",\"circadian_hours\":" << p.circadianPhaseHours
            << ",\"sleep_pressure\":" << p.sleepPressure
            << ",\"ecs_volume_fraction\":" << p.extracellularVolumeFraction
            << ",\"glymphatic_clearance\":" << p.glymphaticClearance
            << ",\"ions\":{\"na_mm\":" << p.meanExtracellularNaMm
            << ",\"k_mm\":" << p.meanExtracellularKMm
            << ",\"ca_mm\":" << p.meanExtracellularCaMm
            << ",\"cl_mm\":" << p.meanExtracellularClMm << '}'
            << ",\"energy\":{\"atp\":" << p.meanAtp
            << ",\"pump\":" << p.meanPumpActivity
            << ",\"atp_consumed\":" << p.cumulativeAtpConsumed
            << ",\"heat_pj\":" << p.heatProductionPj
            << ",\"entropy_pj_k\":" << p.entropyProductionPjPerK << '}'
            << ",\"modulators\":{\"dopamine\":" << p.dopamine
            << ",\"serotonin\":" << p.serotonin
            << ",\"noradrenaline\":" << p.noradrenaline
            << ",\"acetylcholine\":" << p.acetylcholine
            << ",\"camp\":" << p.meanCamp << ",\"ip3\":" << p.meanIp3 << '}'
            << ",\"plasticity\":{\"creb\":" << p.meanCrebActivation
            << ",\"protein\":" << p.meanProteinPool
            << ",\"tag\":" << p.meanSynapticTag
            << ",\"tagged_synapses\":" << p.taggedSynapses
            << ",\"l_ltp_events\":" << p.longTermPotentiationEvents
            << ",\"l_ltd_events\":" << p.longTermDepressionEvents << '}'
            << ",\"oscillations\":{\"gamma_power\":" << p.gammaPower
            << ",\"theta_power\":" << p.thetaPower << '}'
            << ",\"neurons\": [";
        for (std::size_t i = 0; i < state.neurons.size(); ++i) {
            if (i) out << ',';
            const auto& n = state.neurons[i];
            out << "{\"id\":" << n.index << ",\"subtype\":\""
                << neuronSubtypeName(n.subtype) << "\",\"position\":";
            writeSpatialPoint(out, n.position);
            out << ",\"na_mm\":" << n.extracellularNaMm
                << ",\"k_mm\":" << n.extracellularKMm
                << ",\"ca_mm\":" << n.extracellularCaMm
                << ",\"cl_mm\":" << n.extracellularClMm
                << ",\"atp\":" << n.atp
                << ",\"pump\":" << n.pumpActivity
                << ",\"excitability_shift_mv\":" << n.excitabilityShiftMv
                << ",\"dopamine\":" << n.dopamine
                << ",\"serotonin\":" << n.serotonin
                << ",\"noradrenaline\":" << n.noradrenaline
                << ",\"acetylcholine\":" << n.acetylcholine
                << ",\"camp\":" << n.camp << ",\"ip3\":" << n.ip3
                << ",\"hcn\":" << n.hcnDensity << ",\"kv\":" << n.kvDensity
                << ",\"creb\":" << n.crebActivation
                << ",\"mrna\":" << n.mrna << ",\"protein\":" << n.proteinPool
                << ",\"homer1a\":" << n.homer1a << ",\"waste\":" << n.waste << '}';
        }
        out << "],\"synapses\":[";
        for (std::size_t i = 0; i < state.synapses.size(); ++i) {
            if (i) out << ',';
            const auto& s = state.synapses[i];
            out << "{\"id\":" << s.index << ",\"tag\":" << s.tag
                << ",\"polarity\":" << s.polarity
                << ",\"captured_protein\":" << s.capturedProtein << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string liveJson() const {
        const auto state = core->inspectSpatial();
        constexpr std::size_t maxRenderedAxons = 25'000;
        const std::size_t axonStride = std::max<std::size_t>(
            1, (state.axons.size() + maxRenderedAxons - 1) / maxRenderedAxons);
        const auto physiologyState = core->inspectPhysiology();
        const auto& neural = core->metrics();
        const auto biology = biologicalTelemetry();
        const auto prospective = prospectiveTelemetry();
        const auto spatialMemory = core->spatialMemoryMetrics();
        const auto motor = motorTelemetry();
        const auto product = publicMetrics();
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{\"schema\":\"tatarus-live-v3\"";
        out << ",\"sdk_version\":\"" << TATARUS_SDK_VERSION_STRING << "\"";
        out << ",\"step\":" << state.step
            << ",\"simulated_ms\":" << state.step * state.dtMs
            << ",\"experiences\":" << product.experiences
            << ",\"current_assembly\":" << currentAssembly
            << ",\"novelty\":" << currentNovelty
            << ",\"prediction_error\":" << lastPredictionError;

        out << ",\"metrics\":{";
        out << "\"total_spikes\":" << neural.totalSpikes
            << ",\"total_transmissions\":" << neural.totalTransmissions
            << ",\"active_synapses\":" << neural.activeSynapses
            << ",\"assemblies\":" << neural.assemblyCount
            << ",\"mean_rate_hz\":" << neural.meanRateHz
            << ",\"mean_energy\":" << neural.meanEnergy
            << ",\"dopamine\":" << neural.dopamine
            << ",\"acetylcholine\":" << neural.acetylcholine
            << ",\"finite\":" << (neural.finite ? "true" : "false")
            << '}';

        out << ",\"biology\":{";
        out << "\"dendritic_spikes\":" << biology.dendriticSpikes
            << ",\"oxygen\":" << biology.oxygen
            << ",\"glucose\":" << biology.glucose
            << ",\"flow\":" << biology.flow
            << ",\"mean_axon_length_um\":" << biology.meanAxonLengthUm
            << ",\"myelin_coverage\":" << biology.myelinCoverage
            << ",\"conduction_velocity_mps\":" << biology.conductionVelocityMps
            << ",\"effective_delay_ms\":" << biology.effectiveDelayMs
            << ",\"oligodendrocyte_reserve\":" << biology.oligodendrocyteReserve
            << ",\"microglia_activation\":" << biology.microgliaActivation
            << ",\"complement_tag\":" << biology.complementTag
            << ",\"repair_capacity\":" << biology.repairCapacity
            << ",\"inflammatory_tone\":" << biology.inflammatoryTone
            << ",\"pruning_events\":" << biology.microglialPruningEvents
            << ",\"repair_events\":" << biology.microglialRepairEvents
            << ",\"baseline_active_synapses\":" << biology.baselineActiveSynapses
            << ",\"mechanics_active_synapses\":" << biology.mechanicsActiveSynapses
            << ",\"growth_limited_events\":" << biology.growthLimitedEvents
            << ",\"baseline_tissue_volume_um3\":" << biology.baselineTissueVolumeUm3
            << ",\"tissue_volume_um3\":" << biology.tissueVolumeUm3
            << ",\"tissue_volume_ratio\":" << biology.tissueVolumeRatio
            << ",\"linear_expansion\":" << biology.linearExpansion
            << ",\"effective_tissue_mass_ng\":" << biology.effectiveTissueMassNg
            << ",\"net_biomass_change_ng\":" << biology.netBiomassChangeNg
            << ",\"synaptic_material_volume_um3\":" << biology.synapticMaterialVolumeUm3
            << ",\"solid_packing_fraction\":" << biology.solidPackingFraction
            << ",\"ecs_fraction\":" << biology.extracellularSpaceFraction
            << ",\"tissue_pressure_kpa\":" << biology.tissuePressureKPa
            << ",\"material_reserve\":" << biology.materialReserve
            << ",\"material_synthesized_um3\":" << biology.cumulativeMaterialSynthesizedUm3
            << ",\"material_recycled_um3\":" << biology.cumulativeMaterialRecycledUm3
            << '}';

        out << ",\"prospection\":{";
        out << "\"predicted_assembly\":" << prospective.predictedAssemblyId
            << ",\"confidence\":" << prospective.predictionConfidence
            << ",\"prediction_error\":" << prospective.predictionError
            << ",\"surprise\":" << prospective.temporalSurprise
            << ",\"familiarity\":" << prospective.sequenceFamiliarity
            << '}';

        out << ",\"spatial_memory\":{";
        out << "\"available\":" << (spatialMemory.available ? "true" : "false")
            << ",\"learning_enabled\":" << (spatialMemory.learningEnabled ? "true" : "false")
            << ",\"engrams\":" << spatialMemory.engrams
            << ",\"action_updates\":" << spatialMemory.actionUpdates
            << ",\"novel_discoveries\":" << spatialMemory.novelDiscoveries
            << ",\"revisits\":" << spatialMemory.revisits
            << ",\"successful_consolidations\":" << spatialMemory.successfulConsolidations
            << ",\"failed_consolidations\":" << spatialMemory.failedConsolidations
            << ",\"current_assembly\":" << spatialMemory.currentAssemblyId
            << ",\"current_visits\":" << spatialMemory.currentVisits
            << ",\"familiarity\":" << spatialMemory.currentFamiliarity
            << ",\"preferred_direction\":" << spatialMemory.preferredDirection
            << ",\"confidence\":" << spatialMemory.confidence;
        const auto writeQuad = [&out](std::string_view name, const auto& values) {
            out << ",\"" << name << "\":[";
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index) out << ',';
                out << values[index];
            }
            out << ']';
        };
        writeQuad("reward_value", spatialMemory.rewardValue);
        writeQuad("novelty_value", spatialMemory.noveltyValue);
        writeQuad("frontier_value", spatialMemory.frontierValue);
        writeQuad("success_value", spatialMemory.successValue);
        writeQuad("route_value", spatialMemory.routeValue);
        writeQuad("avoidance_value", spatialMemory.avoidanceValue);
        writeQuad("motor_bias", spatialMemory.motorBias);
        writeQuad("action_observations", spatialMemory.actionObservations);
        writeQuad("failed_episode_observations", spatialMemory.failedEpisodeObservations);
        out << '}';

        out << ",\"motor\":{";
        out << "\"source\":\"tatarus_neural_motor\""
            << ",\"north\":" << motor.directionalActivity[0]
            << ",\"east\":" << motor.directionalActivity[1]
            << ",\"south\":" << motor.directionalActivity[2]
            << ",\"west\":" << motor.directionalActivity[3]
            << ",\"selected_direction\":" << motor.selectedDirection
            << ",\"confidence\":" << motor.confidence
            << ",\"attention\":" << motor.attention
            << ",\"movement\":" << motor.movement
            << '}';

        const auto& physiology = physiologyState.metrics;
        out << ",\"physiology\":{";
        out << "\"sleep_stage\":\"" << sleepStageName(physiology.sleepStage) << "\""
            << ",\"circadian_hours\":" << physiology.circadianPhaseHours
            << ",\"sleep_pressure\":" << physiology.sleepPressure
            << ",\"ecs_volume_fraction\":" << physiology.extracellularVolumeFraction
            << ",\"glymphatic_clearance\":" << physiology.glymphaticClearance
            << ",\"tissue_waste\":" << physiology.tissueWaste
            << ",\"na_mm\":" << physiology.meanExtracellularNaMm
            << ",\"k_mm\":" << physiology.meanExtracellularKMm
            << ",\"ca_mm\":" << physiology.meanExtracellularCaMm
            << ",\"cl_mm\":" << physiology.meanExtracellularClMm
            << ",\"atp\":" << physiology.meanAtp
            << ",\"pump\":" << physiology.meanPumpActivity
            << ",\"heat_pj\":" << physiology.heatProductionPj
            << ",\"dopamine\":" << physiology.dopamine
            << ",\"serotonin\":" << physiology.serotonin
            << ",\"noradrenaline\":" << physiology.noradrenaline
            << ",\"acetylcholine\":" << physiology.acetylcholine
            << ",\"camp\":" << physiology.meanCamp
            << ",\"ip3\":" << physiology.meanIp3
            << ",\"hcn\":" << physiology.meanHcnDensity
            << ",\"kv\":" << physiology.meanKvDensity
            << ",\"creb\":" << physiology.meanCrebActivation
            << ",\"protein\":" << physiology.meanProteinPool
            << ",\"synaptic_tag\":" << physiology.meanSynapticTag
            << ",\"tagged_synapses\":" << physiology.taggedSynapses
            << ",\"l_ltp_events\":" << physiology.longTermPotentiationEvents
            << ",\"l_ltd_events\":" << physiology.longTermDepressionEvents
            << ",\"pv_neurons\":" << physiology.parvalbuminNeurons
            << ",\"sst_neurons\":" << physiology.somatostatinNeurons
            << ",\"vip_neurons\":" << physiology.vipNeurons
            << ",\"gamma_power\":" << physiology.gammaPower
            << ",\"theta_power\":" << physiology.thetaPower
            << '}';

        out << ",\"neurons\":[";
        for (std::size_t index = 0; index < state.neurons.size(); ++index) {
            if (index) out << ',';
            const auto& neuron = state.neurons[index];
            out << "{\"id\":" << neuron.index
                << ",\"subtype\":\"" << neuronSubtypeName(neuron.subtype) << "\""
                << ",\"soma_mv\":" << neuron.somaMv
                << ",\"dendrite_mv\":" << neuron.dendriteMv
                << ",\"rate_hz\":" << neuron.filteredRateHz
                << ",\"energy\":" << neuron.energy
                << ",\"spike_count\":" << neuron.spikeCount
                << ",\"spiking\":" << (neuron.spiking ? "true" : "false")
                << ",\"active\":" << (neuron.active ? "true" : "false");
            if (index < physiologyState.neurons.size()) {
                const auto& p = physiologyState.neurons[index];
                out << ",\"na_mm\":" << p.extracellularNaMm
                    << ",\"k_mm\":" << p.extracellularKMm
                    << ",\"ca_mm\":" << p.extracellularCaMm
                    << ",\"cl_mm\":" << p.extracellularClMm
                    << ",\"atp\":" << p.atp
                    << ",\"pump\":" << p.pumpActivity
                    << ",\"excitability_shift_mv\":" << p.excitabilityShiftMv
                    << ",\"hcn\":" << p.hcnDensity
                    << ",\"kv\":" << p.kvDensity
                    << ",\"creb\":" << p.crebActivation
                    << ",\"protein\":" << p.proteinPool;
            }
            out
                << '}';
        }
        out << ']';

        out << ",\"dendrites\":[";
        for (std::size_t index = 0; index < state.dendrites.size(); ++index) {
            if (index) out << ',';
            const auto& segment = state.dendrites[index];
            out << "{\"id\":" << segment.index
                << ",\"membrane_mv\":" << segment.membraneMv
                << ",\"calcium\":" << segment.calcium
                << ",\"energy\":" << segment.localEnergy
                << ",\"spiking\":" << (segment.spiking ? "true" : "false")
                << '}';
        }
        out << ']';

        out << ",\"axons\":[";
        bool emittedAxon = false;
        for (std::size_t index = 0; index < state.axons.size(); ++index) {
            if (index % axonStride != 0) continue;
            if (emittedAxon) out << ',';
            emittedAxon = true;
            const auto& axon = state.axons[index];
            out << "{\"id\":" << axon.index
                << ",\"weight\":" << axon.weight
                << ",\"eligibility\":" << axon.eligibility
                << ",\"resource\":" << axon.resource
                << ",\"usage\":" << axon.usage
                << ",\"myelin\":" << axon.myelinCoverage
                << ",\"velocity_um_ms\":" << axon.conductionVelocityUmPerMs
                << ",\"delay_ms\":" << axon.effectiveDelayMs
                << ",\"complement\":" << axon.complementTag
                << ",\"damage\":" << axon.damageSignal
                << ",\"repair\":" << axon.repairSignal
                << ",\"active\":" << (axon.active ? "true" : "false");
            if (index < physiologyState.synapses.size()) {
                const auto& p = physiologyState.synapses[index];
                out << ",\"tag\":" << p.tag
                    << ",\"tag_polarity\":" << p.polarity
                    << ",\"captured_protein\":" << p.capturedProtein;
            }
            out
                << '}';
        }
        out << ']';

        out << ",\"signals\":[";
        bool emittedSignal = false;
        for (std::size_t index = 0; index < state.signals.size(); ++index) {
            const auto& signal = state.signals[index];
            if (signal.synapse % axonStride != 0) continue;
            if (emittedSignal) out << ',';
            emittedSignal = true;
            out << "{\"synapse\":" << signal.synapse
                << ",\"progress\":" << signal.progress
                << ",\"amplitude\":" << signal.amplitude << '}';
        }
        out << ']';

        out << ",\"astrocytes\":[";
        for (std::size_t index = 0; index < state.astrocytes.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.astrocytes[index];
            out << "{\"id\":" << cell.index
                << ",\"calcium\":" << cell.calcium
                << ",\"glutamate\":" << cell.glutamateLoad
                << ",\"potassium\":" << cell.potassiumLoad
                << ",\"demand\":" << cell.metabolicDemand
                << ",\"support\":" << cell.support << '}';
        }
        out << ']';

        out << ",\"capillaries\":[";
        for (std::size_t index = 0; index < state.capillaries.size(); ++index) {
            if (index) out << ',';
            const auto& vessel = state.capillaries[index];
            out << "{\"id\":" << vessel.index
                << ",\"flow\":" << vessel.flow
                << ",\"oxygen\":" << vessel.oxygen
                << ",\"glucose\":" << vessel.glucose << '}';
        }
        out << ']';

        out << ",\"oligodendrocytes\":[";
        for (std::size_t index = 0; index < state.oligodendrocytes.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.oligodendrocytes[index];
            out << "{\"id\":" << cell.index
                << ",\"reserve\":" << cell.myelinReserve
                << ",\"load\":" << cell.metabolicLoad
                << ",\"remodeling\":" << cell.remodelingSignal << '}';
        }
        out << ']';

        out << ",\"microglia\":[";
        for (std::size_t index = 0; index < state.microglia.size(); ++index) {
            if (index) out << ',';
            const auto& cell = state.microglia[index];
            out << "{\"id\":" << cell.index
                << ",\"activation\":" << cell.activation
                << ",\"inflammation\":" << cell.inflammatoryTone
                << ",\"phagocytic_load\":" << cell.phagocyticLoad
                << ",\"repair_capacity\":" << cell.repairCapacity
                << ",\"surveillance\":" << cell.surveillance
                << ",\"damage\":" << cell.damageLoad << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string stateJson() const {
        const auto m = publicMetrics();
        const auto p = predictionFor(currentAssembly, activeAction ? std::optional<ActionId>(activeAction->id) : std::nullopt, currentEntity ? std::optional<EntityId>(currentEntity->id) : std::nullopt);
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"sdk_version\": \"" << TATARUS_SDK_VERSION_STRING << "\",\n";
        out << "  \"current_assembly\": " << currentAssembly << ",\n";
        out << "  \"novelty\": " << currentNovelty << ",\n";
        out << "  \"prediction_error\": " << lastPredictionError << ",\n";
        if (currentEntity.has_value()) {
            out << "  \"entity\": {\"id\": " << currentEntity->id << ", \"confidence\": " << currentEntity->confidence << "},\n";
        } else {
            out << "  \"entity\": null,\n";
        }
        out << "  \"prediction\": {\"available\": " << (p.available ? "true" : "false")
            << ", \"expected_assembly\": " << p.expectedAssemblyId
            << ", \"confidence\": " << p.confidence << "},\n";
        out << "  \"visual_pathway\": {\"schema\": \"tatarus-biological-vision-v1\""
            << ", \"raw_frame_seen\": "
            << (lastVisualPercept.sourceWidth != 0U ? "true" : "false")
            << ", \"retina\": \"rgb-luminance-on-off-opponent\""
            << ", \"optic_nerve_events\": "
            << lastVisualPercept.opticNerveEvents.size()
            << ", \"v1_orientation_cells\": "
            << lastVisualPercept.v1OrientationCells.size()
            << ", \"v2_object_parts\": 10"
            << ", \"object_candidates\": " << lastVisualPercept.objects.size()
            << ", \"object_cortex_events\": "
            << lastVisualPercept.objectCortexEvents.size()
            << ", \"ventral\": {\"object\": "
            << lastVisualPercept.ventral.lateralOccipitalObject
            << ", \"face\": " << lastVisualPercept.ventral.fusiformFace
            << ", \"place\": " << lastVisualPercept.ventral.parahippocampalPlace
            << ", \"biological_object\": "
            << lastVisualPercept.ventral.biologicalObject << "}},\n";
        out << "  \"ocular_system\": {\"available\": "
            << (lastOcularTelemetry.available ? "true" : "false")
            << ", \"pupil_diameter_mm\": " << lastOcularTelemetry.pupilDiameterMm
            << ", \"adaptation_gain\": " << lastOcularTelemetry.retinalAdaptationGain
            << ", \"gaze_yaw_deg\": " << lastOcularTelemetry.gazeYawDegrees
            << ", \"gaze_pitch_deg\": " << lastOcularTelemetry.gazePitchDegrees
            << ", \"saccade_active\": " << (lastOcularTelemetry.saccadeActive ? "true" : "false")
            << "},\n";
        out << "  \"vestibular_system\": {\"available\": "
            << (lastVestibularPercept.available ? "true" : "false")
            << ", \"afferent_events\": " << lastVestibularPercept.vestibularNerveEvents.size()
            << ", \"angular_motion\": " << lastVestibularPercept.angularMotion
            << ", \"linear_motion\": " << lastVestibularPercept.linearMotion
            << ", \"gravity_confidence\": " << lastVestibularPercept.gravityConfidence
            << ", \"canal\": [" << lastVestibularPercept.canal[0] << ','
            << lastVestibularPercept.canal[1] << ',' << lastVestibularPercept.canal[2] << "]"
            << "},\n";
        out << "  \"metrics\": {\n";
        out << "    \"experiences\": " << m.experiences << ",\n";
        out << "    \"predictions\": " << m.predictions << ",\n";
        out << "    \"hits\": " << m.predictionHits << ",\n";
        out << "    \"misses\": " << m.predictionMisses << ",\n";
        out << "    \"brier\": " << m.cumulativeBrier << ",\n";
        out << "    \"learned_transitions\": " << m.learnedTransitions << ",\n";
        out << "    \"contextual_transitions\": " << m.contextualTransitions << ",\n";
        out << "    \"action_conditioned_transitions\": " << m.actionConditionedTransitions << ",\n";
        out << "    \"entity_conditioned_transitions\": " << m.entityConditionedTransitions << ",\n";
        out << "    \"identities\": " << m.identities << "\n";
        out << "  },\n";
        const auto biology = biologicalTelemetry();
        out << "  \"biology\": {\n";
        out << "    \"dendritic_segments\": " << biology.dendriticSegments << ",\n";
        out << "    \"astrocytes\": " << biology.astrocytes << ",\n";
        out << "    \"capillaries\": " << biology.capillaries << ",\n";
        out << "    \"oligodendrocytes\": " << biology.oligodendrocytes << ",\n";
        out << "    \"microglia\": " << biology.microglia << ",\n";
        out << "    \"myelin_coverage\": " << biology.myelinCoverage << ",\n";
        out << "    \"conduction_velocity_mps\": " << biology.conductionVelocityMps << ",\n";
        out << "    \"effective_delay_ms\": " << biology.effectiveDelayMs << ",\n";
        out << "    \"microglia_activation\": " << biology.microgliaActivation << ",\n";
        out << "    \"complement_tag\": " << biology.complementTag << ",\n";
        out << "    \"repair_capacity\": " << biology.repairCapacity << ",\n";
        out << "    \"prune_events\": " << biology.pruneEvents << ",\n";
        out << "    \"repair_events\": " << biology.repairEvents << "\n";
        out << "  },\n";
        const auto physiology = physiologyTelemetry();
        const char* sleep = physiology.sleepPhase == SleepPhase::Nrem
            ? "nrem" : physiology.sleepPhase == SleepPhase::Rem ? "rem" : "wake";
        out << "  \"physiology\": {\n";
        out << "    \"sleep_stage\": \"" << sleep << "\",\n";
        out << "    \"extracellular_na_mm\": " << physiology.extracellularNaMm << ",\n";
        out << "    \"extracellular_k_mm\": " << physiology.extracellularKMm << ",\n";
        out << "    \"extracellular_ca_mm\": " << physiology.extracellularCaMm << ",\n";
        out << "    \"extracellular_cl_mm\": " << physiology.extracellularClMm << ",\n";
        out << "    \"atp\": " << physiology.atp << ",\n";
        out << "    \"pump_activity\": " << physiology.pumpActivity << ",\n";
        out << "    \"creb\": " << physiology.crebActivation << ",\n";
        out << "    \"tagged_synapses\": " << physiology.taggedSynapses << ",\n";
        out << "    \"gamma_power\": " << physiology.gammaPower << ",\n";
        out << "    \"theta_power\": " << physiology.thetaPower << "\n";
        out << "  },\n";
        const auto prospective = prospectiveTelemetry();
        out << "  \"prospection\": {\n";
        out << "    \"predicted_assembly\": " << prospective.predictedAssemblyId << ",\n";
        out << "    \"confidence\": " << prospective.predictionConfidence << ",\n";
        out << "    \"prediction_error\": " << prospective.predictionError << ",\n";
        out << "    \"temporal_surprise\": " << prospective.temporalSurprise << ",\n";
        out << "    \"sequence_familiarity\": " << prospective.sequenceFamiliarity << "\n";
        out << "  }\n";
        out << "}\n";
        return out.str();
    }

    mutable std::mutex mutex;
    RobotMindConfig config;
    std::unique_ptr<tatarus::neuro::PersistentNervousSystem> core;
    tatarus::neuro::identity::PersonMemory identity;
    tatarus::neuro::vision::OcularSystem ocularSystem;
    tatarus::neuro::vision::OcularTelemetry lastOcularTelemetry;
    tatarus::neuro::vision::VisualPathway visualPathway;
    tatarus::neuro::vestibular::VestibularSystem vestibularSystem;
    tatarus::neuro::vestibular::VestibularPercept lastVestibularPercept;
    tatarus::neuro::vision::VisualPercept lastVisualPercept;
    tatarus::detail::EnvironmentCartographer cartographer;
    std::unordered_map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t, PairHash> transitions;
    std::unordered_map<TransitionKey, std::uint64_t, TransitionKeyHash> actionTransitions;
    std::unordered_map<EntityTransitionKey, std::uint64_t, EntityTransitionKeyHash> entityTransitions;
    mutable ThroughputCounters sdkThroughput;
    std::uint64_t experiences = 0;
    TimestampNs lastObservationTimestampNs = 0;
    std::uint64_t predictionCount = 0;
    std::uint64_t predictionHits = 0;
    std::uint64_t predictionMisses = 0;
    double brierSum = 0.0;
    double predictionErrorSum = 0.0;
    AssemblyId previousAssembly = 0;
    AssemblyId currentAssembly = 0;
    Prediction pendingPrediction;
    double currentNovelty = 0.0;
    double lastPredictionError = 0.0;
    std::optional<EntityContext> currentEntity;
    std::optional<ActionEvent> activeAction;
    tatarus::neuro::MotorAction lastMotorAction;
};

RobotMind::RobotMind(RobotMindConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
RobotMind::~RobotMind() = default;
RobotMind::RobotMind(RobotMind&&) noexcept = default;
RobotMind& RobotMind::operator=(RobotMind&&) noexcept = default;

ObserveResult RobotMind::observe(const Experience& experience) {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->observe(experience);
}

ObserveResult RobotMind::observeWithCognitiveCue(
    const Experience& experience,
    const CognitiveCue& cue) {
    const std::scoped_lock lock(impl_->mutex);
    if (cue.neutral()) return impl_->observe(experience);
    validateCognitiveCue(cue);
    return impl_->observe(experience, &cue);
}

ExplorerResult RobotMind::observeExplorer(
    const Experience& experience,
    const ScannerFrame& scannerFrame) {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->observeExplorer(experience, scannerFrame);
}

ExplorerResult RobotMind::observeExplorerWithCognitiveCue(
    const Experience& experience,
    const ScannerFrame& scannerFrame,
    const CognitiveCue& cue) {
    const std::scoped_lock lock(impl_->mutex);
    if (cue.neutral()) return impl_->observeExplorer(experience, scannerFrame);
    validateCognitiveCue(cue);
    return impl_->observeExplorer(experience, scannerFrame, &cue);
}

CartographyUpdate RobotMind::integrateScan(const ScannerFrame& scannerFrame) {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->integrateScan(scannerFrame);
}

IdentityDecision RobotMind::observeIdentity(const IdentityObservation& observation) {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->observeIdentity(observation);
}

void RobotMind::confirmLastIdentity(bool correct) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->identity.confirmLast(correct);
}

Prediction RobotMind::predict() const {
    const std::scoped_lock lock(impl_->mutex);
    std::optional<ActionId> action;
    if (impl_->activeAction.has_value()) action = impl_->activeAction->id;
    std::optional<EntityId> entity;
    if (impl_->currentEntity.has_value()) entity = impl_->currentEntity->id;
    return impl_->predictionFor(impl_->currentAssembly, action, entity);
}

std::vector<Prediction> RobotMind::predictHorizon(std::size_t depth) const {
    const std::scoped_lock lock(impl_->mutex);
    std::vector<Prediction> result;
    result.reserve(depth);
    AssemblyId source = impl_->currentAssembly;
    for (std::size_t i = 0; i < depth; ++i) {
        auto next = impl_->predictionFor(source);
        if (!next.available) break;
        source = next.expectedAssemblyId;
        result.push_back(std::move(next));
    }
    return result;
}

CognitiveContext RobotMind::context() const {
    const std::scoped_lock lock(impl_->mutex);
    CognitiveContext result;
    result.currentAssemblyId = impl_->currentAssembly;
    result.currentEntity = impl_->currentEntity;
    std::optional<ActionId> action;
    if (impl_->activeAction.has_value()) action = impl_->activeAction->id;
    std::optional<EntityId> entity;
    if (impl_->currentEntity.has_value()) entity = impl_->currentEntity->id;
    result.prediction = impl_->predictionFor(impl_->currentAssembly, action, entity);
    result.novelty = impl_->currentNovelty;
    result.predictionError = impl_->lastPredictionError;
    result.metrics = impl_->publicMetrics();
    result.biology = impl_->biologicalTelemetry();
    result.physiology = impl_->physiologyTelemetry();
    result.prospection = impl_->prospectiveTelemetry();
    result.motor = impl_->motorTelemetry();
    return result;
}

RuntimeMetrics RobotMind::metrics() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->publicMetrics();
}

BiologicalTelemetry RobotMind::biology() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->biologicalTelemetry();
}

PhysiologyTelemetry RobotMind::physiology() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->physiologyTelemetry();
}

ProspectiveTelemetry RobotMind::prospection() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->prospectiveTelemetry();
}

MotorTelemetry RobotMind::motor() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->motorTelemetry();
}

ThroughputCounters RobotMind::throughput() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->throughputTelemetry();
}

RobotMindConfig RobotMind::config() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->config;
}

void RobotMind::resetThroughputCounters() {
    const std::scoped_lock lock(impl_->mutex);
    impl_->core->resetThroughputCounters();
    impl_->sdkThroughput.clear();
}

CartographySummary RobotMind::cartographySummary(EnvironmentId environmentId) const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->cartographer.summary(environmentId);
}

std::optional<MapVoxel> RobotMind::mapVoxelAt(
    EnvironmentId environmentId,
    const std::array<double, 3>& worldPositionMeters) const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->cartographer.voxelAt(environmentId, worldPositionMeters);
}

std::vector<MapVoxel> RobotMind::environmentVoxels(EnvironmentId environmentId) const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->cartographer.voxels(environmentId);
}

std::string RobotMind::environmentMapJson(EnvironmentId environmentId) const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->cartographer.json(environmentId);
}

void RobotMind::clearEnvironmentMap(EnvironmentId environmentId) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->cartographer.clear(environmentId);
}

void RobotMind::beginAction(const ActionEvent& action) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->activeAction = action;
    if (action.id >= 1 && action.id <= 4) {
        impl_->core->beginEmbodiedAction(static_cast<std::uint32_t>(action.id - 1));
    }
}

void RobotMind::endAction(const ActionOutcome& outcome) {
    const std::scoped_lock lock(impl_->mutex);
    if (outcome.id >= 1 && outcome.id <= 4) {
        impl_->core->endEmbodiedAction(
            clampUnit(outcome.reward),
            clamp01(outcome.success),
            clamp01(outcome.novelty));
    }
    tatarus::neuro::SensorFrame consequence;
    consequence.reward = clampUnit(outcome.reward);
    consequence.novelty = clamp01(outcome.novelty);
    for (int i = 0; i < 16; ++i) {
        impl_->lastMotorAction = impl_->core->step(consequence);
    }
    impl_->activeAction.reset();
}

void RobotMind::endEpisode(bool reachedGoal) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->core->endEmbodiedEpisode(reachedGoal);
}

void RobotMind::setLearningEnabled(bool enabled) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->core->setLearningEnabled(enabled);
}

bool RobotMind::learningEnabled() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->core->learningEnabled();
}

void RobotMind::rest(std::uint64_t ticks) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->rest(ticks);
}

void RobotMind::setExperimentalIntervention(
    const ExperimentalIntervention& intervention) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->core->setExperimentalIntervention(tatarus::neuro::ExperimentalIntervention{
        .astrocyteFunction = intervention.astrocyteFunction,
        .oxygenSupply = intervention.oxygenSupply,
        .glucoseSupply = intervention.glucoseSupply,
        .pumpEfficiency = intervention.pumpEfficiency,
        .myelinIntegrity = intervention.myelinIntegrity,
        .microgliaFunction = intervention.microgliaFunction,
        .neuromodulatorGain = intervention.neuromodulatorGain,
        .sleepEnabled = intervention.sleepEnabled,
    });
}

void RobotMind::applyExperimentalDamage(
    double neuronFraction,
    double synapseFraction,
    std::uint64_t seed) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->core->applyDamage(
        std::clamp(neuronFraction, 0.0, 1.0),
        std::clamp(synapseFraction, 0.0, 1.0),
        seed ? seed : impl_->config.seed);
}

void RobotMind::saveSnapshot(const std::filesystem::path& directory) const {
    const std::scoped_lock lock(impl_->mutex);
    impl_->saveSnapshot(directory);
}

bool RobotMind::loadSnapshot(const std::filesystem::path& directory) {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->loadSnapshot(directory);
}

std::string RobotMind::stateJson() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->stateJson();
}

std::string RobotMind::spatialJson() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->spatialJson();
}

std::string RobotMind::liveJson() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->liveJson();
}

std::string RobotMind::physiologyJson() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->physiologyJson();
}

} // namespace tatarus
