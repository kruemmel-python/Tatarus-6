#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tatarus::organism {

// Blood circulation compartments
enum class CirculationCompartment : std::uint8_t {
    Arterial,
    Venous,
    PulmonaryArtery,
    PulmonaryCapillary,
    PulmonaryVein,
    Cerebral,
    Renal,
    Coronary,
    Peripheral, // Muscular / Body Frame
    Count
};

// Cardiac chambers and valves
enum class CardiacChamber : std::uint8_t {
    RightAtrium,
    RightVentricle,
    LeftAtrium,
    LeftVentricle,
    Count
};

enum class CardiacValve : std::uint8_t {
    Tricuspid, // RA -> RV
    Pulmonic,  // RV -> Pulmonary Artery
    Mitral,    // LA -> LV
    Aortic,    // LV -> Aorta
    Count
};

// Electrical conduction node states
enum class CardiacElectricalPhase : std::uint8_t {
    Quiescent,
    PacemakerDepolarizing,
    ActionPotentialPlateau,
    Repolarizing,
    Refractory
};

// Environmental atmosphere conditions supplied by robot external sensors
struct AtmosphericEnvironment {
    double totalPressureKPa = 101.325; // Standard 1 atm ~ 101.325 kPa (Earth)
    double o2Fraction = 0.2095;         // 20.95% O2
    double co2Fraction = 0.0004;        // 0.04% CO2
    double n2Fraction = 0.7808;         // 78.08% N2
    double temperatureC = 20.0;         // 20 deg C
    double relativeHumidity = 0.50;     // 50%
    double dustPpm = 0.0;               // Particulate dust in parts per million
};

// Chemical composition in a fluid compartment (concentrations in mmol/L or mM)
struct SoluteProfile {
    double o2DissolvedMm = 0.127;       // Dissolved O2 ~ 0.127 mM (~95 mmHg PO2)
    double o2BoundHemoglobinMm = 8.70;  // Bound O2 ~ 8.70 mM (97% sat at 15 g/dL Hb)
    double co2DissolvedMm = 1.23;       // Dissolved CO2 ~ 1.23 mM (~40 mmHg PCO2)
    double hco3Mm = 24.0;               // Bicarbonate ~ 24 mM
    double glucoseMm = 5.0;             // Fasting glucose ~ 5.0 mM (90 mg/dL)
    double naMm = 142.0;                // Sodium ~ 142 mM
    double kMm = 4.0;                   // Potassium ~ 4.0 mM
    double caIonizedMm = 1.25;          // Ionized Calcium ~ 1.25 mM
    double clMm = 103.0;                // Chloride ~ 103 mM
    double ureaMm = 5.0;                // Urea ~ 5.0 mM (BUN ~ 14 mg/dL)
    double lactateMm = 1.0;             // Lactate ~ 1.0 mM
    double plasmaProteinGPerL = 70.0;   // 70 g/L (albumin + globulins)
};

// Endogenous circulating hormones and regulatory peptides
struct EndocrineProfile {
    double adrenalinePgPerMl = 30.0;     // Epinephrine basal ~ 30 pg/mL
    double noradrenalinePgPerMl = 250.0; // Norepinephrine basal ~ 250 pg/mL
    double reninUPerMl = 1.0;            // Plasma renin activity normalized
    double angiotensinIIPm = 15.0;       // Angiotensin II ~ 15 pmol/L
    double aldosteronePm = 200.0;        // Aldosterone ~ 200 pmol/L
    double vasopressinPgPerMl = 2.0;     // ADH / Arginine Vasopressin ~ 2 pg/mL
    double anpPgPerMl = 20.0;            // Atrial Natriuretic Peptide ~ 20 pg/mL
};

// Physical robot chassis / motor load
struct RobotPhysicalLoad {
    double motorCurrentA = 2.0;
    double motorVoltageV = 24.0;
    double jointTorqueNm = 5.0;
    double angularVelocityRadPerS = 3.14;
    double mechanicalPowerW = 15.7;
    double cpuGpuPowerW = 25.0;
    double batteryChargeRemainingFraction = 0.95;
    double chassisTemperatureC = 25.0;
};

// Autonomic nervous system efferent tone
struct AutonomicOutput {
    double sympatheticTone = 0.25;      // 0.0 to 1.0
    double parasympatheticTone = 0.75;  // 0.0 to 1.0
};

