#include "tatarus_circulation.hpp"

#include <algorithm>
#include <cmath>

namespace tatarus::organism {

namespace {

// O2 solubility in plasma: 0.0031 mL O2 / (dL blood * mmHg) = 0.00138 mmol / (L * mmHg)
constexpr double kAlphaO2 = 0.00138;
// CO2 solubility in plasma: 0.0307 mmol / (L * mmHg)
constexpr double kAlphaCO2 = 0.0307;
// Hemoglobin binding capacity: 1.34 mL O2 / g Hb = 0.0598 mmol O2 / g Hb
// At 15 g/dL = 150 g/L Hb -> max bound O2 = 150 * 0.0598 = 8.97 mmol/L
constexpr double kMaxHbBindingPerG = 0.0598;
// Baseline P50 for adult human hemoglobin ~ 26.8 mmHg, Hill coefficient ~ 2.7
constexpr double kBaseP50 = 26.8;
constexpr double kHillCoeff = 2.7;

// Dynamic P50 with Bohr, Haldane, and Temperature effects
double calculateDynamicP50(double ph, double pco2MmHg, double tempC) {
    const double deltaPh = ph - 7.40;
    const double pco2Ratio = std::max(10.0, pco2MmHg) / 40.0;
    const double deltaTemp = tempC - 37.0;
    // Bohr shift: acidosis and hypercapnia shift curve right (higher P50 -> lower affinity, easier O2 release)
    const double exponent = -0.40 * deltaPh + 0.06 * std::log10(pco2Ratio) + 0.024 * deltaTemp;
    return kBaseP50 * std::pow(10.0, exponent);
}

double calculateO2Saturation(double po2MmHg, double p50) {
    if (po2MmHg <= 0.0) return 0.0;
    const double ratio = po2MmHg / std::max(5.0, p50);
    const double powered = std::pow(ratio, kHillCoeff);
    return powered / (1.0 + powered);
}

double estimatePO2FromBoundO2(double boundO2Mm, double hbGPerL, double p50) {
    const double maxBound = std::max(0.1, hbGPerL * kMaxHbBindingPerG);
    const double satFrac = std::clamp(boundO2Mm / maxBound, 0.001, 0.999);
    // Inverse Hill equation: PO2 = P50 * (Sat / (1 - Sat))^(1 / HillCoeff)
    const double ratio = satFrac / (1.0 - satFrac);
    const double directPo2 = p50 * std::pow(ratio, 1.0 / kHillCoeff);
    return std::clamp(directPo2, 0.5, 600.0);
}

double calculatePh(double hco3Mm, double pco2MmHg) {
    const double pco2 = std::max(5.0, pco2MmHg);
    const double hco3 = std::max(1.0, hco3Mm);
    const double dissolvedCo2 = kAlphaCO2 * pco2;
    return 6.10 + std::log10(hco3 / dissolvedCo2);
}

} // namespace

Circulation::Circulation(CirculationConfig config)
    : config_(config) {
    telemetry_.available = true;
    arterialSolutes_ = SoluteProfile{};
    venousSolutes_ = SoluteProfile{};

    // Set physiological initial venous values (lower O2, higher CO2)
    venousSolutes_.o2DissolvedMm = 0.055;
    venousSolutes_.o2BoundHemoglobinMm = 6.70; // ~75% saturation
    venousSolutes_.co2DissolvedMm = 1.41;     // ~46 mmHg
    venousSolutes_.hco3Mm = 26.0;

    recalculatePressures(0.0, 15.0);
    recalculateBloodGasesAndPh();
}

double Circulation::pulmonaryCapillaryPressureMmHg() const noexcept {
    return 0.5 * (telemetry_.pulmonaryArteryPressureMmHg + telemetry_.centralVenousPressureMmHg);
}

void Circulation::step(
    double dtSeconds,
    double cardiacOutputMlPerS,
    double rightVentricularOutputMlPerS,
    double cerebralDemandMlPerS,
    double renalDemandMlPerS,
    double coronaryDemandMlPerS,
    double peripheralDemandMlPerS,
    double sympatheticVasomotorTone,
    double angiotensinIIPm) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    if (dtSeconds <= 0.0) return;

