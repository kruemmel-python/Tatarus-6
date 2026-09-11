#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "tatarus_prospection.hpp"
#include "tatarus/throughput.hpp"

namespace tatarus::neuro {

namespace biology { class SpatialSubstrate; }
namespace temporal { class ProspectiveMemory; }
namespace physiology { class PhysiologicalSubstrate; }

enum class PopulationRole : std::uint8_t {
    Sensory,
    Excitatory,
    Inhibitory,
    Context,
    Motor,
    Modulatory
};

enum class NeuronSubtype : std::uint8_t {
    Sensory,
    Pyramidal,
    Parvalbumin,
    Somatostatin,
    Vip,
    Neuromodulatory
};

enum class BrainHemisphere : std::uint8_t {
    Left,
    Right
};

enum class BrainRegion : std::uint8_t {
    Sensory,
    Association,
    Memory,
    Prospection,
    Motor,
    Homeostatic
};

enum class CorticalLayer : std::uint8_t {
    Layer23,
    Layer4,
    Layer5,
    Layer6,
    Subcortical
};

enum class SleepStage : std::uint8_t {
    Wake,
    Nrem,
    Rem
};

enum class ReceptorType : std::uint8_t {
    Ampa,
    Nmda,
    GabaA,
    GabaB,
    Modulatory
};

struct NervousSystemConfig {
    int sensoryNeurons = 16;
    int excitatoryNeurons = 40;
    int inhibitoryNeurons = 12;
    int contextNeurons = 16;
    int motorNeurons = 8;
    int modulatoryNeurons = 4;
    std::uint64_t seed = 7001;
    double dtMs = 1.0;
    double connectionProbability = 0.07;
    double restingMv = -65.0;
    double resetMv = -70.0;
    double thresholdMv = -50.0;
    double tauSomaMs = 20.0;
    double tauDendriteMs = 35.0;
    double somaDendriteCoupling = 0.22;
    double baseCurrent = 12.5;
    double motorRateScaleHz = 20.0;
    double tauAmpaMs = 5.0;
    double tauNmdaMs = 80.0;
    double tauGabaAMs = 10.0;
    double tauGabaBMs = 120.0;
    double ampaReversalMv = 0.0;
    double nmdaReversalMv = 0.0;
    double gabaAReversalMv = -75.0;
    double gabaBReversalMv = -95.0;
    double refractoryMs = 2.0;
    double adaptationIncrementMv = 1.2;
    double adaptationTauMs = 100.0;
    double targetRateHz = 8.0;
    double homeostasisTauMs = 2000.0;
    double homeostasisGain = 0.003;
    double eligibilityTauMs = 400.0;
    double eligibilityTransmissionGain = 1.0;
    double eligibilityIncrement = 8.0;
    double learningRate = 0.0008;
    double consolidationRate = 0.00002;
    double dopamineTauMs = 250.0;
    double acetylcholineTauMs = 180.0;
    double resourceRecoveryTauMs = 180.0;
    double facilitationTauMs = 120.0;
    double releaseProbability = 0.18;
    double energyRecoveryPerMs = 0.0015;
    double spikeEnergyCost = 0.025;
    double transmissionEnergyCost = 0.0004;
    double structuralIntervalMs = 500.0;
    double pruneUsageThreshold = 0.0001;
    double pruneWeightThreshold = 0.004;
    int maximumNewSynapsesPerInterval = 8;
    int maximumAssemblies = 64;
    double assemblySimilarityThreshold = 0.68;
    bool generatedOperatorEnabled = true;
    bool eligibilityMemoryEnabled = true;
    bool shortTermPlasticityEnabled = true;
    bool longTermPlasticityEnabled = true;
    bool homeostasisEnabled = true;
    bool structuralPlasticityEnabled = true;
    bool energyRegulationEnabled = true;
    bool physiologyEnabled = true;
    double physiologyTimeScale = 1.0;
    double circadianCycleMs = 86'400'000.0;
    double sleepPressureTauMs = 57'600'000.0;
    double nremMinimumMs = 4'800'000.0;
    double remMinimumMs = 900'000.0;

