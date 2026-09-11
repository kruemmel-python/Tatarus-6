#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tatarus {

enum class CognitiveAttentionTarget : std::uint8_t {
    Balanced,
    Vision,
    Audio,
    Touch,
    Text,
    Interoception,
};

// Public, bounded top-down context for phase 10. This is intentionally not a
// motor command and carries no reward or physiology mutation channel.
struct CognitiveCue {
    CognitiveAttentionTarget attention = CognitiveAttentionTarget::Balanced;
    std::uint32_t recallCue = 0;
    double recallStrength = 0.0;

    // Four generic context axes. They are injected into the context population
    // only; they are not interpreted as N/E/S/W motor directions.
    std::array<double, 4> goalBias{};
    double goalBiasStrength = 0.0;

    // Small bounded semantic feature vector derived from trusted host goal text
    // and an arbiter-approved strategy kind.
    std::vector<double> semanticContext;

    [[nodiscard]] bool neutral() const noexcept;
};

struct CognitiveCueLimits {
    static constexpr double maximumRecallStrength = 0.50;
    static constexpr double maximumGoalBiasStrength = 0.25;
    static constexpr std::size_t maximumSemanticChannels = 32U;
    static constexpr double maximumSemanticMagnitude = 0.35;
};

} // namespace tatarus
