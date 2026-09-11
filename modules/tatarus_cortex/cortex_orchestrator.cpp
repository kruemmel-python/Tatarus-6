#include "tatarus/cortex.hpp"

#include "cortex_context.hpp"
#include "cortex_json.hpp"
#include "cortex_trigger.hpp"
#include "cortex_worker.hpp"
#include "tatarus/cortex_record_replay.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace tatarus::cortex {

namespace {

[[nodiscard]] std::uint64_t mixExecutiveHash(
    std::uint64_t hash,
    const CortexExecutiveState& executive) noexcept {
    const auto mix = [](std::uint64_t h, std::uint64_t value) {
        h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
        return h;
    };
    const auto mixText = [&](std::uint64_t h, const std::string& text) {
        for (const unsigned char c : text) h = mix(h, c);
        return mix(h, 0xffU);
    };
    hash = mix(hash, executive.activeGoalId);
    hash = mixText(hash, executive.activeGoal);
    hash = mixText(hash, executive.activePlanId);
    hash = mix(hash, executive.activePlanStep);
    for (const auto& item : executive.workingMemory) {
        hash = mixText(hash, item.key);
        hash = mixText(hash, item.value);
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] bool sameWorkingMemory(
    const std::vector<CortexWorkingMemoryItem>& left,
    const std::vector<CortexWorkingMemoryItem>& right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].key != right[index].key
            || left[index].value != right[index].value
            || left[index].createdStep != right[index].createdStep
            || left[index].expiresStep != right[index].expiresStep) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool samePlanPosition(
    const CortexExecutiveState& left,
    const CortexExecutiveState& right) noexcept {
    if (left.activePlanId != right.activePlanId
        || left.activePlanStep != right.activePlanStep
        || left.planSteps.size() != right.planSteps.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.planSteps.size(); ++index) {
        if (left.planSteps[index].id != right.planSteps[index].id
            || left.planSteps[index].status != right.planSteps[index].status) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool sameExecutiveContext(
    const CortexExecutiveState& left,
    const CortexExecutiveState& right) noexcept {
    return left.available == right.available
        && left.activeGoalId == right.activeGoalId
        && left.activeGoal == right.activeGoal
        && left.goalCount == right.goalCount
        && samePlanPosition(left, right)
        && sameWorkingMemory(left.workingMemory, right.workingMemory);
}

[[nodiscard]] bool sameGroundingContext(
    const CortexImaginationState& left,
    const CortexImaginationState& right) noexcept {
    return left.available == right.available
        && left.stage == right.stage
        && left.visualEngrams == right.visualEngrams
        && left.symbolEngrams == right.symbolEngrams
        && left.knownConcepts == right.knownConcepts
        && left.knownSymbols == right.knownSymbols
        && left.knownCategories == right.knownCategories;
}

} // namespace

class CortexOrchestrator::Impl {
public:
    Impl(CortexConfig value, std::shared_ptr<ICortexModelClient> injectedClient)
        : config(std::move(value)),
          trigger(config.trigger),
          arbiter(config.arbiter),
          teacher(config.learning),
          topDown(config.topDown),
          executive(config.executive),
          metacognition(config.metacognition) {
        if (config.workerQueueCapacity == 0U || config.workerQueueCapacity > 64U) {
            throw std::invalid_argument("Cortex workerQueueCapacity must be in [1,64]");
        }
        if (config.limits.maxPromptBytes < 1024U || config.limits.maxPromptBytes > 1024U * 1024U) {
            throw std::invalid_argument("Cortex maxPromptBytes must be in [1024,1048576]");
        }
        if (!config.enabled || config.mode == CortexMode::Disabled
            || config.executionMode == CortexExecutionMode::Off) {
            return;
        }
        if (config.mode != CortexMode::ObserveOnly
            && config.mode != CortexMode::Advisor
            && config.mode != CortexMode::Imagination
            && config.mode != CortexMode::Hybrid) {
            throw std::logic_error("Unsupported TATARUS Cortex mode");
        }
        if (config.executionMode == CortexExecutionMode::Replay) {
            injectedClient = makeReplayCortexClient(
                config.recordReplay.path, config.recordReplay.strict);
        } else {
            if (!injectedClient) {
                injectedClient = std::make_shared<LmStudioClient>(
                    config.provider, config.limits);
            }
            if (config.executionMode == CortexExecutionMode::Record) {
                injectedClient = makeRecordingCortexClient(
                    std::move(injectedClient), config.recordReplay.path);
            } else if (config.executionMode != CortexExecutionMode::Live) {
                throw std::logic_error("unsupported Cortex execution mode");
            }
        }
        worker = std::make_unique<detail::CortexWorker>(
            std::move(injectedClient), config.workerQueueCapacity);
    }

    [[nodiscard]] std::uint64_t fingerprint(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) const {
        const auto base = detail::cortexStateFingerprint(
            mind.context(), organism, spatial, imagination);
        const auto step = organism && organism->available
            ? organism->stepCount
            : mind.context().metrics.experiences;
        return mixExecutiveHash(base, executive.requestState(step));
    }

    [[nodiscard]] CortexRequest currentStateFor(
        const CortexRequest& source,
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) const {
        auto current = detail::buildCortexRequest(
            source.requestId,
            source.task,
            source.trigger,
            mind,
            organism,
            source.goal,
            source.availableCapabilities,
            spatial,
            imagination);
        current.executive = executive.requestState(current.organismStep);
        current.recentOutcomes = executive.recentOutcomes();
        current.stateFingerprint = mixExecutiveHash(
            detail::cortexStateFingerprint(mind.context(), organism, spatial, imagination),
            current.executive);
        return current;
    }

    [[nodiscard]] bool responseStillRelevant(
        const CortexRequest& source,
        const CortexRequest& current) const noexcept {
        if (source.stateFingerprint == current.stateFingerprint) return true;
        if (current.organismStep < source.organismStep
            || current.organismStep - source.organismStep
                > config.freshness.maximumResponseAgeSteps) {
            return false;
        }
        if (source.task != current.task
            || source.goal != current.goal
            || source.physiology.available != current.physiology.available
            || source.physiology.sleepPhase != current.physiology.sleepPhase
            || !sameExecutiveContext(source.executive, current.executive)
            || !sameGroundingContext(source.imagination, current.imagination)) {
            return false;
        }
        if (source.spatial.environmentId != current.spatial.environmentId
            || source.spatial.mapAvailable != current.spatial.mapAvailable) {
            return false;
        }
        return true;
    }

    void recordDecisionMetrics(const CortexDecision& decision) {
        ++observation.metrics.arbiterEvaluations;
        switch (decision.kind) {
            case CortexDecisionKind::Accept:
                ++observation.metrics.arbiterAccepted;
                break;
            case CortexDecisionKind::SendToImagination:
                ++observation.metrics.arbiterAccepted;
                ++observation.metrics.imaginationDirectivesPrepared;
                break;
            case CortexDecisionKind::Reject:
                ++observation.metrics.arbiterRejected;
                break;
            case CortexDecisionKind::Defer:
                ++observation.metrics.arbiterDeferred;
                break;
            case CortexDecisionKind::RequestMoreInformation:
                ++observation.metrics.arbiterInformationRequests;
                break;
        }
    }

    void drain(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) {
        if (!worker) return;
        while (auto completed = worker->poll()) {
            const CortexRequest current = currentStateFor(
                completed->request, mind, organism, spatial, imagination);
            std::scoped_lock lock(statusMutex);
            if (!completed->success || !completed->response.has_value()) {
                ++observation.metrics.transportErrors;
                metacognition.recordTransport(false);
                observation.lastError = completed->error.empty()
                    ? "Cortex request failed without diagnostic"
                    : completed->error;
                continue;
            }

            ++observation.metrics.responsesReceived;
            metacognition.recordTransport(true);
            if (config.executionMode == CortexExecutionMode::Record) ++observation.metrics.recordEntries;
            if (config.executionMode == CortexExecutionMode::Replay) ++observation.metrics.replayHits;
            if (completed->request.task == CortexTaskKind::Dream) {
                ++observation.metrics.remDreamResponses;
            }
            observation.lastResponse = completed->response;
            observation.lastRequest = completed->request;

            if (!responseStillRelevant(completed->request, current)) {
                ++observation.metrics.staleResponses;
                metacognition.recordStaleResponse();
                observation.lastDecision.reset();
                observation.lastCognitiveCue.reset();
                lastCueValidationState.reset();
                observation.lastError = "Cortex response discarded for actuation because the source state is stale";
                continue;
            }

            // Provenance remains bound to the exact submitted request and
            // response.  Safety and grounded scoring, however, are rerun with
            // the current bounded state when harmless live ticks changed the
            // bit-level fingerprint while the response was in flight.
            CortexRequest evaluationRequest = current;
            evaluationRequest.requestId = completed->request.requestId;
            evaluationRequest.stateFingerprint = completed->request.stateFingerprint;
            evaluationRequest.trigger = completed->request.trigger;

            if (config.mode == CortexMode::Advisor
                || config.mode == CortexMode::Imagination
                || config.mode == CortexMode::Hybrid) {
                CortexDecision decision = arbiter.evaluate(
                    evaluationRequest, *completed->response);
                recordDecisionMetrics(decision);
                metacognition.recordDecision(decision);
                if (executive.adoptPlan(evaluationRequest, *completed->response, decision)) {
                    ++observation.metrics.plansAdopted;
                }

                observation.lastCognitiveCue.reset();
                lastCueValidationState.reset();
                if (config.topDown.enabled
                    && completed->request.task != CortexTaskKind::Dream) {
                    const auto prepared = topDown.prepare(evaluationRequest, decision);
                    if (prepared.ready) {
                        observation.lastCognitiveCue = prepared.cue;
                        lastCueValidationState = evaluationRequest;
                        ++observation.metrics.topDownCuesPrepared;
                    } else if (decision.kind == CortexDecisionKind::Accept) {
                        ++observation.metrics.topDownCuesRejected;
                    }
                }
                observation.lastDecision = std::move(decision);
            } else {
                observation.lastDecision.reset();
                observation.lastCognitiveCue.reset();
                lastCueValidationState.reset();
            }
            observation.lastError.clear();
        }
    }

    bool submit(CortexRequest request, CortexTriggerReason reason) {
        if (!worker) return false;
        request.trigger = reason;
        const std::string serialized = detail::serializeRequest(request);
        if (serialized.size() > config.limits.maxPromptBytes) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.validationErrors;
            observation.lastError = "Cortex request exceeds configured prompt byte limit";
            return false;
        }
        if (!worker->idle()) return false;
        if (!worker->submit(std::move(request))) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.queueDrops;
            observation.lastError = "Cortex worker queue is full";
            return false;
        }
        std::scoped_lock lock(statusMutex);
        ++observation.metrics.requestsSubmitted;
        observation.lastTrigger = reason;
        observation.lastDecision.reset();
        observation.lastError.clear();
        return true;
    }

