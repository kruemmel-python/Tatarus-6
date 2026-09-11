#include "cortex_trigger.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace tatarus::cortex::detail {

CortexTrigger::CortexTrigger(CortexTriggerConfig config) : config_(config) {
    const auto inUnit = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!inUnit(config_.novelty)
        || !inUnit(config_.predictionError)
        || !inUnit(config_.lowMotorConfidence)) {
        throw std::invalid_argument("Cortex trigger thresholds must be finite and in [0,1]");
    }
}

CortexTriggerReason CortexTrigger::evaluate(
    const CortexRequest& request,
    std::uint64_t lastTriggeredStep) const noexcept {
    if (request.trigger == CortexTriggerReason::Explicit) return CortexTriggerReason::Explicit;
    if (lastTriggeredStep != std::numeric_limits<std::uint64_t>::max()) {
        if (request.organismStep < lastTriggeredStep) return CortexTriggerReason::None;
        if (request.organismStep - lastTriggeredStep < config_.cooldownSteps) {
            return CortexTriggerReason::None;
        }
    }

    const bool novel = request.neural.novelty >= config_.novelty;
    const bool error = request.neural.predictionError >= config_.predictionError;
    const bool lowConfidence = request.neural.motorConfidence <= config_.lowMotorConfidence;

    const unsigned active = static_cast<unsigned>(novel)
        + static_cast<unsigned>(error)
        + static_cast<unsigned>(lowConfidence);
    if (active >= 2U) return CortexTriggerReason::CombinedUncertainty;
    if (novel) return CortexTriggerReason::Novelty;
    if (error) return CortexTriggerReason::PredictionError;
    if (lowConfidence) return CortexTriggerReason::LowMotorConfidence;
    return CortexTriggerReason::None;
}

} // namespace tatarus::cortex::detail
