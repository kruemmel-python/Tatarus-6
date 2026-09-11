#include "tatarus_kidney.hpp"

#include <algorithm>
#include <cmath>

namespace tatarus::organism {

namespace {

// Calculate oncotic pressure (Landis-Pappenheimer equation) from total protein (g/dL)
// pi = 2.1 * c + 0.16 * c^2 + 0.009 * c^3
double calculateOncoticPressureMmHg(double proteinGPerL) {
    const double cGPerDl = proteinGPerL / 10.0; // 70 g/L -> 7.0 g/dL
    return 2.1 * cGPerDl + 0.16 * std::pow(cGPerDl, 2.0) + 0.009 * std::pow(cGPerDl, 3.0);
}

} // namespace

Kidney::Kidney(KidneyConfig config)
    : config_(config) {
    config_.functionalMassFraction = std::clamp(config_.functionalMassFraction, 0.0, 1.0);
    const double mass = config_.functionalMassFraction;
    maculaDensaNaDeliveryMmolPerMin_ = 1.8 * mass;
    telemetry_.available = true;
    telemetry_.renalBloodFlowMlPerMin = config_.baselineRbfMlPerMin * mass;
    telemetry_.renalPlasmaFlowMlPerMin = config_.baselineRbfMlPerMin * 0.55 * mass;
    telemetry_.glomerularFiltrationRateMlPerMin = 125.0 * mass;
    telemetry_.filtrationFraction = telemetry_.glomerularFiltrationRateMlPerMin /
        std::max(1.0, telemetry_.renalPlasmaFlowMlPerMin);
    telemetry_.urineOutputRateMlPerMin = 1.0 * mass;
    telemetry_.urineOsmolarityMOsmPerKg = 600.0;
    telemetry_.urinePh = 6.0;
    telemetry_.reninSecretionRate = 1.0;
}

void Kidney::setFunctionalMassFraction(double fraction) noexcept {
    const double previousMass = config_.functionalMassFraction;
    const double newMass = std::clamp(fraction, 0.0, 1.0);
    config_.functionalMassFraction = newMass;

    // Keep paused live telemetry truthful as well: interventions can be applied
    // while no simulation step is running, so the UI must not retain stale flow.
    if (newMass <= 0.0) {
        telemetry_.renalBloodFlowMlPerMin = 0.0;
        telemetry_.renalPlasmaFlowMlPerMin = 0.0;
        telemetry_.glomerularFiltrationRateMlPerMin = 0.0;
        telemetry_.filtrationFraction = 0.0;
        telemetry_.urineOutputRateMlPerMin = 0.0;
        telemetry_.urineNaExcretionMmolPerMin = 0.0;
        telemetry_.urineKExcretionMmolPerMin = 0.0;
        telemetry_.urineCaExcretionMmolPerMin = 0.0;
        telemetry_.urineUreaExcretionMmolPerMin = 0.0;
        telemetry_.glucoseExcretionMmolPerMin = 0.0;
        telemetry_.reninSecretionRate = 0.0;
        return;
    }

    if (previousMass > 0.0) {
        const double scale = newMass / previousMass;
        telemetry_.renalBloodFlowMlPerMin *= scale;
        telemetry_.renalPlasmaFlowMlPerMin *= scale;
        telemetry_.glomerularFiltrationRateMlPerMin *= scale;
        telemetry_.urineOutputRateMlPerMin *= scale;
        telemetry_.urineNaExcretionMmolPerMin *= scale;
        telemetry_.urineKExcretionMmolPerMin *= scale;
        telemetry_.urineCaExcretionMmolPerMin *= scale;
        telemetry_.urineUreaExcretionMmolPerMin *= scale;
        telemetry_.glucoseExcretionMmolPerMin *= scale;
    } else {
        telemetry_.renalBloodFlowMlPerMin = config_.baselineRbfMlPerMin * newMass;
        telemetry_.renalPlasmaFlowMlPerMin = config_.baselineRbfMlPerMin * 0.55 * newMass;
        telemetry_.glomerularFiltrationRateMlPerMin = 125.0 * newMass;
        telemetry_.urineOutputRateMlPerMin = 1.0 * newMass;
    }
    telemetry_.filtrationFraction = telemetry_.glomerularFiltrationRateMlPerMin /
        std::max(1.0, telemetry_.renalPlasmaFlowMlPerMin);
    telemetry_.reninSecretionRate = std::max(telemetry_.reninSecretionRate, 1.0);
}

void Kidney::step(
    double dtSeconds,
    double meanArterialPressureMmHg,
    const SoluteProfile& plasmaSolutes,
    const EndocrineProfile& hormones,
    double& outWaterExcretedMl,
    double& outNaExcretedMmol,
    double& outKExcretedMmol,
    double& outCaExcretedMmol,
    double& outClExcretedMmol,
    double& outHco3ExcretedMmol,
    double& outUreaExcretedMmol,
    double& outGlucoseExcretedMmol) {
    if (throughput_) throughput_->recordReadWrite<Kidney>(tatarus::ThroughputDomain::Kidney);
    if (dtSeconds <= 0.0) return;
    if (config_.functionalMassFraction <= 0.0) {
        telemetry_.renalBloodFlowMlPerMin = 0.0;
        telemetry_.renalPlasmaFlowMlPerMin = 0.0;
        telemetry_.glomerularFiltrationRateMlPerMin = 0.0;
        telemetry_.filtrationFraction = 0.0;
        telemetry_.urineOutputRateMlPerMin = 0.0;
        telemetry_.urineNaExcretionMmolPerMin = 0.0;
        telemetry_.urineKExcretionMmolPerMin = 0.0;
        telemetry_.urineCaExcretionMmolPerMin = 0.0;
        telemetry_.urineUreaExcretionMmolPerMin = 0.0;
        telemetry_.glucoseExcretionMmolPerMin = 0.0;
        telemetry_.reninSecretionRate = 0.0;
        outWaterExcretedMl = outNaExcretedMmol = outKExcretedMmol = 0.0;
        outCaExcretedMmol = outClExcretedMmol = outHco3ExcretedMmol = 0.0;
        outUreaExcretedMmol = outGlucoseExcretedMmol = 0.0;
        return;
    }

    // 1. Glomerular Hemodynamics & Starling Ultrafiltration (GFR)
    updateAutoregulationAndGFR(meanArterialPressureMmHg, plasmaSolutes, hormones);

    // 2. Sequential Transit along Nephron Segments & Transporter Reabsorption
    updateTubularTransitAndReabsorption(
        dtSeconds,
        plasmaSolutes,
        hormones,
        outWaterExcretedMl,
        outNaExcretedMmol,
        outKExcretedMmol,
        outCaExcretedMmol,
        outClExcretedMmol,
        outHco3ExcretedMmol,
        outUreaExcretedMmol,
        outGlucoseExcretedMmol);
}

void Kidney::updateAutoregulationAndGFR(
    double meanArterialPressureMmHg,
    const SoluteProfile& plasmaSolutes,
    const EndocrineProfile& hormones) {
    if (throughput_) throughput_->recordReadWrite<Kidney>(tatarus::ThroughputDomain::Kidney);
    // 1. Myogenic & Tubuloglomerular Feedback (TGF) autoregulation of arteriolar resistances
    // Myogenic: Afferent resistance Ra increases when MAP > 90 mmHg to buffer glomerular pressure
    const double myogenicRa = 0.027 * (1.0 + 0.8 * std::max(0.0, (meanArterialPressureMmHg - 90.0) / 50.0));

    // TGF: Macula densa senses high NaCl delivery -> Adenosine release -> Afferent constriction
    const double normalMaculaNaDelivery = 1.8 * std::max(0.01, config_.functionalMassFraction);
    const double tgfRa = 0.027 * std::clamp(
        maculaDensaNaDeliveryMmolPerMin_ / normalMaculaNaDelivery, 0.6, 2.0);
    afferentArteriolarResistance_ = 0.5 * (myogenicRa + tgfRa);

    // Efferent resistance Re is constricted by Angiotensin II (preferential efferent action)
    const double angIIEffect = std::clamp(hormones.angiotensinIIPm / 15.0, 0.5, 3.0);
    efferentArteriolarResistance_ = 0.022 * angIIEffect;

    const double functionalMass = config_.functionalMassFraction;
    // Renal autoregulation holds combined RBF near 1.2 L/min over MAP 80-180
    // mmHg; outside this plateau perfusion follows pressure more strongly.
    double perfusionFactor = 1.0 + 0.002 * (meanArterialPressureMmHg - 93.3);
    if (meanArterialPressureMmHg < 80.0) {
        perfusionFactor *= std::clamp(meanArterialPressureMmHg / 80.0, 0.0, 1.0);
    } else if (meanArterialPressureMmHg > 180.0) {
        perfusionFactor *= 1.0 + 0.004 * (meanArterialPressureMmHg - 180.0);
    }
    const double hormonalResistance = std::clamp(1.0 + 0.18 * (angIIEffect - 1.0), 0.7, 1.5);
    telemetry_.renalBloodFlowMlPerMin = config_.baselineRbfMlPerMin *
        functionalMass * std::clamp(perfusionFactor, 0.0, 1.6) / hormonalResistance;
    telemetry_.renalPlasmaFlowMlPerMin = telemetry_.renalBloodFlowMlPerMin * 0.55;

    // Glomerular capillary pressure is the regulated result of afferent myogenic
    // and tubuloglomerular feedback. Below the autoregulatory range it collapses
    // with perfusion; within the plateau it stays close to 55 mmHg.
    double pGc = 55.0 + 0.06 * (meanArterialPressureMmHg - 93.3);
    if (meanArterialPressureMmHg < 80.0) {
        pGc += 0.65 * (meanArterialPressureMmHg - 80.0);
    } else if (meanArterialPressureMmHg > 180.0) {
        pGc += 0.20 * (meanArterialPressureMmHg - 180.0);
    }
    pGc += 1.5 * (angIIEffect - 1.0);
    telemetry_.glomerularCapillaryHydrostaticPressureMmHg = std::clamp(pGc, 20.0, 75.0);

    // Bowman's Space Pressure: PBS
    telemetry_.bowmanSpaceHydrostaticPressureMmHg = config_.bowmanSpacePressureMmHg;

    // Glomerular Capillary Oncotic Pressure: piGC (increases along capillary due to filtration)
    const double meanPiGc = calculateOncoticPressureMmHg(plasmaSolutes.plasmaProteinGPerL);
    telemetry_.glomerularCapillaryOncoticPressureMmHg = std::clamp(meanPiGc, 15.0, 45.0);

    // Net Ultrafiltration Pressure: PUF = (PGC - PBS) - piGC
    const double pUf = (telemetry_.glomerularCapillaryHydrostaticPressureMmHg - telemetry_.bowmanSpaceHydrostaticPressureMmHg) -
                       telemetry_.glomerularCapillaryOncoticPressureMmHg;
    telemetry_.netUltrafiltrationPressureMmHg = std::max(0.0, pUf);

    // GFR = Kf * PUF (Starling equation)
    const double calculatedGfr = config_.baselineKfMlPerMinMmHg *
                                 telemetry_.netUltrafiltrationPressureMmHg * functionalMass;
    telemetry_.glomerularFiltrationRateMlPerMin = std::clamp(calculatedGfr, 0.0, 200.0 * functionalMass);
    telemetry_.filtrationFraction = telemetry_.glomerularFiltrationRateMlPerMin / std::max(1.0, telemetry_.renalPlasmaFlowMlPerMin);

    // 2. Juxtaglomerular Renin Secretion
    // Sensed by: 1) Low renal perfusion pressure, 2) Low Macula Densa NaCl, 3) High Sympathetic/Adrenaline
    const double pressureTrigger = std::max(0.0, (90.0 - meanArterialPressureMmHg) / 40.0);
    const double maculaTrigger = std::max(
        0.0,
        (normalMaculaNaDelivery - maculaDensaNaDeliveryMmolPerMin_) /
            normalMaculaNaDelivery);
    const double adrenergicTrigger = std::clamp(hormones.noradrenalinePgPerMl / 250.0 - 1.0, 0.0, 3.0);

    telemetry_.reninSecretionRate = 1.0 + 3.5 * pressureTrigger + 2.5 * maculaTrigger + 1.5 * adrenergicTrigger;
    telemetry_.reninSecretionRate = std::clamp(telemetry_.reninSecretionRate, 0.1, 10.0);
}

void Kidney::updateTubularTransitAndReabsorption(
    double dtSeconds,
    const SoluteProfile& plasmaSolutes,
    const EndocrineProfile& hormones,
    double& outWaterExcretedMl,
    double& outNaExcretedMmol,
    double& outKExcretedMmol,
    double& outCaExcretedMmol,
    double& outClExcretedMmol,
    double& outHco3ExcretedMmol,
    double& outUreaExcretedMmol,
    double& outGlucoseExcretedMmol) {
    if (throughput_) throughput_->recordReadWrite<NephronSegmentState>(tatarus::ThroughputDomain::Kidney, 6);
    if (throughput_) throughput_->recordReadWrite<Kidney>(tatarus::ThroughputDomain::Kidney);
    const double gfrMlPerMin = telemetry_.glomerularFiltrationRateMlPerMin;

    // Filtered loads per minute: Load = GFR (L/min) * Concentration (mmol/L)
    const double gfrLPerMin = gfrMlPerMin / 1000.0;
    const double filteredNaMmolPerMin = gfrLPerMin * plasmaSolutes.naMm;
    const double filteredKMmolPerMin = gfrLPerMin * plasmaSolutes.kMm;
    const double filteredCaMmolPerMin = gfrLPerMin * plasmaSolutes.caIonizedMm;
    const double filteredClMmolPerMin = gfrLPerMin * plasmaSolutes.clMm;
    const double filteredHco3MmolPerMin = gfrLPerMin * plasmaSolutes.hco3Mm;
    const double filteredUreaMmolPerMin = gfrLPerMin * plasmaSolutes.ureaMm;
    const double filteredGlucoseMmolPerMin = gfrLPerMin * plasmaSolutes.glucoseMm;

    // 1. Proximal Convoluted Tubule (PCT):
    // - 67% iso-osmotic water and Na+ reabsorption via NHE3 and Na/K-ATPase
    // - 100% glucose reabsorption via SGLT up to Tmax (2.1 mmol/min)
    // - 85% HCO3- reabsorption via NHE3 and carbonic anhydrase
    // - 50% urea reabsorption
    const double pctNaReabsFraction = 0.67;
    const double pctGlucoseReabsMmolPerMin = std::min(
        filteredGlucoseMmolPerMin,
        config_.glucoseTransportMaxMmolPerMin * config_.functionalMassFraction);
    const double pctGlucoseRemainingMmolPerMin = filteredGlucoseMmolPerMin - pctGlucoseReabsMmolPerMin;
    const double pctHco3ReabsMmolPerMin = 0.85 * filteredHco3MmolPerMin;

    telemetry_.proximalTubuleNaReabsorption = pctNaReabsFraction;

    // Fluid leaving PCT:
    double flowLeavingPctMlPerMin = gfrMlPerMin * (1.0 - pctNaReabsFraction);
    double naLeavingPctMmolPerMin = filteredNaMmolPerMin * (1.0 - pctNaReabsFraction);
    double kLeavingPctMmolPerMin = filteredKMmolPerMin * 0.35;
    double caLeavingPctMmolPerMin = filteredCaMmolPerMin * 0.35;
    double clLeavingPctMmolPerMin = filteredClMmolPerMin * 0.35;
    double ureaLeavingPctMmolPerMin = filteredUreaMmolPerMin * 0.50;
    double hco3LeavingPctMmolPerMin = filteredHco3MmolPerMin - pctHco3ReabsMmolPerMin;

    // Osmotic effect of unabsorbed glucose (e.g. hyperglycemia -> osmotic diuresis)
    if (pctGlucoseRemainingMmolPerMin > 0.0) {
        flowLeavingPctMlPerMin += (pctGlucoseRemainingMmolPerMin / 0.15); // osmotic water drag
    }

    // 2. Loop of Henle:
    // - Thin Descending Limb (tDLH): Water reabsorption driven by medullary hypertonicity
    // - Thick Ascending Limb (TAL): Active NKCC2 pumping (25% of filtered Na+) without water -> Diluting segment & building medullary gradient
    const double talNaPumpFraction = 0.25;
    const double talNaPumpMmolPerMin = filteredNaMmolPerMin * talNaPumpFraction;
    naLeavingPctMmolPerMin = std::max(0.0, naLeavingPctMmolPerMin - talNaPumpMmolPerMin);
    clLeavingPctMmolPerMin = std::max(0.0, clLeavingPctMmolPerMin - talNaPumpMmolPerMin * 1.05);
    kLeavingPctMmolPerMin = std::max(0.0, kLeavingPctMmolPerMin - talNaPumpMmolPerMin * 0.1);

    // Medullary Osmotic Gradient generation from NKCC2 pumping
    medullaryOsmolarityMOsmPerKg_ = 300.0 + 900.0 * std::clamp(talNaPumpMmolPerMin / 4.5, 0.2, 1.4);
    telemetry_.medullaryOsmoticGradientMOsm = medullaryOsmolarityMOsmPerKg_;
    telemetry_.henleLoopNaReabsorption = talNaPumpFraction;

    // Macula Densa delivery (sensed at end of TAL / start of DCT)
    maculaDensaNaDeliveryMmolPerMin_ = naLeavingPctMmolPerMin;

    // 3. Distal Convoluted Tubule (DCT):
    // - 5% Na+ reabsorption via NCC
    const double dctNaReabsMmolPerMin = filteredNaMmolPerMin * 0.05;
    naLeavingPctMmolPerMin = std::max(0.0, naLeavingPctMmolPerMin - dctNaReabsMmolPerMin);
    clLeavingPctMmolPerMin = std::max(0.0, clLeavingPctMmolPerMin - dctNaReabsMmolPerMin);
    telemetry_.distalTubuleNaReabsorption = 0.05;

    // 4. Collecting Duct (CD):
    // - ENaC Na+ reabsorption stimulated by Aldosterone
    // - ROMK K+ secretion stimulated by Aldosterone & high flow
    // - Aquaporin-2 water reabsorption stimulated by ADH / Vasopressin
    // - Type A intercalated cell H+ secretion (acid excretion)
    const double aldoFactor = std::clamp(hormones.aldosteronePm / 200.0, 0.2, 3.5);
    const double cdNaReabsFraction = 0.025 * aldoFactor;
    const double cdNaReabsMmolPerMin = filteredNaMmolPerMin * cdNaReabsFraction;
    const double mass = config_.functionalMassFraction;
    const double excretedNaMmolPerMin = std::max(0.01 * mass, naLeavingPctMmolPerMin - cdNaReabsMmolPerMin);
    telemetry_.collectingDuctNaReabsorption = cdNaReabsFraction;

    // K+ excretion in CD (coupled to Na+ reabsorption and Aldosterone)
    const double cdKSecretionMmolPerMin = 0.03 * aldoFactor * (plasmaSolutes.kMm / 4.0);
    const double excretedKMmolPerMin = std::max(0.01 * mass, kLeavingPctMmolPerMin + cdKSecretionMmolPerMin);

    // ADH / Vasopressin Water Reabsorption:
    // High ADH -> high water permeability -> urine concentrates up to medullary osmolarity (~1200 mOsm) -> low urine volume
    // Low ADH -> water impermeable -> dilute urine (~50 mOsm) -> high urine volume
    const double adhFactor = std::clamp(hormones.vasopressinPgPerMl / 2.0, 0.1, 5.0);
    const double maxUrineOsm = 50.0 + (medullaryOsmolarityMOsmPerKg_ - 50.0) * (adhFactor / (adhFactor + 0.5));
    telemetry_.urineOsmolarityMOsmPerKg = std::clamp(maxUrineOsm, 50.0, 1400.0);

    // Total excreted osmoles (Na+ + K+ + Cl- + Urea + Glucose):
    const double excretedOsmolesMOsmPerMin = (excretedNaMmolPerMin * 2.0) + (excretedKMmolPerMin * 2.0) +
                                            (ureaLeavingPctMmolPerMin) + (pctGlucoseRemainingMmolPerMin);

    // Urine Output Rate (mL/min) = (Excreted Osmoles / Urine Osmolarity) * 1000
    const double calculatedUrineFlowMlPerMin = (excretedOsmolesMOsmPerMin / telemetry_.urineOsmolarityMOsmPerKg) * 1000.0;
    telemetry_.urineOutputRateMlPerMin = std::clamp(calculatedUrineFlowMlPerMin, 0.2 * mass, 20.0 * mass);

    // Excretion metrics
    telemetry_.urineNaExcretionMmolPerMin = excretedNaMmolPerMin;
    telemetry_.urineKExcretionMmolPerMin = excretedKMmolPerMin;
    telemetry_.urineCaExcretionMmolPerMin = std::max(0.001 * mass, caLeavingPctMmolPerMin * 0.1);
    telemetry_.urineUreaExcretionMmolPerMin = ureaLeavingPctMmolPerMin * 0.8;
    telemetry_.glucoseExcretionMmolPerMin = pctGlucoseRemainingMmolPerMin;

    // Urine pH & Acid Excretion:
    // If systemic blood is acidotic (low HCO3-), Type-A intercalated cells excrete more H+ -> Urine pH drops to ~4.8
    const double acidLoad = std::max(0.0, (24.0 - plasmaSolutes.hco3Mm) * 0.1);
    telemetry_.urinePh = std::clamp(6.0 - acidLoad * 1.2, 4.6, 8.0);
    const double excretedHco3MmolPerMin = (plasmaSolutes.hco3Mm > 28.0) ? (hco3LeavingPctMmolPerMin * 0.5) : 0.0;

    // Integration for dtSeconds:
    const double dtMinutes = dtSeconds / 60.0;
    outWaterExcretedMl = telemetry_.urineOutputRateMlPerMin * dtMinutes;
    outNaExcretedMmol = telemetry_.urineNaExcretionMmolPerMin * dtMinutes;
    outKExcretedMmol = telemetry_.urineKExcretionMmolPerMin * dtMinutes;
    outCaExcretedMmol = telemetry_.urineCaExcretionMmolPerMin * dtMinutes;
    outClExcretedMmol = (excretedNaMmolPerMin + excretedKMmolPerMin) * dtMinutes;
    outHco3ExcretedMmol = excretedHco3MmolPerMin * dtMinutes;
    outUreaExcretedMmol = telemetry_.urineUreaExcretionMmolPerMin * dtMinutes;
    outGlucoseExcretedMmol = telemetry_.glucoseExcretionMmolPerMin * dtMinutes;
}

namespace {

KidneyConfig halfKidneyConfig(KidneyConfig combinedConfig) {
    combinedConfig.functionalMassFraction =
        std::clamp(combinedConfig.functionalMassFraction, 0.0, 1.0) * 0.5;
    return combinedConfig;
}

double mean(double left, double right) {
    return 0.5 * (left + right);
}

} // namespace

BilateralKidneys::BilateralKidneys(KidneyConfig combinedConfig)
    : left_(halfKidneyConfig(combinedConfig)),
      right_(halfKidneyConfig(combinedConfig)) {
    updateAggregateTelemetry();
}

void BilateralKidneys::setRelativeFunction(double leftFraction, double rightFraction) noexcept {
    left_.setFunctionalMassFraction(0.5 * std::clamp(leftFraction, 0.0, 1.0));
    right_.setFunctionalMassFraction(0.5 * std::clamp(rightFraction, 0.0, 1.0));
    updateAggregateTelemetry();
}

void BilateralKidneys::step(
    double dtSeconds,
    double meanArterialPressureMmHg,
    const SoluteProfile& plasmaSolutes,
    const EndocrineProfile& hormones,
    double& outWaterExcretedMl,
    double& outNaExcretedMmol,
    double& outKExcretedMmol,
    double& outCaExcretedMmol,
    double& outClExcretedMmol,
    double& outHco3ExcretedMmol,
    double& outUreaExcretedMmol,
    double& outGlucoseExcretedMmol) {
    std::array<double, 8> leftOutputs{};
    std::array<double, 8> rightOutputs{};
    left_.step(dtSeconds, meanArterialPressureMmHg, plasmaSolutes, hormones,
               leftOutputs[0], leftOutputs[1], leftOutputs[2], leftOutputs[3],
               leftOutputs[4], leftOutputs[5], leftOutputs[6], leftOutputs[7]);
    right_.step(dtSeconds, meanArterialPressureMmHg, plasmaSolutes, hormones,
                rightOutputs[0], rightOutputs[1], rightOutputs[2], rightOutputs[3],
                rightOutputs[4], rightOutputs[5], rightOutputs[6], rightOutputs[7]);

    outWaterExcretedMl = leftOutputs[0] + rightOutputs[0];
    outNaExcretedMmol = leftOutputs[1] + rightOutputs[1];
    outKExcretedMmol = leftOutputs[2] + rightOutputs[2];
    outCaExcretedMmol = leftOutputs[3] + rightOutputs[3];
    outClExcretedMmol = leftOutputs[4] + rightOutputs[4];
    outHco3ExcretedMmol = leftOutputs[5] + rightOutputs[5];
    outUreaExcretedMmol = leftOutputs[6] + rightOutputs[6];
    outGlucoseExcretedMmol = leftOutputs[7] + rightOutputs[7];
    updateAggregateTelemetry();
}

void BilateralKidneys::updateAggregateTelemetry() noexcept {
    const auto& l = left_.telemetry();
    const auto& r = right_.telemetry();
    aggregate_.available = l.available && r.available;
    aggregate_.renalBloodFlowMlPerMin = l.renalBloodFlowMlPerMin + r.renalBloodFlowMlPerMin;
    aggregate_.renalPlasmaFlowMlPerMin = l.renalPlasmaFlowMlPerMin + r.renalPlasmaFlowMlPerMin;
    aggregate_.glomerularFiltrationRateMlPerMin =
        l.glomerularFiltrationRateMlPerMin + r.glomerularFiltrationRateMlPerMin;
    aggregate_.filtrationFraction = aggregate_.glomerularFiltrationRateMlPerMin /
        std::max(1.0, aggregate_.renalPlasmaFlowMlPerMin);
    aggregate_.glomerularCapillaryHydrostaticPressureMmHg = mean(
        l.glomerularCapillaryHydrostaticPressureMmHg, r.glomerularCapillaryHydrostaticPressureMmHg);
    aggregate_.bowmanSpaceHydrostaticPressureMmHg = mean(
        l.bowmanSpaceHydrostaticPressureMmHg, r.bowmanSpaceHydrostaticPressureMmHg);
    aggregate_.glomerularCapillaryOncoticPressureMmHg = mean(
        l.glomerularCapillaryOncoticPressureMmHg, r.glomerularCapillaryOncoticPressureMmHg);
    aggregate_.netUltrafiltrationPressureMmHg = mean(
        l.netUltrafiltrationPressureMmHg, r.netUltrafiltrationPressureMmHg);
    aggregate_.proximalTubuleNaReabsorption = mean(
        l.proximalTubuleNaReabsorption, r.proximalTubuleNaReabsorption);
    aggregate_.henleLoopNaReabsorption = mean(l.henleLoopNaReabsorption, r.henleLoopNaReabsorption);
    aggregate_.distalTubuleNaReabsorption = mean(
        l.distalTubuleNaReabsorption, r.distalTubuleNaReabsorption);
    aggregate_.collectingDuctNaReabsorption = mean(
        l.collectingDuctNaReabsorption, r.collectingDuctNaReabsorption);
    aggregate_.medullaryOsmoticGradientMOsm = mean(
        l.medullaryOsmoticGradientMOsm, r.medullaryOsmoticGradientMOsm);
    aggregate_.urineOutputRateMlPerMin = l.urineOutputRateMlPerMin + r.urineOutputRateMlPerMin;
    aggregate_.urineSpecificGravity = mean(l.urineSpecificGravity, r.urineSpecificGravity);
    aggregate_.urineOsmolarityMOsmPerKg = mean(l.urineOsmolarityMOsmPerKg, r.urineOsmolarityMOsmPerKg);
    aggregate_.urinePh = mean(l.urinePh, r.urinePh);
    aggregate_.urineNaExcretionMmolPerMin =
        l.urineNaExcretionMmolPerMin + r.urineNaExcretionMmolPerMin;
    aggregate_.urineKExcretionMmolPerMin =
        l.urineKExcretionMmolPerMin + r.urineKExcretionMmolPerMin;
    aggregate_.urineCaExcretionMmolPerMin =
        l.urineCaExcretionMmolPerMin + r.urineCaExcretionMmolPerMin;
    aggregate_.urineUreaExcretionMmolPerMin =
        l.urineUreaExcretionMmolPerMin + r.urineUreaExcretionMmolPerMin;
    aggregate_.glucoseExcretionMmolPerMin =
        l.glucoseExcretionMmolPerMin + r.glucoseExcretionMmolPerMin;
    aggregate_.reninSecretionRate = mean(l.reninSecretionRate, r.reninSecretionRate);
    aggregate_.circulatingAngiotensinIIPm = mean(
        l.circulatingAngiotensinIIPm, r.circulatingAngiotensinIIPm);
    aggregate_.circulatingAldosteronePm = mean(
        l.circulatingAldosteronePm, r.circulatingAldosteronePm);
    aggregate_.circulatingVasopressinPgPerMl = mean(
        l.circulatingVasopressinPgPerMl, r.circulatingVasopressinPgPerMl);
}

} // namespace tatarus::organism
