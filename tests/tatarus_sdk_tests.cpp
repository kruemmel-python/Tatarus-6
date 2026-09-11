#include <tatarus/sdk.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

tatarus::Experience experience(std::uint64_t t, double x) {
    tatarus::Experience e;
    e.timestampNs = t * 1'000'000ULL;
    e.vision = {x, x * 0.75, x * -0.3, x > 0.0 ? 0.9 : -0.9};
    e.audio = {x * 0.4, -x * 0.2};
    e.imu.acceleration = {x, x * 0.5, 9.81};
    e.imu.rotation = {0.1 * x, -0.05 * x, 0.02};
    e.environment.light = x > 0.0 ? 0.9 : 0.1;
    e.environment.soundLevel = x > 0.0 ? 0.7 : 0.2;
    e.environment.temperature = 21.0 + x;
    e.environment.novelty = 0.05;
    e.body.battery = 0.95;
    e.body.balance = 0.1 * x;
    return e;
}

std::array<double, 40> identityFeatures(double base, double motion) {
    std::array<double, 40> f{};
    for (std::size_t i = 0; i < 8; ++i) f[i] = base;
    for (std::size_t i = 8; i < 32; ++i) f[i] = base * 0.7;
    for (std::size_t i = 32; i < 40; ++i) f[i] = motion;
    return f;
}

void trainAlternating(tatarus::RobotMind& mind, std::uint64_t& t, int cycles) {
    for (int i = 0; i < cycles; ++i) {
        mind.observe(experience(t++, -0.9));
        mind.observe(experience(t++, 0.9));
    }
}

void testTatarusBiologyIsLive() {
    tatarus::RobotMind mind;
    const auto biology = mind.biology();
    assert(biology.available);
    assert(biology.dendriticSegments == 672);
    assert(biology.astrocytes == 8);
    assert(biology.capillaries == 8);
    assert(biology.vessels == 8);
    assert(biology.oligodendrocytes == 6);
    assert(biology.microglia == 10);
    assert(std::isfinite(biology.oxygen));
    assert(std::isfinite(biology.glucose));
    assert(std::isfinite(biology.flow));
    assert(std::isfinite(biology.myelinCoverage));
    assert(std::isfinite(biology.conductionVelocityMps));
    assert(std::isfinite(biology.effectiveDelayMs));
    assert(biology.repairCapacity > 0.0);
}

void testRawCameraUsesBiologicalVisualPathway() {
    tatarus::RobotMind mind(tatarus::RobotMindConfig{
        .seed = 8'901U,
        .microstepsPerObservation = 2,
        .enableIdentity = false,
        .enableCartography = false,
    });
    tatarus::Experience input;
    input.timestampNs = 1'000'000ULL;
    input.visualFrame = tatarus::VisualFrame{
        .width = 16U,
        .height = 16U,
        .rgb = std::vector<double>(16U * 16U * 3U, 0.02),
    };
    for (std::size_t y = 3U; y < 13U; ++y) {
        for (std::size_t x = 4U; x < 12U; ++x) {
            const std::size_t pixel = (y * 16U + x) * 3U;
            input.visualFrame->rgb[pixel] = 0.85;
            input.visualFrame->rgb[pixel + 1U] = 0.35;
            input.visualFrame->rgb[pixel + 2U] = 0.10;
        }
    }
    const auto observed = mind.observe(input);
    require(observed.metrics.experiences == 1U,
        "raw visual frame did not reach the persistent nervous system");
    const auto state = mind.stateJson();
    require(state.find("\"schema\": \"tatarus-biological-vision-v1\"")
                != std::string::npos
            && state.find("\"raw_frame_seen\": true") != std::string::npos
            && state.find("\"optic_nerve_events\": 64") != std::string::npos
            && state.find("\"v1_orientation_cells\": 16") != std::string::npos,
        "RobotMind did not expose its integrated biological visual pathway");
}