    [[nodiscard]] int neuronCount() const;
    void validate() const;
};

struct SensorFrame {
    std::vector<double> visionEvents;
    std::vector<double> audioSamples;
    std::vector<double> touch;
    // Dedicated inner-ear afferents: semicircular canals, otoliths, tilt and balance.
    std::vector<double> vestibularEvents;
    std::vector<double> spatialContext;
    std::uint64_t spatialEpisodeContext = 0;
    std::vector<std::uint8_t> textBytes;
    // Restricted top-down channel used by CognitiveBridge. It targets the
    // context population and never addresses individual neurons or synapses.
    std::vector<double> contextEvents;
    double temperature = 0.0;
    double internalEnergy = 1.0;
    double reward = 0.0;
    double novelty = 0.0;
};

struct MotorAction {
    double movement = 0.0;
    double attention = 0.0;
    double vocalization = 0.0;
    double confidence = 0.0;
    std::array<double, 4> directionalActivity{};
    std::uint32_t selectedDirection = 0;
};

// Hippocampal/striatal-style spatial action memory.  The nervous system does
// not store world coordinates or an external occupancy map.  Instead it binds
// the currently reactivated neural assembly to the embodied action that was
// taken and to the subsequently observed reward/novelty/success signal.
//
// This makes "I have been in this neural place-state before" a persistent part
// of the nervous substrate itself.  Unknown exits are preferred during
// exploration, novelty is propagated backwards along a short eligibility
// trace, and a successful route receives stronger long-range consolidation.
struct SpatialMemoryMetrics {
    bool available = false;
    bool learningEnabled = true;
    std::uint64_t engrams = 0;
    std::uint64_t actionUpdates = 0;
    std::uint64_t novelDiscoveries = 0;
    std::uint64_t revisits = 0;
    std::uint64_t successfulConsolidations = 0;
    std::uint64_t failedConsolidations = 0;
    std::uint64_t currentPlaceKey = 0;
    std::uint64_t currentAssemblyId = 0;
    std::uint64_t currentVisits = 0;
    double currentFamiliarity = 0.0;
    std::array<double, 4> rewardValue{};
    std::array<double, 4> noveltyValue{};
    std::array<double, 4> frontierValue{};
    std::array<double, 4> successValue{};
    std::array<double, 4> routeValue{};
    std::array<double, 4> avoidanceValue{};
    std::array<double, 4> motorBias{};
    std::array<std::uint64_t, 4> actionObservations{};
    std::array<std::uint64_t, 4> failedEpisodeObservations{};
    std::uint32_t preferredDirection = 0;
    double confidence = 0.0;
};

struct BiologicalMetrics {
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
    // Coarse-grained tissue mechanics. A simulated neuron/synapse represents
    // a tissue unit; volumes are therefore effective, not one-cell histology.
    std::uint64_t baselineActiveSynapses = 0;
    std::uint64_t mechanicsActiveSynapses = 0;
    std::uint64_t growthLimitedEvents = 0;
    double baselineTissueVolumeUm3 = 0.0;
    double tissueVolumeUm3 = 0.0;
    double tissueVolumeRatio = 1.0;
    double linearExpansion = 1.0;
    double effectiveTissueMassNg = 0.0;
    double netBiomassChangeNg = 0.0;
    double synapticMaterialVolumeUm3 = 0.0;
    double solidPackingFraction = 0.80;
    double extracellularSpaceFraction = 0.20;
    double tissuePressureKPa = 0.0;
    double materialReserve = 1.0;
    double cumulativeMaterialSynthesizedUm3 = 0.0;
    double cumulativeMaterialRecycledUm3 = 0.0;
};

struct NervousSystemMetrics {
    std::uint64_t step = 0;
    std::uint64_t totalSpikes = 0;
    std::uint64_t totalTransmissions = 0;
    int activeSynapses = 0;
    int assemblyCount = 0;
    int activeAssembly = -1;
    double meanRateHz = 0.0;
    double meanEnergy = 1.0;
    double dopamine = 0.0;
    double acetylcholine = 0.0;
    double meanEligibility = 0.0;
    double meanResource = 1.0;
    double structuralGrowth = 0.0;
    double structuralPruning = 0.0;
    bool finite = true;
};

struct NeuronStateView {
    PopulationRole role = PopulationRole::Excitatory;
    NeuronSubtype subtype = NeuronSubtype::Pyramidal;
    double somaMv = 0.0;
    double dendriteMv = 0.0;
    double filteredRateHz = 0.0;
    double fastRateHz = 0.0;
    double energy = 0.0;
    std::uint64_t spikeCount = 0;
    bool active = false;
};

struct SynapseStateView {
    std::size_t index = 0;
    std::int64_t parentSynapse = -1;
    std::uint32_t pre = 0;
    std::uint32_t post = 0;
    ReceptorType receptor = ReceptorType::Ampa;
    double weight = 0.0;
    double consolidatedWeight = 0.0;
    double eligibility = 0.0;
    double resource = 0.0;
    double usage = 0.0;
    bool active = false;
};

struct AssemblyStateView {
    std::uint64_t id = 0;
    std::vector<double> prototype;
    double activation = 0.0;
    std::uint64_t observations = 0;
    std::uint64_t lastActiveStep = 0;
};

struct RepresentationState {
    std::uint64_t step = 0;
    int activeAssembly = -1;
    std::vector<NeuronStateView> neurons;
    std::vector<SynapseStateView> synapses;
    std::vector<AssemblyStateView> assemblies;
};

// Read-only TATARUS spatial instrumentation. These views intentionally copy
// substrate state so consumers can render or record it without gaining mutation
// access to the nervous system.
struct SpatialPointView {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SpatialNeuronView {
    std::size_t index = 0;
    PopulationRole role = PopulationRole::Excitatory;
    NeuronSubtype subtype = NeuronSubtype::Pyramidal;
    BrainHemisphere hemisphere = BrainHemisphere::Left;
    BrainRegion region = BrainRegion::Association;
    CorticalLayer layer = CorticalLayer::Layer23;
    SpatialPointView soma;
    std::uint32_t firstSegment = 0;
    std::uint16_t segmentCount = 0;
    double somaMv = -65.0;
    double dendriteMv = -65.0;
    double filteredRateHz = 0.0;
    double energy = 1.0;
    std::uint64_t spikeCount = 0;
    bool spiking = false;
    bool active = true;
};

struct SpatialDendriteView {
    std::size_t index = 0;
    std::uint32_t neuron = 0;
    std::int32_t parent = -1;
    SpatialPointView position;
    double membraneMv = -65.0;
    double calcium = 0.0;
    double localEnergy = 1.0;
    std::uint16_t astrocyte = 0;
    std::uint8_t branchOrder = 0;
    bool spiking = false;
};

struct SpatialAxonView {
    std::size_t index = 0;
    std::uint32_t pre = 0;
    std::uint32_t post = 0;
    std::uint32_t segment = 0;
    SpatialPointView start;
    SpatialPointView end;
    ReceptorType receptor = ReceptorType::Ampa;
    double lengthUm = 0.0;
    double weight = 0.0;
    double eligibility = 0.0;
    double resource = 1.0;
    double usage = 0.0;
    double myelinCoverage = 0.0;
    double axonCaliber = 1.0;
    double conductionVelocityUmPerMs = 150.0;
    double effectiveDelayMs = 0.0;
    double complementTag = 0.0;
    double damageSignal = 0.0;
    double repairSignal = 0.0;
    std::uint16_t oligodendrocyte = 0;
    std::uint16_t microglia = 0;
    bool active = true;
};

struct SpatialAxonSignalView {
    std::uint32_t synapse = 0;
    double progress = 0.0;
    double amplitude = 0.0;
};

struct SpatialAstrocyteView {
    std::size_t index = 0;
    SpatialPointView center;
    double calcium = 0.0;
    double glutamateLoad = 0.0;
    double potassiumLoad = 0.0;
    double metabolicDemand = 0.0;
    double support = 1.0;
    std::uint16_t vessel = 0;
};

struct SpatialCapillaryView {
    std::size_t index = 0;
    SpatialPointView position;
    double flow = 1.0;
    double oxygen = 1.0;
    double glucose = 1.0;
};

struct SpatialOligodendrocyteView {
    std::size_t index = 0;
    SpatialPointView center;
    double myelinReserve = 1.0;
    double metabolicLoad = 0.0;
    double remodelingSignal = 0.0;
    std::uint16_t vessel = 0;
};

struct SpatialMicrogliaView {
    std::size_t index = 0;
    SpatialPointView center;
    double activation = 0.0;
    double inflammatoryTone = 0.0;
    double phagocyticLoad = 0.0;
    double repairCapacity = 1.0;
    double surveillance = 0.0;
    double damageLoad = 0.0;
    std::uint16_t vessel = 0;
};

struct BiologicalSpatialState {
    std::uint64_t step = 0;
    double dtMs = 1.0;
    std::string tissueShape = "bilateral_brain_ellipsoid";
    SpatialPointView tissueCenter{};
    SpatialPointView tissueRadii{420.0, 330.0, 280.0};
    double interhemisphericFissureUm = 24.0;
    double baselineVolumeUm3 = 0.0;
    double currentVolumeUm3 = 0.0;
    double volumeRatio = 1.0;
    double linearExpansion = 1.0;
    double effectiveTissueMassNg = 0.0;
    double netBiomassChangeNg = 0.0;
    double solidPackingFraction = 0.80;
    double extracellularSpaceFraction = 0.20;
    double pressureKPa = 0.0;
    double materialReserve = 1.0;
    std::vector<SpatialNeuronView> neurons;
    std::vector<SpatialDendriteView> dendrites;
    std::vector<SpatialAxonView> axons;
    std::vector<SpatialAxonSignalView> signals;
    std::vector<SpatialAstrocyteView> astrocytes;
    std::vector<SpatialCapillaryView> capillaries;
    std::vector<SpatialOligodendrocyteView> oligodendrocytes;
    std::vector<SpatialMicrogliaView> microglia;
};

struct PhysiologyMetrics {
    bool available = false;
    bool finite = true;
    SleepStage sleepStage = SleepStage::Wake;
    int parvalbuminNeurons = 0;
    int somatostatinNeurons = 0;
    int vipNeurons = 0;
    int taggedSynapses = 0;
    std::uint64_t longTermPotentiationEvents = 0;
    std::uint64_t longTermDepressionEvents = 0;
    std::uint64_t sleepTransitions = 0;
    double meanExtracellularNaMm = 145.0;
    double meanExtracellularKMm = 3.5;
    double meanExtracellularCaMm = 1.2;
    double meanExtracellularClMm = 130.0;
    double meanAtp = 1.0;
    double meanPumpActivity = 0.0;
    double cumulativeAtpConsumed = 0.0;
    double heatProductionPj = 0.0;
    double entropyProductionPjPerK = 0.0;
    double dopamine = 0.0;
    double serotonin = 0.0;
    double noradrenaline = 0.0;
    double acetylcholine = 0.0;
    double meanCamp = 0.5;
    double meanIp3 = 0.2;
    double meanHcnDensity = 1.0;
    double meanKvDensity = 1.0;
    double meanCrebActivation = 0.0;
    double meanProteinPool = 0.0;
    double meanSynapticTag = 0.0;
    double circadianPhaseHours = 8.0;
    double sleepPressure = 0.0;
    double extracellularVolumeFraction = 0.20;
    double glymphaticClearance = 0.10;
    double tissueWaste = 0.0;
    double gammaPower = 0.0;
    double thetaPower = 0.0;
};

struct PhysiologyNeuronView {
    std::size_t index = 0;
    SpatialPointView position;
    NeuronSubtype subtype = NeuronSubtype::Pyramidal;
    double extracellularNaMm = 145.0;
    double extracellularKMm = 3.5;
    double extracellularCaMm = 1.2;
    double extracellularClMm = 130.0;
    double atp = 1.0;
    double pumpActivity = 0.0;
    double excitabilityShiftMv = 0.0;
    double dopamine = 0.0;
    double serotonin = 0.0;
    double noradrenaline = 0.0;
    double acetylcholine = 0.0;
    double camp = 0.5;
    double ip3 = 0.2;
    double hcnDensity = 1.0;
    double kvDensity = 1.0;
    double crebActivation = 0.0;
    double mrna = 0.0;
    double proteinPool = 0.0;
    double homer1a = 0.0;
    double waste = 0.0;
};

struct PhysiologySynapseView {
    std::size_t index = 0;
    double tag = 0.0;
    double polarity = 0.0;
    double capturedProtein = 0.0;
};

struct PhysiologicalState {
    PhysiologyMetrics metrics;
    std::vector<PhysiologyNeuronView> neurons;
    std::vector<PhysiologySynapseView> synapses;
};

struct DamageReport {
    std::vector<std::size_t> disabledNeurons;
    std::vector<std::size_t> disabledSynapses;
};

struct ExperimentalIntervention {
    double astrocyteFunction = 1.0;
    double oxygenSupply = 1.0;
    double glucoseSupply = 1.0;
    double pumpEfficiency = 1.0;
    double myelinIntegrity = 1.0;
    double microgliaFunction = 1.0;
    double neuromodulatorGain = 1.0;
    bool sleepEnabled = true;
};

struct MechanismDescriptor {
    std::string id;
    std::string formula;
    std::string placement;
    std::string parameterRange;
    std::string evidence;
    std::string status;
};

class MechanismLibrary {
public:
    MechanismLibrary();
    [[nodiscard]] const std::vector<MechanismDescriptor>& entries() const;
    void writeJson(const std::filesystem::path& path) const;

private:
    std::vector<MechanismDescriptor> entries_;
};

class PersistentNervousSystem {
public:
    explicit PersistentNervousSystem(NervousSystemConfig config);
    ~PersistentNervousSystem();

