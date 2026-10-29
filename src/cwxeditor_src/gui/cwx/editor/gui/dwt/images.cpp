#include "images.hpp"
#include "standalone_webp.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include <fstream>
#include <array>
#include <cstring>
#include "cellrender.hpp"
#include "cwxeditor_src/lib/image_mask.hpp"
#include "cwxeditor_src/lib/image_text.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"

#include <cmath>
#include <cctype>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

namespace cwxeditor::cwx::editor::gui::dwt::images {
namespace {

using Byte = std::uint8_t;
using Frame = lib::ImageFrame;

constexpr std::int32_t clampInt(const std::int32_t value,
                                const std::int32_t low = 0,
                                const std::int32_t high = 255) noexcept {
    return std::max(low, std::min(high, value));
}

constexpr Byte toByte(const std::int32_t value) noexcept {
    return static_cast<Byte>(clampInt(value));
}

constexpr std::uint32_t safeScale(const std::uint32_t value) noexcept {
    return value == 0U ? 1U : value;
}

[[nodiscard]] std::uint32_t dpiScale(const lib::Services& services) noexcept {
    if (!services.system) return NORMAL_SCALE;
    try {
        const auto factor = services.system->scale_factor();
        if (!std::isfinite(factor) || factor <= 0.0F) return NORMAL_SCALE;
        // DWT の dpiMuls と同様、DPI/96 の小数部分を切り捨てた整
        // 数を使う。 125% DPIでもエディタの論理サイズを変えないようにする。
        return std::max<std::uint32_t>(
            NORMAL_SCALE, static_cast<std::uint32_t>(std::floor(factor)));
    } catch (...) {
        return NORMAL_SCALE;
    }
}

[[nodiscard]] bool regularFile(const std::filesystem::path& value) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error) && !error;
}

[[nodiscard]] std::filesystem::path scaledPath(
    const std::filesystem::path& value, std::uint32_t target_scale,
    std::uint32_t& source_scale) {
    source_scale = NORMAL_SCALE;
    if (target_scale <= NORMAL_SCALE) return regularFile(value) ? value : std::filesystem::path{};

    const auto extension = value.extension();
    auto stem = value;
    stem.replace_extension();
    auto candidate_scale = target_scale;
    while (candidate_scale > NORMAL_SCALE) {
        auto candidate = stem;
        candidate += structs::pathFromUtf8(".x" + std::to_string(candidate_scale));
        candidate += extension;
        if (regularFile(candidate)) {
            source_scale = candidate_scale;
            return candidate;
        }
        // 指定倍率から整数で半分ずつ下げて拡大素材を探し、見つからなけれ
        // ば元ファイルを使う。通常の素材倍率は2x/4x/8x/16x。
        candidate_scale /= 2U;
    }
    return regularFile(value) ? value : std::filesystem::path{};
}

[[nodiscard]] lib::ImageFormat formatForPath(
    const std::filesystem::path& value) noexcept {
    auto extension = value.extension().string();
    for (auto& character : extension)
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    if (extension == ".bmp") return lib::ImageFormat::Bitmap;
    if (extension == ".png") return lib::ImageFormat::Png;
    if (extension == ".jpg" || extension == ".jpeg") return lib::ImageFormat::Jpeg;
    if (extension == ".gif") return lib::ImageFormat::Gif;
    if (extension == ".webp") return lib::ImageFormat::WebP;
    if (extension == ".ico") return lib::ImageFormat::Icon;
    if (extension == ".cur") return lib::ImageFormat::Cursor;
    return lib::ImageFormat::Unknown;
}

[[nodiscard]] std::vector<std::filesystem::path> resourceCandidates(
    StringView requested, const lib::Services& services) {
    std::vector<std::filesystem::path> result;
    if (requested.empty()) return result;
    const auto path = structs::pathFromUtf8(requested);
    const auto appendUnique = [&result](std::filesystem::path value) {
        if (value.empty()) return;
        value = value.lexically_normal();
        if (std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(std::move(value));
    };
    appendUnique(path);
    std::error_code error;
    if (!path.is_absolute()) {
        const auto current = std::filesystem::absolute(path, error);
        if (!error) appendUnique(current);
    }
    if (services.system) {
        try {
            const auto executable = services.system->executable_path();
            if (!executable.empty()) {
                const auto directory = executable.parent_path();
                appendUnique(directory / std::filesystem::path("resource") / path);
                appendUnique(directory / path);
            }
            const auto application = services.system->special_directory(
                lib::SpecialDirectory::Application);
            if (!application.empty()) {
                appendUnique(application / std::filesystem::path("resource") / path);
                appendUnique(application / path);
            }
        } catch (...) {
        }
    }
    return result;
}

[[nodiscard]] lib::ImageData decodeEncoded(
    const lib::Services& services, std::span<const std::byte> encoded,
    const std::filesystem::path& source, std::string& error) {
    if (!services.image_codecs || encoded.empty()) return {};
    lib::DecodeImageRequest request;
    request.encoded_data = encoded;
    request.format_hint = formatForPath(source);
    try {
        auto native_webp = standalone_webp::decode(encoded);
        if (!native_webp.frames.empty()) return native_webp;
        return services.image_codecs->decode(request, error);
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "image codec threw an unknown exception";
    }
    return {};
}

[[nodiscard]] lib::ImageData decodeFile(
    const lib::Services& services, const std::filesystem::path& source,
    std::string& error) {
    if (!services.image_codecs || !regularFile(source)) return {};
    try {
    if (!lib::editor_window_host_options().all_mode) {
        // 他の形式はヘッダーだけを調べ、既存のコーデックと ALL の処理経路を維持
        // する。 WebPのバイト列の復号は、符号化済みリソースの経路を共有する。
        std::ifstream input(source, std::ios::binary);
        std::array<char, 12> header{};
        if (input.read(header.data(), header.size()) &&
            std::memcmp(header.data(), "RIFF", 4) == 0 &&
            std::memcmp(header.data() + 8, "WEBP", 4) == 0) {
            input.seekg(0, std::ios::end);
            const auto length = input.tellg();
            if (length > 0) {
                input.seekg(0);
                std::vector<std::byte> encoded(static_cast<std::size_t>(length));
                if (input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()))) {
                    auto decoded = decodeEncoded(services, encoded, source, error);
                    if (!decoded.frames.empty()) return decoded;
                }
            }
        }
    }
    } catch (...) {
        // 下にある既存のコーデックの失敗処理と代替経路を維持する。
    }
    lib::DecodeImageRequest request;
    request.path = source;
    request.format_hint = formatForPath(source);
    try {
        return services.image_codecs->decode(request, error);
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "image codec threw an unknown exception";
    }
    return {};
}

[[nodiscard]] lib::ImageData decodeEmbedded(
    const lib::Services& services, const std::filesystem::path& executable,
    StringView requested, const std::filesystem::path& hint,
    std::string& error) {
    if (!services.system || !services.image_codecs || executable.empty() ||
        requested.empty()) return {};
    std::vector<String> names;
    names.emplace_back(requested);
    const auto filename = structs::pathToUtf8(hint.filename());
    if (!filename.empty() &&
        std::find(names.begin(), names.end(), filename) == names.end())
        names.emplace_back(filename);
    for (const auto& name : names) {
        for (const StringView type : {StringView{"RCDATA"}, StringView{"IMAGE"}}) {
            std::string resource_error;
            auto bytes = services.system->executable_resource(
                executable, type, name, resource_error);
            if (bytes.empty()) {
                error = std::move(resource_error);
                continue;
            }
            auto decoded = decodeEncoded(
                services, std::span<const std::byte>(bytes.data(), bytes.size()),
                hint, resource_error);
            if (!decoded.frames.empty()) {
                error.clear();
                return decoded;
            }
            error = std::move(resource_error);
        }
    }
    return {};
}

constexpr std::int32_t scaledDimension(const std::int32_t value,
                                       const std::uint32_t scale) noexcept {
    if (value <= 0) return 0;
    const auto wide = static_cast<std::int64_t>(value) *
                      static_cast<std::int64_t>(safeScale(scale));
    return static_cast<std::int32_t>(std::min<std::int64_t>(
        wide, std::numeric_limits<std::int32_t>::max()));
}

constexpr bool validRect(const Rectangle& value) noexcept {
    return value.width > 0 && value.height > 0;
}