void testLearningAndProspection() {
    tatarus::RobotMind mind;
    std::uint64_t t = 1;
    trainAlternating(mind, t, 30);

    const auto metrics = mind.metrics();
    assert(metrics.experiences == 60);
    assert(metrics.learnedTransitions > 0);
    assert(metrics.contextualTransitions > 0);
    assert(metrics.observedTransitions > 0);
    assert(metrics.finite);

    const auto prospective = mind.prospection();
    assert(prospective.available);
    assert(prospective.learnedTransitions > 0);
    assert(prospective.observedTransitions > 0);
    assert(std::isfinite(prospective.predictionError));
    assert(std::isfinite(prospective.temporalSurprise));

    const auto prediction = mind.predict();
    assert(prediction.available);
    assert(prediction.confidence > 0.0);
}

void testIdentityLifecycleAndEntityConditioning() {
    tatarus::RobotMind mind;
    const auto features = identityFeatures(0.8, 0.2);
    tatarus::IdentityDecision last;
    for (int i = 0; i < 3; ++i) {
        last = mind.observeIdentity({
            .features = features,
            .cameraId = 0,
            .timestampSeconds = 1.0 + i,
            .quality = 0.95,
        });
    }
    assert(last.entityId != 0);
    assert(mind.metrics().identities >= 1);

    // Neural Identity is part of the product context, not a side database.
    std::uint64_t t = 100;
    trainAlternating(mind, t, 8);
    const auto context = mind.context();
    assert(context.currentEntity.has_value());
    assert(context.currentEntity->id == last.entityId);
    assert(context.metrics.entityConditionedTransitions > 0);

    auto changed = features;
    for (std::size_t i = 0; i < changed.size(); ++i) {
        changed[i] += (i % 2 ? 0.02 : -0.02);
    }
    const auto decision = mind.observeIdentity({
        .features = changed,
        .cameraId = 1,
        .timestampSeconds = 42.0,
        .quality = 0.9,
    });
    assert(decision.entityId != 0);
}

void testActionConditioning() {
    tatarus::RobotMind mind;
    std::uint64_t t = 1;
    mind.beginAction({
        .id = 9001,
        .label = "move_to_dock",
        .intensity = 0.8,
        .startedNs = 1,
    });
    trainAlternating(mind, t, 10);
    mind.endAction({.id = 9001, .reward = 0.7, .success = 1.0, .novelty = 0.1});
    assert(mind.metrics().actionConditionedTransitions > 0);
}

void testSnapshotRoundTripPreservesNeurobiology() {
    const auto path = std::filesystem::temp_directory_path()
        / "tatarus_sdk_neurobiology_test_snapshot";
    std::error_code ec;
    std::filesystem::remove_all(path, ec);

    tatarus::RobotMind original;
    std::uint64_t t = 1;
    trainAlternating(original, t, 18);
    original.rest(250);

    const auto beforeMetrics = original.metrics();
    const auto beforeBiology = original.biology();
    const auto beforeProspection = original.prospection();
    original.saveSnapshot(path);

    tatarus::RobotMind restored;
    assert(restored.loadSnapshot(path));
    const auto afterMetrics = restored.metrics();
    const auto afterBiology = restored.biology();
    const auto afterProspection = restored.prospection();

    assert(afterMetrics.experiences == beforeMetrics.experiences);
    assert(afterMetrics.learnedTransitions == beforeMetrics.learnedTransitions);
    assert(afterMetrics.contextualTransitions == beforeMetrics.contextualTransitions);
    assert(afterMetrics.activeSynapses == beforeMetrics.activeSynapses);

    assert(afterBiology.available);
    assert(afterBiology.dendriticSegments == beforeBiology.dendriticSegments);
    assert(afterBiology.microglia == beforeBiology.microglia);
    assert(afterBiology.dendriticSpikes == beforeBiology.dendriticSpikes);
    assert(afterBiology.microglialRepairEvents == beforeBiology.microglialRepairEvents);
    assert(afterBiology.myelinCoverage == beforeBiology.myelinCoverage);
    assert(afterBiology.effectiveDelayMs == beforeBiology.effectiveDelayMs);

    assert(afterProspection.learnedTransitions == beforeProspection.learnedTransitions);
    assert(afterProspection.observedTransitions == beforeProspection.observedTransitions);
    assert(afterProspection.predictionHits == beforeProspection.predictionHits);
    assert(afterProspection.predictionMisses == beforeProspection.predictionMisses);

    std::filesystem::remove_all(path, ec);
}