    bool run(
        CortexTaskKind task,
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination,
        std::vector<std::string> capabilities,
        std::string goal,
        bool explicitRequest,
        CortexTriggerReason forcedReason = CortexTriggerReason::None) {
        if (!worker) return false;

        const SleepPhase sleepPhase = mind.physiology().sleepPhase;
        {
            std::scoped_lock lock(statusMutex);
            observation.lastSleepPhase = sleepPhase;
        }
        if (task == CortexTaskKind::Dream) {
            if (sleepPhase != SleepPhase::Rem) return false;
        } else if (sleepPhase != SleepPhase::Wake) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.sleepBlockedRequests;
            observation.lastCognitiveCue.reset();
            observation.lastError = "wake Cortex request blocked because TATARUS is sleeping";
            return false;
        }

        if (goal.empty()) {
            if (const auto active = executive.activeGoal()) goal = active->text;
        }
        auto preliminary = detail::buildCortexRequest(
            nextRequestId,
            task,
            explicitRequest ? CortexTriggerReason::Explicit : CortexTriggerReason::None,
            mind,
            organism,
            std::move(goal),
            std::move(capabilities),
            spatial,
            imagination);
        preliminary.executive = executive.requestState(preliminary.organismStep);
        preliminary.recentOutcomes = executive.recentOutcomes();
        preliminary.stateFingerprint = mixExecutiveHash(
            detail::cortexStateFingerprint(mind.context(), organism, spatial, imagination),
            preliminary.executive);

