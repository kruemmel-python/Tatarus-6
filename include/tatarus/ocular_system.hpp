#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace tatarus::neuro::vision {

struct OcularConfig {
    double minimumPupilDiameterMm = 2.0;
    double maximumPupilDiameterMm = 8.0;
    double pupilConstrictionTauSeconds = 0.30;
    double pupilDilationTauSeconds = 0.75;
    double lightAdaptationTauSeconds = 1.5;
    double darkAdaptationTauSeconds = 18.0;
    double targetLuminance = 0.18;
    double minimumAdaptationGain = 0.35;
    double maximumAdaptationGain = 4.0;
    double horizontalFieldOfViewDegrees = 90.0;
    double verticalFieldOfViewDegrees = 60.0;
    double maximumGazeDegrees = 18.0;
    double maximumSaccadeSpeedDegreesPerSecond = 420.0;
    double saccadeDeadbandDegrees = 1.25;

    void validate() const;
};

struct OcularTelemetry {
    bool available = false;
    double meanLuminance = 0.0;
    double pupilDiameterMm = 4.0;
    double retinalAdaptationGain = 1.0;
    double gazeYawDegrees = 0.0;
    double gazePitchDegrees = 0.0;
    double saccadeTargetYawDegrees = 0.0;
    double saccadeTargetPitchDegrees = 0.0;
    double vorYawVelocityDegreesPerSecond = 0.0;
    double vorPitchVelocityDegreesPerSecond = 0.0;
    bool saccadeActive = false;
};

struct OcularFrame {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<double> retinalRgb;
    OcularTelemetry telemetry;
};

class OcularSystem {
public:
    explicit OcularSystem(OcularConfig config = {});

    [[nodiscard]] OcularFrame process(
        std::size_t width,
        std::size_t height,
        std::span<const double> interleavedRgb,
        double dtSeconds,
        std::array<double, 3> vorEyeVelocityDegPerSecond = {});

    void reset() noexcept;
    [[nodiscard]] const OcularConfig& config() const noexcept { return config_; }
    [[nodiscard]] const OcularTelemetry& telemetry() const noexcept { return telemetry_; }

private:
    OcularConfig config_;
    OcularTelemetry telemetry_;
};

} // namespace tatarus::neuro::vision
