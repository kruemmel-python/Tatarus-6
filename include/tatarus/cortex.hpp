#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"
#include "tatarus/cortex_learning.hpp"
#include "tatarus/cortex_executive.hpp"
#include "tatarus/cortex_metacognition.hpp"
#include "tatarus/counterfactual.hpp"
#include "tatarus/robot_mind.hpp"
#include "tatarus/organism_types.hpp"
#include "tatarus/rover_cortex.hpp"
#include "tatarus/topdown_cortex.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tatarus::cortex {

class ICortexModelClient {
public:
    virtual ~ICortexModelClient() = default;
    [[nodiscard]] virtual CortexResponse complete(const CortexRequest& request) = 0;
};

class LmStudioClient final : public ICortexModelClient {
public:
    explicit LmStudioClient(
        LmStudioConfig config = {}, CortexLimits limits = {});
    ~LmStudioClient() override;

    LmStudioClient(const LmStudioClient&) = delete;
    LmStudioClient& operator=(const LmStudioClient&) = delete;
    LmStudioClient(LmStudioClient&&) noexcept;
    LmStudioClient& operator=(LmStudioClient&&) noexcept;

    [[nodiscard]] std::vector<std::string> listModels() const;
    [[nodiscard]] std::string resolvedModel() const;
    [[nodiscard]] CortexResponse complete(const CortexRequest& request) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class CortexArbiter {
public:
    explicit CortexArbiter(CortexArbiterConfig config = {});

    [[nodiscard]] CortexDecision evaluate(
        const CortexRequest& request,
        const CortexResponse& response) const;

private:
    CortexArbiterConfig config_;
};

class CortexOrchestrator {
public:
    explicit CortexOrchestrator(CortexConfig config = {});
    CortexOrchestrator(CortexConfig config, std::shared_ptr<ICortexModelClient> client);
    ~CortexOrchestrator();

    CortexOrchestrator(const CortexOrchestrator&) = delete;
    CortexOrchestrator& operator=(const CortexOrchestrator&) = delete;

    void observe(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism = nullptr,
        std::string goal = {});

    void requestExplicitAnalysis(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism = nullptr,
        std::string goal = {});

    void advise(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void requestExplicitAdvice(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void imagine(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void requestExplicitImagination(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void hybrid(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void requestExplicitHybrid(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexSpatialState& spatial,
        const CortexImaginationState& imagination,
        std::vector<std::string> capabilities,
        std::string goal = {});

    void poll(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism = nullptr,
        const CortexSpatialState* spatial = nullptr,
        const CortexImaginationState* imagination = nullptr);

    [[nodiscard]] CortexObservation status() const;
    [[nodiscard]] bool idle() const;

    // Phase 12: persistent executive goals and bounded working memory.
    [[nodiscard]] std::uint64_t pushGoal(std::string text, double priority, std::uint64_t currentStep);
    bool completeGoal(std::uint64_t goalId, bool success, std::uint64_t currentStep);
    bool cancelGoal(std::uint64_t goalId, std::uint64_t currentStep);
    void remember(std::string key, std::string value, double salience, std::uint64_t currentStep, std::uint64_t sourceFingerprint = 0);
    [[nodiscard]] CortexExecutiveSnapshot executiveStatus() const;
    void saveExecutiveState(const std::filesystem::path& path) const;
    void loadExecutiveState(const std::filesystem::path& path);

    // Phase 13: grounded long-horizon plan tracking. Plans never execute motors;
    // only real ActionOutcome values may advance a plan step.
    [[nodiscard]] std::optional<CortexPlanStep> activePlanStep() const;
    void requestExecutivePlan(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism = nullptr,
        const CortexSpatialState* spatial = nullptr,
        const CortexImaginationState* imagination = nullptr,
        std::vector<std::string> capabilities = {});

    // Phase 14: metacognitive reliability of the external Cortex itself.
    [[nodiscard]] CortexMetacognitionSnapshot metacognitionStatus() const;

    // Phase 15: one unified autonomous Cortex cycle. This only schedules
    // cognitive work; host actuation remains outside the Cortex.
    void autonomousCycle(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism = nullptr,
        const CortexSpatialState* spatial = nullptr,
        const CortexImaginationState* imagination = nullptr,
        std::vector<std::string> capabilities = {});

    // Phase 10: prepare/consume a strictly bounded context-only cue. The cue
    // can only be applied to the next real observation and is cleared after use.
    [[nodiscard]] TopDownCuePreparation prepareTopDownCue() const;
    [[nodiscard]] std::optional<ObserveResult> applyTopDownCue(
        RobotMind& mind,
        const Experience& experience,
        const organism::OrganismTelemetry* organism = nullptr,
        const CortexSpatialState* spatial = nullptr,
        const CortexImaginationState* imagination = nullptr);
    [[nodiscard]] std::optional<ExplorerResult> applyTopDownCueToExplorer(
        RobotMind& mind,
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const organism::OrganismTelemetry* organism = nullptr,
        const CortexSpatialState* spatial = nullptr,
        const CortexImaginationState* imagination = nullptr);

    // Phase 11: NREM is Cortex-silent; REM may issue bounded DREAM requests
    // using only IMAGINATIO capabilities.
    void sleepCycle(
        const RobotMind& mind,
        const organism::OrganismTelemetry* organism,
        const CortexImaginationState& imagination,
        std::vector<std::string> imaginationCapabilities,
        std::string goal = {});


    [[nodiscard]] std::optional<CortexTeachingHandle> beginTeachingAction(ActionId actionId);
    [[nodiscard]] std::optional<CortexTeachingHandle> beginAutonomousTeachingAction(ActionId actionId);
    void completeTeachingAction(
        const CortexTeachingHandle& handle,
        const ActionOutcome& outcome,
        const RobotMind& postMind,
        const organism::OrganismTelemetry* postOrganism = nullptr,
        const CortexSpatialState* postSpatial = nullptr,
        const CortexImaginationState* postImagination = nullptr);
    [[nodiscard]] CortexLearningSnapshot learningStatus() const;
    void saveLearningState(const std::filesystem::path& path) const;
    void loadLearningState(const std::filesystem::path& path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] CortexConfig loadCortexConfig(const std::filesystem::path& path);

[[nodiscard]] std::string serializeCortexRequest(const CortexRequest& request);
[[nodiscard]] CortexResponse parseCortexResponse(
    std::string_view json,
    const CortexRequest& request,
    const CortexLimits& limits = {});

} // namespace tatarus::cortex
