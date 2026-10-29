#include "cwxeditor_src/gui/cwx/editor/gui/dwt/jpyimage.hpp"

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"
#include "cwxeditor_src/lib/image_text.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/utf8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::jpyimage {
namespace {

namespace graphics = ::cwxeditor::cwx::graphics;
namespace structs = ::cwxeditor::cwx::structs;
namespace utils = ::cwxeditor::cwx::utils;

using Byte = lib::Byte;
using Bytes = lib::Bytes;
struct Frame {
    lib::Size logical_size{};
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    Bytes pixels;
};

constexpr std::uint32_t default_width = 632U;
constexpr std::uint32_t default_height = 420U;
constexpr std::size_t cache_count = 9U;

[[nodiscard]] std::uint32_t positiveScale(const std::uint32_t value) noexcept {
    return std::max<std::uint32_t>(1U, value);
}

[[nodiscard]] std::uint32_t scaledDimension(const std::int32_t value,
                                             const std::uint32_t scale) noexcept {
    if (value <= 0) return 0U;
    const auto result = static_cast<std::uint64_t>(value) * positiveScale(scale);
    return result > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(result);
}

[[nodiscard]] std::int64_t scaledCoordinate(const std::int32_t value,
                                             const std::uint32_t scale) noexcept {
    return static_cast<std::int64_t>(value) * positiveScale(scale);
}

[[nodiscard]] std::uint32_t logicalDimension(const std::uint32_t pixels,
                                              const std::uint32_t scale) noexcept {
    const auto value = static_cast<std::uint64_t>(pixels) / positiveScale(scale);
    return value > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t roundedDimension(const float value) noexcept {
    if (!(value > 0.0F)) return 0U;
    if (value >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(std::lround(value));
}

[[nodiscard]] String pathString(const std::filesystem::path& value) {
    return structs::pathToUtf8(value);
}

[[nodiscard]] std::filesystem::path pathValue(const String& value) {
    return structs::pathFromUtf8(value);
}

[[nodiscard]] bool regularFile(const std::filesystem::path& value) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error);
}

[[nodiscard]] bool regularDirectory(const std::filesystem::path& value) noexcept {
    std::error_code error;
    return std::filesystem::is_directory(value, error);
}

[[nodiscard]] std::size_t checkedPixelBytes(const std::uint32_t width,
                                            const std::uint32_t height,
                                            const std::size_t bpp) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto pixels = static_cast<std::size_t>(width) * height;
    if (height != 0U && pixels / height != width) throw std::length_error("image is too large");
    if (bpp != 0U && pixels > maximum / bpp) throw std::length_error("image is too large");
    return pixels * bpp;
}

[[nodiscard]] Frame makeFrame(const std::uint32_t logical_width,
                              const std::uint32_t logical_height,
                              const std::uint32_t scale,
                              const jpy::CRGB color) {
    const auto source_scale = positiveScale(scale);
    Frame frame;
    frame.logical_size = {static_cast<float>(logical_width),
                          static_cast<float>(logical_height)};
    frame.pixel_width = scaledDimension(static_cast<std::int32_t>(
                                            std::min<std::uint32_t>(
                                                logical_width,
                                                std::numeric_limits<std::int32_t>::max())),
                                        source_scale);
    frame.pixel_height = scaledDimension(static_cast<std::int32_t>(
                                             std::min<std::uint32_t>(
                                                 logical_height,
                                                 std::numeric_limits<std::int32_t>::max())),
                                         source_scale);
    const auto bytes = checkedPixelBytes(frame.pixel_width, frame.pixel_height, 4U);
    frame.pixels.resize(bytes);
    const auto red = static_cast<Byte>(std::min<std::uint32_t>(color.r, 255U));
    const auto green = static_cast<Byte>(std::min<std::uint32_t>(color.g, 255U));
    const auto blue = static_cast<Byte>(std::min<std::uint32_t>(color.b, 255U));
    const auto alpha = static_cast<Byte>(std::min<std::uint32_t>(color.a, 255U));
    for (std::size_t i = 0; i < frame.pixels.size(); i += 4U) {
        frame.pixels[i] = blue;
        frame.pixels[i + 1U] = green;
        frame.pixels[i + 2U] = red;
        frame.pixels[i + 3U] = alpha;
    }
    return frame;
}

[[nodiscard]] ImageDataWithScale wrapFrame(Frame frame,
                                            const std::uint32_t scale) {
    if (frame.pixel_width == 0U || frame.pixel_height == 0U) return {};
    lib::ImageFrame output;
    output.logical_size = frame.logical_size;
    output.pixel_width = frame.pixel_width;
    output.pixel_height = frame.pixel_height;
    output.pixel_format = lib::PixelFormat::BGRA32;
    output.pixels.resize(frame.pixels.size());
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        output.pixels[i] = static_cast<std::byte>(frame.pixels[i]);
    }
    lib::ImageData data;
    data.format = lib::ImageFormat::Bitmap;
    data.frames.push_back(std::move(output));
    auto result=ImageDataWithScale(std::move(data),positiveScale(scale));
    // 生成した Frame は拡大前の論理境界を保持するが、復号したファイ
    // ルのフレームは物理寸法を返す。 汎用の復号データ変換で、生成済みの
    // 境界を再び割らない。 D版の getWidth(1) と同様、pixelWidth
    // / sourceScale は一度だけ適用する。
    result.logical_size=frame.logical_size;
    return result;
}

[[nodiscard]] ImageDataWithScale blankImage(const std::uint32_t width = 0U,
                                            const std::uint32_t height = 0U,
                                            const std::uint32_t scale = 1U,
                                            const jpy::CRGB color = {}) {
    if (width == 0U || height == 0U) return {};
    return wrapFrame(makeFrame(width, height, scale, color), scale);
}

[[nodiscard]] std::size_t sourceBytesPerPixel(const lib::PixelFormat format,
                                              const std::size_t total,
                                              const std::uint32_t width,
                                              const std::uint32_t height) noexcept {
    switch (format) {
    case lib::PixelFormat::Gray8: return 1U;
    case lib::PixelFormat::RGB24:
    case lib::PixelFormat::BGR24: return 3U;
    case lib::PixelFormat::RGBA32:
    case lib::PixelFormat::BGRA32: return 4U;
    default: break;
    }
    const auto pixels = static_cast<std::size_t>(width) * height;
    if (pixels != 0U && total >= pixels * 4U) return 4U;
    if (pixels != 0U && total >= pixels * 3U) return 3U;
    return 0U;
}

