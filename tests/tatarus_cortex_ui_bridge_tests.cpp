#include "tatarus/c_api.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string cortexJson(tatarus_organism* organism) {
    const std::uint64_t required = tatarus_organism_cortex_get_json(organism, nullptr, 0);
    require(required > 1, "Cortex JSON size query failed");
    std::vector<char> buffer(static_cast<std::size_t>(required));
    const auto written = tatarus_organism_cortex_get_json(
        organism, buffer.data(), static_cast<std::uint64_t>(buffer.size()));
    require(written == required, "Cortex JSON read returned an unexpected size");
    return std::string(buffer.data());
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Cortex UI bridge test requires a config path");
        tatarus_organism* organism = tatarus_organism_create_sized(7411U, 96U);
        require(organism != nullptr, "failed to create test organism");

        require(tatarus_organism_cortex_configure(organism, argv[1]) != 0,
                "failed to configure Cortex through C ABI");
        auto json = cortexJson(organism);
        require(json.find("\"configured\":true") != std::string::npos,
                "Cortex status did not report configured=true");
        require(json.find("\"mode\":\"hybrid\"") != std::string::npos,
                "Cortex status did not report hybrid mode");

        const std::uint64_t goalId = tatarus_organism_cortex_push_goal(
            organism, "Stell dir ein Haus unter einem Baum vor", 0.9);
        require(goalId != 0U, "failed to add an Executive goal through C ABI");
        require(tatarus_organism_cortex_remember(
                    organism, "constraint", "Nur vorhandene Engramme verwenden", 0.85) != 0,
                "failed to add Working Memory through C ABI");

        json = cortexJson(organism);
        require(json.find("Stell dir ein Haus unter einem Baum vor") != std::string::npos,
                "Executive goal is missing from Cortex status JSON");
        require(json.find("Nur vorhandene Engramme verwenden") != std::string::npos,
                "Working Memory is missing from Cortex status JSON");
        require(json.find("\"grounding\"") != std::string::npos,
                "grounding state is missing from Cortex status JSON");

        require(tatarus_organism_cortex_cancel_goal(organism, goalId) != 0,
                "failed to cancel Executive goal through C ABI");
        require(tatarus_organism_cortex_disable(organism) != 0,
                "failed to disable Cortex through C ABI");
        json = cortexJson(organism);
        require(json.find("\"configured\":false") != std::string::npos,
                "Cortex status did not report configured=false after disable");

        tatarus_organism_destroy(organism);
        std::cout << "TATARUS Cortex UI C-ABI bridge tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
