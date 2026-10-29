// cw/imageretouch.py の C++/SDL 互換実装。
#include "cw/imageretouch.hpp"
#include "app/debug_control_server.hpp"
#include "app/platform_font.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/image.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/font_size_policy.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#if __has_include(<SDL3_ttf/SDL_ttf.h>)
#  include <SDL3_ttf/SDL_ttf.h>
#  define CW_IMAGERETOUCH_HAS_SDL_TTF 1
#else
#  define CW_IMAGERETOUCH_HAS_SDL_TTF 0
#endif

namespace cw::imageretouch {
namespace {
using Integer = ::cw::lib::Integer;
using Surface = ::cw::lib::Surface;
using Bytes = ::cw::lib::Bytes;

struct AdapterState {
    std::mutex mutex;
    ::cw::imageretouch::GlyphRenderAdapter glyph_renderer;
    ::cw::imageretouch::LogicalScaleAdapter logical_scale;
    ::cw::imageretouch::FontFaceAdapter face_resolver;
};

AdapterState& adapter_state()
{
    static AdapterState state;
    return state;
}

::cw::imageretouch::SurfaceMetadata metadata_for(const Surface* surface)
{
    if (!surface) return {};
    return {surface->has_colorkey, surface->colorkey, surface->depth1,
            surface->logical_scale > 0.0 ? surface->logical_scale : 1.0};
}

void copy_metadata(const Surface* source, Surface* destination)
{
    if (!source || !destination || source == destination) return;
    destination->has_colorkey = source->has_colorkey;
    destination->colorkey = source->colorkey;
    destination->depth1 = source->depth1;
    destination->logical_scale = source->logical_scale;
    destination->surface_alpha = source->surface_alpha;
    destination->surface_alpha_enabled = source->surface_alpha_enabled;
    destination->has_pixel_alpha = source->has_pixel_alpha;
    destination->pixel_bits = source->pixel_bits;
    destination->bmpdepthis1 = source->bmpdepthis1;
    destination->scr_scale = source->scr_scale;
}

void store_metadata(Surface* surface, ::cw::imageretouch::SurfaceMetadata metadata)
{
    if (!surface) return;
    surface->has_colorkey = metadata.has_colorkey;
    surface->colorkey = metadata.colorkey;
    surface->depth1 = metadata.depth1;
    surface->logical_scale = metadata.logical_scale > 0.0 ? metadata.logical_scale : 1.0;
}

::cw::imageretouch::GlyphRenderAdapter glyph_renderer()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.glyph_renderer;
}

::cw::imageretouch::LogicalScaleAdapter logical_scale()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.logical_scale;
}

::cw::imageretouch::FontFaceAdapter face_resolver()
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    return state.face_resolver;
}

Integer clamp_channel(Integer value) { return std::clamp<Integer>(value, 0, 255); }

template<typename T>
Integer fcolor_wrap(T value)
{
    const auto converted = static_cast<long double>(value);
    return clamp_channel(static_cast<Integer>((converted + 128.0L) / 256.0L));
}

std::size_t pixel_count(Integer width, Integer height)
{
    if (width <= 0 || height <= 0) return 0;
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h ||
        w * h > std::numeric_limits<std::size_t>::max() / 4) {
        throw std::invalid_argument("imageretouch: image dimensions overflow");
    }
    return w * h;
}

void ensure_rgba(const std::shared_ptr<Surface>& image)
{
    if (!image) return;
    const auto count = pixel_count(image->size.width, image->size.height);
    image->rgba.resize(count * 4, 0);
}

std::shared_ptr<Surface> clone_surface(const std::shared_ptr<Surface>& image)
{
    if (!image) return nullptr;
    auto result = std::make_shared<Surface>(*image);
    ensure_rgba(result);
    copy_metadata(image.get(), result.get());
    return result;
}

std::shared_ptr<::cw::lib::Bitmap> clone_bitmap(const std::shared_ptr<::cw::lib::Bitmap>& image)
{
    if (!image) return nullptr;
    auto result = std::make_shared<::cw::lib::Bitmap>(*image);
    ensure_rgba(result);
    copy_metadata(image.get(), result.get());
    return result;
}

// wx.Image.ConvertToGreyscale() と同様、整数の pygame・画
// 像加工用の灰色化ではなく、浮動小数点の重みと wxRound を使う。 既存のマスクピクセル
// は元の RGB を保持し、Python のバイト処理より前にアルファをそのままコピーする。
std::shared_ptr<::cw::lib::Bitmap> wx_grayscale_bitmap(
    const std::shared_ptr<::cw::lib::Bitmap>& image)
{
    auto result = clone_bitmap(image);
    if (!result) return nullptr;
    const auto metadata = metadata_for(image.get());
    const auto [mask_r, mask_g, mask_b] = metadata.colorkey;
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        const auto r = result->rgba[i], g = result->rgba[i + 1], b = result->rgba[i + 2];
        if (metadata.has_colorkey && r == mask_r && g == mask_g && b == mask_b) continue;
        const auto gray = static_cast<std::uint8_t>(std::lround(r * 0.299 + g * 0.587 + b * 0.114));
        result->rgba[i] = result->rgba[i + 1] = result->rgba[i + 2] = gray;
    }
    return result;
}

std::shared_ptr<::cw::lib::Image> clone_image(const std::shared_ptr<::cw::lib::Image>& image)
{
    if (!image) return nullptr;
    auto result = std::make_shared<::cw::lib::Image>(*image);
    ensure_rgba(result);
    copy_metadata(image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> make_surface(Integer width, Integer height, std::uint8_t r = 0,
                                      std::uint8_t g = 0, std::uint8_t b = 0,
                                      std::uint8_t a = 0)
{
    auto result = std::make_shared<Surface>();
    // 終了処理や一ピクセル未満への縮小で、ネイティブの描画面が空になる場
    // 合がある。 wx はその寸法を拒否するが、ネイティブでは空を維持し
    // 、ピクセルアクセス前に確保サイズと入力バッファーの範囲を検証する。
    result->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    result->rgba.resize(pixel_count(result->size.width, result->size.height) * 4);
    ::cw::lib::simd_image::fill_rgba(result->rgba.data(), result->rgba.size() / 4U,
                                     r, g, b, a);
    return result;
}

std::size_t index_of(const Surface& image, Integer x, Integer y)
{
    return static_cast<std::size_t>((y * image.size.width + x) * 4);
}

bool in_bounds(const Surface& image, Integer x, Integer y)
{
    return x >= 0 && y >= 0 && x < image.size.width && y < image.size.height;
}

std::uint8_t channel(const Surface& image, Integer x, Integer y, int c)
{
    if (!in_bounds(image, x, y)) return 0;
    const auto i = index_of(image, x, y);
    return i + static_cast<std::size_t>(c) < image.rgba.size() ? image.rgba[i + c] : 0;
}

void put_rgb(Surface& image, Integer x, Integer y, Integer r, Integer g, Integer b)
{
    if (!in_bounds(image, x, y)) return;
    const auto i = index_of(image, x, y);
    if (i + 3 >= image.rgba.size()) return;
    image.rgba[i] = static_cast<std::uint8_t>(clamp_channel(r));
    image.rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(g));
    image.rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(b));
}

void put_rgba(Surface& image, Integer x, Integer y, Integer r, Integer g, Integer b, Integer a)
{
    if (!in_bounds(image, x, y)) return;
    const auto i = index_of(image, x, y);
    if (i + 3 >= image.rgba.size()) return;
    image.rgba[i] = static_cast<std::uint8_t>(clamp_channel(r));
    image.rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(g));
    image.rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(b));
    image.rgba[i + 3] = static_cast<std::uint8_t>(clamp_channel(a));
}

void alpha_blit(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source,
                Integer dx0, Integer dy0, Integer sx0 = 0, Integer sy0 = 0,
                Integer width = -1, Integer height = -1)
{
    if (!dest || !source) return;
    ensure_rgba(dest);
    const Integer w = width < 0 ? source->size.width - sx0 : width;
    const Integer h = height < 0 ? source->size.height - sy0 : height;
    for (Integer sy = 0; sy < h; ++sy) for (Integer sx = 0; sx < w; ++sx) {
        const Integer ix = sx0 + sx, iy = sy0 + sy, ox = dx0 + sx, oy = dy0 + sy;
        if (!in_bounds(*source, ix, iy) || !in_bounds(*dest, ox, oy)) continue;
        const auto si = index_of(*source, ix, iy);
        const auto di = index_of(*dest, ox, oy);
        if (si + 3 >= source->rgba.size() || di + 3 >= dest->rgba.size()) continue;
        const Integer sa = source->rgba[si + 3];
        if (sa == 255) {
            std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(si), 4,
                        dest->rgba.begin() + static_cast<std::ptrdiff_t>(di));
        } else if (sa != 0) {
            const Integer da = dest->rgba[di + 3];
            const Integer inv = 255 - sa;
            dest->rgba[di] = static_cast<std::uint8_t>((source->rgba[si] * sa + dest->rgba[di] * inv) / 255);
            dest->rgba[di + 1] = static_cast<std::uint8_t>((source->rgba[si + 1] * sa + dest->rgba[di + 1] * inv) / 255);
            dest->rgba[di + 2] = static_cast<std::uint8_t>((source->rgba[si + 2] * sa + dest->rgba[di + 2] * inv) / 255);
            dest->rgba[di + 3] = static_cast<std::uint8_t>(std::min<Integer>(255, sa + da * inv / 255));
        }
    }
}

void apply_retouch_colorkey(const Surface* source, Surface* result)
{
    if (!source || !result) return;
    auto metadata = metadata_for(source);
    if (!metadata.has_colorkey) return;
    if (result->rgba.size() >= 3) {
        metadata.colorkey = {result->rgba[0], result->rgba[1], result->rgba[2]};
    }
    store_metadata(result, metadata);
    const auto [cr, cg, cb] = metadata.colorkey;
    ::cw::lib::simd_image::colorkey_alpha_zero(
        result->rgba.data(), result->rgba.size() / 4U, cr, cg, cb);
}

void clear_wx_depth1(Surface* surface)
{
    if (!surface) return;
    auto metadata = metadata_for(surface);
    if (!metadata.depth1) return;
    metadata.depth1 = false;
    store_metadata(surface, metadata);
}

bool colorkey_match(const Surface& source, std::size_t index, const SurfaceMetadata& metadata)
{
    if (!metadata.has_colorkey || index + 2 >= source.rgba.size()) return false;
    const auto [cr, cg, cb] = metadata.colorkey;
    return source.rgba[index] == cr && source.rgba[index + 1] == cg && source.rgba[index + 2] == cb;
}

void pygame_blit(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source,
                 Integer dx0, Integer dy0,
                 Integer clip_x0 = 0, Integer clip_y0 = 0, Integer clip_x1 = -1, Integer clip_y1 = -1)
{
    if (!dest || !source) return;
    ensure_rgba(dest);
    ensure_rgba(source);
    if (clip_x1 < 0) clip_x1 = dest->size.width;
    if (clip_y1 < 0) clip_y1 = dest->size.height;
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;
    // Py の通常の Surface.blit と同様、RGB の描画面全体の不透明
    // 度を反映し、pygame の形式別のピクセルアルファ演算を使う。 通常のカード
    // ・メッセージ・wx の代替処理は、既存の共通 SIMD 転送処理にまとめる。
    ::cw::image::blit_surface_pixels(dest, source, dx0, dy0,
        ::cw::lib::Rect{clip_x0, clip_y0, clip_x1 - clip_x0, clip_y1 - clip_y0});
}
void opaque_blit(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source,
                 Integer dx0, Integer dy0,
                 Integer clip_x0, Integer clip_y0, Integer clip_x1, Integer clip_y1)
{
    if (!dest || !source) return;
    ensure_rgba(dest);
    ensure_rgba(source);
    for (Integer sy = 0; sy < source->size.height; ++sy) for (Integer sx = 0; sx < source->size.width; ++sx) {
        const Integer ox = dx0 + sx, oy = dy0 + sy;
        if (ox < clip_x0 || oy < clip_y0 || ox >= clip_x1 || oy >= clip_y1) continue;
        if (!in_bounds(*source, sx, sy) || !in_bounds(*dest, ox, oy)) continue;
        const auto si = index_of(*source, sx, sy);
        const auto di = index_of(*dest, ox, oy);
        if (si + 2 >= source->rgba.size() || di + 3 >= dest->rgba.size()) continue;
        dest->rgba[di] = source->rgba[si];
        dest->rgba[di + 1] = source->rgba[si + 1];
        dest->rgba[di + 2] = source->rgba[si + 2];
        dest->rgba[di + 3] = 255;
    }
}

