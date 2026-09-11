#include "cortex_json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tatarus::cortex {

const char* toString(CortexMode value) noexcept {
    switch (value) {
        case CortexMode::Disabled: return "disabled";
        case CortexMode::ObserveOnly: return "observe_only";
        case CortexMode::Advisor: return "advisor";
        case CortexMode::Imagination: return "imagination";
        case CortexMode::Hybrid: return "hybrid";
    }
    return "disabled";
}

const char* toString(CortexTaskKind value) noexcept {
    switch (value) {
        case CortexTaskKind::ObserveState: return "OBSERVE_STATE";
        case CortexTaskKind::Plan: return "PLAN";
        case CortexTaskKind::Imagine: return "IMAGINE";
        case CortexTaskKind::HybridPlan: return "HYBRID_PLAN";
        case CortexTaskKind::Dream: return "DREAM";
        case CortexTaskKind::ExecutivePlan: return "EXECUTIVE_PLAN";
    }
    return "OBSERVE_STATE";
}

const char* toString(CortexTriggerReason value) noexcept {
    switch (value) {
        case CortexTriggerReason::None: return "NONE";
        case CortexTriggerReason::Explicit: return "EXPLICIT";
        case CortexTriggerReason::Novelty: return "NOVELTY";
        case CortexTriggerReason::PredictionError: return "PREDICTION_ERROR";
        case CortexTriggerReason::LowMotorConfidence: return "LOW_MOTOR_CONFIDENCE";
        case CortexTriggerReason::CombinedUncertainty: return "COMBINED_UNCERTAINTY";
        case CortexTriggerReason::SleepRem: return "SLEEP_REM";
        case CortexTriggerReason::GoalChanged: return "GOAL_CHANGED";
        case CortexTriggerReason::MetacognitiveFallback: return "METACOGNITIVE_FALLBACK";
    }
    return "NONE";
}

