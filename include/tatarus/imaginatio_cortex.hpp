#pragma once

#include "tatarus/cortex_types.hpp"
#include "tatarus/imaginatio.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tatarus::cortex {

enum class ImaginatioDirectiveKind : std::uint8_t {
    None,
    VisualRecall,
    SymbolRecall,
    Compose,
    FreeImagination,
};

struct ImaginatioDirective {
    ImaginatioDirectiveKind kind = ImaginatioDirectiveKind::None;
    CortexStrategyKind sourceStrategy = CortexStrategyKind::Unknown;
    std::string strategyId;
    std::vector<std::string> symbols;
    std::string conceptText;
    bool executable = false;
    std::string reason;
};

struct ImaginatioExecutionResult {
    bool executed = false;
    ImaginatioDirective directive;
    std::optional<ImaginationReport> report;
    std::string error;
};

class ImaginatioCortexAdapter {
public:
    [[nodiscard]] static std::vector<std::string> capabilities();

    [[nodiscard]] CortexImaginationState imaginationState(
        const VisualImagination& imagination) const;

    [[nodiscard]] ImaginatioDirective translate(
        const CortexDecision& decision,
        const CortexResponse& response,
        const VisualImagination& imagination,
        std::string_view requestGoal = {}) const;

    [[nodiscard]] ImaginatioExecutionResult execute(
        const ImaginatioDirective& directive,
        VisualImagination& imagination) const;
};

[[nodiscard]] const char* toString(ImaginatioDirectiveKind value) noexcept;

} // namespace tatarus::cortex