std::shared_ptr<Surface> smooth_scale_surface(const std::shared_ptr<Surface>& source,
                                              Integer width, Integer height)
{
    if (!source) return nullptr;
    if (source->size.width == width && source->size.height == height) return source;
    auto result = make_surface(width, height);
    if (source->size.width <= 0 || source->size.height <= 0 || source->rgba.empty()) return result;
    const auto sw = source->size.width;
    const auto sh = source->size.height;
    const auto sample = [source, sw, sh](Integer x, Integer y, std::size_t channel) {
        x = std::clamp<Integer>(x, 0, sw - 1);
        y = std::clamp<Integer>(y, 0, sh - 1);
        const auto offset = static_cast<std::size_t>((y * sw + x) * 4) + channel;
        return offset < source->rgba.size() ? static_cast<double>(source->rgba[offset]) : 0.0;
    };
    // Python/pygame との意図的な描画差として、文字
    // の計測寸法と配置は維持しつつ、高解像度の文字を縮小する際は
    // ピクセルの全範囲を積分する。 双線形補間では非整数倍率で被
    // 覆が欠ける場合がある。色をアルファ乗算済みで平均してからアルファ
    // 非乗算の RGBA へ戻し、 透明な黒で明るい文字の縁を暗く
    // しない。Web を含む SDL_ttf と Windows
    // のフォント処理で共有し、平滑化なしの動作は変えない。
    if (sw >= result->size.width && sh >= result->size.height) {
        for (Integer y = 0; y < result->size.height; ++y) {
            const double top = static_cast<double>(y) * sh / result->size.height;
            const double bottom = static_cast<double>(y + 1) * sh / result->size.height;
            for (Integer x = 0; x < result->size.width; ++x) {
                const double left = static_cast<double>(x) * sw / result->size.width;
                const double right = static_cast<double>(x + 1) * sw / result->size.width;
                double alpha = 0.0;
                double colour[3] = {};
                for (Integer sy = static_cast<Integer>(std::floor(top));
                     sy < std::min(sh, static_cast<Integer>(std::ceil(bottom))); ++sy) {
                    const double wy = std::min(bottom, static_cast<double>(sy + 1))
                                    - std::max(top, static_cast<double>(sy));
                    for (Integer sx = static_cast<Integer>(std::floor(left));
                         sx < std::min(sw, static_cast<Integer>(std::ceil(right))); ++sx) {
                        const double weight = wy * (std::min(right, static_cast<double>(sx + 1))
                                                  - std::max(left, static_cast<double>(sx)));
                        const double coverage = weight * sample(sx, sy, 3);
                        alpha += coverage;
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            colour[channel] += coverage * sample(sx, sy, channel);
                    }
                }
                const auto offset = static_cast<std::size_t>((y * result->size.width + x) * 4);
                for (std::size_t channel = 0; channel < 3; ++channel)
                    result->rgba[offset + channel] = static_cast<std::uint8_t>(
                        std::clamp(std::lround(alpha > 0.0 ? colour[channel] / alpha : 0.0), 0L, 255L));
                result->rgba[offset + 3] = static_cast<std::uint8_t>(std::clamp(
                    std::lround(alpha / ((right - left) * (bottom - top))), 0L, 255L));
            }
        }
        return result;
    }
    for (Integer y = 0; y < result->size.height; ++y) {
        const auto sy = (static_cast<double>(y) + 0.5) * sh / result->size.height - 0.5;
        const auto y0 = static_cast<Integer>(std::floor(sy));
        const auto y1 = y0 + 1;
        const auto fy = sy - y0;
        for (Integer x = 0; x < result->size.width; ++x) {
            const auto sx = (static_cast<double>(x) + 0.5) * sw / result->size.width - 0.5;
            const auto x0 = static_cast<Integer>(std::floor(sx));
            const auto x1 = x0 + 1;
            const auto fx = sx - x0;
            const auto offset = static_cast<std::size_t>((y * result->size.width + x) * 4);
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const auto top = sample(x0, y0, channel) * (1.0 - fx) + sample(x1, y0, channel) * fx;
                const auto bottom = sample(x0, y1, channel) * (1.0 - fx) + sample(x1, y1, channel) * fx;
                const auto value = std::clamp(std::lround(top * (1.0 - fy) + bottom * fy), 0L, 255L);
                if (offset + channel < result->rgba.size())
                    result->rgba[offset + channel] = static_cast<std::uint8_t>(value);
            }
        }
    }
    return result;
}

Integer scale_units(Integer value, bool wx)
{
    if (const auto adapter = logical_scale()) return adapter(value);
    const auto scale = wx ? ::cw::g_UP_WIN : ::cw::g_UP_SCR;
    const auto used = (std::isfinite(scale) && scale > 0.0) ? scale : 1.0;
    const auto scaled = static_cast<long double>(value) * static_cast<long double>(used);
    if (!std::isfinite(scaled)) return value;
    if (scaled > static_cast<long double>(std::numeric_limits<Integer>::max()))
        return std::numeric_limits<Integer>::max();
    if (scaled < static_cast<long double>(std::numeric_limits<Integer>::lowest()))
        return std::numeric_limits<Integer>::lowest();
    return static_cast<Integer>(scaled);
}

::cw::lib::String lower_face(::cw::lib::String value)
{
    return ::cw::lib::encoding::python_lower(value);
}

#if defined(__ANDROID__)
std::filesystem::path bundled_android_ttf()
{
    std::error_code filesystem_error;
    const auto fallback =
        std::filesystem::u8path("Data/Font/NotoSansCJKjp-Regular.otf");
    return std::filesystem::is_regular_file(fallback, filesystem_error)
        ? fallback : std::filesystem::path{};
}
#endif

std::filesystem::path resolve_font_file(const ::cw::lib::String& face)
{
    // Resource::set_systemfonttable は、スキン・Data
    // の論理フォントを明示パスとして保持する場合がある。 ここで主フォントとして扱い
    // 、pygame 互換の高さ・行・描画の計測に同じ SDL_ttf の書体を使う。
    const auto direct = ::cw::lib::encoding::path_from_utf8(
        !face.empty() && face.front() == '@' ? face.substr(1) : face);
    std::error_code direct_error;
    if (std::filesystem::is_regular_file(direct, direct_error)) return direct;
    if (!direct.has_extension()) {
        for (const auto* extension : {".ttf", ".ttc", ".otf"}) {
            const auto candidate = ::cw::lib::encoding::path_from_utf8(face + extension);
            direct_error.clear();
            if (std::filesystem::is_regular_file(candidate, direct_error)) return candidate;
        }
    }

    static const std::array<std::pair<std::array<const char*, 2>, const char*>, 5> mapping{{
        {{{"IPAゴシック", "IPAGothic"}}, "gothic.ttf"},
        {{{"IPA UIゴシック", "IPAUIGothic"}}, "uigothic.ttf"},
        {{{"IPA明朝", "IPAMincho"}}, "mincho.ttf"},
        {{{"IPA P明朝", "IPAPMincho"}}, "pmincho.ttf"},
        {{{"IPA Pゴシック", "IPAPGothic"}}, "pgothic.ttf"}}};
    for (const auto& [names, file] : mapping) {
        if (face != names[0] && face != names[1]) continue;
        const auto user_path = std::filesystem::u8path("System/Font") / file;
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_path, filesystem_error)) return user_path;
        const auto path = std::filesystem::u8path("Data/Font") / file;
        filesystem_error.clear();
        if (std::filesystem::is_regular_file(path, filesystem_error)) return path;
        // Web/Linux では UserData のアップロードを必須にせず、従
        // 来のフォント寸法を維持する。 同梱の IPA ファイルは同じ名前を持つ。
        if (const auto bundled = bundled_logical_font(names[1]); !bundled.empty()) return bundled;
    }

    // 共通 Engine は元の Windows/IPA の論理フォント
    // 名を維持するが、最小の Linux/WSL パッケージには Data/Font
    // 内の Windows フォントがない。 Linux の起動用
    // パッケージが実行ファイルの隣に同梱する日本語対応フォントを同じ
    // 論理名に使い、 計測・描画が縞付きの代替枠を描く draw_placeholder_text()
    // へ落ちないようにする。
    auto logical_face = face;
    if (!logical_face.empty() && logical_face.front() == '@') logical_face.erase(logical_face.begin());
    static constexpr std::array<const char*, 20> bundled_face_aliases{{
        "gothic", "uigothic", "mincho", "pgothic", "pmincho",
        "IPAゴシック", "IPA UIゴシック", "IPA明朝", "IPA Pゴシック", "IPA P明朝",
        "IPAGothic", "IPAUIGothic", "IPAMincho", "IPAPGothic", "IPAPMincho",
        "MS Gothic", "MS UI Gothic", "MS Mincho", "MS PGothic", "MS PMincho"}};
    static constexpr std::array<const char*, 5> bundled_face_aliases_japanese{{
        "ＭＳ ゴシック", "ＭＳ ＵＩゴシック", "ＭＳ 明朝", "ＭＳ Ｐゴシック", "ＭＳ Ｐ明朝"}};
    const auto is_bundled_face = [&logical_face] {
        const auto matches = [&logical_face](const auto& aliases) {
            return std::any_of(aliases.begin(), aliases.end(), [&logical_face](const char* alias) {
                return logical_face == alias;
            });
        };
        return matches(bundled_face_aliases) || matches(bundled_face_aliases_japanese);
    };
    bool portable_family = is_bundled_face();
#if !defined(_WIN32)
    // 既知のインストール済みデスクトップ書体は、従来の IPA/MS
    // の別名とは限らない。 別名と同じプラットフォーム境界で解決する。
    portable_family = true;
#endif
    if (portable_family) {
#if defined(_WIN32)
        // Windows の Win32FontSession は、MS 明朝
        // ・MS ゴシックのコレクション内の書体を実際の名前で選べる。 描画側
        // へ届く前にすべての MS 書体を汎用の先頭 TTF へ解決すると、メ
        // ッセージの寸法と太さが CardWirthPy と変わる。 同梱 IPA
        // は上で処理し、ネイティブの MS 書体は GDI に委ねる。
        if (logical_face == "ＭＳ ゴシック" || logical_face == "MS Gothic" ||
            logical_face == "MS UI Gothic" || logical_face == "ＭＳ ＵＩゴシック" ||
            logical_face == "ＭＳ 明朝" || logical_face == "MS Mincho" ||
            logical_face == "ＭＳ Ｐゴシック" || logical_face == "MS PGothic" ||
            logical_face == "ＭＳ Ｐ明朝" || logical_face == "MS PMincho")
            return {};
#endif
        if (const auto platform_path = find_platform_font(logical_face); !platform_path.empty())
            return ::cw::lib::encoding::path_from_utf8(platform_path);
    }
    // Web/Linux では、スキンがホストの書体名を直接
    // 指定してもホストフォントは使わず、両環境の文字を揃える
    // 。 解決できない書体には同梱の日本語フォントを使う。
    if (const auto bundled = bundled_ui_font(); !bundled.empty()) return bundled;
#if defined(__ANDROID__)
    // Android のスキンも Windows/IPA の論理名を保持する場合が
    // 多い。 解決できない全書体に APK 同梱の CJK フォントを使い、どの端
    // 末でも計測・描画が同じ完全な UTF-8・日本語の文字を使えるようにする。
    if (const auto android_fallback = bundled_android_ttf(); !android_fallback.empty())
        return android_fallback;
#endif
    return {};
}

template<typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* ptr = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return ptr && *ptr ? std::dynamic_pointer_cast<T>(*ptr) : nullptr;
}

bool value_bool(const ::cw::lib::Value& value, bool fallback = false)
{
    if (const auto* v = std::get_if<bool>(&value.storage())) return *v;
    return fallback;
}

Integer value_integer(const ::cw::lib::Value& value, Integer fallback = 0)
{
    if (const auto* v = std::get_if<Integer>(&value.storage())) return *v;
    if (const auto* v = std::get_if<double>(&value.storage())) return static_cast<Integer>(*v);
    return fallback;
}

::cw::lib::String value_string(const ::cw::lib::Value& value, ::cw::lib::String fallback = {})
{
    if (const auto* v = std::get_if<::cw::lib::String>(&value.storage())) return *v;
    return fallback;
}

std::vector<std::uint32_t> codepoints(std::string_view text)
{
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80) { result.push_back(c); continue; }
        std::uint32_t cp = 0;
        int extra = 0;
        if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
        else { result.push_back(0xfffd); continue; }
        bool valid = true;
        for (int n = 0; n < extra; ++n) {
            if (i >= text.size()) { valid = false; break; }
            const auto d = static_cast<unsigned char>(text[i]);
            if ((d & 0xc0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (d & 0x3f);
            ++i;
        }
        result.push_back(valid ? cp : 0xfffd);
    }
    return result;
}

Integer glyph_width(std::uint32_t cp, Integer height, bool bold)
{
    if (cp == '\t') return std::max<Integer>(1, height / 2) * 4;
    if (cp == ' ') return std::max<Integer>(1, height / 3);
    const bool wide = cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
                                       (cp >= 0x2e80 && cp <= 0xa4cf) ||
                                       (cp >= 0xac00 && cp <= 0xd7a3) ||
                                       (cp >= 0xf900 && cp <= 0xfaff) ||
                                       (cp >= 0xfe10 && cp <= 0xfe19) ||
                                       (cp >= 0xff01 && cp <= 0xff60));
    const Integer width = wide ? height : std::max<Integer>(1, (height * 3) / 5);
    return width + (bold ? 1 : 0);
}

std::tuple<Integer, Integer> measure_text(::cw::lib::String text, Integer height, bool bold, bool italic)
{
    Integer line_width = 0, max_width = 0, lines = 1;
    for (const auto cp : codepoints(text)) {
        if (cp == '\n') { max_width = std::max(max_width, line_width); line_width = 0; ++lines; }
        else line_width += glyph_width(cp, height, bold);
    }
    max_width = std::max(max_width, line_width);
    if (italic && max_width > 0) ++max_width;
    return {max_width, std::max<Integer>(1, height) * lines};
}

void draw_placeholder_text(Surface& image, ::cw::lib::String text, Integer height, bool bold,
                           bool italic, bool underline, bool antialias,
                           std::tuple<Integer, Integer, Integer> colour)
{
    const auto [red, green, blue] = colour;
    const auto alpha = antialias ? 220 : 255;
    Integer x = 0, y = 0;
    const Integer line_height = std::max<Integer>(1, height);
    for (const auto cp : codepoints(text)) {
        if (cp == '\n') { x = 0; y += line_height; continue; }
        const Integer advance = glyph_width(cp, height, bold);
        if (cp != ' ' && cp != '\t') {
            const Integer slant = italic ? std::max<Integer>(0, (line_height - 2 - y % line_height) / 5) : 0;
            for (Integer yy = 1; yy < line_height - 1; ++yy) {
                for (Integer xx = 1; xx < std::max<Integer>(1, advance - 1); ++xx) {
                    if ((xx == 1 || yy == 1 || xx + 1 == advance - 1 || yy + 1 == line_height - 1 ||
                         ((xx + yy + static_cast<Integer>(cp)) % 7 == 0)))
                        put_rgba(image, x + xx + slant, y + yy, red, green, blue, alpha);
                }
            }
        }
        x += advance;
        if (x >= image.size.width) break;
    }
    if (underline) {
        for (Integer yy = 0; yy < image.size.height; ++yy) {
            if ((yy + 1) % line_height != 0) continue;
            for (Integer xx = 0; xx < image.size.width; ++xx) put_rgba(image, xx, yy, red, green, blue, alpha);
        }
    }
}

int clamp_to_gdi_int(Integer value)
{
    if (value > static_cast<Integer>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (value < static_cast<Integer>(std::numeric_limits<int>::lowest())) return std::numeric_limits<int>::lowest();
    return static_cast<int>(value);
}

Integer doubled_pixels(Integer pixels)
{
    if (pixels > std::numeric_limits<Integer>::max() / 2) return std::numeric_limits<Integer>::max();
    if (pixels < std::numeric_limits<Integer>::lowest() / 2) return std::numeric_limits<Integer>::lowest();
    return pixels * 2;
}

std::shared_ptr<Surface> shift_blit_copies(const std::shared_ptr<Surface>& source, Integer copies)
{
    if (!source || copies <= 1) return source;
    const Integer extra = copies - 1;
    if (source->size.width > std::numeric_limits<Integer>::max() - extra) return source;
    auto result = make_surface(source->size.width + extra, source->size.height, 0, 0, 0, 0);
    for (Integer x = 0; x < copies; ++x) alpha_blit(result, source, x, 0);
    return result;
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const ::cw::lib::String& value)
{
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count) != count)
        return {};
    return result;
}

