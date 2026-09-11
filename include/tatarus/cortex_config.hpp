#pragma once

#include "tatarus/cortex_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tatarus::cortex {

struct LmStudioConfig {
    std::string baseUrl = "http://127.0.0.1:1234/v1";
    std::string model = "AUTO";
    std::uint32_t timeoutMs = 60000;
    double temperature = 0.20;
    double topP = 0.90;
    // Small local models frequently need more than 384 tokens to close the
    // strict response-schema object (including null plan/imagination fields).
    std::uint32_t maxOutputTokens = 768;
};

struct CortexTriggerConfig {
    double novelty = 0.70;
    double predictionError = 0.45;
    double lowMotorConfidence = 0.35;
    std::uint64_t cooldownSteps = 250;
};

struct CortexFreshnessConfig {
    // Responses that no longer match bit-for-bit may still be revalidated
    // against the current bounded Cortex state for this many organism steps.
    std::uint64_t maximumResponseAgeSteps = 250;
};

struct CortexArbiterConfig {
    double minimumStrategyConfidence = 0.25;
    double maximumStrategyRisk = 0.85;
    double minimumAcceptScore = 0.18;
    double criticalVisceralDistress = 0.85;
    double criticalBrainAtp = 0.15;
};


struct CortexSandboxConfig {
    std::size_t maximumBranches = 8;
};

struct CortexLearningConfig {
    bool enabled = true;
    bool competenceGateEnabled = true;
    std::uint64_t minimumTeachingSuccesses = 2;
    std::uint64_t minimumAutonomousTrials = 2;
    double autonomousSuccessThreshold = 0.75;
    double outcomeSuccessThreshold = 0.50;
};


struct CortexTopDownConfig {
    bool enabled = false;
    double maximumRecallStrength = 0.35;
    double maximumGoalBiasStrength = 0.20;
    std::size_t semanticChannels = 12;
    double semanticGain = 0.15;
};

struct CortexDreamConfig {
    bool enabled = true;
    std::uint64_t minimumRemIntervalSteps = 250;
    std::size_t maximumDreamSymbols = 8;
};


struct CortexExecutiveConfig {
    bool enabled = true;
    std::size_t maximumGoals = 8;
    std::size_t maximumWorkingMemoryItems = 24;
    std::size_t maximumPlanSteps = 8;
    std::size_t maximumRecentOutcomes = 12;
    std::uint64_t workingMemoryTtlSteps = 5000;
};

struct CortexMetacognitionConfig {
    bool enabled = true;
    std::uint64_t minimumSamples = 4;
    double cautiousReliabilityThreshold = 0.65;
    double degradedReliabilityThreshold = 0.40;
    double maximumStaleRate = 0.35;
};

struct CortexRecordReplayConfig {
    std::string path = "cortex_record.bin";
    bool strict = true;
};

struct CortexLimits {
    std::size_t maxStrategies = 3;
    std::size_t maxInformationRequests = 3;
    std::size_t maxSummaryBytes = 1024;
    std::size_t maxRationaleBytes = 512;
    std::size_t maxCapabilityBytes = 96;
    std::size_t maxPromptBytes = 24 * 1024;
};

struct CortexConfig {
    bool enabled = false;
    CortexMode mode = CortexMode::ObserveOnly;
    CortexExecutionMode executionMode = CortexExecutionMode::Live;
    LmStudioConfig provider;
    CortexFreshnessConfig freshness;
    CortexTriggerConfig trigger;
    CortexArbiterConfig arbiter;
    CortexSandboxConfig sandbox;
    CortexLearningConfig learning;
    CortexTopDownConfig topDown;
    CortexDreamConfig dream;
    CortexExecutiveConfig executive;
    CortexMetacognitionConfig metacognition;
    CortexRecordReplayConfig recordReplay;
    CortexLimits limits;
    std::size_t workerQueueCapacity = 2;
};

} // namespace tatarus::cortex
