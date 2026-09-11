#pragma once

#include "tatarus_organism_types.hpp"
#include "tatarus_conservation.hpp"
#include "tatarus_circulation.hpp"
#include "tatarus_heart.hpp"
#include "tatarus_lung.hpp"
#include "tatarus_kidney.hpp"

#include "tatarus/robot_mind.hpp"
#include "tatarus/types.hpp"

#include <memory>
#include <filesystem>
#include <string>

namespace tatarus::organism {

struct OrganismConfig {
    CirculationConfig circulation;
    HeartConfig heart;
    LungConfig lung;
    KidneyConfig kidney;
    RobotMindConfig mind;
    double basalMetabolicRateWatts = 80.0;
    double mechanicalToMetabolicEfficiency = 0.25; // ~25% muscular efficiency
};

class SyntheticOrganism {
public:
    explicit SyntheticOrganism(OrganismConfig config = {});
    ~SyntheticOrganism() = default;

    SyntheticOrganism(const SyntheticOrganism&) = delete;
    SyntheticOrganism& operator=(const SyntheticOrganism&) = delete;
    SyntheticOrganism(SyntheticOrganism&& other) noexcept;
    SyntheticOrganism& operator=(SyntheticOrganism&& other) noexcept;

    // Advance the full coupled organism with embodied experience and external atmosphere
    ObserveResult step(
        const Experience& experience,
        const AtmosphericEnvironment& atmosphere,
        double dtSeconds = 0.05);

    // Advance with explicit physical robot loading
    ObserveResult stepWithLoad(
        const Experience& experience,
        const AtmosphericEnvironment& atmosphere,
        const RobotPhysicalLoad& load,
        double dtSeconds = 0.05);

    // Coupled body + nervous-system + 3-D scanner/cartography step.
    ExplorerResult stepExplorer(
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const AtmosphericEnvironment& atmosphere,
        double dtSeconds = 0.05);
    ExplorerResult stepExplorerWithLoad(
        const Experience& experience,
        const ScannerFrame& scannerFrame,
        const AtmosphericEnvironment& atmosphere,
        const RobotPhysicalLoad& load,
        double dtSeconds = 0.05);

    // Organ accessors
    [[nodiscard]] OrganismConfig config() const { return config_; }

    [[nodiscard]] RobotMind& mind() noexcept { return mind_; }
    [[nodiscard]] const RobotMind& mind() const noexcept { return mind_; }

    [[nodiscard]] Circulation& circulation() noexcept { return circulation_; }
    [[nodiscard]] const Circulation& circulation() const noexcept { return circulation_; }

    [[nodiscard]] Heart& heart() noexcept { return heart_; }
    [[nodiscard]] const Heart& heart() const noexcept { return heart_; }

    [[nodiscard]] Lung& lung() noexcept { return lung_; }
    [[nodiscard]] const Lung& lung() const noexcept { return lung_; }

    [[nodiscard]] BilateralKidneys& kidneys() noexcept { return kidneys_; }
    [[nodiscard]] const BilateralKidneys& kidneys() const noexcept { return kidneys_; }
    [[nodiscard]] Kidney& leftKidney() noexcept { return kidneys_.left(); }
    [[nodiscard]] const Kidney& leftKidney() const noexcept { return kidneys_.left(); }
    [[nodiscard]] Kidney& rightKidney() noexcept { return kidneys_.right(); }
    [[nodiscard]] const Kidney& rightKidney() const noexcept { return kidneys_.right(); }

    [[nodiscard]] const ConservationLedger& ledger() const noexcept { return ledger_; }

    // Telemetry and Inspection
    [[nodiscard]] OrganismTelemetry telemetry() const;
    [[nodiscard]] InteroceptionState interoceptionState() const;
    [[nodiscard]] std::string organismJson() const;
    [[nodiscard]] tatarus::ThroughputCounters throughputCounters() const;
    void resetThroughputCounters();
    [[nodiscard]] std::string throughputJson() const;

    // Conservation audit
    [[nodiscard]] ConservationAudit auditConservation(double tolerance = 1e-4) const;

    // Snapshot serialization & restore
    [[nodiscard]] std::string serializeState() const;
    bool deserializeState(const std::string& serialized);
    void saveSnapshot(const std::filesystem::path& directory) const;
    bool loadSnapshot(const std::filesystem::path& directory);

    // Clinical / Experimental Interventions on the Organism
    void infuseFluid(double volumeMl, const SoluteProfile& solutes);
    void hemorrhage(double volumeMl);
    void setAdrenergicStimulation(double sympatheticOverride);
    void setRenalFunction(double leftFraction, double rightFraction);
    void setExperimentalIntervention(const ExperimentalIntervention& intervention);
    void applyExperimentalDamage(double neuronFraction, double synapseFraction, std::uint64_t seed);

private:
    OrganismConfig config_;
    RobotMind mind_;
    Circulation circulation_;
    Heart heart_;
    Lung lung_;
    BilateralKidneys kidneys_;
    ConservationLedger ledger_;

    std::uint64_t stepCount_ = 0;
    double totalSimulatedTimeS_ = 0.0;

    // Autonomic tone states
    double sympatheticTone_ = 0.25;
    double parasympatheticTone_ = 0.75;
    double baroreceptorAfferent_ = 0.50;

    // Interoception telemetry cache
    InteroceptionTelemetry interoception_;
    ExperimentalIntervention experimentalIntervention_{};
    mutable tatarus::ThroughputCounters organismThroughput_{};

    void bindThroughputCounters() noexcept;

    ExplorerResult stepWithLoadImpl(
        const Experience& experience,
        const ScannerFrame* scannerFrame,
        const AtmosphericEnvironment& atmosphere,
        const RobotPhysicalLoad& load,
        double dtSeconds);

    void updateAutonomicsAndInteroception(
        const BiologicalTelemetry& brainBio,
        const PhysiologyTelemetry& brainPhys,
        double mapMmHg);
};

} // namespace tatarus::organism
