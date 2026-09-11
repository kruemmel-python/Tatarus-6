#include <tatarus/sdk.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

tatarus::VisualCanvas cross(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    const std::size_t center = side / 2U;
    for (std::size_t i = 2; i + 2U < side; ++i) {
        image.setPixel(center, i, 1.0);
        image.setPixel(i, center, 1.0);
    }
    return image;
}

tatarus::VisualCanvas box(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    for (std::size_t i = 2; i + 2U < side; ++i) {
        image.setPixel(i, 2, 0.75);
        image.setPixel(i, side - 3U, 0.75);
        image.setPixel(2, i, 0.75);
        image.setPixel(side - 3U, i, 0.75);
    }
    return image;
}

tatarus::VisualCanvas colourStudy(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            image.setColorPixel(
                x, y,
                static_cast<double>(x) / static_cast<double>(side - 1U),
                static_cast<double>(y) / static_cast<double>(side - 1U),
                ((x / 3U + y / 3U) % 2U) == 0U ? 0.18 : 0.92);
        }
    }
    return image;
}

tatarus::VisualCanvas featurePortrait(std::size_t side, std::size_t variant) {
    static constexpr std::array<std::array<double, 3>, 5> palettes{{
        {0.92, 0.16, 0.12}, {0.12, 0.82, 0.24}, {0.10, 0.28, 0.94},
        {0.94, 0.76, 0.10}, {0.76, 0.12, 0.86},
    }};
    const auto& accent = palettes[variant % palettes.size()];
    tatarus::VisualCanvas image(side, side);
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const double nx = (static_cast<double>(x) + 0.5)
                / static_cast<double>(side);
            const double ny = (static_cast<double>(y) + 0.5)
                / static_cast<double>(side);
            const double face = std::pow((nx - 0.5) / (0.28 + 0.012 * variant), 2.0)
                + std::pow((ny - 0.53) / (0.39 - 0.01 * variant), 2.0);
            std::array<double, 3> color{0.015, 0.018, 0.025};
            if (face <= 1.0) {
                color = {0.62 + 0.025 * variant, 0.43 + 0.018 * variant,
                    0.31 + 0.012 * variant};
            }
            if (ny < 0.31 + 0.012 * variant && face <= 1.22) {
                color = accent;
            }
            const bool leftEye = std::abs(nx - (0.39 - 0.008 * variant)) < 0.055
                && std::abs(ny - 0.44) < 0.025;
            const bool rightEye = std::abs(nx - (0.61 + 0.008 * variant)) < 0.055
                && std::abs(ny - 0.44) < 0.025;
            if (leftEye || rightEye) color = {accent[2], accent[0], accent[1]};
            if (std::abs(nx - 0.5) < 0.018 + 0.004 * variant
                && ny > 0.49 && ny < 0.62) {
                color = {0.34, 0.22, 0.16};
            }
            if (std::abs(ny - (0.69 + 0.006 * variant)) < 0.018
                && std::abs(nx - 0.5) < 0.09 + 0.012 * variant) {
                color = {accent[1], accent[2], accent[0]};
            }
            image.setColorPixel(x, y, color[0], color[1], color[2]);
        }
    }
    return image;
}

tatarus::VisualCanvas structuredObject(
    std::size_t side,
    std::size_t kind,
    std::size_t variant) {
    tatarus::VisualCanvas image(side, side);
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const double nx = (static_cast<double>(x) + 0.5)
                / static_cast<double>(side);
            const double ny = (static_cast<double>(y) + 0.5)
                / static_cast<double>(side);
            std::array<double, 3> color{0.16, 0.42, 0.78};
            if (ny > 0.82) color = {0.14, 0.34, 0.10};
            if (kind == 0U) {
                // Tree: crown, branches and trunk move independently.
                const double crownX = 0.47 + 0.025 * static_cast<double>(variant);
                const double crownY = 0.34 + 0.012 * static_cast<double>(variant);
                const double crown = std::pow((nx - crownX) / (0.25 + 0.01 * variant), 2.0)
                    + std::pow((ny - crownY) / 0.25, 2.0);
                if (crown < 1.0) color = {
                    0.08 + 0.03 * variant, 0.48 + 0.06 * variant, 0.12};
                if (std::abs(nx - crownX) < 0.045 + 0.008 * variant
                    && ny > 0.42 && ny < 0.84) {
                    color = {0.40 + 0.04 * variant, 0.20, 0.06};
                }
            } else if (kind == 1U) {
                // Bird: body, two wings, head and beak.
                const double body = std::pow((nx - 0.50) / 0.21, 2.0)
                    + std::pow((ny - 0.54) / 0.16, 2.0);
                if (body < 1.0) color = {
                    0.72, 0.20 + 0.10 * variant, 0.12 + 0.08 * variant};
                const double wingSlope = 0.58 + 0.04 * variant;
                if (ny > 0.30 && ny < 0.56
                    && std::abs(ny - (wingSlope - std::abs(nx - 0.5))) < 0.055) {
                    color = {0.10, 0.12 + 0.08 * variant, 0.74};
                }
                if (std::pow((nx - 0.67) / 0.09, 2.0)
                        + std::pow((ny - 0.45) / 0.09, 2.0) < 1.0) {
                    color = {0.84, 0.28, 0.10};
                }
                if (nx > 0.74 && nx < 0.87 && std::abs(ny - 0.45) < 0.045) {
                    color = {0.96, 0.72, 0.08};
                }
            } else {
                // House: rectilinear wall, roof, paired windows and door.
                if (nx > 0.22 && nx < 0.78 && ny > 0.42 && ny < 0.82) {
                    color = {0.70 + 0.04 * variant, 0.42, 0.16};
                }
                const double roof = 0.42 - 0.72 * std::abs(nx - 0.5);
                if (ny > roof - 0.055 && ny < roof + 0.055
                    && nx > 0.12 && nx < 0.88) {
                    color = {0.72, 0.08 + 0.05 * variant, 0.06};
                }
                const bool leftWindow = std::abs(nx - 0.36) < 0.065
                    && std::abs(ny - 0.57) < 0.075;
                const bool rightWindow = std::abs(nx - 0.64) < 0.065
                    && std::abs(ny - 0.57) < 0.075;
                if (leftWindow || rightWindow) color = {0.10, 0.62, 0.90};
                if (std::abs(nx - (0.50 + 0.015 * variant)) < 0.07
                    && ny > 0.65 && ny < 0.82) {
                    color = {0.24, 0.10, 0.04};
                }
            }
            image.setColorPixel(x, y, color[0], color[1], color[2]);
        }
    }
    return image;
}

