#include "tatarus/imaginatio.hpp"
#include "tatarus/visual_pathway.hpp"
#include "tatarus/ocular_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tatarus {
namespace {

constexpr std::array<char, 8> kImaginatioMagicV1{'T', 'I', 'M', 'A', 'G', '0', '1', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV2{'T', 'I', 'M', 'A', 'G', '0', '2', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV3{'T', 'I', 'M', 'A', 'G', '0', '3', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV4{'T', 'I', 'M', 'A', 'G', '0', '4', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV5{'T', 'I', 'M', 'A', 'G', '0', '5', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV7{'T', 'I', 'M', 'A', 'G', '0', '7', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV8{'T', 'I', 'M', 'A', 'G', '0', '8', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV9{'T', 'I', 'M', 'A', 'G', '0', '9', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV10{'T', 'I', 'M', 'A', 'G', '1', '0', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV11{'T', 'I', 'M', 'A', 'G', '1', '1', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV12{'T', 'I', 'M', 'A', 'G', '1', '2', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV13{'T', 'I', 'M', 'A', 'G', '1', '3', '\0'};
constexpr std::array<char, 8> kImaginatioMagicV14{'T', 'I', 'M', 'A', 'G', '1', '4', '\0'};
constexpr std::uint64_t kMaximumSnapshotItems = 4'000'000ULL;
constexpr double kPi = 3.14159265358979323846;

double clamp01(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

std::string normalizedSymbol(std::string_view raw) {
    std::size_t first = 0;
    while (first < raw.size()
           && std::isspace(static_cast<unsigned char>(raw[first])) != 0) {
        ++first;
    }
    std::size_t last = raw.size();
    while (last > first
           && std::isspace(static_cast<unsigned char>(raw[last - 1])) != 0) {
        --last;
    }
    std::string result(raw.substr(first, last - first));
    for (char& value : result) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte >= static_cast<unsigned char>('a')
            && byte <= static_cast<unsigned char>('z')) {
            value = static_cast<char>(byte - static_cast<unsigned char>('a')
                + static_cast<unsigned char>('A'));
        }
    }
    return result;
}

std::uint64_t fnv1a(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1 : hash;
}

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value == 0U ? 1U : value;
}

std::uint64_t canvasSignature(const VisualCanvas& canvas) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mixByte = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    for (std::size_t shift = 0; shift < sizeof(std::size_t); ++shift) {
        mixByte(static_cast<std::uint8_t>((canvas.width() >> (shift * 8U)) & 0xffU));
        mixByte(static_cast<std::uint8_t>((canvas.height() >> (shift * 8U)) & 0xffU));
    }
    for (const double pixel : canvas.colorPixels()) {
        const auto quantized = static_cast<std::uint16_t>(
            std::llround(clamp01(pixel) * 4095.0));
        mixByte(static_cast<std::uint8_t>(quantized & 0xffU));
        mixByte(static_cast<std::uint8_t>((quantized >> 8U) & 0xffU));
    }
    return hash == 0 ? 1 : hash;
}

const char* stageName(ImaginationStage stage) {
    switch (stage) {
        case ImaginationStage::ObserveAndTrace: return "observe_and_trace";
        case ImaginationStage::MemoryRecall: return "memory_recall";
        case ImaginationStage::SymbolRecall: return "symbol_recall";
        case ImaginationStage::Composition: return "composition";
        case ImaginationStage::CategoryFusion: return "category_fusion";
        case ImaginationStage::RelationalScene: return "relational_scene";
        case ImaginationStage::ProspectiveImagination: return "prospective_imagination";
    }
    return "unknown";
}

const char* relationName(SceneRelationKind relation) {
    switch (relation) {
        case SceneRelationKind::LeftOf: return "left_of";
        case SceneRelationKind::RightOf: return "right_of";
        case SceneRelationKind::Above: return "above";
        case SceneRelationKind::Below: return "below";
        case SceneRelationKind::InFrontOf: return "in_front_of";
        case SceneRelationKind::Behind: return "behind";
        case SceneRelationKind::Inside: return "inside";
        case SceneRelationKind::Contains: return "contains";
        case SceneRelationKind::Touching: return "touching";
        case SceneRelationKind::Overlapping: return "overlapping";
        case SceneRelationKind::Near: return "near";
        case SceneRelationKind::Far: return "far";
        case SceneRelationKind::LookingAt: return "looking_at";
        case SceneRelationKind::ConnectedTo: return "connected_to";
    }
    return "unknown";
}

const char* sceneActionName(SceneActionKind action) {
    switch (action) {
        case SceneActionKind::Move: return "move";
        case SceneActionKind::Kick: return "kick";
        case SceneActionKind::Push: return "push";
        case SceneActionKind::Pull: return "pull";
        case SceneActionKind::Fall: return "fall";
        case SceneActionKind::Rise: return "rise";
        case SceneActionKind::Approach: return "approach";
        case SceneActionKind::Depart: return "depart";
        case SceneActionKind::Custom: return "custom";
    }
    return "unknown";
}

const char* actionName(PaintActionKind action) {
    switch (action) {
        case PaintActionKind::MoveNorth: return "move_north";
        case PaintActionKind::MoveEast: return "move_east";
        case PaintActionKind::MoveSouth: return "move_south";
        case PaintActionKind::MoveWest: return "move_west";
        case PaintActionKind::Paint: return "paint";
        case PaintActionKind::Erase: return "erase";
        case PaintActionKind::Brighter: return "brighter";
        case PaintActionKind::Darker: return "darker";
        case PaintActionKind::BrushSmall: return "brush_small";
        case PaintActionKind::BrushLarge: return "brush_large";
    }
    return "unknown";
}

AssemblyId representedAssembly(const ObserveResult& observation) {
    if (observation.assemblyId != 0) return observation.assemblyId;
    if (observation.prospection.lastAssemblyId != 0) {
        return observation.prospection.lastAssemblyId;
    }
    return observation.prospection.predictedAssemblyId;
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(byte) << std::dec;
                } else {
                    out << static_cast<char>(byte);
                }
        }
    }
    return out.str();
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t index = 0; index < bytes.size(); index += 3U) {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second = index + 1U < bytes.size() ? bytes[index + 1U] : 0U;
        const std::uint32_t third = index + 2U < bytes.size() ? bytes[index + 2U] : 0U;
        const std::uint32_t packed = (first << 16U) | (second << 8U) | third;
        output.push_back(alphabet[(packed >> 18U) & 0x3fU]);
        output.push_back(alphabet[(packed >> 12U) & 0x3fU]);
        output.push_back(index + 1U < bytes.size() ? alphabet[(packed >> 6U) & 0x3fU] : '=');
        output.push_back(index + 2U < bytes.size() ? alphabet[packed & 0x3fU] : '=');
    }
    return output;
}

std::vector<std::uint8_t> canvasRgb8(const VisualCanvas& canvas) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(canvas.colorPixels().size());
    for (const double channel : canvas.colorPixels()) {
        bytes.push_back(static_cast<std::uint8_t>(
            std::llround(clamp01(channel) * 255.0)));
    }
    return bytes;
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!output) throw std::runtime_error("TATARUS IMAGINATIO snapshot write failed");
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!input) throw std::runtime_error("TATARUS IMAGINATIO snapshot is truncated");
}

void writeString(std::ostream& output, const std::string& value) {
    const auto size = static_cast<std::uint64_t>(value.size());
    writePod(output, size);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("TATARUS IMAGINATIO string write failed");
}

std::string readString(std::istream& input) {
    std::uint64_t size = 0;
    readPod(input, size);
    if (size > kMaximumSnapshotItems) {
        throw std::runtime_error("TATARUS IMAGINATIO snapshot string is implausibly large");
    }
    std::string value(static_cast<std::size_t>(size), '\0');
    input.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!input) throw std::runtime_error("TATARUS IMAGINATIO snapshot string is truncated");
    return value;
}

void appendMoveActions(
    std::vector<PaintAction>& output,
    std::size_t& x,
    std::size_t& y,
    std::size_t targetX,
    std::size_t targetY) {
    const auto append = [&output](PaintActionKind kind, std::size_t count) {
        while (count > 0) {
            const auto part = static_cast<std::uint16_t>(std::min<std::size_t>(
                count, std::numeric_limits<std::uint16_t>::max()));
            output.push_back(PaintAction{.kind = kind, .repetitions = part});
            count -= part;
        }
    };
    if (targetY < y) append(PaintActionKind::MoveNorth, y - targetY);
    if (targetY > y) append(PaintActionKind::MoveSouth, targetY - y);
    if (targetX < x) append(PaintActionKind::MoveWest, x - targetX);
    if (targetX > x) append(PaintActionKind::MoveEast, targetX - x);
    x = targetX;
    y = targetY;
}

} // namespace

VisualCanvas::VisualCanvas(std::size_t width, std::size_t height)
    : width_(width), height_(height), cursorX_(width / 2U), cursorY_(height / 2U) {
    if (width < 2 || height < 2 || width > 1024 || height > 1024
        || width > 1'048'576U / height) {
        throw std::invalid_argument("VisualCanvas dimensions must be in [2, 1024]");
    }
    pixels_.assign(width * height, 0.0);
    colorPixels_.assign(width * height * 3U, 0.0);
}

std::size_t VisualCanvas::width() const noexcept { return width_; }
std::size_t VisualCanvas::height() const noexcept { return height_; }
std::size_t VisualCanvas::cursorX() const noexcept { return cursorX_; }
std::size_t VisualCanvas::cursorY() const noexcept { return cursorY_; }
std::size_t VisualCanvas::brushRadius() const noexcept { return brushRadius_; }
double VisualCanvas::brushTone() const noexcept {
    return clamp01(0.2126 * brushColor_[0]
        + 0.7152 * brushColor_[1] + 0.0722 * brushColor_[2]);
}
std::array<double, 3> VisualCanvas::brushColor() const noexcept { return brushColor_; }
const std::vector<double>& VisualCanvas::pixels() const noexcept { return pixels_; }
const std::vector<double>& VisualCanvas::colorPixels() const noexcept { return colorPixels_; }

void VisualCanvas::updateLuminance(std::size_t index) noexcept {
    const std::size_t colorIndex = index * 3U;
    pixels_[index] = clamp01(0.2126 * colorPixels_[colorIndex]
        + 0.7152 * colorPixels_[colorIndex + 1U]
        + 0.0722 * colorPixels_[colorIndex + 2U]);
}

double VisualCanvas::pixel(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("VisualCanvas pixel is outside the canvas");
    return pixels_[y * width_ + x];
}

std::array<double, 3> VisualCanvas::colorPixel(
    std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("VisualCanvas colour pixel is outside the canvas");
    }
    const std::size_t index = (y * width_ + x) * 3U;
    return {colorPixels_[index], colorPixels_[index + 1U], colorPixels_[index + 2U]};
}

void VisualCanvas::setPixel(std::size_t x, std::size_t y, double value) {
    if (x >= width_ || y >= height_) throw std::out_of_range("VisualCanvas pixel is outside the canvas");
    const std::size_t index = y * width_ + x;
    const double bounded = clamp01(value);
    pixels_[index] = bounded;
    colorPixels_[index * 3U] = bounded;
    colorPixels_[index * 3U + 1U] = bounded;
    colorPixels_[index * 3U + 2U] = bounded;
}

void VisualCanvas::setColorPixel(
    std::size_t x, std::size_t y,
    double red, double green, double blue) {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("VisualCanvas colour pixel is outside the canvas");
    }
    const std::size_t index = y * width_ + x;
    colorPixels_[index * 3U] = clamp01(red);
    colorPixels_[index * 3U + 1U] = clamp01(green);
    colorPixels_[index * 3U + 2U] = clamp01(blue);
    updateLuminance(index);
}

void VisualCanvas::setBrushColor(double red, double green, double blue) {
    brushColor_ = {clamp01(red), clamp01(green), clamp01(blue)};
}

void VisualCanvas::setCursor(std::size_t x, std::size_t y) {
    if (x >= width_ || y >= height_) throw std::out_of_range("VisualCanvas cursor is outside the canvas");
    cursorX_ = x;
    cursorY_ = y;
}

void VisualCanvas::clear(double value) {
    const double bounded = clamp01(value);
    std::fill(pixels_.begin(), pixels_.end(), bounded);
    std::fill(colorPixels_.begin(), colorPixels_.end(), bounded);
    cursorX_ = width_ / 2U;
    cursorY_ = height_ / 2U;
    brushRadius_ = 0;
    brushColor_ = {1.0, 1.0, 1.0};
}

void VisualCanvas::clearColor(double red, double green, double blue) {
    const std::array<double, 3> color{clamp01(red), clamp01(green), clamp01(blue)};
    for (std::size_t index = 0; index < pixels_.size(); ++index) {
        colorPixels_[index * 3U] = color[0];
        colorPixels_[index * 3U + 1U] = color[1];
        colorPixels_[index * 3U + 2U] = color[2];
        updateLuminance(index);
    }
    cursorX_ = width_ / 2U;
    cursorY_ = height_ / 2U;
    brushRadius_ = 0;
    brushColor_ = {1.0, 1.0, 1.0};
}

void VisualCanvas::apply(const PaintAction& action) {
    const auto repetitions = std::max<std::uint16_t>(1, action.repetitions);
    const bool explicitPigment = action.red >= 0.0
        && action.green >= 0.0 && action.blue >= 0.0;
    const std::array<double, 3> pigment = explicitPigment
        ? std::array<double, 3>{clamp01(action.red), clamp01(action.green), clamp01(action.blue)}
        : brushColor_;
    if (explicitPigment && action.kind == PaintActionKind::Paint) {
        brushColor_ = pigment;
    }
    const auto paintDisk = [&](bool erase) {
        if (!erase && action.patchWidth > 0U && action.patchHeight > 0U) {
            const std::size_t patchWidth = std::min<std::size_t>(8U, action.patchWidth);
            const std::size_t patchHeight = std::min<std::size_t>(8U, action.patchHeight);
            const double strength = clamp01(action.intensity);
            for (std::size_t patchY = 0; patchY < patchHeight; ++patchY) {
                for (std::size_t patchX = 0; patchX < patchWidth; ++patchX) {
                    const std::size_t x = cursorX_ + patchX;
                    const std::size_t y = cursorY_ + patchY;
                    if (x >= width_ || y >= height_) continue;
                    const std::size_t pixelIndex = y * width_ + x;
                    const std::size_t colorIndex = pixelIndex * 3U;
                    const std::size_t patchIndex = (patchY * patchWidth + patchX) * 3U;
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        auto& destination = colorPixels_[colorIndex + channel];
                        const double source = static_cast<double>(
                            action.patchRgb[patchIndex + channel]) / 255.0;
                        destination = clamp01(
                            destination * (1.0 - strength) + source * strength);
                    }
                    updateLuminance(pixelIndex);
                }
            }
            return;
        }
        const auto radius = static_cast<std::ptrdiff_t>(brushRadius_);
        for (std::ptrdiff_t dy = -radius; dy <= radius; ++dy) {
            for (std::ptrdiff_t dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) continue;
                const auto x = static_cast<std::ptrdiff_t>(cursorX_) + dx;
                const auto y = static_cast<std::ptrdiff_t>(cursorY_) + dy;
                if (x < 0 || y < 0
                    || x >= static_cast<std::ptrdiff_t>(width_)
                    || y >= static_cast<std::ptrdiff_t>(height_)) {
                    continue;
                }
                const std::size_t pixelIndex = static_cast<std::size_t>(y) * width_
                    + static_cast<std::size_t>(x);
                const std::size_t colorIndex = pixelIndex * 3U;
                const double strength = clamp01(action.intensity);
                if (erase) {
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        colorPixels_[colorIndex + channel] *= 1.0 - strength;
                    }
                } else {
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        auto& destination = colorPixels_[colorIndex + channel];
                        destination = clamp01(
                            destination * (1.0 - strength) + pigment[channel] * strength);
                    }
                }
                updateLuminance(pixelIndex);
            }
        }
    };

    switch (action.kind) {
        case PaintActionKind::MoveNorth:
            for (std::uint16_t i = 0; i < repetitions; ++i) {
                if (cursorY_ > 0) --cursorY_;
            }
            break;
        case PaintActionKind::MoveEast:
            for (std::uint16_t i = 0; i < repetitions; ++i) {
                if (cursorX_ + 1U < width_) ++cursorX_;
            }
            break;
        case PaintActionKind::MoveSouth:
            for (std::uint16_t i = 0; i < repetitions; ++i) {
                if (cursorY_ + 1U < height_) ++cursorY_;
            }
            break;
        case PaintActionKind::MoveWest:
            for (std::uint16_t i = 0; i < repetitions; ++i) {
                if (cursorX_ > 0) --cursorX_;
            }
            break;
        case PaintActionKind::Paint:
            paintDisk(false);
            break;
        case PaintActionKind::Erase:
            paintDisk(true);
            break;
        case PaintActionKind::Brighter:
            for (double& channel : brushColor_) {
                channel = clamp01(channel + 0.125 * repetitions);
            }
            break;
        case PaintActionKind::Darker:
            for (double& channel : brushColor_) {
                channel = clamp01(channel - 0.125 * repetitions);
            }
            break;
        case PaintActionKind::BrushSmall:
            brushRadius_ = 0;
            break;
        case PaintActionKind::BrushLarge:
            brushRadius_ = std::min<std::size_t>(4, brushRadius_ + repetitions);
            break;
    }
}

double VisualCanvas::similarity(const VisualCanvas& other) const {
    if (width_ != other.width_ || height_ != other.height_) {
        throw std::invalid_argument("VisualCanvas similarity requires equal dimensions");
    }
    double absoluteError = 0.0;
    for (std::size_t i = 0; i < colorPixels_.size(); ++i) {
        absoluteError += std::abs(colorPixels_[i] - other.colorPixels_[i]);
    }
    return clamp01(1.0 - absoluteError / static_cast<double>(colorPixels_.size()));
}

double VisualCanvas::meanIntensity() const noexcept {
    if (pixels_.empty()) return 0.0;
    double sum = 0.0;
    for (const double value : pixels_) sum += value;
    return clamp01(sum / static_cast<double>(pixels_.size()));
}

std::size_t VisualCanvas::markedPixels(double threshold) const noexcept {
    const double bounded = clamp01(threshold);
    return static_cast<std::size_t>(std::count_if(
        pixels_.begin(), pixels_.end(),
        [bounded](double value) { return value >= bounded; }));
}

void VisualCanvas::savePgm(const std::filesystem::path& path) const {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create PGM image: " + path.string());
    output << "P5\n" << width_ << ' ' << height_ << "\n255\n";
    for (const double pixelValue : pixels_) {
        const auto byte = static_cast<unsigned char>(
            std::llround(clamp01(pixelValue) * 255.0));
        output.write(reinterpret_cast<const char*>(&byte), 1);
    }
    if (!output) throw std::runtime_error("Cannot finish PGM image: " + path.string());
}

void VisualCanvas::savePpm(const std::filesystem::path& path) const {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create PPM image: " + path.string());
    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (const double channel : colorPixels_) {
        const auto byte = static_cast<unsigned char>(
            std::llround(clamp01(channel) * 255.0));
        output.write(reinterpret_cast<const char*>(&byte), 1);
    }
    if (!output) throw std::runtime_error("Cannot finish PPM image: " + path.string());
}

class VisualImagination::Impl {
public:
    struct PixelChange {
        std::size_t index = 0;
        double value = 0.0;
        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
    };

    struct CausalEvent {
        std::uint64_t sequence = 0;
        std::string phase;
        std::string cause;
        ImaginationStage stage = ImaginationStage::ObserveAndTrace;
        PaintAction action;
        bool hasAction = false;
        bool neuralFeedback = true;
        bool referenceVisible = false;
        std::size_t cursorBeforeX = 0;
        std::size_t cursorBeforeY = 0;
        std::size_t cursorAfterX = 0;
        std::size_t cursorAfterY = 0;
        double similarity = 0.0;
        double reward = 0.0;
        double novelty = 0.0;
        AssemblyId assembly = 0;
        BiologicalTelemetry biology;
        PhysiologyTelemetry physiology;
        ProspectiveTelemetry prospection;
        MotorTelemetry motor;
        std::vector<PixelChange> changes;
        std::string sceneInstance;
        std::size_t prospectiveStep = 0;
    };

    struct MotorMark {
        double normalizedX = 0.0;
        double normalizedY = 0.0;
        double intensity = 1.0;
        double red = 1.0;
        double green = 1.0;
        double blue = 1.0;
        bool erase = false;
        std::uint8_t patchWidth = 0;
        std::uint8_t patchHeight = 0;
        std::array<std::uint8_t, 8U * 8U * 3U> patchRgb{};
    };

    struct RegionToken {
        float centreX = 0.5F;
        float centreY = 0.5F;
        float sigmaX = 0.1F;
        float sigmaY = 0.1F;
        std::array<float, 3U> rgb{0.5F, 0.5F, 0.5F};
        float contrast = 0.0F;
        float salience = 0.0F;
    };

    struct StrokeToken {
        float x = 0.5F;
        float y = 0.5F;
        float dx = 1.0F;
        float dy = 0.0F;
        float length = 0.05F;
        float width = 0.01F;
        std::array<float, 3U> rgb{0.5F, 0.5F, 0.5F};
        float intensity = 0.0F;
        std::uint8_t role = 0U; // 0 fill, 1 edge, 2 shadow, 3 highlight
    };

    // V14 motor memory is formed from TATARUS' own finished canvas after the
    // teacher-visible drawing episode. Every entry is one scalar pigment action
    // bound to a canvas position. No source patch/raster payload is retained.
    struct NeuralMotorStroke {
        std::uint32_t pixelIndex = 0U;
        std::uint8_t red = 0U;
        std::uint8_t green = 0U;
        std::uint8_t blue = 0U;
    };

    struct VisualEngram {
        using Fingerprint = std::array<float, 4U * 4U * 3U>;
        std::uint64_t id = 0;
        std::string label;
        // V14 stores no source bitmap and no retained teacher pigment trace.
        // The fingerprint is a bounded 4x4 population summary used for matching,
        // while shape/features plus derived region/stroke tokens are
        // retina/V1/ventral abstractions from which a fresh motor program is
        // synthesized during recall.
        Fingerprint fingerprint{};
        std::uint64_t contentHash = 0;
        std::array<std::array<double, 2>, 5> shape{};
        neuro::vision::ObjectPartModel featureModel{};
        std::vector<RegionToken> regions;
        std::vector<StrokeToken> strokes;

        // Self-imprint memory: the final canvas actually produced by TATARUS is
        // perceived again and bound to the same engram/assembly. The persistent
        // motor memory is a sequence of scalar strokes, never an RGB patch or
        // bitmap. It is nevertheless intentionally reconstructive: V14 can replay
        // its own learned painting with high fidelity.
        Fingerprint selfFingerprint{};
        std::array<std::array<double, 2>, 5> selfShape{};
        neuro::vision::ObjectPartModel selfFeatureModel{};
        std::vector<NeuralMotorStroke> selfMotorMemory;
        AssemblyId selfAssembly = 0U;
        std::uint64_t selfConsolidations = 0U;
        double selfSimilarity = 0.0;
        bool selfMotorMemoryValid = false;

        std::vector<AssemblyId> assemblies;
        std::uint64_t observations = 0;
        double traceQuality = 0.0;
    };

    struct SymbolAssociation {
        std::uint64_t engramId = 0;
        std::uint64_t observations = 0;
        double strength = 0.0;
    };

    using ShapePoint = std::array<double, 2>;
    using ShapeSignature = std::array<ShapePoint, 5>;
    // V1-like primitive channels: mean RGB, luminance contrast, horizontal,
    // vertical and two diagonal edge energies, plus corner/junction energy.
    using FeatureSignature = neuro::vision::PrimitiveFeatureVector;
    // Background plus an object-adaptive 3x3 part topology. Semantic roles are
    // learned through category experience, never hard-coded as face anatomy.
    using FeatureModel = neuro::vision::ObjectPartModel;

    struct CategoryEngram {
        std::string name;
        std::uint64_t observations = 0;
        // Five cortical structure fields (centre and four quadrants) retain
        // running geometry. No source raster is retained in V14.
        ShapeSignature shapeMean{};
        ShapeSignature shapeM2{};
        // Retina/V1/ventral running statistics: RGB population means,
        // luminance contrast, oriented-edge and junction energies.
        FeatureModel featureMean{};
        FeatureModel featureM2{};
        std::vector<AssemblyId> assemblies;
    };

    struct ObjectEngram {
        std::uint64_t id = 0;
        std::uint64_t observations = 0;
        FeatureModel featureMean{};
        FeatureModel featureM2{};
        std::map<std::string, std::uint64_t> categoryVotes;
    };

    struct ResolutionEngram {
        std::size_t width = 0;
        std::size_t height = 0;
        std::uint64_t observations = 0;
    };

    struct PoseEngram {
        std::string category;
        std::string pose;
        std::uint64_t observations = 0;
        ShapeSignature shapeMean{};
        ShapeSignature shapeM2{};
        FeatureModel featureMean{};
        FeatureModel featureM2{};
        std::vector<std::uint64_t> sourceEngrams;
        std::vector<AssemblyId> assemblies;
    };

    struct RelationEngram {
        SceneRelationKind relation = SceneRelationKind::Near;
        std::string subjectCategory;
        std::string objectCategory;
        std::uint64_t observations = 0;
        // subject minus object: x, y, depth; final component is
        // subjectScale/objectScale.
        std::array<double, 4> mean{};
        std::array<double, 4> m2{};
        std::vector<AssemblyId> assemblies;
    };

    struct ActionEngram {
        SceneActionKind kind = SceneActionKind::Move;
        std::string label;
        std::string actorCategory;
        std::string targetCategory;
        std::uint64_t observations = 0;
        // Translation is stored in action-relative parallel/perpendicular
        // coordinates, followed by depth, scale and rotation deltas.
        std::array<double, 5> actorMean{};
        std::array<double, 5> actorM2{};
        std::array<double, 5> targetMean{};
        std::array<double, 5> targetM2{};
        std::string actorPoseAfter;
        std::string targetPoseAfter;
        std::vector<AssemblyId> assemblies;
    };

    explicit Impl(VisualImaginationConfig value)
        : config(std::move(value)), ownedMind(std::make_unique<RobotMind>(config.mind)),
          mind(*ownedMind), canvas(config.width, config.height) {
        validateConfig();
    }

    Impl(RobotMind& sharedMind, VisualImaginationConfig value)
        : config(std::move(value)), mind(sharedMind), canvas(config.width, config.height) {
        validateConfig();
    }

    void validateConfig() {
        if (config.width < 8 || config.height < 8
            || config.width > 512 || config.height > 512) {
            throw std::invalid_argument("VisualImagination dimensions must be in [8, 512]");
        }
        if (config.exposureObservations == 0 || config.exposureObservations > 10'000) {
            throw std::invalid_argument("exposureObservations must be in [1, 10000]");
        }
        if (config.maximumEngrams == 0 || config.maximumEngrams > 65'536) {
            throw std::invalid_argument("maximumEngrams must be in [1, 65536]");
        }
        if (config.neuralFeedbackStride == 0 || config.neuralFeedbackStride > 256) {
            throw std::invalid_argument("neuralFeedbackStride must be in [1, 256]");
        }
        if (config.paintPatchSide == 0 || config.paintPatchSide > 8) {
            throw std::invalid_argument("paintPatchSide must be in [1, 8]");
        }
        if (config.maximumCategories == 0 || config.maximumCategories > 1024) {
            throw std::invalid_argument("maximumCategories must be in [1, 1024]");
        }
        if (config.categoryAnchorCount < 2U || config.categoryAnchorCount > 32U) {
            throw std::invalid_argument("categoryAnchorCount must be in [2, 32]");
        }
        if (config.maximumObjectEngrams == 0U || config.maximumObjectEngrams > 65'536U) {
            throw std::invalid_argument("maximumObjectEngrams must be in [1, 65536]");
        }
        if (!std::isfinite(config.objectMemoryMatchThreshold)
            || config.objectMemoryMatchThreshold < 0.20
            || config.objectMemoryMatchThreshold > 0.99) {
            throw std::invalid_argument("objectMemoryMatchThreshold must be in [0.20, 0.99]");
        }
        if (config.maximumPoseEngrams == 0U || config.maximumPoseEngrams > 4096U) {
            throw std::invalid_argument("maximumPoseEngrams must be in [1, 4096]");
        }
        if (config.maximumRelationEngrams == 0U
            || config.maximumRelationEngrams > 65'536U) {
            throw std::invalid_argument("maximumRelationEngrams must be in [1, 65536]");
        }
        if (config.maximumActionEngrams == 0U || config.maximumActionEngrams > 65'536U) {
            throw std::invalid_argument("maximumActionEngrams must be in [1, 65536]");
        }
        if (config.maximumSceneObjects == 0U || config.maximumSceneObjects > 1024U) {
            throw std::invalid_argument("maximumSceneObjects must be in [1, 1024]");
        }
        if (!std::isfinite(config.physiologicalExpressionGain)
            || config.physiologicalExpressionGain < 0.0
            || config.physiologicalExpressionGain > 1.0) {
            throw std::invalid_argument("physiologicalExpressionGain must be in [0, 1]");
        }
        last.canvas = VisualCanvas(config.width, config.height);
    }

    void resetTrace() {
        trace.clear();
        traceSequence = 0;
        lastObservedAssembly = 0;
        runningSimilarityValid = false;
    }

    void recordSense(
        const ObserveResult& observed,
        const VisualCanvas* reference,
        ImaginationStage stage,
        bool internallyGenerated = false) {
        CausalEvent event;
        event.sequence = ++traceSequence;
        event.phase = "sense";
        event.cause = internallyGenerated ? "internally_simulated_scene_state"
            : reference == nullptr ? "internal_or_symbol_cue"
                                   : "retinal_reference_and_canvas";
        event.stage = stage;
        event.referenceVisible = reference != nullptr && !internallyGenerated;
        event.cursorBeforeX = canvas.cursorX();
        event.cursorBeforeY = canvas.cursorY();
        event.cursorAfterX = canvas.cursorX();
        event.cursorAfterY = canvas.cursorY();
        event.similarity = reference == nullptr ? 0.0 : canvas.similarity(*reference);
        event.assembly = representedAssembly(observed);
        event.biology = observed.biology;
        event.physiology = observed.physiology;
        event.prospection = observed.prospection;
        event.motor = observed.motor;
        trace.push_back(std::move(event));
    }

    void validateCanvas(const VisualCanvas& value) const {
        if (value.width() != config.width || value.height() != config.height) {
            throw std::invalid_argument("Visual stimulus dimensions do not match IMAGINATIO configuration");
        }
    }

