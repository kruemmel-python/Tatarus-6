#include "tatarus/topdown_cortex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace tatarus::cortex {
namespace {

[[nodiscard]] std::uint64_t fnv1a(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] double signedByte(std::uint64_t hash, unsigned shift) noexcept {
    const auto byte = static_cast<unsigned>((hash >> shift) & 0xffU);
    return (static_cast<double>(byte) / 127.5) - 1.0;
}

[[nodiscard]] CognitiveAttentionTarget attentionFor(CortexStrategyKind kind) noexcept {
    switch (kind) {
        case CortexStrategyKind::RequestScan:
        case CortexStrategyKind::ExploreFrontier:
        case CortexStrategyKind::VisualRecall:
        case CortexStrategyKind::Compose:
        case CortexStrategyKind::FreeImagination:
            return CognitiveAttentionTarget::Vision;
        case CortexStrategyKind::RecallRoute:
        case CortexStrategyKind::FollowKnownRoute:
        case CortexStrategyKind::SymbolRecall:
            return CognitiveAttentionTarget::Text;
        case CortexStrategyKind::WaitAndObserve:
            return CognitiveAttentionTarget::Interoception;
        case CortexStrategyKind::Observe:
        case CortexStrategyKind::SearchAlternative:
        case CortexStrategyKind::UseImagination:
        case CortexStrategyKind::Unknown:
            return CognitiveAttentionTarget::Balanced;
    }
    return CognitiveAttentionTarget::Balanced;
}

[[nodiscard]] std::string trustedSemanticKey(
    const CortexRequest& request,
    const CortexStrategy& strategy) {
    std::string value;
    value.reserve(request.goal.size() + 64U);
    value.append(toString(strategy.kind));
    value.push_back('|');
    value.append(request.goal);
    return value;
}

} // namespace

CortexTopDownAdapter::CortexTopDownAdapter(CortexTopDownConfig config)
    : config_(config) {
    const auto valid01 = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (!valid01(config_.maximumRecallStrength)
        || config_.maximumRecallStrength > CognitiveCueLimits::maximumRecallStrength) {
        throw std::invalid_argument("top_down.maximum_recall_strength exceeds safe CognitiveCue limit");
    }
    if (!valid01(config_.maximumGoalBiasStrength)
        || config_.maximumGoalBiasStrength > CognitiveCueLimits::maximumGoalBiasStrength) {
        throw std::invalid_argument("top_down.maximum_goal_bias_strength exceeds safe CognitiveCue limit");
    }
    if (!valid01(config_.semanticGain)
        || config_.semanticGain > CognitiveCueLimits::maximumSemanticMagnitude) {
        throw std::invalid_argument("top_down.semantic_gain exceeds safe CognitiveCue limit");
    }
    if (config_.semanticChannels == 0U
        || config_.semanticChannels > CognitiveCueLimits::maximumSemanticChannels) {
        throw std::invalid_argument("top_down.semantic_channels exceeds safe CognitiveCue limit");
    }
}

TopDownCuePreparation CortexTopDownAdapter::prepare(
    const CortexRequest& request,
    const CortexDecision& decision) const {
    TopDownCuePreparation result;
    if (!config_.enabled) {
        result.reason = "bounded top-down cognition is disabled";
        return result;
    }
    if (decision.kind != CortexDecisionKind::Accept
        || !decision.selectedStrategy.has_value()) {
        result.reason = "arbiter did not accept a strategy for top-down cognition";
        return result;
    }
    if (request.task == CortexTaskKind::Dream) {
        result.reason = "REM dream requests use IMAGINATIO and never inject a wake top-down cue";
        return result;
    }

    const CortexStrategy& strategy = *decision.selectedStrategy;
    if (strategy.kind == CortexStrategyKind::Unknown) {
        result.reason = "unknown strategy cannot become a cognitive cue";
        return result;
    }

    CognitiveCue cue;
    cue.attention = attentionFor(strategy.kind);

    const double confidence = std::clamp(strategy.confidence, 0.0, 1.0);
    cue.recallStrength = std::min(
        config_.maximumRecallStrength,
        config_.maximumRecallStrength * (0.25 + 0.75 * confidence));

    const std::string key = trustedSemanticKey(request, strategy);
    std::uint64_t hash = fnv1a(key);
    cue.recallCue = static_cast<std::uint32_t>(hash % 64U);

    cue.goalBiasStrength = std::min(
        config_.maximumGoalBiasStrength,
        config_.maximumGoalBiasStrength * (0.25 + 0.75 * confidence));
    for (std::size_t i = 0; i < cue.goalBias.size(); ++i) {
        cue.goalBias[i] = signedByte(hash, static_cast<unsigned>((i * 8U) % 56U));
    }

    cue.semanticContext.reserve(config_.semanticChannels);
    for (std::size_t i = 0; i < config_.semanticChannels; ++i) {
        hash ^= hash >> 12U;
        hash ^= hash << 25U;
        hash ^= hash >> 27U;
        hash *= 2685821657736338717ULL;
        cue.semanticContext.push_back(
            config_.semanticGain * signedByte(hash, static_cast<unsigned>((i * 7U) % 56U)));
    }

    result.ready = !cue.neutral();
    result.cue = std::move(cue);
    result.reason = result.ready
        ? "arbiter-approved strategy converted to bounded context-only CognitiveCue"
        : "prepared CognitiveCue is neutral";
    return result;
}

} // namespace tatarus::cortex
