#pragma once

#include "tatarus/robot_mind.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tatarus {

// Serial actuator vocabulary for the neural-painting environment. Directional
// actions deliberately occupy values 0..3 so they can be coupled to the four
// existing TATARUS motor populations without a second controller.
enum class PaintActionKind : std::uint8_t {
    MoveNorth = 0,
    MoveEast = 1,
    MoveSouth = 2,
    MoveWest = 3,
    Paint = 4,
    Erase = 5,
    Brighter = 6,
    Darker = 7,
    BrushSmall = 8,
    BrushLarge = 9,
};

struct PaintAction {
    PaintActionKind kind = PaintActionKind::Paint;
    std::uint16_t repetitions = 1;
    // Used by Paint/Erase. It permits continuous pigment strength while the
    // action vocabulary itself remains small and discrete.
    double intensity = 1.0;
    // Linear actuator payload in display-referred sRGB. Pigment belongs to a
    // transient or freshly synthesized motor command; V14 visual engrams do
    // not persist these RGB values as source-image memory.
    // Negative RGB values mean "use the current brush colour" and preserve
    // compatibility with the original grayscale actuator calls.
    double red = -1.0;
    double green = -1.0;
    double blue = -1.0;
    // A visible teacher demonstration or a freshly synthesized recall program
    // may carry an RGB8 pigment stamp. Zero dimensions retain scalar brush
    // behaviour. V14 never stores teacher patchRgb inside a persistent visual engram.
    std::uint8_t patchWidth = 0;
    std::uint8_t patchHeight = 0;
    std::array<std::uint8_t, 8U * 8U * 3U> patchRgb{};
};

class VisualCanvas {
public:
    explicit VisualCanvas(std::size_t width = 32, std::size_t height = 32);

    [[nodiscard]] std::size_t width() const noexcept;
    [[nodiscard]] std::size_t height() const noexcept;
    [[nodiscard]] std::size_t cursorX() const noexcept;
    [[nodiscard]] std::size_t cursorY() const noexcept;
    [[nodiscard]] std::size_t brushRadius() const noexcept;
    [[nodiscard]] double brushTone() const noexcept;
    [[nodiscard]] std::array<double, 3> brushColor() const noexcept;
    [[nodiscard]] const std::vector<double>& pixels() const noexcept;
    // Interleaved row-major sRGB triples. pixels() remains the luminance view
    // used by existing grayscale clients.
    [[nodiscard]] const std::vector<double>& colorPixels() const noexcept;

    [[nodiscard]] double pixel(std::size_t x, std::size_t y) const;
    [[nodiscard]] std::array<double, 3> colorPixel(
        std::size_t x, std::size_t y) const;
    void setPixel(std::size_t x, std::size_t y, double value);
    void setColorPixel(
        std::size_t x, std::size_t y,
        double red, double green, double blue);
    void setBrushColor(double red, double green, double blue);
    void setCursor(std::size_t x, std::size_t y);
    void clear(double value = 0.0);
    void clearColor(double red, double green, double blue);
    void apply(const PaintAction& action);

    [[nodiscard]] double similarity(const VisualCanvas& other) const;
    [[nodiscard]] double meanIntensity() const noexcept;
    [[nodiscard]] std::size_t markedPixels(double threshold = 0.01) const noexcept;
    void savePgm(const std::filesystem::path& path) const;
    void savePpm(const std::filesystem::path& path) const;

private:
    std::size_t width_ = 32;
    std::size_t height_ = 32;
    std::size_t cursorX_ = 16;
    std::size_t cursorY_ = 16;
    std::size_t brushRadius_ = 0;
    std::array<double, 3> brushColor_{1.0, 1.0, 1.0};
    std::vector<double> pixels_;
    std::vector<double> colorPixels_;

    void updateLuminance(std::size_t index) noexcept;
};

enum class ImaginationStage : std::uint8_t {
    ObserveAndTrace = 1,
    MemoryRecall = 2,
    SymbolRecall = 3,
    Composition = 4,
    CategoryFusion = 5,
    RelationalScene = 6,
    ProspectiveImagination = 7,
};

