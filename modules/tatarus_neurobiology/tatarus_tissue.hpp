#pragma once

#include "tatarus_neural_network.hpp"
#include "tatarus/throughput.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace tatarus::neuro::biology {

enum class SynapticChannel : std::uint8_t {
    Ampa,
    Nmda,
    GabaA,
    GabaB,
};

struct DendriteOutput {
    double membraneMv = -65.0;
    double gAmpa = 0.0;
    double gNmda = 0.0;
    double gGabaA = 0.0;
    double gGabaB = 0.0;
    double calcium = 0.0;
};

struct ElectrophysiologyParameters {
    double dtMs = 1.0;
    double restingMv = -65.0;
    double tauDendriteMs = 35.0;
    double somaDendriteCoupling = 0.22;
    double tauAmpaMs = 5.0;
    double tauNmdaMs = 80.0;
    double tauGabaAMs = 10.0;
    double tauGabaBMs = 120.0;
    double ampaReversalMv = 0.0;
    double nmdaReversalMv = 0.0;
    double gabaAReversalMv = -75.0;
    double gabaBReversalMv = -95.0;
};

class SpatialSubstrate {
public:
    SpatialSubstrate() = default;
    SpatialSubstrate(
        std::size_t neuronCount,
        const std::vector<PopulationRole>& roles,
        std::uint64_t seed,
        double restingMv,
        double dtMs);

    void rebuild(
        std::size_t neuronCount,
        const std::vector<PopulationRole>& roles,
        std::uint64_t seed,
        double restingMv,
        double dtMs);

    [[nodiscard]] double connectionScale(
        std::size_t pre,
        std::size_t post) const;
    [[nodiscard]] std::uint32_t conductionDelaySteps(
        std::size_t pre,
        std::size_t post,
        double dtMs) const;
    [[nodiscard]] std::uint32_t effectiveConductionDelaySteps(
        std::size_t synapseIndex,
        std::uint32_t fallbackDelaySteps,
        double dtMs) const;

    void synchronizeSynapse(
        std::size_t synapseIndex,
        std::size_t pre,
        std::size_t post);
    void truncateBindings(std::size_t synapseCount);
    void refreshMetrics();
    void updateTissueMechanics(
        std::size_t activeSynapses,
        double proteinAvailability,
        double physiologicalEcsFraction,
        double dtMs);
    [[nodiscard]] bool canAllocateSynapticMaterial() const;
    void recordGrowthLimited();

    void recordAxonUse(std::size_t synapseIndex, double amplitude);
    void recordAxonArrival(std::size_t synapseIndex, double amplitude);

    void reportSynapseDamage(std::size_t synapseIndex, double severity = 1.0);
    void reportNeuronDamage(std::size_t neuronIndex, double severity = 1.0);
    [[nodiscard]] double microglialPruningDrive(
        std::size_t synapseIndex,
        double agePressure,
        double usageProtection,
        double weightProtection) const;
    [[nodiscard]] double microglialRepairDrive(
        std::size_t synapseIndex,
        double consolidatedStrength,
        double usageEvidence) const;
    void recordMicroglialPruning(std::size_t synapseIndex);
    void recordMicroglialRepair(std::size_t synapseIndex);

    void deposit(
        std::size_t synapseIndex,
        SynapticChannel channel,
        double amplitude,
        double localEnergyCost);

    [[nodiscard]] DendriteOutput updateNeuron(
        std::size_t neuronIndex,
        double somaMv,
        double externalDrive,
        const ElectrophysiologyParameters& parameters);

    void updateGliaAndVasculature(
        const std::vector<std::uint8_t>& spikes,
        const std::vector<double>& filteredRatesHz,
        double dtMs);
    void setExperimentalFactors(
        double astrocyteFunction,
        double oxygenSupply,
        double glucoseSupply,
        double myelinIntegrity,
        double microgliaFunction);

    [[nodiscard]] double energyRecoveryScale(std::size_t neuronIndex) const;
    [[nodiscard]] const BiologicalMetrics& metrics() const;
    [[nodiscard]] BiologicalSpatialState inspectSpatial() const;
    [[nodiscard]] std::uint64_t stateHash() const;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    void save(std::ostream& output) const;
    bool load(
        std::istream& input,
        std::size_t expectedNeuronCount,
        double restingMv,
        double dtMs);

private:
    struct Vec3 {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct Morphology {
        Vec3 soma;
        std::uint32_t firstSegment = 0;
        std::uint16_t segmentCount = 0;
    };

    struct Segment {
        std::uint32_t neuron = 0;
        std::int32_t parent = -1;
        Vec3 position;
        double membraneMv = -65.0;
        double calcium = 0.0;
        double gAmpa = 0.0;
        double gNmda = 0.0;
        double gGabaA = 0.0;
        double gGabaB = 0.0;
        double localEnergy = 1.0;
        std::uint16_t astrocyte = 0;
        std::uint8_t branchOrder = 0;
        std::uint8_t activeRefractory = 0;
    };

