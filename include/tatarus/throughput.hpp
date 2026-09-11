#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tatarus {

enum class ThroughputDomain : std::uint8_t {
    Neuron,
    Synapse,
    AxonEvent,
    Dendrite,
    Astrocyte,
    Capillary,
    Oligodendrocyte,
    Microglia,
    Physiology,
    Prospection,
    Cognition,
    SpatialMemory,
    Heart,
    Lung,
    Kidney,
    Circulation,
    Conservation,
    Interoception,
    RobotWorld,
    Count
};

struct ThroughputDomainCounters {
    std::uint64_t entityVisits = 0;
    std::uint64_t logicalReads = 0;
    std::uint64_t logicalWrites = 0;
    std::uint64_t logicalBytesRead = 0;
    std::uint64_t logicalBytesWritten = 0;

    ThroughputDomainCounters& operator+=(const ThroughputDomainCounters& other) noexcept {
        entityVisits += other.entityVisits;
        logicalReads += other.logicalReads;
        logicalWrites += other.logicalWrites;
        logicalBytesRead += other.logicalBytesRead;
        logicalBytesWritten += other.logicalBytesWritten;
        return *this;
    }
};

struct ThroughputCounters {
    std::array<ThroughputDomainCounters, static_cast<std::size_t>(ThroughputDomain::Count)> domains{};
    std::uint64_t neuralTicks = 0;
    std::uint64_t organismSteps = 0;
    std::uint64_t jsonSnapshots = 0;
    std::uint64_t jsonBytes = 0;

    void clear() noexcept { *this = {}; }

    void record(
        ThroughputDomain domain,
        std::uint64_t entities,
        std::uint64_t bytesPerEntity,
        bool read,
        bool write) noexcept {
        auto& counter = domains[static_cast<std::size_t>(domain)];
        counter.entityVisits += entities;
        if (read) {
            counter.logicalReads += entities;
            counter.logicalBytesRead += entities * bytesPerEntity;
        }
        if (write) {
            counter.logicalWrites += entities;
            counter.logicalBytesWritten += entities * bytesPerEntity;
        }
    }

    template <typename T>
    void recordRead(ThroughputDomain domain, std::uint64_t entities = 1) noexcept {
        record(domain, entities, sizeof(T), true, false);
    }

    template <typename T>
    void recordWrite(ThroughputDomain domain, std::uint64_t entities = 1) noexcept {
        record(domain, entities, sizeof(T), false, true);
    }

    template <typename T>
    void recordReadWrite(ThroughputDomain domain, std::uint64_t entities = 1) noexcept {
        record(domain, entities, sizeof(T), true, true);
    }

    ThroughputCounters& operator+=(const ThroughputCounters& other) noexcept {
        for (std::size_t i = 0; i < domains.size(); ++i) {
            domains[i] += other.domains[i];
        }
        neuralTicks += other.neuralTicks;
        organismSteps += other.organismSteps;
        jsonSnapshots += other.jsonSnapshots;
        jsonBytes += other.jsonBytes;
        return *this;
    }

    [[nodiscard]] std::uint64_t totalEntityVisits() const noexcept {
        std::uint64_t total = 0;
        for (const auto& domain : domains) total += domain.entityVisits;
        return total;
    }
    [[nodiscard]] std::uint64_t totalLogicalReads() const noexcept {
        std::uint64_t total = 0;
        for (const auto& domain : domains) total += domain.logicalReads;
        return total;
    }
    [[nodiscard]] std::uint64_t totalLogicalWrites() const noexcept {
        std::uint64_t total = 0;
        for (const auto& domain : domains) total += domain.logicalWrites;
        return total;
    }
    [[nodiscard]] std::uint64_t totalLogicalBytesRead() const noexcept {
        std::uint64_t total = 0;
        for (const auto& domain : domains) total += domain.logicalBytesRead;
        return total;
    }
    [[nodiscard]] std::uint64_t totalLogicalBytesWritten() const noexcept {
        std::uint64_t total = 0;
        for (const auto& domain : domains) total += domain.logicalBytesWritten;
        return total;
    }
};

[[nodiscard]] constexpr std::string_view throughputDomainName(ThroughputDomain domain) noexcept {
    switch (domain) {
        case ThroughputDomain::Neuron: return "neurons";
        case ThroughputDomain::Synapse: return "synapses";
        case ThroughputDomain::AxonEvent: return "axon_events";
        case ThroughputDomain::Dendrite: return "dendrites";
        case ThroughputDomain::Astrocyte: return "astrocytes";
        case ThroughputDomain::Capillary: return "capillaries";
        case ThroughputDomain::Oligodendrocyte: return "oligodendrocytes";
        case ThroughputDomain::Microglia: return "microglia";
        case ThroughputDomain::Physiology: return "physiology";
        case ThroughputDomain::Prospection: return "prospection";
        case ThroughputDomain::Cognition: return "cognition";
        case ThroughputDomain::SpatialMemory: return "spatial_memory";
        case ThroughputDomain::Heart: return "heart";
        case ThroughputDomain::Lung: return "lung";
        case ThroughputDomain::Kidney: return "kidney";
        case ThroughputDomain::Circulation: return "circulation";
        case ThroughputDomain::Conservation: return "conservation";
        case ThroughputDomain::Interoception: return "interoception";
        case ThroughputDomain::RobotWorld: return "robot_world";
        case ThroughputDomain::Count: break;
    }
    return "unknown";
}

} // namespace tatarus
