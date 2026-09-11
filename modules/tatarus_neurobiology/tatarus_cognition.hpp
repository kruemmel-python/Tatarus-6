#pragma once

#include "tatarus_neural_network.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tatarus::neuro {

enum class AttentionTarget : std::uint8_t {
    Balanced,
    Vision,
    Audio,
    Touch,
    Text,
    Interoception
};

struct CognitiveRepresentation {
    std::uint64_t id = 0;
    double activation = 0.0;
    double familiarity = 0.0;
    double ageMs = 0.0;
};

struct MemoryRecall {
    std::uint32_t channel = 0;
    double strength = 0.0;
};

struct CognitiveState {
    std::uint64_t step = 0;
    std::vector<CognitiveRepresentation> activeRepresentations;
    std::vector<MemoryRecall> recalledStates;
    double novelty = 0.0;
    double salience = 0.0;
    double energyNeed = 0.0;
    double activityNeed = 0.0;
    double predictionError = 0.0;
    double confidence = 0.0;
    std::uint64_t functionalFingerprint = 0;
};

struct CognitiveCommand {
    AttentionTarget attention = AttentionTarget::Balanced;
    double motorIntent = 0.0;
    double intentStrength = 0.0;
    std::uint32_t recallCue = 0;
    double recallStrength = 0.0;
    double reward = 0.0;
    std::array<double, 4> goalBias{};
    double goalBiasStrength = 0.0;
    std::vector<double> semanticContext;
    bool contextOnly = false;
};

struct CognitiveStep {
    CognitiveState state;
    MotorAction action;
};

// The bridge is the only interface used by the higher controller. It exposes
// population-level functional channels, never neuron or synapse objects.
class CognitiveBridge {
public:
    static constexpr std::size_t recallGroupCount = 64U;

    explicit CognitiveBridge(PersistentNervousSystem& nervousSystem);

    CognitiveStep step(
        const SensorFrame& observation,
        const CognitiveCommand& command);
    [[nodiscard]] CognitiveState readState() const;

    void saveState(const std::filesystem::path& path) const;
    void loadState(const std::filesystem::path& path);

private:
    PersistentNervousSystem& nervousSystem_;
    std::vector<std::uint64_t> previousSpikeCounts_;
    CognitiveState currentState_;
    double predictedReward_ = 0.0;

    [[nodiscard]] SensorFrame applyCommand(
        const SensorFrame& observation,
        const CognitiveCommand& command) const;
    [[nodiscard]] CognitiveState extractState(
        const RepresentationState& state,
        const SensorFrame& frame,
        const MotorAction& action);
};

std::vector<double> cognitiveFeatureVector(const CognitiveState& state);

}  // namespace tatarus::neuro