    // 1. Fluid distribution and volume dynamics between arterial and venous pools
    const double leftVentricularFlowMl = cardiacOutputMlPerS * dtSeconds;
    const double rightVentricularFlowMl = rightVentricularOutputMlPerS * dtSeconds;

    // Systemic vascular drain according to physical Ohm/Poiseuille resistance network:
    const double deltaP = std::max(0.0, telemetry_.meanArterialPressureMmHg - telemetry_.centralVenousPressureMmHg);
    const double tpr = std::max(0.1, telemetry_.totalPeripheralResistanceMmHgSPerMl);
    const double systemicDrainRateMlPerS = deltaP / tpr;
    const double peripheralDrainMl = systemicDrainRateMlPerS * dtSeconds;

    // Regional organ blood flows proportional to vascular conductance
    const double totalDemand = cerebralDemandMlPerS + renalDemandMlPerS +
                               coronaryDemandMlPerS + peripheralDemandMlPerS + 1e-6;
    telemetry_.cerebralBloodFlowMlPerS = (cerebralDemandMlPerS / totalDemand) * systemicDrainRateMlPerS;
    telemetry_.renalBloodFlowMlPerS = (renalDemandMlPerS / totalDemand) * systemicDrainRateMlPerS;
    telemetry_.coronaryBloodFlowMlPerS = (coronaryDemandMlPerS / totalDemand) * systemicDrainRateMlPerS;
    telemetry_.peripheralBloodFlowMlPerS = (peripheralDemandMlPerS / totalDemand) * systemicDrainRateMlPerS;

    // Arterial volume: receives LV outflow, drains to venous system via organ beds
    arterialVolumeMl_ += (leftVentricularFlowMl - peripheralDrainMl);
    // Venous volume: receives organ bed drain, drains to RV
    venousVolumeMl_ += (peripheralDrainMl - rightVentricularFlowMl);

    // Pulmonary circuit: RV -> Pulmonary Capillaries -> Pulmonary Veins -> LA
    pulmonaryArteryVolumeMl_ += (rightVentricularFlowMl - leftVentricularFlowMl);

    // Ensure mass-preserving volume stability
    const double totalVolMl = arterialVolumeMl_ + venousVolumeMl_ + pulmonaryArteryVolumeMl_ +
                              pulmonaryCapillaryVolumeMl_ + pulmonaryVeinVolumeMl_ + microvascularVolumeMl_;
    telemetry_.totalBloodVolumeL = totalVolMl / 1000.0;
    telemetry_.arterialVolumeL = arterialVolumeMl_ / 1000.0;
    telemetry_.venousVolumeL = venousVolumeMl_ / 1000.0;
    telemetry_.totalCardiacOutputLPerMin = (cardiacOutputMlPerS * 60.0) / 1000.0;

    // 2. Advective mixing of solute concentrations between arterial and venous pools
    const double artTurnover = std::clamp((systemicDrainRateMlPerS * dtSeconds) / std::max(arterialVolumeMl_, 100.0), 0.0, 0.5);
    const double venTurnover = std::clamp((rightVentricularFlowMl) / std::max(venousVolumeMl_, 100.0), 0.0, 0.5);
    const double advectFraction = 0.5 * (artTurnover + venTurnover);

