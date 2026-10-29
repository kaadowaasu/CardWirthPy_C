// cw/image.py の移植。SDL/wx 固有のラスター処理は以下で区別する。
#include "cw/image.hpp"

#include "app/debug_control_server.hpp"
#include "cw/animatedimage.hpp"
#include "cw/binary/image.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/event.hpp"
#include "cw/header.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/rgba_storage_size.hpp"
#include "platform/storage/storage_mount.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/text_raster.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace {
using ::cw::lib::Bytes;
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;

// 整数に対する Python の cw.s() と同様、int(num
// * UP_SCR) でゼロ方向へ切り捨てる。 倍率 1 では拡縮しない
// 。lround() では小数倍率の時に 1px ずれるため使わない。
Integer py_scaled(Integer value, Real scale)
{
    if (!(scale > 0.0) || !std::isfinite(scale) || scale == 1.0) return value;
    return static_cast<Integer>(static_cast<Real>(value) * scale);
}

Integer scaled_win(Integer value, Real scale)
{
    return py_scaled(value, scale);
}
bool has_python_string_value(const std::optional<String>& value)
{
    // Py と同様、文字列の真偽で判定し、明示的な空文字列も false とする。
    return value && !value->empty();
}
std::uint16_t le16(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}
std::uint32_t le32(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}
std::int32_t sle32(const Bytes& data, std::size_t offset) {
    return static_cast<std::int32_t>(le32(data, offset));
}
void put_le32(Bytes& data, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data[offset + static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((value >> (i * 8)) & 0xff);
}
bool bmp40(const Bytes& data) {
    return data.size() >= 54 && data[0] == 'B' && data[1] == 'M' && le32(data, 14) == 40;
}

struct DecodedImage {
    std::shared_ptr<::cw::lib::Surface> surface{};
    ::cw::lib::imagecodec::Info info{};
};

bool image_file_exists(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->is_file();
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::optional<::cw::lib::Real> image_file_stamp(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->is_file()
            ? std::optional<::cw::lib::Real>(static_cast<::cw::lib::Real>(status->modified))
            : std::nullopt;
    }
    std::error_code ec;
    const auto value = std::filesystem::last_write_time(path, ec);
    return ec ? std::nullopt
              : std::optional<::cw::lib::Real>(
                    static_cast<::cw::lib::Real>(value.time_since_epoch().count()));
}

std::optional<Bytes> read_image_data(String path, bool can_loaded_scaledimage)
{
    if (::cw::binary::image::path_is_code(path))
        return ::cw::binary::image::code_to_data(std::move(path));
    if (path.empty()) return std::nullopt;
    const auto up_scr = ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
    const auto [resolved, scale] = ::cw::util::find_scaledimagepath(
        std::move(path), up_scr, can_loaded_scaledimage, false);
    (void)scale;
    const auto file_path = std::filesystem::u8path(resolved);
    if (::cardwirth::platform::is_mounted_user_path(file_path)) {
        const auto mounted = ::cardwirth::platform::read_mounted_user_file(file_path);
        if (!mounted) return std::nullopt;
        Bytes data;
        data.reserve(mounted->size());
        for (const auto value : *mounted)
            data.push_back(std::to_integer<std::uint8_t>(value));
        return data;
    }
    if (!std::filesystem::is_regular_file(file_path)) return std::nullopt;
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return std::nullopt;
    Bytes data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return data;
}

void repair_bitmap_data(Bytes& data)
{
    if (!bmp40(data)) return;
    try {
        // Python の bytes と同様、互換再試行が失敗してもデコーダーが
        // 元の入力を使えるようにする。 すべての修復が終わってから変更を確定する。
        auto repaired = ::cw::image::patch_rle4bitmap(data);
        auto [fixed32, fixed32_ok] = ::cw::image::fix_cwnext32bitbitmap(std::move(repaired));
        (void)fixed32_ok;
        auto [fixed16_value, fixed16_ok] = ::cw::image::fix_cwnext16bitbitmap(std::move(fixed32));
        (void)fixed16_ok;
        if (const auto* fixed16 = std::get_if<Bytes>(&fixed16_value.storage()))
            data = *fixed16;
    } catch (const std::exception&) {
        // 元のデコードエラーは、下の imagecodec から通知する。
        // ビットマップ修復は互換性のための再試行であり、入力バイトを捨て
        // たり、不正画像を架空の描画面に置き換えたりする理由にはしない。
    }
}

DecodedImage decode_image_data(Bytes data, bool mask)
{
    repair_bitmap_data(data);
    const auto info = ::cw::lib::imagecodec::inspect(data);
    const auto rgba = ::cw::lib::imagecodec::decode_rgba(data, 0);
    auto surface = std::make_shared<::cw::lib::Surface>();
    surface->size = {rgba.width, rgba.height};
    surface->rgba = rgba.pixels;
    surface->has_pixel_alpha = ::cw::lib::imagecodec::surface_has_pixel_alpha(info);
    surface->bmpdepthis1 = ::cw::image::get_bmpdepth_view(data) == 1;
    bool applied_mask = false;
    std::uint8_t mask_r = 0, mask_g = 0, mask_b = 0;
    if (mask && !info.has_alpha && !surface->rgba.empty()) {
        mask_r = surface->rgba[0];
        mask_g = surface->rgba[1];
        mask_b = surface->rgba[2];
        applied_mask = true;
        ::cw::lib::simd_image::colorkey_alpha_zero(
            surface->rgba.data(), surface->rgba.size() / 4U, mask_r, mask_g, mask_b);
    }
    const bool is_png = info.format == ::cw::lib::imagecodec::Format::PNG;
    const bool wrap_depth1 = !is_png && surface->bmpdepthis1 && mask;
    ::cw::imageretouch::set_surface_metadata(surface, {
        applied_mask, {mask_r, mask_g, mask_b}, wrap_depth1, 1.0});
    return {std::move(surface), info};
}

using Surface = ::cw::lib::Surface;
using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using Value = ::cw::lib::Value;

std::shared_ptr<Surface> make_surface(Integer width, Integer height, std::uint8_t r = 0, std::uint8_t g = 0,
                                      std::uint8_t b = 0, std::uint8_t a = 0)
{
    auto image = std::make_shared<Surface>();
    image->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    // pygame の確保済み Surface と異なり、この vector に
    // は先行する形式・確保の検証がない。 寸法を掛ける前に桁あふれを拒否する。
    const auto w = static_cast<std::uint64_t>(image->size.width);
    const auto h = static_cast<std::uint64_t>(image->size.height);
    const auto limit = image->rgba.max_size();
    std::size_t bytes{};
    try { bytes = ::cw::lib::checked_rgba_byte_count(w, h, limit); }
    catch (const std::length_error&) {
        throw std::length_error("image: dimensions exceed RGBA allocation limit");
    }
    image->rgba.resize(bytes);
    ::cw::lib::simd_image::fill_rgba(image->rgba.data(), image->rgba.size() / 4U, r, g, b, a);
    return image;
}

std::shared_ptr<Surface> clone_surface(const std::shared_ptr<Surface>& source)
{
    return source ? std::make_shared<Surface>(*source) : nullptr;
}

void blit_surface(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source, Integer x, Integer y,
                  std::optional<::cw::lib::Rect> source_rect = std::nullopt,
                  std::optional<::cw::lib::Rect> destination_clip = std::nullopt)
{
    if (!dest || !source) return;
    if (source->size.width <= 0 || source->size.height <= 0 ||
        dest->size.width <= 0 || dest->size.height <= 0) return;
    const auto source_required = static_cast<std::size_t>(source->size.width) *
        static_cast<std::size_t>(source->size.height) * 4U;
    const auto destination_required = static_cast<std::size_t>(dest->size.width) *
        static_cast<std::size_t>(dest->size.height) * 4U;
    if (source->rgba.size() < source_required || dest->rgba.size() < destination_required) return;
    const auto area = source_rect.value_or(::cw::lib::Rect{0, 0, source->size.width, source->size.height});
    // Py の subsurface と同様、元のピクセルを共有する。 カ
    // ード端の帯をコピーして確保せず、同じ画像転送処理へ入力矩形を渡す。
    if (area.x < 0 || area.y < 0 || area.width < 0 || area.height < 0 ||
        area.x > source->size.width || area.y > source->size.height ||
        area.width > source->size.width - area.x || area.height > source->size.height - area.y)
        throw std::invalid_argument("blit: source rectangle outside surface");
    Integer left = 0, top = 0, right = dest->size.width, bottom = dest->size.height;
    if (destination_clip) {
        const auto& clip = *destination_clip;
        if (clip.width <= 0 || clip.height <= 0) return;
        // 正の幅・高さを加える前に範囲を制限し、 過大なネイティ
        // ブのクリップで右端・下端が桁あふれしないようにする。
        const auto clipped_end = [](Integer start, Integer extent, Integer limit) {
            if (start >= limit) return limit;
            if (start < 0) return std::clamp<Integer>(start + extent, 0, limit);
            return start + std::min<Integer>(extent, limit - start);
        };
        left = std::clamp<Integer>(clip.x, 0, right);
        top = std::clamp<Integer>(clip.y, 0, bottom);
        right = clipped_end(clip.x, clip.width, right);
        bottom = clipped_end(clip.y, clip.height, bottom);
        if (left >= right || top >= bottom) return;
    }
    const Integer source_x = area.x + std::max<Integer>(0, left - x);
    const Integer source_y = area.y + std::max<Integer>(0, top - y);
    const Integer width = std::min<Integer>(area.x + area.width - source_x,
                                             right - std::max<Integer>(left, x));
    const Integer height = std::min<Integer>(area.y + area.height - source_y,
                                              bottom - std::max<Integer>(top, y));
    if (width <= 0 || height <= 0) return;
    const auto source_offset = static_cast<std::size_t>(source_y * source->size.width + source_x) * 4U;
    const auto destination_offset = static_cast<std::size_t>(
        std::max<Integer>(top, y) * dest->size.width + std::max<Integer>(left, x)) * 4U;
    // SDL1/pygame と同様、SRCALPHA の転送は描画面全体の不透明度を無視し、RGB はそれを使う。
    const auto opacity = source->has_pixel_alpha ? std::uint8_t{255} : source->surface_alpha;
    const auto blend = [&](const std::uint8_t* src, std::size_t src_stride,
                           std::uint8_t* dst, std::size_t dst_stride, std::size_t w, std::size_t h) {
        // pygame の RGB から SRCALPHA への転送と同様、 透明な出力先
        // も含め、全体アルファを ALPHA_BLEND の実効入力アルファとして使う。
        if (!source->has_pixel_alpha && dest->has_pixel_alpha)
            ::cw::lib::simd_image::alpha_blend_rgba(src, src_stride, dst, dst_stride, w, h, opacity, true);
        else if (source->has_pixel_alpha && !dest->has_pixel_alpha)
            ::cw::lib::simd_image::alpha_blend_rgba(src, src_stride, dst, dst_stride, w, h, 255, false, true);
        else if (!source->has_pixel_alpha && (source->pixel_bits != 32 || dest->pixel_bits != 32 || source->has_colorkey))
            // 表現の差として、パックされた RGBA は展開済みの RGB565 色を持つ。 SDL1
            // の RGB16 から RGB への全体アルファ転送では、シフトしたチャンネルビットを使う。
            ::cw::lib::simd_image::surface_alpha_blend_rgba(src, src_stride, dst, dst_stride, w, h, opacity, true, source->pixel_bits == 16, source->has_colorkey);
        else
            ::cw::lib::simd_image::alpha_blend_rgba(src, src_stride, dst, dst_stride, w, h, opacity);
    };
    if (source->has_colorkey && !source->has_pixel_alpha) {
        const auto [r, g, b] = source->colorkey;
        for (Integer row = 0; row < height; ++row) for (Integer column = 0; column < width; ++column) {
            const auto si = source_offset + static_cast<std::size_t>(row * source->size.width + column) * 4U;
            const auto di = destination_offset + static_cast<std::size_t>(row * dest->size.width + column) * 4U;
            if (source->rgba[si] == r && source->rgba[si + 1] == g && source->rgba[si + 2] == b) {
                // pygame と同様、透過色付き RGB から SRCALPHA への転送で
                // はアルファゼロで合成する。 透明な出力先にも、見えない RGB の値は渡す。
                if (dest->has_pixel_alpha)
                    ::cw::lib::simd_image::alpha_blend_rgba(source->rgba.data() + si, 4,
                        dest->rgba.data() + di, 4, 1, 1, 0, true);
                continue;
            }
            blend(source->rgba.data() + si, 4, dest->rgba.data() + di, 4, 1, 1);
        }
        return;
    }
    blend(
        source->rgba.data() + source_offset, static_cast<std::size_t>(source->size.width) * 4U,
        dest->rgba.data() + destination_offset, static_cast<std::size_t>(dest->size.width) * 4U,
        static_cast<std::size_t>(width), static_cast<std::size_t>(height));
}

std::shared_ptr<Surface> scale_surface(const std::shared_ptr<Surface>& source, Integer width, Integer height)
{
    if (!source) return nullptr;
    if (source->size.width == width && source->size.height == height) return clone_surface(source);
    auto result = make_surface(width, height);
    result->scr_scale = source->scr_scale;
    result->bmpdepthis1 = source->bmpdepthis1;
    result->has_colorkey = source->has_colorkey;
    result->colorkey = source->colorkey;
    result->depth1 = true;
    result->logical_scale = source->logical_scale;
    result->surface_alpha = source->surface_alpha;
    result->surface_alpha_enabled = source->surface_alpha_enabled;
    if (!source->size.width || !source->size.height) return result;
    result->has_pixel_alpha = source->has_pixel_alpha;
    result->pixel_bits = source->pixel_bits;
    for (Integer y = 0; y < result->size.height; ++y) for (Integer x = 0; x < result->size.width; ++x) {
        const auto sx = std::min<Integer>(source->size.width - 1, x * source->size.width / std::max<Integer>(1, width));
        const auto sy = std::min<Integer>(source->size.height - 1, y * source->size.height / std::max<Integer>(1, height));
        const auto si = static_cast<std::size_t>((sy * source->size.width + sx) * 4);
        const auto di = static_cast<std::size_t>((y * result->size.width + x) * 4);
        if (si + 3 < source->rgba.size() && di + 3 < result->rgba.size()) std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(si), 4, result->rgba.begin() + static_cast<std::ptrdiff_t>(di));
    }
    return result;
}

std::shared_ptr<Surface> scale_loaded_image(const std::shared_ptr<Surface>& source,
                                            const String& path,
                                            bool can_loaded_scaledimage)
{
    if (!source || source->size.width <= 0 || source->size.height <= 0) return source;
    const Real target_scale = (::cw::g_UP_SCR > 0.0 && std::isfinite(::cw::g_UP_SCR))
        ? ::cw::g_UP_SCR : 1.0;
    Integer source_scale = 1;
    if (!::cw::binary::image::path_is_code(path))
        source_scale = std::max<Integer>(1, std::get<1>(::cw::util::find_scaledimagepath(
            path, target_scale, can_loaded_scaledimage, false)));
    if (target_scale == static_cast<Real>(source_scale)) return source;
    // デコードした xN ファイルは、Py の load_image が返す
    // Depth1Surface と同様、論理寸法の N 倍のピクセルを持つ。
    // デコーダーの入力・キャッシュ自体を変えず、コピーへその情報を付け、
    // 共通の拡縮処理が寸法の切り捨てと同じフィルター選択を行えるようにする。
    auto pixels = clone_surface(source);
    pixels->logical_scale = static_cast<Real>(source_scale);
    pixels->scr_scale = static_cast<Real>(source_scale);
    const auto scaled = ::cw::_s_impl(Value(std::static_pointer_cast<::cw::lib::Object>(pixels)), target_scale);
    return std::dynamic_pointer_cast<Surface>(std::get<::cw::lib::ObjectPtr>(scaled.storage()));
}

std::shared_ptr<Surface> scale_display(const std::shared_ptr<Surface>& source, Real target_scale)
{
    if (!source || source->size.width <= 0 || source->size.height <= 0) return source;
    if (!std::isfinite(target_scale) || target_scale <= 0.0) target_scale = 1.0;
    const auto scaled = ::cw::_s_impl(Value(std::static_pointer_cast<::cw::lib::Object>(source)), target_scale);
    return std::dynamic_pointer_cast<Surface>(std::get<::cw::lib::ObjectPtr>(scaled.storage()));
}

std::shared_ptr<Surface> smooth_scale_surface(const std::shared_ptr<Surface>& source,
                                              Integer width, Integer height)
{
    if (!source) return nullptr;
    if (source->size.width == width && source->size.height == height) return source;
    auto result = make_surface(width, height);
    result->scr_scale = source->scr_scale;
    result->bmpdepthis1 = source->bmpdepthis1;
    result->has_colorkey = source->has_colorkey;
    result->colorkey = source->colorkey;
    result->depth1 = true;
    result->logical_scale = source->logical_scale;
    result->surface_alpha = source->surface_alpha;
    result->surface_alpha_enabled = source->surface_alpha_enabled;
    if (source->size.width <= 0 || source->size.height <= 0 || source->rgba.empty()) return result;
    result->has_pixel_alpha = source->has_pixel_alpha;
    result->pixel_bits = source->pixel_bits;
    const auto sw = static_cast<std::size_t>(source->size.width);
    const auto sh = static_cast<std::size_t>(source->size.height);
    // 不正な C++ の内部バッファーに相当する pygame Surface は
    // ない。 所有する RGBA バイト列の外を読まず、サンプリング前に拒否する。
    if (sw > source->rgba.size() / 4 || sh > source->rgba.size() / (sw * 4))
        throw std::invalid_argument("smoothscale: incomplete RGBA surface");
    ::cw::lib::simd_image::smoothscale_rgba(source->rgba.data(), sw, sh,
        result->rgba.data(), static_cast<std::size_t>(width), static_cast<std::size_t>(height));
    if (!result->has_pixel_alpha) {
        ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
        if (result->has_colorkey) {
            const auto [r, g, b] = result->colorkey;
            ::cw::lib::simd_image::colorkey_alpha_zero(result->rgba.data(), result->rgba.size() / 4U,
                static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
        }
    }
    return result;
}


template<class T> std::shared_ptr<T> object_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

String value_string(const Value& value, String fallback = {})
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    return fallback;
}

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    try { return std::stoll(value_string(value)); } catch (...) { return fallback; }
}