// Explicit Interoception state as sensory inputs for brain / embodiment
struct InteroceptionState {
    double arterialPressure = 93.3;     // MAP in mmHg
    double venousPressure = 4.0;        // CVP in mmHg
    double cardiacLoad = 0.0;           // Normalized 0 to 1
    double heartRate = 72.0;            // bpm
    double arterialO2 = 95.0;           // PaO2 in mmHg
    double arterialCO2 = 40.0;          // PaCO2 in mmHg
    double bloodPH = 7.40;              // Arterial pH
    double glucose = 5.0;               // mM
    double sodium = 142.0;              // mM
    double potassium = 4.0;             // mM
    double calcium = 1.25;              // mM
    double osmolarity = 290.0;          // mOsm/kg
    double bodyTemperature = 37.0;      // deg C
    double renalStress = 0.0;           // Normalized 0 to 1
    double hypoxia = 0.0;               // Fractional arterial desaturation
    double hypercapnia = 0.0;           // Elevated PaCO2 index
    double visceralDistress = 0.0;      // Global homeostatic stress index
    double metabolicStress = 0.0;       // Energy depletion index
};

// Telemetry from the vascular circulation
struct CirculationTelemetry {
    bool available = false;
    double totalBloodVolumeL = 5.0;
    double arterialVolumeL = 0.75;
    double venousVolumeL = 3.25;
    double meanArterialPressureMmHg = 93.3;
    double systolicPressureMmHg = 120.0;
    double diastolicPressureMmHg = 80.0;
    double centralVenousPressureMmHg = 4.0;
    double pulmonaryArteryPressureMmHg = 15.0;
    double totalPeripheralResistanceMmHgSPerMl = 1.0;

    // Global systemic blood gas & electrolyte metrics
    double arterialPo2MmHg = 95.0;
    double venousPo2MmHg = 40.0;
    double arterialPco2MmHg = 40.0;
    double venousPco2MmHg = 46.0;
    double arterialOxygenSaturation = 0.98;
    double arterialPh = 7.40;
    double venousPh = 7.36;
    double plasmaGlucoseMm = 5.0;
    double plasmaNaMm = 142.0;
    double plasmaKMm = 4.0;
    double plasmaCaMm = 1.25;
    double plasmaClMm = 103.0;
    double plasmaHco3Mm = 24.0;
    double plasmaUreaMm = 5.0;
    double plasmaLactateMm = 1.0;
    double bloodTemperatureC = 37.0;

    // Flow distributions (mL/s)
    double cerebralBloodFlowMlPerS = 12.5; // ~750 mL/min
    double renalBloodFlowMlPerS = 20.0;    // ~1200 mL/min
    double coronaryBloodFlowMlPerS = 4.0;  // ~240 mL/min
    double peripheralBloodFlowMlPerS = 45.0;// ~2700 mL/min
    double totalCardiacOutputLPerMin = 5.0;
};

// Telemetry from the electromechanical heart
struct HeartTelemetry {
    bool available = false;
    double heartRateBpm = 72.0;
    double strokeVolumeMl = 70.0;
    double cardiacOutputLPerMin = 5.04;
    double ejectionFractionLv = 0.60;
    double ejectionFractionRv = 0.60;

    // Chamber Pressures (mmHg)
    double rightAtriumPressureMmHg = 3.5;
    double rightVentriclePressureMmHg = 22.0;
    double leftAtriumPressureMmHg = 7.5;
    double leftVentriclePressureMmHg = 118.0;

    // Chamber Volumes (mL)
    double endDiastolicVolumeLvMl = 120.0;
    double endSystolicVolumeLvMl = 50.0;
    double endDiastolicVolumeRvMl = 120.0;
    double endSystolicVolumeRvMl = 50.0;

    // Electrophysiology
    double saNodeMembranePotentialMv = -60.0;
    double atrialPotentialMv = -80.0;
    double avNodePotentialMv = -65.0;
    double purkinjePotentialMv = -85.0;
    double ventricularMyocardiumPotentialMv = -85.0;
    double intracellularCalciumLvMicromolar = 0.1;
    double sarcoplasmicReticulumCalciumMm = 1.2;
    double sarcomereActiveTensionKPa = 0.0;
    bool saNodeFired = false;
    bool avNodeFired = false;
    bool ventriclesDepolarized = false;

    // Valve states (open fraction 0.0 to 1.0)
    double tricuspidValveOpen = 0.0;
    double pulmonicValveOpen = 0.0;
    double mitralValveOpen = 0.0;
    double aorticValveOpen = 0.0;

