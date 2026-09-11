#include "tatarus/imaginatio.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
    namespace fs = std::filesystem;

    tatarus::VisualImaginationConfig config;
    config.width = 32;
    config.height = 32;
    config.exposureObservations = 1;
    config.neuralFeedbackStride = 1;
    config.paintPatchSide = 1;
    config.physiologicalExpressionGain = 0.0;

    tatarus::VisualImagination imagination(config);
    imagination.rememberSourceResolution(3840, 2160);
    imagination.rememberSourceResolution(3840, 2160);
    imagination.rememberSourceResolution(1920, 1080);

    tatarus::VisualCanvas reference(32, 32);
    reference.setColorPixel(16, 16, 1.0, 0.25, 0.05);
    const std::vector<tatarus::PaintAction> lesson{{
        .kind = tatarus::PaintActionKind::Paint,
        .repetitions = 1,
        .intensity = 1.0,
        .red = 1.0,
        .green = 0.25,
        .blue = 0.05,
    }};

    require(imagination.learnToTrace(reference, lesson, "NATIVE_RENDER_TEST").success,
        "native render lesson failed");
    require(imagination.drawFromMemory(reference).success,
        "native render recall failed");

    const auto render = imagination.renderLastTargetSpace(3840, 2160);
    require(render.generatedFromMotorTrace, "target-space painting was not trace-driven");
    require(render.executedDirectlyOnTargetCanvas,
        "motor trace was not executed directly on the target canvas");
    require(!render.usedRasterInterpolation,
        "target-space painting silently used raster interpolation");
    require(render.synthesizedHighFrequencyDetail,
        "enlarged target did not synthesize target-space pigment detail");
    require(render.informationWidth == 32 && render.informationHeight == 32,
        "target-space provenance lost the neural working resolution");
    require(render.width == 3840, "target-space width changed");
    require(render.height == 2160, "target-space height changed");
    require(render.rgb8.size() == 3840ULL * 2160ULL * 3ULL,
        "target-space byte count is invalid");
    require(render.motorMarksRendered > 0U,
        "target-space abstract recall produced no drawable motor marks");

    // A dense photographic trace is painted directly at the target size. The
    // resulting pigment grain must contain new target-frequency values (unlike
    // a resized 32x32 raster), remain deterministic, and avoid uncovered holes.
    tatarus::VisualImagination smoothImagination(config);
    tatarus::VisualCanvas gradient(32, 32);
    for (std::size_t y = 0; y < gradient.height(); ++y) {
        for (std::size_t x = 0; x < gradient.width(); ++x) {
            const double position = static_cast<double>(x)
                / static_cast<double>(gradient.width() - 1U);
            gradient.setColorPixel(
                x, y,
                0.002 + 0.798 * position,
                0.004 + 0.656 * position,
                0.006 + 0.534 * position);
        }
    }
    const auto gradientProgram = tatarus::VisualImagination::makeTeacherTrace(
        gradient, 0.0, 8);
    require(smoothImagination.learnToTrace(
        gradient, gradientProgram, "SMOOTH_NATIVE_RENDER").success,
        "continuous reconstruction lesson failed");
    require(smoothImagination.drawFromMemory(gradient).success,
        "continuous reconstruction recall failed");
    const auto smooth = smoothImagination.renderLastTargetSpace(320, 320);
    require(smooth.executedDirectlyOnTargetCanvas && !smooth.usedRasterInterpolation,
        "dense target-space painting fell back to interpolation");
    require(smooth.synthesizedHighFrequencyDetail,
        "dense target-space painting omitted synthesized detail");
    const auto repeat = smoothImagination.renderLastTargetSpace(320, 320);
    require(smooth.rgb8 == repeat.rgb8,
        "target-space pigment synthesis is not deterministic");

    std::size_t targetFrequencyChanges = 0U;
    std::uint64_t centreLuminance = 0U;
    for (std::size_t y = 1; y < smooth.height; ++y) {
        const std::size_t previous = ((y - 1U) * smooth.width + smooth.width / 2U) * 3U;
        const std::size_t current = (y * smooth.width + smooth.width / 2U) * 3U;
        if (smooth.rgb8[current] != smooth.rgb8[previous]
            || smooth.rgb8[current + 1U] != smooth.rgb8[previous + 1U]
            || smooth.rgb8[current + 2U] != smooth.rgb8[previous + 2U]) {
            ++targetFrequencyChanges;
        }
        centreLuminance += static_cast<std::uint64_t>(smooth.rgb8[current])
            + smooth.rgb8[current + 1U] + smooth.rgb8[current + 2U];
    }
    require(targetFrequencyChanges > smooth.height / 8U,
        "enlarged painting contains no new target-frequency pigment structure");
    require(centreLuminance > 3U * 40U * (smooth.height - 1U),
        "target-space brush left uncovered checker holes in the painted field");

    const auto snapshot = fs::temp_directory_path() / "tatarus_imaginatio_v12_resolution_test";
    std::error_code error;
    fs::remove_all(snapshot, error);
    imagination.saveSnapshot(snapshot);

    tatarus::VisualImagination restored(config);
    require(restored.loadSnapshot(snapshot), "target-space render snapshot did not reload");
    const auto resolutions = restored.sourceResolutionMemory();
    require(resolutions.size() == 2, "source resolution memory count changed");
    bool saw4kTwice = false;
    for (const auto& value : resolutions) {
        if (value.width == 3840 && value.height == 2160 && value.observations == 2) {
            saw4kTwice = true;
        }
    }
    require(saw4kTwice, "4K source observations were not consolidated");
    require(restored.stateJson().find("source_resolution_memory") != std::string::npos,
        "source resolution memory is missing from state JSON");

    fs::remove_all(snapshot, error);
    std::cout << "TATARUS direct target-space motor-painting and resolution-memory tests: PASS\n";
    return 0;
    } catch (const std::exception& error) {
        std::cerr << "TATARUS target-space render test failure: " << error.what() << '\n';
        return 1;
    }
}