std::shared_ptr<Surface> surface_value(const Value& value)
{
    if (const auto surface = object_value<Surface>(value)) return surface;
    if (const auto bitmap = object_value<::cw::lib::Bitmap>(value))
        return std::make_shared<Surface>(static_cast<const Surface&>(*bitmap));
    return {};
}

std::shared_ptr<Surface> resource_surface(const Value& table_value, const String& key)
{
    const auto table = object_value<::cw::setting::ResourceTable>(table_value);
    if (!table) return {};
    // ResourceTable は Python の遅延 nokeyfunc と同様、最初
    // の取得でスキンリソースを作成・読み込みする。 ここで m_entries だけを確認す
    // ると、未読み込みのカード背景・状態画像がすべて黒い代替画像になるため、それは行わない。
    try { return surface_value(table->get_item(Value(key))); }
    // 構築・終了中はネイティブのリソース登録表が利用できない場合があ
    // るため、その境界は空画像として扱う。 通常のスキン表は Python
    // 互換の空画像の既定値を持ち、ここでは例外を出さない。
    catch (const std::exception&) { return {}; }
}

std::shared_ptr<::cw::imageretouch::Font> rsrc_font(const String& key)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
    const auto fonts = object_value<::cw::setting::ResourceTable>(::cw::g_cwpy->m_rsrc->m_fonts);
    if (!fonts) return {};
    try { return object_value<::cw::imageretouch::Font>(fonts->get_item(Value(key))); }
    catch (...) { return {}; }
}

Integer card_name_color_hint(const String& bgtype)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return 255;
    const auto table = object_value<::cw::setting::ResourceTable>(::cw::g_cwpy->m_rsrc->m_cardnamecolorhints);
    if (!table) return 255;
    try { return value_integer(table->get_item(Value(bgtype)), 255); }
    catch (...) { return 255; }
}

bool card_name_is_white(const String& bgtype)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return false;
    return card_name_color_hint(bgtype) < ::cw::g_cwpy->m_rsrc->get_cardnamecolorborder();
}

void tint_opaque(const std::shared_ptr<Surface>& image, Integer red, Integer green, Integer blue)
{
    if (!image) return;
    for (std::size_t p = 0; p + 3 < image->rgba.size(); p += 4) {
        if (image->rgba[p + 3] == 0) continue;
        image->rgba[p] = static_cast<std::uint8_t>(red);
        image->rgba[p + 1] = static_cast<std::uint8_t>(green);
        image->rgba[p + 2] = static_cast<std::uint8_t>(blue);
    }
}

void blit_card_name(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& name,
                    Integer x, Integer y, bool white, bool bordering, unsigned outline_alpha = 92,
                    bool dialog_alpha = false)
{
    if (!dest || !name) return;
    auto glyph = clone_surface(name);
    if (white) tint_opaque(glyph, 255, 255, 255);
    if (bordering) {
        auto border = clone_surface(name);
        const Integer shade = white ? 0 : 255;
        tint_opaque(border, shade, shade, shade);
        if (dialog_alpha) {
            // Py の wx の輪郭と同様、mul_wxalpha でアルファだけに倍率を掛け、255
            // で割る。 ゲーム用描画面は、下の pygame の RGB・アルファ乗算を維持する。
            ::cw::lib::simd_image::multiply_alpha(border->rgba.data(), border->rgba.size() / 4U,
                                                  static_cast<std::uint8_t>(outline_alpha));
        } else {
            ::cw::imageretouch::mul_alpha(border, static_cast<Integer>(outline_alpha));
        }
        for (Integer oy = -1; oy <= 1; ++oy)
            for (Integer ox = -1; ox <= 1; ++ox)
                if (ox || oy) blit_surface(dest, border, x + ox, y + oy);
    }
    blit_surface(dest, glyph, x, y);
}

bool bordering_card_name()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->get_bordering_cardname();
}

void blit_premium_marks(const std::shared_ptr<Surface>& image, const Value& table, const String& premium, Real scale)
{
    if (!image) return;
    const auto key = premium == "Rare" ? String("RARE") : premium == "Premium" ? String("PREMIER") : String{};
    if (key.empty()) return;
    auto sub = resource_surface(table, key);
    if (!sub) return;
    const Integer pad = py_scaled(5, scale);  // Python の cw.s(5) に相当する。
    blit_surface(image, sub, image->size.width - sub->size.width - pad, pad);
    blit_surface(image, sub, pad, image->size.height - sub->size.height - pad);
}

std::shared_ptr<Surface> fallback_icon(const String& key, Integer side = 16)
{
    (void)key; (void)side;
    // Py と同様、ResourceTable の素材がなければ空とする
    // 。 架空の色付きアイコンで欠落を隠し、元にない表示を追加しない。
    return ::cw::setting::empty_image();
}

template<class Load>
void blit_card_overlays(const std::shared_ptr<Surface>& image,
                        const std::shared_ptr<::cw::header::CardHeader>& header,
                        const std::shared_ptr<::cw::character::Character>& owner,
                        Real scale, Load load)
{
    if (owner) if (const auto original = header->get_original(); original && original->is_hold())
        blit_surface(image, load("HOLD", false), 0, 0);
    if (header->m_penalty) blit_surface(image, load("PENALTY", false), 0, 0);
    if (owner && owner->is_autoselectedpenalty(header))
        blit_surface(image, load("FIXED", true), scaled_win(20, scale), 0);
}

void fill_surface_rect(const std::shared_ptr<Surface>& image, Integer x, Integer y,
                       Integer width, Integer height, std::tuple<Integer, Integer, Integer> color,
                       std::uint8_t alpha = 255)
{
    if (!image) return;
    const auto [rr, gg, bb] = color;
    const auto r = static_cast<std::uint8_t>(std::clamp<Integer>(rr, 0, 255));
    const auto g = static_cast<std::uint8_t>(std::clamp<Integer>(gg, 0, 255));
    const auto b = static_cast<std::uint8_t>(std::clamp<Integer>(bb, 0, 255));
    for (Integer py = std::max<Integer>(0, y); py < std::min(image->size.height, y + height); ++py)
        for (Integer px = std::max<Integer>(0, x); px < std::min(image->size.width, x + width); ++px) {
            const auto index = static_cast<std::size_t>((py * image->size.width + px) * 4);
            if (index + 3 >= image->rgba.size()) continue;
            image->rgba[index] = r; image->rgba[index + 1] = g; image->rgba[index + 2] = b;
            image->rgba[index + 3] = alpha;
        }
}

std::shared_ptr<Surface> rotate_cw(const std::shared_ptr<Surface>& source)
{
    if (!source) return {};
    auto result = make_surface(source->size.height, source->size.width);
    for (Integer y = 0; y < source->size.height; ++y)
        for (Integer x = 0; x < source->size.width; ++x) {
            const auto si = static_cast<std::size_t>((y * source->size.width + x) * 4);
            const auto dx = source->size.height - 1 - y;
            const auto dy = x;
            const auto di = static_cast<std::size_t>((dy * result->size.width + dx) * 4);
            if (si + 3 < source->rgba.size() && di + 3 < result->rgba.size())
                std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(si), 4,
                            result->rgba.begin() + static_cast<std::ptrdiff_t>(di));
        }
    return result;
}

} // namespace

namespace cw::image {
std::vector<String> split_lines(const String& text)
{
    // Python の str.splitlines() と同様、CRLF は一つの区切りとし、末尾の区切りで空
    // 行を追加しない。 Engine の UTF-8 では Unicode の NEL/LS/PS も認識する。
    std::vector<String> lines;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t boundary = 0;
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\r') boundary = i + 1 < text.size() && text[i + 1] == '\n' ? 2 : 1;
        else if (c == '\n' || c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1e)) boundary = 1;
        else if (c == 0xc2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x85) boundary = 2;
        else if (c == 0xe2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(text[i + 2]) == 0xa8 || static_cast<unsigned char>(text[i + 2]) == 0xa9)) boundary = 3;
        if (boundary) { lines.push_back(text.substr(begin, i - begin)); i += boundary; begin = i; }
        else ++i;
    }
    if (begin < text.size()) lines.push_back(text.substr(begin));
    return lines;
}
} // namespace cw::image

namespace {

std::shared_ptr<Surface> render_text(const String& text, const String& face, Integer size,
                                     const std::tuple<Integer, Integer, Integer>& color,
                                     bool bold, bool italic, bool underline, bool antialias)
{
    auto font = std::make_shared<::cw::imageretouch::Font>();
    font->initialize(face.empty() ? String("ＭＳ Ｐゴシック") : face, -std::max<Integer>(1, std::abs(size)), bold, italic);
    font->set_underline(underline);
    return font->render(text, antialias, color);
}

void draw_outlined(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& text,
                   Integer x, Integer y, const std::optional<std::tuple<Integer, Integer, Integer>>& border)
{
    if (!dest || !text) return;
    if (border) for (Integer oy = -1; oy <= 1; ++oy) for (Integer ox = -1; ox <= 1; ++ox)
        if (ox || oy) blit_surface(dest, text, x + ox, y + oy);
    blit_surface(dest, text, x, y);
}

std::shared_ptr<Surface> status_icon(const Value& statuses, const String& key)
{
    auto image = resource_surface(statuses, key);
    return image ? clone_surface(image) : fallback_icon(key, 16);
}


bool is_trade_area()
{
    if (!::cw::g_cwpy) return false;
    if (!::cw::g_cwpy->is_playingscenario()) return true;
    const auto area = ::cw::g_cwpy->m_areaid;
    return area == std::get<0>(::cw::g_AREAS_TRADE) ||
           area == std::get<1>(::cw::g_AREAS_TRADE) ||
           area == std::get<2>(::cw::g_AREAS_TRADE) ||
           area == value_integer(::cw::g_AREA_CAMP);
}

bool is_special_area()
{
    if (!::cw::g_cwpy) return false;
    const auto area = ::cw::g_cwpy->m_areaid;
    return area == value_integer(std::get<0>(::cw::g_AREAS_SP)) ||
           area == value_integer(std::get<1>(::cw::g_AREAS_SP)) ||
           area == value_integer(std::get<2>(::cw::g_AREAS_SP)) ||
           area == value_integer(std::get<3>(::cw::g_AREAS_SP)) ||
           area == value_integer(std::get<4>(::cw::g_AREAS_SP));
}

Integer blit_uselimit_digits(const std::shared_ptr<Surface>& image, const String& text, bool recycle, Real scale)
{
    if (!image) return 0;
    auto font = rsrc_font("card_uselimit");
    const auto height = std::max<Integer>(8, py_scaled(12, scale));
    if (!font) {
        // フォントリソースがなくても数字を描けるよう、ネイティブの代替を維持する。
        font = std::make_shared<::cw::imageretouch::Font>();
        font->initialize("ＭＳ Ｐゴシック", -height, true, false);
    }
    const auto colour = recycle ? std::tuple<Integer, Integer, Integer>{255, 255, 0}
                                : std::tuple<Integer, Integer, Integer>{255, 255, 255};
    const auto position_x = py_scaled(5, scale);
    const auto position_y = image->size.height - font->get_height() - py_scaled(4, scale);
    const auto step = py_scaled(10, scale);
    Integer advance = 0;
    for (const auto c : text) {
        auto glyph = font->render(String(1, c), true, colour);
        auto shadow = font->render(String(1, c), true, {0, 0, 0});
        if (!glyph) continue;
        for (Integer oy = -1; oy <= 1; ++oy)
            for (Integer ox = -1; ox <= 1; ++ox)
                if (ox || oy) blit_surface(image, shadow, position_x + advance + ox, position_y + oy);
        blit_surface(image, glyph, position_x + advance, position_y);
        advance += step;
    }
    return py_scaled(font->get_height() - 2, scale);
}

Integer blit_wx_uselimit_digits(const std::shared_ptr<Surface>& image, const String& text, bool recycle, Real scale)
{
    if (!image) return 0;
    Integer logical_pixels = 12;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto types = ::cw::g_cwpy->m_setting->get_fonttypes();
        if (const auto found = types.find("uselimit"); found != types.end()) logical_pixels = std::get<2>(found->second);
    }
    const auto pixels = std::get<Integer>(::cw::wins(Value(logical_pixels)).storage());
    const auto style = ::cw::lib::ui::configured_font("uselimit", logical_pixels, false, false, "MS UI Gothic");
    auto font = std::make_shared<::cw::imageretouch::Font>();
    // ネイティブの RGBA 合成では wx.MemoryDC の代わりに共通 Font の描
    // 画境界を使い、 Py と同じ明示的なウィンドウピクセルと設定された書体・装飾を適用する。
    font->initialize(style.face, -pixels, style.bold, style.italic);
    const auto color = recycle ? std::tuple<Integer, Integer, Integer>{255,255,0} : std::tuple<Integer, Integer, Integer>{255,255,255};
    Integer x = py_scaled(5, scale);
    const auto y = image->size.height - pixels - py_scaled(4, scale);
    for (const auto c : text) {
        auto shadow = font->render(String(1, c), true, {0,0,0});
        auto glyph = font->render(String(1, c), true, color);
        for (Integer oy = -1; oy <= 1; ++oy) for (Integer ox = -1; ox <= 1; ++ox)
            if (ox || oy) blit_surface(image, shadow, x + ox, y + oy);
        blit_surface(image, glyph, x, y);
        x += py_scaled(10, scale);
    }
    return py_scaled(logical_pixels - 2, scale);
}

std::tuple<Integer, Integer> bg_size(const String& bgtype)
{
    return ::cw::setting::g_SIZE_RESOURCES.at("CardBg/" + bgtype);
}

bool is_code_path(const String& path) { return ::cw::binary::image::path_is_code(path); }
}

