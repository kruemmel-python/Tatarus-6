#include "tatarus/cognitive_cue.hpp"

#include <cmath>

namespace tatarus {

bool CognitiveCue::neutral() const noexcept {
    if (attention != CognitiveAttentionTarget::Balanced) return false;
    if (recallCue != 0U || std::abs(recallStrength) > 0.0) return false;
    if (std::abs(goalBiasStrength) > 0.0) return false;
    for (const double value : goalBias) {
        if (std::abs(value) > 0.0) return false;
    }
    for (const double value : semanticContext) {
        if (std::abs(value) > 0.0) return false;
    }
    return true;
}

} // namespace tatarus
