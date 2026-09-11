#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"
#include "tatarus/robot_mind.hpp"
#include "tatarus/organism_types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace tatarus::cortex {

struct CortexTeachingHandle {
    std::uint64_t episodeId = 0;
    std::uint64_t contextClass = 0;
    bool cortexUsed = false;

    [[nodiscard]] explicit operator bool() const noexcept { return episodeId != 0U; }
};

struct CortexTeachingEpisode {
    std::uint64_t episodeId = 0;
    std::uint64_t contextClass = 0;
    std::uint64_t requestId = 0;
    std::uint64_t sourceStep = 0;
    std::uint64_t sourceFingerprint = 0;
    std::uint64_t postFingerprint = 0;
    CortexTriggerReason trigger = CortexTriggerReason::None;
    CortexStrategyKind strategy = CortexStrategyKind::Unknown;
    std::string strategyId;
    ActionId actionId = 0;
    bool cortexUsed = false;
    bool completed = false;
    bool successful = false;
    ActionOutcome outcome;
    double prePredictionConfidence = 0.0;
    double postPredictionConfidence = 0.0;
    double preNovelty = 0.0;
    double postNovelty = 0.0;
};

struct LearnedCortexCompetence {
    std::uint64_t contextClass = 0;
    std::uint64_t consultations = 0;
    std::uint64_t cortexSuccesses = 0;
    std::uint64_t autonomousTrials = 0;
    std::uint64_t autonomousSuccesses = 0;
    double cortexSuccessRate = 0.0;
    double autonomousSuccessRate = 0.0;
    double dependency = 1.0;
    bool autonomyProbeOutstanding = false;
    bool cortexOptional = false;
};

struct CortexLearningSnapshot {
    std::uint64_t episodesStarted = 0;
    std::uint64_t episodesCompleted = 0;
    std::uint64_t cortexAssistedEpisodes = 0;
    std::uint64_t autonomousEpisodes = 0;
    std::uint64_t consultationsSuppressed = 0;
    std::vector<LearnedCortexCompetence> competencies;
};

enum class CortexConsultationDecision : std::uint8_t {
    Consult,
    SuppressForAutonomyProbe,
};

class CortexTeacherTransfer {
public:
    explicit CortexTeacherTransfer(CortexLearningConfig config = {});
    ~CortexTeacherTransfer();

    CortexTeacherTransfer(const CortexTeacherTransfer&) = delete;
    CortexTeacherTransfer& operator=(const CortexTeacherTransfer&) = delete;
    CortexTeacherTransfer(CortexTeacherTransfer&&) noexcept;
    CortexTeacherTransfer& operator=(CortexTeacherTransfer&&) noexcept;

    [[nodiscard]] CortexConsultationDecision consultationDecision(
        const CortexRequest& request);

    [[nodiscard]] CortexTeachingHandle beginCortexAssisted(
        const CortexRequest& request,
        const CortexDecision& decision,
        ActionId actionId);

    [[nodiscard]] CortexTeachingHandle beginAutonomous(
        const CortexRequest& request,
        ActionId actionId);

    void completeRealOutcome(
        const CortexTeachingHandle& handle,
        const ActionOutcome& outcome,
        const RobotMind& postMind,
        const organism::OrganismTelemetry* postOrganism = nullptr,
        const CortexSpatialState* postSpatial = nullptr,
        const CortexImaginationState* postImagination = nullptr);

    [[nodiscard]] CortexLearningSnapshot snapshot() const;
    [[nodiscard]] std::optional<CortexTeachingEpisode> episode(
        std::uint64_t episodeId) const;

    void saveState(const std::filesystem::path& path) const;
    void loadState(const std::filesystem::path& path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::uint64_t cortexContextClass(const CortexRequest& request) noexcept;
[[nodiscard]] const char* toString(CortexConsultationDecision value) noexcept;

} // namespace tatarus::cortex
