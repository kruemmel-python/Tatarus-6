#include "cortex_context.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace tatarus::cortex::detail {
namespace {

[[nodiscard]] double finiteOr(double value, double fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] double unit(double value, double fallback = 0.0) noexcept {
    return std::clamp(finiteOr(value, fallback), 0.0, 1.0);
}

[[nodiscard]] std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

[[nodiscard]] std::uint64_t hashDouble(std::uint64_t hash, double value) noexcept {
    const double canonical = std::isfinite(value) ? value : 0.0;
    return mix(hash, std::bit_cast<std::uint64_t>(canonical));
}

} // namespace

std::uint64_t cortexStateFingerprint(
    const CognitiveContext& context,
    const organism::OrganismTelemetry* organism,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix(hash, context.currentAssemblyId);
    hash = mix(hash, context.prediction.expectedAssemblyId);
    hash = mix(hash, context.metrics.experiences);
    hash = hashDouble(hash, context.prediction.confidence);
    hash = hashDouble(hash, context.novelty);
    hash = hashDouble(hash, context.predictionError);
    hash = hashDouble(hash, context.prospection.sequenceFamiliarity);
    hash = hashDouble(hash, context.motor.confidence);
    hash = hashDouble(hash, context.metrics.meanEnergy);
    hash = mix(hash, static_cast<std::uint64_t>(context.physiology.sleepPhase));
    hash = hashDouble(hash, context.physiology.atp);
    hash = hashDouble(hash, context.biology.oxygen);
    hash = hashDouble(hash, context.biology.glucose);
    if (organism && organism->available) {
        hash = mix(hash, organism->stepCount);
        hash = hashDouble(hash, organism->heart.heartRateBpm);
        hash = hashDouble(hash, organism->circulation.meanArterialPressureMmHg);
        hash = hashDouble(hash, organism->circulation.arterialOxygenSaturation);
        hash = hashDouble(hash, organism->interoception.visceralDistress);
    }
    if (spatial) {
        hash = mix(hash, spatial->environmentId);
        hash = mix(hash, static_cast<std::uint64_t>(spatial->mapAvailable));
        hash = hashDouble(hash, spatial->localNovelty);
        hash = hashDouble(hash, spatial->frontierRatio);
        for (const double proximity : spatial->obstacleProximity) {
            hash = hashDouble(hash, proximity);
        }
        hash = hashDouble(hash, spatial->targetDistance);
        hash = hashDouble(hash, spatial->targetBearing);
        hash = mix(hash, static_cast<std::uint64_t>(spatial->knownRouteAvailable));
        hash = hashDouble(hash, spatial->knownRouteConfidence);
        hash = mix(hash, static_cast<std::uint64_t>(spatial->loopDetected));
        hash = mix(hash, spatial->repeatedPlaceCount);
    }
    if (imagination) {
        hash = mix(hash, static_cast<std::uint64_t>(imagination->available));
        hash = mix(hash, imagination->stage);
        hash = mix(hash, imagination->visualEngrams);
        hash = mix(hash, imagination->symbolEngrams);
        hash = hashDouble(hash, imagination->lastSimilarity);
        hash = hashDouble(hash, imagination->lastNovelty);
        const auto mixText = [&hash](const std::string& text) {
            for (const unsigned char byte : text) hash = mix(hash, byte);
            hash = mix(hash, 0xffU);
        };
        for (const auto& conceptName : imagination->knownConcepts) mixText(conceptName);
        for (const auto& symbol : imagination->knownSymbols) mixText(symbol);
        for (const auto& category : imagination->knownCategories) mixText(category);
    }
    return hash == 0U ? 1U : hash;
}

CortexRequest buildCortexRequest(
    std::uint64_t requestId,
    CortexTaskKind task,
    CortexTriggerReason trigger,
    const RobotMind& mind,
    const organism::OrganismTelemetry* organism,
    std::string goal,
    std::vector<std::string> capabilities,
    const CortexSpatialState* spatial,
    const CortexImaginationState* imagination) {
    const CognitiveContext context = mind.context();

    CortexRequest request;
    request.requestId = requestId;
    request.organismStep = organism && organism->available
        ? organism->stepCount
        : context.metrics.experiences;
    request.stateFingerprint = cortexStateFingerprint(context, organism, spatial, imagination);
    request.task = task;
    request.trigger = trigger;
    request.goal = std::move(goal);

    request.neural.assemblyId = context.currentAssemblyId;
    request.neural.predictedAssemblyId = context.prediction.expectedAssemblyId;
    request.neural.predictionConfidence = unit(context.prediction.confidence);
    request.neural.predictionError = unit(std::abs(context.predictionError));
    request.neural.novelty = unit(context.novelty);
    request.neural.sequenceFamiliarity = unit(context.prospection.sequenceFamiliarity);
    request.neural.motorConfidence = unit(context.motor.confidence);
    request.neural.meanEnergy = unit(context.metrics.meanEnergy, 1.0);
    request.neural.brainAtp = unit(context.physiology.atp, 1.0);
    request.neural.brainOxygen = unit(context.biology.oxygen, 1.0);
    request.neural.brainGlucose = unit(context.biology.glucose, 1.0);
    if (request.neural.assemblyId != 0U) {
        request.neural.activeRepresentations.push_back(CortexRepresentation{
            request.neural.assemblyId,
            1.0,
            request.neural.sequenceFamiliarity,
        });
    }
    if (request.neural.predictedAssemblyId != 0U
        && request.neural.predictedAssemblyId != request.neural.assemblyId) {
        request.neural.activeRepresentations.push_back(CortexRepresentation{
            request.neural.predictedAssemblyId,
            request.neural.predictionConfidence,
            request.neural.sequenceFamiliarity,
        });
    }
    request.physiology.sleepPhase = context.physiology.sleepPhase;

    if (organism && organism->available) {
        request.physiology.available = true;
        request.physiology.atp = request.neural.brainAtp;
        request.physiology.heartRateBpm = finiteOr(organism->heart.heartRateBpm, 0.0);
        request.physiology.mapMmHg = finiteOr(
            organism->circulation.meanArterialPressureMmHg, 0.0);
        request.physiology.cardiacOutputLPerMin = finiteOr(
            organism->heart.cardiacOutputLPerMin, 0.0);
        request.physiology.oxygenSaturation = unit(
            organism->circulation.arterialOxygenSaturation, 1.0);
        request.physiology.respirationRateBpm = finiteOr(
            organism->lung.respirationRateBpm, 0.0);
        request.physiology.gfrMlPerMin = finiteOr(
            organism->kidney.glomerularFiltrationRateMlPerMin, 0.0);
        request.physiology.visceralDistress = unit(
            organism->interoception.visceralDistress);
        request.physiology.sympatheticTone = unit(
            organism->interoception.sympatheticTone);
        request.physiology.brainOxygen = request.neural.brainOxygen;
        request.physiology.brainGlucose = request.neural.brainGlucose;
    } else {
        request.physiology.available = false;
        request.physiology.atp = request.neural.brainAtp;
        request.physiology.brainOxygen = request.neural.brainOxygen;
        request.physiology.brainGlucose = request.neural.brainGlucose;
    }

    if (spatial) request.spatial = *spatial;
    if (imagination) request.imagination = *imagination;
    request.availableCapabilities = std::move(capabilities);
    return request;
}

} // namespace tatarus::cortex::detail
