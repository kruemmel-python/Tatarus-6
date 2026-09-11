#include "tatarus_identity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>

namespace tatarus::neuro::identity {
namespace {

constexpr std::array<char, 8> kMagic{'T', 'P', 'M', '2', '4', 'V', '1', '\0'};
constexpr std::size_t kFeatureCount = 40;
constexpr double kCrossAssemblyAppearance = 0.82;
constexpr double kMinimumAssemblyAppearance = 0.55;
constexpr double kHypothesisConsistency = 0.78;
constexpr double kHypothesisLifetimeSeconds = 180.0;
constexpr int kNewIdentityEvidenceRequired = 3;

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double cosineRange(
    const std::vector<double>& left,
    const std::vector<double>& right,
    std::size_t begin,
    std::size_t end) {
    if (left.size() < end || right.size() < end || begin >= end) return 0.0;
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        dot += left[index] * right[index];
        leftNorm += left[index] * left[index];
        rightNorm += right[index] * right[index];
    }
    if (leftNorm < 1e-10 || rightNorm < 1e-10) return 0.0;
    const double cosine = dot / std::sqrt(leftNorm * rightNorm);
    return clamp01(0.5 * (cosine + 1.0));
}

double appearanceSimilarity(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    return 0.35 * cosineRange(left, right, 0, 8)
        + 0.30 * cosineRange(left, right, 8, 32)
        + 0.35 * cosineRange(left, right, 32, 40);
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) throw std::runtime_error("TATARUS-Identity-Zustand konnte nicht geschrieben werden");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) throw std::runtime_error("TATARUS-Identity-Zustand ist abgeschnitten");
}