    Experience experience(
        const VisualCanvas* reference,
        std::string_view symbol,
        ImaginationStage stage,
        std::uint64_t episodeContext,
        const PaintAction* action = nullptr,
        double reward = 0.0,
        double novelty = 0.0) {
        Experience result;
        result.timestampNs = ++timestamp * 1'000'000ULL;
        // Every visual experience now traverses the same biological pathway:
        // cone populations -> retinal ganglion cells -> optic nerve -> V1.
        // Its 4-channel afferent code remains bounded for the small persistent
        // sensory population and therefore cannot turn image resolution into
        // unintended input-current gain.
        const std::size_t afferentCount = visualPathway.config().retinalWidth
            * visualPathway.config().retinalHeight * 4U;
        const std::size_t objectEventCapacity = visualPathway.config().maximumObjects * 16U;
        result.vision.reserve((afferentCount + objectEventCapacity) * 2U);
        const auto appendVisualAfferents = [this, &result, afferentCount, objectEventCapacity](
            const VisualCanvas* source) {
            if (source == nullptr) {
                result.vision.insert(
                    result.vision.end(), afferentCount + objectEventCapacity, 0.0);
                return;
            }
            // IMAGINATIO frames are symbolic/static recalls rather than a continuous
            // camera stream. Reset the ocular transient state so identical recalled
            // images produce identical afferent signatures and can consolidate into
            // the same persistent neural assembly.
            ocularSystem.reset();
            auto ocularFrame = ocularSystem.process(
                source->width(), source->height(), source->colorPixels(), 0.02);
            auto percept = visualPathway.perceive(
                source->width(), source->height(), ocularFrame.retinalRgb);
            result.vision.insert(
                result.vision.end(),
                percept.opticNerveEvents.begin(), percept.opticNerveEvents.end());
            result.vision.insert(
                result.vision.end(),
                percept.objectCortexEvents.begin(), percept.objectCortexEvents.end());
            if (percept.objectCortexEvents.size() < objectEventCapacity) {
                result.vision.insert(
                    result.vision.end(),
                    objectEventCapacity - percept.objectCortexEvents.size(), 0.0);
            }
            lastVisualPercept = std::move(percept);
        };
        appendVisualAfferents(reference);
        appendVisualAfferents(&canvas);

        result.text.assign(symbol.begin(), symbol.end());
        result.spatialEpisodeContext = episodeContext;
        constexpr std::array<double, 2> periods{4.0, 16.0};
        for (const double period : periods) {
            const double xPhase = 2.0 * kPi * static_cast<double>(canvas.cursorX()) / period;
            const double yPhase = 2.0 * kPi * static_cast<double>(canvas.cursorY()) / period;
            result.spatialContext.push_back(std::sin(xPhase));
            result.spatialContext.push_back(std::cos(xPhase));
            result.spatialContext.push_back(std::sin(yPhase));
            result.spatialContext.push_back(std::cos(yPhase));
        }
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto x = static_cast<std::ptrdiff_t>(canvas.cursorX()) + dx;
                const auto y = static_cast<std::ptrdiff_t>(canvas.cursorY()) + dy;
                const auto color = x >= 0 && y >= 0
                        && x < static_cast<std::ptrdiff_t>(canvas.width())
                        && y < static_cast<std::ptrdiff_t>(canvas.height())
                    ? canvas.colorPixel(static_cast<std::size_t>(x), static_cast<std::size_t>(y))
                    : std::array<double, 3>{0.0, 0.0, 0.0};
                for (const double channel : color) {
                    result.spatialContext.push_back(2.0 * channel - 1.0);
                }
            }
        }

        result.context = {
            reference != nullptr ? 1.0 : -1.0,
            -1.0 + 0.5 * static_cast<double>(stage),
            2.0 * static_cast<double>(canvas.cursorX())
                / static_cast<double>(canvas.width() - 1U) - 1.0,
            2.0 * static_cast<double>(canvas.cursorY())
                / static_cast<double>(canvas.height() - 1U) - 1.0,
            static_cast<double>(canvas.brushRadius()) / 4.0,
        };
        result.context.push_back(2.0 * std::min(1.0,
            static_cast<double>(lastVisualPercept.objects.size())
                / static_cast<double>(visualPathway.config().maximumObjects)) - 1.0);
        for (const double channel : canvas.brushColor()) {
            result.context.push_back(2.0 * channel - 1.0);
        }
        if (reference != nullptr) {
            const double similarity = runningSimilarityValid
                ? runningSimilarity : canvas.similarity(*reference);
            result.context.push_back(2.0 * similarity - 1.0);
        }
        if (!symbol.empty()) {
            const auto signature = fnv1a(symbol);
            for (unsigned int shift = 0; shift < 4U; ++shift) {
                const auto part = static_cast<std::uint16_t>((signature >> (shift * 16U)) & 0xffffU);
                result.context.push_back(2.0 * static_cast<double>(part) / 65535.0 - 1.0);
            }
        }
        if (action != nullptr) {
            result.action = ActionEvent{
                .id = static_cast<ActionId>(action->kind) + 1U,
                .label = actionName(action->kind),
                .intensity = clamp01(action->intensity),
                .startedNs = result.timestampNs,
                .endedNs = result.timestampNs,
            };
        }
        const auto physiology = mind.physiology();
        result.body.battery = physiology.available ? clamp01(physiology.atp) : 1.0;
        result.body.powerDraw = action == nullptr ? 0.02 : 0.12;
        result.environment.light = reference != nullptr
            ? 0.5 * (reference->meanIntensity() + canvas.meanIntensity())
            : canvas.meanIntensity();
        result.environment.novelty = clamp01(novelty);
        result.reward = std::clamp(reward, -1.0, 1.0);
        return result;
    }

    ObserveResult expose(
        const VisualCanvas* reference,
        std::string_view symbol,
        ImaginationStage stage,
        std::uint64_t episodeContext,
        bool internallyGenerated = false) {
        ObserveResult observed;
        for (std::size_t i = 0; i < config.exposureObservations; ++i) {
            observed = mind.observe(experience(reference, symbol, stage, episodeContext));
        }
        if (const auto assembly = representedAssembly(observed); assembly != 0) {
            lastObservedAssembly = assembly;
        }
        recordSense(observed, reference, stage, internallyGenerated);
        return observed;
    }

    ObserveResult act(
        PaintAction action,
        const VisualCanvas* reference,
        std::string_view symbol,
        ImaginationStage stage,
        std::uint64_t episodeContext,
        bool training,
        bool expressPhysiology,
        bool neuralFeedback = true) {
        const auto cursorBeforeX = canvas.cursorX();
        const auto cursorBeforeY = canvas.cursorY();
        struct AffectedPixel {
            std::size_t index = 0;
            std::array<double, 3> color{};
            double luminance = 0.0;
        };
        std::vector<AffectedPixel> affected;
        if (action.kind == PaintActionKind::Paint
            || action.kind == PaintActionKind::Erase) {
            if (action.kind == PaintActionKind::Paint
                && action.patchWidth > 0U && action.patchHeight > 0U) {
                const auto patchWidth = std::min<std::size_t>(8U, action.patchWidth);
                const auto patchHeight = std::min<std::size_t>(8U, action.patchHeight);
                affected.reserve(patchWidth * patchHeight);
                for (std::size_t patchY = 0; patchY < patchHeight; ++patchY) {
                    for (std::size_t patchX = 0; patchX < patchWidth; ++patchX) {
                        const std::size_t x = cursorBeforeX + patchX;
                        const std::size_t y = cursorBeforeY + patchY;
                        if (x >= canvas.width() || y >= canvas.height()) continue;
                        const std::size_t index = y * canvas.width() + x;
                        affected.push_back(AffectedPixel{
                            .index = index,
                            .color = canvas.colorPixel(x, y),
                            .luminance = canvas.pixels()[index],
                        });
                    }
                }
            } else {
                const auto radius = static_cast<std::ptrdiff_t>(canvas.brushRadius());
                affected.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
                for (std::ptrdiff_t dy = -radius; dy <= radius; ++dy) {
                    for (std::ptrdiff_t dx = -radius; dx <= radius; ++dx) {
                        if (dx * dx + dy * dy > radius * radius) continue;
                        const auto x = static_cast<std::ptrdiff_t>(cursorBeforeX) + dx;
                        const auto y = static_cast<std::ptrdiff_t>(cursorBeforeY) + dy;
                        if (x < 0 || y < 0
                            || x >= static_cast<std::ptrdiff_t>(canvas.width())
                            || y >= static_cast<std::ptrdiff_t>(canvas.height())) continue;
                        const std::size_t index = static_cast<std::size_t>(y) * canvas.width()
                            + static_cast<std::size_t>(x);
                        affected.push_back(AffectedPixel{
                            .index = index,
                            .color = canvas.colorPixel(
                                static_cast<std::size_t>(x), static_cast<std::size_t>(y)),
                            .luminance = canvas.pixels()[index],
                        });
                    }
                }
            }
        }
        if (reference != nullptr && !runningSimilarityValid) {
            runningSimilarity = canvas.similarity(*reference);
            runningSimilarityValid = true;
        }
        const double similarityBefore = reference == nullptr ? 0.0 : runningSimilarity;
        if (action.kind == PaintActionKind::Paint
            && (action.red < 0.0 || action.green < 0.0 || action.blue < 0.0)) {
            const auto color = canvas.brushColor();
            action.red = color[0];
            action.green = color[1];
            action.blue = color[2];
        }
        if (expressPhysiology
            && (action.kind == PaintActionKind::Paint
                || action.kind == PaintActionKind::Erase)) {
            const auto physiology = mind.physiology();
            const auto motor = mind.motor();
            const double metabolicLoad = physiology.available
                ? 0.70 * (1.0 - clamp01(physiology.atp))
                    + 0.30 * clamp01(physiology.sleepPressure)
                : 0.0;
            const double confidenceLift = motor.available
                ? 0.10 * (clamp01(motor.confidence) - 0.5)
                : 0.0;
            const double expression = std::clamp(
                1.0 - config.physiologicalExpressionGain * metabolicLoad
                    + config.physiologicalExpressionGain * confidenceLift,
                0.55, 1.10);
            action.intensity = clamp01(action.intensity * expression);
        }

        const ActionId actionId = static_cast<ActionId>(action.kind) + 1U;
        if (neuralFeedback) {
            mind.beginAction(ActionEvent{
                .id = actionId,
                .label = actionName(action.kind),
                .intensity = clamp01(action.intensity),
                .startedNs = (timestamp + 1U) * 1'000'000ULL,
            });
        }
        canvas.apply(action);
        double luminanceDelta = 0.0;
        double errorBefore = 0.0;
        double errorAfter = 0.0;
        for (const auto& pixel : affected) {
            const std::size_t colorIndex = pixel.index * 3U;
            luminanceDelta += canvas.pixels()[pixel.index] - pixel.luminance;
            if (reference != nullptr) {
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    const double target = reference->colorPixels()[colorIndex + channel];
                    errorBefore += std::abs(pixel.color[channel] - target);
                    errorAfter += std::abs(canvas.colorPixels()[colorIndex + channel] - target);
                }
            }
        }
        lastCanvasMean = clamp01(lastCanvasMean
            + luminanceDelta / static_cast<double>(canvas.pixels().size()));
        if (reference != nullptr) {
            runningSimilarity = clamp01(runningSimilarity
                + (errorBefore - errorAfter)
                    / static_cast<double>(canvas.colorPixels().size()));
        }
        const double similarityAfter = reference == nullptr ? 0.0 : runningSimilarity;
        const double improvement = similarityAfter - similarityBefore;
        const double reward = training
            ? std::clamp(8.0 * improvement + (improvement >= 0.0 ? 0.01 : -0.01), -1.0, 1.0)
            : 0.0;
        const double novelty = reference == nullptr
            ? std::abs(luminanceDelta) / static_cast<double>(canvas.pixels().size())
            : std::abs(improvement);
        ObserveResult observed;
        if (neuralFeedback) {
            observed = mind.observe(experience(
                reference, symbol, stage, episodeContext, &action, reward, novelty));
            const auto assembly = representedAssembly(observed);
            if (assembly != 0) lastObservedAssembly = assembly;
            mind.endAction(ActionOutcome{
                .id = actionId,
                .reward = reward,
                .success = training ? (improvement >= 0.0 ? 0.25 : 0.0) : 0.0,
                .novelty = clamp01(novelty),
            });
        } else {
            ++timestamp;
        }
        ++actionsExecuted;

        CausalEvent event;
        event.sequence = ++traceSequence;
        event.phase = "act";
        event.cause = training ? "demonstrated_motor_trace"
            : stage == ImaginationStage::Composition
                ? "coactivated_factorized_feature_synthesis"
                : stage == ImaginationStage::CategoryFusion
                    ? "category_factorized_feature_synthesis"
                    : stage == ImaginationStage::RelationalScene
                        ? "relational_scene_object_motor_program"
                        : stage == ImaginationStage::ProspectiveImagination
                            ? "predicted_scene_object_motor_program"
                            : "recalled_sensorimotor_engram";
        event.stage = stage;
        event.action = action;
        event.hasAction = true;
        event.neuralFeedback = neuralFeedback;
        event.referenceVisible = reference != nullptr;
        event.cursorBeforeX = cursorBeforeX;
        event.cursorBeforeY = cursorBeforeY;
        event.cursorAfterX = canvas.cursorX();
        event.cursorAfterY = canvas.cursorY();
        event.similarity = similarityAfter;
        event.reward = reward;
        event.novelty = novelty;
        event.assembly = representedAssembly(observed);
        if (event.assembly == 0) event.assembly = lastObservedAssembly;
        event.biology = mind.biology();
        event.physiology = mind.physiology();
        event.prospection = mind.prospection();
        event.motor = mind.motor();
        for (const auto& pixel : affected) {
            const std::size_t colorIndex = pixel.index * 3U;
            if (std::abs(pixel.color[0] - canvas.colorPixels()[colorIndex]) > 1e-12
                || std::abs(pixel.color[1] - canvas.colorPixels()[colorIndex + 1U]) > 1e-12
                || std::abs(pixel.color[2] - canvas.colorPixels()[colorIndex + 2U]) > 1e-12) {
                event.changes.push_back(PixelChange{
                    .index = pixel.index,
                    .value = canvas.pixels()[pixel.index],
                    .red = canvas.colorPixels()[colorIndex],
                    .green = canvas.colorPixels()[colorIndex + 1U],
                    .blue = canvas.colorPixels()[colorIndex + 2U],
                });
            }
        }
        trace.push_back(std::move(event));
        return observed;
    }

    std::vector<MotorMark> extractMarks(const std::vector<PaintAction>& program) const {
        VisualCanvas scratch(config.width, config.height);
        std::vector<MotorMark> marks;
        marks.reserve(program.size() / 2U + 1U);
        for (const auto& action : program) {
            if (action.kind == PaintActionKind::Paint
                || action.kind == PaintActionKind::Erase) {
                const auto color = action.red >= 0.0 && action.green >= 0.0 && action.blue >= 0.0
                    ? std::array<double, 3>{action.red, action.green, action.blue}
                    : scratch.brushColor();
                MotorMark mark{
                    .normalizedX = static_cast<double>(scratch.cursorX())
                        / static_cast<double>(scratch.width() - 1U),
                    .normalizedY = static_cast<double>(scratch.cursorY())
                        / static_cast<double>(scratch.height() - 1U),
                    .intensity = action.intensity,
                    .red = color[0],
                    .green = color[1],
                    .blue = color[2],
                    .erase = action.kind == PaintActionKind::Erase,
                    .patchWidth = action.patchWidth,
                    .patchHeight = action.patchHeight,
                    .patchRgb = action.patchRgb,
                };
                marks.push_back(std::move(mark));
            }
            scratch.apply(action);
        }
        return marks;
    }

    VisualEngram* engramById(std::uint64_t id) {
        const auto found = std::find_if(engrams.begin(), engrams.end(),
            [id](const VisualEngram& engram) { return engram.id == id; });
        return found == engrams.end() ? nullptr : &*found;
    }

    const VisualEngram* engramById(std::uint64_t id) const {
        const auto found = std::find_if(engrams.begin(), engrams.end(),
            [id](const VisualEngram& engram) { return engram.id == id; });
        return found == engrams.end() ? nullptr : &*found;
    }

    const VisualEngram* closestEngram(
        const VisualCanvas& cue,
        AssemblyId activeAssembly = 0) const {
        const VisualEngram* best = nullptr;
        double bestScore = -1.0;
        for (const auto& engram : engrams) {
            const double assemblyMatch = activeAssembly != 0
                    && std::find(engram.assemblies.begin(), engram.assemblies.end(), activeAssembly)
                        != engram.assemblies.end()
                ? 1.0 : 0.0;
            // The visual cue is authoritative. Assembly reactivation resolves
            // only otherwise indistinguishable visual matches; it must never
            // make a different sparse image beat an exact 512x512 cue.
            const double score = engramSimilarity(engram, cue)
                + 1e-9 * assemblyMatch;
            if (score > bestScore) {
                best = &engram;
                bestScore = score;
            }
        }
        return best;
    }

    static VisualEngram::Fingerprint visualFingerprint(const VisualCanvas& canvas) {
        VisualEngram::Fingerprint result{};
        std::array<std::uint64_t, 4U * 4U> samples{};
        for (std::size_t y = 0; y < canvas.height(); ++y) {
            const std::size_t blockY = std::min<std::size_t>(3U, y * 4U / canvas.height());
            for (std::size_t x = 0; x < canvas.width(); ++x) {
                const std::size_t blockX = std::min<std::size_t>(3U, x * 4U / canvas.width());
                const std::size_t block = blockY * 4U + blockX;
                const auto color = canvas.colorPixel(x, y);
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    result[block * 3U + channel] += static_cast<float>(color[channel]);
                }
                ++samples[block];
            }
        }
        for (std::size_t block = 0; block < samples.size(); ++block) {
            const float divisor = static_cast<float>(std::max<std::uint64_t>(1U, samples[block]));
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                result[block * 3U + channel] /= divisor;
            }
        }
        return result;
    }

    static double fingerprintDistance(
        const VisualEngram::Fingerprint& left,
        const VisualEngram::Fingerprint& right) noexcept {
        double distance = 0.0;
        for (std::size_t index = 0; index < left.size(); ++index) {
            distance += std::abs(static_cast<double>(left[index])
                - static_cast<double>(right[index]));
        }
        return distance / static_cast<double>(left.size());
    }

    static std::uint64_t visualContentHash(const VisualCanvas& canvas) noexcept {
        // Hashes the transient sensory RGB8 stream without retaining it. The
        // hash is used only for exact duplicate recognition; it cannot be
        // inverted into a source image.
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mixByte = [&hash](std::uint8_t value) {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        };
        for (const auto dimension : {canvas.width(), canvas.height()}) {
            auto value = static_cast<std::uint64_t>(dimension);
            for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
                mixByte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
            }
        }
        for (const double channel : canvas.colorPixels()) {
            mixByte(static_cast<std::uint8_t>(
                std::llround(clamp01(channel) * 255.0)));
        }
        return hash;
    }

    static double shapeSimilarity(
        const ShapeSignature& left,
        const ShapeSignature& right) noexcept {
        double squared = 0.0;
        for (std::size_t point = 0; point < left.size(); ++point) {
            for (std::size_t axis = 0; axis < 2U; ++axis) {
                const double delta = left[point][axis] - right[point][axis];
                squared += delta * delta;
            }
        }
        const double rms = std::sqrt(squared / static_cast<double>(left.size() * 2U));
        return std::exp(-8.0 * rms);
    }

    double engramSimilarity(
        const VisualEngram& engram,
        const VisualCanvas& cue) const {
        if (engram.contentHash != 0U && engram.contentHash == visualContentHash(cue)) {
            return 1.0;
        }
        const auto cueFingerprint = visualFingerprint(cue);
        const auto cueShape = extractShape(cue);
        const auto cueFeatures = extractFeatureModel(cue);
        const double fingerprintSimilarity = std::exp(
            -10.0 * fingerprintDistance(cueFingerprint, engram.fingerprint));
        const double structuralSimilarity = shapeSimilarity(cueShape, engram.shape);
        const double perceptualSimilarity = featureSimilarity(cueFeatures, engram.featureModel);
        return clamp01(
            0.30 * fingerprintSimilarity
            + 0.15 * structuralSimilarity
            + 0.55 * perceptualSimilarity);
    }

    static void blendPixel(
        VisualCanvas& target,
        std::size_t x,
        std::size_t y,
        const std::array<double, 3U>& rgb,
        double alpha) {
        if (x >= target.width() || y >= target.height()) return;
        alpha = clamp01(alpha);
        if (alpha <= 0.0) return;
        const auto current = target.colorPixel(x, y);
        target.setColorPixel(
            x, y,
            current[0] + alpha * (rgb[0] - current[0]),
            current[1] + alpha * (rgb[1] - current[1]),
            current[2] + alpha * (rgb[2] - current[2]));
    }

    std::vector<RegionToken> extractRegionTokens(const VisualCanvas& image) const {
        struct Candidate {
            RegionToken token;
            double score = 0.0;
        };
        std::vector<Candidate> candidates;
        const std::size_t gridX = std::min<std::size_t>(12U, image.width());
        const std::size_t gridY = std::min<std::size_t>(12U, image.height());
        const double invWidth = 1.0 / static_cast<double>(std::max<std::size_t>(1U, image.width()));
        const double invHeight = 1.0 / static_cast<double>(std::max<std::size_t>(1U, image.height()));

        for (std::size_t gy = 0; gy < gridY; ++gy) {
            const std::size_t y0 = gy * image.height() / gridY;
            const std::size_t y1 = std::max(y0 + 1U, (gy + 1U) * image.height() / gridY);
            for (std::size_t gx = 0; gx < gridX; ++gx) {
                const std::size_t x0 = gx * image.width() / gridX;
                const std::size_t x1 = std::max(x0 + 1U, (gx + 1U) * image.width() / gridX);
                std::array<double, 3U> mean{};
                double luminanceMean = 0.0;
                double luminanceM2 = 0.0;
                double gradient = 0.0;
                std::size_t samples = 0U;
                std::size_t gradients = 0U;
                for (std::size_t y = y0; y < y1; ++y) {
                    for (std::size_t x = x0; x < x1; ++x) {
                        const auto rgb = image.colorPixel(x, y);
                        const double luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
                        ++samples;
                        for (std::size_t channel = 0; channel < 3U; ++channel) {
                            mean[channel] += rgb[channel];
                        }
                        const double delta = luminance - luminanceMean;
                        luminanceMean += delta / static_cast<double>(samples);
                        luminanceM2 += delta * (luminance - luminanceMean);
                        if (x + 1U < image.width() && y + 1U < image.height()) {
                            const auto rgbX = image.colorPixel(x + 1U, y);
                            const auto rgbY = image.colorPixel(x, y + 1U);
                            const double lumX = 0.2126 * rgbX[0] + 0.7152 * rgbX[1] + 0.0722 * rgbX[2];
                            const double lumY = 0.2126 * rgbY[0] + 0.7152 * rgbY[1] + 0.0722 * rgbY[2];
                            const double gxValue = lumX - luminance;
                            const double gyValue = lumY - luminance;
                            gradient += std::sqrt(gxValue * gxValue + gyValue * gyValue);
                            ++gradients;
                        }
                    }
                }
                if (samples == 0U) continue;
                for (double& channel : mean) channel /= static_cast<double>(samples);
                const double variance = samples > 1U
                    ? luminanceM2 / static_cast<double>(samples - 1U) : 0.0;
                const double contrast = std::sqrt(std::max(0.0, variance));
                const double edgeEnergy = gradients > 0U
                    ? gradient / static_cast<double>(gradients) : 0.0;
                const double salience = clamp01(0.75 * contrast + 0.85 * edgeEnergy);
                if (salience < 0.035 && contrast < 0.018) continue;

                RegionToken token;
                token.centreX = static_cast<float>((static_cast<double>(x0 + x1) * 0.5) * invWidth);
                token.centreY = static_cast<float>((static_cast<double>(y0 + y1) * 0.5) * invHeight);
                token.sigmaX = static_cast<float>(std::max(0.03,
                    0.55 * static_cast<double>(x1 - x0) * invWidth));
                token.sigmaY = static_cast<float>(std::max(0.03,
                    0.55 * static_cast<double>(y1 - y0) * invHeight));
                token.rgb = {static_cast<float>(mean[0]), static_cast<float>(mean[1]), static_cast<float>(mean[2])};
                token.contrast = static_cast<float>(clamp01(contrast * 2.5));
                token.salience = static_cast<float>(salience);
                candidates.push_back(Candidate{token, salience + 0.25 * token.contrast});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.score > right.score;
        });
        const std::size_t limit = std::min<std::size_t>(48U, candidates.size());
        std::vector<RegionToken> regions;
        regions.reserve(limit);
        for (std::size_t i = 0; i < limit; ++i) regions.push_back(candidates[i].token);
        return regions;
    }

    std::vector<StrokeToken> extractStrokeTokens(const VisualCanvas& image) const {
        struct Candidate {
            StrokeToken token;
            double score = 0.0;
        };
        std::vector<Candidate> candidates;
        const std::size_t gridX = std::min<std::size_t>(24U, image.width());
        const std::size_t gridY = std::min<std::size_t>(24U, image.height());
        const double invWidth = 1.0 / static_cast<double>(std::max<std::size_t>(1U, image.width()));
        const double invHeight = 1.0 / static_cast<double>(std::max<std::size_t>(1U, image.height()));
        const double maxDimension = static_cast<double>(std::max(image.width(), image.height()));
        double globalLuminance = 0.0;
        for (std::size_t y = 0; y < image.height(); ++y) {
            for (std::size_t x = 0; x < image.width(); ++x) {
                const auto rgb = image.colorPixel(x, y);
                globalLuminance += 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
            }
        }
        globalLuminance /= static_cast<double>(std::max<std::size_t>(1U, image.width() * image.height()));

        for (std::size_t gy = 0; gy < gridY; ++gy) {
            const std::size_t y0 = gy * image.height() / gridY;
            const std::size_t y1 = std::max(y0 + 1U, (gy + 1U) * image.height() / gridY);
            for (std::size_t gx = 0; gx < gridX; ++gx) {
                const std::size_t x0 = gx * image.width() / gridX;
                const std::size_t x1 = std::max(x0 + 1U, (gx + 1U) * image.width() / gridX);
                std::array<double, 3U> mean{};
                double meanLuminance = 0.0;
                double gxAccum = 0.0;
                double gyAccum = 0.0;
                double edgeEnergy = 0.0;
                std::size_t samples = 0U;
                std::size_t gradients = 0U;
                for (std::size_t y = y0; y < y1; ++y) {
                    for (std::size_t x = x0; x < x1; ++x) {
                        const auto rgb = image.colorPixel(x, y);
                        const double luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
                        ++samples;
                        for (std::size_t channel = 0; channel < 3U; ++channel) {
                            mean[channel] += rgb[channel];
                        }
                        meanLuminance += luminance;
                        const std::size_t xPrev = x == 0U ? 0U : x - 1U;
                        const std::size_t xNext = std::min(image.width() - 1U, x + 1U);
                        const std::size_t yPrev = y == 0U ? 0U : y - 1U;
                        const std::size_t yNext = std::min(image.height() - 1U, y + 1U);
                        const auto rgbLeft = image.colorPixel(xPrev, y);
                        const auto rgbRight = image.colorPixel(xNext, y);
                        const auto rgbUp = image.colorPixel(x, yPrev);
                        const auto rgbDown = image.colorPixel(x, yNext);
                        const double lumLeft = 0.2126 * rgbLeft[0] + 0.7152 * rgbLeft[1] + 0.0722 * rgbLeft[2];
                        const double lumRight = 0.2126 * rgbRight[0] + 0.7152 * rgbRight[1] + 0.0722 * rgbRight[2];
                        const double lumUp = 0.2126 * rgbUp[0] + 0.7152 * rgbUp[1] + 0.0722 * rgbUp[2];
                        const double lumDown = 0.2126 * rgbDown[0] + 0.7152 * rgbDown[1] + 0.0722 * rgbDown[2];
                        const double gradX = 0.5 * (lumRight - lumLeft);
                        const double gradY = 0.5 * (lumDown - lumUp);
                        gxAccum += gradX;
                        gyAccum += gradY;
                        edgeEnergy += std::sqrt(gradX * gradX + gradY * gradY);
                        ++gradients;
                    }
                }
                if (samples == 0U || gradients == 0U) continue;
                for (double& channel : mean) channel /= static_cast<double>(samples);
                meanLuminance /= static_cast<double>(samples);
                edgeEnergy /= static_cast<double>(gradients);
                const double magnitude = std::sqrt(gxAccum * gxAccum + gyAccum * gyAccum);
                const double score = edgeEnergy + 0.30 * magnitude / static_cast<double>(gradients);
                if (score < 0.04) continue;
                double dx = -gyAccum;
                double dy = gxAccum;
                const double norm = std::sqrt(dx * dx + dy * dy);
                if (norm < 1e-8) {
                    dx = 1.0;
                    dy = 0.0;
                } else {
                    dx /= norm;
                    dy /= norm;
                }
                StrokeToken token;
                token.x = static_cast<float>((static_cast<double>(x0 + x1) * 0.5) * invWidth);
                token.y = static_cast<float>((static_cast<double>(y0 + y1) * 0.5) * invHeight);
                token.dx = static_cast<float>(dx);
                token.dy = static_cast<float>(dy);
                const double cellWidth = static_cast<double>(x1 - x0);
                const double cellHeight = static_cast<double>(y1 - y0);
                const double cellScale = 0.5 * (cellWidth + cellHeight);
                const double minimumCellExtent = std::min(cellWidth, cellHeight);
                token.length = static_cast<float>(std::clamp(
                    cellScale / maxDimension * (1.4 + 2.4 * score), 0.012, 0.16));
                token.width = static_cast<float>(std::clamp(
                    minimumCellExtent / maxDimension * 0.75, 0.004, 0.06));
                token.rgb = {static_cast<float>(mean[0]), static_cast<float>(mean[1]), static_cast<float>(mean[2])};
                token.intensity = static_cast<float>(clamp01(0.25 + 2.4 * score));
                token.role = meanLuminance > globalLuminance + 0.08 ? 3U
                    : (meanLuminance < globalLuminance - 0.08 ? 2U : 1U);
                candidates.push_back(Candidate{token, score});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.score > right.score;
        });
        const std::size_t limit = std::min<std::size_t>(192U, candidates.size());
        std::vector<StrokeToken> strokes;
        strokes.reserve(limit);
        for (std::size_t i = 0; i < limit; ++i) strokes.push_back(candidates[i].token);
        return strokes;
    }

    void applyRegionMemory(
        VisualCanvas& result,
        const std::vector<RegionToken>& regions) const {
        std::vector<RegionToken> ordered = regions;
        std::sort(ordered.begin(), ordered.end(), [](const RegionToken& left, const RegionToken& right) {
            return left.salience < right.salience;
        });
        for (const auto& token : ordered) {
            const double sigmaX = std::max(0.02, static_cast<double>(token.sigmaX));
            const double sigmaY = std::max(0.02, static_cast<double>(token.sigmaY));
            const double cx = static_cast<double>(token.centreX) * static_cast<double>(result.width() - 1U);
            const double cy = static_cast<double>(token.centreY) * static_cast<double>(result.height() - 1U);
            const std::size_t x0 = static_cast<std::size_t>(std::max(0.0,
                std::floor((static_cast<double>(token.centreX) - 2.6 * sigmaX)
                    * static_cast<double>(result.width()))));
            const std::size_t y0 = static_cast<std::size_t>(std::max(0.0,
                std::floor((static_cast<double>(token.centreY) - 2.6 * sigmaY)
                    * static_cast<double>(result.height()))));
            const std::size_t x1 = static_cast<std::size_t>(std::min(
                static_cast<double>(result.width()),
                std::ceil((static_cast<double>(token.centreX) + 2.6 * sigmaX)
                    * static_cast<double>(result.width()))));
            const std::size_t y1 = static_cast<std::size_t>(std::min(
                static_cast<double>(result.height()),
                std::ceil((static_cast<double>(token.centreY) + 2.6 * sigmaY)
                    * static_cast<double>(result.height()))));
            const std::array<double, 3U> rgb{
                clamp01(token.rgb[0]), clamp01(token.rgb[1]), clamp01(token.rgb[2])};
            const double baseAlpha = 0.08 + 0.20 * clamp01(token.salience)
                + 0.12 * clamp01(token.contrast);
            for (std::size_t y = y0; y < y1; ++y) {
                for (std::size_t x = x0; x < x1; ++x) {
                    const double dx = (static_cast<double>(x) - cx)
                        / (sigmaX * static_cast<double>(result.width()));
                    const double dy = (static_cast<double>(y) - cy)
                        / (sigmaY * static_cast<double>(result.height()));
                    const double gaussian = std::exp(-0.5 * (dx * dx + dy * dy));
                    blendPixel(result, x, y, rgb, baseAlpha * gaussian);
                }
            }
        }
    }

    void applyStrokeMemory(
        VisualCanvas& result,
        const std::vector<StrokeToken>& strokes,
        std::uint64_t seed) const {
        for (std::size_t index = 0; index < strokes.size(); ++index) {
            const auto& token = strokes[index];
            std::array<double, 3U> rgb{
                clamp01(token.rgb[0]), clamp01(token.rgb[1]), clamp01(token.rgb[2])};
            if (token.role == 3U) {
                for (double& channel : rgb) channel = clamp01(channel + 0.10);
            } else if (token.role == 2U) {
                for (double& channel : rgb) channel = clamp01(channel * 0.82);
            }
            const double centreX = static_cast<double>(token.x) * static_cast<double>(result.width() - 1U);
            const double centreY = static_cast<double>(token.y) * static_cast<double>(result.height() - 1U);
            const double dirX = static_cast<double>(token.dx);
            const double dirY = static_cast<double>(token.dy);
            const double lengthPx = std::max(2.0,
                static_cast<double>(token.length) * static_cast<double>(std::max(result.width(), result.height())));
            const double widthPx = std::max(1.0,
                static_cast<double>(token.width) * static_cast<double>(std::min(result.width(), result.height())));
            const std::size_t steps = static_cast<std::size_t>(std::ceil(lengthPx * 1.35));
            for (std::size_t step = 0; step < steps; ++step) {
                const double t = steps <= 1U ? 0.0
                    : (static_cast<double>(step) / static_cast<double>(steps - 1U) - 0.5);
                const double jitter = 0.12 * shapeNoise(seed, 7101U + static_cast<std::uint64_t>(index) * 131U + step);
                const double px = centreX + (t + jitter * 0.08) * lengthPx * dirX;
                const double py = centreY + (t + jitter * 0.08) * lengthPx * dirY;
                const std::size_t x0 = static_cast<std::size_t>(std::max(0.0, std::floor(px - 2.5 * widthPx)));
                const std::size_t y0 = static_cast<std::size_t>(std::max(0.0, std::floor(py - 2.5 * widthPx)));
                const std::size_t x1 = static_cast<std::size_t>(std::min(
                    static_cast<double>(result.width()), std::ceil(px + 2.5 * widthPx)));
                const std::size_t y1 = static_cast<std::size_t>(std::min(
                    static_cast<double>(result.height()), std::ceil(py + 2.5 * widthPx)));
                for (std::size_t y = y0; y < y1; ++y) {
                    for (std::size_t x = x0; x < x1; ++x) {
                        const double dx = (static_cast<double>(x) - px) / widthPx;
                        const double dy = (static_cast<double>(y) - py) / widthPx;
                        const double radial = dx * dx + dy * dy;
                        const double alpha = (0.035 + 0.13 * clamp01(token.intensity))
                            * std::exp(-0.85 * radial);
                        blendPixel(result, x, y, rgb, alpha);
                    }
                }
            }
        }
    }

    std::vector<NeuralMotorStroke> extractSelfMotorMemory(
        const VisualCanvas& selfResult) const {
        validateCanvas(selfResult);
        std::vector<NeuralMotorStroke> memory;
        memory.reserve(selfResult.pixels().size());
        for (std::size_t index = 0; index < selfResult.pixels().size(); ++index) {
            const std::size_t colorIndex = index * 3U;
            const auto red = static_cast<std::uint8_t>(std::llround(
                clamp01(selfResult.colorPixels()[colorIndex]) * 255.0));
            const auto green = static_cast<std::uint8_t>(std::llround(
                clamp01(selfResult.colorPixels()[colorIndex + 1U]) * 255.0));
            const auto blue = static_cast<std::uint8_t>(std::llround(
                clamp01(selfResult.colorPixels()[colorIndex + 2U]) * 255.0));
            // The persistent canvas is implicitly black. Storing only visible
            // pigment actions keeps the representation motoric rather than a
            // fixed-size bitmap while still retaining exact RGB8 pigment for
            // every action TATARUS actually needs to reproduce.
            if (red == 0U && green == 0U && blue == 0U) continue;
            if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                throw std::runtime_error("IMAGINATIO self motor index exceeds uint32 capacity");
            }
            memory.push_back(NeuralMotorStroke{
                .pixelIndex = static_cast<std::uint32_t>(index),
                .red = red,
                .green = green,
                .blue = blue,
            });
        }
        return memory;
    }

    VisualCanvas selfMotorCanvas(const VisualEngram& engram) const {
        VisualCanvas result(config.width, config.height);
        result.clear();
        if (!engram.selfMotorMemoryValid) return result;
        const std::size_t pixelCount = result.pixels().size();
        for (const auto& stroke : engram.selfMotorMemory) {
            const std::size_t index = static_cast<std::size_t>(stroke.pixelIndex);
            if (index >= pixelCount) {
                throw std::runtime_error("IMAGINATIO self motor memory contains an invalid pixel position");
            }
            const std::size_t x = index % result.width();
            const std::size_t y = index / result.width();
            result.setColorPixel(
                x, y,
                static_cast<double>(stroke.red) / 255.0,
                static_cast<double>(stroke.green) / 255.0,
                static_cast<double>(stroke.blue) / 255.0);
        }
        return result;
    }

    AssemblyId consolidateSelfResult(
        VisualEngram& engram,
        const VisualCanvas& selfResult,
        std::string_view symbol,
        double sourceSimilarity) {
        validateCanvas(selfResult);

        // Re-perceive TATARUS' own finished painting while plasticity is still
        // active. This is the V14 self-imprint step: visual result, motor result
        // and the active episode are bound before the external lesson vanishes.
        const std::string selfSymbol = symbol.empty()
            ? std::string("SELF_RESULT")
            : std::string("SELF_RESULT:") + std::string(symbol);
        const auto selfObserved = expose(
            &selfResult,
            selfSymbol,
            ImaginationStage::ObserveAndTrace,
            mix64(engram.id ^ 0x53454c465f563134ULL),
            true);
        const AssemblyId assembly = representedAssembly(selfObserved);

        ++engram.selfConsolidations;
        const bool replaceMotorMemory = !engram.selfMotorMemoryValid
            || sourceSimilarity >= engram.selfSimilarity;
        if (replaceMotorMemory) {
            engram.selfFingerprint = visualFingerprint(selfResult);
            engram.selfShape = extractShape(selfResult);
            engram.selfFeatureModel = extractFeatureModel(selfResult);
            engram.regions = extractRegionTokens(selfResult);
            engram.strokes = extractStrokeTokens(selfResult);
            engram.selfMotorMemory = extractSelfMotorMemory(selfResult);
            engram.selfSimilarity = clamp01(sourceSimilarity);
            engram.selfMotorMemoryValid = true;
        }
        if (assembly != 0U) {
            engram.selfAssembly = assembly;
            if (std::find(engram.assemblies.begin(), engram.assemblies.end(), assembly)
                == engram.assemblies.end()) {
                engram.assemblies.push_back(assembly);
            }
        }
        return assembly;
    }

    VisualCanvas engramPrototype(const VisualEngram& engram) const {
        if (engram.selfMotorMemoryValid) {
            return selfMotorCanvas(engram);
        }
        auto result = synthesizeAbstractMemory(
            engram.shape,
            engram.featureModel,
            nullptr,
            engram.observations,
            mix64(engram.contentHash != 0U ? engram.contentHash : engram.id));
        applyRegionMemory(result, engram.regions);
        applyStrokeMemory(
            result,
            engram.strokes,
            mix64((engram.contentHash != 0U ? engram.contentHash : engram.id) ^ engram.observations));
        return result;
    }

    const VisualEngram* selectedForSymbol(
        const std::string& symbol,
        AssemblyId activeAssembly) const {
        const auto found = symbols.find(symbol);
        if (found == symbols.end()) return nullptr;
        const VisualEngram* best = nullptr;
        double bestScore = -1.0;
        for (const auto& association : found->second) {
            const auto* engram = engramById(association.engramId);
            if (engram == nullptr) continue;
            const double assemblyMatch = activeAssembly != 0
                    && std::find(engram->assemblies.begin(), engram->assemblies.end(), activeAssembly)
                        != engram->assemblies.end()
                ? 1.0 : 0.0;
            const double score = association.strength + 0.12 * assemblyMatch
                + 0.01 * std::log1p(static_cast<double>(association.observations));
            if (score > bestScore
                || (score == bestScore && best != nullptr && engram->id < best->id)) {
                best = engram;
                bestScore = score;
            }
        }
        return best;
    }

    CategoryEngram* categoryByName(const std::string& name) {
        const auto found = std::find_if(categories.begin(), categories.end(),
            [&name](const CategoryEngram& category) { return category.name == name; });
        return found == categories.end() ? nullptr : &*found;
    }

    const CategoryEngram* categoryByName(const std::string& name) const {
        const auto found = std::find_if(categories.begin(), categories.end(),
            [&name](const CategoryEngram& category) { return category.name == name; });
        return found == categories.end() ? nullptr : &*found;
    }

    const VisualEngram* representativeEngram(const CategoryEngram& category) const {
        const VisualEngram* best = nullptr;
        double bestScore = -1.0;
        for (const auto& engram : engrams) {
            const double score = 0.78 * featureSimilarity(
                engram.featureModel, category.featureMean)
                + 0.22 * shapeSimilarity(engram.shape, category.shapeMean);
            if (score > bestScore) {
                best = &engram;
                bestScore = score;
            }
        }
        return best;
    }

    VisualCanvas alignCategoryExample(
        const VisualCanvas& source,
        const VisualCanvas& target) const {
        const auto centroid = [](const VisualCanvas& image) {
            std::array<double, 3> background{};
            std::size_t borderSamples = 0;
            for (std::size_t x = 0; x < image.width(); ++x) {
                for (const std::size_t y : {std::size_t{0}, image.height() - 1U}) {
                    const auto color = image.colorPixel(x, y);
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        background[channel] += color[channel];
                    }
                    ++borderSamples;
                }
            }
            for (std::size_t y = 1; y + 1U < image.height(); ++y) {
                for (const std::size_t x : {std::size_t{0}, image.width() - 1U}) {
                    const auto color = image.colorPixel(x, y);
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        background[channel] += color[channel];
                    }
                    ++borderSamples;
                }
            }
            for (double& channel : background) {
                channel /= static_cast<double>(std::max<std::size_t>(1U, borderSamples));
            }
            double sumX = 0.0;
            double sumY = 0.0;
            double sumWeight = 0.0;
            for (std::size_t y = 0; y < image.height(); ++y) {
                for (std::size_t x = 0; x < image.width(); ++x) {
                    const auto color = image.colorPixel(x, y);
                    const double difference = std::max({
                        std::abs(color[0] - background[0]),
                        std::abs(color[1] - background[1]),
                        std::abs(color[2] - background[2]),
                    });
                    const double weight = std::max(0.0, difference - 0.06);
                    sumX += weight * static_cast<double>(x);
                    sumY += weight * static_cast<double>(y);
                    sumWeight += weight;
                }
            }
            if (sumWeight <= 1e-9) {
                return std::array<double, 2>{
                    static_cast<double>(image.width() - 1U) * 0.5,
                    static_cast<double>(image.height() - 1U) * 0.5,
                };
            }
            return std::array<double, 2>{sumX / sumWeight, sumY / sumWeight};
        };
        const auto sourceCenter = centroid(source);
        const auto targetCenter = centroid(target);
        const auto maximumShiftX = static_cast<std::ptrdiff_t>(source.width() / 4U);
        const auto maximumShiftY = static_cast<std::ptrdiff_t>(source.height() / 4U);
        const auto shiftX = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(std::llround(targetCenter[0] - sourceCenter[0])),
            -maximumShiftX, maximumShiftX);
        const auto shiftY = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(std::llround(targetCenter[1] - sourceCenter[1])),
            -maximumShiftY, maximumShiftY);
        if (shiftX == 0 && shiftY == 0) return source;

        std::array<double, 3> background{};
        for (std::size_t x = 0; x < source.width(); ++x) {
            const auto color = source.colorPixel(x, 0);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                background[channel] += color[channel];
            }
        }
        for (double& channel : background) channel /= static_cast<double>(source.width());
        VisualCanvas aligned(source.width(), source.height());
        for (std::size_t y = 0; y < aligned.height(); ++y) {
            for (std::size_t x = 0; x < aligned.width(); ++x) {
                const auto sourceX = static_cast<std::ptrdiff_t>(x) - shiftX;
                const auto sourceY = static_cast<std::ptrdiff_t>(y) - shiftY;
                const auto color = sourceX >= 0 && sourceY >= 0
                        && sourceX < static_cast<std::ptrdiff_t>(source.width())
                        && sourceY < static_cast<std::ptrdiff_t>(source.height())
                    ? source.colorPixel(
                        static_cast<std::size_t>(sourceX), static_cast<std::size_t>(sourceY))
                    : background;
                aligned.setColorPixel(x, y, color[0], color[1], color[2]);
            }
        }
        return aligned;
    }

    static double shapeNoise(std::uint64_t seed, std::uint64_t salt) {
        std::uint64_t value = seed + 0x9e3779b97f4a7c15ULL * (salt + 1U);
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        value ^= value >> 31U;
        return 2.0 * static_cast<double>(value >> 11U)
                / static_cast<double>(std::uint64_t{1} << 53U)
            - 1.0;
    }

    static ShapeSignature constrainShape(ShapeSignature shape) {
        // Centre, upper-left, upper-right, lower-left, lower-right. Keeping the
        // fields ordered prevents a deformation from folding the image while
        // still allowing learned proportions and pose to vary.
        shape[0][0] = std::clamp(shape[0][0], 0.28, 0.72);
        shape[0][1] = std::clamp(shape[0][1], 0.25, 0.75);
        shape[1][0] = std::clamp(shape[1][0], 0.08, 0.49);
        shape[1][1] = std::clamp(shape[1][1], 0.08, 0.54);
        shape[2][0] = std::clamp(shape[2][0], 0.51, 0.92);
        shape[2][1] = std::clamp(shape[2][1], 0.08, 0.54);
        shape[3][0] = std::clamp(shape[3][0], 0.08, 0.49);
        shape[3][1] = std::clamp(shape[3][1], 0.46, 0.92);
        shape[4][0] = std::clamp(shape[4][0], 0.51, 0.92);
        shape[4][1] = std::clamp(shape[4][1], 0.46, 0.92);
        return shape;
    }

    std::array<double, 3> imageBackground(const VisualCanvas& image) const {
        std::array<double, 3> background{};
        std::size_t samples = 0;
        for (std::size_t x = 0; x < image.width(); ++x) {
            for (const std::size_t y : {std::size_t{0}, image.height() - 1U}) {
                const auto color = image.colorPixel(x, y);
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    background[channel] += color[channel];
                }
                ++samples;
            }
        }
        for (std::size_t y = 1; y + 1U < image.height(); ++y) {
            for (const std::size_t x : {std::size_t{0}, image.width() - 1U}) {
                const auto color = image.colorPixel(x, y);
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    background[channel] += color[channel];
                }
                ++samples;
            }
        }
        for (double& channel : background) {
            channel /= static_cast<double>(std::max<std::size_t>(1U, samples));
        }
        return background;
    }

    ShapeSignature extractShape(const VisualCanvas& image) const {
        static constexpr ShapeSignature fieldCenters{{
            ShapePoint{0.50, 0.50}, ShapePoint{0.29, 0.32},
            ShapePoint{0.71, 0.32}, ShapePoint{0.32, 0.70},
            ShapePoint{0.68, 0.70},
        }};
        static constexpr std::array<double, 5> fieldSigma{
            0.25, 0.20, 0.20, 0.20, 0.20,
        };
        const auto background = imageBackground(image);
        ShapeSignature weighted{};
        std::array<double, 5> weightSums{};
        for (std::size_t y = 0; y < image.height(); ++y) {
            const double normalizedY = static_cast<double>(y)
                / static_cast<double>(image.height() - 1U);
            const std::size_t up = y == 0U ? y : y - 1U;
            const std::size_t down = std::min(image.height() - 1U, y + 1U);
            for (std::size_t x = 0; x < image.width(); ++x) {
                const double normalizedX = static_cast<double>(x)
                    / static_cast<double>(image.width() - 1U);
                const std::size_t left = x == 0U ? x : x - 1U;
                const std::size_t right = std::min(image.width() - 1U, x + 1U);
                const auto color = image.colorPixel(x, y);
                const double contrast = std::max({
                    std::abs(color[0] - background[0]),
                    std::abs(color[1] - background[1]),
                    std::abs(color[2] - background[2]),
                });
                const double gradient = 0.5 * (
                    std::abs(image.pixel(right, y) - image.pixel(left, y))
                    + std::abs(image.pixel(x, down) - image.pixel(x, up)));
                // Edges carry form; foreground contrast supplies support in
                // smooth regions such as cheeks, paper or a painted surface.
                const double structure = 0.002 + 1.8 * gradient
                    + 0.38 * std::max(0.0, contrast - 0.025);
                for (std::size_t field = 0; field < fieldCenters.size(); ++field) {
                    const double dx = normalizedX - fieldCenters[field][0];
                    const double dy = normalizedY - fieldCenters[field][1];
                    const double sigma = fieldSigma[field];
                    const double window = std::exp(
                        -(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                    const double weight = window * structure;
                    weighted[field][0] += weight * normalizedX;
                    weighted[field][1] += weight * normalizedY;
                    weightSums[field] += weight;
                }
            }
        }
        ShapeSignature result{};
        for (std::size_t field = 0; field < result.size(); ++field) {
            if (weightSums[field] <= 1e-12) {
                result[field] = fieldCenters[field];
                continue;
            }
            // A small cortical prior keeps the five fields semantically
            // ordered; most of their position still comes from the image.
            result[field][0] = 0.18 * fieldCenters[field][0]
                + 0.82 * weighted[field][0] / weightSums[field];
            result[field][1] = 0.18 * fieldCenters[field][1]
                + 0.82 * weighted[field][1] / weightSums[field];
        }
        return constrainShape(result);
    }

    static constexpr std::array<std::string_view, 10> featureRegionNames() {
        return {
            "BACKGROUND",
            "UPPER_LEFT", "UPPER_CENTER", "UPPER_RIGHT",
            "MIDDLE_LEFT", "CENTER", "MIDDLE_RIGHT",
            "LOWER_LEFT", "LOWER_CENTER", "LOWER_RIGHT",
        };
    }

    static std::array<double, 10> featureRegionWeights(
        double normalizedX,
        double normalizedY,
        const ShapeSignature& shape) {
        const double centreX = shape[0][0];
        const double leftX = 0.5 * (shape[1][0] + shape[3][0]);
        const double rightX = 0.5 * (shape[2][0] + shape[4][0]);
        const double upperX = 0.5 * (shape[1][0] + shape[2][0]);
        const double upperY = 0.5 * (shape[1][1] + shape[2][1]);
        const double lowerX = 0.5 * (shape[3][0] + shape[4][0]);
        const double lowerY = 0.5 * (shape[3][1] + shape[4][1]);
        const double faceX = (centreX + upperX + lowerX) / 3.0;
        const double faceY = 0.5 * (upperY + lowerY);
        const double halfWidth = std::clamp(
            0.72 * std::abs(shape[2][0] - shape[1][0]), 0.24, 0.42);
        const double halfHeight = std::clamp(
            0.78 * std::abs(lowerY - upperY) + 0.12, 0.28, 0.46);
        const auto gaussian = [normalizedX, normalizedY](
            double centreRegionX,
            double centreRegionY,
            double sigmaX,
            double sigmaY) {
            const double dx = (normalizedX - centreRegionX) / sigmaX;
            const double dy = (normalizedY - centreRegionY) / sigmaY;
            return std::exp(-0.5 * (dx * dx + dy * dy));
        };
        const double ellipticalDistance = std::sqrt(
            std::pow((normalizedX - faceX) / std::max(0.12, halfWidth), 2.0)
            + std::pow((normalizedY - faceY) / std::max(0.16, halfHeight), 2.0));
        const double outside = clamp01((ellipticalDistance - 0.72) / 0.58);
        const double sigmaX = std::max(0.13, 0.56 * halfWidth);
        const double sigmaY = std::max(0.11, 0.45 * halfHeight);
        std::array<double, 10> weights{
            0.035 + 2.4 * outside * outside,
            gaussian(leftX, upperY, sigmaX, sigmaY),
            gaussian(upperX, upperY, sigmaX, sigmaY),
            gaussian(rightX, upperY, sigmaX, sigmaY),
            gaussian(leftX, shape[0][1], sigmaX, sigmaY),
            1.08 * gaussian(centreX, shape[0][1], sigmaX, sigmaY),
            gaussian(rightX, shape[0][1], sigmaX, sigmaY),
            gaussian(leftX, lowerY, sigmaX, sigmaY),
            gaussian(lowerX, lowerY, sigmaX, sigmaY),
            gaussian(rightX, lowerY, sigmaX, sigmaY),
        };
        // A tiny common support prevents undefined ownership even for unusual
        // non-face categories while retaining smooth, overlap-normalized seams.
        for (double& weight : weights) weight += 1e-6;
        return weights;
    }

    static neuro::vision::ObjectFieldLayout visualFieldLayout(
        const ShapeSignature& shape) {
        const double centreX = shape[0][0];
        const double leftX = 0.5 * (shape[1][0] + shape[3][0]);
        const double rightX = 0.5 * (shape[2][0] + shape[4][0]);
        const double upperX = 0.5 * (shape[1][0] + shape[2][0]);
        const double upperY = 0.5 * (shape[1][1] + shape[2][1]);
        const double lowerX = 0.5 * (shape[3][0] + shape[4][0]);
        const double lowerY = 0.5 * (shape[3][1] + shape[4][1]);
        const double objectX = (centreX + upperX + lowerX) / 3.0;
        const double objectY = 0.5 * (upperY + lowerY);
        const double halfWidth = std::clamp(
            0.72 * std::abs(shape[2][0] - shape[1][0]), 0.24, 0.42);
        const double halfHeight = std::clamp(
            0.78 * std::abs(lowerY - upperY) + 0.12, 0.28, 0.46);
        const double sigmaX = std::max(0.13, 0.56 * halfWidth);
        const double sigmaY = std::max(0.11, 0.45 * halfHeight);
        neuro::vision::ObjectFieldLayout layout;
        layout.objectCenterX = objectX;
        layout.objectCenterY = objectY;
        layout.objectHalfWidth = halfWidth;
        layout.objectHalfHeight = halfHeight;
        layout.fields = {{
            {leftX, upperY, sigmaX, sigmaY},
            {upperX, upperY, sigmaX, sigmaY},
            {rightX, upperY, sigmaX, sigmaY},
            {leftX, shape[0][1], sigmaX, sigmaY},
            {centreX, shape[0][1], sigmaX, sigmaY},
            {rightX, shape[0][1], sigmaX, sigmaY},
            {leftX, lowerY, sigmaX, sigmaY},
            {lowerX, lowerY, sigmaX, sigmaY},
            {rightX, lowerY, sigmaX, sigmaY},
        }};
        return layout;
    }

    FeatureModel extractFeatureModel(const VisualCanvas& image) const {
        const auto shape = extractShape(image);
        ocularSystem.reset();
        auto ocularFrame = ocularSystem.process(
            image.width(), image.height(), image.colorPixels(), 0.02);
        auto percept = visualPathway.perceive(
            image.width(), image.height(), ocularFrame.retinalRgb,
            visualFieldLayout(shape));
        const auto features = percept.integratedParts;
        lastVisualPercept = std::move(percept);
        return features;
    }

    static double featureSimilarity(const FeatureModel& observed, const FeatureModel& candidate) {
        static constexpr std::array<double, 9> scales{
            0.45, 0.45, 0.45, 2.2, 3.0, 3.0, 3.0, 3.0, 2.6,
        };
        double distance = 0.0;
        for (std::size_t region = 0; region < observed.size(); ++region) {
            for (std::size_t feature = 0; feature < scales.size(); ++feature) {
                const double delta = observed[region][feature] - candidate[region][feature];
                distance += scales[feature] * delta * delta;
            }
        }
        distance /= static_cast<double>(observed.size() * scales.size());
        return std::exp(-5.0 * std::sqrt(std::max(0.0, distance)));
    }

    static std::string dominantVentralArea(
        const neuro::vision::VentralStreamActivation& ventral) {
        const std::array<std::pair<std::string_view, double>, 4> areas{{
            {"LATERAL_OCCIPITAL_OBJECT", ventral.lateralOccipitalObject},
            {"FUSIFORM_FACE", ventral.fusiformFace},
            {"PARAHIPPOCAMPAL_PLACE", ventral.parahippocampalPlace},
            {"BIOLOGICAL_OBJECT", ventral.biologicalObject},
        }};
        return std::string(std::max_element(
            areas.begin(), areas.end(),
            [](const auto& left, const auto& right) {
                return left.second < right.second;
            })->first);
    }

    VisualCanvas foveateObject(
        const VisualCanvas& source,
        const std::array<double, 4>& rawBounds) const {
        auto bounds = rawBounds;
        bounds[0] = clamp01(bounds[0]);
        bounds[1] = clamp01(bounds[1]);
        bounds[2] = clamp01(bounds[2]);
        bounds[3] = clamp01(bounds[3]);
        if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) return source;

        const double width = bounds[2] - bounds[0];
        const double height = bounds[3] - bounds[1];
        const double padX = std::max(0.015, 0.08 * width);
        const double padY = std::max(0.015, 0.08 * height);
        bounds[0] = clamp01(bounds[0] - padX);
        bounds[1] = clamp01(bounds[1] - padY);
        bounds[2] = clamp01(bounds[2] + padX);
        bounds[3] = clamp01(bounds[3] + padY);

        VisualCanvas result(config.width, config.height);
        const auto background = imageBackground(source);
        for (std::size_t y = 0; y < result.height(); ++y) {
            for (std::size_t x = 0; x < result.width(); ++x) {
                result.setColorPixel(x, y, background[0], background[1], background[2]);
            }
        }

        const double sourceAspect = (bounds[2] - bounds[0])
            / std::max(1e-6, bounds[3] - bounds[1]);
        const double canvasAspect = static_cast<double>(result.width())
            / static_cast<double>(result.height());
        double targetHalfWidth = 0.42;
        double targetHalfHeight = 0.42;
        if (sourceAspect > canvasAspect) {
            targetHalfHeight *= canvasAspect / sourceAspect;
        } else {
            targetHalfWidth *= sourceAspect / canvasAspect;
        }
        const double left = 0.5 - targetHalfWidth;
        const double right = 0.5 + targetHalfWidth;
        const double top = 0.5 - targetHalfHeight;
        const double bottom = 0.5 + targetHalfHeight;
        for (std::size_t y = 0; y < result.height(); ++y) {
            const double ny = static_cast<double>(y) / static_cast<double>(result.height() - 1U);
            if (ny < top || ny > bottom) continue;
            const double localY = (ny - top) / std::max(1e-9, bottom - top);
            for (std::size_t x = 0; x < result.width(); ++x) {
                const double nx = static_cast<double>(x) / static_cast<double>(result.width() - 1U);
                if (nx < left || nx > right) continue;
                const double localX = (nx - left) / std::max(1e-9, right - left);
                const auto color = bilinearSample(
                    source,
                    bounds[0] + localX * (bounds[2] - bounds[0]),
                    bounds[1] + localY * (bounds[3] - bounds[1]));
                result.setColorPixel(x, y, color[0], color[1], color[2]);
            }
        }
        return result;
    }

    VisualRecognitionReport recognizeCategoriesInternal(
        const VisualCanvas& cue,
        const std::vector<std::string>& rawExpectedCategories,
        std::size_t maximumMatches) const {
        validateCanvas(cue);
        if (maximumMatches == 0U || maximumMatches > 64U) {
            throw std::invalid_argument("Visual recognition maximumMatches must be in [1, 64]");
        }
        VisualRecognitionReport report;
        if (categories.empty()) return report;

        std::vector<std::string> expected;
        expected.reserve(rawExpectedCategories.size());
        for (const auto& value : rawExpectedCategories) {
            const auto normalized = normalizedSymbol(value);
            if (!normalized.empty()
                && std::find(expected.begin(), expected.end(), normalized) == expected.end()) {
                expected.push_back(normalized);
            }
        }

        const auto observedShape = extractShape(cue);
        const auto observedFeatures = extractFeatureModel(cue);
        report.dominantVentralArea = dominantVentralArea(lastVisualPercept.ventral);
        report.matches.reserve(categories.size());
        for (const auto& category : categories) {
            const double conceptSimilarity = featureSimilarity(
                observedFeatures, category.featureMean);
            const double morphologySimilarity = shapeSimilarity(
                observedShape, category.shapeMean);
            // V14 has no exemplar/anchor raster memory. Episodic evidence is
            // represented by the statistical confidence of the learned category
            // concept plus its independent structural morphology.
            const double episodicSimilarity =
                0.78 * conceptSimilarity + 0.22 * morphologySimilarity;
            const double bottomUp =
                0.82 * conceptSimilarity + 0.18 * morphologySimilarity;
            const bool contextExpected =
                std::find(expected.begin(), expected.end(), category.name) != expected.end();
            const double boost = contextExpected
                ? std::min(0.15, 0.15 * bottomUp * (1.0 - bottomUp)) : 0.0;
            report.matches.push_back(VisualCategoryMatch{
                .category = category.name,
                .conceptSimilarity = conceptSimilarity,
                .episodicSimilarity = episodicSimilarity,
                .bottomUpSimilarity = bottomUp,
                .topDownBoost = boost,
                .score = std::min(1.0, bottomUp + boost),
            });
        }
        std::stable_sort(
            report.matches.begin(), report.matches.end(),
            [](const auto& left, const auto& right) {
                if (left.score != right.score) return left.score > right.score;
                return left.category < right.category;
            });
        report.matches.resize(std::min(maximumMatches, report.matches.size()));
        const double scoreSum = std::accumulate(
            report.matches.begin(), report.matches.end(), 0.0,
            [](double sum, const auto& match) { return sum + match.score; });
        for (auto& match : report.matches) {
            match.confidence = scoreSum > 1e-12 ? match.score / scoreSum : 0.0;
        }
        report.success = !report.matches.empty();
        if (report.success) {
            const double margin = report.matches.size() == 1U
                ? report.matches.front().score
                : report.matches.front().score - report.matches[1U].score;
            report.recognized = report.matches.front().bottomUpSimilarity >= 0.45
                && (report.matches.size() == 1U || margin >= 0.015);
        }
        return report;
    }

    ObjectEngram* closestObjectEngram(const FeatureModel& features, double& similarity) {
        ObjectEngram* best = nullptr;
        similarity = 0.0;
        for (auto& memory : objectEngrams) {
            const double candidate = featureSimilarity(features, memory.featureMean);
            if (candidate > similarity) {
                similarity = candidate;
                best = &memory;
            }
        }
        return best;
    }

    const ObjectEngram* closestObjectEngram(
        const FeatureModel& features, double& similarity) const {
        const ObjectEngram* best = nullptr;
        similarity = 0.0;
        for (const auto& memory : objectEngrams) {
            const double candidate = featureSimilarity(features, memory.featureMean);
            if (candidate > similarity) {
                similarity = candidate;
                best = &memory;
            }
        }
        return best;
    }

    static std::string dominantObjectCategory(const ObjectEngram& memory) {
        const auto best = std::max_element(
            memory.categoryVotes.begin(), memory.categoryVotes.end(),
            [](const auto& left, const auto& right) { return left.second < right.second; });
        if (best == memory.categoryVotes.end()) return {};
        const std::uint64_t votes = std::accumulate(
            memory.categoryVotes.begin(), memory.categoryVotes.end(), std::uint64_t{0},
            [](std::uint64_t sum, const auto& value) { return sum + value.second; });
        if (votes == 0U
            || static_cast<double>(best->second) / static_cast<double>(votes) < 0.55) {
            return {};
        }
        return best->first;
    }

    ObjectEngram& learnObject(
        const FeatureModel& features,
        const std::string& category,
        bool& novel,
        double& similarity) {
        auto* memory = closestObjectEngram(features, similarity);
        if (memory != nullptr && similarity >= config.objectMemoryMatchThreshold) {
            novel = false;
            const std::uint64_t nextCount = memory->observations + 1U;
            for (std::size_t region = 0; region < features.size(); ++region) {
                for (std::size_t feature = 0; feature < features[region].size(); ++feature) {
                    const double delta = features[region][feature]
                        - memory->featureMean[region][feature];
                    memory->featureMean[region][feature] += delta
                        / static_cast<double>(nextCount);
                    const double delta2 = features[region][feature]
                        - memory->featureMean[region][feature];
                    memory->featureM2[region][feature] += delta * delta2;
                }
            }
            memory->observations = nextCount;
            if (!category.empty()) ++memory->categoryVotes[category];
            return *memory;
        }

        novel = true;
        if (objectEngrams.size() >= config.maximumObjectEngrams) {
            const auto weakest = std::min_element(
                objectEngrams.begin(), objectEngrams.end(),
                [](const auto& left, const auto& right) {
                    if (left.observations != right.observations) {
                        return left.observations < right.observations;
                    }
                    return left.id < right.id;
                });
            if (weakest != objectEngrams.end()) objectEngrams.erase(weakest);
        }
        ObjectEngram created;
        created.id = nextObjectEngramId++;
        created.observations = 1U;
        created.featureMean = features;
        if (!category.empty()) created.categoryVotes[category] = 1U;
        objectEngrams.push_back(std::move(created));
        similarity = 0.0;
        return objectEngrams.back();
    }

    VisualScenePerceptionReport perceiveSceneInternal(
        const VisualCanvas& cue,
        const std::vector<std::string>& expectedCategories,
        bool learnObjects,
        std::size_t maximumObjects,
        bool internallyGenerated) {
        validateCanvas(cue);
        if (maximumObjects == 0U || maximumObjects > 64U) {
            throw std::invalid_argument("Visual scene maximumObjects must be in [1, 64]");
        }
        ocularSystem.reset();
        auto ocularFrame = ocularSystem.process(
            cue.width(), cue.height(), cue.colorPixels(), 0.02);
        auto fullPercept = visualPathway.perceive(
            cue.width(), cue.height(), ocularFrame.retinalRgb);
        auto candidates = fullPercept.objects;
        lastVisualPercept = fullPercept;
        if (candidates.size() > maximumObjects) candidates.resize(maximumObjects);

        VisualScenePerceptionReport report;
        report.internallyGenerated = internallyGenerated;
        report.candidateCount = candidates.size();
        report.objects.reserve(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            const auto foveated = foveateObject(cue, candidate.bounds);
            const auto features = extractFeatureModel(foveated);
            const auto recognition = recognizeCategoriesInternal(
                foveated, expectedCategories, 3U);
            std::string category;
            bool recognized = false;
            if (recognition.recognized && !recognition.matches.empty()) {
                category = recognition.matches.front().category;
                recognized = true;
            }

            double memorySimilarity = 0.0;
            bool novel = true;
            std::uint64_t memoryId = 0;
            ObjectEngram* writableMemory = nullptr;
            if (learnObjects) {
                writableMemory = &learnObject(
                    features, category, novel, memorySimilarity);
                memoryId = writableMemory->id;
            } else {
                const auto* memory = closestObjectEngram(features, memorySimilarity);
                if (memory != nullptr && memorySimilarity >= config.objectMemoryMatchThreshold) {
                    novel = false;
                    memoryId = memory->id;
                    if (category.empty()) category = dominantObjectCategory(*memory);
                    recognized = recognized || !category.empty();
                }
            }
            if (writableMemory != nullptr && category.empty()) {
                category = dominantObjectCategory(*writableMemory);
                recognized = !category.empty();
            }
            VisualObjectPercept object;
            object.objectEngramId = memoryId;
            object.instance = memoryId != 0U
                ? "OBJECT-" + std::to_string(memoryId)
                : "NOVEL-" + std::to_string(index + 1U);
            object.category = category;
            object.recognizedCategory = recognized;
            object.novelObject = novel;
            object.internallyGenerated = internallyGenerated;
            object.bounds = candidate.bounds;
            object.center = candidate.center;
            object.areaFraction = candidate.areaFraction;
            object.saliency = candidate.saliency;
            object.objectMemorySimilarity = memorySimilarity;
            object.dominantVentralArea = recognition.dominantVentralArea.empty()
                ? dominantVentralArea(candidate.ventral)
                : recognition.dominantVentralArea;
            object.categoryMatches = recognition.matches;
            if (object.recognizedCategory) ++report.recognizedCount;
            if (object.novelObject) ++report.novelCount;
            report.objects.push_back(std::move(object));
        }

        for (std::size_t leftIndex = 0; leftIndex < report.objects.size(); ++leftIndex) {
            for (std::size_t rightIndex = leftIndex + 1U;
                 rightIndex < report.objects.size(); ++rightIndex) {
                const auto& left = report.objects[leftIndex];
                const auto& right = report.objects[rightIndex];
                const double dx = left.center[0] - right.center[0];
                const double dy = left.center[1] - right.center[1];
                const double distance = std::hypot(dx, dy);
                const double ix = std::max(
                    0.0, std::min(left.bounds[2], right.bounds[2])
                        - std::max(left.bounds[0], right.bounds[0]));
                const double iy = std::max(
                    0.0, std::min(left.bounds[3], right.bounds[3])
                        - std::max(left.bounds[1], right.bounds[1]));
                const double intersection = ix * iy;
                if (std::abs(dx) >= 0.08) {
                    report.relations.push_back(SceneRelationCue{
                        .subject = left.instance,
                        .relation = dx < 0.0 ? SceneRelationKind::LeftOf : SceneRelationKind::RightOf,
                        .object = right.instance,
                        .confidence = clamp01(std::abs(dx) / 0.5),
                    });
                }
                if (std::abs(dy) >= 0.08) {
                    report.relations.push_back(SceneRelationCue{
                        .subject = left.instance,
                        .relation = dy < 0.0 ? SceneRelationKind::Above : SceneRelationKind::Below,
                        .object = right.instance,
                        .confidence = clamp01(std::abs(dy) / 0.5),
                    });
                }
                if (intersection > 1e-5) {
                    report.relations.push_back(SceneRelationCue{
                        .subject = left.instance,
                        .relation = SceneRelationKind::Overlapping,
                        .object = right.instance,
                        .confidence = clamp01(intersection
                            / std::max(1e-6, std::min(left.areaFraction, right.areaFraction))),
                    });
                } else if (distance < 0.38) {
                    report.relations.push_back(SceneRelationCue{
                        .subject = left.instance,
                        .relation = SceneRelationKind::Near,
                        .object = right.instance,
                        .confidence = clamp01(1.0 - distance / 0.38),
                    });
                } else if (distance > 0.62) {
                    report.relations.push_back(SceneRelationCue{
                        .subject = left.instance,
                        .relation = SceneRelationKind::Far,
                        .object = right.instance,
                        .confidence = clamp01((distance - 0.62) / 0.5),
                    });
                }
            }
        }
        report.success = !report.objects.empty();
        lastScenePerception = report;
        return report;
    }

    VisualCanvas canvasFromRgb8(const std::vector<std::uint8_t>& bytes) const {
        if (bytes.size() != config.width * config.height * 3U) {
            throw std::runtime_error("Category anchor has invalid RGB storage");
        }
        VisualCanvas result(config.width, config.height);
        for (std::size_t pixel = 0; pixel < result.pixels().size(); ++pixel) {
            result.setColorPixel(
                pixel % result.width(), pixel / result.width(),
                static_cast<double>(bytes[pixel * 3U]) / 255.0,
                static_cast<double>(bytes[pixel * 3U + 1U]) / 255.0,
                static_cast<double>(bytes[pixel * 3U + 2U]) / 255.0);
        }
        return result;
    }

    std::array<double, 3> bilinearSample(
        const VisualCanvas& source,
        double normalizedX,
        double normalizedY) const {
        const double sourceX = std::clamp(normalizedX, 0.0, 1.0)
            * static_cast<double>(source.width() - 1U);
        const double sourceY = std::clamp(normalizedY, 0.0, 1.0)
            * static_cast<double>(source.height() - 1U);
        const std::size_t x0 = static_cast<std::size_t>(std::floor(sourceX));
        const std::size_t y0 = static_cast<std::size_t>(std::floor(sourceY));
        const std::size_t x1 = std::min(source.width() - 1U, x0 + 1U);
        const std::size_t y1 = std::min(source.height() - 1U, y0 + 1U);
        const double fractionX = sourceX - static_cast<double>(x0);
        const double fractionY = sourceY - static_cast<double>(y0);
        const auto topLeft = source.colorPixel(x0, y0);
        const auto topRight = source.colorPixel(x1, y0);
        const auto bottomLeft = source.colorPixel(x0, y1);
        const auto bottomRight = source.colorPixel(x1, y1);
        std::array<double, 3> result{};
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const double top = topLeft[channel]
                + fractionX * (topRight[channel] - topLeft[channel]);
            const double bottom = bottomLeft[channel]
                + fractionX * (bottomRight[channel] - bottomLeft[channel]);
            result[channel] = top + fractionY * (bottom - top);
        }
        return result;
    }

    VisualCanvas warpToShape(
        const VisualCanvas& source,
        const ShapeSignature& sourceShape,
        const ShapeSignature& targetShape) const {
        VisualCanvas warped(config.width, config.height);
        for (std::size_t y = 0; y < config.height; ++y) {
            const double normalizedY = static_cast<double>(y)
                / static_cast<double>(config.height - 1U);
            for (std::size_t x = 0; x < config.width; ++x) {
                const double normalizedX = static_cast<double>(x)
                    / static_cast<double>(config.width - 1U);
                double displacementX = 0.0;
                double displacementY = 0.0;
                double weightSum = 0.0;
                for (std::size_t point = 0; point < targetShape.size(); ++point) {
                    const double dx = normalizedX - targetShape[point][0];
                    const double dy = normalizedY - targetShape[point][1];
                    const double weight = std::exp(-(dx * dx + dy * dy) / 0.075);
                    displacementX += weight
                        * (sourceShape[point][0] - targetShape[point][0]);
                    displacementY += weight
                        * (sourceShape[point][1] - targetShape[point][1]);
                    weightSum += weight;
                }
                const double edgeDistance = std::min({
                    normalizedX, normalizedY, 1.0 - normalizedX, 1.0 - normalizedY,
                });
                double edgeGain = clamp01(edgeDistance / 0.12);
                edgeGain = edgeGain * edgeGain * (3.0 - 2.0 * edgeGain);
                const auto color = bilinearSample(
                    source,
                    normalizedX + edgeGain * displacementX / std::max(1e-9, weightSum),
                    normalizedY + edgeGain * displacementY / std::max(1e-9, weightSum));
                warped.setColorPixel(x, y, color[0], color[1], color[2]);
            }
        }
        return warped;
    }

    ShapeSignature novelShape(
        const std::vector<ShapeSignature>& shapes,
        std::uint64_t seed) const {
        if (shapes.empty()) throw std::invalid_argument("Form synthesis needs visual shapes");
        ShapeSignature target{};
        std::vector<double> sourceWeights(shapes.size(), 1.0);
        double sourceWeightSum = 0.0;
        for (std::size_t source = 0; source < shapes.size(); ++source) {
            sourceWeights[source] = 0.65
                + 0.35 * (shapeNoise(seed, 37U + source) + 1.0) * 0.5;
            sourceWeightSum += sourceWeights[source];
        }
        for (std::size_t point = 0; point < target.size(); ++point) {
            for (std::size_t source = 0; source < shapes.size(); ++source) {
                target[point][0] += sourceWeights[source] * shapes[source][point][0];
                target[point][1] += sourceWeights[source] * shapes[source][point][1];
            }
            target[point][0] /= sourceWeightSum;
            target[point][1] /= sourceWeightSum;
            double varianceX = 0.0;
            double varianceY = 0.0;
            for (const auto& shape : shapes) {
                varianceX += std::pow(shape[point][0] - target[point][0], 2.0);
                varianceY += std::pow(shape[point][1] - target[point][1], 2.0);
            }
            varianceX /= static_cast<double>(shapes.size());
            varianceY /= static_cast<double>(shapes.size());
            target[point][0] += 0.22 * std::sqrt(varianceX)
                * shapeNoise(seed, point * 2U + 101U);
            target[point][1] += 0.22 * std::sqrt(varianceY)
                * shapeNoise(seed, point * 2U + 102U);
        }
        return constrainShape(target);
    }

    VisualCanvas mergeRegisteredForms(
        const std::vector<VisualCanvas>& registered,
        const ShapeSignature& targetShape,
        std::uint64_t seed,
        const FeatureModel* learnedMean,
        const FeatureModel* learnedM2,
        std::uint64_t learnedObservations,
        std::vector<std::size_t>* selectedSources) const {
        if (registered.empty()) throw std::invalid_argument("Form synthesis has no pigments");
        if (registered.size() == 1U) {
            if (selectedSources != nullptr) {
                selectedSources->assign(featureRegionNames().size(), 0U);
            }
            return registered.front();
        }
        std::vector<FeatureModel> sourceFeatures;
        sourceFeatures.reserve(registered.size());
        for (const auto& source : registered) {
            sourceFeatures.push_back(extractFeatureModel(source));
        }
        static constexpr std::array<double, 9> featureScales{
            1.0, 1.0, 1.0, 2.5, 3.0, 3.0, 3.0, 3.0, 2.5,
        };

        // Bottom-up parts must agree on viewpoint and scale before top-down
        // category expectations are allowed to combine them. A small cohort
        // around one seeded context exemplar prevents a close-up object from
        // receiving parts cut from a distant scene, without introducing any
        // category-specific detector or label.
        const auto featureDistance = [&](std::size_t left, std::size_t right) {
            double distance = 0.0;
            for (std::size_t region = 0; region < sourceFeatures[left].size(); ++region) {
                for (std::size_t value = 0; value < featureScales.size(); ++value) {
                    const double delta = sourceFeatures[left][region][value]
                        - sourceFeatures[right][region][value];
                    distance += featureScales[value] * delta * delta;
                }
            }
            return distance / static_cast<double>(
                sourceFeatures[left].size() * featureScales.size());
        };
        const double pivotUnit = 0.5 * (shapeNoise(seed, 613U) + 1.0);
        const std::size_t pivot = std::min(
            registered.size() - 1U,
            static_cast<std::size_t>(
                pivotUnit * static_cast<double>(registered.size())));
        std::vector<std::size_t> cohort(registered.size());
        std::iota(cohort.begin(), cohort.end(), 0U);
        std::stable_sort(cohort.begin(), cohort.end(), [&](std::size_t left, std::size_t right) {
            if (left == right) return false;
            if (left == pivot) return true;
            if (right == pivot) return false;
            const double leftDistance = featureDistance(pivot, left);
            const double rightDistance = featureDistance(pivot, right);
            if (leftDistance != rightDistance) return leftDistance < rightDistance;
            return left < right;
        });
        cohort.resize(std::min<std::size_t>(3U, cohort.size()));

        FeatureModel targetFeatures{};
        if (learnedMean != nullptr) {
            targetFeatures = *learnedMean;
            if (learnedM2 != nullptr && learnedObservations > 1U) {
                for (std::size_t region = 0; region < targetFeatures.size(); ++region) {
                    for (std::size_t value = 0; value < targetFeatures[region].size(); ++value) {
                        const double variance = std::max(0.0,
                            (*learnedM2)[region][value]
                                / static_cast<double>(learnedObservations - 1U));
                        targetFeatures[region][value] += std::min(
                            value < 3U ? 0.16 : 0.08, 0.34 * std::sqrt(variance))
                            * shapeNoise(seed, 701U + region * 17U + value);
                    }
                }
            }
        } else {
            for (const auto& features : sourceFeatures) {
                for (std::size_t region = 0; region < targetFeatures.size(); ++region) {
                    for (std::size_t value = 0; value < targetFeatures[region].size(); ++value) {
                        targetFeatures[region][value] += features[region][value]
                            / static_cast<double>(sourceFeatures.size());
                    }
                }
            }
        }

        // The category mean supplies the concept, while the local context
        // cohort supplies a coherent scale, pose and illumination prior.
        for (std::size_t region = 0; region < targetFeatures.size(); ++region) {
            for (std::size_t value = 0; value < targetFeatures[region].size(); ++value) {
                double cohortMean = 0.0;
                for (const auto source : cohort) {
                    cohortMean += sourceFeatures[source][region][value]
                        / static_cast<double>(cohort.size());
                }
                targetFeatures[region][value] = 0.25 * targetFeatures[region][value]
                    + 0.75 * cohortMean;
            }
        }

        std::array<std::size_t, 10> donors{};
        std::vector<std::size_t> useCounts(registered.size(), 0U);
        const std::size_t maxFieldsPerDonor = std::max<std::size_t>(
            4U, (donors.size() + cohort.size() - 1U) / cohort.size());
        for (std::size_t region = 0; region < donors.size(); ++region) {
            double bestScore = std::numeric_limits<double>::infinity();
            std::size_t bestSource = cohort.front();
            for (const auto source : cohort) {
                // No one concrete memory may own more than four fields while
                // alternatives exist. This preserves coherence without falling
                // back to a single carrier image.
                if (useCounts[source] >= maxFieldsPerDonor
                    && cohort.size() > 1U) continue;
                double score = 0.0;
                for (std::size_t value = 0; value < featureScales.size(); ++value) {
                    const double delta = sourceFeatures[source][region][value]
                        - targetFeatures[region][value];
                    score += featureScales[value] * delta * delta;
                }
                // Seeded tie-breaking and mild exploration make different
                // imagined variants visit different learned exemplars.
                score += 0.018 * (shapeNoise(
                    seed, 809U + region * 131U + source * 17U) + 1.0);
                score += 0.006 * static_cast<double>(useCounts[source] * useCounts[source]);
                if (region >= 2U && (region - 1U) % 3U != 0U
                    && donors[region - 1U] == source) {
                    score -= 0.018;
                }
                if (region >= 4U && donors[region - 3U] == source) {
                    score -= 0.012;
                }
                if (score < bestScore) {
                    bestScore = score;
                    bestSource = source;
                }
            }
            donors[region] = bestSource;
            ++useCounts[bestSource];
        }
        if (cohort.size() > 1U
            && std::all_of(donors.begin(), donors.end(),
                [&donors](std::size_t source) { return source == donors.front(); })) {
            donors.back() = cohort[1U];
            --useCounts[donors.front()];
            ++useCounts[donors.back()];
        }
        // Every member of the coherent cohort contributes at least one part.
        // This makes the result a genuine recombination while keeping the
        // number of identities/objects small enough for perceptual unity.
        for (const auto missing : cohort) {
            if (useCounts[missing] != 0U) continue;
            double bestIncrease = std::numeric_limits<double>::infinity();
            std::size_t replacementRegion = donors.size() - 1U;
            for (std::size_t region = 0; region < donors.size(); ++region) {
                if (useCounts[donors[region]] <= 1U) continue;
                double increase = 0.0;
                for (std::size_t value = 0; value < featureScales.size(); ++value) {
                    const double candidateDelta = sourceFeatures[missing][region][value]
                        - targetFeatures[region][value];
                    const double currentDelta = sourceFeatures[donors[region]][region][value]
                        - targetFeatures[region][value];
                    increase += featureScales[value]
                        * (candidateDelta * candidateDelta - currentDelta * currentDelta);
                }
                if (increase < bestIncrease) {
                    bestIncrease = increase;
                    replacementRegion = region;
                }
            }
            --useCounts[donors[replacementRegion]];
            donors[replacementRegion] = missing;
            ++useCounts[missing];
        }
        if (selectedSources != nullptr) {
            selectedSources->assign(donors.begin(), donors.end());
        }

        VisualCanvas result(config.width, config.height);
        for (std::size_t y = 0; y < config.height; ++y) {
            const double normalizedY = static_cast<double>(y)
                / static_cast<double>(config.height - 1U);
            for (std::size_t x = 0; x < config.width; ++x) {
                const double normalizedX = static_cast<double>(x)
                    / static_cast<double>(config.width - 1U);
                const auto regionWeights = featureRegionWeights(
                    normalizedX, normalizedY, targetShape);
                std::array<double, 10> ownership{};
                for (std::size_t region = 0; region < ownership.size(); ++region) {
                    ownership[region] = std::pow(regionWeights[region], 3.2);
                }
                const double regionWeightSum = std::accumulate(
                    ownership.begin(), ownership.end(), 0.0);
                std::array<double, 3> factorized{};
                for (std::size_t region = 0; region < donors.size(); ++region) {
                    const std::size_t donor = donors[region];
                    const auto color = registered[donors[region]].colorPixel(x, y);
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        const double paletteCorrection = 0.55
                            * (targetFeatures[region][channel]
                                - sourceFeatures[donor][region][channel]);
                        factorized[channel] += ownership[region]
                            * (color[channel] + paletteCorrection)
                            / std::max(1e-12, regionWeightSum);
                    }
                }
                // Full local detail follows the selected donor. Only its mean
                // pigment is nudged toward the category prior; no second full
                // image is superimposed, so contours cannot form double ghosts.
                const auto& color = factorized;
                result.setColorPixel(
                    x, y,
                    clamp01(color[0]), clamp01(color[1]), clamp01(color[2]));
            }
        }
        neuro::vision::TopDownVisualPrior prior;
        prior.expectedParts = targetFeatures;
        prior.gain = learnedMean == nullptr ? 0.12 : 0.25;
        ocularSystem.reset();
        auto ocularFrame = ocularSystem.process(
            result.width(), result.height(), result.colorPixels(), 0.02);
        lastVisualPercept = visualPathway.perceive(
            result.width(), result.height(), ocularFrame.retinalRgb,
            visualFieldLayout(targetShape), &prior);
        return result;
    }

    VisualCanvas synthesizeAbstractMemory(
        const ShapeSignature& rawShape,
        const FeatureModel& learnedMean,
        const FeatureModel* learnedM2,
        std::uint64_t learnedObservations,
        std::uint64_t seed) const {
        const ShapeSignature targetShape = constrainShape(rawShape);
        FeatureModel targetFeatures = learnedMean;
        if (learnedM2 != nullptr && learnedObservations > 1U) {
            for (std::size_t region = 0; region < targetFeatures.size(); ++region) {
                for (std::size_t value = 0; value < targetFeatures[region].size(); ++value) {
                    const double variance = std::max(
                        0.0,
                        (*learnedM2)[region][value]
                            / static_cast<double>(learnedObservations - 1U));
                    const double maximumJitter = value < 3U ? 0.10 : 0.055;
                    targetFeatures[region][value] += std::min(
                        maximumJitter,
                        0.24 * std::sqrt(variance))
                        * shapeNoise(seed, 1201U + region * 19U + value);
                }
            }
        }

        // No pixel exemplar participates here. Pigment is reconstructed from
        // ten object-centred population fields plus V1-like contrast/orientation
        // energies. The resulting raster is therefore an imagined rendering of
        // the memory statistics, not a stored training raster.
        VisualCanvas result(config.width, config.height);
        const double phaseA = kPi * shapeNoise(seed, 1301U);
        const double phaseB = kPi * shapeNoise(seed, 1302U);
        const double frequencyA = 4.0 + 2.0 * (shapeNoise(seed, 1303U) + 1.0);
        const double frequencyB = 6.0 + 2.5 * (shapeNoise(seed, 1304U) + 1.0);

        for (std::size_t y = 0; y < config.height; ++y) {
            const double ny = static_cast<double>(y)
                / static_cast<double>(config.height - 1U);
            for (std::size_t x = 0; x < config.width; ++x) {
                const double nx = static_cast<double>(x)
                    / static_cast<double>(config.width - 1U);
                const auto weights = featureRegionWeights(nx, ny, targetShape);
                const double weightSum = std::accumulate(
                    weights.begin(), weights.end(), 0.0);

                std::array<double, 3> pigment{};
                double contrast = 0.0;
                double horizontal = 0.0;
                double vertical = 0.0;
                double descending = 0.0;
                double ascending = 0.0;
                double junction = 0.0;
                for (std::size_t region = 0; region < weights.size(); ++region) {
                    const double weight = weights[region] / std::max(1e-12, weightSum);
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        pigment[channel] += weight * targetFeatures[region][channel];
                    }
                    contrast += weight * std::max(0.0, targetFeatures[region][3]);
                    horizontal += weight * std::max(0.0, targetFeatures[region][4]);
                    vertical += weight * std::max(0.0, targetFeatures[region][5]);
                    descending += weight * std::max(0.0, targetFeatures[region][6]);
                    ascending += weight * std::max(0.0, targetFeatures[region][7]);
                    junction += weight * std::max(0.0, targetFeatures[region][8]);
                }

                const double horizontalWave = std::sin(
                    (ny * frequencyA * 2.0 * kPi) + phaseA);
                const double verticalWave = std::sin(
                    (nx * frequencyA * 2.0 * kPi) - phaseB);
                const double descendingWave = std::sin(
                    ((nx + ny) * frequencyB * kPi) + phaseB);
                const double ascendingWave = std::sin(
                    ((nx - ny) * frequencyB * kPi) - phaseA);
                const double junctionWave = horizontalWave * verticalWave;
                const double structure =
                    horizontal * horizontalWave
                    + vertical * verticalWave
                    + 0.75 * descending * descendingWave
                    + 0.75 * ascending * ascendingWave
                    + 0.55 * junction * junctionWave;
                const double texture = 0.018 * shapeNoise(
                    seed,
                    1501U + static_cast<std::uint64_t>(x) * 131U
                        + static_cast<std::uint64_t>(y) * 8191U);
                const double modulation = std::clamp(
                    0.10 * contrast * structure + texture,
                    -0.12,
                    0.12);
                for (double& channel : pigment) {
                    channel = clamp01(channel + modulation);
                }
                result.setColorPixel(
                    x, y, pigment[0], pigment[1], pigment[2]);
            }
        }

        neuro::vision::TopDownVisualPrior prior;
        prior.expectedParts = targetFeatures;
        prior.gain = 0.20;
        ocularSystem.reset();
        auto ocularFrame = ocularSystem.process(
            result.width(), result.height(), result.colorPixels(), 0.02);
        lastVisualPercept = visualPathway.perceive(
            result.width(), result.height(), ocularFrame.retinalRgb,
            visualFieldLayout(targetShape), &prior);
        return result;
    }

    VisualCanvas synthesizeForm(
        const std::vector<VisualCanvas>& sources,
        std::uint64_t seed,
        std::optional<ShapeSignature> requestedShape = std::nullopt,
        const FeatureModel* learnedMean = nullptr,
        const FeatureModel* learnedM2 = nullptr,
        std::uint64_t learnedObservations = 0U,
        std::vector<std::size_t>* selectedSources = nullptr) const {
        if (sources.empty()) throw std::invalid_argument("Form synthesis needs source memories");
        std::vector<ShapeSignature> shapes;
        shapes.reserve(sources.size());
        for (const auto& source : sources) shapes.push_back(extractShape(source));
        const ShapeSignature targetShape = requestedShape.has_value()
            ? constrainShape(*requestedShape) : novelShape(shapes, seed);
        std::vector<VisualCanvas> registered;
        registered.reserve(sources.size());
        for (std::size_t source = 0; source < sources.size(); ++source) {
            registered.push_back(warpToShape(sources[source], shapes[source], targetShape));
        }
        return mergeRegisteredForms(
            registered, targetShape, seed, learnedMean, learnedM2,
            learnedObservations, selectedSources);
    }

    void updateCategory(
        const std::string& name,
        const VisualCanvas& cue,
        AssemblyId assembly,
        bool registerGeometry = true) {
        // registerGeometry is retained for source/API compatibility. V14 no
        // longer aligns against a stored raster exemplar because no such
        // exemplar exists after learning.
        static_cast<void>(registerGeometry);
        const auto observedShape = extractShape(cue);
        const auto observedFeatures = extractFeatureModel(cue);
        auto* category = categoryByName(name);
        if (category == nullptr) {
            if (categories.size() >= config.maximumCategories) {
                const auto victim = std::min_element(
                    categories.begin(), categories.end(),
                    [](const CategoryEngram& left, const CategoryEngram& right) {
                        if (left.observations != right.observations) {
                            return left.observations < right.observations;
                        }
                        return left.name < right.name;
                    });
                categories.erase(victim);
            }
            CategoryEngram created;
            created.name = name;
            created.observations = 1U;
            created.shapeMean = observedShape;
            created.featureMean = observedFeatures;
            if (assembly != 0U) created.assemblies.push_back(assembly);
            categories.push_back(std::move(created));
            return;
        }

        const std::uint64_t nextCount = category->observations + 1U;
        for (std::size_t point = 0; point < category->shapeMean.size(); ++point) {
            for (std::size_t axis = 0; axis < 2U; ++axis) {
                const double oldMean = category->shapeMean[point][axis];
                const double delta = observedShape[point][axis] - oldMean;
                category->shapeMean[point][axis] = oldMean
                    + delta / static_cast<double>(nextCount);
                category->shapeM2[point][axis] += delta
                    * (observedShape[point][axis] - category->shapeMean[point][axis]);
            }
        }
        for (std::size_t region = 0; region < category->featureMean.size(); ++region) {
            for (std::size_t value = 0; value < category->featureMean[region].size(); ++value) {
                const double oldMean = category->featureMean[region][value];
                const double delta = observedFeatures[region][value] - oldMean;
                category->featureMean[region][value] = oldMean
                    + delta / static_cast<double>(nextCount);
                category->featureM2[region][value] += delta
                    * (observedFeatures[region][value]
                        - category->featureMean[region][value]);
            }
        }
        category->shapeMean = constrainShape(category->shapeMean);
        category->observations = nextCount;
        if (assembly != 0U
            && std::find(category->assemblies.begin(), category->assemblies.end(), assembly)
                == category->assemblies.end()) {
            category->assemblies.push_back(assembly);
        }
    }

    VisualCanvas categoryVariant(
        const CategoryEngram& category,
        std::uint64_t variationSeed,
        std::vector<std::size_t>* selectedSources = nullptr) const {
        const std::uint64_t seed = variationSeed != 0U
            ? variationSeed : fnv1a(category.name + ':' + std::to_string(category.observations));
        ShapeSignature targetShape = category.shapeMean;
        if (category.observations > 1U) {
            for (std::size_t point = 0; point < targetShape.size(); ++point) {
                for (std::size_t axis = 0; axis < 2U; ++axis) {
                    const double variance = std::max(
                        0.0,
                        category.shapeM2[point][axis]
                            / static_cast<double>(category.observations - 1U));
                    targetShape[point][axis] += std::min(0.055, 0.38 * std::sqrt(variance))
                        * shapeNoise(seed, 211U + point * 2U + axis);
                }
            }
        }
        targetShape = constrainShape(targetShape);
        if (selectedSources != nullptr) {
            // V14 reports abstract cortical fields, not concrete image donors.
            selectedSources->assign(featureRegionNames().size(), 0U);
        }
        return synthesizeAbstractMemory(
            targetShape,
            category.featureMean,
            &category.featureM2,
            category.observations,
            seed);
    }

    PoseEngram* poseByName(const std::string& category, const std::string& pose) {
        const auto found = std::find_if(poses.begin(), poses.end(),
            [&category, &pose](const PoseEngram& value) {
                return value.category == category && value.pose == pose;
            });
        return found == poses.end() ? nullptr : &*found;
    }

    const PoseEngram* poseByName(
        const std::string& category,
        const std::string& pose) const {
        const auto found = std::find_if(poses.begin(), poses.end(),
            [&category, &pose](const PoseEngram& value) {
                return value.category == category && value.pose == pose;
            });
        return found == poses.end() ? nullptr : &*found;
    }

    void updatePose(
        const std::string& category,
        const std::string& pose,
        const VisualCanvas& cue,
        std::uint64_t sourceEngram,
        AssemblyId assembly) {
        const auto observedShape = extractShape(cue);
        const auto observedFeatures = extractFeatureModel(cue);
        auto* memory = poseByName(category, pose);
        if (memory == nullptr) {
            if (poses.size() >= config.maximumPoseEngrams) {
                const auto victim = std::min_element(
                    poses.begin(), poses.end(),
                    [](const PoseEngram& left, const PoseEngram& right) {
                        if (left.observations != right.observations) {
                            return left.observations < right.observations;
                        }
                        return std::tie(left.category, left.pose)
                            < std::tie(right.category, right.pose);
                    });
                poses.erase(victim);
            }
            PoseEngram created;
            created.category = category;
            created.pose = pose;
            created.observations = 1U;
            created.shapeMean = observedShape;
            created.featureMean = observedFeatures;
            if (sourceEngram != 0U) created.sourceEngrams.push_back(sourceEngram);
            if (assembly != 0U) created.assemblies.push_back(assembly);
            poses.push_back(std::move(created));
            return;
        }

        const auto nextCount = memory->observations + 1U;
        for (std::size_t point = 0; point < memory->shapeMean.size(); ++point) {
            for (std::size_t axis = 0; axis < 2U; ++axis) {
                const double oldMean = memory->shapeMean[point][axis];
                const double delta = observedShape[point][axis] - oldMean;
                memory->shapeMean[point][axis] = oldMean
                    + delta / static_cast<double>(nextCount);
                memory->shapeM2[point][axis] += delta
                    * (observedShape[point][axis] - memory->shapeMean[point][axis]);
            }
        }
        for (std::size_t region = 0; region < memory->featureMean.size(); ++region) {
            for (std::size_t value = 0; value < memory->featureMean[region].size(); ++value) {
                const double oldMean = memory->featureMean[region][value];
                const double delta = observedFeatures[region][value] - oldMean;
                memory->featureMean[region][value] = oldMean
                    + delta / static_cast<double>(nextCount);
                memory->featureM2[region][value] += delta
                    * (observedFeatures[region][value]
                        - memory->featureMean[region][value]);
            }
        }
        memory->shapeMean = constrainShape(memory->shapeMean);
        memory->observations = nextCount;
        if (sourceEngram != 0U
            && std::find(memory->sourceEngrams.begin(), memory->sourceEngrams.end(), sourceEngram)
                == memory->sourceEngrams.end()) {
            memory->sourceEngrams.push_back(sourceEngram);
        }
        if (assembly != 0U
            && std::find(memory->assemblies.begin(), memory->assemblies.end(), assembly)
                == memory->assemblies.end()) {
            memory->assemblies.push_back(assembly);
        }
    }

    SceneDescription normalizedScene(
        const SceneDescription& raw,
        bool requireComplete) const {
        if (raw.objects.empty() || raw.objects.size() > config.maximumSceneObjects) {
            throw std::invalid_argument("A scene requires between one and maximumSceneObjects objects");
        }
        SceneDescription result = raw;
        result.name = normalizedSymbol(raw.name);
        for (double channel : result.backgroundColor) {
            if (!std::isfinite(channel) || channel < 0.0 || channel > 1.0) {
                throw std::invalid_argument("Scene background channels must be in [0,1]");
            }
        }
        std::vector<std::string> keys;
        keys.reserve(result.objects.size());
        for (auto& object : result.objects) {
            object.instance = normalizedSymbol(object.instance);
            object.category = normalizedSymbol(object.category);
            object.pose = normalizedSymbol(object.pose);
            if (object.pose.empty()) object.pose = "CANONICAL";
            if (object.instance.empty() || object.category.empty()) {
                throw std::invalid_argument("Every scene object needs an instance and category");
            }
            if (std::find(keys.begin(), keys.end(), object.instance) != keys.end()) {
                throw std::invalid_argument("Scene instance names must be unique");
            }
            keys.push_back(object.instance);
            const auto finiteOptional = [](const std::optional<double>& value) {
                return !value.has_value() || std::isfinite(*value);
            };
            if (!finiteOptional(object.x) || !finiteOptional(object.y)
                || !finiteOptional(object.scale) || !finiteOptional(object.rotation)
                || !finiteOptional(object.depth)) {
                throw std::invalid_argument("Scene object state must be finite");
            }
            if (requireComplete
                && (!object.x.has_value() || !object.y.has_value()
                    || !object.scale.has_value() || !object.depth.has_value())) {
                throw std::invalid_argument(
                    "Observed scene objects require x, y, scale and depth");
            }
            if ((object.x.has_value() && (*object.x < 0.0 || *object.x > 1.0))
                || (object.y.has_value() && (*object.y < 0.0 || *object.y > 1.0))
                || (object.depth.has_value() && (*object.depth < 0.0 || *object.depth > 1.0))
                || (object.scale.has_value() && (*object.scale <= 0.01 || *object.scale > 1.0))) {
                throw std::invalid_argument("Scene position, scale and depth are outside [0,1]");
            }
            if (categoryByName(object.category) == nullptr) {
                throw std::invalid_argument("Scene references an unknown category: " + object.category);
            }
        }
        for (auto& relation : result.relations) {
            relation.subject = normalizedSymbol(relation.subject);
            relation.object = normalizedSymbol(relation.object);
            if (std::find(keys.begin(), keys.end(), relation.subject) == keys.end()
                || std::find(keys.begin(), keys.end(), relation.object) == keys.end()
                || relation.subject == relation.object) {
                throw std::invalid_argument("Scene relation references invalid instances");
            }
            if (static_cast<unsigned int>(relation.relation)
                    > static_cast<unsigned int>(SceneRelationKind::ConnectedTo)
                || !std::isfinite(relation.confidence)
                || relation.confidence < 0.0 || relation.confidence > 1.0) {
                throw std::invalid_argument("Scene relation is invalid");
            }
        }
        return result;
    }

    const RelationEngram* relationMemory(
        SceneRelationKind relation,
        const std::string& subjectCategory,
        const std::string& objectCategory) const {
        const auto exact = std::find_if(relations.begin(), relations.end(),
            [relation, &subjectCategory, &objectCategory](const RelationEngram& value) {
                return value.relation == relation
                    && value.subjectCategory == subjectCategory
                    && value.objectCategory == objectCategory;
            });
        return exact == relations.end() ? nullptr : &*exact;
    }

    static std::array<double, 4> canonicalRelation(SceneRelationKind relation) {
        switch (relation) {
            case SceneRelationKind::LeftOf: return {-0.30, 0.0, 0.0, 1.0};
            case SceneRelationKind::RightOf: return {0.30, 0.0, 0.0, 1.0};
            case SceneRelationKind::Above: return {0.0, -0.30, 0.0, 1.0};
            case SceneRelationKind::Below: return {0.0, 0.30, 0.0, 1.0};
            case SceneRelationKind::InFrontOf: return {0.0, 0.04, -0.30, 1.0};
            case SceneRelationKind::Behind: return {0.0, -0.04, 0.30, 1.0};
            case SceneRelationKind::Inside: return {0.0, 0.0, -0.02, 0.45};
            case SceneRelationKind::Contains: return {0.0, 0.0, 0.02, 2.0};
            case SceneRelationKind::Touching: return {-0.24, 0.0, 0.0, 1.0};
            case SceneRelationKind::Overlapping: return {-0.08, 0.0, -0.05, 1.0};
            case SceneRelationKind::Near: return {-0.22, 0.0, 0.0, 1.0};
            case SceneRelationKind::Far: return {-0.55, 0.0, 0.0, 1.0};
            case SceneRelationKind::LookingAt: return {-0.30, 0.0, 0.0, 1.0};
            case SceneRelationKind::ConnectedTo: return {-0.20, 0.0, 0.0, 1.0};
        }
        return {};
    }

    void updateRelation(
        SceneRelationKind relation,
        const SceneInstance& subject,
        const SceneInstance& object,
        AssemblyId assembly) {
        auto found = std::find_if(relations.begin(), relations.end(),
            [relation, &subject, &object](const RelationEngram& value) {
                return value.relation == relation
                    && value.subjectCategory == subject.category
                    && value.objectCategory == object.category;
            });
        if (found == relations.end()) {
            if (relations.size() >= config.maximumRelationEngrams) {
                const auto victim = std::min_element(relations.begin(), relations.end(),
                    [](const RelationEngram& left, const RelationEngram& right) {
                        return left.observations < right.observations;
                    });
                relations.erase(victim);
            }
            relations.push_back(RelationEngram{
                .relation = relation,
                .subjectCategory = subject.category,
                .objectCategory = object.category,
                .observations = 0U,
                .mean = {},
                .m2 = {},
                .assemblies = {},
            });
            found = std::prev(relations.end());
        }
        const std::array<double, 4> sample{
            subject.x - object.x,
            subject.y - object.y,
            subject.depth - object.depth,
            subject.scale / std::max(0.01, object.scale),
        };
        const auto nextCount = found->observations + 1U;
        for (std::size_t index = 0; index < sample.size(); ++index) {
            const double delta = sample[index] - found->mean[index];
            found->mean[index] += delta / static_cast<double>(nextCount);
            found->m2[index] += delta * (sample[index] - found->mean[index]);
        }
        found->observations = nextCount;
        if (assembly != 0U
            && std::find(found->assemblies.begin(), found->assemblies.end(), assembly)
                == found->assemblies.end()) {
            found->assemblies.push_back(assembly);
        }
    }

    std::vector<SceneInstance> resolveScene(const SceneDescription& scene) const {
        std::vector<SceneInstance> result;
        result.reserve(scene.objects.size());
        const auto columns = static_cast<std::size_t>(std::ceil(
            std::sqrt(static_cast<double>(scene.objects.size()))));
        for (std::size_t index = 0; index < scene.objects.size(); ++index) {
            const auto& cue = scene.objects[index];
            const std::size_t row = index / columns;
            const std::size_t column = index % columns;
            const auto* category = categoryByName(cue.category);
            const auto* pose = poseByName(cue.category, cue.pose);
            const auto* source = category == nullptr ? nullptr : representativeEngram(*category);
            SceneInstance instance{
                .instanceId = fnv1a("SCENE:" + scene.name + ':' + cue.instance),
                .instance = cue.instance,
                .category = cue.category,
                .pose = cue.pose,
                .x = cue.x.value_or((static_cast<double>(column) + 1.0)
                    / (static_cast<double>(columns) + 1.0)),
                .y = cue.y.value_or((static_cast<double>(row) + 1.0)
                    / (static_cast<double>((scene.objects.size() + columns - 1U) / columns) + 1.0)),
                .scale = cue.scale.value_or(0.28),
                .rotation = cue.rotation.value_or(0.0),
                .depth = cue.depth.value_or(0.5 + 0.02 * static_cast<double>(index)),
                .sourceEngramId = source == nullptr ? 0U : source->id,
                .confidence = category == nullptr ? 0.0 : pose != nullptr ? 1.0 : 0.82,
            };
            const double half = 0.5 * instance.scale;
            instance.occupiedRegion = {
                clamp01(instance.x - half), clamp01(instance.y - half),
                clamp01(instance.x + half), clamp01(instance.y + half),
            };
            result.push_back(std::move(instance));
        }
        const auto findIndex = [&result](const std::string& key) {
            return static_cast<std::size_t>(std::distance(result.begin(),
                std::find_if(result.begin(), result.end(), [&key](const SceneInstance& value) {
                    return value.instance == key;
                })));
        };
        std::vector<bool> lockX(scene.objects.size());
        std::vector<bool> lockY(scene.objects.size());
        std::vector<bool> lockScale(scene.objects.size());
        std::vector<bool> lockDepth(scene.objects.size());
        for (std::size_t index = 0; index < scene.objects.size(); ++index) {
            lockX[index] = scene.objects[index].x.has_value();
            lockY[index] = scene.objects[index].y.has_value();
            lockScale[index] = scene.objects[index].scale.has_value();
            lockDepth[index] = scene.objects[index].depth.has_value();
        }
        const auto adjust = [](double& subject, double& object, double desired,
                               bool lockSubject, bool lockObject) {
            const double error = desired - (subject - object);
            if (!lockSubject && !lockObject) {
                subject += 0.5 * error;
                object -= 0.5 * error;
            } else if (!lockSubject) {
                subject += error;
            } else if (!lockObject) {
                object -= error;
            }
        };
        for (std::size_t iteration = 0; iteration < 16U; ++iteration) {
            for (const auto& relation : scene.relations) {
                const auto subjectIndex = findIndex(relation.subject);
                const auto objectIndex = findIndex(relation.object);
                auto& subject = result[subjectIndex];
                auto& object = result[objectIndex];
                const auto* learned = relationMemory(
                    relation.relation, subject.category, object.category);
                const auto desired = learned == nullptr
                    ? canonicalRelation(relation.relation) : learned->mean;
                switch (relation.relation) {
                    case SceneRelationKind::LeftOf:
                    case SceneRelationKind::RightOf:
                    case SceneRelationKind::Touching:
                    case SceneRelationKind::Overlapping:
                    case SceneRelationKind::Near:
                    case SceneRelationKind::Far:
                    case SceneRelationKind::ConnectedTo:
                        adjust(subject.x, object.x, desired[0], lockX[subjectIndex], lockX[objectIndex]);
                        adjust(subject.y, object.y, desired[1], lockY[subjectIndex], lockY[objectIndex]);
                        break;
                    case SceneRelationKind::Above:
                    case SceneRelationKind::Below:
                        adjust(subject.x, object.x, desired[0], lockX[subjectIndex], lockX[objectIndex]);
                        adjust(subject.y, object.y, desired[1], lockY[subjectIndex], lockY[objectIndex]);
                        break;
                    case SceneRelationKind::InFrontOf:
                    case SceneRelationKind::Behind:
                        adjust(subject.depth, object.depth, desired[2],
                            lockDepth[subjectIndex], lockDepth[objectIndex]);
                        break;
                    case SceneRelationKind::Inside:
                    case SceneRelationKind::Contains:
                        adjust(subject.x, object.x, desired[0], lockX[subjectIndex], lockX[objectIndex]);
                        adjust(subject.y, object.y, desired[1], lockY[subjectIndex], lockY[objectIndex]);
                        if (!lockScale[subjectIndex]) {
                            subject.scale = object.scale * desired[3];
                        } else if (!lockScale[objectIndex]) {
                            object.scale = subject.scale / std::max(0.05, desired[3]);
                        }
                        break;
                    case SceneRelationKind::LookingAt:
                        if (!scene.objects[subjectIndex].rotation.has_value()) {
                            subject.rotation = std::atan2(
                                object.y - subject.y, object.x - subject.x);
                        }
                        break;
                }
                subject.x = std::clamp(subject.x, 0.04, 0.96);
                subject.y = std::clamp(subject.y, 0.04, 0.96);
                subject.depth = clamp01(subject.depth);
                subject.scale = std::clamp(subject.scale, 0.04, 1.0);
                object.x = std::clamp(object.x, 0.04, 0.96);
                object.y = std::clamp(object.y, 0.04, 0.96);
                object.depth = clamp01(object.depth);
                object.scale = std::clamp(object.scale, 0.04, 1.0);
            }
        }
        return result;
    }

    std::optional<VisualCanvas> objectSource(
        const SceneInstance& instance,
        std::uint64_t seed) const {
        if (const auto* pose = poseByName(instance.category, instance.pose)) {
            return synthesizeAbstractMemory(
                pose->shapeMean,
                pose->featureMean,
                &pose->featureM2,
                pose->observations,
                seed ^ fnv1a(instance.pose));
        }
        const auto* category = categoryByName(instance.category);
        if (category == nullptr || category->observations == 0U) return std::nullopt;
        return categoryVariant(*category, seed);
    }

    std::array<std::size_t, 4> foregroundBounds(const VisualCanvas& source) const {
        const auto background = imageBackground(source);
        std::size_t left = source.width();
        std::size_t top = source.height();
        std::size_t right = 0;
        std::size_t bottom = 0;
        bool found = false;
        for (std::size_t y = 0; y < source.height(); ++y) {
            for (std::size_t x = 0; x < source.width(); ++x) {
                const auto color = source.colorPixel(x, y);
                const double difference = std::max({
                    std::abs(color[0] - background[0]),
                    std::abs(color[1] - background[1]),
                    std::abs(color[2] - background[2]),
                });
                if (difference <= 0.035) continue;
                found = true;
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
        if (!found) return {0U, 0U, source.width() - 1U, source.height() - 1U};
        return {left, top, right, bottom};
    }

    void compositeObject(
        VisualCanvas& destination,
        const VisualCanvas& source,
        SceneInstance& instance) const {
        const auto bounds = foregroundBounds(source);
        const double sourceWidth = static_cast<double>(bounds[2] - bounds[0] + 1U);
        const double sourceHeight = static_cast<double>(bounds[3] - bounds[1] + 1U);
        const double maximumSourceSide = std::max(sourceWidth, sourceHeight);
        const double destinationMaximum = instance.scale
            * static_cast<double>(std::min(destination.width(), destination.height()));
        const double destinationWidth = destinationMaximum * sourceWidth / maximumSourceSide;
        const double destinationHeight = destinationMaximum * sourceHeight / maximumSourceSide;
        const double centerX = instance.x * static_cast<double>(destination.width() - 1U);
        const double centerY = instance.y * static_cast<double>(destination.height() - 1U);
        const double cosine = std::cos(instance.rotation);
        const double sine = std::sin(instance.rotation);
        const double radius = 0.5 * std::hypot(destinationWidth, destinationHeight) + 1.0;
        const auto beginX = static_cast<std::size_t>(std::max(0.0, std::floor(centerX - radius)));
        const auto beginY = static_cast<std::size_t>(std::max(0.0, std::floor(centerY - radius)));
        const auto endX = static_cast<std::size_t>(std::min(
            static_cast<double>(destination.width() - 1U), std::ceil(centerX + radius)));
        const auto endY = static_cast<std::size_t>(std::min(
            static_cast<double>(destination.height() - 1U), std::ceil(centerY + radius)));
        const auto background = imageBackground(source);
        for (std::size_t y = beginY; y <= endY; ++y) {
            for (std::size_t x = beginX; x <= endX; ++x) {
                const double dx = static_cast<double>(x) - centerX;
                const double dy = static_cast<double>(y) - centerY;
                const double localX = cosine * dx + sine * dy;
                const double localY = -sine * dx + cosine * dy;
                const double unitX = localX / std::max(1.0, destinationWidth) + 0.5;
                const double unitY = localY / std::max(1.0, destinationHeight) + 0.5;
                if (unitX < 0.0 || unitX > 1.0 || unitY < 0.0 || unitY > 1.0) continue;
                const double sourceX = (static_cast<double>(bounds[0])
                    + unitX * std::max(0.0, sourceWidth - 1.0))
                    / static_cast<double>(source.width() - 1U);
                const double sourceY = (static_cast<double>(bounds[1])
                    + unitY * std::max(0.0, sourceHeight - 1.0))
                    / static_cast<double>(source.height() - 1U);
                const auto color = bilinearSample(source, sourceX, sourceY);
                const double difference = std::max({
                    std::abs(color[0] - background[0]),
                    std::abs(color[1] - background[1]),
                    std::abs(color[2] - background[2]),
                });
                const double alpha = clamp01((difference - 0.015) / 0.065);
                if (alpha <= 0.0) continue;
                const auto previous = destination.colorPixel(x, y);
                destination.setColorPixel(
                    x, y,
                    previous[0] + alpha * (color[0] - previous[0]),
                    previous[1] + alpha * (color[1] - previous[1]),
                    previous[2] + alpha * (color[2] - previous[2]));
            }
        }
        instance.occupiedRegion = {
            static_cast<double>(beginX) / static_cast<double>(destination.width() - 1U),
            static_cast<double>(beginY) / static_cast<double>(destination.height() - 1U),
            static_cast<double>(endX) / static_cast<double>(destination.width() - 1U),
            static_cast<double>(endY) / static_cast<double>(destination.height() - 1U),
        };
    }

    std::vector<PaintAction> deltaTrace(
        const VisualCanvas& before,
        const VisualCanvas& after,
        std::size_t patchSide,
        std::size_t startX,
        std::size_t startY) const {
        std::vector<PaintAction> result;
        std::size_t cursorX = startX;
        std::size_t cursorY = startY;
        const std::size_t rows = (after.height() + patchSide - 1U) / patchSide;
        const std::size_t columns = (after.width() + patchSide - 1U) / patchSide;
        for (std::size_t tileY = 0; tileY < rows; ++tileY) {
            const bool reverse = tileY % 2U != 0U;
            for (std::size_t offset = 0; offset < columns; ++offset) {
                const std::size_t tileX = reverse ? columns - 1U - offset : offset;
                const std::size_t originX = tileX * patchSide;
                const std::size_t originY = tileY * patchSide;
                const std::size_t width = std::min(patchSide, after.width() - originX);
                const std::size_t height = std::min(patchSide, after.height() - originY);
                bool changed = false;
                for (std::size_t y = 0; y < height && !changed; ++y) {
                    for (std::size_t x = 0; x < width && !changed; ++x) {
                        const auto a = after.colorPixel(originX + x, originY + y);
                        const auto b = before.colorPixel(originX + x, originY + y);
                        changed = std::abs(a[0] - b[0]) > 1.0 / 510.0
                            || std::abs(a[1] - b[1]) > 1.0 / 510.0
                            || std::abs(a[2] - b[2]) > 1.0 / 510.0;
                    }
                }
                if (!changed) continue;
                appendMoveActions(result, cursorX, cursorY, originX, originY);
                PaintAction paint{
                    .kind = PaintActionKind::Paint,
                    .repetitions = 1,
                    .intensity = 1.0,
                    .patchWidth = static_cast<std::uint8_t>(width),
                    .patchHeight = static_cast<std::uint8_t>(height),
                };
                for (std::size_t y = 0; y < height; ++y) {
                    for (std::size_t x = 0; x < width; ++x) {
                        const auto color = after.colorPixel(originX + x, originY + y);
                        const std::size_t index = (y * width + x) * 3U;
                        paint.patchRgb[index] = static_cast<std::uint8_t>(
                            std::llround(clamp01(color[0]) * 255.0));
                        paint.patchRgb[index + 1U] = static_cast<std::uint8_t>(
                            std::llround(clamp01(color[1]) * 255.0));
                        paint.patchRgb[index + 2U] = static_cast<std::uint8_t>(
                            std::llround(clamp01(color[2]) * 255.0));
                    }
                }
                result.push_back(std::move(paint));
            }
        }
        return result;
    }

    VisualCanvas rasterizedScene(
        std::vector<SceneInstance> instances,
        const SceneDescription& scene,
        std::uint64_t seed) const {
        VisualCanvas result(config.width, config.height);
        result.clearColor(
            scene.backgroundColor[0], scene.backgroundColor[1], scene.backgroundColor[2]);
        std::vector<std::size_t> order(instances.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::stable_sort(order.begin(), order.end(), [&instances](std::size_t left, std::size_t right) {
            return instances[left].depth > instances[right].depth;
        });
        for (const auto index : order) {
            auto source = objectSource(instances[index], seed + index * 131U);
            if (source.has_value()) compositeObject(result, *source, instances[index]);
        }
        return result;
    }

    bool paintScene(
        std::vector<SceneInstance>& instances,
        const SceneDescription& scene,
        ImaginationStage stage,
        std::uint64_t seed,
        ObserveResult& finalObservation) {
        canvas.clearColor(
            scene.backgroundColor[0], scene.backgroundColor[1], scene.backgroundColor[2]);
        canvas.setCursor(config.width / 2U, config.height / 2U);
        lastCanvasMean = canvas.meanIntensity();
        actionsExecuted = 0;
        std::vector<std::size_t> order(instances.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::stable_sort(order.begin(), order.end(), [&instances](std::size_t left, std::size_t right) {
            return instances[left].depth > instances[right].depth;
        });
        const bool learningWasEnabled = mind.learningEnabled();
        mind.setLearningEnabled(false);
        bool paintedAny = false;
        try {
            for (const auto objectIndex : order) {
                auto source = objectSource(instances[objectIndex], seed + objectIndex * 131U);
                if (!source.has_value()) continue;
                VisualCanvas desired = canvas;
                compositeObject(desired, *source, instances[objectIndex]);
                const auto program = deltaTrace(
                    canvas, desired, config.paintPatchSide, canvas.cursorX(), canvas.cursorY());
                const std::string cue = "SCENE:" + scene.name + ':'
                    + instances[objectIndex].instance + ':' + instances[objectIndex].category
                    + ':' + instances[objectIndex].pose;
                for (std::size_t actionIndex = 0; actionIndex < program.size(); ++actionIndex) {
                    const bool feedback = actionIndex == 0U
                        || actionIndex + 1U == program.size()
                        || actionIndex % config.neuralFeedbackStride == 0U;
                    finalObservation = act(
                        program[actionIndex], nullptr, cue, stage,
                        instances[objectIndex].instanceId, false, true, feedback);
                    trace.back().sceneInstance = instances[objectIndex].instance;
                }
                finalObservation = expose(
                    nullptr, cue, stage, instances[objectIndex].instanceId, true);
                trace.back().sceneInstance = instances[objectIndex].instance;
                instances[objectIndex].assemblyId = representedAssembly(finalObservation);
                paintedAny = paintedAny || !program.empty();
            }
            mind.setLearningEnabled(learningWasEnabled);
        } catch (...) {
            mind.setLearningEnabled(learningWasEnabled);
            throw;
        }
        sceneMemoryInstances = instances;
        sceneMemoryRelations = scene.relations;
        return paintedAny;
    }

    double relationAccuracy(
        const std::vector<SceneInstance>& instances,
        const std::vector<SceneRelationCue>& cues) const {
        if (cues.empty()) return 1.0;
        const auto find = [&instances](const std::string& key) -> const SceneInstance* {
            const auto item = std::find_if(instances.begin(), instances.end(),
                [&key](const SceneInstance& value) { return value.instance == key; });
            return item == instances.end() ? nullptr : &*item;
        };
        double score = 0.0;
        for (const auto& cue : cues) {
            const auto* subject = find(cue.subject);
            const auto* object = find(cue.object);
            if (subject == nullptr || object == nullptr) continue;
            const double dx = subject->x - object->x;
            const double dy = subject->y - object->y;
            const double dd = subject->depth - object->depth;
            const double distance = std::hypot(dx, dy);
            bool satisfied = false;
            switch (cue.relation) {
                case SceneRelationKind::LeftOf: satisfied = dx < -0.04; break;
                case SceneRelationKind::RightOf: satisfied = dx > 0.04; break;
                case SceneRelationKind::Above: satisfied = dy < -0.04; break;
                case SceneRelationKind::Below: satisfied = dy > 0.04; break;
                case SceneRelationKind::InFrontOf: satisfied = dd < -0.04; break;
                case SceneRelationKind::Behind: satisfied = dd > 0.04; break;
                case SceneRelationKind::Inside:
                    satisfied = distance < 0.12 && subject->scale < object->scale;
                    break;
                case SceneRelationKind::Contains:
                    satisfied = distance < 0.12 && subject->scale > object->scale;
                    break;
                case SceneRelationKind::Touching:
                    satisfied = std::abs(distance - 0.5 * (subject->scale + object->scale)) < 0.15;
                    break;
                case SceneRelationKind::Overlapping:
                    satisfied = distance < 0.5 * (subject->scale + object->scale);
                    break;
                case SceneRelationKind::Near: satisfied = distance < 0.40; break;
                case SceneRelationKind::Far: satisfied = distance > 0.38; break;
                case SceneRelationKind::LookingAt: {
                    const double desired = std::atan2(-dy, -dx);
                    satisfied = std::abs(std::remainder(subject->rotation - desired, 2.0 * kPi)) < 0.35;
                    break;
                }
                case SceneRelationKind::ConnectedTo: satisfied = distance < 0.36; break;
            }
            score += (satisfied ? 1.0 : 0.0) * cue.confidence;
        }
        double weight = 0.0;
        for (const auto& cue : cues) weight += cue.confidence;
        return weight <= 1e-12 ? 1.0 : clamp01(score / weight);
    }

    SceneImaginationMetrics sceneMetricsFor(
        const std::vector<SceneInstance>& instances,
        const std::vector<SceneRelationCue>& relationsForScene) const {
        SceneImaginationMetrics metrics;
        metrics.objectCount = instances.size();
        metrics.relationCount = relationsForScene.size();
        metrics.spatialRelationAccuracy = relationAccuracy(instances, relationsForScene);
        std::size_t categoriesFound = 0;
        std::size_t posesFound = 0;
        for (const auto& instance : instances) {
            if (categoryByName(instance.category) != nullptr) ++categoriesFound;
            if (instance.pose == "CANONICAL"
                || poseByName(instance.category, instance.pose) != nullptr) ++posesFound;
        }
        metrics.categoryPreservation = instances.empty() ? 0.0
            : static_cast<double>(categoriesFound) / static_cast<double>(instances.size());
        metrics.poseCoherence = instances.empty() ? 0.0
            : static_cast<double>(posesFound) / static_cast<double>(instances.size());
        metrics.occlusionConsistency = 1.0;
        return metrics;
    }

    static std::string actionLabel(const SceneAction& action) {
        const auto custom = normalizedSymbol(action.label);
        if (action.kind == SceneActionKind::Custom) {
            if (custom.empty()) {
                throw std::invalid_argument("A custom scene action requires a label");
            }
            return custom;
        }
        return custom.empty() ? normalizedSymbol(sceneActionName(action.kind)) : custom;
    }

    static std::array<double, 2> actionDirection(
        const SceneAction& action,
        const SceneInstance& actor,
        const SceneInstance* target) {
        double x = action.direction[0];
        double y = action.direction[1];
        if (std::hypot(x, y) <= 1e-9 && target != nullptr) {
            x = target->x - actor.x;
            y = target->y - actor.y;
        }
        const double length = std::hypot(x, y);
        if (length <= 1e-9) return {1.0, 0.0};
        return {x / length, y / length};
    }

    const ActionEngram* actionMemory(
        const SceneAction& action,
        const std::string& actorCategory,
        const std::string& targetCategory) const {
        const auto label = actionLabel(action);
        const auto found = std::find_if(actions.begin(), actions.end(),
            [&action, &label, &actorCategory, &targetCategory](const ActionEngram& value) {
                return value.kind == action.kind && value.label == label
                    && value.actorCategory == actorCategory
                    && value.targetCategory == targetCategory;
            });
        return found == actions.end() ? nullptr : &*found;
    }

    ActionEngram& ensureActionMemory(
        const SceneAction& action,
        const std::string& actorCategory,
        const std::string& targetCategory) {
        if (const auto* existing = actionMemory(action, actorCategory, targetCategory)) {
            return *const_cast<ActionEngram*>(existing);
        }
        if (actions.size() >= config.maximumActionEngrams) {
            const auto victim = std::min_element(actions.begin(), actions.end(),
                [](const ActionEngram& left, const ActionEngram& right) {
                    return left.observations < right.observations;
                });
            actions.erase(victim);
        }
        actions.push_back(ActionEngram{
            .kind = action.kind,
            .label = actionLabel(action),
            .actorCategory = actorCategory,
            .targetCategory = targetCategory,
            .observations = 0U,
            .actorMean = {},
            .actorM2 = {},
            .targetMean = {},
            .targetM2 = {},
            .actorPoseAfter = {},
            .targetPoseAfter = {},
            .assemblies = {},
        });
        return actions.back();
    }

    void updateAction(
        const SceneAction& action,
        const std::vector<SceneInstance>& before,
        const std::vector<SceneInstance>& after,
        AssemblyId assembly) {
        const auto actorKey = normalizedSymbol(action.actor);
        const auto targetKey = normalizedSymbol(action.target);
        const auto find = [](const std::vector<SceneInstance>& values,
                             const std::string& key) -> const SceneInstance* {
            const auto item = std::find_if(values.begin(), values.end(),
                [&key](const SceneInstance& value) { return value.instance == key; });
            return item == values.end() ? nullptr : &*item;
        };
        const auto* actorBefore = find(before, actorKey);
        const auto* actorAfter = find(after, actorKey);
        const auto* targetBefore = targetKey.empty() ? nullptr : find(before, targetKey);
        const auto* targetAfter = targetKey.empty() ? nullptr : find(after, targetKey);
        if (actorBefore == nullptr || actorAfter == nullptr
            || ((!targetKey.empty()) && (targetBefore == nullptr || targetAfter == nullptr))) {
            throw std::invalid_argument("Scene action actor/target is missing from transition states");
        }
        if (!std::isfinite(action.direction[0]) || !std::isfinite(action.direction[1])
            || !std::isfinite(action.magnitude) || action.magnitude <= 0.0
            || !std::isfinite(action.duration) || action.duration <= 0.0
            || static_cast<unsigned int>(action.kind)
                > static_cast<unsigned int>(SceneActionKind::Custom)) {
            throw std::invalid_argument("Scene action parameters are invalid");
        }
        const auto direction = actionDirection(action, *actorBefore, targetBefore);
        const double inverseMagnitude = 1.0 / action.magnitude;
        const auto effect = [&](const SceneInstance& first, const SceneInstance& second) {
            const double dx = (second.x - first.x) * inverseMagnitude;
            const double dy = (second.y - first.y) * inverseMagnitude;
            return std::array<double, 5>{
                dx * direction[0] + dy * direction[1],
                -dx * direction[1] + dy * direction[0],
                (second.depth - first.depth) * inverseMagnitude,
                (second.scale - first.scale) * inverseMagnitude,
                (second.rotation - first.rotation) * inverseMagnitude,
            };
        };
        const auto actorSample = effect(*actorBefore, *actorAfter);
        const auto targetSample = targetBefore == nullptr
            ? std::array<double, 5>{} : effect(*targetBefore, *targetAfter);
        auto& memory = ensureActionMemory(
            action, actorBefore->category,
            targetBefore == nullptr ? std::string{} : targetBefore->category);
        const auto nextCount = memory.observations + 1U;
        const auto update = [nextCount](std::array<double, 5>& mean,
                                        std::array<double, 5>& m2,
                                        const std::array<double, 5>& sample) {
            for (std::size_t index = 0; index < mean.size(); ++index) {
                const double delta = sample[index] - mean[index];
                mean[index] += delta / static_cast<double>(nextCount);
                m2[index] += delta * (sample[index] - mean[index]);
            }
        };
        update(memory.actorMean, memory.actorM2, actorSample);
        update(memory.targetMean, memory.targetM2, targetSample);
        memory.observations = nextCount;
        memory.actorPoseAfter = actorAfter->pose;
        memory.targetPoseAfter = targetAfter == nullptr ? std::string{} : targetAfter->pose;
        if (assembly != 0U
            && std::find(memory.assemblies.begin(), memory.assemblies.end(), assembly)
                == memory.assemblies.end()) {
            memory.assemblies.push_back(assembly);
        }
    }

    bool applyActionPrediction(
        std::vector<SceneInstance>& state,
        const SceneAction& action,
        double& confidence,
        double& predictionError) const {
        const auto actorKey = normalizedSymbol(action.actor);
        const auto targetKey = normalizedSymbol(action.target);
        auto find = [&state](const std::string& key) -> SceneInstance* {
            const auto item = std::find_if(state.begin(), state.end(),
                [&key](const SceneInstance& value) { return value.instance == key; });
            return item == state.end() ? nullptr : &*item;
        };
        auto* actor = find(actorKey);
        auto* target = targetKey.empty() ? nullptr : find(targetKey);
        if (actor == nullptr || (!targetKey.empty() && target == nullptr)
            || !std::isfinite(action.magnitude) || action.magnitude <= 0.0) {
            throw std::invalid_argument("Prospective action references an invalid actor or target");
        }
        const auto* memory = actionMemory(
            action, actor->category, target == nullptr ? std::string{} : target->category);
        if (memory == nullptr || memory->observations == 0U) return false;
        const auto direction = actionDirection(action, *actor, target);
        const auto apply = [&direction, &action](SceneInstance& instance,
                                                 const std::array<double, 5>& mean) {
            const double parallel = mean[0] * action.magnitude;
            const double perpendicular = mean[1] * action.magnitude;
            instance.x += direction[0] * parallel - direction[1] * perpendicular;
            instance.y += direction[1] * parallel + direction[0] * perpendicular;
            instance.depth += mean[2] * action.magnitude;
            instance.scale += mean[3] * action.magnitude;
            instance.rotation += mean[4] * action.magnitude;
            instance.x = std::clamp(instance.x, 0.02, 0.98);
            instance.y = std::clamp(instance.y, 0.02, 0.98);
            instance.depth = clamp01(instance.depth);
            instance.scale = std::clamp(instance.scale, 0.03, 1.0);
            const double half = 0.5 * instance.scale;
            instance.occupiedRegion = {
                clamp01(instance.x - half), clamp01(instance.y - half),
                clamp01(instance.x + half), clamp01(instance.y + half),
            };
        };
        apply(*actor, memory->actorMean);
        if (!memory->actorPoseAfter.empty()) actor->pose = memory->actorPoseAfter;
        if (target != nullptr) {
            apply(*target, memory->targetMean);
            if (!memory->targetPoseAfter.empty()) target->pose = memory->targetPoseAfter;
        }
        double variance = 0.0;
        if (memory->observations > 1U) {
            for (const auto* values : {&memory->actorM2, &memory->targetM2}) {
                for (const double value : *values) {
                    variance += value / static_cast<double>(memory->observations - 1U);
                }
            }
            variance /= 10.0;
        }
        predictionError = std::sqrt(std::max(0.0, variance));
        confidence = clamp01(
            static_cast<double>(memory->observations)
                / static_cast<double>(memory->observations + 1U)
            * std::exp(-4.0 * predictionError));
        return true;
    }

    ImaginationReport finishReport(
        ImaginationStage stage,
        bool success,
        double similarity,
        double novelty,
        AssemblyId activeAssembly,
        std::vector<std::uint64_t> recalled,
        bool referenceVisible,
        std::vector<std::string> fusedCategories = {},
        std::vector<std::string> featureNames = {},
        std::vector<std::size_t> featureSources = {}) {
        ImaginationReport report;
        report.success = success;
        report.stage = stage;
        report.canvas = canvas;
        report.similarity = clamp01(similarity);
        report.novelty = clamp01(novelty);
        report.actionsExecuted = actionsExecuted;
        report.activeAssemblyId = activeAssembly;
        report.recalledEngramIds = std::move(recalled);
        report.categoryNames = std::move(fusedCategories);
        report.featureNames = std::move(featureNames);
        report.featureSourceIndices = std::move(featureSources);
        report.referenceVisibleDuringDrawing = referenceVisible;
        report.physiology = mind.physiology();
        report.prospection = mind.prospection();
        report.motor = mind.motor();
        // Closed visual loop: every internally generated canvas is sent back
        // through ocular preprocessing, retina, V1, object segmentation and
        // ventral/category memory. Generated images are deliberately NOT used
        // to update object memory, preventing self-reinforcing hallucination.
        report.perceivedScene = perceiveSceneInternal(
            report.canvas, report.categoryNames, false,
            std::min<std::size_t>(16U, visualPathway.config().maximumObjects), true);
        last = report;
        ++totalAttempts;
        if (success) ++successfulAttempts;
        else ++failedAttempts;
        return report;
    }

    ImaginationReport replay(
        const VisualEngram& engram,
        ImaginationStage stage,
        std::string_view symbol,
        const VisualCanvas* scoringReference,
        AssemblyId cueAssembly) {
        // V14 recall prefers the self-consolidated motor engram. The temporary
        // RGB patch program is generated only for actuator execution; no patch
        // payload is retained in the visual engram or snapshot.
        const auto imagined = engramPrototype(engram);
        const auto program = VisualImagination::makeTeacherTrace(
            imagined, 0.0, config.paintPatchSide);

        canvas.clear();
        lastCanvasMean = 0.0;
        actionsExecuted = 0;
        const bool learningWasEnabled = mind.learningEnabled();
        mind.setLearningEnabled(false);
        ObserveResult observed;
        ObserveResult selfObserved;
        try {
            for (std::size_t index = 0; index < program.size(); ++index) {
                const bool feedback = index == 0U
                    || index + 1U == program.size()
                    || index % config.neuralFeedbackStride == 0U;
                observed = act(
                    program[index], nullptr, symbol, stage,
                    engram.id, false,
                    // Exact memory recall must not be attenuated by current ATP
                    // or sleep pressure. Physiology remains observable but does
                    // not corrupt a consolidated motor memory.
                    !engram.selfMotorMemoryValid,
                    feedback);
            }

            // Closed-loop self-recognition: after the motor engram has painted
            // the canvas, TATARUS sees its own result again while learning is
            // disabled. This verifies the recalled self-result without creating
            // a new memory from its own imagination.
            selfObserved = expose(
                &canvas,
                symbol,
                stage,
                mix64(engram.id ^ 0x524543414c4c5631ULL),
                true);
            mind.setLearningEnabled(learningWasEnabled);
        } catch (...) {
            mind.setLearningEnabled(learningWasEnabled);
            throw;
        }
        const double similarity = scoringReference == nullptr
            ? 0.0 : canvas.similarity(*scoringReference);
        const double selfMemorySimilarity = canvas.similarity(imagined);
        const AssemblyId selfAssembly = representedAssembly(selfObserved);
        const AssemblyId observedAssembly = representedAssembly(observed);
        const AssemblyId assembly = selfAssembly != 0U
            ? selfAssembly
            : (observedAssembly != 0U ? observedAssembly
                                      : (engram.selfAssembly != 0U
                                            ? engram.selfAssembly : cueAssembly));
        const bool selfRecallPassed = !program.empty()
            && (!engram.selfMotorMemoryValid || selfMemorySimilarity > 0.999999);
        return finishReport(
            stage, selfRecallPassed, similarity,
            scoringReference == nullptr ? 0.0 : 1.0 - similarity,
            assembly, {engram.id}, false);
    }

    void rememberSourceResolution(std::size_t width, std::size_t height) {
        if (width <= 512U && height <= 512U) return;
        if (width < 2U || height < 2U || width > 16'384U || height > 16'384U
            || width > 268'435'456U / height) {
            throw std::invalid_argument("IMAGINATIO source resolution is outside the supported sensory envelope");
        }
        auto found = std::find_if(sourceResolutions.begin(), sourceResolutions.end(),
            [width, height](const ResolutionEngram& value) {
                return value.width == width && value.height == height;
            });
        if (found != sourceResolutions.end()) {
            ++found->observations;
            return;
        }
        constexpr std::size_t kMaximumResolutionEngrams = 64U;
        if (sourceResolutions.size() >= kMaximumResolutionEngrams) {
            const auto victim = std::min_element(
                sourceResolutions.begin(), sourceResolutions.end(),
                [](const ResolutionEngram& left, const ResolutionEngram& right) {
                    if (left.observations != right.observations) {
                        return left.observations < right.observations;
                    }
                    return left.width * left.height < right.width * right.height;
                });
            sourceResolutions.erase(victim);
        }
        sourceResolutions.push_back(ResolutionEngram{
            .width = width, .height = height, .observations = 1U,
        });
    }

    TargetSpaceVisualRender renderLastTargetSpace(
        std::size_t targetWidth,
        std::size_t targetHeight) const {
        constexpr std::size_t kMaximumDimension = 4096U;
        constexpr std::size_t kMaximumPixels = 16'777'216U;
        if (targetWidth < 8U || targetHeight < 8U
            || targetWidth > kMaximumDimension || targetHeight > kMaximumDimension
            || targetWidth > kMaximumPixels / targetHeight) {
            throw std::invalid_argument(
                "IMAGINATIO target-space dimensions exceed the supported 4K raster envelope");
        }
        if (trace.empty()) {
            throw std::runtime_error(
                "IMAGINATIO target-space painting requires a completed motor trace");
        }

        TargetSpaceVisualRender render;
        render.width = targetWidth;
        render.height = targetHeight;
        render.informationWidth = config.width;
        render.informationHeight = config.height;
        render.executedDirectlyOnTargetCanvas = true;
        render.usedRasterInterpolation = false;
        render.synthesizedHighFrequencyDetail = targetWidth > config.width
            || targetHeight > config.height;
        render.rgb8.resize(targetWidth * targetHeight * 3U);

        // Recover the background from border pigments carried by the learned
        // motor events themselves. The 512x512 result canvas is intentionally
        // not read here: it must never become an image-resize source.
        std::array<double, 3> background{};
        std::array<std::vector<double>, 3> borderChannels;
        const auto appendBorderPigment = [&](const std::array<double, 3>& colour) {
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                borderChannels[channel].push_back(colour[channel]);
            }
        };
        for (const auto& event : trace) {
            if (!event.hasAction || event.phase != "act") continue;
            const auto& action = event.action;
            if (action.kind != PaintActionKind::Paint
                && action.kind != PaintActionKind::Erase) continue;
            if (action.kind == PaintActionKind::Paint
                && action.patchWidth > 0U && action.patchHeight > 0U) {
                const auto patchWidth = std::min<std::size_t>(8U, action.patchWidth);
                const auto patchHeight = std::min<std::size_t>(8U, action.patchHeight);
                for (std::size_t patchY = 0; patchY < patchHeight; ++patchY) {
                    for (std::size_t patchX = 0; patchX < patchWidth; ++patchX) {
                        const std::size_t sourceX = event.cursorBeforeX + patchX;
                        const std::size_t sourceY = event.cursorBeforeY + patchY;
                        if (sourceX >= config.width || sourceY >= config.height) continue;
                        if (sourceX != 0U && sourceY != 0U
                            && sourceX + 1U != config.width
                            && sourceY + 1U != config.height) continue;
                        const std::size_t patchIndex = (patchY * patchWidth + patchX) * 3U;
                        appendBorderPigment({
                            static_cast<double>(action.patchRgb[patchIndex]) / 255.0,
                            static_cast<double>(action.patchRgb[patchIndex + 1U]) / 255.0,
                            static_cast<double>(action.patchRgb[patchIndex + 2U]) / 255.0,
                        });
                    }
                }
            } else if (event.cursorBeforeX == 0U || event.cursorBeforeY == 0U
                       || event.cursorBeforeX + 1U == config.width
                       || event.cursorBeforeY + 1U == config.height) {
                appendBorderPigment(action.kind == PaintActionKind::Erase
                    ? std::array<double, 3>{0.0, 0.0, 0.0}
                    : std::array<double, 3>{
                        clamp01(action.red), clamp01(action.green), clamp01(action.blue)});
            }
        }
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            auto& values = borderChannels[channel];
            if (values.empty()) {
                background[channel] = 0.0;
                continue;
            }
            const auto middle = values.begin()
                + static_cast<std::ptrdiff_t>(values.size() / 2U);
            std::nth_element(values.begin(), middle, values.end());
            background[channel] = *middle;
        }

        // Preserve learned geometry through normalized motor coordinates. A
        // square memory remains centred on a 16:9 target instead of stretching.
        const double sourceWidth = static_cast<double>(config.width);
        const double sourceHeight = static_cast<double>(config.height);
        const double scale = std::min(
            static_cast<double>(targetWidth) / sourceWidth,
            static_cast<double>(targetHeight) / sourceHeight);
        const double offsetX = 0.5 * (static_cast<double>(targetWidth) - sourceWidth * scale);
        const double offsetY = 0.5 * (static_cast<double>(targetHeight) - sourceHeight * scale);

        std::vector<float> targetPigment(targetWidth * targetHeight * 3U);
        for (std::size_t index = 0; index < targetWidth * targetHeight; ++index) {
            targetPigment[index * 3U] = static_cast<float>(background[0]);
            targetPigment[index * 3U + 1U] = static_cast<float>(background[1]);
            targetPigment[index * 3U + 2U] = static_cast<float>(background[2]);
        }

        // Stable target-pixel noise supplies a deterministic pigment grain. Its
        // amplitude is learned from colour variation inside each remembered
        // motor patch. It therefore adds new target-space frequencies without
        // pretending to recover details absent from the sensory experience.
        const auto mixHash = [](std::uint64_t value) {
            value ^= value >> 30U;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27U;
            value *= 0x94d049bb133111ebULL;
            value ^= value >> 31U;
            return value;
        };
        const auto signedNoise = [&](std::size_t x, std::size_t y, std::uint64_t seed) {
            const std::uint64_t hash = mixHash(seed
                ^ (static_cast<std::uint64_t>(x) * 0x9e3779b185ebca87ULL)
                ^ (static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL));
            const double unit = static_cast<double>(hash >> 11U)
                * (1.0 / 9007199254740992.0);
            return 2.0 * unit - 1.0;
        };
        const auto paintTargetDab = [&](double centreX,
                                        double centreY,
                                        double tangentRadius,
                                        double normalRadius,
                                        double angle,
                                        const std::array<double, 3>& colour,
                                        double strength,
                                        double textureAmplitude,
                                        std::uint64_t seed,
                                        bool erase) {
            tangentRadius = std::max(0.65, tangentRadius);
            normalRadius = std::max(0.65, normalRadius);
            const double extent = std::max(tangentRadius, normalRadius) + 1.0;
            const auto minimumX = std::max<std::ptrdiff_t>(0,
                static_cast<std::ptrdiff_t>(std::floor(centreX - extent)));
            const auto maximumX = std::min<std::ptrdiff_t>(
                static_cast<std::ptrdiff_t>(targetWidth) - 1,
                static_cast<std::ptrdiff_t>(std::ceil(centreX + extent)));
            const auto minimumY = std::max<std::ptrdiff_t>(0,
                static_cast<std::ptrdiff_t>(std::floor(centreY - extent)));
            const auto maximumY = std::min<std::ptrdiff_t>(
                static_cast<std::ptrdiff_t>(targetHeight) - 1,
                static_cast<std::ptrdiff_t>(std::ceil(centreY + extent)));
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            for (std::ptrdiff_t y = minimumY; y <= maximumY; ++y) {
                for (std::ptrdiff_t x = minimumX; x <= maximumX; ++x) {
                    const double dx = static_cast<double>(x) + 0.5 - centreX;
                    const double dy = static_cast<double>(y) + 0.5 - centreY;
                    const double tangent = (cosine * dx + sine * dy) / tangentRadius;
                    const double normal = (-sine * dx + cosine * dy) / normalRadius;
                    const double radius2 = tangent * tangent + normal * normal;
                    if (radius2 >= 1.0) continue;
                    const double softCore = std::clamp((1.0 - radius2) * 2.35, 0.0, 1.0);
                    const double alpha = clamp01(strength * softCore);
                    const std::size_t pixel = static_cast<std::size_t>(y) * targetWidth
                        + static_cast<std::size_t>(x);
                    const std::size_t destination = pixel * 3U;
                    if (erase) {
                        for (std::size_t channel = 0; channel < 3U; ++channel) {
                            targetPigment[destination + channel] *= static_cast<float>(1.0 - alpha);
                        }
                        continue;
                    }
                    const double grain = signedNoise(
                        static_cast<std::size_t>(x), static_cast<std::size_t>(y), seed);
                    const double fibre = signedNoise(
                        static_cast<std::size_t>(x) / 2U,
                        static_cast<std::size_t>(y) / 2U,
                        seed ^ 0xd6e8feb86659fd93ULL);
                    const double texture = textureAmplitude * (0.72 * grain + 0.28 * fibre);
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        const double channelTexture = texture
                            + textureAmplitude * 0.12 * signedNoise(
                                static_cast<std::size_t>(x),
                                static_cast<std::size_t>(y),
                                seed + channel * 0x9e3779b97f4a7c15ULL);
                        const double source = clamp01(colour[channel] + channelTexture);
                        const double destinationValue = targetPigment[destination + channel];
                        targetPigment[destination + channel] = static_cast<float>(
                            destinationValue + alpha * (source - destinationValue));
                    }
                }
            }
        };

        std::size_t brushRadius = 0U;
        std::array<double, 3> brushColour{1.0, 1.0, 1.0};
        for (const auto& event : trace) {
            if (!event.hasAction || event.phase != "act") continue;
            const auto& action = event.action;
            if (action.kind == PaintActionKind::BrushSmall) {
                brushRadius = 0U;
                continue;
            }
            if (action.kind == PaintActionKind::BrushLarge) {
                brushRadius = std::min<std::size_t>(4U,
                    brushRadius + std::max<std::uint16_t>(1U, action.repetitions));
                continue;
            }
            if (action.kind == PaintActionKind::Brighter
                || action.kind == PaintActionKind::Darker) {
                const double direction = action.kind == PaintActionKind::Brighter ? 1.0 : -1.0;
                for (double& channel : brushColour) {
                    channel = clamp01(channel + direction * 0.125
                        * static_cast<double>(std::max<std::uint16_t>(1U, action.repetitions)));
                }
                continue;
            }
            if (action.kind != PaintActionKind::Paint
                && action.kind != PaintActionKind::Erase) continue;

            const bool erase = action.kind == PaintActionKind::Erase;
            if (!erase && action.red >= 0.0 && action.green >= 0.0 && action.blue >= 0.0) {
                brushColour = {clamp01(action.red), clamp01(action.green), clamp01(action.blue)};
            }
            if (!erase && action.patchWidth > 0U && action.patchHeight > 0U) {
                const std::size_t patchWidth = std::min<std::size_t>(8U, action.patchWidth);
                const std::size_t patchHeight = std::min<std::size_t>(8U, action.patchHeight);
                const auto patchColour = [&](std::ptrdiff_t x, std::ptrdiff_t y) {
                    const auto boundedX = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                        x, 0, static_cast<std::ptrdiff_t>(patchWidth - 1U)));
                    const auto boundedY = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                        y, 0, static_cast<std::ptrdiff_t>(patchHeight - 1U)));
                    const std::size_t index = (boundedY * patchWidth + boundedX) * 3U;
                    return std::array<double, 3>{
                        static_cast<double>(action.patchRgb[index]) / 255.0,
                        static_cast<double>(action.patchRgb[index + 1U]) / 255.0,
                        static_cast<double>(action.patchRgb[index + 2U]) / 255.0,
                    };
                };
                const auto luminance = [](const std::array<double, 3>& value) {
                    return 0.2126 * value[0] + 0.7152 * value[1] + 0.0722 * value[2];
                };
                for (std::size_t patchY = 0; patchY < patchHeight; ++patchY) {
                    for (std::size_t patchX = 0; patchX < patchWidth; ++patchX) {
                        const std::size_t sourceX = event.cursorBeforeX + patchX;
                        const std::size_t sourceY = event.cursorBeforeY + patchY;
                        if (sourceX >= config.width || sourceY >= config.height) continue;
                        const auto colour = patchColour(
                            static_cast<std::ptrdiff_t>(patchX),
                            static_cast<std::ptrdiff_t>(patchY));
                        const double gradientX = 0.5 * (luminance(patchColour(
                            static_cast<std::ptrdiff_t>(patchX) + 1,
                            static_cast<std::ptrdiff_t>(patchY)))
                            - luminance(patchColour(
                                static_cast<std::ptrdiff_t>(patchX) - 1,
                                static_cast<std::ptrdiff_t>(patchY))));
                        const double gradientY = 0.5 * (luminance(patchColour(
                            static_cast<std::ptrdiff_t>(patchX),
                            static_cast<std::ptrdiff_t>(patchY) + 1))
                            - luminance(patchColour(
                                static_cast<std::ptrdiff_t>(patchX),
                                static_cast<std::ptrdiff_t>(patchY) - 1)));
                        double localSpread2 = 0.0;
                        std::size_t localCount = 0U;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                const auto neighbour = patchColour(
                                    static_cast<std::ptrdiff_t>(patchX) + dx,
                                    static_cast<std::ptrdiff_t>(patchY) + dy);
                                for (std::size_t channel = 0; channel < 3U; ++channel) {
                                    const double difference = neighbour[channel] - colour[channel];
                                    localSpread2 += difference * difference;
                                }
                                ++localCount;
                            }
                        }
                        const double localSpread = std::sqrt(localSpread2
                            / static_cast<double>(std::max<std::size_t>(1U, localCount * 3U)));
                        const double edgeStrength = std::clamp(
                            4.0 * std::hypot(gradientX, gradientY), 0.0, 1.0);
                        const double angle = std::atan2(gradientY, gradientX) + 0.5 * kPi;
                        const std::uint64_t seed = mixHash(event.sequence
                            ^ (static_cast<std::uint64_t>(patchX) << 17U)
                            ^ (static_cast<std::uint64_t>(patchY) << 29U));
                        const double jitter = (1.0 - edgeStrength) * 0.12 * scale;
                        const double centreX = offsetX
                            + (static_cast<double>(sourceX) + 0.5) * scale
                            + jitter * signedNoise(sourceX, sourceY, seed);
                        const double centreY = offsetY
                            + (static_cast<double>(sourceY) + 0.5) * scale
                            + jitter * signedNoise(sourceY, sourceX, seed ^ 0xa0761d6478bd642fULL);
                        const double tangentRadius = scale * (0.92 + 0.26 * edgeStrength);
                        const double normalRadius = scale * (0.92 - 0.25 * edgeStrength);
                        const double textureAmplitude = render.synthesizedHighFrequencyDetail
                            ? std::clamp(0.004 + 0.16 * localSpread, 0.004, 0.032)
                            : 0.0;
                        paintTargetDab(
                            centreX, centreY,
                            tangentRadius, normalRadius, angle,
                            colour, clamp01(action.intensity),
                            textureAmplitude, seed, false);
                        ++render.motorMarksRendered;
                    }
                }
                continue;
            }

            const double centreX = offsetX
                + (static_cast<double>(event.cursorBeforeX) + 0.5) * scale;
            const double centreY = offsetY
                + (static_cast<double>(event.cursorBeforeY) + 0.5) * scale;
            const double radius = std::max(0.72 * scale,
                (static_cast<double>(brushRadius) + 0.72) * scale);
            paintTargetDab(
                centreX, centreY, radius, radius, 0.0,
                erase ? std::array<double, 3>{0.0, 0.0, 0.0} : brushColour,
                clamp01(action.intensity),
                render.synthesizedHighFrequencyDetail ? 0.006 : 0.0,
                mixHash(event.sequence), erase);
            ++render.motorMarksRendered;
        }
        if (render.motorMarksRendered == 0U) {
            throw std::runtime_error(
                "The last IMAGINATIO result contains no drawable motor marks");
        }

        for (std::size_t index = 0; index < targetWidth * targetHeight * 3U; ++index) {
            render.rgb8[index] = static_cast<std::uint8_t>(
                std::llround(clamp01(targetPigment[index]) * 255.0));
        }
        render.generatedFromMotorTrace = true;
        return render;
    }

    void removeAssociationsTo(std::uint64_t engramId) {
        for (auto iterator = symbols.begin(); iterator != symbols.end();) {
            auto& associations = iterator->second;
            associations.erase(std::remove_if(
                associations.begin(), associations.end(),
                [engramId](const SymbolAssociation& value) {
                    return value.engramId == engramId;
                }), associations.end());
            if (associations.empty()) iterator = symbols.erase(iterator);
            else ++iterator;
        }
    }

    VisualEngram* retainEngram(
        const VisualCanvas& reference,
        const std::vector<PaintAction>& demonstratedActions,
        const std::string& label,
        std::vector<AssemblyId> assemblies,
        double traceQuality,
        bool compactStorage = false,
        bool rasterScanProgram = false) {
        // Teacher actions are sensory supervision only. V14 deliberately does
        // not copy them into persistent memory because patchRgb may contain an
        // exact source raster. The storage-mode flags are accepted for ABI/source
        // compatibility but no longer alter retention semantics.
        static_cast<void>(demonstratedActions);
        static_cast<void>(compactStorage);
        static_cast<void>(rasterScanProgram);

        const auto fingerprint = visualFingerprint(reference);
        const auto contentHash = visualContentHash(reference);
        auto duplicate = std::find_if(
            engrams.begin(), engrams.end(),
            [contentHash](const VisualEngram& engram) {
                return engram.contentHash != 0U && engram.contentHash == contentHash;
            });
        if (duplicate != engrams.end()) {
            ++duplicate->observations;
            duplicate->label = label.empty() ? duplicate->label : label;
            duplicate->traceQuality = std::max(duplicate->traceQuality, traceQuality);
            for (const auto assembly : assemblies) {
                if (std::find(duplicate->assemblies.begin(), duplicate->assemblies.end(), assembly)
                    == duplicate->assemblies.end()) {
                    duplicate->assemblies.push_back(assembly);
                }
            }
            return &*duplicate;
        }

        if (engrams.size() >= config.maximumEngrams) {
            const auto victim = std::min_element(
                engrams.begin(), engrams.end(),
                [](const auto& left, const auto& right) {
                    if (left.observations != right.observations) {
                        return left.observations < right.observations;
                    }
                    return left.id < right.id;
                });
            removeAssociationsTo(victim->id);
            engrams.erase(victim);
        }

        VisualEngram engram;
        engram.id = nextEngramId++;
        engram.label = label;
        engram.fingerprint = fingerprint;
        engram.contentHash = contentHash;
        engram.shape = extractShape(reference);
        engram.featureModel = extractFeatureModel(reference);
        engram.regions = extractRegionTokens(reference);
        engram.strokes = extractStrokeTokens(reference);
        engram.assemblies = std::move(assemblies);
        engram.observations = 1U;
        engram.traceQuality = traceQuality;
        engrams.push_back(std::move(engram));
        return &engrams.back();
    }

    VisualImaginationConfig config;
    std::unique_ptr<RobotMind> ownedMind;
    RobotMind& mind;
    mutable neuro::vision::OcularSystem ocularSystem;
    neuro::vision::VisualPathway visualPathway;
    mutable neuro::vision::VisualPercept lastVisualPercept;
    VisualCanvas canvas;
    std::vector<VisualEngram> engrams;
    std::map<std::string, std::vector<SymbolAssociation>> symbols;
    std::vector<CategoryEngram> categories;
    std::vector<ObjectEngram> objectEngrams;
    std::vector<ResolutionEngram> sourceResolutions;
    std::vector<PoseEngram> poses;
    std::vector<RelationEngram> relations;
    std::vector<ActionEngram> actions;
    std::vector<SceneInstance> sceneMemoryInstances;
    std::vector<SceneRelationCue> sceneMemoryRelations;
    std::uint64_t timestamp = 0;
    std::uint64_t nextEngramId = 1;
    std::uint64_t nextObjectEngramId = 1;
    std::uint64_t actionsExecuted = 0;
    double lastCanvasMean = 0.0;
    double runningSimilarity = 0.0;
    bool runningSimilarityValid = false;
    ImaginationReport last;
    VisualScenePerceptionReport lastScenePerception;
    std::vector<CausalEvent> trace;
    std::uint64_t traceSequence = 0;
    std::uint64_t totalAttempts = 0;
    std::uint64_t successfulAttempts = 0;
    std::uint64_t failedAttempts = 0;
    AssemblyId lastObservedAssembly = 0;
};

