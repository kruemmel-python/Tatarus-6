#pragma once

#include "tatarus/cognitive_cue.hpp"
#include "tatarus/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tatarus::cortex {

enum class CortexMode : std::uint8_t {
    Disabled,
    ObserveOnly,
    Advisor,
    Imagination,
    Hybrid,
};

enum class CortexExecutionMode : std::uint8_t {
    Off,
    Live,
    Record,
    Replay,
};

enum class CortexTaskKind : std::uint8_t {
    ObserveState,
    Plan,
    Imagine,
    HybridPlan,
    Dream,
    ExecutivePlan,
};

enum class CortexTriggerReason : std::uint8_t {
    None,
    Explicit,
    Novelty,
    PredictionError,
    LowMotorConfidence,
    CombinedUncertainty,
    SleepRem,
    GoalChanged,
    MetacognitiveFallback,
};

enum class CortexStrategyKind : std::uint8_t {
    Observe,
    WaitAndObserve,
    RequestScan,
    RecallRoute,
    FollowKnownRoute,
    ExploreFrontier,
    SearchAlternative,
    UseImagination,
    VisualRecall,
    SymbolRecall,
    Compose,
    FreeImagination,
    Unknown,
};

struct CortexRepresentation {
    AssemblyId id = 0;
    double activation = 0.0;
    double familiarity = 0.0;
};

struct CortexNeuralState {
    AssemblyId assemblyId = 0;
    AssemblyId predictedAssemblyId = 0;
    double predictionConfidence = 0.0;
    double predictionError = 0.0;
    double novelty = 0.0;
    double sequenceFamiliarity = 0.0;
    double motorConfidence = 0.0;
    double meanEnergy = 1.0;
    double brainAtp = 1.0;
    double brainOxygen = 1.0;
    double brainGlucose = 1.0;
    std::vector<CortexRepresentation> activeRepresentations;
};

struct CortexPhysiologyState {
    bool available = false;
    double atp = 1.0;
    double heartRateBpm = 0.0;
    double mapMmHg = 0.0;
    double cardiacOutputLPerMin = 0.0;
    double oxygenSaturation = 1.0;
    double respirationRateBpm = 0.0;
    double gfrMlPerMin = 0.0;
    double visceralDistress = 0.0;
    double sympatheticTone = 0.0;
    double brainOxygen = 1.0;
    double brainGlucose = 1.0;
    SleepPhase sleepPhase = SleepPhase::Wake;
};

struct CortexSpatialState {
    std::uint64_t environmentId = 0;
    bool mapAvailable = false;
    double localNovelty = 0.0;
    double frontierRatio = 0.0;
    std::array<double, 6> obstacleProximity{};
    double targetDistance = 0.0;
    double targetBearing = 0.0;
    bool knownRouteAvailable = false;
    double knownRouteConfidence = 0.0;
    bool loopDetected = false;
    std::uint32_t repeatedPlaceCount = 0;
};

struct CortexImaginationState {
    bool available = false;
    std::uint8_t stage = 0;
    std::size_t visualEngrams = 0;
    std::size_t symbolEngrams = 0;
    double lastSimilarity = 0.0;
    double lastNovelty = 0.0;
    std::vector<std::string> knownConcepts;
    std::vector<std::string> knownSymbols;
    std::vector<std::string> knownCategories;
};

struct CortexRecentOutcome {
    ActionId actionId = 0;
    double reward = 0.0;
    double success = 0.0;
    double novelty = 0.0;
};


enum class CortexGoalStatus : std::uint8_t {
    Pending,
    Active,
    Completed,
    Failed,
    Cancelled,
};

struct CortexGoal {
    std::uint64_t id = 0;
    std::string text;
    double priority = 0.5;
    CortexGoalStatus status = CortexGoalStatus::Pending;
    std::uint64_t createdStep = 0;
    std::uint64_t updatedStep = 0;
};

struct CortexWorkingMemoryItem {
    std::string key;
    std::string value;
    double salience = 0.0;
    std::uint64_t createdStep = 0;
    std::uint64_t expiresStep = 0;
    std::uint64_t sourceFingerprint = 0;
};

enum class CortexPlanStepStatus : std::uint8_t {
    Pending,
    Active,
    Completed,
    Failed,
    Skipped,
};

struct CortexPlanStep {
    std::string id;
    CortexStrategyKind kind = CortexStrategyKind::Unknown;
    std::string objective;
    std::vector<std::string> requiredCapabilities;
    CortexPlanStepStatus status = CortexPlanStepStatus::Pending;
};

struct CortexPlanProposal {
    std::string id;
    std::vector<CortexPlanStep> steps;
};

struct CortexExecutiveState {
    bool available = false;
    std::uint64_t activeGoalId = 0;
    std::string activeGoal;
    double activeGoalPriority = 0.0;
    std::size_t goalCount = 0;
    std::string activePlanId;
    std::size_t activePlanStep = 0;
    std::vector<CortexPlanStep> planSteps;
    std::vector<CortexWorkingMemoryItem> workingMemory;
};

