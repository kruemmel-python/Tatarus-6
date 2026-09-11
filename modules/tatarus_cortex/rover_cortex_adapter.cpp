#include "tatarus/rover_cortex.hpp"

#include <algorithm>
#include <stdexcept>

namespace tatarus::cortex {

const char* toString(RoverDirectiveKind value) noexcept {
    switch (value) {
        case RoverDirectiveKind::None: return "NONE";
        case RoverDirectiveKind::HoldPosition: return "HOLD_POSITION";
        case RoverDirectiveKind::RequestSensorScan: return "REQUEST_SENSOR_SCAN";
        case RoverDirectiveKind::RecallRouteContext: return "RECALL_ROUTE_CONTEXT";
        case RoverDirectiveKind::UseNeuralMotorChoice: return "USE_NEURAL_MOTOR_CHOICE";
        case RoverDirectiveKind::SearchAlternative: return "SEARCH_ALTERNATIVE";
    }
    return "NONE";
}

std::vector<std::string> RoverCortexAdapter::capabilities() {
    return {
        "STATE_ANALYSIS",
        "SCAN",
        "RECALL_ROUTE",
        "FOLLOW_KNOWN_ROUTE",
        "EXPLORE_FRONTIER",
        "SEARCH_ALTERNATIVE",
    };
}

CortexSpatialState RoverCortexAdapter::spatialState(
    const ExplorerResult& explorer) const noexcept {
    CortexSpatialState state;
    state.environmentId = explorer.cartography.environmentId;
    state.mapAvailable = explorer.cartography.summary.available
        && explorer.cartography.summary.mappedVoxels > 0U;
    state.localNovelty = explorer.cartography.localNovelty;
    state.frontierRatio = explorer.cartography.frontierRatio;
    state.obstacleProximity = explorer.cartography.obstacleProximity;
    state.knownRouteConfidence = std::clamp(
        std::max(
            explorer.cognition.prospection.sequenceFamiliarity,
            explorer.cognition.prediction.confidence),
        0.0,
        1.0);
    state.knownRouteAvailable = explorer.cognition.prediction.available
        && explorer.cognition.prospection.sequenceFamiliarity >= 0.05;
    return state;
}

RoverDirective RoverCortexAdapter::translate(
    const CortexDecision& decision,
    const ExplorerResult& explorer) const {
    RoverDirective directive;
    directive.environmentId = explorer.cartography.environmentId;
    directive.reason = decision.reason;

    if (decision.kind == CortexDecisionKind::RequestMoreInformation) {
        for (const auto& request : decision.informationRequests) {
            if (request.capability == "SCAN") {
                directive.kind = RoverDirectiveKind::RequestSensorScan;
                directive.requiresFreshScan = true;
                directive.reason = request.reason;
                return directive;
            }
            if (request.capability == "RECALL_ROUTE") {
                directive.kind = RoverDirectiveKind::RecallRouteContext;
                directive.reason = request.reason;
                return directive;
            }
        }
        return directive;
    }

    if (decision.kind != CortexDecisionKind::Accept
        || !decision.selectedStrategy.has_value()) {
        return directive;
    }

    const auto& strategy = *decision.selectedStrategy;
    directive.sourceStrategy = strategy.kind;
    directive.strategyId = strategy.id;

    switch (strategy.kind) {
        case CortexStrategyKind::Observe:
        case CortexStrategyKind::WaitAndObserve:
            directive.kind = RoverDirectiveKind::HoldPosition;
            return directive;

        case CortexStrategyKind::RequestScan:
            directive.kind = RoverDirectiveKind::RequestSensorScan;
            directive.requiresFreshScan = true;
            return directive;

        case CortexStrategyKind::RecallRoute:
            directive.kind = RoverDirectiveKind::RecallRouteContext;
            return directive;

        case CortexStrategyKind::FollowKnownRoute:
        case CortexStrategyKind::ExploreFrontier:
            // The LLM never supplies a direction. The only direction exposed here
            // is the motor direction already selected by the TATARUS nervous system.
            directive.kind = RoverDirectiveKind::UseNeuralMotorChoice;
            directive.neuralDirection = explorer.cognition.motor.selectedDirection;
            return directive;

        case CortexStrategyKind::SearchAlternative:
            directive.kind = RoverDirectiveKind::SearchAlternative;
            directive.requiresFreshScan = true;
            return directive;

        case CortexStrategyKind::UseImagination:
        case CortexStrategyKind::VisualRecall:
        case CortexStrategyKind::SymbolRecall:
        case CortexStrategyKind::Compose:
        case CortexStrategyKind::FreeImagination:
        case CortexStrategyKind::Unknown:
            break;
    }

    throw std::logic_error("RoverCortexAdapter received a non-rover strategy as accepted action");
}

} // namespace tatarus::cortex