VisualImagination::VisualImagination(VisualImaginationConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

VisualImagination::VisualImagination(
    RobotMind& sharedMind,
    VisualImaginationConfig config)
    : impl_(std::make_unique<Impl>(sharedMind, std::move(config))) {}

VisualImagination::~VisualImagination() = default;
VisualImagination::VisualImagination(VisualImagination&&) noexcept = default;
VisualImagination& VisualImagination::operator=(VisualImagination&&) noexcept = default;

ImaginationReport VisualImagination::learnToTrace(
    const VisualCanvas& reference,
    const std::vector<PaintAction>& demonstratedActions,
    const std::string& label) {
    impl_->validateCanvas(reference);
    if (demonstratedActions.empty()) {
        throw std::invalid_argument("A visual lesson requires at least one demonstrated action");
    }
    if (demonstratedActions.size() > 1'000'000U) {
        throw std::invalid_argument("Demonstrated motor trace is implausibly large");
    }
    for (const auto& action : demonstratedActions) {
        if (static_cast<unsigned int>(action.kind)
                > static_cast<unsigned int>(PaintActionKind::BrushLarge)
            || !std::isfinite(action.intensity)) {
            throw std::invalid_argument("Demonstrated motor trace contains an invalid action");
        }
        if (!std::isfinite(action.red) || !std::isfinite(action.green)
            || !std::isfinite(action.blue)) {
            throw std::invalid_argument("Demonstrated motor trace contains invalid pigment");
        }
        if (action.patchWidth > 8U || action.patchHeight > 8U
            || ((action.patchWidth == 0U) != (action.patchHeight == 0U))) {
            throw std::invalid_argument("Demonstrated motor trace contains an invalid pigment patch");
        }
    }

    impl_->resetTrace();
    impl_->canvas.clear();
    impl_->lastCanvasMean = 0.0;
    impl_->actionsExecuted = 0;
    const auto signature = canvasSignature(reference);
    auto observed = impl_->expose(
        &reference, {}, ImaginationStage::ObserveAndTrace, signature);
    std::vector<AssemblyId> assemblies;
    if (const auto assembly = representedAssembly(observed); assembly != 0) {
        assemblies.push_back(assembly);
    }
    for (std::size_t index = 0; index < demonstratedActions.size(); ++index) {
        const bool feedback = index == 0
            || index + 1U == demonstratedActions.size()
            || index % impl_->config.neuralFeedbackStride == 0;
        observed = impl_->act(
            demonstratedActions[index], &reference, {},
            ImaginationStage::ObserveAndTrace,
            signature, true, false, feedback);
        const auto assembly = representedAssembly(observed);
        if (assembly != 0
            && std::find(assemblies.begin(), assemblies.end(), assembly)
                == assemblies.end()) {
            assemblies.push_back(assembly);
        }
    }
    const double similarity = impl_->canvas.similarity(reference);
    const VisualCanvas selfPainted = impl_->canvas;

    auto* learned = impl_->retainEngram(
        reference, demonstratedActions, label, std::move(assemblies), similarity);
    AssemblyId selfAssembly = 0U;
    if (learned != nullptr) {
        selfAssembly = impl_->consolidateSelfResult(
            *learned, selfPainted, label, similarity);
        // Keep the just-produced painting visible in the live UI. Snapshot V14
        // does not persist this working canvas; only the self-motor engram is
        // retained for later reconstruction.
        // Normalize the visible self-result to the project's 24-bit sRGB
        // motor-memory domain. For real RGB8 corpus/UI input this is byte-exact;
        // for higher-precision synthetic input it defines the remembered result.
        impl_->canvas = impl_->selfMotorCanvas(*learned);
        impl_->lastCanvasMean = impl_->canvas.meanIntensity();
    } else {
        impl_->canvas.clear();
        impl_->lastCanvasMean = 0.0;
    }
    impl_->mind.endEpisode(similarity >= 0.50 && learned != nullptr);

    // Unsupervised object memory is learned from the external reference, not
    // from the motor reconstruction. This gives the system persistent visual
    // identity before a semantic label/category exists.
    static_cast<void>(impl_->perceiveSceneInternal(
        reference, {}, true,
        std::min<std::size_t>(16U, impl_->visualPathway.config().maximumObjects), false));
    return impl_->finishReport(
        ImaginationStage::ObserveAndTrace,
        similarity >= 0.50 && learned != nullptr,
        similarity, 1.0 - similarity,
        selfAssembly != 0U ? selfAssembly : representedAssembly(observed),
        learned == nullptr ? std::vector<std::uint64_t>{}
                           : std::vector<std::uint64_t>{learned->id},
        true);
}

bool VisualImagination::ingestCategoryExample(
    const VisualCanvas& reference,
    const std::string& label,
    const std::string& rawCategory,
    std::size_t patchSide) {
    impl_->validateCanvas(reference);
    const auto category = normalizedSymbol(rawCategory);
    if (category.empty()) throw std::invalid_argument("Category must not be empty");
    if (patchSide == 0U || patchSide > 8U) {
        throw std::invalid_argument("Corpus patch side must be in [1, 8]");
    }

    // V14 corpus ingestion uses the source only as a transient visible lesson.
    // TATARUS first paints the lesson onto its own canvas, then performs a
    // self-imprint pass on that finished canvas. The teacher patch program is
    // never copied into persistent memory.
    impl_->resetTrace();
    impl_->canvas.clear();
    impl_->lastCanvasMean = 0.0;
    impl_->actionsExecuted = 0U;
    const auto signature = canvasSignature(reference);
    auto observed = impl_->expose(
        &reference,
        category,
        ImaginationStage::ObserveAndTrace,
        signature);
    std::vector<AssemblyId> assemblies;
    if (const auto assembly = representedAssembly(observed); assembly != 0U) {
        assemblies.push_back(assembly);
    }

    const auto transientTeacher = makeTeacherTrace(reference, 0.0, patchSide);
    for (std::size_t index = 0; index < transientTeacher.size(); ++index) {
        const bool feedback = index == 0U
            || index + 1U == transientTeacher.size()
            || index % impl_->config.neuralFeedbackStride == 0U;
        observed = impl_->act(
            transientTeacher[index],
            &reference,
            category,
            ImaginationStage::ObserveAndTrace,
            signature,
            true,
            false,
            feedback);
        const auto assembly = representedAssembly(observed);
        if (assembly != 0U
            && std::find(assemblies.begin(), assemblies.end(), assembly)
                == assemblies.end()) {
            assemblies.push_back(assembly);
        }
    }

    const double similarity = impl_->canvas.similarity(reference);
    const VisualCanvas selfPainted = impl_->canvas;
    const std::vector<PaintAction> noPersistentTeacherTrace;
    auto* learned = impl_->retainEngram(
        reference,
        noPersistentTeacherTrace,
        label,
        std::move(assemblies),
        similarity,
        false,
        false);
    if (learned == nullptr) {
        impl_->mind.endEpisode(false);
        return false;
    }
    const AssemblyId selfAssembly = impl_->consolidateSelfResult(
        *learned, selfPainted, category, similarity);
    impl_->updateCategory(category, reference, selfAssembly, false);
    impl_->mind.endEpisode(similarity >= 0.50);

    // Keep only the organism's own finished painting in the live UI. It is not
    // serialized as the snapshot working canvas; cold recall reconstructs it
    // from the bound self-motor engram.
    impl_->canvas = impl_->selfMotorCanvas(*learned);
    impl_->lastCanvasMean = impl_->canvas.meanIntensity();

    ImaginationReport report;
    report.success = similarity >= 0.50;
    report.stage = ImaginationStage::ObserveAndTrace;
    report.canvas = impl_->canvas;
    report.similarity = similarity;
    report.novelty = 1.0 - similarity;
    report.actionsExecuted = impl_->actionsExecuted;
    report.activeAssemblyId = selfAssembly != 0U
        ? selfAssembly : representedAssembly(observed);
    report.recalledEngramIds = {learned->id};
    report.categoryNames = {category};
    report.referenceVisibleDuringDrawing = true;
    report.physiology = impl_->mind.physiology();
    report.prospection = impl_->mind.prospection();
    report.motor = impl_->mind.motor();
    impl_->last = std::move(report);
    ++impl_->totalAttempts;
    if (similarity >= 0.50) ++impl_->successfulAttempts;
    else ++impl_->failedAttempts;
    return similarity >= 0.50;
}

ImaginationReport VisualImagination::drawFromMemory(const VisualCanvas& cue) {
    impl_->validateCanvas(cue);
    impl_->resetTrace();
    if (impl_->engrams.empty()) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::MemoryRecall, false, 0.0, 1.0, 0, {}, false);
    }
    const auto cueObservation = impl_->expose(
        &cue, {}, ImaginationStage::MemoryRecall, canvasSignature(cue));
    const auto cueAssembly = representedAssembly(cueObservation);
    const auto* engram = impl_->closestEngram(cue, cueAssembly);
    if (engram == nullptr) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::MemoryRecall, false, 0.0, 1.0,
            cueAssembly, {}, false);
    }
    return impl_->replay(
        *engram, ImaginationStage::MemoryRecall, {}, &cue,
        cueAssembly);
}

