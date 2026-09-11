#include <tatarus/organism.hpp>
#include <tatarus/c_api.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "Assertion failed: " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}

tatarus::Experience createTestExperience(std::uint64_t step, double jointLoad) {
    tatarus::Experience exp;
    exp.timestampNs = step * 50'000'000ULL; // 50 ms
    exp.vision = {0.5, 0.2, 0.1, 0.8, 0.4, 0.3, 0.2, 0.1};
    exp.audio = {0.3, 0.1};
    exp.touch = {0.0, 0.0, 0.95, 0.0};
    exp.imu.acceleration = {0.0, 0.0, 9.81};
    exp.imu.rotation = {0.0, 0.0, 0.0};
    exp.environment.light = 0.8;
    exp.environment.temperature = 22.0;
    exp.environment.novelty = 0.05;
    exp.body.battery = 0.95;
    exp.body.powerDraw = 20.0 + jointLoad * 100.0;

    // Add 4 simulated robotic joints under mechanical load
    for (std::uint32_t j = 0; j < 4; ++j) {
        tatarus::JointState js;
        js.id = j;
        js.position = 0.5 * std::sin(step * 0.1 + j);
        js.velocity = jointLoad * 2.0;
        js.torque = jointLoad * 15.0; // Nm
        exp.body.joints.push_back(js);
    }
    return exp;
}

void testClosedLoopOrganismBaseline() {
    tatarus::organism::SyntheticOrganism organism;
    tatarus::organism::AtmosphericEnvironment earthAtmosphere; // 101.3 kPa, 21% O2

    for (std::uint64_t i = 0; i < 40; ++i) {
        const auto exp = createTestExperience(i, 0.0); // resting robot
        const auto res = organism.step(exp, earthAtmosphere, 0.05);
        require(res.metrics.experiences == i + 1, "Brain must record experiences");
    }

    const auto t = organism.telemetry();
    require(t.available, "Organism telemetry must be active");
    std::cout << "DEBUG ORGANISM: MAP=" << t.circulation.meanArterialPressureMmHg << " mmHg, SaO2="
              << t.circulation.arterialOxygenSaturation << ", HR=" << t.heart.heartRateBpm
              << " bpm, Distress=" << t.interoception.visceralDistress << std::endl;
    require(t.circulation.meanArterialPressureMmHg >= 65.0 && t.circulation.meanArterialPressureMmHg <= 150.0,
            "Initial two-second MAP transient must remain compensated");
    require(t.circulation.arterialOxygenSaturation >= 0.90,
            "Terrestrial oxygen saturation should be >90%");
    require(t.heart.heartRateBpm >= 50.0 && t.heart.heartRateBpm <= 110.0,
            "Resting heart rate should be physiological");
    require(t.interoception.visceralDistress < 0.35,
            "Resting visceral distress should be low (<0.35)");
    std::cout << "testClosedLoopOrganismBaseline: PASS" << std::endl;
}

void testAtmosphericResponseEarthVsMars() {
    tatarus::organism::SyntheticOrganism earthOrganism;
    tatarus::organism::SyntheticOrganism marsOrganism;

    tatarus::organism::AtmosphericEnvironment earthAtmosphere; // 101.3 kPa, 21% O2
    tatarus::organism::AtmosphericEnvironment marsAtmosphere;  // Mars: 0.636 kPa, 0.13% O2, 95.3% CO2
    marsAtmosphere.totalPressureKPa = 0.636;
    marsAtmosphere.o2Fraction = 0.0013;
    marsAtmosphere.co2Fraction = 0.953;
    marsAtmosphere.n2Fraction = 0.027;
    marsAtmosphere.temperatureC = -40.0;

    // Run 50 steps on Earth
    for (std::uint64_t i = 0; i < 50; ++i) {
        earthOrganism.step(createTestExperience(i, 0.1), earthAtmosphere, 0.05);
    }

    // Run 50 steps on Mars
    for (std::uint64_t i = 0; i < 50; ++i) {
        marsOrganism.step(createTestExperience(i, 0.1), marsAtmosphere, 0.05);
    }

    const auto earthT = earthOrganism.telemetry();
    const auto marsT = marsOrganism.telemetry();

    require(marsT.lung.alveolarPo2MmHg < earthT.lung.alveolarPo2MmHg,
            "Alveolar PO2 on Mars must be drastically lower than on Earth");
    require(marsT.circulation.arterialOxygenSaturation < earthT.circulation.arterialOxygenSaturation,
            "Martian atmospheric exposure must produce arterial hypoxemia");
    require(marsT.interoception.respiratoryHypoxia > earthT.interoception.respiratoryHypoxia,
            "Hypoxia interoceptive distress signal must activate on Mars");
    require(marsT.interoception.visceralDistress > earthT.interoception.visceralDistress,
            "Martian conditions must trigger high visceral distress in the synthetic organism");
    std::cout << "testAtmosphericResponseEarthVsMars: PASS" << std::endl;
}

