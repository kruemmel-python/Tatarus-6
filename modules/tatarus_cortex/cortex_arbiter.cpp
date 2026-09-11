#include "tatarus/cortex.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace tatarus::cortex {
namespace {

[[nodiscard]] double clamp01(double value) noexcept {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] double maximumObstacle(const CortexSpatialState& spatial) noexcept {
    return *std::max_element(
        spatial.obstacleProximity.begin(), spatial.obstacleProximity.end());
}

[[nodiscard]] bool isMovementLike(CortexStrategyKind kind) noexcept {
    return kind == CortexStrategyKind::FollowKnownRoute
        || kind == CortexStrategyKind::ExploreFrontier;
}

[[nodiscard]] bool isImaginationKind(CortexStrategyKind kind) noexcept {
    return kind == CortexStrategyKind::UseImagination
        || kind == CortexStrategyKind::VisualRecall
        || kind == CortexStrategyKind::SymbolRecall
        || kind == CortexStrategyKind::Compose
        || kind == CortexStrategyKind::FreeImagination;
}

[[nodiscard]] std::string_view intrinsicCapability(CortexStrategyKind kind) noexcept {
    switch (kind) {
        case CortexStrategyKind::Observe:
        case CortexStrategyKind::WaitAndObserve:
            return "STATE_ANALYSIS";
        case CortexStrategyKind::RequestScan:
            return "SCAN";
        case CortexStrategyKind::RecallRoute:
            return "RECALL_ROUTE";
        case CortexStrategyKind::FollowKnownRoute:
            return "FOLLOW_KNOWN_ROUTE";
        case CortexStrategyKind::ExploreFrontier:
            return "EXPLORE_FRONTIER";
        case CortexStrategyKind::SearchAlternative:
            return "SEARCH_ALTERNATIVE";
        case CortexStrategyKind::UseImagination:
            return "USE_IMAGINATION";
        case CortexStrategyKind::VisualRecall:
            return "VISUAL_RECALL";
        case CortexStrategyKind::SymbolRecall:
            return "SYMBOL_RECALL";
        case CortexStrategyKind::Compose:
            return "COMPOSE";
        case CortexStrategyKind::FreeImagination:
            return "FREE_IMAGINATION";
        case CortexStrategyKind::Unknown:
            break;
    }
    return {};
}

[[nodiscard]] double groundedSuitability(
    const CortexRequest& request,
    CortexStrategyKind kind) noexcept {
    const double novelty = clamp01(std::max(
        request.neural.novelty, request.spatial.localNovelty));
    const double predictionError = clamp01(request.neural.predictionError);
    const double predictionConfidence = clamp01(request.neural.predictionConfidence);
    const double motorUncertainty = 1.0 - clamp01(request.neural.motorConfidence);
    const double familiarity = clamp01(request.neural.sequenceFamiliarity);
    const double frontier = clamp01(request.spatial.frontierRatio);
    const double distress = request.physiology.available
        ? clamp01(request.physiology.visceralDistress)
        : 0.0;
    const double obstacle = clamp01(maximumObstacle(request.spatial));

    switch (kind) {
        case CortexStrategyKind::Observe:
            return clamp01(0.25 + 0.30 * novelty + 0.25 * predictionError + 0.20 * motorUncertainty);
        case CortexStrategyKind::WaitAndObserve:
            return clamp01(0.10 + 0.30 * motorUncertainty + 0.35 * distress + 0.25 * predictionError);
        case CortexStrategyKind::RequestScan:
            return clamp01(0.10 + 0.35 * novelty + 0.20 * predictionError + 0.20 * frontier + 0.15 * obstacle);
        case CortexStrategyKind::RecallRoute:
            return clamp01(0.10 + 0.35 * familiarity + 0.20 * predictionConfidence
                + 0.20 * static_cast<double>(request.spatial.mapAvailable)
                + 0.15 * clamp01(request.spatial.knownRouteConfidence));
        case CortexStrategyKind::FollowKnownRoute:
            return clamp01(0.05 + 0.30 * familiarity + 0.20 * predictionConfidence
                + 0.25 * clamp01(request.spatial.knownRouteConfidence)
                + 0.20 * (1.0 - novelty));
        case CortexStrategyKind::ExploreFrontier:
            return clamp01(0.05 + 0.40 * frontier + 0.30 * novelty
                + 0.15 * (1.0 - distress) + 0.10 * motorUncertainty);
        case CortexStrategyKind::SearchAlternative:
            return clamp01(0.10 + 0.30 * predictionError + 0.25 * obstacle
                + 0.20 * motorUncertainty + 0.15 * novelty);
        case CortexStrategyKind::UseImagination:
            return clamp01(0.10 + 0.25 * predictionError + 0.25 * motorUncertainty
                + 0.20 * novelty + 0.20 * static_cast<double>(request.imagination.available));
        case CortexStrategyKind::VisualRecall:
            return clamp01(0.10 + 0.20 * predictionError + 0.20 * novelty
                + 0.50 * static_cast<double>(request.imagination.visualEngrams > 0U));
        case CortexStrategyKind::SymbolRecall:
            return clamp01(0.10 + 0.15 * predictionError + 0.15 * novelty
                + 0.60 * static_cast<double>(request.imagination.symbolEngrams > 0U));
        case CortexStrategyKind::Compose:
            return clamp01(0.10 + 0.20 * novelty + 0.15 * predictionError
                + 0.55 * static_cast<double>(request.imagination.symbolEngrams >= 2U));
        case CortexStrategyKind::FreeImagination:
            return clamp01(0.10 + 0.30 * novelty + 0.20 * motorUncertainty
                + 0.40 * static_cast<double>(request.imagination.visualEngrams > 0U));
        case CortexStrategyKind::Unknown:
            break;
    }
    return 0.0;
}

} // namespace