const char* toString(CortexStrategyKind value) noexcept {
    switch (value) {
        case CortexStrategyKind::Observe: return "OBSERVE";
        case CortexStrategyKind::WaitAndObserve: return "WAIT_AND_OBSERVE";
        case CortexStrategyKind::RequestScan: return "REQUEST_SCAN";
        case CortexStrategyKind::RecallRoute: return "RECALL_ROUTE";
        case CortexStrategyKind::FollowKnownRoute: return "FOLLOW_KNOWN_ROUTE";
        case CortexStrategyKind::ExploreFrontier: return "EXPLORE_FRONTIER";
        case CortexStrategyKind::SearchAlternative: return "SEARCH_ALTERNATIVE";
        case CortexStrategyKind::UseImagination: return "USE_IMAGINATION";
        case CortexStrategyKind::VisualRecall: return "VISUAL_RECALL";
        case CortexStrategyKind::SymbolRecall: return "SYMBOL_RECALL";
        case CortexStrategyKind::Compose: return "COMPOSE";
        case CortexStrategyKind::FreeImagination: return "FREE_IMAGINATION";
        case CortexStrategyKind::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* toString(CortexDecisionKind value) noexcept {
    switch (value) {
        case CortexDecisionKind::Reject: return "REJECT";
        case CortexDecisionKind::Accept: return "ACCEPT";
        case CortexDecisionKind::Defer: return "DEFER";
        case CortexDecisionKind::RequestMoreInformation: return "REQUEST_MORE_INFORMATION";
        case CortexDecisionKind::SendToImagination: return "SEND_TO_IMAGINATION";
    }
    return "REJECT";
}

const char* toString(CortexGoalStatus value) noexcept {
    switch (value) {
        case CortexGoalStatus::Pending: return "PENDING";
        case CortexGoalStatus::Active: return "ACTIVE";
        case CortexGoalStatus::Completed: return "COMPLETED";
        case CortexGoalStatus::Failed: return "FAILED";
        case CortexGoalStatus::Cancelled: return "CANCELLED";
    }
    return "PENDING";
}

const char* toString(CortexPlanStepStatus value) noexcept {
    switch (value) {
        case CortexPlanStepStatus::Pending: return "PENDING";
        case CortexPlanStepStatus::Active: return "ACTIVE";
        case CortexPlanStepStatus::Completed: return "COMPLETED";
        case CortexPlanStepStatus::Failed: return "FAILED";
        case CortexPlanStepStatus::Skipped: return "SKIPPED";
    }
    return "PENDING";
}

CortexStrategyKind cortexStrategyKindFromString(std::string_view value) noexcept {
    if (value == "OBSERVE") return CortexStrategyKind::Observe;
    if (value == "WAIT_AND_OBSERVE") return CortexStrategyKind::WaitAndObserve;
    if (value == "REQUEST_SCAN") return CortexStrategyKind::RequestScan;
    if (value == "RECALL_ROUTE") return CortexStrategyKind::RecallRoute;
    if (value == "FOLLOW_KNOWN_ROUTE") return CortexStrategyKind::FollowKnownRoute;
    if (value == "EXPLORE_FRONTIER") return CortexStrategyKind::ExploreFrontier;
    if (value == "SEARCH_ALTERNATIVE") return CortexStrategyKind::SearchAlternative;
    if (value == "USE_IMAGINATION") return CortexStrategyKind::UseImagination;
    if (value == "VISUAL_RECALL") return CortexStrategyKind::VisualRecall;
    if (value == "SYMBOL_RECALL") return CortexStrategyKind::SymbolRecall;
    if (value == "COMPOSE") return CortexStrategyKind::Compose;
    if (value == "FREE_IMAGINATION") return CortexStrategyKind::FreeImagination;
    return CortexStrategyKind::Unknown;
}

} // namespace tatarus::cortex

namespace tatarus::cortex::detail {
namespace {

enum class JsonKind { Null, Boolean, Number, String, Array, Object };

struct JsonValue {
    JsonKind kind = JsonKind::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    [[nodiscard]] const JsonValue* member(std::string_view key) const noexcept {
        for (const auto& [name, value] : objectValue) {
            if (name == key) return &value;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {
        if (input.size() > 2U * 1024U * 1024U) {
            throw std::runtime_error("JSON payload exceeds 2 MiB safety limit");
        }
    }

    [[nodiscard]] JsonValue parse() {
        skipWhitespace();
        auto result = parseValue(0);
        skipWhitespace();
        if (position_ != input_.size()) fail("trailing data");
        return result;
    }

private:
    std::string_view input_;
    std::size_t position_ = 0;

    [[noreturn]] void fail(std::string_view message) const {
        throw std::runtime_error(
            "Invalid JSON at byte " + std::to_string(position_) + ": " + std::string(message));
    }

    void skipWhitespace() noexcept {
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_]);
            if (!std::isspace(c)) break;
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    [[nodiscard]] JsonValue parseValue(unsigned depth) {
        if (depth > 32U) fail("maximum nesting depth exceeded");
        skipWhitespace();
        if (position_ >= input_.size()) fail("unexpected end of input");
        const char c = input_[position_];
        if (c == '{') return parseObject(depth + 1U);
        if (c == '[') return parseArray(depth + 1U);
        if (c == '"') {
            JsonValue value;
            value.kind = JsonKind::String;
            value.stringValue = parseString();
            return value;
        }
        if (c == 't') return parseLiteral("true", JsonKind::Boolean, true);
        if (c == 'f') return parseLiteral("false", JsonKind::Boolean, false);
        if (c == 'n') return parseLiteral("null", JsonKind::Null, false);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        fail("unexpected token");
    }

    [[nodiscard]] JsonValue parseLiteral(
        std::string_view literal,
        JsonKind kind,
        bool boolean) {
        if (input_.substr(position_, literal.size()) != literal) fail("invalid literal");
        position_ += literal.size();
        JsonValue value;
        value.kind = kind;
        value.boolValue = boolean;
        return value;
    }

    static void appendUtf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7FU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0x10FFFFU) {
            output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            throw std::runtime_error("Invalid Unicode code point in JSON string");
        }
    }

    [[nodiscard]] std::uint32_t parseHex4() {
        if (position_ + 4U > input_.size()) fail("truncated unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[position_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    [[nodiscard]] std::string parseString() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return output;
            if (c < 0x20U) fail("unescaped control character");
            if (c != '\\') {
                output.push_back(static_cast<char>(c));
                continue;
            }
            if (position_ >= input_.size()) fail("truncated escape");
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = parseHex4();
                    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                        if (position_ + 2U > input_.size()
                            || input_[position_] != '\\'
                            || input_[position_ + 1U] != 'u') {
                            fail("high surrogate without low surrogate");
                        }
                        position_ += 2U;
                        const std::uint32_t low = parseHex4();
                        if (low < 0xDC00U || low > 0xDFFFU) fail("invalid low surrogate");
                        codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
                    } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                        fail("unexpected low surrogate");
                    }
                    appendUtf8(output, codepoint);
                    break;
                }
                default: fail("invalid escape sequence");
            }
        }
        fail("unterminated string");
    }

    [[nodiscard]] JsonValue parseNumber() {
        const std::size_t start = position_;
        if (consume('-') && position_ >= input_.size()) fail("truncated number");
        if (consume('0')) {
            if (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("leading zero in number");
            }
        } else {
            if (position_ >= input_.size()
                || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid number");
            }
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        if (consume('.')) {
            if (position_ >= input_.size()
                || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid fractional number");
            }
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size()
                || !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                fail("invalid exponent");
            }
            while (position_ < input_.size()
                   && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
        }
        const std::string text(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(number)) fail("non-finite number");
        JsonValue value;
        value.kind = JsonKind::Number;
        value.numberValue = number;
        return value;
    }

    [[nodiscard]] JsonValue parseArray(unsigned depth) {
        expect('[');
        JsonValue value;
        value.kind = JsonKind::Array;
        skipWhitespace();
        if (consume(']')) return value;
        while (true) {
            if (value.arrayValue.size() >= 4096U) fail("array item limit exceeded");
            value.arrayValue.push_back(parseValue(depth));
            skipWhitespace();
            if (consume(']')) return value;
            expect(',');
            skipWhitespace();
        }
    }

    [[nodiscard]] JsonValue parseObject(unsigned depth) {
        expect('{');
        JsonValue value;
        value.kind = JsonKind::Object;
        skipWhitespace();
        if (consume('}')) return value;
        while (true) {
            if (value.objectValue.size() >= 4096U) fail("object member limit exceeded");
            if (position_ >= input_.size() || input_[position_] != '"') fail("object key must be string");
            std::string key = parseString();
            for (const auto& [existing, ignored] : value.objectValue) {
                (void)ignored;
                if (existing == key) fail("duplicate object key");
            }
            skipWhitespace();
            expect(':');
            skipWhitespace();
            value.objectValue.emplace_back(std::move(key), parseValue(depth));
            skipWhitespace();
            if (consume('}')) return value;
            expect(',');
            skipWhitespace();
        }
    }
};

[[nodiscard]] const JsonValue& requireMember(
    const JsonValue& object,
    std::string_view name,
    JsonKind kind) {
    if (object.kind != JsonKind::Object) throw std::runtime_error("Expected JSON object");
    const JsonValue* member = object.member(name);
    if (!member) throw std::runtime_error("Missing JSON field: " + std::string(name));
    if (member->kind != kind) throw std::runtime_error("Wrong JSON type for field: " + std::string(name));
    return *member;
}

