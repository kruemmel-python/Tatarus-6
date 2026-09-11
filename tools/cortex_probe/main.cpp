#include "tatarus/cortex.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        tatarus::cortex::LmStudioClient client;
        const auto models = client.listModels();
        std::cout << "LM Studio: reachable\n";
        if (models.empty()) {
            std::cout << "Model: none loaded\n";
            return 2;
        }
        std::cout << "Models:\n";
        for (const auto& model : models) std::cout << "  - " << model << '\n';
        std::cout << "Selected: " << client.resolvedModel() << '\n';

        tatarus::cortex::CortexRequest request;
        request.requestId = 1;
        request.organismStep = 0;
        request.stateFingerprint = 1;
        request.task = tatarus::cortex::CortexTaskKind::ObserveState;
        request.trigger = tatarus::cortex::CortexTriggerReason::Explicit;
        request.goal = "Connectivity probe. Summarize that the cortex transport is operational.";
        request.availableCapabilities = {"STATE_ANALYSIS"};

        const auto response = client.complete(request);
        std::cout << "JSON contract: valid\n";
        std::cout << "Summary: " << response.summary << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cortex probe failed: " << error.what() << '\n';
        std::cerr << "Start LM Studio, load a local model, and enable its local server on 127.0.0.1:1234.\n";
        return 1;
    }
}
