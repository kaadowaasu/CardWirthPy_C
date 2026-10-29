#include "compat.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/python_integer_value.hpp"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

namespace cw::lib {

std::shared_ptr<Surface> convert_surface_alpha(const std::shared_ptr<Surface>& source)
{
    if (!source) return {};
    auto result = std::make_shared<Surface>(*source);
    if (!result->has_pixel_alpha)
        // SDL_ConvertSurface/pygame の convert_alpha と同様、RGB
        // の描画面全体の不透明度を新しいアルファへ反映する。 RGB にアルファを掛けた値にはしない。
        simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U,
            result->surface_alpha_enabled ? result->surface_alpha : 255);
    if (result->has_colorkey) {
        const auto [r, g, b] = result->colorkey;
        simd_image::colorkey_alpha_zero(result->rgba.data(), result->rgba.size() / 4U,
            static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), true);
    }
    // 不透明な入力でも、透過色によって透明なピクセルが生じる場合がある。
    result->known_opaque = !result->has_colorkey &&
        (result->has_pixel_alpha ? result->known_opaque :
            (!result->surface_alpha_enabled || result->surface_alpha == 255));
    result->has_pixel_alpha = true;
    // pygame 1.9.6 の SDL_ConvertSurface と同様、明示的な set_alpha
    // 後は描画面のアルファ情報にゼロを保持する。 上で求めたピクセルアルファとは別に扱う。
    result->surface_alpha = source->has_pixel_alpha || source->surface_alpha_enabled ? 0 : 255;
    result->pixel_bits = 32;
    result->has_colorkey = false;
    result->bmpdepthis1 = false;
    return result;
}

std::shared_ptr<Bitmap> rescale_bitmap_normal(const std::shared_ptr<Bitmap>& source, Size size) {
    if (!source) return {};
    auto result = std::make_shared<Bitmap>(*source);
    const auto sw = source->size.width, sh = source->size.height;
    if (size.width <= 0 || size.height <= 0 || sw <= 0 || sh <= 0)
        throw std::invalid_argument("bitmap rescale requires positive dimensions");
    const auto source_limit = source->rgba.size() / 4U;
    const auto target_limit = result->rgba.max_size() / 4U;
    if (static_cast<std::uint64_t>(sw) > source_limit ||
        static_cast<std::uint64_t>(sh) > source_limit / static_cast<std::uint64_t>(sw) ||
        static_cast<std::uint64_t>(size.width) > target_limit ||
        static_cast<std::uint64_t>(size.height) > target_limit / static_cast<std::uint64_t>(size.width))
        throw std::invalid_argument("bitmap rescale has invalid RGBA dimensions");
    const auto target_pixels = static_cast<std::uint64_t>(size.width) * static_cast<std::uint64_t>(size.height);
    result->size = size;
    result->rgba.resize(static_cast<std::size_t>(target_pixels) * 4U);
    // ネイティブBitmapはアルファ非乗算
    // のRGBAを保持する。Windowsの
    // PyでImageからBitmapへ戻す
    // 際の、GDIによるアルファ乗算・非乗算
    // への復元に伴う精度低下は再現しない。
    const bool shrink = sw >= size.width && sh >= size.height &&
        sw % size.width == 0 && sh % size.height == 0;
    const Integer xf = shrink ? sw / size.width : 1, yf = shrink ? sh / size.height : 1;
    const auto xdelta = (static_cast<std::uint64_t>(sw) << 16U) / size.width;
    const auto ydelta = (static_cast<std::uint64_t>(sh) << 16U) / size.height;
    for (Integer y = 0; y < size.height; ++y) {
        for (Integer x = 0; x < size.width; ++x) {
            const Integer sx = shrink ? x * xf : static_cast<Integer>((static_cast<std::uint64_t>(x) * xdelta) >> 16U);
            const Integer sy = shrink ? y * yf : static_cast<Integer>((static_cast<std::uint64_t>(y) * ydelta) >> 16U);
            std::uint64_t sums[4]{}, count{};
            for (Integer yy = 0; yy < yf; ++yy) {
                for (Integer xx = 0; xx < xf; ++xx) {
                    const auto i = static_cast<std::size_t>(((sy + yy) * sw + sx + xx) * 4);
                    const auto r = source->rgba[i], g = source->rgba[i + 1], b = source->rgba[i + 2];
                    const bool key = source->has_colorkey &&
                        std::tuple<Integer, Integer, Integer>{r, g, b} == source->colorkey;
                    const auto a = key ? 0 : source->has_pixel_alpha ? source->rgba[i + 3] : 255;
                    if (shrink && key) continue;
                    if (!shrink || a > 0) { sums[0] += r; sums[1] += g; sums[2] += b; }
                    sums[3] += a;
                    ++count;
                }
            }
            const auto out = static_cast<std::size_t>((y * size.width + x) * 4);
            if (count) {
                for (std::size_t channel = 0; channel < 4; ++channel)
                    result->rgba[out + channel] = static_cast<std::uint8_t>(sums[channel] / count);
            } else {
                const auto [r, g, b] = source->colorkey;
                result->rgba[out] = static_cast<std::uint8_t>(r);
                result->rgba[out + 1] = static_cast<std::uint8_t>(g);
                result->rgba[out + 2] = static_cast<std::uint8_t>(b);
                result->rgba[out + 3] = 0;
            }
        }
    }
    return result;
}

