#include "tatarus_cognition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tatarus::neuro {
namespace {

constexpr std::size_t kRecallGroups = CognitiveBridge::recallGroupCount;
constexpr std::array<char, 8> kBridgeMagic{
    'A', 'G', 'C', 'B', 'V', '1', '\0', '\0'};

template <typename T>
void writePod(std::ostream& output, const T& value) {
    output.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
}

template <typename T>
void readPod(std::istream& input, T& value) {
    input.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

void scale(std::vector<double>& values, double factor) {
    for (double& value : values) value *= factor;
}

}  // namespace

CognitiveBridge::CognitiveBridge(PersistentNervousSystem& nervousSystem)
    : nervousSystem_(nervousSystem) {
    const auto state = nervousSystem_.inspect();
    previousSpikeCounts_.reserve(state.neurons.size());
    for (const auto& neuron : state.neurons) {
        previousSpikeCounts_.push_back(neuron.spikeCount);
    }
    currentState_ = extractState(state, SensorFrame{}, MotorAction{});
}

SensorFrame CognitiveBridge::applyCommand(
    const SensorFrame& observation,
    const CognitiveCommand& command) const {
    SensorFrame frame = observation;
    const auto attenuateOther = [&](AttentionTarget selected) {
        if (command.attention != AttentionTarget::Balanced
            && command.attention != selected) {
            return 0.55;
        }
        return command.attention == selected ? 1.35 : 1.0;
    };
    scale(frame.visionEvents, attenuateOther(AttentionTarget::Vision));
    scale(frame.audioSamples, attenuateOther(AttentionTarget::Audio));
    scale(frame.touch, attenuateOther(AttentionTarget::Touch));
    if (command.attention != AttentionTarget::Balanced
        && command.attention != AttentionTarget::Text) {
        if (command.attention != AttentionTarget::Text) {
            for (auto& byte : frame.textBytes) {
                byte = static_cast<std::uint8_t>(byte & 0x55U);
            }
        }
    }
    if (!command.contextOnly) {
        frame.internalEnergy = std::clamp(
            frame.internalEnergy
                + (command.attention == AttentionTarget::Interoception
                    ? 0.05 : 0.0),
            0.0,
            1.0);
        frame.reward = std::clamp(
            frame.reward + command.reward,
            -1.0,
            1.0);
    }
    const std::vector<double> observationContext = frame.contextEvents;
    frame.contextEvents = observationContext;
    frame.contextEvents.insert(frame.contextEvents.end(), {
        std::clamp(command.motorIntent * command.intentStrength, -1.0, 1.0),
        std::clamp(
            command.recallStrength
                * (command.recallCue % 2U == 0U ? -1.0 : 1.0),
            -1.0,
            1.0),
        command.attention == AttentionTarget::Balanced
            ? 0.0
            : static_cast<double>(
                static_cast<std::uint8_t>(command.attention)) / 5.0});

    // Phase-10 bounded top-down context. These channels target the context
    // population only and are never interpreted as direct motor commands.
    const double goalStrength = std::clamp(command.goalBiasStrength, 0.0, 0.25);
    for (const double value : command.goalBias) {
        frame.contextEvents.push_back(
            std::clamp(value, -1.0, 1.0) * goalStrength);
    }
    const std::size_t semanticCount = std::min<std::size_t>(32U, command.semanticContext.size());
    for (std::size_t index = 0; index < semanticCount; ++index) {
        frame.contextEvents.push_back(
            std::clamp(command.semanticContext[index], -0.35, 0.35));
    }
    return frame;
}

CognitiveState CognitiveBridge::extractState(
    const RepresentationState& state,
    const SensorFrame& frame,
    const MotorAction& action) {
    CognitiveState result;
    result.step = state.step;
    std::vector<double> spikeGroups(kRecallGroups, 0.0);
    std::vector<double> membraneGroups(kRecallGroups, 0.0);
    std::vector<double> groupCounts(kRecallGroups, 0.0);
    if (previousSpikeCounts_.size() != state.neurons.size()) {
        previousSpikeCounts_.assign(state.neurons.size(), 0U);
    }
    for (std::size_t index = 0; index < state.neurons.size(); ++index) {
        const auto& neuron = state.neurons[index];
        if (neuron.role == PopulationRole::Sensory
            || neuron.role == PopulationRole::Modulatory) {
            previousSpikeCounts_[index] = neuron.spikeCount;
            continue;
        }
        const std::size_t group =
            (index * 17U + static_cast<std::size_t>(neuron.role) * 7U)
            % kRecallGroups;
        spikeGroups[group] += static_cast<double>(
            neuron.spikeCount - previousSpikeCounts_[index]);
        membraneGroups[group] += std::clamp(
            (neuron.somaMv - nervousSystem_.config().restingMv) / 20.0,
            -1.0,
            1.0);
        groupCounts[group] += 1.0;
        previousSpikeCounts_[index] = neuron.spikeCount;
    }
    result.recalledStates.reserve(kRecallGroups * 2U);
    for (std::size_t group = 0; group < kRecallGroups; ++group) {
        const double divisor = std::sqrt(std::max(1.0, groupCounts[group]));
        result.recalledStates.push_back(MemoryRecall{
            static_cast<std::uint32_t>(group),
            spikeGroups[group] / divisor});
        result.recalledStates.push_back(MemoryRecall{
            static_cast<std::uint32_t>(kRecallGroups + group),
            membraneGroups[group] / divisor});
    }
    const bool recallRequested =
        frame.contextEvents.size() > 1U
        && std::abs(frame.contextEvents[1]) > 0.05;
    if (recallRequested) {
        std::vector<double> synapticMemory(kRecallGroups, 0.0);
        std::vector<double> memoryCounts(kRecallGroups, 0.0);
        for (const auto& synapse : state.synapses) {
            if (!synapse.active) continue;
            const std::size_t group =
                (static_cast<std::size_t>(synapse.pre) * 131U
                    + static_cast<std::size_t>(synapse.post) * 17U
                    + static_cast<std::size_t>(synapse.receptor) * 7U)
                % kRecallGroups;
            synapticMemory[group] +=
                std::tanh(synapse.eligibility)
                * std::abs(synapse.consolidatedWeight)
                * synapse.resource;
            memoryCounts[group] += 1.0;
        }
        for (std::size_t group = 0; group < kRecallGroups; ++group) {
            result.recalledStates.push_back(MemoryRecall{
                static_cast<std::uint32_t>(2U * kRecallGroups + group),
                synapticMemory[group]
                    / std::sqrt(std::max(1.0, memoryCounts[group]))});
        }
    }

    std::vector<AssemblyStateView> assemblies = state.assemblies;
    std::sort(
        assemblies.begin(),
        assemblies.end(),
        [](const auto& left, const auto& right) {
            return left.activation > right.activation;
        });
    const std::size_t count = std::min<std::size_t>(8U, assemblies.size());
    result.activeRepresentations.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& assembly = assemblies[index];
        result.activeRepresentations.push_back(CognitiveRepresentation{
            assembly.id,
            assembly.activation,
            std::clamp(
                static_cast<double>(assembly.observations) / 12.0,
                0.0,
                1.0),
            static_cast<double>(state.step - assembly.lastActiveStep)
                * nervousSystem_.config().dtMs});
    }
    const auto& metrics = nervousSystem_.metrics();
    result.novelty = std::clamp(metrics.acetylcholine / 2.0, 0.0, 1.0);
    double maximumActivation = 0.0;
    for (const auto& assembly : result.activeRepresentations) {
        maximumActivation = std::max(
            maximumActivation,
            std::abs(assembly.activation));
    }
    result.salience = std::clamp(
        0.65 * maximumActivation
            + 0.20 * result.novelty
            + 0.15 * std::abs(metrics.dopamine),
        0.0,
        1.0);
    result.energyNeed = std::clamp(1.0 - metrics.meanEnergy, 0.0, 1.0);
    result.activityNeed = std::clamp(
        std::abs(
            metrics.meanRateHz - nervousSystem_.config().targetRateHz)
            / std::max(1.0, nervousSystem_.config().targetRateHz),
        0.0,
        2.0);
    result.predictionError = std::clamp(
        frame.reward - predictedReward_,
        -2.0,
        2.0);
    predictedReward_ =
        0.97 * predictedReward_ + 0.03 * frame.reward;
    result.confidence = action.confidence;
    std::uint64_t fingerprint = 1469598103934665603ULL;
    fingerprint = mix(fingerprint, result.step);
    for (const auto& recall : result.recalledStates) {
        const auto quantized = static_cast<std::int64_t>(
            std::llround(recall.strength * 1'000'000.0));
        fingerprint = mix(
            fingerprint,
            static_cast<std::uint64_t>(quantized));
    }
    for (const auto& representation : result.activeRepresentations) {
        fingerprint = mix(fingerprint, representation.id);
    }
    result.functionalFingerprint = fingerprint;
    return result;
}

