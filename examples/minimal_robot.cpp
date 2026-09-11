#include <tatarus/sdk.hpp>

#include <iomanip>
#include <iostream>

namespace {

tatarus::Experience state(std::uint64_t tick, double marker, double light) {
    tatarus::Experience e;
    e.timestampNs = tick * 1'000'000ULL;
    e.vision = {marker, marker * 0.7, -marker * 0.4, light};
    e.environment.light = light;
    e.body.battery = 0.9;
    e.imu.acceleration = {marker * 2.0, 0.0, 9.81};
    return e;
}

}

int main() {
    tatarus::RobotMind mind;

    std::uint64_t tick = 1;
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (const auto [marker, light] : {std::pair{-0.8, 0.2}, {0.1, 0.5}, {0.9, 0.9}}) {
            const auto result = mind.observe(state(tick++, marker, light));
            std::cout << "cycle=" << std::setw(2) << cycle
                      << " assembly=" << result.assemblyId;
            if (result.prediction.available) {
                std::cout << " next=" << result.prediction.expectedAssemblyId
                          << " conf=" << std::fixed << std::setprecision(2)
                          << result.prediction.confidence;
            }
            std::cout << " transitions=" << result.metrics.learnedTransitions << '\n';
        }
    }

    std::cout << "\nFinal state:\n" << mind.stateJson();
    return 0;
}