constexpr bool contains(const Rectangle& rect, const std::int32_t x,
                        const std::int32_t y) noexcept {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

constexpr bool intersects(const Rectangle& left,
                          const Rectangle& right) noexcept {
    return left.x < right.x + right.width &&
           right.x < left.x + left.width &&
           left.y < right.y + right.height &&
           right.y < left.y + left.height;
}

lib::Rect asLibRect(const Rectangle value, const float scale = 1.0F) noexcept {
    return {value.x * scale, value.y * scale, value.width * scale,
            value.height * scale};
}

lib::Color asColor(const structs::CRGB value) noexcept {
    return {toByte(static_cast<std::int32_t>(value.r)),
            toByte(static_cast<std::int32_t>(value.g)),
            toByte(static_cast<std::int32_t>(value.b)),
            toByte(static_cast<std::int32_t>(value.a))};
}

lib::FontSpec asFont(const structs::CFont& value,
                     const bool underline = false,
                     const bool strike = false) {
    lib::FontSpec result;
    result.family = value.name;
    result.points = value.point == 0U ? 10.0F
                                     : static_cast<float>(value.point);
    result.weight = value.bold ? 700U : 400U;
    result.italic = value.italic;
    result.underline = underline;
    result.strikeout = strike;
    // D版ではゼロをプラットフォームの既定の高さとして扱う。 共通の提供処理がネ
    // イティブの既定寸法を解決する間、移植版の初期値として 10 を維持する。
    result.size_mode = value.point > 0U ? lib::FontSizeMode::CellPixels
                                        : lib::FontSizeMode::DefaultHeight;
    return result;
}

const Frame* firstFrame(const ImageDataWithScale& value) noexcept {
    return value.data.frames.empty() ? nullptr : &value.data.frames.front();
}

Frame* firstFrame(lib::ImageData& value) noexcept {
    return value.frames.empty() ? nullptr : &value.frames.front();
}

std::size_t bytesPerPixel(const lib::PixelFormat format) noexcept {
    switch (format) {
    case lib::PixelFormat::Gray8: return 1U;
    case lib::PixelFormat::RGB24:
    case lib::PixelFormat::BGR24: return 3U;
    case lib::PixelFormat::RGBA32:
    case lib::PixelFormat::BGRA32: return 4U;
    default: return 0U;
    }
}

struct Pixel {
    Byte r{};
    Byte g{};
    Byte b{};
    Byte a{};
};

Pixel readPixel(const Frame& frame, const std::uint32_t x,
                const std::uint32_t y) noexcept {
    if (x >= frame.pixel_width || y >= frame.pixel_height) return {};
    const auto bpp = bytesPerPixel(frame.pixel_format);
    if (bpp == 0U) return {};
    const auto stride = static_cast<std::size_t>(frame.pixel_width) * bpp;
    const auto offset = static_cast<std::size_t>(y) * stride +
                        static_cast<std::size_t>(x) * bpp;
    if (offset + bpp > frame.pixels.size()) return {};
    const auto get = [&](const std::size_t index) {
        return std::to_integer<Byte>(frame.pixels[offset + index]);
    };
    switch (frame.pixel_format) {
    case lib::PixelFormat::Gray8:
        return {get(0U), get(0U), get(0U), 255U};
    case lib::PixelFormat::RGB24:
        return {get(0U), get(1U), get(2U), 255U};
    case lib::PixelFormat::BGR24:
        return {get(2U), get(1U), get(0U), 255U};
    case lib::PixelFormat::RGBA32:
        return {get(0U), get(1U), get(2U), get(3U)};
    case lib::PixelFormat::BGRA32:
        return {get(2U), get(1U), get(0U), get(3U)};
    default:
        return {};
    }
}

void writePixel(Frame& frame, const std::uint32_t x, const std::uint32_t y,
                const Pixel value) noexcept {
    if (x >= frame.pixel_width || y >= frame.pixel_height) return;
    const auto bpp = bytesPerPixel(frame.pixel_format);
    if (bpp == 0U) return;
    const auto stride = static_cast<std::size_t>(frame.pixel_width) * bpp;
    const auto offset = static_cast<std::size_t>(y) * stride +
                        static_cast<std::size_t>(x) * bpp;
    if (offset + bpp > frame.pixels.size()) return;
    const auto put = [&](const std::size_t index, const Byte byte) {
        frame.pixels[offset + index] = static_cast<std::byte>(byte);
    };
    switch (frame.pixel_format) {
    case lib::PixelFormat::Gray8:
        put(0U, static_cast<Byte>((static_cast<unsigned>(value.r) * 77U +
                                   static_cast<unsigned>(value.g) * 150U +
                                   static_cast<unsigned>(value.b) * 29U) >> 8U));
        break;
    case lib::PixelFormat::RGB24:
        put(0U, value.r); put(1U, value.g); put(2U, value.b);
        break;
    case lib::PixelFormat::BGR24:
        put(0U, value.b); put(1U, value.g); put(2U, value.r);
        break;
    case lib::PixelFormat::RGBA32:
        put(0U, value.r); put(1U, value.g); put(2U, value.b); put(3U, value.a);
        break;
    case lib::PixelFormat::BGRA32:
        put(0U, value.b); put(1U, value.g); put(2U, value.r); put(3U, value.a);
        break;
    default:
        break;
    }
}

ImageDataWithScale makeBitmap(const std::uint32_t width,
                              const std::uint32_t height,
                              const std::uint32_t scale) {
    lib::ImageData image;
    image.format = lib::ImageFormat::Png;
    lib::ImageFrame frame;
    frame.logical_size = {static_cast<float>(width) / safeScale(scale),
                          static_cast<float>(height) / safeScale(scale)};
    frame.pixel_width = width;
    frame.pixel_height = height;
    frame.pixel_format = lib::PixelFormat::RGBA32;
    frame.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    image.frames.push_back(std::move(frame));
    ImageDataWithScale result(std::move(image), safeScale(scale));
    result.logical_size = {static_cast<float>(width) / safeScale(scale),
                           static_cast<float>(height) / safeScale(scale)};
    return result;
}

void blendPixel(Frame& destination, const std::uint32_t x,
                const std::uint32_t y, Pixel source,
                const Byte opacity = 255U) noexcept {
    if (opacity != 255U) {
        source.a = static_cast<Byte>((static_cast<unsigned>(source.a) *
                                      opacity + 127U) / 255U);
    }
    if (source.a == 0U) return;
    const auto old = readPixel(destination, x, y);
    if (source.a == 255U) {
        writePixel(destination, x, y, source);
        return;
    }
    const auto sa = static_cast<unsigned>(source.a);
    const auto da = static_cast<unsigned>(old.a);
    const auto out_a = sa + ((da * (255U - sa) + 127U) / 255U);
    if (out_a == 0U) {
        writePixel(destination, x, y, {});
        return;
    }
    const auto channel = [sa, da, out_a](const Byte s, const Byte d) {
        const auto value = static_cast<unsigned>(s) * sa * 255U +
                           static_cast<unsigned>(d) * da * (255U - sa);
        return static_cast<Byte>(std::min(255U, (value / out_a + 127U) / 255U));
    };
    writePixel(destination, x, y,
               {channel(source.r, old.r), channel(source.g, old.g),
                channel(source.b, old.b), static_cast<Byte>(out_a)});
}

void blit(const ImageDataWithScale& source, Frame& destination,
          const Rectangle destination_rect, const std::uint8_t opacity,
          const bool transparent, const std::int32_t mask_x,
          const std::int32_t mask_y) {
    const auto* frame = firstFrame(source);
    if (frame == nullptr || frame->pixel_width == 0U ||
        frame->pixel_height == 0U || !validRect(destination_rect)) return;
    // 拡大縮小後のサンプルを選ぶ前に、元の画素でアルファを設定する。 復号済
    // みRGBAだけでなく、直接渡されたRGB・グレースケールにも対応する。
    Frame masked;
    if (transparent) {
        masked = *frame;
        lib::apply_image_color_key(masked, mask_x, mask_y);
        frame = &masked;
    }
    const auto source_width = static_cast<std::int32_t>(frame->pixel_width);
    const auto source_height = static_cast<std::int32_t>(frame->pixel_height);
    for (std::int32_t y = 0; y < destination_rect.height; ++y) {
        const auto sy = static_cast<std::uint32_t>(std::clamp(
            static_cast<std::int64_t>(y) * source_height /
                std::max(1, destination_rect.height),
            std::int64_t{0}, static_cast<std::int64_t>(source_height - 1)));
        for (std::int32_t x = 0; x < destination_rect.width; ++x) {
            const auto sx = static_cast<std::uint32_t>(std::clamp(
                static_cast<std::int64_t>(x) * source_width /
                    std::max(1, destination_rect.width),
                std::int64_t{0}, static_cast<std::int64_t>(source_width - 1)));
            auto pixel = readPixel(*frame, sx, sy);

            const auto dx = destination_rect.x + x;
            const auto dy = destination_rect.y + y;
            if (dx >= 0 && dy >= 0) {
                blendPixel(destination, static_cast<std::uint32_t>(dx),
                           static_cast<std::uint32_t>(dy), pixel, opacity);
            }
        }
    }
}

std::int32_t interpolate(const std::int32_t from, const std::int32_t to,
                         const std::uint32_t part) noexcept {
    return clampInt(from + static_cast<std::int32_t>(
        ((to - from) * static_cast<std::int64_t>(part) + 128) >> 8U));
}

void applyFilter(Frame& frame, const Rectangle image_rect,
                 const types::BlendMode blend_mode,
                 const types::GradientDir gradient_dir,
                 const structs::CRGB color1, const structs::CRGB color2) {
    if (frame.pixel_width == 0U || frame.pixel_height == 0U) return;
    const auto first_x = std::max(0, image_rect.x);
    const auto first_y = std::max(0, image_rect.y);
    const auto last_x = std::min(static_cast<std::int32_t>(frame.pixel_width),
                                 image_rect.x + image_rect.width);
    const auto last_y = std::min(static_cast<std::int32_t>(frame.pixel_height),
                                 image_rect.y + image_rect.height);
    for (std::int32_t y = first_y; y < last_y; ++y) {
        for (std::int32_t x = first_x; x < last_x; ++x) {
            const auto part = gradient_dir == types::GradientDir::LeftToRight
                                  ? static_cast<std::uint32_t>(
                                      (static_cast<std::int64_t>(x - image_rect.x)
                                       << 8U) /
                                      std::max(1, image_rect.width))
                                  : gradient_dir == types::GradientDir::TopToBottom
                                        ? static_cast<std::uint32_t>(
                                              (static_cast<std::int64_t>(y - image_rect.y)
                                               << 8U) /
                                              std::max(1, image_rect.height))
                                        : 0U;
            const auto r = interpolate(static_cast<std::int32_t>(color1.r),
                                       static_cast<std::int32_t>(color2.r), part);
            const auto g = interpolate(static_cast<std::int32_t>(color1.g),
                                       static_cast<std::int32_t>(color2.g), part);
            const auto b = interpolate(static_cast<std::int32_t>(color1.b),
                                       static_cast<std::int32_t>(color2.b), part);
            const auto a = interpolate(static_cast<std::int32_t>(color1.a),
                                       static_cast<std::int32_t>(color2.a), part);
            auto old = readPixel(frame, static_cast<std::uint32_t>(x),
                                 static_cast<std::uint32_t>(y));
            const auto blend = [&](const std::int32_t value,
                                   const Byte channel) {
                switch (blend_mode) {
                case types::BlendMode::Add:
                    return toByte(static_cast<std::int32_t>(channel) + value);
                case types::BlendMode::Subtract:
                    return toByte(static_cast<std::int32_t>(channel) - value);
                case types::BlendMode::Multiply:
                    return toByte((static_cast<std::int32_t>(channel) * value +
                                   127) / 255);
                default:
                    return toByte(value);
                }
            };
            Pixel filtered{blend(r, old.r), blend(g, old.g), blend(b, old.b),
                           toByte(a)};
            if (blend_mode == types::BlendMode::Normal ||
                blend_mode == types::BlendMode::Mask) {
                blendPixel(frame, static_cast<std::uint32_t>(x),
                           static_cast<std::uint32_t>(y), filtered);
            } else {
                writePixel(frame, static_cast<std::uint32_t>(x),
                           static_cast<std::uint32_t>(y), filtered);
            }
        }
    }
}

// 共通のインライン TextCell のラスタライズ方針に合わせる計測アダプター。 画素の生成は cellrender/lib::rasterize_inline_text
// に任せ、別の描画処理は設けない。
class RasterMetrics final : public lib::DrawingContext {
public:
    explicit RasterMetrics(std::shared_ptr<lib::GraphicsBackend> graphics)
        : graphics_(std::move(graphics)) {}
    void save() override {} void restore() override {} void clip(lib::Rect) override {}
    void clear(lib::Color) override {}
    void draw_line(lib::Point, lib::Point, lib::Color, float, lib::StrokeStyle) override {}
    void draw_rect(lib::Rect, lib::Color, float, lib::StrokeStyle) override {}
    void fill_rect(lib::Rect, lib::Color) override {}
    void draw_text(std::string_view, lib::Point, const lib::FontSpec&, lib::Color) override {}
    void draw_image(lib::ResourceId, lib::Rect, lib::Rect, lib::Interpolation) override {}
    lib::Size measure_text(std::string_view value, const lib::FontSpec& font, float wrap) override {
        return graphics_->measure_text(value, font, wrap);
    }
    lib::FontMetrics font_metrics(const lib::FontSpec& font) override { return graphics_->font_metrics(font); }
private:
    std::shared_ptr<lib::GraphicsBackend> graphics_;
};

ImageDataWithScale resizeComposed(const ImageDataWithScale& source,
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t scale, const bool smooth) {
    const auto* frame = firstFrame(source);
    if (!frame) return {};
    // 境界部分では画素の配置形式だけを変換する。 補間と独立したアルフ
    // ァチャンネルの処理には、共通のD版描画のサイズ変更処理を使う。
    lib::Bytes pixels(std::size_t(frame->pixel_width) * frame->pixel_height * 4U);
    lib::Bytes alpha(std::size_t(frame->pixel_width) * frame->pixel_height);
    for (std::uint32_t y = 0; y < frame->pixel_height; ++y) {
        for (std::uint32_t x = 0; x < frame->pixel_width; ++x) {
            const auto p = readPixel(*frame, x, y);
            const auto index = std::size_t(y) * frame->pixel_width + x;
            pixels[index*4]=p.b; pixels[index*4+1]=p.g; pixels[index*4+2]=p.r;
            pixels[index*4+3]=255; alpha[index]=p.a;
        }
    }
    std::size_t stride{};
    auto resized = smooth
        ? ::cwxeditor::cwx::graphics::smoothResize(width, height, pixels, alpha, 32,
            frame->pixel_width, frame->pixel_height, std::size_t(frame->pixel_width)*4, stride)
        : ::cwxeditor::cwx::graphics::resize(width, height, pixels, alpha, 32,
            frame->pixel_width, frame->pixel_height, std::size_t(frame->pixel_width)*4, stride);
    auto result = makeBitmap(width, height, scale);
    auto* output = firstFrame(result.data);
    for (std::uint32_t y=0; y<height; ++y) for (std::uint32_t x=0; x<width; ++x) {
        const auto at=std::size_t(y)*stride+std::size_t(x)*4;
        const auto ai=std::size_t(y)*width+x;
        if (at+2<resized.size() && ai<alpha.size())
            writePixel(*output,x,y,{resized[at+2],resized[at+1],resized[at],alpha[ai]});
    }
    return result;
}

bool pathMatches(const StringView pattern, const StringView value) {
    // D版の cfnmatch はワイルドカード照合ではなく、ファイル名の一致判定。 * と
    // ? は通常のファイル名のバイトとして扱い、既存のプラットフォーム別パス方針を維持する。
    std::error_code error;
    const auto left = std::filesystem::absolute(
        structs::pathFromUtf8(pattern), error).lexically_normal();
    const auto right = std::filesystem::absolute(
        structs::pathFromUtf8(value), error).lexically_normal();
    return (!left.empty() && !right.empty() && left == right) ||
           pattern == value;
}

bool hasModifier(const lib::Modifiers value, const lib::Modifiers flag) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(flag)) != 0U;
}

}  // namespace