    struct SynapseBinding {
        std::uint32_t segment = 0;
        double axonLengthUm = 0.0;
    };

    struct Astrocyte {
        Vec3 center;
        double calcium = 0.0;
        double glutamateLoad = 0.0;
        double potassiumLoad = 0.0;
        double metabolicDemand = 0.0;
        double support = 1.0;
        std::uint16_t vessel = 0;
    };

    struct Capillary {
        Vec3 position;
        double flow = 1.0;
        double oxygen = 1.0;
        double glucose = 1.0;
    };

    struct Oligodendrocyte {
        Vec3 center;
        double myelinReserve = 1.0;
        double metabolicLoad = 0.0;
        double remodelingSignal = 0.0;
        std::uint16_t vessel = 0;
    };

    struct MyelinState {
        std::uint32_t preNeuron = 0;
        std::uint32_t postNeuron = 0;
        std::uint16_t oligodendrocyte = 0;
        double coverage = 0.0;
        double axonCaliber = 1.0;
        double activityTrace = 0.0;
        double arrivalTrace = 0.0;
        double coincidenceTrace = 0.0;
        double remodelingRate = 0.0;
        double conductionVelocityUmPerMs = 180.0;
    };

    struct Microglia {
        Vec3 center;
        double activation = 0.08;
        double inflammatoryTone = 0.0;
        double phagocyticLoad = 0.0;
        double repairCapacity = 0.92;
        double surveillance = 0.70;
        double damageLoad = 0.0;
        std::uint16_t vessel = 0;
    };

    struct SynapseMaintenance {
        std::uint32_t preNeuron = 0;
        std::uint32_t postNeuron = 0;
        std::uint16_t microglia = 0;
        double functionalTrace = 0.0;
        double complementTag = 0.04;
        double damageSignal = 0.0;
        double repairSignal = 0.0;
        double surveillanceScore = 0.0;
    };

    std::uint64_t seed_ = 0;
    double restingMv_ = -65.0;
    double dtMs_ = 1.0;
    std::vector<Morphology> morphologies_;
    std::vector<PopulationRole> roles_;
    std::vector<BrainHemisphere> hemispheres_;
    std::vector<BrainRegion> regions_;
    std::vector<CorticalLayer> layers_;
    std::vector<Segment> segments_;
    std::vector<SynapseBinding> bindings_;
    std::vector<Astrocyte> astrocytes_;
    std::vector<Capillary> capillaries_;
    std::vector<Oligodendrocyte> oligodendrocytes_;
    std::vector<MyelinState> myelin_;
    std::vector<Microglia> microglia_;
    std::vector<SynapseMaintenance> maintenance_;
    BiologicalMetrics metrics_;
    tatarus::ThroughputCounters* throughput_ = nullptr;
    struct TissueMechanics {
        std::uint64_t baselineActiveSynapses = 0;
        std::uint64_t activeSynapses = 0;
        std::uint64_t growthLimitedEvents = 0;
        double baselineVolumeUm3 = 0.0;
        double currentVolumeUm3 = 0.0;
        double linearExpansion = 1.0;
        double synapticMaterialVolumeUm3 = 0.0;
        double solidPackingFraction = 0.80;
        double extracellularSpaceFraction = 0.20;
        double pressureKPa = 0.0;
        double materialReserve = 1.0;
        double cumulativeSynthesizedUm3 = 0.0;
        double cumulativeRecycledUm3 = 0.0;
        double previousSynapticMaterialUm3 = 0.0;
        bool initialized = false;
    } mechanics_;
    double experimentalAstrocyteFunction_ = 1.0;
    double experimentalOxygenSupply_ = 1.0;
    double experimentalGlucoseSupply_ = 1.0;
    double experimentalMyelinIntegrity_ = 1.0;
    double experimentalMicrogliaFunction_ = 1.0;

    [[nodiscard]] static double distance(const Vec3& a, const Vec3& b);
    [[nodiscard]] static std::uint64_t mix64(std::uint64_t value);
    [[nodiscard]] std::uint32_t selectSegment(
        std::size_t post,
        std::size_t synapseIndex,
        std::size_t pre) const;
    void rebuildOligodendrocytes();
    void rebuildMicroglia();
    void initializeMyelinState(
        std::size_t synapseIndex,
        std::size_t pre,
        std::size_t post);
    void initializeMaintenanceState(
        std::size_t synapseIndex,
        std::size_t pre,
        std::size_t post);
    [[nodiscard]] std::uint16_t nearestOligodendrocyte(
        const Vec3& point) const;
    [[nodiscard]] std::uint16_t nearestMicroglia(
        const Vec3& point) const;
    [[nodiscard]] double conductionVelocity(const MyelinState& state) const;
    [[nodiscard]] Vec3 scaledPoint(const Vec3& point) const;
    void updateMetrics();
};

}  // namespace tatarus::neuro::biology