    // Convective advection equilibrates systemic non-metabolic electrolytes
    const double diffRate = (0.05 + 0.1 * advectFraction) * dtSeconds;
    arterialSolutes_.naMm += diffRate * (venousSolutes_.naMm - arterialSolutes_.naMm);
    arterialSolutes_.kMm += diffRate * (venousSolutes_.kMm - arterialSolutes_.kMm);
    arterialSolutes_.caIonizedMm += diffRate * (venousSolutes_.caIonizedMm - arterialSolutes_.caIonizedMm);
    arterialSolutes_.clMm += diffRate * (venousSolutes_.clMm - arterialSolutes_.clMm);
    arterialSolutes_.glucoseMm += diffRate * (venousSolutes_.glucoseMm - arterialSolutes_.glucoseMm);
    arterialSolutes_.ureaMm += diffRate * (venousSolutes_.ureaMm - arterialSolutes_.ureaMm);
    arterialSolutes_.lactateMm += diffRate * (venousSolutes_.lactateMm - arterialSolutes_.lactateMm);

    // Update pressures and blood gases
    recalculatePressures(sympatheticVasomotorTone, angiotensinIIPm);
    recalculateBloodGasesAndPh();
}

void Circulation::recalculatePressures(double vasomotorTone, double angIIPm) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    const double totalVolMl = arterialVolumeMl_ + venousVolumeMl_ + pulmonaryArteryVolumeMl_ +
                              pulmonaryCapillaryVolumeMl_ + pulmonaryVeinVolumeMl_ + microvascularVolumeMl_;
    telemetry_.totalBloodVolumeL = totalVolMl / 1000.0;
    telemetry_.arterialVolumeL = arterialVolumeMl_ / 1000.0;
    telemetry_.venousVolumeL = venousVolumeMl_ / 1000.0;

    // Total peripheral resistance increases with sympathetic tone and Angiotensin II
    const double raasFactor = std::clamp(angIIPm / 15.0, 0.5, 3.0);
    const double sympFactor = 1.0 + 0.8 * std::clamp(vasomotorTone, 0.0, 2.0);
    telemetry_.totalPeripheralResistanceMmHgSPerMl = config_.baselinePeripheralResistanceMmHgSPerMl * sympFactor * raasFactor;

    // Arterial pressure = Unstressed volume + (Stressed Volume / Compliance)
    const double stressedArterialMl = std::max(0.0, arterialVolumeMl_ - unstressedArterialVolumeMl_);
    const double compArt = config_.baselineArterialComplianceMlPerMmHg / (1.0 + 0.3 * vasomotorTone);
    const double meanArtPressure = stressedArterialMl / std::max(compArt, 0.1);

    telemetry_.meanArterialPressureMmHg = std::clamp(meanArtPressure, 20.0, 240.0);
    // Pulse pressure approximation: PP ~ SV / C_art
    const double pulsePressure = 40.0 * (stressedArterialMl / 150.0);
    telemetry_.systolicPressureMmHg = telemetry_.meanArterialPressureMmHg + 0.6 * pulsePressure;
    telemetry_.diastolicPressureMmHg = telemetry_.meanArterialPressureMmHg - 0.4 * pulsePressure;

    // Central Venous Pressure = (Stressed Venous Volume / Compliance)
    const double stressedVenousMl = std::max(0.0, venousVolumeMl_ - unstressedVenousVolumeMl_);
    telemetry_.centralVenousPressureMmHg = std::clamp(stressedVenousMl / config_.baselineVenousComplianceMlPerMmHg, 0.5, 25.0);

    // Pulmonary Artery Pressure
    telemetry_.pulmonaryArteryPressureMmHg = std::clamp(10.0 + (pulmonaryArteryVolumeMl_ - 150.0) * 0.15, 5.0, 60.0);
}