CortexArbiter::CortexArbiter(CortexArbiterConfig config) : config_(config) {
    const auto valid = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (!valid(config_.minimumStrategyConfidence)
        || !valid(config_.maximumStrategyRisk)
        || !valid(config_.minimumAcceptScore)
        || !valid(config_.criticalVisceralDistress)
        || !valid(config_.criticalBrainAtp)) {
        throw std::invalid_argument("Cortex arbiter thresholds must be finite and in [0,1]");
    }
}

CortexDecision CortexArbiter::evaluate(
    const CortexRequest& request,
    const CortexResponse& response) const {
    CortexDecision decision;

    if (response.requestId != request.requestId
        || response.sourceFingerprint != request.stateFingerprint) {
        decision.reason = "response provenance does not match current Cortex request";
        return decision;
    }

    const std::unordered_set<std::string> capabilities(
        request.availableCapabilities.begin(), request.availableCapabilities.end());
    const auto hasCapability = [&capabilities](std::string_view capability) {
        return capability.empty()
            || capabilities.contains(std::string(capability));
    };

    for (const auto& information : response.informationRequests) {
        if (hasCapability(information.capability)) {
            decision.informationRequests.push_back(information);
        }
    }

    double bestScore = -1.0;
    const CortexStrategy* best = nullptr;

    for (const auto& strategy : response.strategies) {
        CortexStrategyEvaluation evaluation;
        evaluation.strategyId = strategy.id;
        evaluation.kind = strategy.kind;

        if (strategy.kind == CortexStrategyKind::Unknown) {
            evaluation.reason = "unknown strategy kind";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }

        if (request.task == CortexTaskKind::Dream) {
            if (request.physiology.sleepPhase != SleepPhase::Rem) {
                evaluation.reason = "DREAM strategies are valid only during TATARUS REM sleep";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            if (!isImaginationKind(strategy.kind)) {
                evaluation.reason = "DREAM forbids real-world and rover strategies";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
        }

        bool missingCapability = false;
        const auto intrinsic = intrinsicCapability(strategy.kind);
        if (!hasCapability(intrinsic)) missingCapability = true;
        for (const auto& required : strategy.requiredCapabilities) {
            if (!hasCapability(required)) {
                missingCapability = true;
                break;
            }
        }
        if (missingCapability) {
            evaluation.reason = "strategy requires a capability not exposed by the host";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }

        if (strategy.confidence < config_.minimumStrategyConfidence) {
            evaluation.reason = "LLM confidence below arbiter minimum";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.estimatedRisk > config_.maximumStrategyRisk) {
            evaluation.reason = "LLM-declared risk exceeds arbiter maximum";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }

        if (isMovementLike(strategy.kind) && request.physiology.available) {
            if (request.physiology.visceralDistress >= config_.criticalVisceralDistress) {
                evaluation.reason = "movement-like strategy blocked by organism distress";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            if (request.physiology.atp <= config_.criticalBrainAtp
                || request.physiology.brainOxygen <= config_.criticalBrainAtp) {
                evaluation.reason = "movement-like strategy blocked by low neural reserve";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
        }

        if (strategy.kind == CortexStrategyKind::FollowKnownRoute
            && (!request.spatial.mapAvailable || !request.spatial.knownRouteAvailable)) {
            evaluation.reason = "known-route strategy requires TATARUS-confirmed route familiarity";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::ExploreFrontier
            && (!request.spatial.mapAvailable || request.spatial.frontierRatio <= 0.0)) {
            evaluation.reason = "frontier exploration requires an observed map frontier";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }

        if (isImaginationKind(strategy.kind) && !request.imagination.available) {
            evaluation.reason = "imagination strategy requires an available TATARUS IMAGINATIO state";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::VisualRecall
            && request.imagination.visualEngrams == 0U) {
            evaluation.reason = "VISUAL_RECALL requires TATARUS visual memory";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::SymbolRecall
            && request.imagination.symbolEngrams == 0U) {
            evaluation.reason = "SYMBOL_RECALL requires TATARUS symbol memory";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::Compose
            && request.imagination.symbolEngrams < 2U) {
            evaluation.reason = "COMPOSE requires at least two TATARUS symbol engrams";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::FreeImagination
            && request.imagination.visualEngrams == 0U) {
            evaluation.reason = "FREE_IMAGINATION requires TATARUS visual memory";
            decision.evaluations.push_back(std::move(evaluation));
            continue;
        }
        if (strategy.kind == CortexStrategyKind::UseImagination) {
            if (!response.imagination.has_value()) {
                evaluation.reason = "USE_IMAGINATION requires a structured imagination directive";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            const auto& directive = *response.imagination;
            if (directive.mode == "VISUAL_RECALL" && request.imagination.visualEngrams == 0U) {
                evaluation.reason = "generic imagination directive requires visual memory";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            if (directive.mode == "SYMBOL_RECALL" && request.imagination.symbolEngrams == 0U) {
                evaluation.reason = "generic imagination directive requires symbol memory";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            if (directive.mode == "COMPOSE" && request.imagination.symbolEngrams < 2U) {
                evaluation.reason = "generic COMPOSE directive requires at least two symbol engrams";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
            if (directive.mode == "FREE_IMAGINATION" && request.imagination.visualEngrams == 0U) {
                evaluation.reason = "generic free imagination requires visual memory";
                decision.evaluations.push_back(std::move(evaluation));
                continue;
            }
        }

        const double grounded = groundedSuitability(request, strategy.kind);
        const double score = clamp01(
            0.55 * grounded
            + 0.15 * clamp01(strategy.estimatedBenefit)
            + 0.10 * clamp01(strategy.confidence)
            + 0.20 * (1.0 - clamp01(strategy.estimatedRisk)));

        evaluation.score = score;
        evaluation.eligible = score >= config_.minimumAcceptScore;
        evaluation.reason = evaluation.eligible
            ? "eligible after grounded TATARUS evaluation"
            : "grounded score below arbiter acceptance threshold";
        decision.evaluations.push_back(evaluation);

        if (evaluation.eligible && score > bestScore) {
            bestScore = score;
            best = &strategy;
        }
    }

    if (best) {
        decision.selectedStrategy = *best;
        decision.score = bestScore;
        if (isImaginationKind(best->kind)) {
            decision.kind = CortexDecisionKind::SendToImagination;
            decision.reason = "strategy passed the grounded arbiter and is authorized for the bounded IMAGINATIO adapter";
        } else {
            decision.kind = CortexDecisionKind::Accept;
            decision.reason = "highest grounded eligible strategy accepted";
        }
        return decision;
    }

    if (!decision.informationRequests.empty()) {
        decision.kind = CortexDecisionKind::RequestMoreInformation;
        decision.reason = "no actionable strategy accepted; valid host information was requested";
        return decision;
    }

    if (!response.strategies.empty()) {
        decision.kind = CortexDecisionKind::Reject;
        decision.reason = "all proposed strategies were rejected by capability, physiology, or grounded scoring";
    } else {
        decision.kind = CortexDecisionKind::Defer;
        decision.reason = "model returned no actionable strategy";
    }
    return decision;
}

} // namespace tatarus::cortex
