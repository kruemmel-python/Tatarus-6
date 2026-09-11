#include "tatarus/ocular_system.hpp"
#include "tatarus/vestibular_system.hpp"
#include "tatarus/visual_pathway.hpp"
#include "tatarus/robot_mind.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace tatarus::neuro;

    vision::OcularSystem eye;
    std::vector<double> dark(8U * 8U * 3U, 0.02);
    std::vector<double> bright(8U * 8U * 3U, 0.95);
    for (int i = 0; i < 80; ++i) {
        [[maybe_unused]] auto frame = eye.process(8, 8, dark, 0.02);
    }
    const double darkPupil = eye.telemetry().pupilDiameterMm;
    const double darkGain = eye.telemetry().retinalAdaptationGain;
    for (int i = 0; i < 80; ++i) {
        [[maybe_unused]] auto frame = eye.process(8, 8, bright, 0.02);
    }
    assert(darkPupil > eye.telemetry().pupilDiameterMm);
    assert(darkGain > eye.telemetry().retinalAdaptationGain);

    vestibular::VestibularSystem innerEar;
    tatarus::ImuState imu;
    imu.acceleration = {0.0, 0.0, 9.80665};
    imu.rotation = {0.0, 0.0, 1.0};
    vestibular::VestibularPercept percept;
    for (int i = 0; i < 20; ++i) percept = innerEar.step(imu, 0.02);
    assert(percept.available);
    assert(percept.vestibularNerveEvents.size() == 18U);
    assert(percept.canal[2] > 0.0);
    assert(percept.vorEyeVelocityDegPerSecond[2] < 0.0);
    assert(percept.gravityConfidence > 0.9);

    const double beforeYaw = eye.telemetry().gazeYawDegrees;
    [[maybe_unused]] auto stabilized = eye.process(
        8, 8, bright, 0.02, percept.vorEyeVelocityDegPerSecond);
    assert(eye.telemetry().vorYawVelocityDegreesPerSecond < 0.0);
    assert(eye.telemetry().gazeYawDegrees <= beforeYaw + 8.5); // bounded even with saccadic competition

    // Multi-object early visual cortex: two spatially separated coloured
    // objects must become two cortical candidates, each with its own object
    // representation rather than one whole-frame descriptor.
    vision::VisualPathway pathway;
    std::vector<double> scene(32U * 32U * 3U, 0.01);
    const auto paint = [&scene](std::size_t x0, std::size_t y0, std::size_t x1,
                                std::size_t y1, std::array<double, 3> color) {
        for (std::size_t y = y0; y < y1; ++y) {
            for (std::size_t x = x0; x < x1; ++x) {
                const std::size_t offset = (y * 32U + x) * 3U;
                scene[offset] = color[0];
                scene[offset + 1U] = color[1];
                scene[offset + 2U] = color[2];
            }
        }
    };
    paint(3, 5, 11, 14, {0.95, 0.08, 0.05});
    paint(21, 18, 29, 28, {0.05, 0.85, 0.15});
    const auto scenePercept = pathway.perceive(32, 32, scene);
    assert(scenePercept.objects.size() >= 2U);
    assert(scenePercept.objectCortexEvents.size()
        == scenePercept.objects.size() * 16U);

    // End-to-end fusion: the public RobotMind receives raw RGB + IMU together.
    tatarus::RobotMindConfig config;
    config.neuronCount = 96;
    config.microstepsPerObservation = 2;
    tatarus::RobotMind mind(config);
    tatarus::Experience experience;
    experience.timestampNs = 20'000'000ULL;
    experience.imu = imu;
    tatarus::VisualFrame camera;
    camera.width = 8;
    camera.height = 8;
    camera.rgb = bright;
    experience.visualFrame = camera;
    const auto result = mind.observe(experience);
    assert(result.metrics.finite);
    assert(mind.biology().available);

    std::cout << "TATARUS ocular/vestibular sensor-fusion tests passed\n";
    return 0;
}