PileImage::PileImage(String path, const std::uint32_t target_scale,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode)
    : path_(std::move(path)), target_scale_(target_scale),
      rect_{x, y, base_width, base_height}, init_width_(base_width),
      init_height_(base_height), scale_mode_(scale_mode) {}

PileImage::PileImage(String path, const std::uint32_t target_scale,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode)
    : PileImage(std::move(path), target_scale, 0, 0, base_width, base_height,
                scale_mode) {}

PileImage::PileImage(ImageDataWithScale data,
                     const std::uint32_t target_scale, const std::int32_t x,
                     const std::int32_t y, const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode,
                     const bool data_resizable)
    : data_(std::move(data)), target_scale_(target_scale),
      rect_{x, y, base_width, base_height}, init_width_(base_width),
      init_height_(base_height), scale_mode_(scale_mode),
      data_resizable_(data_resizable) {}

PileImage::PileImage(ImageDataWithScale data,
                     const std::uint32_t target_scale,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode)
    : PileImage(std::move(data), target_scale, 0, 0, base_width, base_height,
                scale_mode, true) {}

PileImage::PileImage(const ImageType type, const std::uint32_t target_scale,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode)
    : type_(type), target_scale_(target_scale),
      rect_{x, y, base_width, base_height}, init_width_(base_width),
      init_height_(base_height), scale_mode_(scale_mode) {}

PileImage::PileImage(String text, const std::uint32_t target_scale,
                     String font_name, const std::int32_t size,
                     const structs::CRGB color, const bool bold,
                     const bool italic, const bool underline,
                     const bool strike, const bool vertical,
                     const bool antialias,
                     const types::BorderingType bordering_type,
                     const structs::CRGB bordering_color,
                     const std::uint32_t bordering_width,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height)
    : PileImage(ImageType::Text, target_scale, x, y, base_width, base_height,
                false) {
    title_ = std::move(text);
    title_font_ = {std::move(font_name), static_cast<std::uint32_t>(std::max(0, size)),
                   bold, italic};
    text_color_ = color;
    underline_ = underline;
    strike_ = strike;
    vertical_ = vertical;
    antialias_ = antialias;
    bordering_type_ = bordering_type;
    bordering_color_ = bordering_color;
    bordering_width_ = bordering_width;
    font_pixel_size_ = size;
}

PileImage::PileImage(Drawer drawer, const std::uint32_t target_scale,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode)
    : PileImage(ImageType::Drawer, target_scale, x, y, base_width, base_height,
                scale_mode) {
    drawer_ = std::move(drawer);
}

PileImage::~PileImage() { lifetime_.reset();releaseResource(); }

PileImage::PileImage(PileImage&& other) noexcept
    : type_(other.type_), path_(std::move(other.path_)),
      data_(std::move(other.data_)), image_data_(std::move(other.image_data_)),
      base_size_data_(std::move(other.base_size_data_)),
      appends_(std::move(other.appends_)), target_scale_(other.target_scale_),
      scale_(other.scale_), rect_(other.rect_), init_width_(other.init_width_),
      init_height_(other.init_height_), scale_mode_(other.scale_mode_),
      layer_(other.layer_),
      data_resizable_(other.data_resizable_), transparent_(other.transparent_),
      smoothing_(other.smoothing_), alpha_(other.alpha_),
      separator_(other.separator_), visible_(other.visible_),
      image_dirty_(other.image_dirty_), created_(other.created_),
      image_revision_(other.image_revision_),
      graphics_(other.graphics_), services_(std::move(other.services_)),
      title_(std::move(other.title_)), title_font_(other.title_font_),
      title_point_(other.title_point_), title_shrink_width_(other.title_shrink_width_),
      title_antialias_(other.title_antialias_), title_color_(other.title_color_),
      title_hemming_color_(other.title_hemming_color_), mask_r_(other.mask_r_),
      mask_g_(other.mask_g_), mask_b_(other.mask_b_), mask_a_(other.mask_a_),
      font_pixel_size_(other.font_pixel_size_), text_color_(other.text_color_),
      underline_(other.underline_), strike_(other.strike_),
      vertical_(other.vertical_), antialias_(other.antialias_),
      bordering_type_(other.bordering_type_), bordering_color_(other.bordering_color_),
      bordering_width_(other.bordering_width_),
      preview_text_(std::move(other.preview_text_)),
      created_preview_text_(std::move(other.created_preview_text_)),
      blend_mode_(other.blend_mode_), gradient_dir_(other.gradient_dir_),
      color1_(other.color1_), color2_(other.color2_),
      drawer_(std::move(other.drawer_)) {
    other.image_data_.resource = 0U;
    other.image_data_.owned_resource = false;
    other.created_ = false;
    other.image_dirty_ = true;
    ++other.image_revision_;
}

PileImage& PileImage::operator=(PileImage&& other) noexcept {
    if (this == &other) return *this;
    releaseResource();
    PileImage replacement(std::move(other));
    type_ = replacement.type_;
    path_ = std::move(replacement.path_);
    data_ = std::move(replacement.data_);
    image_data_ = std::move(replacement.image_data_);
    base_size_data_ = std::move(replacement.base_size_data_);
    appends_ = std::move(replacement.appends_);
    target_scale_ = replacement.target_scale_;
    scale_ = replacement.scale_;
    rect_ = replacement.rect_;
    init_width_ = replacement.init_width_;
    init_height_ = replacement.init_height_;
    scale_mode_ = replacement.scale_mode_;
    layer_ = replacement.layer_;
    data_resizable_ = replacement.data_resizable_;
    transparent_ = replacement.transparent_;
    smoothing_ = replacement.smoothing_;
    alpha_ = replacement.alpha_;
    separator_ = replacement.separator_;
    visible_ = replacement.visible_;
    image_dirty_ = replacement.image_dirty_;
    created_ = replacement.created_;
    image_revision_ = replacement.image_revision_;
    creating_image_ = false;
    graphics_ = replacement.graphics_;
    services_ = std::move(replacement.services_);
    title_ = std::move(replacement.title_);
    title_font_ = replacement.title_font_;
    title_point_ = replacement.title_point_;
    title_shrink_width_ = replacement.title_shrink_width_;
    title_antialias_ = replacement.title_antialias_;
    title_color_ = replacement.title_color_;
    title_hemming_color_ = replacement.title_hemming_color_;
    mask_r_ = replacement.mask_r_; mask_g_ = replacement.mask_g_;
    mask_b_ = replacement.mask_b_; mask_a_ = replacement.mask_a_;
    font_pixel_size_ = replacement.font_pixel_size_;
    text_color_ = replacement.text_color_;
    underline_ = replacement.underline_; strike_ = replacement.strike_;
    vertical_ = replacement.vertical_; antialias_ = replacement.antialias_;
    bordering_type_ = replacement.bordering_type_;
    bordering_color_ = replacement.bordering_color_;
    bordering_width_ = replacement.bordering_width_;
    preview_text_ = std::move(replacement.preview_text_);
    created_preview_text_ = std::move(replacement.created_preview_text_);
    blend_mode_ = replacement.blend_mode_;
    gradient_dir_ = replacement.gradient_dir_;
    color1_ = replacement.color1_; color2_ = replacement.color2_;
    drawer_ = std::move(replacement.drawer_);
    replacement.image_data_.resource = 0U;
    replacement.image_data_.owned_resource = false;
    return *this;
}

std::uint32_t PileImage::positiveScale() const noexcept {
    return target_scale_ == 0U ? dpiScale(services_) : safeScale(target_scale_);
}

void PileImage::setServices(lib::Services services) noexcept {
    services_ = std::move(services);
    // 対象倍率がゼロならホストのDPIに従う。サービスの置
    // 換で選ばれる x2/x4 の素材が変わる可能性があ
    // るため、 遅延生成した画像を無効化する必要がある。
    invalidate();
}

void PileImage::setGraphics(lib::GraphicsBackend* graphics) noexcept {
    if (graphics_ == graphics) return;
    auto* const old_graphics = graphics_;
    const auto old_id = image_data_.owned_resource ? image_data_.resource : 0U;
    image_data_.resource = 0U;
    image_data_.owned_resource = false;
    graphics_ = graphics;
    invalidate();
    created_ = false;
    if (old_graphics != nullptr && old_id != 0U)
        old_graphics->destroy_image(old_id);
}

void PileImage::releaseResource() const noexcept {
    const auto id = image_data_.owned_resource ? image_data_.resource : 0U;
    auto* const graphics = graphics_;
    image_data_.resource = 0U;
    image_data_.owned_resource = false;
    if (graphics != nullptr && id != 0U) graphics->destroy_image(id);
}

void PileImage::targetScale(const std::uint32_t value) noexcept {
    if (target_scale_ == value) return;
    target_scale_ = value;
    invalidate();
}

bool PileImage::updatePath(const StringView old_path, const StringView new_path) {
    bool changed = false;
    if (pathMatches(old_path, path_)) {
        path_ = String(new_path);
        changed = true;
    }
    for (auto& value : appends_) {
        if (pathMatches(old_path, value.path)) {
            value.path = String(new_path);
            changed = true;
        }
    }
    if (changed) invalidate();
    return changed;
}

void PileImage::setBaseImage(ImageDataWithScale data,
                             const std::int32_t base_width,
                             const std::int32_t base_height) {
    data_ = std::move(data);
    init_width_ = base_width;
    init_height_ = base_height;
    // D版と同様、表示中の矩形を変えずに基準寸法を更新する。 createImage()
    // が代替画像を用意し、同じリソースを更新できるようにな
    // るまで、 現在公開しているアップロードを表示し続ける。
    invalidate();
}

void PileImage::append(String path, const Insets insets,
                       const ScaleType scale_type, const bool transparent,
                       const std::int32_t mask_x, const std::int32_t mask_y,
                       const std::uint8_t alpha,
                       const std::uint32_t drawing_scale) {
    Append value;
    value.path = std::move(path);
    value.insets = insets;
    value.scale_type = scale_type;
    value.transparent = transparent;
    value.mask_x = mask_x;
    value.mask_y = mask_y;
    value.alpha = alpha;
    value.drawing_scale = drawing_scale; // ゼロなら、生成時の対象倍率に従う。
    appends_.push_back(std::move(value));
    invalidate();
}

void PileImage::append(String path, const Insets insets,
                       const ScaleType scale_type, const bool transparent,
                       const std::int32_t mask_x, const std::int32_t mask_y,
                       const std::uint8_t alpha,
                       std::function<std::uint32_t()> drawing_scale) {
    append(std::move(path), insets, scale_type, transparent, mask_x, mask_y, alpha, 0U);
    appends_.back().drawing_scale_override = std::move(drawing_scale);
}

void PileImage::append(ImageDataWithScale data, const Insets insets,
                       const ScaleType scale_type, const std::uint8_t alpha) {
    Append value;
    value.data = std::move(data);
    value.insets = insets;
    value.scale_type = scale_type;
    value.alpha = alpha;
    appends_.push_back(std::move(value));
    invalidate();
}

void PileImage::append(ImageDataWithScale data, const Point point,
                       const ScaleType scale_type, const std::uint8_t alpha) {
    const auto size = data.size();
    append(std::move(data),
           {point.y, init_width_ - point.x -
                         static_cast<std::int32_t>(size.width),
            init_height_ - point.y - static_cast<std::int32_t>(size.height),
            point.x},
           scale_type, alpha);
}

void PileImage::append(FontData font_data, String text, const Insets insets) {
    Append value;
    value.font_data = std::move(font_data);
    value.has_font_data = true;
    value.text = std::move(text);
    value.insets = insets;
    appends_.push_back(std::move(value));
    invalidate();
}

void PileImage::append(String text, const Insets insets,
                       const structs::CFont font,
                       const structs::CRGB font_color,
                       const bool antialias, const TPos pos) {
    Append value;
    value.text = std::move(text);
    value.insets = insets;
    value.font = font;
    value.font_color = font_color;
    value.antialias = antialias;
    value.text_pos = pos;
    appends_.push_back(std::move(value));
    invalidate();
}

