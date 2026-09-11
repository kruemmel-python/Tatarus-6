#include "tatarus_neural_network.hpp"
#include "tatarus_cognition.hpp"
#include "tatarus_tissue.hpp"
#include "tatarus/visual_pathway.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace {

using tatarus::neuro::ContinuousEnvironment;
using tatarus::neuro::NervousSystemConfig;
using tatarus::neuro::PersistentNervousSystem;
using tatarus::neuro::SensorFrame;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void biologicalVisualPathwayTest() {
    constexpr std::size_t side = 32U;
    std::vector<double> rgb(side * side * 3U, 0.04);
    const auto paint = [&](std::size_t x, std::size_t y, std::array<double, 3> color) {
        const std::size_t pixel = (y * side + x) * 3U;
        rgb[pixel] = color[0];
        rgb[pixel + 1U] = color[1];
        rgb[pixel + 2U] = color[2];
    };
    // Category-neutral house-like fixture: roof diagonals, wall edges, door
    // and paired windows exercise all early visual channels.
    for (std::size_t y = 10U; y < 28U; ++y) {
        for (std::size_t x = 7U; x < 25U; ++x) {
            paint(x, y, {0.72, 0.42, 0.18});
        }
    }
    for (std::size_t offset = 0; offset < 10U; ++offset) {
        paint(6U + offset, 10U - offset / 2U, {0.82, 0.12, 0.08});
        paint(25U - offset, 10U - offset / 2U, {0.82, 0.12, 0.08});
    }
    for (std::size_t y = 15U; y < 20U; ++y) {
        for (std::size_t x : {10U, 11U, 20U, 21U}) {
            paint(x, y, {0.12, 0.42, 0.88});
        }
    }
    for (std::size_t y = 20U; y < 28U; ++y) {
        paint(15U, y, {0.16, 0.08, 0.03});
        paint(16U, y, {0.16, 0.08, 0.03});
    }

    tatarus::neuro::vision::VisualPathway pathway;
    const auto bottomUp = pathway.perceive(side, side, rgb);
    require(bottomUp.photoreceptors.size() == 16U
            && bottomUp.retinalGanglionCells.size() == 16U
            && bottomUp.opticNerveEvents.size() == 64U
            && bottomUp.v1OrientationCells.size() == 16U,
        "retina/optic-nerve/V1 population sizes are not bounded and explicit");
    const double v1Energy = std::accumulate(
        bottomUp.v1OrientationCells.begin(), bottomUp.v1OrientationCells.end(), 0.0,
        [](double total, const auto& cell) {
            return total + std::accumulate(cell.begin(), cell.end(), 0.0);
        });
    require(v1Energy > 0.05
            && bottomUp.ventral.lateralOccipitalObject > 0.05
            && bottomUp.ventral.parahippocampalPlace > 0.01,
        "early or ventral visual cortex did not respond to structured form");

    tatarus::neuro::vision::TopDownVisualPrior prior;
    for (auto& part : prior.expectedParts) part.fill(0.9);
    prior.gain = 1.0; // Must be clamped by the biological bridge.
    const auto integrated = pathway.perceive(
        side, side, rgb,
        tatarus::neuro::vision::VisualPathway::defaultObjectFields(), &prior);
    require(std::abs(integrated.topDownGainApplied - 0.35) < 1e-12
            && integrated.integratedParts[5][4] > bottomUp.bottomUpParts[5][4],
        "bounded top-down visual feedback was not integrated");
}

NervousSystemConfig compactConfig() {
    NervousSystemConfig cfg;
    cfg.sensoryNeurons = 12;
    cfg.excitatoryNeurons = 20;
    cfg.inhibitoryNeurons = 6;
    cfg.contextNeurons = 8;
    cfg.motorNeurons = 4;
    cfg.modulatoryNeurons = 2;
    cfg.connectionProbability = 0.16;
    cfg.structuralIntervalMs = 40.0;
    cfg.maximumAssemblies = 12;
    cfg.targetRateHz = 10.0;
    cfg.validate();
    return cfg;
}

SensorFrame deterministicFrame(std::uint64_t step) {
    SensorFrame frame;
    frame.visionEvents = {
        std::sin(static_cast<double>(step) * 0.071),
        std::cos(static_cast<double>(step) * 0.043),
        (step % 17U == 0U) ? 1.0 : 0.0
    };
    frame.audioSamples = {
        std::sin(static_cast<double>(step) * 0.19),
        std::sin(static_cast<double>(step) * 0.07)
    };
    frame.touch = {(step % 31U == 0U) ? 1.0 : 0.0};
    if (step % 53U < 8U) {
        frame.textBytes = {'A', 'B'};
    }
    frame.temperature = 0.25 * std::sin(static_cast<double>(step) * 0.011);
    frame.internalEnergy = 0.8;
    frame.reward = (step % 101U == 0U) ? 0.4 : 0.0;
    frame.novelty = (step % 67U == 0U) ? 0.7 : 0.0;
    return frame;
}

void deterministicTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 123456U;
    PersistentNervousSystem a(seeded);
    PersistentNervousSystem b(seeded);
    for (std::uint64_t step = 0; step < 800U; ++step) {
        const auto frame = deterministicFrame(step);
        const auto actionA = a.step(frame);
        const auto actionB = b.step(frame);
        require(actionA.movement == actionB.movement &&
                    actionA.attention == actionB.attention &&
                    actionA.vocalization == actionB.vocalization &&
                    actionA.confidence == actionB.confidence,
                "same seed produced different motor actions");
    }
    require(a.stateHash() == b.stateHash(), "same seed produced different state hash");
    require(a.dalePrincipleHolds() && b.dalePrincipleHolds(), "Dale law violated");
}

void spatialActionMemoryTest(
    const NervousSystemConfig& cfg,
    const std::filesystem::path& snapshotPath) {
    auto memoryConfig = cfg;
    memoryConfig.seed = 0x5A17A1U;
    memoryConfig.structuralPlasticityEnabled = false;
    PersistentNervousSystem system(memoryConfig);

    SensorFrame place;
    place.visionEvents = {0.15, 0.82, 0.0, 0.0};
    place.touch = {0.2, -0.1, 0.4};
    place.spatialContext = {
        0.21, 0.98, -0.37, 0.93, 0.61, -0.79, 0.88, 0.47
    };
    place.contextEvents = {
        0.18, 0.98, -0.44, 0.90, 0.71, -0.22, 0.53, 0.31
    };
    place.internalEnergy = 0.9;
    place.novelty = 0.8;

    for (int tick = 0; tick < 30; ++tick) {
        system.step(place);
    }
    const auto initial = system.spatialMemoryMetrics();
    require(initial.available && initial.engrams >= 1 && initial.currentAssemblyId != 0,
        "spatial nervous memory did not form a place engram");

    // Repeatedly trying west from the same neural place-state is bad/familiar.
    for (int repeat = 0; repeat < 4; ++repeat) {
        system.beginEmbodiedAction(3);
        system.endEmbodiedAction(-0.30, 0.0, 0.0);
    }
    // East discovers a frontier from exactly the same neural place-state.
    system.beginEmbodiedAction(1);
    system.endEmbodiedAction(0.18, 0.0, 1.0);

    // The episode ends without reaching its goal. The complete neural trace
    // must mark the repeatedly taken west path more strongly than the single
    // novel east transition, and the mark must survive place recall.
    system.endEmbodiedEpisode(false);
    system.step(place);

    const auto learned = system.spatialMemoryMetrics();
    require(learned.actionUpdates == 5,
        "spatial action outcomes were not stored inside the nervous system");
    require(learned.noveltyValue[1] > learned.noveltyValue[3],
        "spatial memory did not distinguish novel and repeatedly familiar exits");
    require(learned.rewardValue[1] > learned.rewardValue[3],
        "spatial memory did not bind reward to place and action");
    require(learned.motorBias[1] > learned.motorBias[3],
        "spatial engram did not bias the neural motor readout away from the repeated path");
    require(learned.failedConsolidations == 1,
        "failed embodied episode was not consolidated in neural memory");
    require(learned.failedEpisodeObservations[3] == 1
            && learned.failedEpisodeObservations[1] == 1,
        "failed route membership was not retained per place and action");
    require(learned.avoidanceValue[3] > learned.avoidanceValue[1],
        "repeated failed path did not form the stronger avoidance engram");

    const auto hashBefore = system.stateHash();
    system.saveSnapshot(snapshotPath);
    PersistentNervousSystem restored(memoryConfig);
    restored.loadSnapshot(snapshotPath);
    const auto restoredMemory = restored.spatialMemoryMetrics();
    require(restored.stateHash() == hashBefore,
        "spatial nervous memory snapshot did not restore exactly");
    require(restoredMemory.actionUpdates == learned.actionUpdates
            && restoredMemory.engrams == learned.engrams
            && restoredMemory.motorBias == learned.motorBias
            && restoredMemory.routeValue == learned.routeValue
            && restoredMemory.avoidanceValue == learned.avoidanceValue
            && restoredMemory.failedEpisodeObservations
                == learned.failedEpisodeObservations,
        "spatial place/action engrams were not persisted in the nervous snapshot");

}