bool VisualImagination::associateSymbol(
    const std::string& symbol,
    const VisualCanvas& cue) {
    impl_->validateCanvas(cue);
    impl_->resetTrace();
    const auto normalized = normalizedSymbol(symbol);
    if (normalized.empty()) throw std::invalid_argument("Symbol must not be empty");
    const auto visualObservation = impl_->expose(
        &cue, normalized, ImaginationStage::SymbolRecall, fnv1a(normalized));
    auto* engram = const_cast<Impl::VisualEngram*>(
        impl_->closestEngram(cue, representedAssembly(visualObservation)));
    if (engram == nullptr || impl_->engramSimilarity(*engram, cue) < 0.50) return false;
    const auto visualAssembly = representedAssembly(visualObservation);
    if (visualAssembly != 0
        && std::find(engram->assemblies.begin(), engram->assemblies.end(),
            visualAssembly) == engram->assemblies.end()) {
        engram->assemblies.push_back(visualAssembly);
    }

    auto& associations = impl_->symbols[normalized];
    auto found = std::find_if(associations.begin(), associations.end(),
        [engram](const Impl::SymbolAssociation& value) {
            return value.engramId == engram->id;
        });
    const double evidence = impl_->engramSimilarity(*engram, cue);
    if (found == associations.end()) {
        associations.push_back(Impl::SymbolAssociation{
            .engramId = engram->id,
            .observations = 1,
            .strength = evidence,
        });
    } else {
        ++found->observations;
        const double rate = 1.0 / static_cast<double>(found->observations);
        found->strength += rate * (evidence - found->strength);
    }
    return true;
}