tatarus::VisualImaginationConfig testConfig() {
    return tatarus::VisualImaginationConfig{
        .width = 12,
        .height = 12,
        .exposureObservations = 8,
        .maximumEngrams = 16,
        .neuralFeedbackStride = 256,
        .physiologicalExpressionGain = 0.0,
        .mind = tatarus::RobotMindConfig{
            .seed = 9'060'926U,
            .neuronCount = 96,
            .microstepsPerObservation = 12,
            .maximumPredictionAlternatives = 5,
            .enableIdentity = false,
            .enableCartography = false,
            .cartography = {},
            .sleepTiming = {},
        },
    };
}

void canvasAndActuatorTest() {
    tatarus::VisualCanvas canvas(8, 8);
    require(canvas.cursorX() == 4 && canvas.cursorY() == 4,
        "canvas cursor did not begin in the center");
    canvas.apply({.kind = tatarus::PaintActionKind::MoveNorth, .repetitions = 2});
    canvas.apply({.kind = tatarus::PaintActionKind::MoveWest, .repetitions = 3});
    canvas.apply({.kind = tatarus::PaintActionKind::Darker, .repetitions = 2});
    canvas.apply({.kind = tatarus::PaintActionKind::Paint});
    require(canvas.cursorX() == 1 && canvas.cursorY() == 2,
        "directional paint actuator moved to the wrong cell");
    require(std::abs(canvas.pixel(1, 2) - 0.75) < 1e-12,
        "grayscale paint actuator used the wrong tone");
    canvas.apply({.kind = tatarus::PaintActionKind::Erase, .intensity = 1.0});
    require(canvas.pixel(1, 2) == 0.0, "erase actuator did not clear its pixel");

    canvas.apply({
        .kind = tatarus::PaintActionKind::Paint,
        .intensity = 1.0,
        .red = 0.10,
        .green = 0.45,
        .blue = 0.90,
    });
    const auto colour = canvas.colorPixel(1, 2);
    require(std::abs(colour[0] - 0.10) < 1e-12
            && std::abs(colour[1] - 0.45) < 1e-12
            && std::abs(colour[2] - 0.90) < 1e-12,
        "RGB pigment actuator did not preserve its colour");
    require(canvas.colorPixels().size() == 8U * 8U * 3U,
        "canvas does not expose interleaved RGB storage");
}