[[nodiscard]] std::optional<Frame> toBgra(const lib::ImageFrame& source) {
    if (source.pixel_width == 0U || source.pixel_height == 0U) return std::nullopt;
    const auto bpp = sourceBytesPerPixel(source.pixel_format, source.pixels.size(),
                                         source.pixel_width, source.pixel_height);
    if (bpp == 0U) return std::nullopt;
    const auto minimum_stride = static_cast<std::size_t>(source.pixel_width) * bpp;
    if (source.pixels.size() < minimum_stride * source.pixel_height) return std::nullopt;
    const auto stride = source.pixels.size() / source.pixel_height;
    if (stride < minimum_stride) return std::nullopt;
    Frame result;
    result.logical_size = source.logical_size;
    result.pixel_width = source.pixel_width;
    result.pixel_height = source.pixel_height;
    result.pixels.resize(checkedPixelBytes(result.pixel_width, result.pixel_height, 4U));
    const auto sourceByte = [&source](const std::size_t index) {
        return static_cast<Byte>(std::to_integer<unsigned int>(source.pixels[index]));
    };
    const auto format = source.pixel_format == lib::PixelFormat::Unknown
                            ? (bpp == 1U ? lib::PixelFormat::Gray8
                                         : bpp == 3U ? lib::PixelFormat::BGR24
                                                      : lib::PixelFormat::BGRA32)
                            : source.pixel_format;
    for (std::uint32_t y = 0; y < source.pixel_height; ++y) {
        for (std::uint32_t x = 0; x < source.pixel_width; ++x) {
            const auto si = static_cast<std::size_t>(y) * stride +
                            static_cast<std::size_t>(x) * bpp;
            const auto di = (static_cast<std::size_t>(y) * result.pixel_width + x) * 4U;
            switch (format) {
            case lib::PixelFormat::Gray8:
                result.pixels[di] = sourceByte(si);
                result.pixels[di + 1U] = sourceByte(si);
                result.pixels[di + 2U] = sourceByte(si);
                result.pixels[di + 3U] = 255U;
                break;
            case lib::PixelFormat::RGB24:
                result.pixels[di] = sourceByte(si + 2U);
                result.pixels[di + 1U] = sourceByte(si + 1U);
                result.pixels[di + 2U] = sourceByte(si);
                result.pixels[di + 3U] = 255U;
                break;
            case lib::PixelFormat::RGBA32:
                result.pixels[di] = sourceByte(si + 2U);
                result.pixels[di + 1U] = sourceByte(si + 1U);
                result.pixels[di + 2U] = sourceByte(si);
                result.pixels[di + 3U] = sourceByte(si + 3U);
                break;
            case lib::PixelFormat::BGR24:
            case lib::PixelFormat::BGRA32:
                result.pixels[di] = sourceByte(si);
                result.pixels[di + 1U] = sourceByte(si + 1U);
                result.pixels[di + 2U] = sourceByte(si + 2U);
                result.pixels[di + 3U] = bpp == 4U ? sourceByte(si + 3U) : 255U;
                break;
            default:
                return std::nullopt;
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<Frame> resizeFrame(Frame frame,
                                                const std::uint32_t width,
                                                const std::uint32_t height,
                                                const bool smooth) {
    if (width == 0U || height == 0U) return std::nullopt;
    if (frame.pixel_width == width && frame.pixel_height == height) return frame;
    auto alpha = Bytes(checkedPixelBytes(frame.pixel_width, frame.pixel_height, 1U));
    for (std::uint32_t y = 0; y < frame.pixel_height; ++y) {
        for (std::uint32_t x = 0; x < frame.pixel_width; ++x) {
            alpha[static_cast<std::size_t>(y) * frame.pixel_width + x] =
                frame.pixels[(static_cast<std::size_t>(y) * frame.pixel_width + x) * 4U + 3U];
            frame.pixels[(static_cast<std::size_t>(y) * frame.pixel_width + x) * 4U + 3U] = 255U;
        }
    }
    std::size_t new_bytes_per_line = 0U;
    auto data = smooth
                    ? graphics::smoothResize(width, height, frame.pixels, alpha,
                                              32U, frame.pixel_width,
                                              frame.pixel_height,
                                              static_cast<std::size_t>(frame.pixel_width) * 4U,
                                              new_bytes_per_line)
                    : graphics::resize(width, height, frame.pixels, alpha, 32U,
                                        frame.pixel_width, frame.pixel_height,
                                        static_cast<std::size_t>(frame.pixel_width) * 4U,
                                        new_bytes_per_line);
    if (new_bytes_per_line < static_cast<std::size_t>(width) * 4U) return std::nullopt;
    Frame result = std::move(frame);
    result.pixel_width = width;
    result.pixel_height = height;
    result.pixels = std::move(data);
    if (result.pixels.size() < checkedPixelBytes(width, height, 4U)) return std::nullopt;
    const auto row_stride = new_bytes_per_line;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto ai = static_cast<std::size_t>(y) * width + x;
            const auto di = static_cast<std::size_t>(y) * row_stride +
                            static_cast<std::size_t>(x) * 4U;
            if (di + 3U < result.pixels.size()) result.pixels[di + 3U] = alpha[ai];
        }
    }
    if (row_stride != static_cast<std::size_t>(width) * 4U) {
        Bytes compact(checkedPixelBytes(width, height, 4U));
        for (std::uint32_t y = 0; y < height; ++y) {
            std::copy_n(result.pixels.begin() + static_cast<std::ptrdiff_t>(y * row_stride),
                        static_cast<std::size_t>(width) * 4U,
                        compact.begin() + static_cast<std::ptrdiff_t>(y * width * 4U));
        }
        result.pixels = std::move(compact);
    }
    return result;
}

[[nodiscard]] std::optional<Frame> scaledFrame(const ImageDataWithScale& image,
                                               const std::uint32_t target_scale,
                                               const std::uint32_t fallback_width,
                                               const std::uint32_t fallback_height) {
    const auto scale = positiveScale(target_scale);
    if (!image.data.frames.empty()) {
        const auto converted = toBgra(image.data.frames.front());
        if (converted) {
            auto frame = *converted;
            const auto source_scale = positiveScale(image.source_scale);
            const auto logical_width = frame.logical_size.width > 0.0F
                                           ? frame.logical_size.width
                                           : static_cast<float>(frame.pixel_width) / source_scale;
            const auto logical_height = frame.logical_size.height > 0.0F
                                            ? frame.logical_size.height
                                            : static_cast<float>(frame.pixel_height) / source_scale;
            frame.logical_size = {logical_width, logical_height};
            const auto width = std::max<std::uint32_t>(1U, roundedDimension(logical_width * scale));
            const auto height = std::max<std::uint32_t>(1U, roundedDimension(logical_height * scale));
            if (frame.pixel_width != width || frame.pixel_height != height) {
                auto resized = resizeFrame(std::move(frame), width, height, false);
                if (!resized) return std::nullopt;
                frame = std::move(*resized);
            }
            return frame;
        }
    }
    const auto size = image.size();
    const auto width = fallback_width != 0U
                           ? fallback_width
                           : roundedDimension(size.width);
    const auto height = fallback_height != 0U
                            ? fallback_height
                            : roundedDimension(size.height);
    if (width == 0U || height == 0U) return std::nullopt;
    return makeFrame(logicalDimension(width, scale), logicalDimension(height, scale),
                     scale, {});
}

void applyEffects(Frame& frame, const jpy::Jpy1Sec& section,
                  const std::uint32_t target_scale) {
    auto width = static_cast<std::size_t>(frame.pixel_width);
    auto height = static_cast<std::size_t>(frame.pixel_height);
    if (width == 0U || height == 0U) return;
    Bytes alpha(checkedPixelBytes(frame.pixel_width, frame.pixel_height, 1U));
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        alpha[i] = frame.pixels[i * 4U + 3U];
        frame.pixels[i * 4U + 3U] = 255U;
    }
    auto bytes_per_line = width * 4U;
    if (section.colorexchange != jpy::Colorexchange::NONE) {
        static_cast<void>(graphics::colorexchange(section.colorexchange, frame.pixels,
                                                   alpha, 32U, width, height,
                                                   bytes_per_line));
    }
    if (section.filter != jpy::Filter::NONE) {
        static_cast<void>(graphics::filter(section.filter, frame.pixels, alpha,
                                           32U, width, height, bytes_per_line));
    }
    if (section.colormap != jpy::Colormap::NONE) {
        static_cast<void>(graphics::colormap(section.colormap, frame.pixels, alpha,
                                             32U, width, height, bytes_per_line));
    }
    if (section.flip) static_cast<void>(graphics::flip(frame.pixels, alpha, 32U,
                                                       width, height, bytes_per_line));
    if (section.mirror) static_cast<void>(graphics::mirror(frame.pixels, alpha, 32U,
                                                           width, height, bytes_per_line));
    if (section.noise != jpy::Noise::NONE && section.noisepoint != 0) {
        static_cast<void>(graphics::noise(section.noise, section.noisepoint,
                                           frame.pixels, alpha, 32U, width,
                                           height, bytes_per_line));
    }
    if (section.turn != jpy::Turn::NONE) {
        graphics::turn(frame.pixels, alpha, width, height, bytes_per_line,
                       section.turn, 32U);
    }
    auto new_width = static_cast<std::uint32_t>(width);
    auto new_height = static_cast<std::uint32_t>(height);
    if (section.width > 0) new_width = scaledDimension(section.width, target_scale);
    if (section.height > 0) new_height = scaledDimension(section.height, target_scale);
    if (new_width != frame.pixel_width || new_height != frame.pixel_height) {
        std::size_t new_bytes_per_line = 0U;
        auto resized = section.smooth
                           ? graphics::smoothResize(new_width, new_height, frame.pixels,
                                                     alpha, 32U, width, height,
                                                     bytes_per_line, new_bytes_per_line)
                           : graphics::resize(new_width, new_height, frame.pixels, alpha,
                                               32U, width, height, bytes_per_line,
                                               new_bytes_per_line);
        frame.pixels = std::move(resized);
        width = new_width;
        height = new_height;
        bytes_per_line = new_bytes_per_line;
    }
    if (section.mask != jpy::Mask::NONE) {
        static_cast<void>(graphics::mask(section.mask, frame.pixels, alpha, 32U,
                                         width, height, bytes_per_line));
    }
    frame.pixel_width = static_cast<std::uint32_t>(width);
    frame.pixel_height = static_cast<std::uint32_t>(height);
    const auto compact_stride = width * 4U;
    if (bytes_per_line != compact_stride) {
        Bytes compact(checkedPixelBytes(frame.pixel_width, frame.pixel_height, 4U));
        for (std::size_t y = 0; y < height; ++y) {
            std::copy_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(y * bytes_per_line),
                        compact_stride,
                        compact.begin() + static_cast<std::ptrdiff_t>(y * compact_stride));
        }
        frame.pixels = std::move(compact);
    }
    for (std::size_t i = 0; i < alpha.size(); ++i) frame.pixels[i * 4U + 3U] = alpha[i];
}