ImaginationReport VisualImagination::drawFromSymbol(const std::string& symbol) {
    impl_->resetTrace();
    const auto normalized = normalizedSymbol(symbol);
    if (normalized.empty()) throw std::invalid_argument("Symbol must not be empty");
    const auto cueObservation = impl_->expose(
        nullptr, normalized, ImaginationStage::SymbolRecall, fnv1a(normalized));
    const auto cueAssembly = representedAssembly(cueObservation);
    const auto* engram = impl_->selectedForSymbol(normalized, cueAssembly);
    if (engram == nullptr) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::SymbolRecall, false, 0.0, 1.0,
            cueAssembly, {}, false);
    }
    const auto prototype = impl_->engramPrototype(*engram);
    return impl_->replay(
        *engram, ImaginationStage::SymbolRecall, normalized,
        &prototype, cueAssembly);
}

ImaginationReport VisualImagination::drawFreely(const std::string& label) {
    impl_->resetTrace();
    if (impl_->engrams.empty()) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::MemoryRecall, false, 0.0, 1.0, 0, {}, false);
    }
    const auto normalized = normalizedSymbol(label);
    const Impl::VisualEngram* selected = nullptr;
    for (const auto& engram : impl_->engrams) {
        if (!normalized.empty() && normalizedSymbol(engram.label) != normalized) continue;
        if (selected == nullptr
            || engram.observations > selected->observations
            || (engram.observations == selected->observations
                && engram.traceQuality > selected->traceQuality)) {
            selected = &engram;
        }
    }
    if (selected == nullptr) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::MemoryRecall, false, 0.0, 1.0, 0, {}, false);
    }
    const auto cueObservation = impl_->expose(
        nullptr, {}, ImaginationStage::MemoryRecall, selected->id);
    const auto prototype = impl_->engramPrototype(*selected);
    return impl_->replay(
        *selected, ImaginationStage::MemoryRecall, {},
        &prototype, representedAssembly(cueObservation));
}

