#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tatarus {

using AssemblyId = std::uint64_t;
using EntityId = std::uint64_t;
using ActionId = std::uint64_t;
using TimestampNs = std::uint64_t;

struct ImuState {
    std::array<double, 3> acceleration{};
    std::array<double, 3> rotation{};
};

struct EnvironmentState {
    double light = 0.0;
    double soundLevel = 0.0;
    double proximity = 0.0;
    double temperature = 0.0;
    double novelty = 0.0;
};

struct JointState {
    std::uint32_t id = 0;
    double position = 0.0;
    double velocity = 0.0;
    double torque = 0.0;
    double temperature = 0.0;
};

struct BodyState {
    std::vector<JointState> joints;
    double battery = 1.0;
    double powerDraw = 0.0;
    double balance = 0.0;
    double contactLeft = 0.0;
    double contactRight = 0.0;
};

struct ActionEvent {
    ActionId id = 0;
    std::string label;
    double intensity = 0.0;
    TimestampNs startedNs = 0;
    TimestampNs endedNs = 0;
};

struct ActionOutcome {
    ActionId id = 0;
    double reward = 0.0;
    double success = 0.0;
    double novelty = 0.0;
};

struct EntityContext {
    EntityId id = 0;
    double confidence = 0.0;
    double familiarity = 0.0;
    bool known = false;
    bool newlyConsolidated = false;
};

struct VisualFrame {
    std::size_t width = 0;
    std::size_t height = 0;
    // Interleaved linearized RGB values in [0, 1]. The RobotMind routes this
    // raw camera frame through its biological retina and optic nerve.
    std::vector<double> rgb;
};

struct Experience {
    TimestampNs timestampNs = 0;
    std::vector<double> vision;
    std::optional<VisualFrame> visualFrame;
    std::vector<double> audio;
    std::vector<double> touch;
    // Distributed allocentric/grid-cell-like cue.  This is a sensory code,
    // not a world map: the nervous substrate turns it into persistent place
    // engrams and action associations.
    std::vector<double> spatialContext;
    // Optional opaque context used only to scope episodic spatial memory. It
    // is deliberately not injected as a sensory feature, so the shared neural
    // representation can still generalize across environments.
    std::uint64_t spatialEpisodeContext = 0;
    std::vector<std::uint8_t> text;
    std::vector<double> context;
    ImuState imu;
    EnvironmentState environment;
    BodyState body;
    std::optional<EntityContext> entity;
    std::optional<ActionEvent> action;
    double reward = 0.0;
};

struct AlternativePrediction {
    AssemblyId assemblyId = 0;
    double probability = 0.0;
    std::uint64_t observations = 0;
};

// Product-level prediction. Entity/action-conditioned experience memory is used
// when available; otherwise the intrinsic neural prospective state is used as
// the live one-step prediction for the current assembly.
struct Prediction {
    bool available = false;
    AssemblyId sourceAssemblyId = 0;
    AssemblyId expectedAssemblyId = 0;
    double confidence = 0.0;
    double familiarity = 0.0;
    std::vector<AlternativePrediction> alternatives;
};

struct IdentityObservation {
    std::array<double, 40> features{};
    int cameraId = 0;
    double timestampSeconds = 0.0;
    double quality = 1.0;
};

enum class IdentityDecisionKind : std::uint8_t {
    NewIdentity,
    MatchedIdentity,
    ProvisionalAssociation,
    ProvisionalIdentity,
    Uncertain
};

struct IdentityDecision {
    IdentityDecisionKind kind = IdentityDecisionKind::Uncertain;
    EntityId entityId = 0;
    AssemblyId activeAssemblyId = 0;
    double confidence = 0.0;
    int hypothesisEvidence = 0;
    int evidenceRequired = 0;
    bool baselineAccepted = false;
};

// Direct mapping of the TATARUS Neurobiology substrate telemetry.
struct BiologicalTelemetry {
    bool available = false;

    std::uint64_t dendriticSegments = 0;
    std::uint64_t astrocytes = 0;
    std::uint64_t capillaries = 0;
    std::uint64_t vessels = 0; // compatibility alias for capillaries
    std::uint64_t oligodendrocytes = 0;
    std::uint64_t microglia = 0;

    std::uint64_t dendriticSpikes = 0;
    std::uint64_t myelinRemodelingUpdates = 0;
    std::uint64_t microglialSurveillanceUpdates = 0;
    std::uint64_t microglialPruningEvents = 0;
    std::uint64_t microglialRepairEvents = 0;
    std::uint64_t microglialDamageSignals = 0;

    double meanDendriticCalcium = 0.0;
    double meanAstrocyteCalcium = 0.0;
    double oxygen = 1.0;
    double glucose = 1.0;
    double flow = 1.0;
    double meanAxonLengthUm = 0.0;
    double meanLocalEnergy = 1.0;

    double myelinCoverage = 0.0;
    double conductionVelocityMps = 0.0;
    double effectiveDelayMs = 0.0;
    double oligodendrocyteReserve = 1.0;

    double microgliaActivation = 0.0;
    double complementTag = 0.0;
    double repairCapacity = 1.0;
    double inflammatoryTone = 0.0;

