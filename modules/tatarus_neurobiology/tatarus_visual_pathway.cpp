#include "tatarus/visual_pathway.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tatarus::neuro::vision {
namespace {

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

double luminance(const std::span<const double> rgb, std::size_t pixel) {
    return 0.2126 * rgb[pixel * 3U]
        + 0.7152 * rgb[pixel * 3U + 1U]
        + 0.0722 * rgb[pixel * 3U + 2U];
}

VentralStreamActivation ventralActivation(const ObjectPartModel& parts) {
    double edgeEnergy = 0.0;
    double orthogonalEnergy = 0.0;
    double diagonalEnergy = 0.0;
    double cornerEnergy = 0.0;
    for (std::size_t part = 1; part < parts.size(); ++part) {
        edgeEnergy += parts[part][4] + parts[part][5]
            + parts[part][6] + parts[part][7];
        orthogonalEnergy += parts[part][4] + parts[part][5];
        diagonalEnergy += parts[part][6] + parts[part][7];
        cornerEnergy += parts[part][8];
    }
    edgeEnergy /= 9.0;
    orthogonalEnergy /= 9.0;
    diagonalEnergy /= 9.0;
    cornerEnergy /= 9.0;

    constexpr std::array<std::array<std::size_t, 2>, 3> bilateralPairs{{
        {1U, 3U}, {4U, 6U}, {7U, 9U},
    }};
    double bilateralError = 0.0;
    for (const auto& pair : bilateralPairs) {
        for (std::size_t feature = 3; feature < 9U; ++feature) {
            bilateralError += std::abs(
                parts[pair[0]][feature] - parts[pair[1]][feature]);
        }
    }
    bilateralError /= 18.0;
    const double bilateralSymmetry = std::exp(-8.0 * bilateralError);
    const double centreStructure = parts[5][3] + parts[5][8]
        + 0.5 * (parts[4][8] + parts[6][8]);
    const double rectilinearity = orthogonalEnergy
        / std::max(1e-9, orthogonalEnergy + diagonalEnergy);

    VentralStreamActivation result;
    result.lateralOccipitalObject = clamp01(4.0 * edgeEnergy + 1.5 * cornerEnergy);
    result.fusiformFace = clamp01(
        bilateralSymmetry * (0.35 + 2.2 * centreStructure));
    result.parahippocampalPlace = clamp01(
        rectilinearity * (2.8 * orthogonalEnergy + 2.0 * cornerEnergy));
    result.biologicalObject = clamp01(
        2.6 * diagonalEnergy + 1.4 * (1.0 - bilateralSymmetry)
            + 0.8 * centreStructure);
    return result;
}

ObjectPartModel objectPartModel(
    std::size_t width,
    std::size_t height,
    const std::span<const double> rgb,
    const ObjectFieldLayout& layout) {
    ObjectPartModel sums{};
    std::array<double, 10> luminanceSquares{};
    std::array<double, 10> weightSums{};
    for (std::size_t y = 0; y < height; ++y) {
        const double ny = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
        const std::size_t up = y == 0U ? y : y - 1U;
        const std::size_t down = std::min(height - 1U, y + 1U);
        for (std::size_t x = 0; x < width; ++x) {
            const double nx = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            const std::size_t left = x == 0U ? x : x - 1U;
            const std::size_t right = std::min(width - 1U, x + 1U);
            const std::size_t pixel = y * width + x;
            const double lum = clamp01(luminance(rgb, pixel));
            const double horizontal = 0.5 * std::abs(
                luminance(rgb, y * width + right) - luminance(rgb, y * width + left));
            const double vertical = 0.5 * std::abs(
                luminance(rgb, down * width + x) - luminance(rgb, up * width + x));
            const double descending = 0.5 * std::abs(
                luminance(rgb, down * width + right) - luminance(rgb, up * width + left));
            const double ascending = 0.5 * std::abs(
                luminance(rgb, up * width + right) - luminance(rgb, down * width + left));
            const double junction = std::sqrt(std::max(0.0, horizontal * vertical));
            std::array<double, 10> weights{};
            const double ellipse = std::sqrt(
                std::pow((nx - layout.objectCenterX)
                    / std::max(0.05, layout.objectHalfWidth), 2.0)
                + std::pow((ny - layout.objectCenterY)
                    / std::max(0.05, layout.objectHalfHeight), 2.0));
            const double outside = clamp01((ellipse - 0.72) / 0.58);
            weights[0] = 0.035 + 2.4 * outside * outside;
            for (std::size_t field = 0; field < layout.fields.size(); ++field) {
                const auto& definition = layout.fields[field];
                const double dx = (nx - definition[0]) / std::max(0.025, definition[2]);
                const double dy = (ny - definition[1]) / std::max(0.025, definition[3]);
                weights[field + 1U] = std::exp(-0.5 * (dx * dx + dy * dy));
            }
            for (std::size_t region = 0; region < weights.size(); ++region) {
                const double weight = weights[region] + 1e-9;
                sums[region][0] += weight * clamp01(rgb[pixel * 3U]);
                sums[region][1] += weight * clamp01(rgb[pixel * 3U + 1U]);
                sums[region][2] += weight * clamp01(rgb[pixel * 3U + 2U]);
                sums[region][4] += weight * horizontal;
                sums[region][5] += weight * vertical;
                sums[region][6] += weight * descending;
                sums[region][7] += weight * ascending;
                sums[region][8] += weight * junction;
                luminanceSquares[region] += weight * lum * lum;
                weightSums[region] += weight;
            }
        }
    }
    for (std::size_t region = 0; region < sums.size(); ++region) {
        const double divisor = std::max(1e-12, weightSums[region]);
        for (double& value : sums[region]) value /= divisor;
        const double mean = 0.2126 * sums[region][0]
            + 0.7152 * sums[region][1] + 0.0722 * sums[region][2];
        sums[region][3] = std::sqrt(std::max(
            0.0, luminanceSquares[region] / divisor - mean * mean));
    }
    return sums;
}

ObjectFieldLayout layoutForBounds(const std::array<double, 4>& bounds) {
    ObjectFieldLayout layout;
    const double left = bounds[0];
    const double top = bounds[1];
    const double right = bounds[2];
    const double bottom = bounds[3];
    const double objectWidth = std::max(0.04, right - left);
    const double objectHeight = std::max(0.04, bottom - top);
    layout.objectCenterX = 0.5 * (left + right);
    layout.objectCenterY = 0.5 * (top + bottom);
    layout.objectHalfWidth = std::max(0.04, 0.54 * objectWidth);
    layout.objectHalfHeight = std::max(0.04, 0.54 * objectHeight);
    const double sigmaX = std::max(0.025, 0.22 * objectWidth);
    const double sigmaY = std::max(0.025, 0.22 * objectHeight);
    std::size_t field = 0;
    for (const double fy : {0.23, 0.50, 0.77}) {
        for (const double fx : {0.23, 0.50, 0.77}) {
            layout.fields[field++] = {
                left + fx * objectWidth,
                top + fy * objectHeight,
                sigmaX,
                sigmaY,
            };
        }
    }
    return layout;
}

double projectedObjectFeature(
    const ObjectPartModel& parts,
    std::size_t projection) {
    double sum = 0.0;
    std::size_t flatIndex = 0;
    for (const auto& region : parts) {
        for (const double value : region) {
            std::uint64_t hash = 0x9e3779b97f4a7c15ULL
                ^ (static_cast<std::uint64_t>(projection + 1U) * 0xbf58476d1ce4e5b9ULL)
                ^ (static_cast<std::uint64_t>(flatIndex + 1U) * 0x94d049bb133111ebULL);
            hash ^= hash >> 30U;
            hash *= 0xbf58476d1ce4e5b9ULL;
            hash ^= hash >> 27U;
            const double sign = (hash & 1U) != 0U ? 1.0 : -1.0;
            sum += sign * (2.0 * clamp01(value) - 1.0);
            ++flatIndex;
        }
    }
    return std::tanh(sum / std::sqrt(static_cast<double>(flatIndex)));
}

std::vector<CorticalObjectCandidate> segmentObjects(
    const VisualPathwayConfig& config,
    std::size_t width,
    std::size_t height,
    const std::span<const double> rgb) {
    const std::size_t gridWidth = std::max<std::size_t>(
        4U, std::min(config.objectAnalysisWidth, width));
    const std::size_t gridHeight = std::max<std::size_t>(
        4U, std::min(config.objectAnalysisHeight, height));
    const std::size_t cellCount = gridWidth * gridHeight;
    struct Cell {
        std::array<double, 3> color{};
        double luminance = 0.0;
        double saliency = 0.0;
    };
    std::vector<Cell> cells(cellCount);

    for (std::size_t gy = 0; gy < gridHeight; ++gy) {
        const std::size_t beginY = gy * height / gridHeight;
        const std::size_t endY = std::max(beginY + 1U, (gy + 1U) * height / gridHeight);
        for (std::size_t gx = 0; gx < gridWidth; ++gx) {
            const std::size_t beginX = gx * width / gridWidth;
            const std::size_t endX = std::max(beginX + 1U, (gx + 1U) * width / gridWidth);
            auto& cell = cells[gy * gridWidth + gx];
            std::size_t samples = 0;
            for (std::size_t y = beginY; y < std::min(height, endY); ++y) {
                for (std::size_t x = beginX; x < std::min(width, endX); ++x) {
                    const std::size_t pixel = y * width + x;
                    cell.color[0] += clamp01(rgb[pixel * 3U]);
                    cell.color[1] += clamp01(rgb[pixel * 3U + 1U]);
                    cell.color[2] += clamp01(rgb[pixel * 3U + 2U]);
                    ++samples;
                }
            }
            const double divisor = static_cast<double>(std::max<std::size_t>(1U, samples));
            for (double& value : cell.color) value /= divisor;
            cell.luminance = 0.2126 * cell.color[0]
                + 0.7152 * cell.color[1] + 0.0722 * cell.color[2];
        }
    }

    std::array<double, 3> background{};
    std::size_t borderSamples = 0;
    for (std::size_t gy = 0; gy < gridHeight; ++gy) {
        for (std::size_t gx = 0; gx < gridWidth; ++gx) {
            if (gx != 0U && gy != 0U && gx + 1U != gridWidth && gy + 1U != gridHeight) continue;
            const auto& color = cells[gy * gridWidth + gx].color;
            for (std::size_t channel = 0; channel < 3U; ++channel) background[channel] += color[channel];
            ++borderSamples;
        }
    }
    for (double& value : background) {
        value /= static_cast<double>(std::max<std::size_t>(1U, borderSamples));
    }
    double backgroundVariance = 0.0;
    for (std::size_t gy = 0; gy < gridHeight; ++gy) {
        for (std::size_t gx = 0; gx < gridWidth; ++gx) {
            if (gx != 0U && gy != 0U && gx + 1U != gridWidth && gy + 1U != gridHeight) continue;
            const auto& color = cells[gy * gridWidth + gx].color;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const double delta = color[channel] - background[channel];
                backgroundVariance += delta * delta;
            }
        }
    }
    const double backgroundSigma = std::sqrt(
        backgroundVariance
        / static_cast<double>(std::max<std::size_t>(1U, borderSamples * 3U)));