        drain(mind, organism, spatial, imagination);

        CortexTriggerReason reason = forcedReason;
        if (reason == CortexTriggerReason::None) {
            reason = CortexTriggerReason::Explicit;
            if (!explicitRequest) {
                {
                    std::scoped_lock lock(statusMutex);
                    ++observation.metrics.triggerEvaluations;
                }
                reason = trigger.evaluate(preliminary, lastTriggeredStep);
            }
        }
        if (reason == CortexTriggerReason::None) return false;

        CortexRequest request = preliminary;
        request.requestId = nextRequestId;
        request.trigger = reason;

        if (!explicitRequest
            && config.metacognition.enabled
            && !metacognition.allowAutomaticConsultation(task, reason)) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.metacognitiveSuppressions;
            observation.lastSuppressedRequest = request;
            observation.lastTrigger = CortexTriggerReason::MetacognitiveFallback;
            observation.lastDecision.reset();
            observation.lastError = "Cortex automatic consultation suppressed by metacognitive degraded-state fallback";
            ++nextRequestId;
            if (task != CortexTaskKind::Dream) lastTriggeredStep = preliminary.organismStep;
            return false;
        }

        if (task != CortexTaskKind::Dream
            && !explicitRequest
            && config.learning.enabled
            && config.learning.competenceGateEnabled
            && teacher.consultationDecision(request)
                == CortexConsultationDecision::SuppressForAutonomyProbe) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.consultationsSuppressed;
            observation.lastSuppressedRequest = request;
            observation.lastTrigger = reason;
            observation.lastDecision.reset();
            observation.lastError = "Cortex consultation suppressed for a measured autonomous-transfer probe";
            ++nextRequestId;
            lastTriggeredStep = preliminary.organismStep;
            return false;
        }

        {
            std::scoped_lock lock(statusMutex);
            observation.lastSuppressedRequest.reset();
        }
        if (submit(std::move(request), reason)) {
            ++nextRequestId;
            if (task == CortexTaskKind::Dream) {
                lastDreamStep = preliminary.organismStep;
                std::scoped_lock lock(statusMutex);
                ++observation.metrics.remDreamRequests;
            } else {
                lastTriggeredStep = preliminary.organismStep;
            }
            return true;
        }
        return false;
    }

    void observe(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        std::string goal,
        bool explicitRequest) {
        run(
            CortexTaskKind::ObserveState,
            mind,
            organism,
            nullptr,
            nullptr,
            {"STATE_ANALYSIS"},
            std::move(goal),
            explicitRequest);
    }

    void advise(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        std::vector<std::string> capabilities,
        std::string goal,
        bool explicitRequest) {
        if (config.enabled
            && config.mode != CortexMode::Advisor
            && config.mode != CortexMode::Hybrid) {
            throw std::logic_error("Cortex advise() requires CortexMode::Advisor or Hybrid");
        }
        run(
            CortexTaskKind::Plan,
            mind,
            organism,
            &spatial,
            nullptr,
            std::move(capabilities),
            std::move(goal),
            explicitRequest);
    }

    void imagine(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal,
        bool explicitRequest) {
        if (config.enabled
            && config.mode != CortexMode::Imagination
            && config.mode != CortexMode::Hybrid) {
            throw std::logic_error("Cortex imagine() requires CortexMode::Imagination or Hybrid");
        }
        run(
            CortexTaskKind::Imagine,
            mind,
            organism,
            nullptr,
            &imagination,
            std::move(capabilities),
            std::move(goal),
            explicitRequest);
    }

    void hybrid(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal,
        bool explicitRequest) {
        if (config.enabled && config.mode != CortexMode::Hybrid) {
            throw std::logic_error("Cortex hybrid() requires CortexMode::Hybrid");
        }
        {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.hybridRequests;
        }
        run(
            CortexTaskKind::HybridPlan,
            mind,
            organism,
            &spatial,
            &imagination,
            std::move(capabilities),
            std::move(goal),
            explicitRequest);
    }

    void executivePlan(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination,
        std::vector<std::string> capabilities,
        bool explicitRequest) {
        if (config.enabled
            && config.mode != CortexMode::Advisor
            && config.mode != CortexMode::Hybrid) {
            throw std::logic_error("Cortex executive planning requires Advisor or Hybrid mode");
        }
        run(
            CortexTaskKind::ExecutivePlan,
            mind,
            organism,
            spatial,
            imagination,
            std::move(capabilities),
            {},
            explicitRequest,
            explicitRequest ? CortexTriggerReason::Explicit : CortexTriggerReason::GoalChanged);
    }

    [[nodiscard]] TopDownCuePreparation prepareTopDownCue() const {
        std::scoped_lock lock(statusMutex);
        TopDownCuePreparation result;
        if (!observation.lastCognitiveCue.has_value()) {
            result.reason = "no bounded cognitive cue is pending";
            return result;
        }
        result.ready = true;
        result.cue = *observation.lastCognitiveCue;
        result.reason = "bounded cognitive cue is ready for one real observation";
        return result;
    }

    [[nodiscard]] std::optional<CognitiveCue> consumeTopDownCue(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) {
        std::scoped_lock lock(statusMutex);
        if (!observation.lastCognitiveCue.has_value()
            || !lastCueValidationState.has_value()) {
            return std::nullopt;
        }
        if (mind.physiology().sleepPhase != SleepPhase::Wake) {
            ++observation.metrics.topDownCuesRejected;
            observation.lastCognitiveCue.reset();
            lastCueValidationState.reset();
            observation.lastError = "top-down cue rejected because TATARUS is not awake";
            return std::nullopt;
        }
        const CortexRequest current = currentStateFor(
            *lastCueValidationState, mind, organism, spatial, imagination);
        if (!responseStillRelevant(*lastCueValidationState, current)) {
            ++observation.metrics.topDownCuesRejected;
            observation.lastCognitiveCue.reset();
            lastCueValidationState.reset();
            observation.lastError = "top-down cue rejected because its source state is stale";
            return std::nullopt;
        }
        CognitiveCue cue = *observation.lastCognitiveCue;
        observation.lastCognitiveCue.reset();
        lastCueValidationState.reset();
        return cue;
    }

    [[nodiscard]] std::optional<ObserveResult> applyTopDownCue(
        RobotMind& mind,
        const Experience& experience,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) {
        auto cue = consumeTopDownCue(mind, organism, spatial, imagination);
        if (!cue.has_value()) return std::nullopt;
        auto result = mind.observeWithCognitiveCue(experience, *cue);
        std::scoped_lock lock(statusMutex);
        ++observation.metrics.topDownCuesApplied;
        observation.lastError.clear();
        return result;
    }

    [[nodiscard]] std::optional<ExplorerResult> applyTopDownCueToExplorer(
        RobotMind& mind,
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) {
        auto cue = consumeTopDownCue(mind, organism, spatial, imagination);
        if (!cue.has_value()) return std::nullopt;
        auto result = mind.observeExplorerWithCognitiveCue(experience, scannerFrame, *cue);
        std::scoped_lock lock(statusMutex);
        ++observation.metrics.topDownCuesApplied;
        observation.lastError.clear();
        return result;
    }

    void sleepCycle(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexImaginationState& imagination,
        std::vector<std::string> imaginationCapabilities,
        std::string goal) {
        const SleepPhase phase = mind.physiology().sleepPhase;
        {
            std::scoped_lock lock(statusMutex);
            observation.lastSleepPhase = phase;
        }
        if (phase == SleepPhase::Wake) return;
        if (phase == SleepPhase::Nrem) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.nremCycles;
            observation.lastCognitiveCue.reset();
            lastCueValidationState.reset();
            observation.lastDecision.reset();
            observation.lastError.clear();
            return;
        }
        if (!config.dream.enabled || !worker || !imagination.available) return;

        const auto step = organism && organism->available
            ? organism->stepCount
            : mind.context().metrics.experiences;
        if (lastDreamStep != std::numeric_limits<std::uint64_t>::max()
            && step >= lastDreamStep
            && step - lastDreamStep < config.dream.minimumRemIntervalSteps) {
            return;
        }

        static const std::vector<std::string> allowed{
            "USE_IMAGINATION", "VISUAL_RECALL", "SYMBOL_RECALL",
            "COMPOSE", "FREE_IMAGINATION"};
        std::vector<std::string> filtered;
        for (const auto& capability : imaginationCapabilities) {
            if (std::find(allowed.begin(), allowed.end(), capability) != allowed.end()) {
                filtered.push_back(capability);
            }
        }
        if (filtered.empty()) return;

        CortexImaginationState boundedImagination = imagination;
        const auto boundNames = [this](std::vector<std::string>& values) {
            if (values.size() > config.dream.maximumDreamSymbols) {
                values.resize(config.dream.maximumDreamSymbols);
            }
        };
        boundNames(boundedImagination.knownConcepts);
        boundNames(boundedImagination.knownSymbols);
        boundNames(boundedImagination.knownCategories);

        run(
            CortexTaskKind::Dream,
            mind,
            organism,
            nullptr,
            &boundedImagination,
            std::move(filtered),
            std::move(goal),
            false,
            CortexTriggerReason::SleepRem);
    }

    void poll(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination) {
        if (!worker) return;
        if (mind.physiology().sleepPhase == SleepPhase::Rem && imagination) {
            CortexImaginationState bounded = *imagination;
            const auto boundNames = [this](std::vector<std::string>& values) {
                if (values.size() > config.dream.maximumDreamSymbols) {
                    values.resize(config.dream.maximumDreamSymbols);
                }
            };
            boundNames(bounded.knownConcepts);
            boundNames(bounded.knownSymbols);
            boundNames(bounded.knownCategories);
            drain(mind, organism, spatial, &bounded);
            return;
        }
        drain(mind, organism, spatial, imagination);
    }

    [[nodiscard]] CortexObservation status() const {
        std::scoped_lock lock(statusMutex);
        return observation;
    }

    [[nodiscard]] bool idle() const {
        return !worker || worker->idle();
    }

    [[nodiscard]] std::uint64_t pushGoal(
        std::string text, double priority, std::uint64_t currentStep) {
        const auto id = executive.pushGoal(std::move(text), priority, currentStep);
        if (id != 0U) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.goalsCreated;
        }
        return id;
    }

    bool completeGoal(std::uint64_t goalId, bool success, std::uint64_t currentStep) {
        return executive.completeGoal(goalId, success, currentStep);
    }

    bool cancelGoal(std::uint64_t goalId, std::uint64_t currentStep) {
        return executive.cancelGoal(goalId, currentStep);
    }

    void remember(
        std::string key,
        std::string value,
        double salience,
        std::uint64_t currentStep,
        std::uint64_t sourceFingerprint) {
        executive.remember(
            std::move(key), std::move(value), salience, currentStep, sourceFingerprint);
    }

    [[nodiscard]] CortexExecutiveSnapshot executiveStatus() const {
        return executive.snapshot();
    }

    [[nodiscard]] std::optional<CortexPlanStep> activePlanStep() const {
        return executive.activePlanStep();
    }

    [[nodiscard]] CortexMetacognitionSnapshot metacognitionStatus() const {
        return metacognition.snapshot();
    }

    void saveExecutiveState(const std::filesystem::path& path) const {
        executive.saveState(path);
    }

    void loadExecutiveState(const std::filesystem::path& path) {
        executive.loadState(path);
    }

    void autonomousCycle(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState* spatial,
        const CortexImaginationState* imagination,
        std::vector<std::string> capabilities) {
        {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.executiveCycles;
        }
        const auto phase = mind.physiology().sleepPhase;
        if (phase != SleepPhase::Wake) {
            if (imagination) sleepCycle(
                mind, organism, *imagination, std::move(capabilities), {});
            return;
        }

        const auto goal = executive.activeGoal();
        if (!goal.has_value()) {
            observe(mind, organism, {}, false);
            return;
        }

        if (!executive.activePlanStep().has_value()
            && (config.mode == CortexMode::Advisor || config.mode == CortexMode::Hybrid)) {
            executivePlan(
                mind, organism, spatial, imagination, std::move(capabilities), false);
            return;
        }

        std::string effectiveGoal = goal->text;
        if (const auto step = executive.activePlanStep()) {
            effectiveGoal += " | active plan step: " + step->objective;
        }
        if (config.mode == CortexMode::Hybrid && spatial && imagination) {
            hybrid(
                mind, organism, *spatial, *imagination,
                std::move(capabilities), std::move(effectiveGoal), false);
        } else if ((config.mode == CortexMode::Advisor || config.mode == CortexMode::Hybrid) && spatial) {
            advise(
                mind, organism, *spatial,
                std::move(capabilities), std::move(effectiveGoal), false);
        } else if ((config.mode == CortexMode::Imagination || config.mode == CortexMode::Hybrid) && imagination) {
            imagine(
                mind, organism, *imagination,
                std::move(capabilities), std::move(effectiveGoal), false);
        } else {
            observe(mind, organism, std::move(effectiveGoal), false);
        }
    }

    [[nodiscard]] std::optional<CortexTeachingHandle> beginTeachingAction(ActionId actionId) {
        std::optional<CortexRequest> request;
        std::optional<CortexDecision> decision;
        {
            std::scoped_lock lock(statusMutex);
            request = observation.lastRequest;
            decision = observation.lastDecision;
        }
        if (!request.has_value() || !decision.has_value()
            || decision->kind != CortexDecisionKind::Accept
            || !decision->selectedStrategy.has_value()) {
            return std::nullopt;
        }
        return teacher.beginCortexAssisted(*request, *decision, actionId);
    }

    [[nodiscard]] std::optional<CortexTeachingHandle> beginAutonomousTeachingAction(ActionId actionId) {
        std::optional<CortexRequest> request;
        {
            std::scoped_lock lock(statusMutex);
            request = observation.lastSuppressedRequest;
        }
        if (!request.has_value()) return std::nullopt;
        return teacher.beginAutonomous(*request, actionId);
    }

    void completeTeachingAction(
        const CortexTeachingHandle& handle,
        const ActionOutcome& outcome,
        const RobotMind& postMind,
        const organism::OrganismTelemetry* postOrganism,
        const CortexSpatialState* postSpatial,
        const CortexImaginationState* postImagination) {
        teacher.completeRealOutcome(
            handle, outcome, postMind, postOrganism, postSpatial, postImagination);
        executive.recordRealOutcome(outcome, postMind.metrics().experiences);
        if (executive.completeActivePlanStep(outcome, config.learning.outcomeSuccessThreshold)) {
            std::scoped_lock lock(statusMutex);
            ++observation.metrics.planStepsCompleted;
        }
        metacognition.recordRealOutcome(
            handle.cortexUsed, outcome.success >= config.learning.outcomeSuccessThreshold);
        if (!handle.cortexUsed) {
            std::scoped_lock lock(statusMutex);
            observation.lastSuppressedRequest.reset();
        }
    }

    [[nodiscard]] CortexLearningSnapshot learningStatus() const {
        return teacher.snapshot();
    }

    void saveLearningState(const std::filesystem::path& path) const {
        teacher.saveState(path);
    }

    void loadLearningState(const std::filesystem::path& path) {
        teacher.loadState(path);
    }

    CortexConfig config;
    detail::CortexTrigger trigger;
    CortexArbiter arbiter;
    CortexTeacherTransfer teacher;
    CortexTopDownAdapter topDown;
    CortexExecutive executive;
    CortexMetacognition metacognition;
    std::unique_ptr<detail::CortexWorker> worker;
    std::uint64_t nextRequestId = 1;
    std::uint64_t lastTriggeredStep = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastDreamStep = std::numeric_limits<std::uint64_t>::max();
    mutable std::mutex statusMutex;
    CortexObservation observation;
    std::optional<CortexRequest> lastCueValidationState;
};

