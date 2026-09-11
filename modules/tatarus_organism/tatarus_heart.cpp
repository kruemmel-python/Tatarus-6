#include "tatarus_heart.hpp"

#include <algorithm>
#include <cmath>

namespace tatarus::organism {

namespace {

// Biophysical Constants for Myocardial Electrophysiology
constexpr double kGasConstant = 8.314;       // J / (mol * K)
constexpr double kFaraday = 96485.0;         // C / mol
constexpr double kBodyTempKelvin = 310.15;   // 37 deg C
constexpr double kRToverF = (kGasConstant * kBodyTempKelvin) / kFaraday * 1000.0; // ~26.7 mV

// Intracellular baseline ion concentrations
constexpr double kIntracellularKMm = 140.0;

// Nernst equilibrium potentials
double calculateNernstK(double extracellularKMm) {
    const double ratio = std::max(1.0, extracellularKMm) / kIntracellularKMm;
    return kRToverF * std::log(ratio); // ~ -95 mV at 4.0 mM [K+]o
}


} // namespace

Heart::Heart(HeartConfig config)
    : config_(config) {
    telemetry_.available = true;
    telemetry_.heartRateBpm = config_.baseIntrinsicHeartRateBpm;
    telemetry_.strokeVolumeMl = 70.0;
    telemetry_.cardiacOutputLPerMin = (telemetry_.heartRateBpm * telemetry_.strokeVolumeMl) / 1000.0;
    telemetry_.ejectionFractionLv = 0.60;
    telemetry_.ejectionFractionRv = 0.60;
}

void Heart::step(
    double dtSeconds,
    double systemicArterialPressureMmHg,
    double centralVenousPressureMmHg,
    double pulmonaryArteryPressureMmHg,
    double pulmonaryCapillaryPressureMmHg,
    double sympatheticTone,
    double parasympatheticTone,
    double coronaryO2Mm,
    double extracellularKMm,
    double extracellularCaMm) {
    if (throughput_) throughput_->recordReadWrite<Heart>(tatarus::ThroughputDomain::Heart);
    if (dtSeconds <= 0.0) return;

    currentSympathetic_ = sympatheticTone;
    currentParasympathetic_ = parasympatheticTone;

    // 1. Electrophysiology: Action potentials through SA -> Atria -> AV -> Purkinje -> Ventricles
    updateElectrophysiology(dtSeconds, sympatheticTone, parasympatheticTone, extracellularKMm, extracellularCaMm);

    // 2. Excitation-Contraction Coupling & Hemodynamics: Ca2+ -> Tension -> 4-Chamber Pressures -> 4 Valves -> Flows
    updateMechanicsAndValves(
        dtSeconds,
        systemicArterialPressureMmHg,
        centralVenousPressureMmHg,
        pulmonaryArteryPressureMmHg,
        pulmonaryCapillaryPressureMmHg,
        coronaryO2Mm);
}

void Heart::updateElectrophysiology(
    double dtSeconds,
    double sympatheticTone,
    double parasympatheticTone,
    double extracellularKMm,
    double extracellularCaMm) {
    if (throughput_) throughput_->recordReadWrite<Heart>(tatarus::ThroughputDomain::Heart);
    // 1. SA Node: Spontaneous Phase-4 Pacemaker Depolarization
    // If current (funny current) is shifted positive by Sympathetic tone (cAMP-dependent)
    // Parasympathetic / Vagal tone activates I_K,ACh causing hyperpolarization and reducing slope
    const double ek = calculateNernstK(extracellularKMm);
    const double symp = std::clamp(sympatheticTone, 0.0, 3.0);
    const double vagus = std::clamp(parasympatheticTone, 0.0, 3.0);

    // Phase 4 depolarization slope (dV/dt in mV/s)
    // Baseline intrinsic firing rate ~ 72 bpm -> Period ~ 0.833 s -> Phase 4 is ~0.65 s from -60 mV to -40 mV (slope ~ 30 mV/s)
    const double baseSlope = 32.0;
    const double sympBoost = 1.0 + 1.2 * symp;
    const double vagusInhibition = 1.0 / (1.0 + 1.5 * vagus);

    // Extracellular Potassium effect: Hyperkalemia depolarizes maximum diastolic potential (MDP)
    const double mdp = std::clamp(-65.0 + (extracellularKMm - 4.0) * 3.5, -75.0, -45.0);
    const double thresholdMv = -40.0;

    saFired_ = false;
    if (saNodePotentialMv_ < thresholdMv) {
        // Phase 4 pacemaker depolarization
        const double slope = baseSlope * sympBoost * vagusInhibition;
        saNodePotentialMv_ += slope * dtSeconds;
    } else {
        // Action potential upstroke and repolarization
        saFired_ = true;
        saNodePotentialMv_ = mdp; // reset to Maximum Diastolic Potential
    }

    telemetry_.saNodeMembranePotentialMv = saNodePotentialMv_;
    telemetry_.saNodeFired = saFired_;

    // Beat interval tracking & Instantaneous HR emergence
    const double targetHr = std::clamp(72.0 * sympBoost * vagusInhibition, 35.0, 210.0);
    telemetry_.heartRateBpm += 2.0 * dtSeconds * (targetHr - telemetry_.heartRateBpm);

    beatTimerS_ += dtSeconds;
    if (saFired_) {
        if (beatTimerS_ > 0.25) { // physiological refractory period cap ~240 bpm
            const double instantaneousHr = 60.0 / beatTimerS_;
            telemetry_.heartRateBpm = 0.5 * telemetry_.heartRateBpm + 0.5 * instantaneousHr;
            beatTimerS_ = 0.0;
        }
    }

    // 2. Atrial Conduction to AV Node
    // Atrial action potential follows SA node firing
    if (saFired_) {
        atrialPotentialMv_ = 20.0; // Peak Phase 0
        avDelayTimerS_ = 0.13 * (1.0 + 0.5 * vagus) / (1.0 + 0.3 * symp); // PR delay ~ 120-180 ms
    } else {
        // Atrial repolarization
        atrialPotentialMv_ += (ek - atrialPotentialMv_) * 8.0 * dtSeconds;
    }
    telemetry_.atrialPotentialMv = atrialPotentialMv_;

    // 3. AV Node & His-Purkinje Conduction Delay
    avFired_ = false;
    if (avDelayTimerS_ > 0.0) {
        avDelayTimerS_ -= dtSeconds;
        if (avDelayTimerS_ <= 0.0) {
            avFired_ = true;
            avNodePotentialMv_ = 15.0;
            purkinjePotentialMv_ = 25.0;
            ventriclesDepolarized_ = true;
            ventricularActionPotentialTimeS_ = 0.0;
        }
    } else {
        avNodePotentialMv_ += (-65.0 - avNodePotentialMv_) * 6.0 * dtSeconds;
        purkinjePotentialMv_ += (ek - purkinjePotentialMv_) * 10.0 * dtSeconds;
    }
    telemetry_.avNodePotentialMv = avNodePotentialMv_;
    telemetry_.purkinjePotentialMv = purkinjePotentialMv_;
    telemetry_.avNodeFired = avFired_;

    // 4. Ventricular Action Potential (Phases 0-4) & Intracellular Calcium Transient
    const double vRest = ek; // Resting potential tracks extracellular [K+] via Nernst
    const double apDurationS = 0.28 * (1.0 / std::sqrt(std::max(0.5, telemetry_.heartRateBpm / 72.0))); // Bazett-like APD restitution

    if (ventriclesDepolarized_) {
        ventricularActionPotentialTimeS_ += dtSeconds;
        if (ventricularActionPotentialTimeS_ < 0.02) {
            // Phase 0: Rapid depolarization
            ventricularPotentialMv_ = 25.0;
        } else if (ventricularActionPotentialTimeS_ < apDurationS) {
            // Phase 2: Plateau governed by L-type Calcium current
            ventricularPotentialMv_ = 5.0 - 15.0 * (ventricularActionPotentialTimeS_ / apDurationS);

            // Calcium influx & Ryanodine Receptor (RyR) CICR release from Sarcoplasmic Reticulum (SR)
            const double caInflux = 1.8 * (extracellularCaMm / 1.25) * (1.0 + 0.6 * symp);
            intracellularCaLv_ += caInflux * dtSeconds * 15.0;
            intracellularCaRv_ += caInflux * dtSeconds * 15.0;
            srCalciumLvMm_ = std::max(0.5, srCalciumLvMm_ - 0.05 * dtSeconds);
        } else {
            // Phase 3: Repolarization back to Phase 4 resting potential
            ventricularPotentialMv_ += (vRest - ventricularPotentialMv_) * 15.0 * dtSeconds;
            if (std::abs(ventricularPotentialMv_ - vRest) < 2.0) {
                ventricularPotentialMv_ = vRest;
                ventriclesDepolarized_ = false;
            }
        }
    } else {
        ventricularPotentialMv_ = vRest;
    }

    // SERCA pump reuptake & NCX extrusion of cytosolic Calcium
    const double sercaRate = 6.5 * (1.0 + 0.4 * symp);
    intracellularCaLv_ += (0.10 - intracellularCaLv_) * sercaRate * dtSeconds;
    intracellularCaRv_ += (0.10 - intracellularCaRv_) * sercaRate * dtSeconds;
    srCalciumLvMm_ += (1.2 - srCalciumLvMm_) * 0.8 * dtSeconds;

    intracellularCaLv_ = std::clamp(intracellularCaLv_, 0.05, 3.0);
    intracellularCaRv_ = std::clamp(intracellularCaRv_, 0.05, 3.0);

    telemetry_.ventricularMyocardiumPotentialMv = ventricularPotentialMv_;
    telemetry_.ventriclesDepolarized = ventriclesDepolarized_;
    telemetry_.intracellularCalciumLvMicromolar = intracellularCaLv_;
    telemetry_.sarcoplasmicReticulumCalciumMm = srCalciumLvMm_;
}

void Heart::updateMechanicsAndValves(
    double dtSeconds,
    double systemicArterialPressureMmHg,
    double centralVenousPressureMmHg,
    double pulmonaryArteryPressureMmHg,
    double pulmonaryCapillaryPressureMmHg,
    double coronaryO2Mm) {
    if (throughput_) throughput_->recordReadWrite<Heart>(tatarus::ThroughputDomain::Heart);
    // 1. Calcium-Troponin C binding & Crossbridge Active Tension
    // Hill equation for myofilament Calcium sensitivity (EC50 ~ 0.6 uM, Hill n = 2.5)
    constexpr double kCaEC50 = 0.60;
    constexpr double kCaHill = 2.5;

    const double caPoweredLv = std::pow(intracellularCaLv_, kCaHill);
    const double caPoweredRv = std::pow(intracellularCaRv_, kCaHill);
    const double ec50Powered = std::pow(kCaEC50, kCaHill);

    // Frank-Starling mechanism: End-diastolic sarcomere stretch increases myofilament sensitivity
    const double starlingFactorLv = 1.0 + 0.4 * std::clamp((volumeLvMl_ - config_.unstressedVolumeLvMl) / 95.0, 0.0, 1.8);
    const double starlingFactorRv = 1.0 + 0.4 * std::clamp((volumeRvMl_ - config_.unstressedVolumeRvMl) / 95.0, 0.0, 1.8);

    activeTensionLv_ = (caPoweredLv / (caPoweredLv + ec50Powered)) * starlingFactorLv;
    activeTensionRv_ = (caPoweredRv / (caPoweredRv + ec50Powered)) * starlingFactorRv;
    activeTensionAtria_ = (atrialPotentialMv_ > -30.0) ? 0.8 : 0.0;

    // Ischemic myocardial stress: if coronary O2 is inadequate, reduce active tension and contractility
    const double ischemicStress = std::clamp((0.10 - coronaryO2Mm) / 0.10, 0.0, 0.9);
    telemetry_.ischemicStressIndex = ischemicStress;
    activeTensionLv_ *= (1.0 - 0.7 * ischemicStress);
    activeTensionRv_ *= (1.0 - 0.7 * ischemicStress);

    telemetry_.sarcomereActiveTensionKPa = activeTensionLv_ * 80.0; // Peak active tension in kPa

    // 2. Chamber Pressures from Time-Varying Elastance & Passive Compliance
    // P(t) = P_passive(V) + E_active(t) * (V - V0)
    const double passiveLv = config_.minPassiveElastanceLv * std::max(0.0, volumeLvMl_ - config_.unstressedVolumeLvMl) *
                             std::exp(0.015 * std::max(0.0, volumeLvMl_ - 100.0));
    const double activeElastanceLv = activeTensionLv_ * config_.maxContractilityElastanceLv;
    pressureLvMmHg_ = passiveLv + activeElastanceLv * std::max(0.0, volumeLvMl_ - config_.unstressedVolumeLvMl);

    const double passiveRv = config_.minPassiveElastanceRv * std::max(0.0, volumeRvMl_ - config_.unstressedVolumeRvMl);
    const double activeElastanceRv = activeTensionRv_ * config_.maxContractilityElastanceRv;
    pressureRvMmHg_ = passiveRv + activeElastanceRv * std::max(0.0, volumeRvMl_ - config_.unstressedVolumeRvMl);

    pressureLaMmHg_ = 4.0 + (0.08 + activeTensionAtria_ * config_.maxContractilityElastanceLa) * std::max(0.0, volumeLaMl_ - config_.unstressedVolumeLaMl);
    pressureRaMmHg_ = 2.0 + (0.06 + activeTensionAtria_ * config_.maxContractilityElastanceRa) * std::max(0.0, volumeRaMl_ - config_.unstressedVolumeRaMl);

    // 3. Pressure-Driven Heart Valves & Volumetric Flows
    // Q_valve = max(0, deltaP / R_valve)
    const double rValv = config_.valveResistanceMmHgSPerMl;

    // Tricuspid: RA -> RV
    const double deltaPTricuspid = pressureRaMmHg_ - pressureRvMmHg_;
    double flowTricuspid = (deltaPTricuspid > 0.0) ? (deltaPTricuspid / rValv) : 0.0;
    const double maxTricuspidFlow = std::max(0.0, (volumeRaMl_ - 15.0) / dtSeconds);
    flowTricuspid = std::min(flowTricuspid, maxTricuspidFlow);
    telemetry_.tricuspidValveOpen = (deltaPTricuspid > 0.0) ? 1.0 : 0.0;

    // Pulmonic: RV -> Pulmonary Artery
    const double deltaPPulmonic = pressureRvMmHg_ - pulmonaryArteryPressureMmHg;
    double flowPulmonic = (deltaPPulmonic > 0.0) ? (deltaPPulmonic / rValv) : 0.0;
    const double maxPulmonicFlow = std::max(0.0, (volumeRvMl_ - 25.0) / dtSeconds);
    flowPulmonic = std::min(flowPulmonic, maxPulmonicFlow);
    telemetry_.pulmonicValveOpen = (deltaPPulmonic > 0.0) ? 1.0 : 0.0;

    // Mitral: LA -> LV
    const double deltaPMitral = pressureLaMmHg_ - pressureLvMmHg_;
    double flowMitral = (deltaPMitral > 0.0) ? (deltaPMitral / rValv) : 0.0;
    const double maxMitralFlow = std::max(0.0, (volumeLaMl_ - 15.0) / dtSeconds);
    flowMitral = std::min(flowMitral, maxMitralFlow);
    telemetry_.mitralValveOpen = (deltaPMitral > 0.0) ? 1.0 : 0.0;

    // Aortic: LV -> Systemic Aorta
    const double deltaPAortic = pressureLvMmHg_ - systemicArterialPressureMmHg;
    double flowAortic = (deltaPAortic > 0.0) ? (deltaPAortic / rValv) : 0.0;
    const double maxAorticFlow = std::max(0.0, (volumeLvMl_ - 25.0) / dtSeconds);
    flowAortic = std::min(flowAortic, maxAorticFlow);
    telemetry_.aorticValveOpen = (deltaPAortic > 0.0) ? 1.0 : 0.0;

    // Venous return to atria
    // CVP approximates right-atrial pressure; systemic venous return is driven
    // by the upstream mean systemic filling pressure, not by CVP alone.
    const double meanSystemicFillingPressureMmHg = centralVenousPressureMmHg + 5.0;
    const double flowVenousReturnRa =
        std::max(0.0, meanSystemicFillingPressureMmHg - pressureRaMmHg_) / 0.045;
    const double flowVenousReturnLa = std::max(0.0, pulmonaryCapillaryPressureMmHg - pressureLaMmHg_) / 0.04;

    // 4. Update Chamber Volumes
    volumeRaMl_ += (flowVenousReturnRa - flowTricuspid) * dtSeconds;
    volumeRvMl_ += (flowTricuspid - flowPulmonic) * dtSeconds;
    volumeLaMl_ += (flowVenousReturnLa - flowMitral) * dtSeconds;
    volumeLvMl_ += (flowMitral - flowAortic) * dtSeconds;

    volumeRaMl_ = std::clamp(volumeRaMl_, 15.0, 150.0);
    volumeRvMl_ = std::clamp(volumeRvMl_, 20.0, 160.0);
    volumeLaMl_ = std::clamp(volumeLaMl_, 15.0, 150.0);
    volumeLvMl_ = std::clamp(volumeLvMl_, 20.0, 220.0);

    leftVentricularOutflowMlPerS_ = flowAortic;
    rightVentricularOutflowMlPerS_ = flowPulmonic;
    cycleAccumulatedLvOutflowMl_ += flowAortic * dtSeconds;

    // Track EDV and ESV
    if (activeTensionLv_ < 0.1 && volumeLvMl_ > peakEdvLv_) {
        peakEdvLv_ = volumeLvMl_;
    }
    if (activeTensionLv_ > 0.8 && volumeLvMl_ < peakEsvLv_) {
        peakEsvLv_ = volumeLvMl_;
    }

    if (saFired_) {
        lastStrokeVolumeMl_ = std::max(10.0, peakEdvLv_ - peakEsvLv_);
        telemetry_.strokeVolumeMl = lastStrokeVolumeMl_;
        telemetry_.ejectionFractionLv = std::clamp(lastStrokeVolumeMl_ / std::max(peakEdvLv_, 1.0), 0.1, 0.9);
        telemetry_.ejectionFractionRv = telemetry_.ejectionFractionLv;

        telemetry_.endDiastolicVolumeLvMl = peakEdvLv_;
        telemetry_.endSystolicVolumeLvMl = peakEsvLv_;
        telemetry_.endDiastolicVolumeRvMl = peakEdvRv_;
        telemetry_.endSystolicVolumeRvMl = peakEsvRv_;

        peakEdvLv_ = volumeLvMl_;
        peakEsvLv_ = volumeLvMl_;
        cycleAccumulatedLvOutflowMl_ = 0.0;
    }

    const double inotropy = (1.0 + 0.25 * currentSympathetic_) / (1.0 + 0.15 * currentParasympathetic_);
    telemetry_.strokeVolumeMl = std::clamp(lastStrokeVolumeMl_ * inotropy, 30.0, 110.0);
    telemetry_.cardiacOutputLPerMin = (telemetry_.heartRateBpm * telemetry_.strokeVolumeMl) / 1000.0;

    // 5. Myocardial Energetics (MVO2) from Pressure-Volume Area (PVA) + Inotropic Work
    // PVA = Stroke Work + End-Systolic Potential Energy
    const double strokeWork = lastStrokeVolumeMl_ * (systemicArterialPressureMmHg - centralVenousPressureMmHg) * 1.33322e-4; // Joules
    const double pva = strokeWork + 0.5 * pressureLvMmHg_ * (peakEsvLv_ - config_.unstressedVolumeLvMl) * 1.33322e-4;
    const double basalMvo2 = 8.0; // Basal cardiac consumption in mL O2 / min
    // Calibrated pressure-volume-area relation: normal workload yields roughly
    // 20-30 mL O2/min for the complete myocardium.
    const double mechanicalMvo2 = 15.0 * pva * (telemetry_.heartRateBpm / 60.0);
    telemetry_.myocardialO2ConsumptionMlPerMin = std::clamp(basalMvo2 + mechanicalMvo2, 10.0, 95.0);
    telemetry_.myocardialGlucoseConsumptionMmolPerMin = 0.006 * telemetry_.myocardialO2ConsumptionMlPerMin;

    // Coronary perfusion pressure = Diastolic Aortic Pressure - RV/LV intramyocardial pressure
    telemetry_.coronaryPerfusionPressureMmHg = std::max(10.0, systemicArterialPressureMmHg - 0.5 * pressureLvMmHg_);

    // Update telemetry pressures
    telemetry_.rightAtriumPressureMmHg = pressureRaMmHg_;
    telemetry_.rightVentriclePressureMmHg = pressureRvMmHg_;
    telemetry_.leftAtriumPressureMmHg = pressureLaMmHg_;
    telemetry_.leftVentriclePressureMmHg = pressureLvMmHg_;
}

} // namespace tatarus::organism
