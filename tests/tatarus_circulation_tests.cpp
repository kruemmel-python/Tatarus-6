#include <tatarus/circulation.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "Assertion failed: " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}

void testCirculationBasicsAndPressures() {
    tatarus::organism::Circulation circ;
    const auto& t = circ.telemetry();
    require(t.available, "Circulation telemetry must be available");
    require(std::abs(t.totalBloodVolumeL - 5.0) < 0.1, "Total blood volume should initialize to ~5.0 L");
    require(t.meanArterialPressureMmHg >= 80.0 && t.meanArterialPressureMmHg <= 110.0, "MAP should be physiological (80-110 mmHg)");
    require(t.systolicPressureMmHg > t.diastolicPressureMmHg, "Systolic pressure must exceed diastolic pressure");
    require(t.centralVenousPressureMmHg > 0.0 && t.centralVenousPressureMmHg < 10.0, "CVP should be 1-8 mmHg");
    std::cout << "testCirculationBasicsAndPressures: PASS" << std::endl;
}

void testCirculationBloodGasesAndPh() {
    tatarus::organism::Circulation circ;
    const auto& t = circ.telemetry();
    require(t.arterialPh >= 7.35 && t.arterialPh <= 7.45, "Arterial pH should be physiological ~7.40");
    require(t.arterialPo2MmHg >= 85.0 && t.arterialPo2MmHg <= 105.0, "Arterial PO2 should be ~95 mmHg");
    require(t.arterialOxygenSaturation >= 0.95, "Arterial oxygen saturation should be >95%");
    require(t.arterialPco2MmHg >= 35.0 && t.arterialPco2MmHg <= 45.0, "Arterial PCO2 should be ~40 mmHg");
    require(t.venousPo2MmHg < t.arterialPo2MmHg, "Venous PO2 must be lower than arterial PO2");
    require(t.venousPco2MmHg > t.arterialPco2MmHg, "Venous PCO2 must be higher than arterial PCO2");
    std::cout << "testCirculationBloodGasesAndPh: PASS" << std::endl;
}

void testCirculationMassConservation() {
    tatarus::organism::Circulation circ;
    const double initialBloodVol = circ.telemetry().totalBloodVolumeL;

    // Simulate 100 steps of steady state stepping
    for (int i = 0; i < 100; ++i) {
        circ.step(0.05, 83.3, 83.3, 12.5, 20.0, 4.0, 45.0, 0.2, 15.0);
    }

    const double finalBloodVol = circ.telemetry().totalBloodVolumeL;
    require(std::abs(finalBloodVol - initialBloodVol) < 1e-4, "Closed loop volume must be strictly conserved");
    std::cout << "testCirculationMassConservation: PASS" << std::endl;
}

void testCirculationFluidInfusionAndHemorrhage() {
    tatarus::organism::Circulation circ;
    const double baselineMap = circ.arterialPressureMmHg();

    // Bleed 500 mL
    circ.bleedFluidMl(500.0);
    require(circ.telemetry().totalBloodVolumeL < 4.8, "Blood volume should drop after bleed");
    require(circ.arterialPressureMmHg() < baselineMap, "MAP must drop after hemorrhage");

    // Infuse 500 mL saline
    tatarus::organism::SoluteProfile saline;
    saline.naMm = 154.0;
    saline.clMm = 154.0;
    circ.infuseFluidMl(500.0, saline);
    require(circ.telemetry().totalBloodVolumeL > 4.8, "Blood volume should recover after infusion");
    require(circ.arterialPressureMmHg() > 70.0, "MAP should recover after fluid resuscitation");
    std::cout << "testCirculationFluidInfusionAndHemorrhage: PASS" << std::endl;
}

} // namespace

int main() {
    try {
        testCirculationBasicsAndPressures();
        testCirculationBloodGasesAndPh();
        testCirculationMassConservation();
        testCirculationFluidInfusionAndHemorrhage();
        std::cout << "TATARUS Circulation tests: ALL PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with error: " << e.what() << std::endl;
        return 1;
    }
}
