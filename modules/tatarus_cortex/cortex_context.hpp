#pragma once

#include "tatarus/cortex_types.hpp"
#include "tatarus/robot_mind.hpp"
#include "tatarus/organism_types.hpp"

#include <string>
#include <vector>

namespace tatarus::cortex::detail {

[[nodiscard]] CortexRequest buildCortexRequest(
    std::uint64_t requestId,
    CortexTaskKind task,
    CortexTriggerReason trigger,
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    std::string goal,
    std::vector<std::string> capabilities = {},
    const CortexSpatialState* spatial = nullptr,
    const CortexImaginationState* imagination = nullptr);

[[nodiscard]] std::uint64_t cortexStateFingerprint(
    const CognitiveContext& context,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial = nullptr,
    const CortexImaginationState* imagination = nullptr) noexcept;

} // namespace tatarus::cortex::detail
