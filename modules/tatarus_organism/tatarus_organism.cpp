#include "tatarus_organism.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace tatarus::organism {

namespace {

std::string formatJsonDouble(double value, int precision = 3) {
    if (!std::isfinite(value)) return "0.0";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

double centered(double value, double center, double radius) {
    return std::clamp((value - center) / radius, -1.0, 1.0);
}

template <typename T>
void writePod(std::ostream& stream, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool readPod(std::istream& stream, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(stream);
}

Experience addInteroceptiveAfferents(
    const Experience& external,
    const CirculationTelemetry& circulation,
    const InteroceptionTelemetry& interoception) {
    Experience embodied = external;
    // These afferents are deliberately appended to the ordinary experience
    // stream: the nervous tissue never receives privileged world coordinates,
    // only normalized internal receptor signals from the previous body tick.
    embodied.context.push_back(centered(circulation.meanArterialPressureMmHg, 93.3, 50.0));
    embodied.context.push_back(centered(circulation.centralVenousPressureMmHg, 4.0, 8.0));
    embodied.context.push_back(centered(circulation.arterialPo2MmHg, 95.0, 60.0));
    embodied.context.push_back(centered(circulation.arterialPco2MmHg, 40.0, 30.0));
    embodied.context.push_back(centered(circulation.arterialPh, 7.40, 0.50));
    embodied.context.push_back(centered(circulation.plasmaGlucoseMm, 5.0, 5.0));
    embodied.context.push_back(centered(circulation.plasmaKMm, 4.0, 3.0));
    embodied.context.push_back(2.0 * interoception.visceralDistress - 1.0);
    embodied.context.push_back(centered(interoception.sympatheticTone, 1.0, 2.0));
    embodied.context.push_back(centered(interoception.parasympatheticTone, 1.0, 1.5));
    return embodied;
}

ExperimentalIntervention physiologicalSupplyFromBody(
    const CirculationTelemetry& circulation,
    const ExperimentalIntervention& experiment) {
    ExperimentalIntervention intervention = experiment;
    intervention.oxygenSupply *= std::clamp(
        circulation.arterialOxygenSaturation / 0.98, 0.05, 1.10);
    intervention.glucoseSupply *= std::clamp(circulation.plasmaGlucoseMm / 5.0, 0.05, 1.50);
    const double potassiumPenalty = std::abs(circulation.plasmaKMm - 4.0) / 4.0;
    const double phPenalty = std::abs(circulation.arterialPh - 7.40) / 0.60;
    intervention.pumpEfficiency *= std::clamp(
        1.0 - 0.35 * potassiumPenalty - 0.25 * phPenalty, 0.10, 1.0);
    return intervention;
}

} // namespace

SyntheticOrganism::SyntheticOrganism(OrganismConfig config)
    : config_(config)
    , mind_(config.mind)
    , circulation_(config.circulation)
    , heart_(config.heart)
    , lung_(config.lung)
    , kidneys_(config.kidney) {
    interoception_.available = true;
    bindThroughputCounters();

    // Record initial mass and volume inventory into the ConservationLedger
    ledger_.recordInitialInventory(circulation_.totalSubstanceAmounts());
}


void SyntheticOrganism::bindThroughputCounters() noexcept {
    circulation_.setThroughputCounters(&organismThroughput_);
    heart_.setThroughputCounters(&organismThroughput_);
    lung_.setThroughputCounters(&organismThroughput_);
    kidneys_.setThroughputCounters(&organismThroughput_);
    ledger_.setThroughputCounters(&organismThroughput_);
}

SyntheticOrganism::SyntheticOrganism(SyntheticOrganism&& other) noexcept
    : config_(std::move(other.config_))
    , mind_(std::move(other.mind_))
    , circulation_(std::move(other.circulation_))
    , heart_(std::move(other.heart_))
    , lung_(std::move(other.lung_))
    , kidneys_(std::move(other.kidneys_))
    , ledger_(std::move(other.ledger_))
    , stepCount_(other.stepCount_)
    , totalSimulatedTimeS_(other.totalSimulatedTimeS_)
    , sympatheticTone_(other.sympatheticTone_)
    , parasympatheticTone_(other.parasympatheticTone_)
    , baroreceptorAfferent_(other.baroreceptorAfferent_)
    , interoception_(other.interoception_)
    , experimentalIntervention_(other.experimentalIntervention_)
    , organismThroughput_(other.organismThroughput_) {
    bindThroughputCounters();
}

SyntheticOrganism& SyntheticOrganism::operator=(SyntheticOrganism&& other) noexcept {
    if (this == &other) return *this;
    config_ = std::move(other.config_);
    mind_ = std::move(other.mind_);
    circulation_ = std::move(other.circulation_);
    heart_ = std::move(other.heart_);
    lung_ = std::move(other.lung_);
    kidneys_ = std::move(other.kidneys_);
    ledger_ = std::move(other.ledger_);
    stepCount_ = other.stepCount_;
    totalSimulatedTimeS_ = other.totalSimulatedTimeS_;
    sympatheticTone_ = other.sympatheticTone_;
    parasympatheticTone_ = other.parasympatheticTone_;
    baroreceptorAfferent_ = other.baroreceptorAfferent_;
    interoception_ = other.interoception_;
    experimentalIntervention_ = other.experimentalIntervention_;
    organismThroughput_ = other.organismThroughput_;
    bindThroughputCounters();
    return *this;
}

ObserveResult SyntheticOrganism::step(
    const Experience& experience,
    const AtmosphericEnvironment& atmosphere,
    double dtSeconds) {
    RobotPhysicalLoad defaultLoad{};
    return stepWithLoad(experience, atmosphere, defaultLoad, dtSeconds);
}

ObserveResult SyntheticOrganism::stepWithLoad(
    const Experience& experience,
    const AtmosphericEnvironment& atmosphere,
    const RobotPhysicalLoad& load,
    double dtSeconds) {
    return stepWithLoadImpl(experience, nullptr, atmosphere, load, dtSeconds).cognition;
}

ExplorerResult SyntheticOrganism::stepExplorer(
    const Experience& experience,
    const ScannerFrame& scannerFrame,
    const AtmosphericEnvironment& atmosphere,
    double dtSeconds) {
    RobotPhysicalLoad defaultLoad{};
    return stepExplorerWithLoad(
        experience, scannerFrame, atmosphere, defaultLoad, dtSeconds);
}

ExplorerResult SyntheticOrganism::stepExplorerWithLoad(
    const Experience& experience,
    const ScannerFrame& scannerFrame,
    const AtmosphericEnvironment& atmosphere,
    const RobotPhysicalLoad& load,
    double dtSeconds) {
    return stepWithLoadImpl(experience, &scannerFrame, atmosphere, load, dtSeconds);
}

ExplorerResult SyntheticOrganism::stepWithLoadImpl(
    const Experience& experience,
    const ScannerFrame* scannerFrame,
    const AtmosphericEnvironment& atmosphere,
    const RobotPhysicalLoad& load,
    double dtSeconds) {
    ++organismThroughput_.organismSteps;
    if (dtSeconds <= 0.0) {
        if (scannerFrame) return mind_.observeExplorer(experience, *scannerFrame);
        ExplorerResult result;
        result.cognition = mind_.observe(experience);
        return result;
    }

    // 1. Close the body -> nervous-system branch before advancing the brain.
    // Blood oxygen, glucose, electrolytes and internal receptor signals therefore
    // causally alter this same embodied execution instead of being UI-only data.
    mind_.setExperimentalIntervention(physiologicalSupplyFromBody(
        circulation_.telemetry(), experimentalIntervention_));
    const Experience embodiedExperience =
        addInteroceptiveAfferents(experience, circulation_.telemetry(), interoception_);
    ExplorerResult explorerResult;
    if (scannerFrame) {
        explorerResult = mind_.observeExplorer(embodiedExperience, *scannerFrame);
    } else {
        explorerResult.cognition = mind_.observe(embodiedExperience);
    }
    const BiologicalTelemetry brainBio = mind_.biology();
    const PhysiologyTelemetry brainPhys = mind_.physiology();

    // 2. Derive metabolic demands dynamically from actual Brain activity & Somatic Robot loading
    // Brain metabolic demand (Baseline ~ 45 mL O2/min, 0.30 mmol glucose/min)
    const double spikeMultiplier = std::clamp(1.0 + (brainBio.dendriticSpikes / 100.0) * 0.05, 0.8, 3.0);
    const double pumpMultiplier = std::clamp(brainPhys.pumpActivity, 0.5, 2.5);
    const double brainO2DemandMlPerMin = 45.0 * spikeMultiplier * pumpMultiplier;
    const double brainGlucoseDemandMmolPerMin = 0.30 * spikeMultiplier * pumpMultiplier;
    const double brainCo2ProductionMlPerMin = 0.95 * brainO2DemandMlPerMin;

    // Convert brain demands to mmol for dtSeconds:
    // 1 mmol gas ~ 25.4 mL
    const double dtMinutes = dtSeconds / 60.0;
    const double brainO2Mmol = (brainO2DemandMlPerMin * dtMinutes) / 25.4;
    const double brainGlucoseMmol = brainGlucoseDemandMmolPerMin * dtMinutes;
    const double brainCo2Mmol = (brainCo2ProductionMlPerMin * dtMinutes) / 25.4;
    const double brainLactateMmol = (brainBio.oxygen < 0.70) ? (0.05 * brainGlucoseMmol) : 0.0;

    // Somatic / Muscular / Robot mechanical load (Power W = tau * omega + I^2 * R + CPU/GPU)
    const double mechPowerW = std::max(0.0, load.mechanicalPowerW + load.cpuGpuPowerW);
    // 1 Watt ~ 1 Joule/s. 1 mL O2 aerobic metabolism ~ 20.1 Joules
    const double muscleO2DemandMlPerMin = (mechPowerW / (config_.mechanicalToMetabolicEfficiency * 20.1)) * 60.0;
    const double muscleO2Mmol = (muscleO2DemandMlPerMin * dtMinutes) / 25.4;
    const double muscleGlucoseMmol = (muscleO2DemandMlPerMin * 0.007) * dtMinutes;
    const double muscleCo2Mmol = 0.85 * muscleO2Mmol;
    const double muscleLactateMmol = (mechPowerW > 50.0) ? (0.10 * muscleGlucoseMmol) : 0.0;
    const double somaticHeatJoules = mechPowerW * dtSeconds;

    // 3. Heart step: Electrophysiology, Excitation-Contraction coupling, 4 chambers, 4 valves
    const double mapMmHg = circulation_.arterialPressureMmHg();
    const double cvpMmHg = circulation_.centralVenousPressureMmHg();
    const double papMmHg = circulation_.telemetry().pulmonaryArteryPressureMmHg;
    const double pcpMmHg = circulation_.pulmonaryCapillaryPressureMmHg();
    const double coronaryO2Mm = circulation_.arterialSolutes().o2DissolvedMm;
    const double extraKMm = circulation_.arterialSolutes().kMm;
    const double extraCaMm = circulation_.arterialSolutes().caIonizedMm;

    heart_.step(
        dtSeconds,
        mapMmHg,
        cvpMmHg,
        papMmHg,
        pcpMmHg,
        sympatheticTone_,
        parasympatheticTone_,
        coronaryO2Mm,
        extraKMm,
        extraCaMm);

    // The heart exposes instantaneous valve flows for waveforms and an
    // integrated beat-level cardiac output for the slower circulation solver.
    // Use the latter here so UI-sized dt values cannot alias away systolic flow.
    const double beatAveragedOutflowMlPerS =
        heart_.telemetry().cardiacOutputLPerMin * 1000.0 / 60.0;
    const double lvOutflowMlPerS = beatAveragedOutflowMlPerS;
    const double rvOutflowMlPerS = beatAveragedOutflowMlPerS;

    // Coronary metabolism
    const double coronaryO2Mmol = (heart_.myocardialO2ConsumptionMlPerMin() * dtMinutes) / 25.4;
    const double coronaryGlucoseMmol = heart_.myocardialGlucoseConsumptionMmolPerMin() * dtMinutes;
    const double coronaryCo2Mmol = 0.90 * coronaryO2Mmol;

    // 4. Lung step: Atmospheric physics, dynamic mechanics, V/Q compartments, alveolar diffusion
    double o2AddedFromLungMmol = 0.0;
    double co2RemovedFromLungMmol = 0.0;
    const double totalMetabolicCo2MlPerMin = brainCo2ProductionMlPerMin + (muscleO2DemandMlPerMin * 0.85) +
                                            (heart_.myocardialO2ConsumptionMlPerMin() * 0.90);

    lung_.step(
        dtSeconds,
        atmosphere,
        circulation_.telemetry().arterialPo2MmHg,
        circulation_.telemetry().arterialPco2MmHg,
        circulation_.telemetry().arterialPh,
        circulation_.telemetry().venousPo2MmHg,
        circulation_.telemetry().venousPco2MmHg,
        totalMetabolicCo2MlPerMin,
        o2AddedFromLungMmol,
        co2RemovedFromLungMmol);

    // 5. Both kidneys: two independent nephron states with summed excretion.
    double waterExcretedMl = 0.0;
    double naExcretedMmol = 0.0;
    double kExcretedMmol = 0.0;
    double caExcretedMmol = 0.0;
    double clExcretedMmol = 0.0;
    double hco3ExcretedMmol = 0.0;
    double ureaExcretedMmol = 0.0;
    double glucoseExcretedMmol = 0.0;

    kidneys_.step(
        dtSeconds,
        mapMmHg,
        circulation_.arterialSolutes(),
        circulation_.endocrine(),
        waterExcretedMl,
        naExcretedMmol,
        kExcretedMmol,
        caExcretedMmol,
        clExcretedMmol,
        hco3ExcretedMmol,
        ureaExcretedMmol,
        glucoseExcretedMmol);

    // 6. Circulation step & Multi-Compartment Solute Exchanges
    // Flow distribution demands (mL/s)
    const double cerebralDemandMlPerS = 12.5; // ~750 mL/min
    const double renalDemandMlPerS = (kidneys_.telemetry().renalBloodFlowMlPerMin / 60.0);
    const double coronaryDemandMlPerS = 4.0;
    const double peripheralDemandMlPerS = 45.0 * (1.0 + mechPowerW / 40.0);

    circulation_.step(
        dtSeconds,
        lvOutflowMlPerS,
        rvOutflowMlPerS,
        cerebralDemandMlPerS,
        renalDemandMlPerS,
        coronaryDemandMlPerS,
        peripheralDemandMlPerS,
        sympatheticTone_,
        circulation_.endocrine().angiotensinIIPm);

    // Apply strict mass-conserving solute exchanges
    circulation_.exchangePulmonary(
        o2AddedFromLungMmol,
        co2RemovedFromLungMmol,
        lung_.telemetry().alveolarPo2MmHg,
        lung_.telemetry().alveolarPco2MmHg);
    circulation_.exchangeCerebral(
        brainO2Mmol,
        brainGlucoseMmol,
        brainCo2Mmol,
        brainLactateMmol,
        0.0, 0.0, 0.0);
    circulation_.exchangeCoronary(coronaryO2Mmol, coronaryGlucoseMmol, coronaryCo2Mmol);
    circulation_.exchangePeripheral(
        muscleO2Mmol,
        muscleGlucoseMmol,
        muscleCo2Mmol,
        muscleLactateMmol,
        somaticHeatJoules);
    circulation_.exchangeRenal(
        waterExcretedMl,
        naExcretedMmol,
        kExcretedMmol,
        caExcretedMmol,
        clExcretedMmol,
        hco3ExcretedMmol,
        ureaExcretedMmol,
        glucoseExcretedMmol);

    // 7. Record All Metabolic Fluxes in the ConservationLedger
    ledger_.recordIntake(SubstanceId::O2, o2AddedFromLungMmol);
    ledger_.recordExcretion(SubstanceId::CO2, co2RemovedFromLungMmol);

    ledger_.recordExcretion(SubstanceId::Water, waterExcretedMl / 1000.0);
    ledger_.recordExcretion(SubstanceId::Na, naExcretedMmol);
    ledger_.recordExcretion(SubstanceId::K, kExcretedMmol);
    ledger_.recordExcretion(SubstanceId::Ca, caExcretedMmol);
    ledger_.recordExcretion(SubstanceId::Cl, clExcretedMmol);
    ledger_.recordExcretion(SubstanceId::HCO3, hco3ExcretedMmol);
    ledger_.recordExcretion(SubstanceId::Urea, ureaExcretedMmol);
    ledger_.recordExcretion(SubstanceId::Glucose, glucoseExcretedMmol);

    ledger_.recordConsumption(SubstanceId::O2, (brainO2Mmol + muscleO2Mmol + coronaryO2Mmol));
    ledger_.recordConsumption(SubstanceId::Glucose, (brainGlucoseMmol + muscleGlucoseMmol + coronaryGlucoseMmol));
    ledger_.recordProduction(SubstanceId::CO2, (brainCo2Mmol + muscleCo2Mmol + coronaryCo2Mmol));
    ledger_.recordProduction(SubstanceId::Lactate, (brainLactateMmol + muscleLactateMmol));

    // Endocrine hormone updates
    circulation_.updateHormones(
        dtSeconds,
        sympatheticTone_,
        kidneys_.reninSecretionRate(),
        kidneys_.reninSecretionRate(),
        kidneys_.telemetry().circulatingAldosteronePm / 200.0,
        interoception_.fluidElectrolyteImbalance + 1.0);

    // 8. Autonomic Regulation & Interoception synthesis
    updateAutonomicsAndInteroception(
        brainBio, brainPhys, circulation_.telemetry().meanArterialPressureMmHg);

    stepCount_++;
    totalSimulatedTimeS_ += dtSeconds;

    return explorerResult;
}

void SyntheticOrganism::updateAutonomicsAndInteroception(
    const BiologicalTelemetry& brainBio,
    const PhysiologyTelemetry& brainPhys,
    double mapMmHg) {
    organismThroughput_.recordReadWrite<InteroceptionTelemetry>(tatarus::ThroughputDomain::Interoception);
    // 1. Arterial Baroreflex (Carotid Sinus & Aortic Arch Stretch Receptors)
    // Sigmoidal afferent stretch activity centered at setpoint ~93.3 mmHg
    const double baroDelta = mapMmHg - 93.3;
    baroreceptorAfferent_ = 1.0 / (1.0 + std::exp(-baroDelta / 12.0));

    // Sympathetic tone is inhibited by high baroreceptor stretch, stimulated by low pressure or hypoxia
    const double hypoxiaStimulus = std::clamp((90.0 - circulation_.telemetry().arterialPo2MmHg) / 40.0, 0.0, 2.0);
    const double hypercapniaStimulus = std::clamp((circulation_.telemetry().arterialPco2MmHg - 40.0) / 20.0, 0.0, 2.0);
    const double autonomicStress = hypoxiaStimulus + hypercapniaStimulus;

    sympatheticTone_ = std::clamp(1.2 * (1.0 - baroreceptorAfferent_) + 0.4 * autonomicStress, 0.05, 3.0);
    parasympatheticTone_ = std::clamp(1.4 * baroreceptorAfferent_ - 0.3 * autonomicStress, 0.05, 2.5);

    // 2. Interoception Synthesis
    interoception_.baroreceptorAfferentActivity = baroreceptorAfferent_;
    interoception_.sympatheticTone = sympatheticTone_;
    interoception_.parasympatheticTone = parasympatheticTone_;

    interoception_.cardiovascularLoad = std::clamp((heart_.telemetry().heartRateBpm - 60.0) / 120.0, 0.0, 1.0);
    interoception_.respiratoryHypoxia = std::clamp((0.98 - circulation_.telemetry().arterialOxygenSaturation) / 0.30, 0.0, 1.0);
    interoception_.metabolicDepletion = std::clamp(
        0.65 * (1.0 - brainPhys.atp) + 0.35 * (1.0 - brainBio.oxygen), 0.0, 1.0);

    const double naImbalance = std::abs(circulation_.arterialSolutes().naMm - 142.0) / 20.0;
    const double kImbalance = std::abs(circulation_.arterialSolutes().kMm - 4.0) / 2.0;
    const double volImbalance = std::abs(circulation_.telemetry().totalBloodVolumeL - 5.0) / 2.0;
    interoception_.fluidElectrolyteImbalance = std::clamp(naImbalance + kImbalance + volImbalance, 0.0, 1.0);
    interoception_.thermalStress = std::clamp(std::abs(circulation_.bloodTemperatureC() - 37.0) / 5.0, 0.0, 1.0);

    // Global Visceral Distress Index (0.0 = optimal homeostasis, 1.0 = catastrophic decompensation)
    const double distress = 0.30 * interoception_.respiratoryHypoxia +
                           0.25 * interoception_.metabolicDepletion +
                           0.20 * interoception_.cardiovascularLoad +
                           0.15 * interoception_.fluidElectrolyteImbalance +
                           0.10 * interoception_.thermalStress;
    interoception_.visceralDistress = std::clamp(distress, 0.0, 1.0);

    // Populate explicit InteroceptionState struct
    interoception_.state.arterialPressure = mapMmHg;
    interoception_.state.venousPressure = circulation_.centralVenousPressureMmHg();
    interoception_.state.cardiacLoad = interoception_.cardiovascularLoad;
    interoception_.state.heartRate = heart_.telemetry().heartRateBpm;
    interoception_.state.arterialO2 = circulation_.telemetry().arterialPo2MmHg;
    interoception_.state.arterialCO2 = circulation_.telemetry().arterialPco2MmHg;
    interoception_.state.bloodPH = circulation_.telemetry().arterialPh;
    interoception_.state.glucose = circulation_.arterialSolutes().glucoseMm;
    interoception_.state.sodium = circulation_.arterialSolutes().naMm;
    interoception_.state.potassium = circulation_.arterialSolutes().kMm;
    interoception_.state.calcium = circulation_.arterialSolutes().caIonizedMm;
    interoception_.state.osmolarity = 2.0 * (circulation_.arterialSolutes().naMm + circulation_.arterialSolutes().kMm) +
                                     circulation_.arterialSolutes().glucoseMm + circulation_.arterialSolutes().ureaMm;
    interoception_.state.bodyTemperature = circulation_.bloodTemperatureC();
    interoception_.state.renalStress = std::clamp((125.0 - kidneys_.gfrMlPerMin()) / 100.0, 0.0, 1.0);
    interoception_.state.hypoxia = interoception_.respiratoryHypoxia;
    interoception_.state.hypercapnia = std::clamp((circulation_.telemetry().arterialPco2MmHg - 40.0) / 40.0, 0.0, 1.0);
    interoception_.state.visceralDistress = interoception_.visceralDistress;
    interoception_.state.metabolicStress = interoception_.metabolicDepletion;
}

tatarus::ThroughputCounters SyntheticOrganism::throughputCounters() const {
    auto total = organismThroughput_;
    total += mind_.throughput();
    return total;
}

void SyntheticOrganism::resetThroughputCounters() {
    organismThroughput_.clear();
    mind_.resetThroughputCounters();
}

std::string SyntheticOrganism::throughputJson() const {
    const auto counters = throughputCounters();
    std::ostringstream out;
    out << "{\"schema\":\"tatarus-throughput-v1\""
        << ",\"neural_ticks\":" << counters.neuralTicks
        << ",\"organism_steps\":" << counters.organismSteps
        << ",\"entity_visits\":" << counters.totalEntityVisits()
        << ",\"logical_reads\":" << counters.totalLogicalReads()
        << ",\"logical_writes\":" << counters.totalLogicalWrites()
        << ",\"logical_state_ops\":" << (counters.totalLogicalReads() + counters.totalLogicalWrites())
        << ",\"logical_bytes_read\":" << counters.totalLogicalBytesRead()
        << ",\"logical_bytes_written\":" << counters.totalLogicalBytesWritten()
        << ",\"logical_bytes_touched\":" << (counters.totalLogicalBytesRead() + counters.totalLogicalBytesWritten())
        << ",\"domains\":{";
    bool first = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(tatarus::ThroughputDomain::Count); ++i) {
        const auto domain = static_cast<tatarus::ThroughputDomain>(i);
        const auto& c = counters.domains[i];
        if (!first) out << ',';
        first = false;
        out << '\"' << tatarus::throughputDomainName(domain) << "\":{"
            << "\"entity_visits\":" << c.entityVisits
            << ",\"logical_reads\":" << c.logicalReads
            << ",\"logical_writes\":" << c.logicalWrites
            << ",\"logical_bytes_read\":" << c.logicalBytesRead
            << ",\"logical_bytes_written\":" << c.logicalBytesWritten
            << '}';
    }
    out << "}}";
    return out.str();
}

OrganismTelemetry SyntheticOrganism::telemetry() const {
    OrganismTelemetry tel;
    tel.available = true;
    tel.stepCount = stepCount_;
    tel.simulatedTimeSeconds = totalSimulatedTimeS_;
    tel.circulation = circulation_.telemetry();
    tel.heart = heart_.telemetry();
    tel.lung = lung_.telemetry();
    tel.kidney = kidneys_.telemetry();
    tel.leftKidney = kidneys_.left().telemetry();
    tel.rightKidney = kidneys_.right().telemetry();
    tel.interoception = interoception_;
    return tel;
}

InteroceptionState SyntheticOrganism::interoceptionState() const {
    return interoception_.state;
}

ConservationAudit SyntheticOrganism::auditConservation(double tolerance) const {
    return ledger_.audit(circulation_.totalSubstanceAmounts(), tolerance);
}

void SyntheticOrganism::infuseFluid(double volumeMl, const SoluteProfile& solutes) {
    circulation_.infuseFluidMl(volumeMl, solutes);
    // Record fluid and solutes in ConservationLedger
    ledger_.recordIntake(SubstanceId::Water, volumeMl / 1000.0);
    ledger_.recordIntake(SubstanceId::Na, (volumeMl / 1000.0) * solutes.naMm);
    ledger_.recordIntake(SubstanceId::K, (volumeMl / 1000.0) * solutes.kMm);
    ledger_.recordIntake(SubstanceId::Ca, (volumeMl / 1000.0) * solutes.caIonizedMm);
    ledger_.recordIntake(SubstanceId::Cl, (volumeMl / 1000.0) * solutes.clMm);
    ledger_.recordIntake(SubstanceId::Glucose, (volumeMl / 1000.0) * solutes.glucoseMm);
}

void SyntheticOrganism::hemorrhage(double volumeMl) {
    // Record loss from ledger
    const double volL = volumeMl / 1000.0;
    ledger_.recordExcretion(SubstanceId::Water, volL);
    ledger_.recordExcretion(SubstanceId::Na, volL * circulation_.arterialSolutes().naMm);
    ledger_.recordExcretion(SubstanceId::K, volL * circulation_.arterialSolutes().kMm);
    ledger_.recordExcretion(SubstanceId::Ca, volL * circulation_.arterialSolutes().caIonizedMm);
    ledger_.recordExcretion(SubstanceId::Cl, volL * circulation_.arterialSolutes().clMm);
    ledger_.recordExcretion(SubstanceId::Glucose, volL * circulation_.arterialSolutes().glucoseMm);
    ledger_.recordExcretion(SubstanceId::Urea, volL * circulation_.arterialSolutes().ureaMm);
    circulation_.bleedFluidMl(volumeMl);
}

void SyntheticOrganism::setAdrenergicStimulation(double sympatheticOverride) {
    sympatheticTone_ = std::clamp(sympatheticOverride, 0.0, 3.0);
}

void SyntheticOrganism::setRenalFunction(double leftFraction, double rightFraction) {
    kidneys_.setRelativeFunction(leftFraction, rightFraction);
}

void SyntheticOrganism::setExperimentalIntervention(
    const ExperimentalIntervention& intervention) {
    experimentalIntervention_ = intervention;
}

void SyntheticOrganism::applyExperimentalDamage(
    double neuronFraction,
    double synapseFraction,
    std::uint64_t seed) {
    mind_.applyExperimentalDamage(neuronFraction, synapseFraction, seed);
}

std::string SyntheticOrganism::serializeState() const {
    std::ostringstream ss;
    ss << stepCount_ << " "
       << totalSimulatedTimeS_ << " "
       << sympatheticTone_ << " "
       << parasympatheticTone_ << " "
       << baroreceptorAfferent_ << " "
       << circulation_.arterialPressureMmHg() << " "
       << circulation_.centralVenousPressureMmHg() << " "
       << circulation_.telemetry().totalBloodVolumeL << " "
       << heart_.telemetry().heartRateBpm << " "
       << heart_.telemetry().strokeVolumeMl << " "
       << lung_.telemetry().respirationRateBpm << " "
       << lung_.telemetry().tidalVolumeL << " "
       << kidneys_.telemetry().glomerularFiltrationRateMlPerMin << " "
       << kidneys_.telemetry().urineOutputRateMlPerMin << " "
       << kidneys_.left().functionalMassFraction() << " "
       << kidneys_.right().functionalMassFraction();
    return ss.str();
}

bool SyntheticOrganism::deserializeState(const std::string& serialized) {
    std::istringstream ss(serialized);
    double map = 0.0, cvp = 0.0, vol = 0.0, hr = 0.0, sv = 0.0, rr = 0.0, vt = 0.0;
    double gfr = 0.0, urine = 0.0, leftMass = 0.5, rightMass = 0.5;
    if (!(ss >> stepCount_
             >> totalSimulatedTimeS_
             >> sympatheticTone_
             >> parasympatheticTone_
             >> baroreceptorAfferent_
             >> map
             >> cvp
             >> vol
             >> hr
             >> sv
             >> rr
             >> vt
             >> gfr
             >> urine
             >> leftMass
             >> rightMass)) {
        return false;
    }
    kidneys_.setRelativeFunction(leftMass * 2.0, rightMass * 2.0);
    return true;
}

void SyntheticOrganism::saveSnapshot(const std::filesystem::path& directory) const {
    std::filesystem::create_directories(directory);
    mind_.saveSnapshot(directory / "mind");

    std::ofstream body(directory / "organism-body-v1.bin", std::ios::binary | std::ios::trunc);
    if (!body) throw std::runtime_error("cannot create organism body snapshot");
    constexpr std::array<char, 16> magic{
        'T','A','T','A','R','U','S','4','B','O','D','Y','0','0','0','1'};
    body.write(magic.data(), magic.size());
    writePod(body, config_);
    writePod(body, circulation_);
    writePod(body, heart_);
    writePod(body, lung_);
    writePod(body, kidneys_);
    writePod(body, ledger_);
    writePod(body, stepCount_);
    writePod(body, totalSimulatedTimeS_);
    writePod(body, sympatheticTone_);
    writePod(body, parasympatheticTone_);
    writePod(body, baroreceptorAfferent_);
    writePod(body, interoception_);
    writePod(body, experimentalIntervention_);
    if (!body) throw std::runtime_error("failed to write organism body snapshot");
}

bool SyntheticOrganism::loadSnapshot(const std::filesystem::path& directory) {
    std::ifstream body(directory / "organism-body-v1.bin", std::ios::binary);
    if (!body) return false;
    constexpr std::array<char, 16> expectedMagic{
        'T','A','T','A','R','U','S','4','B','O','D','Y','0','0','0','1'};
    std::array<char, 16> magic{};
    body.read(magic.data(), magic.size());
    if (!body || magic != expectedMagic) return false;

    OrganismConfig config{};
    Circulation circulation(config_.circulation);
    Heart heart(config_.heart);
    Lung lung(config_.lung);
    BilateralKidneys kidneys(config_.kidney);
    ConservationLedger ledger;
    std::uint64_t stepCount = 0;
    double simulatedTime = 0.0;
    double sympathetic = 0.25;
    double parasympathetic = 0.75;
    double baroreceptor = 0.5;
    InteroceptionTelemetry interoception{};
    ExperimentalIntervention experiment{};
    if (!readPod(body, config) ||
        !readPod(body, circulation) ||
        !readPod(body, heart) ||
        !readPod(body, lung) ||
        !readPod(body, kidneys) ||
        !readPod(body, ledger) ||
        !readPod(body, stepCount) ||
        !readPod(body, simulatedTime) ||
        !readPod(body, sympathetic) ||
        !readPod(body, parasympathetic) ||
        !readPod(body, baroreceptor) ||
        !readPod(body, interoception) ||
        !readPod(body, experiment)) {
        return false;
    }
    if (!mind_.loadSnapshot(directory / "mind")) return false;

    config_ = config;
    circulation_ = circulation;
    heart_ = heart;
    lung_ = lung;
    kidneys_ = kidneys;
    ledger_ = ledger;
    stepCount_ = stepCount;
    totalSimulatedTimeS_ = simulatedTime;
    sympatheticTone_ = sympathetic;
    parasympatheticTone_ = parasympathetic;
    baroreceptorAfferent_ = baroreceptor;
    interoception_ = interoception;
    experimentalIntervention_ = experiment;
    // The body components contain non-owning pointers to this organism's
    // throughput counters. Raw snapshot bytes necessarily contain the source
    // instance's pointer values, so restore the local bindings before any
    // component is stepped or destroyed.
    bindThroughputCounters();
    return true;
}

std::string SyntheticOrganism::organismJson() const {
    const auto tel = telemetry();
    const auto& c = tel.circulation;
    const auto& h = tel.heart;
    const auto& l = tel.lung;
    const auto& k = tel.kidney;
    const auto& kl = tel.leftKidney;
    const auto& kr = tel.rightKidney;
    const auto& i = tel.interoception;

    std::ostringstream ss;
    ss << "{\n"
       << "  \"available\": true,\n"
       << "  \"stepCount\": " << tel.stepCount << ",\n"
       << "  \"simulatedTimeSeconds\": " << formatJsonDouble(tel.simulatedTimeSeconds, 2) << ",\n"
       << "  \"circulation\": {\n"
       << "    \"totalBloodVolumeL\": " << formatJsonDouble(c.totalBloodVolumeL, 2) << ",\n"
       << "    \"meanArterialPressureMmHg\": " << formatJsonDouble(c.meanArterialPressureMmHg, 1) << ",\n"
       << "    \"systolicPressureMmHg\": " << formatJsonDouble(c.systolicPressureMmHg, 1) << ",\n"
       << "    \"diastolicPressureMmHg\": " << formatJsonDouble(c.diastolicPressureMmHg, 1) << ",\n"
       << "    \"centralVenousPressureMmHg\": " << formatJsonDouble(c.centralVenousPressureMmHg, 1) << ",\n"
       << "    \"arterialPo2MmHg\": " << formatJsonDouble(c.arterialPo2MmHg, 1) << ",\n"
       << "    \"arterialPco2MmHg\": " << formatJsonDouble(c.arterialPco2MmHg, 1) << ",\n"
       << "    \"arterialOxygenSaturation\": " << formatJsonDouble(c.arterialOxygenSaturation, 3) << ",\n"
       << "    \"arterialPh\": " << formatJsonDouble(c.arterialPh, 2) << ",\n"
       << "    \"plasmaGlucoseMm\": " << formatJsonDouble(c.plasmaGlucoseMm, 2) << ",\n"
       << "    \"plasmaNaMm\": " << formatJsonDouble(c.plasmaNaMm, 1) << ",\n"
       << "    \"plasmaKMm\": " << formatJsonDouble(c.plasmaKMm, 2) << ",\n"
       << "    \"plasmaCaMm\": " << formatJsonDouble(c.plasmaCaMm, 2) << ",\n"
       << "    \"bloodTemperatureC\": " << formatJsonDouble(c.bloodTemperatureC, 1) << "\n"
       << "  },\n"
       << "  \"heart\": {\n"
       << "    \"heartRateBpm\": " << formatJsonDouble(h.heartRateBpm, 1) << ",\n"
       << "    \"strokeVolumeMl\": " << formatJsonDouble(h.strokeVolumeMl, 1) << ",\n"
       << "    \"cardiacOutputLPerMin\": " << formatJsonDouble(h.cardiacOutputLPerMin, 2) << ",\n"
       << "    \"ejectionFractionLv\": " << formatJsonDouble(h.ejectionFractionLv, 2) << ",\n"
       << "    \"leftVentriclePressureMmHg\": " << formatJsonDouble(h.leftVentriclePressureMmHg, 1) << ",\n"
       << "    \"rightVentriclePressureMmHg\": " << formatJsonDouble(h.rightVentriclePressureMmHg, 1) << ",\n"
       << "    \"leftAtriumPressureMmHg\": " << formatJsonDouble(h.leftAtriumPressureMmHg, 1) << ",\n"
       << "    \"rightAtriumPressureMmHg\": " << formatJsonDouble(h.rightAtriumPressureMmHg, 1) << ",\n"
       << "    \"tricuspidValveOpen\": " << formatJsonDouble(h.tricuspidValveOpen, 2) << ",\n"
       << "    \"pulmonicValveOpen\": " << formatJsonDouble(h.pulmonicValveOpen, 2) << ",\n"
       << "    \"mitralValveOpen\": " << formatJsonDouble(h.mitralValveOpen, 2) << ",\n"
       << "    \"aorticValveOpen\": " << formatJsonDouble(h.aorticValveOpen, 2) << ",\n"
       << "    \"myocardialO2ConsumptionMlPerMin\": " << formatJsonDouble(h.myocardialO2ConsumptionMlPerMin, 1) << "\n"
       << "  },\n"
       << "  \"lung\": {\n"
       << "    \"respirationRateBpm\": " << formatJsonDouble(l.respirationRateBpm, 1) << ",\n"
       << "    \"tidalVolumeL\": " << formatJsonDouble(l.tidalVolumeL, 2) << ",\n"
       << "    \"minuteVentilationLPerMin\": " << formatJsonDouble(l.minuteVentilationLPerMin, 2) << ",\n"
       << "    \"alveolarPo2MmHg\": " << formatJsonDouble(l.alveolarPo2MmHg, 1) << ",\n"
       << "    \"alveolarPco2MmHg\": " << formatJsonDouble(l.alveolarPco2MmHg, 1) << ",\n"
       << "    \"o2UptakeRateMlPerMin\": " << formatJsonDouble(l.o2UptakeRateMlPerMin, 1) << "\n"
       << "  },\n"
       << "  \"kidney\": {\n"
       << "    \"glomerularFiltrationRateMlPerMin\": " << formatJsonDouble(k.glomerularFiltrationRateMlPerMin, 1) << ",\n"
       << "    \"urineOutputRateMlPerMin\": " << formatJsonDouble(k.urineOutputRateMlPerMin, 2) << ",\n"
       << "    \"urineOsmolarityMOsmPerKg\": " << formatJsonDouble(k.urineOsmolarityMOsmPerKg, 0) << ",\n"
       << "    \"reninSecretionRate\": " << formatJsonDouble(k.reninSecretionRate, 2) << "\n"
       << "  },\n"
       << "  \"kidneys\": {\n"
       << "    \"count\": 2,\n"
       << "    \"left\": {\n"
       << "      \"glomerularFiltrationRateMlPerMin\": " << formatJsonDouble(kl.glomerularFiltrationRateMlPerMin, 1) << ",\n"
       << "      \"renalBloodFlowMlPerMin\": " << formatJsonDouble(kl.renalBloodFlowMlPerMin, 1) << ",\n"
       << "      \"urineOutputRateMlPerMin\": " << formatJsonDouble(kl.urineOutputRateMlPerMin, 2) << ",\n"
       << "      \"reninSecretionRate\": " << formatJsonDouble(kl.reninSecretionRate, 2) << "\n"
       << "    },\n"
       << "    \"right\": {\n"
       << "      \"glomerularFiltrationRateMlPerMin\": " << formatJsonDouble(kr.glomerularFiltrationRateMlPerMin, 1) << ",\n"
       << "      \"renalBloodFlowMlPerMin\": " << formatJsonDouble(kr.renalBloodFlowMlPerMin, 1) << ",\n"
       << "      \"urineOutputRateMlPerMin\": " << formatJsonDouble(kr.urineOutputRateMlPerMin, 2) << ",\n"
       << "      \"reninSecretionRate\": " << formatJsonDouble(kr.reninSecretionRate, 2) << "\n"
       << "    }\n"
       << "  },\n"
       << "  \"interoception\": {\n"
       << "    \"visceralDistress\": " << formatJsonDouble(i.visceralDistress, 3) << ",\n"
       << "    \"cardiovascularLoad\": " << formatJsonDouble(i.cardiovascularLoad, 3) << ",\n"
       << "    \"metabolicDepletion\": " << formatJsonDouble(i.metabolicDepletion, 3) << ",\n"
       << "    \"respiratoryHypoxia\": " << formatJsonDouble(i.respiratoryHypoxia, 3) << ",\n"
       << "    \"fluidElectrolyteImbalance\": " << formatJsonDouble(i.fluidElectrolyteImbalance, 3) << ",\n"
       << "    \"sympatheticTone\": " << formatJsonDouble(i.sympatheticTone, 2) << ",\n"
       << "    \"parasympatheticTone\": " << formatJsonDouble(i.parasympatheticTone, 2) << "\n"
       << "  }\n"
       << "}\n";
    return ss.str();
}

} // namespace tatarus::organism