void colourLearningAndSnapshotTest(const std::filesystem::path& output) {
    const auto reference = colourStudy(12);
    tatarus::VisualImagination imagination(testConfig());
    const auto program = tatarus::VisualImagination::makeTeacherTrace(reference);
    const auto learned = imagination.learnToTrace(reference, program, "colour-study");
    require(learned.success && learned.similarity > 0.999999,
        "RGB teacher demonstration did not reproduce the visible lesson");

    // V14 does not retain the source raster or teacher trace. It retains
    // TATARUS' own self-consolidated scalar motor memory, so a direct recall of
    // a successfully traced lesson should reproduce the self-painted result.
    const auto learnedSelfCanvas = learned.canvas;
    const auto recalled = imagination.drawFromMemory(reference);
    const double sourceSimilarity = recalled.canvas.similarity(reference);
    require(recalled.success
            && recalled.canvas.similarity(learnedSelfCanvas) > 0.999999
            && sourceSimilarity > 0.999,
        "V14 self-motor recall did not reproduce the 24-bit learned painting");
    require(!recalled.referenceVisibleDuringDrawing && recalled.actionsExecuted > 0U,
        "V14 recall did not synthesize a target-free paint program");

    const auto state = imagination.stateJson();
    const auto trace = imagination.traceJson();
    require(state.find("\"schema\":\"tatarus-imaginatio-v14\"") != std::string::npos
            && state.find("\"snapshot_format_version\":14") != std::string::npos
            && state.find("\"source_raster_retained\":false") != std::string::npos
            && state.find("\"teacher_trace_retained\":false") != std::string::npos
            && state.find("\"working_canvas_persisted\":false") != std::string::npos
            && state.find("\"self_motor_trace_retained\":true") != std::string::npos
            && state.find("\"self_motor_memory_valid\":true") != std::string::npos
            && state.find("\"color_space\":\"sRGB\"") != std::string::npos,
        "V14 state JSON omitted neural-memory policy or colour metadata");
    require(trace.find("\"schema\":\"tatarus-imaginatio-causal-trace-v7\"") != std::string::npos
            && trace.find("recalled_sensorimotor_engram") != std::string::npos,
        "V14 causal trace omitted synthesized recall provenance");

    std::filesystem::create_directories(output);
    const auto ppm = output / "colour-study-v14-recall.ppm";
    recalled.canvas.savePpm(ppm);
    require(std::filesystem::exists(ppm) && std::filesystem::file_size(ppm) > 100,
        "V14 reconstructed RGB canvas export did not create a usable PPM image");

    const auto expectedFreeRecall = imagination.drawFreely("colour-study");
    require(expectedFreeRecall.success, "V14 free recall failed before snapshot");
    const auto snapshot = output / "colour-snapshot-v14";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot), "V14 RGB snapshot could not be loaded");
    const auto restoredRecall = restored.drawFreely("colour-study");
    require(restoredRecall.success
            && restoredRecall.canvas.similarity(expectedFreeRecall.canvas) > 0.999999,
        "V14 snapshot did not preserve deterministic self-motor visual recall");
    require(restoredRecall.canvas.similarity(reference) > 0.999,
        "V14 snapshot did not restore the self-consolidated 24-bit motor memory");
}

void allFourStagesAndSnapshotTest(const std::filesystem::path& output) {
    const auto crossImage = cross(12);
    const auto boxImage = box(12);
    tatarus::VisualImagination imagination(testConfig());

    const auto crossProgram = tatarus::VisualImagination::makeTeacherTrace(crossImage);
    const auto learnedCross = imagination.learnToTrace(crossImage, crossProgram, "cross");
    require(learnedCross.success && learnedCross.stage == tatarus::ImaginationStage::ObserveAndTrace,
        "stage 1 did not form a visual/motor engram");
    require(learnedCross.referenceVisibleDuringDrawing,
        "stage 1 did not expose the reference during drawing");
    require(learnedCross.similarity > 0.999 && imagination.visualEngramCount() == 1,
        "stage 1 trace did not reproduce the reference");
    require(learnedCross.activeAssemblyId != 0,
        "stage 1 visual/motor engram was not bound to a neural assembly");
    require(imagination.mind().metrics().experiences > 0
            && imagination.mind().metrics().totalSpikes > 0,
        "stage 1 bypassed the persistent nervous system");

    const auto recalled = imagination.drawFromMemory(crossImage);
    require(recalled.success && recalled.stage == tatarus::ImaginationStage::MemoryRecall,
        "stage 2 memory recall failed");
    const double recalledSourceSimilarity = recalled.canvas.similarity(crossImage);
    require(!recalled.referenceVisibleDuringDrawing
            && recalledSourceSimilarity > 0.999999,
        "stage 2 did not replay the self-consolidated neural motor memory");
    require(recalled.actionsExecuted > 0U && !recalled.recalledEngramIds.empty(),
        "stage 2 did not synthesize a fresh motor trace from the visual engram");
    const auto recallTrace = imagination.traceJson();
    require(recallTrace.find("\"schema\":\"tatarus-imaginatio-causal-trace-v7\"")
                != std::string::npos
            && recallTrace.find("recalled_sensorimotor_engram") != std::string::npos
            && recallTrace.find("\"changed_pixels\"") != std::string::npos
            && recallTrace.find("\"dendritic_spikes\"") != std::string::npos,
        "causal trace omitted action provenance, pixels or neural activity");

    require(imagination.associateSymbol("KREUZ", crossImage),
        "stage 3 could not associate symbol and image");
    require(imagination.symbolEngramCount() == 1,
        "stage 3 did not retain the deterministic symbol pattern");
    const auto symbolic = imagination.drawFromSymbol(" kreuz ");
    require(symbolic.success && symbolic.stage == tatarus::ImaginationStage::SymbolRecall,
        "stage 3 symbol recall failed");
    require(!symbolic.referenceVisibleDuringDrawing
            && symbolic.canvas.similarity(crossImage) > 0.999999,
        "stage 3 symbol recall did not recover the bound self-motor engram");
    const auto freeRecall = imagination.drawFreely("cross");
    require(freeRecall.success && !freeRecall.referenceVisibleDuringDrawing
            && freeRecall.actionsExecuted > 0U
            && freeRecall.canvas.similarity(crossImage) > 0.999999,
        "free reconstruction did not replay the learned self-motor concept without a cue");

    const auto learnedBox = imagination.learnToTrace(
        boxImage, tatarus::VisualImagination::makeTeacherTrace(boxImage), "box");
    require(learnedBox.success && imagination.visualEngramCount() == 2,
        "second visual experience was not retained independently");
    require(imagination.associateSymbol("KASTEN", boxImage),
        "second symbol association failed");

    const auto composed = imagination.compose({"KREUZ", "KASTEN"});
    require(composed.success && composed.stage == tatarus::ImaginationStage::Composition,
        "stage 4 composition failed");
    require(!composed.referenceVisibleDuringDrawing
            && composed.recalledEngramIds.size() == 2
            && composed.actionsExecuted > 0,
        "stage 4 did not combine two motor engrams without a target");
    require(composed.canvas.markedPixels() > 0 && composed.novelty > 0.01,
        "stage 4 produced neither a drawing nor a novel arrangement");
    require(composed.prospection.available && std::isfinite(composed.prospection.predictionError),
        "prospective neural state was absent from drawing telemetry");
    require(composed.physiology.available && std::isfinite(composed.physiology.atp),
        "physiology was absent from drawing telemetry");

    std::filesystem::create_directories(output);
    const auto pgm = output / "composition.pgm";
    composed.canvas.savePgm(pgm);
    require(std::filesystem::exists(pgm) && std::filesystem::file_size(pgm) > 32,
        "canvas export did not create a usable PGM image");

    const auto snapshot = output / "snapshot";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot), "IMAGINATIO snapshot could not be loaded");
    require(restored.visualEngramCount() == 2 && restored.symbolEngramCount() == 2,
        "snapshot lost visual or symbol engrams");
    const auto restoredSymbol = restored.drawFromSymbol("KREUZ");
    require(restoredSymbol.success
            && restoredSymbol.canvas.similarity(symbolic.canvas) > 0.999999
            && restoredSymbol.canvas.similarity(crossImage) > 0.999999,
        "V14 snapshot did not preserve self-motor symbol recall deterministically");

    const auto json = restored.stateJson();
    require(json.find("\"schema\":\"tatarus-imaginatio-v14\"") != std::string::npos
            && json.find("\"source_raster_retained\":false") != std::string::npos
            && json.find("\"teacher_trace_retained\":false") != std::string::npos
            && json.find("\"visual_engrams\"") != std::string::npos
            && json.find("\"symbol_engrams\"") != std::string::npos
            && json.find("\"pixels\"") != std::string::npos
            && json.find("\"failed_attempts\"") != std::string::npos
            && json.find("\"atp\"") != std::string::npos,
        "IMAGINATIO state JSON omitted integrated telemetry");
}