// Stage 6 keeps objects independent and learns how their normalized spatial
// states covary. Relations are persistent engrams, not hard-coded drawing
// operations; the enum is only the stable sensor/API vocabulary.
enum class SceneRelationKind : std::uint8_t {
    LeftOf = 0,
    RightOf = 1,
    Above = 2,
    Below = 3,
    InFrontOf = 4,
    Behind = 5,
    Inside = 6,
    Contains = 7,
    Touching = 8,
    Overlapping = 9,
    Near = 10,
    Far = 11,
    LookingAt = 12,
    ConnectedTo = 13,
};

enum class SceneActionKind : std::uint8_t {
    Move = 0,
    Kick = 1,
    Push = 2,
    Pull = 3,
    Fall = 4,
    Rise = 5,
    Approach = 6,
    Depart = 7,
    Custom = 8,
};

// Unset numeric fields are inferred from learned relation engrams. Positions,
// scale and depth are normalized to [0,1], rotation is measured in radians.
struct SceneObjectCue {
    std::string instance;
    std::string category;
    std::string pose = "canonical";
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> scale;
    std::optional<double> rotation;
    std::optional<double> depth;
};

struct SceneRelationCue {
    std::string subject;
    SceneRelationKind relation = SceneRelationKind::Near;
    std::string object;
    double confidence = 1.0;
};

struct SceneDescription {
    std::string name;
    std::vector<SceneObjectCue> objects;
    std::vector<SceneRelationCue> relations;
    std::array<double, 3> backgroundColor{0.0, 0.0, 0.0};
};

struct SceneInstance {
    std::uint64_t instanceId = 0;
    std::string instance;
    std::string category;
    std::string pose;
    double x = 0.5;
    double y = 0.5;
    double scale = 0.3;
    double rotation = 0.0;
    // Larger values are farther away; smaller values are foreground.
    double depth = 0.5;
    // Normalized left, top, right, bottom occupied region.
    std::array<double, 4> occupiedRegion{0.0, 0.0, 1.0, 1.0};
    std::uint64_t assemblyId = 0;
    std::uint64_t sourceEngramId = 0;
    double confidence = 0.0;
};

struct SceneAction {
    SceneActionKind kind = SceneActionKind::Move;
    // Required only for Custom and included in every action-engram key.
    std::string label;
    std::string actor;
    std::string target;
    std::array<double, 2> direction{0.0, 0.0};
    double magnitude = 1.0;
    double duration = 1.0;
};

struct ProspectiveSceneState {
    std::size_t step = 0;
    std::vector<SceneInstance> instances;
    double confidence = 0.0;
    double predictionError = 0.0;
};

struct SceneImaginationMetrics {
    std::size_t objectCount = 0;
    std::size_t relationCount = 0;
    std::size_t learnedEffectsUsed = 0;
    double categoryPreservation = 0.0;
    double spatialRelationAccuracy = 0.0;
    double occlusionConsistency = 0.0;
    double poseCoherence = 0.0;
    double predictionConfidence = 0.0;
    double predictionError = 0.0;
};

struct VisualImaginationConfig {
    std::size_t width = 512;
    std::size_t height = 512;
    std::size_t exposureObservations = 4;
    std::size_t maximumEngrams = 256;
    // Dense colour images are executed as motor chunks. Every action remains
    // serial and traceable, while costly retinal/neural feedback is sampled at
    // this stride (first and last actions are always observed).
    std::size_t neuralFeedbackStride = 32;
    // Side length of a transient/synthesized RGB pigment stamp used by the
    // deterministic painting curriculum. It is an execution granularity, not
    // a persistent source-raster storage format.
    std::size_t paintPatchSide = 8;
    // Category memory is incremental: every example updates bounded geometry
    // and hierarchical retina/V1/ventral feature-part statistics. V14 keeps no
    // raster exemplar reservoir; this field remains for configuration/ABI
    // compatibility when importing older snapshots.
    std::size_t maximumCategories = 64;
    std::size_t categoryAnchorCount = 16;
    std::size_t maximumPoseEngrams = 256;
    std::size_t maximumRelationEngrams = 1024;
    std::size_t maximumActionEngrams = 512;
    std::size_t maximumSceneObjects = 64;
    // Unsupervised object memory is independent from named category memory.
    // Repeatedly encountered object-centred feature patterns converge into a
    // stable object engram even when no human label has ever been supplied.
    std::size_t maximumObjectEngrams = 512;
    double objectMemoryMatchThreshold = 0.72;
    // The recalled mark strength is coupled to ATP, sleep pressure and the
    // current neural motor confidence. Set to zero for deterministic expression
    // controls; recall is still synthesized from V14 self-consolidated memory.
    double physiologicalExpressionGain = 0.12;
    RobotMindConfig mind;
};