CortexOrchestrator::CortexOrchestrator(CortexConfig config)
    : impl_(std::make_unique<Impl>(std::move(config), nullptr)) {}

CortexOrchestrator::CortexOrchestrator(
    CortexConfig config,
    std::shared_ptr<ICortexModelClient> client)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(client))) {}

CortexOrchestrator::~CortexOrchestrator() = default;

void CortexOrchestrator::observe(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    std::string goal) {
    impl_->observe(mind, organism, std::move(goal), false);
}

void CortexOrchestrator::requestExplicitAnalysis(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    std::string goal) {
    impl_->observe(mind, organism, std::move(goal), true);
}

void CortexOrchestrator::advise(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState& spatial,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->advise(
        mind, organism, spatial, std::move(capabilities), std::move(goal), false);
}

void CortexOrchestrator::requestExplicitAdvice(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState& spatial,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->advise(
        mind, organism, spatial, std::move(capabilities), std::move(goal), true);
}

void CortexOrchestrator::imagine(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexImaginationState& imagination,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->imagine(
        mind, organism, imagination, std::move(capabilities), std::move(goal), false);
}

void CortexOrchestrator::requestExplicitImagination(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexImaginationState& imagination,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->imagine(
        mind, organism, imagination, std::move(capabilities), std::move(goal), true);
}