void sharedMindEmbeddingTest() {
    auto config = testConfig();
    tatarus::RobotMind sharedMind(config.mind);
    const auto before = sharedMind.metrics().experiences;
    tatarus::VisualImagination embedded(sharedMind, config);
    require(&embedded.mind() == &sharedMind,
        "embedded drawing environment created a second nervous system");
    const auto image = cross(12);
    const auto learned = embedded.learnToTrace(
        image, tatarus::VisualImagination::makeTeacherTrace(image), "shared-cross");
    require(learned.success && sharedMind.metrics().experiences > before,
        "drawing experience did not advance the shared nervous system");
}

void corpusIngestionTest(const std::filesystem::path& output) {
    tatarus::VisualImagination imagination(testConfig());
    const auto first = colourStudy(12);
    auto second = colourStudy(12);
    for (std::size_t y = 0; y < second.height(); ++y) {
        for (std::size_t x = 0; x < second.width(); ++x) {
            const auto color = second.colorPixel(x, y);
            second.setColorPixel(x, y, color[2], color[0], color[1]);
        }
    }
    const auto experiencesBefore = imagination.mind().metrics().experiences;
    require(imagination.ingestCategoryExample(first, "corpus-a", "corpus-colour", 8)
            && imagination.ingestCategoryExample(second, "corpus-b", "corpus-colour", 8),
        "offline corpus ingestion did not retain categorized examples");
    require(imagination.visualEngramCount() == 2U
            && imagination.categoryEngramCount() == 1U
            && imagination.mind().metrics().experiences > experiencesBefore,
        "offline corpus ingestion bypassed visual, category, or neural memory");
    const auto expectedCorpusSelfCanvas = imagination.canvas();
    const auto state = imagination.stateJson();
    require(state.find("\"category\":\"CORPUS-COLOUR\"") != std::string::npos
            && state.find("\"examples\":2") != std::string::npos,
        "offline corpus category evidence is absent from state telemetry");

    const auto snapshot = output / "corpus-ingestion-snapshot";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot)
            && restored.visualEngramCount() == 2U
            && restored.categoryEngramCount() == 1U,
        "offline corpus snapshot did not preserve categorized memories");
    const auto recalled = restored.drawFreely("corpus-b");
    require(recalled.success && recalled.actionsExecuted > 0U
            && recalled.canvas.similarity(expectedCorpusSelfCanvas) > 0.999999
            && recalled.canvas.similarity(second) > 0.999,
        "offline corpus self-motor engram was not recallable after cold restore");
    const auto restoredState = restored.stateJson();
    require(restoredState.find("\"source_raster_retained\":false") != std::string::npos
            && restoredState.find("\"teacher_trace_retained\":false") != std::string::npos,
        "offline corpus snapshot violated the V14 no-source-raster invariant");
}