namespace cw::image {
void blit_surface_pixels(const std::shared_ptr<::cw::lib::Surface>& destination,
                         const std::shared_ptr<::cw::lib::Surface>& source,
                         ::cw::lib::Integer x, ::cw::lib::Integer y,
                         std::optional<::cw::lib::Rect> destination_clip)
{
    blit_surface(destination, source, x, y, std::nullopt, destination_clip);
}


std::shared_ptr<::cw::lib::Surface> compose_lifebar(
    const std::shared_ptr<::cw::lib::Surface>& arg_lifebar,
    const std::shared_ptr<::cw::lib::Surface>& arg_guage,
    const std::shared_ptr<::cw::lib::Surface>& arg_lifemask,
    ::cw::lib::Integer arg_barx, ::cw::lib::Integer arg_bary)
{
    if (!arg_guage || arg_guage->size.width <= 0 || arg_guage->size.height <= 0)
        return {};

    // pygame の convert_alpha() と同様、LIFEGUAGE2
    // の合成は完全透明から始める。 従来の convert()
    // 経路は不透明で、転送前に指定した透過色を使う。 両方を
    // アルファ専用画像とみなさず、この形式の違いを維持する。
    const bool masked_frame = static_cast<bool>(arg_lifemask);
    auto result = make_surface(arg_guage->size.width, arg_guage->size.height,
                               0, 0, 0, masked_frame ? 0 : 255);
    result->has_pixel_alpha = masked_frame;
    if (!masked_frame && arg_guage->rgba.size() >= 3) {
        // Py と同様、二回の転送前に透過色付きの RGB convert() 画像を使う
        // 。 保存するアルファは不透明のままとし、共通転送処理が透過色のマスクを適用する。
        const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> key{
            arg_guage->rgba[0], arg_guage->rgba[1], arg_guage->rgba[2]};
        ::cw::imageretouch::set_surface_metadata(result, {
            true, key, false, result->scr_scale});
    }
    blit_surface(result, arg_lifebar, arg_barx, arg_bary);
    blit_surface(result, arg_guage, 0, 0);

    if (arg_lifemask) {
        // Python と同様、先に (255,255,255,0) の BLEND_RGBA_MAX
        // 、その後に BLEND_RGBA_MULT を適用する。 MAX は
        // RGB を 255 にしてマスクのアルファを維持するため、四チャン
        // ネルを明示し、 特殊マスクを枠・透過色の操作へ誤って単純化しない。
        const ::cw::lib::Integer width = std::min(result->size.width, arg_lifemask->size.width);
        const ::cw::lib::Integer height = std::min(result->size.height, arg_lifemask->size.height);
        for (::cw::lib::Integer y = 0; y < height; ++y)
            for (::cw::lib::Integer x = 0; x < width; ++x) {
                const auto target_index = static_cast<std::size_t>((y * result->size.width + x) * 4);
                const auto mask_index = static_cast<std::size_t>((y * arg_lifemask->size.width + x) * 4);
                if (target_index + 3 >= result->rgba.size() || mask_index + 3 >= arg_lifemask->rgba.size()) continue;
                const auto mask_a = arg_lifemask->rgba[mask_index + 3];
                // 共通の pygame 1.9.6 互換 BLEND_RGBA_MULT は、MAX 後に 255
                // を掛ける RGB も含め 8 ビット右シフトを使う。 255 で割ると 1 ずれる。
                ::cw::lib::simd_image::multiply_rgba_alpha(
                    result->rgba.data() + target_index, 1, mask_a);
            }
    }
    return result;
}

void Image::initialize(std::shared_ptr<::cw::lib::Surface> arg_image)
{
    m_image = std::move(arg_image);
}

std::shared_ptr<::cw::lib::Surface> Image::get_image()
{
    return m_image;
}

std::shared_ptr<::cw::lib::Surface> Image::get_negaimg()
{
    // 共通の画像加工ヘルパーで、ネイティブの null 許容 Surface の境
    // 界を維持する。 Python も加工前に仮想の取得メソッドから画像を得る。
    return ::cw::imageretouch::to_negative(get_image());
}

void ImageInfo::initialize(::cw::lib::String arg_path, ::cw::lib::Integer arg_pcnumber, std::optional<std::shared_ptr<::cw::image::ImageInfo>> arg_base, ::cw::lib::String arg_postype, std::optional<::cw::lib::String> arg_basecardtype)
{
    m_path = std::move(arg_path);
    m_pcnumber = arg_pcnumber;
    while (arg_base && *arg_base && (*arg_base)->m_base && *(*arg_base)->m_base)
        arg_base = (*arg_base)->m_base;
    m_base = std::move(arg_base);
    m_postype = m_base && *m_base ? (*m_base)->m_postype : std::move(arg_postype);
    m_basecardtype = std::move(arg_basecardtype);
}

CardImage::~CardImage()
{
    release_animated_paths();
}

std::shared_ptr<::cw::image::ImageInfo> ImageInfo::copy()
{
    auto result = std::make_shared<ImageInfo>();
    result->initialize(m_path, m_pcnumber, std::nullopt, m_postype, m_basecardtype);
    return result;
}

void ImageInfo::set_attr(std::shared_ptr<::cw::data::CWPyElement> arg_e)
{
    if (!arg_e) throw std::invalid_argument("ImageInfo::set_attr requires ImagePath element");
    if (arg_e->m_tag != "ImagePath") throw std::invalid_argument("ImageInfo::set_attr requires ImagePath tag");
    // Py と同様、Default 以外の全文字列を書き込み、明示的な空文字列も含める。
    if (m_postype != "Default") arg_e->set("positiontype", m_postype);
}

::cw::lib::Rect ImageInfo::calc_basecardposition(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_params, bool arg_noscale, std::optional<::cw::lib::String> arg_basecardtype, std::optional<::cw::lib::String> arg_cardpostype)
{
    return _calc_basecardposition_impl(std::get<0>(arg_params), std::get<1>(arg_params),
                                       arg_noscale, std::move(arg_basecardtype), std::move(arg_cardpostype),
                                       ::cw::g_UP_SCR);
}

::cw::lib::Rect ImageInfo::calc_basecardposition_wx(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_imgsize, bool arg_noscale, std::optional<::cw::lib::String> arg_basecardtype, std::optional<::cw::lib::String> arg_cardpostype)
{
    return _calc_basecardposition_impl(std::get<0>(arg_imgsize), std::get<1>(arg_imgsize),
                                       arg_noscale, std::move(arg_basecardtype), std::move(arg_cardpostype),
                                       ::cw::g_UP_WIN);
}

::cw::lib::Rect ImageInfo::_calc_basecardposition_impl(::cw::lib::Integer arg_imgwidth, ::cw::lib::Integer arg_imgheight, bool arg_noscale, std::optional<::cw::lib::String> arg_basecardtype, std::optional<::cw::lib::String> arg_cardpostype, ::cw::lib::Real arg_scale)
{
    if (has_python_string_value(m_basecardtype)) arg_basecardtype = m_basecardtype;
    ::cw::lib::Integer x = 0, y = 0, bx = 0, by = 0, width = arg_imgwidth, height = arg_imgheight;
    if (arg_cardpostype == "LargeCard") { x = 11; y = 18; }
    else if (arg_cardpostype == "NormalCard") { x = 3; y = 13; }
    ::cw::lib::String default_position;
    if (arg_basecardtype == "LargeCard") {
        width = 95; height = 130; bx = 11; by = 18;
        default_position = arg_cardpostype == "NotCard" ? "TopLeft" : "Center";
    } else if (arg_basecardtype == "NormalCard") {
        width = 80; height = 110; bx = 3; by = 13; default_position = "TopLeft";
    } else if (arg_basecardtype == "Bill") {
        width = 400; height = 370; bx = 163; by = 70; default_position = "TopLeft";
    } else return {0, 0, arg_imgwidth, arg_imgheight};
    if (!arg_noscale) {
        const auto scale = (arg_scale > 0.0 && std::isfinite(arg_scale)) ? arg_scale : 1.0;
        // Python の ss() は cw.s()/cw.wins() であり、丸めではなく int() の切り捨てを使う。
        x = py_scaled(x, scale); y = py_scaled(y, scale);
        width = py_scaled(width, scale); height = py_scaled(height, scale);
        bx = py_scaled(bx, scale); by = py_scaled(by, scale);
    }
    const auto position = (m_postype == "TopLeft" || m_postype == "Center") ? m_postype : default_position;
    if (position == "Center") {
        // Pythonの//は負数で負の無限大方向。通常サイズでは非負だが互換ヘルパーを使う。
        auto floor_half = [](auto value) { return value >= 0 ? value / 2 : -((-value + 1) / 2); };
        x = floor_half(width - arg_imgwidth); y = floor_half(height - arg_imgheight);
    } else { x = bx; y = by; }
    if (has_python_string_value(arg_cardpostype)) { x -= bx; y -= by; }
    return {x, y, width, height};
}

bool ImageInfo::equals(::cw::lib::Value arg_other)
{
    auto object = std::get_if<::cw::lib::ObjectPtr>(&arg_other.storage());
    auto other = object ? std::dynamic_pointer_cast<ImageInfo>(*object) : nullptr;
    if (!other) throw std::invalid_argument("ImageInfo comparison requires ImageInfo");
    return m_path == other->m_path && m_pcnumber == other->m_pcnumber && m_postype == other->m_postype;
}

bool ImageInfo::not_equals(::cw::lib::Value arg_other)
{
    return !equals(std::move(arg_other));
}

::cw::lib::String ImageInfo::to_string()
{
    if (m_pcnumber) return "PC: " + std::to_string(m_pcnumber);
    return "File: " + m_path + ", Position Type: " + m_postype;
}

void CardImage::initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, ::cw::lib::String arg_premium, ::cw::lib::Value arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_anotherscenariocard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size)
{
    m_paths_typed = std::move(arg_paths); m_bgtype_typed = std::move(arg_bgtype); m_name_typed = std::move(arg_name); m_premium_typed = std::move(arg_premium);
    m_is_scenariocard_typed = arg_is_scenariocard; m_scedir_typed = std::move(arg_scedir); m_anotherscenariocard_typed = arg_anotherscenariocard; m_card_size_typed = arg_card_size;
    m_can_loaded_scaledimage_typed.assign(m_paths_typed.size(), false);
    if (const auto* b = std::get_if<bool>(&arg_can_loaded_scaledimage.storage())) std::fill(m_can_loaded_scaledimage_typed.begin(), m_can_loaded_scaledimage_typed.end(), *b);
    else if (const auto list = object_value<::cw::data::VariantSequence>(arg_can_loaded_scaledimage)) for (std::size_t i = 0; i < list->m_values.size() && i < m_can_loaded_scaledimage_typed.size(); ++i) m_can_loaded_scaledimage_typed[i] = value_integer(list->m_values[i]) != 0;
    m_paths = Value(std::make_shared<::cw::data::VariantSequence>());
    if (const auto seq = object_value<::cw::data::VariantSequence>(m_paths)) for (const auto& info : m_paths_typed) seq->m_values.emplace_back(info ? std::static_pointer_cast<::cw::lib::Object>(info) : nullptr);
    m_bgtype = Value(m_bgtype_typed); m_name = Value(m_name_typed); m_premium = Value(m_premium_typed); m_scedir = Value(m_scedir_typed); m_card_size = Value(std::make_shared<::cw::data::VariantSequence>());
    m_is_scenariocard = Value(arg_is_scenariocard); m_anotherscenariocard = Value(arg_anotherscenariocard); m_can_loaded_scaledimage = arg_can_loaded_scaledimage;
    m_use_excache = false;
    m_paths_upd.reset();
    m_can_loaded_scaledimage_upd.reset();
    m_animated_frame_ids.clear(); m_animated_instance_ids.clear(); m__animated_paths.clear(); m_animated_paths_initialized = false; m__animated_owner = "CardImage:" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    update_scale(); update_animated_paths();
}

void CardImage::update_scale()
{
    m__bmp.reset(); m__wxbmp.reset(); m_image_mtime.clear(); m__upwin = _upwinmemo();
    const auto [w0, h0] = bg_size(m_bgtype_typed); auto [w, h] = m_card_size_typed;
    auto resource = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? resource_surface(::cw::g_cwpy->m_rsrc->m_cardbgs, m_bgtype_typed)
        : std::shared_ptr<Surface>{};
    const auto target_width = w > 0
        ? std::get<Integer>(::cw::s(Value(w)).storage())
        : (resource ? resource->size.width : std::get<Integer>(::cw::s(Value(w0)).storage()));
    const auto target_height = h > 0
        ? std::get<Integer>(::cw::s(Value(h)).storage())
        : (resource ? resource->size.height : std::get<Integer>(::cw::s(Value(h0)).storage()));
    m_cardbg_typed = resource ? clone_surface(resource) : make_surface(target_width, target_height, 0, 0, 0, 255);
    if (m_cardbg_typed && (m_cardbg_typed->size.width != target_width || m_cardbg_typed->size.height != target_height))
        m_cardbg_typed = scale_surface(m_cardbg_typed, target_width, target_height);
    m_rect_typed = {0, 0, target_width, target_height};
    m_rect = Value(std::make_shared<::cw::lib::Surface>(*m_cardbg_typed));
}

void CardImage::set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height)
{
    auto [w, h] = m_card_size_typed; const auto nw = arg_width > 0 ? arg_width : w; const auto nh = arg_height > 0 ? arg_height : h; if (std::tie(nw, nh) != m_card_size_typed) { m_card_size_typed = {nw, nh}; update_scale(); }
}

void CardImage::set_menu_card_text(const bool arg_enabled)
{
    if (m_menu_card_text_typed == arg_enabled) return;
    m_menu_card_text_typed = arg_enabled;
    clear_cache();
}

std::shared_ptr<::cw::imageretouch::Font> CardImage::card_text_font(
    const String& role, const Real output_scale) const
{
    const bool level = role == "level";
    const auto style = ::cw::lib::ui::configured_game_font(role, level ? 33 : 12,
        output_scale, !level, level, level ? "ＭＳ 明朝" : "MS UI Gothic");
    auto font = std::make_shared<::cw::imageretouch::Font>();
    font->initialize(style.face, -style.pixels, style.bold, style.italic);
    return font;
}

bool CardImage::smooth_card_name() const
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
           ::cw::g_cwpy->m_setting->get_fontsmoothing_cardname();
}

bool CardImage::border_card_name() const { return bordering_card_name(); }

void CardImage::set_name(::cw::lib::String name)
{
    m_name_typed = std::move(name);
    m_name = Value(m_name_typed);
    clear_cache();
}

void CardImage::clear_cache()
{
    m__bmp.reset(); m__wxbmp.reset(); m_image_mtime.clear(); m_animated_frame_ids.clear();
    update_animated_paths();
}

std::tuple<::cw::lib::String, bool> CardImage::_resolve_imagepath(std::shared_ptr<::cw::image::ImageInfo> arg_info)
{
    if (!arg_info) return {::cw::lib::String{}, false};
    auto path = arg_info->m_path;
    const auto pisc = is_code_path(path);
    // EnemyCard/FriendCard は Python と同様、CharacterCardImage
    // を作る前に get_materialpath() の結果を ImageInfo
    // に保持する。 既存のパスを再びシナリオディレクトリー基準で解決し、Scenario/.../Scenario/.../file
    // のように重複させない。
    if (!pisc && m_is_scenariocard_typed && !m_anotherscenariocard_typed && !arg_info->m_pcnumber) {
        // 宿と PC の素材は get_yadofilepath を通し
        // 、 保存済みのファイルがあっても一時保存された編集を優先する。
        if (image_file_exists(std::filesystem::u8path(path)))
            return {path, false};
    }
    if (!pisc && (m_anotherscenariocard_typed || !m_is_scenariocard_typed || arg_info->m_pcnumber)) path = ::cw::util::get_yadofilepath(path);
    if ((path.empty() || (m_is_scenariocard_typed && !m_anotherscenariocard_typed)) && !arg_info->m_pcnumber) path = ::cw::util::get_materialpath(arg_info->m_path, ::cw::g_M_IMG, m_scedir_typed, !m_is_scenariocard_typed, true);
    return {path, pisc};
}

std::shared_ptr<::cw::lib::Surface> CardImage::_load_pygame_image(::cw::lib::String arg_path, bool arg_mask, bool arg_can_loaded_scaledimage)
{
    if (!::cw::binary::image::path_is_code(arg_path) && ::cw::animatedimage::is_supported_path(arg_path)) {
        const auto instance = m_animated_instance_ids.find(arg_path);
        const auto instance_id = instance == m_animated_instance_ids.end()
            ? std::optional<Integer>{} : std::optional<Integer>{instance->second};
        try {
            auto [animated, frame_id] = ::cw::animatedimage::get_surface(arg_path, instance_id);
            if (animated && *animated) {
                m_animated_frame_ids[arg_path] = frame_id;
                return *animated;
            }
        } catch (const ::cw::lib::imagecodec::Error&) {
            // ネイティブのワーカーでデコードに失敗した場合も、 Py のアニ
            // メーションフレームがない場合と同じ先頭フレームの代替を使う。
        }
        try {
            if (image_file_exists(std::filesystem::u8path(arg_path))) {
                const auto data = read_image_data(arg_path, false);
                if (data) {
                    auto decoded = decode_image_data(*data, false); // Pillow と同様、透過色を持たない RGBA へ変換する。
                    m_animated_frame_ids[arg_path] = 0;
                    decoded.surface->has_pixel_alpha = true; // Pillow.convert("RGBA") + convert_alpha().
                    decoded.surface->pixel_bits = 32;
                    return decoded.surface;
                }
            }
        } catch (const std::exception&) {
            // Py と同様、Pillow の先頭フレーム取得の失敗を捕捉し、util.load_image で再試行する。
        }
    }
    // 入力ピクセルと入力倍率の情報を返す。 公開の Py ロー
    // ダーと同様、各合成処理が cw.s を一度だけ適用する。
    return ::cw::util::load_image(arg_path, arg_mask, {0, 0}, std::nullopt,
        true, false, arg_can_loaded_scaledimage, false, std::nullopt, m_use_excache);
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::_load_wxbmp_image(::cw::lib::String arg_path, bool arg_mask, bool arg_can_loaded_scaledimage)
{
    if (!::cw::binary::image::path_is_code(arg_path) && ::cw::animatedimage::is_supported_path(arg_path)) {
        const auto instance = m_animated_instance_ids.find(arg_path);
        const auto instance_id = instance == m_animated_instance_ids.end()
            ? std::optional<Integer>{} : std::optional<Integer>{instance->second};
        try {
            auto [animated, frame_id] = ::cw::animatedimage::get_wxbmp(arg_path, instance_id);
            if (animated && *animated) {
                m_animated_frame_ids[arg_path] = frame_id;
                (*animated)->scr_scale = 1.0;
                (*animated)->logical_scale = 1.0;
                return *animated;
            }
        } catch (const std::exception&) {
            // ネイティブワーカーの失敗時は、Py のアニメーション画像
            // がない場合と同じ通常の wx 読み込み処理を代替に使う。
        }
    }
    return ::cw::util::load_wxbmp(arg_path, arg_mask, nullptr, Value{},
        std::nullopt, true, arg_can_loaded_scaledimage, false, std::nullopt);
}

std::set<::cw::lib::String> CardImage::get_animated_paths()
{
    std::set<String> result;
    for (const auto& info : m_paths_typed) {
        if (!info) continue;
        auto path = info->m_path;
        auto code = ::cw::binary::image::path_is_code(path);
        if (!code && !image_file_exists(std::filesystem::u8path(path)))
            std::tie(path, code) = _resolve_imagepath(info);
        if (!code && image_file_exists(std::filesystem::u8path(path)) &&
            ::cw::animatedimage::is_supported_path(path))
            result.insert(path);
    }
    return result;
}

void CardImage::update_animated_paths()
{
    const auto paths = get_animated_paths();
    for (const auto& path : m__animated_paths) {
        if (!paths.contains(path)) {
            ::cw::animatedimage::release(m__animated_owner, path);
            m_animated_instance_ids.erase(path);
            // Py と同様、現在のパス集合から入力元が消えても、clear_cache
            // までは最後に取得したフレームを保持する。
        }
    }
    for (const auto& path : paths) {
        if (const auto found = m_animated_instance_ids.find(path);
            found != m_animated_instance_ids.end()) {
            // この CardImage の確認が一時的に止まっている間、子モー
            // ダルやワーカーの待機時の整理で共通インスタンスが解放される場合が
            // ある。 整数の ID があるだけで、生存しているとは判断しない。
            if (::cw::animatedimage::has_instance(path, found->second)) continue;
            m_animated_instance_ids.erase(found);
            m_animated_frame_ids[path] = 0;
        }
        if (const auto instance = ::cw::animatedimage::acquire(path, m__animated_owner))
            m_animated_instance_ids[path] = *instance;
    }
    m__animated_paths = paths;
    m_animated_paths_initialized = true;
}

void CardImage::release_animated_paths()
{
    ::cw::animatedimage::release(m__animated_owner);
    m__animated_paths.clear(); m_animated_instance_ids.clear();
    // Python と同様、登録は解除するが最後に取得したフレームは保持する。 その履歴は
    // clear_cache() と initialize() で明示的にリセットする。
    m_animated_paths_initialized = false;
}

void CardImage::py_del()
{
    release_animated_paths();
}

bool CardImage::update_animation()
{
    // 以前なかったアニメーションが追加された場合に備え、現在のパスを再確認する。
    bool reacquire = !m_animated_paths_initialized || get_animated_paths() != m__animated_paths;
    if (!reacquire) {
        for (const auto& [path, instance_id] : m_animated_instance_ids) {
            if (!::cw::animatedimage::has_instance(path, instance_id)) {
                reacquire = true;
                break;
            }
        }
    }
    if (reacquire) update_animated_paths();
    bool updated = false;
    for (const auto& path : m__animated_paths) {
        const auto instance = m_animated_instance_ids.find(path);
        const auto instance_id = instance == m_animated_instance_ids.end()
            ? std::optional<Integer>{} : std::optional<Integer>{instance->second};
        const auto frame_id = ::cw::animatedimage::get_frame_id(path, instance_id);
        if (frame_id && m_animated_frame_ids[path] != frame_id) {
            m_animated_frame_ids[path] = frame_id;
            updated = true;
        }
    }
    if (updated) {
        m__bmp.reset();
        m__wxbmp.reset();
    }
    return updated;
}

// Py は保留中のパスと拡縮許可が対であることをアサートし、通常のネイ
// ティブのスプライト生成側も両方を渡す。 この型付き互換ブリッジでは
// 開発用アサートを省き、パスだけの直接要求では許可を欠落扱いにせず以
// 前の値を維持する。 その不完全な状態は通常の生成側の出力ではない。
void CardImage::fix_pcimage_updated()
{
    if (!m_paths_upd) return;
    m_paths_typed = std::move(*m_paths_upd);
    if (m_can_loaded_scaledimage_upd) m_can_loaded_scaledimage_typed = std::move(*m_can_loaded_scaledimage_upd);
    auto paths = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& info : m_paths_typed)
        paths->m_values.emplace_back(info ? std::static_pointer_cast<::cw::lib::Object>(info) : nullptr);
    m_paths = Value(paths);
    auto scaled = std::make_shared<::cw::data::VariantSequence>();
    for (bool flag : m_can_loaded_scaledimage_typed) scaled->m_values.emplace_back(flag);
    m_can_loaded_scaledimage = Value(scaled);
    m_paths_upd.reset();
    m_can_loaded_scaledimage_upd.reset();
    clear_cache();
}

