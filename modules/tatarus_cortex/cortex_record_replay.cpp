#include "tatarus/cortex_record_replay.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tatarus::cortex {
namespace {

constexpr std::array<char, 8> kMagic{'T','C','R','P','1','5','\0','\0'};
constexpr std::uint64_t kMaxString = 1024 * 1024;
constexpr std::uint64_t kMaxItems = 4096;

template <class T>
void writePod(std::ostream& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!out) throw std::runtime_error("failed to write Cortex record/replay tape");
}

template <class T>
void readPod(std::istream& in, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) throw std::runtime_error("truncated Cortex record/replay tape");
}

void writeString(std::ostream& out, const std::string& value) {
    if (value.size() > kMaxString) throw std::runtime_error("Cortex tape string too large");
    const std::uint64_t n = value.size();
    writePod(out, n);
    if (n) out.write(value.data(), static_cast<std::streamsize>(n));
    if (!out) throw std::runtime_error("failed to write Cortex tape string");
}

std::string readString(std::istream& in) {
    std::uint64_t n = 0;
    readPod(in, n);
    if (n > kMaxString) throw std::runtime_error("implausible Cortex tape string length");
    std::string value(static_cast<std::size_t>(n), '\0');
    if (n) in.read(value.data(), static_cast<std::streamsize>(n));
    if (!in) throw std::runtime_error("truncated Cortex tape string");
    return value;
}

void writeStrings(std::ostream& out, const std::vector<std::string>& values) {
    const std::uint64_t n = values.size();
    writePod(out, n);
    for (const auto& value : values) writeString(out, value);
}

std::vector<std::string> readStrings(std::istream& in) {
    std::uint64_t n = 0;
    readPod(in, n);
    if (n > kMaxItems) throw std::runtime_error("implausible Cortex tape list length");
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) result.push_back(readString(in));
    return result;
}

struct TapeKey {
    std::uint64_t stateFingerprint = 0;
    CortexTaskKind task = CortexTaskKind::ObserveState;
    std::string goal;
    std::vector<std::string> capabilities;
    std::uint64_t activeGoalId = 0;
    std::string activePlanId;
    std::size_t activePlanStep = 0;
};

TapeKey keyFor(const CortexRequest& request) {
    TapeKey key;
    key.stateFingerprint = request.stateFingerprint;
    key.task = request.task;
    key.goal = request.goal;
    key.capabilities = request.availableCapabilities;
    key.activeGoalId = request.executive.activeGoalId;
    key.activePlanId = request.executive.activePlanId;
    key.activePlanStep = request.executive.activePlanStep;
    return key;
}

bool matches(const TapeKey& key, const CortexRequest& request) {
    return key.stateFingerprint == request.stateFingerprint
        && key.task == request.task
        && key.goal == request.goal
        && key.capabilities == request.availableCapabilities
        && key.activeGoalId == request.executive.activeGoalId
        && key.activePlanId == request.executive.activePlanId
        && key.activePlanStep == request.executive.activePlanStep;
}

void writeKey(std::ostream& out, const TapeKey& key) {
    writePod(out, key.stateFingerprint);
    writePod(out, key.task);
    writeString(out, key.goal);
    writeStrings(out, key.capabilities);
    writePod(out, key.activeGoalId);
    writeString(out, key.activePlanId);
    const std::uint64_t step = key.activePlanStep;
    writePod(out, step);
}

TapeKey readKey(std::istream& in) {
    TapeKey key;
    readPod(in, key.stateFingerprint);
    readPod(in, key.task);
    key.goal = readString(in);
    key.capabilities = readStrings(in);
    readPod(in, key.activeGoalId);
    key.activePlanId = readString(in);
    std::uint64_t step = 0; readPod(in, step); key.activePlanStep = static_cast<std::size_t>(step);
    return key;
}

void writeStrategy(std::ostream& out, const CortexStrategy& s) {
    writeString(out, s.id); writePod(out, s.kind); writeString(out, s.rationale);
    writePod(out, s.estimatedRisk); writePod(out, s.estimatedBenefit); writePod(out, s.confidence);
    writeStrings(out, s.requiredCapabilities);
}

CortexStrategy readStrategy(std::istream& in) {
    CortexStrategy s;
    s.id = readString(in); readPod(in, s.kind); s.rationale = readString(in);
    readPod(in, s.estimatedRisk); readPod(in, s.estimatedBenefit); readPod(in, s.confidence);
    s.requiredCapabilities = readStrings(in);
    return s;
}

void writeResponse(std::ostream& out, const CortexResponse& response) {
    const std::uint64_t strategyCount = response.strategies.size(); writePod(out, strategyCount);
    for (const auto& s : response.strategies) writeStrategy(out, s);
    const std::uint64_t infoCount = response.informationRequests.size(); writePod(out, infoCount);
    for (const auto& info : response.informationRequests) {
        writeString(out, info.capability); writeString(out, info.reason);
    }
    const bool hasImagination = response.imagination.has_value(); writePod(out, hasImagination);
    if (hasImagination) {
        writeString(out, response.imagination->mode);
        writeStrings(out, response.imagination->symbols);
        writeString(out, response.imagination->conceptText);
    }
    const bool hasPlan = response.plan.has_value(); writePod(out, hasPlan);
    if (hasPlan) {
        writeString(out, response.plan->id);
        const std::uint64_t stepCount = response.plan->steps.size(); writePod(out, stepCount);
        for (const auto& step : response.plan->steps) {
            writeString(out, step.id); writePod(out, step.kind); writeString(out, step.objective);
            writeStrings(out, step.requiredCapabilities); writePod(out, step.status);
        }
    }
    writeString(out, response.summary);
}

