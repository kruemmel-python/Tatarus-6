#include "tatarus/vestibular_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tatarus::neuro::vestibular {
namespace {

constexpr double kRadiansToDegrees = 57.295779513082320876;

double clampUnit(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, -1.0, 1.0);
}

double norm(const std::array<double, 3>& value) {
    return std::sqrt(value[0] * value[0] + value[1] * value[1]
        + value[2] * value[2]);
}

double smoothingAlpha(double dtSeconds, double tauSeconds) {
    return 1.0 - std::exp(-dtSeconds / std::max(1e-6, tauSeconds));
}

void appendSignedPopulation(std::vector<double>& events, double value) {
    const double bounded = clampUnit(value);
    events.push_back(std::max(0.0, bounded));
    events.push_back(std::max(0.0, -bounded));
}

} // namespace

void VestibularConfig::validate() const {
    const auto positiveFinite = [](double value) {
        return std::isfinite(value) && value > 0.0;
    };
    if (!positiveFinite(gravityMps2)
        || !positiveFinite(canalAdaptationTauSeconds)
        || !positiveFinite(otolithGravityTauSeconds)
        || !positiveFinite(maximumAngularVelocityRadPerSecond)
        || !positiveFinite(maximumLinearAccelerationG)
        || !std::isfinite(vorGain) || vorGain < 0.0 || vorGain > 1.5) {
        throw std::invalid_argument("Invalid vestibular system configuration");
    }
}

VestibularSystem::VestibularSystem(VestibularConfig config)
    : config_(std::move(config)) {
    config_.validate();
}

VestibularPercept VestibularSystem::step(const ImuState& imu, double dtSeconds) {
    VestibularPercept percept;
    dtSeconds = std::clamp(
        std::isfinite(dtSeconds) ? dtSeconds : 0.02, 0.001, 0.25);

    const double accelerationNorm = norm(imu.acceleration);
    const double rotationNorm = norm(imu.rotation);
    // A default-constructed ImuState represents "sensor not supplied" in the
    // public SDK. Do not reinterpret it as physical free fall.
    if (accelerationNorm < 1e-9 && rotationNorm < 1e-9) {
        return percept;
    }
    percept.available = true;

    if (!gravityInitialized_) {
        if (accelerationNorm > 0.25 * config_.gravityMps2
            && accelerationNorm < 1.75 * config_.gravityMps2) {
            gravityEstimate_ = imu.acceleration;
        } else {
            gravityEstimate_ = {0.0, 0.0, config_.gravityMps2};
        }
        gravityInitialized_ = true;
    }

    const double canalAlpha = smoothingAlpha(
        dtSeconds, config_.canalAdaptationTauSeconds);
    const double gravityAlpha = smoothingAlpha(
        dtSeconds, config_.otolithGravityTauSeconds);

    for (std::size_t axis = 0; axis < 3U; ++axis) {
        // Semicircular canals are approximated as a high-pass response: a
        // slowly adapting cupula baseline is removed from angular velocity.
        canalAdaptation_[axis] += canalAlpha
            * (imu.rotation[axis] - canalAdaptation_[axis]);
        percept.canal[axis] = clampUnit(
            (imu.rotation[axis] - canalAdaptation_[axis])
            / config_.maximumAngularVelocityRadPerSecond);

        gravityEstimate_[axis] += gravityAlpha
            * (imu.acceleration[axis] - gravityEstimate_[axis]);
        percept.gravity[axis] = gravityEstimate_[axis];
        percept.linearAcceleration[axis] =
            imu.acceleration[axis] - gravityEstimate_[axis];
        percept.tilt[axis] = clampUnit(
            gravityEstimate_[axis] / config_.gravityMps2);
        percept.vorEyeVelocityDegPerSecond[axis] =
            -config_.vorGain * imu.rotation[axis] * kRadiansToDegrees;
    }

    percept.angularMotion = clampUnit(
        rotationNorm / config_.maximumAngularVelocityRadPerSecond);
    percept.linearMotion = clampUnit(
        norm(percept.linearAcceleration)
        / (config_.maximumLinearAccelerationG * config_.gravityMps2));
    percept.gravityConfidence = std::exp(
        -2.0 * std::abs(accelerationNorm - config_.gravityMps2)
        / config_.gravityMps2);

    // 18 afferents: +/- populations for three canal axes, three linear
    // acceleration axes and three tilt/gravity axes.
    percept.vestibularNerveEvents.reserve(18U);
    for (double value : percept.canal) appendSignedPopulation(percept.vestibularNerveEvents, value);
    for (double value : percept.linearAcceleration) {
        appendSignedPopulation(
            percept.vestibularNerveEvents,
            value / (config_.maximumLinearAccelerationG * config_.gravityMps2));
    }
    for (double value : percept.tilt) appendSignedPopulation(percept.vestibularNerveEvents, value);
    return percept;
}

void VestibularSystem::reset() noexcept {
    canalAdaptation_.fill(0.0);
    gravityEstimate_.fill(0.0);
    gravityInitialized_ = false;
}

} // namespace tatarus::neuro::vestibular