[[nodiscard]] const JsonValue* optionalMember(
    const JsonValue& object,
    std::string_view name,
    JsonKind kind) {
    if (object.kind != JsonKind::Object) throw std::runtime_error("Expected JSON object");
    const JsonValue* member = object.member(name);
    if (!member || member->kind == JsonKind::Null) return nullptr;
    if (member->kind != kind) throw std::runtime_error("Wrong JSON type for field: " + std::string(name));
    return member;
}

[[nodiscard]] std::uint64_t unsignedInteger(const JsonValue& value, std::string_view field) {
    if (value.kind != JsonKind::Number || value.numberValue < 0.0
        || value.numberValue > static_cast<double>(std::numeric_limits<std::uint64_t>::max())
        || std::floor(value.numberValue) != value.numberValue) {
        throw std::runtime_error("Field must be an unsigned integer: " + std::string(field));
    }
    return static_cast<std::uint64_t>(value.numberValue);
}

[[nodiscard]] double normalizedNumber(const JsonValue& value, std::string_view field) {
    if (value.kind != JsonKind::Number || !std::isfinite(value.numberValue)
        || value.numberValue < 0.0 || value.numberValue > 1.0) {
        throw std::runtime_error("Field must be finite and in [0,1]: " + std::string(field));
    }
    return value.numberValue;
}

void requireLength(std::string_view value, std::size_t maximum, std::string_view field) {
    if (value.size() > maximum) {
        throw std::runtime_error("Field exceeds byte limit: " + std::string(field));
    }
}

void writeString(std::ostringstream& out, std::string_view value) {
    out << '"' << escapeJson(value) << '"';
}

} // namespace

std::string escapeJson(std::string_view value) {
    std::ostringstream out;
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    out << "\\u00" << hex[(c >> 4U) & 0x0FU] << hex[c & 0x0FU];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}