CortexResponse readResponse(std::istream& in) {
    CortexResponse response;
    std::uint64_t count = 0; readPod(in, count);
    if (count > 64U) throw std::runtime_error("implausible Cortex tape strategy count");
    for (std::uint64_t i = 0; i < count; ++i) response.strategies.push_back(readStrategy(in));
    readPod(in, count);
    if (count > 64U) throw std::runtime_error("implausible Cortex tape info count");
    for (std::uint64_t i = 0; i < count; ++i) {
        CortexInformationRequest info; info.capability = readString(in); info.reason = readString(in);
        response.informationRequests.push_back(std::move(info));
    }
    bool present = false; readPod(in, present);
    if (present) {
        ImaginationDirective d; d.mode = readString(in); d.symbols = readStrings(in); d.conceptText = readString(in);
        response.imagination = std::move(d);
    }
    readPod(in, present);
    if (present) {
        CortexPlanProposal plan; plan.id = readString(in); readPod(in, count);
        if (count > 64U) throw std::runtime_error("implausible Cortex tape plan size");
        for (std::uint64_t i = 0; i < count; ++i) {
            CortexPlanStep step; step.id = readString(in); readPod(in, step.kind); step.objective = readString(in);
            step.requiredCapabilities = readStrings(in); readPod(in, step.status);
            plan.steps.push_back(std::move(step));
        }
        response.plan = std::move(plan);
    }
    response.summary = readString(in);
    return response;
}

struct TapeEntry {
    TapeKey key;
    CortexResponse response;
};

class RecordingClient final : public ICortexModelClient {
public:
    RecordingClient(std::shared_ptr<ICortexModelClient> inner, std::filesystem::path path)
        : inner_(std::move(inner)), path_(std::move(path)) {
        if (!inner_) throw std::invalid_argument("recording Cortex client requires inner client");
        if (path_.empty()) throw std::invalid_argument("recording Cortex path must not be empty");
    }

    CortexResponse complete(const CortexRequest& request) override {
        CortexResponse response = inner_->complete(request);
        std::scoped_lock lock(mutex_);
        const bool exists = std::filesystem::exists(path_) && std::filesystem::file_size(path_) > 0U;
        std::ofstream out(path_, std::ios::binary | std::ios::app);
        if (!out) throw std::runtime_error("cannot append Cortex record tape: " + path_.string());
        if (!exists) out.write(kMagic.data(), kMagic.size());
        const std::uint32_t marker = 0x54415045U; // TAPE
        writePod(out, marker);
        writeKey(out, keyFor(request));
        writeResponse(out, response);
        return response;
    }

private:
    std::shared_ptr<ICortexModelClient> inner_;
    std::filesystem::path path_;
    std::mutex mutex_;
};

class ReplayClient final : public ICortexModelClient {
public:
    ReplayClient(std::filesystem::path path, bool strict) : strict_(strict) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open Cortex replay tape: " + path.string());
        std::array<char, 8> magic{}; in.read(magic.data(), magic.size());
        if (!in || magic != kMagic) throw std::runtime_error("unknown Cortex replay tape format");
        while (in.peek() != std::char_traits<char>::eof()) {
            std::uint32_t marker = 0; readPod(in, marker);
            if (marker != 0x54415045U) throw std::runtime_error("corrupt Cortex replay tape marker");
            TapeEntry entry; entry.key = readKey(in); entry.response = readResponse(in);
            entries_.push_back(std::move(entry));
            if (entries_.size() > 1'000'000U) throw std::runtime_error("Cortex replay tape too large");
        }
    }

    CortexResponse complete(const CortexRequest& request) override {
        std::scoped_lock lock(mutex_);
        if (cursor_ >= entries_.size()) throw std::runtime_error("Cortex replay tape exhausted");
        std::size_t selected = cursor_;
        if (!matches(entries_[selected].key, request)) {
            if (strict_) throw std::runtime_error("Cortex replay request does not match next recorded entry");
            bool found = false;
            for (std::size_t i = cursor_ + 1U; i < entries_.size(); ++i) {
                if (matches(entries_[i].key, request)) { selected = i; found = true; break; }
            }
            if (!found) throw std::runtime_error("Cortex replay contains no matching entry");
        }
        cursor_ = selected + 1U;
        CortexResponse response = entries_[selected].response;
        response.requestId = request.requestId;
        response.sourceFingerprint = request.stateFingerprint;
        return response;
    }

private:
    bool strict_ = true;
    std::vector<TapeEntry> entries_;
    std::size_t cursor_ = 0;
    std::mutex mutex_;
};

} // namespace

std::shared_ptr<ICortexModelClient> makeRecordingCortexClient(
    std::shared_ptr<ICortexModelClient> inner,
    std::filesystem::path path) {
    return std::make_shared<RecordingClient>(std::move(inner), std::move(path));
}

std::shared_ptr<ICortexModelClient> makeReplayCortexClient(
    std::filesystem::path path,
    bool strict) {
    return std::make_shared<ReplayClient>(std::move(path), strict);
}

} // namespace tatarus::cortex