void PileImage::append(String text, const Point point,
                       const structs::CFont font,
                       const structs::CRGB font_color, const bool antialias) {
    append(std::move(text), {point.y, 0, 0, point.x}, font, font_color,
           antialias, TPos::LEFT);
}

void PileImage::setPath(String path) {
    if (path_ == path) return;
    path_ = std::move(path);
    data_.clear();
    invalidate();
}

void PileImage::setPath(const std::size_t index, String path) {
    if (index >= appends_.size()) return;
    if (appends_[index].path == path) return;
    appends_[index].path = std::move(path);
    appends_[index].data.clear();
    invalidate();
}

void PileImage::setTransparent(const std::size_t index, const bool value) {
    if (index >= appends_.size()) return;
    if (appends_[index].transparent == value) return;
    appends_[index].transparent = value;
    invalidate();
}

void PileImage::setImageData(ImageDataWithScale data) {
    path_.clear();
    data_ = std::move(data);
    invalidate();
}

void PileImage::setImageData(const std::size_t index, ImageDataWithScale data) {
    if (index >= appends_.size()) return;
    appends_[index].path.clear();
    appends_[index].data = std::move(data);
    invalidate();
}

void PileImage::setTitle(String title, const structs::CFont font,
                         const Point point, const std::int32_t shrink_width,
                         const bool antialias) {
    const auto normalized_shrink_width = std::max(0, shrink_width);
    if (title_ == title && title_font_ == font && title_point_ == point &&
        title_shrink_width_ == normalized_shrink_width &&
        title_antialias_ == antialias) return;
    title_ = std::move(title);
    title_font_ = font;
    title_point_ = point;
    title_shrink_width_ = normalized_shrink_width;
    title_antialias_ = antialias;
    font_pixel_size_ = static_cast<std::int32_t>(font.point);
    invalidate();
}

void PileImage::setTitle(String title, String font_name,
                         const std::int32_t size, const bool bold,
                         const bool italic, const bool vertical) {
    if (vertical_ != vertical) {
        vertical_ = vertical;
        invalidate();
    }
    setTitle(std::move(title),
             {std::move(font_name), static_cast<std::uint32_t>(std::max(0, size)),
              bold, italic},
             {}, 0, false);
}

void PileImage::colorMask(const std::int32_t r, const std::int32_t g,
                          const std::int32_t b, const std::int32_t a) noexcept {
    const auto next_r = clampInt(r), next_g = clampInt(g);
    const auto next_b = clampInt(b), next_a = clampInt(a);
    if (mask_r_ == next_r && mask_g_ == next_g && mask_b_ == next_b &&
        mask_a_ == next_a) return;
    mask_r_ = next_r; mask_g_ = next_g;
    mask_b_ = next_b; mask_a_ = next_a;
    invalidate();
}

ImageDataWithScale PileImage::loadPath(const StringView path,
                                       const std::uint32_t scale,
                                       const bool transparent,
                                       const std::int32_t mask_x,
                                       const std::int32_t mask_y) const {
    if (path.empty()) return {};
    const auto requested_scale = scale == 0U ? positiveScale() : scale;
    const auto prepare = [&](lib::ImageData data, std::uint32_t source_scale) {
        if (transparent) lib::apply_image_color_key(data, mask_x, mask_y);
        for (auto& frame : data.frames)
            frame.logical_size = {
                static_cast<float>(frame.pixel_width) / source_scale,
                static_cast<float>(frame.pixel_height) / source_scale};
        return ImageDataWithScale(std::move(data), source_scale);
    };
    if (utils::isBinImg(path) && services_.image_codecs) {
        auto encoded = utils::strToBImg(path);
        lib::DecodeImageRequest request;
        request.encoded_data = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(encoded.data()), encoded.size());
        std::string error;
        auto data = decodeEncoded(services_, request.encoded_data, {}, error);
        if (!data.frames.empty()) {
            return prepare(std::move(data), requested_scale);
        }
    }
    const auto candidates = resourceCandidates(path, services_);
    std::string last_error;
    for (const auto& candidate : candidates) {
        std::uint32_t source_scale = NORMAL_SCALE;
        const auto source = scaledPath(candidate, requested_scale, source_scale);
        if (source.empty()) continue;
        auto data = decodeFile(services_, source, last_error);
        if (!data.frames.empty()) {
            return prepare(std::move(data), source_scale);
        }
    }

    // D版の image.d と同様、外部リソースディレクトリの後に、実行
    // ファイルの取り込み画像一覧も調べる。 共通の SystemBackend
    // はネイティブのホストにもこの機能を提供する。 起動後に追加され
    // たリソースを次の描画で見つけられるよう、検索失敗を固定しない。
    if (services_.system && services_.image_codecs) {
        try {
            const auto executable = services_.system->executable_path();
            auto data = decodeEmbedded(services_, executable, path,
                                       structs::pathFromUtf8(path), last_error);
            if (!data.frames.empty())
                return prepare(std::move(data), NORMAL_SCALE);
        } catch (...) {
        }
    }

    // バイナリ画像文字列と旧APIからの呼び出しは dskin が処理する。 通
    // 常ファイルは上のウィンドウごとのサービスを使い、最後の代替経路としてのみ
    // dskin を使う。 dskin はCWの埋め込み画像形式も復号できる。
    try {
        auto fallback = dskin::imgd(path, requested_scale, dskin::MaskType::NoMask);
        if (fallback.valid()) {
            if (transparent) lib::apply_image_color_key(fallback.data, mask_x, mask_y);
            return fallback;
        }
    } catch (...) {
    }
    return {};
}

ImageDataWithScale PileImage::createImageData() const {
    // デコーダーやフォント提供処理からの通知で、単独所有されているこの補助オブ
    // ジェクトが破棄される場合がある。 独立した値のスナップショットを使って
    // 準備し、下の一つの実装で通常の呼び出しと保持型APIの呼び出しを扱う。
    const std::weak_ptr<void> life = lifetime_;
    const auto revision = image_revision_;
    PileImage snapshot(String{},target_scale_,rect_.x,rect_.y,init_width_,init_height_,scale_mode_);
    snapshot.type_=type_;snapshot.path_=path_;snapshot.data_=data_;snapshot.appends_=appends_;
    snapshot.scale_=scale_;snapshot.rect_=rect_;snapshot.data_resizable_=data_resizable_;
    snapshot.transparent_=transparent_;snapshot.mask_r_=mask_r_;snapshot.mask_g_=mask_g_;
    snapshot.mask_b_=mask_b_;snapshot.mask_a_=mask_a_;snapshot.blend_mode_=blend_mode_;
    snapshot.gradient_dir_=gradient_dir_;snapshot.color1_=color1_;snapshot.color2_=color2_;
    snapshot.title_=title_;snapshot.title_font_=title_font_;snapshot.title_point_=title_point_;
    snapshot.title_shrink_width_=title_shrink_width_;snapshot.title_antialias_=title_antialias_;
    snapshot.title_color_=title_color_;snapshot.title_hemming_color_=title_hemming_color_;
    snapshot.services_=services_;snapshot.graphics_=graphics_;snapshot.preview_text_=preview_text_;
    snapshot.alpha_=alpha_; snapshot.smoothing_=smoothing_;
    snapshot.text_color_=text_color_;snapshot.underline_=underline_;snapshot.strike_=strike_;
    snapshot.vertical_=vertical_;snapshot.antialias_=antialias_;
    snapshot.bordering_type_=bordering_type_;snapshot.bordering_color_=bordering_color_;
    snapshot.bordering_width_=bordering_width_;
    auto result = snapshot.createImageDataImpl();
    if (!life.expired() && image_revision_ == revision) {
        base_size_data_ = snapshot.base_size_data_;
        created_preview_text_ = snapshot.created_preview_text_;
    }
    return result;
}

ImageDataWithScale PileImage::createImageDataImpl() const {
    if (init_width_ <= 0 || init_height_ <= 0 ||
        width() <= 0 || height() <= 0) return {};
    if (type_ == ImageType::Drawer) return {}; // D版では Drawer のラスター画像を意図的に持たない。
    if (type_ == ImageType::Text) {
        created_preview_text_ = preview_text_ ? preview_text_(title_) : title_;
        if (bordering_type_ != types::BorderingType::Inline || !graphics_) return {};
        auto graphics = services_.graphics;
        if (graphics.get() != graphics_)
            graphics = std::shared_ptr<lib::GraphicsBackend>(graphics_, [](auto*) {});
        RasterMetrics drawing(graphics);
        const auto scale = positiveScale();
        auto font = asFont(title_font_, underline_, strike_);
        font.points *= scale; font.antialias = antialias_;
        cellrender::Text text{created_preview_text_, font, asColor(text_color_),
            asColor(bordering_color_), bordering_type_, float(bordering_width_) * scale,
            vertical_, graphics, title_antialias_};
        lib::ImageData pixels;
        text.raster_output = [&pixels](lib::ImageData value) { pixels = std::move(value); };
        cellrender::draw_text(drawing, {0, 0, float(scaledDimension(width(), scale)),
            float(scaledDimension(height(), scale))}, {0, 0}, std::move(text));
        if (pixels.frames.empty()) return {};
        const auto& frame = pixels.frames.front();
        const lib::Size logical{float(frame.pixel_width) / scale, float(frame.pixel_height) / scale};
        pixels.frames.front().logical_size = logical;
        ImageDataWithScale result(std::move(pixels), scale);
        result.logical_size = logical;
        return result;
    }

    const auto scale = positiveScale();
    if (type_ == ImageType::Image && !data_resizable_ && data_.valid() &&
        appends_.empty() && title_.empty() && mask_a_ == 0) {
        // D版と同様、サイズ変更が無効なら、渡されたラスター画像を元の論理サイズで保持する。
        auto result = data_;
        const auto* frame = firstFrame(result);
        if (frame != nullptr && safeScale(result.source_scale) != scale) {
            const auto source_scale = safeScale(result.source_scale);
            const auto target_width = static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(
                    static_cast<std::uint64_t>(frame->pixel_width) * scale /
                        source_scale,
                    1U, std::numeric_limits<std::uint32_t>::max()));
            const auto target_height = static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(
                    static_cast<std::uint64_t>(frame->pixel_height) * scale /
                        source_scale,
                    1U, std::numeric_limits<std::uint32_t>::max()));
            result = resizeComposed(result, target_width, target_height, scale, false);
        }
        lib::apply_image_opacity(result.data, static_cast<lib::Byte>(alpha_));
        base_size_data_ = result;
        return result;
    }
    const auto pixel_width = static_cast<std::uint32_t>(
        std::max(1, scaledDimension(type_ == ImageType::Image ? init_width_ : width(), scale)));
    const auto pixel_height = static_cast<std::uint32_t>(
        std::max(1, scaledDimension(type_ == ImageType::Image ? init_height_ : height(), scale)));
    auto result = makeBitmap(pixel_width, pixel_height, scale);
    auto* output = firstFrame(result.data);
    if (output == nullptr) return {};

    ImageDataWithScale material = data_;
    if (!material.valid() && !path_.empty()) {
        material = loadPath(path_, scale, transparent_, 0, 0);
    }
    if (material.valid()) {
        blit(material, *output,
             {0, 0, pixel_width > 0U
                        ? static_cast<std::int32_t>(pixel_width) : 0,
              pixel_height > 0U
                  ? static_cast<std::int32_t>(pixel_height) : 0},
             255U, transparent_, 0, 0);
    }

    for (const auto& value : appends_) {
        ImageDataWithScale append_data = value.data;
        if (!append_data.valid() && !value.path.empty()) {
            append_data = loadPath(value.path,
                                   value.drawing_scale_override ? value.drawing_scale_override()
                                       : (value.drawing_scale == 0U ? scale : value.drawing_scale),
                                   value.transparent, value.mask_x,
                                   value.mask_y);
        }
        if (!append_data.valid()) continue;
        const auto* source = firstFrame(append_data);
        if (source == nullptr) continue;
        const auto placement=lib::plan_image_layer_destination({pixel_width,pixel_height,
            source->pixel_width,source->pixel_height,scale,safeScale(append_data.source_scale),
            {value.insets.n,value.insets.e,value.insets.s,value.insets.w},value.scale_type});
        const Rectangle destination{placement.x,placement.y,placement.width,placement.height};
        blit(append_data, *output, destination, value.alpha,
             value.transparent, value.mask_x, value.mask_y);
    }

    if (mask_a_ != 0) {
        const Pixel mask{toByte(mask_r_), toByte(mask_g_), toByte(mask_b_),
                         toByte(mask_a_)};
        for (std::uint32_t y = 0; y < output->pixel_height; ++y)
            for (std::uint32_t x = 0; x < output->pixel_width; ++x)
                blendPixel(*output, x, y, mask);
    }
    if (type_ == ImageType::ColorFilter) {
        applyFilter(*output, {0, 0, static_cast<std::int32_t>(pixel_width),
                              static_cast<std::int32_t>(pixel_height)},
                    blend_mode_, gradient_dir_, color1_, color2_);
    }
    if(!title_.empty() && graphics_) {
        auto font=asFont(title_font_);font.points*=scale;
        auto graphics=services_.graphics;
        // 従来の生ポインターを使う setGraphics API は、呼び出し元がバッ
        // クエンドを保持する必要がある。 設定済みのサービスは、共有所有権を提供する。
        if(graphics.get()!=graphics_)graphics=std::shared_ptr<lib::GraphicsBackend>(graphics_,[](auto*){});
        const auto foreground=title_color_.value_or(lib::Color{0,0,0,255});
        const lib::Point position{float(title_point_.x)*scale-1,float(title_point_.y)*scale-1};
        const auto style=lib::ImageTextStyle{font,foreground,asColor(title_hemming_color_),float(title_shrink_width_)*scale,title_antialias_};
        const auto title=preview_text_ ? preview_text_(title_) : title_;
        const auto caption=lib::rasterize_image_text(graphics,title,
            style);
        if(caption.pixel_width && caption.pixel_height)
            (void)lib::composite_image_pixels(*output,caption,
                position,
                {0,0,float(pixel_width),float(pixel_height)});
    }
    if(type_==ImageType::Image)lib::apply_image_opacity(result.data,static_cast<lib::Byte>(alpha_));
    // D版と同様、必要に応じて表示サイズを変える前に、合成した基準サイズの画素を保持する。
    base_size_data_ = result;
    if (type_ == ImageType::Image && data_resizable_) {
        const auto final_width = static_cast<std::uint32_t>(std::max(1, scaledDimension(width(), scale)));
        const auto final_height = static_cast<std::uint32_t>(std::max(1, scaledDimension(height(), scale)));
        if (final_width != pixel_width || final_height != pixel_height)
            result = resizeComposed(result, final_width, final_height, scale, smoothing_);
    }
    return result;
}