void CortexOrchestrator::hybrid(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState& spatial,
    const CortexImaginationState& imagination,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->hybrid(
        mind, organism, spatial, imagination, std::move(capabilities), std::move(goal), false);
}

void CortexOrchestrator::requestExplicitHybrid(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState& spatial,
    const CortexImaginationState& imagination,
    std::vector<std::string> capabilities,
    std::string goal) {
    impl_->hybrid(
        mind, organism, spatial, imagination, std::move(capabilities), std::move(goal), true);
}

void CortexOrchestrator::poll(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination) {
    impl_->poll(mind, organism, spatial, imagination);
}

CortexObservation CortexOrchestrator::status() const {
    return impl_->status();
}

bool CortexOrchestrator::idle() const {
    return impl_->idle();
}

TopDownCuePreparation CortexOrchestrator::prepareTopDownCue() const {
    return impl_->prepareTopDownCue();
}

std::optional<ObserveResult> CortexOrchestrator::applyTopDownCue(
    RobotMind& mind,
    const Experience& experience,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination) {
    return impl_->applyTopDownCue(mind, experience, organism, spatial, imagination);
}

std::optional<ExplorerResult> CortexOrchestrator::applyTopDownCueToExplorer(
    RobotMind& mind,
    const Experience& experience,
    const ScannerFrame& scannerFrame,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination) {
    return impl_->applyTopDownCueToExplorer(
        mind, experience, scannerFrame, organism, spatial, imagination);
}