void testSomaticLoadSpikeAndCardiacResponse() {
    tatarus::organism::SyntheticOrganism organism;
    tatarus::organism::AtmosphericEnvironment earthAtmosphere;

    tatarus::organism::RobotPhysicalLoad restingLoad{};
    restingLoad.mechanicalPowerW = 6.0;
    restingLoad.cpuGpuPowerW = 8.0;
    tatarus::organism::RobotPhysicalLoad heavyLoad = restingLoad;
    heavyLoad.mechanicalPowerW = 150.0;
    heavyLoad.motorCurrentA = 7.0;

    // 1. Warm-up at low load
    for (std::uint64_t i = 0; i < 30; ++i) {
        organism.stepWithLoad(createTestExperience(i, 0.0), earthAtmosphere, restingLoad, 0.05);
    }
    const double baselineHr = organism.telemetry().heart.heartRateBpm;

    // 2. High mechanical robot load (climbing / heavy torque)
    for (std::uint64_t i = 30; i < 70; ++i) {
        organism.stepWithLoad(createTestExperience(i, 1.0), earthAtmosphere, heavyLoad, 0.05);
    }
    const double stressHr = organism.telemetry().heart.heartRateBpm;
    const double stressCardioLoad = organism.telemetry().interoception.cardiovascularLoad;

    require(stressHr > baselineHr, "High physical robot workload must trigger compensatory cardiac output and heart rate increase");
    require(stressCardioLoad > 0.0, "Cardiovascular load telemetry must register mechanical strain");
    std::cout << "testSomaticLoadSpikeAndCardiacResponse: PASS" << std::endl;
}

void testLongRunningEarthHomeostasis() {
    tatarus::organism::SyntheticOrganism organism;
    tatarus::organism::AtmosphericEnvironment earth{};
    tatarus::organism::RobotPhysicalLoad restingRobot{};
    restingRobot.mechanicalPowerW = 6.0;
    restingRobot.cpuGpuPowerW = 8.0;
    for (uint64_t i = 1; i <= 600; ++i) {
        organism.stepWithLoad(createTestExperience(i, 0.0), earth, restingRobot, 0.10);
    }
    const auto t = organism.telemetry();
    require(t.circulation.meanArterialPressureMmHg > 65.0 &&
            t.circulation.meanArterialPressureMmHg < 140.0,
            "Earth baseline MAP must remain homeostatic over 60 simulated seconds");
    require(t.circulation.arterialOxygenSaturation > 0.90,
            "Earth baseline arterial saturation must remain physiological");
    require(t.circulation.arterialPh > 7.20 && t.circulation.arterialPh < 7.55,
            "Earth baseline arterial pH must remain compensated");
    require(t.circulation.bloodTemperatureC > 36.0 && t.circulation.bloodTemperatureC < 38.5,
            "Earth baseline blood temperature must remain regulated");
    require(t.leftKidney.glomerularFiltrationRateMlPerMin > 20.0 &&
            t.rightKidney.glomerularFiltrationRateMlPerMin > 20.0,
            "Both kidneys must remain perfused in the long-running baseline");
    std::cout << "testLongRunningEarthHomeostasis: PASS" << std::endl;
}