    const auto cellAt = [&](std::ptrdiff_t x, std::ptrdiff_t y) -> const Cell& {
        x = std::clamp<std::ptrdiff_t>(x, 0, static_cast<std::ptrdiff_t>(gridWidth - 1U));
        y = std::clamp<std::ptrdiff_t>(y, 0, static_cast<std::ptrdiff_t>(gridHeight - 1U));
        return cells[static_cast<std::size_t>(y) * gridWidth + static_cast<std::size_t>(x)];
    };
    double saliencyMean = 0.0;
    for (std::size_t gy = 0; gy < gridHeight; ++gy) {
        for (std::size_t gx = 0; gx < gridWidth; ++gx) {
            auto& cell = cells[gy * gridWidth + gx];
            double colorDistance = 0.0;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const double delta = cell.color[channel] - background[channel];
                colorDistance += delta * delta;
            }
            colorDistance = std::sqrt(colorDistance / 3.0);
            const auto x = static_cast<std::ptrdiff_t>(gx);
            const auto y = static_cast<std::ptrdiff_t>(gy);
            const double horizontal = 0.5 * std::abs(
                cellAt(x + 1, y).luminance - cellAt(x - 1, y).luminance);
            const double vertical = 0.5 * std::abs(
                cellAt(x, y + 1).luminance - cellAt(x, y - 1).luminance);
            const double gradient = std::sqrt(horizontal * horizontal + vertical * vertical);
            cell.saliency = clamp01(1.65 * colorDistance + 1.85 * gradient);
            saliencyMean += cell.saliency;
        }
    }
    saliencyMean /= static_cast<double>(cellCount);
    double saliencyVariance = 0.0;
    for (const auto& cell : cells) {
        const double delta = cell.saliency - saliencyMean;
        saliencyVariance += delta * delta;
    }
    const double saliencySigma = std::sqrt(saliencyVariance / static_cast<double>(cellCount));
    const double adaptiveThreshold = std::clamp(
        std::max(config.objectSaliencyThreshold, saliencyMean + 0.20 * saliencySigma),
        0.06, 0.55);
    const double backgroundDistanceThreshold = std::clamp(
        std::max(0.075, 2.4 * backgroundSigma), 0.075, 0.30);

    std::vector<unsigned char> active(cellCount, 0U);
    for (std::size_t index = 0; index < cellCount; ++index) {
        double colorDistance = 0.0;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const double delta = cells[index].color[channel] - background[channel];
            colorDistance += delta * delta;
        }
        colorDistance = std::sqrt(colorDistance / 3.0);
        active[index] = (cells[index].saliency >= adaptiveThreshold
                || colorDistance >= backgroundDistanceThreshold)
            ? 1U : 0U;
    }

    // Local recurrent spread closes small holes inside an object contour.
    std::vector<unsigned char> spread = active;
    for (std::size_t gy = 0; gy < gridHeight; ++gy) {
        for (std::size_t gx = 0; gx < gridWidth; ++gx) {
            if (active[gy * gridWidth + gx] == 0U) continue;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (std::abs(dx) + std::abs(dy) > 1) continue;
                    const auto nx = static_cast<std::ptrdiff_t>(gx) + dx;
                    const auto ny = static_cast<std::ptrdiff_t>(gy) + dy;
                    if (nx < 0 || ny < 0
                        || nx >= static_cast<std::ptrdiff_t>(gridWidth)
                        || ny >= static_cast<std::ptrdiff_t>(gridHeight)) continue;
                    spread[static_cast<std::size_t>(ny) * gridWidth
                        + static_cast<std::size_t>(nx)] = 1U;
                }
            }
        }
    }

    struct Component {
        std::size_t left = 0;
        std::size_t top = 0;
        std::size_t right = 0;
        std::size_t bottom = 0;
        std::size_t cells = 0;
        double saliency = 0.0;
        bool touchesBorder = false;
    };
    std::vector<Component> components;
    std::vector<unsigned char> visited(cellCount, 0U);
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
    }};
    for (std::size_t seed = 0; seed < cellCount; ++seed) {
        if (spread[seed] == 0U || visited[seed] != 0U) continue;
        Component component;
        component.left = component.right = seed % gridWidth;
        component.top = component.bottom = seed / gridWidth;
        std::queue<std::size_t> queue;
        queue.push(seed);
        visited[seed] = 1U;
        while (!queue.empty()) {
            const std::size_t index = queue.front();
            queue.pop();
            const std::size_t x = index % gridWidth;
            const std::size_t y = index / gridWidth;
            component.left = std::min(component.left, x);
            component.right = std::max(component.right, x);
            component.top = std::min(component.top, y);
            component.bottom = std::max(component.bottom, y);
            component.saliency += cells[index].saliency;
            ++component.cells;
            component.touchesBorder = component.touchesBorder
                || x == 0U || y == 0U || x + 1U == gridWidth || y + 1U == gridHeight;
            for (const auto& direction : directions) {
                const auto nx = static_cast<std::ptrdiff_t>(x) + direction[0];
                const auto ny = static_cast<std::ptrdiff_t>(y) + direction[1];
                if (nx < 0 || ny < 0
                    || nx >= static_cast<std::ptrdiff_t>(gridWidth)
                    || ny >= static_cast<std::ptrdiff_t>(gridHeight)) continue;
                const std::size_t neighbour = static_cast<std::size_t>(ny) * gridWidth
                    + static_cast<std::size_t>(nx);
                if (spread[neighbour] != 0U && visited[neighbour] == 0U) {
                    visited[neighbour] = 1U;
                    queue.push(neighbour);
                }
            }
        }
        const double fraction = static_cast<double>(component.cells)
            / static_cast<double>(cellCount);
        if (fraction < config.minimumObjectAreaFraction) continue;
        if (component.touchesBorder && fraction > 0.62) continue;
        component.saliency /= static_cast<double>(component.cells);
        components.push_back(component);
    }

    std::stable_sort(components.begin(), components.end(),
        [](const Component& left, const Component& right) {
            const double leftScore = left.saliency * std::sqrt(static_cast<double>(left.cells));
            const double rightScore = right.saliency * std::sqrt(static_cast<double>(right.cells));
            return leftScore > rightScore;
        });
    if (components.size() > config.maximumObjects) components.resize(config.maximumObjects);

    std::vector<CorticalObjectCandidate> objects;
    objects.reserve(components.size());
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& component = components[index];
        const std::size_t expandedLeft = component.left == 0U ? 0U : component.left - 1U;
        const std::size_t expandedTop = component.top == 0U ? 0U : component.top - 1U;
        const std::size_t expandedRight = std::min(gridWidth - 1U, component.right + 1U);
        const std::size_t expandedBottom = std::min(gridHeight - 1U, component.bottom + 1U);
        const std::array<double, 4> bounds{
            static_cast<double>(expandedLeft) / static_cast<double>(gridWidth),
            static_cast<double>(expandedTop) / static_cast<double>(gridHeight),
            static_cast<double>(expandedRight + 1U) / static_cast<double>(gridWidth),
            static_cast<double>(expandedBottom + 1U) / static_cast<double>(gridHeight),
        };
        const auto layout = layoutForBounds(bounds);
        const auto parts = objectPartModel(width, height, rgb, layout);
        objects.push_back(CorticalObjectCandidate{
            .index = index,
            .bounds = bounds,
            .center = {0.5 * (bounds[0] + bounds[2]), 0.5 * (bounds[1] + bounds[3])},
            .areaFraction = (bounds[2] - bounds[0]) * (bounds[3] - bounds[1]),
            .saliency = clamp01(component.saliency),
            .parts = parts,
            .ventral = ventralActivation(parts),
        });
    }
    return objects;
}

