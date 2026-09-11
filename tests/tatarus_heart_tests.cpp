#include <tatarus/heart.hpp>

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

void testHeartPacemakerAndConduction() {
    tatarus::organism::Heart heart;

    bool saFired = false;
    bool avFired = false;
    bool ventriclesDepol = false;

    // Step for 1.5 seconds at 1 ms microsteps
    for (int i = 0; i < 1500; ++i) {
        heart.step(0.001, 93.3, 4.0, 15.0, 8.0, 0.2, 0.8, 8.8, 4.0, 1.25);
        if (heart.telemetry().saNodeFired) saFired = true;
        if (heart.telemetry().avNodeFired) avFired = true;
        if (heart.telemetry().ventriclesDepolarized) ventriclesDepol = true;
    }

    require(saFired, "SA node must generate pacemaker spikes");
    require(avFired, "AV node must conduct excitation with physiological delay");
    require(ventriclesDepol, "Purkinje network must depolarize ventricular myocardium");
    std::cout << "testHeartPacemakerAndConduction: PASS" << std::endl;
}

void testHeartHemodynamicsAndValves() {
    tatarus::organism::Heart heart;

    double maxLvPressure = 0.0;
    double maxRvPressure = 0.0;
    double maxAorticFlow = 0.0;

    for (int i = 0; i < 2000; ++i) {
        heart.step(0.001, 80.0, 4.0, 15.0, 8.0, 0.5, 0.5, 8.8, 4.0, 1.25);
        maxLvPressure = std::max(maxLvPressure, heart.telemetry().leftVentriclePressureMmHg);
        maxRvPressure = std::max(maxRvPressure, heart.telemetry().rightVentriclePressureMmHg);
        maxAorticFlow = std::max(maxAorticFlow, heart.leftVentricularOutflowMlPerS());
    }

    require(maxLvPressure >= 90.0, "LV systolic pressure must reach >90 mmHg");
    require(maxRvPressure >= 15.0 && maxRvPressure <= 35.0, "RV systolic pressure should be ~20-30 mmHg");
    require(maxAorticFlow > 0.0, "Aortic valve must open and eject blood during systole");
    std::cout << "DEBUG: SV=" << heart.strokeVolumeMl() << " mL, CO=" << heart.cardiacOutputLPerMin() << " L/min" << std::endl;
    require(heart.strokeVolumeMl() >= 30.0 && heart.strokeVolumeMl() <= 110.0, "Stroke volume should be physiological (30-110 mL)");
    require(heart.cardiacOutputLPerMin() >= 2.5 && heart.cardiacOutputLPerMin() <= 9.0, "Cardiac output should be 2.5-9 L/min");
    std::cout << "testHeartHemodynamicsAndValves: PASS" << std::endl;
}

void testHeartAutonomicModulation() {
    tatarus::organism::Heart heartRest;
    tatarus::organism::Heart heartStress;

    // Rest: High parasympathetic, low sympathetic
    for (int i = 0; i < 1000; ++i) {
        heartRest.step(0.001, 93.3, 4.0, 15.0, 8.0, 0.0, 1.5, 8.8, 4.0, 1.25);
    }

    // Stress: High sympathetic, low parasympathetic
    for (int i = 0; i < 1000; ++i) {
        heartStress.step(0.001, 93.3, 4.0, 15.0, 8.0, 2.0, 0.0, 8.8, 4.0, 1.25);
    }

    require(heartStress.instantaneousHeartRateBpm() > heartRest.instantaneousHeartRateBpm(),
            "Sympathetic stimulation must increase heart rate (tachycardia)");
    require(heartStress.cardiacOutputLPerMin() > heartRest.cardiacOutputLPerMin(),
            "Sympathetic drive must enhance cardiac output");
    std::cout << "testHeartAutonomicModulation: PASS" << std::endl;
}

} // namespace

int main() {
    try {
        testHeartPacemakerAndConduction();
        testHeartHemodynamicsAndValves();
        testHeartAutonomicModulation();
        std::cout << "TATARUS Heart tests: ALL PASS" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Heart test failed: " << e.what() << std::endl;
        return 1;
    }
}