void PileImage::createImage() {
    if (!image_dirty_ || creating_image_) return;
    const std::weak_ptr<void> life=lifetime_;
    creating_image_ = true;
    struct CreatingGuard {
        bool* value;std::weak_ptr<void> life;
        ~CreatingGuard() { if(!life.expired())*value = false; }
    } guard{&creating_image_,life};
    const auto revision = image_revision_;
    const auto graphics_owner=services_.graphics;
    auto* const graphics = graphics_;
    const auto old_id = image_data_.owned_resource ? image_data_.resource : 0U;

    auto prepared = createImageData();
    if(life.expired() || revision!=image_revision_ || graphics!=graphics_)return;
    auto prepared_base = base_size_data_;
    if (!prepared.valid() && data_.valid()) prepared = data_;
    bool created_resource = false;
    if (prepared.resource == 0U && graphics != nullptr &&
        !prepared.data.frames.empty()) {
        if (old_id != 0U) {
            graphics->update_image(old_id, prepared.data);
            prepared.resource = old_id;
            prepared.owned_resource = true;
        } else {
            prepared.resource = graphics->create_image(prepared.data);
            prepared.owned_resource = prepared.resource != 0U;
            created_resource = prepared.owned_resource;
        }
    }
    if (life.expired() || revision != image_revision_ || graphics != graphics_) {
        if (created_resource && graphics != nullptr)
            graphics->destroy_image(prepared.resource);
        return;
    }
    if (old_id != 0U && old_id == prepared.resource)
        prepared.owned_resource = true;
    const auto release_id = old_id != 0U && old_id != prepared.resource
                                ? old_id : 0U;
    image_data_ = std::move(prepared);
    base_size_data_ = std::move(prepared_base);
    created_ = image_data_.valid();
    image_dirty_ = false;
    if (release_id != 0U && graphics != nullptr) graphics->destroy_image(release_id);
}

void PileImage::clearImage() noexcept {
    const auto id = image_data_.owned_resource ? image_data_.resource : 0U;
    auto* const graphics = graphics_;
    ++image_revision_;
    image_data_.clear();
    base_size_data_.clear();
    created_ = false;
    image_dirty_ = true;
    if (graphics != nullptr && id != 0U) graphics->destroy_image(id);
}

const ImageDataWithScale& PileImage::imageData() const {
    if (image_dirty_) const_cast<PileImage*>(this)->createImage();
    return image_data_;
}

ImageDataWithScale& PileImage::imageData() {
    if (image_dirty_) createImage();
    return image_data_;
}

std::int32_t PileImage::width() const noexcept {
    return scale_mode_ ? static_cast<std::int32_t>(
        (static_cast<std::int64_t>(init_width_) * scale_) / 100) : rect_.width;
}

std::int32_t PileImage::height() const noexcept {
    return scale_mode_ ? static_cast<std::int32_t>(
        (static_cast<std::int64_t>(init_height_) * scale_) / 100) : rect_.height;
}

void PileImage::width(const std::int32_t value) noexcept {
    if (scale_mode_ || rect_.width == value) return;
    rect_.width = value;
    invalidate();
}

void PileImage::height(const std::int32_t value) noexcept {
    if (scale_mode_ || rect_.height == value) return;
    rect_.height = value;
    invalidate();
}

Rectangle PileImage::bounds() const noexcept {
    return {x(), y(), width(), height()};
}

void PileImage::bounds(const Rectangle value) noexcept {
    if (scale_mode_) {
        const auto base = init_width_ >= init_height_ ? init_width_ : init_height_;
        const auto current = value.width >= value.height ? value.width : value.height;
        boundsWithScale(value.x, value.y,
                        base > 0 ? static_cast<std::uint32_t>(
                            std::max(1, current * 100 / base)) : 100U);
        return;
    }
    const bool raster_size_changed = rect_.width != value.width ||
                                     rect_.height != value.height;
    rect_ = value;
    if (raster_size_changed) invalidate();
}

void PileImage::boundsWithScale(const std::int32_t x_value,
                                const std::int32_t y_value,
                                const std::uint32_t value) noexcept {
    if (!scale_mode_) return;
    const auto normalized = std::max(1U, value);
    const bool raster_scale_changed = scale_ != normalized;
    rect_.x = x_value;
    rect_.y = y_value;
    scale_ = normalized;
    if (raster_scale_changed) invalidate();
}

bool PileImage::isOpaqueWithoutAlpha() const {
    if (!visible_ || width() <= 0 || height() <= 0) return false;
    if (type_ == ImageType::Text || type_ == ImageType::Drawer) return false;
    if (type_ == ImageType::ColorFilter) {
        return blend_mode_ == types::BlendMode::Normal &&
               color1_.a >= 255U &&
               (gradient_dir_ == types::GradientDir::None || color2_.a >= 255U);
    }
    if (transparent_ || mask_a_ < 255) return false;
    const auto& image = imageData();
    const auto* frame = firstFrame(image);
    if (frame == nullptr) return image.resource != 0U;
    for (std::uint32_t y = 0; y < frame->pixel_height; ++y)
        for (std::uint32_t x = 0; x < frame->pixel_width; ++x)
            if (readPixel(*frame, x, y).a != 255U) return false;
    return true;
}

bool PileImage::isOpaque() const {
    return alpha_ >= 255 && isOpaqueWithoutAlpha();
}

void PileImage::draw(lib::DrawingContext& context,
                     const Rectangle range) const {
    if (!visible_ || width() <= 0 || height() <= 0) return;
    const auto image_rect = bounds();
    if (validRect(range) && !intersects(image_rect, range)) return;
    auto* self = const_cast<PileImage*>(this);
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=image_revision_;
    const auto current=[this,life,revision]{return !life.expired() && image_revision_==revision;};
    self->createImage();
    if(!current())return;
    // D版ではGCが描画先を保持する。共通APIは単独所有にも対応するため
    // 、 外部の描画・フォントコールバックでは値として保存した状態を使う。
    const auto image=image_data_;
    const auto preview=preview_text_;
    const auto border_width=bordering_width_;
    const auto image_type=type_;
    const auto title=title_;
    const auto title_point=title_point_;
    const auto title_font=title_font_;
    const auto text_color=text_color_;
    const auto underline=underline_,strike=strike_;
    const auto border=bordering_type_;
    const auto border_color=bordering_color_;
    const auto appends=appends_;
    text_caches_.resize(appends.size()+1);
    for(auto& cache:text_caches_) if(!cache) cache=std::make_shared<cellrender::TextRasterCache>();
    const auto text_caches=text_caches_;
    std::size_t text_cache_index{};
    const auto drawer=drawer_;
    const auto smoothing=smoothing_;
    const auto vertical=vertical_,antialias=antialias_,title_antialias=title_antialias_;
    auto text_graphics=services_.graphics;
    // 従来の生ポインターを使う setGraphics API は、呼び出し元が提供側を保持する
    // 条件を維持する。 通常の設定済み Services は、ラスター画像に実際の所有権を渡す。
    if(graphics_ && text_graphics.get()!=graphics_)text_graphics=std::shared_ptr<lib::GraphicsBackend>(graphics_,[](auto*){});
    const auto image_width=width();
    auto image_destination=asLibRect(image_rect);
    if (image_type == ImageType::Image && !data_resizable_) {
        const auto native_size = image.size();
        if (native_size.width > 0.0F && native_size.height > 0.0F) {
            image_destination.width = native_size.width;
            image_destination.height = native_size.height;
        }
    }
    context.save();
    struct Restore {lib::DrawingContext& context;~Restore(){context.restore();}} restore{context};
    if(!current())return;
    context.clip(image_destination);
    if(!current())return;

    if (image.resource != 0U) {
        const auto* frame = firstFrame(image);
        const auto source = frame == nullptr
                                ? lib::Rect{}
                                : lib::Rect{0.0F, 0.0F,
                                            static_cast<float>(frame->pixel_width),
                                            static_cast<float>(frame->pixel_height)};
        context.draw_image(image.resource, source, image_destination,
                           smoothing ? lib::Interpolation::Best
                                      : lib::Interpolation::Nearest);
    }

    if(!current())return;
    const auto drawText = [&](StringView text,const Point point,const structs::CFont& font,
        structs::CRGB color,const std::uint8_t alpha,const bool underline,const bool strike,
        types::BorderingType border,structs::CRGB border_color){
        if(text.empty() || !current())return;
        auto actual=String(text);if(preview)actual=preview(actual);
        if(!current())return;
        created_preview_text_=actual;
        auto spec=asFont(font,underline,strike);spec.antialias=antialias;
        const auto with_alpha=[alpha](lib::Color value){value.alpha=static_cast<Byte>((value.alpha*alpha+127U)/255U);return value;};
        // 共通の色のアルファ値は、すべて一様に反映する。 D版のGCによる文字
        // 描画が独立した色のアルファ値を省略していた場合も、意図的に反映する。
        cellrender::draw_text(context,asLibRect(image_rect),
            {static_cast<float>(image_rect.x+point.x),static_cast<float>(image_rect.y+point.y)},
            {std::move(actual),spec,with_alpha(asColor(color)),with_alpha(asColor(border_color)),border,
                static_cast<float>(border_width),vertical,text_graphics,title_antialias},current,text_caches.at(text_cache_index++));
    };

    if (image_type == ImageType::Text && !title.empty() && image.resource == 0U) {
        drawText(title, title_point, title_font, text_color, 255U,
                 underline, strike, border, border_color);
    }
    if(!current())return;
    for (const auto& value : appends) {
        if (value.text.empty()) continue;
        const auto text_width = context.measure_text(
            value.text, asFont(value.font), 0.0F).width;
        if(!current())return;
        auto point = Point{value.insets.w, value.insets.n};
        if (value.text_pos == TPos::RIGHT)
            point.x = image_width - value.insets.e -
                      static_cast<std::int32_t>(std::lround(text_width));
        drawText(value.text, point, value.font, value.font_color, value.alpha,
                 false, false, types::BorderingType::None, {});
        if(!current())return;
    }
    if (image_type == ImageType::Drawer && drawer && current())drawer(*this,context);
}

