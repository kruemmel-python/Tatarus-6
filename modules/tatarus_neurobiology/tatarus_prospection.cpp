#include "tatarus_prospection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tatarus::neuro::temporal {
namespace {

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) throw std::runtime_error("Prospektiver Snapshot konnte nicht geschrieben werden");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("Prospektiver Snapshot ist abgeschnitten");
}

template <class T>
void writeVector(std::ostream& output, const std::vector<T>& values) {
    const std::uint64_t size = values.size();
    writePod(output, size);
    if (!values.empty()) {
        output.write(
            reinterpret_cast<const char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
        if (!output) throw std::runtime_error("Prospektiver Snapshot-Vektor konnte nicht geschrieben werden");
    }
}

template <class T>
void readVector(std::istream& input, std::vector<T>& values, std::uint64_t maxSize) {
    std::uint64_t size = 0;
    readPod(input, size);
    if (size > maxSize) throw std::runtime_error("Prospektiver Snapshot-Vektor ist zu gross");
    values.resize(static_cast<std::size_t>(size));
    if (!values.empty()) {
        input.read(
            reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
        if (!input) throw std::runtime_error("Prospektiver Snapshot-Vektor ist abgeschnitten");
    }
}

std::uint64_t fnv1a(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

void ProspectiveMemory::reset() {
    transitions_.clear();
    prototypes_.clear();
    primingPattern_.clear();
    predictedPrototype_.clear();
    metrics_ = {};
    previousAssemblyId_ = 0;
    previousAssemblyStep_ = 0;
    predictionOriginStep_ = 0;
    predictionDeadlineStep_ = 0;
    primingGain_ = 0.0;
}

void ProspectiveMemory::tick(std::uint64_t step, double dtMs) {
    if (!std::isfinite(dtMs) || dtMs <= 0.0) return;
    if (throughput_) {
        throughput_->recordReadWrite<ProspectiveMetrics>(tatarus::ThroughputDomain::Prospection);
        throughput_->recordReadWrite<double>(tatarus::ThroughputDomain::Prospection, 1);
    }

    const double decay = std::exp(-dtMs / 320.0);
    primingGain_ *= decay;
    metrics_.prospectiveActivation = primingGain_;
    metrics_.predictionError *= std::exp(-dtMs / 1400.0);
    metrics_.temporalSurprise *= std::exp(-dtMs / 3500.0);

    if (metrics_.predictedAssemblyId != 0
        && predictionDeadlineStep_ != 0
        && step > predictionDeadlineStep_) {
        const double missedError = std::clamp(metrics_.predictionConfidence, 0.0, 1.0);
        ++metrics_.predictionMisses;
        metrics_.predictionError = std::max(metrics_.predictionError, missedError);
        metrics_.temporalSurprise = std::clamp(
            0.82 * metrics_.temporalSurprise + 0.18 * missedError,
            0.0,
            1.0);
        clearPendingPrediction();
    }
}

void ProspectiveMemory::observe(
    std::uint64_t assemblyId,
    const std::vector<double>& prototype,
    std::uint64_t step,
    double dtMs,
    double reward,
    double novelty) {
    if (assemblyId == 0 || prototype.empty()) return;
    if (throughput_) {
        throughput_->record(tatarus::ThroughputDomain::Prospection, prototypes_.size(), sizeof(PrototypeRecord), true, false);
        throughput_->record(tatarus::ThroughputDomain::Prospection, prototype.size(), sizeof(double), true, false);
    }

    auto prototypeIt = std::find_if(
        prototypes_.begin(), prototypes_.end(),
        [assemblyId](const PrototypeRecord& record) { return record.id == assemblyId; });
    if (prototypeIt == prototypes_.end()) {
        prototypes_.push_back(PrototypeRecord{assemblyId, prototype});
    } else {
        prototypeIt->prototype = prototype;
    }

    evaluatePendingPrediction(assemblyId, prototype, step, dtMs);

    if (previousAssemblyId_ != 0 && previousAssemblyStep_ < step) {
        learnTransition(
            previousAssemblyId_,
            assemblyId,
            previousAssemblyStep_,
            step,
            dtMs,
            reward,
            novelty);
    }

    previousAssemblyId_ = assemblyId;
    previousAssemblyStep_ = step;
    metrics_.lastAssemblyId = assemblyId;
    predictFrom(assemblyId, step, dtMs);
}

void ProspectiveMemory::recall(
    std::uint64_t assemblyId,
    const std::vector<double>& prototype,
    std::uint64_t step,
    double dtMs) {
    if (assemblyId == 0 || prototype.empty()) return;
    evaluatePendingPrediction(assemblyId, prototype, step, dtMs);
    previousAssemblyId_ = assemblyId;
    previousAssemblyStep_ = step;
    metrics_.lastAssemblyId = assemblyId;
    predictFrom(assemblyId, step, dtMs);
}

const ProspectiveMetrics& ProspectiveMemory::metrics() const {
    return metrics_;
}

const std::vector<double>& ProspectiveMemory::primingPattern() const {
    return primingPattern_;
}

void ProspectiveMemory::clearPendingPrediction() {
    metrics_.predictedAssemblyId = 0;
    metrics_.predictionConfidence = 0.0;
    metrics_.expectedDelayMs = 0.0;
    metrics_.prospectiveActivation = 0.0;
    predictionOriginStep_ = 0;
    predictionDeadlineStep_ = 0;
    primingGain_ = 0.0;
    primingPattern_.clear();
    predictedPrototype_.clear();
}

void ProspectiveMemory::evaluatePendingPrediction(
    std::uint64_t actualAssemblyId,
    const std::vector<double>& actualPrototype,
    std::uint64_t step,
    double dtMs) {
    if (metrics_.predictedAssemblyId == 0) return;
    if (throughput_) {
        throughput_->recordReadWrite<ProspectiveMetrics>(tatarus::ThroughputDomain::Prospection);
        throughput_->record(tatarus::ThroughputDomain::Prospection, predictedPrototype_.size() + actualPrototype.size(), sizeof(double), true, false);
    }

    const double confidence = std::clamp(metrics_.predictionConfidence, 0.0, 1.0);
    const bool hit = actualAssemblyId == metrics_.predictedAssemblyId;
    const double prototypeSimilarity = predictedPrototype_.empty()
        ? (hit ? 1.0 : 0.0)
        : std::clamp(cosineSimilarity(predictedPrototype_, actualPrototype), -1.0, 1.0);
    const double prototypeDistance = std::clamp((1.0 - prototypeSimilarity) * 0.5, 0.0, 1.0);

    const double actualDelayMs = predictionOriginStep_ > 0 && step >= predictionOriginStep_
        ? static_cast<double>(step - predictionOriginStep_) * dtMs
        : metrics_.expectedDelayMs;
    const double timingError = metrics_.expectedDelayMs > 1e-9
        ? std::clamp(
            std::abs(actualDelayMs - metrics_.expectedDelayMs)
                / std::max(25.0, metrics_.expectedDelayMs),
            0.0,
            1.0)
        : 0.0;

    double rawError = 0.0;
    if (hit) {
        ++metrics_.predictionHits;
        rawError = confidence * (0.18 * timingError + 0.12 * prototypeDistance);
    } else {
        ++metrics_.predictionMisses;
        rawError = confidence * (0.62 + 0.38 * prototypeDistance);
    }
    metrics_.predictionError = std::clamp(rawError, 0.0, 1.0);
    metrics_.temporalSurprise = std::clamp(
        0.78 * metrics_.temporalSurprise + 0.22 * rawError,
        0.0,
        1.0);
    clearPendingPrediction();
    // Keep the just-computed error and surprise after clearing the pending target.
    metrics_.predictionError = std::clamp(rawError, 0.0, 1.0);
}

void ProspectiveMemory::learnTransition(
    std::uint64_t fromId,
    std::uint64_t toId,
    std::uint64_t fromStep,
    std::uint64_t toStep,
    double dtMs,
    double reward,
    double novelty) {
    if (toStep <= fromStep || !std::isfinite(dtMs) || dtMs <= 0.0) return;
    const double intervalMs = static_cast<double>(toStep - fromStep) * dtMs;
    if (throughput_) throughput_->record(tatarus::ThroughputDomain::Prospection, transitions_.size(), sizeof(Transition), true, false);

    auto it = std::find_if(
        transitions_.begin(), transitions_.end(),
        [fromId, toId](const Transition& transition) {
            return transition.fromId == fromId && transition.toId == toId;
        });
    if (it == transitions_.end()) {
        if (throughput_) throughput_->record(tatarus::ThroughputDomain::Prospection, 1, sizeof(Transition), false, true);
        Transition transition;
        transition.fromId = fromId;
        transition.toId = toId;
        transition.observations = 1;
        transition.meanIntervalMs = intervalMs;
        transition.meanReward = std::clamp(reward, -1.0, 1.0);
        transition.meanNovelty = std::clamp(novelty, 0.0, 1.0);
        transition.lastObservedStep = toStep;
        transitions_.push_back(transition);
        metrics_.learnedTransitions = transitions_.size();
        ++metrics_.observedTransitions;
        return;
    }

    if (throughput_) throughput_->record(tatarus::ThroughputDomain::Prospection, 1, sizeof(Transition), true, true);
    ++it->observations;
    ++metrics_.observedTransitions;
    const double n = static_cast<double>(it->observations);
    const double delta = intervalMs - it->meanIntervalMs;
    it->meanIntervalMs += delta / n;
    const double delta2 = intervalMs - it->meanIntervalMs;
    it->intervalM2 += delta * delta2;
    it->meanReward += (std::clamp(reward, -1.0, 1.0) - it->meanReward) / n;
    it->meanNovelty += (std::clamp(novelty, 0.0, 1.0) - it->meanNovelty) / n;
    it->lastObservedStep = toStep;
    metrics_.learnedTransitions = transitions_.size();
}

void ProspectiveMemory::predictFrom(
    std::uint64_t assemblyId,
    std::uint64_t step,
    double dtMs) {
    std::uint64_t totalObservations = 0;
    const Transition* winner = nullptr;
    double winnerScore = -std::numeric_limits<double>::infinity();
    if (throughput_) throughput_->record(tatarus::ThroughputDomain::Prospection, transitions_.size(), sizeof(Transition), true, false);

    for (const auto& transition : transitions_) {
        if (transition.fromId != assemblyId || transition.observations == 0) continue;
        totalObservations += transition.observations;
        const double recencySteps = step >= transition.lastObservedStep
            ? static_cast<double>(step - transition.lastObservedStep)
            : 0.0;
        const double recency = std::exp(-recencySteps * dtMs / 120000.0);
        const double outcomeBias = 1.0 + 0.10 * std::max(0.0, transition.meanReward);
        const double score = static_cast<double>(transition.observations) * recency * outcomeBias;
        if (score > winnerScore) {
            winnerScore = score;
            winner = &transition;
        }
    }

    if (winner == nullptr || totalObservations == 0) {
        metrics_.sequenceFamiliarity = 0.0;
        clearPendingPrediction();
        return;
    }

    const double branchProbability = static_cast<double>(winner->observations)
        / static_cast<double>(totalObservations);
    const double maturity = 1.0 - std::exp(-static_cast<double>(winner->observations) / 2.5);
    const double familiarity = 1.0 - std::exp(-static_cast<double>(totalObservations) / 4.0);
    const double confidence = std::clamp(branchProbability * maturity, 0.0, 1.0);

    if (throughput_) throughput_->record(tatarus::ThroughputDomain::Prospection, prototypes_.size(), sizeof(PrototypeRecord), true, false);
    const auto prototypeIt = std::find_if(
        prototypes_.begin(), prototypes_.end(),
        [winner](const PrototypeRecord& record) { return record.id == winner->toId; });
    if (prototypeIt == prototypes_.end() || prototypeIt->prototype.empty()) {
        clearPendingPrediction();
        metrics_.sequenceFamiliarity = familiarity;
        return;
    }

    metrics_.predictedAssemblyId = winner->toId;
    metrics_.predictionConfidence = confidence;
    metrics_.expectedDelayMs = std::max(dtMs, winner->meanIntervalMs);
    metrics_.sequenceFamiliarity = familiarity;
    primingGain_ = 0.15 + 0.85 * confidence;
    metrics_.prospectiveActivation = primingGain_;
    predictionOriginStep_ = step;

    const double sigma = transitionIntervalStdDev(*winner);
    const double toleranceMs = std::max(90.0, 2.5 * sigma + 0.35 * metrics_.expectedDelayMs);
    const auto deadlineDelta = static_cast<std::uint64_t>(std::ceil(
        (metrics_.expectedDelayMs + toleranceMs) / std::max(dtMs, 1e-9)));
    predictionDeadlineStep_ = step + std::max<std::uint64_t>(1, deadlineDelta);

    predictedPrototype_ = prototypeIt->prototype;
    if (throughput_) {
        throughput_->record(tatarus::ThroughputDomain::Prospection, predictedPrototype_.size(), sizeof(double), true, true);
        throughput_->record(tatarus::ThroughputDomain::Prospection, predictedPrototype_.size(), sizeof(double), false, true);
    }
    primingPattern_.resize(predictedPrototype_.size());
    for (std::size_t i = 0; i < predictedPrototype_.size(); ++i) {
        primingPattern_[i] = std::clamp(predictedPrototype_[i], -1.0, 1.0) * primingGain_;
    }
}

double ProspectiveMemory::cosineSimilarity(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    if (left.size() != right.size() || left.empty()) return 0.0;
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        dot += left[i] * right[i];
        leftNorm += left[i] * left[i];
        rightNorm += right[i] * right[i];
    }
    return dot / (std::sqrt(leftNorm * rightNorm) + 1e-12);
}

double ProspectiveMemory::transitionIntervalStdDev(const Transition& transition) const {
    if (transition.observations < 2) return 0.0;
    return std::sqrt(std::max(
        0.0,
        transition.intervalM2 / static_cast<double>(transition.observations - 1)));
}

std::uint64_t ProspectiveMemory::stateHash() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](const auto& value) {
        hash = fnv1a(hash, &value, sizeof(value));
    };
    add(previousAssemblyId_);
    add(previousAssemblyStep_);
    add(predictionOriginStep_);
    add(predictionDeadlineStep_);
    add(primingGain_);
    add(metrics_.learnedTransitions);
    add(metrics_.observedTransitions);
    add(metrics_.predictionHits);
    add(metrics_.predictionMisses);
    add(metrics_.predictedAssemblyId);
    add(metrics_.lastAssemblyId);
    add(metrics_.predictionConfidence);
    add(metrics_.expectedDelayMs);
    add(metrics_.predictionError);
    add(metrics_.temporalSurprise);
    add(metrics_.sequenceFamiliarity);
    add(metrics_.prospectiveActivation);
    for (const auto& transition : transitions_) {
        add(transition.fromId);
        add(transition.toId);
        add(transition.observations);
        add(transition.meanIntervalMs);
        add(transition.intervalM2);
        add(transition.meanReward);
        add(transition.meanNovelty);
        add(transition.lastObservedStep);
    }
    for (const auto& record : prototypes_) {
        add(record.id);
        const std::uint64_t size = record.prototype.size();
        add(size);
        if (!record.prototype.empty()) {
            hash = fnv1a(
                hash,
                record.prototype.data(),
                record.prototype.size() * sizeof(double));
        }
    }
    for (const auto* values : {&primingPattern_, &predictedPrototype_}) {
        const std::uint64_t size = values->size();
        add(size);
        if (!values->empty()) {
            hash = fnv1a(hash, values->data(), values->size() * sizeof(double));
        }
    }
    return hash;
}

