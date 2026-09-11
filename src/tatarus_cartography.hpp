#pragma once

#include "tatarus/cartography.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace tatarus::detail {

class EnvironmentCartographer {
public:
    explicit EnvironmentCartographer(CartographyConfig config = {});
    ~EnvironmentCartographer();

    EnvironmentCartographer(const EnvironmentCartographer&) = delete;
    EnvironmentCartographer& operator=(const EnvironmentCartographer&) = delete;
    EnvironmentCartographer(EnvironmentCartographer&&) noexcept;
    EnvironmentCartographer& operator=(EnvironmentCartographer&&) noexcept;

    CartographyUpdate integrate(const ScannerFrame& frame);
    [[nodiscard]] CartographySummary summary(EnvironmentId environmentId) const;
    [[nodiscard]] std::optional<MapVoxel> voxelAt(
        EnvironmentId environmentId,
        const std::array<double, 3>& worldPositionMeters) const;
    [[nodiscard]] std::vector<MapVoxel> voxels(EnvironmentId environmentId) const;
    [[nodiscard]] std::string json(EnvironmentId environmentId) const;

    void clear(EnvironmentId environmentId);
    void clearAll();
    void save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tatarus::detail