void FlexImage::reset() noexcept {
    pending_ = bounds();
    // C++版では直接サイズ指定でも公開される保留中の倍率を同期する。
    // D版は倍率モードが有効でない限り、その未使用の値を変更しない。
    new_scale = scale();
    pending_initialized_ = true;
    retoggle();
}

std::int32_t FlexImage::newX() const noexcept {
    return pending_initialized_ ? pending_.x : x();
}

void FlexImage::newX(const std::int32_t value) noexcept {
    if (!pending_initialized_) const_cast<FlexImage*>(this)->reset();
    pending_.x = value;
    retoggle();
}

std::int32_t FlexImage::newY() const noexcept {
    return pending_initialized_ ? pending_.y : y();
}

void FlexImage::newY(const std::int32_t value) noexcept {
    if (!pending_initialized_) const_cast<FlexImage*>(this)->reset();
    pending_.y = value;
    retoggle();
}

std::int32_t FlexImage::newWidth() const noexcept {
    if (scaleMode())
        return static_cast<std::int32_t>(
            (static_cast<std::int64_t>(baseWidth()) * new_scale) / 100);
    return pending_initialized_ ? pending_.width : width();
}

void FlexImage::newWidth(const std::int32_t value) noexcept {
    if (scaleMode()) return;
    if (!pending_initialized_) reset();
    pending_.width = roundMWidth(value);
    retoggle();
}

std::int32_t FlexImage::newHeight() const noexcept {
    if (scaleMode())
        return static_cast<std::int32_t>(
            (static_cast<std::int64_t>(baseHeight()) * new_scale) / 100);
    return pending_initialized_ ? pending_.height : height();
}

void FlexImage::newHeight(const std::int32_t value) noexcept {
    if (scaleMode()) return;
    if (!pending_initialized_) reset();
    pending_.height = roundMHeight(value);
    retoggle();
}

Rectangle FlexImage::newBounds() const noexcept {
    return {newX(), newY(), newWidth(), newHeight()};
}

void FlexImage::newBounds(const Rectangle value) noexcept {
    if (scaleMode()) {
        // D版と同様、基準画像の縦横の向きから倍率の基準軸を選ぶ。
        const bool use_width = baseWidth() >= baseHeight();
        const auto base = use_width ? baseWidth() : baseHeight();
        const auto current = use_width ? value.width : value.height;
        // 大きな矩形でもオーバーフローしないよう、乗算は広い整数型で行う。
        const auto percent = base > 0
            ? std::max<std::int64_t>(1,
                  static_cast<std::int64_t>(current) * 100 / base)
            : 100;
        newBoundsWithScale(value.x, value.y,
                           static_cast<std::uint32_t>(percent));
        return;
    }
    if (!pending_initialized_) reset();
    pending_.x = value.x;
    pending_.y = value.y;
    pending_.width = roundMWidth(value.width);
    pending_.height = roundMHeight(value.height);
    retoggle();
}

void FlexImage::newBoundsWithScale(const std::int32_t x_value,
                                   const std::int32_t y_value,
                                   const std::uint32_t value) noexcept {
    if (!pending_initialized_) reset();
    pending_.x = x_value;
    pending_.y = y_value;
    new_scale = roundScale(value);
    retoggle();
}

void FlexImage::newScale(const std::uint32_t value) noexcept {
    new_scale = roundScale(value);
    if (!pending_initialized_) reset();
    retoggle();
}

std::uint32_t FlexImage::roundScale(const std::uint32_t value) const noexcept {
    return std::clamp(value, std::max(1U, minimum_scale),
                      std::max(std::max(1U, minimum_scale), maximum_scale));
}

std::int32_t FlexImage::roundMWidth(const std::int32_t value) const noexcept {
    return std::clamp(value, minimum_width, std::max(minimum_width, maximum_width));
}

std::int32_t FlexImage::roundMHeight(const std::int32_t value) const noexcept {
    return std::clamp(value, minimum_height, std::max(minimum_height, maximum_height));
}

Toggle FlexImage::inToggle(const std::int32_t point_x,
                           const std::int32_t point_y,
                           const bool move) const noexcept {
    if (selected && !fixed) {
        constexpr Toggle values[] = {
            Toggle::LEFT_TOP, Toggle::LEFT_MIDDLE, Toggle::LEFT_BOTTOM,
            Toggle::MIDDLE_TOP, Toggle::MIDDLE_BOTTOM, Toggle::RIGHT_TOP,
            Toggle::RIGHT_MIDDLE, Toggle::RIGHT_BOTTOM,
        };
        for (const auto value : values) {
            if (contains(toggleRect(value), point_x, point_y)) return value;
        }
    }
    if (move && contains(bounds(), point_x, point_y)) return Toggle::MOVE;
    return Toggle::NONE;
}

Rectangle FlexImage::toggleRect(const Toggle value) const noexcept {
    const auto rect = newBounds();
    const auto size = static_cast<std::int32_t>(toggle_size);
    const auto half = size / 2;
    const auto center_x = rect.x + rect.width / 2 - half;
    const auto center_y = rect.y + rect.height / 2 - half;
    const auto right = rect.x + rect.width - half - 1;
    const auto bottom = rect.y + rect.height - half - 1;
    switch (value) {
    case Toggle::LEFT_TOP: return {rect.x - size + half, rect.y - size + half, size, size};
    case Toggle::LEFT_MIDDLE: return {rect.x - size + half, center_y, size, size};
    case Toggle::LEFT_BOTTOM: return {rect.x - size + half, bottom, size, size};
    case Toggle::MIDDLE_TOP: return {center_x, rect.y - size + half, size, size};
    case Toggle::MIDDLE_BOTTOM: return {center_x, bottom, size, size};
    case Toggle::RIGHT_TOP: return {right, rect.y - size + half, size, size};
    case Toggle::RIGHT_MIDDLE: return {right, center_y, size, size};
    case Toggle::RIGHT_BOTTOM: return {right, bottom, size, size};
    case Toggle::MOVE:
    case Toggle::NONE:
        return {};
    }
    return {};
}

Rectangle FlexImage::drawNewArea() const noexcept {
    const auto rect = newBounds();
    if (fixed && !selected) return {rect.x - 1, rect.y - 1, rect.width + 2, rect.height + 2};
    const auto size = static_cast<std::int32_t>(toggle_size);
    return {rect.x - size, rect.y - size, rect.width + size * 2, rect.height + size * 2};
}

Rectangle FlexImage::drawArea() const noexcept {
    const auto rect = bounds();
    if (fixed && !selected) return rect;
    const auto size = static_cast<std::int32_t>(toggle_size);
    return {rect.x - size, rect.y - size, rect.width + size * 2, rect.height + size * 2};
}

void FlexImage::retoggle() noexcept {
    if (!pending_initialized_) return;
    if (!scaleMode()) {
        pending_.width = roundMWidth(pending_.width);
        pending_.height = roundMHeight(pending_.height);
    } else {
        new_scale = roundScale(new_scale);
    }
}

void FlexImage::resize(const bool call_listeners) {
    if (!pending_initialized_) reset();
    const auto old_bounds = bounds();
    const auto old_scale = scale();
    if (scaleMode()) {
        x(pending_.x);
        y(pending_.y);
        scale(new_scale);
    } else {
        PileImage::bounds(pending_);
    }
    const auto changed = old_bounds.x != x() || old_bounds.y != y() ||
                         old_bounds.width != width() ||
                         old_bounds.height != height() ||
                         old_scale != scale();
    pending_ = bounds();
    pending_initialized_ = true;
    if (call_listeners && changed) {
        if (scaleMode()) {
            for (auto& listener : scale_resize_listeners_)
                if (listener) listener(*this, x(), y(), scale());
        } else {
            for (auto& listener : resize_listeners_)
                if (listener) listener(*this, x(), y(), width(), height());
        }
    }
    if (changed || !isCreated()) createImage();
}

void FlexImage::drawToggle(lib::DrawingContext& context,
                           const std::int32_t image_scale,
                           const bool draw_xor_selection_line) const {
    if (!visible() || !selected) return;
    const auto factor = static_cast<float>(std::max(1, image_scale));
    const auto rect = newBounds();
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto current=[life]{return !life.expired();};
    constexpr Toggle values[] = {
        Toggle::LEFT_TOP, Toggle::LEFT_MIDDLE, Toggle::LEFT_BOTTOM,
        Toggle::MIDDLE_TOP, Toggle::MIDDLE_BOTTOM, Toggle::RIGHT_TOP,
        Toggle::RIGHT_MIDDLE, Toggle::RIGHT_BOTTOM,
    };
    // D版のGCが所有する描画先とは異なり、公開C++画像は描画コールバックによって無効化
    // される場合がある。 その呼び出しに入る前に、ハンドルのスナップショットを取得する。
    std::vector<lib::Rect> handles;
    for(const auto value:values)handles.push_back(asLibRect(toggleRect(value),factor));
    if(!cellrender::draw_selection_rect(context,asLibRect(rect,factor),draw_xor_selection_line,1,current))return;
    for (const auto handle : handles) {
        context.fill_rect({handle.x+1,handle.y+1,std::max(0.F,handle.width-1),
            std::max(0.F,handle.height-1)}, {255U, 255U, 255U, 255U});
        if(!current())return;
        context.draw_rect(handle, {0U, 0U, 0U, 255U}, 1.0F,
                          lib::StrokeStyle::Solid);
        if(!current())return;
    }
}

ImagePane::ImagePane(const std::int32_t width, const std::int32_t height,
                     const std::int32_t image_scale,
                     const std::int32_t drawing_scale)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      image_scale_(std::max(1, image_scale)),
      drawing_scale_(std::max(1, drawing_scale)) {
    set_bounds({0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_)});
    on_pointer = [this](lib::PointerEvent& event) {
        event.handled = handlePointer(event);
    };
    on_key_down = [this](lib::KeyEvent& event) {
        event.handled = handleKey(event);
    };
}

ImagePane::ImagePane(lib::Container* parent, const std::int32_t style,
                     const std::int32_t width, const std::int32_t height,
                     const std::int32_t image_scale,
                     const std::int32_t drawing_scale)
    : ImagePane(width, height, image_scale, drawing_scale) {
    static_cast<void>(style);
    static_cast<void>(parent);
}

void ImagePane::rebuildImageView() const {
    // D版は現在の backs の順序を直接公開する。C++版は slots_ で所有
    // 権を保持し、 所有権を持たないこのビューを通じて、同じシーンの順序を公開する。
    image_view_.clear();
    image_view_.reserve(slots_.size());
    for (const auto& slot : slots_)
        if (slot.image != nullptr) image_view_.push_back(slot.image);
}

const std::vector<PileImage*>& ImagePane::images() const noexcept {
    rebuildImageView();
    return image_view_;
}

std::vector<PileImage*>& ImagePane::images() noexcept {
    rebuildImageView();
    return image_view_;
}

std::vector<PileImage*> ImagePane::orderedImages() const {
    rebuildImageView();
    auto result = image_view_;
    std::stable_sort(result.begin(), result.end(),
                     [](const PileImage* left, const PileImage* right) {
                         return left->layer() < right->layer();
                     });
    return result;
}

void ImagePane::markImageChanged() {
    ++image_revision_;
    for (auto& action : changing_images_)
        if (action) action();
    redrawAll();
}

void ImagePane::append(PileImage& image) {
    slots_.push_back({&image, {}});
    markImageChanged();
}

void ImagePane::append(ImagePtr image) {
    if (!image) return;
    slots_.push_back({image.get(), std::move(image)});
    markImageChanged();
}

void ImagePane::append(const std::vector<PileImage*>& values) {
    for (auto* value : values)
        if (value != nullptr) slots_.push_back({value, {}});
    markImageChanged();
}

void ImagePane::insert(const std::size_t index, PileImage& image) {
    const auto at = std::min(index, slots_.size());
    slots_.insert(slots_.begin() + static_cast<std::ptrdiff_t>(at), {&image, {}});
    markImageChanged();
}