void ProspectiveMemory::save(std::ostream& output) const {
    const std::array<char, 4> magic{'P', 'R', 'O', '1'};
    output.write(magic.data(), magic.size());
    if (!output) throw std::runtime_error("Prospektiver Snapshot konnte nicht begonnen werden");

    writePod(output, metrics_.learnedTransitions);
    writePod(output, metrics_.observedTransitions);
    writePod(output, metrics_.predictionHits);
    writePod(output, metrics_.predictionMisses);
    writePod(output, metrics_.predictedAssemblyId);
    writePod(output, metrics_.lastAssemblyId);
    writePod(output, metrics_.predictionConfidence);
    writePod(output, metrics_.expectedDelayMs);
    writePod(output, metrics_.predictionError);
    writePod(output, metrics_.temporalSurprise);
    writePod(output, metrics_.sequenceFamiliarity);
    writePod(output, metrics_.prospectiveActivation);
    writePod(output, previousAssemblyId_);
    writePod(output, previousAssemblyStep_);
    writePod(output, predictionOriginStep_);
    writePod(output, predictionDeadlineStep_);
    writePod(output, primingGain_);

    const std::uint64_t transitionCount = transitions_.size();
    writePod(output, transitionCount);
    for (const auto& transition : transitions_) {
        writePod(output, transition.fromId);
        writePod(output, transition.toId);
        writePod(output, transition.observations);
        writePod(output, transition.meanIntervalMs);
        writePod(output, transition.intervalM2);
        writePod(output, transition.meanReward);
        writePod(output, transition.meanNovelty);
        writePod(output, transition.lastObservedStep);
    }

    const std::uint64_t prototypeCount = prototypes_.size();
    writePod(output, prototypeCount);
    for (const auto& record : prototypes_) {
        writePod(output, record.id);
        writeVector(output, record.prototype);
    }
    writeVector(output, primingPattern_);
    writeVector(output, predictedPrototype_);
}