void categoryFusionAndSnapshotTest(const std::filesystem::path& output) {
    tatarus::VisualImagination imagination(testConfig());
    const auto crossImage = cross(12);
    const auto boxImage = box(12);
    auto colourA = colourStudy(12);
    auto colourB = colourStudy(12);
    for (std::size_t y = 0; y < colourB.height(); ++y) {
        for (std::size_t x = 0; x < colourB.width(); ++x) {
            const auto color = colourB.colorPixel(x, y);
            colourB.setColorPixel(x, y, color[2], color[0], color[1]);
        }
    }
    const auto learn = [&imagination](
        const tatarus::VisualCanvas& image,
        const std::string& label,
        const std::string& category) {
        const auto result = imagination.learnToTrace(
            image, tatarus::VisualImagination::makeTeacherTrace(image), label);
        require(result.success, "category example could not be learned concretely");
        require(imagination.addCategoryExample(category, image),
            "category example was not attached to its sensomotor engram");
    };
    learn(crossImage, "cross-example", "shape");
    learn(boxImage, "box-example", "shape");
    learn(colourA, "palette-a", "colour");
    learn(colourB, "palette-b", "colour");
    require(imagination.categoryEngramCount() == 2U,
        "incremental category memory did not retain two abstractions");

    const auto variant = imagination.drawFromCategory("shape", 11U);
    require(variant.success
            && variant.stage == tatarus::ImaginationStage::CategoryFusion
            && !variant.referenceVisibleDuringDrawing
            && variant.categoryNames == std::vector<std::string>{"SHAPE"},
        "stage 5 did not create a target-free category variant");
    const double closestConcrete = std::max(
        variant.canvas.similarity(crossImage), variant.canvas.similarity(boxImage));
    require(closestConcrete < 0.999999,
        "category variant merely copied a concrete training example");

    const auto fused = imagination.fuseCategories({"shape", "colour"}, 29U);
    require(fused.success && fused.categoryNames.size() == 2U
            && fused.actionsExecuted > 0U && fused.novelty > 0.0,
        "five-point fusion did not merge two learned categories");
    const auto state = imagination.stateJson();
    const auto trace = imagination.traceJson();
    require(state.find("\"category_engrams\":[") != std::string::npos
            && state.find("\"examples\":2") != std::string::npos
            && state.find("\"form_model\":\"five-field-structural-morphology\"")
                != std::string::npos
            && state.find("\"feature_model\":\"hierarchical-object-part-running-statistics\"")
                != std::string::npos
            && state.find("\"detail_policy\":\"factorized-soft-region-recombination\"")
                != std::string::npos
            && state.find("\"feature_means\":[") != std::string::npos
            && state.find("\"shape_points\":[") != std::string::npos,
        "category state omitted incremental example evidence");
    require(trace.find("category_factorized_feature_synthesis") != std::string::npos,
        "causal trace omitted stage-5 fusion provenance");

    const auto snapshot = output / "category-snapshot";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot) && restored.categoryEngramCount() == 2U,
        "snapshot v5 lost category abstractions");
    const auto restoredFusion = restored.fuseCategories({"shape", "colour"}, 29U);
    require(restoredFusion.success
            && restoredFusion.canvas.similarity(fused.canvas) > 0.999999,
        "snapshot v5 did not reproduce category fusion deterministically");
}