void Circulation::recalculateBloodGasesAndPh() {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    const double hbGPerL = config_.hemoglobinGPerDl * 10.0;

    // 1. Arterial blood gases with Bohr effect
    telemetry_.arterialPco2MmHg = arterialSolutes_.co2DissolvedMm / kAlphaCO2;
    telemetry_.arterialPh = calculatePh(arterialSolutes_.hco3Mm, telemetry_.arterialPco2MmHg);

    const double arterialP50 = calculateDynamicP50(telemetry_.arterialPh, telemetry_.arterialPco2MmHg, bloodTemperatureC_);
    telemetry_.arterialPo2MmHg = estimatePO2FromBoundO2(arterialSolutes_.o2BoundHemoglobinMm, hbGPerL, arterialP50);
    telemetry_.arterialOxygenSaturation = calculateO2Saturation(telemetry_.arterialPo2MmHg, arterialP50);
    arterialSolutes_.o2BoundHemoglobinMm = hbGPerL * kMaxHbBindingPerG * telemetry_.arterialOxygenSaturation;
    arterialSolutes_.o2DissolvedMm = kAlphaO2 * telemetry_.arterialPo2MmHg;

    // 2. Venous blood gases with Bohr effect
    telemetry_.venousPco2MmHg = venousSolutes_.co2DissolvedMm / kAlphaCO2;
    telemetry_.venousPh = calculatePh(venousSolutes_.hco3Mm, telemetry_.venousPco2MmHg);

    const double venousP50 = calculateDynamicP50(telemetry_.venousPh, telemetry_.venousPco2MmHg, bloodTemperatureC_);
    telemetry_.venousPo2MmHg = estimatePO2FromBoundO2(venousSolutes_.o2BoundHemoglobinMm, hbGPerL, venousP50);
    telemetry_.arterialOxygenSaturation = calculateO2Saturation(telemetry_.arterialPo2MmHg, arterialP50);
    venousSolutes_.o2BoundHemoglobinMm = hbGPerL * kMaxHbBindingPerG * calculateO2Saturation(telemetry_.venousPo2MmHg, venousP50);
    venousSolutes_.o2DissolvedMm = kAlphaO2 * telemetry_.venousPo2MmHg;

    // Systemic electrolyte metrics
    telemetry_.plasmaGlucoseMm = arterialSolutes_.glucoseMm;
    telemetry_.plasmaNaMm = arterialSolutes_.naMm;
    telemetry_.plasmaKMm = arterialSolutes_.kMm;
    telemetry_.plasmaCaMm = arterialSolutes_.caIonizedMm;
    telemetry_.plasmaClMm = arterialSolutes_.clMm;
    telemetry_.plasmaHco3Mm = arterialSolutes_.hco3Mm;
    telemetry_.plasmaUreaMm = arterialSolutes_.ureaMm;
    telemetry_.plasmaLactateMm = arterialSolutes_.lactateMm;
    telemetry_.bloodTemperatureC = bloodTemperatureC_;
}

void Circulation::exchangePulmonary(
    double o2AddedMmol,
    double co2RemovedMmol,
    double alveolarPo2MmHg,
    double alveolarPco2MmHg) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    (void)o2AddedMmol;
    (void)co2RemovedMmol;
    const double hbGPerL = config_.hemoglobinGPerDl * 10.0;
    const double p50 = calculateDynamicP50(telemetry_.arterialPh, telemetry_.arterialPco2MmHg, bloodTemperatureC_);
    const double satCap = calculateO2Saturation(alveolarPo2MmHg, p50);
    const double maxBound = hbGPerL * kMaxHbBindingPerG;
    const double o2CapillaryBound = maxBound * satCap;

    arterialSolutes_.o2BoundHemoglobinMm = 0.5 * arterialSolutes_.o2BoundHemoglobinMm + 0.5 * o2CapillaryBound;
    arterialSolutes_.co2DissolvedMm = 0.5 * arterialSolutes_.co2DissolvedMm + 0.5 * (kAlphaCO2 * alveolarPco2MmHg);
    recalculateBloodGasesAndPh();
}