void ImagePane::insert(const std::size_t index, ImagePtr image) {
    if (!image) return;
    const auto at = std::min(index, slots_.size());
    slots_.insert(slots_.begin() + static_cast<std::ptrdiff_t>(at),
                  {image.get(), std::move(image)});
    markImageChanged();
}

void ImagePane::insert(const std::size_t index,
                       const std::vector<PileImage*>& values) {
    auto at = std::min(index, slots_.size());
    for (auto* value : values) {
        if (value == nullptr) continue;
        slots_.insert(slots_.begin() + static_cast<std::ptrdiff_t>(at++),
                      {value, {}});
    }
    markImageChanged();
}

void ImagePane::set(const std::size_t index, PileImage& image) {
    if (index >= slots_.size()) return;
    slots_[index] = {&image, {}};
    markImageChanged();
}

void ImagePane::set(const std::size_t index, ImagePtr image) {
    if (index >= slots_.size() || !image) return;
    slots_[index] = {image.get(), std::move(image)};
    markImageChanged();
}

void ImagePane::remove(const std::size_t index) {
    if (index >= slots_.size()) return;
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
    markImageChanged();
}

void ImagePane::removeRange(std::size_t from, std::size_t to) {
    from = std::min(from, slots_.size());
    to = std::min(to, slots_.size());
    if (from >= to) return;
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(from),
                 slots_.begin() + static_cast<std::ptrdiff_t>(to));
    markImageChanged();
}

void ImagePane::swap(const std::size_t first, const std::size_t second) {
    if (first >= slots_.size() || second >= slots_.size()) return;
    std::swap(slots_[first], slots_[second]);
    markImageChanged();
}

void ImagePane::doSelect(FlexImage& image) {
    if (image.selected) return;
    image.selected = true;
    for (auto& listener : image.selection_listeners_)
        if (listener) listener(image);
}

void ImagePane::doDeselect(FlexImage& image) {
    if (!image.selected) return;
    image.selected = false;
    for (auto& listener : image.selection_listeners_)
        if (listener) listener(image);
}

void ImagePane::select(FlexImage* image) {
    if (image != nullptr) image->selected = true;
    redrawAll();
}

void ImagePane::select(const std::vector<FlexImage*>& values) {
    for (auto* value : values)
        if (value != nullptr) value->selected = true;
    redrawAll();
}

void ImagePane::select(const std::vector<std::int32_t>& indices) {
    deselectAll();
    for (const auto index : indices)
        if (index >= 0 && static_cast<std::size_t>(index) < slots_.size())
            if (auto* image = dynamic_cast<FlexImage*>(slots_[index].image))
                image->selected = true;
    redrawAll();
}

void ImagePane::deselect(FlexImage* image) {
    if (image != nullptr) image->selected = false;
    redrawAll();
}

void ImagePane::deselectAll() {
    for (const auto& slot : slots_)
        if (auto* image = dynamic_cast<FlexImage*>(slot.image))
            image->selected = false;
    redrawAll();
}

void ImagePane::deselectRange(std::size_t from, std::size_t to) {
    from = std::min(from, slots_.size());
    to = std::min(to, slots_.size());
    for (std::size_t index = from; index < to; ++index)
        if (auto* image = dynamic_cast<FlexImage*>(slots_[index].image))
            image->selected = false;
    redrawAll();
}

std::vector<std::int32_t> ImagePane::selectedIndices() const {
    std::vector<std::int32_t> result;
    for (std::size_t index = 0; index < slots_.size(); ++index)
        if (auto* image = dynamic_cast<FlexImage*>(slots_[index].image);
            image != nullptr && image->selected)
            result.push_back(static_cast<std::int32_t>(index));
    return result;
}

std::int32_t ImagePane::selectedIndex() const {
    for (std::size_t index = slots_.size(); index-- > 0U;)
        if (auto* image = dynamic_cast<FlexImage*>(slots_[index].image);
            image != nullptr && image->selected)
            return static_cast<std::int32_t>(index);
    return -1;
}

