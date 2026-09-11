#pragma once

#include "tatarus/types.hpp"

#include <optional>

namespace tatarus {

class SensorAdapter {
public:
    virtual ~SensorAdapter() = default;
    [[nodiscard]] virtual Experience sample() = 0;
};

class IdentityFeatureAdapter {
public:
    virtual ~IdentityFeatureAdapter() = default;
    [[nodiscard]] virtual std::optional<IdentityObservation> sampleIdentity() = 0;
};

} // namespace tatarus