void ensure_bundled_ipa_fonts()
{
    static std::once_flag once;
    std::call_once(once, [] {
        for (const auto* file : {"gothic.ttf", "uigothic.ttf", "mincho.ttf", "pgothic.ttf", "pmincho.ttf"}) {
            const auto path = std::filesystem::u8path("Data/Font") / file;
            std::error_code filesystem_error;
            if (!std::filesystem::is_regular_file(path, filesystem_error)) continue;
            AddFontResourceExW(path.wstring().c_str(), FR_PRIVATE, nullptr);
        }
    });
}

struct Win32FontSession {
    HDC dc{};
    HFONT font{};
    HGDIOBJ old_font{};
    HBITMAP metric_bitmap{};
    HGDIOBJ old_bitmap{};
    TEXTMETRICW metrics{};
    bool italic{};
    bool ok{};

    Win32FontSession(const ::cw::lib::String& face, Integer pixels, bool bold, bool italic, bool underline,
                     bool wx_extent = false)
    {
        this->italic = italic;
        ensure_bundled_ipa_fonts();
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return;
        if (wx_extent) {
            // wx.ClientDC はカラーの画面に対して計測する。新しいメ
            // モリー DC は 1 ビット画像を選択するため、 同じ HFONT
            // と文字送り幅でも GDI の ABC 幅が変わる。 wx ダイ
            // アログの寸法 API だけで、その画面用の計測方法に合わせる。
            const auto display_dc = GetDC(nullptr);
            if (display_dc) {
                metric_bitmap = CreateCompatibleBitmap(display_dc, 1, 1);
                ReleaseDC(nullptr, display_dc);
            }
            if (!metric_bitmap) return;
            old_bitmap = SelectObject(dc, metric_bitmap);
            if (!old_bitmap || old_bitmap == HGDI_ERROR) { old_bitmap = nullptr; return; }
        }
        const auto wide_face = utf8_to_wide(face);
        font = CreateFontW(clamp_to_gdi_int(pixels), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic ? TRUE : FALSE,
                           // wxPython 4.1.0 と同じ PROOF_QUALITY
                           // と FF_SWISS を使う。 送り幅が同じでも ABC 幅が
                           // 変わるため、ダイアログの寸法は同じフォント生成条件に合わせる。
                           // 既存の pygame 形式の描画セッションの設定は維持する。
                           underline ? TRUE : FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           wx_extent ? PROOF_QUALITY : NONANTIALIASED_QUALITY,
                           wx_extent ? FF_SWISS : 0,
                           wide_face.c_str());
        if (!font) {
            DeleteDC(dc);
            dc = nullptr;
            return;
        }
        old_font = SelectObject(dc, font);
        if (!GetTextMetricsW(dc, &metrics)) return;
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        ok = true;
    }

    Win32FontSession(const Win32FontSession&) = delete;
    Win32FontSession& operator=(const Win32FontSession&) = delete;

    ~Win32FontSession()
    {
        if (dc && font) SelectObject(dc, old_font);
        if (font) DeleteObject(font);
        if (dc && old_bitmap) SelectObject(dc, old_bitmap);
        if (metric_bitmap) DeleteObject(metric_bitmap);
        if (dc) DeleteDC(dc);
    }

    std::tuple<Integer, Integer> measure(const ::cw::lib::String& text, bool overhang, bool clamp_min_one) const
    {
        if (!ok) return {clamp_min_one ? 1 : 0, clamp_min_one ? 1 : 0};
        const auto wide = utf8_to_wide(text);
        SIZE size{};
        GetTextExtentPoint32W(dc, wide.data(), static_cast<int>(wide.size()), &size);
        if (overhang && italic && !wide.empty()) {
            ABC abc{};
            // Py の元の C ビルドは UNICODE を定義しないため、文字が wchar_t
            // でも汎用の GetCharABCWidths は A 版になる。 その斜
            // 体のはみ出し寸法を維持し、文字描画自体は Unicode のままにする。
            if (GetCharABCWidthsA(dc, wide.back(), wide.back(), &abc)) {
                size.cx += static_cast<LONG>(std::abs(abc.abcA) + std::abs(abc.abcC));
            }
        }
        Integer width = static_cast<Integer>(size.cx);
        Integer height = static_cast<Integer>(size.cy);
        if (clamp_min_one) {
            width = std::max<Integer>(1, width);
            height = std::max<Integer>(1, height);
        }
        return {width, height};
    }

    std::tuple<Integer, Integer> measure_wx(const ::cw::lib::String& text) const
    {
        auto [width,height]=measure(text,false,false);
        const auto wide=utf8_to_wide(text);
        // 後の v3.1.4 タグではなく、wxPython 4.1.0 が固定した wxWidgets e803408 に合
        // わせる。 その wxTextMeasure は通常のフォントでも境界の負の余白を補正する。ゲームの送り幅・斜体寸法は
        // 変更しない。 https://github.com/wxWidgets/wxWidgets/blob/e803408058186a7a9a61c456246f145abcaccd13/src/msw/textmeasure.cpp
        //
        if (ok && !wide.empty()) {
            ABC abc{};
            if (GetCharABCWidthsW(dc,wide.front(),wide.front(),&abc)) {
                if (abc.abcA<0) width-=abc.abcA;
                if (wide.size()>1)
                    GetCharABCWidthsW(dc,wide.back(),wide.back(),&abc);
                if (abc.abcC<0) width-=abc.abcC;
            }
        }
        return {width,height};
    }

    Integer line_height() const { return ok ? static_cast<Integer>(metrics.tmHeight) : 1; }

    std::shared_ptr<Surface> render(const ::cw::lib::String& text,
                                    std::tuple<Integer, Integer, Integer> colour) const
    {
        if (!ok) return {};
        const auto wide = utf8_to_wide(text);
        if (wide.empty()) return make_surface(1, 1, 0, 0, 0, 0);
        const auto [width, height] = measure(text, true, true);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = clamp_to_gdi_int(width);
        info.bmiHeader.biHeight = -clamp_to_gdi_int(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) {
            if (bitmap) DeleteObject(bitmap);
            return {};
        }
        const HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        std::memset(bits, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        TabbedTextOutW(dc, 0, 0, wide.data(), static_cast<int>(wide.size()), 0, nullptr, 0);
        auto result = make_surface(width, height, 0, 0, 0, 0);
        const auto* source = static_cast<const std::uint8_t*>(bits);
        const auto [red, green, blue] = colour;
        for (std::size_t offset = 0; offset + 3 < result->rgba.size(); offset += 4) {
            result->rgba[offset] = static_cast<std::uint8_t>(clamp_channel(red));
            result->rgba[offset + 1] = static_cast<std::uint8_t>(clamp_channel(green));
            result->rgba[offset + 2] = static_cast<std::uint8_t>(clamp_channel(blue));
            result->rgba[offset + 3] = source[offset];
        }
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        return result;
    }
};

std::shared_ptr<Surface> render_win32_text(const ::cw::lib::String& face, Integer pixels,
                                           bool bold, bool italic, bool underline,
                                           const ::cw::lib::String& text,
                                           std::tuple<Integer, Integer, Integer> colour,
                                           bool antialias, bool sbold)
{
    Win32FontSession session(face, pixels, bold, italic, underline);
    if (!session.ok) return {};
    if (antialias) {
        Win32FontSession session2x(face, doubled_pixels(pixels), bold, italic, underline);
        if (!session2x.ok) return {};
        auto image = session2x.render(text, colour);
        if (!image) return {};
        auto [width, height] = session.measure(text, true, true);
        if (sbold) {
            image = shift_blit_copies(image, 4);
            if (width < std::numeric_limits<Integer>::max()) ++width;
        }
        return smooth_scale_surface(image, width, height);
    }
    auto image = session.render(text, colour);
    if (image && sbold) image = shift_blit_copies(image, 2);
    return image;
}
#endif

#if CW_IMAGERETOUCH_HAS_SDL_TTF
bool ttf_ready()
{
    static std::once_flag init_once;
    static bool initialized = false;
    std::call_once(init_once, [] { initialized = TTF_Init(); });
    return initialized;
}

using TtfFont = std::shared_ptr<TTF_Font>;

TtfFont open_styled_ttf(const std::filesystem::path& path, Integer pixels, bool bold, bool italic, bool underline)
{
    if (!ttf_ready()) return {};
    const auto utf8_path = ::cw::lib::encoding::path_to_utf8(path);
    pixels = std::max<Integer>(1, pixels);
    TTF_FontStyleFlags style = TTF_STYLE_NORMAL;
    if (bold) style |= TTF_STYLE_BOLD;
    if (italic) style |= TTF_STYLE_ITALIC;
    if (underline) style |= TTF_STYLE_UNDERLINE;
#if defined(__EMSCRIPTEN__)
    // 不変のプログラム同梱フォントだけをキャッシュし、UserData のフォントは通常の再読
    // み込み動作を維持する。 キャッシュがないと、MEMFS に読み込み済みでも FreeType
    // の小さな読み取り・シークごとにワーカーとメインスレッド間の RPC が発生する。
    if (utf8_path == "/program/NotoSansCJKjp-Regular.otf" ||
        utf8_path == "/program/Font/gothic.ttf" ||
        utf8_path == "/program/Font/uigothic.ttf" ||
        utf8_path == "/program/Font/mincho.ttf" ||
        utf8_path == "/program/Font/pgothic.ttf" ||
        utf8_path == "/program/Font/pmincho.ttf") {
        struct ProgramFontCache {
            struct Entry { std::string path; Integer pixels; TTF_FontStyleFlags style; TtfFont font; };
            struct Buffer { std::shared_ptr<void> bytes; std::size_t size{}; };
            std::map<std::string, Buffer> buffers;
            std::list<Entry> fonts;
        };
        // SDL_ttf のフォントオブジェクトはスレッドに結び付くため、ホスト内の
        // Engine と ALL の描画側でハンドルを共有しない。 キャッシュか
        // ら除外されても呼び出し側がフォントを持つ間は、そのバッファーを保持する。
        thread_local ProgramFontCache cache;
        for (auto it = cache.fonts.begin(); it != cache.fonts.end(); ++it) {
            if (it->path != utf8_path || it->pixels != pixels || it->style != style) continue;
            auto font = it->font;
            cache.fonts.splice(cache.fonts.begin(), cache.fonts, it);
            return font;
        }
        auto& buffer = cache.buffers[utf8_path];
        if (!buffer.bytes) buffer.bytes.reset(SDL_LoadFile(utf8_path.c_str(), &buffer.size), SDL_free);
        if (!buffer.bytes) return {};
        auto* io = SDL_IOFromConstMem(buffer.bytes.get(), buffer.size);
        if (!io) return {};
        auto* opened = TTF_OpenFontIO(io, true, static_cast<float>(pixels));
        if (!opened) return {};
        TtfFont font(opened, [bytes = buffer.bytes](TTF_Font* value) { TTF_CloseFont(value); });
        TTF_SetFontStyle(font.get(), style);
        cache.fonts.push_front({utf8_path, pixels, style, font});
        if (cache.fonts.size() > 16) cache.fonts.pop_back();
        return font;
    }
#endif
    // 性能のため、開いた書体をスレッドごとの小さな LRU に保持する
    // 。Python も pygame の Font オブジェクトを保
    // 持する。 size()/render() ごとに TTF_OpenFont
    // を呼ぶと、説明文の折り返しや各ラベルの計測で何度もファイル
    // を開くことになる。 SDL_ttf のハンドルはスレッド間で
    // 共有せず、キーにファイルサイズと更新時刻を含め、 置き換え
    // られた UserData のフォントは従来どおり開き直す。
    struct FileFontCache {
        struct Entry {
            std::string path; Integer pixels; TTF_FontStyleFlags style;
            std::uintmax_t bytes; std::filesystem::file_time_type time; TtfFont font;
        };
        std::list<Entry> fonts;
    };
    thread_local FileFontCache file_cache;
    std::error_code stat_error;
    const auto file_bytes = std::filesystem::file_size(path, stat_error);
    const auto file_time = stat_error ? std::filesystem::file_time_type{}
                                      : std::filesystem::last_write_time(path, stat_error);
    if (!stat_error) {
        for (auto it = file_cache.fonts.begin(); it != file_cache.fonts.end(); ++it) {
            if (it->path != utf8_path || it->pixels != pixels || it->style != style ||
                it->bytes != file_bytes || it->time != file_time) continue;
            auto font = it->font;
            file_cache.fonts.splice(file_cache.fonts.begin(), file_cache.fonts, it);
            return font;
        }
    }
    // キャッシュした書体は、終了時の thread_local の破棄で TTF_Quit よ
    // り後まで残る場合がある。 その時は FreeType が解放済みなので、二重に閉じない。
    TtfFont font(TTF_OpenFont(utf8_path.c_str(), static_cast<float>(pixels)),
                 [](TTF_Font* value) { if (TTF_WasInit() > 0) TTF_CloseFont(value); });
    if (!font) return {};
    TTF_SetFontStyle(font.get(), style);
    if (!stat_error) {
        file_cache.fonts.push_front({utf8_path, pixels, style, file_bytes, file_time, font});
        if (file_cache.fonts.size() > 32) file_cache.fonts.pop_back();
    }
    return font;
}

Integer pygame_ttf_pixels(const std::filesystem::path& path, Integer stored)
{
    Integer pixels = stored < 0
        ? (stored == std::numeric_limits<Integer>::min() ? std::numeric_limits<Integer>::max() / 2 : -stored) + 1
        : stored + 1;
    if (pixels < 1) pixels = 1;
    if (stored >= 0) return pixels;
    auto font = open_styled_ttf(path, pixels, false, false, false);
    if (!font) return pixels;
    const int height = TTF_GetFontHeight(font.get());
    if (pixels < height && height != 0) {
        pixels = static_cast<Integer>(static_cast<double>(pixels) / static_cast<double>(height) *
                                      static_cast<double>(pixels));
        if (pixels < 1) pixels = 1;
    }
    return pixels;
}

std::shared_ptr<Surface> copy_ttf_surface(SDL_Surface* rendered)
{
    if (!rendered) return {};
    // 単色の文字画像はインデックス形式で透過色を持つ。 ピクセルを読む
    // 前に透過色をアルファへ変換し、背景が不透明にならないようにする。
    const bool keyed = SDL_SurfaceHasColorKey(rendered);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
        keyed || rendered->format != SDL_PIXELFORMAT_RGBA32
            ? SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32) : nullptr,
        SDL_DestroySurface);
    if (keyed && !rgba) return {};
    if (rgba) rendered = rgba.get();
    auto result = make_surface(rendered->w, rendered->h, 0, 0, 0, 0);
    // RGBA32 は Surface の R,G,B,A のバイト順と同じなので、 性能のためピクセ
    // ルごとの SDL_ReadSurfacePixel ではなく行全体をコピーする。値は変わらない。
    if (rendered->format == SDL_PIXELFORMAT_RGBA32 && rendered->pixels &&
        (!SDL_MUSTLOCK(rendered) || SDL_LockSurface(rendered))) {
        const auto row_bytes = static_cast<std::size_t>(result->size.width) * 4U;
        for (Integer y = 0; y < result->size.height; ++y)
            std::memcpy(result->rgba.data() + static_cast<std::size_t>(y) * row_bytes,
                        static_cast<const std::uint8_t*>(rendered->pixels) +
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(rendered->pitch),
                        row_bytes);
        if (SDL_MUSTLOCK(rendered)) SDL_UnlockSurface(rendered);
        return result;
    }
    for (Integer y = 0; y < result->size.height; ++y) {
        for (Integer x = 0; x < result->size.width; ++x) {
            Uint8 rr{}, gg{}, bb{}, aa{};
            if (!SDL_ReadSurfacePixel(rendered, static_cast<int>(x), static_cast<int>(y), &rr, &gg, &bb, &aa))
                continue;
            put_rgba(*result, x, y, rr, gg, bb, aa);
        }
    }
    return result;
}