std::int32_t ImagePane::findIndex(const std::int32_t point_x,
                                  const std::int32_t point_y) const {
    const auto ordered = orderedImages();
    for (auto iterator = ordered.rbegin(); iterator != ordered.rend(); ++iterator) {
        auto* image = dynamic_cast<FlexImage*>(*iterator);
        if (image != nullptr && image->visible() &&
            contains(image->bounds(), point_x, point_y)) {
            for (std::size_t index = 0; index < slots_.size(); ++index)
                if (slots_[index].image == *iterator)
                    return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

std::vector<std::int32_t> ImagePane::findIndices(const std::int32_t point_x,
                                                const std::int32_t point_y) const {
    std::vector<std::int32_t> result;
    for (auto* value : orderedImages()) {
        auto* image = dynamic_cast<FlexImage*>(value);
        if (image == nullptr || !image->visible() ||
            !contains(image->bounds(), point_x, point_y)) continue;
        for (std::size_t index = 0; index < slots_.size(); ++index)
            if (slots_[index].image == value)
                result.push_back(static_cast<std::int32_t>(index));
    }
    return result;
}

std::int32_t ImagePane::findSelectedIndex(const std::int32_t point_x,
                                          const std::int32_t point_y) const {
    const auto ordered = orderedImages();
    for (auto iterator = ordered.rbegin(); iterator != ordered.rend(); ++iterator) {
        auto* image = dynamic_cast<FlexImage*>(*iterator);
        if (image == nullptr || !image->selected || !image->visible() ||
            !contains(image->bounds(), point_x, point_y)) continue;
        for (std::size_t index = 0; index < slots_.size(); ++index)
            if (slots_[index].image == *iterator)
                return static_cast<std::int32_t>(index);
    }
    return -1;
}

std::vector<std::int32_t> ImagePane::findSelectedIndices(
    const std::int32_t point_x, const std::int32_t point_y) const {
    std::vector<std::int32_t> result;
    for (auto* value : orderedImages()) {
        auto* image = dynamic_cast<FlexImage*>(value);
        if (image == nullptr || !image->selected || !image->visible() ||
            !contains(image->bounds(), point_x, point_y)) continue;
        for (std::size_t index = 0; index < slots_.size(); ++index)
            if (slots_[index].image == value)
                result.push_back(static_cast<std::int32_t>(index));
    }
    return result;
}

void ImagePane::fixedRange(const bool fixed_value, std::size_t from,
                           std::size_t to) {
    from = std::min(from, slots_.size());
    to = std::min(to, slots_.size());
    for (std::size_t index = from; index < to; ++index)
        if (auto* image = dynamic_cast<FlexImage*>(slots_[index].image))
            image->fixed = fixed_value;
    redrawAll();
}

bool ImagePane::isMoving() const noexcept {
    for (const auto& slot : slots_) {
        const auto* image = dynamic_cast<const FlexImage*>(slot.image);
        if (image != nullptr && (image->x() != image->newX() ||
                                 image->y() != image->newY() ||
                                 image->width() != image->newWidth() ||
                                 image->height() != image->newHeight()))
            return true;
    }
    return false;
}

void ImagePane::redrawImage(PileImage& image) {
    const auto area = dynamic_cast<FlexImage*>(&image) != nullptr
                          ? static_cast<FlexImage&>(image).drawArea()
                          : image.bounds();
    addRedraw(area.x, area.y, area.width, area.height);
}

void ImagePane::resetGrid() const {
    grid_x_highlight_.clear();
    grid_y_highlight_.clear();
}

void ImagePane::redrawAll() {
    cancel_full_redraw_ = false;
    dirty_.push_back({0, 0, width_, height_});
    if (on_paint) on_paint({0.0F, 0.0F, static_cast<float>(width_),
                            static_cast<float>(height_)});
}

void ImagePane::addRedraw(const std::int32_t x_value,
                          const std::int32_t y_value,
                          const std::int32_t width,
                          const std::int32_t height) {
    if (width <= 0 || height <= 0) return;
    cancel_full_redraw_ = false;
    dirty_.push_back({x_value, y_value, width, height});
    if (on_paint) on_paint(asLibRect({x_value, y_value, width, height}));
}

void ImagePane::updateDrag(FlexImage& image, const Toggle toggle,
                           const std::int32_t dx, const std::int32_t dy,
                           const bool ratio_fix) {
    if (image.fixed || !image.visible()) return;
    const auto saved = drag_bounds_.find(&image);
    const auto original = saved == drag_bounds_.end() ? image.bounds() : saved->second;
    auto changed = original;
    if (toggle == Toggle::MOVE) {
        changed.x += dx;
        changed.y += dy;
    } else {
        if (toggle == Toggle::LEFT_TOP || toggle == Toggle::LEFT_MIDDLE ||
            toggle == Toggle::LEFT_BOTTOM) {
            changed.x += dx;
            changed.width -= dx;
        } else if (toggle == Toggle::RIGHT_TOP ||
                   toggle == Toggle::RIGHT_MIDDLE ||
                   toggle == Toggle::RIGHT_BOTTOM) {
            changed.width += dx;
        }
        if (toggle == Toggle::LEFT_TOP || toggle == Toggle::MIDDLE_TOP ||
            toggle == Toggle::RIGHT_TOP) {
            changed.y += dy;
            changed.height -= dy;
        } else if (toggle == Toggle::LEFT_BOTTOM ||
                   toggle == Toggle::MIDDLE_BOTTOM ||
                   toggle == Toggle::RIGHT_BOTTOM) {
            changed.height += dy;
        }
        changed.width = image.roundMWidth(changed.width);
        changed.height = image.roundMHeight(changed.height);
        if (ratio_fix || image.scaleMode() || image.keep_ratio) {
            const auto base_width = image.scaleMode() ? image.baseWidth() : original.width;
            const auto base_height = image.scaleMode() ? image.baseHeight() : original.height;
            if (base_width > 0 && base_height > 0) {
                const auto width_delta = std::abs(changed.width - original.width);
                const auto height_delta = std::abs(changed.height - original.height);
                if (width_delta >= height_delta)
                    changed.height = std::max(1, changed.width * base_height / base_width);
                else
                    changed.width = std::max(1, changed.height * base_width / base_height);
                if (toggle == Toggle::LEFT_TOP || toggle == Toggle::MIDDLE_TOP ||
                    toggle == Toggle::RIGHT_TOP)
                    changed.y = original.y + original.height - changed.height;
                if (toggle == Toggle::LEFT_TOP || toggle == Toggle::LEFT_MIDDLE ||
                    toggle == Toggle::LEFT_BOTTOM)
                    changed.x = original.x + original.width - changed.width;
            }
        }
    }
    if (image.scaleMode()) {
        const auto base = image.baseWidth() >= image.baseHeight()
                              ? image.baseWidth() : image.baseHeight();
        const auto size = changed.width >= changed.height
                              ? changed.width : changed.height;
        image.newBoundsWithScale(changed.x, changed.y,
                                 base > 0 ? static_cast<std::uint32_t>(
                                     std::max(1, size * 100 / base)) : 100U);
    } else {
        image.newBounds(changed);
    }
}

bool ImagePane::handlePointer(lib::PointerEvent& event) {
    const auto point_x = static_cast<std::int32_t>(
        std::lround(event.position.x / std::max(1, image_scale_)));
    const auto point_y = static_cast<std::int32_t>(
        std::lround(event.position.y / std::max(1, image_scale_)));
    const auto control = hasModifier(event.modifiers, lib::Modifiers::Control);
    const auto shift = hasModifier(event.modifiers, lib::Modifiers::Shift);
    if (event.action == lib::PointerAction::Down &&
        event.button == lib::PointerButton::Primary) {
        ++drag_generation_;
        drag_bounds_.clear();
        moved_ = false;
        const auto index = findIndex(point_x, point_y);
        FlexImage* image = index < 0 ? nullptr
                                     : dynamic_cast<FlexImage*>(slots_[index].image);
        if (!control && !shift)
            for (const auto& slot : slots_)
                if (auto* selected = dynamic_cast<FlexImage*>(slot.image)) doDeselect(*selected);
        if (image != nullptr) {
            const auto toggle = image->inToggle(point_x, point_y, true);
            if (toggle != Toggle::NONE && !image->fixed) {
                doSelect(*image);
                mouse_image_ = image;
                drag_toggle_ = toggle;
                drag_start_ = {point_x, point_y};
                for (const auto& slot : slots_)
                    if (auto* selected = dynamic_cast<FlexImage*>(slot.image); selected && selected->selected)
                        drag_bounds_.emplace(selected, selected->bounds());
            } else {
                doSelect(*image);
                mouse_image_ = nullptr;
                drag_toggle_ = Toggle::NONE;
            }
        } else {
            mouse_image_ = nullptr;
            drag_toggle_ = Toggle::NONE;
            range_selecting_ = true;
            range_start_ = {point_x, point_y};
            range_end_ = range_start_;
        }
        event.handled = true;
        return true;
    }
    if (event.action == lib::PointerAction::Move) {
        if (mouse_image_ != nullptr && drag_toggle_ != Toggle::NONE) {
            const auto dx = point_x - drag_start_.x;
            const auto dy = point_y - drag_start_.y;
            auto ratio_width = std::numeric_limits<double>::quiet_NaN();
            auto ratio_height = std::numeric_limits<double>::quiet_NaN();
            const bool resize_group = resizing_image_with_ratio_ &&
                                      drag_toggle_ != Toggle::MOVE;
            if (resize_group) {
                const auto anchor = drag_bounds_.find(mouse_image_);
                if (anchor != drag_bounds_.end()) {
                    if (anchor->second.width != 0)
                        ratio_width = static_cast<double>(anchor->second.width + dx) /
                                      anchor->second.width;
                    if (anchor->second.height != 0)
                        ratio_height = static_cast<double>(anchor->second.height + dy) /
                                       anchor->second.height;
                }
                if (std::isnan(ratio_width)) {
                    for (auto slot = slots_.rbegin(); slot != slots_.rend(); ++slot) {
                        const auto* image = dynamic_cast<const FlexImage*>(slot->image);
                        if (image == nullptr || image->width() == 0) continue;
                        ratio_width = static_cast<double>(image->width() + dx) /
                                      image->width();
                        break;
                    }
                }
                if (std::isnan(ratio_height)) {
                    for (auto slot = slots_.rbegin(); slot != slots_.rend(); ++slot) {
                        const auto* image = dynamic_cast<const FlexImage*>(slot->image);
                        if (image == nullptr || image->height() == 0) continue;
                        ratio_height = static_cast<double>(image->height() + dy) /
                                       image->height();
                        break;
                    }
                }
            }
            for (const auto& slot : slots_)
                if (auto* image = dynamic_cast<FlexImage*>(slot.image);
                    image != nullptr && image->selected) {
                    auto image_dx = dx;
                    auto image_dy = dy;
                    const auto original = drag_bounds_.find(image);
                    if (original != drag_bounds_.end()) {
                        if (!std::isnan(ratio_width) && original->second.width != 0)
                            image_dx = static_cast<std::int32_t>(
                                original->second.width * ratio_width) -
                                original->second.width;
                        if (!std::isnan(ratio_height) && original->second.height != 0)
                            image_dy = static_cast<std::int32_t>(
                                original->second.height * ratio_height) -
                                original->second.height;
                    }
                    updateDrag(*image, drag_toggle_, image_dx, image_dy, shift);
                }
            if (resize_group) {
                const auto primary_saved = drag_bounds_.find(mouse_image_);
                if (primary_saved != drag_bounds_.end()) {
                    const auto primary_after = mouse_image_->newBounds();
                    const bool left_edge = drag_toggle_ == Toggle::LEFT_TOP ||
                                           drag_toggle_ == Toggle::LEFT_MIDDLE ||
                                           drag_toggle_ == Toggle::LEFT_BOTTOM;
                    const bool top_edge = drag_toggle_ == Toggle::LEFT_TOP ||
                                          drag_toggle_ == Toggle::MIDDLE_TOP ||
                                          drag_toggle_ == Toggle::RIGHT_TOP;
                    for (const auto& slot : slots_) {
                        auto* peer = dynamic_cast<FlexImage*>(slot.image);
                        if (peer == nullptr || peer == mouse_image_ ||
                            !peer->selected || peer->fixed || !peer->visible())
                            continue;
                        const auto saved = drag_bounds_.find(peer);
                        if (saved == drag_bounds_.end()) continue;
                        const auto original = saved->second;
                        if (primary_saved->second.width != primary_after.width) {
                            const auto scale = original.width == 0
                                ? 1.0
                                : static_cast<double>(peer->newWidth()) / original.width;
                            const auto offset = left_edge
                                ? static_cast<double>(original.x - primary_saved->second.x) * scale
                                : static_cast<double>(original.x - primary_after.x) * scale;
                            peer->newX(primary_after.x + static_cast<std::int32_t>(offset));
                        }
                        if (primary_saved->second.height != primary_after.height) {
                            const auto scale = original.height == 0
                                ? 1.0
                                : static_cast<double>(peer->newHeight()) / original.height;
                            const auto offset = top_edge
                                ? static_cast<double>(original.y - primary_saved->second.y) * scale
                                : static_cast<double>(original.y - primary_after.y) * scale;
                            peer->newY(primary_after.y + static_cast<std::int32_t>(offset));
                        }
                    }
                }
            }
            moved_ = true;
            redrawAll();
            event.handled = true;
            return true;
        }
        if (range_selecting_) {
            range_end_ = {point_x, point_y};
            const Rectangle selected_rect{
                std::min(range_start_.x, range_end_.x),
                std::min(range_start_.y, range_end_.y),
                std::abs(range_start_.x - range_end_.x),
                std::abs(range_start_.y - range_end_.y)};
            for (const auto& slot : slots_) {
                auto* image = dynamic_cast<FlexImage*>(slot.image);
                if (image == nullptr || !image->visible()) continue;
                if (range_selectable_ && !range_selectable_(*image)) continue;
                const auto hit = intersects(image->bounds(), selected_rect);
                if (hit) doSelect(*image);
                else if (!control && !shift) doDeselect(*image);
            }
            redrawAll();
            event.handled = true;
            return true;
        }
    }
    if (event.action == lib::PointerAction::Up &&
        event.button == lib::PointerButton::Primary) {
        const std::weak_ptr<int> life = drag_lifetime_;
        const auto revision = image_revision_;
        const auto generation = ++drag_generation_;
        std::vector<Slot> changed;
        if (mouse_image_ != nullptr) for (const auto& slot : slots_)
            if (auto* image = dynamic_cast<FlexImage*>(slot.image);
                image && image->selected && image->bounds() != image->newBounds()) changed.push_back(slot);
        mouse_image_ = nullptr;
        drag_toggle_ = Toggle::NONE;
        range_selecting_ = false;
        drag_bounds_.clear();
        if (!changed.empty()) {
            // D版と同様、最初の画像の変更を確定する前に一度だけ通知し、 取
            // り消し処理の監視側が、すべての元の境界をまだ読めるようにする。
            const auto callbacks = changing_images_;
            for (const auto& callback : callbacks) {
                if (callback) callback();
                if (life.expired() || image_revision_ != revision || drag_generation_ != generation)
                    return event.handled = true;
            }
            for (const auto& slot : changed) {
                static_cast<FlexImage*>(slot.image)->resize();
                if (life.expired() || image_revision_ != revision || drag_generation_ != generation)
                    return event.handled = true;
            }
        }
        redrawAll();
        event.handled = true;
        return true;
    }
    return false;
}

bool ImagePane::handleKey(lib::KeyEvent& event) {
    const auto control = hasModifier(event.modifiers, lib::Modifiers::Control);
    const auto alternate = hasModifier(event.modifiers, lib::Modifiers::Alt);
    const auto shift = hasModifier(event.modifiers, lib::Modifiers::Shift);
    const auto step = control && alternate ? high_point_ : 1;
    bool handled = true;
    for (const auto& slot : slots_) {
        auto* image = dynamic_cast<FlexImage*>(slot.image);
        if (image == nullptr || !image->selected || image->fixed) continue;
        switch (event.key) {
        case lib::Key::Left:
            if (shift) image->scaleMode() ? image->newScale(image->newScale() - step)
                                          : image->newWidth(image->newWidth() - step);
            else image->newX(image->newX() - step);
            break;
        case lib::Key::Right:
            if (shift) image->scaleMode() ? image->newScale(image->newScale() + step)
                                          : image->newWidth(image->newWidth() + step);
            else image->newX(image->newX() + step);
            break;
        case lib::Key::Up:
            if (shift) image->scaleMode() ? image->newScale(image->newScale() - step)
                                          : image->newHeight(image->newHeight() - step);
            else image->newY(image->newY() - step);
            break;
        case lib::Key::Down:
            if (shift) image->scaleMode() ? image->newScale(image->newScale() + step)
                                          : image->newHeight(image->newHeight() + step);
            else image->newY(image->newY() + step);
            break;
        case lib::Key::Escape:
            image->reset();
            break;
        case lib::Key::Enter:
        case lib::Key::Space:
            image->resize();
            break;
        default:
            handled = false;
            break;
        }
    }
    if (handled) redrawAll();
    return handled;
}

void ImagePane::paint(lib::DrawingContext& context,
                      const Rectangle client_area) const {
    const Rectangle canvas{0, 0, width_, height_};
    const auto clip_rect = validRect(client_area) ? client_area : canvas;
    context.save();
    context.clip(asLibRect(clip_rect, static_cast<float>(image_scale_)));
    context.clear(background_color_.value_or(lib::Color{0U, 0U, 0U, 0U}));
    for (const auto& value : appends_) {
        if (value.resource == 0U) continue;
        const auto* frame = firstFrame(value);
        const auto source = frame == nullptr
                                ? lib::Rect{}
                                : lib::Rect{0.0F, 0.0F,
                                            static_cast<float>(frame->pixel_width),
                                            static_cast<float>(frame->pixel_height)};
        context.draw_image(value.resource, source,
                           {0.0F, 0.0F, static_cast<float>(width_ * image_scale_),
                            static_cast<float>(height_ * image_scale_)},
                           lib::Interpolation::Nearest);
    }
    for (auto* image : orderedImages()) {
        if (image == nullptr) continue;
        const std::weak_ptr<void> image_life = image->lifetime_;
        const auto* flex = dynamic_cast<const FlexImage*>(image);
        const bool alpha_composed = image->type() == ImageType::Image &&
            image->alpha() >= 0 && image->alpha() < 255;
        const bool selection_filter = image->visible() && flex && flex->selected &&
            flex->has_selection_filter && !alpha_composed;
        const auto filter_bounds = asLibRect(image->bounds(), static_cast<float>(image_scale_));
        image->draw(context, clip_rect);
        // D版と同じく対象画像の直後に重ね、前面画像と操作ハンドルを覆わない。
        // 半透明画像はD版のアルファ合成経路に合わせ、追加のフィルターを描かない。
        if (selection_filter && !image_life.expired())
            context.fill_rect(filter_bounds, {0U, 0U, 0U, 64U});
    }
    for (auto* image : orderedImages())
        if (auto* flex = dynamic_cast<FlexImage*>(image);
            flex != nullptr) {
            flex->drawToggle(context, image_scale_, draw_xor_selection_line_);
        }
    if (grid_x_ > 1 || grid_y_ > 1) {
        const auto grid = grid_color_.value_or(lib::Color{96U, 96U, 96U, 255U});
        for (std::int32_t x = grid_x_; x < width_; x += std::max(1, grid_x_))
            context.draw_line({static_cast<float>(x * image_scale_), 0.0F},
                              {static_cast<float>(x * image_scale_),
                               static_cast<float>(height_ * image_scale_)},
                              grid, 1.0F, lib::StrokeStyle::Dot);
        for (std::int32_t y = grid_y_; y < height_; y += std::max(1, grid_y_))
            context.draw_line({0.0F, static_cast<float>(y * image_scale_)},
                              {static_cast<float>(width_ * image_scale_),
                               static_cast<float>(y * image_scale_)},
                              grid, 1.0F, lib::StrokeStyle::Dot);
    }
    if (range_selecting_) {
        const Rectangle range{
            std::min(range_start_.x, range_end_.x),
            std::min(range_start_.y, range_end_.y),
            std::abs(range_start_.x - range_end_.x),
            std::abs(range_start_.y - range_end_.y)};
        (void)cellrender::draw_selection_rect(context,
            asLibRect(range,static_cast<float>(image_scale_)),draw_xor_selection_line_,1);
    }
    context.restore();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::images
