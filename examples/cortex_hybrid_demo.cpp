#include "tatarus/cortex.hpp"
#include "tatarus/imaginatio_cortex.hpp"
#include "tatarus/imaginatio.hpp"
#include "tatarus/rover_cortex.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using namespace tatarus;
using namespace tatarus::cortex;

VisualCanvas house() {
    VisualCanvas canvas(32, 32);
    for (std::size_t y = 14; y < 27; ++y) {
        for (std::size_t x = 8; x < 24; ++x) canvas.setPixel(x, y, 0.8);
    }
    for (std::size_t row = 0; row < 8; ++row) {
        const std::size_t y = 13 - row;
        for (std::size_t x = 15 - row; x <= 16 + row; ++x) canvas.setPixel(x, y, 1.0);
    }
    return canvas;
}

VisualCanvas tree() {
    VisualCanvas canvas(32, 32);
    for (std::size_t y = 17; y < 29; ++y) {
        for (std::size_t x = 14; x < 18; ++x) canvas.setPixel(x, y, 0.75);
    }
    for (std::size_t y = 5; y < 20; ++y) {
        const std::size_t radius = 1 + (y - 5) / 3;
        for (std::size_t x = 16 - radius; x <= 16 + radius; ++x) canvas.setPixel(x, y, 0.95);
    }
    return canvas;
}

std::vector<std::string> allCapabilities() {
    auto result = RoverCortexAdapter::capabilities();
    for (const auto& capability : ImaginatioCortexAdapter::capabilities()) {
        if (std::find(result.begin(), result.end(), capability) == result.end()) {
            result.push_back(capability);
        }
    }
    return result;
}

ExplorerResult observeWorld(RobotMind& mind) {
    Experience experience;
    experience.timestampNs = 1'000'000'000ULL;
    experience.body.battery = 0.92;
    experience.environment.temperature = 18.0;
    experience.environment.novelty = 0.8;
    experience.vision = {0.2, 0.1, 0.7, 0.1};

    ScannerFrame scanner;
    scanner.environmentId = 6707;
    scanner.timestampNs = experience.timestampNs;
    scanner.pose.positionMeters = {1.0, 0.0, 1.0};
    scanner.readings = {
        {.direction = {1.0, 0.0, 0.0}, .distanceMeters = 4.0,
         .maxRangeMeters = 5.0, .confidence = 0.95, .hit = false},
        {.direction = {0.0, 0.0, 1.0}, .distanceMeters = 2.0,
         .maxRangeMeters = 5.0, .confidence = 0.95, .hit = true,
         .semanticLabel = "rock"},
    };
    return mind.observeExplorer(experience, scanner);
}

} // namespace

int main(int argc, char** argv) {
    try {
        RobotMindConfig mindConfig;
        mindConfig.seed = 6707;
        mindConfig.neuronCount = 96;
        mindConfig.enableIdentity = false;
        mindConfig.enableCartography = true;
        RobotMind mind(mindConfig);

        VisualImaginationConfig imaginationConfig;
        imaginationConfig.width = 32;
        imaginationConfig.height = 32;
        imaginationConfig.exposureObservations = 2;
        imaginationConfig.maximumEngrams = 16;
        imaginationConfig.neuralFeedbackStride = 8;
        imaginationConfig.paintPatchSide = 4;
        imaginationConfig.physiologicalExpressionGain = 0.0;
        VisualImagination imagination(mind, imaginationConfig);

        const auto houseImage = house();
        const auto treeImage = tree();
        imagination.learnToTrace(
            houseImage, VisualImagination::makeTeacherTrace(houseImage, 0.01, 4), "house");
        imagination.learnToTrace(
            treeImage, VisualImagination::makeTeacherTrace(treeImage, 0.01, 4), "tree");
        imagination.associateSymbol("HOUSE", houseImage);
        imagination.associateSymbol("TREE", treeImage);

        const auto explorer = observeWorld(mind);
        RoverCortexAdapter roverAdapter;
        ImaginatioCortexAdapter imaginationAdapter;
        const auto spatial = roverAdapter.spatialState(explorer);
        const auto internal = imaginationAdapter.imaginationState(imagination);

        CortexConfig config;
        if (argc > 1) {
            config = loadCortexConfig(argv[1]);
        } else {
            config.enabled = true;
            config.mode = CortexMode::Hybrid;
        }
        if (!config.enabled || config.mode != CortexMode::Hybrid) {
            std::cerr << "Hybrid demo requires enabled=true and mode=hybrid.\n";
            return 2;
        }

        CortexOrchestrator cortex(config);
        cortex.requestExplicitHybrid(
            mind, nullptr, spatial, internal, allCapabilities(),
            "Reach the target safely; use internal imagination only if it helps evaluate the situation.");

        for (int i = 0; i < 5000 && !cortex.idle(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        cortex.poll(mind, nullptr, &spatial, &internal);
        const auto status = cortex.status();

        if (!status.lastDecision.has_value()) {
            std::cerr << "No Cortex decision: " << status.lastError << '\n';
            return 3;
        }

        std::cout << "Cortex decision: " << toString(status.lastDecision->kind)
                  << " | " << status.lastDecision->reason << '\n';

        if (status.lastDecision->kind == CortexDecisionKind::SendToImagination
            && status.lastResponse.has_value()) {
            const auto directive = imaginationAdapter.translate(
                *status.lastDecision, *status.lastResponse, imagination);
            std::cout << "IMAGINATIO directive: " << toString(directive.kind)
                      << " | " << directive.reason << '\n';
            const auto executed = imaginationAdapter.execute(directive, imagination);
            if (executed.executed && executed.report.has_value()) {
                const std::filesystem::path output = "cortex_hybrid_imagination.ppm";
                executed.report->canvas.savePpm(output);
                std::cout << "Saved bounded TATARUS imagination to " << output << '\n';
            } else {
                std::cout << "No imagination executed: " << executed.error << '\n';
            }
        } else if (status.lastDecision->kind == CortexDecisionKind::Accept) {
            const auto directive = roverAdapter.translate(*status.lastDecision, explorer);
            std::cout << "Rover directive: " << toString(directive.kind) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TATARUS hybrid cortex demo failed: " << error.what() << '\n';
        return 1;
    }
}