    PersistentNervousSystem(const PersistentNervousSystem&) = delete;
    PersistentNervousSystem& operator=(const PersistentNervousSystem&) = delete;

    MotorAction step(const SensorFrame& frame);
    std::vector<MotorAction> run(
        const std::vector<SensorFrame>& frames);

    [[nodiscard]] const NervousSystemConfig& config() const;
    [[nodiscard]] const NervousSystemMetrics& metrics() const;
    [[nodiscard]] const BiologicalMetrics& biologicalMetrics() const;
    [[nodiscard]] const PhysiologyMetrics& physiologyMetrics() const;
    [[nodiscard]] const ProspectiveMetrics& prospectiveMetrics() const;
    [[nodiscard]] SpatialMemoryMetrics spatialMemoryMetrics() const;
    [[nodiscard]] const tatarus::ThroughputCounters& throughputCounters() const noexcept { return throughput_; }
    void resetThroughputCounters() noexcept { throughput_.clear(); }
    [[nodiscard]] std::uint64_t stateHash() const;
    [[nodiscard]] bool dalePrincipleHolds() const;
    [[nodiscard]] RepresentationState inspect() const;
    [[nodiscard]] BiologicalSpatialState inspectSpatial() const;
    [[nodiscard]] PhysiologicalState inspectPhysiology() const;