bool ProspectiveMemory::load(std::istream& input) {
    const auto start = input.tellg();
    std::array<char, 4> magic{};
    input.read(magic.data(), magic.size());
    const std::array<char, 4> expected{'P', 'R', 'O', '1'};
    if (input.gcount() != static_cast<std::streamsize>(magic.size()) || magic != expected) {
        input.clear();
        if (start != std::streampos(-1)) input.seekg(start);
        reset();
        return false;
    }

    readPod(input, metrics_.learnedTransitions);
    readPod(input, metrics_.observedTransitions);
    readPod(input, metrics_.predictionHits);
    readPod(input, metrics_.predictionMisses);
    readPod(input, metrics_.predictedAssemblyId);
    readPod(input, metrics_.lastAssemblyId);
    readPod(input, metrics_.predictionConfidence);
    readPod(input, metrics_.expectedDelayMs);
    readPod(input, metrics_.predictionError);
    readPod(input, metrics_.temporalSurprise);
    readPod(input, metrics_.sequenceFamiliarity);
    readPod(input, metrics_.prospectiveActivation);
    readPod(input, previousAssemblyId_);
    readPod(input, previousAssemblyStep_);
    readPod(input, predictionOriginStep_);
    readPod(input, predictionDeadlineStep_);
    readPod(input, primingGain_);

    std::uint64_t transitionCount = 0;
    readPod(input, transitionCount);
    if (transitionCount > 100000) throw std::runtime_error("Zu viele prospektive Transitionen im Snapshot");
    transitions_.resize(static_cast<std::size_t>(transitionCount));
    for (auto& transition : transitions_) {
        readPod(input, transition.fromId);
        readPod(input, transition.toId);
        readPod(input, transition.observations);
        readPod(input, transition.meanIntervalMs);
        readPod(input, transition.intervalM2);
        readPod(input, transition.meanReward);
        readPod(input, transition.meanNovelty);
        readPod(input, transition.lastObservedStep);
    }

    std::uint64_t prototypeCount = 0;
    readPod(input, prototypeCount);
    if (prototypeCount > 10000) throw std::runtime_error("Zu viele prospektive Prototypen im Snapshot");
    prototypes_.clear();
    prototypes_.reserve(static_cast<std::size_t>(prototypeCount));
    for (std::uint64_t i = 0; i < prototypeCount; ++i) {
        PrototypeRecord record;
        readPod(input, record.id);
        readVector(input, record.prototype, 100000);
        prototypes_.push_back(std::move(record));
    }
    readVector(input, primingPattern_, 100000);
    readVector(input, predictedPrototype_, 100000);

    metrics_.learnedTransitions = transitions_.size();
    if (!std::isfinite(metrics_.predictionConfidence)
        || !std::isfinite(metrics_.predictionError)
        || !std::isfinite(metrics_.temporalSurprise)
        || !std::isfinite(metrics_.sequenceFamiliarity)
        || !std::isfinite(metrics_.prospectiveActivation)) {
        throw std::runtime_error("Prospektiver Snapshot enthaelt nichtendliche Werte");
    }
    return true;
}

}  // namespace tatarus::neuro::temporal
