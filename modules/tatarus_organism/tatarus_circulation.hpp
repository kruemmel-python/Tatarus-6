#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus/throughput.hpp"
#include "tatarus_conservation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tatarus::organism {

struct CirculationConfig {
    double totalBloodVolumeL = 5.0;
    double baselineArterialComplianceMlPerMmHg = 1.5;
    double baselineVenousComplianceMlPerMmHg = 100.0;
    double baselinePeripheralResistanceMmHgSPerMl = 1.0;
    double hematocrit = 0.45;
    double hemoglobinGPerDl = 15.0;
};

class Circulation {
public:
    explicit Circulation(CirculationConfig config = {});
    ~Circulation() = default;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    // Advance hemodynamics, advective solute transport, and mass balance for dtSeconds
    void step(
        double dtSeconds,
        double cardiacOutputMlPerS,
        double rightVentricularOutputMlPerS,
        double cerebralDemandMlPerS,
        double renalDemandMlPerS,
        double coronaryDemandMlPerS,
        double peripheralDemandMlPerS,
        double sympatheticVasomotorTone,
        double angiotensinIIPm);

    // Strict mass-conserving solute exchange APIs:
    void exchangePulmonary(
        double o2AddedMmol,
        double co2RemovedMmol,
        double alveolarPo2MmHg,
        double alveolarPco2MmHg);
    void exchangeCerebral(
        double o2ConsumedMmol,
        double glucoseConsumedMmol,
        double co2ProducedMmol,
        double lactateProducedMmol,
        double naFluxMmol,
        double kFluxMmol,
        double caFluxMmol);
    void exchangeCoronary(
        double o2ConsumedMmol,
        double glucoseConsumedMmol,
        double co2ProducedMmol);
    void exchangePeripheral(
        double o2ConsumedMmol,
        double glucoseConsumedMmol,
        double co2ProducedMmol,
        double lactateProducedMmol,
        double heatAddedJoules);
    void exchangeRenal(
        double waterRemovedMl,
        double naRemovedMmol,
        double kRemovedMmol,
        double caRemovedMmol,
        double clRemovedMmol,
        double hco3RemovedMmol,
        double ureaRemovedMmol,
        double glucoseRemovedMmol);

    // Endocrine updates
    void updateHormones(
        double dtSeconds,
        double sympatheticDrive,
        double reninRate,
        double angIIRate,
        double aldosteroneRate,
        double adhRate);

    // Infusion / fluid balance intervention
    void infuseFluidMl(double volumeMl, const SoluteProfile& soluteConcentrations);
    void bleedFluidMl(double volumeMl);

    // Conservation inventory query
    [[nodiscard]] SubstanceAmounts totalSubstanceAmounts() const;

    // Accessors
    [[nodiscard]] const CirculationTelemetry& telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] const SoluteProfile& arterialSolutes() const noexcept { return arterialSolutes_; }
    [[nodiscard]] const SoluteProfile& venousSolutes() const noexcept { return venousSolutes_; }
    [[nodiscard]] const EndocrineProfile& endocrine() const noexcept { return endocrine_; }
    [[nodiscard]] double arterialPressureMmHg() const noexcept { return telemetry_.meanArterialPressureMmHg; }
    [[nodiscard]] double centralVenousPressureMmHg() const noexcept { return telemetry_.centralVenousPressureMmHg; }
    [[nodiscard]] double pulmonaryCapillaryPressureMmHg() const noexcept;
    [[nodiscard]] double bloodTemperatureC() const noexcept { return bloodTemperatureC_; }

private:
    CirculationConfig config_;
    CirculationTelemetry telemetry_;
    tatarus::ThroughputCounters* throughput_ = nullptr;
    SoluteProfile arterialSolutes_;
    SoluteProfile venousSolutes_;
    EndocrineProfile endocrine_;

    // Volumes in mL
    double arterialVolumeMl_ = 750.0;
    double venousVolumeMl_ = 3250.0;
    double pulmonaryArteryVolumeMl_ = 150.0;
    double pulmonaryCapillaryVolumeMl_ = 100.0;
    double pulmonaryVeinVolumeMl_ = 250.0;
    double microvascularVolumeMl_ = 500.0;

    double unstressedArterialVolumeMl_ = 610.0;
    double unstressedVenousVolumeMl_ = 2950.0;

    double bloodTemperatureC_ = 37.0;
    // 5 L blood × 1.06 kg/L × 3.85 J/(g*K) initialized at 37 °C.
    double totalHeatJoules_ = 37.0 * 5300.0 * 3.85;

    void recalculateBloodGasesAndPh();
    void recalculatePressures(double vasomotorTone, double angIIPm);
};

} // namespace tatarus::organism
