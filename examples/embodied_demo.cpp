#include <tatarus/sdk.hpp>

#include <iostream>

int main() {
    tatarus::RobotMind mind;

    tatarus::ActionEvent move{
        .id = 1001,
        .label = "move_to_dock",
        .intensity = 0.8,
    };

    for (std::uint64_t cycle = 0; cycle < 15; ++cycle) {
        tatarus::Experience corridor;
        corridor.timestampNs = cycle * 10'000'000ULL + 1;
        corridor.vision = {-0.6, 0.1, 0.2};
        corridor.imu.acceleration = {0.2, 0.0, 9.81};
        corridor.environment.light = 0.3;
        corridor.environment.soundLevel = 0.25;
        corridor.body.battery = 0.4;
        mind.observe(corridor);

        mind.beginAction(move);

        tatarus::Experience dock;
        dock.timestampNs = cycle * 10'000'000ULL + 2;
        dock.vision = {0.8, 0.7, 0.9};
        dock.imu.acceleration = {0.05, 0.0, 9.81};
        dock.environment.light = 0.8;
        dock.environment.soundLevel = 0.12;
        dock.body.battery = 0.4;

        const auto result = mind.observe(dock);
        mind.endAction({
            .id = move.id,
            .reward = 0.8,
            .success = 1.0,
            .novelty = 0.05,
        });

        std::cout
            << "cycle=" << cycle
            << " assembly=" << result.assemblyId
            << " neural_transitions=" << result.metrics.learnedTransitions
            << " action_edges=" << result.metrics.actionConditionedTransitions
            << " brier=" << result.metrics.cumulativeBrier
            << '\n';
    }

    const auto context = mind.context();
    std::cout << "\nCurrent assembly: " << context.currentAssemblyId << '\n';
    std::cout
        << "Predicted next: " << context.prediction.expectedAssemblyId
        << " confidence=" << context.prediction.confidence
        << '\n';

    std::cout
        << "TATARUS neurobiology: dendrites=" << context.biology.dendriticSegments
        << " astro=" << context.biology.astrocytes
        << " oligo=" << context.biology.oligodendrocytes
        << " micro=" << context.biology.microglia
        << " myelin=" << context.biology.myelinCoverage
        << " delay_ms=" << context.biology.effectiveDelayMs
        << '\n';

    std::cout
        << "TATARUS Physiology: K=" << context.physiology.extracellularKMm
        << "mM ATP=" << context.physiology.atp
        << " pump=" << context.physiology.pumpActivity
        << " CREB=" << context.physiology.crebActivation
        << " tagged=" << context.physiology.taggedSynapses
        << " gamma=" << context.physiology.gammaPower
        << '\n';

    std::cout
        << "Intrinsic prospection: transitions=" << context.prospection.learnedTransitions
        << " confidence=" << context.prospection.predictionConfidence
        << " error=" << context.prospection.predictionError
        << " familiarity=" << context.prospection.sequenceFamiliarity
        << '\n';

    return 0;
}
