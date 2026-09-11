#include "tatarus/ocular_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tatarus::neuro::vision {
namespace {

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

double luminance(std::span<const double> rgb, std::size_t pixel) {
    return 0.2126 * clamp01(rgb[pixel * 3U])
        + 0.7152 * clamp01(rgb[pixel * 3U + 1U])
        + 0.0722 * clamp01(rgb[pixel * 3U + 2U]);
}

double smoothingAlpha(double dtSeconds, double tauSeconds) {
    return 1.0 - std::exp(-dtSeconds / std::max(1e-6, tauSeconds));
}

double approach(double current, double target, double maximumDelta) {
    return current + std::clamp(target - current, -maximumDelta, maximumDelta);
}

double bilinearChannel(
    std::span<const double> rgb,
    std::size_t width,
    std::size_t height,
    double x,
    double y,
    std::size_t channel) {
    x = std::clamp(x, 0.0, static_cast<double>(width - 1U));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1U));
    const auto x0 = static_cast<std::size_t>(std::floor(x));
    const auto y0 = static_cast<std::size_t>(std::floor(y));
    const auto x1 = std::min(width - 1U, x0 + 1U);
    const auto y1 = std::min(height - 1U, y0 + 1U);
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);
    const auto sample = [&](std::size_t sx, std::size_t sy) {
        return clamp01(rgb[(sy * width + sx) * 3U + channel]);
    };
    const double top = (1.0 - fx) * sample(x0, y0) + fx * sample(x1, y0);
    const double bottom = (1.0 - fx) * sample(x0, y1) + fx * sample(x1, y1);
    return (1.0 - fy) * top + fy * bottom;
}

} // namespace

void OcularConfig::validate() const {
    const auto positiveFinite = [](double value) {
        return std::isfinite(value) && value > 0.0;
    };
    if (!positiveFinite(minimumPupilDiameterMm)
        || !positiveFinite(maximumPupilDiameterMm)
        || maximumPupilDiameterMm <= minimumPupilDiameterMm
        || !positiveFinite(pupilConstrictionTauSeconds)
        || !positiveFinite(pupilDilationTauSeconds)
        || !positiveFinite(lightAdaptationTauSeconds)
        || !positiveFinite(darkAdaptationTauSeconds)
        || !positiveFinite(targetLuminance)
        || !positiveFinite(minimumAdaptationGain)
        || !positiveFinite(maximumAdaptationGain)
        || maximumAdaptationGain < minimumAdaptationGain
        || !positiveFinite(horizontalFieldOfViewDegrees)
        || !positiveFinite(verticalFieldOfViewDegrees)
        || !positiveFinite(maximumGazeDegrees)
        || !positiveFinite(maximumSaccadeSpeedDegreesPerSecond)
        || !positiveFinite(saccadeDeadbandDegrees)) {
        throw std::invalid_argument("Invalid ocular system configuration");
    }
}

OcularSystem::OcularSystem(OcularConfig config)
    : config_(std::move(config)) {
    config_.validate();
    telemetry_.pupilDiameterMm = 0.5
        * (config_.minimumPupilDiameterMm + config_.maximumPupilDiameterMm);
}