struct VisualCategoryMatch {
    std::string category;
    double conceptSimilarity = 0.0;
    double episodicSimilarity = 0.0;
    double bottomUpSimilarity = 0.0;
    double topDownBoost = 0.0;
    double score = 0.0;
    double confidence = 0.0;
};

struct VisualObjectPercept {
    std::uint64_t objectEngramId = 0;
    std::string instance;
    std::string category;
    bool recognizedCategory = false;
    bool novelObject = false;
    bool internallyGenerated = false;
    std::array<double, 4> bounds{};
    std::array<double, 2> center{};
    double areaFraction = 0.0;
    double saliency = 0.0;
    double objectMemorySimilarity = 0.0;
    std::string dominantVentralArea;
    std::vector<VisualCategoryMatch> categoryMatches;
};

struct VisualScenePerceptionReport {
    bool success = false;
    bool internallyGenerated = false;
    std::size_t candidateCount = 0;
    std::size_t recognizedCount = 0;
    std::size_t novelCount = 0;
    std::vector<VisualObjectPercept> objects;
    std::vector<SceneRelationCue> relations;
};

struct ImaginationReport {
    bool success = false;
    ImaginationStage stage = ImaginationStage::ObserveAndTrace;
    VisualCanvas canvas{32, 32};
    double similarity = 0.0;
    double novelty = 0.0;
    std::uint64_t actionsExecuted = 0;
    std::uint64_t activeAssemblyId = 0;
    std::vector<std::uint64_t> recalledEngramIds;
    std::vector<std::string> categoryNames;
    // Stage 4/5 provenance: one source index for every independently selected
    // soft feature field. This makes multi-memory recombination auditable.
    std::vector<std::string> featureNames;
    std::vector<std::size_t> featureSourceIndices;
    std::vector<SceneInstance> sceneInstances;
    std::vector<SceneRelationCue> sceneRelations;
    std::vector<ProspectiveSceneState> prospectiveStates;
    SceneImaginationMetrics sceneMetrics;
    // The produced image is re-entered through the same retina/V1/ventral
    // pathway used for external images. This is perception of imagination,
    // not metadata copied from the generation request.
    VisualScenePerceptionReport perceivedScene;
    bool referenceVisibleDuringDrawing = false;
    PhysiologyTelemetry physiology;
    ProspectiveTelemetry prospection;
    MotorTelemetry motor;
};

struct VisualRecognitionReport {
    bool success = false;
    bool recognized = false;
    std::string dominantVentralArea;
    std::vector<VisualCategoryMatch> matches;
};

// Target-space IMAGINATIO painting. The compatibility type name predates the
// target-space renderer. No 512x512 result is resized: normalized motor marks
// are painted directly into a fresh canvas of the requested size. When the
// target is larger than the working retina, fine pigment structure is
// synthesized deterministically from the learned local colour/edge variation;
// it is inferred texture, never claimed as recovered source truth.
struct TargetSpaceVisualRender {
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t informationWidth = 0;
    std::size_t informationHeight = 0;
    std::vector<std::uint8_t> rgb8;
    std::uint64_t motorMarksRendered = 0;
    bool generatedFromMotorTrace = false;
    bool executedDirectlyOnTargetCanvas = false;
    bool usedRasterInterpolation = false;
    bool synthesizedHighFrequencyDetail = false;
};

// Kept only so existing C++ callers do not break when updating the SDK.
using NativeVisualRender = TargetSpaceVisualRender;

