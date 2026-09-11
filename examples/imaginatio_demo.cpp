#include <tatarus/sdk.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

tatarus::VisualCanvas circle(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    const double center = static_cast<double>(side - 1U) * 0.5;
    const double radius = static_cast<double>(side) * 0.30;
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const double dx = static_cast<double>(x) - center;
            const double dy = static_cast<double>(y) - center;
            if (std::abs(std::sqrt(dx * dx + dy * dy) - radius) <= 0.65) {
                image.setPixel(x, y, 1.0);
            }
        }
    }
    return image;
}

tatarus::VisualCanvas tree(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    const std::size_t center = side / 2U;
    for (std::size_t y = side / 6U; y < side * 2U / 3U; ++y) {
        const auto halfWidth = static_cast<std::ptrdiff_t>(
            (y - side / 6U) / 2U + 1U);
        for (std::ptrdiff_t x = static_cast<std::ptrdiff_t>(center) - halfWidth;
             x <= static_cast<std::ptrdiff_t>(center) + halfWidth; ++x) {
            if (x >= 0 && x < static_cast<std::ptrdiff_t>(side)) {
                image.setPixel(static_cast<std::size_t>(x), y, 0.82);
            }
        }
    }
    for (std::size_t y = side * 2U / 3U; y < side * 9U / 10U; ++y) {
        image.setPixel(center, y, 1.0);
        if (center + 1U < side) image.setPixel(center + 1U, y, 1.0);
    }
    return image;
}

tatarus::VisualCanvas moon(std::size_t side) {
    tatarus::VisualCanvas image(side, side);
    const double centerX = static_cast<double>(side) * 0.48;
    const double centerY = static_cast<double>(side) * 0.46;
    const double radius = static_cast<double>(side) * 0.28;
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double dy = static_cast<double>(y) - centerY;
            const double cutDx = static_cast<double>(x) - (centerX + radius * 0.45);
            const bool inOuter = dx * dx + dy * dy <= radius * radius;
            const bool inCutout = cutDx * cutDx + dy * dy <= radius * radius * 0.78;
            if (inOuter && !inCutout) image.setPixel(x, y, 0.90);
        }
    }
    return image;
}