void frozenEvaluationTest(const NervousSystemConfig& cfg) {
    auto frozenConfig = cfg;
    frozenConfig.seed = 0xF20E3U;
    frozenConfig.structuralIntervalMs = 5.0;
    PersistentNervousSystem system(frozenConfig);

    SensorFrame place;
    place.visionEvents = {0.0, 0.8, 0.0, 0.0};
    place.touch = {0.1, -0.2, 0.3};
    place.spatialContext = {0.2, 0.9, -0.4, 0.7, 0.6, -0.5};
    place.contextEvents = place.spatialContext;
    place.internalEnergy = 0.9;
    for (int tick = 0; tick < 30; ++tick) system.step(place);
    system.beginEmbodiedAction(1);
    system.endEmbodiedAction(0.2, 0.0, 1.0);

    const auto learned = system.spatialMemoryMetrics();
    const auto assemblyCount = system.metrics().assemblyCount;
    const auto structuralGrowth = system.metrics().structuralGrowth;
    system.setLearningEnabled(false);
    require(!system.learningEnabled(), "frozen evaluation mode was not enabled");

    for (int tick = 0; tick < 80; ++tick) {
        system.step(place);
        system.beginEmbodiedAction(static_cast<std::uint32_t>(tick % 4));
        system.endEmbodiedAction(-0.4, 0.0, 0.0);
    }
    system.endEmbodiedEpisode(false);

    const auto frozen = system.spatialMemoryMetrics();
    require(!frozen.learningEnabled,
        "spatial telemetry does not report frozen evaluation mode");
    require(frozen.engrams == learned.engrams
            && frozen.actionUpdates == learned.actionUpdates
            && frozen.actionObservations == learned.actionObservations
            && frozen.rewardValue == learned.rewardValue
            && frozen.routeValue == learned.routeValue
            && frozen.avoidanceValue == learned.avoidanceValue,
        "frozen evaluation changed long-term spatial memory");
    require(system.metrics().assemblyCount == assemblyCount,
        "frozen evaluation formed a new neural assembly");
    require(system.metrics().structuralGrowth == structuralGrowth,
        "frozen evaluation changed structural connectivity");

    system.setLearningEnabled(true);
    system.beginEmbodiedAction(2);
    system.endEmbodiedAction(0.1, 0.0, 0.0);
    require(system.spatialMemoryMetrics().actionUpdates == learned.actionUpdates + 1,
        "learning did not resume after frozen evaluation");
}

void spatialInstrumentationTest(const NervousSystemConfig& cfg) {
    auto spatialConfig = cfg;
    spatialConfig.seed = 20260830U;
    PersistentNervousSystem system(spatialConfig);
    auto spatial = system.inspectSpatial();
    require(
        spatial.neurons.size()
            == static_cast<std::size_t>(spatialConfig.neuronCount()),
        "spatial instrumentation omitted neurons");
    require(
        spatial.dendrites.size() == spatial.neurons.size() * 7U,
        "spatial instrumentation omitted dendrite segments");
    require(!spatial.axons.empty(), "spatial instrumentation omitted axons");
    require(!spatial.astrocytes.empty(), "spatial instrumentation omitted astrocytes");
    require(!spatial.oligodendrocytes.empty(), "spatial instrumentation omitted oligodendrocytes");
    require(!spatial.microglia.empty(), "spatial instrumentation omitted microglia");
    require(spatial.tissueShape == "bilateral_brain_ellipsoid",
        "spatial substrate does not report the brain-shaped tissue body");
    require(spatial.interhemisphericFissureUm > 0.0,
        "brain-shaped tissue omitted the interhemispheric fissure");
    bool leftHemisphere = false;
    bool rightHemisphere = false;
    std::array<bool, 6> regions{};
    std::array<bool, 5> layers{};
    for (const auto& neuron : spatial.neurons) {
        leftHemisphere = leftHemisphere || neuron.soma.x < 0.0;
        rightHemisphere = rightHemisphere || neuron.soma.x > 0.0;
        regions[static_cast<std::size_t>(neuron.region)] = true;
        layers[static_cast<std::size_t>(neuron.layer)] = true;
        require(std::abs(neuron.soma.x) <= spatial.tissueRadii.x + 40.0
                && std::abs(neuron.soma.y) <= spatial.tissueRadii.y + 12.0
                && std::abs(neuron.soma.z) <= spatial.tissueRadii.z + 12.0,
            "neuron lies outside the declared brain-shaped tissue bounds");
    }
    require(leftHemisphere && rightHemisphere,
        "brain-shaped tissue did not populate both hemispheres");
    require(std::all_of(regions.begin(), regions.end(), [](bool value) { return value; }),
        "functional brain regions are incomplete");
    require(std::all_of(layers.begin(), layers.end(), [](bool value) { return value; }),
        "cortical and subcortical layers are incomplete");
    require(
        std::all_of(
            spatial.axons.begin(),
            spatial.axons.end(),
            [](const auto& axon) {
                return std::isfinite(axon.start.x)
                    && std::isfinite(axon.end.z)
                    && std::isfinite(axon.lengthUm)
                    && axon.lengthUm >= 0.0
                    && std::isfinite(axon.effectiveDelayMs);
            }),
        "spatial axon instrumentation is non-finite");

    system.step(deterministicFrame(1));
    spatial = system.inspectSpatial();
    require(spatial.step == 1U, "live spatial step did not advance");
    require(
        std::all_of(
            spatial.neurons.begin(),
            spatial.neurons.end(),
            [](const auto& neuron) {
                return std::isfinite(neuron.somaMv)
                    && std::isfinite(neuron.dendriteMv)
                    && std::isfinite(neuron.energy);
            }),
        "live neuron instrumentation is non-finite");
}

