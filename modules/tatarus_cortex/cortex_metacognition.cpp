#include "tatarus/cortex_metacognition.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tatarus::cortex {

const char* toString(CortexReliabilityState value) noexcept {
    switch (value) {
        case CortexReliabilityState::Nominal: return "NOMINAL";
        case CortexReliabilityState::Cautious: return "CAUTIOUS";
        case CortexReliabilityState::Degraded: return "DEGRADED";
    }
    return "NOMINAL";
}

CortexMetacognition::CortexMetacognition(CortexMetacognitionConfig config)
    : config_(config) {
    const auto valid = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (config_.minimumSamples == 0U) {
        throw std::invalid_argument("metacognition.minimum_samples must be greater than zero");
    }
    if (!valid(config_.cautiousReliabilityThreshold)
        || !valid(config_.degradedReliabilityThreshold)
        || !valid(config_.maximumStaleRate)
        || config_.degradedReliabilityThreshold > config_.cautiousReliabilityThreshold) {
        throw std::invalid_argument("invalid Cortex metacognition thresholds");
    }
}

void CortexMetacognition::recordTransport(bool success) {
    if (!config_.enabled) return;
    if (success) ++state_.responses;
    else ++state_.transportErrors;
    recompute();
}

void CortexMetacognition::recordStaleResponse() {
    if (!config_.enabled) return;
    ++state_.staleResponses;
    recompute();
}

void CortexMetacognition::recordDecision(const CortexDecision& decision) {
    if (!config_.enabled) return;
    if (decision.kind == CortexDecisionKind::Accept
        || decision.kind == CortexDecisionKind::SendToImagination) {
        ++state_.acceptedDecisions;
    } else if (decision.kind == CortexDecisionKind::Reject) {
        ++state_.rejectedDecisions;
    }
    recompute();
}

void CortexMetacognition::recordRealOutcome(bool cortexUsed, bool success) {
    if (!config_.enabled || !cortexUsed) return;
    ++state_.realAssistedOutcomes;
    if (success) ++state_.realAssistedSuccesses;
    recompute();
}

bool CortexMetacognition::allowAutomaticConsultation(
    CortexTaskKind task,
    CortexTriggerReason reason) const {
    if (!config_.enabled) return true;
    if (reason == CortexTriggerReason::Explicit) return true;
    if (state_.state != CortexReliabilityState::Degraded) return true;

    // In degraded mode TATARUS falls back to its own learned control. REM dreams
    // are also suppressed because they are optional cognitive work, not survival.
    (void)task;
    return false;
}

CortexMetacognitionSnapshot CortexMetacognition::snapshot() const {
    return state_;
}

void CortexMetacognition::recompute() {
    const double transportTotal = static_cast<double>(state_.responses + state_.transportErrors);
    state_.transportReliability = transportTotal > 0.0
        ? static_cast<double>(state_.responses) / transportTotal
        : 1.0;

    const double decisionTotal = static_cast<double>(state_.acceptedDecisions + state_.rejectedDecisions);
    state_.decisionAcceptanceRate = decisionTotal > 0.0
        ? static_cast<double>(state_.acceptedDecisions) / decisionTotal
        : 1.0;

    state_.assistedSuccessRate = state_.realAssistedOutcomes > 0U
        ? static_cast<double>(state_.realAssistedSuccesses)
            / static_cast<double>(state_.realAssistedOutcomes)
        : 1.0;

    const double responseLike = static_cast<double>(state_.responses + state_.staleResponses);
    state_.staleRate = responseLike > 0.0
        ? static_cast<double>(state_.staleResponses) / responseLike
        : 0.0;

    // Reliability is deliberately conservative. Transport/validation health and
    // real-world assisted success dominate; acceptance is only weak evidence.
    state_.aggregateReliability = std::clamp(
        0.45 * state_.transportReliability
        + 0.40 * state_.assistedSuccessRate
        + 0.15 * state_.decisionAcceptanceRate,
        0.0,
        1.0);

    const std::uint64_t samples = state_.responses + state_.transportErrors
        + state_.realAssistedOutcomes;
    if (samples < config_.minimumSamples) {
        state_.state = CortexReliabilityState::Nominal;
        return;
    }
    if (state_.staleRate > config_.maximumStaleRate
        || state_.aggregateReliability < config_.degradedReliabilityThreshold) {
        state_.state = CortexReliabilityState::Degraded;
    } else if (state_.aggregateReliability < config_.cautiousReliabilityThreshold) {
        state_.state = CortexReliabilityState::Cautious;
    } else {
        state_.state = CortexReliabilityState::Nominal;
    }
}

} // namespace tatarus::cortex