void factorizedFeatureLearningTest(const std::filesystem::path& output) {
    tatarus::VisualImagination imagination(testConfig());
    std::vector<tatarus::VisualCanvas> learnedFaces;
    for (std::size_t variant = 0; variant < 5U; ++variant) {
        learnedFaces.push_back(featurePortrait(12U, variant));
        const auto label = "factor-face-" + std::to_string(variant);
        const auto learned = imagination.learnToTrace(
            learnedFaces.back(),
            tatarus::VisualImagination::makeTeacherTrace(learnedFaces.back()),
            label);
        require(learned.success, "factorized portrait could not be learned");
        require(imagination.addCategoryExample("human-face", learnedFaces.back()),
            "factorized portrait was not added to category memory");
    }

    const auto imagined = imagination.drawFromCategory("human-face", 71U);
    require(imagined.success && imagined.featureNames.size() == 10U
            && imagined.featureSourceIndices.size() == 10U,
        "factorized imagination omitted generic object-part provenance");
    const std::set<std::size_t> donors(
        imagined.featureSourceIndices.begin(), imagined.featureSourceIndices.end());
    require(donors == std::set<std::size_t>{0U},
        "V14 category imagination unexpectedly retained concrete exemplar donors");
    double closest = 0.0;
    for (const auto& learned : learnedFaces) {
        closest = std::max(closest, imagined.canvas.similarity(learned));
    }
    require(closest < 0.999999,
        "factorized imagination copied one complete learned portrait");
    const auto state = imagination.stateJson();
    require(state.find("\"perceptual_hierarchy\":[\"retinal_rgb\",\"oriented_edges\","
                       "\"object_parts\",\"category_concept\",\"top_down_prior\"]")
                != std::string::npos
            && state.find("\"feature_fields\":[\"BACKGROUND\",\"UPPER_LEFT\","
                          "\"UPPER_CENTER\",\"UPPER_RIGHT\"") != std::string::npos
            && state.find("\"schema\":\"tatarus-biological-vision-v1\"")
                != std::string::npos
            && state.find("\"optic_nerve\":\"bounded-ganglion-afferent-events\"")
                != std::string::npos
            && state.find("\"feature_sources\":[{") != std::string::npos,
        "factorized feature model is not inspectable in state JSON");

    const auto snapshot = output / "factorized-feature-snapshot";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot),
        "snapshot v8 lost factorized category statistics");
    const auto restoredImagined = restored.drawFromCategory("human-face", 71U);
    require(restoredImagined.success
            && restoredImagined.featureSourceIndices == imagined.featureSourceIndices
            && restoredImagined.canvas.similarity(imagined.canvas) > 0.999999,
        "snapshot v8 did not reproduce factorized imagination deterministically");
}

void generalObjectLearningTest() {
    tatarus::VisualImagination imagination(testConfig());
    const std::array<std::string, 3> categories{"TREE", "BIRD", "HOUSE"};
    for (std::size_t kind = 0; kind < categories.size(); ++kind) {
        std::vector<tatarus::VisualCanvas> examples;
        for (std::size_t variant = 0; variant < 4U; ++variant) {
            examples.push_back(structuredObject(12U, kind, variant));
            const auto label = categories[kind] + '-' + std::to_string(variant);
            const auto learned = imagination.learnToTrace(
                examples.back(),
                tatarus::VisualImagination::makeTeacherTrace(examples.back()),
                label);
            require(learned.success
                    && imagination.addCategoryExample(categories[kind], examples.back()),
                "general visual category could not be learned");
        }
        const auto imagined = imagination.drawFromCategory(
            categories[kind], 901U + kind);
        const std::set<std::size_t> donors(
            imagined.featureSourceIndices.begin(),
            imagined.featureSourceIndices.end());
        require(imagined.success && imagined.featureNames.size() == 10U
                && donors == std::set<std::size_t>{0U},
            "V14 general category unexpectedly depended on retained concrete exemplars");
        double closest = 0.0;
        for (const auto& example : examples) {
            closest = std::max(closest, imagined.canvas.similarity(example));
        }
        require(closest < 0.999999,
            "general category imagination copied a complete learned object");
        const auto recognized = imagination.recognizeCategories(
            examples[2U], {categories[kind]}, 3U);
        require(recognized.success && !recognized.matches.empty()
                && recognized.matches.front().category == categories[kind]
                && recognized.matches.front().topDownBoost <= 0.15
                && !recognized.dominantVentralArea.empty(),
            "ventral stream did not recognize a learned general object category");
    }
}