::cw::lib::Value CardImage::_upwinmemo()
{
    const auto setting = ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
    const auto font=card_text_font(m_bgtype_typed=="LARGE"?"ccardname":"cardname",
        std::max<Real>(1.0,::cw::g_UP_WIN));
    const auto face=font?value_string(font->m_face):String{};
    // 使用回数と基本フォント表全体も含め、Py の全キャッシュ依存を維
    // 持し、ネイティブのピクセル依存も加える。 ネイティブではシリアラ
    // イズしたキーを使い、長さ付き文字列で曖昧さを避け、max_digits10
    // で異なる Python の浮動小数点倍率を区別する。
    std::ostringstream key;
    key << std::setprecision(std::numeric_limits<Real>::max_digits10)
        << ::cw::g_UP_WIN << ',' << ::cw::g_UP_SCR << ','
        << smooth_card_name() << ',' << border_card_name() << ','
        << (setting && setting->m_show_premiumicon) << ',';
    const auto append_string = [&](const String& value) {
        key << value.size() << ':' << value << ';';
    };
    if (setting) {
        const auto basefonts = setting->get_basefont();
        key << basefonts.size() << ';';
        for (const auto& [name, value] : basefonts) {
            append_string(name); append_string(value);
        }
        const auto fonttypes = setting->get_fonttypes();
        for (const auto* name : {"cardname", "uselimit"}) {
            const auto found = fonttypes.find(name);
            key << (found != fonttypes.end()) << ';';
            if (found != fonttypes.end()) {
                const auto& [family, type, pixels, bold, italic, underline] = found->second;
                append_string(family); append_string(type);
                key << pixels << ',' << bold << ',' << italic << ',' << underline << ';';
            }
        }
    } else {
        key << "no-setting;";
    }
    append_string(face);
    key << (font?value_integer(font->m_pixels):0) << ','
        << (font && font->get_bold()) << ',' << (font && font->get_italic());
    return Value(key.str());
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxcardbg()
{
    const auto resource = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? resource_surface(::cw::g_cwpy->m_rsrc->m_wxcardbgs, m_bgtype_typed) : nullptr;
    if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(resource)) return bitmap;
    if (!resource) return {};
    // ネイティブの初期・既定の表は、wx リソース読み込み前に Surface
    // を持つ場合がある。 その追加の型境界だけを変換し、通常の Bitmap
    // 項目は直接返して Python の共有リソースの同一性を維持する。
    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    static_cast<Surface&>(*bitmap) = *resource;
    return bitmap;
}

std::shared_ptr<::cw::lib::Object> CardImage::get_wxrect()
{
    const auto [width, height] = bg_size(m_bgtype_typed);
    return std::make_shared<::cw::RectValue>(::cw::lib::Rect{0, 0,
        std::get<Integer>(::cw::wins(Value(width)).storage()),
        std::get<Integer>(::cw::wins(Value(height)).storage())});
}

bool CardImage::is_modifiedfile()
{
    // Py はここで全体の FileSync キューを必ず完了する。
    // ネイティブでは入力一覧が空か、解決済みの全項目がコードパスの場
    // 合だけ省く。 それらには更新する外部画像がないためである。空・
    // null も含めコード以外の項目があれば、全体の同期を行う。
    // is_waiting は現在 true を返し、sync は全
    // キューを処理するため、入力元ごとの保留書き込み選別ではない。
    if (::cw::g_fsync) {
        for (const auto& info : m_paths_typed) {
            const auto [path, code] = _resolve_imagepath(info);
            if (!code && ::cw::g_fsync->is_waiting(path)) {
                ::cw::g_fsync->sync();
                break;
            }
        }
    }
    for (const auto& info : m_paths_typed) {
        const auto [path, code] = _resolve_imagepath(info);
        if (code) continue;
        const auto stamp_value = image_file_stamp(std::filesystem::u8path(path));
        if (!stamp_value) continue;
        const auto stamp = *stamp_value;
        const auto found = m_image_mtime.find(path);
        const auto previous_stamp = found == m_image_mtime.end() ? 0.0 : found->second;
        if (previous_stamp != stamp) return true;
    }
    return false;
}

std::shared_ptr<::cw::lib::Surface> CardImage::get_image()
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto image_started = std::chrono::steady_clock::now();
    const auto finish = [&](std::shared_ptr<::cw::lib::Surface> image) {
        ::cardwirth::debug::log_slow(
            "card.get_image", std::chrono::steady_clock::now() - image_started,
            "name=" + m_name_typed + " paths=" + std::to_string(m_paths_typed.size()) +
            " result=" + (image ? std::to_string(image->size.width) + "x" + std::to_string(image->size.height) : "none"));
        return image;
    };
#endif
    const bool cache_composite = dynamic_cast<LargeCardImage*>(this) == nullptr;
    if (cache_composite && m__bmp && !is_modifiedfile()) {
#if defined(CARDWIRTH_LOG_ENABLED)
        return finish(clone_surface(*m__bmp));
#else
        return clone_surface(*m__bmp);
#endif
    }
    auto image = clone_surface(m_cardbg_typed);
    if (!image) {
#if defined(CARDWIRTH_LOG_ENABLED)
        return finish({});
#else
        return {};
#endif
    }
    const bool large_card = m_bgtype_typed == "LARGE";
    const auto scale = ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
    const auto cardbgs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_cardbgs : Value{};
    const bool draw_premium = large_card || !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
                              !::cw::g_cwpy->m_setting->m_show_premiumicon;
    if (draw_premium) blit_premium_marks(image, cardbgs, m_premium_typed, scale);
    if (cache_composite) m_image_mtime.clear();
    for (std::size_t i = 0; i < m_paths_typed.size(); ++i) {
        const auto& info = m_paths_typed[i]; const auto [path, code] = _resolve_imagepath(info);
        if (!code) {
            const auto stamp = image_file_stamp(std::filesystem::u8path(path));
            if (!stamp) continue;
            m_image_mtime[path] = *stamp;
        }
        const auto allow = i < m_can_loaded_scaledimage_typed.size() && m_can_loaded_scaledimage_typed[i]; auto sub = scale_display(_load_pygame_image(path, true, allow), ::cw::g_UP_SCR); if (!sub) continue;
        const auto basecardtype = large_card ? String("LargeCard") : String("NormalCard");
        const auto base = info ? info->calc_basecardposition({sub->size.width, sub->size.height}, false,
                                                             basecardtype, basecardtype)
                               : ::cw::lib::Rect{};
        // 公式 release_5.3 の配置を維持する。独自の Python
        // 派生版とは横中央の扱いが異なるが、この配置を意図的に使う。
        // 通常カードは (cw.s(3) + baserect.x,
        // cw.s(13) + baserect.y)、 大型カー
        // ドは (cw.s(11) + baserect.x, cw.s(18)
        // + baserect.y) とする。 baserect
        // は既に TopLeft/Center を反映するため
        // 、常に横中央へ置き直さない。 置き直すと通常の既定 TopLeft
        // を無視し、74px 以外の画像がずれる。
        const auto left = py_scaled(large_card ? 11 : 3, scale) + base.x;
        const auto top = py_scaled(large_card ? 18 : 13, scale) + base.y;
        ::cw::imageretouch::blit_2bitbmp_to_card(image, sub, {left, top});
    }
    if (!m_name_typed.empty()) {
        const bool white = !large_card && card_name_is_white(m_bgtype_typed);
        const bool smooth = smooth_card_name();
        const auto font = card_text_font(large_card ? "ccardname" : "cardname", scale);
        auto name_image = font->render(m_name_typed, smooth, white
            ? std::tuple<Integer, Integer, Integer>{255, 255, 255}
            : std::tuple<Integer, Integer, Integer>{0, 0, 0});
        // Python の Resource.create_font と同様、フォン
        // トは最終のピクセル寸法を持つため、プラットフォーム固有の追加拡大は行わな
        // い。 左余白は cw.s(5)、幅上限は通常カードで rect.w -
        // left*2、大型カードで rect.w - cw.s(10) とする。
        const Integer left = py_scaled(5, scale);
        const Integer max_width = std::max<Integer>(1, image->size.width -
            (large_card ? py_scaled(10, scale) : left * 2));
        if (name_image && name_image->size.width > max_width)
            name_image = ::cw::image::smoothscale(name_image, {max_width, name_image->size.height}, smooth, false);
        if (name_image) {

            const Integer name_top = py_scaled(5, scale);  // Python の cw.s(5) に相当する。
            blit_card_name(image, name_image, left, name_top, white, bordering_card_name());
        }
    }
    if (cache_composite) m__bmp = clone_surface(image);
#if defined(CARDWIRTH_LOG_ENABLED)
    return finish(image);
#else
    return image;
#endif
}

