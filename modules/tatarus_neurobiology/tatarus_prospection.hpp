#pragma once

#include "tatarus/throughput.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace tatarus::neuro {

struct ProspectiveMetrics {
    std::uint64_t learnedTransitions = 0;
    std::uint64_t observedTransitions = 0;
    std::uint64_t predictionHits = 0;
    std::uint64_t predictionMisses = 0;
    std::uint64_t predictedAssemblyId = 0;
    std::uint64_t lastAssemblyId = 0;
    double predictionConfidence = 0.0;
    double expectedDelayMs = 0.0;
    double predictionError = 0.0;
    double temporalSurprise = 0.0;
    double sequenceFamiliarity = 0.0;
    double prospectiveActivation = 0.0;
};

namespace temporal {

class ProspectiveMemory {
public:
    ProspectiveMemory() = default;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    void reset();
    void tick(std::uint64_t step, double dtMs);
    void observe(
        std::uint64_t assemblyId,
        const std::vector<double>& prototype,
        std::uint64_t step,
        double dtMs,
        double reward,
        double novelty);
    // Read-only use of learned transitions during frozen evaluation. Dynamic
    // prediction state and metrics continue to evolve, but no prototype or
    // transition parameter is written.
    void recall(
        std::uint64_t assemblyId,
        const std::vector<double>& prototype,
        std::uint64_t step,
        double dtMs);

    [[nodiscard]] const ProspectiveMetrics& metrics() const;
    [[nodiscard]] const std::vector<double>& primingPattern() const;
    [[nodiscard]] std::uint64_t stateHash() const;

    void save(std::ostream& output) const;
    [[nodiscard]] bool load(std::istream& input);

private:
    struct PrototypeRecord {
        std::uint64_t id = 0;
        std::vector<double> prototype;
    };

    struct Transition {
        std::uint64_t fromId = 0;
        std::uint64_t toId = 0;
        std::uint64_t observations = 0;
        double meanIntervalMs = 0.0;
        double intervalM2 = 0.0;
        double meanReward = 0.0;
        double meanNovelty = 0.0;
        std::uint64_t lastObservedStep = 0;
    };

    std::vector<Transition> transitions_;
    std::vector<PrototypeRecord> prototypes_;
    std::vector<double> primingPattern_;
    std::vector<double> predictedPrototype_;
    ProspectiveMetrics metrics_;

    std::uint64_t previousAssemblyId_ = 0;
    std::uint64_t previousAssemblyStep_ = 0;
    std::uint64_t predictionOriginStep_ = 0;
    std::uint64_t predictionDeadlineStep_ = 0;
    double primingGain_ = 0.0;
    tatarus::ThroughputCounters* throughput_ = nullptr;

    void clearPendingPrediction();
    void evaluatePendingPrediction(
        std::uint64_t actualAssemblyId,
        const std::vector<double>& actualPrototype,
        std::uint64_t step,
        double dtMs);
    void learnTransition(
        std::uint64_t fromId,
        std::uint64_t toId,
        std::uint64_t fromStep,
        std::uint64_t toStep,
        double dtMs,
        double reward,
        double novelty);
    void predictFrom(
        std::uint64_t assemblyId,
        std::uint64_t step,
        double dtMs);
    [[nodiscard]] static double cosineSimilarity(
        const std::vector<double>& left,
        const std::vector<double>& right);
    [[nodiscard]] double transitionIntervalStdDev(const Transition& transition) const;
};

}  // namespace temporal
}  // namespace tatarus::neuro