std::tuple<Integer, Integer> ttf_measure(const std::filesystem::path& path, Integer stored, bool bold, bool italic,
                                         bool underline, const ::cw::lib::String& text)
{
    auto font = open_styled_ttf(path, pygame_ttf_pixels(path, stored), bold, italic, underline);
    if (!font) return {0, 0};
    int width = 0, height = 0;
    TTF_GetStringSize(font.get(), text.c_str(), text.size(), &width, &height);
    return {width, height};
}

Integer ttf_linesize(const std::filesystem::path& path, Integer stored, bool bold, bool italic, bool underline)
{
    auto font = open_styled_ttf(path, pygame_ttf_pixels(path, stored), bold, italic, underline);
    if (!font) return 1;
    const Integer skip = TTF_GetFontLineSkip(font.get());
    return std::max<Integer>(1, skip);
}

Integer ttf_height(const std::filesystem::path& path, Integer stored, bool bold, bool italic, bool underline)
{
    auto font = open_styled_ttf(path, pygame_ttf_pixels(path, stored), bold, italic, underline);
    if (!font) return 1;
    const Integer height = TTF_GetFontHeight(font.get());
    return std::max<Integer>(1, height);
}

std::shared_ptr<Surface> render_ttf_pygame_style(const std::filesystem::path& path, Integer stored, bool bold,
                                                 bool italic, bool underline, const ::cw::lib::String& text,
                                                 bool antialias, std::tuple<Integer, Integer, Integer> colour,
                                                 bool sbold)
{
    const Integer pixels = pygame_ttf_pixels(path, stored);
    const auto [r, g, b] = colour;
    const SDL_Color foreground{static_cast<Uint8>(clamp_channel(r)),
                               static_cast<Uint8>(clamp_channel(g)),
                               static_cast<Uint8>(clamp_channel(b)), 255};
    auto render_at = [&](Integer size, bool aa) -> std::shared_ptr<Surface> {
        auto font = open_styled_ttf(path, size, bold, italic, underline);
        if (!font) return {};
        // Python の Font.render(..., False) と同様、単色の
        // 文字描画を使う。 平滑化した画像の薄い縁をすべて不透明に変える方式にはしない。
        auto* rendered = aa
            ? TTF_RenderText_Blended(font.get(), text.c_str(), text.size(), foreground)
            : TTF_RenderText_Solid(font.get(), text.c_str(), text.size(), foreground);
        if (!rendered) return make_surface(1, 1, 0, 0, 0, 0);
        auto result = copy_ttf_surface(rendered);
        SDL_DestroySurface(rendered);
        return result;
    };
    if (antialias) {
        auto image = render_at(doubled_pixels(pixels), true);
        if (!image) return {};
        auto [width, height] = ttf_measure(path, stored, bold, italic, underline, text);
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        if (sbold) {
            image = shift_blit_copies(image, 4);
            if (width < std::numeric_limits<Integer>::max()) ++width;
        }
        return smooth_scale_surface(image, width, height);
    }
    auto image = render_at(pixels, false);
    if (image && sbold) image = shift_blit_copies(image, 2);
    return image;
}

#endif

std::shared_ptr<Surface> callback_retouch(const ::cw::lib::Callback& func,
                                          const std::shared_ptr<Surface>& image,
                                          const ::cw::lib::Arguments& extra)
{
    if (!image) return nullptr;
    if (image->size.width == 0 || image->size.height == 0) return clone_surface(image);
    auto image_size = std::make_shared<::cw::data::VariantSequence>();
    image_size->m_values = {image->size.width, image->size.height};
    ::cw::lib::Arguments args;
    args.emplace_back(image->rgba);
    args.emplace_back(std::static_pointer_cast<::cw::lib::Object>(image_size));
    args.insert(args.end(), extra.begin(), extra.end());
    const auto value = func(args, {});
    if (const auto* bytes = std::get_if<Bytes>(&value.storage())) {
        if (bytes->size() != image->rgba.size())
            throw std::invalid_argument("retouch callback returned an invalid RGBA buffer size");
        auto result = clone_surface(image);
        result->rgba = *bytes;
        apply_retouch_colorkey(image.get(), result.get());
        return result;
    }
    // ネイティブ拡張として、コールバックはバイト列の代わりに Surface
    // を返せる。 入力は RGBA バイト列と寸法のま
    // ま維持し、例外を伝播させ、別の引数で再試行しない。
    if (auto result = object_value<Surface>(value)) {
        result = clone_surface(result);
        apply_retouch_colorkey(image.get(), result.get());
        return result;
    }
    throw std::invalid_argument("retouch callback must return RGBA bytes or a Surface");
}

Integer positive_font_pixels(Integer pixels)
{
    if (pixels == std::numeric_limits<Integer>::min()) return std::numeric_limits<Integer>::max() / 2;
    return std::max<Integer>(1, pixels < 0 ? -pixels + 1 : pixels + 1);
}

} // namespace

void Font::initialize(::cw::lib::String arg_face, ::cw::lib::Integer arg_pixels, bool arg_bold, bool arg_italic)
{
    dispose();
    arg_face = get_fontface(std::move(arg_face));
    m_face = arg_face;
    m_pixels = arg_pixels;
    m_bold = arg_bold;
    m_italic = arg_italic;
    m_underline = false;
    const auto [font, font2x, font_notitalic] = _create_mfont(arg_face, get_raster_pixels(), arg_bold, arg_italic, true);
    m_font = std::static_pointer_cast<::cw::lib::Object>(font);
    m_font2x = std::static_pointer_cast<::cw::lib::Object>(font2x);
    m_font_notitalic = std::static_pointer_cast<::cw::lib::Object>(font_notitalic);
    m_fontinfo = {};
    m_fontinfo2x = {};
}

bool Font::_is_cachable(::cw::lib::String arg_s)
{
    const auto cps = codepoints(arg_s);
    if (cps.size() != 1) return false;
    const auto cp = cps.front();
    return (cp >= 0x3041 && cp <= 0x30f6) || cp <= 0xff || (cp >= 0xff01 && cp <= 0xff9f);
}

void Font::dispose()
{
    m_font = {};
    m_font2x = {};
    m_font_notitalic = {};
    m_fontinfo = {};
    m_fontinfo2x = {};
    m_raster_source = RasterSource::Unresolved;
    m_raster_file.clear();
    m__cache.clear();
}

void Font::initialize_wx(::cw::lib::String arg_face, Integer arg_pixels, bool arg_bold, bool arg_italic)
{
    initialize(std::move(arg_face),arg_pixels,arg_bold,arg_italic);
#if defined(_WIN32)
    // wx/MSW は同梱 IPA でも選択した HFONT を GDI で計
    // 測する。 pygame.Font は同じ書体に SDL_ttf を使える
    // が、既存のゲームフォント経路は維持し、ダイアログ用の計測方法だけを選ぶ。
    m_raster_source = RasterSource::Native;
    m_raster_file.clear();
#endif
}

void Font::py_del() { dispose(); }

bool Font::get_bold() { return value_bool(m_bold); }

void Font::set_bold(bool arg_v)
{
    m_bold = arg_v;
    m__cache.clear();
}

bool Font::get_italic() { return value_bool(m_italic); }

void Font::set_italic(bool arg_v)
{
    m_italic = arg_v;
    m__cache.clear();
}

bool Font::get_underline() { return m_underline; }

void Font::set_underline(bool arg_v)
{
    m_underline = arg_v;
    m__cache.clear();
}

void Font::select_raster_source()
{
    if (m_raster_source != RasterSource::Unresolved) return;
    // size()・height・render() で別々に解決せず、Font ごとに一度だけ解決
    // する。 不正ファイルで計測は TTF、描画はネイティブ・代替画像という不一致を起こさない。
    m_raster_source = RasterSource::Placeholder;
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (const auto path = resolve_font_file(value_string(m_face)); !path.empty()) {
        if (auto probe = open_styled_ttf(path, 16, false, false, false)) {
            m_raster_file = ::cw::lib::encoding::path_to_utf8(path);
            m_raster_source = RasterSource::TrueType;
            return;
        }
    }
#endif
#if defined(_WIN32)
    // GDI は実際のフォントを開く時にネイティブの代替書体を選ぶ。 この
    // 境界を分類するだけのために、余分な GDI フォントを確保しない。
    m_raster_source = RasterSource::Native;
#endif
}

Integer Font::get_raster_pixels() const
{
    // ゲーム文字とダイアログ文字は、全ホストで同じ共通境界を使う。 高解像度・複
    // 製フォントが要求値を再利用するため、m_pixels 自体は拡縮しない。
    return ::cw::lib::font_size::raster_pixels(value_integer(m_pixels, 16));
}

Integer Font::get_height()
{
    select_raster_source();
    const auto pixels = get_raster_pixels();
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (m_raster_source == RasterSource::TrueType)
        return ttf_height(::cw::lib::encoding::path_from_utf8(m_raster_file),
                          pixels, get_bold(), get_italic(), m_underline);
#endif
    // Python のネイティブ Font.get_height() と同様、要求された文字高を返す。
    // get_linesize()/size() はネイティブのセル寸法を持ち、同じ値の別名ではない。
    if (pixels == std::numeric_limits<Integer>::min()) return std::numeric_limits<Integer>::max();
    const auto height = pixels < 0 ? -pixels : pixels;
    return m_raster_source == RasterSource::Native ? height : std::max<Integer>(1, height);
}

Integer Font::get_linesize()
{
    select_raster_source();
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (m_raster_source == RasterSource::TrueType)
        return ttf_linesize(::cw::lib::encoding::path_from_utf8(m_raster_file),
                            get_raster_pixels(), get_bold(), get_italic(), m_underline);
#endif
#if defined(_WIN32)
    if (m_raster_source == RasterSource::Native) {
        Win32FontSession session(value_string(m_face), get_raster_pixels(), get_bold(), get_italic(), m_underline);
        if (session.ok) return std::max<Integer>(1, session.line_height());
    }
#endif
    return get_height();
}

std::tuple<Integer, Integer> Font::size(::cw::lib::String arg_text)
{
    select_raster_source();
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (m_raster_source == RasterSource::TrueType)
        return ttf_measure(::cw::lib::encoding::path_from_utf8(m_raster_file),
                           get_raster_pixels(), get_bold(), get_italic(), m_underline, arg_text);
#endif
#if defined(_WIN32)
    if (m_raster_source == RasterSource::Native) {
        Win32FontSession session(value_string(m_face), get_raster_pixels(), get_bold(), get_italic(), m_underline);
        if (session.ok) return session.measure(arg_text, true, true);
    }
#endif
    return measure_text(arg_text, get_height(), get_bold(), get_italic());
}

// Python の _create_mfont は font_notitalic
// にも斜体指定を渡す。 共通の TrueType・代替経路では意図的に
// 直立の書体を計測し、斜体のはみ出しで隣の文字の配置幅を広げない。 ネ
// イティブの経路は描画 API からはみ出しを除いた送り幅を取得する。
std::tuple<Integer, Integer> Font::size_withoutoverhang(::cw::lib::String arg_text)
{
    select_raster_source();
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (m_raster_source == RasterSource::TrueType)
        return ttf_measure(::cw::lib::encoding::path_from_utf8(m_raster_file),
                           get_raster_pixels(), get_bold(), false, m_underline, arg_text);
#endif
#if defined(_WIN32)
    if (m_raster_source == RasterSource::Native) {
        Win32FontSession session(value_string(m_face), get_raster_pixels(), get_bold(), get_italic(), m_underline);
        if (session.ok) return session.measure(arg_text, false, false);
    }
#endif
    return measure_text(arg_text, get_height(), get_bold(), false);
}

std::tuple<Integer, Integer> Font::wx_text_extent(::cw::lib::String arg_text)
{
    select_raster_source();
#if defined(_WIN32)
    if (m_raster_source == RasterSource::Native) {
        Win32FontSession session(value_string(m_face),get_raster_pixels(),get_bold(),get_italic(),m_underline,true);
        if (session.ok) return session.measure_wx(arg_text);
    }
#endif
    // 他の描画バックエンドは size() で外接寸法を公開する。 ホスト
    // のフォント処理が異なっても、このダイアログ API は共通に保つ。
    return size(std::move(arg_text));
}

std::shared_ptr<Surface> Font::render(::cw::lib::String arg_text, bool arg_antialias,
                                     std::tuple<Integer, Integer, Integer> arg_colour)
{
    return _render_impl(std::move(arg_text), arg_antialias, arg_colour, false);
}

