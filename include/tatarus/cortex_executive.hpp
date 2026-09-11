#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tatarus::cortex {

struct CortexExecutiveSnapshot {
    std::uint64_t nextGoalId = 1;
    std::vector<CortexGoal> goals;
    std::vector<CortexWorkingMemoryItem> workingMemory;
    std::vector<CortexRecentOutcome> recentRealOutcomes;
    std::optional<CortexPlanProposal> activePlan;
    std::size_t activePlanStep = 0;
};

class CortexExecutive {
public:
    explicit CortexExecutive(CortexExecutiveConfig config = {});

    [[nodiscard]] std::uint64_t pushGoal(
        std::string text,
        double priority,
        std::uint64_t currentStep);
    bool completeGoal(std::uint64_t goalId, bool success, std::uint64_t currentStep);
    bool cancelGoal(std::uint64_t goalId, std::uint64_t currentStep);
    [[nodiscard]] std::optional<CortexGoal> activeGoal() const;

    void remember(
        std::string key,
        std::string value,
        double salience,
        std::uint64_t currentStep,
        std::uint64_t sourceFingerprint = 0);
    void recordRealOutcome(const ActionOutcome& outcome, std::uint64_t currentStep);

    [[nodiscard]] CortexExecutiveState requestState(std::uint64_t currentStep) const;
    [[nodiscard]] std::vector<CortexRecentOutcome> recentOutcomes() const;

    bool adoptPlan(
        const CortexRequest& request,
        const CortexResponse& response,
        const CortexDecision& decision);
    bool completeActivePlanStep(const ActionOutcome& outcome, double successThreshold);
    [[nodiscard]] std::optional<CortexPlanStep> activePlanStep() const;

    [[nodiscard]] CortexExecutiveSnapshot snapshot() const;
    void saveState(const std::filesystem::path& path) const;
    void loadState(const std::filesystem::path& path);

private:
    CortexExecutiveConfig config_;
    CortexExecutiveSnapshot state_;
};

} // namespace tatarus::cortex