    void saveSnapshot(const std::filesystem::path& path) const;
    void loadSnapshot(const std::filesystem::path& path);
    void writeStateJson(const std::filesystem::path& path) const;
    void applyDamage(
        double neuronFraction,
        double synapseFraction,
        std::uint64_t seed);
    DamageReport applyDamageWithReport(
        double neuronFraction,
        double synapseFraction,
        std::uint64_t seed);
    DamageReport disableSynapses(
        const std::vector<std::size_t>& synapseIndices);
    void setStructuralPlasticityEnabled(bool enabled);
    void setLearningEnabled(bool enabled);
    [[nodiscard]] bool learningEnabled() const noexcept { return learningEnabled_; }
    void setExperimentalIntervention(const ExperimentalIntervention& intervention);

    // Bind the currently reactivated assembly to an embodied action and feed
    // the causal outcome back into the same persistent nervous substrate.
    // Action indices are zero based (north/east/south/west = 0..3).
    void beginEmbodiedAction(std::uint32_t actionIndex);
    void endEmbodiedAction(double reward, double success, double novelty);
    // Consolidate a complete embodied episode. A failed episode strengthens
    // avoidance engrams for repeated/familiar place-action paths; a successful
    // episode stabilises the loop-erased route and weakens old failure marks.
    void endEmbodiedEpisode(bool reachedGoal);

private:
    struct Neuron;
    struct Synapse;
    struct AxonEvent;
    struct Assembly;
    struct SpatialActionEngram;
    struct SpatialEligibilityTrace;
    struct SpatialEpisodeStep;