void CortexOrchestrator::sleepCycle(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexImaginationState& imagination,
    std::vector<std::string> imaginationCapabilities,
    std::string goal) {
    impl_->sleepCycle(
        mind, organism, imagination, std::move(imaginationCapabilities), std::move(goal));
}

std::uint64_t CortexOrchestrator::pushGoal(
    std::string text, double priority, std::uint64_t currentStep) {
    return impl_->pushGoal(std::move(text), priority, currentStep);
}

bool CortexOrchestrator::completeGoal(
    std::uint64_t goalId, bool success, std::uint64_t currentStep) {
    return impl_->completeGoal(goalId, success, currentStep);
}

bool CortexOrchestrator::cancelGoal(std::uint64_t goalId, std::uint64_t currentStep) {
    return impl_->cancelGoal(goalId, currentStep);
}

void CortexOrchestrator::remember(
    std::string key,
    std::string value,
    double salience,
    std::uint64_t currentStep,
    std::uint64_t sourceFingerprint) {
    impl_->remember(
        std::move(key), std::move(value), salience, currentStep, sourceFingerprint);
}

CortexExecutiveSnapshot CortexOrchestrator::executiveStatus() const {
    return impl_->executiveStatus();
}

void CortexOrchestrator::saveExecutiveState(const std::filesystem::path& path) const {
    impl_->saveExecutiveState(path);
}

