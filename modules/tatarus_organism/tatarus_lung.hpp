#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus/throughput.hpp"

#include <cstddef>
#include <cstdint>

namespace tatarus::organism {

struct LungConfig {
    double baselineRespiratoryRateBpm = 14.0;
    double baselineTidalVolumeL = 0.50;
    double anatomicalDeadSpaceL = 0.15;
    double functionalResidualCapacityL = 2.40;
    double totalLungCapacityL = 6.0;
    double lungComplianceLPerCmH2O = 0.10;
    double airwayResistanceCmH2OSPerL = 1.5;
    double dlO2MlPerMinMmHg = 30.0;   // Alveolar-capillary diffusion capacity for O2
    double dlCO2MlPerMinMmHg = 450.0; // Alveolar-capillary diffusion capacity for CO2
};

class Lung {
public:
    explicit Lung(LungConfig config = {});
    ~Lung() = default;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    // Step respiration and alveolar gas exchange over dtSeconds
    // Returns molar amounts of O2 added and CO2 removed to apply to blood circulation
    void step(
        double dtSeconds,
        const AtmosphericEnvironment& environment,
        double arterialPo2MmHg,
        double arterialPco2MmHg,
        double arterialPh,
        double mixedVenousPo2MmHg,
        double mixedVenousPco2MmHg,
        double metabolicCo2ProductionMlPerMin,
        double& outO2AddedMmol,
        double& outCo2RemovedMmol);

    [[nodiscard]] const LungTelemetry& telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] double minuteVentilationLPerMin() const noexcept { return telemetry_.minuteVentilationLPerMin; }
    [[nodiscard]] double alveolarPo2MmHg() const noexcept { return telemetry_.alveolarPo2MmHg; }
    [[nodiscard]] double alveolarPco2MmHg() const noexcept { return telemetry_.alveolarPco2MmHg; }
    [[nodiscard]] double o2UptakeRateMlPerMin() const noexcept { return telemetry_.o2UptakeRateMlPerMin; }
    [[nodiscard]] double co2EliminationRateMlPerMin() const noexcept { return telemetry_.co2EliminationRateMlPerMin; }

private:
    LungConfig config_;
    LungTelemetry telemetry_;
    tatarus::ThroughputCounters* throughput_ = nullptr;

    // Dynamic mechanical states
    double currentVolumeL_ = 2.40;
    double respiratoryPhaseTimeS_ = 0.0;
    double respiratoryPeriodS_ = 4.28; // ~14 bpm
    double dynamicVentilationDrive_ = 1.0;

    // Chemoreceptor states
    double centralChemoreceptorDrive_ = 1.0;
    double peripheralChemoreceptorDrive_ = 1.0;

    // Dust & Environmental filter load
    double accumulatedDustFilter_ = 0.0;

    // Multi-compartment V/Q distribution states
    double vqZone1Po2_ = 105.0; // High V/Q
    double vqZone2Po2_ = 100.0; // Matched V/Q
    double vqZone3Po2_ = 90.0;  // Low V/Q

    void updateChemoreceptors(
        double dtSeconds,
        double arterialPo2MmHg,
        double arterialPco2MmHg,
        double arterialPh);

    void updateMechanics(
        double dtSeconds,
        const AtmosphericEnvironment& environment);
};

} // namespace tatarus::organism