void Circulation::exchangeCerebral(
    double o2ConsumedMmol,
    double glucoseConsumedMmol,
    double co2ProducedMmol,
    double lactateProducedMmol,
    double naFluxMmol,
    double kFluxMmol,
    double caFluxMmol) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    const double venVolL = std::max(0.5, venousVolumeMl_ / 1000.0);

    venousSolutes_.o2BoundHemoglobinMm = std::max(0.0, venousSolutes_.o2BoundHemoglobinMm - (o2ConsumedMmol / venVolL));
    venousSolutes_.glucoseMm = std::max(0.1, venousSolutes_.glucoseMm - (glucoseConsumedMmol / venVolL));
    venousSolutes_.co2DissolvedMm += (co2ProducedMmol / venVolL);
    venousSolutes_.lactateMm += (lactateProducedMmol / venVolL);

    // Ion fluxes
    venousSolutes_.naMm += (naFluxMmol / venVolL);
    venousSolutes_.kMm += (kFluxMmol / venVolL);
    venousSolutes_.caIonizedMm += (caFluxMmol / venVolL);
}

void Circulation::exchangeCoronary(
    double o2ConsumedMmol,
    double glucoseConsumedMmol,
    double co2ProducedMmol) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    const double venVolL = std::max(0.5, venousVolumeMl_ / 1000.0);
    venousSolutes_.o2BoundHemoglobinMm = std::max(0.0, venousSolutes_.o2BoundHemoglobinMm - (o2ConsumedMmol / venVolL));
    venousSolutes_.glucoseMm = std::max(0.1, venousSolutes_.glucoseMm - (glucoseConsumedMmol / venVolL));
    venousSolutes_.co2DissolvedMm += (co2ProducedMmol / venVolL);
}

void Circulation::exchangePeripheral(
    double o2ConsumedMmol,
    double glucoseConsumedMmol,
    double co2ProducedMmol,
    double lactateProducedMmol,
    double heatAddedJoules) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    const double venVolL = std::max(0.5, venousVolumeMl_ / 1000.0);
    venousSolutes_.o2BoundHemoglobinMm = std::max(0.0, venousSolutes_.o2BoundHemoglobinMm - (o2ConsumedMmol / venVolL));
    venousSolutes_.glucoseMm = std::max(0.1, venousSolutes_.glucoseMm - (glucoseConsumedMmol / venVolL));
    venousSolutes_.co2DissolvedMm += (co2ProducedMmol / venVolL);
    venousSolutes_.lactateMm += (lactateProducedMmol / venVolL);

    // Heat transfer to blood
    totalHeatJoules_ += heatAddedJoules;
    const double heatDissipation = 50.0 * (bloodTemperatureC_ - 20.0);
    totalHeatJoules_ -= heatDissipation * 0.001;
    const double bloodMassG = (telemetry_.totalBloodVolumeL * 1000.0) * 1.06;
    bloodTemperatureC_ = totalHeatJoules_ / (bloodMassG * 3.85);
    bloodTemperatureC_ = std::clamp(bloodTemperatureC_, 25.0, 43.0);
}

