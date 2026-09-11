#include <tatarus/sdk.hpp>

#include <array>
#include <iostream>

namespace {
std::array<double, 40> person(double base, double motion, double variation = 0.0) {
    std::array<double, 40> f{};
    for (std::size_t i = 0; i < 8; ++i) f[i] = base + variation * ((i % 2) ? 1.0 : -1.0);
    for (std::size_t i = 8; i < 32; ++i) f[i] = 0.6 * base + variation * 0.5;
    for (std::size_t i = 32; i < 40; ++i) f[i] = motion + variation * ((i % 3) - 1.0);
    return f;
}

const char* name(tatarus::IdentityDecisionKind kind) {
    using K = tatarus::IdentityDecisionKind;
    switch (kind) {
        case K::NewIdentity: return "new";
        case K::MatchedIdentity: return "matched";
        case K::ProvisionalAssociation: return "association-hypothesis";
        case K::ProvisionalIdentity: return "identity-hypothesis";
        case K::Uncertain: return "uncertain";
    }
    return "?";
}
}

int main() {
    tatarus::RobotMind mind;

    for (int i = 0; i < 3; ++i) {
        auto d = mind.observeIdentity({.features = person(0.75, 0.2), .cameraId = 0, .timestampSeconds = 1.0 + i, .quality = 0.95});
        std::cout << "A observation " << i << ": " << name(d.kind) << " id=" << d.entityId << " confidence=" << d.confidence << '\n';
    }

    auto changed = mind.observeIdentity({.features = person(0.72, 0.24, 0.03), .cameraId = 1, .timestampSeconds = 42.0, .quality = 0.9});
    std::cout << "A changed view: " << name(changed.kind) << " id=" << changed.entityId << " confidence=" << changed.confidence << '\n';
    if (changed.kind == tatarus::IdentityDecisionKind::ProvisionalAssociation) mind.confirmLastIdentity(true);

    for (int i = 0; i < 3; ++i) {
        auto d = mind.observeIdentity({.features = person(-0.65, -0.55), .cameraId = 0, .timestampSeconds = 80.0 + i, .quality = 0.95});
        std::cout << "B observation " << i << ": " << name(d.kind) << " id=" << d.entityId << " confidence=" << d.confidence << '\n';
    }

    std::cout << "Identities: " << mind.metrics().identities << '\n';
    return 0;
}