std::shared_ptr<Surface> Font::render_sbold(::cw::lib::String arg_text, bool arg_antialias,
                                            std::tuple<Integer, Integer, Integer> arg_colour)
{
    return _render_impl(std::move(arg_text), arg_antialias, arg_colour, true);
}

std::shared_ptr<Surface> Font::_render_impl(::cw::lib::String arg_text, bool arg_antialias,
                                            std::tuple<Integer, Integer, Integer> arg_colour, bool arg_sbold)
{
    const bool cachable = _is_cachable(arg_text);
    const auto [r, g, b] = arg_colour;
    // Py と同様、render と render_sbold は text・antialias・colour
    // のキャッシュを共有する。 同じキーのキャッシュ画像は、最初に描いた時の見た目を維持する。
    const auto key = arg_text + (arg_antialias ? "|1|" : "|0|") + std::to_string(r) + "," +
                     std::to_string(g) + "," + std::to_string(b);
    if (cachable) {
        if (const auto it = m__cache.find(key); it != m__cache.end()) {
            if (auto cached = object_value<Surface>(it->second)) return clone_surface(cached);
        }
    }
    select_raster_source();
    std::shared_ptr<Surface> result;
    bool sbold_applied = false;
    if (const auto adapter = glyph_renderer()) {
        result = adapter(value_string(m_face), get_height(), get_bold(), get_italic(), m_underline,
                         arg_text, arg_antialias, arg_colour);
    }
#if CW_IMAGERETOUCH_HAS_SDL_TTF
    if (!result && m_raster_source == RasterSource::TrueType) {
        result = render_ttf_pygame_style(::cw::lib::encoding::path_from_utf8(m_raster_file),
            get_raster_pixels(), get_bold(), get_italic(), m_underline,
            arg_text, arg_antialias, arg_colour, arg_sbold);
        if (result) sbold_applied = true;
    }
#endif
#if defined(_WIN32)
    if (!result && m_raster_source == RasterSource::Native) {
        result = render_win32_text(value_string(m_face), get_raster_pixels(), get_bold(), get_italic(),
                                   m_underline, arg_text, arg_colour, arg_antialias, arg_sbold);
        if (result) sbold_applied = true;
    }
#endif
    if (!result) {
        const auto [width0, height] = size(arg_text);
        result = make_surface(width0, height, 0, 0, 0, 0);
        draw_placeholder_text(*result, arg_text, get_height(), get_bold(), get_italic(), m_underline,
                              arg_antialias, arg_colour);
    }
    if (arg_sbold && !sbold_applied) {
        result = shift_blit_copies(result, 2);
    }
    if (cachable) m__cache[key] = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(clone_surface(result)));
    return result;
}

::cw::lib::Value g__RetouchArg1{};
::cw::lib::Value g__RetouchArg2{};
::cw::lib::Value g__RetouchArg3{};

std::shared_ptr<Surface> _retouch(::cw::lib::Callback arg_func, std::shared_ptr<Surface> arg_image)
{
    return callback_retouch(arg_func, arg_image, {});
}

std::shared_ptr<Surface> _retouch_2(::cw::lib::Callback arg_func, std::shared_ptr<Surface> arg_image,
                                    ::cw::lib::Value arg_py_arg1)
{
    return callback_retouch(arg_func, arg_image, {std::move(arg_py_arg1)});
}

std::shared_ptr<Surface> _retouch_3(::cw::lib::Callback arg_func, std::shared_ptr<Surface> arg_image,
                                    ::cw::lib::Value arg_py_arg1, ::cw::lib::Value arg_py_arg2)
{
    return callback_retouch(arg_func, arg_image, {std::move(arg_py_arg1), std::move(arg_py_arg2)});
}

std::shared_ptr<Surface> _retouch_4(::cw::lib::Callback arg_func, std::shared_ptr<Surface> arg_image,
                                    ::cw::lib::Value arg_py_arg1, ::cw::lib::Value arg_py_arg2,
                                    ::cw::lib::Value arg_py_arg3)
{
    return callback_retouch(arg_func, arg_image,
                            {std::move(arg_py_arg1), std::move(arg_py_arg2), std::move(arg_py_arg3)});
}

std::shared_ptr<Surface> _retouch_5(::cw::lib::Callback arg_func, std::shared_ptr<Surface> arg_image,
                                    const ::cw::lib::Arguments& arg_args)
{
    return callback_retouch(arg_func, arg_image, arg_args);
}

std::shared_ptr<Surface> to_negative(std::shared_ptr<Surface> arg_image)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    ::cw::lib::simd_image::invert_rgb(result->rgba.data(), result->rgba.size() / 4U);
    return result;
}

std::shared_ptr<Surface> to_negative_for_card(std::shared_ptr<Surface> arg_image, Integer arg_framewidth)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const Integer inset = scale_units(arg_framewidth, false);
    const Integer min_size = scale_units(1 + arg_framewidth * 2, false);
    const Integer shrink = scale_units(arg_framewidth * 2, false);
    if (result->size.width < min_size || result->size.height < min_size) return result;
    const Integer x1 = inset + std::max<Integer>(0, result->size.width - shrink);
    const Integer y1 = inset + std::max<Integer>(0, result->size.height - shrink);
    const Integer right = std::min<Integer>(x1, result->size.width);
    const Integer bottom = std::min<Integer>(y1, result->size.height);
    for (Integer y = inset; y < bottom; ++y)
        ::cw::lib::simd_image::invert_rgb(
            result->rgba.data() + index_of(*result, inset, y),
            static_cast<std::size_t>(std::max<Integer>(0, right - inset)));
    return result;
}

std::shared_ptr<::cw::lib::Bitmap> to_negative_for_wxcard(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp,
                                                            Integer arg_framewidth)
{
    auto result = clone_bitmap(arg_wxbmp);
    if (!result) return nullptr;
    clear_wx_depth1(result.get());
    // Py は empty_bitmap の不透明な 24 ビット画像
    // へ描く。wx は反転用にアルファを取り除く時、入力のアルファ乗算
    // 済み RGB を公開する。 既存の SIMD 合成でその黒背景の
    // 画像を再現し、内側だけを反転する。入力のアルファは保持しない。
    if (result->has_pixel_alpha) {
        const auto source = result->rgba;
        ::cw::lib::simd_image::fill_rgba(result->rgba.data(), result->rgba.size() / 4U, 0, 0, 0, 255);
        ::cw::lib::simd_image::composite_straight_alpha(source.data(), result->rgba.data(),
                                                     result->rgba.size() / 4U, false);
    } else {
        ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
    }
    result->has_pixel_alpha = false;
    result->has_colorkey = false;
    result->pixel_bits = 24;
    result->known_opaque = true;
    const Integer inset = scale_units(arg_framewidth, true);
    const Integer min_size = scale_units(1 + arg_framewidth * 2, true);
    const Integer shrink = scale_units(arg_framewidth * 2, true);
    if (result->size.width < min_size || result->size.height < min_size) return result;
    const Integer x1 = inset + std::max<Integer>(0, result->size.width - shrink);
    const Integer y1 = inset + std::max<Integer>(0, result->size.height - shrink);
    const Integer right = std::min<Integer>(x1, result->size.width);
    const Integer bottom = std::min<Integer>(y1, result->size.height);
    for (Integer y = inset; y < bottom; ++y)
        ::cw::lib::simd_image::invert_rgb(
            result->rgba.data() + index_of(*result, inset, y),
            static_cast<std::size_t>(std::max<Integer>(0, right - inset)));
    return result;
}

std::shared_ptr<Surface> add_lightness(std::shared_ptr<Surface> arg_image, Integer arg_value)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const Integer value = std::clamp<Integer>(arg_value, -255, 255);
    // Python と同様、両極端の値では Surface.convert() を
    // 使う。 Engine の表示形式は不透明 RGB なのでピクセルアルファを
    // 捨てるが、元の透過色は維持する。出力 RGB から新しい透過色は求めない。
    const bool converted = value == -255 || value == 255;
    const auto metadata = metadata_for(result.get());
    if (converted) result->known_opaque = !metadata.has_colorkey;
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        result->rgba[i] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i] + value));
        result->rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i + 1] + value));
        result->rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i + 2] + value));
        if (converted) result->rgba[i + 3] = colorkey_match(*result,i,metadata) ? 0 : 255;
    }
    return result;
}

std::shared_ptr<Surface> add_mosaic(std::shared_ptr<Surface> arg_image, Integer arg_value)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const Integer value = std::clamp<Integer>(arg_value, 0, 255);
    if (value == 0) {
        apply_retouch_colorkey(arg_image.get(), result.get());
        return result;
    }
    const Integer block = std::max<Integer>(1, value);
    const auto source = result->rgba;
    for (Integer y = 0; y < result->size.height; ++y) for (Integer x = 0; x < result->size.width; ++x) {
        const Integer sx = (x / block) * block;
        const Integer sy = (y / block) * block;
        const auto from = index_of(*result, sx, sy);
        const auto to = index_of(*result, x, y);
        if (from + 3 < source.size() && to + 3 < result->rgba.size())
            std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(from), 4,
                        result->rgba.begin() + static_cast<std::ptrdiff_t>(to));
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> to_binaryformat(std::shared_ptr<Surface> arg_image, Integer arg_value,
                                         std::tuple<Integer, Integer, Integer> arg_basecolor)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const auto [base_r, base_g, base_b] = arg_basecolor;
    const Integer threshold = std::clamp<Integer>(arg_value, -1, 255);
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        const bool black = threshold == -1
            ? (result->rgba[i] != clamp_channel(base_r) || result->rgba[i + 1] != clamp_channel(base_g) ||
               result->rgba[i + 2] != clamp_channel(base_b))
            : (result->rgba[i] <= threshold && result->rgba[i + 1] <= threshold && result->rgba[i + 2] <= threshold);
        const auto v = static_cast<std::uint8_t>(black ? 0 : 255);
        result->rgba[i] = result->rgba[i + 1] = result->rgba[i + 2] = v;
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> add_noise(std::shared_ptr<Surface> arg_image, Integer arg_value, bool arg_colornoise)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const Integer amount = std::clamp<Integer>(arg_value, -1, 255);
    if (amount == 0) {
        apply_retouch_colorkey(arg_image.get(), result.get());
        return result;
    }
    const Integer random_max = amount < 0 ? 2 : amount * 2 + 1;
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        const auto noise = [&]() {
            const int sample = std::rand() % static_cast<int>(random_max);
            if (amount < 0) return sample ? Integer{0} : Integer{255};
            return static_cast<Integer>(sample) - amount;
        };
        const Integer n = noise();
        result->rgba[i] = static_cast<std::uint8_t>(clamp_channel(amount < 0 ? n : result->rgba[i] + n));
        const Integer ng = arg_colornoise ? noise() : n;
        const Integer nb = arg_colornoise ? noise() : n;
        result->rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(amount < 0 ? ng : result->rgba[i + 1] + ng));
        result->rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(amount < 0 ? nb : result->rgba[i + 2] + nb));
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> exchange_rgbcolor(std::shared_ptr<Surface> arg_image, ::cw::lib::String arg_colormodel)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    std::transform(arg_colormodel.begin(), arg_colormodel.end(), arg_colormodel.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool supported = arg_colormodel == "gbr" || arg_colormodel == "brg" ||
                           arg_colormodel == "grb" || arg_colormodel == "bgr";
    if (!supported) {
        apply_retouch_colorkey(arg_image.get(), result.get());
        return result;
    }
    const auto channel_index = [](char c) { return c == 'r' ? 0 : (c == 'g' ? 1 : 2); };
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        const auto old = std::array<std::uint8_t, 3>{result->rgba[i], result->rgba[i + 1], result->rgba[i + 2]};
        result->rgba[i] = old[channel_index(arg_colormodel[0])];
        result->rgba[i + 1] = old[channel_index(arg_colormodel[1])];
        result->rgba[i + 2] = old[channel_index(arg_colormodel[2])];
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> to_grayscale(std::shared_ptr<Surface> arg_image)
{
    return to_sepiatone(std::move(arg_image), {0, 0, 0});
}

std::shared_ptr<Surface> to_sepiatone(std::shared_ptr<Surface> arg_image,
                                      std::tuple<Integer, Integer, Integer> arg_color)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const auto [radd, gadd, badd] = arg_color;
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        const Integer gray = (306 * result->rgba[i] + 601 * result->rgba[i + 1] + 117 * result->rgba[i + 2]) >> 10;
        result->rgba[i] = static_cast<std::uint8_t>(clamp_channel(gray + radd));
        result->rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(gray + gadd));
        result->rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(gray + badd));
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> spread_pixels(std::shared_ptr<Surface> arg_image)
{
    auto source = clone_surface(arg_image);
    if (!source) return nullptr;
    auto result = clone_surface(source);
    if (source->size.width == 0 || source->size.height == 0) {
        apply_retouch_colorkey(arg_image.get(), result.get());
        return result;
    }
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (Integer y = 0; y < source->size.height; ++y) for (Integer x = 0; x < source->size.width; ++x) {
        const Integer y2 = std::clamp<Integer>(y - (std::rand() % 5 + 2), 0, source->size.height - 1);
        const Integer x2 = std::clamp<Integer>(x - (std::rand() % 5 + 2), 0, source->size.width - 1);
        const auto from = index_of(*source, x2, y2), to = index_of(*result, x, y);
        std::copy_n(source->rgba.begin() + static_cast<std::ptrdiff_t>(from), 4,
                    result->rgba.begin() + static_cast<std::ptrdiff_t>(to));
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> _filter(std::shared_ptr<Surface> arg_image,
                                 std::tuple<std::tuple<Integer, Integer, Integer>, std::tuple<Integer, Integer, Integer>, std::tuple<Integer, Integer, Integer>> arg_weight,
                                 Integer arg_offset, Integer arg_div)
{
    if (arg_div == 0) throw std::invalid_argument("_filter: divisor must not be zero");
    auto source = clone_surface(arg_image);
    if (!source) return nullptr;
    auto result = clone_surface(source);
    for (Integer y = 0; y < source->size.height; ++y) for (Integer x = 0; x < source->size.width; ++x) {
        const auto di = index_of(*result, x, y);
        for (int c = 0; c < 3; ++c) {
            long double sum = 0.0L;
            for (Integer ky = -1; ky <= 1; ++ky) for (Integer kx = -1; kx <= 1; ++kx) {
                const auto& row = ky == -1 ? std::get<0>(arg_weight) : (ky == 0 ? std::get<1>(arg_weight) : std::get<2>(arg_weight));
                const Integer weight = kx == -1 ? std::get<0>(row) : (kx == 0 ? std::get<1>(row) : std::get<2>(row));
                sum += static_cast<long double>(channel(*source, std::clamp(x + kx, Integer{0}, source->size.width - 1),
                                                        std::clamp(y + ky, Integer{0}, source->size.height - 1), c)) *
                       static_cast<long double>(weight);
            }
            // 元のネイティブ画像加工と同様、符号付き整数の除算を先に行って
            // からずれを加える。 共通の範囲制限より前に商を切り捨てる。
            const auto normalized = std::trunc(sum / static_cast<long double>(arg_div)) + static_cast<long double>(arg_offset);
            result->rgba[di + c] = static_cast<std::uint8_t>(std::clamp<long double>(normalized, 0.0L, 255.0L));
        }
    }
    apply_retouch_colorkey(arg_image.get(), result.get());
    return result;
}

std::shared_ptr<Surface> filter_shape(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{1, 1, 1}, std::tuple<Integer, Integer, Integer>{1, 1, 1}, std::tuple<Integer, Integer, Integer>{1, 1, 1}), 0, 9);
}