void Circulation::exchangeRenal(
    double waterRemovedMl,
    double naRemovedMmol,
    double kRemovedMmol,
    double caRemovedMmol,
    double clRemovedMmol,
    double hco3RemovedMmol,
    double ureaRemovedMmol,
    double glucoseRemovedMmol) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    venousVolumeMl_ = std::max(500.0, venousVolumeMl_ - waterRemovedMl);
    const double totalVolL = std::max(1.0, telemetry_.totalBloodVolumeL);

    // Solute mass conservation
    arterialSolutes_.naMm = std::max(50.0, arterialSolutes_.naMm - (naRemovedMmol / totalVolL));
    venousSolutes_.naMm = std::max(50.0, venousSolutes_.naMm - (naRemovedMmol / totalVolL));

    arterialSolutes_.kMm = std::max(1.0, arterialSolutes_.kMm - (kRemovedMmol / totalVolL));
    venousSolutes_.kMm = std::max(1.0, venousSolutes_.kMm - (kRemovedMmol / totalVolL));

    arterialSolutes_.caIonizedMm = std::max(0.2, arterialSolutes_.caIonizedMm - (caRemovedMmol / totalVolL));
    venousSolutes_.caIonizedMm = std::max(0.2, venousSolutes_.caIonizedMm - (caRemovedMmol / totalVolL));

    arterialSolutes_.clMm = std::max(40.0, arterialSolutes_.clMm - (clRemovedMmol / totalVolL));
    venousSolutes_.clMm = std::max(40.0, venousSolutes_.clMm - (clRemovedMmol / totalVolL));

    arterialSolutes_.hco3Mm = std::max(5.0, arterialSolutes_.hco3Mm - (hco3RemovedMmol / totalVolL));
    venousSolutes_.hco3Mm = std::max(5.0, venousSolutes_.hco3Mm - (hco3RemovedMmol / totalVolL));

    arterialSolutes_.ureaMm = std::max(0.5, arterialSolutes_.ureaMm - (ureaRemovedMmol / totalVolL));
    venousSolutes_.ureaMm = std::max(0.5, venousSolutes_.ureaMm - (ureaRemovedMmol / totalVolL));

    arterialSolutes_.glucoseMm = std::max(0.1, arterialSolutes_.glucoseMm - (glucoseRemovedMmol / totalVolL));
    venousSolutes_.glucoseMm = std::max(0.1, venousSolutes_.glucoseMm - (glucoseRemovedMmol / totalVolL));
}

void Circulation::updateHormones(
    double dtSeconds,
    double sympatheticDrive,
    double reninRate,
    double angIIRate,
    double aldosteroneRate,
    double adhRate) {
    if (throughput_) throughput_->recordReadWrite<Circulation>(tatarus::ThroughputDomain::Circulation);
    if (dtSeconds <= 0.0) return;

    const double adrTarget = 30.0 + 350.0 * std::clamp(sympatheticDrive, 0.0, 3.0);
    const double norTarget = 250.0 + 1200.0 * std::clamp(sympatheticDrive, 0.0, 3.0);
    const double angIITarget = 15.0 * std::clamp(angIIRate, 0.1, 10.0);
    const double aldoTarget = 200.0 * std::clamp(aldosteroneRate, 0.1, 10.0);
    const double adhTarget = 2.0 * std::clamp(adhRate, 0.1, 10.0);

    const double kClrFast = 0.05 * dtSeconds;
    const double kClrSlow = 0.01 * dtSeconds;

    endocrine_.adrenalinePgPerMl += kClrFast * (adrTarget - endocrine_.adrenalinePgPerMl);
    endocrine_.noradrenalinePgPerMl += kClrFast * (norTarget - endocrine_.noradrenalinePgPerMl);
    endocrine_.reninUPerMl += kClrFast * (reninRate - endocrine_.reninUPerMl);
    endocrine_.angiotensinIIPm += kClrFast * (angIITarget - endocrine_.angiotensinIIPm);
    endocrine_.aldosteronePm += kClrSlow * (aldoTarget - endocrine_.aldosteronePm);
    endocrine_.vasopressinPgPerMl += kClrSlow * (adhTarget - endocrine_.vasopressinPgPerMl);
}

