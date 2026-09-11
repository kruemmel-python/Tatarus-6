#pragma once

#include "tatarus/cortex_config.hpp"
#include "tatarus/cortex_types.hpp"

#include <cstdint>

namespace tatarus::cortex {

enum class CortexReliabilityState : std::uint8_t {
    Nominal,
    Cautious,
    Degraded,
};

struct CortexMetacognitionSnapshot {
    CortexReliabilityState state = CortexReliabilityState::Nominal;
    std::uint64_t responses = 0;
    std::uint64_t transportErrors = 0;
    std::uint64_t staleResponses = 0;
    std::uint64_t acceptedDecisions = 0;
    std::uint64_t rejectedDecisions = 0;
    std::uint64_t realAssistedOutcomes = 0;
    std::uint64_t realAssistedSuccesses = 0;
    double transportReliability = 1.0;
    double decisionAcceptanceRate = 1.0;
    double assistedSuccessRate = 1.0;
    double staleRate = 0.0;
    double aggregateReliability = 1.0;
};

class CortexMetacognition {
public:
    explicit CortexMetacognition(CortexMetacognitionConfig config = {});

    void recordTransport(bool success);
    void recordStaleResponse();
    void recordDecision(const CortexDecision& decision);
    void recordRealOutcome(bool cortexUsed, bool success);

    [[nodiscard]] bool allowAutomaticConsultation(
        CortexTaskKind task,
        CortexTriggerReason reason) const;
    [[nodiscard]] CortexMetacognitionSnapshot snapshot() const;

private:
    void recompute();

    CortexMetacognitionConfig config_;
    CortexMetacognitionSnapshot state_;
};

[[nodiscard]] const char* toString(CortexReliabilityState value) noexcept;

} // namespace tatarus::cortex