CortexConfig parseConfig(std::string_view json) {
    const JsonValue root = JsonParser(json).parse();
    if (root.kind != JsonKind::Object) throw std::runtime_error("Cortex config root must be an object");

    CortexConfig config;
    if (const auto* value = optionalMember(root, "enabled", JsonKind::Boolean)) {
        config.enabled = value->boolValue;
    }
    if (const auto* value = optionalMember(root, "mode", JsonKind::String)) {
        if (value->stringValue == "disabled") config.mode = CortexMode::Disabled;
        else if (value->stringValue == "observe_only") config.mode = CortexMode::ObserveOnly;
        else if (value->stringValue == "advisor") config.mode = CortexMode::Advisor;
        else if (value->stringValue == "imagination") config.mode = CortexMode::Imagination;
        else if (value->stringValue == "hybrid") config.mode = CortexMode::Hybrid;
        else throw std::runtime_error("Unknown Cortex mode: " + value->stringValue);
    }
    if (const auto* value = optionalMember(root, "execution_mode", JsonKind::String)) {
        if (value->stringValue == "off") config.executionMode = CortexExecutionMode::Off;
        else if (value->stringValue == "live") config.executionMode = CortexExecutionMode::Live;
        else if (value->stringValue == "record") config.executionMode = CortexExecutionMode::Record;
        else if (value->stringValue == "replay") config.executionMode = CortexExecutionMode::Replay;
        else throw std::runtime_error("Unknown Cortex execution_mode: " + value->stringValue);
    }

    if (const auto* provider = optionalMember(root, "provider", JsonKind::Object)) {
        if (const auto* type = optionalMember(*provider, "type", JsonKind::String)) {
            if (type->stringValue != "lm_studio") {
                throw std::runtime_error("Only lm_studio provider is supported in Cortex phases 0-15");
            }
        }
        if (const auto* value = optionalMember(*provider, "base_url", JsonKind::String)) {
            config.provider.baseUrl = value->stringValue;
        }
        if (const auto* value = optionalMember(*provider, "model", JsonKind::String)) {
            config.provider.model = value->stringValue;
        }
        if (const auto* value = optionalMember(*provider, "timeout_ms", JsonKind::Number)) {
            config.provider.timeoutMs = static_cast<std::uint32_t>(unsignedInteger(*value, "provider.timeout_ms"));
        }
        if (const auto* value = optionalMember(*provider, "temperature", JsonKind::Number)) {
            config.provider.temperature = value->numberValue;
        }
        if (const auto* value = optionalMember(*provider, "top_p", JsonKind::Number)) {
            config.provider.topP = value->numberValue;
        }
        if (const auto* value = optionalMember(*provider, "max_output_tokens", JsonKind::Number)) {
            config.provider.maxOutputTokens = static_cast<std::uint32_t>(unsignedInteger(*value, "provider.max_output_tokens"));
        }
    }

    if (const auto* worker = optionalMember(root, "worker", JsonKind::Object)) {
        if (const auto* value = optionalMember(*worker, "queue_capacity", JsonKind::Number)) {
            config.workerQueueCapacity = static_cast<std::size_t>(unsignedInteger(*value, "worker.queue_capacity"));
        }
    }

    if (const auto* freshness = optionalMember(root, "freshness", JsonKind::Object)) {
        if (const auto* value = optionalMember(
                *freshness, "maximum_response_age_steps", JsonKind::Number)) {
            config.freshness.maximumResponseAgeSteps = unsignedInteger(
                *value, "freshness.maximum_response_age_steps");
        }
        if (config.freshness.maximumResponseAgeSteps == 0U
            || config.freshness.maximumResponseAgeSteps > 1'000'000U) {
            throw std::runtime_error(
                "freshness.maximum_response_age_steps must be in [1,1000000]");
        }
    }

    if (const auto* trigger = optionalMember(root, "trigger", JsonKind::Object)) {
        if (const auto* value = optionalMember(*trigger, "novelty", JsonKind::Number)) {
            config.trigger.novelty = normalizedNumber(*value, "trigger.novelty");
        }
        if (const auto* value = optionalMember(*trigger, "prediction_error", JsonKind::Number)) {
            config.trigger.predictionError = normalizedNumber(*value, "trigger.prediction_error");
        }
        if (const auto* value = optionalMember(*trigger, "low_motor_confidence", JsonKind::Number)) {
            config.trigger.lowMotorConfidence = normalizedNumber(*value, "trigger.low_motor_confidence");
        }
        if (const auto* value = optionalMember(*trigger, "cooldown_steps", JsonKind::Number)) {
            config.trigger.cooldownSteps = unsignedInteger(*value, "trigger.cooldown_steps");
        }
    }

    if (const auto* arbiter = optionalMember(root, "arbiter", JsonKind::Object)) {
        if (const auto* value = optionalMember(*arbiter, "minimum_strategy_confidence", JsonKind::Number)) {
            config.arbiter.minimumStrategyConfidence = normalizedNumber(*value, "arbiter.minimum_strategy_confidence");
        }
        if (const auto* value = optionalMember(*arbiter, "maximum_strategy_risk", JsonKind::Number)) {
            config.arbiter.maximumStrategyRisk = normalizedNumber(*value, "arbiter.maximum_strategy_risk");
        }
        if (const auto* value = optionalMember(*arbiter, "minimum_accept_score", JsonKind::Number)) {
            config.arbiter.minimumAcceptScore = normalizedNumber(*value, "arbiter.minimum_accept_score");
        }
        if (const auto* value = optionalMember(*arbiter, "critical_visceral_distress", JsonKind::Number)) {
            config.arbiter.criticalVisceralDistress = normalizedNumber(*value, "arbiter.critical_visceral_distress");
        }
        if (const auto* value = optionalMember(*arbiter, "critical_brain_atp", JsonKind::Number)) {
            config.arbiter.criticalBrainAtp = normalizedNumber(*value, "arbiter.critical_brain_atp");
        }
    }

    if (const auto* sandbox = optionalMember(root, "sandbox", JsonKind::Object)) {
        if (const auto* value = optionalMember(*sandbox, "maximum_branches", JsonKind::Number)) {
            config.sandbox.maximumBranches = static_cast<std::size_t>(unsignedInteger(*value, "sandbox.maximum_branches"));
            if (config.sandbox.maximumBranches == 0U || config.sandbox.maximumBranches > 64U) {
                throw std::runtime_error("sandbox.maximum_branches must be in [1,64]");
            }
        }
    }

    if (const auto* learning = optionalMember(root, "learning", JsonKind::Object)) {
        if (const auto* value = optionalMember(*learning, "enabled", JsonKind::Boolean)) {
            config.learning.enabled = value->boolValue;
        }
        if (const auto* value = optionalMember(*learning, "competence_gate_enabled", JsonKind::Boolean)) {
            config.learning.competenceGateEnabled = value->boolValue;
        }
        if (const auto* value = optionalMember(*learning, "minimum_teaching_successes", JsonKind::Number)) {
            config.learning.minimumTeachingSuccesses = unsignedInteger(*value, "learning.minimum_teaching_successes");
        }
        if (const auto* value = optionalMember(*learning, "minimum_autonomous_trials", JsonKind::Number)) {
            config.learning.minimumAutonomousTrials = unsignedInteger(*value, "learning.minimum_autonomous_trials");
        }
        if (const auto* value = optionalMember(*learning, "autonomous_success_threshold", JsonKind::Number)) {
            config.learning.autonomousSuccessThreshold = normalizedNumber(*value, "learning.autonomous_success_threshold");
        }
        if (const auto* value = optionalMember(*learning, "outcome_success_threshold", JsonKind::Number)) {
            config.learning.outcomeSuccessThreshold = normalizedNumber(*value, "learning.outcome_success_threshold");
        }
        if (config.learning.minimumTeachingSuccesses == 0U || config.learning.minimumAutonomousTrials == 0U) {
            throw std::runtime_error("Cortex learning trial thresholds must be greater than zero");
        }
    }

    if (const auto* topDown = optionalMember(root, "top_down", JsonKind::Object)) {
        if (const auto* value = optionalMember(*topDown, "enabled", JsonKind::Boolean)) {
            config.topDown.enabled = value->boolValue;
        }
        if (const auto* value = optionalMember(*topDown, "maximum_recall_strength", JsonKind::Number)) {
            config.topDown.maximumRecallStrength = normalizedNumber(*value, "top_down.maximum_recall_strength");
        }
        if (const auto* value = optionalMember(*topDown, "maximum_goal_bias_strength", JsonKind::Number)) {
            config.topDown.maximumGoalBiasStrength = normalizedNumber(*value, "top_down.maximum_goal_bias_strength");
        }
        if (const auto* value = optionalMember(*topDown, "semantic_channels", JsonKind::Number)) {
            config.topDown.semanticChannels = static_cast<std::size_t>(unsignedInteger(*value, "top_down.semantic_channels"));
        }
        if (const auto* value = optionalMember(*topDown, "semantic_gain", JsonKind::Number)) {
            config.topDown.semanticGain = normalizedNumber(*value, "top_down.semantic_gain");
        }
        if (config.topDown.maximumRecallStrength > CognitiveCueLimits::maximumRecallStrength
            || config.topDown.maximumGoalBiasStrength > CognitiveCueLimits::maximumGoalBiasStrength
            || config.topDown.semanticGain > CognitiveCueLimits::maximumSemanticMagnitude
            || config.topDown.semanticChannels == 0U
            || config.topDown.semanticChannels > CognitiveCueLimits::maximumSemanticChannels) {
            throw std::runtime_error("top_down configuration exceeds hard CognitiveCue safety limits");
        }
    }

    if (const auto* dream = optionalMember(root, "dream", JsonKind::Object)) {
        if (const auto* value = optionalMember(*dream, "enabled", JsonKind::Boolean)) {
            config.dream.enabled = value->boolValue;
        }
        if (const auto* value = optionalMember(*dream, "minimum_rem_interval_steps", JsonKind::Number)) {
            config.dream.minimumRemIntervalSteps = unsignedInteger(*value, "dream.minimum_rem_interval_steps");
        }
        if (const auto* value = optionalMember(*dream, "maximum_dream_symbols", JsonKind::Number)) {
            config.dream.maximumDreamSymbols = static_cast<std::size_t>(unsignedInteger(*value, "dream.maximum_dream_symbols"));
        }
        if (config.dream.maximumDreamSymbols == 0U || config.dream.maximumDreamSymbols > 32U) {
            throw std::runtime_error("dream.maximum_dream_symbols must be in [1,32]");
        }
    }


    if (const auto* executive = optionalMember(root, "executive", JsonKind::Object)) {
        if (const auto* value = optionalMember(*executive, "enabled", JsonKind::Boolean)) config.executive.enabled = value->boolValue;
        if (const auto* value = optionalMember(*executive, "maximum_goals", JsonKind::Number)) config.executive.maximumGoals = static_cast<std::size_t>(unsignedInteger(*value, "executive.maximum_goals"));
        if (const auto* value = optionalMember(*executive, "maximum_working_memory_items", JsonKind::Number)) config.executive.maximumWorkingMemoryItems = static_cast<std::size_t>(unsignedInteger(*value, "executive.maximum_working_memory_items"));
        if (const auto* value = optionalMember(*executive, "maximum_plan_steps", JsonKind::Number)) config.executive.maximumPlanSteps = static_cast<std::size_t>(unsignedInteger(*value, "executive.maximum_plan_steps"));
        if (const auto* value = optionalMember(*executive, "maximum_recent_outcomes", JsonKind::Number)) config.executive.maximumRecentOutcomes = static_cast<std::size_t>(unsignedInteger(*value, "executive.maximum_recent_outcomes"));
        if (const auto* value = optionalMember(*executive, "working_memory_ttl_steps", JsonKind::Number)) config.executive.workingMemoryTtlSteps = unsignedInteger(*value, "executive.working_memory_ttl_steps");
        if (config.executive.maximumGoals == 0U || config.executive.maximumGoals > 128U
            || config.executive.maximumWorkingMemoryItems == 0U || config.executive.maximumWorkingMemoryItems > 1024U
            || config.executive.maximumPlanSteps == 0U || config.executive.maximumPlanSteps > 64U
            || config.executive.maximumRecentOutcomes == 0U || config.executive.maximumRecentOutcomes > 256U) {
            throw std::runtime_error("executive configuration exceeds hard bounds");
        }
    }

    if (const auto* metacognition = optionalMember(root, "metacognition", JsonKind::Object)) {
        if (const auto* value = optionalMember(*metacognition, "enabled", JsonKind::Boolean)) config.metacognition.enabled = value->boolValue;
        if (const auto* value = optionalMember(*metacognition, "minimum_samples", JsonKind::Number)) config.metacognition.minimumSamples = unsignedInteger(*value, "metacognition.minimum_samples");
        if (const auto* value = optionalMember(*metacognition, "cautious_reliability_threshold", JsonKind::Number)) config.metacognition.cautiousReliabilityThreshold = normalizedNumber(*value, "metacognition.cautious_reliability_threshold");
        if (const auto* value = optionalMember(*metacognition, "degraded_reliability_threshold", JsonKind::Number)) config.metacognition.degradedReliabilityThreshold = normalizedNumber(*value, "metacognition.degraded_reliability_threshold");
        if (const auto* value = optionalMember(*metacognition, "maximum_stale_rate", JsonKind::Number)) config.metacognition.maximumStaleRate = normalizedNumber(*value, "metacognition.maximum_stale_rate");
        if (config.metacognition.minimumSamples == 0U
            || config.metacognition.degradedReliabilityThreshold > config.metacognition.cautiousReliabilityThreshold) {
            throw std::runtime_error("invalid metacognition configuration");
        }
    }

    if (const auto* tape = optionalMember(root, "record_replay", JsonKind::Object)) {
        if (const auto* value = optionalMember(*tape, "path", JsonKind::String)) config.recordReplay.path = value->stringValue;
        if (const auto* value = optionalMember(*tape, "strict", JsonKind::Boolean)) config.recordReplay.strict = value->boolValue;
        if (config.recordReplay.path.empty() || config.recordReplay.path.size() > 4096U) {
            throw std::runtime_error("record_replay.path must contain 1..4096 bytes");
        }
    }

    if (const auto* limits = optionalMember(root, "limits", JsonKind::Object)) {
        if (const auto* value = optionalMember(*limits, "max_strategies", JsonKind::Number)) {
            config.limits.maxStrategies = static_cast<std::size_t>(unsignedInteger(*value, "limits.max_strategies"));
        }
        if (const auto* value = optionalMember(*limits, "max_information_requests", JsonKind::Number)) {
            config.limits.maxInformationRequests = static_cast<std::size_t>(unsignedInteger(*value, "limits.max_information_requests"));
        }
        if (const auto* value = optionalMember(*limits, "max_summary_bytes", JsonKind::Number)) {
            config.limits.maxSummaryBytes = static_cast<std::size_t>(unsignedInteger(*value, "limits.max_summary_bytes"));
        }
        if (const auto* value = optionalMember(*limits, "max_rationale_bytes", JsonKind::Number)) {
            config.limits.maxRationaleBytes = static_cast<std::size_t>(unsignedInteger(*value, "limits.max_rationale_bytes"));
        }
        if (const auto* value = optionalMember(*limits, "max_prompt_bytes", JsonKind::Number)) {
            config.limits.maxPromptBytes = static_cast<std::size_t>(unsignedInteger(*value, "limits.max_prompt_bytes"));
        }
    }

    if (const auto* safety = optionalMember(root, "safety_contract", JsonKind::Object)) {
        for (const std::string_view key : {
                 "direct_reward", "direct_motor_control", "physiology_mutation",
                 "neuron_access", "synapse_access"}) {
            if (const auto* value = optionalMember(*safety, key, JsonKind::Boolean)) {
                if (value->boolValue) {
                    throw std::runtime_error(
                        "Cortex phases 0-15 forbid enabling safety_contract." + std::string(key));
                }
            }
        }
    }
    return config;
}