struct SourceResolutionMemory {
    std::size_t width = 0;
    std::size_t height = 0;
    std::uint64_t observations = 0;
};

// TATARUS IMAGINATIO is an embodied environment around one RobotMind. It does
// not calculate an output image in parallel. Every result is produced by a
// serial motor program while the changed canvas is fed back as visual input.
class VisualImagination {
public:
    explicit VisualImagination(VisualImaginationConfig config = {});
    // Embeds the painting environment into an already existing organism. The
    // caller retains ownership of the mind; visual and symbol engrams remain
    // owned by this environment and participate in its snapshots.
    VisualImagination(RobotMind& sharedMind, VisualImaginationConfig config = {});
    ~VisualImagination();

    VisualImagination(const VisualImagination&) = delete;
    VisualImagination& operator=(const VisualImagination&) = delete;
    VisualImagination(VisualImagination&&) noexcept;
    VisualImagination& operator=(VisualImagination&&) noexcept;

    // Stage 1: jointly observes a reference and a demonstrated serial motor
    // trace. The demonstration is transient supervision only. V14 retains a visual engram plus a self-consolidated neural motor engram.
    // The motor memory is formed only after TATARUS has completed its own
    // painting; the external source raster and demonstrated teacher patch trace
    // are never persisted.
    ImaginationReport learnToTrace(
        const VisualCanvas& reference,
        const std::vector<PaintAction>& demonstratedActions,
        const std::string& label = {});

    // Offline corpus ingestion. The source is presented once through the
    // retinal/neural pathway, painted transiently by TATARUS, then re-perceived as
    // a self-result and consolidated into V14 visual + motor memory. The dense
    // teacher trace exists only during the lesson and is never persisted.
    bool ingestCategoryExample(
        const VisualCanvas& reference,
        const std::string& label,
        const std::string& category,
        std::size_t patchSide = 8);

    // Stage 2: presents a cue, removes it, clears the canvas, synthesizes an
    // internal visual approximation from the closest abstract engram and then
    // generates a fresh serial paint program from that approximation.
    ImaginationReport drawFromMemory(const VisualCanvas& cue);

    // Stage 3: the UTF-8 bytes and visual pattern are co-presented as ordinary
    // sensor channels. No tokenizer or language model is involved.
    bool associateSymbol(const std::string& symbol, const VisualCanvas& cue);
    ImaginationReport drawFromSymbol(const std::string& symbol);

    // Synthesizes the strongest learned visual engram without showing a cue.
    // An optional label restricts selection to a learned concept.
    ImaginationReport drawFreely(const std::string& label = {});

    // Stage 4: co-activates two or more symbol engrams, learns one shared
    // object-adaptive part geometry and turns the structurally registered
    // result into a new serial action chain (not a transparent collage).
    ImaginationReport compose(const std::vector<std::string>& symbols);

    // Adds an already learned visual example to an incremental category
    // abstraction. V14 category memory stores only running shape and retina/V1/ventral feature
    // statistics plus assembly bindings; no category consensus raster or
    // representative source-image reservoir is retained.
    bool addCategoryExample(const std::string& category, const VisualCanvas& cue);
    // Runs an unknown image through retina, optic nerve, V1/V2 and the learned
    // ventral category memories. Optional expected categories model bounded
    // top-down context; they may boost but never replace bottom-up evidence.
    [[nodiscard]] VisualRecognitionReport recognizeCategories(
        const VisualCanvas& cue,
        const std::vector<std::string>& expectedCategories = {},
        std::size_t maximumMatches = 3) const;
    // Multi-object scene perception. `learnObjects` updates unsupervised object
    // engrams only for externally presented images. Internal imagination calls
    // this same pathway with learning disabled to prevent hallucination from
    // silently rewriting sensory memory.
    [[nodiscard]] VisualScenePerceptionReport perceiveScene(
        const VisualCanvas& cue,
        const std::vector<std::string>& expectedCategories = {},
        bool learnObjects = true,
        std::size_t maximumObjects = 16);
    // Stage 5 creates a new geometry from one category abstraction or several
    // co-activated abstract category memories. Pigment is reconstructed from
    // learned feature populations; concrete source-image donors are absent.
    ImaginationReport drawFromCategory(
        const std::string& category,
        std::uint64_t variationSeed = 0);
    ImaginationReport fuseCategories(
        const std::vector<std::string>& categories,
        std::uint64_t variationSeed = 0);

