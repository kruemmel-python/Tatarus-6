#include "tatarus/cortex_executive.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace tatarus::cortex {
namespace {

constexpr std::array<char, 8> kMagic{'T','C','E','X','1','2','\0','\0'};
constexpr std::uint64_t kMaxSerializedItems = 4096;
constexpr std::uint64_t kMaxStringBytes = 64 * 1024;

template <class T>
void writePod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!out) throw std::runtime_error("failed to write Cortex executive state");
}

template <class T>
void readPod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) throw std::runtime_error("truncated Cortex executive state");
}

void writeString(std::ostream& out, const std::string& value) {
    const std::uint64_t n = value.size();
    if (n > kMaxStringBytes) throw std::runtime_error("Cortex executive string too large");
    writePod(out, n);
    out.write(value.data(), static_cast<std::streamsize>(n));
    if (!out) throw std::runtime_error("failed to write Cortex executive string");
}

std::string readString(std::istream& in) {
    std::uint64_t n = 0;
    readPod(in, n);
    if (n > kMaxStringBytes) throw std::runtime_error("implausible Cortex executive string length");
    std::string value(static_cast<std::size_t>(n), '\0');
    if (n) in.read(value.data(), static_cast<std::streamsize>(n));
    if (!in) throw std::runtime_error("truncated Cortex executive string");
    return value;
}

bool capabilitySubset(
    const std::vector<std::string>& required,
    const std::vector<std::string>& available) {
    const std::unordered_set<std::string> set(available.begin(), available.end());
    return std::all_of(required.begin(), required.end(), [&](const std::string& capability) {
        return set.contains(capability);
    });
}

} // namespace

CortexExecutive::CortexExecutive(CortexExecutiveConfig config) : config_(config) {
    if (config_.maximumGoals == 0U || config_.maximumGoals > 128U) {
        throw std::invalid_argument("executive.maximum_goals must be in [1,128]");
    }
    if (config_.maximumWorkingMemoryItems == 0U || config_.maximumWorkingMemoryItems > 1024U) {
        throw std::invalid_argument("executive.maximum_working_memory_items must be in [1,1024]");
    }
    if (config_.maximumPlanSteps == 0U || config_.maximumPlanSteps > 64U) {
        throw std::invalid_argument("executive.maximum_plan_steps must be in [1,64]");
    }
    if (config_.maximumRecentOutcomes == 0U || config_.maximumRecentOutcomes > 256U) {
        throw std::invalid_argument("executive.maximum_recent_outcomes must be in [1,256]");
    }
}

std::uint64_t CortexExecutive::pushGoal(
    std::string text,
    double priority,
    std::uint64_t currentStep) {
    if (!config_.enabled) return 0;
    if (text.empty() || text.size() > 4096U) {
        throw std::invalid_argument("Cortex goal text must contain 1..4096 bytes");
    }
    if (!std::isfinite(priority) || priority < 0.0 || priority > 1.0) {
        throw std::invalid_argument("Cortex goal priority must be finite and in [0,1]");
    }

    if (state_.goals.size() >= config_.maximumGoals) {
        auto removable = std::min_element(
            state_.goals.begin(), state_.goals.end(),
            [](const CortexGoal& a, const CortexGoal& b) {
                const bool aFinished = a.status == CortexGoalStatus::Completed
                    || a.status == CortexGoalStatus::Failed
                    || a.status == CortexGoalStatus::Cancelled;
                const bool bFinished = b.status == CortexGoalStatus::Completed
                    || b.status == CortexGoalStatus::Failed
                    || b.status == CortexGoalStatus::Cancelled;
                if (aFinished != bFinished) return aFinished;
                if (a.priority != b.priority) return a.priority < b.priority;
                return a.updatedStep < b.updatedStep;
            });
        if (removable != state_.goals.end()) state_.goals.erase(removable);
    }

    CortexGoal goal;
    goal.id = state_.nextGoalId++;
    if (goal.id == 0U) goal.id = state_.nextGoalId++;
    goal.text = std::move(text);
    goal.priority = priority;
    goal.status = CortexGoalStatus::Pending;
    goal.createdStep = currentStep;
    goal.updatedStep = currentStep;
    state_.goals.push_back(goal);
    return goal.id;
}

