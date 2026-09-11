#include "tatarus/imaginatio_cortex.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace tatarus::cortex {
namespace {

[[nodiscard]] std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

[[nodiscard]] std::optional<std::string> canonicalName(
    const std::vector<std::string>& available,
    const std::string& requested) {
    const std::string target = upper(requested);
    for (const auto& item : available) {
        if (upper(item) == target) return item;
    }
    return std::nullopt;
}

[[nodiscard]] bool containsExplicitName(
    std::string_view requestGoal,
    std::string_view knownName) {
    const std::string goal = upper(std::string(requestGoal));
    const std::string name = upper(std::string(knownName));
    std::size_t position = goal.find(name);
    while (position != std::string::npos) {
        const auto isNameCharacter = [](unsigned char value) {
            return std::isalnum(value) != 0 || value == '_';
        };
        const bool startsAtBoundary = position == 0U
            || !isNameCharacter(static_cast<unsigned char>(goal[position - 1U]));
        const std::size_t end = position + name.size();
        const bool endsAtBoundary = end == goal.size()
            || !isNameCharacter(static_cast<unsigned char>(goal[end]));
        if (startsAtBoundary && endsAtBoundary) return true;
        position = goal.find(name, position + 1U);
    }
    return false;
}

[[nodiscard]] ImaginatioDirectiveKind directiveKindFromMode(
    std::string_view mode) noexcept {
    if (mode == "VISUAL_RECALL") return ImaginatioDirectiveKind::VisualRecall;
    if (mode == "SYMBOL_RECALL") return ImaginatioDirectiveKind::SymbolRecall;
    if (mode == "COMPOSE") return ImaginatioDirectiveKind::Compose;
    if (mode == "FREE_IMAGINATION") return ImaginatioDirectiveKind::FreeImagination;
    return ImaginatioDirectiveKind::None;
}

[[nodiscard]] ImaginatioDirectiveKind directiveKindFromStrategy(
    CortexStrategyKind kind) noexcept {
    switch (kind) {
        case CortexStrategyKind::VisualRecall: return ImaginatioDirectiveKind::VisualRecall;
        case CortexStrategyKind::SymbolRecall: return ImaginatioDirectiveKind::SymbolRecall;
        case CortexStrategyKind::Compose: return ImaginatioDirectiveKind::Compose;
        case CortexStrategyKind::FreeImagination: return ImaginatioDirectiveKind::FreeImagination;
        default: return ImaginatioDirectiveKind::None;
    }
}

} // namespace

const char* toString(ImaginatioDirectiveKind value) noexcept {
    switch (value) {
        case ImaginatioDirectiveKind::None: return "NONE";
        case ImaginatioDirectiveKind::VisualRecall: return "VISUAL_RECALL";
        case ImaginatioDirectiveKind::SymbolRecall: return "SYMBOL_RECALL";
        case ImaginatioDirectiveKind::Compose: return "COMPOSE";
        case ImaginatioDirectiveKind::FreeImagination: return "FREE_IMAGINATION";
    }
    return "NONE";
}

std::vector<std::string> ImaginatioCortexAdapter::capabilities() {
    return {
        "STATE_ANALYSIS",
        "USE_IMAGINATION",
        "VISUAL_RECALL",
        "SYMBOL_RECALL",
        "COMPOSE",
        "FREE_IMAGINATION",
    };
}

CortexImaginationState ImaginatioCortexAdapter::imaginationState(
    const VisualImagination& imagination) const {
    CortexImaginationState state;
    state.available = true;
    state.stage = static_cast<std::uint8_t>(imagination.lastReport().stage);
    state.visualEngrams = imagination.visualEngramCount();
    state.symbolEngrams = imagination.symbolEngramCount();
    state.lastSimilarity = imagination.lastReport().similarity;
    state.lastNovelty = imagination.lastReport().novelty;
    state.knownConcepts = imagination.visualConceptNames();
    state.knownSymbols = imagination.symbolNames();
    state.knownCategories = imagination.categoryNames();
    return state;
}

