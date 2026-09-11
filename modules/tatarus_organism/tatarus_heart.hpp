#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus/throughput.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tatarus::organism {

struct HeartConfig {
    double baseIntrinsicHeartRateBpm = 72.0;
    double maxContractilityElastanceLv = 2.5;  // mmHg/mL
    double minPassiveElastanceLv = 0.08;       // mmHg/mL
    double maxContractilityElastanceRv = 0.28; // mmHg/mL
    double minPassiveElastanceRv = 0.04;       // mmHg/mL
    double maxContractilityElastanceLa = 0.25; // mmHg/mL
    double maxContractilityElastanceRa = 0.20; // mmHg/mL
    double unstressedVolumeLvMl = 25.0;
    double unstressedVolumeRvMl = 25.0;
    double unstressedVolumeLaMl = 15.0;
    double unstressedVolumeRaMl = 15.0;
    double valveResistanceMmHgSPerMl = 0.005;
};

class Heart {
public:
    explicit Heart(HeartConfig config = {});
    ~Heart() = default;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    // Step cardiac cycle over dtSeconds (typically milliseconds)
    // Returns cardiac output in mL/s for systemic (LV) and pulmonary (RV) circuits
    void step(
        double dtSeconds,
        double systemicArterialPressureMmHg,
        double centralVenousPressureMmHg,
        double pulmonaryArteryPressureMmHg,
        double pulmonaryCapillaryPressureMmHg,
        double sympatheticTone,
        double parasympatheticTone,
        double coronaryO2Mm,
        double extracellularKMm,
        double extracellularCaMm);

    [[nodiscard]] const HeartTelemetry& telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] double leftVentricularOutflowMlPerS() const noexcept { return leftVentricularOutflowMlPerS_; }
    [[nodiscard]] double rightVentricularOutflowMlPerS() const noexcept { return rightVentricularOutflowMlPerS_; }
    [[nodiscard]] double instantaneousHeartRateBpm() const noexcept { return telemetry_.heartRateBpm; }
    [[nodiscard]] double strokeVolumeMl() const noexcept { return telemetry_.strokeVolumeMl; }
    [[nodiscard]] double cardiacOutputLPerMin() const noexcept { return telemetry_.cardiacOutputLPerMin; }
    [[nodiscard]] double myocardialO2ConsumptionMlPerMin() const noexcept { return telemetry_.myocardialO2ConsumptionMlPerMin; }
    [[nodiscard]] double myocardialGlucoseConsumptionMmolPerMin() const noexcept { return telemetry_.myocardialGlucoseConsumptionMmolPerMin; }

private:
    HeartConfig config_;
    HeartTelemetry telemetry_;
    tatarus::ThroughputCounters* throughput_ = nullptr;

    // Chamber volumes (mL)
    double volumeRaMl_ = 60.0;
    double volumeRvMl_ = 120.0;
    double volumeLaMl_ = 50.0;
    double volumeLvMl_ = 120.0;

    // Chamber pressures (mmHg)
    double pressureRaMmHg_ = 4.0;
    double pressureRvMmHg_ = 20.0;
    double pressureLaMmHg_ = 8.0;
    double pressureLvMmHg_ = 120.0;

    // Instantaneous flows (mL/s)
    double leftVentricularOutflowMlPerS_ = 0.0;
    double rightVentricularOutflowMlPerS_ = 0.0;

    // Conduction network membrane states (mV)
    double saNodePotentialMv_ = -60.0;
    double atrialPotentialMv_ = -80.0;
    double avNodePotentialMv_ = -65.0;
    double purkinjePotentialMv_ = -85.0;
    double ventricularPotentialMv_ = -85.0;

    // Conduction timers & flags
    double saNodePhaseTimeS_ = 0.0;
    double saNodePeriodS_ = 0.833; // ~72 bpm
    bool saFired_ = false;
    double avDelayTimerS_ = 0.0;
    bool avFired_ = false;
    bool ventriclesDepolarized_ = false;
    double ventricularActionPotentialTimeS_ = 0.0;

    // Intracellular Calcium & Crossbridge dynamics
    double intracellularCaLv_ = 0.1; // micromolar
    double intracellularCaRv_ = 0.1; // micromolar
    double srCalciumLvMm_ = 1.2;     // mM
    double activeTensionLv_ = 0.0;
    double activeTensionRv_ = 0.0;
    double activeTensionAtria_ = 0.0;

    // Stroke tracking
    double peakEdvLv_ = 120.0;
    double peakEsvLv_ = 50.0;
    double peakEdvRv_ = 120.0;
    double peakEsvRv_ = 50.0;
    double lastStrokeVolumeMl_ = 70.0;
    double beatTimerS_ = 0.0;
    double cycleAccumulatedLvOutflowMl_ = 0.0;
    double currentSympathetic_ = 0.0;
    double currentParasympathetic_ = 0.0;

    void updateElectrophysiology(
        double dtSeconds,
        double sympatheticTone,
        double parasympatheticTone,
        double extracellularKMm,
        double extracellularCaMm);

    void updateMechanicsAndValves(
        double dtSeconds,
        double systemicArterialPressureMmHg,
        double centralVenousPressureMmHg,
        double pulmonaryArteryPressureMmHg,
        double pulmonaryCapillaryPressureMmHg,
        double coronaryO2Mm);
};

} // namespace tatarus::organism