    // Associates a pose with the same category abstraction. The cue must match
    // an existing visual engram; the pose memory itself stores only running
    // shape/feature statistics so CATEGORY and POSE remain separate factors.
    bool addPoseExample(
        const std::string& category,
        const std::string& pose,
        const VisualCanvas& cue);

    // Stage 6. Observations require explicit normalized object states and
    // update persistent relation engrams. Imagination may omit any state that
    // should be inferred from those engrams and canonical priors.
    bool observeScene(const SceneDescription& scene);
    ImaginationReport imagineScene(
        const SceneDescription& scene,
        std::uint64_t variationSeed = 0);

    // Stage 7. A transition lesson binds an action to object-state deltas and
    // to the RobotMind's existing temporal prospection. imagineFuture changes
    // no external/canvas state while rolling the horizon forward; only its
    // final predicted state is subsequently painted through motor actions.
    bool learnSceneTransition(
        const SceneDescription& before,
        const SceneAction& action,
        const SceneDescription& after);
    ImaginationReport imagineFuture(
        const SceneDescription& initial,
        const std::vector<SceneAction>& actions,
        std::uint64_t variationSeed = 0);

    // Deterministic curriculum/expression helper. For supervised learning it
    // creates a transient demonstration; recall and imagination may also use it
    // on an internally synthesized canvas to produce a fresh motor program.
    // Its output is never persisted inside a V14 visual engram.
    [[nodiscard]] static std::vector<PaintAction> makeTeacherTrace(
        const VisualCanvas& reference,
        double threshold = 0.01,
        std::size_t patchSide = 1);

    [[nodiscard]] const VisualCanvas& canvas() const noexcept;
    void resetCanvas();
    [[nodiscard]] std::size_t visualEngramCount() const noexcept;
    [[nodiscard]] std::size_t symbolEngramCount() const noexcept;
    // Read-only semantic indexes used by bounded external cognition layers.
    // They expose labels only; no motor program, canvas payload or neural state.
    [[nodiscard]] std::vector<std::string> visualConceptNames() const;
    [[nodiscard]] std::vector<std::string> symbolNames() const;
    [[nodiscard]] std::vector<std::string> categoryNames() const;
    [[nodiscard]] std::size_t categoryEngramCount() const noexcept;
    [[nodiscard]] std::size_t poseEngramCount() const noexcept;
    [[nodiscard]] std::size_t relationEngramCount() const noexcept;
    [[nodiscard]] std::size_t actionEngramCount() const noexcept;
    [[nodiscard]] std::size_t objectEngramCount() const noexcept;
    [[nodiscard]] const ImaginationReport& lastReport() const noexcept;

    // Re-executes normalized motor marks directly on the requested target
    // canvas. This path performs no resize/interpolation of the working canvas.
    [[nodiscard]] TargetSpaceVisualRender renderLastTargetSpace(
        std::size_t width,
        std::size_t height) const;

    // ABI/source-compatibility name for renderLastTargetSpace().
    [[nodiscard]] NativeVisualRender renderLastNative(
        std::size_t width,
        std::size_t height) const;

    // Stores only the physical source dimensions as sensory context while the
    // retina remains normalized. It retains no source pixels or hidden detail.
    void rememberSourceResolution(std::size_t width, std::size_t height);
    [[nodiscard]] std::vector<SourceResolutionMemory> sourceResolutionMemory() const;

    [[nodiscard]] RobotMind& mind() noexcept;
    [[nodiscard]] const RobotMind& mind() const noexcept;

    void saveSnapshot(const std::filesystem::path& directory) const;
    bool loadSnapshot(const std::filesystem::path& directory);
    [[nodiscard]] std::string stateJson() const;
    // Exact event stream used by the Zeichenlabor. It exposes the causal
    // sequence, changed pixels and the neural/physiological telemetry that
    // accompanied every serial motor command.
    [[nodiscard]] std::string traceJson() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tatarus