bool CortexExecutive::completeGoal(
    std::uint64_t goalId,
    bool success,
    std::uint64_t currentStep) {
    for (auto& goal : state_.goals) {
        if (goal.id != goalId) continue;
        goal.status = success ? CortexGoalStatus::Completed : CortexGoalStatus::Failed;
        goal.updatedStep = currentStep;
        if (state_.activePlan.has_value()) {
            state_.activePlan.reset();
            state_.activePlanStep = 0;
        }
        return true;
    }
    return false;
}

bool CortexExecutive::cancelGoal(std::uint64_t goalId, std::uint64_t currentStep) {
    for (auto& goal : state_.goals) {
        if (goal.id != goalId) continue;
        goal.status = CortexGoalStatus::Cancelled;
        goal.updatedStep = currentStep;
        return true;
    }
    return false;
}

std::optional<CortexGoal> CortexExecutive::activeGoal() const {
    const CortexGoal* best = nullptr;
    for (const auto& goal : state_.goals) {
        if (goal.status != CortexGoalStatus::Pending && goal.status != CortexGoalStatus::Active) continue;
        if (!best || goal.priority > best->priority
            || (goal.priority == best->priority && goal.id > best->id)) {
            best = &goal;
        }
    }
    if (!best) return std::nullopt;
    CortexGoal result = *best;
    result.status = CortexGoalStatus::Active;
    return result;
}

void CortexExecutive::remember(
    std::string key,
    std::string value,
    double salience,
    std::uint64_t currentStep,
    std::uint64_t sourceFingerprint) {
    if (!config_.enabled) return;
    if (key.empty() || key.size() > 256U || value.size() > 4096U) {
        throw std::invalid_argument("Cortex working-memory key/value exceeds bounds");
    }
    if (!std::isfinite(salience) || salience < 0.0 || salience > 1.0) {
        throw std::invalid_argument("Cortex working-memory salience must be in [0,1]");
    }

    auto existing = std::find_if(state_.workingMemory.begin(), state_.workingMemory.end(),
        [&](const CortexWorkingMemoryItem& item) { return item.key == key; });
    CortexWorkingMemoryItem item;
    item.key = std::move(key);
    item.value = std::move(value);
    item.salience = salience;
    item.createdStep = currentStep;
    item.expiresStep = config_.workingMemoryTtlSteps == 0U
        ? std::numeric_limits<std::uint64_t>::max()
        : currentStep + config_.workingMemoryTtlSteps;
    item.sourceFingerprint = sourceFingerprint;
    if (existing != state_.workingMemory.end()) *existing = std::move(item);
    else state_.workingMemory.push_back(std::move(item));

    while (state_.workingMemory.size() > config_.maximumWorkingMemoryItems) {
        const auto drop = std::min_element(
            state_.workingMemory.begin(), state_.workingMemory.end(),
            [](const auto& a, const auto& b) {
                if (a.salience != b.salience) return a.salience < b.salience;
                return a.createdStep < b.createdStep;
            });
        state_.workingMemory.erase(drop);
    }
}