    NervousSystemConfig config_;
    std::mt19937_64 random_;
    std::vector<Neuron> neurons_;
    std::vector<Synapse> synapses_;
    std::vector<std::vector<std::size_t>> outgoing_;
    std::vector<std::vector<AxonEvent>> axonQueue_;
    std::vector<Assembly> assemblies_;
    std::vector<std::uint8_t> spikes_;
    std::vector<double> preTrace_;
    std::vector<double> postTrace_;
    std::vector<double> assemblyAccumulator_;
    std::vector<double> assemblyBaseline_;
    NervousSystemMetrics metrics_;
    mutable tatarus::ThroughputCounters throughput_;
    std::unique_ptr<biology::SpatialSubstrate> biologicalSubstrate_;
    std::unique_ptr<physiology::PhysiologicalSubstrate> physiologicalSubstrate_;
    std::unique_ptr<temporal::ProspectiveMemory> prospectiveMemory_;
    double dopamine_ = 0.0;
    double acetylcholine_ = 0.0;
    std::uint64_t lastStructuralStep_ = 0;
    std::uint64_t stimulusSignature_ = 0;
    int stimulusAgeSteps_ = 0;
    bool stimulusWasPresent_ = false;
    std::vector<SpatialActionEngram> spatialActionMemory_;
    std::vector<SpatialEligibilityTrace> spatialEligibilityTrace_;
    std::vector<SpatialEpisodeStep> spatialEpisodeTrace_;
    std::uint64_t recognizedPlaceKey_ = 0;
    std::uint64_t recognizedAssemblyId_ = 0;
    std::uint64_t pendingSpatialPlaceKey_ = 0;
    std::uint32_t pendingSpatialAction_ = 0;
    bool pendingSpatialActionValid_ = false;
    std::uint64_t spatialActionUpdates_ = 0;
    std::uint64_t spatialNovelDiscoveries_ = 0;
    std::uint64_t spatialRevisits_ = 0;
    std::uint64_t spatialSuccessfulConsolidations_ = 0;
    std::uint64_t spatialFailedConsolidations_ = 0;
    std::uint64_t lastSpatialCueSignature_ = 0;
    bool spatialCueWasPresent_ = false;
    bool learningEnabled_ = true;