std::vector<double> encodeObjectCortexEvents(
    const std::vector<CorticalObjectCandidate>& objects) {
    constexpr std::size_t projectionCount = 8U;
    constexpr std::size_t valuesPerObject = 16U;
    std::vector<double> events;
    events.reserve(objects.size() * valuesPerObject);
    // Divisive normalization keeps total cortical drive approximately bounded
    // when a crowded scene contains many independently segmented objects.
    const double gain = objects.empty()
        ? 1.0 : 1.0 / std::sqrt(static_cast<double>(objects.size()));
    for (const auto& object : objects) {
        events.push_back(gain * (2.0 * clamp01(object.center[0]) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.center[1]) - 1.0));
        events.push_back(gain * (2.0 * clamp01(std::sqrt(object.areaFraction)) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.saliency) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.ventral.lateralOccipitalObject) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.ventral.fusiformFace) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.ventral.parahippocampalPlace) - 1.0));
        events.push_back(gain * (2.0 * clamp01(object.ventral.biologicalObject) - 1.0));
        for (std::size_t projection = 0; projection < projectionCount; ++projection) {
            events.push_back(gain * projectedObjectFeature(object.parts, projection));
        }
    }
    return events;
}

} // namespace

void VisualPathwayConfig::validate() const {
    if (retinalWidth < 2U || retinalWidth > 32U
        || retinalHeight < 2U || retinalHeight > 32U) {
        throw std::invalid_argument("Retinal dimensions must be in [2, 32]");
    }
    if (!std::isfinite(topDownMaximumGain)
        || topDownMaximumGain < 0.0 || topDownMaximumGain > 0.5) {
        throw std::invalid_argument("Visual top-down gain must be in [0, 0.5]");
    }
    if (objectAnalysisWidth < 4U || objectAnalysisWidth > 128U
        || objectAnalysisHeight < 4U || objectAnalysisHeight > 128U) {
        throw std::invalid_argument("Visual object analysis dimensions must be in [4, 128]");
    }
    if (maximumObjects == 0U || maximumObjects > 64U) {
        throw std::invalid_argument("Visual maximumObjects must be in [1, 64]");
    }
    if (!std::isfinite(objectSaliencyThreshold)
        || objectSaliencyThreshold < 0.0 || objectSaliencyThreshold > 1.0) {
        throw std::invalid_argument("Visual object saliency threshold must be in [0, 1]");
    }
    if (!std::isfinite(minimumObjectAreaFraction)
        || minimumObjectAreaFraction <= 0.0 || minimumObjectAreaFraction > 0.25) {
        throw std::invalid_argument("Visual minimum object area fraction must be in (0, 0.25]");
    }
}