void show(const char* name, const tatarus::ImaginationReport& report) {
    std::cout << name
              << ": success=" << (report.success ? "yes" : "no")
              << " actions=" << report.actionsExecuted
              << " similarity=" << report.similarity
              << " novelty=" << report.novelty
              << " assembly=" << report.activeAssemblyId
              << " ATP=" << report.physiology.atp << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path outputDirectory = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("imaginatio_output");
        std::filesystem::create_directories(outputDirectory);

        constexpr std::size_t side = 512;
        tatarus::VisualImagination imagination(tatarus::VisualImaginationConfig{
            .width = side,
            .height = side,
            .exposureObservations = 4,
            .neuralFeedbackStride = 64,
            .paintPatchSide = 8,
            .physiologicalExpressionGain = 0.12,
            .mind = tatarus::RobotMindConfig{
                .seed = 20'260'906U,
                .neuronCount = 96,
                .microstepsPerObservation = 12,
            },
        });

        const auto circleImage = circle(side);
        const auto treeImage = tree(side);
        const auto moonImage = moon(side);

        // Stage 1: visual targets and demonstrated actuator traces are jointly
        // experienced. The teacher helper is absent from all later stages.
        const auto learnedCircle = imagination.learnToTrace(
            circleImage, tatarus::VisualImagination::makeTeacherTrace(circleImage, 0.01, 8), "circle");
        const auto learnedTree = imagination.learnToTrace(
            treeImage, tatarus::VisualImagination::makeTeacherTrace(treeImage, 0.01, 8), "tree");
        const auto learnedMoon = imagination.learnToTrace(
            moonImage, tatarus::VisualImagination::makeTeacherTrace(moonImage, 0.01, 8), "moon");
        show("1a learn circle", learnedCircle);
        show("1b learn tree", learnedTree);
        show("1c learn moon", learnedMoon);

        // Stage 2: after exposure the target vanishes; the reconstruction is a
        // sequence of actions on an empty canvas.
        const auto rememberedCircle = imagination.drawFromMemory(circleImage);
        rememberedCircle.canvas.savePgm(outputDirectory / "stage2_memory_circle.pgm");
        show("2 memory circle", rememberedCircle);

        // Stage 3: plain UTF-8 byte patterns are associated with visual engrams.
        imagination.associateSymbol("KREIS", circleImage);
        imagination.associateSymbol("BAUM", treeImage);
        imagination.associateSymbol("MOND", moonImage);
        const auto symbolicTree = imagination.drawFromSymbol("BAUM");
        symbolicTree.canvas.savePgm(outputDirectory / "stage3_symbol_tree.pgm");
        show("3 symbol BAUM", symbolicTree);

        // Stage 4: both concepts are present, but no target pixels are. Their
        // learned motor marks are transformed into a new combined action chain.
        const auto composition = imagination.compose({"MOND", "BAUM"});
        composition.canvas.savePgm(outputDirectory / "stage4_moon_tree.pgm");
        show("4 MOND + BAUM", composition);

        // Stage 5: multiple concrete examples update bounded running
        // shape/feature statistics. A new image is synthesized from that
        // abstract category memory, without retaining source rasters.
        imagination.addCategoryExample("HIMMELSFORM", circleImage);
        imagination.addCategoryExample("HIMMELSFORM", moonImage);
        const auto categoryVariant = imagination.drawFromCategory("HIMMELSFORM", 42U);
        categoryVariant.canvas.savePpm(outputDirectory / "stage5_category_variant.ppm");
        show("5 category HIMMELSFORM", categoryVariant);

        // Stage 6: category identity survives scene composition. The relation
        // lesson is stored in the same persistent organism and later fills in
        // omitted moon coordinates before the instances are painted one by one.
        imagination.addCategoryExample("BAUM", treeImage);
        imagination.addPoseExample("BAUM", "STEHEND", treeImage);
        const tatarus::SceneDescription sceneLesson{
            .name = "MOND-UEBER-BAUM",
            .objects = {
                {.instance = "MOND_1", .category = "HIMMELSFORM",
                 .x = 0.70, .y = 0.20, .scale = 0.24, .rotation = 0.0, .depth = 0.90},
                {.instance = "BAUM_1", .category = "BAUM", .pose = "STEHEND",
                 .x = 0.40, .y = 0.65, .scale = 0.50, .rotation = 0.0, .depth = 0.40},
            },
            .relations = {
                {.subject = "MOND_1", .relation = tatarus::SceneRelationKind::Above,
                 .object = "BAUM_1"},
                {.subject = "MOND_1", .relation = tatarus::SceneRelationKind::Behind,
                 .object = "BAUM_1"},
            },
        };
        imagination.observeScene(sceneLesson);
        auto sceneCue = sceneLesson;
        sceneCue.name = "NEUE-MOND-BAUM-SZENE";
        sceneCue.objects[0].x.reset();
        sceneCue.objects[0].y.reset();
        sceneCue.objects[0].depth.reset();
        const auto relationalScene = imagination.imagineScene(sceneCue, 61U);
        relationalScene.canvas.savePpm(outputDirectory / "stage6_relational_scene.ppm");
        show("6 relational scene", relationalScene);

        // Stage 7: a visible before/after experience teaches a normalized
        // action effect. Prospection then changes no canvas until its final
        // internally predicted state is expressed as another motor program.
        auto sceneAfter = sceneLesson;
        sceneAfter.name = "BAUM-BEWEGT";
        sceneAfter.objects[1].x = 0.56;
        const tatarus::SceneAction moveTree{
            .kind = tatarus::SceneActionKind::Move,
            .actor = "BAUM_1",
            .direction = {1.0, 0.0},
            .magnitude = 1.0,
        };
        imagination.learnSceneTransition(sceneLesson, moveTree, sceneAfter);
        const auto prospective = imagination.imagineFuture(sceneLesson, {moveTree}, 73U);
        prospective.canvas.savePpm(outputDirectory / "stage7_predicted_scene.ppm");
        show("7 prospective scene", prospective);

        imagination.saveSnapshot(outputDirectory / "snapshot");
        std::ofstream state(outputDirectory / "imaginatio_state.json", std::ios::trunc);
        state << imagination.stateJson() << '\n';

        const bool passed = learnedCircle.success && learnedTree.success
            && learnedMoon.success && rememberedCircle.success
            && symbolicTree.success && composition.success && categoryVariant.success
            && relationalScene.success && prospective.success;
        std::cout << "\nTATARUS IMAGINATIO all-seven-stage run: "
                  << (passed ? "PASS" : "FAIL") << '\n'
                  << "Artifacts: " << std::filesystem::absolute(outputDirectory).string() << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "TATARUS IMAGINATIO demo failed: " << error.what() << '\n';
        return 1;
    }
}