void applyTransparency(Frame& frame, const jpy::Jpy1Sec& section) {
    const bool d_keyed_overwrite =
        section.paintmode == jpy::Paintmode::AND || section.paintmode == jpy::Paintmode::OR;
    if ((!section.transparent && !d_keyed_overwrite) ||
        frame.pixel_width == 0U || frame.pixel_height == 0U) return;
    const auto first = frame.pixels;
    const Byte first_b = first[0];
    const Byte first_g = first[1];
    const Byte first_r = first[2];
    for (std::uint32_t y = 0; y < frame.pixel_height; ++y) {
        for (std::uint32_t x = 0; x < frame.pixel_width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * frame.pixel_width + x) * 4U;
            const auto b = frame.pixels[i];
            const auto g = frame.pixels[i + 1U];
            const auto r = frame.pixels[i + 2U];
            bool transparent = false;
            switch (section.paintmode) {
            case jpy::Paintmode::AND:
                transparent = (r == 255U && g == 255U && b == 255U) ||
                              (section.transparent && r == first_r && g == first_g && b == first_b);
                break;
            case jpy::Paintmode::OR:
                transparent = (r == 0U && g == 0U && b == 0U) ||
                              (section.transparent && r == first_r && g == first_g && b == first_b);
                break;
            default:
                transparent = r == first_r && g == first_g && b == first_b;
                break;
            }
            // D版と同様、AND/OR では処理範囲のアルファを置き換える。
            // 先行する画像効果で透明になった、キー色以外の画素も対象にする。
            if (d_keyed_overwrite) frame.pixels[i + 3U] = transparent ? 0U : 255U;
            else if (transparent) frame.pixels[i + 3U] = 0U;
        }
    }
}

