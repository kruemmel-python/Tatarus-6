#include "tatarus/counterfactual.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace tatarus::cortex {
namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix) {
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path()
            / (std::string(prefix) + "_" + std::to_string(stamp)
               + "_" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::uint64_t fnvByte(std::uint64_t hash, unsigned char byte) noexcept {
    hash ^= static_cast<std::uint64_t>(byte);
    return hash * 1099511628211ULL;
}

[[nodiscard]] std::uint64_t snapshotHash(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [&root](const auto& left, const auto& right) {
        return std::filesystem::relative(left, root).generic_string()
            < std::filesystem::relative(right, root).generic_string();
    });

    std::uint64_t hash = 1469598103934665603ULL;
    std::array<char, 8192> buffer{};
    for (const auto& file : files) {
        const auto relative = std::filesystem::relative(file, root).generic_string();
        for (const unsigned char byte : relative) hash = fnvByte(hash, byte);
        hash = fnvByte(hash, 0xffU);

        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("cannot hash counterfactual snapshot file");
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            for (std::streamsize index = 0; index < count; ++index) {
                hash = fnvByte(hash, static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]));
            }
        }
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] CounterfactualStateSummary summarize(
    const organism::SyntheticOrganism& organism) {
    CounterfactualStateSummary result;
    const auto body = organism.telemetry();
    const auto mind = organism.mind().context();
    result.organismStep = body.stepCount;
    result.experiences = mind.metrics.experiences;
    result.brainAtp = std::clamp(mind.physiology.atp, 0.0, 1.0);
    result.brainOxygen = std::clamp(mind.biology.oxygen, 0.0, 1.0);
    result.motorConfidence = std::clamp(mind.motor.confidence, 0.0, 1.0);
    result.predictionConfidence = std::clamp(mind.prediction.confidence, 0.0, 1.0);
    result.predictionError = std::clamp(std::abs(mind.predictionError), 0.0, 1.0);
    result.sequenceFamiliarity = std::clamp(mind.prospection.sequenceFamiliarity, 0.0, 1.0);
    result.visceralDistress = std::clamp(body.interoception.visceralDistress, 0.0, 1.0);
    result.heartRateBpm = body.heart.heartRateBpm;
    result.mapMmHg = body.circulation.meanArterialPressureMmHg;
    result.oxygenSaturation = std::clamp(body.circulation.arterialOxygenSaturation, 0.0, 1.0);
    result.gfrMlPerMin = body.kidney.glomerularFiltrationRateMlPerMin;
    return result;
}

[[nodiscard]] double clampSigned(double value) noexcept {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, -1.0, 1.0);
}

[[nodiscard]] double branchUtility(const CounterfactualBranchResult& result) noexcept {
    const double meanObservedReward = result.stepsExecuted > 0U
        ? result.cumulativeObservedReward / static_cast<double>(result.stepsExecuted)
        : 0.0;
    const double atpDelta = result.final.brainAtp - result.initial.brainAtp;
    const double oxygenDelta = result.final.brainOxygen - result.initial.brainOxygen;
    const double distressDelta = result.final.visceralDistress - result.initial.visceralDistress;
    const double predictionDelta = result.final.predictionConfidence - result.initial.predictionConfidence;

    // This is strictly sandbox ranking telemetry. It is never fed into TATARUS
    // as reward and therefore cannot directly modify the real organism.
    return clampSigned(
        0.30 * clampSigned(meanObservedReward)
        + 0.25 * std::clamp(result.terminalSuccess, 0.0, 1.0)
        + 0.15 * clampSigned(result.terminalReward)
        + 0.10 * clampSigned(atpDelta)
        + 0.05 * clampSigned(oxygenDelta)
        - 0.10 * clampSigned(distressDelta)
        + 0.05 * clampSigned(predictionDelta));
}

void validateScenario(const CounterfactualScenario& scenario) {
    if (scenario.id.empty()) throw std::invalid_argument("counterfactual scenario id must not be empty");
    if (scenario.frames.empty()) throw std::invalid_argument("counterfactual scenario must contain at least one frame");
    if (scenario.actionId != 0U) {
        if (!scenario.terminalOutcome.has_value()) {
            throw std::invalid_argument("counterfactual action scenario requires an explicit terminal outcome");
        }
        if (scenario.terminalOutcome->id != scenario.actionId) {
            throw std::invalid_argument("counterfactual terminal outcome id does not match action id");
        }
    }
}

} // namespace