void relationalSceneAndProspectionTest(const std::filesystem::path& output) {
    tatarus::VisualImagination imagination(testConfig());
    const auto person = cross(12);
    const auto ball = box(12);
    const auto learnCategory = [&imagination](
        const tatarus::VisualCanvas& image,
        const std::string& label,
        const std::string& category) {
        const auto learned = imagination.learnToTrace(
            image, tatarus::VisualImagination::makeTeacherTrace(image), label);
        require(learned.success, "scene category concrete engram could not be learned");
        require(imagination.addCategoryExample(category, image),
            "scene category was not bound to its concrete engram");
    };
    learnCategory(person, "person-standing", "person");
    learnCategory(ball, "ball-round", "ball");
    require(imagination.addPoseExample("person", "standing", person),
        "stage 6 did not bind category and pose independently");
    require(imagination.poseEngramCount() == 1U,
        "stage 6 did not retain a pose engram");

    const tatarus::SceneDescription lesson{
        .name = "person-left-of-ball",
        .objects = {
            {.instance = "person-1", .category = "person", .pose = "standing",
             .x = 0.25, .y = 0.62, .scale = 0.34, .rotation = 0.0, .depth = 0.30},
            {.instance = "ball-1", .category = "ball", .pose = "canonical",
             .x = 0.65, .y = 0.62, .scale = 0.24, .rotation = 0.0, .depth = 0.25},
        },
        .relations = {
            {.subject = "person-1", .relation = tatarus::SceneRelationKind::LeftOf,
             .object = "ball-1"},
        },
    };
    require(imagination.observeScene(lesson) && imagination.observeScene(lesson),
        "stage 6 could not observe a relational scene");
    require(imagination.relationEngramCount() == 1U,
        "stage 6 did not retain a relation engram");

    const tatarus::SceneDescription sceneCue{
        .name = "novel-layout",
        .objects = {
            {.instance = "person-1", .category = "person", .pose = "standing",
             .x = 0.20, .y = 0.58, .scale = 0.34, .rotation = 0.0, .depth = 0.30},
            {.instance = "ball-1", .category = "ball", .pose = "canonical",
             .x = std::nullopt, .y = 0.58, .scale = 0.24, .rotation = 0.0, .depth = 0.25},
        },
        .relations = lesson.relations,
    };
    const auto imagined = imagination.imagineScene(sceneCue, 67U);
    require(imagined.success
            && imagined.stage == tatarus::ImaginationStage::RelationalScene
            && imagined.sceneInstances.size() == 2U
            && imagined.sceneMetrics.objectCount == 2U
            && imagined.sceneMetrics.categoryPreservation == 1.0
            && imagined.sceneMetrics.poseCoherence == 1.0
            && imagined.sceneMetrics.spatialRelationAccuracy == 1.0,
        "stage 6 did not preserve object identity, pose and learned relation");
    require(imagined.sceneInstances[0].x < imagined.sceneInstances[1].x
            && imagined.canvas.markedPixels() > 0U
            && imagined.actionsExecuted > 0U
            && !imagined.referenceVisibleDuringDrawing,
        "stage 6 did not execute an embodied target-free scene drawing");
    const auto sceneTrace = imagination.traceJson();
    require(sceneTrace.find("relational_scene_object_motor_program") != std::string::npos
            && sceneTrace.find("\"scene_instance\":\"PERSON-1\"") != std::string::npos
            && sceneTrace.find("internally_simulated_scene_state") != std::string::npos,
        "stage 6 trace omitted objectwise motor execution or scene feedback");

    const tatarus::SceneDescription before{
        .name = "kick-before",
        .objects = {
            {.instance = "person-1", .category = "person", .pose = "standing",
             .x = 0.20, .y = 0.58, .scale = 0.34, .rotation = 0.0, .depth = 0.30},
            {.instance = "ball-1", .category = "ball", .pose = "canonical",
             .x = 0.45, .y = 0.58, .scale = 0.24, .rotation = 0.0, .depth = 0.25},
        },
        .relations = sceneCue.relations,
    };
    const tatarus::SceneDescription after{
        .name = "kick-after",
        .objects = {
            {.instance = "person-1", .category = "person", .pose = "standing",
             .x = 0.21, .y = 0.58, .scale = 0.34, .rotation = 0.0, .depth = 0.30},
            {.instance = "ball-1", .category = "ball", .pose = "canonical",
             .x = 0.75, .y = 0.58, .scale = 0.24, .rotation = 0.0, .depth = 0.25},
        },
        .relations = {},
    };
    const tatarus::SceneAction kick{
        .kind = tatarus::SceneActionKind::Kick,
        .label = "kick",
        .actor = "person-1",
        .target = "ball-1",
        .direction = {1.0, 0.0},
        .magnitude = 1.0,
    };
    require(imagination.learnSceneTransition(before, kick, after)
            && imagination.learnSceneTransition(before, kick, after)
            && imagination.actionEngramCount() == 1U,
        "stage 7 did not learn a persistent action-effect engram");
    const auto future = imagination.imagineFuture(before, {kick}, 71U);
    require(future.success
            && future.stage == tatarus::ImaginationStage::ProspectiveImagination
            && future.prospectiveStates.size() == 2U
            && future.sceneMetrics.learnedEffectsUsed == 1U
            && future.sceneMetrics.predictionConfidence > 0.60
            && future.sceneMetrics.predictionError < 1e-9,
        "stage 7 did not simulate the learned transition with confidence");
    const auto& predictedBall = future.prospectiveStates.back().instances[1];
    require(predictedBall.instance == "BALL-1"
            && std::abs(predictedBall.x - 0.75) < 1e-9
            && future.actionsExecuted > 0U
            && !future.referenceVisibleDuringDrawing,
        "stage 7 changed identity or failed to paint only the predicted final state");
    const auto futureTrace = imagination.traceJson();
    require(futureTrace.find("internally_simulated_scene_state") != std::string::npos
            && futureTrace.find("predicted_scene_object_motor_program") != std::string::npos
            && futureTrace.find("\"prospective_step\":1") != std::string::npos,
        "stage 7 causal trace omitted internal simulation or final motor expression");
    const auto state = imagination.stateJson();
    require(state.find("\"pose_engrams\":[") != std::string::npos
            && state.find("\"relation_engrams\":[") != std::string::npos
            && state.find("\"action_engrams\":[") != std::string::npos
            && state.find("\"scene_memory\":") != std::string::npos
            && state.find("\"prospective_states\":[") != std::string::npos,
        "stage 6/7 state telemetry is incomplete");

    const auto snapshot = output / "stage-7-snapshot";
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(testConfig());
    require(restored.loadSnapshot(snapshot)
            && restored.poseEngramCount() == 1U
            && restored.relationEngramCount() == 1U
            && restored.actionEngramCount() == 1U,
        "snapshot v7 lost pose, relation or action memory");
    const auto restoredFuture = restored.imagineFuture(before, {kick}, 71U);
    require(restoredFuture.success
            && restoredFuture.canvas.similarity(future.canvas) > 0.999999,
        "snapshot v7 did not restore prospective imagination deterministically");
}