void blendFrame(Frame& destination, const Frame& source, const std::int64_t dx,
                const std::int64_t dy, const jpy::Paintmode mode,
                const std::int32_t alpha_value) {
    if (destination.pixel_width == 0U || destination.pixel_height == 0U ||
        source.pixel_width == 0U || source.pixel_height == 0U) return;
    const auto opacity = static_cast<std::uint32_t>(std::clamp(alpha_value, 0, 255));
    for (std::uint32_t y = 0; y < source.pixel_height; ++y) {
        const auto target_y = static_cast<std::int64_t>(y) + dy;
        if (target_y < 0 || target_y >= destination.pixel_height) continue;
        for (std::uint32_t x = 0; x < source.pixel_width; ++x) {
            const auto target_x = static_cast<std::int64_t>(x) + dx;
            if (target_x < 0 || target_x >= destination.pixel_width) continue;
            const auto si = (static_cast<std::size_t>(y) * source.pixel_width + x) * 4U;
            const auto di = (static_cast<std::size_t>(target_y) * destination.pixel_width +
                             static_cast<std::size_t>(target_x)) * 4U;
            const auto source_alpha = static_cast<std::uint32_t>(source.pixels[si + 3U]) * opacity / 255U;
            if (source_alpha == 0U) continue;
            if (mode == jpy::Paintmode::AND || mode == jpy::Paintmode::OR) {
                if (mode == jpy::Paintmode::AND) {
                    destination.pixels[di] &= source.pixels[si];
                    destination.pixels[di + 1U] &= source.pixels[si + 1U];
                    destination.pixels[di + 2U] &= source.pixels[si + 2U];
                } else {
                    destination.pixels[di] |= source.pixels[si];
                    destination.pixels[di + 1U] |= source.pixels[si + 1U];
                    destination.pixels[di + 2U] |= source.pixels[si + 2U];
                }
                destination.pixels[di + 3U] = 255U;
                continue;
            }
            const auto inverse = 255U - source_alpha;
            destination.pixels[di] = static_cast<Byte>((source.pixels[si] * source_alpha +
                                                         destination.pixels[di] * inverse) / 255U);
            destination.pixels[di + 1U] = static_cast<Byte>((source.pixels[si + 1U] * source_alpha +
                                                             destination.pixels[di + 1U] * inverse) / 255U);
            destination.pixels[di + 2U] = static_cast<Byte>((source.pixels[si + 2U] * source_alpha +
                                                             destination.pixels[di + 2U] * inverse) / 255U);
            const auto destination_alpha = static_cast<std::uint32_t>(destination.pixels[di + 3U]);
            destination.pixels[di + 3U] = static_cast<Byte>(
                std::min<std::uint32_t>(255U, source_alpha + destination_alpha * inverse / 255U));
        }
    }
}

void drawInto(Frame& destination, const Frame& source, const std::int64_t x,
              const std::int64_t y, const std::uint32_t width,
              const std::uint32_t height) {
    if (width == 0U || height == 0U) return;
    auto copy = source;
    if (copy.pixel_width != width || copy.pixel_height != height) {
        const auto resized = resizeFrame(std::move(copy), width, height, false);
        if (!resized) return;
        copy = *resized;
    }
    blendFrame(destination, copy, x, y, jpy::Paintmode::NONE, 255);
}

[[nodiscard]] std::vector<std::filesystem::path> sectionDirectories(
    const jpy::Jpy1Sec& section, const std::filesystem::path& image_path,
    const jpy::Jpy1& jpy_file, const Skin* skin, const Summary* summ) {
    std::vector<std::filesystem::path> result;
    const auto current = image_path.parent_path();
    switch (section.dirtype) {
    case jpy::Dirtype::CURRENT:
        result.push_back(current);
        break;
    case jpy::Dirtype::TABLE:
        if (skin) for (const auto& value : skin->tableDirs()) result.push_back(pathValue(value));
        break;
    case jpy::Dirtype::SCHEME:
        if (skin) {
            if (skin->legacy()) {
                const auto engine = pathValue(skin->engine());
                const auto directory = engine.parent_path() / "scheme";
                if (regularDirectory(directory)) result.push_back(directory);
            } else if (!skin->path().empty()) {
                const auto directory = pathValue(skin->path()) / "EffectBooster";
                if (regularDirectory(directory)) result.push_back(directory);
            }
        }
        break;
    case jpy::Dirtype::SCENARIO:
        if (summ) {
            auto directory = pathValue(summ->scenarioPath());
            for (std::int32_t depth = 0; depth < jpy_file.dirdepth; ++depth) {
                directory = directory.parent_path();
            }
            result.push_back(directory);
        }
        break;
    case jpy::Dirtype::WAV:
        if (skin) for (const auto& value : skin->seDirs()) result.push_back(pathValue(value));
        break;
    case jpy::Dirtype::PARENT:
        result.push_back(current.parent_path());
        break;
    case jpy::Dirtype::PROGRAM:
        if (skin) result.push_back(pathValue(skin->engine()).parent_path());
        break;
    default:
        break;
    }
    return result;
}

[[nodiscard]] ImageDataWithScale decodeWithoutSkin(
    Props& prop, const String& path, const std::uint32_t target_scale,
    const lib::Size fallback) {
    ImageDataWithScale result;
    result.source_scale = positiveScale(target_scale);
    const auto& services = prop.images().services();
    if (services.image_codecs && regularFile(pathValue(path))) {
        lib::DecodeImageRequest request;
        request.path = pathValue(path);
        std::string error;
        try {
            result.data = services.image_codecs->decode(request, error);
        } catch (...) {
            result.data = {};
        }
        if (!result.data.frames.empty()) {
            result.loaded = true;
            return result;
        }
    }
    result.logical_size = fallback;
    return result;
}

