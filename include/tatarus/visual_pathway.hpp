#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace tatarus::neuro::vision {

// A compact but explicit retina -> optic nerve -> V1/V2 -> ventral-stream
// pathway. The spatial resolution is deliberately bounded: a simulated cell
// represents a biological population rather than an individual neuron.
struct VisualPathwayConfig {
    std::size_t retinalWidth = 4;
    std::size_t retinalHeight = 4;
    double topDownMaximumGain = 0.35;
    // A bounded retinotopic attention sheet used to form object candidates
    // from local contrast, colour discontinuities and V1-like edge energy.
    // This remains deliberately small: one cell represents a cortical
    // population/receptive field rather than a camera pixel.
    std::size_t objectAnalysisWidth = 32;
    std::size_t objectAnalysisHeight = 32;
    std::size_t maximumObjects = 16;
    double objectSaliencyThreshold = 0.12;
    double minimumObjectAreaFraction = 0.0015;

    void validate() const;
};

struct ObjectFieldLayout {
    // Ellipse used to distinguish object support from surround/background.
    double objectCenterX = 0.50;
    double objectCenterY = 0.52;
    double objectHalfWidth = 0.34;
    double objectHalfHeight = 0.43;
    // Upper/middle/lower x left/centre/right receptive fields.
    std::array<std::array<double, 4>, 9> fields{}; // cx, cy, sigma-x, sigma-y
};

// RGB, luminance contrast, horizontal, vertical, descending diagonal,
// ascending diagonal and junction/corner energy.
using PrimitiveFeatureVector = std::array<double, 9>;
using ObjectPartModel = std::array<PrimitiveFeatureVector, 10>;

struct TopDownVisualPrior {
    ObjectPartModel expectedParts{};
    double gain = 0.0;
};

struct VentralStreamActivation {
    double lateralOccipitalObject = 0.0;
    double fusiformFace = 0.0;
    double parahippocampalPlace = 0.0;
    double biologicalObject = 0.0;
};

// A single attentional object token formed after early visual processing.
// Bounds are normalized left/top/right/bottom coordinates in the source
// image. `parts` is object-centred, so the same object at another position or
// scale remains comparable in the downstream ventral stream.
struct CorticalObjectCandidate {
    std::size_t index = 0;
    std::array<double, 4> bounds{};
    std::array<double, 2> center{};
    double areaFraction = 0.0;
    double saliency = 0.0;
    ObjectPartModel parts{};
    VentralStreamActivation ventral;
};

struct VisualPercept {
    std::size_t sourceWidth = 0;
    std::size_t sourceHeight = 0;
    // Cone-like R/G/B/luminance populations at retinal resolution.
    std::vector<std::array<double, 4>> photoreceptors;
    // ON/OFF centre-surround, two colour-opponent and luminance channels.
    std::vector<std::array<double, 5>> retinalGanglionCells;
    // Explicit bounded optic-nerve code delivered to the persistent nervous
    // system: ON-centre, OFF-centre and two colour-opponent channels.
    std::vector<double> opticNerveEvents;
    // Horizontal, vertical, two diagonals and junctions per retinal field.
    std::vector<std::array<double, 5>> v1OrientationCells;
    ObjectPartModel bottomUpParts{};
    ObjectPartModel integratedParts{};
    VentralStreamActivation ventral;
    // Multi-object attentional decomposition. These are hypotheses, not
    // semantic labels; semantic identity is learned by higher memory layers.
    std::vector<CorticalObjectCandidate> objects;
    // Population code delivered to the persistent nervous system. Each object
    // contributes position/scale/saliency, ventral activations and a compact
    // deterministic projection of its object-centred part representation.
    std::vector<double> objectCortexEvents;
    double topDownGainApplied = 0.0;
};

class VisualPathway {
public:
    explicit VisualPathway(VisualPathwayConfig config = {});

    [[nodiscard]] VisualPercept perceive(
        std::size_t width,
        std::size_t height,
        std::span<const double> interleavedRgb) const;
    [[nodiscard]] VisualPercept perceive(
        std::size_t width,
        std::size_t height,
        std::span<const double> interleavedRgb,
        const ObjectFieldLayout& layout,
        const TopDownVisualPrior* topDown = nullptr) const;

    [[nodiscard]] const VisualPathwayConfig& config() const noexcept { return config_; }
    [[nodiscard]] static ObjectFieldLayout defaultObjectFields();

private:
    VisualPathwayConfig config_;
};

} // namespace tatarus::neuro::vision
