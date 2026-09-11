#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus/throughput.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tatarus::organism {

enum class SubstanceId : std::uint8_t {
    Water,       // Liters
    O2,          // Moles
    CO2,         // Moles
    Glucose,     // Moles
    Na,          // Moles
    K,           // Moles
    Ca,          // Moles
    Cl,          // Moles
    HCO3,        // Moles
    H,           // Moles
    Lactate,     // Moles
    Urea,        // Moles
    Count
};

constexpr std::size_t kSubstanceCount = static_cast<std::size_t>(SubstanceId::Count);

const char* getSubstanceName(SubstanceId id);

struct SubstanceAmounts {
    std::array<double, kSubstanceCount> amounts{};

    double& operator[](SubstanceId id) {
        return amounts[static_cast<std::size_t>(id)];
    }
    double operator[](SubstanceId id) const {
        return amounts[static_cast<std::size_t>(id)];
    }

    SubstanceAmounts& operator+=(const SubstanceAmounts& other) {
        for (std::size_t i = 0; i < kSubstanceCount; ++i) {
            amounts[i] += other.amounts[i];
        }
        return *this;
    }
    SubstanceAmounts& operator-=(const SubstanceAmounts& other) {
        for (std::size_t i = 0; i < kSubstanceCount; ++i) {
            amounts[i] -= other.amounts[i];
        }
        return *this;
    }
};

struct ConservationAudit {
    bool balanced = true;
    double maxDiscrepancy = 0.0;
    std::array<double, kSubstanceCount> initialAmounts{};
    std::array<double, kSubstanceCount> currentAmounts{};
    std::array<double, kSubstanceCount> cumulativeInputs{};
    std::array<double, kSubstanceCount> cumulativeOutputs{};
    std::array<double, kSubstanceCount> cumulativeConsumed{};
    std::array<double, kSubstanceCount> cumulativeProduced{};
    std::array<double, kSubstanceCount> cumulativeTransformed{};
    std::array<double, kSubstanceCount> discrepancies{};
};

class ConservationLedger {
public:
    ConservationLedger();
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    void recordInitialInventory(const SubstanceAmounts& initial);
    void recordIntake(SubstanceId id, double amount);
    void recordExcretion(SubstanceId id, double amount);
    void recordConsumption(SubstanceId id, double amount);
    void recordProduction(SubstanceId id, double amount);
    void recordChemicalTransformation(SubstanceId from, SubstanceId to, double amount, double stoichiometryRatio = 1.0);

    ConservationAudit audit(const SubstanceAmounts& currentInventory, double tolerance = 1e-4) const;

    const SubstanceAmounts& getCumulativeIntake() const { return m_cumulativeIntake; }
    const SubstanceAmounts& getCumulativeExcretion() const { return m_cumulativeExcretion; }
    const SubstanceAmounts& getCumulativeConsumed() const { return m_cumulativeConsumed; }
    const SubstanceAmounts& getCumulativeProduced() const { return m_cumulativeProduced; }

private:
    tatarus::ThroughputCounters* throughput_ = nullptr;
    SubstanceAmounts m_initialInventory{};
    SubstanceAmounts m_cumulativeIntake{};
    SubstanceAmounts m_cumulativeExcretion{};
    SubstanceAmounts m_cumulativeConsumed{};
    SubstanceAmounts m_cumulativeProduced{};
    SubstanceAmounts m_cumulativeTransformed{};
};

} // namespace tatarus::organism