[[nodiscard]] ImageDataWithScale loadSectionImage(
    Props& prop, const Skin* skin, const Summary* summ, const String& path,
    const std::uint32_t target_scale, const lib::Size fallback,
    std::vector<String> stratum) {
    const auto extension = pathValue(path).extension().string();
    if (utils::cfnmatch(extension, ".jpy1") ||
        utils::cfnmatch(extension, ".jptx") ||
        utils::cfnmatch(extension, ".jpdc")) {
        bool resizable = true;
        auto result = loadJPYImageWithScale(prop, skin, summ, path, target_scale,
                                             std::move(stratum), resizable);
        if (result.valid()) return result;
    }
    if (skin) {
        return areaviewutils::detail::detailLoadImage(prop, *skin, summ, path,
                                                       target_scale, fallback);
    }
    return decodeWithoutSkin(prop, path, target_scale, fallback);
}

[[nodiscard]] std::uint32_t cacheIndex(const jpy::Cache value) noexcept {
    const auto index = static_cast<std::int32_t>(value);
    return index >= 0 && index < static_cast<std::int32_t>(cache_count)
               ? static_cast<std::uint32_t>(index)
               : 0U;
}

[[nodiscard]] lib::Size imageSize(const ImageDataWithScale& image) noexcept {
    return image.size();
}

[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> textExtent(
    Props& prop, const String& text, const jpy::JptxParam& param,
    const jpy::Jptx& jptx, const std::uint32_t target_scale) {
    const auto pixels = std::max<std::int32_t>(1, param.pixels >= 0 ? param.pixels : jptx.fontpixels);
    const auto face = param.face.empty() ? jptx.fontface : param.face;
    const auto& services = prop.images().services();
    if (services.graphics) {
        lib::FontSpec font;
        font.family = face;
        font.points = static_cast<float>(std::fabs(static_cast<double>(pixels)));
        font.size_mode = pixels > 0 ? lib::FontSizeMode::CellPixels
            : (pixels < 0 ? lib::FontSizeMode::CharacterPixels
                          : lib::FontSizeMode::DefaultHeight);
        font.weight = param.b ? 700U : 400U;
        font.italic = param.i;
        font.underline = param.u;
        font.strikeout = param.s;
        const auto measured = services.graphics->measure_text(text, font, 0.0F);
        return {std::max<std::uint32_t>(1U, roundedDimension(measured.width * positiveScale(target_scale))),
                std::max<std::uint32_t>(1U, roundedDimension(measured.height * positiveScale(target_scale)))};
    }
    const auto width = static_cast<std::uint32_t>(std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::ceil(text.size() * pixels * 0.6))));
    const auto height = static_cast<std::uint32_t>(std::max<std::int32_t>(1, pixels * 6 / 5));
    return {width * positiveScale(target_scale), height * positiveScale(target_scale)};
}

void drawTextBars(Frame& frame, const std::vector<std::size_t>& lengths,
                  const Byte red, const Byte green, const Byte blue) {
    if (frame.pixel_width == 0U || frame.pixel_height == 0U) return;
    std::uint32_t y = 2U;
    for (const auto length : lengths) {
        if (y >= frame.pixel_height) break;
        const auto bar_width = std::min<std::uint32_t>(frame.pixel_width - 4U,
            static_cast<std::uint32_t>(std::max<std::size_t>(4U, length * 6U)));
        const auto bar_height = std::min<std::uint32_t>(3U, frame.pixel_height - y);
        for (std::uint32_t yy = 0; yy < bar_height; ++yy) {
            for (std::uint32_t x = 2U; x < 2U + bar_width; ++x) {
                const auto i = (static_cast<std::size_t>(y + yy) * frame.pixel_width + x) * 4U;
                frame.pixels[i] = blue;
                frame.pixels[i + 1U] = green;
                frame.pixels[i + 2U] = red;
                frame.pixels[i + 3U] = 255U;
            }
        }
        y += 8U;
    }
}

}  // namespace

ImageDataWithScale loadJPYImageWithScale(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    const std::uint32_t target_scale, std::vector<String> stratum,
    bool& resizable) {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    return loadJPYImageWithScale(prop, skin, summ, std::move(path), target_scale,
                                 std::move(stratum), width, height, resizable);
}

ImageDataWithScale loadJPYImageWithScale(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    const std::uint32_t target_scale, std::vector<String> stratum,
    std::uint32_t& width, std::uint32_t& height, bool& resizable) {
    width = 0U;
    height = 0U;
    resizable = true;
    try {
        const auto extension = pathValue(path).extension().string();
        if (utils::cfnmatch(extension, ".jpy1")) {
            auto image = loadJPYImageImpl(prop, skin, summ, std::move(path), target_scale,
                                           std::move(stratum));
            if (image.valid()) {
                const auto size = imageSize(image);
                width = roundedDimension(size.width);
                height = roundedDimension(size.height);
                return image;
            }
        } else if (utils::cfnmatch(extension, ".jptx")) {
            auto image = loadJPTXImage(prop, std::move(path), target_scale);
            const auto size = imageSize(image);
            width = roundedDimension(size.width);
            height = roundedDimension(size.height);
            return image;
        } else if (utils::cfnmatch(extension, ".jpdc")) {
            auto image = loadJPDCImage(prop, summ ? summ->scenarioPath() : String{},
                                       std::move(path), target_scale);
            const auto size = imageSize(image);
            width = roundedDimension(size.width);
            height = roundedDimension(size.height);
            resizable = false;
            return image;
        }
    } catch (const jpy::EffectBoosterError& error) {
        resizable = false;
        auto image = warningImage(prop, summ, error);
        if (!image.frames.empty()) {
            const auto& frame = image.frames.front();
            width = roundedDimension(frame.logical_size.width > 0.0F
                                         ? frame.logical_size.width
                                         : static_cast<float>(frame.pixel_width));
            height = roundedDimension(frame.logical_size.height > 0.0F
                                          ? frame.logical_size.height
                                          : static_cast<float>(frame.pixel_height));
        }
        return ImageDataWithScale(std::move(image), 1U);
    } catch (const std::exception&) {
        width = 0U;
        height = 0U;
    }
    const auto scale = positiveScale(target_scale);
    // D版と同様、未解決ファイルには透明な代替ラスターを返すが、作成済みの
    // 内容としては公開しない。 呼び出し元は論理寸法をゼロのままにする。
    width = 0U;
    height = 0U;
    return wrapFrame(makeFrame(1U, 1U, scale, {0U, 0U, 0U, 0U}), scale);
}