VisualPathway::VisualPathway(VisualPathwayConfig config)
    : config_(std::move(config)) {
    config_.validate();
}

ObjectFieldLayout VisualPathway::defaultObjectFields() {
    ObjectFieldLayout layout;
    std::size_t field = 0;
    for (const double y : {0.30, 0.52, 0.72}) {
        for (const double x : {0.31, 0.50, 0.69}) {
            layout.fields[field++] = {x, y, 0.19, 0.16};
        }
    }
    return layout;
}

VisualPercept VisualPathway::perceive(
    std::size_t width,
    std::size_t height,
    std::span<const double> interleavedRgb) const {
    const auto layout = defaultObjectFields();
    return perceive(width, height, interleavedRgb, layout, nullptr);
}

VisualPercept VisualPathway::perceive(
    std::size_t width,
    std::size_t height,
    std::span<const double> rgb,
    const ObjectFieldLayout& layout,
    const TopDownVisualPrior* topDown) const {
    if (width < 2U || height < 2U || rgb.size() != width * height * 3U) {
        throw std::invalid_argument("Visual pathway received an invalid RGB retina frame");
    }
    VisualPercept percept;
    percept.sourceWidth = width;
    percept.sourceHeight = height;
    const std::size_t retinalCells = config_.retinalWidth * config_.retinalHeight;
    percept.photoreceptors.resize(retinalCells);
    percept.retinalGanglionCells.resize(retinalCells);
    percept.v1OrientationCells.resize(retinalCells);
    percept.opticNerveEvents.reserve(retinalCells * 4U);

    for (std::size_t cellY = 0; cellY < config_.retinalHeight; ++cellY) {
        for (std::size_t cellX = 0; cellX < config_.retinalWidth; ++cellX) {
            const std::size_t beginX = cellX * width / config_.retinalWidth;
            const std::size_t endX = (cellX + 1U) * width / config_.retinalWidth;
            const std::size_t beginY = cellY * height / config_.retinalHeight;
            const std::size_t endY = (cellY + 1U) * height / config_.retinalHeight;
            std::array<double, 4> cone{};
            std::size_t samples = 0;
            for (std::size_t y = beginY; y < endY; ++y) {
                for (std::size_t x = beginX; x < endX; ++x) {
                    const std::size_t pixel = y * width + x;
                    cone[0] += clamp01(rgb[pixel * 3U]);
                    cone[1] += clamp01(rgb[pixel * 3U + 1U]);
                    cone[2] += clamp01(rgb[pixel * 3U + 2U]);
                    cone[3] += clamp01(luminance(rgb, pixel));
                    ++samples;
                }
            }
            for (double& value : cone) {
                value /= static_cast<double>(std::max<std::size_t>(1U, samples));
            }
            percept.photoreceptors[cellY * config_.retinalWidth + cellX] = cone;
        }
    }

    const auto retinalLuminance = [&](std::ptrdiff_t x, std::ptrdiff_t y) {
        x = std::clamp<std::ptrdiff_t>(x, 0,
            static_cast<std::ptrdiff_t>(config_.retinalWidth - 1U));
        y = std::clamp<std::ptrdiff_t>(y, 0,
            static_cast<std::ptrdiff_t>(config_.retinalHeight - 1U));
        return percept.photoreceptors[
            static_cast<std::size_t>(y) * config_.retinalWidth
                + static_cast<std::size_t>(x)][3];
    };
    for (std::size_t cellY = 0; cellY < config_.retinalHeight; ++cellY) {
        for (std::size_t cellX = 0; cellX < config_.retinalWidth; ++cellX) {
            const std::size_t index = cellY * config_.retinalWidth + cellX;
            const auto& cone = percept.photoreceptors[index];
            const auto x = static_cast<std::ptrdiff_t>(cellX);
            const auto y = static_cast<std::ptrdiff_t>(cellY);
            const double surround = 0.25 * (
                retinalLuminance(x - 1, y) + retinalLuminance(x + 1, y)
                + retinalLuminance(x, y - 1) + retinalLuminance(x, y + 1));
            const double centreSurround = cone[3] - surround;
            percept.retinalGanglionCells[index] = {
                clamp01(0.5 + centreSurround),
                clamp01(0.5 - centreSurround),
                clamp01(0.5 + 0.5 * (cone[0] - cone[1])),
                clamp01(0.5 + 0.5 * (cone[2] - 0.5 * (cone[0] + cone[1]))),
                cone[3],
            };
            const double horizontal = 0.5 * std::abs(
                retinalLuminance(x + 1, y) - retinalLuminance(x - 1, y));
            const double vertical = 0.5 * std::abs(
                retinalLuminance(x, y + 1) - retinalLuminance(x, y - 1));
            const double descending = 0.5 * std::abs(
                retinalLuminance(x + 1, y + 1) - retinalLuminance(x - 1, y - 1));
            const double ascending = 0.5 * std::abs(
                retinalLuminance(x + 1, y - 1) - retinalLuminance(x - 1, y + 1));
            const double junction = std::sqrt(std::max(0.0, horizontal * vertical));
            percept.v1OrientationCells[index] = {
                horizontal, vertical, descending, ascending, junction,
            };
            percept.opticNerveEvents.push_back(percept.retinalGanglionCells[index][0]);
            percept.opticNerveEvents.push_back(percept.retinalGanglionCells[index][1]);
            percept.opticNerveEvents.push_back(percept.retinalGanglionCells[index][2]);
            percept.opticNerveEvents.push_back(percept.retinalGanglionCells[index][3]);
        }
    }

    ObjectPartModel sums{};
    std::array<double, 10> luminanceSquares{};
    std::array<double, 10> weightSums{};
    for (std::size_t y = 0; y < height; ++y) {
        const double ny = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
        const std::size_t up = y == 0U ? y : y - 1U;
        const std::size_t down = std::min(height - 1U, y + 1U);
        for (std::size_t x = 0; x < width; ++x) {
            const double nx = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            const std::size_t left = x == 0U ? x : x - 1U;
            const std::size_t right = std::min(width - 1U, x + 1U);
            const std::size_t pixel = y * width + x;
            const double lum = luminance(rgb, pixel);
            const double horizontal = 0.5 * std::abs(
                luminance(rgb, y * width + right) - luminance(rgb, y * width + left));
            const double vertical = 0.5 * std::abs(
                luminance(rgb, down * width + x) - luminance(rgb, up * width + x));
            const double descending = 0.5 * std::abs(
                luminance(rgb, down * width + right) - luminance(rgb, up * width + left));
            const double ascending = 0.5 * std::abs(
                luminance(rgb, up * width + right) - luminance(rgb, down * width + left));
            const double junction = std::sqrt(std::max(0.0, horizontal * vertical));
            std::array<double, 10> weights{};
            const double ellipse = std::sqrt(
                std::pow((nx - layout.objectCenterX)
                    / std::max(0.12, layout.objectHalfWidth), 2.0)
                + std::pow((ny - layout.objectCenterY)
                    / std::max(0.16, layout.objectHalfHeight), 2.0));
            const double outside = clamp01((ellipse - 0.72) / 0.58);
            weights[0] = 0.035 + 2.4 * outside * outside;
            for (std::size_t field = 0; field < layout.fields.size(); ++field) {
                const auto& definition = layout.fields[field];
                const double dx = (nx - definition[0]) / std::max(0.04, definition[2]);
                const double dy = (ny - definition[1]) / std::max(0.04, definition[3]);
                weights[field + 1U] = std::exp(-0.5 * (dx * dx + dy * dy));
            }
            for (std::size_t region = 0; region < weights.size(); ++region) {
                const double weight = weights[region] + 1e-9;
                sums[region][0] += weight * clamp01(rgb[pixel * 3U]);
                sums[region][1] += weight * clamp01(rgb[pixel * 3U + 1U]);
                sums[region][2] += weight * clamp01(rgb[pixel * 3U + 2U]);
                sums[region][4] += weight * horizontal;
                sums[region][5] += weight * vertical;
                sums[region][6] += weight * descending;
                sums[region][7] += weight * ascending;
                sums[region][8] += weight * junction;
                luminanceSquares[region] += weight * lum * lum;
                weightSums[region] += weight;
            }
        }
    }
    for (std::size_t region = 0; region < sums.size(); ++region) {
        const double divisor = std::max(1e-12, weightSums[region]);
        for (double& value : sums[region]) value /= divisor;
        const double mean = 0.2126 * sums[region][0]
            + 0.7152 * sums[region][1] + 0.0722 * sums[region][2];
        sums[region][3] = std::sqrt(std::max(
            0.0, luminanceSquares[region] / divisor - mean * mean));
    }
    percept.bottomUpParts = sums;
    percept.integratedParts = sums;
    if (topDown != nullptr) {
        percept.topDownGainApplied = std::clamp(
            topDown->gain, 0.0, config_.topDownMaximumGain);
        for (std::size_t region = 0; region < sums.size(); ++region) {
            for (std::size_t feature = 0; feature < sums[region].size(); ++feature) {
                percept.integratedParts[region][feature] =
                    (1.0 - percept.topDownGainApplied) * sums[region][feature]
                    + percept.topDownGainApplied
                        * topDown->expectedParts[region][feature];
            }
        }
    }
    percept.ventral = ventralActivation(percept.integratedParts);
    percept.objects = segmentObjects(config_, width, height, rgb);
    percept.objectCortexEvents = encodeObjectCortexEvents(percept.objects);
    return percept;
}

} // namespace tatarus::neuro::vision
