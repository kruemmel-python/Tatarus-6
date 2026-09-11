#include "tatarus_neural_network.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

tatarus::neuro::NervousSystemConfig configuration() {
    tatarus::neuro::NervousSystemConfig config;
    config.sensoryNeurons = 8;
    config.excitatoryNeurons = 18;
    config.inhibitoryNeurons = 10;
    config.contextNeurons = 6;
    config.motorNeurons = 4;
    config.modulatoryNeurons = 4;
    config.connectionProbability = 0.30;
    config.structuralPlasticityEnabled = false;
    config.seed = 3'000'027;
    config.validate();
    return config;
}

tatarus::neuro::SensorFrame stimulus(std::uint64_t step) {
    tatarus::neuro::SensorFrame frame;
    const double polarity = (step / 24U) % 2U == 0U ? 1.0 : -0.65;
    frame.visionEvents = {polarity, 0.8, -0.3, 0.65};
    frame.audioSamples = {0.4 * polarity, 0.7};
    frame.touch = {0.8, -0.2 * polarity};
    frame.contextEvents = {0.5, polarity};
    frame.internalEnergy = 0.92;
    frame.temperature = 0.1;
    frame.reward = step % 48U < 8U ? 0.75 : 0.0;
    frame.novelty = step % 24U == 0U ? 0.9 : 0.08;
    return frame;
}

void substrateCoverageTest() {
    auto config = configuration();
    tatarus::neuro::PersistentNervousSystem system(config);
    const auto initial = system.physiologyMetrics();
    require(initial.available && initial.finite, "physiology substrate unavailable");
    require(initial.parvalbuminNeurons == 5, "PV assignment is not deterministic");
    require(initial.somatostatinNeurons == 3, "SST assignment is not deterministic");
    require(initial.vipNeurons == 2, "VIP assignment is not deterministic");
    require(std::abs(initial.meanExtracellularKMm - 3.5) < 1e-9,
        "extracellular potassium baseline is wrong");

    const auto state = system.inspectPhysiology();
    require(state.neurons.size() == static_cast<std::size_t>(config.neuronCount()),
        "physiology omitted neuron compartments");
    const auto [minHcn, maxHcn] = std::minmax_element(
        state.neurons.begin(), state.neurons.end(),
        [](const auto& a, const auto& b) { return a.hcnDensity < b.hcnDensity; });
    require(maxHcn->hcnDensity > minHcn->hcnDensity,
        "3D HCN density gradient was not constructed");
    const auto [minKv, maxKv] = std::minmax_element(
        state.neurons.begin(), state.neurons.end(),
        [](const auto& a, const auto& b) { return a.kvDensity < b.kvDensity; });
    require(maxKv->kvDensity > minKv->kvDensity,
        "3D Kv density gradient was not constructed");
}

void causalPhysiologyTest() {
    auto enabledConfig = configuration();
    auto disabledConfig = enabledConfig;
    disabledConfig.physiologyEnabled = false;
    tatarus::neuro::PersistentNervousSystem enabled(enabledConfig);
    tatarus::neuro::PersistentNervousSystem disabled(disabledConfig);
    for (std::uint64_t step = 0; step < 1'200U; ++step) {
        const auto frame = stimulus(step);
        enabled.step(frame);
        disabled.step(frame);
    }
    const auto physiology = enabled.physiologyMetrics();
    require(physiology.finite, "coupled physiology became non-finite");
    require(physiology.meanPumpActivity > 0.01,
        "Na/K ATPase did not activate");
    require(physiology.cumulativeAtpConsumed > 0.0,
        "ATP consumption was not accumulated");
    require(physiology.dopamine > 0.02
            && physiology.noradrenaline > 0.08
            && physiology.acetylcholine > 0.06,
        "diffuse neuromodulator fields did not respond");
    require(physiology.meanCamp > 0.45 && physiology.meanIp3 > 0.18,
        "GPCR second messengers did not respond");
    require(physiology.meanCrebActivation > 0.0,
        "activity did not reach CREB signaling");
    require(enabled.stateHash() != disabled.stateHash(),
        "physiology is telemetry-only and did not alter network dynamics");
}