ImageDataWithScale loadJPYImageImpl(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    const std::uint32_t target_scale, std::vector<String> stratum) {
    const auto source_scale = positiveScale(target_scale);
    const auto normalized_path = utils::nabs(StringView{path});
    // 再帰処理に入る前に、処理中の描画パスを意図的に登録・検査する。 D版は loadImageWithScale
    // が戻った後に入れ子のパスを追加するため、 最初の自己参
    // 照や、相互参照するJPYファイルの再帰を止められない。
    if (std::find(stratum.begin(), stratum.end(), normalized_path) != stratum.end()) {
        jpy::EffectBoosterError error;
        error.add("Recursive EffectBooster image reference", path, 0U);
        throw error;
    }
    stratum.push_back(normalized_path);
    const auto jpy_file = jpy::Jpy1::load(prop.parent(),
                                          summ ? summ->scenarioPath() : String{}, path);
    if (jpy_file.sections.empty()) return {};
    const auto& initial = *jpy_file.sections.front();
    auto base_width = default_width;
    auto base_height = default_height;
    if (initial.backwidth >= 0) base_width = static_cast<std::uint32_t>(initial.backwidth);
    if (initial.backheight >= 0) base_height = static_cast<std::uint32_t>(initial.backheight);
    if (base_width == 0U || base_height == 0U) return {};

    auto image_path = pathValue(utils::nabs(StringView{path}));
    Frame canvas = makeFrame(base_width, base_height, source_scale, {0, 0, 0, 0});
    std::array<std::optional<Frame>, cache_count> cache;
    for (std::size_t index = 0; index < jpy_file.sections.size(); ++index) {
        const auto& section = *jpy_file.sections[index];
        if (section.animation != jpy::Animation::NONE &&
            section.animation != jpy::Animation::DRAW) continue;

        const auto section_width = index == 0U || section.width < 0
                                       ? scaledDimension(static_cast<std::int32_t>(base_width), source_scale)
                                       : scaledDimension(section.width, source_scale);
        const auto section_height = index == 0U || section.height < 0
                                        ? scaledDimension(static_cast<std::int32_t>(base_height), source_scale)
                                        : scaledDimension(section.height, source_scale);
        // 指定寸法がゼロでも元画像は読み込む。 D版と同
        // 様、未指定の寸法には元画像固有の寸法を使う。
        std::optional<Frame> source;
        if (section.loadcache != jpy::Cache::NONE) {
            const auto cache_value = cache[cacheIndex(section.loadcache)];
            if (cache_value) source = *cache_value;
            else {
                if (section.savecache != jpy::Cache::NONE) cache[cacheIndex(section.savecache)].reset();
                continue;
            }
        }
        if (!source && !section.filename.empty() &&
            !utils::cfnmatch(pathValue(section.filename).extension().string(), ".wav")) {
            const auto directories = sectionDirectories(section, image_path, jpy_file, skin, summ);
            const auto fallback = lib::Size{static_cast<float>(logicalDimension(section_width, source_scale)),
                                           static_cast<float>(logicalDimension(section_height, source_scale))};
            for (const auto& directory : directories) {
                const auto candidate = directory / pathValue(section.filename);
                if (!regularFile(candidate)) continue;
                const auto candidate_name = pathString(candidate);
                auto loaded = loadSectionImage(prop, skin, summ, candidate_name,
                                               section.dirtype == jpy::Dirtype::CURRENT ||
                                                       section.dirtype == jpy::Dirtype::SCENARIO ||
                                                       section.dirtype == jpy::Dirtype::PARENT
                                                   ? prop.drawingScaleForImage(summ)
                                                   : source_scale,
                                                fallback, stratum);
                source = scaledFrame(loaded, source_scale,
                                     logicalDimension(section_width, source_scale),
                                     logicalDimension(section_height, source_scale));
                if (source) break;
            }
        }

        const auto data_width = source ? source->pixel_width : section_width;
        const auto data_height = source ? source->pixel_height : section_height;
        if (data_width == 0U || data_height == 0U) {
            if (section.savecache != jpy::Cache::NONE) cache[cacheIndex(section.savecache)].reset();
            continue;
        }
        const auto background = index == 0U ? initial.backcolor : section.color;
        Frame section_image = makeFrame(logicalDimension(data_width, source_scale),
                                        logicalDimension(data_height, source_scale),
                                        source_scale, background);
        if (source) {
            if (section.clip.width > 0 && section.clip.height > 0) {
                // D版と同様、切り抜き矩形は元画像の座標として扱う。 その画素を
                // 元画像全体の寸法へ引き伸ばし、出力先の (0,0) に描く。
                const auto x = scaledCoordinate(section.clip.x, source_scale);
                const auto y = scaledCoordinate(section.clip.y, source_scale);
                const auto width = scaledDimension(section.clip.width, source_scale);
                const auto height = scaledDimension(section.clip.height, source_scale);
                if (x < 0 || y < 0 || x + width > source->pixel_width ||
                    y + height > source->pixel_height)
                    throw std::out_of_range("JPY source clip exceeds image bounds");
                Frame clipped;
                clipped.pixel_width = width; clipped.pixel_height = height;
                clipped.logical_size = {float(width) / source_scale, float(height) / source_scale};
                clipped.pixels.resize(checkedPixelBytes(width, height, 4U));
                for (std::uint32_t row = 0; row < height; ++row)
                    std::copy_n(source->pixels.begin() + static_cast<std::ptrdiff_t>(
                        ((y + row) * source->pixel_width + x) * 4), std::size_t(width) * 4,
                        clipped.pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t(row) * width * 4));
                drawInto(section_image, clipped, 0, 0, source->pixel_width, source->pixel_height);
            } else {
                drawInto(section_image, *source, 0, 0, source->pixel_width,
                         source->pixel_height);
            }
        }
        applyEffects(section_image, section, source_scale);
        applyTransparency(section_image, section);
        if (section.savecache != jpy::Cache::NONE) cache[cacheIndex(section.savecache)] = section_image;
        if (section.visible && section.paintmode != jpy::Paintmode::NO_PAINT) {
            const auto alpha = section.paintmode == jpy::Paintmode::BLEND ? section.alpha : 255;
            auto paintmode = section.paintmode;
            // D版と同様、AND/OR はすべての表示で白・黒のカラーキーに
            // よる上書きを選ぶ。 出力先とのビット演算による合成ではない。
            if (paintmode == jpy::Paintmode::AND || paintmode == jpy::Paintmode::OR)
                paintmode = jpy::Paintmode::NONE;
            blendFrame(canvas, section_image, section.position.x, section.position.y,
                       paintmode, alpha);
        }
    }
    return wrapFrame(std::move(canvas), source_scale);
}