    // Energetics & Metabolic Cost
    double myocardialO2ConsumptionMlPerMin = 25.0;
    double myocardialGlucoseConsumptionMmolPerMin = 0.15;
    double coronaryPerfusionPressureMmHg = 75.0;
    double ischemicStressIndex = 0.0; // 0.0 = adequate O2, >0 = hypoxic myocardium
};

// Telemetry from the respiratory lung
struct LungTelemetry {
    bool available = false;
    double respirationRateBpm = 14.0;
    double tidalVolumeL = 0.50;
    double minuteVentilationLPerMin = 7.0;
    double alveolarPo2MmHg = 102.0;
    double alveolarPco2MmHg = 40.0;
    double o2UptakeRateMlPerMin = 250.0;
    double co2EliminationRateMlPerMin = 200.0;
    double respiratoryExchangeRatio = 0.80;
    double pulmonaryVascularResistance = 0.12;
    double alveolarHypoxiaIndex = 0.0;
    double centralChemoreceptorDrive = 1.0;
    double peripheralChemoreceptorDrive = 1.0;
    double airwayResistance = 1.5;
    double lungCompliance = 0.10;
    double vqMismatchIndex = 0.0;
    double particulateLoadFilterFraction = 0.0;
};

// Telemetry from the nephron kidney
struct KidneyTelemetry {
    bool available = false;
    double renalBloodFlowMlPerMin = 1200.0;
    double renalPlasmaFlowMlPerMin = 660.0;
    double glomerularFiltrationRateMlPerMin = 125.0; // GFR
    double filtrationFraction = 0.19; // GFR / RPF

    // Glomerular & Tubule Pressures (mmHg)
    double glomerularCapillaryHydrostaticPressureMmHg = 55.0;
    double bowmanSpaceHydrostaticPressureMmHg = 15.0;
    double glomerularCapillaryOncoticPressureMmHg = 28.0;
    double netUltrafiltrationPressureMmHg = 12.0;

    // Segmental reabsorption fractions
    double proximalTubuleNaReabsorption = 0.67;
    double henleLoopNaReabsorption = 0.25;
    double distalTubuleNaReabsorption = 0.05;
    double collectingDuctNaReabsorption = 0.025;
    double medullaryOsmoticGradientMOsm = 1200.0;

    // Excretion rates
    double urineOutputRateMlPerMin = 1.0; // ~1.44 L/day
    double urineSpecificGravity = 1.015;
    double urineOsmolarityMOsmPerKg = 600.0;
    double urinePh = 6.0;
    double urineNaExcretionMmolPerMin = 0.14;
    double urineKExcretionMmolPerMin = 0.05;
    double urineCaExcretionMmolPerMin = 0.003;
    double urineUreaExcretionMmolPerMin = 0.30;
    double glucoseExcretionMmolPerMin = 0.0; // 0 when below renal threshold

    // RAAS and Hormones
    double reninSecretionRate = 1.0;
    double circulatingAngiotensinIIPm = 15.0;
    double circulatingAldosteronePm = 200.0;
    double circulatingVasopressinPgPerMl = 2.0;
};

// Interoceptive signals synthesized for the robot nervous system
struct InteroceptionTelemetry {
    bool available = false;
    double visceralDistress = 0.0;       // 0.0 = optimal homeostasis, 1.0 = acute failure
    double cardiovascularLoad = 0.0;     // Fractional cardiac strain
    double metabolicDepletion = 0.0;     // Fractional depletion of ATP / glucose
    double respiratoryHypoxia = 0.0;     // Arterial desaturation stress
    double fluidElectrolyteImbalance = 0.0; // Deviations in Na+, K+, Osmolarity, Blood volume
    double thermalStress = 0.0;          // Deviation from 37.0 C
    double baroreceptorAfferentActivity = 0.5; // Stretch signal from carotid sinus
    double sympatheticTone = 0.2;        // Efferent sympathetic nervous drive
    double parasympatheticTone = 0.8;    // Efferent vagal parasympathetic drive
    InteroceptionState state;
};

// Complete combined telemetry of the Synthetic Organism
struct OrganismTelemetry {
    bool available = false;
    std::uint64_t stepCount = 0;
    double simulatedTimeSeconds = 0.0;
    CirculationTelemetry circulation;
    HeartTelemetry heart;
    LungTelemetry lung;
    // Aggregate kidney telemetry retained for compatibility, plus both
    // independently stateful anatomical organs for diagnosis and visualization.
    KidneyTelemetry kidney;
    KidneyTelemetry leftKidney;
    KidneyTelemetry rightKidney;
    InteroceptionTelemetry interoception;
};

} // namespace tatarus::organism
