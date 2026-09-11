#pragma once

#include "tatarus/cortex.hpp"
#include "tatarus/cortex_config.hpp"

#include <filesystem>
#include <memory>

namespace tatarus::cortex {

[[nodiscard]] std::shared_ptr<ICortexModelClient> makeRecordingCortexClient(
    std::shared_ptr<ICortexModelClient> inner,
    std::filesystem::path path);

[[nodiscard]] std::shared_ptr<ICortexModelClient> makeReplayCortexClient(
    std::filesystem::path path,
    bool strict = true);

} // namespace tatarus::cortex