void CortexExecutive::recordRealOutcome(const ActionOutcome& outcome, std::uint64_t currentStep) {
    (void)currentStep;
    CortexRecentOutcome recent;
    recent.actionId = outcome.id;
    recent.reward = std::clamp(outcome.reward, -1.0, 1.0);
    recent.success = std::clamp(outcome.success, 0.0, 1.0);
    recent.novelty = std::clamp(outcome.novelty, 0.0, 1.0);
    state_.recentRealOutcomes.push_back(recent);
    if (state_.recentRealOutcomes.size() > config_.maximumRecentOutcomes) {
        const auto excess = state_.recentRealOutcomes.size() - config_.maximumRecentOutcomes;
        state_.recentRealOutcomes.erase(
            state_.recentRealOutcomes.begin(),
            state_.recentRealOutcomes.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}

CortexExecutiveState CortexExecutive::requestState(std::uint64_t currentStep) const {
    CortexExecutiveState out;
    out.available = config_.enabled;
    if (!config_.enabled) return out;
    if (const auto goal = activeGoal()) {
        out.activeGoalId = goal->id;
        out.activeGoal = goal->text;
        out.activeGoalPriority = goal->priority;
    }
    out.goalCount = std::count_if(state_.goals.begin(), state_.goals.end(), [](const CortexGoal& goal) {
        return goal.status == CortexGoalStatus::Pending || goal.status == CortexGoalStatus::Active;
    });
    if (state_.activePlan.has_value()) {
        out.activePlanId = state_.activePlan->id;
        out.activePlanStep = state_.activePlanStep;
        out.planSteps = state_.activePlan->steps;
    }
    for (const auto& item : state_.workingMemory) {
        if (item.expiresStep >= currentStep) out.workingMemory.push_back(item);
    }
    std::sort(out.workingMemory.begin(), out.workingMemory.end(), [](const auto& a, const auto& b) {
        if (a.salience != b.salience) return a.salience > b.salience;
        return a.createdStep > b.createdStep;
    });
    return out;
}

std::vector<CortexRecentOutcome> CortexExecutive::recentOutcomes() const {
    return state_.recentRealOutcomes;
}

bool CortexExecutive::adoptPlan(
    const CortexRequest& request,
    const CortexResponse& response,
    const CortexDecision& decision) {
    if (!config_.enabled || !response.plan.has_value()) return false;
    if (decision.kind != CortexDecisionKind::Accept
        && decision.kind != CortexDecisionKind::SendToImagination) return false;
    CortexPlanProposal plan = *response.plan;
    if (plan.id.empty() || plan.id.size() > 128U
        || plan.steps.empty() || plan.steps.size() > config_.maximumPlanSteps) return false;
    for (const auto& step : plan.steps) {
        if (step.id.empty() || step.id.size() > 96U || step.objective.size() > 1024U
            || step.kind == CortexStrategyKind::Unknown
            || !capabilitySubset(step.requiredCapabilities, request.availableCapabilities)) {
            return false;
        }
    }
    for (auto& step : plan.steps) step.status = CortexPlanStepStatus::Pending;
    plan.steps.front().status = CortexPlanStepStatus::Active;
    state_.activePlan = std::move(plan);
    state_.activePlanStep = 0U;
    return true;
}

bool CortexExecutive::completeActivePlanStep(
    const ActionOutcome& outcome,
    double successThreshold) {
    if (!state_.activePlan.has_value()
        || state_.activePlanStep >= state_.activePlan->steps.size()) return false;
    auto& step = state_.activePlan->steps[state_.activePlanStep];
    const bool success = std::isfinite(outcome.success) && outcome.success >= successThreshold;
    step.status = success ? CortexPlanStepStatus::Completed : CortexPlanStepStatus::Failed;
    if (!success) return false;
    ++state_.activePlanStep;
    if (state_.activePlanStep < state_.activePlan->steps.size()) {
        state_.activePlan->steps[state_.activePlanStep].status = CortexPlanStepStatus::Active;
    }
    return true;
}

std::optional<CortexPlanStep> CortexExecutive::activePlanStep() const {
    if (!state_.activePlan.has_value()
        || state_.activePlanStep >= state_.activePlan->steps.size()) return std::nullopt;
    return state_.activePlan->steps[state_.activePlanStep];
}

CortexExecutiveSnapshot CortexExecutive::snapshot() const {
    return state_;
}

void CortexExecutive::saveState(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create Cortex executive state: " + path.string());
    out.write(kMagic.data(), kMagic.size());
    writePod(out, state_.nextGoalId);

    const std::uint64_t goalCount = state_.goals.size();
    writePod(out, goalCount);
    for (const auto& goal : state_.goals) {
        writePod(out, goal.id); writeString(out, goal.text); writePod(out, goal.priority);
        writePod(out, goal.status); writePod(out, goal.createdStep); writePod(out, goal.updatedStep);
    }

    const std::uint64_t memoryCount = state_.workingMemory.size();
    writePod(out, memoryCount);
    for (const auto& item : state_.workingMemory) {
        writeString(out, item.key); writeString(out, item.value); writePod(out, item.salience);
        writePod(out, item.createdStep); writePod(out, item.expiresStep); writePod(out, item.sourceFingerprint);
    }

    const std::uint64_t outcomeCount = state_.recentRealOutcomes.size();
    writePod(out, outcomeCount);
    for (const auto& item : state_.recentRealOutcomes) {
        writePod(out, item.actionId); writePod(out, item.reward); writePod(out, item.success); writePod(out, item.novelty);
    }

    const bool hasPlan = state_.activePlan.has_value();
    writePod(out, hasPlan);
    if (hasPlan) {
        writeString(out, state_.activePlan->id);
        writePod(out, state_.activePlanStep);
        const std::uint64_t stepCount = state_.activePlan->steps.size();
        writePod(out, stepCount);
        for (const auto& step : state_.activePlan->steps) {
            writeString(out, step.id); writePod(out, step.kind); writeString(out, step.objective);
            writePod(out, step.status);
            const std::uint64_t capabilityCount = step.requiredCapabilities.size();
            writePod(out, capabilityCount);
            for (const auto& capability : step.requiredCapabilities) writeString(out, capability);
        }
    }
}

void CortexExecutive::loadState(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open Cortex executive state: " + path.string());
    std::array<char, 8> magic{};
    in.read(magic.data(), magic.size());
    if (!in || magic != kMagic) throw std::runtime_error("unknown Cortex executive state format");

    CortexExecutiveSnapshot loaded;
    readPod(in, loaded.nextGoalId);
    std::uint64_t count = 0;
    readPod(in, count);
    if (count > kMaxSerializedItems) throw std::runtime_error("implausible Cortex goal count");
    for (std::uint64_t i = 0; i < count; ++i) {
        CortexGoal goal;
        readPod(in, goal.id); goal.text = readString(in); readPod(in, goal.priority);
        readPod(in, goal.status); readPod(in, goal.createdStep); readPod(in, goal.updatedStep);
        loaded.goals.push_back(std::move(goal));
    }
    readPod(in, count);
    if (count > kMaxSerializedItems) throw std::runtime_error("implausible Cortex memory count");
    for (std::uint64_t i = 0; i < count; ++i) {
        CortexWorkingMemoryItem item;
        item.key = readString(in); item.value = readString(in); readPod(in, item.salience);
        readPod(in, item.createdStep); readPod(in, item.expiresStep); readPod(in, item.sourceFingerprint);
        loaded.workingMemory.push_back(std::move(item));
    }
    readPod(in, count);
    if (count > kMaxSerializedItems) throw std::runtime_error("implausible Cortex outcome count");
    for (std::uint64_t i = 0; i < count; ++i) {
        CortexRecentOutcome item;
        readPod(in, item.actionId); readPod(in, item.reward); readPod(in, item.success); readPod(in, item.novelty);
        loaded.recentRealOutcomes.push_back(item);
    }
    bool hasPlan = false;
    readPod(in, hasPlan);
    if (hasPlan) {
        CortexPlanProposal plan;
        plan.id = readString(in); readPod(in, loaded.activePlanStep); readPod(in, count);
        if (count > 64U) throw std::runtime_error("implausible Cortex plan step count");
        for (std::uint64_t i = 0; i < count; ++i) {
            CortexPlanStep step;
            step.id = readString(in); readPod(in, step.kind); step.objective = readString(in); readPod(in, step.status);
            std::uint64_t capabilityCount = 0; readPod(in, capabilityCount);
            if (capabilityCount > 64U) throw std::runtime_error("implausible Cortex plan capability count");
            for (std::uint64_t c = 0; c < capabilityCount; ++c) step.requiredCapabilities.push_back(readString(in));
            plan.steps.push_back(std::move(step));
        }
        loaded.activePlan = std::move(plan);
    }
    state_ = std::move(loaded);
}

} // namespace tatarus::cortex
