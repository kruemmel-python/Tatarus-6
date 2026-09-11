#pragma once

#include "tatarus/cortex_types.hpp"
#include "tatarus/cartography.hpp"
#include "tatarus/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tatarus::cortex {

enum class RoverDirectiveKind : std::uint8_t {
    None,
    HoldPosition,
    RequestSensorScan,
    RecallRouteContext,
    UseNeuralMotorChoice,
    SearchAlternative,
};

struct RoverDirective {
    RoverDirectiveKind kind = RoverDirectiveKind::None;
    CortexStrategyKind sourceStrategy = CortexStrategyKind::Unknown;
    std::string strategyId;
    EnvironmentId environmentId = 0;
    std::optional<std::uint32_t> neuralDirection;
    bool requiresFreshScan = false;
    std::string reason;
};

class RoverCortexAdapter {
public:
    [[nodiscard]] static std::vector<std::string> capabilities();

    [[nodiscard]] CortexSpatialState spatialState(
        const ExplorerResult& explorer) const noexcept;

    [[nodiscard]] RoverDirective translate(
        const CortexDecision& decision,
        const ExplorerResult& explorer) const;
};

[[nodiscard]] const char* toString(RoverDirectiveKind value) noexcept;

} // namespace tatarus::cortex
