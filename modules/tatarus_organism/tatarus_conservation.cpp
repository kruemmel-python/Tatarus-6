#include "tatarus_conservation.hpp"
#include <cmath>
#include <algorithm>

namespace tatarus::organism {

const char* getSubstanceName(SubstanceId id) {
    switch (id) {
        case SubstanceId::Water: return "Water";
        case SubstanceId::O2: return "O2";
        case SubstanceId::CO2: return "CO2";
        case SubstanceId::Glucose: return "Glucose";
        case SubstanceId::Na: return "Na+";
        case SubstanceId::K: return "K+";
        case SubstanceId::Ca: return "Ca2+";
        case SubstanceId::Cl: return "Cl-";
        case SubstanceId::HCO3: return "HCO3-";
        case SubstanceId::H: return "H+";
        case SubstanceId::Lactate: return "Lactate";
        case SubstanceId::Urea: return "Urea";
        default: return "Unknown";
    }
}

ConservationLedger::ConservationLedger() = default;

void ConservationLedger::recordInitialInventory(const SubstanceAmounts& initial) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    m_initialInventory = initial;
}

void ConservationLedger::recordIntake(SubstanceId id, double amount) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    if (amount > 0.0) {
        m_cumulativeIntake[id] += amount;
    }
}

void ConservationLedger::recordExcretion(SubstanceId id, double amount) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    if (amount > 0.0) {
        m_cumulativeExcretion[id] += amount;
    }
}

void ConservationLedger::recordConsumption(SubstanceId id, double amount) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    if (amount > 0.0) {
        m_cumulativeConsumed[id] += amount;
    }
}

void ConservationLedger::recordProduction(SubstanceId id, double amount) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    if (amount > 0.0) {
        m_cumulativeProduced[id] += amount;
    }
}

void ConservationLedger::recordChemicalTransformation(SubstanceId from, SubstanceId to, double amount, double stoichiometryRatio) {
    if (throughput_) throughput_->recordReadWrite<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    if (amount > 0.0) {
        m_cumulativeTransformed[from] -= amount;
        m_cumulativeTransformed[to] += (amount * stoichiometryRatio);
    }
}

ConservationAudit ConservationLedger::audit(const SubstanceAmounts& currentInventory, double tolerance) const {
    if (throughput_) throughput_->recordRead<ConservationLedger>(tatarus::ThroughputDomain::Conservation);
    ConservationAudit result;
    result.balanced = true;
    result.maxDiscrepancy = 0.0;

    for (std::size_t i = 0; i < kSubstanceCount; ++i) {
        const auto id = static_cast<SubstanceId>(i);
        result.initialAmounts[i] = m_initialInventory[id];
        result.currentAmounts[i] = currentInventory[id];
        result.cumulativeInputs[i] = m_cumulativeIntake[id];
        result.cumulativeOutputs[i] = m_cumulativeExcretion[id];
        result.cumulativeConsumed[i] = m_cumulativeConsumed[id];
        result.cumulativeProduced[i] = m_cumulativeProduced[id];
        result.cumulativeTransformed[i] = m_cumulativeTransformed[id];

        // Theoretical balance: M(t) = M(0) + Input - Output - Consumed + Produced + Transformed
        const double expected = result.initialAmounts[i] + result.cumulativeInputs[i] - result.cumulativeOutputs[i]
                              - result.cumulativeConsumed[i] + result.cumulativeProduced[i] + result.cumulativeTransformed[i];
        const double discrepancy = std::abs(result.currentAmounts[i] - expected);
        result.discrepancies[i] = discrepancy;

        if (discrepancy > result.maxDiscrepancy) {
            result.maxDiscrepancy = discrepancy;
        }

        // Relative or absolute tolerance check
        const double scale = std::max(1.0, std::abs(expected));
        if (discrepancy / scale > tolerance) {
            result.balanced = false;
        }
    }

    return result;
}

} // namespace tatarus::organism