ImaginatioDirective ImaginatioCortexAdapter::translate(
    const CortexDecision& decision,
    const CortexResponse& response,
    const VisualImagination& imagination,
    std::string_view requestGoal) const {
    ImaginatioDirective result;
    result.reason = decision.reason;

    if (decision.kind != CortexDecisionKind::SendToImagination
        || !decision.selectedStrategy.has_value()) {
        result.reason = "arbiter did not authorize an imagination strategy";
        return result;
    }

    const auto& strategy = *decision.selectedStrategy;
    result.sourceStrategy = strategy.kind;
    result.strategyId = strategy.id;

    ImaginatioDirectiveKind kind = directiveKindFromStrategy(strategy.kind);
    if (strategy.kind == CortexStrategyKind::UseImagination) {
        if (!response.imagination.has_value()) {
            result.reason = "generic USE_IMAGINATION requires a structured imagination directive";
            return result;
        }
        kind = directiveKindFromMode(response.imagination->mode);
    }
    if (kind == ImaginatioDirectiveKind::None) {
        result.reason = "selected cortex strategy is not executable by IMAGINATIO";
        return result;
    }

    if (response.imagination.has_value()) {
        const auto explicitKind = directiveKindFromMode(response.imagination->mode);
        if (explicitKind != ImaginatioDirectiveKind::None
            && strategy.kind != CortexStrategyKind::UseImagination
            && explicitKind != kind) {
            result.reason = "imagination directive mode conflicts with the arbiter-selected strategy";
            return result;
        }
        result.conceptText = response.imagination->conceptText;
        result.symbols = response.imagination->symbols;
    }

    const auto concepts = imagination.visualConceptNames();
    const auto symbols = imagination.symbolNames();

    // If a person explicitly names two or more learned symbols, their request
    // is stronger grounding than a compact language model's tendency to copy
    // the complete known-symbol list into the directive. Abstract goals that
    // contain no literal symbol names remain semantically open to the Cortex.
    std::vector<std::string> explicitlyRequestedSymbols;
    for (const auto& symbol : symbols) {
        if (containsExplicitName(requestGoal, symbol)) {
            explicitlyRequestedSymbols.push_back(symbol);
        }
    }
    const std::string upperGoal = upper(std::string(requestGoal));
    std::stable_sort(
        explicitlyRequestedSymbols.begin(), explicitlyRequestedSymbols.end(),
        [&upperGoal](const std::string& left, const std::string& right) {
            return upperGoal.find(upper(left)) < upperGoal.find(upper(right));
        });
    if (explicitlyRequestedSymbols.size() >= 2U) {
        if (kind != ImaginatioDirectiveKind::Compose) {
            result.reason = "an explicit multi-symbol request requires COMPOSE";
            return result;
        }
        result.symbols = std::move(explicitlyRequestedSymbols);
    }

    result.kind = kind;
    switch (kind) {
        case ImaginatioDirectiveKind::VisualRecall: {
            if (concepts.empty()) {
                result.reason = "VISUAL_RECALL requires a learned visual concept";
                return result;
            }
            if (result.conceptText.empty()) {
                if (concepts.size() == 1U) result.conceptText = concepts.front();
                else {
                    result.reason = "VISUAL_RECALL is ambiguous without a known concept label";
                    return result;
                }
            }
            const auto canonical = canonicalName(concepts, result.conceptText);
            if (!canonical.has_value()) {
                result.reason = "VISUAL_RECALL concept is not present in TATARUS visual memory";
                return result;
            }
            result.conceptText = *canonical;
            break;
        }
        case ImaginatioDirectiveKind::SymbolRecall: {
            if (result.symbols.size() != 1U) {
                result.reason = "SYMBOL_RECALL requires exactly one learned symbol";
                return result;
            }
            const auto canonical = canonicalName(symbols, result.symbols.front());
            if (!canonical.has_value()) {
                result.reason = "SYMBOL_RECALL symbol is not present in TATARUS symbol memory";
                return result;
            }
            result.symbols = {*canonical};
            break;
        }
        case ImaginatioDirectiveKind::Compose: {
            if (result.symbols.size() < 2U) {
                result.reason = "COMPOSE requires at least two learned symbols";
                return result;
            }
            std::vector<std::string> canonicalSymbols;
            std::unordered_set<std::string> seen;
            for (const auto& requested : result.symbols) {
                const auto canonical = canonicalName(symbols, requested);
                if (!canonical.has_value()) {
                    result.reason = "COMPOSE references a symbol not present in TATARUS memory";
                    return result;
                }
                if (seen.insert(*canonical).second) canonicalSymbols.push_back(*canonical);
            }
            if (canonicalSymbols.size() < 2U) {
                result.reason = "COMPOSE requires two distinct learned symbols";
                return result;
            }
            result.symbols = std::move(canonicalSymbols);
            break;
        }
        case ImaginatioDirectiveKind::FreeImagination:
            if (imagination.visualEngramCount() == 0U) {
                result.reason = "FREE_IMAGINATION requires at least one learned visual engram";
                return result;
            }
            // An unknown semantic concept is never converted into pixels by the
            // language model. It simply falls back to TATARUS' strongest visual
            // engram; a known label can restrict that existing memory recall.
            if (!result.conceptText.empty()) {
                if (const auto canonical = canonicalName(concepts, result.conceptText)) {
                    result.conceptText = *canonical;
                } else {
                    result.conceptText.clear();
                }
            }
            break;
        case ImaginatioDirectiveKind::None:
            return result;
    }

    result.executable = true;
    result.reason = "bounded cortex directive resolved entirely against TATARUS IMAGINATIO memory";
    return result;
}

ImaginatioExecutionResult ImaginatioCortexAdapter::execute(
    const ImaginatioDirective& directive,
    VisualImagination& imagination) const {
    ImaginatioExecutionResult result;
    result.directive = directive;
    if (!directive.executable) {
        result.error = directive.reason.empty() ? "imagination directive is not executable" : directive.reason;
        return result;
    }

    try {
        switch (directive.kind) {
            case ImaginatioDirectiveKind::VisualRecall:
                result.report = imagination.drawFreely(directive.conceptText);
                break;
            case ImaginatioDirectiveKind::SymbolRecall:
                result.report = imagination.drawFromSymbol(directive.symbols.at(0));
                break;
            case ImaginatioDirectiveKind::Compose:
                result.report = imagination.compose(directive.symbols);
                break;
            case ImaginatioDirectiveKind::FreeImagination:
                result.report = imagination.drawFreely(directive.conceptText);
                break;
            case ImaginatioDirectiveKind::None:
                result.error = "NONE imagination directive cannot execute";
                return result;
        }
        result.executed = result.report.has_value() && result.report->success;
        if (!result.executed && result.error.empty()) {
            result.error = "IMAGINATIO executed the bounded directive but did not report success";
        }
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

} // namespace tatarus::cortex