std::string serializeRequest(const CortexRequest& request) {
    std::ostringstream out;
    out.precision(8);
    out << '{';
    out << "\"request_id\":\"" << request.requestId << "\"";
    out << ",\"organism_step\":\"" << request.organismStep << "\"";
    out << ",\"state_fingerprint\":\"" << request.stateFingerprint << "\"";
    out << ",\"task\":"; writeString(out, toString(request.task));
    out << ",\"trigger\":"; writeString(out, toString(request.trigger));
    out << ",\"goal\":"; writeString(out, request.goal);

    const auto& n = request.neural;
    out << ",\"neural\":{";
    out << "\"assembly_id\":" << n.assemblyId;
    out << ",\"predicted_assembly_id\":" << n.predictedAssemblyId;
    out << ",\"prediction_confidence\":" << n.predictionConfidence;
    out << ",\"prediction_error\":" << n.predictionError;
    out << ",\"novelty\":" << n.novelty;
    out << ",\"sequence_familiarity\":" << n.sequenceFamiliarity;
    out << ",\"motor_confidence\":" << n.motorConfidence;
    out << ",\"mean_energy\":" << n.meanEnergy;
    out << ",\"brain_atp\":" << n.brainAtp;
    out << ",\"brain_oxygen\":" << n.brainOxygen;
    out << ",\"brain_glucose\":" << n.brainGlucose;
    out << ",\"active_representations\":[";
    for (std::size_t i = 0; i < n.activeRepresentations.size(); ++i) {
        if (i) out << ',';
        const auto& representation = n.activeRepresentations[i];
        out << "{\"id\":\"" << representation.id << "\"";
        out << ",\"activation\":" << representation.activation;
        out << ",\"familiarity\":" << representation.familiarity << '}';
    }
    out << "]}";

    const auto& p = request.physiology;
    out << ",\"physiology\":{";
    out << "\"available\":" << (p.available ? "true" : "false");
    out << ",\"atp\":" << p.atp;
    out << ",\"heart_rate_bpm\":" << p.heartRateBpm;
    out << ",\"map_mmhg\":" << p.mapMmHg;
    out << ",\"cardiac_output_l_min\":" << p.cardiacOutputLPerMin;
    out << ",\"oxygen_saturation\":" << p.oxygenSaturation;
    out << ",\"respiration_rate_bpm\":" << p.respirationRateBpm;
    out << ",\"gfr_ml_min\":" << p.gfrMlPerMin;
    out << ",\"visceral_distress\":" << p.visceralDistress;
    out << ",\"sympathetic_tone\":" << p.sympatheticTone;
    out << ",\"brain_oxygen\":" << p.brainOxygen;
    out << ",\"brain_glucose\":" << p.brainGlucose;
    out << ",\"sleep_phase\":";
    switch (p.sleepPhase) {
        case SleepPhase::Wake: writeString(out, "WAKE"); break;
        case SleepPhase::Nrem: writeString(out, "NREM"); break;
        case SleepPhase::Rem: writeString(out, "REM"); break;
    }
    out << '}';

    const auto& spatial = request.spatial;
    out << ",\"spatial\":{";
    out << "\"environment_id\":\"" << spatial.environmentId << "\"";
    out << ",\"map_available\":" << (spatial.mapAvailable ? "true" : "false");
    out << ",\"local_novelty\":" << spatial.localNovelty;
    out << ",\"frontier_ratio\":" << spatial.frontierRatio;
    out << ",\"known_route_available\":" << (spatial.knownRouteAvailable ? "true" : "false");
    out << ",\"known_route_confidence\":" << spatial.knownRouteConfidence;
    out << ",\"target_distance\":" << spatial.targetDistance;
    out << ",\"target_bearing\":" << spatial.targetBearing;
    out << ",\"loop_detected\":" << (spatial.loopDetected ? "true" : "false");
    out << ",\"repeated_place_count\":" << spatial.repeatedPlaceCount;
    out << ",\"obstacle_proximity\":[";
    for (std::size_t i = 0; i < spatial.obstacleProximity.size(); ++i) {
        if (i) out << ',';
        out << spatial.obstacleProximity[i];
    }
    out << "]}";

    const auto& imagination = request.imagination;
    out << ",\"imagination\":{";
    out << "\"available\":" << (imagination.available ? "true" : "false");
    out << ",\"stage\":" << static_cast<unsigned>(imagination.stage);
    out << ",\"visual_engrams\":" << imagination.visualEngrams;
    out << ",\"symbol_engrams\":" << imagination.symbolEngrams;
    out << ",\"last_similarity\":" << imagination.lastSimilarity;
    out << ",\"last_novelty\":" << imagination.lastNovelty;
    out << ",\"known_concepts\":[";
    for (std::size_t i = 0; i < imagination.knownConcepts.size(); ++i) {
        if (i) out << ',';
        writeString(out, imagination.knownConcepts[i]);
    }
    out << "]";
    out << ",\"known_symbols\":[";
    for (std::size_t i = 0; i < imagination.knownSymbols.size(); ++i) {
        if (i) out << ',';
        writeString(out, imagination.knownSymbols[i]);
    }
    out << "]";
    out << ",\"known_categories\":[";
    for (std::size_t i = 0; i < imagination.knownCategories.size(); ++i) {
        if (i) out << ',';
        writeString(out, imagination.knownCategories[i]);
    }
    out << "]}";

    const auto& executive = request.executive;
    out << ",\"executive\":{";
    out << "\"available\":" << (executive.available ? "true" : "false");
    out << ",\"active_goal_id\":\"" << executive.activeGoalId << "\"";
    out << ",\"active_goal\":"; writeString(out, executive.activeGoal);
    out << ",\"active_goal_priority\":" << executive.activeGoalPriority;
    out << ",\"goal_count\":" << executive.goalCount;
    out << ",\"active_plan_id\":"; writeString(out, executive.activePlanId);
    out << ",\"active_plan_step\":" << executive.activePlanStep;
    out << ",\"plan_steps\":[";
    for (std::size_t i = 0; i < executive.planSteps.size(); ++i) {
        if (i) out << ',';
        const auto& step = executive.planSteps[i];
        out << "{\"id\":"; writeString(out, step.id);
        out << ",\"kind\":"; writeString(out, toString(step.kind));
        out << ",\"objective\":"; writeString(out, step.objective);
        out << ",\"status\":"; writeString(out, toString(step.status));
        out << '}';
    }
    out << "]";
    out << ",\"working_memory\":[";
    for (std::size_t i = 0; i < executive.workingMemory.size(); ++i) {
        if (i) out << ',';
        const auto& item = executive.workingMemory[i];
        out << "{\"key\":"; writeString(out, item.key);
        out << ",\"value\":"; writeString(out, item.value);
        out << ",\"salience\":" << item.salience << '}';
    }
    out << "]}";

    out << ",\"available_capabilities\":[";
    for (std::size_t i = 0; i < request.availableCapabilities.size(); ++i) {
        if (i) out << ',';
        writeString(out, request.availableCapabilities[i]);
    }
    out << ']';

    out << ",\"recent_outcomes\":[";
    for (std::size_t i = 0; i < request.recentOutcomes.size(); ++i) {
        if (i) out << ',';
        const auto& outcome = request.recentOutcomes[i];
        out << "{\"action_id\":" << outcome.actionId
            << ",\"reward\":" << outcome.reward
            << ",\"success\":" << outcome.success
            << ",\"novelty\":" << outcome.novelty << '}';
    }
    out << "]}";
    return out.str();
}