const char* toString(CounterfactualOrigin value) noexcept {
    switch (value) {
        case CounterfactualOrigin::Simulation: return "SIMULATION";
        case CounterfactualOrigin::Imagination: return "IMAGINATION";
    }
    return "IMAGINATION";
}

CounterfactualSandbox::CounterfactualSandbox(CortexSandboxConfig config)
    : config_(config) {
    if (config_.maximumBranches == 0U || config_.maximumBranches > 64U) {
        throw std::invalid_argument("counterfactual maximumBranches must be in [1,64]");
    }
}

CounterfactualBatchResult CounterfactualSandbox::evaluate(
    const organism::SyntheticOrganism& realOrganism,
    const std::vector<CounterfactualScenario>& scenarios) const {
    if (scenarios.empty()) throw std::invalid_argument("counterfactual sandbox requires at least one scenario");
    if (scenarios.size() > config_.maximumBranches) {
        throw std::invalid_argument("counterfactual scenario count exceeds configured maximumBranches");
    }
    for (const auto& scenario : scenarios) validateScenario(scenario);

    TemporaryDirectory workspace("tatarus_counterfactual");
    const auto source = workspace.path() / "source";
    const auto after = workspace.path() / "real_after";
    realOrganism.saveSnapshot(source);

    CounterfactualBatchResult batch;
    batch.realSnapshotHashBefore = snapshotHash(source);
    batch.branches.reserve(scenarios.size());

    double bestUtility = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
        const auto& scenario = scenarios[index];
        CounterfactualBranchResult result;
        result.id = scenario.id;
        result.strategyId = scenario.strategyId;
        result.origin = scenario.origin;

        try {
            organism::SyntheticOrganism branch(realOrganism.config());
            if (!branch.loadSnapshot(source)) {
                throw std::runtime_error("failed to restore isolated organism branch");
            }
            result.initial = summarize(branch);

            if (scenario.actionId != 0U) {
                const auto started = scenario.frames.front().experience.timestampNs;
                branch.mind().beginAction(ActionEvent{
                    .id = scenario.actionId,
                    .label = "counterfactual:" + scenario.strategyId,
                    .intensity = 1.0,
                    .startedNs = started,
                    .endedNs = 0,
                });
            }

            for (const auto& frame : scenario.frames) {
                if (frame.scanner.has_value()) {
                    if (frame.usePhysicalLoad) {
                        (void)branch.stepExplorerWithLoad(
                            frame.experience, *frame.scanner, frame.atmosphere, frame.load);
                    } else {
                        (void)branch.stepExplorer(
                            frame.experience, *frame.scanner, frame.atmosphere);
                    }
                } else if (frame.usePhysicalLoad) {
                    (void)branch.stepWithLoad(frame.experience, frame.atmosphere, frame.load);
                } else {
                    (void)branch.step(frame.experience, frame.atmosphere);
                }
                result.cumulativeObservedReward += frame.experience.reward;
                ++result.stepsExecuted;
            }

            if (scenario.terminalOutcome.has_value()) {
                result.terminalReward = scenario.terminalOutcome->reward;
                result.terminalSuccess = scenario.terminalOutcome->success;
                branch.mind().endAction(*scenario.terminalOutcome);
            }
            if (scenario.endEpisode) branch.mind().endEpisode(scenario.reachedGoal);

            result.final = summarize(branch);
            result.utility = branchUtility(result);

            const auto branchPath = workspace.path() / ("branch_" + std::to_string(index));
            branch.saveSnapshot(branchPath);
            result.branchSnapshotHash = snapshotHash(branchPath);
            result.success = true;

            if (result.utility > bestUtility) {
                bestUtility = result.utility;
                batch.bestBranchId = result.id;
            }
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        batch.branches.push_back(std::move(result));
    }

    realOrganism.saveSnapshot(after);
    batch.realSnapshotHashAfter = snapshotHash(after);
    batch.isolationVerified = batch.realSnapshotHashBefore == batch.realSnapshotHashAfter;
    if (!batch.isolationVerified) {
        throw std::runtime_error("counterfactual sandbox detected mutation leakage into the real organism");
    }
    return batch;
}

} // namespace tatarus::cortex