OcularFrame OcularSystem::process(
    std::size_t width,
    std::size_t height,
    std::span<const double> rgb,
    double dtSeconds,
    std::array<double, 3> vorEyeVelocityDegPerSecond) {
    if (width < 2U || height < 2U || rgb.size() != width * height * 3U) {
        throw std::invalid_argument("Ocular system received an invalid RGB frame");
    }
    dtSeconds = std::clamp(
        std::isfinite(dtSeconds) ? dtSeconds : 0.02, 0.001, 0.25);

    double mean = 0.0;
    for (std::size_t pixel = 0; pixel < width * height; ++pixel) {
        mean += luminance(rgb, pixel);
    }
    mean /= static_cast<double>(width * height);

    // Pupillary light reflex: bright scenes constrict, dark scenes dilate.
    const double normalizedLight = clamp01(std::sqrt(mean));
    const double targetPupil = config_.maximumPupilDiameterMm
        - normalizedLight
            * (config_.maximumPupilDiameterMm - config_.minimumPupilDiameterMm);
    const double pupilTau = targetPupil < telemetry_.pupilDiameterMm
        ? config_.pupilConstrictionTauSeconds
        : config_.pupilDilationTauSeconds;
    telemetry_.pupilDiameterMm += smoothingAlpha(dtSeconds, pupilTau)
        * (targetPupil - telemetry_.pupilDiameterMm);

    // Retina-like gain control. Darkness adapts deliberately slower than
    // bright-light adaptation, matching the asymmetric biological response.
    const double desiredGain = std::clamp(
        config_.targetLuminance / std::max(0.015, mean),
        config_.minimumAdaptationGain,
        config_.maximumAdaptationGain);
    const double adaptationTau = desiredGain > telemetry_.retinalAdaptationGain
        ? config_.darkAdaptationTauSeconds
        : config_.lightAdaptationTauSeconds;
    telemetry_.retinalAdaptationGain += smoothingAlpha(dtSeconds, adaptationTau)
        * (desiredGain - telemetry_.retinalAdaptationGain);

    // Bottom-up saliency centroid drives bounded saccades. Contrast rather
    // than absolute brightness is used so a uniformly bright scene does not
    // continuously drag the gaze.
    double weightSum = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t pixel = y * width + x;
            const double contrast = std::abs(luminance(rgb, pixel) - mean);
            const double weight = contrast * contrast;
            weightSum += weight;
            weightedX += weight * (static_cast<double>(x) + 0.5);
            weightedY += weight * (static_cast<double>(y) + 0.5);
        }
    }
    if (weightSum > 1e-10) {
        const double nx = weightedX / weightSum / static_cast<double>(width) - 0.5;
        const double ny = weightedY / weightSum / static_cast<double>(height) - 0.5;
        telemetry_.saccadeTargetYawDegrees = std::clamp(
            nx * config_.horizontalFieldOfViewDegrees,
            -config_.maximumGazeDegrees, config_.maximumGazeDegrees);
        telemetry_.saccadeTargetPitchDegrees = std::clamp(
            -ny * config_.verticalFieldOfViewDegrees,
            -config_.maximumGazeDegrees, config_.maximumGazeDegrees);
    } else {
        telemetry_.saccadeTargetYawDegrees = 0.0;
        telemetry_.saccadeTargetPitchDegrees = 0.0;
    }

    // Vestibulo-ocular reflex first counter-rotates the eyes; the saccadic
    // controller then moves them toward the salient target.
    telemetry_.vorYawVelocityDegreesPerSecond = vorEyeVelocityDegPerSecond[2];
    telemetry_.vorPitchVelocityDegreesPerSecond = vorEyeVelocityDegPerSecond[1];
    telemetry_.gazeYawDegrees += telemetry_.vorYawVelocityDegreesPerSecond * dtSeconds;
    telemetry_.gazePitchDegrees += telemetry_.vorPitchVelocityDegreesPerSecond * dtSeconds;

    const double maxSaccadeStep = config_.maximumSaccadeSpeedDegreesPerSecond * dtSeconds;
    const double yawError = telemetry_.saccadeTargetYawDegrees - telemetry_.gazeYawDegrees;
    const double pitchError = telemetry_.saccadeTargetPitchDegrees - telemetry_.gazePitchDegrees;
    telemetry_.saccadeActive = std::hypot(yawError, pitchError)
        > config_.saccadeDeadbandDegrees;
    if (telemetry_.saccadeActive) {
        telemetry_.gazeYawDegrees = approach(
            telemetry_.gazeYawDegrees,
            telemetry_.saccadeTargetYawDegrees,
            maxSaccadeStep);
        telemetry_.gazePitchDegrees = approach(
            telemetry_.gazePitchDegrees,
            telemetry_.saccadeTargetPitchDegrees,
            maxSaccadeStep);
    }
    telemetry_.gazeYawDegrees = std::clamp(
        telemetry_.gazeYawDegrees, -config_.maximumGazeDegrees, config_.maximumGazeDegrees);
    telemetry_.gazePitchDegrees = std::clamp(
        telemetry_.gazePitchDegrees, -config_.maximumGazeDegrees, config_.maximumGazeDegrees);

    OcularFrame frame;
    frame.width = width;
    frame.height = height;
    frame.retinalRgb.resize(rgb.size());

    const double shiftX = telemetry_.gazeYawDegrees
        / config_.horizontalFieldOfViewDegrees * static_cast<double>(width);
    const double shiftY = -telemetry_.gazePitchDegrees
        / config_.verticalFieldOfViewDegrees * static_cast<double>(height);
    const double pupilAreaRatio = std::pow(
        telemetry_.pupilDiameterMm / 4.0, 2.0);
    // Keep pupil optics influential but bounded; retinal adaptation provides
    // the slower compensating response.
    const double opticalGain = std::clamp(
        std::sqrt(pupilAreaRatio) * telemetry_.retinalAdaptationGain,
        0.20, 5.0);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const double sx = static_cast<double>(x) + shiftX;
            const double sy = static_cast<double>(y) + shiftY;
            const std::size_t pixel = y * width + x;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                frame.retinalRgb[pixel * 3U + channel] = clamp01(
                    bilinearChannel(rgb, width, height, sx, sy, channel)
                    * opticalGain);
            }
        }
    }

    telemetry_.available = true;
    telemetry_.meanLuminance = mean;
    frame.telemetry = telemetry_;
    return frame;
}

void OcularSystem::reset() noexcept {
    telemetry_ = {};
    telemetry_.pupilDiameterMm = 0.5
        * (config_.minimumPupilDiameterMm + config_.maximumPupilDiameterMm);
    telemetry_.retinalAdaptationGain = 1.0;
}

} // namespace tatarus::neuro::vision