CortexResponse parseResponse(
    std::string_view json,
    const CortexRequest& request,
    const CortexLimits& limits) {
    const JsonValue root = JsonParser(json).parse();
    if (root.kind != JsonKind::Object) throw std::runtime_error("Cortex response root must be an object");

    CortexResponse response;
    response.requestId = request.requestId;
    response.sourceFingerprint = request.stateFingerprint;

    const auto parseU64String = [](const JsonValue& value, std::string_view field) {
        if (value.kind != JsonKind::String || value.stringValue.empty()) {
            throw std::runtime_error("Field must be an unsigned integer string: " + std::string(field));
        }
        std::uint64_t parsed = 0;
        const auto [ptr, ec] = std::from_chars(
            value.stringValue.data(), value.stringValue.data() + value.stringValue.size(), parsed);
        if (ec != std::errc{} || ptr != value.stringValue.data() + value.stringValue.size()) {
            throw std::runtime_error("Invalid unsigned integer string: " + std::string(field));
        }
        return parsed;
    };
    if (const auto* value = optionalMember(root, "request_id", JsonKind::String)) {
        const auto parsed = parseU64String(*value, "request_id");
        if (parsed != request.requestId) throw std::runtime_error("Cortex response request_id mismatch");
    }
    if (const auto* value = optionalMember(root, "source_fingerprint", JsonKind::String)) {
        const auto parsed = parseU64String(*value, "source_fingerprint");
        if (parsed != request.stateFingerprint) throw std::runtime_error("Cortex response source_fingerprint mismatch");
    }

    if (const auto* strategies = optionalMember(root, "strategies", JsonKind::Array)) {
        if (strategies->arrayValue.size() > limits.maxStrategies) {
            throw std::runtime_error("Cortex response contains too many strategies");
        }
        for (const auto& item : strategies->arrayValue) {
            if (item.kind != JsonKind::Object) throw std::runtime_error("Strategy must be an object");
            CortexStrategy strategy;
            strategy.id = requireMember(item, "id", JsonKind::String).stringValue;
            requireLength(strategy.id, 96U, "strategy.id");
            const auto& kind = requireMember(item, "kind", JsonKind::String).stringValue;
            strategy.kind = cortexStrategyKindFromString(kind);
            if (strategy.kind == CortexStrategyKind::Unknown) {
                throw std::runtime_error("Unknown cortex strategy kind: " + kind);
            }
            strategy.rationale = requireMember(item, "rationale", JsonKind::String).stringValue;
            requireLength(strategy.rationale, limits.maxRationaleBytes, "strategy.rationale");
            strategy.estimatedRisk = normalizedNumber(
                requireMember(item, "estimated_risk", JsonKind::Number), "estimated_risk");
            strategy.estimatedBenefit = normalizedNumber(
                requireMember(item, "estimated_benefit", JsonKind::Number), "estimated_benefit");
            strategy.confidence = normalizedNumber(
                requireMember(item, "confidence", JsonKind::Number), "confidence");
            if (const auto* capabilities = optionalMember(item, "required_capabilities", JsonKind::Array)) {
                if (capabilities->arrayValue.size() > 16U) {
                    throw std::runtime_error("Strategy contains too many required capabilities");
                }
                for (const auto& capability : capabilities->arrayValue) {
                    if (capability.kind != JsonKind::String) {
                        throw std::runtime_error("required_capabilities must contain strings");
                    }
                    requireLength(capability.stringValue, limits.maxCapabilityBytes, "required_capabilities");
                    strategy.requiredCapabilities.push_back(capability.stringValue);
                }
            }
            response.strategies.push_back(std::move(strategy));
        }
    }

    if (const auto* information = optionalMember(root, "information_requests", JsonKind::Array)) {
        if (information->arrayValue.size() > limits.maxInformationRequests) {
            throw std::runtime_error("Cortex response contains too many information requests");
        }
        for (const auto& item : information->arrayValue) {
            if (item.kind != JsonKind::Object) throw std::runtime_error("Information request must be an object");
            CortexInformationRequest requestItem;
            requestItem.capability = requireMember(item, "capability", JsonKind::String).stringValue;
            requestItem.reason = requireMember(item, "reason", JsonKind::String).stringValue;
            requireLength(requestItem.capability, limits.maxCapabilityBytes, "information_request.capability");
            requireLength(requestItem.reason, limits.maxRationaleBytes, "information_request.reason");
            response.informationRequests.push_back(std::move(requestItem));
        }
    }

    if (const auto* imagination = optionalMember(root, "imagination", JsonKind::Object)) {
        ImaginationDirective directive;
        directive.mode = requireMember(*imagination, "mode", JsonKind::String).stringValue;
        if (directive.mode != "VISUAL_RECALL"
            && directive.mode != "SYMBOL_RECALL"
            && directive.mode != "COMPOSE"
            && directive.mode != "FREE_IMAGINATION") {
            throw std::runtime_error("Unknown imagination directive mode: " + directive.mode);
        }
        directive.conceptText = requireMember(*imagination, "concept", JsonKind::String).stringValue;
        requireLength(directive.mode, 64U, "imagination.mode");
        requireLength(directive.conceptText, limits.maxRationaleBytes, "imagination.concept");
        if (const auto* symbols = optionalMember(*imagination, "symbols", JsonKind::Array)) {
            if (symbols->arrayValue.size() > 32U) throw std::runtime_error("Too many imagination symbols");
            for (const auto& symbol : symbols->arrayValue) {
                if (symbol.kind != JsonKind::String) throw std::runtime_error("Imagination symbols must be strings");
                requireLength(symbol.stringValue, 96U, "imagination.symbol");
                directive.symbols.push_back(symbol.stringValue);
            }
        }
        response.imagination = std::move(directive);
    }

    if (const auto* planValue = optionalMember(root, "plan", JsonKind::Object)) {
        CortexPlanProposal plan;
        plan.id = requireMember(*planValue, "id", JsonKind::String).stringValue;
        requireLength(plan.id, 128U, "plan.id");
        const auto& steps = requireMember(*planValue, "steps", JsonKind::Array);
        if (steps.arrayValue.empty() || steps.arrayValue.size() > 16U) {
            throw std::runtime_error("Cortex plan must contain 1..16 steps");
        }
        for (const auto& item : steps.arrayValue) {
            if (item.kind != JsonKind::Object) throw std::runtime_error("Cortex plan step must be an object");
            CortexPlanStep step;
            step.id = requireMember(item, "id", JsonKind::String).stringValue;
            requireLength(step.id, 96U, "plan.step.id");
            const auto& kind = requireMember(item, "kind", JsonKind::String).stringValue;
            step.kind = cortexStrategyKindFromString(kind);
            if (step.kind == CortexStrategyKind::Unknown) {
                throw std::runtime_error("Unknown cortex plan strategy kind: " + kind);
            }
            step.objective = requireMember(item, "objective", JsonKind::String).stringValue;
            requireLength(step.objective, limits.maxRationaleBytes, "plan.step.objective");
            if (const auto* capabilities = optionalMember(item, "required_capabilities", JsonKind::Array)) {
                if (capabilities->arrayValue.size() > 16U) throw std::runtime_error("Cortex plan step has too many capabilities");
                for (const auto& capability : capabilities->arrayValue) {
                    if (capability.kind != JsonKind::String) throw std::runtime_error("plan required_capabilities must contain strings");
                    requireLength(capability.stringValue, limits.maxCapabilityBytes, "plan.required_capability");
                    step.requiredCapabilities.push_back(capability.stringValue);
                }
            }
            plan.steps.push_back(std::move(step));
        }
        response.plan = std::move(plan);
    }

    if (const auto* summary = optionalMember(root, "summary", JsonKind::String)) {
        response.summary = summary->stringValue;
        requireLength(response.summary, limits.maxSummaryBytes, "summary");
    }
    return response;
}