std::shared_ptr<Surface> filter_sharpness(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{-1, -1, -1}, std::tuple<Integer, Integer, Integer>{-1, 24, -1}, std::tuple<Integer, Integer, Integer>{-1, -1, -1}), 0, 16);
}

std::shared_ptr<Surface> filter_sunpower(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{1, 3, 1}, std::tuple<Integer, Integer, Integer>{3, 5, 3}, std::tuple<Integer, Integer, Integer>{1, 3, 1}), 0, 16);
}

std::shared_ptr<Surface> filter_emboss(std::shared_ptr<Surface> arg_image)
{
    return _filter(to_grayscale(std::move(arg_image)), std::make_tuple(std::tuple<Integer, Integer, Integer>{-1, 0, 0}, std::tuple<Integer, Integer, Integer>{0, 1, 0}, std::tuple<Integer, Integer, Integer>{0, 0, 0}), 128, 1);
}

std::shared_ptr<Surface> filter_coloremboss(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{-1, -1, -1}, std::tuple<Integer, Integer, Integer>{0, 1, 0}, std::tuple<Integer, Integer, Integer>{1, 1, 1}), 0, 1);
}

std::shared_ptr<Surface> filter_darkemboss(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{-1, -2, -1}, std::tuple<Integer, Integer, Integer>{0, 0, 0}, std::tuple<Integer, Integer, Integer>{1, 2, 1}), 128, 1);
}

std::shared_ptr<Surface> filter_electrical(std::shared_ptr<Surface> arg_image)
{
    return _filter(std::move(arg_image), std::make_tuple(std::tuple<Integer, Integer, Integer>{1, 1, 1}, std::tuple<Integer, Integer, Integer>{1, -15, 1}, std::tuple<Integer, Integer, Integer>{1, 1, 1}), 0, 1);
}

std::shared_ptr<Surface> add_transparentline(std::shared_ptr<Surface> arg_image, bool arg_vline, bool arg_hline,
                                             std::optional<std::tuple<Integer, Integer, Integer, Integer>> arg_rect,
                                             bool arg_setalpha)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const auto [x0, y0, width, height] = arg_rect.value_or(std::tuple<Integer, Integer, Integer, Integer>{0, 0, result->size.width, result->size.height});
    const auto ci = in_bounds(*result, 0, 0) ? index_of(*result, 0, 0) : 0;
    const Integer cr = ci + 2 < result->rgba.size() ? result->rgba[ci] : 0;
    const Integer cg = ci + 2 < result->rgba.size() ? result->rgba[ci + 1] : 0;
    const Integer cb = ci + 2 < result->rgba.size() ? result->rgba[ci + 2] : 0;
    const Integer ca = arg_setalpha ? 0 : (ci + 3 < result->rgba.size() ? result->rgba[ci + 3] : 255);
    const auto draw = [&](Integer x, Integer y) { put_rgba(*result, x, y, cr, cg, cb, ca); };
    if (arg_vline) for (Integer cnt = 0; cnt < width / 2 - 1; ++cnt) {
        const Integer x = x0 + cnt * 2;
        for (Integer y = 0; y <= height; ++y) draw(x, y);
    }
    if (arg_hline) for (Integer cnt = 0; cnt < height / 2 - 1; ++cnt) {
        const Integer y = y0 + cnt * 2;
        for (Integer x = 0; x <= width; ++x) draw(x, y);
    }
    return result;
}

std::shared_ptr<Surface> add_transparentmesh(std::shared_ptr<Surface> arg_image,
                                             std::optional<std::tuple<Integer, Integer, Integer, Integer>> arg_rect,
                                             bool arg_setalpha)
{
    auto result = clone_surface(arg_image);
    if (!result) return nullptr;
    const auto [x0, y0, width, height] = arg_rect.value_or(std::tuple<Integer, Integer, Integer, Integer>{0, 0, result->size.width, result->size.height});
    const auto ci = in_bounds(*result, 0, 0) ? index_of(*result, 0, 0) : 0;
    const Integer cr = ci + 2 < result->rgba.size() ? result->rgba[ci] : 0;
    const Integer cg = ci + 2 < result->rgba.size() ? result->rgba[ci + 1] : 0;
    const Integer cb = ci + 2 < result->rgba.size() ? result->rgba[ci + 2] : 0;
    const Integer ca = arg_setalpha ? 0 : (ci + 3 < result->rgba.size() ? result->rgba[ci + 3] : 255);
    const auto draw_line = [&](Integer x1, Integer y1, Integer x2, Integer y2) {
        const Integer dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
        const Integer dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
        Integer error = dx + dy;
        for (;;) {
            if (x1 >= x0 && x1 < x0 + width && y1 >= y0 && y1 < y0 + height)
                put_rgba(*result, x1, y1, cr, cg, cb, ca);
            if (x1 == x2 && y1 == y2) break;
            const Integer twice = error * 2;
            if (twice >= dy) { error += dy; x1 += sx; }
            if (twice <= dx) { error += dx; y1 += sy; }
        }
    };
    for (Integer cnt = 0; cnt < width + height; cnt += 2)
        draw_line(x0 + cnt, y0, x0 + cnt - height, y0 + height);
    return result;
}

void add_border(std::shared_ptr<Surface> arg_img, std::tuple<Integer, Integer, Integer> arg_bordercolor,
                Integer arg_borderwidth)
{
    if (!arg_img || arg_borderwidth <= 0) return;
    ensure_rgba(arg_img);
    const auto [red, green, blue] = arg_bordercolor;
    std::vector<std::pair<Integer, Integer>> points;
    for (Integer y = 0; y < arg_img->size.height; ++y) for (Integer x = 0; x < arg_img->size.width; ++x) {
        if (channel(*arg_img, x, y, 3) == 0) continue;
        const auto transparent = [&](Integer dx, Integer dy, bool allowed) {
            if (!allowed) return false;
            const auto index = (y + dy) * arg_img->size.width + x + dx;
            // 安全のため、Py の入れ替わった下側斜めのポイン
            // ターによるバッファー外の読み取りは再現しない。
            return index >= 0 && static_cast<std::size_t>(index) < arg_img->rgba.size() / 4 &&
                arg_img->rgba[static_cast<std::size_t>(index) * 4 + 3] == 0;
        };
        const bool left = x > 0, right = x + 1 < arg_img->size.width;
        const bool top = y > 0, bottom = y + 1 < arg_img->size.height;
        // 元の C の輪郭処理で使う下側斜めのポインターと境界判定の組を維
        // 持する。 画像端でバッファー内の隣の行へ回り込む動作も含める。
        const bool adjacent = transparent(-1, -1, left && top) ||
            transparent(0, -1, top) || transparent(1, -1, right && top) ||
            transparent(-1, 0, left) || transparent(1, 0, right) ||
            transparent(1, 1, left && bottom) || transparent(0, 1, bottom) ||
            transparent(-1, 1, right && bottom);
        if (adjacent) points.emplace_back(x, y);
    }
    const auto fill_ellipse = [&](Integer cx, Integer cy) {
        // Py の pygame 1.9.6 の draw.c の塗りつぶし楕円に合わせ、
        // 固定小数点の整数走査と両端を含む横方向の塗りを使い、端のクリッピングも再現する。
        const Integer offset = (arg_borderwidth & 1) ^ 1;
        const Integer rx = arg_borderwidth >> 1;
        const Integer ry = (arg_borderwidth >> 1) + 1 - offset;
        Integer oh = 0xffff, oi = 0xffff, oj = 0xffff, ok = 0xffff;
        const auto horizontal = [&](Integer x1, Integer y, Integer x2) {
            if (y < 0 || y >= arg_img->size.height) return;
            if (x2 < x1) std::swap(x1, x2);
            x1 = std::max<Integer>(0, x1);
            x2 = std::min<Integer>(arg_img->size.width - 1, x2);
            for (Integer x = x1; x <= x2; ++x)
                put_rgba(*arg_img, x, y, red, green, blue, 255);
        };
        Integer ix = 0, iy = std::max(rx, ry) * 64;
        Integer h, i;
        if (rx >= ry) {
            do {
                h = (ix + 8) >> 6; i = (iy + 8) >> 6;
                const Integer j = h * ry / rx, k = i * ry / rx;
                if (ok != k && oj != k && k < ry) {
                    horizontal(cx - h, cy - k - offset, cx + h - offset);
                    horizontal(cx - h, cy + k, cx + h - offset);
                    ok = k;
                }
                if (oj != j && ok != j && k != j) {
                    horizontal(cx - i, cy + j, cx + i - offset);
                    horizontal(cx - i, cy - j - offset, cx + i - offset);
                    oj = j;
                }
                ix += iy / rx; iy -= ix / rx;
            } while (i > h);
        } else {
            do {
                h = (ix + 8) >> 6; i = (iy + 8) >> 6;
                const Integer j = h * rx / ry, k = i * rx / ry;
                if (oi != i && oh != i && i < ry) {
                    horizontal(cx - j, cy + i, cx + j - offset);
                    horizontal(cx - j, cy - i - offset, cx + j - offset);
                    oi = i;
                }
                if (oh != h && oi != h && i != h) {
                    horizontal(cx - k, cy + h, cx + k - offset);
                    horizontal(cx - k, cy - h - offset, cx + k - offset);
                    oh = h;
                }
                ix += iy / ry; iy -= ix / ry;
            } while (i > h);
        }
    };
    for (const auto [x, y] : points) {
        if (arg_borderwidth == 1) put_rgba(*arg_img, x, y, red, green, blue, 255);
        else if (arg_borderwidth == 2) {
            // Py の Surface.fill と同様、負の矩形原点は 2×2
            // の大きさを減らさずゼロへ収め、 その後で右端・下端を切り取る。
            const Integer left = std::max<Integer>(0, x - 1);
            const Integer top = std::max<Integer>(0, y - 1);
            for (Integer dy = 0; dy < 2; ++dy)
                for (Integer dx = 0; dx < 2; ++dx)
                    put_rgba(*arg_img, left + dx, top + dy, red, green, blue, 255);
        } else fill_ellipse(x, y);
    }
    // 元の C の輪郭処理が選んだ点に枠を付ける。
}

void blend_1_50(std::shared_ptr<Surface> arg_dest, std::tuple<Integer, Integer> arg_pos,
                std::shared_ptr<Surface> arg_source, Integer arg_flag)
{
    if (!arg_dest || !arg_source) return;
    const auto [x0, y0] = arg_pos;
    const bool add = arg_flag == 1 || arg_flag == 6;
    const bool sub = arg_flag == 2 || arg_flag == 7;
    const bool mult = arg_flag == 3 || arg_flag == 8;
    if (!add && !sub && !mult) throw std::invalid_argument("blend_1_50: unsupported blend flag");
    ensure_rgba(arg_dest);
    ensure_rgba(arg_source);
    for (Integer y = 0; y < arg_source->size.height; ++y) for (Integer x = 0; x < arg_source->size.width; ++x) {
        if (!in_bounds(*arg_dest, x0 + x, y0 + y)) continue;
        const auto si = index_of(*arg_source, x, y), di = index_of(*arg_dest, x0 + x, y0 + y);
        Integer dr = arg_dest->rgba[di], dg = arg_dest->rgba[di + 1], db = arg_dest->rgba[di + 2];
        const Integer sr = arg_source->rgba[si], sg = arg_source->rgba[si + 1], sb = arg_source->rgba[si + 2];
        const Integer sa = arg_source->rgba[si + 3];
        if (add) {
            if (sa != 255) {
                dr = fcolor_wrap(dr * (255 - sa) + (dr + sr) * sa);
                dg = fcolor_wrap(dg * (255 - sa) + (dg + sg) * sa);
                db = fcolor_wrap(db * (255 - sa) + (db + sb) * sa);
            } else {
                dr = clamp_channel(dr + sr); dg = clamp_channel(dg + sg); db = clamp_channel(db + sb);
            }
        } else if (sub) {
            if (sa != 255) {
                dr = std::max(fcolor_wrap(dr * (255 - sa)), clamp_channel(dr - fcolor_wrap(sr * sa)));
                dg = std::max(fcolor_wrap(dg * (255 - sa)), clamp_channel(dg - fcolor_wrap(sg * sa)));
                db = std::max(fcolor_wrap(db * (255 - sa)), clamp_channel(db - fcolor_wrap(sb * sa)));
            } else {
                dr = clamp_channel(dr - sr); dg = clamp_channel(dg - sg); db = clamp_channel(db - sb);
            }
        } else {
            Integer mr = sr, mg = sg, mb = sb;
            if (sa != 255) {
                mr = fcolor_wrap(sr * sa + (255 - sa) * 255);
                mg = fcolor_wrap(sg * sa + (255 - sa) * 255);
                mb = fcolor_wrap(sb * sa + (255 - sa) * 255);
            }
            dr = fcolor_wrap(dr * mr); dg = fcolor_wrap(dg * mg); db = fcolor_wrap(db * mb);
        }
        arg_dest->rgba[di] = static_cast<std::uint8_t>(dr);
        arg_dest->rgba[di + 1] = static_cast<std::uint8_t>(dg);
        arg_dest->rgba[di + 2] = static_cast<std::uint8_t>(db);
    }
}

