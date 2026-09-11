#pragma once

#include "tatarus_neural_network.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tatarus::neuro::identity {

enum class DecisionKind : std::uint8_t {
    NewIdentity,
    MatchedIdentity,
    ProvisionalAssociation,
    ProvisionalIdentity,
    Uncertain
};

struct PersonObservation {
    std::vector<double> features;
    int cameraId = 0;
    double timestampSeconds = 0.0;
    double quality = 1.0;
};

struct CandidateScore {
    std::uint64_t identityId = 0;
    double total = 0.0;
    double assembly = 0.0;
    double appearance = 0.0;
    double transition = 0.0;
    double recency = 0.0;
};

struct AssociationDecision {
    DecisionKind kind = DecisionKind::Uncertain;
    std::uint64_t identityId = 0;
    std::uint64_t activeAssemblyId = 0;
    double confidence = 0.0;
    std::vector<CandidateScore> candidates;
    std::uint64_t baselineIdentityId = 0;
    double baselineScore = 0.0;
    bool baselineAccepted = false;
    int hypothesisEvidence = 0;
    int evidenceRequired = 0;
};

class PersonMemory {
public:
    explicit PersonMemory(std::uint64_t seed = 24001);

    AssociationDecision observe(const PersonObservation& observation);
    void confirmLast(bool correct);
    void reset();

    void save(const std::filesystem::path& basePath) const;
    bool load(const std::filesystem::path& basePath);

    [[nodiscard]] std::size_t identityCount() const;
    [[nodiscard]] std::size_t pendingHypothesisCount() const;
    [[nodiscard]] std::size_t linkedAssemblyCount(std::uint64_t identityId) const;
    [[nodiscard]] const NervousSystemMetrics& metrics() const;

private:
    struct Engram {
        std::uint64_t id = 0;
        std::vector<std::uint64_t> assemblies;
        std::vector<double> mean;
        std::vector<double> m2;
        std::uint64_t observations = 0;
        int lastCamera = -1;
        double lastSeenSeconds = 0.0;
    };

    struct Hypothesis {
        std::uint64_t id = 0;
        std::uint64_t proposedIdentityId = 0;
        std::uint64_t knownIdentityId = 0;
        std::uint64_t assemblyId = 0;
        std::vector<double> mean;
        std::uint64_t observations = 0;
        int lastCamera = -1;
        double lastSeenSeconds = 0.0;
    };

    std::uint64_t seed_ = 0;
    std::unique_ptr<PersistentNervousSystem> nervousSystem_;
    std::vector<Engram> engrams_;
    std::vector<Hypothesis> hypotheses_;
    std::uint64_t nextIdentityId_ = 1;
    std::uint64_t nextHypothesisId_ = 1;
    double simulatedUntilSeconds_ = 0.0;
    std::uint64_t lastDecisionIdentity_ = 0;
    std::uint64_t lastDecisionAssembly_ = 0;
    std::uint64_t lastHypothesisId_ = 0;
    std::uint64_t lastCreatedIdentity_ = 0;
    DecisionKind lastDecisionKind_ = DecisionKind::Uncertain;
    PersonObservation lastObservation_;

    void createSystem();
    void advanceIdle(double timestampSeconds);
    std::uint64_t stimulate(const PersonObservation& observation);
    CandidateScore score(
        const Engram& engram,
        const PersonObservation& observation,
        std::uint64_t assemblyId) const;
    void updateEngram(
        Engram& engram,
        const PersonObservation& observation,
        std::uint64_t assemblyId);
    Hypothesis& updateHypothesis(
        Hypothesis& hypothesis,
        const PersonObservation& observation);
    Hypothesis& createHypothesis(
        const PersonObservation& observation,
        std::uint64_t assemblyId,
        std::uint64_t knownIdentityId);
    Engram& promoteHypothesis(Hypothesis& hypothesis);
    Engram& createEngram(
        const PersonObservation& observation,
        std::uint64_t assemblyId);
};

std::string decisionName(DecisionKind kind);

}  // namespace tatarus::neuro::identity