std::string extractChatContent(std::string_view json) {
    const JsonValue root = JsonParser(json).parse();
    const auto& choices = requireMember(root, "choices", JsonKind::Array);
    if (choices.arrayValue.empty()) throw std::runtime_error("LM Studio response contains no choices");
    const auto& first = choices.arrayValue.front();
    if (first.kind != JsonKind::Object) throw std::runtime_error("LM Studio choice is not an object");
    const auto& message = requireMember(first, "message", JsonKind::Object);
    return requireMember(message, "content", JsonKind::String).stringValue;
}

std::vector<std::string> extractModelIds(std::string_view json) {
    const JsonValue root = JsonParser(json).parse();
    const auto& data = requireMember(root, "data", JsonKind::Array);
    std::vector<std::string> result;
    result.reserve(data.arrayValue.size());
    for (const auto& item : data.arrayValue) {
        if (item.kind != JsonKind::Object) continue;
        const auto* id = item.member("id");
        if (id && id->kind == JsonKind::String && !id->stringValue.empty()) {
            result.push_back(id->stringValue);
        }
    }
    return result;
}

} // namespace tatarus::cortex::detail

namespace tatarus::cortex {

CortexConfig loadCortexConfig(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open Cortex config: " + path.string());
    const std::string json{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return detail::parseConfig(json);
}

std::string serializeCortexRequest(const CortexRequest& request) {
    return detail::serializeRequest(request);
}

CortexResponse parseCortexResponse(
    std::string_view json,
    const CortexRequest& request,
    const CortexLimits& limits) {
    return detail::parseResponse(json, request, limits);
}

} // namespace tatarus::cortex