void CortexOrchestrator::loadExecutiveState(const std::filesystem::path& path) {
    impl_->loadExecutiveState(path);
}

std::optional<CortexPlanStep> CortexOrchestrator::activePlanStep() const {
    return impl_->activePlanStep();
}

void CortexOrchestrator::requestExecutivePlan(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination,
    std::vector<std::string> capabilities) {
    impl_->executivePlan(
        mind, organism, spatial, imagination, std::move(capabilities), true);
}

CortexMetacognitionSnapshot CortexOrchestrator::metacognitionStatus() const {
    return impl_->metacognitionStatus();
}

void CortexOrchestrator::autonomousCycle(
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination,
    std::vector<std::string> capabilities) {
    impl_->autonomousCycle(
        mind, organism, spatial, imagination, std::move(capabilities));
}

std::optional<CortexTeachingHandle> CortexOrchestrator::beginTeachingAction(ActionId actionId) {
    return impl_->beginTeachingAction(actionId);
}

std::optional<CortexTeachingHandle> CortexOrchestrator::beginAutonomousTeachingAction(ActionId actionId) {
    return impl_->beginAutonomousTeachingAction(actionId);
}

void CortexOrchestrator::completeTeachingAction(
    const CortexTeachingHandle& handle,
    const ActionOutcome& outcome,
    const RobotMind& postMind,
    const organism::OrganismTelemetry* postOrganism,
    const CortexSpatialState* postSpatial,
    const CortexImaginationState* postImagination) {
    impl_->completeTeachingAction(
        handle, outcome, postMind, postOrganism, postSpatial, postImagination);
}

CortexLearningSnapshot CortexOrchestrator::learningStatus() const {
    return impl_->learningStatus();
}

void CortexOrchestrator::saveLearningState(const std::filesystem::path& path) const {
    impl_->saveLearningState(path);
}

void CortexOrchestrator::loadLearningState(const std::filesystem::path& path) {
    impl_->loadLearningState(path);
}

} // namespace tatarus::cortex