std::shared_ptr<::cw::lib::Bitmap> to_disabledimage(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp,
                                                     std::tuple<Integer, Integer> arg_maskpos)
{
    auto result = wx_grayscale_bitmap(arg_wxbmp);
    if (!result) return nullptr;
    const auto metadata = metadata_for(arg_wxbmp.get());
    // Python のヘルパーと同様、wx の灰色化後の先頭ピクセルと完全に同じ RGB
    // を除外する。 そのピクセルがマスク色なら、灰色化後も色が付いている場合がある。
    const auto key = std::array<std::uint8_t, 3>{
        channel(*result, 0, 0, 0), channel(*result, 0, 0, 1), channel(*result, 0, 0, 2)};
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        if (result->rgba[i] == key[0] && result->rgba[i + 1] == key[1] && result->rgba[i + 2] == key[2]) continue;
        for (std::size_t c = 0; c < 3; ++c)
            result->rgba[i + c] = static_cast<std::uint8_t>(140 + result->rgba[i + c] * 100 / 255);
    }
    // Py と同様、alphaBuffer なしの ImageFromBuffer
    // 相当で画像を再構築する。 入力アルファを捨て、新しい色マスクで透明度を与える。
    ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
    result->has_pixel_alpha = false;
    result->pixel_bits = 24;
    result->surface_alpha = 255;
    result->surface_alpha_enabled = false;
    result->known_opaque = false;
    const auto [mask_x, mask_y] = arg_maskpos;
    auto output_metadata = metadata;
    if (in_bounds(*result, mask_x, mask_y)) {
        const auto mask_index = index_of(*result, mask_x, mask_y);
        output_metadata.has_colorkey = true;
        output_metadata.colorkey = {result->rgba[mask_index], result->rgba[mask_index + 1], result->rgba[mask_index + 2]};
    }
    store_metadata(result.get(), output_metadata);
    clear_wx_depth1(result.get());
    return result;
}

void _to_disabledimage(Bytes& arg_buf, std::tuple<Integer, Integer> arg_size)
{
    (void)arg_size;
    if (arg_buf.size() < 3) return;
    const auto key = std::array<std::uint8_t, 3>{arg_buf[0], arg_buf[1], arg_buf[2]};
    for (std::size_t i = 0; i + 2 < arg_buf.size(); i += 3) if (arg_buf[i] != key[0] || arg_buf[i + 1] != key[1] || arg_buf[i + 2] != key[2]) {
        arg_buf[i] = static_cast<std::uint8_t>(140 + arg_buf[i] * 100 / 255);
        arg_buf[i + 1] = static_cast<std::uint8_t>(140 + arg_buf[i + 1] * 100 / 255);
        arg_buf[i + 2] = static_cast<std::uint8_t>(140 + arg_buf[i + 2] * 100 / 255);
    }
    // Pythonのbytearrayに合わせてBytesを参照更新する。
}

std::shared_ptr<Surface> to_disabledsurface(std::shared_ptr<Surface> arg_image)
{
    return to_grayscale(add_lightness(std::move(arg_image), 128));
}

std::shared_ptr<::cw::lib::Bitmap> add_lightness_for_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp,
                                                            Integer arg_lightness,
                                                            std::tuple<Integer, Integer> arg_maskpos)
{
    auto result = wx_grayscale_bitmap(arg_wxbmp);
    if (!result) return nullptr;
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        result->rgba[i] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i] + std::clamp<Integer>(arg_lightness, -255, 255)));
        result->rgba[i + 1] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i + 1] + std::clamp<Integer>(arg_lightness, -255, 255)));
        result->rgba[i + 2] = static_cast<std::uint8_t>(clamp_channel(result->rgba[i + 2] + std::clamp<Integer>(arg_lightness, -255, 255)));
    }
    const auto [mask_x, mask_y] = arg_maskpos;
    auto output_metadata = metadata_for(arg_wxbmp.get());
    if (in_bounds(*result, mask_x, mask_y)) {
        const auto mask_index = index_of(*result, mask_x, mask_y);
        output_metadata.has_colorkey = true;
        output_metadata.colorkey = {result->rgba[mask_index], result->rgba[mask_index + 1], result->rgba[mask_index + 2]};
    }
    store_metadata(result.get(), output_metadata);
    clear_wx_depth1(result.get());
    return result;
}

void _add_lightness(Bytes& arg_buf, std::tuple<Integer, Integer> arg_size, Integer arg_lightness)
{
    (void)arg_size;
    for (auto& value : arg_buf) value = static_cast<std::uint8_t>(clamp_channel(value + arg_lightness));
}

std::tuple<Integer, Integer, Integer> hex2color(Integer arg_hexnum)
{
    return {(arg_hexnum >> 16) & 0xff, (arg_hexnum >> 8) & 0xff, arg_hexnum & 0xff};
}

Integer colorwrap(Integer arg_num) { return clamp_channel(arg_num); }

Bytes decode_rle4data(Bytes arg_data, Integer arg_h, Integer arg_bpl)
{
    // Py の C 拡張は不正寸法でも h*bpl を直接計算するが、ネイティ
    // ブ版では確保前に寸法が非負で、 積が size_t に収まることを確認
    // する。符号付きから size_t への変換の折り返し・桁あふれを防ぐ。
    if (arg_h < 0 || arg_bpl < 0 ||
        (arg_h != 0 && static_cast<std::uint64_t>(arg_bpl) > std::numeric_limits<std::size_t>::max() / static_cast<std::uint64_t>(arg_h)))
        throw std::invalid_argument("decode_rle4data: invalid output dimensions");
    // Python版 _imageretouch.c decode_rle4data と同じ解釈（例外を出さない）:
    // - 終端マーカーが無いまま入力が尽きたら、そこまでの結果を返す。
    // - 行末を越えた画素は次の行へ続けて書く（出力全体の範囲内なら可）。
    // - 入力不足・出力全体からのはみ出しは、全画素0の画像を返す。
    // - 画素は OR で書き込む。
    const auto outlen = static_cast<std::size_t>(arg_h) * static_cast<std::size_t>(arg_bpl);
    Bytes output(outlen, 0);
    const std::size_t slen = arg_data.size();
    std::size_t si = 0;  // 4 ビット単位の位置。
    std::size_t linepos = 0, x = 0, y = 0;
    bool failed = false;
    const auto get_byte = [&](std::uint8_t& value) {
        if (si & 1U) ++si;
        if (slen <= si / 2) return false;
        value = arg_data[si / 2];
        si += 2;
        return true;
    };
    const auto get_pixel = [&](std::uint8_t& value) {
        if (slen <= si / 2) return false;
        value = (si & 1U) == 0 ? (arg_data[si / 2] >> 4) & 0x0f : arg_data[si / 2] & 0x0f;
        ++si;
        return true;
    };
    const auto put_pixel = [&](std::uint8_t value) {
        const auto at = linepos + x / 2;
        if (at < linepos || outlen <= at) return false;
        if ((x & 1U) == 0) output[at] |= static_cast<std::uint8_t>((value & 0x0f) << 4);
        else output[at] |= static_cast<std::uint8_t>(value & 0x0f);
        ++x;
        return true;
    };
    const auto line_start = [&]() {
        const auto bpl = static_cast<std::size_t>(arg_bpl);
        linepos = y > std::numeric_limits<std::size_t>::max() / (bpl ? bpl : 1)
            ? std::numeric_limits<std::size_t>::max() : y * bpl;
    };
    while (!failed && si < slen * 2) {
        std::uint8_t count = 0;
        if (!get_byte(count)) { failed = true; break; }
        if (count == 0) {
            if (!get_byte(count)) { failed = true; break; }
            if (count == 0) { x = 0; ++y; line_start(); }
            else if (count == 1) { si = slen * 2; }
            else if (count == 2) {
                std::uint8_t pixels = 0, lines = 0;
                if (!get_byte(pixels) || !get_byte(lines)) { failed = true; break; }
                x += pixels; y += lines; line_start();
            } else {
                for (std::uint32_t j = 0; j < count; ++j) {
                    std::uint8_t pixel = 0;
                    if (!get_pixel(pixel) || !put_pixel(pixel)) { failed = true; break; }
                }
                if ((((count + 1U) / 2U) & 1U) == 1U) si += 2;
            }
        } else {
            std::uint8_t packed = 0;
            if (!get_byte(packed)) { failed = true; break; }
            for (std::uint32_t j = 0; j < count; ++j) {
                if (!put_pixel((j & 1U) == 0 ? (packed >> 4) & 0x0f : packed & 0x0f)) { failed = true; break; }
            }
        }
    }
    if (failed) std::fill(output.begin(), output.end(), std::uint8_t{0});
    return output;
}

Bytes decode_rle8data(Bytes arg_data, Integer arg_h, Integer arg_bpl)
{
    // Py の C 拡張は不正寸法でも h*bpl を直接計算するが、ネイティ
    // ブ版では確保前に寸法が非負で、 積が size_t に収まることを確認
    // する。符号付きから size_t への変換の折り返し・桁あふれを防ぐ。
    if (arg_h < 0 || arg_bpl < 0 || (arg_h != 0 && static_cast<std::uint64_t>(arg_bpl) > std::numeric_limits<std::size_t>::max() / static_cast<std::uint64_t>(arg_h)))
        throw std::invalid_argument("decode_rle8data: invalid output dimensions");
    // Python版 _imageretouch.c decode_rle8data と同じ解釈（例外を出さない）:
    // 入力が尽きたらそこまでの結果を返し、行末を越えた画素は次の行へ続ける。
    // Python版は範囲外の読み書きを検査しない（未定義動作）ため、ここでは
    // 入力の範囲外は終端、出力の範囲外への書き込みは無視として扱う（意図的な差）。
    const auto outlen = static_cast<std::size_t>(arg_h) * static_cast<std::size_t>(arg_bpl);
    Bytes output(outlen, 0);
    const std::size_t slen = arg_data.size();
    std::size_t si = 0, x = 0, y = 0;
    const auto bpl = static_cast<std::size_t>(arg_bpl);
    const auto put = [&](std::uint8_t value) {
        if (bpl && y < outlen / bpl + 1) {
            const auto at = y * bpl + x;
            if (at < outlen) output[at] = value;
        }
        ++x;
    };
    const auto read = [&](std::uint8_t& value) {
        if (si >= slen) return false;
        value = arg_data[si++];
        return true;
    };
    while (si < slen) {
        std::uint8_t count = 0, value = 0;
        if (!read(count)) break;
        if (count == 0) {
            if (!read(value)) break;
            if (value == 0) { x = 0; ++y; }
            else if (value == 1) break;
            else if (value == 2) {
                std::uint8_t pixels = 0, lines = 0;
                if (!read(pixels) || !read(lines)) break;
                x += pixels; y += lines;
            } else {
                bool ended = false;
                for (std::uint32_t j = 0; j < value; ++j) {
                    std::uint8_t pixel = 0;
                    if (!read(pixel)) { ended = true; break; }
                    put(pixel);
                }
                if (ended) break;
                if (value & 1U) ++si;
            }
        } else {
            if (!read(value)) break;
            for (std::uint32_t j = 0; j < count; ++j) put(value);
        }
    }
    return output;
}