void testRestDoesNotInventExperience() {
    tatarus::RobotMind mind;
    mind.observe(experience(1, 0.5));
    const auto before = mind.metrics().experiences;
    mind.rest(200);
    assert(mind.metrics().experiences == before);
    assert(mind.biology().available);
}

void testStateJsonExposesIntegratedProduct() {
    tatarus::RobotMind mind;
    std::uint64_t t = 1;
    trainAlternating(mind, t, 5);
    const std::string json = mind.stateJson();
    assert(json.find("\"sdk_version\": \"4.0.0\"") != std::string::npos);
    assert(json.find("\"biology\"") != std::string::npos);
    assert(json.find("\"microglia\"") != std::string::npos);
    assert(json.find("\"prospection\"") != std::string::npos);
}

void testLiveSpatialJson() {
    tatarus::RobotMind mind;
    const std::string spatial = mind.spatialJson();
    assert(spatial.find("\"schema\":\"tatarus-spatial-v3\"") != std::string::npos);
    assert(spatial.find("\"neurons\":[") != std::string::npos);
    assert(spatial.find("\"dendrites\":[") != std::string::npos);
    assert(spatial.find("\"axons\":[") != std::string::npos);
    assert(spatial.find("\"astrocytes\":[") != std::string::npos);
    assert(spatial.find("\"oligodendrocytes\":[") != std::string::npos);
    assert(spatial.find("\"microglia\":[") != std::string::npos);

    mind.observe(experience(1, 0.8));
    const std::string live = mind.liveJson();
    assert(live.find("\"schema\":\"tatarus-live-v1\"") != std::string::npos);
    assert(live.find("\"step\":24") != std::string::npos);
    assert(live.find("\"signals\":[") != std::string::npos);
    assert(live.find("\"myelin\":") != std::string::npos);
    assert(live.find("\"microglia\":[") != std::string::npos);
}

void testNeuralMotorReadout() {
    tatarus::RobotMind mind;
    const auto observed = mind.observe(experience(1, 0.8));
    const auto motor = mind.motor();

    require(motor.available, "neural motor readout is unavailable");
    require(observed.motor.available, "observation omitted neural motor telemetry");
    require(motor.selectedDirection < motor.directionalActivity.size(),
        "neural motor direction is outside the four embodied actions");
    for (const double activity : motor.directionalActivity) {
        require(std::isfinite(activity) && activity >= 0.0 && activity <= 1.0,
            "neural motor-pool activity is invalid");
    }
    require(std::isfinite(motor.confidence)
            && motor.confidence >= 0.0 && motor.confidence <= 1.0,
        "neural motor confidence is invalid");

    const std::string live = mind.liveJson();
    require(live.find("\"motor\":{\"source\":\"tatarus_neural_motor\"")
            != std::string::npos,
        "live telemetry does not identify the nervous system as motor source");
}

void testScalableRegionalTissueProfile() {
    const tatarus::RobotMindConfig config{
        .seed = 44'044U,
        .neuronCount = 384,
        .microstepsPerObservation = 2};
    tatarus::RobotMind first(config);
    tatarus::RobotMind second(config);
    require(first.biology().dendriticSegments == 2'688,
        "384-neuron tissue profile did not scale biological morphology");
    const auto geometry = first.spatialJson();
    require(geometry == second.spatialJson(),
        "same-seed scalable geometry is not exactly reproducible");
    require(geometry.find("\"neurons\":384") != std::string::npos
            && geometry.find("\"regions\":6") != std::string::npos
            && geometry.find("\"hemisphere\":\"left\"") != std::string::npos
            && geometry.find("\"hemisphere\":\"right\"") != std::string::npos
            && geometry.find("\"region\":\"memory\"") != std::string::npos
            && geometry.find("\"layer\":\"L5\"") != std::string::npos,
        "scalable geometry omitted regional brain metadata");
    const auto result = first.observe(experience(1, 0.4));
    require(result.motor.available && first.metrics().finite,
        "scaled tissue did not produce a finite embodied motor readout");
}