void molecularPlasticityAndSleepTest() {
    auto config = configuration();
    config.physiologyTimeScale = 40.0;
    config.circadianCycleMs = 2'400.0;
    config.sleepPressureTauMs = 500.0;
    config.nremMinimumMs = 320.0;
    config.remMinimumMs = 120.0;
    config.validate();
    tatarus::neuro::PersistentNervousSystem system(config);
    for (std::uint64_t step = 0; step < 2'400U; ++step) {
        system.step(stimulus(step));
    }
    const auto p = system.physiologyMetrics();
    require(p.sleepTransitions > 0, "accelerated circadian model never entered sleep");
    require(p.extracellularVolumeFraction > 0.20,
        "sleep did not expand the extracellular volume");
    require(p.meanCrebActivation > 0.0 && p.meanProteinPool > 0.0,
        "CREB transcription/protein synthesis chain stayed inactive");
    require(p.taggedSynapses > 0 || p.longTermPotentiationEvents > 0
            || p.longTermDepressionEvents > 0,
        "synaptic tagging/capture stayed inactive");
    require(p.glymphaticClearance >= 0.10 && p.tissueWaste >= 0.0,
        "glymphatic state is invalid");
    require(std::isfinite(p.gammaPower) && std::isfinite(p.thetaPower),
        "PV/SST oscillation metrics are invalid");
}

void experimentalInterventionTest() {
    auto config = configuration();
    tatarus::neuro::PersistentNervousSystem control(config);
    tatarus::neuro::PersistentNervousSystem treatment(config);
    treatment.setExperimentalIntervention(tatarus::neuro::ExperimentalIntervention{
        .astrocyteFunction = 0.25,
        .oxygenSupply = 0.30,
        .glucoseSupply = 0.35,
        .pumpEfficiency = 0.20,
        .myelinIntegrity = 0.22,
        .microgliaFunction = 0.25,
        .neuromodulatorGain = 0.30,
        .sleepEnabled = false,
    });
    for (std::uint64_t step = 0; step < 1'000U; ++step) {
        const auto frame = stimulus(step);
        control.step(frame);
        treatment.step(frame);
    }
    const auto normalPhysiology = control.physiologyMetrics();
    const auto alteredPhysiology = treatment.physiologyMetrics();
    const auto normalBiology = control.biologicalMetrics();
    const auto alteredBiology = treatment.biologicalMetrics();
    require(alteredPhysiology.meanPumpActivity < normalPhysiology.meanPumpActivity,
        "pump intervention did not causally reduce Na/K pump activity");
    require(alteredBiology.meanOxygen < normalBiology.meanOxygen
            && alteredBiology.meanGlucose < normalBiology.meanGlucose,
        "supply intervention did not reduce tissue oxygen and glucose");
    require(alteredBiology.meanMyelinCoverage < normalBiology.meanMyelinCoverage,
        "myelin intervention did not change conduction substrate");
    require(control.stateHash() != treatment.stateHash(),
        "experimental intervention remained telemetry-only");
}

void snapshotDeterminismTest(const std::filesystem::path& output) {
    auto config = configuration();
    tatarus::neuro::PersistentNervousSystem original(config);
    for (std::uint64_t step = 0; step < 360U; ++step) original.step(stimulus(step));
    const auto path = output / "tatarus_physiology.tns";
    std::filesystem::create_directories(output);
    original.saveSnapshot(path);
    auto other = config;
    other.seed = 1;
    tatarus::neuro::PersistentNervousSystem restored(other);
    restored.loadSnapshot(path);
    require(original.stateHash() == restored.stateHash(),
        "TATARUS Physiology snapshot is not exact");
    for (std::uint64_t step = 360U; step < 520U; ++step) {
        original.step(stimulus(step));
        restored.step(stimulus(step));
    }
    require(original.stateHash() == restored.stateHash(),
        "SDK 3.0 snapshot continuation diverged");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "tatarus_physiology_test_output";
        substrateCoverageTest();
        causalPhysiologyTest();
        molecularPlasticityAndSleepTest();
        experimentalInterventionTest();
        snapshotDeterminismTest(output);
        std::cout << "TATARUS Physiology tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TATARUS Physiology test failure: " << error.what() << '\n';
        return 1;
    }
}