ImaginationReport VisualImagination::compose(const std::vector<std::string>& rawSymbols) {
    impl_->resetTrace();
    if (rawSymbols.size() < 2U) {
        throw std::invalid_argument("Composition requires at least two symbols");
    }
    std::vector<std::string> normalized;
    normalized.reserve(rawSymbols.size());
    std::string combined;
    for (const auto& raw : rawSymbols) {
        auto symbol = normalizedSymbol(raw);
        if (symbol.empty()) throw std::invalid_argument("Composition contains an empty symbol");
        if (!combined.empty()) combined.push_back('+');
        combined += symbol;
        normalized.push_back(std::move(symbol));
    }

    const auto cueObservation = impl_->expose(
        nullptr, combined, ImaginationStage::Composition, fnv1a(combined));
    std::vector<const Impl::VisualEngram*> sources;
    sources.reserve(normalized.size());
    for (const auto& symbol : normalized) {
        const auto* selected = impl_->selectedForSymbol(
            symbol, representedAssembly(cueObservation));
        if (selected == nullptr) {
            impl_->canvas.clear();
            impl_->actionsExecuted = 0;
            return impl_->finishReport(
                ImaginationStage::Composition, false, 0.0, 1.0,
                representedAssembly(cueObservation), {}, false);
        }
        sources.push_back(selected);
    }

    std::vector<VisualCanvas> sourceMemories;
    sourceMemories.reserve(sources.size());
    for (const auto* source : sources) {
        sourceMemories.push_back(impl_->engramPrototype(*source));
    }
    // Stage 4 constructs one novel geometry and independently reactivates a
    // generic object-adaptive 3x3 part topology plus background. The same
    // mechanism can combine face, animal, plant, building or tool structure.
    std::vector<std::size_t> featureSources;
    const auto candidate = impl_->synthesizeForm(
        sourceMemories, fnv1a("COMPOSITION:" + combined),
        std::nullopt, nullptr, nullptr, 0U, &featureSources);
    const auto compositionProgram = VisualImagination::makeTeacherTrace(
        candidate, 0.0, impl_->config.paintPatchSide);

    impl_->canvas.clear();
    impl_->lastCanvasMean = 0.0;
    impl_->actionsExecuted = 0;
    const bool learningWasEnabled = impl_->mind.learningEnabled();
    impl_->mind.setLearningEnabled(false);
    ObserveResult observed;
    try {
        for (std::size_t index = 0; index < compositionProgram.size(); ++index) {
            const bool feedback = index == 0
                || index + 1U == compositionProgram.size()
                || index % impl_->config.neuralFeedbackStride == 0;
            observed = impl_->act(
                compositionProgram[index], nullptr, combined,
                ImaginationStage::Composition,
                fnv1a(combined), false, true, feedback);
        }
        impl_->mind.setLearningEnabled(learningWasEnabled);
    } catch (...) {
        impl_->mind.setLearningEnabled(learningWasEnabled);
        throw;
    }

    double maximumSourceSimilarity = 0.0;
    std::vector<std::uint64_t> sourceIds;
    sourceIds.reserve(sources.size());
    for (const auto* source : sources) {
        maximumSourceSimilarity = std::max(
            maximumSourceSimilarity, impl_->engramSimilarity(*source, impl_->canvas));
        sourceIds.push_back(source->id);
    }
    const double novelty = 1.0 - maximumSourceSimilarity;
    const AssemblyId observedAssembly = representedAssembly(observed);
    const AssemblyId cueAssembly = representedAssembly(cueObservation);
    const AssemblyId assembly = observedAssembly != 0
        ? observedAssembly : cueAssembly;
    std::vector<std::string> featureNames;
    for (const auto name : Impl::featureRegionNames()) {
        featureNames.emplace_back(name);
    }
    return impl_->finishReport(
        ImaginationStage::Composition,
        !compositionProgram.empty() && impl_->canvas.markedPixels() > 0,
        0.0, novelty, assembly, std::move(sourceIds), false, {},
        std::move(featureNames), std::move(featureSources));
}

bool VisualImagination::addCategoryExample(
    const std::string& rawCategory,
    const VisualCanvas& cue) {
    impl_->validateCanvas(cue);
    const auto category = normalizedSymbol(rawCategory);
    if (category.empty()) {
        throw std::invalid_argument("Category must not be empty");
    }
    const auto* concrete = impl_->closestEngram(cue);
    if (concrete == nullptr || impl_->engramSimilarity(*concrete, cue) < 0.995) {
        return false;
    }
    const auto observed = impl_->expose(
        &cue, category, ImaginationStage::ObserveAndTrace,
        fnv1a("CATEGORY:" + category));
    const AssemblyId observedAssembly = representedAssembly(observed);
    const AssemblyId assembly = observedAssembly != 0
        ? observedAssembly
        : concrete->assemblies.empty() ? 0 : concrete->assemblies.front();
    impl_->updateCategory(category, cue, assembly);
    // Bind semantic category experience to the already learned (or newly
    // created) object identity. A later multi-object scene can therefore
    // retrieve the category from object memory even under weaker bottom-up
    // evidence.
    static_cast<void>(impl_->perceiveSceneInternal(
        cue, {category}, true,
        std::min<std::size_t>(16U, impl_->visualPathway.config().maximumObjects), false));
    return true;
}

VisualRecognitionReport VisualImagination::recognizeCategories(
    const VisualCanvas& cue,
    const std::vector<std::string>& rawExpectedCategories,
    std::size_t maximumMatches) const {
    return impl_->recognizeCategoriesInternal(
        cue, rawExpectedCategories, maximumMatches);
}

VisualScenePerceptionReport VisualImagination::perceiveScene(
    const VisualCanvas& cue,
    const std::vector<std::string>& expectedCategories,
    bool learnObjects,
    std::size_t maximumObjects) {
    return impl_->perceiveSceneInternal(
        cue, expectedCategories, learnObjects, maximumObjects, false);
}

ImaginationReport VisualImagination::drawFromCategory(
    const std::string& category,
    std::uint64_t variationSeed) {
    return fuseCategories({category}, variationSeed);
}

ImaginationReport VisualImagination::fuseCategories(
    const std::vector<std::string>& rawCategories,
    std::uint64_t variationSeed) {
    impl_->resetTrace();
    if (rawCategories.empty() || rawCategories.size() > 5U) {
        throw std::invalid_argument("Category fusion requires one to five categories");
    }
    std::vector<std::string> names;
    std::vector<const Impl::CategoryEngram*> sources;
    std::string combined;
    for (const auto& raw : rawCategories) {
        const auto name = normalizedSymbol(raw);
        if (name.empty()) throw std::invalid_argument("Category fusion contains an empty category");
        if (std::find(names.begin(), names.end(), name) != names.end()) continue;
        const auto* source = impl_->categoryByName(name);
        if (source == nullptr || source->observations < 2U) {
            impl_->canvas.clear();
            impl_->actionsExecuted = 0;
            return impl_->finishReport(
                ImaginationStage::CategoryFusion, false, 0.0, 1.0,
                0, {}, false, names);
        }
        if (!combined.empty()) combined.push_back('+');
        combined += name;
        names.push_back(name);
        sources.push_back(source);
    }
    if (sources.empty()) {
        impl_->canvas.clear();
        impl_->actionsExecuted = 0;
        return impl_->finishReport(
            ImaginationStage::CategoryFusion, false, 0.0, 1.0,
            0, {}, false, names);
    }

    const auto cueObservation = impl_->expose(
        nullptr, combined, ImaginationStage::CategoryFusion,
        fnv1a("FUSION:" + combined));
    std::vector<VisualCanvas> variants;
    variants.reserve(sources.size());
    std::vector<std::size_t> featureSources;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        std::vector<std::size_t> categoryFeatureSources;
        variants.push_back(impl_->categoryVariant(
            *sources[index], variationSeed + index * 0x9e3779b97f4a7c15ULL,
            sources.size() == 1U ? &categoryFeatureSources : nullptr));
        if (sources.size() == 1U) featureSources = std::move(categoryFeatureSources);
    }

    // A fusion of categories is again one structural act: first derive one
    // target geometry from all reactivated category variants, then register
    // them and let whole cortical fields contribute. Direct translucent pixel
    // averaging is deliberately absent.
    const VisualCanvas candidate = variants.size() == 1U
        ? variants.front()
        : impl_->synthesizeForm(
            variants,
            variationSeed != 0U ? variationSeed : fnv1a("FUSION-FORM:" + combined),
            std::nullopt, nullptr, nullptr, 0U, &featureSources);

    const auto program = VisualImagination::makeTeacherTrace(
        candidate, 0.0, impl_->config.paintPatchSide);
    impl_->canvas.clear();
    impl_->lastCanvasMean = 0.0;
    impl_->actionsExecuted = 0;
    const bool learningWasEnabled = impl_->mind.learningEnabled();
    impl_->mind.setLearningEnabled(false);
    ObserveResult observed;
    try {
        for (std::size_t index = 0; index < program.size(); ++index) {
            const bool feedback = index == 0 || index + 1U == program.size()
                || index % impl_->config.neuralFeedbackStride == 0;
            observed = impl_->act(
                program[index], nullptr, combined,
                ImaginationStage::CategoryFusion, fnv1a(combined),
                false, true, feedback);
        }
        impl_->mind.setLearningEnabled(learningWasEnabled);
    } catch (...) {
        impl_->mind.setLearningEnabled(learningWasEnabled);
        throw;
    }

    double maximumSimilarity = 0.0;
    std::vector<std::uint64_t> representativeEngrams;
    const auto outputFeatures = impl_->extractFeatureModel(impl_->canvas);
    for (const auto* source : sources) {
        maximumSimilarity = std::max(
            maximumSimilarity,
            Impl::featureSimilarity(outputFeatures, source->featureMean));
        const auto* representative = impl_->representativeEngram(*source);
        if (representative != nullptr
            && std::find(representativeEngrams.begin(), representativeEngrams.end(),
                representative->id) == representativeEngrams.end()) {
            representativeEngrams.push_back(representative->id);
        }
    }
    const AssemblyId observedAssembly = representedAssembly(observed);
    const AssemblyId cueAssembly = representedAssembly(cueObservation);
    std::vector<std::string> featureNames;
    for (const auto name : Impl::featureRegionNames()) {
        featureNames.emplace_back(name);
    }
    return impl_->finishReport(
        ImaginationStage::CategoryFusion, !program.empty(),
        maximumSimilarity, 1.0 - maximumSimilarity,
        observedAssembly != 0 ? observedAssembly : cueAssembly,
        std::move(representativeEngrams), false, std::move(names),
        std::move(featureNames), std::move(featureSources));
}

bool VisualImagination::addPoseExample(
    const std::string& rawCategory,
    const std::string& rawPose,
    const VisualCanvas& cue) {
    impl_->validateCanvas(cue);
    const auto category = normalizedSymbol(rawCategory);
    const auto pose = normalizedSymbol(rawPose);
    if (category.empty() || pose.empty()) {
        throw std::invalid_argument("Pose examples require a category and pose");
    }
    if (impl_->categoryByName(category) == nullptr) {
        throw std::invalid_argument("Pose example references an unknown category");
    }
    const auto* concrete = impl_->closestEngram(cue);
    if (concrete == nullptr || impl_->engramSimilarity(*concrete, cue) < 0.995) return false;
    impl_->resetTrace();
    const auto observed = impl_->expose(
        &cue, category + ':' + pose, ImaginationStage::RelationalScene,
        fnv1a("POSE:" + category + ':' + pose));
    const auto assembly = representedAssembly(observed);
    impl_->updatePose(category, pose, cue, concrete->id, assembly);
    return true;
}

bool VisualImagination::observeScene(const SceneDescription& rawScene) {
    impl_->resetTrace();
    const auto scene = impl_->normalizedScene(rawScene, true);
    auto instances = impl_->resolveScene(scene);
    const auto observedCanvas = impl_->rasterizedScene(
        instances, scene, fnv1a("SCENE-LESSON:" + scene.name));
    const auto observed = impl_->expose(
        &observedCanvas, "SCENE:" + scene.name,
        ImaginationStage::RelationalScene, fnv1a("SCENE:" + scene.name));
    const auto assembly = representedAssembly(observed);
    const auto find = [&instances](const std::string& key) -> const SceneInstance* {
        const auto item = std::find_if(instances.begin(), instances.end(),
            [&key](const SceneInstance& value) { return value.instance == key; });
        return item == instances.end() ? nullptr : &*item;
    };
    for (const auto& relation : scene.relations) {
        const auto* subject = find(relation.subject);
        const auto* object = find(relation.object);
        if (subject != nullptr && object != nullptr) {
            impl_->updateRelation(relation.relation, *subject, *object, assembly);
        }
    }
    for (auto& instance : instances) instance.assemblyId = assembly;
    impl_->sceneMemoryInstances = std::move(instances);
    impl_->sceneMemoryRelations = scene.relations;
    return true;
}

ImaginationReport VisualImagination::imagineScene(
    const SceneDescription& rawScene,
    std::uint64_t variationSeed) {
    impl_->resetTrace();
    const auto scene = impl_->normalizedScene(rawScene, false);
    auto instances = impl_->resolveScene(scene);
    const std::uint64_t seed = variationSeed != 0U
        ? variationSeed : fnv1a("SCENE-IMAGINATION:" + scene.name);
    const auto cueObservation = impl_->expose(
        nullptr, "SCENE:" + scene.name, ImaginationStage::RelationalScene,
        fnv1a("SCENE:" + scene.name), true);
    ObserveResult finalObservation = cueObservation;
    const bool painted = impl_->paintScene(
        instances, scene, ImaginationStage::RelationalScene, seed, finalObservation);
    std::vector<std::string> categories;
    std::vector<std::uint64_t> sources;
    for (const auto& instance : instances) {
        if (std::find(categories.begin(), categories.end(), instance.category) == categories.end()) {
            categories.push_back(instance.category);
        }
        if (instance.sourceEngramId != 0U
            && std::find(sources.begin(), sources.end(), instance.sourceEngramId) == sources.end()) {
            sources.push_back(instance.sourceEngramId);
        }
    }
    const auto assembly = representedAssembly(finalObservation) != 0U
        ? representedAssembly(finalObservation) : representedAssembly(cueObservation);
    auto report = impl_->finishReport(
        ImaginationStage::RelationalScene,
        painted && instances.size() == scene.objects.size(),
        impl_->relationAccuracy(instances, scene.relations),
        1.0 - impl_->relationAccuracy(instances, scene.relations),
        assembly, std::move(sources), false, std::move(categories));
    report.sceneInstances = instances;
    report.sceneRelations = scene.relations;
    report.sceneMetrics = impl_->sceneMetricsFor(instances, scene.relations);
    impl_->last = report;
    return report;
}

bool VisualImagination::learnSceneTransition(
    const SceneDescription& rawBefore,
    const SceneAction& action,
    const SceneDescription& rawAfter) {
    impl_->resetTrace();
    const auto before = impl_->normalizedScene(rawBefore, true);
    const auto after = impl_->normalizedScene(rawAfter, true);
    auto beforeInstances = impl_->resolveScene(before);
    auto afterInstances = impl_->resolveScene(after);
    if (beforeInstances.size() != afterInstances.size()) {
        throw std::invalid_argument("Transition states must contain the same scene instances");
    }
    for (const auto& instance : beforeInstances) {
        const auto match = std::find_if(afterInstances.begin(), afterInstances.end(),
            [&instance](const SceneInstance& value) {
                return value.instance == instance.instance && value.category == instance.category;
            });
        if (match == afterInstances.end()) {
            throw std::invalid_argument(
                "Transition states must preserve instance identity and category");
        }
    }
    const auto beforeCanvas = impl_->rasterizedScene(
        beforeInstances, before, fnv1a("TRANSITION-BEFORE:" + before.name));
    const auto afterCanvas = impl_->rasterizedScene(
        afterInstances, after, fnv1a("TRANSITION-AFTER:" + after.name));
    const auto label = Impl::actionLabel(action);
    impl_->expose(
        &beforeCanvas, "STATE:T0:" + before.name,
        ImaginationStage::ProspectiveImagination,
        fnv1a("TRANSITION:" + label));
    const ActionId actionId = 100U + static_cast<ActionId>(action.kind);
    impl_->mind.beginAction(ActionEvent{
        .id = actionId,
        .label = sceneActionName(action.kind),
        .intensity = clamp01(action.magnitude),
        .startedNs = (impl_->timestamp + 1U) * 1'000'000ULL,
    });
    const auto observed = impl_->expose(
        &afterCanvas, "STATE:T1:" + after.name + ':' + label,
        ImaginationStage::ProspectiveImagination,
        fnv1a("TRANSITION:" + label));
    const double change = 1.0 - beforeCanvas.similarity(afterCanvas);
    impl_->mind.endAction(ActionOutcome{
        .id = actionId,
        .reward = change > 0.0 ? 0.5 : 0.0,
        .success = 1.0,
        .novelty = change,
    });
    impl_->updateAction(
        action, beforeInstances, afterInstances, representedAssembly(observed));
    impl_->sceneMemoryInstances = std::move(afterInstances);
    impl_->sceneMemoryRelations = after.relations;
    return true;
}

ImaginationReport VisualImagination::imagineFuture(
    const SceneDescription& rawInitial,
    const std::vector<SceneAction>& actions,
    std::uint64_t variationSeed) {
    if (actions.empty() || actions.size() > 256U) {
        throw std::invalid_argument("Prospective imagination requires 1 to 256 actions");
    }
    impl_->resetTrace();
    const auto initial = impl_->normalizedScene(rawInitial, false);
    auto state = impl_->resolveScene(initial);
    std::vector<ProspectiveSceneState> timeline;
    timeline.push_back(ProspectiveSceneState{
        .step = 0,
        .instances = state,
        .confidence = 1.0,
        .predictionError = 0.0,
    });
    double cumulativeConfidence = 1.0;
    double cumulativeError = 0.0;
    std::size_t learnedEffects = 0;
    ObserveResult observed;
    const auto initialCanvas = impl_->rasterizedScene(
        state, initial, fnv1a("PROSPECTION:T0:" + initial.name));
    observed = impl_->expose(
        &initialCanvas, "IMAGINED:T0:" + initial.name,
        ImaginationStage::ProspectiveImagination,
        fnv1a("PROSPECTION:" + initial.name), true);
    impl_->trace.back().prospectiveStep = 0;
    for (std::size_t step = 0; step < actions.size(); ++step) {
        double confidence = 0.0;
        double error = 1.0;
        if (!impl_->applyActionPrediction(state, actions[step], confidence, error)) {
            impl_->canvas.clearColor(
                initial.backgroundColor[0], initial.backgroundColor[1], initial.backgroundColor[2]);
            impl_->actionsExecuted = 0;
            auto failed = impl_->finishReport(
                ImaginationStage::ProspectiveImagination, false, 0.0, 1.0,
                representedAssembly(observed), {}, false);
            failed.prospectiveStates = std::move(timeline);
            failed.sceneInstances = state;
            failed.sceneRelations = initial.relations;
            failed.sceneMetrics = impl_->sceneMetricsFor(state, initial.relations);
            failed.sceneMetrics.learnedEffectsUsed = learnedEffects;
            failed.sceneMetrics.predictionConfidence = 0.0;
            failed.sceneMetrics.predictionError = 1.0;
            impl_->last = failed;
            return failed;
        }
        ++learnedEffects;
        cumulativeConfidence *= confidence;
        cumulativeError += error;
        timeline.push_back(ProspectiveSceneState{
            .step = step + 1U,
            .instances = state,
            .confidence = cumulativeConfidence,
            .predictionError = cumulativeError / static_cast<double>(step + 1U),
        });
        SceneDescription imagined = initial;
        imagined.name = initial.name + ":T" + std::to_string(step + 1U);
        imagined.objects.clear();
        for (const auto& instance : state) {
            imagined.objects.push_back(SceneObjectCue{
                .instance = instance.instance,
                .category = instance.category,
                .pose = instance.pose,
                .x = instance.x,
                .y = instance.y,
                .scale = instance.scale,
                .rotation = instance.rotation,
                .depth = instance.depth,
            });
        }
        const auto internalCanvas = impl_->rasterizedScene(
            state, imagined, fnv1a("PROSPECTION:" + imagined.name));
        observed = impl_->expose(
            &internalCanvas,
            "IMAGINED:T" + std::to_string(step + 1U) + ':'
                + Impl::actionLabel(actions[step]),
            ImaginationStage::ProspectiveImagination,
            fnv1a("PROSPECTION:" + initial.name), true);
        impl_->trace.back().prospectiveStep = step + 1U;
    }

    SceneDescription finalScene = initial;
    finalScene.name = initial.name + ":PREDICTED";
    finalScene.objects.clear();
    for (const auto& instance : state) {
        finalScene.objects.push_back(SceneObjectCue{
            .instance = instance.instance,
            .category = instance.category,
            .pose = instance.pose,
            .x = instance.x,
            .y = instance.y,
            .scale = instance.scale,
            .rotation = instance.rotation,
            .depth = instance.depth,
        });
    }
    const std::uint64_t seed = variationSeed != 0U
        ? variationSeed : fnv1a("PROSPECTIVE-PAINT:" + initial.name);
    ObserveResult finalObservation = observed;
    const bool painted = impl_->paintScene(
        state, finalScene, ImaginationStage::ProspectiveImagination,
        seed, finalObservation);
    const auto finalRaster = impl_->rasterizedScene(state, finalScene, seed);
    const double novelty = 1.0 - initialCanvas.similarity(finalRaster);
    auto report = impl_->finishReport(
        ImaginationStage::ProspectiveImagination,
        painted && learnedEffects == actions.size(),
        cumulativeConfidence, novelty,
        representedAssembly(finalObservation), {}, false);
    report.sceneInstances = state;
    report.sceneRelations = finalScene.relations;
    report.prospectiveStates = std::move(timeline);
    report.sceneMetrics = impl_->sceneMetricsFor(state, finalScene.relations);
    report.sceneMetrics.learnedEffectsUsed = learnedEffects;
    report.sceneMetrics.predictionConfidence = cumulativeConfidence;
    report.sceneMetrics.predictionError = cumulativeError
        / static_cast<double>(actions.size());
    impl_->last = report;
    return report;
}

const VisualCanvas& VisualImagination::canvas() const noexcept { return impl_->canvas; }

void VisualImagination::resetCanvas() {
    impl_->resetTrace();
    impl_->canvas.clear();
    impl_->lastCanvasMean = 0.0;
}

std::size_t VisualImagination::visualEngramCount() const noexcept {
    return impl_->engrams.size();
}

std::size_t VisualImagination::symbolEngramCount() const noexcept {
    return impl_->symbols.size();
}

std::vector<std::string> VisualImagination::visualConceptNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->engrams.size());
    for (const auto& engram : impl_->engrams) {
        if (!engram.label.empty()) names.push_back(engram.label);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::vector<std::string> VisualImagination::symbolNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->symbols.size());
    for (const auto& [symbol, associations] : impl_->symbols) {
        if (!symbol.empty() && !associations.empty()) names.push_back(symbol);
    }
    return names;
}

std::vector<std::string> VisualImagination::categoryNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->categories.size());
    for (const auto& category : impl_->categories) {
        if (!category.name.empty()) names.push_back(category.name);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::size_t VisualImagination::categoryEngramCount() const noexcept {
    return impl_->categories.size();
}

std::size_t VisualImagination::poseEngramCount() const noexcept {
    return impl_->poses.size();
}

std::size_t VisualImagination::relationEngramCount() const noexcept {
    return impl_->relations.size();
}

std::size_t VisualImagination::actionEngramCount() const noexcept {
    return impl_->actions.size();
}

std::size_t VisualImagination::objectEngramCount() const noexcept {
    return impl_->objectEngrams.size();
}

const ImaginationReport& VisualImagination::lastReport() const noexcept {
    return impl_->last;
}

TargetSpaceVisualRender VisualImagination::renderLastTargetSpace(
    std::size_t width,
    std::size_t height) const {
    return impl_->renderLastTargetSpace(width, height);
}

NativeVisualRender VisualImagination::renderLastNative(
    std::size_t width,
    std::size_t height) const {
    return renderLastTargetSpace(width, height);
}

void VisualImagination::rememberSourceResolution(
    std::size_t width, std::size_t height) {
    impl_->rememberSourceResolution(width, height);
}

std::vector<SourceResolutionMemory> VisualImagination::sourceResolutionMemory() const {
    std::vector<SourceResolutionMemory> result;
    result.reserve(impl_->sourceResolutions.size());
    for (const auto& value : impl_->sourceResolutions) {
        result.push_back(SourceResolutionMemory{
            .width = value.width,
            .height = value.height,
            .observations = value.observations,
        });
    }
    return result;
}

RobotMind& VisualImagination::mind() noexcept { return impl_->mind; }
const RobotMind& VisualImagination::mind() const noexcept { return impl_->mind; }

void VisualImagination::saveSnapshot(const std::filesystem::path& directory) const {
    const auto parent = directory.parent_path().empty()
        ? std::filesystem::current_path() : directory.parent_path();
    std::filesystem::create_directories(parent);
    const auto temporary = parent / (directory.filename().string() + ".imaginatio.tmp");
    const auto backup = parent / (directory.filename().string() + ".imaginatio.bak");
    std::error_code error;
    std::filesystem::remove_all(temporary, error);
    std::filesystem::create_directories(temporary);

    impl_->mind.saveSnapshot(temporary / "mind");
    std::ofstream output(temporary / "imaginatio.bin", std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create TATARUS IMAGINATIO snapshot");
    output.write(kImaginatioMagicV14.data(), static_cast<std::streamsize>(kImaginatioMagicV14.size()));
    const auto width = static_cast<std::uint64_t>(impl_->config.width);
    const auto height = static_cast<std::uint64_t>(impl_->config.height);
    writePod(output, width);
    writePod(output, height);
    writePod(output, impl_->timestamp);
    writePod(output, impl_->nextEngramId);

    // V14 does not serialize the live working canvas at all. Only actuator
    // state is persisted; the visible image must be regenerated from memory.
    const auto cursorX = static_cast<std::uint64_t>(impl_->canvas.cursorX());
    const auto cursorY = static_cast<std::uint64_t>(impl_->canvas.cursorY());
    const auto radius = static_cast<std::uint64_t>(impl_->canvas.brushRadius());
    const auto brush = impl_->canvas.brushColor();
    writePod(output, cursorX);
    writePod(output, cursorY);
    writePod(output, radius);
    writePod(output, brush[0]);
    writePod(output, brush[1]);
    writePod(output, brush[2]);

    const auto engramCount = static_cast<std::uint64_t>(impl_->engrams.size());
    writePod(output, engramCount);
    for (const auto& engram : impl_->engrams) {
        writePod(output, engram.id);
        writeString(output, engram.label);
        writePod(output, engram.contentHash);
        for (const float value : engram.fingerprint) writePod(output, value);
        for (const auto& point : engram.shape) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& region : engram.featureModel) {
            for (const double value : region) writePod(output, value);
        }
        const auto assemblyCount = static_cast<std::uint64_t>(engram.assemblies.size());
        writePod(output, assemblyCount);
        for (const auto assembly : engram.assemblies) writePod(output, assembly);
        writePod(output, engram.observations);
        writePod(output, engram.traceQuality);
        const auto regionCount = static_cast<std::uint64_t>(engram.regions.size());
        writePod(output, regionCount);
        for (const auto& token : engram.regions) {
            writePod(output, token.centreX);
            writePod(output, token.centreY);
            writePod(output, token.sigmaX);
            writePod(output, token.sigmaY);
            for (const float value : token.rgb) writePod(output, value);
            writePod(output, token.contrast);
            writePod(output, token.salience);
        }
        const auto strokeCount = static_cast<std::uint64_t>(engram.strokes.size());
        writePod(output, strokeCount);
        for (const auto& token : engram.strokes) {
            writePod(output, token.x);
            writePod(output, token.y);
            writePod(output, token.dx);
            writePod(output, token.dy);
            writePod(output, token.length);
            writePod(output, token.width);
            for (const float value : token.rgb) writePod(output, value);
            writePod(output, token.intensity);
            writePod(output, token.role);
        }

        writePod(output, engram.selfMotorMemoryValid);
        writePod(output, engram.selfAssembly);
        writePod(output, engram.selfConsolidations);
        writePod(output, engram.selfSimilarity);
        for (const float value : engram.selfFingerprint) writePod(output, value);
        for (const auto& point : engram.selfShape) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& region : engram.selfFeatureModel) {
            for (const double value : region) writePod(output, value);
        }

        // Compact 7-byte motor-stroke stream: uint32 pixel position + RGB8.
        // The bytes describe TATARUS' own executed painting, never a retained
        // teacher patch. Write in one block to keep 512x512 corpora practical.
        const auto motorStrokeCount = static_cast<std::uint64_t>(engram.selfMotorMemory.size());
        writePod(output, motorStrokeCount);
        std::vector<std::uint8_t> motorBytes;
        if (motorStrokeCount > 0U) {
            if (motorStrokeCount > std::numeric_limits<std::size_t>::max() / 7U) {
                throw std::runtime_error("IMAGINATIO motor memory is too large to serialize");
            }
            motorBytes.reserve(static_cast<std::size_t>(motorStrokeCount) * 7U);
            for (const auto& stroke : engram.selfMotorMemory) {
                for (unsigned int shift = 0U; shift < 4U; ++shift) {
                    motorBytes.push_back(static_cast<std::uint8_t>(
                        (stroke.pixelIndex >> (shift * 8U)) & 0xffU));
                }
                motorBytes.push_back(stroke.red);
                motorBytes.push_back(stroke.green);
                motorBytes.push_back(stroke.blue);
            }
            output.write(
                reinterpret_cast<const char*>(motorBytes.data()),
                static_cast<std::streamsize>(motorBytes.size()));
            if (!output) {
                throw std::runtime_error("Cannot write TATARUS self motor memory");
            }
        }
    }

    const auto symbolCount = static_cast<std::uint64_t>(impl_->symbols.size());
    writePod(output, symbolCount);
    for (const auto& [symbol, associations] : impl_->symbols) {
        writeString(output, symbol);
        const auto associationCount = static_cast<std::uint64_t>(associations.size());
        writePod(output, associationCount);
        for (const auto& association : associations) {
            writePod(output, association.engramId);
            writePod(output, association.observations);
            writePod(output, association.strength);
        }
    }
    const auto categoryCount = static_cast<std::uint64_t>(impl_->categories.size());
    writePod(output, categoryCount);
    for (const auto& category : impl_->categories) {
        writeString(output, category.name);
        writePod(output, category.observations);
        for (const auto& point : category.shapeMean) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& point : category.shapeM2) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& region : category.featureMean) {
            for (const double value : region) writePod(output, value);
        }
        for (const auto& region : category.featureM2) {
            for (const double value : region) writePod(output, value);
        }
        const auto assemblyCount = static_cast<std::uint64_t>(category.assemblies.size());
        writePod(output, assemblyCount);
        for (const auto assembly : category.assemblies) writePod(output, assembly);
    }

    // V9: persistent object identity memory. Unlike category memory, these
    // engrams do not require labels: they encode repeated perceptual identity
    // and can later acquire semantic votes through category experience.
    writePod(output, impl_->nextObjectEngramId);
    const auto objectEngramCount = static_cast<std::uint64_t>(impl_->objectEngrams.size());
    writePod(output, objectEngramCount);
    for (const auto& memory : impl_->objectEngrams) {
        writePod(output, memory.id);
        writePod(output, memory.observations);
        for (const auto& region : memory.featureMean) {
            for (const double value : region) writePod(output, value);
        }
        for (const auto& region : memory.featureM2) {
            for (const double value : region) writePod(output, value);
        }
        const auto voteCount = static_cast<std::uint64_t>(memory.categoryVotes.size());
        writePod(output, voteCount);
        for (const auto& [name, votes] : memory.categoryVotes) {
            writeString(output, name);
            writePod(output, votes);
        }
    }

    const auto poseCount = static_cast<std::uint64_t>(impl_->poses.size());
    writePod(output, poseCount);
    for (const auto& pose : impl_->poses) {
        writeString(output, pose.category);
        writeString(output, pose.pose);
        writePod(output, pose.observations);
        for (const auto& point : pose.shapeMean) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& point : pose.shapeM2) {
            writePod(output, point[0]);
            writePod(output, point[1]);
        }
        for (const auto& region : pose.featureMean) {
            for (const double value : region) writePod(output, value);
        }
        for (const auto& region : pose.featureM2) {
            for (const double value : region) writePod(output, value);
        }
        const auto sourceCount = static_cast<std::uint64_t>(pose.sourceEngrams.size());
        writePod(output, sourceCount);
        for (const auto source : pose.sourceEngrams) writePod(output, source);
        const auto assemblyCount = static_cast<std::uint64_t>(pose.assemblies.size());
        writePod(output, assemblyCount);
        for (const auto assembly : pose.assemblies) writePod(output, assembly);
    }

    const auto relationCount = static_cast<std::uint64_t>(impl_->relations.size());
    writePod(output, relationCount);
    for (const auto& relation : impl_->relations) {
        writePod(output, relation.relation);
        writeString(output, relation.subjectCategory);
        writeString(output, relation.objectCategory);
        writePod(output, relation.observations);
        for (const double value : relation.mean) writePod(output, value);
        for (const double value : relation.m2) writePod(output, value);
        const auto assemblyCount = static_cast<std::uint64_t>(relation.assemblies.size());
        writePod(output, assemblyCount);
        for (const auto assembly : relation.assemblies) writePod(output, assembly);
    }

    const auto actionCount = static_cast<std::uint64_t>(impl_->actions.size());
    writePod(output, actionCount);
    for (const auto& action : impl_->actions) {
        writePod(output, action.kind);
        writeString(output, action.label);
        writeString(output, action.actorCategory);
        writeString(output, action.targetCategory);
        writePod(output, action.observations);
        for (const double value : action.actorMean) writePod(output, value);
        for (const double value : action.actorM2) writePod(output, value);
        for (const double value : action.targetMean) writePod(output, value);
        for (const double value : action.targetM2) writePod(output, value);
        writeString(output, action.actorPoseAfter);
        writeString(output, action.targetPoseAfter);
        const auto assemblyCount = static_cast<std::uint64_t>(action.assemblies.size());
        writePod(output, assemblyCount);
        for (const auto assembly : action.assemblies) writePod(output, assembly);
    }

    const auto sceneInstanceCount = static_cast<std::uint64_t>(
        impl_->sceneMemoryInstances.size());
    writePod(output, sceneInstanceCount);
    for (const auto& instance : impl_->sceneMemoryInstances) {
        writePod(output, instance.instanceId);
        writeString(output, instance.instance);
        writeString(output, instance.category);
        writeString(output, instance.pose);
        writePod(output, instance.x);
        writePod(output, instance.y);
        writePod(output, instance.scale);
        writePod(output, instance.rotation);
        writePod(output, instance.depth);
        for (const double value : instance.occupiedRegion) writePod(output, value);
        writePod(output, instance.assemblyId);
        writePod(output, instance.sourceEngramId);
        writePod(output, instance.confidence);
    }
    const auto sceneRelationCount = static_cast<std::uint64_t>(
        impl_->sceneMemoryRelations.size());
    writePod(output, sceneRelationCount);
    for (const auto& relation : impl_->sceneMemoryRelations) {
        writeString(output, relation.subject);
        writePod(output, relation.relation);
        writeString(output, relation.object);
        writePod(output, relation.confidence);
    }

    // V10: persistent source-dimension context. Neural vision remains
    // normalized; this stores only width, height and observation count, never
    // source pixels or detail unavailable to the working retina.
    const auto resolutionCount = static_cast<std::uint64_t>(impl_->sourceResolutions.size());
    writePod(output, resolutionCount);
    for (const auto& resolution : impl_->sourceResolutions) {
        const auto sourceWidth = static_cast<std::uint64_t>(resolution.width);
        const auto sourceHeight = static_cast<std::uint64_t>(resolution.height);
        writePod(output, sourceWidth);
        writePod(output, sourceHeight);
        writePod(output, resolution.observations);
    }
    output.close();
    if (!output) throw std::runtime_error("Cannot finish TATARUS IMAGINATIO snapshot");

    std::uint64_t totalSelfMotorStrokes = 0U;
    for (const auto& engram : impl_->engrams) {
        const auto count = static_cast<std::uint64_t>(engram.selfMotorMemory.size());
        if (count > std::numeric_limits<std::uint64_t>::max() - totalSelfMotorStrokes) {
            throw std::runtime_error("IMAGINATIO self motor stroke count overflow");
        }
        totalSelfMotorStrokes += count;
    }

    std::ofstream manifest(temporary / "manifest.txt", std::ios::trunc);
    if (!manifest) throw std::runtime_error("Cannot create TATARUS IMAGINATIO manifest");
    manifest << "format=TATARUS_VISUAL_IMAGINATION_NEURAL_PAINTING\n"
             << "version=14\n"
             << "color_space=sRGB\n"
             << "color_depth=24-bit\n"
             << "paint_patch=" << impl_->config.paintPatchSide << 'x'
                 << impl_->config.paintPatchSide << "\n"
             << "canvas=" << width << 'x' << height << "\n"
             << "visual_engrams=" << engramCount << "\n"
             << "visual_memory_storage=self-consolidated-neural-motor-engram\n"
             << "teacher_trace_storage=transient-only\n"
             << "source_raster_retention=none\n"
             << "working_canvas_persisted=false\n"
             << "self_imprint=retina-v1-ventral-motor-coupled\n"
             << "self_motor_memory=scalar-rgb8-motor-strokes\n"
             << "self_motor_strokes=" << totalSelfMotorStrokes << "\n"
             << "symbol_engrams=" << symbolCount << "\n";
    manifest << "category_engrams=" << categoryCount << "\n"
             << "fusion_points=5\n"
             << "form_model=five-field-structural-morphology\n"
             << "feature_model=hierarchical-object-part-running-statistics\n"
             << "detail_policy=factorized-soft-region-recombination\n"
             << "object_engrams=" << objectEngramCount << "\n"
             << "object_perception=retina-v1-segmentation-foveation-ventral-memory\n"
             << "source_resolution_engrams=" << resolutionCount << "\n"
             << "source_resolution_policy=dimension-only-context-with-normalized-retina\n"
             << "pose_engrams=" << poseCount << "\n"
             << "relation_engrams=" << relationCount << "\n"
             << "action_engrams=" << actionCount << "\n"
             << "scene_instances=" << sceneInstanceCount << "\n"
             << "scene_model=relational-object-memory\n"
             << "prospection_model=learned-action-state-transition\n";
    manifest.close();

    std::filesystem::remove_all(backup, error);
    if (std::filesystem::exists(directory)) {
        std::filesystem::rename(directory, backup, error);
        if (error) throw std::runtime_error("Cannot back up previous IMAGINATIO snapshot: " + error.message());
    }
    error.clear();
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        if (std::filesystem::exists(backup)) {
            std::error_code restoreError;
            std::filesystem::rename(backup, directory, restoreError);
        }
        throw std::runtime_error("Cannot publish IMAGINATIO snapshot: " + error.message());
    }
    std::filesystem::remove_all(backup, error);
}