void highResolutionPigmentTest(const std::filesystem::path& output) {
    constexpr std::size_t side = 512;
    tatarus::VisualCanvas reference(side, side);
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            reference.setColorPixel(
                x, y,
                static_cast<double>(x & 0xffU) / 255.0,
                static_cast<double>(y & 0xffU) / 255.0,
                static_cast<double>((x * 13U + y * 7U) & 0xffU) / 255.0);
        }
    }
    auto config = tatarus::VisualImaginationConfig{
        .width = side,
        .height = side,
        .exposureObservations = 1,
        .maximumEngrams = 2,
        .neuralFeedbackStride = 256,
        .paintPatchSide = 8,
        .physiologicalExpressionGain = 0.0,
        .mind = tatarus::RobotMindConfig{
            .seed = 5'125'120U,
            .neuronCount = 96,
            .microstepsPerObservation = 4,
            .maximumPredictionAlternatives = 5,
            .enableIdentity = false,
            .enableCartography = false,
            .cartography = {},
            .sleepTiming = {},
        },
    };
    tatarus::VisualImagination imagination(config);
    const auto program = tatarus::VisualImagination::makeTeacherTrace(reference, 0.0, 8);
    require(program.size() < 10'000U,
        "512x512 transient teacher trace exceeded the bounded pigment curriculum");
    const auto learned = imagination.learnToTrace(reference, program, "rgb24-gradient");
    require(learned.success && learned.similarity > 0.999999,
        "512x512 visible teacher demonstration was not exact");
    require(learned.canvas.similarity(reference) > 0.999999,
        "V14 did not keep TATARUS' own completed painting visible after learning");

    const auto state = imagination.stateJson();
    require(state.find("\"schema\":\"tatarus-imaginatio-v14\"") != std::string::npos
            && state.find("\"source_raster_retained\":false") != std::string::npos
            && state.find("\"teacher_trace_retained\":false") != std::string::npos,
        "512x512 state omitted V14 no-source-raster memory policy");

    const auto snapshot = output / "high-resolution-snapshot-v14";
    imagination.saveSnapshot(snapshot);
    const auto manifest = snapshot / "manifest.txt";
    std::ifstream manifestInput(manifest);
    const std::string manifestText(
        (std::istreambuf_iterator<char>(manifestInput)),
        std::istreambuf_iterator<char>());
    require(manifestText.find("version=14") != std::string::npos
            && manifestText.find("visual_memory_storage=self-consolidated-neural-motor-engram")
                != std::string::npos
            && manifestText.find("teacher_trace_storage=transient-only") != std::string::npos
            && manifestText.find("source_raster_retention=none") != std::string::npos
            && manifestText.find("working_canvas_persisted=false") != std::string::npos
            && manifestText.find("self_motor_memory=scalar-rgb8-motor-strokes") != std::string::npos,
        "V14 snapshot manifest does not declare the no-source-raster invariant");

    // Cold restore must work from the snapshot alone. The working canvas is
    // absent from the snapshot; fidelity must come from the self-motor engram.
    tatarus::VisualImagination restored(config);
    require(restored.loadSnapshot(snapshot), "512x512 V14 snapshot could not be loaded");
    const auto recalled = restored.drawFreely("rgb24-gradient");
    require(recalled.success && recalled.actionsExecuted > 0U,
        "512x512 self-motor snapshot could not synthesize a recall painting");
    require(recalled.canvas.similarity(reference) > 0.999999,
        "512x512 V14 cold recall did not reproduce the self-painted memory");

    const auto repeat = restored.drawFreely("rgb24-gradient");
    require(repeat.success && repeat.canvas.similarity(recalled.canvas) > 0.999999,
        "V14 self-motor recall is not deterministic for a fixed engram state");
}


} // namespace

int main(int argc, char** argv) {
    try {
        const auto output = std::filesystem::path(
            argc > 1 ? argv[1] : "tatarus_imaginatio_test_output");
        std::error_code error;
        std::filesystem::remove_all(output, error);
        canvasAndActuatorTest();
        allFourStagesAndSnapshotTest(output);
        colourLearningAndSnapshotTest(output);
        sharedMindEmbeddingTest();
        corpusIngestionTest(output);
        categoryFusionAndSnapshotTest(output);
        factorizedFeatureLearningTest(output);
        generalObjectLearningTest();
        relationalSceneAndProspectionTest(output);
        highResolutionPigmentTest(output);
        std::cout << "TATARUS IMAGINATIO all-stage tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TATARUS IMAGINATIO test failure: " << error.what() << '\n';
        return 1;
    }
}