void testSnapshotRebindsComponentThroughput() {
    namespace fs = std::filesystem;
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto snapshot = fs::temp_directory_path()
        / ("tatarus-organism-roundtrip-" + suffix);

    tatarus::organism::SyntheticOrganism source;
    tatarus::organism::AtmosphericEnvironment earth{};
    tatarus::organism::RobotPhysicalLoad load{};
    load.mechanicalPowerW = 12.0;
    load.cpuGpuPowerW = 8.0;
    source.stepWithLoad(createTestExperience(1, 0.2), earth, load, 0.05);
    source.saveSnapshot(snapshot);

    tatarus::organism::SyntheticOrganism restored;
    require(restored.loadSnapshot(snapshot),
        "Organism snapshot must load into a different instance");
    const auto sourceBefore = source.throughputCounters();
    const auto restoredBefore = restored.throughputCounters();

    restored.stepWithLoad(createTestExperience(2, 0.3), earth, load, 0.05);

    const auto sourceAfter = source.throughputCounters();
    const auto restoredAfter = restored.throughputCounters();
    require(sourceAfter.totalLogicalReads() == sourceBefore.totalLogicalReads()
            && sourceAfter.totalLogicalWrites() == sourceBefore.totalLogicalWrites(),
        "Restored body components retained throughput pointers into the source instance");
    require(restoredAfter.totalLogicalReads() > restoredBefore.totalLogicalReads()
            && restoredAfter.totalLogicalWrites() > restoredBefore.totalLogicalWrites(),
        "Restored body components did not update their local throughput counters");

    std::error_code ec;
    fs::remove_all(snapshot, ec);
    std::cout << "testSnapshotRebindsComponentThroughput: PASS" << std::endl;
}

void testOrganismCApi() {
    tatarus_organism* org = tatarus_organism_create(42);
    require(org != nullptr, "tatarus_organism_create must return a valid handle");

    tatarus_observation obs{};
    obs.timestamp_ns = 100000000;
    obs.proximity[0] = 0.5;
    obs.battery = 0.9;
    obs.temperature = 21.0;

    tatarus_atmospheric_environment env{};
    env.total_pressure_kpa = 101.325;
    env.o2_fraction = 0.21;
    env.co2_fraction = 0.0004;
    env.temperature_c = 20.0;

    tatarus_state st{};
    const int stepRes = tatarus_organism_step(org, &obs, &env, 0.05, &st);
    require(stepRes == 1, "tatarus_organism_step must succeed");

    tatarus_organism_telemetry_c telem{};
    const int telemRes = tatarus_organism_get_telemetry(org, &telem);
    require(telemRes == 1, "tatarus_organism_get_telemetry must succeed");
    require(telem.available == 1, "Telemetry must be available");
    require(telem.heart_rate_bpm > 0.0, "Heart rate must be positive");
    require(telem.left_kidney_gfr_ml_per_min > 0.0, "Left kidney must filter");
    require(telem.right_kidney_gfr_ml_per_min > 0.0, "Right kidney must filter");
    require(std::abs(telem.glomerular_filtration_rate_ml_per_min -
                     telem.left_kidney_gfr_ml_per_min -
                     telem.right_kidney_gfr_ml_per_min) < 1e-9,
            "C API aggregate GFR must equal both kidneys");

    require(tatarus_organism_set_renal_function(org, 0.0, 1.0) == 1,
            "Unilateral renal intervention must succeed");
    require(tatarus_organism_step(org, &obs, &env, 0.05, &st) == 1,
            "Organism must continue after unilateral renal intervention");
    require(tatarus_organism_get_telemetry(org, &telem) == 1,
            "Telemetry after renal intervention must succeed");
    require(telem.left_kidney_gfr_ml_per_min == 0.0,
            "Disabled left kidney must report zero GFR through the C API");
    require(telem.right_kidney_gfr_ml_per_min > 0.0,
            "Right kidney must remain active through the C API");

    char jsonBuf[4096];
    const uint64_t len = tatarus_organism_get_json(org, jsonBuf, sizeof(jsonBuf));
    require(len > 0, "tatarus_organism_get_json must export valid JSON");

    tatarus_organism_destroy(org);
    std::cout << "testOrganismCApi: PASS" << std::endl;
}

} // namespace

int main() {
    try {
        testClosedLoopOrganismBaseline();
        testAtmosphericResponseEarthVsMars();
        testSomaticLoadSpikeAndCardiacResponse();
        testLongRunningEarthHomeostasis();
        testSnapshotRebindsComponentThroughput();
        testOrganismCApi();
        std::cout << "TATARUS Synthetic Organism closed-loop tests: ALL PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Organism integration test failed: " << e.what() << std::endl;
        return 1;
    }
}