void causalTissueMechanicsTest() {
    using tatarus::neuro::PopulationRole;
    using tatarus::neuro::biology::SpatialSubstrate;
    std::vector<PopulationRole> roles(96, PopulationRole::Excitatory);
    SpatialSubstrate tissue(roles.size(), roles, 424242U, -65.0, 1.0);
    for (std::size_t i = 0; i < 100; ++i) {
        tissue.synchronizeSynapse(i, i % 96, (i * 17 + 11) % 96);
    }
    tissue.updateTissueMechanics(100, 1.0, 0.20, 0.0);
    const auto baseline = tissue.metrics();
    const auto delayBefore = tissue.effectiveConductionDelaySteps(4, 1, 0.05);
    for (std::size_t i = 100; i < 500; ++i) {
        tissue.synchronizeSynapse(i, i % 96, (i * 29 + 7) % 96);
    }
    tissue.updateTissueMechanics(500, 1.0, 0.20, 60'000.0);
    const auto grown = tissue.metrics();
    const auto delayAfter = tissue.effectiveConductionDelaySteps(4, 1, 0.05);
    require(grown.tissueVolumeRatio > baseline.tissueVolumeRatio,
        "synaptic material did not expand the tissue envelope");
    require(grown.linearExpansion > 1.0
            && grown.synapticMaterialVolumeUm3
                > baseline.synapticMaterialVolumeUm3,
        "tissue mechanics did not convert synaptogenesis into physical growth");
    require(grown.solidPackingFraction > 0.0
            && grown.extracellularSpaceFraction < 0.20
            && grown.tissuePressureKPa >= 0.0,
        "packing, extracellular space, or pressure coupling is missing");
    require(delayAfter >= delayBefore,
        "physical expansion did not feed back into axonal delay");
    tissue.updateTissueMechanics(100, 1.0, 0.20, 60'000.0);
    const auto remodeled = tissue.metrics();
    require(remodeled.cumulativeMaterialRecycledUm3 > 0.0
            && remodeled.tissueVolumeUm3 < grown.tissueVolumeUm3,
        "pruning did not recycle material and relax tissue volume");
}

void scalableSparseTissueTest() {
    NervousSystemConfig cfg;
    cfg.sensoryNeurons = 256;
    cfg.excitatoryNeurons = 640;
    cfg.inhibitoryNeurons = 192;
    cfg.contextNeurons = 256;
    cfg.motorNeurons = 128;
    cfg.modulatoryNeurons = 64;
    cfg.seed = 30'030U;
    cfg.validate();
    PersistentNervousSystem first(cfg);
    PersistentNervousSystem second(cfg);
    const auto spatial = first.inspectSpatial();
    require(spatial.neurons.size() == 1'536U,
        "large tissue profile has the wrong neuron count");
    require(spatial.dendrites.size() == 10'752U,
        "large tissue profile has the wrong dendrite count");
    require(spatial.axons.size() > spatial.neurons.size() * 8U
            && spatial.axons.size() < spatial.neurons.size() * 40U,
        "large tissue connectivity is not sparsely bounded");
    require(first.stateHash() == second.stateHash(),
        "large tissue construction is not deterministic");
    const auto action = first.step(deterministicFrame(1));
    require(std::isfinite(action.movement) && first.metrics().finite,
        "large tissue did not execute a finite causal step");
}

void snapshotContinuationTest(const NervousSystemConfig& cfg,
                              const std::filesystem::path& snapshotPath) {
    auto seeded = cfg;
    seeded.seed = 778899U;
    PersistentNervousSystem uninterrupted(seeded);
    for (std::uint64_t step = 0; step < 420U; ++step) {
        uninterrupted.step(deterministicFrame(step));
    }
    uninterrupted.saveSnapshot(snapshotPath.string());

    auto restoreConfig = cfg;
    restoreConfig.seed = 1U;
    PersistentNervousSystem restored(restoreConfig);
    restored.loadSnapshot(snapshotPath.string());
    require(uninterrupted.stateHash() == restored.stateHash(),
            "snapshot restore did not reproduce exact state");

    for (std::uint64_t step = 420U; step < 900U; ++step) {
        const auto frame = deterministicFrame(step);
        const auto actionA = uninterrupted.step(frame);
        const auto actionB = restored.step(frame);
        require(actionA.movement == actionB.movement &&
                    actionA.attention == actionB.attention &&
                    actionA.vocalization == actionB.vocalization &&
                    actionA.confidence == actionB.confidence,
                "snapshot continuation changed motor output");
    }
    require(uninterrupted.stateHash() == restored.stateHash(),
            "snapshot continuation diverged");
}

void multimodalPersistentTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 20260729U;
    PersistentNervousSystem system(seeded);
    const auto initialHash = system.stateHash();
    for (std::uint64_t step = 0; step < 1400U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto metrics = system.metrics();
    require(metrics.step == 1400U, "continuous step counter was reset");
    require(system.stateHash() != initialHash, "persistent state did not change");
    require(metrics.totalSpikes > 0U, "multimodal stream caused no spikes");
    require(metrics.totalTransmissions > 0U, "network caused no synaptic transmissions");
    require(std::isfinite(metrics.meanRateHz), "mean rate is not finite");
    require(std::isfinite(metrics.meanEnergy), "mean energy is not finite");
    require(metrics.meanEnergy >= 0.0 && metrics.meanEnergy <= 1.0,
            "energy left normalized range");
    require(system.dalePrincipleHolds(), "Dale law violated after plasticity");
}

void damageAndRecoveryTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 404U;
    PersistentNervousSystem system(seeded);
    ContinuousEnvironment environment(909U);
    environment.injectTextUtf8("continuous raw byte stream");
    tatarus::neuro::runClosedLoop(system, environment, 500U);
    const auto before = system.metrics();
    const auto damage = system.applyDamageWithReport(0.12, 0.20, 8181U);
    require(!damage.disabledNeurons.empty(), "damage report contains no neurons");
    require(!damage.disabledSynapses.empty(), "damage report contains no synapses");
    tatarus::neuro::runClosedLoop(system, environment, 1000U);
    const auto after = system.metrics();
    require(after.step == before.step + 1000U, "damaged system stopped advancing");
    require(std::isfinite(after.meanRateHz) && std::isfinite(after.meanEnergy),
            "damaged system became non-finite");
    require(after.totalSpikes >= before.totalSpikes, "cumulative spike state regressed");
    require(after.activeSynapses > 0, "all synapses disappeared after partial damage");
    require(system.dalePrincipleHolds(), "Dale law violated during damage recovery");
}

void biologicalSubstrateTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 20260816U;
    PersistentNervousSystem system(seeded);
    for (std::uint64_t step = 0; step < 1400U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto biology = system.biologicalMetrics();
    require(
        biology.dendriteSegments == cfg.neuronCount() * 7,
        "unexpected dendritic segment count");
    require(biology.astrocytes > 0 && biology.capillaries > 0,
            "glial or vascular substrate was not created");
    require(biology.dendriticSpikes > 0U,
            "active dendrites produced no local spikes");
    require(std::isfinite(biology.meanOxygen)
                && biology.meanOxygen > 0.0
                && std::isfinite(biology.meanBloodFlow)
                && biology.meanBloodFlow > 0.0,
            "neurovascular state became invalid");
    require(biology.meanAxonLengthUm > 0.0,
            "spatial synapses have no physical axon length");
}


void adaptiveMyelinationTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 260826U;
    PersistentNervousSystem system(seeded);
    const auto initial = system.biologicalMetrics();
    require(initial.oligodendrocytes > 0,
            "oligodendrocyte substrate was not created");
    require(initial.meanMyelinCoverage > 0.0
                && initial.meanConductionVelocityUmPerMs > 0.0,
            "initial myelin state is invalid");

    for (std::uint64_t step = 0; step < 4200U; ++step) {
        auto frame = deterministicFrame(step);
        if (step % 9U < 5U) {
            frame.visionEvents = {1.0, 0.8, 0.0, 0.0};
            frame.touch = {1.0};
            frame.novelty = 0.8;
        }
        system.step(frame);
    }

    const auto adapted = system.biologicalMetrics();
    require(adapted.myelinRemodelingUpdates > initial.myelinRemodelingUpdates,
            "adaptive myelin produced no remodeling events");
    require(std::isfinite(adapted.meanMyelinCoverage)
                && adapted.meanMyelinCoverage >= 0.03
                && adapted.meanMyelinCoverage <= 0.96,
            "myelin coverage left biological bounds");
    require(std::isfinite(adapted.meanConductionVelocityUmPerMs)
                && adapted.meanConductionVelocityUmPerMs > 90.0,
            "adaptive conduction velocity became invalid");
    require(std::isfinite(adapted.meanEffectiveDelayMs)
                && adapted.meanEffectiveDelayMs > 0.0,
            "adaptive conduction delay became invalid");
    require(std::isfinite(adapted.meanOligodendrocyteReserve)
                && adapted.meanOligodendrocyteReserve > 0.0,
            "oligodendrocyte energy reserve collapsed");
    require(std::abs(adapted.meanMyelinCoverage - initial.meanMyelinCoverage) > 1e-5,
            "myelin state did not change under repeated experience");
}

void microglialMaintenanceTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 270826U;
    PersistentNervousSystem system(seeded);

    for (std::uint64_t step = 0; step < 1800U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto baseline = system.biologicalMetrics();
    require(baseline.microglia > 0,
            "microglial surveillance substrate was not created");
    require(std::isfinite(baseline.meanMicroglialActivation)
                && std::isfinite(baseline.meanComplementTag)
                && std::isfinite(baseline.meanMicroglialRepairCapacity),
            "initial microglial state is non-finite");

    const auto state = system.inspect();
    std::vector<std::size_t> targets;
    for (const auto& synapse : state.synapses) {
        if (!synapse.active
            || std::abs(synapse.consolidatedWeight) < 0.035
            || synapse.usage < 0.01) {
            continue;
        }
        targets.push_back(synapse.index);
        if (targets.size() == 6U) break;
    }
    require(targets.size() >= 4U,
            "microglia test found too few functionally established synapses");

    const auto report = system.disableSynapses(targets);
    require(report.disabledSynapses.size() == targets.size(),
            "targeted synaptic damage was not applied");
    const auto damaged = system.biologicalMetrics();
    require(damaged.microglialDamageSignals
                >= baseline.microglialDamageSignals + targets.size(),
            "synaptic damage did not reach microglia");

    for (std::uint64_t step = 1800U; step < 3600U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto repaired = system.biologicalMetrics();
    require(repaired.microglialSurveillanceUpdates
                > baseline.microglialSurveillanceUpdates,
            "microglia performed no spatial surveillance");
    require(repaired.microglialRepairEvents > baseline.microglialRepairEvents,
            "microglia produced no repair events after targeted damage");
    require(std::isfinite(repaired.meanMicroglialActivation)
                && repaired.meanMicroglialActivation >= 0.0
                && repaired.meanMicroglialActivation <= 1.5,
            "microglial activation left its bounded range");
    require(std::isfinite(repaired.meanComplementTag)
                && repaired.meanComplementTag >= 0.0
                && repaired.meanComplementTag <= 1.5,
            "complement-like tagging left its bounded range");
    require(std::isfinite(repaired.meanMicroglialRepairCapacity)
                && repaired.meanMicroglialRepairCapacity > 0.0,
            "microglial repair capacity collapsed");

    const auto recoveredState = system.inspect();
    const bool lineageRecovered = std::any_of(
        recoveredState.synapses.begin(),
        recoveredState.synapses.end(),
        [&targets](const auto& synapse) {
            return synapse.active
                && synapse.parentSynapse >= 0
                && std::find(
                    targets.begin(),
                    targets.end(),
                    static_cast<std::size_t>(synapse.parentSynapse))
                    != targets.end();
        });
    require(lineageRecovered,
            "microglial repair did not restore a damaged synaptic lineage");
}

void microglialPruningTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 2727U;
    PersistentNervousSystem system(seeded);
    for (std::uint64_t step = 0; step < 1500U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto before = system.biologicalMetrics();
    const auto damage = system.applyDamageWithReport(0.25, 0.0, 123U);
    require(!damage.disabledNeurons.empty(),
            "microglial pruning test produced no damaged tissue");

    SensorFrame quiet;
    quiet.internalEnergy = 1.0;
    for (std::uint64_t step = 0; step < 3000U; ++step) {
        system.step(quiet);
    }
    const auto after = system.biologicalMetrics();
    require(after.microglialPruningEvents > before.microglialPruningEvents,
            "microglia pruned no chronically silent damaged pathways");
    require(system.metrics().structuralPruning >= after.microglialPruningEvents,
            "structural pruning metrics did not include microglial decisions");
    require(std::isfinite(after.meanInflammatoryTone)
                && after.meanInflammatoryTone >= 0.0
                && after.meanInflammatoryTone <= 1.5,
            "microglial inflammatory tone left its bounded range");
}


void prospectiveSequenceMemoryTest() {
    tatarus::neuro::temporal::ProspectiveMemory memory;
    const std::vector<double> a{1.0, 0.0, 0.0, 0.0};
    const std::vector<double> b{0.0, 1.0, 0.0, 0.0};
    const std::vector<double> c{0.0, 0.0, 1.0, 0.0};

    std::uint64_t step = 100;
    for (int cycle = 0; cycle < 8; ++cycle) {
        memory.observe(1, a, step, 1.0, 0.1, 0.2); step += 40;
        memory.observe(2, b, step, 1.0, 0.2, 0.2); step += 55;
        memory.observe(3, c, step, 1.0, 0.1, 0.2); step += 70;
    }

    memory.observe(1, a, step, 1.0, 0.1, 0.1);
    const auto predicted = memory.metrics();
    require(predicted.learnedTransitions >= 3,
            "prospective memory learned too few temporal transitions");
    require(predicted.predictedAssemblyId == 2,
            "prospective memory did not predict the learned successor");
    require(predicted.predictionConfidence > 0.70,
            "prospective successor confidence stayed too low");
    require(predicted.expectedDelayMs > 20.0,
            "prospective memory lost transition timing");
    require(predicted.sequenceFamiliarity > 0.70,
            "repeated sequence did not become familiar");
    require(!memory.primingPattern().empty(),
            "prospective successor created no top-down priming pattern");

    const auto hitsBefore = predicted.predictionHits;
    step += 40;
    memory.observe(2, b, step, 1.0, 0.2, 0.1);
    require(memory.metrics().predictionHits > hitsBefore,
            "correct learned continuation was not counted as prediction hit");

    // After B, the learned continuation is C. Deliberately violate it with A.
    const auto missesBefore = memory.metrics().predictionMisses;
    step += 55;
    memory.observe(1, a, step, 1.0, 0.0, 0.9);
    require(memory.metrics().predictionMisses > missesBefore,
            "unexpected continuation produced no prediction miss");
    require(memory.metrics().predictionError > 0.35,
            "unexpected continuation produced too little prediction error");
    require(memory.metrics().temporalSurprise > 0.05,
            "unexpected continuation produced no temporal surprise");

    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    memory.save(buffer);
    const auto expectedHash = memory.stateHash();
    tatarus::neuro::temporal::ProspectiveMemory restored;
    buffer.seekg(0);
    require(restored.load(buffer),
            "prospective memory snapshot block could not be restored");
    require(restored.stateHash() == expectedHash,
            "prospective memory snapshot did not restore exactly");
}

void prospectiveNervousIntegrationTest(const NervousSystemConfig& cfg) {
    auto seeded = cfg;
    seeded.seed = 281626U;
    seeded.assemblySimilarityThreshold = 0.80;
    PersistentNervousSystem system(seeded);

    auto runPhase = [&system](SensorFrame frame, int ticks) {
        frame.internalEnergy = 0.9;
        for (int i = 0; i < ticks; ++i) system.step(frame);
    };

    SensorFrame visual;
    visual.visionEvents = {1.0, 0.0, 0.0, 0.0};
    SensorFrame auditory;
    auditory.audioSamples = {1.0, -0.6};
    SensorFrame tactile;
    tactile.touch = {1.0, 0.8, 0.0, 0.0};

    for (int cycle = 0; cycle < 10; ++cycle) {
        runPhase(visual, 30);
        runPhase(auditory, 30);
        runPhase(tactile, 30);
    }

    const auto prospective = system.prospectiveMetrics();
    require(system.metrics().assemblyCount > 0,
            "nervous system formed no assemblies for temporal experience");
    require(prospective.observedTransitions > 4,
            "nervous system did not feed assembly succession into prospective memory");
    require(prospective.learnedTransitions > 0,
            "nervous system learned no successor transitions");
    require(prospective.predictionHits > 0,
            "repeated real assembly stream produced no prospective hits");
    require(std::isfinite(prospective.predictionError)
                && std::isfinite(prospective.temporalSurprise)
                && std::isfinite(prospective.sequenceFamiliarity),
            "prospective nervous-system metrics became non-finite");
}

void representationInstrumentationTest(const NervousSystemConfig& cfg) {
    auto controlled = cfg;
    controlled.seed = 5151U;
    controlled.eligibilityMemoryEnabled = false;
    PersistentNervousSystem system(controlled);
    for (std::uint64_t step = 0; step < 500U; ++step) {
        system.step(deterministicFrame(step));
    }
    const auto state = system.inspect();
    require(state.step == 500U, "representation state has wrong step");
    require(state.neurons.size() == static_cast<std::size_t>(cfg.neuronCount()),
            "representation state has wrong neuron count");
    require(!state.synapses.empty(), "representation state contains no synapses");
    require(std::all_of(
        state.synapses.begin(),
        state.synapses.end(),
        [](const auto& synapse) {
            return synapse.eligibility == 0.0;
        }),
        "disabled eligibility memory retained a trace");
}

void mechanismLibraryTest(const std::filesystem::path& libraryPath) {
    const tatarus::neuro::MechanismLibrary library;
    require(library.entries().size() >= 6U, "mechanism library is incomplete");
    library.writeJson(libraryPath.string());
    require(std::filesystem::exists(libraryPath), "mechanism library was not written");
    require(std::filesystem::file_size(libraryPath) > 100U,
            "mechanism library artifact is unexpectedly small");
}

void cognitiveBridgeTest(
    const NervousSystemConfig& cfg,
    const std::filesystem::path& outputDirectory) {
    auto controlled = cfg;
    controlled.seed = 91919U;
    controlled.eligibilityTauMs = 800.0;
    controlled.eligibilityTransmissionGain = 10.0;
    controlled.eligibilityIncrement = 20.0;
    PersistentNervousSystem system(controlled);
    tatarus::neuro::CognitiveBridge bridge(system);
    tatarus::neuro::CognitiveCommand command;
    command.attention = tatarus::neuro::AttentionTarget::Vision;
    command.recallCue = 1U;
    command.recallStrength = 0.5;
    SensorFrame frame;
    frame.internalEnergy = 0.9;
    frame.visionEvents = {1.0, 0.0, 0.0, 0.0};
    for (int step = 0; step < 80; ++step) {
        bridge.step(frame, command);
    }
    const auto state = bridge.readState();
    require(
        state.recalledStates.size()
            == 3U * tatarus::neuro::CognitiveBridge::recallGroupCount,
        "cognitive bridge did not expose pooled recall channels");
    require(
        std::all_of(
            state.recalledStates.begin(),
            state.recalledStates.end(),
            [](const auto& recall) {
                return std::isfinite(recall.strength);
            }),
        "cognitive bridge produced a non-finite recall");
    const auto nervousPath = outputDirectory / "bridge_nervous.agns";
    const auto bridgePath = outputDirectory / "bridge_state.bin";
    system.saveSnapshot(nervousPath);
    bridge.saveState(bridgePath);
    const auto expected = bridge.step(frame, command);
    const auto expectedHash = system.stateHash();
    system.loadSnapshot(nervousPath);
    bridge.loadState(bridgePath);
    const auto actual = bridge.step(frame, command);
    require(
        expectedHash == system.stateHash()
            && expected.action.movement == actual.action.movement
            && expected.state.functionalFingerprint
                == actual.state.functionalFingerprint,
        "composite cognitive snapshot did not continue exactly");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto outputDirectory =
            std::filesystem::path(argc > 1 ? argv[1] : "tatarus_neurobiology_test_output");
        std::filesystem::create_directories(outputDirectory);

        const auto cfg = compactConfig();
        biologicalVisualPathwayTest();
        deterministicTest(cfg);
        spatialActionMemoryTest(cfg, outputDirectory / "spatial_memory.agns");
        frozenEvaluationTest(cfg);
        spatialInstrumentationTest(cfg);
        causalTissueMechanicsTest();
        scalableSparseTissueTest();
        snapshotContinuationTest(cfg, outputDirectory / "continuation.agns");
        multimodalPersistentTest(cfg);
        damageAndRecoveryTest(cfg);
        biologicalSubstrateTest(cfg);
        adaptiveMyelinationTest(cfg);
        microglialMaintenanceTest(cfg);
        microglialPruningTest(cfg);
        prospectiveSequenceMemoryTest();
        prospectiveNervousIntegrationTest(cfg);
        representationInstrumentationTest(cfg);
        cognitiveBridgeTest(cfg, outputDirectory);
        mechanismLibraryTest(outputDirectory / "mechanism_library.json");

        std::ofstream report(outputDirectory / "test_report.txt");
        report << "PASS biological_retina_optic_v1_v2_ventral_topdown\n"
               << "PASS deterministic_same_seed\n"
               << "PASS intrinsic_spatial_action_memory\n"
               << "PASS frozen_evaluation_without_long_term_writes\n"
               << "PASS live_spatial_instrumentation\n"
               << "PASS causal_tissue_growth_mechanics\n"
               << "PASS scalable_sparse_regional_tissue\n"
               << "PASS exact_snapshot_continuation\n"
               << "PASS continuous_multimodal_state\n"
               << "PASS dale_plasticity\n"
               << "PASS damage_recovery\n"
               << "PASS biological_spatial_substrate\n"
               << "PASS adaptive_myelination\n"
               << "PASS microglial_surveillance_repair\n"
               << "PASS microglial_selective_pruning\n"
               << "PASS prospective_sequence_memory\n"
               << "PASS prospective_nervous_integration\n"
               << "PASS representation_instrumentation\n"
               << "PASS restricted_tatarus_cognition\n"
               << "PASS mechanism_library\n";
        std::cout << "All persistent nervous-system tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Persistent nervous-system test failure: " << error.what() << '\n';
        return 1;
    }
}