    void createNeurons();
    void createBiologicalSubstrate();
    void createPhysiologicalSubstrate();
    void synchronizeBiologicalSubstrate();
    void createSynapses();
    void rebuildOutgoing();
    void encodeSensors(
        const SensorFrame& frame,
        std::vector<double>& somaDrive,
        std::vector<double>& dendriteDrive) const;
    void deliverAxonEvents(const SensorFrame& frame);
    void updateNeurons(
        const std::vector<double>& somaDrive,
        const std::vector<double>& dendriteDrive);
    void scheduleSpikeEvents();
    void updatePlasticity(const SensorFrame& frame);
    void updateAssemblies(const SensorFrame& frame);
    void updateHomeostasisAndEnergy();
    void updateStructuralPlasticity();
    void updateMetrics();
    [[nodiscard]] MotorAction decodeAction() const;
    [[nodiscard]] SpatialActionEngram* spatialEngram(std::uint64_t placeKey);
    [[nodiscard]] const SpatialActionEngram* spatialEngram(std::uint64_t placeKey) const;
    SpatialActionEngram& ensureSpatialEngram(std::uint64_t placeKey);
    void updateSpatialCue(const SensorFrame& frame);
    void noteSpatialAssembly(std::uint64_t assemblyId);
    void decaySpatialEligibility();
    void reinforceSpatialTrace(double novelty, double success);
    [[nodiscard]] std::array<double, 4> spatialMotorBias(std::uint64_t placeKey) const;
    [[nodiscard]] PopulationRole roleForIndex(int index) const;
    [[nodiscard]] NeuronSubtype subtypeForIndex(int index) const;
    [[nodiscard]] bool connectionExists(int pre, int post) const;
};

class ContinuousEnvironment {
public:
    explicit ContinuousEnvironment(std::uint64_t seed = 9001);

    SensorFrame sense() const;
    double apply(const MotorAction& action);
    void injectTextUtf8(const std::string& text);
    [[nodiscard]] bool reachedTarget() const;
    [[nodiscard]] double position() const;
    [[nodiscard]] double target() const;
    [[nodiscard]] double cumulativeReward() const;

private:
    std::uint64_t seed_ = 0;
    double position_ = -0.75;
    double target_ = 0.65;
    double temperature_ = 0.0;
    double cumulativeReward_ = 0.0;
    double lastReward_ = 0.0;
    std::vector<std::uint8_t> textStream_;
    std::size_t textIndex_ = 0;
};

struct ClosedLoopResult {
    int steps = 0;
    double cumulativeReward = 0.0;
    double finalDistance = 0.0;
    double meanEnergy = 0.0;
    int assemblies = 0;
    int activeSynapses = 0;
    std::uint64_t stateHash = 0;
};

ClosedLoopResult runClosedLoop(
    PersistentNervousSystem& nervousSystem,
    ContinuousEnvironment& environment,
    int steps);

}  // namespace tatarus::neuro
