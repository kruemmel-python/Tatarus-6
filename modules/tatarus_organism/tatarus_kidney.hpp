#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus/throughput.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tatarus::organism {

struct NephronSegmentState {
    double volumeMl = 1.0;
    double naMm = 142.0;
    double kMm = 4.0;
    double caMm = 1.25;
    double clMm = 103.0;
    double hco3Mm = 24.0;
    double glucoseMm = 5.0;
    double ureaMm = 5.0;
    double osmolarityMOsm = 290.0;
};

struct KidneyConfig {
    double baselineKfMlPerMinMmHg = 9.5; // Glomerular ultrafiltration coefficient
    double baselineRbfMlPerMin = 1200.0;  // Renal blood flow
    double bowmanSpacePressureMmHg = 15.0;
    double glucoseTransportMaxMmolPerMin = 2.1; // ~375 mg/min
    // Fraction of the complete bilateral renal mass represented by this instance.
    // A single anatomical kidney normally represents 0.5.
    double functionalMassFraction = 1.0;
};

class Kidney {
public:
    explicit Kidney(KidneyConfig config = {});
    ~Kidney() = default;
    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { throughput_ = counters; }

    // Step nephron processes over dtSeconds
    // Outputs exact mass quantities excreted in urine to subtract from systemic blood
    void step(
        double dtSeconds,
        double meanArterialPressureMmHg,
        const SoluteProfile& plasmaSolutes,
        const EndocrineProfile& hormones,
        double& outWaterExcretedMl,
        double& outNaExcretedMmol,
        double& outKExcretedMmol,
        double& outCaExcretedMmol,
        double& outClExcretedMmol,
        double& outHco3ExcretedMmol,
        double& outUreaExcretedMmol,
        double& outGlucoseExcretedMmol);

    [[nodiscard]] const KidneyTelemetry& telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] double gfrMlPerMin() const noexcept { return telemetry_.glomerularFiltrationRateMlPerMin; }
    [[nodiscard]] double urineOutputRateMlPerMin() const noexcept { return telemetry_.urineOutputRateMlPerMin; }
    [[nodiscard]] double reninSecretionRate() const noexcept { return telemetry_.reninSecretionRate; }
    [[nodiscard]] double functionalMassFraction() const noexcept { return config_.functionalMassFraction; }
    void setFunctionalMassFraction(double fraction) noexcept;

private:
    KidneyConfig config_;
    KidneyTelemetry telemetry_;
    tatarus::ThroughputCounters* throughput_ = nullptr;

    // Sequential stateful nephron segments
    NephronSegmentState bowmanSpace_;
    NephronSegmentState proximalTubule_;
    NephronSegmentState henleDescending_;
    NephronSegmentState henleAscending_;
    NephronSegmentState distalTubule_;
    NephronSegmentState collectingDuct_;

    // Medullary interstitium osmotic gradient (mOsm/kg H2O)
    double medullaryOsmolarityMOsmPerKg_ = 1200.0;

    // Autoregulation & JGA states
    double maculaDensaNaDeliveryMmolPerMin_ = 1.8;
    double afferentArteriolarResistance_ = 0.035;
    double efferentArteriolarResistance_ = 0.020;

    void updateAutoregulationAndGFR(
        double meanArterialPressureMmHg,
        const SoluteProfile& plasmaSolutes,
        const EndocrineProfile& hormones);

    void updateTubularTransitAndReabsorption(
        double dtSeconds,
        const SoluteProfile& plasmaSolutes,
        const EndocrineProfile& hormones,
        double& outWaterExcretedMl,
        double& outNaExcretedMmol,
        double& outKExcretedMmol,
        double& outCaExcretedMmol,
        double& outClExcretedMmol,
        double& outHco3ExcretedMmol,
        double& outUreaExcretedMmol,
        double& outGlucoseExcretedMmol);
};

// Two stateful, independently degradable kidneys whose public telemetry is the
// mass-conserving sum/mean of the left and right organs.
class BilateralKidneys {
public:
    explicit BilateralKidneys(KidneyConfig combinedConfig = {});

    void step(
        double dtSeconds,
        double meanArterialPressureMmHg,
        const SoluteProfile& plasmaSolutes,
        const EndocrineProfile& hormones,
        double& outWaterExcretedMl,
        double& outNaExcretedMmol,
        double& outKExcretedMmol,
        double& outCaExcretedMmol,
        double& outClExcretedMmol,
        double& outHco3ExcretedMmol,
        double& outUreaExcretedMmol,
        double& outGlucoseExcretedMmol);

    void setThroughputCounters(tatarus::ThroughputCounters* counters) noexcept { left_.setThroughputCounters(counters); right_.setThroughputCounters(counters); throughput_ = counters; }
    [[nodiscard]] Kidney& left() noexcept { return left_; }
    [[nodiscard]] const Kidney& left() const noexcept { return left_; }
    [[nodiscard]] Kidney& right() noexcept { return right_; }
    [[nodiscard]] const Kidney& right() const noexcept { return right_; }
    [[nodiscard]] const KidneyTelemetry& telemetry() const noexcept { return aggregate_; }
    [[nodiscard]] double gfrMlPerMin() const noexcept { return aggregate_.glomerularFiltrationRateMlPerMin; }
    [[nodiscard]] double urineOutputRateMlPerMin() const noexcept { return aggregate_.urineOutputRateMlPerMin; }
    [[nodiscard]] double reninSecretionRate() const noexcept { return aggregate_.reninSecretionRate; }

    // Relative function is expressed against each organ's normal half of the
    // total renal mass: 1.0/1.0 = two healthy kidneys, 0.0/1.0 = nephrectomy.
    void setRelativeFunction(double leftFraction, double rightFraction) noexcept;

private:
    Kidney left_;
    Kidney right_;
    KidneyTelemetry aggregate_;
    tatarus::ThroughputCounters* throughput_ = nullptr;

    void updateAggregateTelemetry() noexcept;
};

} // namespace tatarus::organism
