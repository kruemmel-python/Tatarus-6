#pragma once

#include "tatarus/cartography.hpp"
#include "tatarus/cognitive_cue.hpp"
#include "tatarus/types.hpp"
#include "tatarus/throughput.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tatarus {

struct SleepTimingConfig {
    double circadianCycleMs = 86'400'000.0;
    double sleepPressureTauMs = 57'600'000.0;
    double nremMinimumMs = 4'800'000.0;
    double remMinimumMs = 900'000.0;
};

struct RobotMindConfig {
    std::uint64_t seed = 7411;
    std::size_t neuronCount = 96;
    int microstepsPerObservation = 24;
    std::size_t maximumPredictionAlternatives = 5;
    bool enableIdentity = true;
    bool enableCartography = true;
    CartographyConfig cartography;
    SleepTimingConfig sleepTiming;
};

struct ExperimentalIntervention {
    double astrocyteFunction = 1.0;
    double oxygenSupply = 1.0;
    double glucoseSupply = 1.0;
    double pumpEfficiency = 1.0;
    double myelinIntegrity = 1.0;
    double microgliaFunction = 1.0;
    double neuromodulatorGain = 1.0;
    bool sleepEnabled = true;
};

class RobotMind {
public:
    explicit RobotMind(RobotMindConfig config = {});
    ~RobotMind();

    RobotMind(const RobotMind&) = delete;
    RobotMind& operator=(const RobotMind&) = delete;
    RobotMind(RobotMind&&) noexcept;
    RobotMind& operator=(RobotMind&&) noexcept;

    ObserveResult observe(const Experience& experience);
    ObserveResult observeWithCognitiveCue(
        const Experience& experience,
        const CognitiveCue& cue);
    // Fuses a scanner frame into persistent map memory and lets the nervous
    // system observe the same exploration event in one deterministic step.
    ExplorerResult observeExplorer(
        const Experience& experience,
        const ScannerFrame& scannerFrame);
    ExplorerResult observeExplorerWithCognitiveCue(
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const CognitiveCue& cue);
    CartographyUpdate integrateScan(const ScannerFrame& scannerFrame);
    IdentityDecision observeIdentity(const IdentityObservation& observation);
    void confirmLastIdentity(bool correct);

    Prediction predict() const;
    std::vector<Prediction> predictHorizon(std::size_t depth) const;
    CognitiveContext context() const;
    RuntimeMetrics metrics() const;
    BiologicalTelemetry biology() const;
    PhysiologyTelemetry physiology() const;
    ProspectiveTelemetry prospection() const;
    MotorTelemetry motor() const;
    [[nodiscard]] ThroughputCounters throughput() const;
    [[nodiscard]] RobotMindConfig config() const;
    void resetThroughputCounters();

    [[nodiscard]] CartographySummary cartographySummary(
        EnvironmentId environmentId) const;
    [[nodiscard]] std::optional<MapVoxel> mapVoxelAt(
        EnvironmentId environmentId,
        const std::array<double, 3>& worldPositionMeters) const;
    [[nodiscard]] std::vector<MapVoxel> environmentVoxels(
        EnvironmentId environmentId) const;
    [[nodiscard]] std::string environmentMapJson(
        EnvironmentId environmentId) const;
    void clearEnvironmentMap(EnvironmentId environmentId);

    void beginAction(const ActionEvent& action);
    void endAction(const ActionOutcome& outcome);
    void endEpisode(bool reachedGoal);
    void rest(std::uint64_t ticks);
    void setLearningEnabled(bool enabled);
    [[nodiscard]] bool learningEnabled() const;
    void setExperimentalIntervention(const ExperimentalIntervention& intervention);
    void applyExperimentalDamage(
        double neuronFraction,
        double synapseFraction,
        std::uint64_t seed);

    void saveSnapshot(const std::filesystem::path& directory) const;
    bool loadSnapshot(const std::filesystem::path& directory);

    [[nodiscard]] std::string stateJson() const;
    [[nodiscard]] std::string spatialJson() const;
    [[nodiscard]] std::string liveJson() const;
    [[nodiscard]] std::string physiologyJson() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tatarus
