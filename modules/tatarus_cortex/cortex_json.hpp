#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace tatarus::cortex::detail {

[[nodiscard]] CortexConfig parseConfig(std::string_view json);
[[nodiscard]] std::string serializeRequest(const CortexRequest& request);
[[nodiscard]] CortexResponse parseResponse(
    std::string_view json,
    const CortexRequest& request,
    const CortexLimits& limits);
[[nodiscard]] std::string extractChatContent(std::string_view json);
[[nodiscard]] std::vector<std::string> extractModelIds(std::string_view json);
[[nodiscard]] std::string escapeJson(std::string_view value);

} // namespace tatarus::cortex::detail