bool VisualImagination::loadSnapshot(const std::filesystem::path& directory) {
    const auto statePath = directory / "imaginatio.bin";
    const auto mindPath = directory / "mind";
    if (!std::filesystem::exists(statePath) || !std::filesystem::is_directory(mindPath)) {
        return false;
    }
    if (!impl_->mind.loadSnapshot(mindPath)) return false;
    std::ifstream input(statePath, std::ios::binary);
    if (!input) return false;
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    const bool legacyV1 = magic == kImaginatioMagicV1;
    const bool legacyV2 = magic == kImaginatioMagicV2;
    const bool snapshotV3 = magic == kImaginatioMagicV3;
    const bool snapshotV4 = magic == kImaginatioMagicV4;
    const bool snapshotV5 = magic == kImaginatioMagicV5;
    const bool snapshotV7 = magic == kImaginatioMagicV7;
    const bool snapshotV8 = magic == kImaginatioMagicV8;
    const bool snapshotV9 = magic == kImaginatioMagicV9;
    const bool snapshotV10 = magic == kImaginatioMagicV10;
    const bool snapshotV11 = magic == kImaginatioMagicV11;
    const bool snapshotV12 = magic == kImaginatioMagicV12;
    const bool snapshotV13 = magic == kImaginatioMagicV13;
    const bool snapshotV14 = magic == kImaginatioMagicV14;
    const bool hasPatchPayload = snapshotV3 || snapshotV4 || snapshotV5
        || snapshotV7 || snapshotV8 || snapshotV9 || snapshotV10 || snapshotV11;
    if (!legacyV1 && !legacyV2 && !snapshotV3 && !snapshotV4 && !snapshotV5
        && !snapshotV7 && !snapshotV8 && !snapshotV9 && !snapshotV10
        && !snapshotV11 && !snapshotV12 && !snapshotV13 && !snapshotV14) {
        throw std::runtime_error("Unsupported TATARUS IMAGINATIO snapshot format");
    }
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    readPod(input, width);
    readPod(input, height);
    if (width < 2U || height < 2U || width > 1024U || height > 1024U
        || width > 1'048'576U / height) {
        throw std::runtime_error("IMAGINATIO snapshot canvas dimensions are invalid");
    }
    readPod(input, impl_->timestamp);
    readPod(input, impl_->nextEngramId);

    const auto readCanvas = [&input, width, height, legacyV1]() {
        VisualCanvas canvas(static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        std::uint64_t pixelCount = 0;
        readPod(input, pixelCount);
        if (pixelCount != width * height || pixelCount > kMaximumSnapshotItems) {
            throw std::runtime_error("IMAGINATIO snapshot has invalid canvas storage");
        }
        for (std::size_t index = 0; index < static_cast<std::size_t>(pixelCount); ++index) {
            if (legacyV1) {
                double pixel = 0.0;
                readPod(input, pixel);
                canvas.setPixel(index % static_cast<std::size_t>(width),
                    index / static_cast<std::size_t>(width), pixel);
            } else {
                double red = 0.0;
                double green = 0.0;
                double blue = 0.0;
                readPod(input, red);
                readPod(input, green);
                readPod(input, blue);
                canvas.setColorPixel(index % static_cast<std::size_t>(width),
                    index / static_cast<std::size_t>(width), red, green, blue);
            }
        }
        std::uint64_t cursorX = 0;
        std::uint64_t cursorY = 0;
        std::uint64_t radius = 0;
        readPod(input, cursorX);
        readPod(input, cursorY);
        readPod(input, radius);
        std::array<double, 3> brush{1.0, 1.0, 1.0};
        if (legacyV1) {
            readPod(input, brush[0]);
            brush[1] = brush[0];
            brush[2] = brush[0];
        } else {
            readPod(input, brush[0]);
            readPod(input, brush[1]);
            readPod(input, brush[2]);
        }
        if (cursorX >= width || cursorY >= height || radius > 4
            || !std::isfinite(brush[0]) || !std::isfinite(brush[1])
            || !std::isfinite(brush[2])) {
            throw std::runtime_error("IMAGINATIO snapshot has invalid brush state");
        }
        canvas.setCursor(static_cast<std::size_t>(cursorX), static_cast<std::size_t>(cursorY));
        if (radius > 0) {
            canvas.apply(PaintAction{
                .kind = PaintActionKind::BrushLarge,
                .repetitions = static_cast<std::uint16_t>(radius),
            });
        }
        canvas.setBrushColor(brush[0], brush[1], brush[2]);
        return canvas;
    };
    if (snapshotV14) {
        impl_->canvas = VisualCanvas(
            static_cast<std::size_t>(width), static_cast<std::size_t>(height));
        impl_->canvas.clear();
        std::uint64_t cursorX = 0U;
        std::uint64_t cursorY = 0U;
        std::uint64_t radius = 0U;
        std::array<double, 3U> brush{1.0, 1.0, 1.0};
        readPod(input, cursorX);
        readPod(input, cursorY);
        readPod(input, radius);
        readPod(input, brush[0]);
        readPod(input, brush[1]);
        readPod(input, brush[2]);
        if (cursorX >= width || cursorY >= height || radius > 4U
            || !std::isfinite(brush[0]) || !std::isfinite(brush[1])
            || !std::isfinite(brush[2])) {
            throw std::runtime_error("IMAGINATIO V14 snapshot has invalid actuator state");
        }
        impl_->canvas.setCursor(
            static_cast<std::size_t>(cursorX), static_cast<std::size_t>(cursorY));
        if (radius > 0U) {
            impl_->canvas.apply(PaintAction{
                .kind = PaintActionKind::BrushLarge,
                .repetitions = static_cast<std::uint16_t>(radius),
            });
        }
        impl_->canvas.setBrushColor(brush[0], brush[1], brush[2]);
    } else {
        impl_->canvas = readCanvas();
    }

    std::uint64_t engramCount = 0;
    readPod(input, engramCount);
    if (engramCount > impl_->config.maximumEngrams) {
        throw std::runtime_error("IMAGINATIO snapshot contains too many visual engrams");
    }
    std::vector<Impl::VisualEngram> restoredEngrams;
    restoredEngrams.reserve(static_cast<std::size_t>(engramCount));
    for (std::uint64_t i = 0; i < engramCount; ++i) {
        Impl::VisualEngram engram;
        readPod(input, engram.id);
        engram.label = readString(input);

        if (snapshotV12 || snapshotV13 || snapshotV14) {
            readPod(input, engram.contentHash);
            for (float& value : engram.fingerprint) {
                readPod(input, value);
                if (!std::isfinite(value)) {
                    throw std::runtime_error("IMAGINATIO V12 fingerprint contains a non-finite value");
                }
            }
            for (auto& point : engram.shape) {
                readPod(input, point[0]);
                readPod(input, point[1]);
            }
            engram.shape = Impl::constrainShape(engram.shape);
            for (auto& region : engram.featureModel) {
                for (double& value : region) {
                    readPod(input, value);
                    if (!std::isfinite(value)) {
                        throw std::runtime_error("IMAGINATIO V12 feature memory contains a non-finite value");
                    }
                }
            }
        } else {
            VisualCanvas legacyPrototype(static_cast<std::size_t>(width),
                                         static_cast<std::size_t>(height));
            if (snapshotV11) {
                bool compactPrototype = false;
                readPod(input, compactPrototype);
                if (compactPrototype) {
                    std::uint64_t byteCount = 0;
                    readPod(input, byteCount);
                    if (byteCount != width * height * 3U || byteCount > kMaximumSnapshotItems) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot has invalid compact prototype storage");
                    }
                    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
                    input.read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                    if (!input) {
                        throw std::runtime_error("IMAGINATIO compact prototype is truncated");
                    }
                    for (std::size_t pixel = 0; pixel < legacyPrototype.pixels().size(); ++pixel) {
                        legacyPrototype.setColorPixel(
                            pixel % legacyPrototype.width(), pixel / legacyPrototype.width(),
                            static_cast<double>(bytes[pixel * 3U]) / 255.0,
                            static_cast<double>(bytes[pixel * 3U + 1U]) / 255.0,
                            static_cast<double>(bytes[pixel * 3U + 2U]) / 255.0);
                    }
                } else {
                    legacyPrototype = readCanvas();
                }
                bool legacyRasterScanProgram = false;
                readPod(input, legacyRasterScanProgram);
                static_cast<void>(legacyRasterScanProgram);
            } else {
                legacyPrototype = readCanvas();
            }

            std::uint64_t programSize = 0;
            readPod(input, programSize);
            if (programSize > kMaximumSnapshotItems) {
                throw std::runtime_error("IMAGINATIO snapshot motor trace is implausibly large");
            }
            // Consume legacy teacher/motor data only for migration. It is never
            // retained in the V14 organism state.
            for (std::uint64_t actionIndex = 0; actionIndex < programSize; ++actionIndex) {
                PaintAction action;
                readPod(input, action.kind);
                readPod(input, action.repetitions);
                readPod(input, action.intensity);
                if (!legacyV1) {
                    readPod(input, action.red);
                    readPod(input, action.green);
                    readPod(input, action.blue);
                }
                if (hasPatchPayload) {
                    readPod(input, action.patchWidth);
                    readPod(input, action.patchHeight);
                    if (action.patchWidth > 8U || action.patchHeight > 8U
                        || ((action.patchWidth == 0U) != (action.patchHeight == 0U))) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot contains an invalid pigment patch");
                    }
                    const std::size_t patchBytes = static_cast<std::size_t>(action.patchWidth)
                        * static_cast<std::size_t>(action.patchHeight) * 3U;
                    if (patchBytes > 0U) {
                        input.read(reinterpret_cast<char*>(action.patchRgb.data()),
                            static_cast<std::streamsize>(patchBytes));
                        if (!input) {
                            throw std::runtime_error("IMAGINATIO snapshot patch is truncated");
                        }
                    }
                }
            }

            std::uint64_t markCount = 0;
            readPod(input, markCount);
            if (markCount > kMaximumSnapshotItems) {
                throw std::runtime_error("IMAGINATIO snapshot mark trace is implausibly large");
            }
            for (std::uint64_t markIndex = 0; markIndex < markCount; ++markIndex) {
                Impl::MotorMark mark;
                readPod(input, mark.normalizedX);
                readPod(input, mark.normalizedY);
                readPod(input, mark.intensity);
                if (legacyV1) {
                    mark.red = mark.intensity;
                    mark.green = mark.intensity;
                    mark.blue = mark.intensity;
                    mark.intensity = 1.0;
                } else {
                    readPod(input, mark.red);
                    readPod(input, mark.green);
                    readPod(input, mark.blue);
                }
                readPod(input, mark.erase);
                if (hasPatchPayload) {
                    readPod(input, mark.patchWidth);
                    readPod(input, mark.patchHeight);
                    if (mark.patchWidth > 8U || mark.patchHeight > 8U
                        || ((mark.patchWidth == 0U) != (mark.patchHeight == 0U))) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot contains an invalid mark patch");
                    }
                    const std::size_t patchBytes = static_cast<std::size_t>(mark.patchWidth)
                        * static_cast<std::size_t>(mark.patchHeight) * 3U;
                    if (patchBytes > 0U) {
                        input.read(reinterpret_cast<char*>(mark.patchRgb.data()),
                            static_cast<std::streamsize>(patchBytes));
                        if (!input) {
                            throw std::runtime_error("IMAGINATIO snapshot mark patch is truncated");
                        }
                    }
                }
            }

            // One-way migration: extract only bounded neural/structural memory,
            // then allow the legacy raster and pigment buffers to die here.
            engram.contentHash = Impl::visualContentHash(legacyPrototype);
            engram.fingerprint = Impl::visualFingerprint(legacyPrototype);
            engram.shape = impl_->extractShape(legacyPrototype);
            engram.featureModel = impl_->extractFeatureModel(legacyPrototype);
            engram.regions = impl_->extractRegionTokens(legacyPrototype);
            engram.strokes = impl_->extractStrokeTokens(legacyPrototype);
        }

        std::uint64_t assemblyCount = 0;
        readPod(input, assemblyCount);
        if (assemblyCount > kMaximumSnapshotItems) {
            throw std::runtime_error("IMAGINATIO snapshot has too many assembly bindings");
        }
        engram.assemblies.resize(static_cast<std::size_t>(assemblyCount));
        for (auto& assembly : engram.assemblies) readPod(input, assembly);
        readPod(input, engram.observations);
        readPod(input, engram.traceQuality);
        if (engram.id == 0U || engram.observations == 0U
            || !std::isfinite(engram.traceQuality)) {
            throw std::runtime_error("IMAGINATIO snapshot contains an invalid visual engram");
        }
        if (snapshotV13 || snapshotV14) {
            std::uint64_t regionCount = 0;
            readPod(input, regionCount);
            if (regionCount > 4096U) {
                throw std::runtime_error("IMAGINATIO snapshot has too many region tokens");
            }
            engram.regions.resize(static_cast<std::size_t>(regionCount));
            for (auto& token : engram.regions) {
                readPod(input, token.centreX);
                readPod(input, token.centreY);
                readPod(input, token.sigmaX);
                readPod(input, token.sigmaY);
                for (float& value : token.rgb) readPod(input, value);
                readPod(input, token.contrast);
                readPod(input, token.salience);
            }
            std::uint64_t strokeCount = 0;
            readPod(input, strokeCount);
            if (strokeCount > 32768U) {
                throw std::runtime_error("IMAGINATIO snapshot has too many stroke tokens");
            }
            engram.strokes.resize(static_cast<std::size_t>(strokeCount));
            for (auto& token : engram.strokes) {
                readPod(input, token.x);
                readPod(input, token.y);
                readPod(input, token.dx);
                readPod(input, token.dy);
                readPod(input, token.length);
                readPod(input, token.width);
                for (float& value : token.rgb) readPod(input, value);
                readPod(input, token.intensity);
                readPod(input, token.role);
            }
        } else if (snapshotV12) {
            const auto synthesized = impl_->synthesizeAbstractMemory(
                engram.shape,
                engram.featureModel,
                nullptr,
                engram.observations,
                mix64(engram.contentHash != 0U ? engram.contentHash : engram.id));
            engram.regions = impl_->extractRegionTokens(synthesized);
            engram.strokes = impl_->extractStrokeTokens(synthesized);
        }

        if (snapshotV14) {
            readPod(input, engram.selfMotorMemoryValid);
            readPod(input, engram.selfAssembly);
            readPod(input, engram.selfConsolidations);
            readPod(input, engram.selfSimilarity);
            if (!std::isfinite(engram.selfSimilarity)
                || engram.selfSimilarity < 0.0 || engram.selfSimilarity > 1.0) {
                throw std::runtime_error("IMAGINATIO V14 snapshot has invalid self similarity");
            }
            for (float& value : engram.selfFingerprint) {
                readPod(input, value);
                if (!std::isfinite(value)) {
                    throw std::runtime_error("IMAGINATIO V14 snapshot has invalid self fingerprint");
                }
            }
            for (auto& point : engram.selfShape) {
                readPod(input, point[0]);
                readPod(input, point[1]);
            }
            engram.selfShape = Impl::constrainShape(engram.selfShape);
            for (auto& region : engram.selfFeatureModel) {
                for (double& value : region) {
                    readPod(input, value);
                    if (!std::isfinite(value)) {
                        throw std::runtime_error("IMAGINATIO V14 snapshot has invalid self feature memory");
                    }
                }
            }
            std::uint64_t motorStrokeCount = 0U;
            readPod(input, motorStrokeCount);
            const std::uint64_t maximumMotorStrokes = width * height;
            if (motorStrokeCount > maximumMotorStrokes
                || motorStrokeCount > kMaximumSnapshotItems) {
                throw std::runtime_error("IMAGINATIO V14 snapshot has too many self motor strokes");
            }
            if (motorStrokeCount > 0U) {
                if (motorStrokeCount > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max() / 7U)) {
                    throw std::runtime_error("IMAGINATIO V14 motor memory byte count overflows size_t");
                }
                const std::size_t byteCount = static_cast<std::size_t>(motorStrokeCount) * 7U;
                std::vector<std::uint8_t> motorBytes(byteCount);
                input.read(
                    reinterpret_cast<char*>(motorBytes.data()),
                    static_cast<std::streamsize>(motorBytes.size()));
                if (!input) {
                    throw std::runtime_error("IMAGINATIO V14 self motor memory is truncated");
                }
                engram.selfMotorMemory.reserve(static_cast<std::size_t>(motorStrokeCount));
                std::uint32_t previousIndex = 0U;
                bool firstStroke = true;
                for (std::uint64_t strokeIndex = 0U;
                     strokeIndex < motorStrokeCount; ++strokeIndex) {
                    const std::size_t offset = static_cast<std::size_t>(strokeIndex) * 7U;
                    std::uint32_t pixelIndex = 0U;
                    for (unsigned int shift = 0U; shift < 4U; ++shift) {
                        pixelIndex |= static_cast<std::uint32_t>(motorBytes[offset + shift])
                            << (shift * 8U);
                    }
                    if (static_cast<std::uint64_t>(pixelIndex) >= maximumMotorStrokes) {
                        throw std::runtime_error("IMAGINATIO V14 motor stroke position is out of range");
                    }
                    if (!firstStroke && pixelIndex <= previousIndex) {
                        throw std::runtime_error("IMAGINATIO V14 motor strokes are not strictly ordered");
                    }
                    firstStroke = false;
                    previousIndex = pixelIndex;
                    engram.selfMotorMemory.push_back(Impl::NeuralMotorStroke{
                        .pixelIndex = pixelIndex,
                        .red = motorBytes[offset + 4U],
                        .green = motorBytes[offset + 5U],
                        .blue = motorBytes[offset + 6U],
                    });
                }
            }
            if (engram.selfMotorMemoryValid && engram.selfConsolidations == 0U) {
                throw std::runtime_error("IMAGINATIO V14 self motor memory lacks consolidation evidence");
            }
        }
        restoredEngrams.push_back(std::move(engram));
    }

    std::uint64_t symbolCount = 0;
    readPod(input, symbolCount);
    if (symbolCount > kMaximumSnapshotItems) {
        throw std::runtime_error("IMAGINATIO snapshot has too many symbol engrams");
    }
    std::map<std::string, std::vector<Impl::SymbolAssociation>> restoredSymbols;
    for (std::uint64_t i = 0; i < symbolCount; ++i) {
        auto symbol = readString(input);
        std::uint64_t associationCount = 0;
        readPod(input, associationCount);
        if (associationCount > kMaximumSnapshotItems) {
            throw std::runtime_error("IMAGINATIO snapshot has too many symbol associations");
        }
        auto& associations = restoredSymbols[symbol];
        associations.resize(static_cast<std::size_t>(associationCount));
        for (auto& association : associations) {
            readPod(input, association.engramId);
            readPod(input, association.observations);
            readPod(input, association.strength);
            if (!std::isfinite(association.strength)) {
                throw std::runtime_error("IMAGINATIO snapshot has a non-finite association");
            }
        }
    }
    std::vector<Impl::CategoryEngram> restoredCategories;
    if (snapshotV4 || snapshotV5 || snapshotV7 || snapshotV8 || snapshotV9
        || snapshotV10 || snapshotV11 || snapshotV12 || snapshotV13 || snapshotV14) {
        std::uint64_t categoryCount = 0;
        readPod(input, categoryCount);
        if (categoryCount > impl_->config.maximumCategories) {
            throw std::runtime_error("IMAGINATIO snapshot has too many category engrams");
        }
        restoredCategories.reserve(static_cast<std::size_t>(categoryCount));
        for (std::uint64_t index = 0; index < categoryCount; ++index) {
            Impl::CategoryEngram category;
            category.name = readString(input);
            readPod(input, category.observations);

            if (snapshotV12 || snapshotV13 || snapshotV14) {
                for (auto& point : category.shapeMean) {
                    readPod(input, point[0]);
                    readPod(input, point[1]);
                }
                for (auto& point : category.shapeM2) {
                    readPod(input, point[0]);
                    readPod(input, point[1]);
                }
                for (std::size_t point = 0; point < category.shapeMean.size(); ++point) {
                    for (std::size_t axis = 0; axis < 2U; ++axis) {
                        if (!std::isfinite(category.shapeMean[point][axis])
                            || !std::isfinite(category.shapeM2[point][axis])
                            || category.shapeM2[point][axis] < 0.0) {
                            throw std::runtime_error(
                                "IMAGINATIO V12 snapshot has invalid category form memory");
                        }
                    }
                }
                category.shapeMean = Impl::constrainShape(category.shapeMean);

                for (auto& region : category.featureMean) {
                    for (double& value : region) readPod(input, value);
                }
                for (auto& region : category.featureM2) {
                    for (double& value : region) readPod(input, value);
                }
                for (std::size_t region = 0; region < category.featureMean.size(); ++region) {
                    for (std::size_t value = 0;
                         value < category.featureMean[region].size(); ++value) {
                        if (!std::isfinite(category.featureMean[region][value])
                            || !std::isfinite(category.featureM2[region][value])
                            || category.featureM2[region][value] < 0.0) {
                            throw std::runtime_error(
                                "IMAGINATIO V12 snapshot has invalid category feature memory");
                        }
                    }
                }
            } else {
                // V4-V11 carried source-raster-derived category memories. Read
                // them only long enough to migrate their statistics; no raster
                // survives in the restored V14 organism state.
                const auto legacyConsensus = readCanvas();
                static_cast<void>(legacyConsensus);
                std::uint64_t m2Count = 0;
                readPod(input, m2Count);
                if (m2Count != width * height * 3U || m2Count > kMaximumSnapshotItems) {
                    throw std::runtime_error(
                        "IMAGINATIO snapshot has invalid category variance storage");
                }
                std::vector<float> legacyM2(static_cast<std::size_t>(m2Count));
                for (float& value : legacyM2) {
                    readPod(input, value);
                    if (!std::isfinite(value)) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot has non-finite category variance");
                    }
                }

                if (snapshotV5 || snapshotV7 || snapshotV8 || snapshotV9
                    || snapshotV10 || snapshotV11) {
                    for (auto& point : category.shapeMean) {
                        readPod(input, point[0]);
                        readPod(input, point[1]);
                    }
                    for (auto& point : category.shapeM2) {
                        readPod(input, point[0]);
                        readPod(input, point[1]);
                    }
                    for (std::size_t point = 0; point < category.shapeMean.size(); ++point) {
                        for (std::size_t axis = 0; axis < 2U; ++axis) {
                            if (!std::isfinite(category.shapeMean[point][axis])
                                || !std::isfinite(category.shapeM2[point][axis])
                                || category.shapeM2[point][axis] < 0.0) {
                                throw std::runtime_error(
                                    "IMAGINATIO snapshot has invalid category form memory");
                            }
                        }
                    }
                    category.shapeMean = Impl::constrainShape(category.shapeMean);
                }

                if (snapshotV8 || snapshotV9 || snapshotV10 || snapshotV11) {
                    for (auto& region : category.featureMean) {
                        for (double& value : region) readPod(input, value);
                    }
                    for (auto& region : category.featureM2) {
                        for (double& value : region) readPod(input, value);
                    }
                    for (std::size_t region = 0;
                         region < category.featureMean.size(); ++region) {
                        for (std::size_t value = 0;
                             value < category.featureMean[region].size(); ++value) {
                            if (!std::isfinite(category.featureMean[region][value])
                                || !std::isfinite(category.featureM2[region][value])
                                || category.featureM2[region][value] < 0.0) {
                                throw std::runtime_error(
                                    "IMAGINATIO snapshot has invalid category feature memory");
                            }
                        }
                    }
                }

                std::uint64_t anchorCount = 0;
                readPod(input, anchorCount);
                if (anchorCount > impl_->config.categoryAnchorCount) {
                    throw std::runtime_error(
                        "IMAGINATIO snapshot has too many category anchors");
                }
                std::vector<std::vector<std::uint8_t>> legacyAnchors(
                    static_cast<std::size_t>(anchorCount));
                for (auto& anchor : legacyAnchors) {
                    std::uint64_t byteCount = 0;
                    readPod(input, byteCount);
                    if (byteCount != width * height * 3U
                        || byteCount > kMaximumSnapshotItems) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot has invalid category anchor storage");
                    }
                    anchor.resize(static_cast<std::size_t>(byteCount));
                    input.read(reinterpret_cast<char*>(anchor.data()),
                        static_cast<std::streamsize>(anchor.size()));
                    if (!input) {
                        throw std::runtime_error(
                            "IMAGINATIO snapshot category anchor is truncated");
                    }
                }

                if (legacyAnchors.empty()) {
                    throw std::runtime_error(
                        "IMAGINATIO legacy snapshot contains an incomplete category engram");
                }

                if (!snapshotV8 && !snapshotV9 && !snapshotV10 && !snapshotV11) {
                    std::uint64_t featureCount = 0U;
                    for (const auto& anchor : legacyAnchors) {
                        VisualCanvas anchorCanvas(
                            static_cast<std::size_t>(width),
                            static_cast<std::size_t>(height));
                        for (std::size_t pixel = 0;
                             pixel < anchorCanvas.pixels().size(); ++pixel) {
                            anchorCanvas.setColorPixel(
                                pixel % anchorCanvas.width(), pixel / anchorCanvas.width(),
                                static_cast<double>(anchor[pixel * 3U]) / 255.0,
                                static_cast<double>(anchor[pixel * 3U + 1U]) / 255.0,
                                static_cast<double>(anchor[pixel * 3U + 2U]) / 255.0);
                        }
                        const auto sample = impl_->extractFeatureModel(anchorCanvas);
                        ++featureCount;
                        for (std::size_t region = 0;
                             region < category.featureMean.size(); ++region) {
                            for (std::size_t value = 0;
                                 value < category.featureMean[region].size(); ++value) {
                                const double delta = sample[region][value]
                                    - category.featureMean[region][value];
                                category.featureMean[region][value] += delta
                                    / static_cast<double>(featureCount);
                                category.featureM2[region][value] += delta
                                    * (sample[region][value]
                                        - category.featureMean[region][value]);
                            }
                        }
                    }
                    if (featureCount > 1U && category.observations > featureCount) {
                        const double scale = static_cast<double>(category.observations - 1U)
                            / static_cast<double>(featureCount - 1U);
                        for (auto& region : category.featureM2) {
                            for (double& value : region) value *= scale;
                        }
                    }
                }

                if (snapshotV4) {
                    std::vector<Impl::ShapeSignature> legacyShapes;
                    legacyShapes.reserve(legacyAnchors.size());
                    for (const auto& anchor : legacyAnchors) {
                        VisualCanvas anchorCanvas(
                            static_cast<std::size_t>(width),
                            static_cast<std::size_t>(height));
                        for (std::size_t pixel = 0;
                             pixel < anchorCanvas.pixels().size(); ++pixel) {
                            anchorCanvas.setColorPixel(
                                pixel % anchorCanvas.width(), pixel / anchorCanvas.width(),
                                static_cast<double>(anchor[pixel * 3U]) / 255.0,
                                static_cast<double>(anchor[pixel * 3U + 1U]) / 255.0,
                                static_cast<double>(anchor[pixel * 3U + 2U]) / 255.0);
                        }
                        legacyShapes.push_back(impl_->extractShape(anchorCanvas));
                    }
                    for (const auto& shape : legacyShapes) {
                        for (std::size_t point = 0;
                             point < category.shapeMean.size(); ++point) {
                            for (std::size_t axis = 0; axis < 2U; ++axis) {
                                category.shapeMean[point][axis] += shape[point][axis]
                                    / static_cast<double>(legacyShapes.size());
                            }
                        }
                    }
                    if (legacyShapes.size() > 1U && category.observations > 1U) {
                        for (const auto& shape : legacyShapes) {
                            for (std::size_t point = 0;
                                 point < category.shapeMean.size(); ++point) {
                                for (std::size_t axis = 0; axis < 2U; ++axis) {
                                    const double delta = shape[point][axis]
                                        - category.shapeMean[point][axis];
                                    category.shapeM2[point][axis] += delta * delta
                                        * static_cast<double>(category.observations - 1U)
                                        / static_cast<double>(legacyShapes.size() - 1U);
                                }
                            }
                        }
                    }
                    category.shapeMean = Impl::constrainShape(category.shapeMean);
                }
            }

            std::uint64_t assemblyCount = 0;
            readPod(input, assemblyCount);
            if (assemblyCount > kMaximumSnapshotItems) {
                throw std::runtime_error(
                    "IMAGINATIO snapshot has too many category assemblies");
            }
            category.assemblies.resize(static_cast<std::size_t>(assemblyCount));
            for (auto& assembly : category.assemblies) readPod(input, assembly);

            if (category.name.empty() || category.observations == 0U) {
                throw std::runtime_error(
                    "IMAGINATIO snapshot contains an incomplete category engram");
            }

            restoredCategories.push_back(std::move(category));
        }
    }
    std::vector<Impl::ObjectEngram> restoredObjectEngrams;
    std::uint64_t restoredNextObjectEngramId = 1U;
    if (snapshotV9 || snapshotV10 || snapshotV11 || snapshotV12 || snapshotV13 || snapshotV14) {
        readPod(input, restoredNextObjectEngramId);
        std::uint64_t objectCount = 0;
        readPod(input, objectCount);
        if (objectCount > impl_->config.maximumObjectEngrams) {
            throw std::runtime_error("IMAGINATIO snapshot has too many object engrams");
        }
        restoredObjectEngrams.reserve(static_cast<std::size_t>(objectCount));
        for (std::uint64_t index = 0; index < objectCount; ++index) {
            Impl::ObjectEngram memory;
            readPod(input, memory.id);
            readPod(input, memory.observations);
            for (auto& region : memory.featureMean) {
                for (double& value : region) readPod(input, value);
            }
            for (auto& region : memory.featureM2) {
                for (double& value : region) readPod(input, value);
            }
            for (std::size_t region = 0; region < memory.featureMean.size(); ++region) {
                for (std::size_t value = 0; value < memory.featureMean[region].size(); ++value) {
                    if (!std::isfinite(memory.featureMean[region][value])
                        || !std::isfinite(memory.featureM2[region][value])
                        || memory.featureM2[region][value] < 0.0) {
                        throw std::runtime_error("IMAGINATIO snapshot has invalid object memory");
                    }
                }
            }
            std::uint64_t voteCount = 0;
            readPod(input, voteCount);
            if (voteCount > impl_->config.maximumCategories) {
                throw std::runtime_error("IMAGINATIO snapshot has too many object category votes");
            }
            for (std::uint64_t vote = 0; vote < voteCount; ++vote) {
                auto name = readString(input);
                std::uint64_t observations = 0;
                readPod(input, observations);
                if (name.empty() || observations == 0U) {
                    throw std::runtime_error("IMAGINATIO snapshot has an invalid object category vote");
                }
                memory.categoryVotes.emplace(std::move(name), observations);
            }
            if (memory.id == 0U || memory.observations == 0U) {
                throw std::runtime_error("IMAGINATIO snapshot contains an incomplete object engram");
            }
            restoredObjectEngrams.push_back(std::move(memory));
        }
    }
    std::vector<Impl::PoseEngram> restoredPoses;
    std::vector<Impl::RelationEngram> restoredRelations;
    std::vector<Impl::ActionEngram> restoredActions;
    std::vector<SceneInstance> restoredSceneInstances;
    std::vector<SceneRelationCue> restoredSceneRelations;
    std::vector<Impl::ResolutionEngram> restoredSourceResolutions;
    if (snapshotV7 || snapshotV8 || snapshotV9 || snapshotV10 || snapshotV11 || snapshotV12 || snapshotV13 || snapshotV14) {
        const auto readAssemblies = [&input]() {
            std::uint64_t count = 0;
            readPod(input, count);
            if (count > kMaximumSnapshotItems) {
                throw std::runtime_error("IMAGINATIO snapshot has too many assembly bindings");
            }
            std::vector<AssemblyId> values(static_cast<std::size_t>(count));
            for (auto& value : values) readPod(input, value);
            return values;
        };
        std::uint64_t poseCount = 0;
        readPod(input, poseCount);
        if (poseCount > impl_->config.maximumPoseEngrams) {
            throw std::runtime_error("IMAGINATIO snapshot has too many pose engrams");
        }
        restoredPoses.reserve(static_cast<std::size_t>(poseCount));
        for (std::uint64_t index = 0; index < poseCount; ++index) {
            Impl::PoseEngram pose;
            pose.category = readString(input);
            pose.pose = readString(input);
            readPod(input, pose.observations);
            if (snapshotV12 || snapshotV13 || snapshotV14) {
                for (auto& point : pose.shapeMean) {
                    readPod(input, point[0]);
                    readPod(input, point[1]);
                }
                for (auto& point : pose.shapeM2) {
                    readPod(input, point[0]);
                    readPod(input, point[1]);
                }
                pose.shapeMean = Impl::constrainShape(pose.shapeMean);
                for (auto& region : pose.featureMean) {
                    for (double& value : region) readPod(input, value);
                }
                for (auto& region : pose.featureM2) {
                    for (double& value : region) readPod(input, value);
                }
                for (std::size_t point = 0; point < pose.shapeMean.size(); ++point) {
                    for (std::size_t axis = 0; axis < 2U; ++axis) {
                        if (!std::isfinite(pose.shapeMean[point][axis])
                            || !std::isfinite(pose.shapeM2[point][axis])
                            || pose.shapeM2[point][axis] < 0.0) {
                            throw std::runtime_error(
                                "IMAGINATIO V12 snapshot has invalid pose form memory");
                        }
                    }
                }
                for (std::size_t region = 0; region < pose.featureMean.size(); ++region) {
                    for (std::size_t value = 0; value < pose.featureMean[region].size(); ++value) {
                        if (!std::isfinite(pose.featureMean[region][value])
                            || !std::isfinite(pose.featureM2[region][value])
                            || pose.featureM2[region][value] < 0.0) {
                            throw std::runtime_error(
                                "IMAGINATIO V12 snapshot has invalid pose feature memory");
                        }
                    }
                }
            } else {
                const auto legacyPrototype = readCanvas();
                pose.shapeMean = impl_->extractShape(legacyPrototype);
                pose.featureMean = impl_->extractFeatureModel(legacyPrototype);
                for (auto& point : pose.shapeM2) point = {0.0, 0.0};
                for (auto& region : pose.featureM2) region.fill(0.0);
            }
            std::uint64_t sourceCount = 0;
            readPod(input, sourceCount);
            if (sourceCount > kMaximumSnapshotItems) {
                throw std::runtime_error("IMAGINATIO snapshot has too many pose sources");
            }
            pose.sourceEngrams.resize(static_cast<std::size_t>(sourceCount));
            for (auto& source : pose.sourceEngrams) readPod(input, source);
            pose.assemblies = readAssemblies();
            if (pose.category.empty() || pose.pose.empty() || pose.observations == 0U) {
                throw std::runtime_error("IMAGINATIO snapshot contains an invalid pose engram");
            }
            restoredPoses.push_back(std::move(pose));
        }

        std::uint64_t relationCount = 0;
        readPod(input, relationCount);
        if (relationCount > impl_->config.maximumRelationEngrams) {
            throw std::runtime_error("IMAGINATIO snapshot has too many relation engrams");
        }
        restoredRelations.reserve(static_cast<std::size_t>(relationCount));
        for (std::uint64_t index = 0; index < relationCount; ++index) {
            Impl::RelationEngram relation;
            readPod(input, relation.relation);
            relation.subjectCategory = readString(input);
            relation.objectCategory = readString(input);
            readPod(input, relation.observations);
            for (double& value : relation.mean) readPod(input, value);
            for (double& value : relation.m2) readPod(input, value);
            relation.assemblies = readAssemblies();
            if (static_cast<unsigned int>(relation.relation)
                    > static_cast<unsigned int>(SceneRelationKind::ConnectedTo)
                || relation.subjectCategory.empty() || relation.objectCategory.empty()
                || relation.observations == 0U) {
                throw std::runtime_error("IMAGINATIO snapshot contains an invalid relation engram");
            }
            for (std::size_t value = 0; value < relation.mean.size(); ++value) {
                if (!std::isfinite(relation.mean[value])
                    || !std::isfinite(relation.m2[value]) || relation.m2[value] < 0.0) {
                    throw std::runtime_error("IMAGINATIO snapshot contains invalid relation statistics");
                }
            }
            restoredRelations.push_back(std::move(relation));
        }

        std::uint64_t actionCount = 0;
        readPod(input, actionCount);
        if (actionCount > impl_->config.maximumActionEngrams) {
            throw std::runtime_error("IMAGINATIO snapshot has too many action engrams");
        }
        restoredActions.reserve(static_cast<std::size_t>(actionCount));
        for (std::uint64_t index = 0; index < actionCount; ++index) {
            Impl::ActionEngram action;
            readPod(input, action.kind);
            action.label = readString(input);
            action.actorCategory = readString(input);
            action.targetCategory = readString(input);
            readPod(input, action.observations);
            for (double& value : action.actorMean) readPod(input, value);
            for (double& value : action.actorM2) readPod(input, value);
            for (double& value : action.targetMean) readPod(input, value);
            for (double& value : action.targetM2) readPod(input, value);
            action.actorPoseAfter = readString(input);
            action.targetPoseAfter = readString(input);
            action.assemblies = readAssemblies();
            if (static_cast<unsigned int>(action.kind)
                    > static_cast<unsigned int>(SceneActionKind::Custom)
                || action.label.empty() || action.actorCategory.empty()
                || action.observations == 0U) {
                throw std::runtime_error("IMAGINATIO snapshot contains an invalid action engram");
            }
            for (const auto* values : {
                    &action.actorMean, &action.actorM2,
                    &action.targetMean, &action.targetM2}) {
                for (const double value : *values) {
                    if (!std::isfinite(value)) {
                        throw std::runtime_error("IMAGINATIO snapshot contains invalid action statistics");
                    }
                }
            }
            for (const auto* values : {&action.actorM2, &action.targetM2}) {
                for (const double value : *values) {
                    if (value < 0.0) {
                        throw std::runtime_error("IMAGINATIO action variance must be non-negative");
                    }
                }
            }
            restoredActions.push_back(std::move(action));
        }

        std::uint64_t sceneInstanceCount = 0;
        readPod(input, sceneInstanceCount);
        if (sceneInstanceCount > impl_->config.maximumSceneObjects) {
            throw std::runtime_error("IMAGINATIO snapshot has too many scene instances");
        }
        restoredSceneInstances.resize(static_cast<std::size_t>(sceneInstanceCount));
        for (auto& instance : restoredSceneInstances) {
            readPod(input, instance.instanceId);
            instance.instance = readString(input);
            instance.category = readString(input);
            instance.pose = readString(input);
            readPod(input, instance.x);
            readPod(input, instance.y);
            readPod(input, instance.scale);
            readPod(input, instance.rotation);
            readPod(input, instance.depth);
            for (double& value : instance.occupiedRegion) readPod(input, value);
            readPod(input, instance.assemblyId);
            readPod(input, instance.sourceEngramId);
            readPod(input, instance.confidence);
            if (instance.instance.empty() || instance.category.empty()
                || !std::isfinite(instance.x) || !std::isfinite(instance.y)
                || !std::isfinite(instance.scale) || !std::isfinite(instance.rotation)
                || !std::isfinite(instance.depth) || !std::isfinite(instance.confidence)) {
                throw std::runtime_error("IMAGINATIO snapshot contains an invalid scene instance");
            }
            for (const double value : instance.occupiedRegion) {
                if (!std::isfinite(value)) {
                    throw std::runtime_error("IMAGINATIO scene bounds are invalid");
                }
            }
        }
        std::uint64_t sceneRelationCount = 0;
        readPod(input, sceneRelationCount);
        if (sceneRelationCount > kMaximumSnapshotItems) {
            throw std::runtime_error("IMAGINATIO snapshot has too many scene relations");
        }
        restoredSceneRelations.resize(static_cast<std::size_t>(sceneRelationCount));
        for (auto& relation : restoredSceneRelations) {
            relation.subject = readString(input);
            readPod(input, relation.relation);
            relation.object = readString(input);
            readPod(input, relation.confidence);
            if (relation.subject.empty() || relation.object.empty()
                || static_cast<unsigned int>(relation.relation)
                    > static_cast<unsigned int>(SceneRelationKind::ConnectedTo)
                || !std::isfinite(relation.confidence)) {
                throw std::runtime_error("IMAGINATIO snapshot contains an invalid scene relation");
            }
        }
        if (snapshotV10 || snapshotV11 || snapshotV12 || snapshotV13 || snapshotV14) {
            std::uint64_t resolutionCount = 0;
            readPod(input, resolutionCount);
            if (resolutionCount > 64U) {
                throw std::runtime_error("IMAGINATIO snapshot has too many source-resolution engrams");
            }
            restoredSourceResolutions.resize(static_cast<std::size_t>(resolutionCount));
            for (auto& resolution : restoredSourceResolutions) {
                std::uint64_t sourceWidth = 0;
                std::uint64_t sourceHeight = 0;
                readPod(input, sourceWidth);
                readPod(input, sourceHeight);
                readPod(input, resolution.observations);
                if (sourceWidth <= 512U && sourceHeight <= 512U) {
                    throw std::runtime_error("IMAGINATIO snapshot contains a non-high-resolution source memory");
                }
                if (sourceWidth > 16'384U || sourceHeight > 16'384U
                    || sourceWidth == 0U || sourceHeight == 0U
                    || resolution.observations == 0U) {
                    throw std::runtime_error("IMAGINATIO snapshot contains an invalid source resolution");
                }
                resolution.width = static_cast<std::size_t>(sourceWidth);
                resolution.height = static_cast<std::size_t>(sourceHeight);
            }
        }
    }

    const auto resampleCanvas = [](const VisualCanvas& source,
                                   std::size_t targetWidth,
                                   std::size_t targetHeight) {
        VisualCanvas target(targetWidth, targetHeight);
        for (std::size_t y = 0; y < targetHeight; ++y) {
            const std::size_t sourceY = std::min(source.height() - 1U,
                y * source.height() / targetHeight);
            for (std::size_t x = 0; x < targetWidth; ++x) {
                const std::size_t sourceX = std::min(source.width() - 1U,
                    x * source.width() / targetWidth);
                const auto color = source.colorPixel(sourceX, sourceY);
                target.setColorPixel(x, y, color[0], color[1], color[2]);
            }
        }
        target.setCursor(
            std::min(targetWidth - 1U, source.cursorX() * targetWidth / source.width()),
            std::min(targetHeight - 1U, source.cursorY() * targetHeight / source.height()));
        if (source.brushRadius() > 0U) {
            target.apply(PaintAction{
                .kind = PaintActionKind::BrushLarge,
                .repetitions = static_cast<std::uint16_t>(source.brushRadius()),
            });
        }
        const auto brush = source.brushColor();
        target.setBrushColor(brush[0], brush[1], brush[2]);
        return target;
    };

    // V14 never persists a working raster. Its loaded canvas contains actuator
    // state only. Older snapshots may have serialized a working image, so that
    // raster is explicitly discarded during migration.
    if (snapshotV14) {
        if (width != impl_->config.width || height != impl_->config.height) {
            impl_->canvas = resampleCanvas(
                impl_->canvas, impl_->config.width, impl_->config.height);
        }
    } else {
        impl_->canvas = VisualCanvas(impl_->config.width, impl_->config.height);
        impl_->canvas.clear();
    }

    impl_->engrams = std::move(restoredEngrams);
    impl_->symbols = std::move(restoredSymbols);
    impl_->categories = std::move(restoredCategories);
    impl_->objectEngrams = std::move(restoredObjectEngrams);
    impl_->nextObjectEngramId = restoredNextObjectEngramId;
    impl_->poses = std::move(restoredPoses);
    impl_->relations = std::move(restoredRelations);
    impl_->actions = std::move(restoredActions);
    impl_->sceneMemoryInstances = std::move(restoredSceneInstances);
    impl_->sceneMemoryRelations = std::move(restoredSceneRelations);
    impl_->sourceResolutions = std::move(restoredSourceResolutions);
    impl_->actionsExecuted = 0;
    impl_->lastCanvasMean = impl_->canvas.meanIntensity();
    impl_->runningSimilarityValid = false;
    impl_->last = ImaginationReport{};
    impl_->last.canvas = impl_->canvas;
    return true;
}