template <class T>
void writeVector(std::ostream& output, const std::vector<T>& values) {
    const std::uint64_t count = values.size();
    writePod(output, count);
    if (!values.empty()) {
        output.write(
            reinterpret_cast<const char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
    if (!output) throw std::runtime_error("TATARUS-Identity-Vektor konnte nicht geschrieben werden");
}

template <class T>
void readVector(std::istream& input, std::vector<T>& values) {
    std::uint64_t count = 0;
    readPod(input, count);
    if (count > 1'000'000ULL) throw std::runtime_error("TATARUS-Identity-Vektor ist unplausibel gross");
    values.resize(static_cast<std::size_t>(count));
    if (!values.empty()) {
        input.read(
            reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(values.size() * sizeof(T)));
    }
    if (!input) throw std::runtime_error("TATARUS-Identity-Vektor ist abgeschnitten");
}

}  // namespace

PersonMemory::PersonMemory(std::uint64_t seed) : seed_(seed) {
    createSystem();
}

void PersonMemory::createSystem() {
    NervousSystemConfig config;
    config.seed = seed_;
    config.sensoryNeurons = 64;
    config.excitatoryNeurons = 64;
    config.inhibitoryNeurons = 16;
    config.contextNeurons = 32;
    config.motorNeurons = 8;
    config.modulatoryNeurons = 4;
    config.maximumAssemblies = 256;
    config.assemblySimilarityThreshold = 0.70;
    config.eligibilityTauMs = 60'000.0;
    config.dopamineTauMs = 5'000.0;
    config.structuralIntervalMs = 2'000.0;
    nervousSystem_ = std::make_unique<PersistentNervousSystem>(config);
}

void PersonMemory::reset() {
    engrams_.clear();
    hypotheses_.clear();
    nextIdentityId_ = 1;
    nextHypothesisId_ = 1;
    simulatedUntilSeconds_ = 0.0;
    lastDecisionIdentity_ = 0;
    lastDecisionAssembly_ = 0;
    lastHypothesisId_ = 0;
    lastCreatedIdentity_ = 0;
    lastDecisionKind_ = DecisionKind::Uncertain;
    lastObservation_ = {};
    createSystem();
}

void PersonMemory::advanceIdle(double timestampSeconds) {
    if (!std::isfinite(timestampSeconds) || timestampSeconds < 0.0) {
        throw std::invalid_argument("Ungueltiger Beobachtungszeitpunkt");
    }
    if (simulatedUntilSeconds_ <= 0.0) {
        simulatedUntilSeconds_ = timestampSeconds;
        return;
    }
    const double elapsed = std::clamp(
        timestampSeconds - simulatedUntilSeconds_,
        0.0,
        120.0);
    const int steps = static_cast<int>(std::llround(
        elapsed * 1000.0 / nervousSystem_->config().dtMs));
    SensorFrame empty;
    for (int step = 0; step < steps; ++step) nervousSystem_->step(empty);
    simulatedUntilSeconds_ = timestampSeconds;
}

std::uint64_t PersonMemory::stimulate(const PersonObservation& observation) {
    SensorFrame frame;
    frame.visionEvents.reserve(observation.features.size());
    for (const double value : observation.features) {
        const double bounded = std::clamp(value, -1.0, 1.0);
        frame.visionEvents.push_back(std::round(bounded * 20.0) / 20.0);
    }
    frame.contextEvents = {
        observation.cameraId == 0 ? -1.0 : 1.0,
        std::clamp(observation.quality, 0.0, 1.0)};
    frame.novelty = 0.35 * (1.0 - std::clamp(observation.quality, 0.0, 1.0));

    std::uint64_t activeAssemblyId = 0;
    for (int microstep = 0; microstep < 24; ++microstep) {
        nervousSystem_->step(frame);
        const auto state = nervousSystem_->inspect();
        if (state.activeAssembly >= 0
            && static_cast<std::size_t>(state.activeAssembly) < state.assemblies.size()) {
            activeAssemblyId = state.assemblies[
                static_cast<std::size_t>(state.activeAssembly)].id;
        }
    }
    simulatedUntilSeconds_ += 0.024;
    return activeAssemblyId;
}

CandidateScore PersonMemory::score(
    const Engram& engram,
    const PersonObservation& observation,
    std::uint64_t assemblyId) const {
    CandidateScore result;
    result.identityId = engram.id;
    result.assembly = assemblyId != 0
        && std::find(engram.assemblies.begin(), engram.assemblies.end(), assemblyId)
            != engram.assemblies.end()
        ? 1.0
        : 0.0;

    result.appearance = appearanceSimilarity(observation.features, engram.mean);

    const double elapsed = std::max(0.0, observation.timestampSeconds - engram.lastSeenSeconds);
    if (observation.cameraId != engram.lastCamera) {
        result.transition = std::exp(-std::abs(elapsed - 40.0) / 35.0);
    } else {
        result.transition = 0.65 * std::exp(-elapsed / 20.0);
    }
    result.recency = std::exp(-elapsed / 120.0);

    const double quality = 0.55 + 0.45 * std::clamp(observation.quality, 0.0, 1.0);
    result.total = clamp01(
        0.45 * result.assembly
        + quality * 0.27 * result.appearance
        + 0.20 * result.transition
        + 0.08 * result.recency);
    return result;
}

PersonMemory::Hypothesis& PersonMemory::updateHypothesis(
    Hypothesis& hypothesis,
    const PersonObservation& observation) {
    if (hypothesis.mean.empty()) {
        hypothesis.mean = observation.features;
        hypothesis.observations = 1;
    } else {
        ++hypothesis.observations;
        const double learning = 1.0 / static_cast<double>(hypothesis.observations);
        for (std::size_t index = 0; index < hypothesis.mean.size(); ++index) {
            hypothesis.mean[index] += learning
                * (observation.features[index] - hypothesis.mean[index]);
        }
    }
    hypothesis.lastCamera = observation.cameraId;
    hypothesis.lastSeenSeconds = observation.timestampSeconds;
    return hypothesis;
}

PersonMemory::Hypothesis& PersonMemory::createHypothesis(
    const PersonObservation& observation,
    std::uint64_t assemblyId,
    std::uint64_t knownIdentityId) {
    Hypothesis hypothesis;
    hypothesis.id = nextHypothesisId_++;
    hypothesis.knownIdentityId = knownIdentityId;
    hypothesis.proposedIdentityId = knownIdentityId;
    if (knownIdentityId == 0) {
        hypothesis.proposedIdentityId = nextIdentityId_;
        for (const auto& pending : hypotheses_) {
            if (pending.knownIdentityId == 0) {
                hypothesis.proposedIdentityId = std::max(
                    hypothesis.proposedIdentityId,
                    pending.proposedIdentityId + 1);
            }
        }
    }
    hypothesis.assemblyId = assemblyId;
    hypotheses_.push_back(std::move(hypothesis));
    return updateHypothesis(hypotheses_.back(), observation);
}

PersonMemory::Engram& PersonMemory::promoteHypothesis(Hypothesis& hypothesis) {
    Engram engram;
    engram.id = hypothesis.proposedIdentityId;
    if (std::any_of(engrams_.begin(), engrams_.end(),
            [&](const Engram& existing) { return existing.id == engram.id; })) {
        engram.id = nextIdentityId_;
    }
    nextIdentityId_ = std::max(nextIdentityId_, engram.id + 1);
    if (hypothesis.assemblyId != 0) engram.assemblies.push_back(hypothesis.assemblyId);
    engram.mean = hypothesis.mean;
    engram.m2.assign(hypothesis.mean.size(), 0.0);
    engram.observations = hypothesis.observations;
    engram.lastCamera = hypothesis.lastCamera;
    engram.lastSeenSeconds = hypothesis.lastSeenSeconds;
    engrams_.push_back(std::move(engram));
    return engrams_.back();
}

void PersonMemory::updateEngram(
    Engram& engram,
    const PersonObservation& observation,
    std::uint64_t assemblyId) {
    if (assemblyId != 0
        && std::find(engram.assemblies.begin(), engram.assemblies.end(), assemblyId)
            == engram.assemblies.end()) {
        engram.assemblies.push_back(assemblyId);
    }
    if (engram.mean.empty()) {
        engram.mean = observation.features;
        engram.m2.assign(observation.features.size(), 0.0);
        engram.observations = 1;
    } else {
        ++engram.observations;
        const double learning = std::clamp(observation.quality, 0.1, 1.0)
            / static_cast<double>(std::min<std::uint64_t>(engram.observations, 20));
        for (std::size_t index = 0; index < engram.mean.size(); ++index) {
            const double delta = observation.features[index] - engram.mean[index];
            engram.mean[index] += learning * delta;
            engram.m2[index] += delta * (observation.features[index] - engram.mean[index]);
        }
    }
    engram.lastCamera = observation.cameraId;
    engram.lastSeenSeconds = observation.timestampSeconds;
}

PersonMemory::Engram& PersonMemory::createEngram(
    const PersonObservation& observation,
    std::uint64_t assemblyId) {
    Engram engram;
    engram.id = nextIdentityId_++;
    engrams_.push_back(std::move(engram));
    updateEngram(engrams_.back(), observation, assemblyId);
    return engrams_.back();
}

AssociationDecision PersonMemory::observe(const PersonObservation& observation) {
    if (observation.features.size() != kFeatureCount) {
        throw std::invalid_argument("TATARUS Identity erwartet genau 40 Merkmale");
    }
    if (!std::all_of(observation.features.begin(), observation.features.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("Nichtendliches Personenmerkmal");
    }

    advanceIdle(observation.timestampSeconds);
    const std::uint64_t assemblyId = stimulate(observation);
    std::erase_if(hypotheses_, [&](const Hypothesis& hypothesis) {
        return observation.timestampSeconds - hypothesis.lastSeenSeconds
            > kHypothesisLifetimeSeconds;
    });

    AssociationDecision decision;
    decision.activeAssemblyId = assemblyId;

    for (const auto& engram : engrams_) {
        decision.candidates.push_back(score(engram, observation, assemblyId));
    }
    std::sort(decision.candidates.begin(), decision.candidates.end(),
        [](const auto& left, const auto& right) { return left.total > right.total; });

    for (const auto& engram : engrams_) {
        const double baseline = appearanceSimilarity(observation.features, engram.mean);
        if (baseline > decision.baselineScore) {
            decision.baselineScore = baseline;
            decision.baselineIdentityId = engram.id;
        }
    }
    decision.baselineAccepted = decision.baselineScore >= 0.82;

    auto assemblyCandidate = std::find_if(
        decision.candidates.begin(), decision.candidates.end(),
        [](const CandidateScore& candidate) { return candidate.assembly >= 1.0; });

    lastCreatedIdentity_ = 0;
    lastHypothesisId_ = 0;
    if (assemblyCandidate != decision.candidates.end()
        && assemblyCandidate->appearance >= kMinimumAssemblyAppearance) {
        const double second = decision.candidates.size() > 1
            ? (decision.candidates.front().identityId == assemblyCandidate->identityId
                ? decision.candidates[1].total
                : decision.candidates.front().total)
            : 0.0;
        const double margin = std::max(0.0, assemblyCandidate->total - second);
        decision.kind = DecisionKind::MatchedIdentity;
        decision.identityId = assemblyCandidate->identityId;
        decision.confidence = clamp01(
            0.65 * assemblyCandidate->total
            + 0.35 * std::min(1.0, margin / 0.25));
        auto iterator = std::find_if(engrams_.begin(), engrams_.end(),
            [&](const Engram& engram) { return engram.id == decision.identityId; });
        if (iterator != engrams_.end()) updateEngram(*iterator, observation, assemblyId);
    } else if (decision.baselineIdentityId != 0
        && decision.baselineScore >= kCrossAssemblyAppearance) {
        auto hypothesis = std::find_if(hypotheses_.begin(), hypotheses_.end(),
            [&](const Hypothesis& pending) {
                return pending.knownIdentityId == decision.baselineIdentityId
                    && pending.assemblyId == assemblyId;
            });
        if (hypothesis == hypotheses_.end()) {
            createHypothesis(
                observation, assemblyId, decision.baselineIdentityId);
            hypothesis = std::prev(hypotheses_.end());
        } else {
            updateHypothesis(*hypothesis, observation);
        }
        decision.kind = DecisionKind::ProvisionalAssociation;
        decision.identityId = decision.baselineIdentityId;
        decision.confidence = clamp01(decision.baselineScore);
        decision.hypothesisEvidence = static_cast<int>(hypothesis->observations);
        decision.evidenceRequired = 1;
        lastHypothesisId_ = hypothesis->id;
    } else {
        Hypothesis* bestHypothesis = nullptr;
        double bestConsistency = -1.0;
        for (auto& hypothesis : hypotheses_) {
            if (hypothesis.knownIdentityId != 0) continue;
            const double consistency = appearanceSimilarity(
                observation.features, hypothesis.mean);
            const bool sameAssembly = assemblyId != 0
                && hypothesis.assemblyId == assemblyId;
            if ((consistency >= 0.86
                    || (sameAssembly && consistency >= kHypothesisConsistency))
                && consistency > bestConsistency) {
                bestConsistency = consistency;
                bestHypothesis = &hypothesis;
            }
        }
        if (bestHypothesis == nullptr) {
            bestHypothesis = &createHypothesis(observation, assemblyId, 0);
        } else {
            updateHypothesis(*bestHypothesis, observation);
        }

        decision.identityId = bestHypothesis->proposedIdentityId;
        decision.hypothesisEvidence = static_cast<int>(bestHypothesis->observations);
        decision.evidenceRequired = kNewIdentityEvidenceRequired;
        lastHypothesisId_ = bestHypothesis->id;
        if (bestHypothesis->observations
            >= static_cast<std::uint64_t>(kNewIdentityEvidenceRequired)) {
            const std::uint64_t hypothesisId = bestHypothesis->id;
            auto& created = promoteHypothesis(*bestHypothesis);
            decision.kind = DecisionKind::NewIdentity;
            decision.identityId = created.id;
            decision.confidence = clamp01(
                0.70 + 0.10 * (bestHypothesis->observations
                    - kNewIdentityEvidenceRequired));
            lastCreatedIdentity_ = created.id;
            std::erase_if(hypotheses_, [&](const Hypothesis& hypothesis) {
                return hypothesis.id == hypothesisId;
            });
            lastHypothesisId_ = 0;
        } else {
            decision.kind = DecisionKind::ProvisionalIdentity;
            decision.confidence = clamp01(
                0.25 + 0.20 * bestHypothesis->observations);
        }
    }

    SensorFrame consequence;
    consequence.reward = decision.kind == DecisionKind::MatchedIdentity
        ? 0.65
        : (decision.kind == DecisionKind::NewIdentity ? 0.35 : 0.10);
    nervousSystem_->step(consequence);
    lastDecisionIdentity_ = decision.identityId;
    lastDecisionAssembly_ = assemblyId;
    lastDecisionKind_ = decision.kind;
    lastObservation_ = observation;
    return decision;
}

void PersonMemory::confirmLast(bool correct) {
    auto pending = std::find_if(hypotheses_.begin(), hypotheses_.end(),
        [&](const Hypothesis& hypothesis) { return hypothesis.id == lastHypothesisId_; });

    if (correct && pending != hypotheses_.end()) {
        const std::uint64_t hypothesisId = pending->id;
        if (lastDecisionKind_ == DecisionKind::ProvisionalAssociation) {
            auto engram = std::find_if(engrams_.begin(), engrams_.end(),
                [&](const Engram& value) { return value.id == pending->knownIdentityId; });
            if (engram != engrams_.end()) {
                updateEngram(*engram, lastObservation_, lastDecisionAssembly_);
                lastDecisionIdentity_ = engram->id;
                lastDecisionKind_ = DecisionKind::MatchedIdentity;
            }
        } else if (lastDecisionKind_ == DecisionKind::ProvisionalIdentity) {
            auto& created = promoteHypothesis(*pending);
            lastDecisionIdentity_ = created.id;
            lastCreatedIdentity_ = created.id;
            lastDecisionKind_ = DecisionKind::NewIdentity;
        }
        std::erase_if(hypotheses_, [&](const Hypothesis& hypothesis) {
            return hypothesis.id == hypothesisId;
        });
        lastHypothesisId_ = 0;
    } else if (!correct) {
        if (pending != hypotheses_.end()) {
            const std::uint64_t hypothesisId = pending->id;
            std::erase_if(hypotheses_, [&](const Hypothesis& hypothesis) {
                return hypothesis.id == hypothesisId;
            });
            lastHypothesisId_ = 0;
        } else if (lastDecisionKind_ == DecisionKind::MatchedIdentity) {
            auto engram = std::find_if(engrams_.begin(), engrams_.end(),
                [&](const Engram& value) { return value.id == lastDecisionIdentity_; });
            if (engram != engrams_.end() && lastDecisionAssembly_ != 0) {
                std::erase(engram->assemblies, lastDecisionAssembly_);
            }
        } else if (lastDecisionKind_ == DecisionKind::NewIdentity
            && lastCreatedIdentity_ != 0) {
            std::erase_if(engrams_, [&](const Engram& engram) {
                return engram.id == lastCreatedIdentity_;
            });
            lastCreatedIdentity_ = 0;
        }
    }

    SensorFrame consequence;
    consequence.reward = correct ? 1.0 : -1.0;
    consequence.novelty = correct ? 0.0 : 0.8;
    for (int step = 0; step < 40; ++step) nervousSystem_->step(consequence);
}

void PersonMemory::save(const std::filesystem::path& basePath) const {
    nervousSystem_->saveSnapshot(basePath.string() + ".tns");
    std::ofstream output(basePath.string() + ".pms", std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Personengedachtnis konnte nicht gespeichert werden");
    output.write(kMagic.data(), kMagic.size());
    writePod(output, seed_);
    writePod(output, nextIdentityId_);
    writePod(output, simulatedUntilSeconds_);
    const std::uint64_t count = engrams_.size();
    writePod(output, count);
    for (const auto& engram : engrams_) {
        writePod(output, engram.id);
        writePod(output, engram.observations);
        writePod(output, engram.lastCamera);
        writePod(output, engram.lastSeenSeconds);
        writeVector(output, engram.assemblies);
        writeVector(output, engram.mean);
        writeVector(output, engram.m2);
    }
}

bool PersonMemory::load(const std::filesystem::path& basePath) {
    const auto memoryPath = std::filesystem::path(basePath.string() + ".pms");
    const auto nervousPath = std::filesystem::path(basePath.string() + ".tns");
    if (!std::filesystem::exists(memoryPath) || !std::filesystem::exists(nervousPath)) return false;
    std::ifstream input(memoryPath, std::ios::binary);
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    if (magic != kMagic) throw std::runtime_error("Unbekanntes TATARUS-Identity-Speicherformat");
    std::uint64_t storedSeed = 0;
    readPod(input, storedSeed);
    if (storedSeed != seed_) throw std::runtime_error("TATARUS-Identity-Snapshot verwendet einen anderen Seed");
    readPod(input, nextIdentityId_);
    readPod(input, simulatedUntilSeconds_);
    std::uint64_t count = 0;
    readPod(input, count);
    if (count > 100'000ULL) throw std::runtime_error("Zu viele Personen im Snapshot");
    std::vector<Engram> loaded(static_cast<std::size_t>(count));
    for (auto& engram : loaded) {
        readPod(input, engram.id);
        readPod(input, engram.observations);
        readPod(input, engram.lastCamera);
        readPod(input, engram.lastSeenSeconds);
        readVector(input, engram.assemblies);
        readVector(input, engram.mean);
        readVector(input, engram.m2);
        if (engram.mean.size() != kFeatureCount || engram.m2.size() != kFeatureCount) {
            throw std::runtime_error("TATARUS-Identity-Merkmalszahl passt nicht");
        }
    }
    nervousSystem_->loadSnapshot(nervousPath);
    engrams_ = std::move(loaded);
    hypotheses_.clear();
    nextHypothesisId_ = 1;
    lastHypothesisId_ = 0;
    lastCreatedIdentity_ = 0;
    lastDecisionKind_ = DecisionKind::Uncertain;
    return true;
}

std::size_t PersonMemory::identityCount() const {
    return engrams_.size();
}

std::size_t PersonMemory::pendingHypothesisCount() const {
    return hypotheses_.size();
}

std::size_t PersonMemory::linkedAssemblyCount(std::uint64_t identityId) const {
    const auto engram = std::find_if(engrams_.begin(), engrams_.end(),
        [&](const Engram& value) { return value.id == identityId; });
    return engram == engrams_.end() ? 0 : engram->assemblies.size();
}

const NervousSystemMetrics& PersonMemory::metrics() const {
    return nervousSystem_->metrics();
}

std::string decisionName(DecisionKind kind) {
    switch (kind) {
        case DecisionKind::NewIdentity: return "NEUE IDENTITAET";
        case DecisionKind::MatchedIdentity: return "BEKANNTE IDENTITAET";
        case DecisionKind::ProvisionalAssociation: return "ASSEMBLY-HYPOTHESE";
        case DecisionKind::ProvisionalIdentity: return "IDENTITAETS-HYPOTHESE";
        case DecisionKind::Uncertain: return "UNSICHER";
    }
    return "UNBEKANNT";
}

}  // namespace tatarus::neuro::identity