ImageDataWithScale loadJPTXImage(Props& prop, String path,
                                 const std::uint32_t target_scale) {
    const auto source_scale = positiveScale(target_scale);
    auto jptx = jpy::Jptx::load(prop.parent(), path);
    if (jptx.backwidth == 0 || jptx.backheight == 0) return {};
    std::uint32_t auto_width = 1U;
    std::uint32_t auto_height = 1U;
    const auto coordinate_limit = static_cast<std::int64_t>(
        std::numeric_limits<std::uint32_t>::max());
    // D版と同様、文字列断片の位置は符号付きで保持する。 キャン
    // バス外の字形は原点へ戻さず、描画バックエンドで切り取る。
    const auto shift_position = [coordinate_limit](
        std::int64_t value, std::int64_t shift) -> std::int64_t {
        if (shift > coordinate_limit - value) return coordinate_limit;
        if (shift < -coordinate_limit - value) return -coordinate_limit;
        return value + shift;
    };
    const auto advance_position = [coordinate_limit](
        std::int64_t value, std::uint32_t extent) -> std::int64_t {
        return std::min(coordinate_limit, value + extent);
    };
    const auto positive_bound = [coordinate_limit](std::int64_t value) {
        return static_cast<std::uint32_t>(std::clamp(value, std::int64_t{0}, coordinate_limit));
    };
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::uint32_t line_count = 0U;
    jptx.parse([&](String text, const jpy::JptxParam& param) {
        const auto shift_x = scaledCoordinate(param.shiftx, source_scale);
        const auto shift_y = scaledCoordinate(param.shifty, source_scale);
        x = shift_position(x, shift_x);
        y = shift_position(y, shift_y);
        const auto extent = textExtent(prop, text, param, jptx, source_scale);
        if (text == "\n") {
            const auto line_height = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(extent.second) *
                static_cast<std::uint32_t>(std::max(1, param.lineheight)) / 100U);
            y = advance_position(y, line_height);
            x = 0U;
            return;
        }
        x = advance_position(x, extent.first);
        auto_width = std::max(auto_width, positive_bound(x));
        const auto bottom = positive_bound(y + extent.second);
        if (bottom > auto_height) {
            auto_height = bottom;
            ++line_count;
        }
    });
    if ((line_count & 1U) != 0U && auto_height < std::numeric_limits<std::uint32_t>::max()) ++auto_height;
    const auto width = jptx.backwidth < 0
                           ? std::max<std::uint32_t>(1U, logicalDimension(auto_width, source_scale))
                           : static_cast<std::uint32_t>(jptx.backwidth);
    const auto height = jptx.backheight < 0
                            ? std::max<std::uint32_t>(1U, logicalDimension(auto_height, source_scale))
                            : static_cast<std::uint32_t>(jptx.backheight);
    Frame frame = makeFrame(width, height, source_scale, jptx.backcolor);
    if (jptx.fonttransparent && frame.pixel_height != 0U) {
        for (std::uint32_t x_pos = 0; x_pos < frame.pixel_width; ++x_pos) {
            const auto i = static_cast<std::size_t>(x_pos) * 4U;
            frame.pixels[i] = static_cast<Byte>(std::min<std::uint32_t>(jptx.fontcolor.b, 255U));
            frame.pixels[i + 1U] = static_cast<Byte>(std::min<std::uint32_t>(jptx.fontcolor.g, 255U));
            frame.pixels[i + 2U] = static_cast<Byte>(std::min<std::uint32_t>(jptx.fontcolor.r, 255U));
            frame.pixels[i + 3U] = static_cast<Byte>(std::min<std::uint32_t>(jptx.fontcolor.a, 255U));
        }
    }
    auto result = wrapFrame(std::move(frame), source_scale);
    const auto& services = prop.images().services();
    if (!services.graphics || result.data.frames.empty()) return result;

    // 計測とラスタライズでは、同じ解析済みの文字列断片を使う。
    // 自動幅・高さを計算して背景だけを返すのではなく、 D版
    // と同様にすべてのJPTXの文字列断片を最終画像へ描く。
    std::int64_t draw_x = 0;
    std::int64_t draw_y = 0;
    jptx.parse([&](String text, const jpy::JptxParam& param) {
        const auto shift_x = scaledCoordinate(param.shiftx, source_scale);
        const auto shift_y = scaledCoordinate(param.shifty, source_scale);
        draw_x = shift_position(draw_x, shift_x);
        draw_y = shift_position(draw_y, shift_y);
        const auto extent = textExtent(prop, text, param, jptx, source_scale);
        if (text == "\n") {
            const auto line_height = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(extent.second) *
                static_cast<std::uint32_t>(std::max(1, param.lineheight)) / 100U);
            draw_y = advance_position(draw_y, line_height);
            draw_x = 0U;
            return;
        }
        const auto pixels = std::max<std::int32_t>(
            1, param.pixels >= 0 ? param.pixels : jptx.fontpixels);
        lib::FontSpec font;
        font.family = param.face.empty() ? jptx.fontface : param.face;
        // D版と同様、基準フォントの字形を1倍で計測・ラスタライズし、 shrinkDrawText
        // でそのマスクを倍率適用済みのJPTX寸法へ変更する。
        font.points = static_cast<float>(std::fabs(static_cast<double>(pixels)));
        font.size_mode = pixels > 0 ? lib::FontSizeMode::CellPixels
            : (pixels < 0 ? lib::FontSizeMode::CharacterPixels
                          : lib::FontSizeMode::DefaultHeight);
        font.weight = param.b ? 700U : 400U;
        font.italic = param.i;
        font.underline = param.u;
        font.strikeout = param.s;
        font.antialias = true;
        const lib::Color color{
            static_cast<std::uint8_t>(std::min<std::uint32_t>(param.color.r, 255U)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(param.color.g, 255U)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(param.color.b, 255U)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(param.color.a, 255U))};
        lib::ImageTextStyle style{font,color,{0,0,0,0},0.0F,true};
        style.target_size=lib::Size{
            static_cast<float>(std::max<std::uint32_t>(1U,extent.first)),
            static_cast<float>(std::max<std::uint32_t>(1U,extent.second))};
        style.preserve_font_antialias=true;
        auto caption=lib::rasterize_image_text(services.graphics,text,std::move(style));
        if(caption.pixel_width && caption.pixel_height && !result.data.frames.empty()) {
            auto& destination=result.data.frames.front();
            (void)lib::composite_image_pixels(destination,caption,
                {static_cast<float>(draw_x)-1.0F,
                 static_cast<float>(draw_y)-1.0F},
                {0,0,static_cast<float>(destination.pixel_width),
                 static_cast<float>(destination.pixel_height)});
        }
        draw_x = advance_position(draw_x, extent.first);
    });
    return result;
}