void testExplorerCartographyAndSnapshot() {
    constexpr tatarus::EnvironmentId marsSector = 7;
    tatarus::RobotMind mind(tatarus::RobotMindConfig{
        .seed = 70'007U,
        .microstepsPerObservation = 2,
        .cartography = tatarus::CartographyConfig{
            .voxelSizeMeters = 0.5,
            .maximumRangeMeters = 20.0}});

    tatarus::ScannerFrame scan;
    scan.environmentId = marsSector;
    scan.timestampNs = 1'000'000ULL;
    scan.pose.positionMeters = {0.0, 0.0, 0.0};
    scan.readings = {
        tatarus::RangeReading{
            .direction = {1.0, 0.0, 0.0},
            .distanceMeters = 2.0,
            .maxRangeMeters = 5.0,
            .confidence = 0.95,
            .hit = true,
            .semanticLabel = "basalt_rock"},
        tatarus::RangeReading{
            .direction = {0.0, 0.0, 1.0},
            .distanceMeters = 3.0,
            .maxRangeMeters = 3.0,
            .confidence = 0.9,
            .hit = false},
    };

    const auto result = mind.observeExplorer(experience(1, 0.25), scan);
    require(result.cognition.metrics.experiences == 1,
        "explorer observation did not reach the nervous system");
    require(result.cartography.acceptedRays == 2,
        "cartographer did not accept valid scanner rays");
    require(result.cartography.summary.mappedVoxels > 2,
        "scanner rays did not produce a sparse environment matrix");
    require(result.cartography.summary.occupiedVoxels >= 1,
        "scanner hit did not produce an occupied voxel");
    require(result.cartography.summary.freeVoxels >= 1,
        "scanner ray did not mark traversed space as free");

    const auto rock = mind.mapVoxelAt(marsSector, {2.1, 0.0, 0.0});
    require(rock.has_value() && rock->state == tatarus::OccupancyState::Occupied,
        "occupied endpoint cannot be queried by world coordinate");
    require(rock->semanticLabel == "basalt_rock",
        "semantic scanner finding was not retained on the map");
    require(!mind.mapVoxelAt(marsSector, {100.0, 0.0, 100.0}).has_value(),
        "unobserved space is not represented as unknown");

    const std::string mapJson = mind.environmentMapJson(marsSector);
    require(mapJson.find("\"schema\":\"tatarus-environment-map-v1\"")
            != std::string::npos,
        "environment map JSON schema is missing");
    require(mapJson.find("\"semantic_label\":\"basalt_rock\"")
            != std::string::npos,
        "environment map JSON omitted semantic findings");

    const auto path = std::filesystem::temp_directory_path()
        / "tatarus_sdk_cartography_snapshot";
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    mind.saveSnapshot(path);

    tatarus::RobotMind restored(tatarus::RobotMindConfig{
        .seed = 70'007U,
        .microstepsPerObservation = 2,
        .cartography = tatarus::CartographyConfig{
            .voxelSizeMeters = 0.5,
            .maximumRangeMeters = 20.0}});
    require(restored.loadSnapshot(path), "explorer snapshot could not be loaded");
    require(restored.environmentMapJson(marsSector) == mapJson,
        "environment matrix did not survive snapshot round-trip");
    require(!restored.cartographySummary(99).available,
        "separate environments leaked into each other");
    restored.clearEnvironmentMap(marsSector);
    require(!restored.cartographySummary(marsSector).available,
        "environment map could not be cleared independently");
    std::filesystem::remove_all(path, ec);
}

} // namespace

int main() {
    testTatarusBiologyIsLive();
    testRawCameraUsesBiologicalVisualPathway();
    testLearningAndProspection();
    testIdentityLifecycleAndEntityConditioning();
    testActionConditioning();
    testSnapshotRoundTripPreservesNeurobiology();
    testRestDoesNotInventExperience();
    testStateJsonExposesIntegratedProduct();
    testLiveSpatialJson();
    testNeuralMotorReadout();
    testScalableRegionalTissueProfile();
    testExplorerCartographyAndSnapshot();
    std::cout << "TATARUS SDK integration tests: PASS\n";
    return 0;
}