std::shared_ptr<Surface> patch_alphadata(std::shared_ptr<Surface> arg_image, ::cw::lib::String arg_ext, Bytes arg_data)
{
    if (!arg_image || arg_image->pixel_bits != 32) return arg_image;
    std::transform(arg_ext.begin(), arg_ext.end(), arg_ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool is_bmp = arg_ext == ".bmp";
    // Py と同じヘッダー判定を使う。短い・不正な BMP 識別子は 0 とし、 BITMAPINFOHEADER
    // より小さい DIB ヘッダーには圧縮形式の項目がないものとする。
    const bool bitfields = is_bmp && ::cw::image::get_bicompression(std::move(arg_data)) == 3;
    bool has_alpha = false;
    for (std::size_t i = 3; i < arg_image->rgba.size(); i += 4)
        if (arg_image->rgba[i] != (is_bmp ? 0 : 255)) { has_alpha = true; break; }
    if (!bitfields && has_alpha) return arg_image;
    // Py の RGBX への置き換えは別の形式であり、参照するアルファや透過色を持たない。
    auto result = clone_surface(arg_image);
    ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
    result->has_pixel_alpha = false;
    result->has_colorkey = false;
    // Py の fromstring(..., "RGBX") と同様、set_alpha なしの新しい描画面を作る。
    result->surface_alpha = 255;
    result->surface_alpha_enabled = false;
    result->known_opaque = true;
    return result;
}

std::shared_ptr<::cw::lib::Image> mul_wxalpha(std::shared_ptr<::cw::lib::Image> arg_wximg, Integer arg_alpha)
{
    if (!arg_wximg) return nullptr;
    ensure_rgba(arg_wximg);
    const Integer alpha = std::clamp<Integer>(arg_alpha, 0, 255);
    ::cw::lib::simd_image::multiply_alpha(
        arg_wximg->rgba.data(), arg_wximg->rgba.size() / 4U, static_cast<std::uint8_t>(alpha));
    return arg_wximg;
}

std::shared_ptr<Surface> mul_alpha(std::shared_ptr<Surface> arg_image, Integer arg_alpha)
{
    if (!arg_image) return nullptr;
    ensure_rgba(arg_image);
    const Integer alpha = std::clamp<Integer>(arg_alpha, 0, 255);
    ::cw::lib::simd_image::multiply_rgba_alpha(
        arg_image->rgba.data(), arg_image->rgba.size() / 4U, static_cast<std::uint8_t>(alpha));
    return arg_image;
}

void blend_card_masked(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source,
                       std::tuple<Integer, Integer> pos,
                       std::tuple<Integer, Integer, Integer> mask,
                       Integer clip_x0 = 0, Integer clip_y0 = 0, Integer clip_x1 = -1, Integer clip_y1 = -1)
{
    if (!dest || !source) return;
    ensure_rgba(dest); ensure_rgba(source);
    if (clip_x1 < 0) clip_x1 = dest->size.width;
    if (clip_y1 < 0) clip_y1 = dest->size.height;
    const auto [mask_r, mask_g, mask_b] = mask;
    const auto [x0, y0] = pos;
    for (Integer y = 0; y < source->size.height; ++y) for (Integer x = 0; x < source->size.width; ++x) {
        if (!in_bounds(*dest, x0 + x, y0 + y)) continue;
        if (x0 + x < clip_x0 || y0 + y < clip_y0 || x0 + x >= clip_x1 || y0 + y >= clip_y1) continue;
        const auto si = index_of(*source, x, y), di = index_of(*dest, x0 + x, y0 + y);
        if (source->rgba[si] == mask_r && source->rgba[si + 1] == mask_g && source->rgba[si + 2] == mask_b) continue;
        dest->rgba[di] &= source->rgba[si];
        dest->rgba[di + 1] &= source->rgba[si + 1];
        dest->rgba[di + 2] &= source->rgba[si + 2];
        dest->rgba[di + 3] = 255;
    }
}

void blend_message_masked(const std::shared_ptr<Surface>& dest, const std::shared_ptr<Surface>& source,
                          std::tuple<Integer, Integer> pos,
                          std::tuple<Integer, Integer, Integer, Integer> wincolour,
                          std::tuple<Integer, Integer, Integer> mask)
{
    if (!dest || !source) return;
    ensure_rgba(dest); ensure_rgba(source);
    const auto [mask_r, mask_g, mask_b] = mask;
    const auto base_a = clamp_channel(std::get<3>(wincolour));
    const auto [x0, y0] = pos;
    for (Integer y = 0; y < source->size.height; ++y) for (Integer x = 0; x < source->size.width; ++x) {
        if (!in_bounds(*dest, x0 + x, y0 + y)) continue;
        const auto si = index_of(*source, x, y), di = index_of(*dest, x0 + x, y0 + y);
        const auto sr = static_cast<Integer>(source->rgba[si]);
        const auto sg = static_cast<Integer>(source->rgba[si + 1]);
        const auto sb = static_cast<Integer>(source->rgba[si + 2]);
        if (sr == mask_r && sg == mask_g && sb == mask_b) continue;
        const Integer out_r = base_a & sr, out_g = base_a & sg, out_b = base_a & sb;
        const Integer out_a = std::max<Integer>(base_a, 255 - (sr + sg + sb) / 3);
        if (out_a >= 255) {
            dest->rgba[di] = static_cast<std::uint8_t>(out_r);
            dest->rgba[di + 1] = static_cast<std::uint8_t>(out_g);
            dest->rgba[di + 2] = static_cast<std::uint8_t>(out_b);
            dest->rgba[di + 3] = 255;
        } else if (out_a > 0) {
            const auto inv = 255 - out_a;
            dest->rgba[di] = static_cast<std::uint8_t>((out_r * out_a + dest->rgba[di] * inv) / 255);
            dest->rgba[di + 1] = static_cast<std::uint8_t>((out_g * out_a + dest->rgba[di + 1] * inv) / 255);
            dest->rgba[di + 2] = static_cast<std::uint8_t>((out_b * out_a + dest->rgba[di + 2] * inv) / 255);
            dest->rgba[di + 3] = static_cast<std::uint8_t>(std::min<Integer>(255, out_a + dest->rgba[di + 3] * inv / 255));
        }
    }
}

void blit_2bitbmp_to_card(std::shared_ptr<Surface> arg_dest, std::shared_ptr<Surface> arg_source,
                          std::tuple<Integer, Integer> arg_pos)
{
    if (!arg_dest || !arg_source) return;
    const auto metadata = metadata_for(arg_source.get());
    // Python の source.get_colorkey() と Depth1Surface の型判定と bmpdepthis1 の論理積に対応する。
    if (!metadata.has_colorkey || !metadata.depth1 || !arg_source->bmpdepthis1) {
        pygame_blit(arg_dest, arg_source, std::get<0>(arg_pos), std::get<1>(arg_pos));
        return;
    }
    blend_card_masked(arg_dest, arg_source, arg_pos, metadata.colorkey);
}

void blit_2bitbmp_to_message(std::shared_ptr<Surface> arg_dest, std::shared_ptr<Surface> arg_source,
                             std::tuple<Integer, Integer> arg_pos,
                             std::tuple<Integer, Integer, Integer, Integer> arg_wincolour)
{
    if (!arg_dest || !arg_source) return;
    const auto metadata = metadata_for(arg_source.get());
    if (!metadata.has_colorkey || !metadata.depth1 || !arg_source->bmpdepthis1) {
        pygame_blit(arg_dest, arg_source, std::get<0>(arg_pos), std::get<1>(arg_pos));
        return;
    }
    blend_message_masked(arg_dest, arg_source, arg_pos, arg_wincolour, metadata.colorkey);
}

void wxblit_2bitbmp_to_card(std::shared_ptr<::cw::lib::Window> arg_dc,
                            std::shared_ptr<::cw::lib::Bitmap> arg_dest,
                            std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, Integer arg_x, Integer arg_y,
                            bool arg_useMask, std::optional<std::shared_ptr<::cw::lib::Bitmap>> arg_bitsizekey)
{
    if (!arg_dest || !arg_wxbmp) return;
    Integer clip_x0 = 0, clip_y0 = 0, clip_x1 = arg_dest->size.width, clip_y1 = arg_dest->size.height;
    if (arg_dc && arg_dc->bounds.width > 0 && arg_dc->bounds.height > 0) {
        clip_x0 = std::max(clip_x0, arg_dc->bounds.x);
        clip_y0 = std::max(clip_y0, arg_dc->bounds.y);
        clip_x1 = std::min(clip_x1, arg_dc->bounds.x + arg_dc->bounds.width);
        clip_y1 = std::min(clip_y1, arg_dc->bounds.y + arg_dc->bounds.height);
    }
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;
    const auto source_metadata = metadata_for(arg_wxbmp.get());
    const auto key_metadata = arg_bitsizekey && *arg_bitsizekey
        ? metadata_for((*arg_bitsizekey).get()) : source_metadata;
    if (arg_useMask && key_metadata.has_colorkey && key_metadata.depth1) {
        const auto mask = source_metadata.has_colorkey ? source_metadata.colorkey : key_metadata.colorkey;
        blend_card_masked(arg_dest, arg_wxbmp, {arg_x, arg_y}, mask, clip_x0, clip_y0, clip_x1, clip_y1);
    } else if (arg_useMask) {
        pygame_blit(arg_dest, arg_wxbmp, arg_x, arg_y, clip_x0, clip_y0, clip_x1, clip_y1);
    } else {
        opaque_blit(arg_dest, arg_wxbmp, arg_x, arg_y, clip_x0, clip_y0, clip_x1, clip_y1);
    }
}

std::tuple<std::shared_ptr<::cw::lib::Font>, std::shared_ptr<::cw::lib::Font>, std::shared_ptr<::cw::lib::Font>>
_create_mfont(::cw::lib::String arg_name, Integer arg_pixels, bool arg_bold, bool arg_italic, bool arg_sys)
{
    (void)arg_bold;
    (void)arg_italic;
    (void)arg_sys;
    // compat::Font は不透明なネイティブハンドルを持たず、描画時に SDL_ttf または登録済み GlyphRenderAdapter
    // へ装飾を渡す。 これにより Windows 以外でも処理を置き換えられるようにする。
    const auto pixels = positive_font_pixels(arg_pixels);
    auto font = std::make_shared<::cw::lib::Font>();
    auto font2x = std::make_shared<::cw::lib::Font>();
    auto font_notitalic = std::make_shared<::cw::lib::Font>();
    const auto font_path = ::cw::lib::encoding::path_from_utf8(arg_name);
    font->path = font_path; font->point_size = static_cast<double>(pixels);
    const auto pixels2 = pixels > std::numeric_limits<Integer>::max() / 2 ? std::numeric_limits<Integer>::max() : pixels * 2;
    font2x->path = font_path; font2x->point_size = static_cast<double>(pixels2);
    font_notitalic->path = font_path; font_notitalic->point_size = static_cast<double>(pixels);
    return {font, font2x, font_notitalic};
}

::cw::lib::String resolve_font_face(::cw::lib::String face,
    const std::set<::cw::lib::String>& available_faces_lower,
    const std::map<::cw::lib::String, ::cw::lib::String>& initial_fonts)
{
    if (!face.empty() && face.front() == '@')
        return "@" + resolve_font_face(face.substr(1), available_faces_lower, initial_fonts);
    // Resource の共通フォント表は、書体名だけでなくファイルパスも含む。 実
    // 際のフォントファイルを未知の書体名として扱い、既定のゴシックへ置き換えない。
    std::error_code error;
    if (std::filesystem::is_regular_file(::cw::lib::encoding::path_from_utf8(face), error)) return face;
    const auto lowered = lower_face(face);
    if (available_faces_lower.count(lowered)) return face;
    const auto mapped = [&](const char* key) -> ::cw::lib::String {
        const auto it = initial_fonts.find(key);
        return it == initial_fonts.end() ? ::cw::lib::String{} : it->second;
    };
    ::cw::lib::String preferred;
    if (lowered == lower_face("ＭＳ Ｐゴシック") || lowered == "ms pgothic") preferred = mapped("pgothic");
    else if (lowered == lower_face("ＭＳ Ｐ明朝") || lowered == "ms pmincho") preferred = mapped("pmincho");
    else if (lowered == lower_face("ＭＳ ゴシック") || lowered == "ms gothic") preferred = mapped("gothic");
    else if (lowered == lower_face("ＭＳ 明朝") || lowered == "ms mincho") preferred = mapped("mincho");
    else if (lowered == lower_face("ＭＳ ＵＩゴシック") || lowered == "ms ui gothic") preferred = mapped("uigothic");
    if (!preferred.empty()) return preferred;
    if (available_faces_lower.count(lower_face("ＭＳ Ｐゴシック"))) return "ＭＳ Ｐゴシック";
    const auto gothic = mapped("gothic");
    return gothic.empty() ? face : gothic;
}

::cw::lib::String get_fontface(::cw::lib::String arg_fontface)
{
    if (!arg_fontface.empty() && arg_fontface.front() == '@') return "@" + get_fontface(arg_fontface.substr(1));
    if (const auto adapter = face_resolver()) {
        if (const auto resolved = adapter(arg_fontface); !resolved.empty()) return resolved;
    }
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return arg_fontface;
    return resolve_font_face(std::move(arg_fontface), ::cw::setting::Resource::s_facenames_lower,
                             ::cw::g_cwpy->m_rsrc->m_fontnames_init_typed);
}

void set_glyph_render_adapter(GlyphRenderAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.glyph_renderer = std::move(arg_adapter);
}

void set_logical_scale_adapter(LogicalScaleAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.logical_scale = std::move(arg_adapter);
}

void set_font_face_adapter(FontFaceAdapter arg_adapter)
{
    auto& state = adapter_state();
    std::scoped_lock lock(state.mutex);
    state.face_resolver = std::move(arg_adapter);
}

void set_surface_metadata(const std::shared_ptr<::cw::lib::Surface>& arg_surface,
                          SurfaceMetadata arg_metadata)
{
    store_metadata(arg_surface.get(), std::move(arg_metadata));
}

SurfaceMetadata get_surface_metadata(const std::shared_ptr<const ::cw::lib::Surface>& arg_surface)
{
    return metadata_for(arg_surface.get());
}

void clear_surface_metadata(const std::shared_ptr<::cw::lib::Surface>& arg_surface)
{
    if (!arg_surface) return;
    store_metadata(arg_surface.get(), {});
}

std::shared_ptr<::cw::lib::Surface> resize_wx_bilinear(
    const std::shared_ptr<::cw::lib::Surface>& source, Integer width, Integer height)
{
    if (!source) return {};
    auto result = std::make_shared<Surface>(*source);
    if (width == source->size.width && height == source->size.height) return result;
    // 終了処理や一ピクセル未満への縮小で、ネイティブの描画面が空になる場
    // 合がある。 wx はその寸法を拒否するが、ネイティブでは空を維持し
    // 、ピクセルアクセス前に確保サイズと入力バッファーの範囲を検証する。
    result->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    const auto w = static_cast<std::uint64_t>(result->size.width);
    const auto h = static_cast<std::uint64_t>(result->size.height);
    if (w > result->rgba.max_size() / 4U || (w && h > result->rgba.max_size() / (w * 4U)))
        throw std::length_error("wx bilinear: dimensions exceed RGBA allocation limit");
    result->rgba.assign(static_cast<std::size_t>(w * h * 4U), 0);
    const auto sw = source->size.width, sh = source->size.height;
    if (!w || !h || sw <= 0 || sh <= 0) return result;
    if (static_cast<std::uint64_t>(sw) > source->rgba.size() / 4U ||
        static_cast<std::uint64_t>(sh) > source->rgba.size() / (static_cast<std::uint64_t>(sw) * 4U))
        throw std::invalid_argument("wx bilinear: incomplete source pixels");
    // wx と同様、端点を揃えた座標を使い、出力が一ピクセルなら入力の中央を読
    // む。 wxImage に合わせて RGB とアルファを独立に補間し、pygame
    // の面積フィルターやアルファ乗算済みの UI 拡縮は使わない。
    const auto at = [](Integer i, Integer old, Integer count) {
        return count == 1 ? (old - 1) / 2.0 : static_cast<double>(i) * (static_cast<double>(old - 1) / (count - 1));
    };
    for (Integer y = 0; y < result->size.height; ++y) {
        const auto sy = at(y, sh, result->size.height);
        const auto y0 = static_cast<Integer>(sy), y1 = std::min(y0 + 1, sh - 1);
        const auto fy = sy - y0;
        for (Integer x = 0; x < result->size.width; ++x) {
            const auto sx = at(x, sw, result->size.width);
            const auto x0 = static_cast<Integer>(sx), x1 = std::min(x0 + 1, sw - 1);
            const auto fx = sx - x0;
            const auto index = [&](Integer px, Integer py) { return static_cast<std::size_t>(py * sw + px) * 4U; };
            const auto out = static_cast<std::size_t>(y * result->size.width + x) * 4U;
            for (std::size_t c = 0; c < 4; ++c) {
                const auto top = source->rgba[index(x0, y0) + c] * (1.0 - fx) + source->rgba[index(x1, y0) + c] * fx;
                const auto bottom = source->rgba[index(x0, y1) + c] * (1.0 - fx) + source->rgba[index(x1, y1) + c] * fx;
                result->rgba[out + c] = static_cast<std::uint8_t>(std::clamp(top * (1.0 - fy) + bottom * fy + 0.5, 0.0, 255.0));
            }
        }
    }
    return result;
}

void main() { /* Python 版も何もしない単体実行用エントリーポイント。 */ }

} // namespace cw::imageretouch