std::string VisualImagination::stateJson() const {
    std::array<std::uint64_t, 10> actionHistogram{};
    std::uint64_t repeatedPatterns = 0;
    std::optional<PaintActionKind> previousAction;
    for (const auto& event : impl_->trace) {
        if (!event.hasAction) continue;
        const auto index = static_cast<std::size_t>(event.action.kind);
        if (index < actionHistogram.size()) ++actionHistogram[index];
        if (previousAction.has_value() && *previousAction == event.action.kind) {
            ++repeatedPatterns;
        }
        previousAction = event.action.kind;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    const auto brush = impl_->canvas.brushColor();
    std::uint64_t selfMotorStrokeCount = 0U;
    std::uint64_t selfConsolidationCount = 0U;
    for (const auto& engram : impl_->engrams) {
        selfMotorStrokeCount += static_cast<std::uint64_t>(engram.selfMotorMemory.size());
        selfConsolidationCount += engram.selfConsolidations;
    }
    out << "{\"schema\":\"tatarus-imaginatio-v14\""
        << ",\"snapshot_format_version\":14"
        << ",\"visual_memory_policy\":{\"encoding\":\"self-consolidated-neural-motor-engram\",\"source_raster_retained\":false,\"teacher_trace_retained\":false,\"working_canvas_persisted\":false,\"self_motor_trace_retained\":true}"
        << ",\"self_memory\":{\"motor_strokes\":" << selfMotorStrokeCount
        << ",\"consolidations\":" << selfConsolidationCount << "}"
        << ",\"canvas\":{\"width\":" << impl_->canvas.width()
        << ",\"height\":" << impl_->canvas.height()
        << ",\"color_space\":\"sRGB\""
        << ",\"channels\":3"
        << ",\"cursor\":[" << impl_->canvas.cursorX() << ',' << impl_->canvas.cursorY() << ']'
        << ",\"brush_radius\":" << impl_->canvas.brushRadius()
        << ",\"brush_tone\":" << impl_->canvas.brushTone()
        << ",\"brush_color\":[" << brush[0] << ',' << brush[1] << ',' << brush[2] << ']'
        << ",\"mean_intensity\":" << impl_->canvas.meanIntensity()
        << ",\"marked_pixels\":" << impl_->canvas.markedPixels();
    if (impl_->canvas.pixels().size() <= 4096U) {
        out << ",\"pixels\":[";
        for (std::size_t i = 0; i < impl_->canvas.pixels().size(); ++i) {
            if (i) out << ',';
            out << impl_->canvas.pixels()[i];
        }
        out << "]"
            << ",\"rgb_pixels\":[";
        for (std::size_t i = 0; i < impl_->canvas.colorPixels().size(); ++i) {
            if (i) out << ',';
            out << impl_->canvas.colorPixels()[i];
        }
        out << ']';
    } else {
        out << ",\"encoding\":\"base64-rgb8\""
            << ",\"pixels\":[]"
            << ",\"rgb_pixels\":[]"
            << ",\"rgb8_base64\":\"" << base64Encode(canvasRgb8(impl_->canvas)) << '"';
    }
    out << "}"
        << ",\"source_resolution_memory\":[";
    for (std::size_t index = 0; index < impl_->sourceResolutions.size(); ++index) {
        if (index) out << ',';
        const auto& resolution = impl_->sourceResolutions[index];
        out << "{\"width\":" << resolution.width
            << ",\"height\":" << resolution.height
            << ",\"observations\":" << resolution.observations
            << ",\"megapixels\":"
            << (static_cast<double>(resolution.width) * static_cast<double>(resolution.height) / 1'000'000.0)
            << '}';
    }
    out << "]"
        << ",\"visual_pathway\":{\"schema\":\"tatarus-biological-vision-v1\""
        << ",\"retina\":{\"photoreceptors\":\"rgb-luminance\",\"ganglion_cells\":"
            "\"on-off-centre-surround-and-colour-opponent\",\"width\":"
        << impl_->visualPathway.config().retinalWidth
        << ",\"height\":" << impl_->visualPathway.config().retinalHeight << '}'
        << ",\"ocular_front_end\":{\"pupil_diameter_mm\":"
        << impl_->ocularSystem.telemetry().pupilDiameterMm
        << ",\"adaptation_gain\":" << impl_->ocularSystem.telemetry().retinalAdaptationGain
        << ",\"gaze_yaw_deg\":" << impl_->ocularSystem.telemetry().gazeYawDegrees
        << ",\"gaze_pitch_deg\":" << impl_->ocularSystem.telemetry().gazePitchDegrees
        << ",\"saccade_active\":"
        << (impl_->ocularSystem.telemetry().saccadeActive ? "true" : "false") << '}'
        << ",\"optic_nerve\":\"bounded-ganglion-afferent-events\""
        << ",\"v1\":\"horizontal-vertical-diagonal-junction-receptive-fields\""
        << ",\"v2\":\"object-adaptive-part-pooling\""
        << ",\"object_cortex\":{\"candidate_count\":"
        << impl_->lastVisualPercept.objects.size()
        << ",\"event_count\":" << impl_->lastVisualPercept.objectCortexEvents.size()
        << ",\"persistent_object_engrams\":" << impl_->objectEngrams.size() << '}'
        << ",\"ventral_stream\":[\"lateral_occipital_object\",\"fusiform_face\","
            "\"parahippocampal_place\",\"biological_object\"]"
        << ",\"top_down\":{\"bounded\":true,\"maximum_gain\":"
        << impl_->visualPathway.config().topDownMaximumGain
        << ",\"last_applied_gain\":" << impl_->lastVisualPercept.topDownGainApplied << '}'
        << ",\"last_activation\":{\"lateral_occipital_object\":"
        << impl_->lastVisualPercept.ventral.lateralOccipitalObject
        << ",\"fusiform_face\":" << impl_->lastVisualPercept.ventral.fusiformFace
        << ",\"parahippocampal_place\":"
        << impl_->lastVisualPercept.ventral.parahippocampalPlace
        << ",\"biological_object\":"
        << impl_->lastVisualPercept.ventral.biologicalObject << "}}"
        << ",\"visual_engrams\":[";
    for (std::size_t i = 0; i < impl_->engrams.size(); ++i) {
        if (i) out << ',';
        const auto& engram = impl_->engrams[i];
        out << "{\"id\":" << engram.id
            << ",\"concept\":\"" << jsonEscape(engram.label) << "\""
            << ",\"observations\":" << engram.observations
            << ",\"trace_quality\":" << engram.traceQuality
            << ",\"memory_encoding\":\"visual-plus-self-motor-engram-v14\""
            << ",\"source_raster_retained\":false"
            << ",\"teacher_trace_retained\":false"
            << ",\"self_motor_memory_valid\":"
            << (engram.selfMotorMemoryValid ? "true" : "false")
            << ",\"self_motor_strokes\":" << engram.selfMotorMemory.size()
            << ",\"self_consolidations\":" << engram.selfConsolidations
            << ",\"self_assembly\":" << engram.selfAssembly
            << ",\"self_similarity\":" << engram.selfSimilarity
            << ",\"motor_actions\":" << engram.selfMotorMemory.size()
            << ",\"motor_marks\":" << engram.selfMotorMemory.size()
            << ",\"region_tokens\":" << engram.regions.size()
            << ",\"stroke_tokens\":" << engram.strokes.size()
            << ",\"assemblies\":[";
        for (std::size_t j = 0; j < engram.assemblies.size(); ++j) {
            if (j) out << ',';
            out << engram.assemblies[j];
        }
        out << "]}";
    }
    out << "]"
        << ",\"symbol_engrams\":[";
    bool emittedSymbol = false;
    for (const auto& [symbol, associations] : impl_->symbols) {
        if (emittedSymbol) out << ',';
        emittedSymbol = true;
        out << "{\"symbol\":\"" << jsonEscape(symbol) << "\",\"associations\":[";
        for (std::size_t i = 0; i < associations.size(); ++i) {
            if (i) out << ',';
            out << "{\"engram_id\":" << associations[i].engramId
                << ",\"observations\":" << associations[i].observations
                << ",\"strength\":" << associations[i].strength << '}';
        }
        out << "]}";
    }
    out << "]"
        << ",\"category_engrams\":[";
    for (std::size_t i = 0; i < impl_->categories.size(); ++i) {
        if (i) out << ',';
        const auto& category = impl_->categories[i];
        out << "{\"category\":\"" << jsonEscape(category.name) << "\""
            << ",\"examples\":" << category.observations
            << ",\"anchors\":0"
            << ",\"source_raster_retained\":false"
            << ",\"memory_encoding\":\"running-shape-feature-statistics\""
            << ",\"fusion_points\":5"
            << ",\"form_model\":\"five-field-structural-morphology\""
            << ",\"feature_model\":\"hierarchical-object-part-running-statistics\""
            << ",\"detail_policy\":\"factorized-soft-region-recombination\""
            << ",\"perceptual_hierarchy\":[\"retinal_rgb\",\"oriented_edges\","
                "\"object_parts\",\"category_concept\",\"top_down_prior\"]"
            << ",\"top_down_model\":\"category-conditioned-feature-prior\""
            << ",\"feature_fields\":[\"BACKGROUND\",\"UPPER_LEFT\","
                "\"UPPER_CENTER\",\"UPPER_RIGHT\",\"MIDDLE_LEFT\",\"CENTER\","
                "\"MIDDLE_RIGHT\",\"LOWER_LEFT\",\"LOWER_CENTER\",\"LOWER_RIGHT\"]"
            << ",\"feature_means\":[";
        for (std::size_t region = 0; region < category.featureMean.size(); ++region) {
            if (region) out << ',';
            out << '[';
            for (std::size_t value = 0;
                 value < category.featureMean[region].size(); ++value) {
                if (value) out << ',';
                out << category.featureMean[region][value];
            }
            out << ']';
        }
        out << ']'
            << ",\"shape_points\":[";
        for (std::size_t point = 0; point < category.shapeMean.size(); ++point) {
            if (point) out << ',';
            out << '[' << category.shapeMean[point][0] << ','
                << category.shapeMean[point][1] << ']';
        }
        out << "]"
            << ",\"assemblies\":[";
        for (std::size_t assembly = 0; assembly < category.assemblies.size(); ++assembly) {
            if (assembly) out << ',';
            out << category.assemblies[assembly];
        }
        out << "]}";
    }
    out << "]"
        << ",\"object_engrams\":[";
    for (std::size_t index = 0; index < impl_->objectEngrams.size(); ++index) {
        if (index) out << ',';
        const auto& memory = impl_->objectEngrams[index];
        out << "{\"id\":" << memory.id
            << ",\"observations\":" << memory.observations
            << ",\"dominant_category\":\""
            << jsonEscape(Impl::dominantObjectCategory(memory)) << "\""
            << ",\"category_votes\":{";
        bool emittedVote = false;
        for (const auto& [name, votes] : memory.categoryVotes) {
            if (emittedVote) out << ',';
            emittedVote = true;
            out << '\"' << jsonEscape(name) << "\":" << votes;
        }
        out << "}}";
    }
    out << "]"
        << ",\"pose_engrams\":[";
    for (std::size_t index = 0; index < impl_->poses.size(); ++index) {
        if (index) out << ',';
        const auto& pose = impl_->poses[index];
        out << "{\"category\":\"" << jsonEscape(pose.category) << "\""
            << ",\"pose\":\"" << jsonEscape(pose.pose) << "\""
            << ",\"observations\":" << pose.observations
            << ",\"source_engrams\":[";
        for (std::size_t source = 0; source < pose.sourceEngrams.size(); ++source) {
            if (source) out << ',';
            out << pose.sourceEngrams[source];
        }
        out << "]}";
    }
    out << "]"
        << ",\"relation_engrams\":[";
    for (std::size_t index = 0; index < impl_->relations.size(); ++index) {
        if (index) out << ',';
        const auto& relation = impl_->relations[index];
        out << "{\"relation\":\"" << relationName(relation.relation) << "\""
            << ",\"subject_category\":\"" << jsonEscape(relation.subjectCategory) << "\""
            << ",\"object_category\":\"" << jsonEscape(relation.objectCategory) << "\""
            << ",\"observations\":" << relation.observations
            << ",\"mean_delta\":[" << relation.mean[0] << ',' << relation.mean[1]
            << ',' << relation.mean[2] << ']'
            << ",\"mean_scale_ratio\":" << relation.mean[3]
            << ",\"assemblies\":[";
        for (std::size_t assembly = 0; assembly < relation.assemblies.size(); ++assembly) {
            if (assembly) out << ',';
            out << relation.assemblies[assembly];
        }
        out << "]}";
    }
    out << "]"
        << ",\"action_engrams\":[";
    for (std::size_t index = 0; index < impl_->actions.size(); ++index) {
        if (index) out << ',';
        const auto& action = impl_->actions[index];
        out << "{\"action\":\"" << sceneActionName(action.kind) << "\""
            << ",\"label\":\"" << jsonEscape(action.label) << "\""
            << ",\"actor_category\":\"" << jsonEscape(action.actorCategory) << "\""
            << ",\"target_category\":\"" << jsonEscape(action.targetCategory) << "\""
            << ",\"observations\":" << action.observations
            << ",\"actor_effect\":[";
        for (std::size_t value = 0; value < action.actorMean.size(); ++value) {
            if (value) out << ',';
            out << action.actorMean[value];
        }
        out << "]"
            << ",\"target_effect\":[";
        for (std::size_t value = 0; value < action.targetMean.size(); ++value) {
            if (value) out << ',';
            out << action.targetMean[value];
        }
        out << "]}";
    }
    out << "]"
        << ",\"scene_memory\":{\"instances\":[";
    for (std::size_t index = 0; index < impl_->sceneMemoryInstances.size(); ++index) {
        if (index) out << ',';
        const auto& instance = impl_->sceneMemoryInstances[index];
        out << "{\"id\":" << instance.instanceId
            << ",\"instance\":\"" << jsonEscape(instance.instance) << "\""
            << ",\"category\":\"" << jsonEscape(instance.category) << "\""
            << ",\"pose\":\"" << jsonEscape(instance.pose) << "\""
            << ",\"position\":[" << instance.x << ',' << instance.y << ']'
            << ",\"scale\":" << instance.scale
            << ",\"rotation\":" << instance.rotation
            << ",\"depth\":" << instance.depth
            << ",\"occupied_region\":[" << instance.occupiedRegion[0] << ','
            << instance.occupiedRegion[1] << ',' << instance.occupiedRegion[2] << ','
            << instance.occupiedRegion[3] << "]}"
            ;
    }
    out << "],\"relations\":[";
    for (std::size_t index = 0; index < impl_->sceneMemoryRelations.size(); ++index) {
        if (index) out << ',';
        const auto& relation = impl_->sceneMemoryRelations[index];
        out << "{\"subject\":\"" << jsonEscape(relation.subject) << "\""
            << ",\"relation\":\"" << relationName(relation.relation) << "\""
            << ",\"object\":\"" << jsonEscape(relation.object) << "\""
            << ",\"confidence\":" << relation.confidence << '}';
    }
    out << "]}"
        << ",\"last_result\":{\"stage\":\"" << stageName(impl_->last.stage) << "\""
        << ",\"success\":" << (impl_->last.success ? "true" : "false")
        << ",\"actions\":" << impl_->last.actionsExecuted
        << ",\"similarity\":" << impl_->last.similarity
        << ",\"novelty\":" << impl_->last.novelty
        << ",\"active_assembly\":" << impl_->last.activeAssemblyId
        << ",\"recalled_engrams\":[";
    for (std::size_t i = 0; i < impl_->last.recalledEngramIds.size(); ++i) {
        if (i) out << ',';
        out << impl_->last.recalledEngramIds[i];
    }
    out << ']'
        << ",\"categories\":[";
    for (std::size_t i = 0; i < impl_->last.categoryNames.size(); ++i) {
        if (i) out << ',';
        out << '"' << jsonEscape(impl_->last.categoryNames[i]) << '"';
    }
    out << ']'
        << ",\"feature_sources\":[";
    for (std::size_t i = 0; i < impl_->last.featureSourceIndices.size(); ++i) {
        if (i) out << ',';
        out << "{\"feature\":\""
            << jsonEscape(i < impl_->last.featureNames.size()
                ? impl_->last.featureNames[i] : std::string{})
            << "\",\"source_index\":" << impl_->last.featureSourceIndices[i] << '}';
    }
    out << ']'
        << ",\"perceived_scene\":{\"success\":"
        << (impl_->last.perceivedScene.success ? "true" : "false")
        << ",\"internally_generated\":"
        << (impl_->last.perceivedScene.internallyGenerated ? "true" : "false")
        << ",\"candidate_count\":" << impl_->last.perceivedScene.candidateCount
        << ",\"recognized_count\":" << impl_->last.perceivedScene.recognizedCount
        << ",\"novel_count\":" << impl_->last.perceivedScene.novelCount
        << ",\"objects\":[";
    for (std::size_t index = 0; index < impl_->last.perceivedScene.objects.size(); ++index) {
        if (index) out << ',';
        const auto& object = impl_->last.perceivedScene.objects[index];
        out << "{\"id\":" << object.objectEngramId
            << ",\"instance\":\"" << jsonEscape(object.instance) << "\""
            << ",\"category\":\"" << jsonEscape(object.category) << "\""
            << ",\"recognized\":" << (object.recognizedCategory ? "true" : "false")
            << ",\"novel\":" << (object.novelObject ? "true" : "false")
            << ",\"saliency\":" << object.saliency
            << ",\"memory_similarity\":" << object.objectMemorySimilarity
            << ",\"bounds\":[" << object.bounds[0] << ',' << object.bounds[1] << ','
            << object.bounds[2] << ',' << object.bounds[3] << "]}";
    }
    out << "]}"
        << ",\"reference_visible_during_drawing\":"
        << (impl_->last.referenceVisibleDuringDrawing ? "true" : "false")
        << ",\"scene_metrics\":{\"objects\":" << impl_->last.sceneMetrics.objectCount
        << ",\"relations\":" << impl_->last.sceneMetrics.relationCount
        << ",\"category_preservation\":" << impl_->last.sceneMetrics.categoryPreservation
        << ",\"spatial_relation_accuracy\":"
        << impl_->last.sceneMetrics.spatialRelationAccuracy
        << ",\"occlusion_consistency\":" << impl_->last.sceneMetrics.occlusionConsistency
        << ",\"pose_coherence\":" << impl_->last.sceneMetrics.poseCoherence
        << ",\"learned_effects_used\":" << impl_->last.sceneMetrics.learnedEffectsUsed
        << ",\"prediction_confidence\":"
        << impl_->last.sceneMetrics.predictionConfidence
        << ",\"prediction_error\":" << impl_->last.sceneMetrics.predictionError
        << "},\"prospective_states\":[";
    for (std::size_t state = 0; state < impl_->last.prospectiveStates.size(); ++state) {
        if (state) out << ',';
        const auto& value = impl_->last.prospectiveStates[state];
        out << "{\"step\":" << value.step
            << ",\"confidence\":" << value.confidence
            << ",\"prediction_error\":" << value.predictionError
            << ",\"instances\":[";
        for (std::size_t instance = 0; instance < value.instances.size(); ++instance) {
            if (instance) out << ',';
            const auto& object = value.instances[instance];
            out << "{\"id\":" << object.instanceId
                << ",\"instance\":\"" << jsonEscape(object.instance) << "\""
                << ",\"category\":\"" << jsonEscape(object.category) << "\""
                << ",\"pose\":\"" << jsonEscape(object.pose) << "\""
                << ",\"position\":[" << object.x << ',' << object.y << ']'
                << ",\"scale\":" << object.scale
                << ",\"rotation\":" << object.rotation
                << ",\"depth\":" << object.depth << '}';
        }
        out << "]}";
    }
    out << "]}"
        << ",\"analysis\":{\"attempts\":" << impl_->totalAttempts
        << ",\"successful_attempts\":" << impl_->successfulAttempts
        << ",\"failed_attempts\":" << impl_->failedAttempts
        << ",\"causal_events\":" << impl_->trace.size()
        << ",\"maximum_engrams\":" << impl_->config.maximumEngrams
        << ",\"neural_feedback_stride\":" << impl_->config.neuralFeedbackStride
        << ",\"repeated_action_patterns\":" << repeatedPatterns
        << ",\"action_histogram\":{";
    for (std::size_t i = 0; i < actionHistogram.size(); ++i) {
        if (i) out << ',';
        out << '\"' << actionName(static_cast<PaintActionKind>(i)) << "\":"
            << actionHistogram[i];
    }
    out << "}}"
        << ",\"organism\":{\"experiences\":" << impl_->mind.metrics().experiences
        << ",\"neural_transitions\":" << impl_->mind.metrics().learnedTransitions
        << ",\"atp\":" << impl_->mind.physiology().atp
        << ",\"creb\":" << impl_->mind.physiology().crebActivation
        << ",\"protein\":" << impl_->mind.physiology().proteinPool
        << ",\"sleep_pressure\":" << impl_->mind.physiology().sleepPressure
        << ",\"prospective_activation\":" << impl_->mind.prospection().prospectiveActivation
        << "}}";
    return out.str();
}

std::string VisualImagination::traceJson() const {
    std::array<std::uint64_t, 10> actionHistogram{};
    std::uint64_t strokes = 0;
    std::uint64_t repeatedPatterns = 0;
    std::optional<PaintActionKind> previousAction;
    for (const auto& event : impl_->trace) {
        if (!event.hasAction) continue;
        const auto index = static_cast<std::size_t>(event.action.kind);
        if (index < actionHistogram.size()) ++actionHistogram[index];
        if (event.action.kind == PaintActionKind::Paint
            || event.action.kind == PaintActionKind::Erase) {
            ++strokes;
        }
        if (previousAction.has_value() && *previousAction == event.action.kind) {
            ++repeatedPatterns;
        }
        previousAction = event.action.kind;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\"schema\":\"tatarus-imaginatio-causal-trace-v7\""
        << ",\"color_space\":\"sRGB\""
        << ",\"causal_boundary\":\"RGB pigment and movement commands come from demonstrated, recalled, form-synthesized or relational scene motor programs. Stage 6 re-senses the persistent canvas after each independent depth-ordered instance; Stage 7 simulates learned action effects without changing it and paints only the predicted final state.\""
        << ",\"summary\":{\"events\":" << impl_->trace.size()
        << ",\"motor_commands\":" << impl_->last.actionsExecuted
        << ",\"strokes\":" << strokes
        << ",\"repeated_action_patterns\":" << repeatedPatterns
        << ",\"action_histogram\":{";
    for (std::size_t i = 0; i < actionHistogram.size(); ++i) {
        if (i) out << ',';
        out << '\"' << actionName(static_cast<PaintActionKind>(i)) << "\":"
            << actionHistogram[i];
    }
    out << "}},\"events\":[";
    for (std::size_t index = 0; index < impl_->trace.size(); ++index) {
        if (index) out << ',';
        const auto& event = impl_->trace[index];
        out << "{\"sequence\":" << event.sequence
            << ",\"phase\":\"" << jsonEscape(event.phase) << "\""
            << ",\"cause\":\"" << jsonEscape(event.cause) << "\""
            << ",\"stage\":\"" << stageName(event.stage) << "\""
            << ",\"scene_instance\":\"" << jsonEscape(event.sceneInstance) << "\""
            << ",\"prospective_step\":" << event.prospectiveStep
            << ",\"reference_visible\":"
            << (event.referenceVisible ? "true" : "false")
            << ",\"neural_feedback\":"
            << (event.neuralFeedback ? "true" : "false")
            << ",\"cursor_before\":[" << event.cursorBeforeX << ','
            << event.cursorBeforeY << ']'
            << ",\"cursor_after\":[" << event.cursorAfterX << ','
            << event.cursorAfterY << ']'
            << ",\"similarity\":" << event.similarity
            << ",\"reward\":" << event.reward
            << ",\"novelty\":" << event.novelty;
        if (event.hasAction) {
            out << ",\"action\":{\"kind\":\"" << actionName(event.action.kind)
                << "\",\"id\":" << (static_cast<unsigned int>(event.action.kind) + 1U)
                << ",\"repetitions\":" << event.action.repetitions
                << ",\"intensity\":" << event.action.intensity
                << ",\"color\":[" << event.action.red << ','
                << event.action.green << ',' << event.action.blue << ']'
                << ",\"patch\":[" << static_cast<unsigned int>(event.action.patchWidth)
                << ',' << static_cast<unsigned int>(event.action.patchHeight) << "]}";
        } else {
            out << ",\"action\":null";
        }
        out << ",\"neural\":{\"active_assembly\":" << event.assembly
            << ",\"dendritic_spikes\":" << event.biology.dendriticSpikes
            << ",\"mean_dendritic_calcium\":" << event.biology.meanDendriticCalcium
            << ",\"predicted_assembly\":" << event.prospection.predictedAssemblyId
            << ",\"prediction_error\":" << event.prospection.predictionError
            << ",\"sequence_familiarity\":" << event.prospection.sequenceFamiliarity
            << ",\"prospective_activation\":" << event.prospection.prospectiveActivation
            << ",\"motor\":{\"selected_direction\":" << event.motor.selectedDirection
            << ",\"confidence\":" << event.motor.confidence
            << ",\"directional_activity\":[";
        for (std::size_t direction = 0; direction < event.motor.directionalActivity.size(); ++direction) {
            if (direction) out << ',';
            out << event.motor.directionalActivity[direction];
        }
        out << "]}}"
            << ",\"plasticity\":{\"tagged_synapses\":" << event.physiology.taggedSynapses
            << ",\"synaptic_tag\":" << event.physiology.synapticTag
            << ",\"creb\":" << event.physiology.crebActivation
            << ",\"protein\":" << event.physiology.proteinPool << '}'
            << ",\"metabolism\":{\"atp\":" << event.physiology.atp
            << ",\"sleep_pressure\":" << event.physiology.sleepPressure << '}'
            << ",\"changed_count\":" << event.changes.size();
        if (impl_->canvas.pixels().size() <= 4096U) {
            out << ",\"changed_pixels\":[";
            for (std::size_t change = 0; change < event.changes.size(); ++change) {
                if (change) out << ',';
                out << '[' << event.changes[change].index << ','
                    << event.changes[change].value << ']';
            }
            out << "]"
                << ",\"changed_colors\":[";
            for (std::size_t change = 0; change < event.changes.size(); ++change) {
                if (change) out << ',';
                out << '[' << event.changes[change].index << ','
                    << event.changes[change].red << ','
                    << event.changes[change].green << ','
                    << event.changes[change].blue << ']';
            }
            out << ']';
        } else {
            std::vector<std::uint8_t> packedChanges;
            packedChanges.reserve(event.changes.size() * 7U);
            for (const auto& change : event.changes) {
                const auto pixelIndex = static_cast<std::uint32_t>(change.index);
                for (unsigned int shift = 0; shift < 4U; ++shift) {
                    packedChanges.push_back(static_cast<std::uint8_t>(
                        (pixelIndex >> (shift * 8U)) & 0xffU));
                }
                packedChanges.push_back(static_cast<std::uint8_t>(
                    std::llround(clamp01(change.red) * 255.0)));
                packedChanges.push_back(static_cast<std::uint8_t>(
                    std::llround(clamp01(change.green) * 255.0)));
                packedChanges.push_back(static_cast<std::uint8_t>(
                    std::llround(clamp01(change.blue) * 255.0)));
            }
            out << ",\"changed_pixels\":[]"
                << ",\"changed_colors\":[]"
                << ",\"changed_rgb8_base64\":\""
                << base64Encode(packedChanges) << '"';
        }
        out << '}';
    }
    out << "]}";
    return out.str();
}

std::vector<PaintAction> VisualImagination::makeTeacherTrace(
    const VisualCanvas& reference,
    double threshold,
    std::size_t patchSide) {
    const double boundedThreshold = clamp01(threshold);
    if (patchSide == 0 || patchSide > 8) {
        throw std::invalid_argument("Teacher pigment patch side must be in [1, 8]");
    }
    std::vector<PaintAction> result;
    std::size_t x = reference.width() / 2U;
    std::size_t y = reference.height() / 2U;
    if (patchSide > 1U) {
        const std::size_t rows = (reference.height() + patchSide - 1U) / patchSide;
        const std::size_t columns = (reference.width() + patchSide - 1U) / patchSide;
        result.reserve(rows * columns * 2U);
        for (std::size_t tileY = 0; tileY < rows; ++tileY) {
            const bool reverse = tileY % 2U != 0;
            for (std::size_t tileOffset = 0; tileOffset < columns; ++tileOffset) {
                const std::size_t tileX = reverse ? columns - 1U - tileOffset : tileOffset;
                const std::size_t originX = tileX * patchSide;
                const std::size_t originY = tileY * patchSide;
                const std::size_t width = std::min(patchSide, reference.width() - originX);
                const std::size_t height = std::min(patchSide, reference.height() - originY);
                bool marked = false;
                std::array<double, 3> sum{0.0, 0.0, 0.0};
                PaintAction paint{
                    .kind = PaintActionKind::Paint,
                    .repetitions = 1,
                    .intensity = 1.0,
                    .patchWidth = static_cast<std::uint8_t>(width),
                    .patchHeight = static_cast<std::uint8_t>(height),
                };
                for (std::size_t patchY = 0; patchY < height; ++patchY) {
                    for (std::size_t patchX = 0; patchX < width; ++patchX) {
                        const auto color = reference.colorPixel(originX + patchX, originY + patchY);
                        marked = marked || std::max({color[0], color[1], color[2]}) >= boundedThreshold;
                        const std::size_t patchIndex = (patchY * width + patchX) * 3U;
                        for (std::size_t channel = 0; channel < 3U; ++channel) {
                            sum[channel] += color[channel];
                            paint.patchRgb[patchIndex + channel] = static_cast<std::uint8_t>(
                                std::llround(clamp01(color[channel]) * 255.0));
                        }
                    }
                }
                if (!marked) continue;
                const double sampleCount = static_cast<double>(width * height);
                paint.red = sum[0] / sampleCount;
                paint.green = sum[1] / sampleCount;
                paint.blue = sum[2] / sampleCount;
                appendMoveActions(result, x, y, originX, originY);
                result.push_back(std::move(paint));
            }
        }
        return result;
    }
    for (std::size_t row = 0; row < reference.height(); ++row) {
        const bool reverse = row % 2U != 0;
        for (std::size_t offset = 0; offset < reference.width(); ++offset) {
            const std::size_t column = reverse
                ? reference.width() - 1U - offset : offset;
            const auto color = reference.colorPixel(column, row);
            if (std::max({color[0], color[1], color[2]}) < boundedThreshold) continue;
            appendMoveActions(result, x, y, column, row);
            result.push_back(PaintAction{
                .kind = PaintActionKind::Paint,
                .repetitions = 1,
                .intensity = 1.0,
                .red = color[0],
                .green = color[1],
                .blue = color[2],
            });
        }
    }
    return result;
}

} // namespace tatarus