ImageData warningImage(Props& prop, const Summary* summ,
                       const jpy::EffectBoosterError& error) {
    const auto& errors = error.errors();
    if (errors.empty()) return {};
    const auto scenario_root = summ ? summ->scenarioPath() : String{};
    std::vector<String> messages;
    messages.reserve(errors.size());
    std::vector<std::size_t> lengths;
    lengths.reserve(errors.size());
    std::uint32_t width = 64U;
    for (const auto& value : errors) {
        const auto file_path = pathValue(value.file);
        const String file = summ && !scenario_root.empty() &&
                                    utils::isSubDirectory(StringView{value.file}, StringView{scenario_root})
                                ? utils::abs2rel(StringView{value.file}, StringView{scenario_root})
                                : pathString(file_path.filename());
        auto message = utils::tryFormat(
            prop.requiredMsgs().jpyError.value, value.msg, file, value.line);
        const auto length = message.size();
        lengths.push_back(length);
        messages.push_back(std::move(message));
        const auto candidate = static_cast<std::uint64_t>(length) * 6U + 32U;
        width = std::max(width, candidate > std::numeric_limits<std::uint32_t>::max()
                                  ? std::numeric_limits<std::uint32_t>::max()
                                  : static_cast<std::uint32_t>(candidate));
    }
    const auto height64 = static_cast<std::uint64_t>(errors.size()) * 16U + 4U;
    const auto height = height64 > std::numeric_limits<std::uint32_t>::max()
                            ? std::numeric_limits<std::uint32_t>::max()
                            : static_cast<std::uint32_t>(height64);
    auto frame = makeFrame(width, height, 1U, {255U, 255U, 255U, 255U});
    const auto& services = prop.images().services();
    if (!services.graphics) drawTextBars(frame, lengths, 0U, 0U, 0U);
    auto result = wrapFrame(std::move(frame), 1U).data;
    String message;
    for (const auto& line : messages) {
        if (!message.empty()) message.push_back('\n');
        message += line;
    }
    if (services.graphics)
        (void)services.graphics->draw_text_to_image(
            result, 0U, message, {4.0F, 2.0F,
                                  static_cast<float>(width - 8U),
                                  static_cast<float>(height - 4U)},
            {}, {0U, 0U, 0U, 255U});
    return result;
}

ImageDataWithScale loadJPDCImage(Props& prop, String scenario_path, String path,
                                 const std::uint32_t target_scale) {
    const auto source_scale = positiveScale(target_scale);
    const auto jpdc = jpy::Jpdc::load(prop.parent(), scenario_path, path);
    const auto width = jpdc->clip.width;
    const auto height = jpdc->clip.height;
    if (width <= 0 || height <= 0) return {};
    Frame frame = makeFrame(static_cast<std::uint32_t>(width),
                            static_cast<std::uint32_t>(height), source_scale,
                            {255U, 255U, 255U, 255U});
    const auto text_length = jpdc->saveFileName.empty()
                                 ? std::size_t(26U)
                                 : jpdc->saveFileName.size() + 16U;
    const auto pixel_width = frame.pixel_width;
    const auto pixel_height = frame.pixel_height;
    const auto& services = prop.images().services();
    if (!services.graphics) drawTextBars(frame, {text_length}, 0U, 0U, 0U);
    auto result = wrapFrame(std::move(frame), source_scale);
    const String label = String("JPDC Save to: ") +
        (jpdc->saveFileName.empty() ? String("(undefined)") : jpdc->saveFileName);
    if (services.graphics) {
        const auto wrap_width = static_cast<float>(pixel_width > 4U
                                                       ? pixel_width - 4U : 1U);
        const lib::FontSpec font{};
        std::vector<String> lines;
        String line;
        // D版の逐次計測による折り返し規則を保ち、C++版のUTF-8の各スカラー値を一つの描画文字として扱う。
        for (std::size_t offset = 0U; offset < label.size();) {
            const StringView remaining(label.data() + offset, label.size() - offset);
            auto scalar_tail = remaining;
            (void)lib::utf8::consume_scalar(scalar_tail);
            const auto scalar_size = remaining.size() - scalar_tail.size();
            String scalar(label.data() + offset, scalar_size);
            auto candidate = line + scalar;
            const auto extent = services.graphics->measure_text(candidate, font, 0.0F);
            if (!line.empty() && extent.width > wrap_width) {
                lines.push_back(std::move(line));
                line = std::move(scalar);
            } else {
                line = std::move(candidate);
            }
            offset += scalar_size;
        }
        if (!line.empty()) lines.push_back(std::move(line));

        float y = 2.0F;
        for (const auto& row : lines) {
            const auto extent = services.graphics->measure_text(row, font, 0.0F);
            const auto row_height = std::max(1.0F, extent.height);
            (void)services.graphics->draw_text_to_image(
                result.data, 0U, row,
                {2.0F, y, wrap_width, row_height},
                font, {0U, 0U, 0U, 255U});
            y += row_height;
        }
    }
    return result;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::jpyimage