std::shared_ptr<::cw::lib::Surface> CardImage::get_cardimg(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    auto image = arg_header && arg_header->m_negaflag ? get_negaimg() : get_image();
    if (!image || !arg_header) return image;

    auto owner_value = arg_header->get_owner();
    auto owner = owner_value ? object_value<::cw::character::Character>(*owner_value) : nullptr;
    auto aptitude = owner;
    if (const auto owner = arg_header->m_personal_owner.lock()) aptitude = owner;
    const auto stones = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_stones : Value{};
    const auto dialogs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_pygamedialogs : Value{};
    const auto cardbgs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_cardbgs : Value{};
    const auto scale = ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
    Integer icony = py_scaled(90, scale);
    Integer uselimith = 0;
    auto blit_icon = [&image](const std::shared_ptr<Surface>& icon, Integer x, Integer y) {
        if (icon) blit_surface(image, icon, x, y);
    };

    if (aptitude) {
        const auto level = arg_header->get_showed_vocation_level(aptitude);
        auto icon = resource_surface(stones, "HAND" + std::to_string(level));
        if (!icon) icon = fallback_icon("HAND" + std::to_string(level));
        blit_icon(icon, py_scaled(60, scale), icony);
        icony -= py_scaled(15, scale);
    }
    if (owner) {
        if (arg_header->m_type == "SkillCard") {
            const auto level = arg_header->get_uselimit_level() + 5;
            auto icon = resource_surface(stones, "HAND" + std::to_string(level));
            if (!icon) icon = fallback_icon("HAND" + std::to_string(level));
            blit_icon(icon, py_scaled(60, scale), icony);
            icony -= py_scaled(15, scale);
        }
    }
    blit_card_overlays(image, arg_header, owner, scale, [&](const String& key, bool dialog) {
        auto icon = resource_surface(dialog ? dialogs : cardbgs, key);
        return icon ? icon : fallback_icon(key);
    });

    if (arg_header->m_type == "ItemCard" || arg_header->m_type == "BeastCard") {
        const auto [uselimit, maxuselimit] = arg_header->get_uselimit(false);
        if (uselimit || arg_header->m_recycle || (arg_header->m_type == "BeastCard" && maxuselimit)) {
            uselimith = blit_uselimit_digits(image, std::to_string(uselimit), arg_header->m_recycle, scale);
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_cardkind && is_trade_area()) {
        const bool owner_character = owner != nullptr;
        const bool selected = ::cw::g_cwpy->m_selectedheader && *::cw::g_cwpy->m_selectedheader == arg_header;
        if (!owner_character || selected) {
            const String key = arg_header->m_type == "SkillCard" ? "STATUS8" :
                               arg_header->m_type == "ItemCard" ? "STATUS9" :
                               arg_header->m_type == "BeastCard" ? "STATUS10" : String{};
            if (!key.empty()) {
                auto icon = resource_surface(dialogs, key);
                if (!icon) icon = fallback_icon(key);
                blit_icon(icon, py_scaled(60, scale), icony);
            }
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_premiumicon) {
        const String key = arg_header->m_premium == "Premium" ? "PREMIER_ICON" :
                           arg_header->m_premium == "Rare" ? "RARE_ICON" : String{};
        if (!key.empty()) {
            auto icon = resource_surface(dialogs, key);
            if (!icon) icon = fallback_icon(key);
            blit_icon(icon, py_scaled(5, scale),
                      py_scaled(90, scale) - uselimith);
        }
    }
    return image;
}

std::shared_ptr<::cw::lib::Surface> CardImage::get_negaimg()
{
    // Python の to_negative_for_card と同様、
    // カード専用の加工経路を通す。 画像全体の汎用反転には置き換えない。
    return ::cw::imageretouch::to_negative_for_card(get_image(), 0);
}

std::shared_ptr<::cw::lib::Surface> CardImage::get_clickedimg(std::optional<std::shared_ptr<::cw::lib::Object>> arg_rect, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_image)
{
    auto rect = m_rect_typed;
    if (arg_rect && *arg_rect) {
        if (const auto typed = std::dynamic_pointer_cast<::cw::RectValue>(*arg_rect)) rect = typed->rect;
    }
    const auto image = arg_image && *arg_image ? *arg_image : get_negaimg();
    if (!image) return {};
    return scale_surface(image, std::max<Integer>(0, rect.width * 9 / 10),
                         std::max<Integer>(0, rect.height * 9 / 10));
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxbmp()
{
    const bool cache_composite = dynamic_cast<LargeCardImage*>(this) == nullptr;
    const auto memo = _upwinmemo();
    if (cache_composite && m__wxbmp && value_string(m__upwin) == value_string(memo) && !is_modifiedfile()) {
        auto bitmap = std::make_shared<::cw::lib::Bitmap>();
        static_cast<::cw::lib::Surface&>(*bitmap) = static_cast<const ::cw::lib::Surface&>(*m__wxbmp.value());
        return bitmap;
    }
    m__upwin = memo;

    const Real win_scale = std::max(::cw::g_UP_WIN > 0.0 ? ::cw::g_UP_WIN : 1.0, 1.0);
    const auto [w0, h0] = bg_size(m_bgtype_typed);
    const Integer target_w = static_cast<Integer>(std::lround(static_cast<Real>(w0) * win_scale));
    const Integer target_h = static_cast<Integer>(std::lround(static_cast<Real>(h0) * win_scale));

    const auto wxcardbgs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_wxcardbgs : Value{};
    auto cardbg = resource_surface(wxcardbgs, m_bgtype_typed);
    std::shared_ptr<Surface> image;
    if (cardbg) {
        image = clone_surface(cardbg);
        if (image->size.width != target_w || image->size.height != target_h)
            image = scale_surface(image, target_w, target_h);
    } else {
        image = make_surface(target_w, target_h, 0, 0, 0, 255);
    }
    if (!image) return {};

    const bool large_card = m_bgtype_typed == "LARGE";
    const bool draw_premium = large_card || !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
                              !::cw::g_cwpy->m_setting->m_show_premiumicon;
    if (draw_premium) blit_premium_marks(image, wxcardbgs, m_premium_typed, win_scale);

    for (std::size_t i = 0; i < m_paths_typed.size(); ++i) {
        const auto& info = m_paths_typed[i];
        const auto [path, code] = _resolve_imagepath(info);
        if (!code) {
            const auto stamp = image_file_stamp(std::filesystem::u8path(path));
            if (!stamp) continue;
            m_image_mtime[path] = *stamp;
        }
        const auto allow = i < m_can_loaded_scaledimage_typed.size() && m_can_loaded_scaledimage_typed[i];
        std::shared_ptr<::cw::lib::Bitmap> sub_bmp;
        const bool animated = !code && ::cw::animatedimage::is_supported_path(path);
        const auto sdata = ::cw::g_cwpy ? object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata) : nullptr;
        if (!animated && cache_composite && sdata) {
            const auto md5 = code ? ::cw::util::get_md5_from_data(::cw::binary::image::code_to_data(path)) : String{};
            const auto cachepath = code ? String{} : path;
            // Python はタプルキーを使うが、ValueMap は文字列キーなので、
            // 長さでパス・ハッシュを区切って曖昧さを避ける。 元の表記を維持し、max_digits10
            // で異なる Python の浮動小数点倍率キーを区別する。
            std::ostringstream scale_key;
            scale_key << std::setprecision(std::numeric_limits<Real>::max_digits10) << ::cw::g_UP_WIN;
            const auto key = String("card.wx.source:") + std::to_string(cachepath.size()) + ":" + cachepath + ":" +
                std::to_string(md5.size()) + ":" + md5 + ":" + scale_key.str() + ":" + (allow ? "1" : "0");
            if (const auto found = sdata->m_resource_cache.find(key); found != sdata->m_resource_cache.end())
                sub_bmp = object_value<::cw::lib::Bitmap>(found->second);
            if (!sub_bmp) {
                sub_bmp = _load_wxbmp_image(path, true, allow);
                sdata->sweep_resourcecache(::cw::util::calc_wxbmpsize(sub_bmp));
                sdata->m_resource_cache[key] = Value(std::static_pointer_cast<::cw::lib::Object>(sub_bmp));
            }
        } else sub_bmp = _load_wxbmp_image(path, true, allow);
        if (!sub_bmp || sub_bmp->size.width <= 0 || sub_bmp->size.height <= 0) continue;
        auto sub = object_value<Surface>(::cw::wins(Value(std::static_pointer_cast<::cw::lib::Object>(sub_bmp))));
        const auto basecardtype = large_card ? String("LargeCard") : String("NormalCard");
        const auto base = info ? info->calc_basecardposition_wx({sub->size.width, sub->size.height}, false,
                                                                basecardtype, basecardtype)
                               : ::cw::lib::Rect{};
        const auto left = scaled_win(large_card ? 11 : 3, win_scale) + base.x;
        const auto top = scaled_win(large_card ? 18 : 13, win_scale) + base.y;
        ::cw::imageretouch::blit_2bitbmp_to_card(image, sub, {left, top});
    }

    if (!m_name_typed.empty()) {
        const bool white = !large_card && card_name_is_white(m_bgtype_typed);
        const auto font=card_text_font(large_card ? "ccardname" : "cardname",win_scale);
        // Python の get_wxfont(..., adjustsizewx3=False)
        // と同様、1px の減算補正はしない。 全 OS で同じフォントサービスを使い、cw/image.py
        // と同様、ダイアログとゲームカードで描画品質の方針だけを分ける。
        const Integer left=std::max<Integer>(1,scaled_win(5,win_scale));
        const Integer padding=scaled_win(large_card ? 6 : 5,win_scale);
        const auto name_image=::cw::lib::text_raster::dialog_card_name(
            m_name_typed,font,smooth_card_name(),white,image->size.width,padding);
        if(name_image)blit_card_name(image,name_image,left,scaled_win(5,win_scale),
            white,border_card_name(),128,true);
    }

    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    static_cast<Surface&>(*bitmap) = *image;
    if (cache_composite) m__wxbmp = bitmap;
    return bitmap;
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_cardwxbmp(::cw::lib::Value arg_header, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude)
{
    const auto header = object_value<::cw::header::CardHeader>(arg_header);
    std::shared_ptr<Surface> image;
    if (header && header->m_negaflag) {
        if (auto neg = get_wxnegabmp()) image = std::make_shared<Surface>(static_cast<const Surface&>(*neg));
    } else if (auto wx = get_wxbmp()) {
        image = std::make_shared<Surface>(static_cast<const Surface&>(*wx));
    }
    if (!image || !header) {
        auto bitmap = std::make_shared<::cw::lib::Bitmap>();
        if (image) static_cast<Surface&>(*bitmap) = *image;
        return bitmap;
    }

    const Real win_scale = std::max(::cw::g_UP_WIN > 0.0 ? ::cw::g_UP_WIN : 1.0, 1.0);
    const auto stones = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_wxstones : Value{};
    const auto wxcardbgs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_wxcardbgs : Value{};
    const auto dialogs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_dialogs : Value{};
    const auto wx_resource = [](const Value& table, const String& key) {
        // wx 用カードを合成する前に、ResourceTable がゲーム倍
        // 率の .x2/.x4 を選んでいる場合がある。 カード自体は wins()
        // 座標なので、全重ね合わせ画像を転送前に UP_WIN へ一度だけ
        // 正規化する。 そうしないと UP_SCR=2、UP_WIN=1 で
        // 、本来 14×14 の HAND が 28×28 のままになる。
        return ::cw::lib::ui::resource_surface_for_window(
            resource_surface(table, key));
    };
    auto aptitude = arg_test_aptitude ? std::dynamic_pointer_cast<::cw::character::Character>(*arg_test_aptitude) : nullptr;
    auto owner_value = header->get_owner();
    auto owner = owner_value ? object_value<::cw::character::Character>(*owner_value) : nullptr;
    if (!aptitude) {
        const auto personal_owner=header->m_personal_owner.lock();
        aptitude=personal_owner?personal_owner:owner;
    }

    Integer icony = scaled_win(90, win_scale);
    if (aptitude) {
        const auto level = header->get_showed_vocation_level(aptitude);
        if (auto icon = wx_resource(stones, "HAND" + std::to_string(level))) {
            blit_surface(image, icon, scaled_win(60, win_scale), icony);
            icony -= scaled_win(15, win_scale);
        }
    }
    if (owner) {
        if (header->m_type == "SkillCard") {
            const auto level = header->get_uselimit_level() + 5;
            if (auto icon = wx_resource(stones, "HAND" + std::to_string(level)))
                blit_surface(image, icon, scaled_win(60, win_scale), scaled_win(75, win_scale));
            icony -= scaled_win(15, win_scale);
        }
    }
    blit_card_overlays(image, header, owner, win_scale, [&](const String& key, bool dialog) {
        return wx_resource(dialog ? dialogs : wxcardbgs, key);
    });

    Integer uselimith = 0;
    if (header->m_type == "ItemCard" || header->m_type == "BeastCard") {
        const auto [uselimit, maxuselimit] = header->get_uselimit(false);
        if (uselimit || header->m_recycle || (header->m_type == "BeastCard" && maxuselimit)) {
            uselimith = blit_wx_uselimit_digits(image, std::to_string(uselimit), header->m_recycle, win_scale);
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_cardkind && is_trade_area()) {
        const bool owner_character = owner != nullptr;
        const bool selected = ::cw::g_cwpy->m_selectedheader && *::cw::g_cwpy->m_selectedheader == header;
        if (!owner_character || selected) {
            const String key = header->m_type == "SkillCard" ? "STATUS8" :
                               header->m_type == "ItemCard" ? "STATUS9" :
                               header->m_type == "BeastCard" ? "STATUS10" : String{};
            if (!key.empty()) {
                auto icon = wx_resource(dialogs, key);
                if (!icon) icon = fallback_icon(key);
                if (icon) blit_surface(image, icon, scaled_win(60, win_scale), icony);
            }
        }
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_premiumicon) {
        const String key = header->m_premium == "Premium" ? "PREMIER_ICON" :
                           header->m_premium == "Rare" ? "RARE_ICON" : String{};
        if (!key.empty()) {
            auto icon = wx_resource(dialogs, key);
            if (!icon) icon = fallback_icon(key);
            if (icon) blit_surface(image, icon, scaled_win(5, win_scale), scaled_win(90, win_scale) - uselimith);
        }
    }

    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    static_cast<Surface&>(*bitmap) = *image;
    return bitmap;
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxnegabmp()
{
    return ::cw::imageretouch::to_negative_for_wxcard(get_wxbmp(), 0);
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxclickedbmp(::cw::lib::Value arg_header, std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_test_aptitude)
{
    std::optional<std::shared_ptr<::cw::character::Character>> aptitude;
    if (arg_test_aptitude && *arg_test_aptitude)
        aptitude = std::dynamic_pointer_cast<::cw::character::Character>(*arg_test_aptitude);
    std::shared_ptr<Surface> source;
    if (arg_wxbmp) source = std::make_shared<Surface>(static_cast<Surface&>(*arg_wxbmp));
    else if (auto bmp = get_cardwxbmp(arg_header, aptitude))
        source = std::make_shared<Surface>(static_cast<const Surface&>(*bmp));
    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    if (source) static_cast<Surface&>(*bitmap) = *::cw::imageretouch::resize_wx_bilinear(source, source->size.width * 9 / 10, source->size.height * 9 / 10);
    return bitmap;
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxclickedbmp_static(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp)
{
    if (!arg_wxbmp) return {}; auto source = std::make_shared<Surface>(static_cast<Surface&>(*arg_wxbmp)); auto bitmap = std::make_shared<::cw::lib::Bitmap>(); static_cast<Surface&>(*bitmap) = *::cw::imageretouch::resize_wx_bilinear(source, source->size.width * 9 / 10, source->size.height * 9 / 10); return bitmap;
}

std::shared_ptr<::cw::lib::Bitmap> CardImage::get_wxdealingbmp(::cw::lib::Value arg_header, std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::Integer arg_n, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude)
{
    std::shared_ptr<Surface> source;
    if (arg_wxbmp) source = std::make_shared<Surface>(static_cast<Surface&>(*arg_wxbmp));
    else if (auto bmp = get_cardwxbmp(arg_header, arg_test_aptitude))
        source = std::make_shared<Surface>(static_cast<const Surface&>(*bmp));
    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    if (source) {
        const Real win_scale = std::max(::cw::g_UP_WIN > 0.0 ? ::cw::g_UP_WIN : 1.0, 1.0);
        const auto [w0, h0] = bg_size(m_bgtype_typed);
        const auto width = std::max<Integer>(0, scaled_win(w0, win_scale) * arg_n / 100);
        const auto height = scaled_win(h0, win_scale);
        static_cast<Surface&>(*bitmap) = *::cw::imageretouch::resize_wx_bilinear(source, width, height);
    }
    return bitmap;
}

void CardImage::update(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_card, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_test_aptitude)
{
    (void)arg_card; (void)arg_test_aptitude; // Py の CardImage.update と同様、何もしない。
}

void CharacterCardImage::initialize(::cw::lib::Value arg_ccard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, bool arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_is_override_name, ::cw::lib::String arg_override_name, bool arg_is_override_image, std::optional<std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>>> arg_override_images, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size, bool arg_borrowed_owner)
{
    const auto owner = object_value<::cw::character::Character>(arg_ccard); m_ccard_typed = owner; m_pos_noscale_typed = arg_pos_noscale; m_is_scenariocard_typed = arg_is_scenariocard; m_scedir_typed = std::move(arg_scedir); m_card_size_typed = arg_card_size; m_is_override_image = Value(arg_is_override_image); m_is_override_name = Value(arg_is_override_name); m_override_name = Value(std::move(arg_override_name));
    m_override_paths_typed.clear(); m_override_flags_typed.clear(); if (arg_override_images) { m_override_paths_typed = std::get<0>(*arg_override_images); m_override_flags_typed = std::get<1>(*arg_override_images); }
    if (!owner) throw std::invalid_argument("CharacterCardImage requires Character");
    // 管理対象のキャラクターが画像を所有するため、画像からの二つの逆参
    // 照は所有しない。 スタック上・借用中のキャラクターに限り既存の
    // 非所有参照を維持し、弱参照が使えるよう制御ブロックを保持する。
    if (!arg_borrowed_owner) {
        m_ccard_typed = owner;
        m_ccard = Value{};
    } else {
        m_ccard = Value(std::static_pointer_cast<::cw::lib::Object>(owner));
    }
    m_can_loaded_scaledimage = Value(arg_can_loaded_scaledimage); m_scedir = Value(m_scedir_typed); m_rect_typed = {std::get<0>(m_pos_noscale_typed), std::get<1>(m_pos_noscale_typed), std::get<0>(m_card_size_typed) > 0 ? std::get<0>(m_card_size_typed) : 95, std::get<1>(m_card_size_typed) > 0 ? std::get<1>(m_card_size_typed) : 130};
    m__animated_owner = "CharacterCardImage:" + std::to_string(reinterpret_cast<std::uintptr_t>(this)); update_scale();
}

void CharacterCardImage::update_scale()
{
    m_image_mtime.clear();
    auto [w, h] = m_card_size_typed;
    w = w > 0 ? w : 95;
    h = h > 0 ? h : 130;
    // 実際の生成元の矩形には、共通の Py 互換 cw.s のスカラー変換を使う。 そうしないと 130px
    // のカードが 1.25 倍で 163px となり、Py の基準 162px とずれる。
    const auto scaled = [](Integer value) {
        return std::get<Integer>(::cw::s(Value(value)).storage());
    };
    const auto sx = scaled(std::get<0>(m_pos_noscale_typed));
    const auto sy = scaled(std::get<1>(m_pos_noscale_typed));
    w = scaled(w);
    h = scaled(h);
    m_rect_typed = {sx, sy, w, h};
    m_rect = Value(std::make_shared<::cw::lib::Surface>(*make_surface(w, h, 0, 0, 0, 0)));
    if (const auto owner = m_ccard_typed.lock()) {
        std::vector<std::shared_ptr<ImageInfo>> paths;
        if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(owner)) paths = player->m_imgpaths;
        else if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(owner)) paths = enemy->m_imgpaths;
        else if (const auto friendcard = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(owner)) paths = friendcard->m_imgpaths;
        set_faceimgs(std::move(paths), m_can_loaded_scaledimage);
        if (value_integer(m_is_override_name))
            set_nameimg(value_string(m_override_name));
        else
            set_nameimg(owner->m_name);
        set_levelimg(owner->m_level);
    }
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> CharacterCardImage::_bottom_left_position(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos)
{
    // Py と同様、座標は拡縮済みとして受け取り、130px の基準値には cw.s の切り捨てを使う。
    return {std::get<0>(arg_pos), m_rect_typed.height -
        (static_cast<Integer>(130 * ::cw::g_UP_SCR) - std::get<1>(arg_pos))};
}

void CharacterCardImage::fix_pcimage_updated()
{
    CardImage::fix_pcimage_updated(); if (m_override_images_upd) { m_override_paths_typed = std::get<0>(*m_override_images_upd); m_override_flags_typed = std::get<1>(*m_override_images_upd); m_override_images_upd.reset(); clear_cache(); }
}

void CharacterCardImage::set_faceimgs(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::Value arg_can_loaded_scaledimage)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto faceimgs_started = std::chrono::steady_clock::now();
#endif
    m_paths_typed = std::move(arg_paths); m_paths = Value(std::make_shared<::cw::data::VariantSequence>()); m_cardimgs_typed.clear();
    // 今回の読み込みだけでなく、倍率・アニメーションの再構築でも現在の規則
    // を維持する。 保留中の画像更新では、画像ごとの許可一覧を指定できる。
    m_can_loaded_scaledimage = arg_can_loaded_scaledimage;
    m_can_loaded_scaledimage_typed.assign(m_paths_typed.size(), false);
    if (const auto permissions = object_value<::cw::data::VariantSequence>(arg_can_loaded_scaledimage)) {
        for (std::size_t i = 0; i < permissions->m_values.size() && i < m_paths_typed.size(); ++i)
            m_can_loaded_scaledimage_typed[i] = value_integer(permissions->m_values[i]) != 0;
    } else {
        m_can_loaded_scaledimage_typed.assign(m_paths_typed.size(), value_integer(arg_can_loaded_scaledimage) != 0);
    }
    if (const auto seq = object_value<::cw::data::VariantSequence>(m_paths)) for (const auto& info : m_paths_typed) seq->m_values.emplace_back(info ? std::static_pointer_cast<::cw::lib::Object>(info) : nullptr);
    update_animated_paths();
    if (value_integer(m_is_override_image) != 0) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log_slow(
            "card.set_faceimgs", std::chrono::steady_clock::now() - faceimgs_started,
            "paths=" + std::to_string(m_paths_typed.size()) + " override=1");
#endif
        return;
    }
    for (std::size_t i = 0; i < m_paths_typed.size(); ++i) {
        const auto& info = m_paths_typed[i];
        auto path = info ? info->m_path : String{};
        auto code = is_code_path(path);
        if (!code && !image_file_exists(std::filesystem::u8path(path)))
            std::tie(path, code) = _resolve_imagepath(info);
        auto image = _load_pygame_image(path, true,
            i < m_can_loaded_scaledimage_typed.size() && m_can_loaded_scaledimage_typed[i]);
        m_cardimgs_typed.push_back(scale_display(image, ::cw::g_UP_SCR));
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "card.set_faceimgs", std::chrono::steady_clock::now() - faceimgs_started,
        "paths=" + std::to_string(m_paths_typed.size()) + " loaded=" +
        std::to_string(m_cardimgs_typed.size()) + " override=0");
#endif
}

bool CharacterCardImage::update_animation()
{
    if (!CardImage::update_animation()) return false;
    if (value_integer(m_is_override_image) == 0)
        set_faceimgs(m_paths_typed, m_can_loaded_scaledimage);
    return true;
}

void CharacterCardImage::set_nameimg(::cw::lib::String arg_name)
{
    if (arg_name.empty()) { m_nameimg_typed.reset(); m_nameimg.reset(); return; }
    const bool smooth = smooth_card_name();
    const auto font = card_text_font("ccardname", ::cw::g_UP_SCR);
    auto image = font->render(arg_name, smooth, {0, 0, 0});
    const auto max_width = std::max<Integer>(1, m_rect_typed.width - py_scaled(10, ::cw::g_UP_SCR));
    if (image && image->size.width > max_width)
        image = ::cw::image::smoothscale(image, {max_width, image->size.height}, smooth, false);
    m_nameimg_typed = std::move(image); m_nameimg = m_nameimg_typed;
}

void CharacterCardImage::set_levelimg(::cw::lib::Integer arg_level)
{
    const auto font = card_text_font("level", ::cw::g_UP_SCR);
    const auto scaled = [](Integer value) {
        return py_scaled(value, ::cw::g_UP_SCR);
    };
    const Integer width = std::max<Integer>(1, m_rect_typed.width + scaled(4));
    Integer overhang = 0, height = 1;
    for (char digit = '0'; digit <= '9'; ++digit) {
        const String text(1, digit);
        const auto size = font->size(text);
        height = std::max(height, std::get<1>(size));
        if (font->get_italic())
            overhang = std::max(overhang,
                std::get<0>(size) - std::get<0>(font->size_withoutoverhang(text)));
    }
    auto image = make_surface(width, height, 0, 0, 0, 0);
    Integer pen = width - overhang;
    const auto text = std::to_string(arg_level);
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        const String digit(1, *it);
        pen -= std::get<0>(font->size_withoutoverhang(digit));
        blit_surface(image, font->render(digit, true, {0, 0, 0}), pen, 0);
    }

    Integer top = height, right = 0;
    bool has_ink = false;
    for (Integer y = 0; y < height; ++y) for (Integer x = 0; x < width; ++x) {
        auto& alpha = image->rgba[static_cast<std::size_t>((y * width + x) * 4 + 3)];
        if (alpha) {
            has_ink = true;
            alpha = static_cast<std::uint8_t>(alpha / 2);
            top = std::min(top, y);
            right = std::max(right, x);
        }
    }
    Integer x = 0, y = 0;
    if (has_ink) {
        const Integer adjust = scaled(font->get_italic() ? 4 : 6);
        if (m_rect_typed.width - adjust < right) x = m_rect_typed.width - adjust - right;
        else if (right < m_rect_typed.width - scaled(10)) x = m_rect_typed.width - scaled(10) - right;
        const auto height_delta = font->get_height() - height;
        // Python の // と同様、負の奇数の余白は負の無限大方向へ切り下げる。 C++ の整数除算とは異なる。
        y = height_delta / 2 - (height_delta < 0 && height_delta % 2 != 0 ? 1 : 0);
        if (y + top < scaled(5)) y = scaled(5) - top;
    }
    // Python と同様、余白を含み得る文字画像ではなく、実際に描かれた文字形を基準に揃える。
    m_levelimg_pos = {Value(x), Value(y)};
    m_levelimg_typed = std::move(image);
    m_levelimg = Value(std::static_pointer_cast<::cw::lib::Object>(m_levelimg_typed));
}

void CharacterCardImage::update(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_card, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_test_aptitude)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto character_update_started = std::chrono::steady_clock::now();
#endif
    auto card = std::dynamic_pointer_cast<::cw::character::Character>(arg_card); if (!card) card = m_ccard_typed.lock();
    const auto bgname = get_cardbgname(card);
    const auto cardbgs = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_cardbgs : Value{};
    auto image = resource_surface(cardbgs, bgname);
    if (image) image = scale_surface(image, m_rect_typed.width, m_rect_typed.height);
    if (!image) image = make_surface(m_rect_typed.width, m_rect_typed.height, 0, 0, 0, 255);
    if (card && card->is_analyzable() && m_levelimg_typed) {
        // Python と同様、レベル表示を顔画像より先に描き、顔の絵の下に置く。
        blit_surface(image, m_levelimg_typed,
                     value_integer(std::get<0>(m_levelimg_pos)),
                     value_integer(std::get<1>(m_levelimg_pos)));
    }
    const auto left = py_scaled(11, ::cw::g_UP_SCR);
    const auto top = py_scaled(18, ::cw::g_UP_SCR);
    const auto blit_face = [&](const std::shared_ptr<::cw::image::ImageInfo>& info,
                               const std::shared_ptr<Surface>& sub) {
        if (!sub) return;
        const auto base = info
            ? info->calc_basecardposition({sub->size.width, sub->size.height}, false,
                                          String("LargeCard"), String("LargeCard"))
            : ::cw::lib::Rect{};
        // 公式 release_5.3 の顔画像配置を維持する。独自の Python
        // 派生版とは x 座標が異なるが、 (cw.s(11)+base.x,
        // cw.s(18)+base.y) を使う。 baserect
        // は Center（既定）・TopLeft を反映するた
        // め、TopLeft を無視して常に横中央へ置き直さない。
        ::cw::imageretouch::blit_2bitbmp_to_card(image, sub, {left + base.x, top + base.y});
    };
    m_image_mtime.clear();
    if (value_integer(m_is_override_image) != 0) {
        for (std::size_t i = 0; i < m_override_paths_typed.size(); ++i) {
            const auto& info = m_override_paths_typed[i];
            if (!info) continue;
            auto path = info->m_path;
            auto code = is_code_path(path);
            if (!code && !image_file_exists(std::filesystem::u8path(path)))
                std::tie(path, code) = _resolve_imagepath(info);
            if (!code) {
                const auto stamp = image_file_stamp(std::filesystem::u8path(path));
                if (!stamp) continue;
                m_image_mtime[path] = *stamp;
            }
            auto sub = _load_pygame_image(path, true,
                i < m_override_flags_typed.size() && m_override_flags_typed[i]);
            blit_face(m_override_paths_typed[i], scale_display(sub, ::cw::g_UP_SCR));
        }
    } else {
        std::vector<bool> selected;
        try {
            selected = ::cw::util::pick_up_face_image(
                Value(std::static_pointer_cast<::cw::lib::Object>(card)), m_paths, true);
        } catch (...) {
            selected.assign(m_cardimgs_typed.size(), true);
        }
        for (std::size_t i = 0; i < m_cardimgs_typed.size(); ++i) {
            if (i < selected.size() && !selected[i]) continue;
            blit_face(i < m_paths_typed.size() ? m_paths_typed[i] : nullptr,
                      m_cardimgs_typed[i]);
        }
    }
    if (m_nameimg_typed) {
        const bool white = card_name_is_white(bgname);
        blit_card_name(image, m_nameimg_typed,
                       py_scaled(5, ::cw::g_UP_SCR),
                       py_scaled(5, ::cw::g_UP_SCR),
                       white, bordering_card_name());
    }

    m_lifeimg.reset();
    if (card && card->is_analyzable() && !card->is_unconscious()) {
        const auto statuses = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_statuses : Value{};
        auto lifebar = resource_surface(statuses, "LIFEBAR");
        const auto optional_status = [&](const String& key) -> std::shared_ptr<Surface> {
            if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
            const auto path = ::cw::util::join_paths({Value(value_string(::cw::g_cwpy->m_skindir)),
                Value("Resource/Image/Status"), Value(key)});
            return ::cw::util::find_resource(path, ::cw::g_M_IMG).empty()
                ? std::shared_ptr<Surface>{} : resource_surface(statuses, key);
        };
        auto guage = optional_status("LIFEGUAGE2");
        auto lifemask = guage ? optional_status("LIFEGUAGE2_MASK") : std::shared_ptr<Surface>{};
        if (!guage || !lifemask) {
            guage = resource_surface(statuses, "LIFEGUAGE");
            lifemask.reset();
        }
        if (!guage) guage = make_surface(std::max<Integer>(1, static_cast<Integer>(std::lround(48 * ::cw::g_UP_SCR))),
                                          std::max<Integer>(1, static_cast<Integer>(std::lround(7 * ::cw::g_UP_SCR))), 0, 0, 0, 0);
        if (!lifebar) lifebar = make_surface(guage->size.width, guage->size.height, 196, 32, 32, 255);
        const auto life = std::clamp(card->m_life, Integer{0}, std::max<Integer>(1, card->m_maxlife));
        const auto ratio = static_cast<double>(life) / std::max<Integer>(1, card->m_maxlife);
        const auto unit = static_cast<Integer>(::cw::g_UP_SCR);
        const auto barx = static_cast<Integer>(
            ratio * (guage->size.width + unit) + 0.5) -
            (guage->size.width + unit);
        auto lifeimg = compose_lifebar(lifebar, guage, lifemask, barx, unit);
        m_lifeimg = lifeimg;
        const auto [lx, ly] = _bottom_left_position({
            static_cast<Integer>(8 * ::cw::g_UP_SCR),
            static_cast<Integer>(110 * ::cw::g_UP_SCR)});
        blit_surface(image, m_lifeimg, lx, ly);
    }
    if (arg_test_aptitude && *arg_test_aptitude && card) {
        const auto level = (*arg_test_aptitude)->get_showed_vocation_level(card);
        const auto stones = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_stones : Value{};
        auto icon = resource_surface(stones, "HAND" + std::to_string(level));
        if (!icon) icon = fallback_icon("HAND" + std::to_string(level));
        if (icon) {
            const auto scale = ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
            blit_surface(image, icon,
                         static_cast<Integer>(73 * scale),
                         static_cast<Integer>(95 * scale));
        }
    }
    Image::m_image = image; m_no_statusimg_typed = clone_surface(image); update_statusimg(Value(std::static_pointer_cast<::cw::lib::Object>(card)), std::nullopt);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "card.character_update", std::chrono::steady_clock::now() - character_update_started,
        "width=" + std::to_string(m_rect_typed.width) +
        " height=" + std::to_string(m_rect_typed.height) +
        " faces=" + std::to_string(m_cardimgs_typed.size()) +
        " status=" + (card && card->is_analyzable() ? "analyzable" : "plain"));
#endif
}

std::optional<std::shared_ptr<::cw::lib::Object>> CharacterCardImage::update_statusimg(::cw::lib::Value arg_ccard, std::optional<bool> arg_is_runningevent)
{
    const auto card = object_value<::cw::character::Character>(arg_ccard);
    if (!card || !m_no_statusimg_typed) return std::nullopt;
    auto image = clone_surface(m_no_statusimg_typed);
    const auto statuses = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_statuses : Value{};
    std::vector<Value> sequence;
    const auto analyzable = card->is_analyzable();
    const auto number = [this, arg_is_runningevent](std::shared_ptr<Surface> icon, Integer value, bool always) {
        return _put_number(std::move(icon), value, always, arg_is_runningevent);
    };
    const auto icon = [&statuses](const String& key) { return status_icon(statuses, key); };
    const auto duration = analyzable ? card->m_mentality_dur : 0;
    const auto beast = card->has_beast();
    if (beast) sequence.emplace_back(number(icon("SUMMON"), beast, true));
    if (card->is_poison()) sequence.emplace_back(number(icon("BODY0"), analyzable ? card->m_poison : 0, false));
    const auto show_time = !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
                           ::cw::g_cwpy->m_setting->m_show_statustime == "True" ||
                           ::cw::g_cwpy->m_setting->m_show_statustime == "NotEventTime";
    if (show_time && card->is_paralyze()) sequence.emplace_back(number(icon("BODY1"), analyzable ? card->m_paralyze : 0, false));
    if (show_time && card->is_sleep()) sequence.emplace_back(number(icon("MIND1"), duration, false));
    if (card->is_confuse()) sequence.emplace_back(number(icon("MIND2"), duration, false));
    else if (card->is_overheat()) sequence.emplace_back(number(icon("MIND3"), duration, false));
    else if (card->is_brave()) sequence.emplace_back(number(icon("MIND4"), duration, false));
    else if (card->is_panic()) sequence.emplace_back(number(icon("MIND5"), duration, false));
    if (show_time && card->is_bind()) sequence.emplace_back(number(icon("MAGIC0"), analyzable ? card->m_bind : 0, false));
    if (card->is_silence()) sequence.emplace_back(number(icon("MAGIC1"), analyzable ? card->m_silence : 0, false));
    if (card->is_faceup()) sequence.emplace_back(number(icon("MAGIC2"), analyzable ? card->m_faceup : 0, false));
    if (card->is_antimagic()) sequence.emplace_back(number(icon("MAGIC3"), analyzable ? card->m_antimagic : 0, false));
    const auto enhance = [this, &sequence, &icon, arg_is_runningevent](const String& positive, const String& negative,
                                                                         Integer value, Integer value_duration) {
        if (value > 0) _put_enhanceimg(sequence, icon(positive), value, value_duration, arg_is_runningevent);
        else if (value < 0) _put_enhanceimg(sequence, icon(negative), value, value_duration, arg_is_runningevent);
    };
    enhance("UP0", "DOWN0", card->m_enhance_act, analyzable ? card->m_enhance_act_dur : 0);
    enhance("UP1", "DOWN1", card->m_enhance_avo, analyzable ? card->m_enhance_avo_dur : 0);
    enhance("UP2", "DOWN2", card->m_enhance_res, analyzable ? card->m_enhance_res_dur : 0);
    enhance("UP3", "DOWN3", card->m_enhance_def, analyzable ? card->m_enhance_def_dur : 0);

    // Py と同様、加算前に各項へ cw.s を適用する。 まと
    // めた式を切り捨てると、小数倍率で後のアイコン位置が変わる。
    const Integer x = static_cast<Integer>(7 * ::cw::g_UP_SCR);
    const Integer y = static_cast<Integer>((analyzable && !card->is_unconscious() ? 92 : 107) * ::cw::g_UP_SCR);
    const Integer step = static_cast<Integer>(17 * ::cw::g_UP_SCR);
    const Integer column = static_cast<Integer>(85 * ::cw::g_UP_SCR);
    Integer index = 0;
    const auto sprite = std::dynamic_pointer_cast<::cw::sprite::base::CWPySprite>(card);
    const auto current_rect = sprite ? sprite->m_rect : m_rect_typed;
    ::cw::lib::Rect clip{current_rect.x, current_rect.y, 0, 0};
    for (const auto& item : sequence) {
        auto subimg = surface_value(item);
        if (!subimg) continue;
        const auto [px, py] = _bottom_left_position({x + index / 5 * step,
                                                       y - index * step + index / 5 * column});
        blit_surface(image, subimg, px, py);
        const auto left = px + current_rect.x;
        const auto top = py + current_rect.y;
        if (clip.width == 0 || clip.height == 0) clip = {left, top, subimg->size.width, subimg->size.height};
        else {
            const auto right = std::max(clip.x + clip.width, left + subimg->size.width);
            const auto bottom = std::max(clip.y + clip.height, top + subimg->size.height);
            clip.x = std::min(clip.x, left); clip.y = std::min(clip.y, top);
            clip.width = right - clip.x; clip.height = bottom - clip.y;
        }
        ++index;
    }
    Image::m_image = image;
    if (index == 0) return std::nullopt;
    return std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::RectValue>(clip));
}

