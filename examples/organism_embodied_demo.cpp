#include <tatarus/organism.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

tatarus::Experience createRoverExperience(
    std::uint64_t step,
    double slopeAngleDeg,
    double speedMps,
    double batteryLevel) {
    tatarus::Experience exp;
    exp.timestampNs = step * 100'000'000ULL; // 100 ms per step

    // Calculate mechanical joint load based on terrain slope and speed
    const double slopeRad = slopeAngleDeg * 3.141592653589793 / 180.0;
    const double requiredTorqueNm = 5.0 + 35.0 * std::sin(slopeRad) * (speedMps / 1.5);
    const double jointVelocityRadPerS = speedMps * 4.0;

    exp.body.battery = batteryLevel;
    exp.body.powerDraw = 25.0 + 4.0 * (requiredTorqueNm * jointVelocityRadPerS);
    exp.body.balance = std::cos(slopeRad);

    for (std::uint32_t j = 0; j < 4; ++j) {
        tatarus::JointState js;
        js.id = j;
        js.position = step * 0.1;
        js.velocity = jointVelocityRadPerS;
        js.torque = requiredTorqueNm;
        exp.body.joints.push_back(js);
    }

    exp.imu.acceleration = {std::sin(slopeRad) * 9.81, 0.0, std::cos(slopeRad) * 9.81};
    exp.environment.temperature = 20.0;
    exp.environment.light = 0.8;
    exp.environment.novelty = 0.05;
    return exp;
}

void printOrganismTelemetry(const std::string& phase, std::uint64_t step, const tatarus::organism::OrganismTelemetry& t) {
    std::cout << "\n======================================================================\n";
    std::cout << " [TATARUS 4 SYNTHETIC ORGANISM] " << phase << " | Step " << step
              << " (" << std::fixed << std::setprecision(1) << t.simulatedTimeSeconds << "s)\n";
    std::cout << "======================================================================\n";

    std::cout << " CIRCULATION: Vol=" << std::setprecision(2) << t.circulation.totalBloodVolumeL << " L | "
              << "MAP=" << std::setprecision(1) << t.circulation.meanArterialPressureMmHg << " mmHg ("
              << t.circulation.systolicPressureMmHg << "/" << t.circulation.diastolicPressureMmHg << ") | "
              << "SaO2=" << std::setprecision(1) << (t.circulation.arterialOxygenSaturation * 100.0) << "% | "
              << "pH=" << std::setprecision(2) << t.circulation.arterialPh << " | "
              << "K+=" << std::setprecision(1) << t.circulation.plasmaKMm << " mM\n";

    std::cout << " HEART:       HR=" << std::setprecision(0) << t.heart.heartRateBpm << " bpm | "
              << "SV=" << std::setprecision(1) << t.heart.strokeVolumeMl << " mL | "
              << "CO=" << std::setprecision(2) << t.heart.cardiacOutputLPerMin << " L/min | "
              << "EF=" << std::setprecision(0) << (t.heart.ejectionFractionLv * 100.0) << "% | "
              << "MVO2=" << std::setprecision(1) << t.heart.myocardialO2ConsumptionMlPerMin << " mL/min\n";

    std::cout << " LUNG:        RR=" << std::setprecision(0) << t.lung.respirationRateBpm << " bpm | "
              << "VT=" << std::setprecision(2) << t.lung.tidalVolumeL << " L | "
              << "PAO2=" << std::setprecision(1) << t.lung.alveolarPo2MmHg << " mmHg | "
              << "O2 Uptake=" << std::setprecision(0) << t.lung.o2UptakeRateMlPerMin << " mL/min\n";

    std::cout << " KIDNEY:      GFR=" << std::setprecision(1) << t.kidney.glomerularFiltrationRateMlPerMin << " mL/min | "
              << "Urine=" << std::setprecision(2) << t.kidney.urineOutputRateMlPerMin << " mL/min | "
              << "Renin=" << std::setprecision(2) << t.kidney.reninSecretionRate << "x baseline\n";

    std::cout << " INTEROCEPTION: Visceral Distress=" << std::setprecision(2) << t.interoception.visceralDistress
              << " | Cardio Load=" << t.interoception.cardiovascularLoad
              << " | Hypoxia=" << t.interoception.respiratoryHypoxia
              << " | Symp/Para=" << t.interoception.sympatheticTone << "/" << t.interoception.parasympatheticTone << "\n";
}

} // namespace

int main() {
    std::cout << "======================================================================\n";
    std::cout << " TATARUS 4 EMBODIED SYNTHETIC ORGANISM DEMONSTRATION\n";
    std::cout << " Multi-Organ Closed-Loop: Brain + Circulation + Heart + Lung + Kidney\n";
    std::cout << "======================================================================\n";

    tatarus::organism::SyntheticOrganism organism;

    // Phase 1: Earth Atmosphere, Flat terrain cruising
    tatarus::organism::AtmosphericEnvironment earthEnv;
    std::cout << "\n>>> Starting Phase 1: Flat Terrestrial Terrain (Baseline Homeostasis)\n";
    for (std::uint64_t step = 1; step <= 20; ++step) {
        const auto exp = createRoverExperience(step, 0.0, 1.0, 0.98);
        organism.step(exp, earthEnv, 0.1);
    }
    printOrganismTelemetry("Phase 1 - Flat Cruising", 20, organism.telemetry());

    // Phase 2: Steep Hill Climb (Heavy Motor Torque & Metabolic Spike)
    std::cout << "\n>>> Starting Phase 2: 25-Degree Steep Incline (High Somatic Load)\n";
    for (std::uint64_t step = 21; step <= 50; ++step) {
        const auto exp = createRoverExperience(step, 25.0, 1.2, 0.92);
        organism.step(exp, earthEnv, 0.1);
    }
    printOrganismTelemetry("Phase 2 - Steep Climb Peak", 50, organism.telemetry());

    // Phase 3: Transition to Martian Atmosphere
    tatarus::organism::AtmosphericEnvironment marsEnv;
    marsEnv.totalPressureKPa = 0.636; // 0.6 kPa
    marsEnv.o2Fraction = 0.0013;      // 0.13% O2
    marsEnv.co2Fraction = 0.953;      // 95.3% CO2
    marsEnv.temperatureC = -45.0;

    std::cout << "\n>>> Starting Phase 3: Simulated Martian Atmospheric Exposure (Hypoxic Stress)\n";
    for (std::uint64_t step = 51; step <= 80; ++step) {
        const auto exp = createRoverExperience(step, 0.0, 0.5, 0.85);
        organism.step(exp, marsEnv, 0.1);
    }
    printOrganismTelemetry("Phase 3 - Mars Atmosphere", 80, organism.telemetry());

    std::cout << "\n[SUCCESS] TATARUS 4 Synthetic Organism demonstration completed cleanly.\n";
    return 0;
}