    // Effective coarse-grained tissue growth and mechanics.
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

    // Stable public aliases for host integrations.
    std::uint64_t pruneEvents = 0;
    std::uint64_t repairEvents = 0;
};

// Intrinsic TATARUS prospective episodic cognition. This is separate from the
// SDK's entity/action-conditioned transition layer because the intrinsic layer
// also primes dendrites inside the nervous substrate.
struct ProspectiveTelemetry {
    bool available = false;
    std::uint64_t learnedTransitions = 0;
    std::uint64_t observedTransitions = 0;
    std::uint64_t predictionHits = 0;
    std::uint64_t predictionMisses = 0;
    AssemblyId predictedAssemblyId = 0;
    AssemblyId lastAssemblyId = 0;
    double predictionConfidence = 0.0;
    double expectedDelayMs = 0.0;
    double predictionError = 0.0;
    double temporalSurprise = 0.0;
    double sequenceFamiliarity = 0.0;
    double prospectiveActivation = 0.0;
};

enum class SleepPhase : std::uint8_t {
    Wake,
    Nrem,
    Rem
};

// SDK 3.0 tissue physiology. Concentrations use millimolar (mM); normalized
// molecular states are unitless bounded activities unless stated otherwise.
struct PhysiologyTelemetry {
    bool available = false;
    bool finite = true;
    SleepPhase sleepPhase = SleepPhase::Wake;
    std::uint64_t parvalbuminNeurons = 0;
    std::uint64_t somatostatinNeurons = 0;
    std::uint64_t vipNeurons = 0;
    std::uint64_t taggedSynapses = 0;
    std::uint64_t longTermPotentiationEvents = 0;
    std::uint64_t longTermDepressionEvents = 0;
    std::uint64_t sleepTransitions = 0;
    double extracellularNaMm = 145.0;
    double extracellularKMm = 3.5;
    double extracellularCaMm = 1.2;
    double extracellularClMm = 130.0;
    double atp = 1.0;
    double pumpActivity = 0.0;
    double cumulativeAtpConsumed = 0.0;
    double heatProductionPj = 0.0;
    double entropyProductionPjPerK = 0.0;
    double dopamine = 0.0;
    double serotonin = 0.0;
    double noradrenaline = 0.0;
    double acetylcholine = 0.0;
    double camp = 0.5;
    double ip3 = 0.2;
    double hcnDensity = 1.0;
    double kvDensity = 1.0;
    double crebActivation = 0.0;
    double proteinPool = 0.0;
    double synapticTag = 0.0;
    double circadianPhaseHours = 8.0;
    double sleepPressure = 0.0;
    double extracellularVolumeFraction = 0.20;
    double glymphaticClearance = 0.10;
    double tissueWaste = 0.0;
    double gammaPower = 0.0;
    double thetaPower = 0.0;
};

// Read-only motor population output decoded from the same persistent nervous
// substrate that receives the robot's sensors and reward.
struct MotorTelemetry {
    bool available = false;
    std::array<double, 4> directionalActivity{}; // north, east, south, west
    std::uint32_t selectedDirection = 0;
    double movement = 0.0;
    double attention = 0.0;
    double vocalization = 0.0;
    double confidence = 0.0;
};

struct RuntimeMetrics {
    std::uint64_t experiences = 0;

    // Product-level, context-conditioned prediction scoring.
    std::uint64_t predictions = 0;
    std::uint64_t predictionHits = 0;
    std::uint64_t predictionMisses = 0;
    double cumulativeBrier = 0.0;
    double meanPredictionError = 0.0;

    // Intrinsic neural temporal memory.
    std::uint64_t learnedTransitions = 0;
    std::uint64_t observedTransitions = 0;
    std::uint64_t neuralPredictionHits = 0;
    std::uint64_t neuralPredictionMisses = 0;
    double neuralPredictionError = 0.0;
    double sequenceFamiliarity = 0.0;

    // Product-specific conditioned memory dimensions.
    std::uint64_t contextualTransitions = 0;
    std::uint64_t actionConditionedTransitions = 0;
    std::uint64_t entityConditionedTransitions = 0;

    std::uint64_t identities = 0;
    std::uint64_t activeSynapses = 0;
    std::uint64_t totalSpikes = 0;
    double meanEnergy = 1.0;
    bool finite = true;
};

struct CognitiveContext {
    AssemblyId currentAssemblyId = 0;
    std::optional<EntityContext> currentEntity;
    Prediction prediction;
    double novelty = 0.0;
    double predictionError = 0.0;
    RuntimeMetrics metrics;
    BiologicalTelemetry biology;
    PhysiologyTelemetry physiology;
    ProspectiveTelemetry prospection;
    MotorTelemetry motor;
};

struct ObserveResult {
    AssemblyId assemblyId = 0;
    Prediction prediction;
    double predictionError = 0.0;
    double novelty = 0.0;
    RuntimeMetrics metrics;
    BiologicalTelemetry biology;
    PhysiologyTelemetry physiology;
    ProspectiveTelemetry prospection;
    MotorTelemetry motor;
};

} // namespace tatarus