std::shared_ptr<::cw::lib::Surface> CharacterCardImage::_put_number(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_num, bool arg_always, std::optional<bool> arg_is_runningevent)
{
    if (!arg_image) return arg_image;
    // Py と同様、イベント状態の引数が欠落していれば、値がゼロでも解決する。
    bool running = arg_is_runningevent ? *arg_is_runningevent : (::cw::g_cwpy && ::cw::g_cwpy->is_runningevent());
    if (::cw::g_cwpy) {
        running = running && ::cw::g_cwpy->is_playingscenario() && !::cw::g_cwpy->is_reloading();
        running = running && !is_special_area();
        if (::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_cardeffectmotion &&
            !::cw::g_cwpy->is_battlestatus())
            running = false;
    }
    const String show = !::cw::g_cwpy || !::cw::g_cwpy->m_setting ? String("True") : ::cw::g_cwpy->m_setting->m_show_statustime;
    if (arg_num && (arg_always || show == "True" || (show == "NotEventTime" && !running)))
        return ::cw::util::put_number(arg_image, arg_num);
    return arg_image;
}

void CharacterCardImage::_put_enhanceimg(std::vector<::cw::lib::Value>& arg_seq, std::shared_ptr<::cw::lib::Surface> arg_bmp, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration, std::optional<bool> arg_is_runningevent)
{
    if (!arg_bmp) return;
    if (arg_value == 0) {
        arg_seq.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(
            _put_number(arg_bmp, arg_duration, false, arg_is_runningevent))));
        return;
    }
    std::tuple<Integer, Integer, Integer> color{0, 0, 0};
    if (arg_value >= 10) color = {255, 0, 0};
    else if (arg_value >= 7) color = {175, 0, 0};
    else if (arg_value >= 4) color = {127, 0, 0};
    else if (arg_value > 0) color = {79, 0, 0};
    else if (arg_value <= -10) color = {0, 0, 51};
    else if (arg_value <= -7) color = {0, 0, 85};
    else if (arg_value <= -4) color = {0, 0, 136};
    else color = {0, 0, 187};
    // Python は不透明な色の矩形を追加し、その同じ位置へ透明
    // な UP/DOWN の記号を描く。 ここでは両者を一つの描
    // 画面へ合成する。記号画像自体を塗りつぶして記号を隠さない。
    auto image = make_surface(arg_bmp->size.width, arg_bmp->size.height,
                              static_cast<std::uint8_t>(std::get<0>(color)),
                              static_cast<std::uint8_t>(std::get<1>(color)),
                              static_cast<std::uint8_t>(std::get<2>(color)), 255);
    blit_surface(image, arg_bmp, 0, 0);
    image = _put_number(image, arg_duration, false, arg_is_runningevent);
    arg_seq.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(image)));
}

::cw::lib::String CharacterCardImage::get_cardbgname(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    if (!arg_ccard) return "LARGE"; if (arg_ccard->is_unconscious()) return "FAINT"; if (arg_ccard->is_petrified()) return "PETRIF"; if (arg_ccard->is_paralyze()) return "PARALY"; if (arg_ccard->is_sleep()) return "SLEEP"; if (arg_ccard->is_bind()) return "BIND"; if (arg_ccard->is_heavyinjured()) return "DANGER"; if (arg_ccard->is_injured()) return "INJURY"; return "LARGE";
}

std::shared_ptr<::cw::lib::Surface> CharacterCardImage::get_image()
{
    return Image::m_image;
}

std::shared_ptr<::cw::lib::Surface> CharacterCardImage::get_cardimg(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    (void)arg_header; return get_image();
}

void LargeCardImage::initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, ::cw::lib::String arg_premium, ::cw::lib::Value arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_anotherscenariocard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size)
{
    (void)arg_bgtype; // Python版もLargeCardImageでは常に"LARGE"へ固定する。
    CardImage::initialize(std::move(arg_paths), "LARGE", std::move(arg_name), std::move(arg_premium), std::move(arg_can_loaded_scaledimage), arg_is_scenariocard, std::move(arg_scedir), arg_anotherscenariocard, arg_card_size);
}