void Circulation::infuseFluidMl(double volumeMl, const SoluteProfile& soluteConcentrations) {
    if (volumeMl <= 0.0) return;
    const double oldVolL = telemetry_.totalBloodVolumeL;
    const double newVolL = oldVolL + (volumeMl / 1000.0);
    const double venAdd = volumeMl * 0.8;
    const double artAdd = volumeMl * 0.2;
    venousVolumeMl_ += venAdd;
    arterialVolumeMl_ += artAdd;

    // Mix solutes
    const double fOld = oldVolL / newVolL;
    const double fNew = (volumeMl / 1000.0) / newVolL;

    arterialSolutes_.naMm = fOld * arterialSolutes_.naMm + fNew * soluteConcentrations.naMm;
    venousSolutes_.naMm = fOld * venousSolutes_.naMm + fNew * soluteConcentrations.naMm;

    arterialSolutes_.kMm = fOld * arterialSolutes_.kMm + fNew * soluteConcentrations.kMm;
    venousSolutes_.kMm = fOld * venousSolutes_.kMm + fNew * soluteConcentrations.kMm;

    arterialSolutes_.glucoseMm = fOld * arterialSolutes_.glucoseMm + fNew * soluteConcentrations.glucoseMm;
    venousSolutes_.glucoseMm = fOld * venousSolutes_.glucoseMm + fNew * soluteConcentrations.glucoseMm;

    recalculatePressures(0.0, endocrine_.angiotensinIIPm);
    recalculateBloodGasesAndPh();
}

void Circulation::bleedFluidMl(double volumeMl) {
    if (volumeMl <= 0.0) return;
    const double venLoss = volumeMl * 0.8;
    const double artLoss = volumeMl * 0.2;
    venousVolumeMl_ = std::max(200.0, venousVolumeMl_ - venLoss);
    arterialVolumeMl_ = std::max(100.0, arterialVolumeMl_ - artLoss);
    recalculatePressures(0.0, endocrine_.angiotensinIIPm);
    recalculateBloodGasesAndPh();
}

SubstanceAmounts Circulation::totalSubstanceAmounts() const {
    SubstanceAmounts amounts{};
    const double artVolL = arterialVolumeMl_ / 1000.0;
    const double venVolL = (venousVolumeMl_ + pulmonaryArteryVolumeMl_ +
                            pulmonaryCapillaryVolumeMl_ + pulmonaryVeinVolumeMl_ + microvascularVolumeMl_) / 1000.0;

    amounts[SubstanceId::Water] = telemetry_.totalBloodVolumeL;
    amounts[SubstanceId::O2] = (arterialSolutes_.o2BoundHemoglobinMm + arterialSolutes_.o2DissolvedMm) * artVolL +
                               (venousSolutes_.o2BoundHemoglobinMm + venousSolutes_.o2DissolvedMm) * venVolL;
    amounts[SubstanceId::CO2] = (arterialSolutes_.co2DissolvedMm) * artVolL +
                                (venousSolutes_.co2DissolvedMm) * venVolL;
    amounts[SubstanceId::Glucose] = (arterialSolutes_.glucoseMm * artVolL) +
                                    (venousSolutes_.glucoseMm * venVolL);
    amounts[SubstanceId::Na] = (arterialSolutes_.naMm * artVolL) +
                               (venousSolutes_.naMm * venVolL);
    amounts[SubstanceId::K] = (arterialSolutes_.kMm * artVolL) +
                              (venousSolutes_.kMm * venVolL);
    amounts[SubstanceId::Ca] = (arterialSolutes_.caIonizedMm * artVolL) +
                               (venousSolutes_.caIonizedMm * venVolL);
    amounts[SubstanceId::Cl] = (arterialSolutes_.clMm * artVolL) +
                               (venousSolutes_.clMm * venVolL);
    amounts[SubstanceId::HCO3] = (arterialSolutes_.hco3Mm * artVolL) +
                                 (venousSolutes_.hco3Mm * venVolL);
    amounts[SubstanceId::H] = std::pow(10.0, -telemetry_.arterialPh) * artVolL +
                              std::pow(10.0, -telemetry_.venousPh) * venVolL;
    amounts[SubstanceId::Lactate] = (arterialSolutes_.lactateMm * artVolL) +
                                    (venousSolutes_.lactateMm * venVolL);
    amounts[SubstanceId::Urea] = (arterialSolutes_.ureaMm * artVolL) +
                                 (venousSolutes_.ureaMm * venVolL);

    return amounts;
}

} // namespace tatarus::organism
