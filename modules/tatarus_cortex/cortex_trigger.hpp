#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"

#include <cstdint>

namespace tatarus::cortex::detail {

class CortexTrigger {
public:
    explicit CortexTrigger(CortexTriggerConfig config = {});

    [[nodiscard]] CortexTriggerReason evaluate(
        const CortexRequest& request,
        std::uint64_t lastTriggeredStep) const noexcept;

private:
    CortexTriggerConfig config_;
};

} // namespace tatarus::cortex::detail