struct CortexRequest {
    std::uint64_t requestId = 0;
    std::uint64_t organismStep = 0;
    std::uint64_t stateFingerprint = 0;
    CortexTaskKind task = CortexTaskKind::ObserveState;
    CortexTriggerReason trigger = CortexTriggerReason::None;
    std::string goal;
    CortexNeuralState neural;
    CortexPhysiologyState physiology;
    CortexSpatialState spatial;
    CortexImaginationState imagination;
    CortexExecutiveState executive;
    std::vector<CortexRecentOutcome> recentOutcomes;
    std::vector<std::string> availableCapabilities;
};

struct CortexStrategy {
    std::string id;
    CortexStrategyKind kind = CortexStrategyKind::Unknown;
    std::string rationale;
    double estimatedRisk = 0.0;
    double estimatedBenefit = 0.0;
    double confidence = 0.0;
    std::vector<std::string> requiredCapabilities;
};

struct CortexInformationRequest {
    std::string capability;
    std::string reason;
};

struct ImaginationDirective {
    std::string mode;
    std::vector<std::string> symbols;
    std::string conceptText;
};

struct CortexResponse {
    std::uint64_t requestId = 0;
    std::uint64_t sourceFingerprint = 0;
    std::vector<CortexStrategy> strategies;
    std::vector<CortexInformationRequest> informationRequests;
    std::optional<ImaginationDirective> imagination;
    std::optional<CortexPlanProposal> plan;
    std::string summary;
};

enum class CortexDecisionKind : std::uint8_t {
    Reject,
    Accept,
    Defer,
    RequestMoreInformation,
    SendToImagination,
};

struct CortexStrategyEvaluation {
    std::string strategyId;
    CortexStrategyKind kind = CortexStrategyKind::Unknown;
    bool eligible = false;
    double score = 0.0;
    std::string reason;
};

struct CortexDecision {
    CortexDecisionKind kind = CortexDecisionKind::Reject;
    std::optional<CortexStrategy> selectedStrategy;
    double score = 0.0;
    std::string reason;
    std::vector<CortexStrategyEvaluation> evaluations;
    std::vector<CortexInformationRequest> informationRequests;
};

struct CortexCompletedRequest {
    bool success = false;
    CortexRequest request;
    std::optional<CortexResponse> response;
    std::string error;
};

struct CortexRuntimeMetrics {
    std::uint64_t requestsSubmitted = 0;
    std::uint64_t responsesReceived = 0;
    std::uint64_t transportErrors = 0;
    std::uint64_t validationErrors = 0;
    std::uint64_t staleResponses = 0;
    std::uint64_t queueDrops = 0;
    std::uint64_t triggerEvaluations = 0;
    std::uint64_t arbiterEvaluations = 0;
    std::uint64_t arbiterAccepted = 0;
    std::uint64_t arbiterRejected = 0;
    std::uint64_t arbiterDeferred = 0;
    std::uint64_t arbiterInformationRequests = 0;
    std::uint64_t imaginationDirectivesPrepared = 0;
    std::uint64_t hybridRequests = 0;
    std::uint64_t consultationsSuppressed = 0;
    std::uint64_t topDownCuesPrepared = 0;
    std::uint64_t topDownCuesApplied = 0;
    std::uint64_t topDownCuesRejected = 0;
    std::uint64_t nremCycles = 0;
    std::uint64_t remDreamRequests = 0;
    std::uint64_t remDreamResponses = 0;
    std::uint64_t sleepBlockedRequests = 0;
    std::uint64_t executiveCycles = 0;
    std::uint64_t goalsCreated = 0;
    std::uint64_t plansAdopted = 0;
    std::uint64_t planStepsCompleted = 0;
    std::uint64_t metacognitiveSuppressions = 0;
    std::uint64_t recordEntries = 0;
    std::uint64_t replayHits = 0;
};

struct CortexObservation {
    CortexRuntimeMetrics metrics;
    CortexTriggerReason lastTrigger = CortexTriggerReason::None;
    std::optional<CortexRequest> lastRequest;
    std::optional<CortexRequest> lastSuppressedRequest;
    std::optional<CortexResponse> lastResponse;
    std::optional<CortexDecision> lastDecision;
    std::optional<CognitiveCue> lastCognitiveCue;
    SleepPhase lastSleepPhase = SleepPhase::Wake;
    std::string lastError;
};

[[nodiscard]] const char* toString(CortexMode value) noexcept;
[[nodiscard]] const char* toString(CortexTaskKind value) noexcept;
[[nodiscard]] const char* toString(CortexTriggerReason value) noexcept;
[[nodiscard]] const char* toString(CortexStrategyKind value) noexcept;
[[nodiscard]] const char* toString(CortexDecisionKind value) noexcept;
[[nodiscard]] const char* toString(CortexGoalStatus value) noexcept;
[[nodiscard]] const char* toString(CortexPlanStepStatus value) noexcept;
[[nodiscard]] CortexStrategyKind cortexStrategyKindFromString(std::string_view value) noexcept;

} // namespace tatarus::cortex