bool Value::as_bool() const {
    if (const auto* value = std::get_if<std::monostate>(&storage_)) {
        (void)value;
        return false;
    }
    if (const auto* value = std::get_if<bool>(&storage_)) return *value;
    if (const auto* value = std::get_if<Integer>(&storage_)) return *value != 0;
    if (const auto* value = std::get_if<Real>(&storage_)) return *value != 0.0;
    if (const auto* value = std::get_if<Decimal>(&storage_)) return !value->is_zero();
    if (const auto* value = std::get_if<String>(&storage_)) return !value->empty();
    if (const auto* value = std::get_if<Bytes>(&storage_)) return !value->empty();
    if (const auto integer = python_integer_object(*this)) return !integer->is_zero();
    if (const auto* value = std::get_if<ObjectPtr>(&storage_)) return static_cast<bool>(*value);
    return false;
}

Integer Value::as_integer() const {
    if (const auto integer = python_integer_object(*this)) return integer->to_integer();
    if (const auto* value = std::get_if<Integer>(&storage_)) return *value;
    if (const auto* value = std::get_if<Real>(&storage_)) return static_cast<Integer>(*value);
    if (const auto* value = std::get_if<bool>(&storage_)) return *value ? 1 : 0;
    if (const auto* value = std::get_if<Decimal>(&storage_)) {
        // Decimal を double 経由で変換しない。 Integer
        // 変換を明示的に要求した呼び出し側には、桁あふれを通知する。
        return value->to_integer(DecimalRounding::Down);
    }
    if (const auto* value = std::get_if<String>(&storage_)) {
        try {
            return std::stoll(*value);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

Real Value::as_real() const {
    if (const auto integer = python_integer_object(*this)) return integer->to_real();
    if (const auto* value = std::get_if<Real>(&storage_)) return *value;
    if (const auto* value = std::get_if<Integer>(&storage_)) return static_cast<Real>(*value);
    if (const auto* value = std::get_if<bool>(&storage_)) return *value ? 1.0 : 0.0;
    if (const auto* value = std::get_if<Decimal>(&storage_)) {
        try {
            return std::stod(value->to_string(false));
        } catch (const std::out_of_range&) {
            return value->negative() ? -std::numeric_limits<Real>::infinity() : std::numeric_limits<Real>::infinity();
        } catch (...) {
            return 0.0;
        }
    }
    if (const auto* value = std::get_if<String>(&storage_)) {
        try {
            return std::stod(*value);
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

String Value::as_string() const {
    if (const auto integer = python_integer_object(*this)) return integer->to_string();
    if (const auto* value = std::get_if<String>(&storage_)) return *value;
    if (const auto* value = std::get_if<Decimal>(&storage_)) return value->to_string(true);
    if (const auto* value = std::get_if<Integer>(&storage_)) return std::to_string(*value);
    if (const auto* value = std::get_if<Real>(&storage_)) {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<Real>::max_digits10) << *value;
        return out.str();
    }
    if (const auto* value = std::get_if<bool>(&storage_)) return *value ? "TRUE" : "FALSE";
    return {};
}

bool FileSystem::exists(const Path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

Bytes FileSystem::read_binary(const Path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

String FileSystem::read_text(const Path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return String(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool FileSystem::write_binary(const Path& path, const Bytes& data) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(stream);
}

std::shared_ptr<SqlConnection> Database::open(const Path& path) const {
    auto connection = std::make_shared<SqlConnection>();
    connection->filename = path;
    connection->open = true;
    return connection;
}

std::shared_ptr<SqlCursor> Database::execute(
    const std::shared_ptr<SqlConnection>& connection,
    const String& sql,
    const Arguments& parameters) const {
    (void)connection;
    (void)sql;
    (void)parameters;
    return std::make_shared<SqlCursor>();
}

std::shared_ptr<XmlDocument> XmlParser::parse(const String& xml) const {
    auto document = std::make_shared<XmlDocument>();
    document->root = std::make_shared<XmlNode>();
    document->root->text = xml;
    return document;
}

}  // namespace cw::lib
