#include <tatarus/sdk.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

tatarus::VisualImaginationConfig config() {
    tatarus::VisualImaginationConfig value;
    value.width = 24;
    value.height = 24;
    value.exposureObservations = 1;
    value.maximumEngrams = 16;
    value.maximumObjectEngrams = 32;
    value.neuralFeedbackStride = 256;
    value.paintPatchSide = 8;
    value.physiologicalExpressionGain = 0.0;
    value.mind.neuronCount = 96;
    value.mind.microstepsPerObservation = 1;
    value.mind.enableIdentity = false;
    value.mind.enableCartography = false;
    return value;
}

tatarus::VisualCanvas objectExample(int kind) {
    tatarus::VisualCanvas canvas(24, 24);
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 24; ++x) {
            canvas.setColorPixel(x, y, 0.01, 0.01, 0.01);
        }
    }
    if (kind == 0) {
        for (std::size_t y = 6; y < 18; ++y) {
            for (std::size_t x = 5; x < 19; ++x) {
                if (x == 5 || x == 18 || y == 6 || y == 17) {
                    canvas.setColorPixel(x, y, 0.95, 0.08, 0.05);
                }
            }
        }
    } else {
        for (std::size_t y = 4; y < 20; ++y) {
            for (std::size_t x = 10; x < 14; ++x) {
                canvas.setColorPixel(x, y, 0.05, 0.90, 0.12);
            }
        }
        for (std::size_t y = 10; y < 14; ++y) {
            for (std::size_t x = 5; x < 19; ++x) {
                canvas.setColorPixel(x, y, 0.05, 0.90, 0.12);
            }
        }
    }
    return canvas;
}

tatarus::VisualCanvas multiObjectScene() {
    tatarus::VisualCanvas canvas(24, 24);
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 24; ++x) {
            canvas.setColorPixel(x, y, 0.01, 0.01, 0.01);
        }
    }
    for (std::size_t y = 4; y < 11; ++y) {
        for (std::size_t x = 2; x < 9; ++x) {
            if (x == 2 || x == 8 || y == 4 || y == 10) {
                canvas.setColorPixel(x, y, 0.95, 0.08, 0.05);
            }
        }
    }
    for (std::size_t y = 13; y < 22; ++y) {
        for (std::size_t x = 17; x < 20; ++x) {
            canvas.setColorPixel(x, y, 0.05, 0.90, 0.12);
        }
    }
    for (std::size_t y = 16; y < 19; ++y) {
        for (std::size_t x = 14; x < 23; ++x) {
            canvas.setColorPixel(x, y, 0.05, 0.90, 0.12);
        }
    }
    return canvas;
}

void learnCategory(
    tatarus::VisualImagination& imagination,
    const tatarus::VisualCanvas& example,
    const std::string& label,
    const std::string& category) {
    const auto trace = tatarus::VisualImagination::makeTeacherTrace(example, 0.01, 8);
    const auto lesson = imagination.learnToTrace(example, trace, label);
    require(lesson.success, "object visual lesson failed");
    require(imagination.addCategoryExample(category, example),
        "object category binding failed");
}

} // namespace

int main() {
    tatarus::VisualImagination imagination(config());
    const auto redBox = objectExample(0);
    const auto greenCross = objectExample(1);
    learnCategory(imagination, redBox, "red-object", "RED_BOX");
    learnCategory(imagination, greenCross, "green-object", "GREEN_CROSS");
    require(imagination.objectEngramCount() >= 2U,
        "unsupervised persistent object identities were not learned");

    const auto perception = imagination.perceiveScene(
        multiObjectScene(), {"RED_BOX", "GREEN_CROSS"}, false, 16);
    require(perception.success, "multi-object perception produced no objects");
    require(perception.candidateCount >= 2U,
        "multi-object scene was collapsed into one whole-frame object");
    require(perception.recognizedCount >= 2U,
        "learned semantic categories were not recovered per object");
    require(perception.novelCount == 0U,
        "known objects were incorrectly treated as novel");
    require(!perception.relations.empty(),
        "object cortex produced no spatial relation representation");

    const auto recalled = imagination.drawFromMemory(redBox);
    require(recalled.success, "visual memory recall failed");
    require(recalled.perceivedScene.internallyGenerated,
        "IMAGINATIO output was not marked as internally generated perception");
    require(recalled.perceivedScene.candidateCount >= 1U,
        "IMAGINATIO output did not re-enter the object perception pathway");
    require(recalled.perceivedScene.recognizedCount >= 1U,
        "IMAGINATIO could not recognize an object in its own recalled image");

    const auto snapshot = std::filesystem::temp_directory_path()
        / "tatarus_object_cortex_v9_test";
    std::error_code error;
    std::filesystem::remove_all(snapshot, error);
    imagination.saveSnapshot(snapshot);
    tatarus::VisualImagination restored(config());
    require(restored.loadSnapshot(snapshot), "object-cortex snapshot V9 did not load");
    require(restored.objectEngramCount() == imagination.objectEngramCount(),
        "persistent object identities were lost across snapshot round-trip");
    const auto restoredPerception = restored.perceiveScene(
        multiObjectScene(), {"RED_BOX", "GREEN_CROSS"}, false, 16);
    require(restoredPerception.recognizedCount >= 2U,
        "restored object/category memory no longer recognizes the scene");
    std::filesystem::remove_all(snapshot, error);

    std::cout << "TATARUS object-cortex perception/learning/imagination tests passed\n";
    return 0;
}
