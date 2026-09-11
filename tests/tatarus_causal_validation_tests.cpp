#include "tatarus_organism.hpp"
#include "tatarus_conservation.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <vector>

using namespace tatarus;
using namespace tatarus::organism;

namespace {

Experience makeBaselineExperience(uint64_t step) {
    Experience exp;
    exp.timestampNs = step * 50000000ULL;
    exp.vision = {0.5, 0.2, 0.1, 0.8, 0.4, 0.3, 0.2, 0.1};
    exp.audio = {0.3, 0.1};
    exp.touch = {0.0, 0.0, 0.95, 0.0};
    exp.imu.acceleration = {0.0, 0.0, 9.81};
    exp.imu.rotation = {0.0, 0.0, 0.0};
    exp.environment.light = 0.8;
    exp.environment.temperature = 22.0;
    exp.environment.novelty = 0.05;
    exp.body.battery = 0.95;
    exp.body.powerDraw = 25.0;
    return exp;
}

void testHemorrhageCausalChain() {
    std::cout << "[TEST 1] Hemorrhage / Blood Loss Causal Chain..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    // Settle baseline for 50 steps
    for (uint64_t i = 1; i <= 50; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto baseline = organism.telemetry();
    const double baseVol = baseline.circulation.totalBloodVolumeL;
    const double baseMap = baseline.circulation.meanArterialPressureMmHg;
    const double baseSymp = baseline.interoception.sympatheticTone;
    const double baseRenin = baseline.kidney.reninSecretionRate;

    // Induce massive 600 mL hemorrhage
    organism.hemorrhage(600.0);

    // Step for 40 steps to allow baroreflex and RAAS response
    for (uint64_t i = 51; i <= 90; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto postHemorrhage = organism.telemetry();

    std::cout << "  Baseline: Vol=" << baseVol << " L, MAP=" << baseMap << " mmHg, Symp=" << baseSymp << ", Renin=" << baseRenin << std::endl;
    std::cout << "  Post-Bleed: Vol=" << postHemorrhage.circulation.totalBloodVolumeL
              << " L, MAP=" << postHemorrhage.circulation.meanArterialPressureMmHg
              << " mmHg, Symp=" << postHemorrhage.interoception.sympatheticTone
              << ", Renin=" << postHemorrhage.kidney.reninSecretionRate << std::endl;

    assert(postHemorrhage.circulation.totalBloodVolumeL < baseVol);
    assert(postHemorrhage.circulation.meanArterialPressureMmHg < baseMap);
    assert(postHemorrhage.interoception.sympatheticTone > baseSymp);
    assert(postHemorrhage.kidney.reninSecretionRate > baseRenin);
    std::cout << "  -> PASSED (Volume down -> MAP down -> Sympathetic up -> Renin up)\n" << std::endl;
}

void testHypoxiaCausalChain() {
    std::cout << "[TEST 2] Hypoxia Causal Chain..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    for (uint64_t i = 1; i <= 40; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto baseline = organism.telemetry();

    // Switch to hypoxic environment: FiO2 = 10%
    AtmosphericEnvironment hypoxicAtmo = earthAtmo;
    hypoxicAtmo.o2Fraction = 0.10;

    for (uint64_t i = 41; i <= 90; ++i) {
        organism.step(makeBaselineExperience(i), hypoxicAtmo, 0.05);
    }
    const auto postHypoxia = organism.telemetry();

    std::cout << "  Baseline: PaO2=" << baseline.circulation.arterialPo2MmHg << " mmHg, SaO2=" << (baseline.circulation.arterialOxygenSaturation * 100)
              << "%, RR=" << baseline.lung.respirationRateBpm << "/min, Distress=" << baseline.interoception.visceralDistress << std::endl;
    std::cout << "  Hypoxia:  PaO2=" << postHypoxia.circulation.arterialPo2MmHg << " mmHg, SaO2=" << (postHypoxia.circulation.arterialOxygenSaturation * 100)
              << "%, RR=" << postHypoxia.lung.respirationRateBpm << "/min, Distress=" << postHypoxia.interoception.visceralDistress << std::endl;

    assert(postHypoxia.circulation.arterialPo2MmHg < baseline.circulation.arterialPo2MmHg);
    assert(postHypoxia.circulation.arterialOxygenSaturation < baseline.circulation.arterialOxygenSaturation);
    assert(postHypoxia.lung.respirationRateBpm > baseline.lung.respirationRateBpm);
    assert(postHypoxia.interoception.visceralDistress > baseline.interoception.visceralDistress);
    std::cout << "  -> PASSED (FiO2 down -> PaO2 down -> Chemoreceptors trigger hyperventilation -> Distress up)\n" << std::endl;
}

void testHypercapniaCausalChain() {
    std::cout << "[TEST 3] Hypercapnia & Acidosis Causal Chain..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    for (uint64_t i = 1; i <= 40; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto baseline = organism.telemetry();

    // Elevate CO2 in atmosphere: 5% CO2
    AtmosphericEnvironment hypercapnicAtmo = earthAtmo;
    hypercapnicAtmo.co2Fraction = 0.05;

    for (uint64_t i = 41; i <= 90; ++i) {
        organism.step(makeBaselineExperience(i), hypercapnicAtmo, 0.05);
    }
    const auto postHypercapnia = organism.telemetry();

    std::cout << "  Baseline: PaCO2=" << baseline.circulation.arterialPco2MmHg << " mmHg, pH=" << baseline.circulation.arterialPh
              << ", MinuteVentilation=" << baseline.lung.minuteVentilationLPerMin << " L/min" << std::endl;
    std::cout << "  Hypercapnia: PaCO2=" << postHypercapnia.circulation.arterialPco2MmHg << " mmHg, pH=" << postHypercapnia.circulation.arterialPh
              << ", MinuteVentilation=" << postHypercapnia.lung.minuteVentilationLPerMin << " L/min" << std::endl;

    assert(postHypercapnia.circulation.arterialPco2MmHg > baseline.circulation.arterialPco2MmHg);
    assert(postHypercapnia.circulation.arterialPh < baseline.circulation.arterialPh);
    assert(postHypercapnia.lung.minuteVentilationLPerMin > baseline.lung.minuteVentilationLPerMin);
    std::cout << "  -> PASSED (FiCO2 up -> PaCO2 up -> pH down -> Central chemoreceptors drive ventilation up)\n" << std::endl;
}

void testHyperkalemiaElectrophysiology() {
    std::cout << "[TEST 4] Hyperkalemia Electrophysiology..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    for (uint64_t i = 1; i <= 30; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto baseline = organism.telemetry();

    // Infuse hyperkalemic solution (e.g. 150 mL with 30 mM K+)
    SoluteProfile highK{};
    highK.kMm = 30.0;
    organism.infuseFluid(150.0, highK);

    for (uint64_t i = 31; i <= 70; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto postInfusion = organism.telemetry();

    std::cout << "  Baseline: Plasma K=" << baseline.circulation.plasmaKMm << " mM, Ventricular Vm=" << baseline.heart.ventricularMyocardiumPotentialMv << " mV" << std::endl;
    std::cout << "  Post-High-K: Plasma K=" << postInfusion.circulation.plasmaKMm << " mM, Ventricular Vm=" << postInfusion.heart.ventricularMyocardiumPotentialMv << " mV" << std::endl;

    assert(postInfusion.circulation.plasmaKMm > baseline.circulation.plasmaKMm);
    assert(postInfusion.heart.ventricularMyocardiumPotentialMv > baseline.heart.ventricularMyocardiumPotentialMv); // Depolarized resting potential
    std::cout << "  -> PASSED (K+ infusion -> Plasma K+ up -> Resting membrane potential depolarizes via Nernst)\n" << std::endl;
}

void testRenalHypoperfusionAndRAAS() {
    std::cout << "[TEST 5] Renal Hypoperfusion & Starling GFR..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    for (uint64_t i = 1; i <= 40; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto baseline = organism.telemetry();

    // Bleed 800 mL to drop arterial pressure drastically
    organism.hemorrhage(800.0);

    for (uint64_t i = 41; i <= 90; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto postHypoperfusion = organism.telemetry();

    std::cout << "  Baseline: MAP=" << baseline.circulation.meanArterialPressureMmHg << " mmHg, GFR=" << baseline.kidney.glomerularFiltrationRateMlPerMin
              << " mL/min, Renin=" << baseline.kidney.reninSecretionRate << std::endl;
    std::cout << "  Hypoperf: MAP=" << postHypoperfusion.circulation.meanArterialPressureMmHg << " mmHg, GFR=" << postHypoperfusion.kidney.glomerularFiltrationRateMlPerMin
              << " mL/min, Renin=" << postHypoperfusion.kidney.reninSecretionRate << std::endl;

    assert(postHypoperfusion.kidney.glomerularFiltrationRateMlPerMin < baseline.kidney.glomerularFiltrationRateMlPerMin);
    assert(postHypoperfusion.kidney.reninSecretionRate > baseline.kidney.reninSecretionRate);
    std::cout << "  -> PASSED (Perfusion down -> Starling GFR down -> Macula densa NaCl down -> Renin up)\n" << std::endl;
}

void testRobotMechanicalLoadCoupling() {
    std::cout << "[TEST 6] Robot Mechanical Workload & Metabolic Coupling..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    // Step with idle load
    RobotPhysicalLoad idleLoad{};
    idleLoad.mechanicalPowerW = 5.0;
    for (uint64_t i = 1; i <= 40; ++i) {
        organism.stepWithLoad(makeBaselineExperience(i), earthAtmo, idleLoad, 0.05);
    }
    const auto idleTel = organism.telemetry();

    // Step with heavy motor workload (150 W)
    RobotPhysicalLoad heavyLoad{};
    heavyLoad.mechanicalPowerW = 150.0;
    heavyLoad.motorCurrentA = 6.25;
    for (uint64_t i = 41; i <= 90; ++i) {
        organism.stepWithLoad(makeBaselineExperience(i), earthAtmo, heavyLoad, 0.05);
    }
    const auto heavyTel = organism.telemetry();

    std::cout << "  Idle (5W):   CO=" << idleTel.heart.cardiacOutputLPerMin << " L/min, MinuteVentilation=" << idleTel.lung.minuteVentilationLPerMin
              << " L/min, O2 Uptake=" << idleTel.lung.o2UptakeRateMlPerMin << " mL/min" << std::endl;
    std::cout << "  Heavy (150W): CO=" << heavyTel.heart.cardiacOutputLPerMin << " L/min, MinuteVentilation=" << heavyTel.lung.minuteVentilationLPerMin
              << " L/min, O2 Uptake=" << heavyTel.lung.o2UptakeRateMlPerMin << " mL/min" << std::endl;

    assert(heavyTel.lung.o2UptakeRateMlPerMin > idleTel.lung.o2UptakeRateMlPerMin);
    assert(heavyTel.lung.minuteVentilationLPerMin > idleTel.lung.minuteVentilationLPerMin);
    std::cout << "  -> PASSED (Motor mechanical work -> Metabolic O2 demand up -> Ventilation & Cardiac workload up)\n" << std::endl;
}

void testMarsEnvironmentEmergence() {
    std::cout << "[TEST 7] Mars Atmospheric Environment..." << std::endl;
    SyntheticOrganism organism;

    // Atmospheric properties of Mars: 0.636 kPa, 0.13% O2, 95.32% CO2, -60 deg C, 50 ppm dust
    AtmosphericEnvironment marsAtmo;
    marsAtmo.totalPressureKPa = 0.636;
    marsAtmo.o2Fraction = 0.0013;
    marsAtmo.co2Fraction = 0.9532;
    marsAtmo.n2Fraction = 0.026;
    marsAtmo.temperatureC = -60.0;
    marsAtmo.relativeHumidity = 0.03;
    marsAtmo.dustPpm = 50.0;

    for (uint64_t i = 1; i <= 60; ++i) {
        organism.step(makeBaselineExperience(i), marsAtmo, 0.05);
    }
    const auto marsTel = organism.telemetry();

    std::cout << "  Mars response: Alveolar PO2=" << marsTel.lung.alveolarPo2MmHg << " mmHg, SaO2=" << (marsTel.circulation.arterialOxygenSaturation * 100)
              << "%, Airway Resistance=" << marsTel.lung.airwayResistance << ", Visceral Distress=" << marsTel.interoception.visceralDistress << std::endl;

    assert(marsTel.lung.alveolarPo2MmHg < 5.0); // Extreme hypoxic atmosphere
    assert(marsTel.circulation.arterialOxygenSaturation < 0.20);
    assert(marsTel.lung.airwayResistance > 1.5); // Increased by dust filter accumulation
    assert(marsTel.interoception.visceralDistress > 0.40);
    std::cout << "  -> PASSED (Pure environmental physics: low total pressure + dust -> acute desaturation & distress)\n" << std::endl;
}

void testBodyToNervousSystemSupplyFeedback() {
    std::cout << "[TEST 8] Blood Supply & Interoception -> Nervous Tissue Feedback..." << std::endl;
    SyntheticOrganism earthOrganism;
    SyntheticOrganism hypoxicOrganism;
    AtmosphericEnvironment earth{};
    AtmosphericEnvironment hypoxic = earth;
    hypoxic.o2Fraction = 0.05;

    for (uint64_t i = 1; i <= 120; ++i) {
        const Experience input = makeBaselineExperience(i);
        earthOrganism.step(input, earth, 0.05);
        hypoxicOrganism.step(input, hypoxic, 0.05);
    }

    const auto earthBrain = earthOrganism.mind().biology();
    const auto hypoxicBrain = hypoxicOrganism.mind().biology();
    const auto earthBody = earthOrganism.telemetry();
    const auto hypoxicBody = hypoxicOrganism.telemetry();
    std::cout << "  Earth: SaO2=" << earthBody.circulation.arterialOxygenSaturation
              << ", tissue O2=" << earthBrain.oxygen << std::endl;
    std::cout << "  Hypoxia: SaO2=" << hypoxicBody.circulation.arterialOxygenSaturation
              << ", tissue O2=" << hypoxicBrain.oxygen << std::endl;

    assert(hypoxicBody.circulation.arterialOxygenSaturation <
           earthBody.circulation.arterialOxygenSaturation);
    assert(hypoxicBrain.oxygen < earthBrain.oxygen);
    assert(hypoxicOrganism.interoceptionState().hypoxia >
           earthOrganism.interoceptionState().hypoxia);
    std::cout << "  -> PASSED (Atmosphere -> lung -> blood -> neural oxygen + interoception)\n" << std::endl;
}

void testDeterministicSnapshotRoundtrip() {
    std::cout << "[TEST 10] Deterministic Snapshot Roundtrip..." << std::endl;
    const AtmosphericEnvironment earthAtmo{};

    // Run A: 100 continuous steps
    SyntheticOrganism orgA;
    for (uint64_t i = 1; i <= 100; ++i) {
        orgA.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto telA = orgA.telemetry();

    // Run B: 50 steps -> Snapshot -> Restore -> 50 steps
    SyntheticOrganism orgB;
    for (uint64_t i = 1; i <= 50; ++i) {
        orgB.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto snapshotDir = std::filesystem::temp_directory_path() /
        "tatarus-organism-snapshot-validation";
    std::filesystem::remove_all(snapshotDir);
    orgB.saveSnapshot(snapshotDir);

    SyntheticOrganism orgC;
    const bool restored = orgC.loadSnapshot(snapshotDir);
    assert(restored);
    assert(orgB.organismJson() == orgC.organismJson());
    assert(orgB.mind().stateJson() == orgC.mind().stateJson());

    for (uint64_t i = 51; i <= 100; ++i) {
        orgC.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }
    const auto telC = orgC.telemetry();

    std::cout << "  Continuous Run Step=" << telA.stepCount << ", MAP=" << telA.circulation.meanArterialPressureMmHg << " mmHg, HR=" << telA.heart.heartRateBpm << " bpm" << std::endl;
    std::cout << "  Restored Run   Step=" << telC.stepCount << ", MAP=" << telC.circulation.meanArterialPressureMmHg << " mmHg, HR=" << telC.heart.heartRateBpm << " bpm" << std::endl;

    assert(telA.stepCount == telC.stepCount);
    assert(std::abs(telA.circulation.meanArterialPressureMmHg - telC.circulation.meanArterialPressureMmHg) < 1e-4);
    assert(std::abs(telA.heart.heartRateBpm - telC.heart.heartRateBpm) < 1e-4);
    assert(orgA.organismJson() == orgC.organismJson());
    assert(orgA.mind().stateJson() == orgC.mind().stateJson());
    std::filesystem::remove_all(snapshotDir);
    std::cout << "  -> PASSED (Snapshot save and load preserves state)\n" << std::endl;
}

void testConservationAuditLedger() {
    std::cout << "[PROPERTY TEST] ConservationLedger Audit..." << std::endl;
    SyntheticOrganism organism;
    const AtmosphericEnvironment earthAtmo{};

    for (uint64_t i = 1; i <= 100; ++i) {
        organism.step(makeBaselineExperience(i), earthAtmo, 0.05);
    }

    const auto audit = organism.auditConservation(1e-3);
    std::cout << "  Conservation Audit: Balanced=" << (audit.balanced ? "YES" : "NO")
              << ", Max Discrepancy=" << audit.maxDiscrepancy << std::endl;

    assert(audit.balanced);
    assert(audit.maxDiscrepancy < 1e-3);
    std::cout << "  -> PASSED (All substance fluxes rigorously accounted for)\n" << std::endl;
}

} // namespace

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "TATARUS 4 - CAUSAL PHYSIOLOGICAL VALIDATION & PROPERTY TESTS" << std::endl;
    std::cout << "============================================================\n" << std::endl;

    testHemorrhageCausalChain();
    testHypoxiaCausalChain();
    testHypercapniaCausalChain();
    testHyperkalemiaElectrophysiology();
    testRenalHypoperfusionAndRAAS();
    testRobotMechanicalLoadCoupling();
    testMarsEnvironmentEmergence();
    testBodyToNervousSystemSupplyFeedback();
    testDeterministicSnapshotRoundtrip();
    testConservationAuditLedger();

    std::cout << "============================================================" << std::endl;
    std::cout << "ALL CAUSAL VALIDATION AND PROPERTY TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
