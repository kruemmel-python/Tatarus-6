#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"
#include "tatarus/organism.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tatarus::cortex {

enum class CounterfactualOrigin : std::uint8_t {
    Simulation,
    Imagination,
};

struct CounterfactualFrame {
    Experience experience;
    organism::AtmosphericEnvironment atmosphere;
    organism::RobotPhysicalLoad load;
    std::optional<ScannerFrame> scanner;
    bool usePhysicalLoad = false;
};

struct CounterfactualScenario {
    std::string id;
    std::string strategyId;
    CounterfactualOrigin origin = CounterfactualOrigin::Imagination;
    ActionId actionId = 0;
    std::vector<CounterfactualFrame> frames;
    std::optional<ActionOutcome> terminalOutcome;
    bool endEpisode = false;
    bool reachedGoal = false;
};

struct CounterfactualStateSummary {
    std::uint64_t organismStep = 0;
    std::uint64_t experiences = 0;
    double brainAtp = 1.0;
    double brainOxygen = 1.0;
    double motorConfidence = 0.0;
    double predictionConfidence = 0.0;
    double predictionError = 0.0;
    double sequenceFamiliarity = 0.0;
    double visceralDistress = 0.0;
    double heartRateBpm = 0.0;
    double mapMmHg = 0.0;
    double oxygenSaturation = 1.0;
    double gfrMlPerMin = 0.0;
};

struct CounterfactualBranchResult {
    std::string id;
    std::string strategyId;
    CounterfactualOrigin origin = CounterfactualOrigin::Imagination;
    bool success = false;
    std::uint64_t stepsExecuted = 0;
    std::uint64_t branchSnapshotHash = 0;
    double cumulativeObservedReward = 0.0;
    double terminalReward = 0.0;
    double terminalSuccess = 0.0;
    double utility = 0.0;
    CounterfactualStateSummary initial;
    CounterfactualStateSummary final;
    std::string error;
};

struct CounterfactualBatchResult {
    std::uint64_t realSnapshotHashBefore = 0;
    std::uint64_t realSnapshotHashAfter = 0;
    bool isolationVerified = false;
    std::vector<CounterfactualBranchResult> branches;
    std::optional<std::string> bestBranchId;
};

class CounterfactualSandbox {
public:
    explicit CounterfactualSandbox(CortexSandboxConfig config = {});

    [[nodiscard]] CounterfactualBatchResult evaluate(
        const organism::SyntheticOrganism& realOrganism,
        const std::vector<CounterfactualScenario>& scenarios) const;

private:
    CortexSandboxConfig config_;
};

[[nodiscard]] const char* toString(CounterfactualOrigin value) noexcept;

} // namespace tatarus::cortex
