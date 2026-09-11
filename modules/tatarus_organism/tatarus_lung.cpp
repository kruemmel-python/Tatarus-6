#include "tatarus_lung.hpp"

#include <algorithm>
#include <cmath>

namespace tatarus::organism {

namespace {

// Water vapor saturation pressure at temperature T (deg C)
// Antoine equation form ~ 47.0 mmHg at 37.0 C
double saturatedWaterVaporPressureMmHg(double tempC) {
    return 47.0 * std::exp(0.05 * (tempC - 37.0));
}

// Convert pressure in kPa to mmHg (1 kPa = 7.50062 mmHg)
constexpr double kKPaToMmHg = 7.50062;

// Standard conversion: 1 mmol of ideal gas at standard body conditions ~ 25.4 mL
constexpr double kMlPerMmolGas = 25.4;

} // namespace

Lung::Lung(LungConfig config)
    : config_(config) {
    telemetry_.available = true;
    telemetry_.respirationRateBpm = config_.baselineRespiratoryRateBpm;
    telemetry_.tidalVolumeL = config_.baselineTidalVolumeL;
    telemetry_.minuteVentilationLPerMin = config_.baselineRespiratoryRateBpm * config_.baselineTidalVolumeL;
    telemetry_.alveolarPo2MmHg = 102.0;
    telemetry_.alveolarPco2MmHg = 40.0;
    telemetry_.o2UptakeRateMlPerMin = 250.0;
    telemetry_.co2EliminationRateMlPerMin = 200.0;
    telemetry_.respiratoryExchangeRatio = 0.80;
    telemetry_.airwayResistance = config_.airwayResistanceCmH2OSPerL;
    telemetry_.lungCompliance = config_.lungComplianceLPerCmH2O;
}

void Lung::step(
    double dtSeconds,
    const AtmosphericEnvironment& environment,
    double arterialPo2MmHg,
    double arterialPco2MmHg,
    double arterialPh,
    double mixedVenousPo2MmHg,
    double mixedVenousPco2MmHg,
    double metabolicCo2ProductionMlPerMin,
    double& outO2AddedMmol,
    double& outCo2RemovedMmol) {
    if (throughput_) throughput_->recordReadWrite<Lung>(tatarus::ThroughputDomain::Lung);
    if (dtSeconds <= 0.0) return;

    // 1. Chemoreceptor integration (Central medullary + Peripheral carotid/aortic)
    updateChemoreceptors(dtSeconds, arterialPo2MmHg, arterialPco2MmHg, arterialPh);

    // 2. Dynamic respiratory mechanics & ventilation
    updateMechanics(dtSeconds, environment);

    // 3. Ambient atmospheric partial pressures
    const double barometricPressureMmHg = environment.totalPressureKPa * kKPaToMmHg;
    const double pH2O = saturatedWaterVaporPressureMmHg(37.0);
    const double dryGasPressureMmHg = std::max(0.0, barometricPressureMmHg - pH2O);

    const double inspiredPo2MmHg = environment.o2Fraction * dryGasPressureMmHg;
    const double inspiredPco2MmHg = environment.co2Fraction * dryGasPressureMmHg;

    // 4. Alveolar Gas Equations across 3 V/Q zones:
    // Zone 1: High V/Q (Apex) -> V/Q = 3.0
    // Zone 2: Matched V/Q (Mid) -> V/Q = 0.85
    // Zone 3: Low V/Q (Base) -> V/Q = 0.30
    const double rExchange = 0.80; // Respiratory Exchange Ratio R = VCO2 / VO2

    // Overall alveolar ventilation (L/min) = (VT - VD) * RR
    const double alveolarVentilationLPerMin = std::max(0.2, (telemetry_.tidalVolumeL - config_.anatomicalDeadSpaceL) * telemetry_.respirationRateBpm);

    // Alveolar PCO2 = Inspired PCO2 + (VCO2 * k / VA)
    const double metabolicVco2 = std::max(50.0, metabolicCo2ProductionMlPerMin);
    const double alvPco2 = inspiredPco2MmHg + (metabolicVco2 * 0.863) / alveolarVentilationLPerMin;
    telemetry_.alveolarPco2MmHg = std::clamp(alvPco2, 2.0, 180.0);

    // Alveolar PO2 = Inspired PO2 - (Alveolar PCO2 / R)
    const double alvPo2 = inspiredPo2MmHg - (telemetry_.alveolarPco2MmHg / rExchange);
    telemetry_.alveolarPo2MmHg = std::clamp(alvPo2, 0.0, 600.0);

    // Regional V/Q computation
    vqZone1Po2_ = std::clamp(inspiredPo2MmHg - (telemetry_.alveolarPco2MmHg * 0.5 / rExchange), 0.0, 600.0);
    vqZone2Po2_ = telemetry_.alveolarPo2MmHg;
    vqZone3Po2_ = std::clamp(inspiredPo2MmHg - (telemetry_.alveolarPco2MmHg * 1.5 / rExchange), 0.0, 600.0);

    telemetry_.vqMismatchIndex = std::abs(vqZone1Po2_ - vqZone3Po2_) / std::max(10.0, vqZone2Po2_);

    // 5. Alveolar-Capillary Fickian Diffusion of O2 and CO2
    // Diffusion capacity DL is reduced by accumulated particulate load
    const double effectiveDlO2 = config_.dlO2MlPerMinMmHg * (1.0 - 0.5 * std::clamp(accumulatedDustFilter_, 0.0, 1.0));
    const double effectiveDlCO2 = config_.dlCO2MlPerMinMmHg * (1.0 - 0.5 * std::clamp(accumulatedDustFilter_, 0.0, 1.0));

    // Capillary gradient
    const double deltaPo2 = std::max(0.0, telemetry_.alveolarPo2MmHg - mixedVenousPo2MmHg);
    const double deltaPco2 = std::max(0.0, mixedVenousPco2MmHg - telemetry_.alveolarPco2MmHg);

    // Uptake and elimination rates in mL/min
    telemetry_.o2UptakeRateMlPerMin = std::clamp(effectiveDlO2 * deltaPo2, 0.0, 3000.0);
    telemetry_.co2EliminationRateMlPerMin = std::clamp(effectiveDlCO2 * deltaPco2, 0.0, 3000.0);

    if (telemetry_.o2UptakeRateMlPerMin > 1.0) {
        telemetry_.respiratoryExchangeRatio = telemetry_.co2EliminationRateMlPerMin / telemetry_.o2UptakeRateMlPerMin;
    }

    // Convert gas volume transferred to molar amounts for dtSeconds:
    // mL/min * (dtSeconds / 60) -> mL in dt -> / 25.4 mL/mmol -> mmol
    const double dtMinutes = dtSeconds / 60.0;
    outO2AddedMmol = (telemetry_.o2UptakeRateMlPerMin * dtMinutes) / kMlPerMmolGas;
    outCo2RemovedMmol = (telemetry_.co2EliminationRateMlPerMin * dtMinutes) / kMlPerMmolGas;

    // Alveolar hypoxia index
    telemetry_.alveolarHypoxiaIndex = std::clamp((60.0 - telemetry_.alveolarPo2MmHg) / 60.0, 0.0, 1.0);

    // Hypoxic Pulmonary Vasoconstriction (HPV): pulmonary vascular resistance increases with alveolar hypoxia
    telemetry_.pulmonaryVascularResistance = 0.12 * (1.0 + 1.5 * telemetry_.alveolarHypoxiaIndex);
}

void Lung::updateChemoreceptors(
    double dtSeconds,
    double arterialPo2MmHg,
    double arterialPco2MmHg,
    double arterialPh) {
    if (throughput_) throughput_->recordReadWrite<Lung>(tatarus::ThroughputDomain::Lung);
    // 1. Central Chemoreceptors (Medulla): Sensitive to CSF / Brain ECS PCO2 and pH (time constant ~ 20 s)
    // Baseline PaCO2 = 40 mmHg, baseline pH = 7.40
    const double deltaPco2 = arterialPco2MmHg - 40.0;
    const double deltaPh = 7.40 - arterialPh; // acidosis increases drive
    const double centralTarget = 1.0 + 0.08 * deltaPco2 + 2.5 * deltaPh;
    const double kCentralTau = 0.05 * dtSeconds; // ~20 s time constant
    centralChemoreceptorDrive_ += kCentralTau * (std::max(0.1, centralTarget) - centralChemoreceptorDrive_);

    // 2. Peripheral Chemoreceptors (Carotid / Aortic bodies): Fast response to PaO2 < 60 mmHg (time constant ~ 3 s)
    double peripheralTarget = 1.0;
    if (arterialPo2MmHg < 60.0) {
        // Hyperbolic increase in carotid sinus nerve firing below 60 mmHg
        const double hypoxiaFactor = std::pow((60.0 - arterialPo2MmHg) / 40.0, 1.5);
        peripheralTarget += 2.8 * hypoxiaFactor;
    }
    // Also sensitive to acute acidosis and hypercapnia synergistically
    if (deltaPco2 > 0.0) {
        peripheralTarget += 0.04 * deltaPco2;
    }
    const double kPeripheralTau = 0.30 * dtSeconds; // ~3 s time constant
    peripheralChemoreceptorDrive_ += kPeripheralTau * (std::max(0.1, peripheralTarget) - peripheralChemoreceptorDrive_);

    telemetry_.centralChemoreceptorDrive = centralChemoreceptorDrive_;
    telemetry_.peripheralChemoreceptorDrive = peripheralChemoreceptorDrive_;

    // Combined neural respiratory drive multiplier
    dynamicVentilationDrive_ = 0.65 * centralChemoreceptorDrive_ + 0.35 * peripheralChemoreceptorDrive_;
    dynamicVentilationDrive_ = std::clamp(dynamicVentilationDrive_, 0.2, 5.0);
}

void Lung::updateMechanics(
    double dtSeconds,
    const AtmosphericEnvironment& environment) {
    if (throughput_) throughput_->recordReadWrite<Lung>(tatarus::ThroughputDomain::Lung);
    // Dust accumulation on airways increases airway resistance
    if (environment.dustPpm > 0.0) {
        accumulatedDustFilter_ += (environment.dustPpm / 1000.0) * 0.001 * dtSeconds;
        accumulatedDustFilter_ = std::clamp(accumulatedDustFilter_, 0.0, 1.0);
    }
    telemetry_.particulateLoadFilterFraction = accumulatedDustFilter_;
    telemetry_.airwayResistance = config_.airwayResistanceCmH2OSPerL * (1.0 + 2.0 * accumulatedDustFilter_);

    // Temperature & Humidity effect on compliance
    const double tempEffect = 1.0 - 0.005 * std::abs(environment.temperatureC - 20.0);
    telemetry_.lungCompliance = config_.lungComplianceLPerCmH2O * std::clamp(tempEffect, 0.7, 1.1);

    // Neural drive sets target RR and VT
    // VT scales with drive^0.4, RR scales with drive^0.6
    const double targetRr = config_.baselineRespiratoryRateBpm * std::pow(dynamicVentilationDrive_, 0.6);
    const double targetVt = config_.baselineTidalVolumeL * std::pow(dynamicVentilationDrive_, 0.4);

    telemetry_.respirationRateBpm = std::clamp(targetRr, 4.0, 48.0);
    telemetry_.tidalVolumeL = std::clamp(targetVt, 0.15, 2.50);
    telemetry_.minuteVentilationLPerMin = telemetry_.respirationRateBpm * telemetry_.tidalVolumeL;

    // Respiration phase oscillator (Inspiration 40%, Expiration 60%)
    respiratoryPeriodS_ = 60.0 / telemetry_.respirationRateBpm;
    respiratoryPhaseTimeS_ += dtSeconds;
    if (respiratoryPhaseTimeS_ >= respiratoryPeriodS_) {
        respiratoryPhaseTimeS_ -= respiratoryPeriodS_;
    }

    const double phaseFrac = respiratoryPhaseTimeS_ / respiratoryPeriodS_;
    if (phaseFrac < 0.40) {
        // Inspiration
        const double inspProgress = phaseFrac / 0.40;
        currentVolumeL_ = config_.functionalResidualCapacityL + telemetry_.tidalVolumeL * (0.5 - 0.5 * std::cos(inspProgress * 3.14159));
    } else {
        // Expiration (Passive elastic recoil)
        const double expProgress = (phaseFrac - 0.40) / 0.60;
        currentVolumeL_ = config_.functionalResidualCapacityL + telemetry_.tidalVolumeL * (0.5 + 0.5 * std::cos(expProgress * 3.14159));
    }
}

} // namespace tatarus::organism