CognitiveStep CognitiveBridge::step(
    const SensorFrame& observation,
    const CognitiveCommand& command) {
    const SensorFrame frame = applyCommand(observation, command);
    MotorAction action = nervousSystem_.step(frame);
    if (!command.contextOnly) {
        action.movement = std::tanh(
            action.movement
                + command.intentStrength * command.motorIntent);
    }
    currentState_ = extractState(nervousSystem_.inspect(), frame, action);
    return CognitiveStep{currentState_, action};
}

CognitiveState CognitiveBridge::readState() const {
    return currentState_;
}

void CognitiveBridge::saveState(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Bridge-State konnte nicht gespeichert werden");
    output.write(kBridgeMagic.data(), kBridgeMagic.size());
    writePod(output, predictedReward_);
    const std::uint64_t count = previousSpikeCounts_.size();
    writePod(output, count);
    for (const auto value : previousSpikeCounts_) writePod(output, value);
    if (!output) throw std::runtime_error("Bridge-State ist unvollständig");
}

void CognitiveBridge::loadState(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Bridge-State konnte nicht geladen werden");
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    if (magic != kBridgeMagic) {
        throw std::runtime_error("Unbekanntes Cognitive-Bridge-Format");
    }
    readPod(input, predictedReward_);
    std::uint64_t count = 0;
    readPod(input, count);
    if (count != nervousSystem_.inspect().neurons.size()) {
        throw std::runtime_error("Bridge-State passt nicht zum Nervensystem");
    }
    previousSpikeCounts_.resize(static_cast<std::size_t>(count));
    for (auto& value : previousSpikeCounts_) readPod(input, value);
    if (!input) throw std::runtime_error("Bridge-State ist beschädigt");
    currentState_ = CognitiveState{};
    currentState_.step = nervousSystem_.metrics().step;
}

std::vector<double> cognitiveFeatureVector(const CognitiveState& state) {
    std::vector<double> features(
        3U * CognitiveBridge::recallGroupCount,
        0.0);
    for (const auto& recall : state.recalledStates) {
        if (recall.channel < features.size()) {
            features[recall.channel] = recall.strength;
        }
    }
    for (std::size_t index = 0; index < 8U; ++index) {
        features.push_back(
            index < state.activeRepresentations.size()
                ? state.activeRepresentations[index].activation
                : 0.0);
    }
    features.push_back(state.novelty);
    features.push_back(state.salience);
    features.push_back(state.energyNeed);
    features.push_back(state.activityNeed);
    features.push_back(state.predictionError);
    features.push_back(state.confidence);
    features.push_back(1.0);
    return features;
}

}  // namespace tatarus::neuro