std::shared_ptr<::cw::lib::Surface> LargeCardImage::get_image()
{
    return CardImage::get_image();
}

std::shared_ptr<::cw::lib::Bitmap> LargeCardImage::get_wxbmp()
{
    return CardImage::get_wxbmp();
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imageinfos(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_pcnumber)
{
    std::vector<std::shared_ptr<ImageInfo>> result; if (!arg_data) return result;
    const auto add_path = [&result](String path, String postype) { if (!path.empty()) { auto info = std::make_shared<ImageInfo>(); info->initialize(std::move(path), 0, std::nullopt, std::move(postype), std::nullopt); result.push_back(info); } };
    if (arg_data->m_tag == "ImagePaths") { for (const auto& child : arg_data->m_children) if (child && child->m_tag == "ImagePath") add_path(::cw::util::validate_filepath(child->m_text), child->get("positiontype", "Default")); else if (arg_pcnumber && child && child->m_tag == "PCNumber") { const auto n = child->getint(".", std::nullopt, 0); if (n) { auto info = std::make_shared<ImageInfo>(); info->initialize({}, n, std::nullopt, "Default", std::nullopt); result.push_back(info); } } return result; }
    add_path(::cw::util::validate_filepath(arg_data->get("path", "")), arg_data->get("positiontype", "Default")); if (arg_pcnumber) { const auto n = arg_data->getint(".", std::optional<String>("pcNumber"), 0); if (n) { auto info = std::make_shared<ImageInfo>(); info->initialize({}, n, std::nullopt, "Default", std::nullopt); result.push_back(info); } }
    if (const auto path = arg_data->find("ImagePath")) add_path(::cw::util::validate_filepath((*path)->m_text), (*path)->get("positiontype", "Default")); if (arg_pcnumber) if (const auto pcn = arg_data->find("PCNumber")) { const auto n = (*pcn)->getint(".", std::nullopt, 0); if (n) { auto info = std::make_shared<ImageInfo>(); info->initialize({}, n, std::nullopt, "Default", std::nullopt); result.push_back(info); } }
    if (const auto paths = arg_data->find("ImagePaths")) { auto nested = get_imageinfos(*paths, arg_pcnumber); result.insert(result.end(), nested.begin(), nested.end()); } return result;
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imageinfos_p(std::shared_ptr<::cw::header::GetProperty> arg_prop, bool arg_pcnumber)
{
    std::vector<std::shared_ptr<ImageInfo>> result;
    if (!arg_prop) return result;
    const auto add = [&result](String path, String postype, Integer pc) {
        if (path.empty() && pc <= 0) return;
        auto info = std::make_shared<ImageInfo>();
        info->initialize(std::move(path), pc, std::nullopt, std::move(postype), std::nullopt);
        result.push_back(info);
    };
    const auto parse_pc = [](const String& text) -> Integer {
        if (text.empty()) return 0;
        const auto normalized = ::cw::lib::normalize_python_integer(text);
        if (!normalized) throw std::invalid_argument("invalid PCNumber: " + text);
        std::size_t used = 0;
        const auto value = std::stoll(*normalized, &used, 10);
        if (used != normalized->size()) throw std::invalid_argument("invalid PCNumber: " + text);
        return static_cast<Integer>(value);
    };
    String postype = "Default";
    if (const auto it = arg_prop->m_attrs.find(std::optional<String>{"ImagePath"}); it != arg_prop->m_attrs.end()) {
        if (const auto pit = it->second.find("positiontype"); pit != it->second.end())
            postype = pit->second;
    }
    add(::cw::util::validate_filepath(arg_prop->m_properties.count("ImagePath") ? arg_prop->m_properties["ImagePath"] : ""),
        postype, 0);
    if (arg_pcnumber) {
        const auto n = arg_prop->m_properties.count("PCNumber") ? parse_pc(arg_prop->m_properties["PCNumber"]) : 0;
        if (n > 0) add({}, "Default", n);
    }
    if (const auto it = arg_prop->m_third.find("ImagePaths"); it != arg_prop->m_third.end()) {
        for (const auto& [tag, attrs, path] : it->second) {
            if (tag == "ImagePath") {
                add(::cw::util::validate_filepath(path),
                    attrs.count("positiontype") ? attrs.at("positiontype") : String("Default"), 0);
            } else if (tag == "PCNumber") {
                const auto n = parse_pc(path);
                if (n > 0) add({}, "Default", n);
            }
        }
    }
    return result;
}

std::shared_ptr<::cw::lib::Surface> create_type2textcell(::cw::lib::String arg_text, ::cw::lib::String arg_face, ::cw::lib::Integer arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_sline, bool arg_vertical, bool arg_antialias, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_cellsize, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_bcolor, ::cw::lib::Integer arg_bwidth)
{
    const auto work_size = arg_antialias ? std::tuple<Integer, Integer>{std::get<0>(arg_cellsize) * 2, std::get<1>(arg_cellsize) * 2} : arg_cellsize;
    auto image = make_surface(std::get<0>(work_size), std::get<1>(work_size), 0, 0, 0, 0);
    const auto [original_font, original_lineheight] = get_textcellfont(arg_size, arg_face, arg_color, arg_bold, arg_italic, arg_uline, arg_vertical, false);
    std::shared_ptr<::cw::imageretouch::Font> font = original_font;
    Integer lineheight = original_lineheight;
    if (arg_antialias) std::tie(font, lineheight) = get_textcellfont(arg_size * 2, arg_face, arg_color, arg_bold, arg_italic, arg_uline, arg_vertical, false);
    const auto width = std::get<0>(work_size);
    Integer x = arg_vertical ? width - lineheight : 0;
    Integer y = 0;
    for (const auto& line : split_lines(arg_text)) {
        auto subimg = font ? font->render(line, false, arg_color) : nullptr;
        if (!subimg) subimg = make_surface(std::max<Integer>(1, lineheight), std::max<Integer>(1, lineheight), 0, 0, 0, 0);
        if (arg_antialias && original_font) {
            const auto [ow, oh] = original_font->size(line);
            if (subimg->size.width != ow * 2 || subimg->size.height != oh * 2)
                subimg = scale_surface(subimg, std::max<Integer>(1, ow * 2), std::max<Integer>(1, oh * 2));
        }
        if (arg_sline) {
            auto strike = font ? font->render("―", false, arg_color) : nullptr;
            if (strike) {
                strike = scale_surface(strike, std::max<Integer>(1, subimg->size.width + std::get<Integer>(::cw::s(Value(Integer{10})).storage())),
                                       std::max<Integer>(1, subimg->size.height));
                blit_surface(subimg, strike, std::get<Integer>(::cw::s(Value(Integer{-5})).storage()), 0);
            }
        }
        if (arg_vertical) subimg = rotate_cw(subimg);
        blit_surface(image, subimg, x, y);
        if (arg_vertical) x -= lineheight; else y += lineheight;
    }
    ::cw::imageretouch::add_border(image, arg_bcolor, arg_antialias ? arg_bwidth * 2 : arg_bwidth);
    return arg_antialias ? smooth_scale_surface(image, std::get<0>(arg_cellsize), std::get<1>(arg_cellsize)) : image;
}

void draw_textcell(std::shared_ptr<::cw::lib::Surface> arg_image, std::shared_ptr<::cw::lib::Object> arg_rect, ::cw::lib::String arg_text, ::cw::lib::String arg_face, ::cw::lib::Integer arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_sline, bool arg_vertical, bool arg_antialias, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_bcolor)
{
    if (!arg_image) return;
    auto rect_value = std::dynamic_pointer_cast<::cw::RectValue>(arg_rect);
    if (!rect_value) return;
    const auto rect = rect_value->rect;
    auto image = make_surface(std::max<Integer>(0, rect.width), std::max<Integer>(0, rect.height), 0, 0, 0, 0);
    const auto [font, lineheight] = get_textcellfont(arg_size, arg_face, arg_color, arg_bold, arg_italic, arg_uline, arg_vertical, arg_antialias);
    if (!font) return;
    Integer x = arg_vertical ? rect.width - lineheight : 0;
    Integer y = 0;
    for (const auto& line : split_lines(arg_text)) {
        if (arg_bcolor) {
            auto border = font->render(line, arg_antialias, *arg_bcolor);
            if (border && arg_sline) {
                auto strike = font->render("―", arg_antialias, *arg_bcolor);
                if (strike) blit_surface(border, scale_surface(strike, std::max<Integer>(1, border->size.width + std::get<Integer>(::cw::s(Value(Integer{10})).storage())), std::max<Integer>(1, lineheight)), std::get<Integer>(::cw::s(Value(Integer{-5})).storage()), 0);
            }
            if (border && arg_vertical) border = rotate_cw(border);
            if (border) for (const auto [ox, oy] : {std::pair<Integer, Integer>{-1, -1}, {-1, 1}, {1, -1}, {1, 1}})
                blit_surface(image, border, x + ox, y + oy);
        }
        auto subimg = font->render(line, arg_antialias, arg_color);
        if (subimg && arg_sline) {
            auto strike = font->render("―", arg_antialias, arg_color);
            if (strike) blit_surface(subimg, scale_surface(strike, std::max<Integer>(1, subimg->size.width + std::get<Integer>(::cw::s(Value(Integer{10})).storage())), std::max<Integer>(1, lineheight)), std::get<Integer>(::cw::s(Value(Integer{-5})).storage()), 0);
        }
        if (subimg && arg_vertical) subimg = rotate_cw(subimg);
        if (subimg) blit_surface(image, subimg, x, y);
        if (arg_vertical) x -= lineheight; else y += lineheight;
    }
    blit_surface(arg_image, image, rect.x, rect.y);
}

std::tuple<std::shared_ptr<::cw::imageretouch::Font>, ::cw::lib::Integer> get_textcellfont(::cw::lib::Integer arg_size, ::cw::lib::String arg_face, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_vertical, bool arg_antialiased)
{
    (void)arg_color; (void)arg_vertical; (void)arg_antialiased;
    auto font = std::make_shared<::cw::imageretouch::Font>();
    font->initialize(std::move(arg_face), -arg_size, arg_bold, arg_italic);
    if (arg_uline) font->set_underline(true);
    return {font, font->get_linesize()};
}

std::shared_ptr<::cw::lib::Surface> create_colorcell(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color1, ::cw::lib::String arg_gradient, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color2)
{
    const auto w = std::max<Integer>(0, std::get<0>(arg_size));
    const auto h = std::max<Integer>(0, std::get<1>(arg_size));
    const auto [r1, g1, b1, a1] = arg_color1;
    const auto [r2, g2, b2, a2] = arg_color2;
    // Py のシナリオ所有のリソースキャッシュとバイト上限による整理を使う
    // 。 ネイティブの文字列キーには型付き ColorCell キーの接
    // 頭辞と長さを付け、ファイルのキーと区別する。 シナリオがない場合も
    // 共通ヘルパーは描画するが、プロセス全体のキャッシュには保持しない。
    const auto sdata = ::cw::g_cwpy ? object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata) : nullptr;
    const auto key = String("colorcell|") + std::to_string(w) + "x" + std::to_string(h) + "|" +
        std::to_string(arg_gradient.size()) + ":" + arg_gradient + "|" +
        std::to_string(r1) + "," + std::to_string(g1) + "," + std::to_string(b1) + "," + std::to_string(a1) + "|" +
        std::to_string(r2) + "," + std::to_string(g2) + "," + std::to_string(b2) + "," + std::to_string(a2);
    if (sdata) {
        if (const auto found = sdata->m_resource_cache.find(key); found != sdata->m_resource_cache.end()) {
            if (const auto cached = object_value<Surface>(found->second)) return cached;
        }
    }

    auto image = make_surface(w, h);
    const auto cache_image = [&] {
        if (sdata) {
            sdata->sweep_resourcecache(::cw::util::calc_imagesize(image));
            sdata->m_resource_cache[key] = Value(std::static_pointer_cast<::cw::lib::Object>(image));
        }
        return image;
    };
    // 空の描画面でも Py と同じキャッシュ追加・容量計上を行
    // う。 どちらかの寸法がゼロなら先頭行へアクセスしない。
    if (w == 0 || h == 0) return cache_image();
    const auto mix = [](Integer a, Integer b, Integer p) {
        return create_colorcell_calc_per(a, b, p);
    };
    const auto put_row = [&](Integer y, Integer p, bool invert) {
        const auto r = static_cast<std::uint8_t>(invert ? mix(r2, r1, p) : mix(r1, r2, p));
        const auto g = static_cast<std::uint8_t>(invert ? mix(g2, g1, p) : mix(g1, g2, p));
        const auto b = static_cast<std::uint8_t>(invert ? mix(b2, b1, p) : mix(b1, b2, p));
        const auto a = static_cast<std::uint8_t>(invert ? mix(a2, a1, p) : mix(a1, a2, p));
        auto i = static_cast<std::size_t>(y * w * 4);
        for (Integer x = 0; x < w; ++x, i += 4) {
            image->rgba[i] = r; image->rgba[i + 1] = g;
            image->rgba[i + 2] = b; image->rgba[i + 3] = a;
        }
    };
    if (arg_gradient == "LeftToRight") {
        for (Integer x = 0; x < w; ++x) {
            const auto p = w ? (x << 8) / w : 0;
            const auto i = static_cast<std::size_t>(x * 4);
            image->rgba[i] = static_cast<std::uint8_t>(mix(r1, r2, p));
            image->rgba[i + 1] = static_cast<std::uint8_t>(mix(g1, g2, p));
            image->rgba[i + 2] = static_cast<std::uint8_t>(mix(b1, b2, p));
            image->rgba[i + 3] = static_cast<std::uint8_t>(mix(a1, a2, p));
        }
        const auto row_bytes = static_cast<std::size_t>(w * 4);
        for (Integer y = 1; y < h; ++y)
            std::copy_n(image->rgba.begin(), row_bytes,
                        image->rgba.begin() + static_cast<std::ptrdiff_t>(y * w * 4));
    } else if (arg_gradient == "TopToBottom") {
        for (Integer y = 0; y < h; ++y)
            put_row(y, h ? ((h - y - 1) << 8) / h : 0, true);
    } else {
        // Python と同様、color1 を勾配補間せず直接塗る。 ゼロ位置で
        // calc_per を適用すると、減少するチャンネルが 1 暗くなる。
        ::cw::lib::simd_image::fill_rgba(image->rgba.data(), image->rgba.size() / 4U,
            static_cast<std::uint8_t>(r1), static_cast<std::uint8_t>(g1),
            static_cast<std::uint8_t>(b1), static_cast<std::uint8_t>(a1));
    }
    return cache_image();
}

::cw::lib::Integer create_colorcell_calc_per(::cw::lib::Integer arg_mn, ::cw::lib::Integer arg_mx, ::cw::lib::Integer arg_per)
{
    if (arg_mn == arg_mx) return arg_mn;
    const auto c = arg_mx - arg_mn;
    const auto numerator = c * arg_per + (c >= 0 ? 128 : -128);
    // Python の符号付き 8 ビット右シフトは負の無限大方向へ切り下げる。 C++ の除算はゼロ方向へ切り捨てるため、その違いを補う。
    const auto rounded = numerator / 256 - (numerator < 0 && numerator % 256 != 0 ? 1 : 0);
    return std::clamp(arg_mn + rounded, Integer{0}, Integer{255});
}

std::shared_ptr<::cw::lib::Surface> zoomcard(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Real arg_scale)
{
    if (!arg_image || arg_scale == 1.0) return arg_image;
    bool smoothing = true;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        smoothing = arg_scale > 1.0 ? ::cw::g_cwpy->m_setting->m_smoothing_card_up
                                    : ::cw::g_cwpy->m_setting->m_smoothing_card_down;
    return smoothscale(arg_image,
                       {static_cast<Integer>(arg_image->size.width * arg_scale),
                        static_cast<Integer>(arg_image->size.height * arg_scale)},
                       smoothing, true);
}

std::shared_ptr<::cw::lib::Surface> scale_nearest(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size)
{
    if (!arg_surface) return {};
    const auto [width, height] = arg_size;
    if (width < 0 || height < 0) throw std::invalid_argument("scale: negative dimensions");
    auto result = scale_surface(arg_surface, width, height);
    // サイズゼロの入力でも、形式・不透明度・透過色の情報を維持する。
    // ピクセル拡縮は smoothscale の最近傍分岐と共有する。
    result->has_pixel_alpha = arg_surface->has_pixel_alpha;
    result->pixel_bits = arg_surface->pixel_bits;
    result->depth1 = arg_surface->depth1;
    result->draw_scale = arg_surface->draw_scale;
    result->known_opaque = arg_surface->known_opaque;
    return result;
}

std::shared_ptr<::cw::lib::Surface> smoothscale_card(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, bool arg_smoothing)
{
    return smoothscale(std::move(arg_surface), std::move(arg_size), arg_smoothing, true);
}

std::shared_ptr<::cw::lib::Surface> rotozoom_zero(std::shared_ptr<::cw::lib::Surface> source, Real scale)
{
    if (!source) return {};
    // pygame 1.9.6 の transform.c と同様、倍率を一度 C の float にしてから rotozoom.c
    // へ渡す。 特に 20 * float(0.9) は 18 ではなく 17 に切り捨てられる。
    const auto parsed = static_cast<float>(scale);
    if (!std::isfinite(parsed)) throw std::invalid_argument("rotozoom: non-finite scale");
    if (parsed == 0.0f) {
        auto result = make_surface(source->size.width, source->size.height);
        result->pixel_bits = source->pixel_bits;
        result->has_pixel_alpha = source->has_pixel_alpha;
        return result;
    }
    const double zoom = std::max(0.001, static_cast<double>(parsed));
    const auto dimension = [zoom](Integer size) {
        const auto value = static_cast<double>(size) * zoom;
        if (value >= static_cast<double>(std::numeric_limits<Integer>::max()))
            throw std::length_error("rotozoom: dimensions exceed integer range");
        return std::max<Integer>(1, static_cast<Integer>(value));
    };
    const auto width = dimension(source->size.width), height = dimension(source->size.height);
    auto result = make_surface(width, height);
    result->pixel_bits = 32;
    result->has_pixel_alpha = source->pixel_bits != 32 || source->has_pixel_alpha;
    result->draw_scale = source->draw_scale;
    if (source->size.width <= 0 || source->size.height <= 0) return result;
    const auto sw = static_cast<std::uint64_t>(source->size.width);
    const auto sh = static_cast<std::uint64_t>(source->size.height);
    if (sw > source->rgba.size() / 4U || sh > source->rgba.size() / (sw * 4U))
        throw std::invalid_argument("rotozoom: incomplete RGBA surface");
    auto pixels = source;
    if (source->pixel_bits != 32) {
        // rotozoom.c は pygame の上書きした Surface.blit ではなく
        // SDL_BlitSurface を直接使う。 RGB の不透明度は SDL の汎用の丸
        // め付き RGB 演算を使い、アルファを不透明にする。透過色のピクセルはゼロのままにする。
        pixels = make_surface(source->size.width, source->size.height);
        const auto opacity = source->surface_alpha_enabled ? source->surface_alpha : std::uint8_t{255};
        const auto convert = [&](std::size_t offset, std::size_t count) {
            // SDL と同様、不透明度ゼロの RGB 全体アルファ転送は
            // 完全に省く。 出力先のアルファも初期値のゼロを維持する。
            if (opacity == 0) return;
            ::cw::lib::simd_image::force_alpha(pixels->rgba.data() + offset, count, 255);
            ::cw::lib::simd_image::surface_alpha_blend_rgba(
                source->rgba.data() + offset, count * 4U,
                pixels->rgba.data() + offset, count * 4U, count, 1,
                opacity, true, source->pixel_bits == 16, source->has_colorkey);
        };
        if (source->has_colorkey) {
            const auto [r, g, b] = source->colorkey;
            for (std::size_t i = 0; i < static_cast<std::size_t>(sw * sh) * 4U; i += 4U) {
                if (source->rgba[i] == r && source->rgba[i + 1] == g && source->rgba[i + 2] == b) continue;
                convert(i, 1);
            }
        } else {
            convert(0, static_cast<std::size_t>(sw * sh));
        }
    }
    // 角度ゼロの SDL_gfx の拡縮は、smoothscale の面積平均ではなく
    // 16.16 の双線形補間を使う。 独立した符号付きの切り下げ手順を維持する。
    const auto sx = static_cast<Integer>(65536.0 * static_cast<float>(source->size.width - 1) / static_cast<float>(width));
    const auto sy = static_cast<Integer>(65536.0 * static_cast<float>(source->size.height - 1) / static_cast<float>(height));
    const auto floor16 = [](Integer value) {
        return value / 65536 - (value < 0 && value % 65536 != 0 ? 1 : 0);
    };
    for (Integer y = 0; y < height; ++y) {
        const auto fy = y * sy;
        const auto y0 = std::min(source->size.height - 1, fy / 65536);
        // 安全のため、pygame の旧実装が一ピクセルの行
        // ・列の外を読む箇所では、境界のピクセルを繰り返す。
        const auto y1 = std::min(source->size.height - 1, y0 + 1);
        for (Integer x = 0; x < width; ++x) {
            const auto fx = x * sx;
            const auto x0 = std::min(source->size.width - 1, fx / 65536);
            const auto x1 = std::min(source->size.width - 1, x0 + 1);
            const auto at = [&](Integer px, Integer py, int c) {
                return Integer(pixels->rgba[static_cast<std::size_t>((py * source->size.width + px) * 4 + c)]);
            };
            const auto out = static_cast<std::size_t>((y * width + x) * 4);
            for (int c = 0; c < 4; ++c) {
                const auto top = at(x0, y0, c) + floor16((at(x1, y0, c) - at(x0, y0, c)) * (fx & 65535));
                const auto bottom = at(x0, y1, c) + floor16((at(x1, y1, c) - at(x0, y1, c)) * (fx & 65535));
                result->rgba[out + c] = static_cast<std::uint8_t>(top + floor16((bottom - top) * (fy & 65535)));
            }
        }
    }
    return result;
}

std::shared_ptr<::cw::lib::Surface> smoothscale(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, bool arg_smoothing, bool arg_iscard)
{
    if (!arg_surface) return {};
    // Python と同様、範囲内へ収める前に要求サイズを確認し、(0,0) も含める。
    if (arg_surface->size.width == std::get<0>(arg_size) &&
        arg_surface->size.height == std::get<1>(arg_size)) return arg_surface;
    const auto width = std::max<Integer>(1, std::get<0>(arg_size));
    const auto height = std::max<Integer>(1, std::get<1>(arg_size));
    if (arg_surface->size.width <= 0 || arg_surface->size.height <= 0)
    {
        auto result = make_surface(width, height, 0, 0, 0, 255);
        result->has_pixel_alpha = false; // Python と同様、入力が空の場合の代替は convert_alpha() ではなく convert() を使う。
        return result;
    }
    if (arg_surface->size.height <= 1)
        return scale_surface(arg_surface, width, height);
    if (!arg_smoothing || (width % arg_surface->size.width == 0 && height % arg_surface->size.height == 0))
        return scale_surface(arg_surface, width, height);

    if (arg_iscard && arg_surface->size.width < width && arg_surface->size.height < height) {
        if (arg_surface->pixel_bits < 24)
            throw std::invalid_argument("smoothscale: only 24-bit or 32-bit surfaces can be smoothly scaled");
        const auto w = arg_surface->size.width, h = arg_surface->size.height;
        if (static_cast<std::uint64_t>(w) > arg_surface->rgba.size() / 4U ||
            static_cast<std::uint64_t>(h) > arg_surface->rgba.size() / (static_cast<std::uint64_t>(w) * 4U))
            throw std::invalid_argument("smoothscale: incomplete card RGBA surface");
        auto prepared = scale_surface(arg_surface, w + 1, h + 1);
        ::cw::lib::simd_image::fill_rgba(prepared->rgba.data(), prepared->rgba.size() / 4U,
            0, 0, 0, prepared->has_pixel_alpha ? 0 : 255);
        blit_surface(prepared, arg_surface, 0, 0);
        blit_surface(prepared, arg_surface, w, 0, ::cw::lib::Rect{w - 1, 0, 1, h});
        // 正方形でないカードも含め、Py の実際の (h,0) の配置を維持する。
        blit_surface(prepared, arg_surface, h, 0, ::cw::lib::Rect{0, h - 1, w, 1});
        const auto corner = static_cast<std::size_t>((h * prepared->size.width + w) * 4);
        const auto original_corner = static_cast<std::size_t>((h * w - 1) * 4);
        std::copy_n(arg_surface->rgba.data() + original_corner, 4, prepared->rgba.data() + corner);
        if (!prepared->has_pixel_alpha) prepared->rgba[corner + 3] = 255;
        return smooth_scale_surface(prepared, width, height);
    }
    if (arg_surface->pixel_bits < 24) {
        auto converted = clone_surface(arg_surface);
        converted->pixel_bits = 24;
        converted->has_pixel_alpha = false;
        return smooth_scale_surface(converted, width, height);
    }
    return smooth_scale_surface(arg_surface, width, height);
}

std::tuple<::cw::lib::Value, bool> fix_cwnext16bitbitmap(::cw::lib::Bytes arg_data)
{
    if (!bmp40(arg_data)) return {::cw::lib::Value(std::move(arg_data)), true};
    auto offset = le32(arg_data, 10);
    if (offset == 0) return {::cw::lib::Value(std::move(arg_data)), true};
    const auto width = sle32(arg_data, 18);
    const auto height_raw = sle32(arg_data, 22);
    const auto bit_count = le16(arg_data, 28);
    const auto compression = le32(arg_data, 30);
    auto colors = le32(arg_data, 46);
    if (width < 0) return {::cw::lib::Value(std::move(arg_data)), true};
    const auto height = height_raw < 0 ? -static_cast<std::int64_t>(height_raw) : height_raw;
    const auto line_size = ((static_cast<std::uint64_t>(width) * bit_count + 31) / 32) * 4;
    bool unchanged = offset <= arg_data.size() &&
        arg_data.size() - offset == line_size * static_cast<std::uint64_t>(height);
    if (!unchanged) {
        bool game_thread = false;
        if (::cw::g_cwpy) {
            std::scoped_lock lock(::cw::g_cwpy->m_game_exec_mutex);
            game_thread = ::cw::g_cwpy->m_game_thread_id == std::this_thread::get_id();
        }
        if (!game_thread) {
            // Py の cw.image と同様、CWPy 以外のスレッドだけで画像ローダ
            // ーがデータを受け付けるか確認し、その前に RLE4 を修復する。 CWPy
            // ・ゲームスレッドでは長さが不一致なら常に bfOffBits を修復する。
            arg_data = patch_rle4bitmap(std::move(arg_data));
            try {
                (void)::cw::lib::imagecodec::decode_rgba(arg_data, 0);
                return {::cw::lib::Value(std::move(arg_data)), true};
            } catch (const ::cw::lib::imagecodec::Error&) {
                // 実際にデコードに失敗した画像だけを再試行する。
            }
        }
        std::uint64_t corrected = 54;
        if (compression == 3) corrected += 12;
        if ((bit_count == 1 || bit_count == 4 || bit_count == 8) && colors == 0)
            // Py の CardWirthNext 修復式を維持し、従来の bits*bits
            // のパレット数も使う。 BMP 仕様の 2^bits へ変えると、同じ
            // 1 ビット・8 ビット入力で修復後の bfOffBits が変わる。
            colors = static_cast<std::uint32_t>(bit_count) * bit_count;
        corrected += static_cast<std::uint64_t>(colors) * 4;
        if (corrected <= UINT32_MAX) put_le32(arg_data, 10, static_cast<std::uint32_t>(corrected));
        else unchanged = true;
    }
    // RLE4 は imageretouch の実デコーダへ接続済み。展開後の画素データと
    // BITMAPINFOHEADER の圧縮/サイズを patch_rle4bitmap で整合させる。
    arg_data = patch_rle4bitmap(std::move(arg_data));
    return {::cw::lib::Value(std::move(arg_data)), unchanged};
}

std::tuple<::cw::lib::Bytes, bool> fix_cwnext32bitbitmap(::cw::lib::Bytes arg_data)
{
    if (!bmp40(arg_data) || le16(arg_data, 28) != 32) return {std::move(arg_data), true};
    auto colors = le32(arg_data, 46);
    if (colors == 0) return {std::move(arg_data), true};
    auto file_size = le32(arg_data, 2);
    auto offset = le32(arg_data, 10);
    const auto image_size = le32(arg_data, 34);
    const std::uint64_t palette_size = static_cast<std::uint64_t>(colors) * 4;
    // ネイティブ版では、パレットを取り除く前に矛盾する寸法・
    // 位置や、実際の入力外のパレットも拒否する。 Py はヘ
    // ッダー値から直接進むが、ここでは元のバイト列を保持し
    // 、修復中の桁下がりや不正なイテレーター範囲を避ける。
    if (file_size < 54 + image_size || offset < file_size - 54 - image_size ||
        palette_size > arg_data.size() - 54 || file_size < palette_size)
        return {std::move(arg_data), true};
    offset -= file_size - 54 - image_size;
    file_size -= static_cast<std::uint32_t>(palette_size);
    const auto corrected_image_size = file_size - 54;
    Bytes corrected;
    corrected.reserve(arg_data.size() - static_cast<std::size_t>(palette_size));
    corrected.insert(corrected.end(), arg_data.begin(), arg_data.begin() + 54);
    corrected.insert(corrected.end(), arg_data.begin() + 54 + static_cast<std::ptrdiff_t>(palette_size), arg_data.end());
    put_le32(corrected, 2, file_size);
    put_le32(corrected, 10, offset);
    put_le32(corrected, 34, corrected_image_size);
    put_le32(corrected, 46, 0);
    return {std::move(corrected), false};
}

::cw::lib::Bytes patch_rle4bitmap(::cw::lib::Bytes arg_data)
{
    if (!bmp40(arg_data) || le32(arg_data, 10) == 0 || le32(arg_data, 30) != 2)
        return arg_data;
    const auto offset = le32(arg_data, 10);
    const auto width = sle32(arg_data, 18);
    const auto raw_height = sle32(arg_data, 22);
    const auto bit_count = le16(arg_data, 28);
    // Py の直接デコードとは異なり、不正な寸法・入力位置を
    // 拒否する。 切り出し範囲と出力確保に使う値なので、バッ
    // ファー外の範囲を作らず元のバイト列を維持する。 後のゼ
    // ロ以下の高さ・行間隔の確認でも、不正な出力寸法を防ぐ。
    if (width < 0 || offset > arg_data.size()) return arg_data;
    const auto height = raw_height < 0 ? -static_cast<std::int64_t>(raw_height) : raw_height;
    const auto bpl = ((static_cast<std::int64_t>(width) * bit_count + 31) / 32) * 4;
    if (height <= 0 || bpl <= 0) return arg_data;
    Bytes encoded(arg_data.begin() + static_cast<std::ptrdiff_t>(offset), arg_data.end());
    auto decoded = ::cw::imageretouch::decode_rle4data(
        std::move(encoded), static_cast<::cw::lib::Integer>(height), static_cast<::cw::lib::Integer>(bpl));
    arg_data.resize(offset);
    arg_data.insert(arg_data.end(), decoded.begin(), decoded.end());
    put_le32(arg_data, 2, static_cast<std::uint32_t>(arg_data.size()));
    put_le32(arg_data, 30, 0);
    put_le32(arg_data, 34, static_cast<std::uint32_t>(decoded.size()));
    return arg_data;
}

std::tuple<::cw::lib::Bytes, bool> patch_rle8bitmap(::cw::lib::Bytes arg_data)
{
    if (!bmp40(arg_data) || le32(arg_data, 10) == 0 || le32(arg_data, 30) != 1)
        return {std::move(arg_data), true};
    const auto offset = le32(arg_data, 10);
    const auto width = sle32(arg_data, 18);
    const auto raw_height = sle32(arg_data, 22);
    const auto bit_count = le16(arg_data, 28);
    // Py の直接デコードとは異なり、不正な寸法・入力位置を
    // 拒否する。 切り出し範囲と出力確保に使う値なので、バッ
    // ファー外の範囲を作らず元のバイト列を維持する。 後のゼ
    // ロ以下の高さ・行間隔の確認でも、不正な出力寸法を防ぐ。
    if (width < 0 || offset > arg_data.size()) return {std::move(arg_data), true};
    const auto height = raw_height < 0 ? -static_cast<std::int64_t>(raw_height) : raw_height;
    const auto bpl = ((static_cast<std::int64_t>(width) * bit_count + 31) / 32) * 4;
    if (height <= 0 || bpl <= 0) return {std::move(arg_data), true};
    Bytes encoded(arg_data.begin() + static_cast<std::ptrdiff_t>(offset), arg_data.end());
    auto decoded = ::cw::imageretouch::decode_rle8data(
        std::move(encoded), static_cast<::cw::lib::Integer>(height), static_cast<::cw::lib::Integer>(bpl));
    arg_data.resize(offset);
    arg_data.insert(arg_data.end(), decoded.begin(), decoded.end());
    put_le32(arg_data, 2, static_cast<std::uint32_t>(arg_data.size()));
    put_le32(arg_data, 30, 0);
    put_le32(arg_data, 34, static_cast<std::uint32_t>(decoded.size()));
    return {std::move(arg_data), false};
}

::cw::lib::Integer get_bmpdepth_view(std::span<const std::uint8_t> arg_data)
{
    // Py と同様、CORE ヘッダーでも先頭 54 バイトを検査に必要とする。
    if (arg_data.size() < 54 || arg_data[0] != 'B' || arg_data[1] != 'M') return 0;
    const auto dib_size = static_cast<std::uint32_t>(arg_data[14]) |
        (static_cast<std::uint32_t>(arg_data[15]) << 8) |
        (static_cast<std::uint32_t>(arg_data[16]) << 16) |
        (static_cast<std::uint32_t>(arg_data[17]) << 24);
    const std::size_t offset = dib_size >= 40 ? 28 : 24;
    return static_cast<::cw::lib::Integer>(arg_data[offset]) |
        (static_cast<::cw::lib::Integer>(arg_data[offset + 1]) << 8);
}

::cw::lib::Integer get_bmpdepth(::cw::lib::Bytes arg_data)
{
    return get_bmpdepth_view(arg_data);
}

std::optional<::cw::lib::Integer> get_bicompression(::cw::lib::Bytes arg_data)
{
    if (arg_data.size() < 54 || arg_data[0] != 'B' || arg_data[1] != 'M') return ::cw::lib::Integer{0};
    if (40 <= le32(arg_data, 14)) return static_cast<::cw::lib::Integer>(sle32(arg_data, 30));
    return std::nullopt;
}

bool has_pngalpha(::cw::lib::Bytes arg_data)
{
    // Py と同じ固定 33 バイトの IHDR 確認を行う。 tRNS の透明度とチ
    // ャンク CRC の検証はデコーダーが担当し、この公開の色形式判定には含めない。
    constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (arg_data.size() < 33 || !std::equal(signature.begin(), signature.end(), arg_data.begin())) return false;
    if (arg_data[8] != 0 || arg_data[9] != 0 || arg_data[10] != 0 || arg_data[11] != 13 ||
        arg_data[12] != 'I' || arg_data[13] != 'H' || arg_data[14] != 'D' || arg_data[15] != 'R') return false;
    return arg_data[25] == 4 || arg_data[25] == 6;
}

void main()
{
}

}

