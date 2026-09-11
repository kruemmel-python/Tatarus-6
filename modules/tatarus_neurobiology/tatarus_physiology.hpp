#pragma once

#include "tatarus_neural_network.hpp"
#include "tatarus/throughput.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace tatarus::neuro::physiology {

struct SynapseCoupling {
    std::uint32_t pre = 0;
    std::uint32_t post = 0;
    double eligibility = 0.0;
    double weight = 0.0;
    bool inhibitory = false;
    bool active = false;
};

struct PlasticityEffect {
    double consolidationGain = 0.0;
    double weightScale = 1.0;
};

// A bounded tissue-level physiology model. It conserves ion flux between
// intracellular and extracellular compartments, models spatial diffusion,
// and exposes only causal modifiers to the electrical network.
class PhysiologicalSubstrate {
public:
    PhysiologicalSubstrate(
        const NervousSystemConfig& config,
        const BiologicalSpatialState& spatial,
        const std::vector<PopulationRole>& roles,
        const std::vector<NeuronSubtype>& subtypes);
    ~PhysiologicalSubstrate();

    void step(
        const SensorFrame& frame,
        const std::vector<std::uint8_t>& spikes,
        const std::vector<double>& somaMv,
        const std::vector<double>& dendriteMv,
        const std::vector<double>& ratesHz,
        const std::vector<double>& energy);

    [[nodiscard]] double somaBiasMv(std::size_t neuron) const;
    [[nodiscard]] double dendriteBiasMv(std::size_t neuron) const;
    [[nodiscard]] double metabolicScale(std::size_t neuron) const;
    [[nodiscard]] std::vector<PlasticityEffect> updateSynapses(
        const std::vector<SynapseCoupling>& synapses);
    void setExperimentalFactors(
        double pumpEfficiency,
        double astrocyteFunction,
        double neuromodulatorGain,
        double metabolicSupply,
        bool sleepEnabled);

    [[nodiscard]] const PhysiologyMetrics& metrics() const;
    [[nodiscard]] PhysiologicalState inspect() const;
    [[nodiscard]] std::uint64_t stateHash() const;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    void save(std::ostream& output) const;
    [[nodiscard]] bool load(std::istream& input);

private:
    struct Node;
    struct Edge;
    struct SynapseMolecule;

    NervousSystemConfig config_;
    std::vector<Node> nodes_;
    std::vector<Edge> diffusionEdges_;
    std::vector<SynapseMolecule> synapseMolecules_;
    PhysiologyMetrics metrics_;
    tatarus::ThroughputCounters* throughput_ = nullptr;
    SleepStage sleepStage_ = SleepStage::Wake;
    double simulatedMs_ = 0.0;
    double stageMs_ = 0.0;
    double sleepPressure_ = 0.15;
    double circadianPhaseHours_ = 8.0;
    double gammaPhase_ = 0.0;
    double thetaPhase_ = 0.0;
    double gammaX_ = 0.0;
    double gammaY_ = 0.0;
    double thetaX_ = 0.0;
    double thetaY_ = 0.0;
    double extracellularVolumeFraction_ = 0.20;
    double tissueWaste_ = 0.0;
    double cumulativeAtpConsumed_ = 0.0;
    std::uint64_t ltpEvents_ = 0;
    std::uint64_t ltdEvents_ = 0;
    std::uint64_t sleepTransitions_ = 0;
    double experimentalPumpEfficiency_ = 1.0;
    double experimentalAstrocyteFunction_ = 1.0;
    double experimentalNeuromodulatorGain_ = 1.0;
    double experimentalMetabolicSupply_ = 1.0;
    bool experimentalSleepEnabled_ = true;

    void updateSleep(double dtMs);
    void updateNeuromodulators(const SensorFrame& frame, double dtMs);
    void updateIons(
        const SensorFrame& frame,
        const std::vector<std::uint8_t>& spikes,
        const std::vector<double>& energy,
        double dtMs);
    void updateChannelsAndGenes(
        const std::vector<std::uint8_t>& spikes,
        const std::vector<double>& somaMv,
        const std::vector<double>& dendriteMv,
        const std::vector<double>& ratesHz,
        double dtMs);
    void updateMetrics();
};

} // namespace tatarus::neuro::physiology
