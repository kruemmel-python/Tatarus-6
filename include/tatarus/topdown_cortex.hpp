#pragma once

#include "tatarus/cognitive_cue.hpp"
#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"

#include <optional>
#include <string>

namespace tatarus::cortex {

struct TopDownCuePreparation {
    bool ready = false;
    CognitiveCue cue;
    std::string reason;
};

class CortexTopDownAdapter {
public:
    explicit CortexTopDownAdapter(CortexTopDownConfig config = {});

    [[nodiscard]] TopDownCuePreparation prepare(
        const CortexRequest& request,
        const CortexDecision& decision) const;

private:
    CortexTopDownConfig config_;
};

} // namespace tatarus::cortex
