#include "cw/effectbooster.hpp"
#include "cw/animatedimage.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/header.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/regex_grapheme.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/base.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace cw::effectbooster {
namespace {
using I = ::cw::lib::Integer;
using R = ::cw::lib::Real;
using D = ::cw::lib::Decimal;
using S = ::cw::lib::String;
using V = ::cw::lib::Value;
using Surface = ::cw::lib::Surface;

I real_to_int(R value, I fallback)
{
    if (!std::isfinite(value)) return fallback;
    const R truncated = std::trunc(value);
    // この半開区間の外から浮動小数点を整数へ変換すると、MSVC で
    // は未定義となる。 Decimal 値は下の正確な変換を使う。
    const R max_exclusive = static_cast<R>((std::numeric_limits<I>::max)()) + 1.0;
    const R min_inclusive = static_cast<R>((std::numeric_limits<I>::lowest)());
    if (truncated >= max_exclusive || truncated < min_inclusive) return fallback;
    return static_cast<I>(truncated);
}

I value_int(const V& value, I fallback)
{
    if (const auto* p = std::get_if<I>(&value.storage())) return *p;
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? 1 : 0;
    if (const auto* p = std::get_if<D>(&value.storage())) {
        try { return p->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* p = std::get_if<R>(&value.storage())) return real_to_int(*p, fallback);
    if (const auto* p = std::get_if<S>(&value.storage())) {
        try { return std::stoll(*p); } catch (...) { return fallback; }
    }
    return fallback;
}

bool value_bool(const V& value, bool fallback)
{
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p;
    if (const auto* p = std::get_if<D>(&value.storage())) return !p->is_zero();
    if (const auto* p = std::get_if<R>(&value.storage())) return std::isfinite(*p) ? *p != 0.0 : fallback;
    if (!value.is_none()) return value_int(value, fallback ? 1 : 0) != 0;
    return fallback;
}

S value_string(const V& value, S fallback = {})
{
    if (const auto* p = std::get_if<S>(&value.storage())) return *p;
    if (const auto* p = std::get_if<D>(&value.storage())) return p->to_string(true);
    if (const auto* p = std::get_if<I>(&value.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<R>(&value.storage())) {
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<R>::max_digits10) << *p;
        return text.str();
    }
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p ? "True" : "False";
    return fallback;
}

I value_length(const V& value, I fallback)
{
    return value_int(value, fallback);
}

S lower(S value)
{
    return ::cw::lib::encoding::python_lower(value);
}

S trim(S value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == S::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

I scale_i(I value)
{
    try { return value_int(::cw::s(V(value)), value); }
    catch (...) { return value; }
}

std::tuple<I,I> scale_xy(std::tuple<I,I> value)
{
    return {scale_i(std::get<0>(value)), scale_i(std::get<1>(value))};
}

::cw::lib::Rect scale_rect(::cw::lib::Rect rect)
{
    return {scale_i(rect.x), scale_i(rect.y), scale_i(rect.width), scale_i(rect.height)};
}

I area_w() { return scale_i(std::get<0>(::cw::g_SIZE_AREA)); }
I area_h() { return scale_i(std::get<1>(::cw::g_SIZE_AREA)); }

std::optional<I> parse_int_optional(S value, bool allow_px = true)
{
    value = ::cw::lib::python_strip(value);
    // get_int は小文字の px 接尾辞を受け付ける。 get_ints はカンマ区切
    // りの各項目を通常の Python の int と同じ規則で変換し、px は除去しない。
    if (allow_px && value.size() >= 2 && value.substr(value.size() - 2) == "px")
        value.resize(value.size() - 2);
    return ::cw::lib::parse_python_integer(value);
}

I parse_int_strict(S value, I fallback)
{
    return parse_int_optional(std::move(value)).value_or(fallback);
}

bool version_lessthan(const S& version, I hint)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_sct) return false;
    const auto data = [&]() -> std::shared_ptr<::cw::data::ScenarioData> {
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); p && *p)
            return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*p);
        return {};
    }();
    if (!data) return false;
    return ::cw::g_cwpy->m_sct->lessthan(version, data->get_versionhint(hint));
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    if (!::cw::g_cwpy) return {};
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); p && *p)
        return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*p);
    return {};
}

std::shared_ptr<::cw::data::CWPyElement> carddata_of(const std::shared_ptr<::cw::header::CardHeader>& header)
{
    if (!header) return {};
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&header->m_carddata.storage()); object && *object)
        return std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object);
    return {};
}

S join2(const S& a, const S& b)
{
    return ::cw::util::join_paths({V(a), V(b)});
}

std::shared_ptr<Surface> make_surface(I width, I height, std::tuple<I,I,I> color = {0,0,0});
std::shared_ptr<Surface> copy_surface(const std::shared_ptr<Surface>& source);

void add_background_lazydraw()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_background) return;
    ::cw::g_cwpy->add_lazydraw_rect(
        static_cast<::cw::sprite::base::CWPySprite*>(::cw::g_cwpy->m_background.get())->m_rect);
}

std::shared_ptr<Surface> background_surface_copy()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        if (auto image = static_cast<::cw::sprite::base::CWPySprite*>(::cw::g_cwpy->m_background.get())->m_image)
            return copy_surface(image);
    }
    return make_surface(area_w(), area_h());
}

std::shared_ptr<Surface> ensure_temporal_background(const std::shared_ptr<JpyCache>& cache)
{
    if (::cw::g_cwpy) {
        std::shared_ptr<::cw::lib::Object> topgrp;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_topgrp.storage()); p && *p)
            topgrp = *p;
        const auto sprs = ::cw::sprite::background::sprites_from_layer(topgrp, ::cw::g_LAYER_JPY_TEMPORAL);
        if (!sprs.empty() && sprs[0] && sprs[0]->image) {
            if (cache) cache->restore();
            return sprs[0]->image;
        }
        auto background = background_surface_copy();
        if (cache) cache->restore();
        std::shared_ptr<::cw::lib::Object> cardgrp;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
            cardgrp = *p;
        ::cw::sprite::background::layered_draw_ex(cardgrp, background);
        // 一時スプライトは最上位のグループが所有する。 プロセスの生存期
        // 間中の強参照で、以前のエリア・シナリオの画像を保持し続けない。
        auto sprite = std::make_shared<::cw::sprite::background::Jpy1TemporalSprite>();
        sprite->initialize(background, sprite);
        return background;
    }
    if (cache) cache->restore();
    if (cache && cache->m_beforeback && *cache->m_beforeback) return *cache->m_beforeback;
    return make_surface(area_w(), area_h());
}

std::shared_ptr<Surface> scale_surface_value(const std::shared_ptr<Surface>& image)
{
    if (!image) return image;
    try {
        const auto scaled = ::cw::s(V(std::static_pointer_cast<::cw::lib::Object>(image)));
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&scaled.storage()); object && *object)
            if (auto result = std::dynamic_pointer_cast<Surface>(*object)) return result;
        throw std::logic_error("EffectBooster: surface scale returned no Surface");
    } catch (...) {
        // Python と同様、cw.s の失敗を伝播させる。 ネイティ
        // ブ版では再送出前に診断情報を記録し、古い寸法を黙って使わない。
        ::cw::util::print_ex(std::nullopt);
        throw;
    }
}

I ticks_now()
{
    if (const auto* p = std::get_if<I>(&(::cw::g_cwpy ? ::cw::g_cwpy->m_starttick : V{}).storage())) {
        (void)p;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<Surface> make_surface(I width, I height, std::tuple<I,I,I> color)
{
    auto image = std::make_shared<Surface>();
    image->size.width = std::max<I>(0, width);
    image->size.height = std::max<I>(0, height);
    const auto [r,g,b] = color;
    if (image->size.width > 0 && image->size.height > 0) {
        image->rgba.resize(static_cast<std::size_t>(image->size.width * image->size.height * 4));
        ::cw::lib::simd_image::fill_rgba(
            image->rgba.data(), image->rgba.size() / 4U,
            static_cast<std::uint8_t>(std::clamp<I>(r, 0, 255)),
            static_cast<std::uint8_t>(std::clamp<I>(g, 0, 255)),
            static_cast<std::uint8_t>(std::clamp<I>(b, 0, 255)), 255);
    }
    return image;
}

// Py は JPY の合成背景・部品を Surface(...).convert()
// で作る。 格納されたアルファのバイトは読み取って使うアルファチャンネルではない。
// JPTX と透明な一時クリップ画像に使う汎用 RGBA の生成処理は変更しない。
std::shared_ptr<Surface> make_rgb_surface(I width, I height,
                                         std::tuple<I,I,I> color = {0,0,0})
{
    auto image = make_surface(width, height, color);
    image->has_pixel_alpha = false;
    return image;
}

std::shared_ptr<Surface> copy_surface(const std::shared_ptr<Surface>& source)
{
    if (!source) return make_surface(0, 0);
    return std::make_shared<Surface>(*source);
}

struct ResourceCacheEntry : ::cw::lib::Object {
    S source_path{};
    S scenario_scope{};
    std::int64_t mtime{};
    std::shared_ptr<Surface> image{};
};

S current_resource_scope()
{
    if (const auto data = scenario_data()) return ::cw::util::normalize_path(data->m_scedir);
    return {};
}

std::optional<std::int64_t> resource_mtime(const S& path)
{
    const auto native_path = std::filesystem::u8path(path);
    // Python の stat と同様、論理リソースの参照先を調べる。マウント済みファイ
    // ルにはネイティブのディレクトリー項目がないため、 内容を実体化せず Storage
    // のメタデータを使う。この値は同じバックエンド内でだけ比較し、日付としては扱わない。
    if (::cardwirth::platform::is_mounted_user_path(native_path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native_path);
        // ネイティブの Storage は、時刻情報の欠落と実際に取得した基
        // 準時刻を区別する。 有効な時刻がなければキャッシュを省き、マウン
        // ト済みリソースの全版を時刻ゼロの同じファイルとして扱わない。 旧
        // プロバイダーでは有効フラグがなくても、ゼロでない時刻は維持する。
        return status && status->is_file() &&
               (status->modified_valid || status->modified != 0)
            ? std::optional<std::int64_t>(status->modified) : std::nullopt;
    }
    std::error_code error;
    const auto mtime = std::filesystem::last_write_time(native_path, error);
    return error ? std::nullopt : std::optional<std::int64_t>{mtime.time_since_epoch().count()};
}

S resource_cache_key(const S& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(path), error).lexically_normal();
    // Python は (_JpySubImage, scale, mask, path) をキー
    // に使う。 ValueMap は文字列キーなので、画像・音声のキーと区別する名前空間を付ける。
    const S normalized = error ? ::cw::util::normalize_path(path)
                              : ::cw::lib::encoding::path_to_utf8(absolute);
    return S("\x1f" "EffectBooster|") + normalized + "|" + std::to_string(::cw::g_UP_SCR);
}

std::shared_ptr<Surface> load_resource_cache(const S& path)
{
    const auto data = scenario_data();
    if (!data || !::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return nullptr;
    const auto key = resource_cache_key(path);
    const auto mtime = resource_mtime(path);
    if (!mtime) return nullptr;
    const auto it = data->m_resource_cache.find(key);
    if (it == data->m_resource_cache.end()) return nullptr;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage());
    const auto entry = object && *object ? std::dynamic_pointer_cast<ResourceCacheEntry>(*object) : nullptr;
    if (!entry || entry->scenario_scope != current_resource_scope() ||
        // Py と同様、リソースがキャッシュより新しい場合だけ無効化する。
        entry->mtime < *mtime || !entry->image) {
        data->m_resource_cache.erase(it);
        return nullptr;
    }
    return copy_surface(entry->image);
}

void save_resource_cache(const S& path, const std::shared_ptr<Surface>& image)
{
    const auto data = scenario_data();
    if (!image || !data || !::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return;
    const auto key = resource_cache_key(path);
    const auto mtime = resource_mtime(path);
    if (!mtime) return;
    // Python と同様、ScenarioData のバイト上限を背景・音声と共
    // 有する。 別の 256 件のマップでは大きな RGBA 画像を多数保持し、
    // SystemData のメモリー不足・生存期間の管理から消去できなくなる。
    data->sweep_resourcecache(static_cast<I>(image->rgba.size()));
    auto entry = std::make_shared<ResourceCacheEntry>();
    entry->source_path = ::cw::util::normalize_path(path);
    entry->scenario_scope = current_resource_scope();
    entry->mtime = *mtime;
    entry->image = copy_surface(image);
    data->m_resource_cache[key] = V(std::static_pointer_cast<::cw::lib::Object>(entry));
}

void invalidate_jpy_resource_cache()
{
    const auto data = scenario_data();
    if (!data) return;
    for (auto it = data->m_resource_cache.begin(); it != data->m_resource_cache.end();) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage());
        const auto entry = object && *object ? std::dynamic_pointer_cast<ResourceCacheEntry>(*object) : nullptr;
        const bool jpy_part = entry && lower(std::get<1>(::cw::util::splitext(entry->source_path))) == ".jpy1";
        // 音声キャッシュのキーは通常のファイル名であり、POSIX では「|」を含められ
        // る。 背景のタプルキー形式として解釈するのは Surface 値だけとする。
        const bool jpy_background = object && *object && std::dynamic_pointer_cast<Surface>(*object) &&
            ::cw::sprite::background::is_jpy_surface_cache_key(it->first);
        if (jpy_part || jpy_background)
            it = data->m_resource_cache.erase(it);
        else ++it;
    }
    // Python の選択的な削除と同様、他のリソースと累積バイト数は保持
    // する。 次の共通の整理処理で、安全側にまとめて消去する場合はある。
}

std::shared_ptr<Surface> decode_image_file(const S& path)
{
    const auto decoded = ::cw::lib::imagecodec::decode_rgba_file(std::filesystem::u8path(path));
    if (decoded.width <= 0 || decoded.height <= 0) return make_surface(0, 0);
    const auto expected = static_cast<std::size_t>(decoded.width) * static_cast<std::size_t>(decoded.height) * 4u;
    if (decoded.pixels.size() != expected) throw ::cw::lib::imagecodec::DecodeError("decoded image has invalid RGBA size");
    auto image = make_surface(decoded.width, decoded.height);
    image->rgba = decoded.pixels;
    return image;
}

void ensure_rgba(const std::shared_ptr<Surface>& image)
{
    if (!image) return;
    const auto count = static_cast<std::size_t>(std::max<I>(0, image->size.width) * std::max<I>(0, image->size.height) * 4);
    if (image->rgba.size() != count) image->rgba.assign(count, 0);
}

void apply_colorkey(const std::shared_ptr<Surface>& image)
{
    if (!image || image->size.width <= 0 || image->size.height <= 0) return;
    ensure_rgba(image);
    if (image->rgba.size() < 4) return;
    const auto r = image->rgba[0], g = image->rgba[1], b = image->rgba[2];
    image->has_colorkey = true;
    image->colorkey = {r, g, b};
    ::cw::lib::simd_image::colorkey_alpha_zero(
        image->rgba.data(), image->rgba.size() / 4U, r, g, b);
}

void save_surface_file(const std::shared_ptr<Surface>& image, const std::filesystem::path& path)
{
    if (!image) throw ::cw::lib::imagecodec::EncodeError("JPDC: missing capture surface");
    // pygame 1.9.6 と同様、ドットの有無を要求せずパス末尾の文字で
    // 判定する。 それ以外は拡張子なしも含め、RLE TGA として保存する。
    const auto name = lower(::cw::lib::encoding::path_to_utf8(path));
    const bool named_format = name.size() > 3;
    ::cw::lib::imagecodec::RGBAImage rgba{image->size.width, image->size.height, image->rgba};
    ::cw::lib::Bytes encoded;
    if (named_format && name.ends_with("png")) encoded = ::cw::lib::imagecodec::encode_png(rgba);
    else if (named_format && name.ends_with("bmp")) encoded = ::cw::lib::imagecodec::encode_bmp(rgba);
    else if (named_format && (name.ends_with("jpg") || name.ends_with("jpeg")))
        encoded = ::cw::lib::imagecodec::encode_jpeg(rgba, image->has_pixel_alpha);
    else encoded = ::cw::lib::imagecodec::encode_tga(rgba, image->has_pixel_alpha);
    // C++ の論理・暗号化ストレージは追加のプラットフォーム境界である。 一度エンコ
    // ードしてから、マウント先への不可分な書き込みも含む共通ライターを使う。 pygame.image.save
    // と同様、キャッシュ無効化より前にエラーを伝播させる。
    ::cw::util::write_file(::cw::lib::encoding::path_to_utf8(path), V(std::move(encoded)),
                          nullptr, "wb", std::nullopt);
}

std::optional<std::filesystem::path> safe_child_path(const std::filesystem::path& root, const S& filename)
{
    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(root, error).lexically_normal();
    if (error) return std::nullopt;
    const auto candidate = std::filesystem::absolute(absolute_root / std::filesystem::u8path(filename), error).lexically_normal();
    if (error) return std::nullopt;
    const auto relative = candidate.lexically_relative(absolute_root);
    if (relative.empty()) return std::nullopt;
    const auto first = *relative.begin();
    if (first == std::filesystem::path("..")) return std::nullopt;
    return candidate;
}

void blit_surface(const std::shared_ptr<Surface>& destination, const std::shared_ptr<Surface>& source,
                  I x, I y, I mode = 0)
{
    if (!destination || !source) return;
    ensure_rgba(destination);
    ensure_rgba(source);
    if (source->size.width <= 0 || source->size.height <= 0) return;
    if (mode == 0) {
        // 通常の画像転送と同じ RGB・全体アルファ・透過色の規則を使う。 pygame の SRCALPHA
        // は全体の不透明度を無視し、RGB はここで set_alpha を使う。
        ::cw::image::blit_surface_pixels(destination, source, x, y);
        return;
    }
    if (mode != 1 && mode != 2) throw std::invalid_argument("EffectBooster: invalid RGB blend mode");
    if (destination == source) {
        // pygame の重なりのある画像転送は、元の入力ピクセルを読む。 パックされた vector
        // には独立したロック済みの入力ビューがないため、その場合はコピーを取る。
        blit_surface(destination, copy_surface(source), x, y, mode);
        return;
    }
    const I sx=x<0 ? (x<=-source->size.width ? source->size.width : -x) : 0;
    const I sy=y<0 ? (y<=-source->size.height ? source->size.height : -y) : 0;
    const I dx=std::max<I>(0,x),dy=std::max<I>(0,y);
    if (dx>=destination->size.width || dy>=destination->size.height) return;
    const I width=std::min(source->size.width-sx,destination->size.width-dx);
    const I height=std::min(source->size.height-sy,destination->size.height-dy);
    if (width<=0 || height<=0) return;
    const auto si=(static_cast<std::size_t>(sy)*source->size.width+sx)*4U;
    const auto di=(static_cast<std::size_t>(dy)*destination->size.width+dx)*4U;
    ::cw::lib::simd_image::blend_rgb_rgba(source->rgba.data()+si,source->size.width*4U,
        destination->rgba.data()+di,destination->size.width*4U,width,height,mode==2);
}

void alpha_blit_surface(const std::shared_ptr<Surface>& destination, const std::shared_ptr<Surface>& source, I x, I y)
{
    if (!destination || !source) return;
    ensure_rgba(destination);
    ensure_rgba(source);
    const I source_x = std::max<I>(0, -x);
    const I source_y = std::max<I>(0, -y);
    const I width = std::min<I>(source->size.width - source_x,
                                destination->size.width - std::max<I>(0, x));
    const I height = std::min<I>(source->size.height - source_y,
                                 destination->size.height - std::max<I>(0, y));
    if (width <= 0 || height <= 0) return;
    const auto source_offset = static_cast<std::size_t>(source_y * source->size.width + source_x) * 4U;
    const auto destination_offset = static_cast<std::size_t>(
        std::max<I>(0, y) * destination->size.width + std::max<I>(0, x)) * 4U;
    ::cw::lib::simd_image::alpha_blend_rgba(
        source->rgba.data() + source_offset, static_cast<std::size_t>(source->size.width) * 4U,
        destination->rgba.data() + destination_offset, static_cast<std::size_t>(destination->size.width) * 4U,
        static_cast<std::size_t>(width), static_cast<std::size_t>(height));
}

std::shared_ptr<Surface> crop_surface(const std::shared_ptr<Surface>& source, I x, I y, I width, I height)
{
    const auto empty = [] {
        auto image = make_surface(0, 0);
        image->has_pixel_alpha = false; // Py と同様、空の Surface は convert_alpha() ではなく convert() を使う。
        return image;
    };
    if (!source || width <= 0 || height <= 0) return empty();
    // Integer は pygame.Rect より広い。 クリッピング前に桁あ
    // ふれする端点だけを飽和させ、その境界で符号付き整数の桁あふれを起こさない。
    const auto end = [](I start, I length) {
        return start > std::numeric_limits<I>::max() - length
            ? std::numeric_limits<I>::max() : start + length;
    };
    const I left = std::max<I>(0, x);
    const I top = std::max<I>(0, y);
    const I right = std::min<I>(source->size.width, end(x, width));
    const I bottom = std::min<I>(source->size.height, end(y, height));
    if (right <= left || bottom <= top) return empty();
    auto result = make_surface(right-left, bottom-top);
    result->has_pixel_alpha = source->has_pixel_alpha;
    result->pixel_bits = source->pixel_bits;
    result->surface_alpha = source->surface_alpha;
    result->surface_alpha_enabled = source->surface_alpha_enabled;
    result->has_colorkey = source->has_colorkey;
    result->colorkey = source->colorkey;
    result->scr_scale = source->scr_scale;
    result->bmpdepthis1 = source->bmpdepthis1;
    result->depth1 = source->depth1;
    result->logical_scale = source->logical_scale;
    result->draw_scale = source->draw_scale;
    result->known_opaque = source->known_opaque;
    // 言語差として、Surface はパックされたピクセルを所有し、pygame の
    // 親ビュー・行間隔を持たない。 交差部分をコピーして生の RGBA と形式フラグ
    // を保持する。ここでアルファ転送すると、部分画像が不透明な黒へ合成されてしまう。
    const auto row_bytes = static_cast<std::size_t>(right - left) * 4U;
    for (I row = 0; row < bottom - top; ++row) {
        const auto offset = (static_cast<std::size_t>(top + row) *
                             static_cast<std::size_t>(source->size.width) +
                             static_cast<std::size_t>(left)) * 4U;
        if (offset > source->rgba.size() || row_bytes > source->rgba.size() - offset)
            throw std::invalid_argument("EffectBooster crop: incomplete pixel storage");
        std::copy_n(source->rgba.data() + offset, row_bytes,
                    result->rgba.data() + static_cast<std::size_t>(row) * row_bytes);
    }
    return result;
}

std::shared_ptr<Surface> crop_surface_fixed(const std::shared_ptr<Surface>& source, I x, I y, I width, I height,
                                             std::tuple<I, I, I> fill = {255, 255, 255})
{
    if (width <= 0 || height <= 0) return make_surface(0, 0);
    // Py の JPDC と同様、画像内に収まる矩形は生の部分画像
    // を使う。 画像外へはみ出す要求だけを不透明な白へ合成する。
    if (source && x >= 0 && y >= 0 && x <= source->size.width && y <= source->size.height &&
        width <= source->size.width - x && height <= source->size.height - y)
        return crop_surface(source, x, y, width, height);
    auto result = make_surface(width, height, fill);
    if (!source) return result;
    const auto clipped = crop_surface(source, x, y, width, height);
    if (clipped->size.width > 0 && clipped->size.height > 0) {
        // pygame の JPDC 互換経路と同様、入力画像の外から始
        // まる要求も含め、 切り取った交差部分を (0,0) に置く。
        blit_surface(result, clipped, 0, 0);
    }
    return result;
}

std::shared_ptr<Surface> resize_surface(const std::shared_ptr<Surface>& source, I width, I height)
{
    if (!source || width <= 0 || height <= 0) return make_surface(0, 0);
    auto result = make_surface(width, height);
    result->has_pixel_alpha = source->has_pixel_alpha;
    result->pixel_bits = source->pixel_bits;
    ensure_rgba(source);
    ensure_rgba(result);
    for (I y = 0; y < height; ++y) for (I x = 0; x < width; ++x) {
        const I sx = std::min<I>(source->size.width - 1, x * source->size.width / width);
        const I sy = std::min<I>(source->size.height - 1, y * source->size.height / height);
        if (sx < 0 || sy < 0) continue;
        const auto si = static_cast<std::size_t>((sy * source->size.width + sx) * 4);
        const auto di = static_cast<std::size_t>((y * width + x) * 4);
        if (si + 3 < source->rgba.size() && di + 3 < result->rgba.size()) std::copy_n(source->rgba.data()+si, 4, result->rgba.data()+di);
    }
    return result;
}

I utf8_length(const S& text)
{
    I count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = (c & 0x80u) == 0 ? 1 : (c & 0xe0u) == 0xc0 ? 2 : (c & 0xf0u) == 0xe0 ? 3 : (c & 0xf8u) == 0xf0 ? 4 : 1;
        i += std::min(n, text.size()-i);
        ++count;
    }
    return count;
}

std::optional<I> optional_value_int(const V& value)
{
    if (value.is_none()) return std::nullopt;
    if (const auto* p = std::get_if<I>(&value.storage())) return *p;
    if (const auto* p = std::get_if<D>(&value.storage())) {
        try { return p->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return std::nullopt; }
    }
    if (const auto* p = std::get_if<R>(&value.storage())) {
        if (!std::isfinite(*p)) return std::nullopt;
        const R truncated = std::trunc(*p);
        const R max_exclusive = static_cast<R>((std::numeric_limits<I>::max)()) + 1.0;
        const R min_inclusive = static_cast<R>((std::numeric_limits<I>::lowest)());
        if (truncated >= max_exclusive || truncated < min_inclusive) return std::nullopt;
        return static_cast<I>(truncated);
    }
    if (const auto* p = std::get_if<S>(&value.storage())) {
        try { return std::stoll(*p); } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

std::tuple<I,I,I> parse_color(const S& source, std::tuple<I,I,I> fallback)
{
    static const std::map<S, std::tuple<I,I,I>> names = {
        {"black",{0,0,0}}, {"maroon",{128,0,0}}, {"green",{0,128,0}}, {"olive",{128,128,0}},
        {"navy",{0,0,128}}, {"purple",{128,0,128}}, {"teal",{0,128,128}}, {"gray",{128,128,128}},
        {"silver",{192,192,192}}, {"red",{255,0,0}}, {"lime",{0,255,0}}, {"yellow",{255,255,0}},
        {"blue",{0,0,255}}, {"fuchsia",{255,0,255}}, {"aqua",{0,255,255}}, {"white",{255,255,255}}
    };
    const S value = ::cw::lib::python_strip(source);
    if (const auto it = names.find(value); it != names.end()) return it->second;
    const auto characters = ::cw::lib::regex_grapheme::decode_utf8(value);
    if (characters.size() == 7 && characters.front() == '$') {
        // 文字単位で切り出し、不正な$RRGGBBはPython版のValueError
        // に相当する整数変換エラーをそのまま伝える。
        std::vector<std::size_t> offsets{0};
        for (const auto c : characters)
            offsets.push_back(offsets.back() + (c <= 0x7f ? 1 : c <= 0x7ff ? 2 : c <= 0xffff ? 3 : 4));
        const auto component = [&](std::size_t first) {
            return ::cw::lib::require_python_integer(
                value.substr(offsets[first], offsets[first + 2] - offsets[first]), 16);
        };
        return {component(1), component(3), component(5)};
    }
    if (const auto n = ::cw::lib::python_hex_integer_low_bits(value, 24))
        return {static_cast<I>((*n >> 16) & 0xff), static_cast<I>((*n >> 8) & 0xff), static_cast<I>(*n & 0xff)};
    return fallback;
}

std::vector<I> parse_ints(const S& source)
{
    std::vector<I> result;
    std::stringstream stream(source);
    S part;
    while (std::getline(stream, part, ',')) {
        const auto parsed = parse_int_optional(part, false);
        result.push_back(parsed.value_or(0));
    }
    // Python の str.split(",") と同様、連続するカンマも含め末尾の空項目を残
    // す。 EffectBoosterConfig.get_ints では不正な項目をゼロとする。
    if (source.empty() || source.back() == ',') result.push_back(0);
    return result;
}


bool is_sound_path(const S& path)
{
    const auto ext = lower(std::filesystem::u8path(path).extension().generic_string());
    // EffectBooster のリソースは、CardWirthPy の EXTS_SND の規則に従う。
    return ext == ".wav" || ext == ".wave" || ext == ".ogg";
}

std::chrono::steady_clock::time_point now_tick()
{
    // pygame.get_ticks と Engine のフレーム時刻は、どちらも整数ミリ
    // 秒を使う。 ここでナノ秒を保持すると、経過時間の引き算が 1ms 変わる場合がある。
    return std::chrono::steady_clock::time_point{std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())};
}

std::chrono::steady_clock::time_point frame_start_tick()
{
    if (::cw::g_cwpy) {
        if (const auto* tick = std::get_if<I>(&::cw::g_cwpy->m_starttick.storage()))
            return std::chrono::steady_clock::time_point{std::chrono::milliseconds(*tick)};
    }
    // ネイティブの画面なし画像処理にはゲームフレームがないため、その境界だけで現在時刻
    // を使う。 Engine が動作中なら、上の Py と同じフレーム開始時刻を使う。
    return now_tick();
}

std::mutex s_backend_mutex;
std::shared_ptr<EffectBoosterBackend> s_backend;

std::shared_ptr<EffectBoosterBackend> backend()
{
    std::lock_guard lock(s_backend_mutex);
    if (!s_backend) s_backend = make_default_backend();
    return s_backend;
}
}

void set_backend(std::shared_ptr<EffectBoosterBackend> arg_backend)
{
    std::lock_guard lock(s_backend_mutex);
    s_backend = std::move(arg_backend);
}

std::shared_ptr<EffectBoosterBackend> make_default_backend()
{
    auto result = std::make_shared<EffectBoosterBackend>();
    result->wait = [](I waittime, std::shared_ptr<AnimationCounter> anime) {
        if (waittime > 0) std::this_thread::sleep_for(std::chrono::milliseconds(waittime));
        // Engine が動作中なら、操作を受け付ける待機は wait_effectbooster
        // の共通 EventHandlerForEffectBooster
        // で扱う。 実行環境なしのバックエンドはプラットフォー
        // ムに依存させず、画面なしのデコード・テストだけに使う。
        if (anime) anime->m_time_elapsed = 0;
    };
    result->play_sound = [](const S& path, bool from_scenario) {
        // Python の sound.play(True) はループではなく「シナリオ由
        // 来」を意味する。 全 OS で一つの共通 SoundInterface を通す。
        if (::cw::g_cwpy)
            ::cw::g_cwpy->_play_sound_with(path, from_scenario, std::nullopt, 100, 1, 0, 0);
    };
    result->capture_background = [] {
        const auto background = ::cw::g_cwpy && ::cw::g_cwpy->m_background
            ? static_cast<::cw::sprite::base::CWPySprite*>(::cw::g_cwpy->m_background.get()) : nullptr;
        auto result_image = background && background->m_image ? copy_surface(background->m_image)
            : make_surface(std::get<0>(::cw::g_SIZE_AREA), std::get<1>(::cw::g_SIZE_AREA));
        if (::cw::thread::CWPy::s_cardgrp)
            ::cw::sprite::background::layered_draw_ex(::cw::thread::CWPy::s_cardgrp, result_image);
        return result_image;
    };
    result->compose_temporal = [](std::shared_ptr<Surface>& target) {
        if (!target || !::cw::thread::CWPy::s_topgrp) return;
        const auto sprites = ::cw::sprite::background::sprites_from_layer(
            ::cw::thread::CWPy::s_topgrp, ::cw::g_LAYER_JPY_TEMPORAL);
        for (const auto& sprite : sprites)
            if (sprite && sprite->image) alpha_blit_surface(target, sprite->image, sprite->rect.x, sprite->rect.y);
    };
    result->set_title = [](const S&) {};
    return result;
}

void AnimationCounter::initialize()
{
    m_count = 0;
    m_skip_count = 0;
    m_time_elapsed = 0;
    m_all_cut = false;
}

std::shared_ptr<AnimationCounter> AnimationCounter::get_reloadcounter()
{
    auto counter = std::make_shared<AnimationCounter>();
    counter->m_skip_count = m_count - 1;
    counter->m_time_elapsed = m_time_elapsed;
    return counter;
}

bool AnimationCounter::countup()
{
    ++m_count;
    return !m_all_cut && m_skip_count < m_count;
}

void CutAnimation::initialize()
{
    AnimationCounter::initialize();
    m_all_cut = true;
}

std::shared_ptr<AnimationCounter> CutAnimation::get_reloadcounter()
{
    // this から非所有の shared_ptr を作らない。再読み込みカウンターは要求し
    // た呼び出し元より長く残る場合がある。 Py の self 返却とは異なり、所有された値
    // のコピーを返す。 BackGround::update_scale は、予約した再読
    // み込みコールバックに元のカウンターへの参照ではなく、そのスナップショットを保持する。
    return std::make_shared<CutAnimation>(*this);
}

std::optional<std::tuple<I,I,I>> parse_config_color_optional(const S& source)
{
    static const std::map<S, std::tuple<I,I,I>> names = {
        {"black",{0,0,0}}, {"maroon",{128,0,0}}, {"green",{0,128,0}}, {"olive",{128,128,0}},
        {"navy",{0,0,128}}, {"purple",{128,0,128}}, {"teal",{0,128,128}}, {"gray",{128,128,128}},
        {"silver",{192,192,192}}, {"red",{255,0,0}}, {"lime",{0,255,0}}, {"yellow",{255,255,0}},
        {"blue",{0,0,255}}, {"fuchsia",{255,0,255}}, {"aqua",{0,255,255}}, {"white",{255,255,255}}
    };
    const S value = lower(source);
    if (const auto found = names.find(value); found != names.end()) return found->second;
    // EffectBoosterConfig.get_color() は意図的に先頭
    // 文字を無視し、[1:3]・[3:5]・[5:7] を読む。 この動作は接頭辞な
    // しの値で結果に現れ、JPTX の get_fontcolor() とは異なる。
    try {
        const auto utf16=::cw::lib::encoding::utf8_to_utf16(value,::cw::lib::encoding::ByteOrder::Little);
        std::vector<std::size_t> offsets;
        for (std::size_t i=0;i<utf16.size();i+=2) {
            offsets.push_back(i);
            const auto c=static_cast<unsigned char>(utf16[i])|(static_cast<unsigned char>(utf16[i+1])<<8);
            if (0xd800<=c && c<=0xdbff) i+=2;
        }
        offsets.push_back(utf16.size());
        const auto component=[&](std::size_t offset)->I {
            if (offset>=offsets.size()-1) throw std::invalid_argument("short EffectBooster color");
            const auto end=std::min(offset+2,offsets.size()-1);
            const auto part=::cw::lib::encoding::utf16_to_utf8(::cw::lib::Bytes(utf16.begin()+offsets[offset],utf16.begin()+offsets[end]),::cw::lib::encoding::ByteOrder::Little);
            return ::cw::lib::require_python_integer(part,16);
        };
        return std::tuple<I,I,I>{component(1),component(3),component(5)};
    } catch (...) { return std::nullopt; }
}

std::tuple<I,I,I> parse_config_color(const S& source,std::tuple<I,I,I> fallback)
{
    return parse_config_color_optional(source).value_or(fallback);
}

bool CutAnimation::countup() { return false; }

void EffectBoosterConfig::initialize(S arg_path, S arg_firstsection, bool arg_section_ignorecase)
{
    m_path_typed = ::cw::util::normalize_path(std::move(arg_path));
    m_path = V(m_path_typed);
    m_dirdepth = 0;
    m_section_ignorecase_typed = arg_section_ignorecase;
    m_orderedsecs_typed.clear();
    m_sections_typed.clear();
    m__orderedsecs.clear();
    m__sections.clear();
    const S ext = lower(std::filesystem::u8path(m_path_typed).extension().generic_string());
    const auto native_path = std::filesystem::u8path(m_path_typed);
    ::cw::lib::Bytes bytes;
    // マウントされたシナリオリソースは、この入口の前にファイルへ展開しない。
    // 平文への代替経路を追加せず、既存の Storage 読み取りを使う。
    if (::cardwirth::platform::is_mounted_user_path(native_path)) {
        const auto stored = ::cardwirth::platform::read_mounted_user_file(native_path);
        if (!stored) throw std::runtime_error("EffectBoosterConfig: cannot read " + m_path_typed);
        bytes.reserve(stored->size());
        for (const auto byte : *stored) bytes.push_back(std::to_integer<std::uint8_t>(byte));
    } else {
        std::ifstream input(native_path, std::ios::binary);
        if (!input.is_open()) throw std::runtime_error("EffectBoosterConfig: cannot open " + m_path_typed);
        std::array<char, 65536> buffer;
        for (;;) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            bytes.insert(bytes.end(), buffer.data(), buffer.data() + input.gcount());
            if (input.bad() || (input.fail() && !input.eof()))
                throw std::runtime_error("EffectBoosterConfig: cannot read " + m_path_typed);
            if (input.eof()) break;
        }
    }
    const S decoded = ::cw::lib::encoding::cp932_to_utf8(bytes, ::cw::lib::encoding::ErrorPolicy::Replace);
    std::istringstream stream(decoded);
    S current;
    std::map<S, S> discarded_section;
    std::map<S, S>* current_values = nullptr;
    bool in_jptxtxt = false;
    bool has_jptxtxt = false;
    S jptxtxt;
    S line;
    const bool old_card = version_lessthan("1.30", ::cw::g_HINT_CARD);
    while (std::getline(stream, line)) {
        const bool had_linefeed = !stream.eof();
        // Py のバイナリファイルの行走査と同様、置換するのは CRLF だけとする。
        // JPTX の末尾に単独の CR があれば、元のテキストの一部として残す。
        if (had_linefeed && !line.empty() && line.back() == '\r') line.pop_back();
        if (!in_jptxtxt && !line.empty() && (line.front() == '#' || line.front() == ';')) continue;
        const S lower_line = lower(line);
        const S stripped = ::cw::lib::python_strip(lower_line);
        if (stripped == "[jptx:end]") { in_jptxtxt = false; break; }
        if (stripped == "[jptx:begin]") { in_jptxtxt = true; has_jptxtxt = true; continue; }
        if (in_jptxtxt) { jptxtxt += line; if (had_linefeed) jptxtxt += "\n"; continue; }
        const auto section_end = stripped.find(']');
        if (stripped.size() >= 2 && stripped.front() == '[' && section_end != S::npos && section_end > 1) {
            // Python と同様、全体一致ではなく re.match() の規
            // 則を使い、最初の「]」の後の文字は無視する。 section_ignorecase
            // が false でも小文字化した文字列に対して照合し、
            // そのモードで条件付きになるのは内側の空白除去だけとする。
            current = stripped.substr(1, section_end - 1);
            current = arg_section_ignorecase ? lower(::cw::lib::python_strip(current)) : current;
            m_orderedsecs_typed.push_back(current);
            if (old_card) {
                m_sections_typed[current] = {};
                current_values = &m_sections_typed[current];
            } else {
                const auto [it, inserted] = m_sections_typed.try_emplace(current);
                if (inserted) current_values = &it->second;
                else {
                    // 1.50 互換として、同じセクション名があれば
                    // 再度実行するが、後から出た設定値は使い捨ての辞
                    // 書へ書く。 最初のセクションの値を優先する。
                    discarded_section.clear();
                    current_values = &discarded_section;
                }
            }
            continue;
        }
        // Py と同様、セクション・設定項目の前の Unicode 空白を受け付
        // ける。 ここで項目末尾の空白を除くのは value.strip() と
        // 同じであり、生の JPTX テキストは既に上の別分岐で処理済みである。
        const S candidate = ::cw::lib::python_strip(line);
        const auto colon = candidate.find_first_of(":=");
        if (colon == S::npos || !current_values) continue;
        const S rawkey = candidate.substr(0, colon);
        if (rawkey.empty() || rawkey.find_first_of(" \t") == 0) continue;
        const S key = lower(::cw::lib::python_strip(rawkey));
        S value = ::cw::lib::python_strip(candidate.substr(colon+1));
        // CardWirthPy/EffectBooster
        // の不正な引用符への互換動作を維持する。 先頭が引
        // 用符なら、末尾が引用符でなくても両端を取り除く。
        if (!value.empty() && value.front() == '"')
            value = value.size() >= 2 ? value.substr(1, value.size()-2) : S{};
        auto& section = *current_values;
        if (ext == ".jptx" || !section.contains(key)) section[key] = value;
    }
    if (has_jptxtxt) {
        m_sections_typed["jptx:begin"]["jptx:end"] = jptxtxt;
    }
    if (m_sections_typed.empty() && !arg_firstsection.empty()) {
        m_sections_typed[arg_firstsection] = {};
        m_orderedsecs_typed.push_back(arg_firstsection);
    }
    for (const auto& sec : m_orderedsecs_typed) m__orderedsecs.emplace_back(sec);
    for (const auto& [sec, unused] : m_sections_typed) { (void)unused; m__sections[sec] = V{}; }
}

std::vector<S> EffectBoosterConfig::sections() { return m_orderedsecs_typed; }
bool EffectBoosterConfig::has_section(S arg_name) { return m_sections_typed.contains(arg_name); }

S EffectBoosterConfig::get(S arg_section, S arg_option, S arg_py_default)
{
    const auto sec = m_sections_typed.find(arg_section);
    if (sec == m_sections_typed.end()) return arg_py_default;
    const auto it = sec->second.find(lower(arg_option));
    return it == sec->second.end() ? arg_py_default : it->second;
}

std::optional<S> EffectBoosterConfig::get_2(S s,S o,V d)
{
    const auto raw=get_3(s,o,std::nullopt);
    if (raw) return raw;
    if (d.is_none()) return std::nullopt;
    return value_string(d);
}
std::optional<S> EffectBoosterConfig::get_3(S s, S o, std::optional<S> d)
{
    const auto sec=m_sections_typed.find(s);
    if(sec!=m_sections_typed.end()) { const auto it=sec->second.find(lower(o)); if(it!=sec->second.end()) return it->second; }
    return d;
}

I EffectBoosterConfig::get_int(S s, S o, I d)
{
    const S raw = get(s, o, {});
    if (raw.empty()) return d;
    return parse_int_strict(raw, d);
}
std::optional<I> EffectBoosterConfig::get_int_2(S s, S o, V d) { return get_int_3(s,o,optional_value_int(d)); }
std::optional<I> EffectBoosterConfig::get_int_3(S s, S o, std::optional<I> d)
{
    const S raw=get(s,o,{});
    if(raw.empty())return d;
    const auto parsed = parse_int_optional(raw);
    return parsed ? parsed : d;
}
bool EffectBoosterConfig::get_bool(S s, S o, bool d) { return get_int(s,o,d?1:0) != 0; }

std::tuple<I,I,I> EffectBoosterConfig::get_color(S s, S o, std::tuple<I,I,I> d) { const S raw=get(s,o,{}); return raw.empty()?d:parse_config_color(raw,d); }
std::optional<std::tuple<I,I,I>> EffectBoosterConfig::get_color_2(S s,S o,V d)
{
    std::optional<std::tuple<I,I,I>> fallback;
    if (!d.is_none()) {
        const auto* object=std::get_if<::cw::lib::ObjectPtr>(&d.storage());
        const auto seq=object && *object?std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object):nullptr;
        if (!seq || seq->m_values.size()!=3) throw std::invalid_argument("color default requires a tuple");
        fallback=std::tuple<I,I,I>{value_int(seq->m_values[0],0),value_int(seq->m_values[1],0),value_int(seq->m_values[2],0)};
    }
    return get_color_3(s,o,fallback);
}
std::optional<std::tuple<I,I,I>> EffectBoosterConfig::get_color_3(S s,S o,std::optional<std::tuple<I,I,I>> d)
{
    const auto raw=get_3(s,o,std::nullopt);
    if (!raw) return d;
    const auto parsed=parse_config_color_optional(*raw);
    return parsed?parsed:d;
}

template<class Tuple>
std::optional<Tuple> ints_optional(const std::optional<S>& raw, I length, std::optional<Tuple> fallback)
{
    // 設定項目がなければ None、明示的に空ならゼロのトークンとする。
    if (!raw) return fallback;
    auto values=parse_ints(*raw);
    if (static_cast<I>(values.size()) < length) return fallback;
    Tuple out{};
    [&]<std::size_t... N>(std::index_sequence<N...>) { ((std::get<N>(out)=values[N]),...); }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    return out;
}

std::tuple<I,I> EffectBoosterConfig::get_ints(S s,S o,V l,std::tuple<I,I> d) { return ints_optional<std::tuple<I,I>>(get_3(s,o,std::nullopt),value_length(l,2),d).value_or(d); }
std::optional<std::tuple<I,I>> EffectBoosterConfig::get_ints_2(S s,S o,V l,V d) { (void)d; return ints_optional<std::tuple<I,I>>(get_3(s,o,std::nullopt),value_length(l,2),std::nullopt); }
std::tuple<I,I,I> EffectBoosterConfig::get_ints_3(S s,S o,V l,std::tuple<I,I,I> d) { return ints_optional<std::tuple<I,I,I>>(get_3(s,o,std::nullopt),value_length(l,3),d).value_or(d); }
std::optional<std::tuple<I,I,I>> EffectBoosterConfig::get_ints_4(S s,S o,V l,V d) { (void)d; return ints_optional<std::tuple<I,I,I>>(get_3(s,o,std::nullopt),value_length(l,3),std::nullopt); }
std::tuple<I,I,I,I> EffectBoosterConfig::get_ints_5(S s,S o,V l,std::tuple<I,I,I,I> d) { return ints_optional<std::tuple<I,I,I,I>>(get_3(s,o,std::nullopt),value_length(l,4),d).value_or(d); }
std::optional<std::tuple<I,I,I,I>> EffectBoosterConfig::get_ints_6(S s,S o,V l,V d) { (void)d; return ints_optional<std::tuple<I,I,I,I>>(get_3(s,o,std::nullopt),value_length(l,4),std::nullopt); }
std::optional<std::vector<I>> EffectBoosterConfig::get_ints_7(
    S s, S o, I length, std::optional<std::vector<I>> fallback)
{
    const auto raw = get_3(s, o, std::nullopt);
    if (!raw) return fallback;
    auto values = parse_ints(*raw);
    const auto count = static_cast<I>(values.size());
    if (length > count) return fallback;
    // Python の seq[:length] と同様、負の終端は末尾から数え、最小をゼロ
    // に収める。 負の length に対して begin()+length を作らない。
    const auto end = length < 0 ? std::max<I>(0, count + length) : length;
    values.resize(static_cast<std::size_t>(end));
    return values;
}

void JpyCache::initialize()
{
    m_before.reset(); m_beforeback.reset(); m_beforerect.reset();
    m_beforerect_typed.reset(); m_img.clear(); m_img_typed.clear();
    m_pos_noscale.reset(); m_pos_noscale_typed.reset();
}

void JpyCache::restore()
{
    // Py は before が背景と矩形を持つことをアサートする。 ネイティブ版で
    // は完全なスナップショットだけを復元し、不完全な状態は下のリセットで破棄する。
    if (m_before && *m_before && m_beforeback && *m_beforeback && m_beforerect_typed) {
        const auto& r = *m_beforerect_typed;
        blit_surface(*m_beforeback, *m_before, r.x, r.y);
    }
    m_before.reset(); m_beforeback.reset(); m_beforerect.reset(); m_beforerect_typed.reset();
}

void JpyCache::save_position_noscale(std::tuple<I,I> position)
{
    m_pos_noscale = position; m_pos_noscale_typed = position;
}

std::tuple<I,I> JpyCache::load_position_noscale()
{
    return m_pos_noscale_typed.value_or(std::tuple<I,I>{0,0});
}

void JpyCache::save_image(I n, std::shared_ptr<Surface> image)
{
    if (!image) image = make_surface(0,0);
    m_img[n] = image; m_img_typed[n] = image;
}

std::shared_ptr<Surface> JpyCache::load_image(I n)
{
    const auto it = m_img_typed.find(n);
    return it == m_img_typed.end() ? make_rgb_surface(0,0) : copy_surface(it->second);
}

void wait_effectbooster(I waittime, std::shared_ptr<AnimationCounter> doanime)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->is_running()) {
        if (waittime && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->refresh_activeitem();
        std::shared_ptr<::cw::sprite::base::StopTheWorld> stw;
        if (waittime > 0) {
            auto start = static_cast<::cw::lib::Real>(ticks_now()) - (doanime ? static_cast<::cw::lib::Real>(doanime->m_time_elapsed) : 0);
            stw = std::make_shared<::cw::sprite::base::StopTheWorld>();
            stw->initialize(start, waittime);
        } else {
            ::cw::g_cwpy->change_cursor("mouse", false);
        }
        auto previous = ::cw::g_cwpy->m_interrupt_eventhandler;
        auto handler = std::make_shared<::cw::eventhandler::EventHandlerForEffectBooster>();
        handler->initialize();
        // 重ね表示はクリック待ちの間だけ有効にし、例外による終了時も解除する。
        const auto runtime = ::cw::g_cwpy;
        const bool previous_notice = runtime->m_effectbooster_click_wait;
        struct RestoreNotice {
            std::shared_ptr<::cw::thread::CWPy> runtime;
            bool previous;
            ~RestoreNotice() {
                runtime->m_effectbooster_click_wait = previous;
                runtime->add_lazydraw_full();
            }
        } restore_notice{runtime, previous_notice};
        runtime->m_effectbooster_click_wait = waittime <= 0;
        runtime->add_lazydraw_full();
        try {
            if (doanime) doanime->m_time_elapsed = 0;
            ::cw::g_cwpy->set_interrupt_eventhandler(handler);
            ::cw::g_cwpy->clear_selection();
            while (::cw::g_cwpy->is_running() && (waittime <= 0 || (stw && stw->is_waiting())) &&
                   handler->m_running && ::cw::g_cwpy->is_playingscenario()) {
                ::cw::g_cwpy->update_groups({[&] {
                    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sbargrp.storage()); p && *p)
                        return *p;
                    return std::shared_ptr<::cw::lib::Object>{};
                }()});
                // Python版のcanskip省略時の既定値に合わせ、効果待機ではtrueを渡す。
                ::cw::g_cwpy->wait_frame(1, true, std::nullopt, 1000);
                ::cw::g_cwpy->input(false, false, false);
                handler->run();
            }
            // Py と同様、時間指定の効果待機が終わったら次のフレーム間隔を開
            // 始する。 経過時間が既に待機時間を消費している場合も同じとする。
            if (waittime > 0) {
                const auto tick = ticks_now();
                ::cw::g_cwpy->m_starttick = V(tick);
                ::cw::thread::CWPy::s_starttick = tick; // ネイティブ側に保持する starttick の値。
            }
            if (!::cw::g_cwpy->is_playingscenario()) throw ::cw::event::EffectBreakError{};
            if (doanime) doanime->m_time_elapsed = 0;
        } catch (const ScreenRescale&) {
            if (waittime > 0 && doanime) doanime->m_time_elapsed = ticks_now() - static_cast<I>(stw ? stw->m_start_ticks : 0);
            else if (doanime) doanime->m_time_elapsed = 0;
            ::cw::g_cwpy->set_interrupt_eventhandler(previous);
            if (waittime <= 0) ::cw::g_cwpy->change_cursor("arrow", false);
            throw;
        } catch (...) {
            ::cw::g_cwpy->set_interrupt_eventhandler(previous);
            if (waittime <= 0) ::cw::g_cwpy->change_cursor("arrow", false);
            throw;
        }
        ::cw::g_cwpy->set_interrupt_eventhandler(previous);
        if (waittime <= 0) ::cw::g_cwpy->change_cursor("arrow", false);
        return;
    }
    if (doanime) doanime->m_time_elapsed = 0;
    const auto current = backend();
    if (current && current->wait) {
        current->wait(waittime, doanime);
        return;
    }
    if (waittime > 0) std::this_thread::sleep_for(std::chrono::milliseconds(waittime));
    if (doanime) doanime->m_time_elapsed = 0;
}

std::tuple<S,bool> get_filepath_s(S configpath, I dirdepth, S filename, I dirtype, S scedir)
{
    // Python と同様、空のファイル名でも選択したディレクトリーの規則に従う。
    if (dirtype == -1) dirtype = 1;
    std::shared_ptr<::cw::header::CardHeader> inusecard;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        const auto card = ::cw::g_cwpy->m_event->get_inusecard();
        if (card) inusecard = *card;
    }
    auto carddata = carddata_of(inusecard);
    if (scedir.empty()) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_inusecardevent &&
            ::cw::g_cwpy->is_runningevent() && carddata) {
            const auto materials = carddata->gettext("Property/Materials", S{});
            if (!materials.empty()) scedir = ::cw::util::join_yadodir(materials);
        }
        if (scedir.empty()) {
            if (const auto data = scenario_data()) scedir = data->m_scedir;
        }
    }
    const auto mtype_of = [](const S& path) { return is_sound_path(path) ? ::cw::g_M_SND : ::cw::g_M_IMG; };
    const auto resource_path = [](S path) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) path = ::cw::g_cwpy->m_rsrc->get_filepath(path);
        return path;
    };
    const auto find_material = [&](S fpath, S material_name) {
        const auto mtype = mtype_of(fpath);
        auto inuse = ::cw::util::get_inusecardmaterialpath(material_name, mtype, std::nullopt, false);
        if (!inuse.empty()) fpath = inuse;
        else fpath = ::cw::util::get_materialpath(material_name, mtype, scedir, false, false);
        return resource_path(fpath);
    };
    const auto store = [](const S& path) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_background && !path.empty())
            ::cw::g_cwpy->m_background->store_filepath(path, 0);
    };
    const auto inuse_scaled = [&] {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_inusecardevent && carddata)
            return carddata->getbool(".", V("scaledimage"), false);
        return ::cw::g_cwpy && scenario_data() && scenario_data()->m_can_loaded_scaledimage;
    };
    const auto finish_path = [&](const S& joined) -> std::tuple<S, bool> {
        // Py と同様、dirtype 1 の素材解決後も共通の末尾処理を行う。
        auto path = resource_path(joined.empty() ? S{"."} : ::cw::util::normalize_path(joined));
        store(path);
        return {path, inuse_scaled()};
    };
    const S skin = ::cw::g_cwpy ? value_string(::cw::g_cwpy->m_skindir) : S{};
    const auto is_effect = [](const S& name) {
        const auto ext = lower(std::filesystem::u8path(name).extension().generic_string());
        return ext == ".jpy1" || ext == ".jptx" || ext == ".jpdc";
    };
    if (dirtype == 1) {
        if (configpath.empty()) return {"", false};
        auto fpath = join2(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(configpath).parent_path()), filename);
        filename = ::cw::util::relpath(fpath, scedir);
        fpath = find_material(fpath, filename);
        if (!::cw::util::path_is_file(fpath)) return {"", false};
        return finish_path(fpath);
    }
    if (dirtype == 2 || dirtype == 3) {
        const S folder = dirtype == 2 ? S{"Table"} : S{"EffectBooster"};
        const auto dpath = join2(skin, folder);
        S fpath;
        if (is_effect(filename)) fpath = resource_path(join2(dpath, filename));
        else {
            const auto [stem, unused] = ::cw::util::splitext(filename); (void)unused;
            fpath = ::cw::util::find_resource(join2(dpath, stem), mtype_of(filename));
        }
        return {fpath, true};
    }
    if (dirtype == 4) {
        if (::cw::g_cwpy && ::cw::g_cwpy->is_runningevent() && ::cw::g_cwpy->m_event &&
            ::cw::g_cwpy->m_event->m_in_inusecardevent && inusecard && carddata &&
            !carddata->getbool(".", V("scenariocard"), false)) {
            if (const auto mates = carddata->find("Property/Materials"); mates && *mates) {
                S dpath = (*mates)->m_text;
                for (I i = 0; i < dirdepth; ++i) dpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(dpath).parent_path());
                auto fpath = ::cw::util::join_yadodir(join2(dpath, filename));
                if (::cw::util::path_is_file(fpath))
                    return {fpath, carddata->getbool(".", V("scaledimage"), false)};
            }
        }
        // dirtype 1 と異なり、dirtype 4 は空の設定パスも解決する。
        auto dpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(configpath).parent_path());
        for (I i = 0; i < dirdepth; ++i) dpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(dpath).parent_path());
        auto fpath = join2(dpath, filename);
        filename = ::cw::util::relpath(fpath, scedir);
        fpath = find_material(fpath, filename);
        if (!::cw::util::path_is_file(fpath)) return {"", false};
        store(fpath);
        return {fpath, inuse_scaled()};
    }
    if (dirtype == 5) {
        for (const auto& dir : {S{"Sound"}, S{"BgmAndSound"}}) {
            const auto [stem, unused] = ::cw::util::splitext(filename); (void)unused;
            const auto fpath = ::cw::util::find_resource(join2(join2(skin, dir), stem), mtype_of(filename));
            if (!fpath.empty()) return {fpath, true};
        }
        return {"", true};
    }
    S dpath;
    if (dirtype == 6) {
        if (configpath.empty()) return {"", false};
        dpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(configpath).parent_path().parent_path());
    } else if (dirtype == 7) {
        dpath = {};
    } else {
        if (configpath.empty()) return {"", false};
        dpath = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(configpath).parent_path());
    }
    // Python の normpath("") は「.」となるため、空ファイル名に対するこの公開の結果を維持する。
    return finish_path(join2(dpath, filename));
}


S get_filepath_s_find_materialpath(S path) { return ::cw::util::path_is_file(path) ? path : S{}; }

void JpdcImage::initialize(bool mask, S path, std::optional<std::shared_ptr<JpyCache>> cache, I defaultcopymode, std::optional<std::shared_ptr<AnimationCounter>> doanime)
{
    (void)cache;
    auto anime = doanime.value_or(std::make_shared<AnimationCounter>());
    EffectBoosterConfig config; config.initialize(path, "jpdc:init", true);
    auto ints = config.get_ints_5("jpdc:init", "clip", V(4), {0,0,632,420});
    auto [x_noscale,y_noscale,w_noscale,h_noscale] = ints;
    x_noscale = std::max<I>(0,x_noscale); y_noscale = std::max<I>(0,y_noscale);
    if (w_noscale <= 0) w_noscale=std::get<0>(::cw::g_SIZE_AREA); if (h_noscale <= 0) h_noscale=std::get<1>(::cw::g_SIZE_AREA);
    const I x = scale_i(x_noscale), y = scale_i(y_noscale), w = scale_i(w_noscale), h = scale_i(h_noscale);
    m_clip_typed = {x,y,w,h};
    I copy_mode = config.get_int("jpdc:init", "copymode", 0);
    if (!version_lessthan("1.30", ::cw::g_HINT_AREA) && copy_mode == 1) copy_mode = 2;
    if (!copy_mode) copy_mode = defaultcopymode;
    m_copymode_typed = copy_mode; m_path_typed = ::cw::util::normalize_path(std::move(path));
    // Python は BackGround スプライトを含むア
    // ルファ付き画像に cardgrp を描くが、C++ の描画
    // 側は BackGround を cardgrp の外で管理
    // する。 不透明な黒から始めると前景・カードに覆われていない
    // 背景が失われ、特に copymode=1 の部分取得で黒く
    // なったりマスクが不正に見えたりする。 現在の合成済み背景か
    // ら始めて cardgrp のレイヤーを加え、copymode=2
    // ではさらに下で JPY の一時レイヤーを追加する。
    std::shared_ptr<Surface> image = background_surface_copy();
    if (copy_mode == 3) image = make_surface(area_w(), area_h(), {255,255,255});
    else if (::cw::g_cwpy) {
        const auto selection = ::cw::g_cwpy->m_selection;
        ::cw::g_cwpy->clear_selection();
        std::shared_ptr<::cw::lib::Object> cardgrp;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p) cardgrp = *p;
        ::cw::sprite::background::layered_draw_ex(cardgrp, image);
        ::cw::g_cwpy->change_selection(selection, std::nullopt);
        if (copy_mode == 2) {
            std::shared_ptr<::cw::lib::Object> topgrp;
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_topgrp.storage()); p && *p)
                topgrp = *p;
            const auto sprites = ::cw::sprite::background::sprites_from_layer(topgrp, ::cw::g_LAYER_JPY_TEMPORAL);
            for (const auto& sprite : sprites)
                if (sprite && sprite->image) alpha_blit_surface(image, sprite->image, sprite->rect.x, sprite->rect.y);
        }
        if (::cw::g_cwpy->m_background) ::cw::g_cwpy->m_background->m_reload_jpdcimage = false;
    } else {
        const auto current = backend();
        image = current && current->capture_background ? current->capture_background() : image;
        if (copy_mode == 2 && current && current->compose_temporal) current->compose_temporal(image);
    }
    image = crop_surface_fixed(image, x, y, w, h);
    Image::m_image = image;
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    const S filename_raw = config.get("jpdc:init", "savefilename", "");
    S savecomment = config.get("jpdc:init", "savecomment", "");
    const bool playing = ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario();
    if (!filename_raw.empty() && anime && !anime->m_all_cut && playing && !(::cw::g_cwpy && ::cw::g_cwpy->m_update_scaling)) {
        // Python と同様、タイトル変更や置き換える画像の調査より
        // 前に、予約済みの書き込みを待つ。 外部パスも同じ扱いとする。
        if (::cw::g_fsync) ::cw::g_fsync->sync();
        const S filename = ::cw::util::repl_dischar(filename_raw);
        savecomment = [] (S text, const S& file, const S& dir) {
            const auto replace = [](S source, const S& from, const S& to) {
                std::size_t pos = 0;
                while ((pos = source.find(from, pos)) != S::npos) { source.replace(pos, from.size(), to); pos += to.size(); }
                return source;
            };
            text = replace(std::move(text), "%file%", file);
            return replace(std::move(text), "%dir%", dir);
        }(savecomment, filename, ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(m_path_typed).parent_path()));
        if (::cw::g_cwpy && !anime->m_all_cut) {
            const auto title = (savecomment.empty() ? filename : savecomment) + " - " + ::cw::g_cwpy->create_title();
            ::cw::g_cwpy->set_titlebar(title);
        }
        auto save_noscale = image;
        std::shared_ptr<Surface> save_scaled;
        if (::cw::g_UP_SCR != 1.0) {
            save_noscale = ::cw::image::smoothscale(image, {w_noscale, h_noscale}, true, false);
            save_scaled = image;
        }
        S destination = join2(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(m_path_typed).parent_path()), filename);
        bool redirected_to_scenario_log = false;
        S scenario_relative;
        S scenario_temp_root;
        if (const auto data = scenario_data()) {
            const auto scedir = std::filesystem::absolute(std::filesystem::u8path(data->m_scedir)).lexically_normal();
            std::error_code ec;
            const auto abs_dest = std::filesystem::absolute(std::filesystem::u8path(destination), ec).lexically_normal();
            if (!ec) {
                // 共通の relpath は Windows で ntpath.normcase
                // に従いつつ、 出力先の子の名前の表記は維持する。
                const auto relative_text = ::cw::util::relpath(
                    ::cw::lib::encoding::path_to_utf8(abs_dest), ::cw::lib::encoding::path_to_utf8(scedir));
                const auto relative = std::filesystem::u8path(relative_text);
                if (!relative.empty() && relative != "." && !relative.has_root_path() &&
                    *relative.begin() != std::filesystem::path("..")) {
                    const auto temppath = join2(value_string(::cw::g_tempdir), "ScenarioLog/TempFile");
                    scenario_relative = ::cw::lib::encoding::path_to_utf8(relative);
                    scenario_temp_root = temppath;
                    destination = join2(temppath, scenario_relative);
                    redirected_to_scenario_log = true;
                }
            }
        }
        std::vector<S> prior_temporary_images;
        std::set<S> written_images;
        if (redirected_to_scenario_log) {
            // 上書き前にメッセージ・バックログと ScenarioLog の履
            // 歴を保持する。 Python と同様、最初の変更前画像だけを記
            // 録し、一時的な上書き画像がまだなければ元のシナリオ画像を使う。
            ::cw::sprite::message::store_messagelogimage(destination, true);
            if (const auto data = scenario_data()) {
                const S key = ::cw::util::get_keypath(destination);
                if (!data->m_ex_cache.contains(key)) {
                    auto sequence = std::make_shared<::cw::data::VariantSequence>();
                    const S original = join2(data->m_scedir, scenario_relative);
                    const bool from_scenario = !::cw::util::path_is_file(destination) && ::cw::util::path_is_file(original);
                    const auto split = ::cw::util::splitext(from_scenario ? original : destination);
                    // Python の不具合を修正する。SCALE_LIST には四
                    // つの倍率があり、さらに基本画像が必要だが、 len(SCALE_LIST)
                    // の確保では後の i+1 への代入に一枠足りない。
                    const I scales[5] = {1, 2, 4, 8, 16};
                    for (const I scale : scales) {
                        const S candidate = scale == 1 ? std::get<0>(split) + std::get<1>(split)
                            : std::get<0>(split) + ".x" + std::to_string(scale) + std::get<1>(split);
                        if (::cw::util::path_is_file(candidate)) {
                            // 履歴のバイト列・パスの代替は Py と同じにする。失
                            // 敗時の境界は意図的に変え、 以前の一時ファイルの削除
                            // を置き換えの書き込み成功後まで遅らせる。 Py は読
                            // み取り後に削除するため保存失敗で旧ファイルを失うが、
                            // マウント先の不可分な書き込みでは旧対象を維持する。
                            sequence->m_values.emplace_back(candidate);
                            try {
                                sequence->m_values.back() = V(::cw::util::read_file_bytes(candidate));
                                if (!from_scenario) prior_temporary_images.push_back(candidate);
                            } catch (const std::runtime_error&) {
                                ::cw::util::print_ex(std::nullopt);
                            }
                        } else sequence->m_values.emplace_back(V{});
                    }
                    data->m_ex_cache[key] = V(std::static_pointer_cast<::cw::lib::Object>(sequence));
                }
            }
        }
        // 出力とリソース無効化はすべて、Python と同じシナリオ内判定の
        // 分岐で行う。 スキン・Table の JPDC も取得はできるが
        // 、シナリオ外の savefilename を作成・上書きしない。
        if (redirected_to_scenario_log) {
            save_surface_file(save_noscale, std::filesystem::u8path(destination));
            written_images.insert(destination);
            if (save_scaled) {
                const auto split = ::cw::util::splitext(destination);
                // Python の %d によるファイル名と同様、整数でない描画倍率は切り捨てる。
                const auto scale = real_to_int(::cw::g_UP_SCR, 1);
                const S scaled_destination = std::get<0>(split) + ".x" + std::to_string(scale) + std::get<1>(split);
                const auto data = scenario_data();
                bool can_scaled = data && data->m_can_loaded_scaledimage;
                if (::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->m_event->m_in_inusecardevent) {
                    if (const auto inuse = ::cw::g_cwpy->m_event->get_inusecard(); inuse && *inuse) {
                        if (const auto carddata = carddata_of(*inuse))
                            can_scaled = carddata->getbool(".", V("scaledimage"), false);
                    }
                }
                const auto original_scaled = std::filesystem::u8path(
                    join2(scenario_data() ? scenario_data()->m_scedir : S{},
                          ::cw::util::relpath(scaled_destination, scenario_temp_root)));
                if (can_scaled || !::cw::util::path_is_file(::cw::lib::encoding::path_to_utf8(original_scaled))) {
                    save_surface_file(save_scaled, std::filesystem::u8path(scaled_destination));
                    written_images.insert(scaled_destination);
                }
            }
            // 要求された全書き込みが成功してから、不要な倍率の上書き画像を削除す
            // る。 今回の取得で置き換えたばかりのパスは削除しない。これは各失敗
            // 先を維持する処理であり、複数ファイル全体の不可分な保存ではない。
            for (const auto& prior : prior_temporary_images) {
                if (written_images.contains(prior)) continue;
                try { ::cw::util::remove(prior, false); }
                catch (const std::runtime_error&) { ::cw::util::print_ex(std::nullopt); }
            }
            invalidate_jpy_resource_cache();
            if (const auto data = scenario_data()) {
                data->m_path_cache.clear();
            }
            if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
                // Python と同様、既存の遅延読み込み項目をリセットする。 表自体を置
                // き換えると追加したローダーが失われ、所有者が古い表を使い続けてしまう。
                const auto* object = std::get_if<::cw::lib::ObjectPtr>(
                    &::cw::g_cwpy->m_rsrc->m_specialchars.storage());
                if (const auto table = object && *object
                        ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object) : nullptr)
                    table->reset();
            }
            if (::cw::g_cwpy && !::cw::g_cwpy->m_update_scaling) {
                for (const auto& value : ::cw::g_cwpy->get_mcards(V{}, "")) {
                    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
                    const auto menu = object && *object ? std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object) : nullptr;
                    if (!menu || !menu->is_initialized() || !menu->has_cardimg()) continue;
                    const auto cardimg = menu->get_cardimg();
                    if (cardimg && cardimg->is_modifiedfile()) {
                        cardimg->clear_cache();
                        cardimg->m_use_excache = true;
                        ::cw::g_cwpy->m_file_updates.insert(menu);
                    }
                }
                if (::cw::g_cwpy->m_background && !::cw::g_cwpy->m_file_updates_bg &&
                    ::cw::g_cwpy->m_background->is_modifiedfile()) {
                    ::cw::g_cwpy->m_background->m_use_excache = true;
                    ::cw::g_cwpy->m_file_updates_bg = true;
                }
            }
        }
        if (!anime->m_all_cut) {
            add_background_lazydraw();
            wait(anime);
            if (::cw::g_cwpy) ::cw::g_cwpy->update_titlebar();
        }
        if (::cw::g_cwpy && ::cw::g_cwpy->m_background) ::cw::g_cwpy->m_background->m_reload_jpdcimage = false;
    }
    if (mask) apply_colorkey(Image::m_image);
}

void JpdcImage::wait(std::shared_ptr<AnimationCounter> doanime)
{
    // Python版は右クリック解除までイベントループを回す。
    wait_effectbooster(0, doanime);
}

std::tuple<I,I,I> JptxImage::get_fontcolor(S fontcolor, std::tuple<I,I,I> fallback)
{
    return parse_color(fontcolor, fallback);
}

std::tuple<bool,S,std::map<S,S>> JptxImage::parse_tag(S tag)
{
    // 引用符で囲まれた値も空白で切る動作を含め、従来の正規表現パーサーに合わせる。 引
    // 用符を解釈する HTML パーサーにすると、元データのフォント名の扱いが変わる。
    const auto first = tag.find_first_not_of("<> ");
    if (first == S::npos) return {true, {}, {}};
    const auto last = tag.find_last_not_of("<> ");
    tag = tag.substr(first, last - first + 1);
    const auto chars = ::cw::lib::regex_grapheme::decode_utf8(tag);
    std::vector<std::size_t> offsets{0};
    for (const auto code : chars)
        offsets.push_back(offsets.back() + (code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4));
    const auto space = [&](std::size_t i) { return ::cw::lib::is_python_str_whitespace(chars[i]); };
    const auto token = [&](std::size_t i) { return !space(i) && chars[i] != '='; };
    const auto slice = [&](std::size_t a, std::size_t b) { return tag.substr(offsets[a], offsets[b] - offsets[a]); };
    std::size_t pos = !chars.empty() && chars[0] == '/' ? 1 : 0;
    while (pos < chars.size() && space(pos)) ++pos;
    const auto begin = pos;
    while (pos < chars.size() && token(pos)) ++pos;
    S name = pos == begin ? (!chars.empty() && chars[0] == '/' ? S{"/"} : S{})
        : ::cw::lib::python_strip(slice(0, pos));
    const bool start = name.empty() || name.front() != '/';
    if (!start) {
        name.erase(std::remove(name.begin(), name.end(), '/'), name.end());
        name = ::cw::lib::python_strip(name);
    }
    std::map<S,S> attrs;
    for (pos = 0; pos < chars.size();) {
        if (!token(pos)) { ++pos; continue; }
        const auto key_begin = pos;
        while (pos < chars.size() && token(pos)) ++pos;
        const auto key_end = pos;
        while (pos < chars.size() && space(pos)) ++pos;
        if (pos == chars.size() || chars[pos] != '=') continue;
        ++pos;
        // Py と同様、「=」の後の空白も値として取得し、ASCII
        // の空白と引用符だけを除く。 タブや全角空白は残す。
        const auto value_begin = pos;
        while (pos < chars.size() && space(pos)) ++pos;
        const auto token_begin = pos;
        while (pos < chars.size() && token(pos)) ++pos;
        if (pos == token_begin) continue;
        auto value = slice(value_begin, pos);
        const auto vfirst = value.find_first_not_of(" \"'");
        value = vfirst == S::npos ? S{} : value.substr(vfirst, value.find_last_not_of(" \"'") - vfirst + 1);
        attrs[::cw::lib::python_strip(slice(key_begin, key_end))] = std::move(value);
    }
    return {start, name, attrs};
}

void JptxImage::initialize(S path, bool mask)
{
    EffectBoosterConfig config; config.initialize(path, "jptx:init", false);
    m_mask_typed = mask;
    if (!config.has_section("jptx:init")) {
        // Py と同様、縦横とも cw.s(1) を使い、画像全体を透明にする。
        const I empty_size = scale_i(1);
        Image::m_image = make_surface(empty_size, empty_size, {0,0,0});
        ensure_rgba(Image::m_image);
        ::cw::lib::simd_image::force_alpha(
            Image::m_image->rgba.data(), Image::m_image->rgba.size() / 4U, 0);
        Image::m_image->has_pixel_alpha = true;
        Image::m_image->known_opaque = false;
        m_image = V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image));
        return;
    }
    const auto backcolor = config.get_color("jptx:init","backcolor",{0,0,0});
    const I backwidth = scale_i(config.get_int("jptx:init","backwidth",-1));
    const I backheight = scale_i(config.get_int("jptx:init","backheight",-1));
    const bool autoline = config.get_bool("jptx:init","autoline",true);
    const I lineheight = config.get_int("jptx:init","lineheight",100);
    const I fontpixels_noscale = config.get_int("jptx:init","fontpixels",12);
    const I fontpixels = scale_i(fontpixels_noscale);
    const S fontface = config.get("jptx:init","fontface","ＭＳ Ｐゴシック");
    auto fontcolor = config.get_color("jptx:init","fontcolor",{255,255,255});
    const bool antialias = config.get_bool("jptx:init","antialias",false);
    const bool fonttransparent = config.get_bool("jptx:init","fonttransparent",false);
    S text = config.get("jptx:begin","jptx:end","");
    if (!autoline) { S flattened; for(char c:text) if(c!='\n') flattened+=c; text=std::move(flattened); }
    { S flattened; for(char c:text) if(c!='\t') flattened+=c; text=std::move(flattened); }
    auto aa_pixels_from_noscale = [](I pixels) {
        // Python の切り下げ除算と剰余補正を、pixels_aa = 2*pixels
        // + {0,1,1,-1}[pixels % 4] として正確にまとめ
        // る。 正の剰余で負の要求も扱え、その場合の最終 max(1, ...)
        // は常に 1 となる。 固定幅の中間積が桁あふれしないようにする。
        const I remainder = (pixels % 4 + 4) % 4;
        const I correction = remainder == 1 || remainder == 2 ? 1 : remainder == 3 ? -1 : 0;
        if (pixels <= 0) return I{1};
        const I limit = std::numeric_limits<I>::max();
        if (pixels > limit / 2) {
            if (pixels == limit / 2 + 1 && correction == -1) return limit;
            // Python の整数は任意精度だが、ネイティブの Font のピクセル指
            // 定には Integer の上限がある。 その寸法は折り返さず拒否する。
            throw std::overflow_error("JPTX antialias font size exceeds Integer");
        }
        return std::max<I>(1, pixels * 2 + correction);
    };
    I pixels_aa_noscale = aa_pixels_from_noscale(fontpixels_noscale);
    I pixels_aa = scale_i(pixels_aa_noscale);
    auto font = std::make_shared<::cw::imageretouch::Font>();
    auto font_noscale = std::make_shared<::cw::imageretouch::Font>();
    auto font2 = std::make_shared<::cw::imageretouch::Font>();
    auto font2_noscale = std::make_shared<::cw::imageretouch::Font>();
    font->initialize(fontface, fontpixels, false, false);
    font_noscale->initialize(fontface, fontpixels_noscale, false, false);
    font2->initialize(fontface, pixels_aa, false, false);
    font2_noscale->initialize(fontface, pixels_aa_noscale, false, false);
    m_font = font; m_font_noscale = font_noscale; m_font2 = font2; m_font2_noscale = font2_noscale;
    I width = backwidth > 0 ? backwidth : area_w();
    // Python/cwconv はこの背景高に領域の幅を使う。 backheight=0
    // でも、テキスト高の自動切り取り前にはその従来動作を維持する。
    I height = backheight > 0 ? backheight : area_w();
    m_fontface=V(fontface); m_fontpixels=V(fontpixels); m_fontpixels_noscale=V(fontpixels_noscale);
    m_fontcolor=V(std::to_string(std::get<0>(fontcolor))+","+std::to_string(std::get<1>(fontcolor))+","+std::to_string(std::get<2>(fontcolor)));
    m_lineheight=V(lineheight);
    Image::m_image=make_surface(width,height,backcolor);
    if(fonttransparent&&Image::m_image&&Image::m_image->size.width>0){ensure_rgba(Image::m_image);for(I xx=0;xx<Image::m_image->size.width;++xx){const auto p=static_cast<std::size_t>(xx*4);Image::m_image->rgba[p]=static_cast<std::uint8_t>(std::get<0>(fontcolor));Image::m_image->rgba[p+1]=static_cast<std::uint8_t>(std::get<1>(fontcolor));Image::m_image->rgba[p+2]=static_cast<std::uint8_t>(std::get<2>(fontcolor));}}
    m_image=V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image));
    I x = 0, max_width = 0, calculated_height = 0, current_pixels = fontpixels, current_pixels_noscale = fontpixels_noscale;
    R y = 0.0;
    I current_lineheight = lineheight;
    S current_face = fontface;
    bool bold = false, underline = false, italic = false, strike = false, nolinedata = true, tagonly = true;
    std::vector<std::tuple<S, I, I, std::tuple<I, I, I>>> oldfonts;
    S chars;
    auto apply_font = [&](const S& face, I pixels, I pixels_noscale_value) {
        current_face = face; current_pixels = pixels; current_pixels_noscale = pixels_noscale_value;
        pixels_aa_noscale = aa_pixels_from_noscale(pixels_noscale_value);
        pixels_aa = scale_i(pixels_aa_noscale);
        font->initialize(face, pixels, bold, italic);
        font_noscale->initialize(face, pixels_noscale_value, bold, italic);
        font2->initialize(face, pixels_aa, bold, italic);
        font2_noscale->initialize(face, pixels_aa_noscale, bold, italic);
        font->set_underline(underline); font_noscale->set_underline(underline);
        font2->set_underline(underline); font2_noscale->set_underline(underline);
    };
    auto flush = [&] {
        if (chars.empty()) return;
        if (font) {
            // Python と同様、入れ子の <font> タグ内でも、ここでは外側・既定
            // の fontface を参照する。 従来の cwconv 互換性を維持する。
            const bool antialias2 = antialias || (!antialias && current_pixels_noscale > 22 &&
                (fontface == "ＭＳ Ｐ明朝" || fontface == "ＭＳ 明朝" || fontface == "ＭＳ Ｐゴシック" ||
                 fontface == "ＭＳ ゴシック" || fontface == "MS UI Gothic"));
            I advance = 0, yp = 0;
            std::shared_ptr<Surface> sub;
            if (antialias) {
                sub = font2->render(chars, true, fontcolor);
                auto size2 = font2->size(chars);
                if (::cw::g_UP_SCR != 1.0) {
                    const auto measured = font2_noscale->size(chars);
                    size2 = {scale_i(std::get<0>(measured)), scale_i(std::get<1>(measured))};
                }
                const I dest_w = std::get<0>(size2) / 2;
                const I dest_h = std::get<1>(size2) / 2;
                auto bg = crop_surface(Image::m_image, x, static_cast<I>(y), dest_w, dest_h);
                if (bg && bg->size.width > 0 && bg->size.height > 0) {
                    // Py と同様、背景を二倍へ拡大する前に切り取る。 実
                    // 際に切り取った寸法を使い、UP != 1 の場合も含
                    // め範囲が空なら元の font2 の文字形を維持する。
                    if (::cw::g_UP_SCR != 1.0 && sub &&
                        (sub->size.width != std::get<0>(size2) || sub->size.height != std::get<1>(size2)))
                        sub = ::cw::image::smoothscale(sub, size2, true, false);
                    auto bg2 = resize_surface(bg, bg->size.width * 2, bg->size.height * 2);
                    alpha_blit_surface(bg2, sub, 0, 0);
                    sub = ::cw::image::smoothscale(bg2, {bg->size.width, bg->size.height}, true, false);
                }
                advance = (::cw::g_UP_SCR == 1.0
                    ? std::get<0>(font2->size_withoutoverhang(chars))
                    : scale_i(std::get<0>(font2_noscale->size_withoutoverhang(chars)))) / 2;
            } else {
                sub = font->render(chars, antialias2, fontcolor);
                if (::cw::g_UP_SCR != 1.0 && sub) {
                    const auto measured = font_noscale->size(chars);
                    const auto expected = std::make_tuple(
                        scale_i(std::get<0>(measured)), scale_i(std::get<1>(measured)));
                    if (sub->size.width != std::get<0>(expected) || sub->size.height != std::get<1>(expected))
                        sub = ::cw::image::smoothscale(sub, expected, antialias2, false);
                }
                yp = scale_i(1);
                advance = ::cw::g_UP_SCR == 1.0
                    ? std::get<0>(font->size_withoutoverhang(chars))
                    : scale_i(std::get<0>(font_noscale->size_withoutoverhang(chars)));
            }
            if (strike) {
                auto line = font->render("―", false, fontcolor);
                const I line_width = advance + scale_i(10);
                const I line_height = current_pixels + scale_i(2);
                if (line_width < 0 || line_height < 0)
                    throw std::invalid_argument("JPTX strike: negative scale dimensions");
                line = line_width == 0 || line_height == 0
                    ? make_surface(line_width, line_height)
                    : resize_surface(line, line_width, line_height);
                // Py の Info.render と同様、通常の外側への画像転送
                // より前に、 描画した部分文字列へ取り消し線の文字形を合成する。
                alpha_blit_surface(sub, line, scale_i(-5), 0);
            }
            if (sub) alpha_blit_surface(Image::m_image, sub, x, static_cast<I>(y) + yp);
            x += advance;
            max_width = std::max(max_width, x);
        }
        chars.clear();
        nolinedata = false; tagonly = false;
    };
    auto new_line = [&] {
        flush(); x = 0;
        if (nolinedata || !tagonly)
            y += static_cast<R>(current_pixels) * current_lineheight / 100.0;
        calculated_height = static_cast<I>(y); nolinedata = true; tagonly = true;
    };
    const S face_default = fontface; const auto colour_default = fontcolor;
    S tag;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        // Python と同様、未完成のタグを蓄積中でも元データの LF
        // を処理する。 タグ全体を先読みせず、その順序を維持する。
        if (ch == '\n') { new_line(); continue; }
        if (ch == '<') {
            flush(); nolinedata = false; tag += ch; continue;
        }
        if (ch == '>') {
            // 対応する開始記号のない「>」も、元のタグ終了分岐へ進める。 通常の文字
            // としては描かず、この HTML とは異なる従来動作を意図的に維持する。
            nolinedata = false; tag += ch;
            // 元の str.lower と同様、指定フォント属性の値も含むタグ全体
            // に、 Unicode の文脈依存・複数文字への小文字変換を適用する。
            const auto [start, rawname, attrs] = parse_tag(::cw::lib::encoding::python_lower(tag));
            const S name = ::cw::lib::encoding::python_lower(rawname);
            const bool skip_br_newline = name == "br" && i + 1 < text.size() && text[i + 1] == '\n';
            if (name == "br") {
                // タグだけの行の後にある元データの LF とは異なり、Python
                // と同様、<br> は空・連続の場合も常に一行進める。
                flush(); x = 0;
                y += static_cast<R>(current_pixels) * current_lineheight / 100.0;
                calculated_height = static_cast<I>(y); nolinedata = true; tagonly = true;
            }
            else if (name == "b") { bold = start; font->set_bold(start); font_noscale->set_bold(start); font2->set_bold(start); font2_noscale->set_bold(start); }
            else if (name == "u") { underline = start; font->set_underline(start); font_noscale->set_underline(start); font2->set_underline(start); font2_noscale->set_underline(start); }
            else if (name == "i") { italic = start; font->set_italic(start); font_noscale->set_italic(start); font2->set_italic(start); font2_noscale->set_italic(start); }
            else if (name == "s") strike = start;
            else if (name == "shiftx" && start) { x += scale_i(::cw::lib::require_python_integer(attrs.at("shiftx"))); }
            else if (name == "shifty" && start) { y += static_cast<R>(scale_i(::cw::lib::require_python_integer(attrs.at("shifty")))); }
            // 元の lineheight 分岐と同様、終了形式のタグにも適用する
            // 。 数値属性の欠落・不正は、位置ずらしと同様にエラーを伝播させる。
            else if (name == "lineheight") { current_lineheight = ::cw::lib::require_python_integer(attrs.at("lineheight")); }
            else if (name.rfind("font", 0) == 0) {
                if (start) {
                    oldfonts.emplace_back(current_face, current_pixels, current_pixels_noscale, fontcolor);
                    I p = current_pixels_noscale;
                    if (attrs.contains("fontpixels")) p = ::cw::lib::require_python_integer(attrs.at("fontpixels"));
                    if (attrs.contains("pixels")) p = ::cw::lib::require_python_integer(attrs.at("pixels"));
                    S face = attrs.contains("fontface") ? attrs.at("fontface") : face_default;
                    if (attrs.contains("face")) face = attrs.at("face");
                    auto colour = fontcolor;
                    S cs = attrs.contains("fontcolor") ? attrs.at("fontcolor") : S{};
                    if (attrs.contains("color")) cs = attrs.at("color");
                    if (!cs.empty()) colour = get_fontcolor(cs, colour_default);
                    apply_font(face, scale_i(p), p); fontcolor = colour;
                } else {
                    // 元の list.pop と同様、対応のない </font> は、
                    // 前の文字が描画済みでもエラーを伝播させる。 黙って回復しない。
                    if (oldfonts.empty()) throw std::out_of_range("JptxImage: empty font stack");
                    const S face = std::get<0>(oldfonts.back()); const I p = std::get<1>(oldfonts.back()); const I pn = std::get<2>(oldfonts.back()); const auto colour = std::get<3>(oldfonts.back()); oldfonts.pop_back();
                    apply_font(face, p, pn); fontcolor = colour;
                }
            }
            tag.clear(); if (skip_br_newline) ++i; continue;
        }
        if (!tag.empty()) {
            flush(); tag += ch; continue;
        }
        chars += ch;
        nolinedata = false; tagonly = false;
    }
    if (!chars.empty() || !nolinedata) {
        flush();
        if (nolinedata || !tagonly)
            y += static_cast<R>(current_pixels) * current_lineheight / 100.0;
        calculated_height = static_cast<I>(y);
    }
    m_w = max_width; m_h = calculated_height;
    if (backwidth < 0 || backheight < 0) {
        const I crop_w = backwidth < 0 ? std::min(max_width, Image::m_image->size.width) : std::min(backwidth, Image::m_image->size.width);
        const I crop_h = backheight < 0 ? std::min(calculated_height, Image::m_image->size.height) : std::min(backheight, Image::m_image->size.height);
        Image::m_image = crop_surface(Image::m_image, 0, 0, std::max<I>(0, crop_w), std::max<I>(0, crop_h));
    }
    m_width_typed = Image::m_image->size.width; m_height_typed = Image::m_image->size.height;
    if (mask && Image::m_image) {
        ensure_rgba(Image::m_image);
        const auto [kr,kg,kb] = backcolor;
        for (std::size_t i = 0; i + 3 < Image::m_image->rgba.size(); i += 4)
            if (Image::m_image->rgba[i] == static_cast<std::uint8_t>(kr) &&
                Image::m_image->rgba[i+1] == static_cast<std::uint8_t>(kg) &&
                Image::m_image->rgba[i+2] == static_cast<std::uint8_t>(kb)) Image::m_image->rgba[i+3] = 0;
    }
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image));
}

void JpyImage::initialize(S path, bool mask, std::optional<std::shared_ptr<JpyCache>> cache_arg,
                          std::optional<std::shared_ptr<AnimationCounter>> anime_arg,
                          std::optional<std::shared_ptr<_JpySubImage>> parent,
                          AnimatedSurfaceLoader animated_surface_loader)
{
    auto starttick = now_tick();
    auto cache = cache_arg.value_or(std::make_shared<JpyCache>());
    auto anime = anime_arg.value_or(std::make_shared<AnimationCounter>());
    if (!cache) cache=std::make_shared<JpyCache>(); if (!anime) anime=std::make_shared<AnimationCounter>();
    m_path_typed=::cw::util::normalize_path(std::move(path));
    m_animated_paths_typed.clear();
    m_animated_surface_loader = std::move(animated_surface_loader);
    // 初期化中は一つの正しいパスを使い続ける。path は既に
    // m_path_typed へ移動済みで空の場合があるた
    // め、移動元の値で設定を読まない。 Python と同様、
    // ここでは常に JpyImage を合成する。入力キャッシ
    // ュは _JpySubImage::load が加工前に
    // 管理する。 ここで合成済みキャッシュを返すと待機を省略
    // し、can_mask・parent の互換性を失うため
    // 、共通の入力キャッシュを使いつつ構築は通常どおり行う。
    EffectBoosterConfig config; config.initialize(m_path_typed,"init",true);
    auto back=std::make_shared<JpyBackGroundImage>(); back->initialize(std::make_shared<EffectBoosterConfig>(config),cache,mask);
    if(back->m_vanish_anime_typed){
        Image::m_image=make_surface(0,0); m_image=V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image)); m_is_cacheable=true; m_is_animated=false; return;
    }
    config.m_path_typed=::cw::lib::encoding::path_to_utf8(std::filesystem::absolute(std::filesystem::u8path(m_path_typed))); config.m_path=V(config.m_path_typed); config.m_dirdepth=back->m_dirdepth_typed;
    bool can_mask=true;
    back->m_animated_surface_loader = m_animated_surface_loader;
    back->load(anime);
    m_animated_paths_typed.insert(back->m_animated_paths_typed.begin(), back->m_animated_paths_typed.end());
    cache->m_beforeback=back->Image::m_image; I defaultcopymode=1;
    m_is_cacheable=!back->m_transparent_bg_typed; m_is_animated=back->m_is_animated_typed;
    if(parent&&back->m_loadcache_typed) m_is_cacheable=false;
    for(const auto& section:config.sections()){
        if(section=="init")continue;
        auto parts=std::make_shared<JpyPartsImage>(); parts->initialize(std::make_shared<EffectBoosterConfig>(config),section,cache,mask);
        parts->m_animated_surface_loader = m_animated_surface_loader;
        if(parts->m_haswidth_parts_typed&&parts->m_width_parts_typed<0&&parts->m_dirtype_typed==2) break;
        parts->m_defaultcopymode_typed=defaultcopymode; parts->m_defaultcopymode=defaultcopymode; parts->m_starttick_typed=starttick;
        parts->load(anime); parts->retouch(); parts->drawtemp(anime); parts->draw2back(back,mask);
        m_animated_paths_typed.insert(parts->m_animated_paths_typed.begin(), parts->m_animated_paths_typed.end());
        if(!parts->m_is_cacheable_typed)m_is_cacheable=false; if(parts->m_is_animated_typed)m_is_animated=true;
        if(parent&&parts->m_loadcache_typed)m_is_cacheable=false;
        if(parts->m_animation_typed==1||parts->m_animation_typed==2||parts->m_animation_typed==3)defaultcopymode=2;
        can_mask=can_mask&&parts->m_can_mask_typed;
        starttick=now_tick();
    }
    back->m_starttick_typed=starttick; back->retouch(); back->drawtemp(anime);
    if(!back->m_is_cacheable_typed)m_is_cacheable=false; if(back->m_is_animated_typed)m_is_animated=true; can_mask=can_mask&&back->m_can_mask_typed;
    Image::m_image=back->get_image(); if(!Image::m_image)Image::m_image=make_surface(0,0);
    if (version_lessthan("1.30", ::cw::g_HINT_CARD) && Image::m_image && Image::m_image->size.width > 0) {
        if (mask && can_mask) apply_colorkey(Image::m_image);
    } else if (mask && (!parent.has_value() || !parent.value() || can_mask)) {
        apply_colorkey(Image::m_image);
    }
    m_image=V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image));
}

void _JpySubImage::initialize(std::shared_ptr<EffectBoosterConfig> config, S section, std::shared_ptr<JpyCache> cache)
{
    m_cache=cache?V(std::static_pointer_cast<::cw::lib::Object>(cache)):V{};
    m_configpath_typed=config?config->m_path_typed:S{}; m_configdepth_typed=config?config->m_dirdepth:0;
    m_starttick_typed=frame_start_tick();
    auto get=[&](S key,S def){return config?config->get(section,key,def):def;};
    auto geti=[&](S key,I def){return config?config->get_int(section,key,def):def;};
    auto getb=[&](S key,bool def){return config?config->get_bool(section,key,def):def;};
    auto getints2=[&](S key,std::optional<std::tuple<I,I>> def){if(!config)return def;const auto raw=config->get(section,key,{});return ints_optional<std::tuple<I,I>>(raw,2,def);};
    auto getints4=[&](S key,std::optional<std::tuple<I,I,I,I>> def){if(!config)return def;const auto raw=config->get(section,key,{});return ints_optional<std::tuple<I,I,I,I>>(raw,4,def);};
    m_dirtype_typed=geti("dirtype",1);m_filename_typed=get("filename","");m_smooth_typed=getb("smooth",false);if(const auto clip=getints4("clip",std::nullopt);clip)m_clip_typed=scale_rect(::cw::lib::Rect{std::get<0>(*clip),std::get<1>(*clip),std::get<2>(*clip),std::get<3>(*clip)});else m_clip_typed.reset();
    m_loadcache_typed=geti("loadcache",0);m_flip_typed=getb("flip",false);m_mirror_typed=getb("mirror",false);m_turn_typed=geti("turn",0);m_mask_typed=geti("mask",0);m_colormap_typed=geti("colormap",0);m_alpha_typed=geti("alpha",0);
    m_exchange_typed = config ? config->get_int_3(section,"exchange",std::nullopt) : std::nullopt;
    if (!m_exchange_typed) m_exchange_typed = geti("colorexchange",0);
    m_noise_typed=geti("noise",0);m_noisepoint_typed=geti("noisepoint",0);m_filter_typed=geti("filter",0);m_waittime_typed=geti("wait",0);m_animation_typed=geti("animation",0);
    m_animemove_noscale_typed=getints2("animemove",std::nullopt);if(const auto clip=getints4("animeclip",std::nullopt);clip)m_animeclip_typed=scale_rect(::cw::lib::Rect{std::get<0>(*clip),std::get<1>(*clip),std::get<2>(*clip),std::get<3>(*clip)});else m_animeclip_typed.reset();m_animespeed_typed=geti("animespeed",0);m_animeposition_noscale_typed=getints2("animeposition",std::nullopt);m_paintmode_typed=geti("paintmode",0);
    m_defaultcopymode_typed=2;m_is_cacheable_typed=true;m_is_animated_typed=false;m_can_mask_typed=true;
    // 生成済みの Value メンバーを使う呼び出し側のため、それらも同期する。
    m_configpath=V(m_configpath_typed);m_configdepth=V(m_configdepth_typed);m_dirtype=V(m_dirtype_typed);m_filename=V(m_filename_typed);m_smooth=V(m_smooth_typed);m_loadcache=V(m_loadcache_typed);m_flip=V(m_flip_typed);m_mirror=V(m_mirror_typed);m_turn=V(m_turn_typed);m_mask=V(m_mask_typed);m_colormap=V(m_colormap_typed);m_alpha=V(m_alpha_typed);m_noise=V(m_noise_typed);m_noisepoint=V(m_noisepoint_typed);m_filter=V(m_filter_typed);m_waittime=V(m_waittime_typed);m_animation=V(m_animation_typed);m_animespeed=V(m_animespeed_typed);m_paintmode=V(m_paintmode_typed);
}

std::tuple<S,bool> _JpySubImage::get_filepath(I dirtype)
{
    if(m_filename_typed.empty())return {"",false};
    return get_filepath_s(m_configpath_typed,m_configdepth_typed,m_filename_typed,dirtype == -1 ? m_dirtype_typed : dirtype,"");
}

void _JpySubImage::load(std::shared_ptr<AnimationCounter> anime)
{
    m_animated_paths_typed.clear();
    const auto [path,can_scaled]=get_filepath(-1);
    std::shared_ptr<Surface> image;
    const S ext=lower(std::filesystem::u8path(path).extension().generic_string());
    if(::cw::util::path_is_file(path)){
        const bool sound = is_sound_path(path);
        const bool animated_image = ::cw::animatedimage::is_supported_path(path);
        const bool resource_cacheable = !sound && ext != ".jpdc" && !animated_image;
        bool resource_cache_store_needed = false;
        if (resource_cacheable) image = load_resource_cache(path);
        if(!image && sound){
            m_is_cacheable_typed=false;m_is_animated_typed=true;
            if (anime && anime->countup()) {
                const auto current = backend();
                if (current && current->play_sound) current->play_sound(path, true);
            }
            image=make_rgb_surface(0,0);
        }else if(!image&&ext==".jpy1"){
            // Python の (_JpySubImage,UP_SCR,False,path) キーに相当する
            // プロセス内キャッシュを path+mtime で実装済み。JPY1 は合成結果の
            // cacheable/animated 状態を入れ子の JpyImage から引き継ぐ。
            auto nested=std::make_shared<JpyImage>();std::shared_ptr<JpyCache> nested_cache; if(const auto* p=std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage());p&&*p)nested_cache=std::dynamic_pointer_cast<JpyCache>(*p); if(!nested_cache)nested_cache=std::make_shared<JpyCache>(); nested->initialize(path,false,nested_cache,anime,std::shared_ptr<_JpySubImage>(this, [](auto*){}),m_animated_surface_loader);
            image=nested->get_image();m_is_cacheable_typed=nested->m_is_cacheable;m_is_animated_typed=nested->m_is_animated;resource_cache_store_needed=true;
            m_animated_paths_typed.insert(nested->m_animated_paths_typed.begin(), nested->m_animated_paths_typed.end());
        }else if(!image&&ext==".jpdc"){
            m_is_cacheable_typed=false;auto nested=std::make_shared<JpdcImage>();nested->initialize(false,path,std::nullopt,m_defaultcopymode_typed,anime);image=nested->get_image();
        }else if(!image&&ext==".jptx"){
            // JPTX は静的な生成画像なので、Python の resource_cache と同じく
            // サイズ・RGBAを保持し、mtime変更時にだけ再生成する。
            auto nested=std::make_shared<JptxImage>();nested->initialize(path,false);image=nested->get_image();resource_cache_store_needed=true;
        }else if(!image&&animated_image){
            // EffectBoosterの内包画像も、カード・背景・ダイアログと同じ
            // animatedimageスロットから現在フレームを取得する。独自タイマーを
            // 持たせず、呼出側へ依存パスを返して同じ再描画契約に参加させる。
            m_is_cacheable_typed=false;
            m_is_animated_typed=true;
            m_animated_paths_typed.insert(path);
            const auto current = m_animated_surface_loader
                ? m_animated_surface_loader(path)
                : ::cw::animatedimage::get_surface(path);
            image = std::get<0>(current).value_or(nullptr);
            if (image) image = scale_surface_value(image);
        }
        if(!image&&!sound&&ext!=".jpy1"&&ext!=".jpdc"&&ext!=".jptx"){
            // PNG/BMP/JPEG/GIF/WebP は Pillow/SDL_image 代替の imagecodec で
            // 実デコードし、Info由来のサイズとRGBA（アルファを含む）を保持する。
            {
                const bool use_ex = ::cw::g_cwpy && ::cw::g_cwpy->m_background && ::cw::g_cwpy->m_background->m_use_excache;
                image = ::cw::util::load_image(path, false, {0,0}, std::nullopt, true, true, can_scaled, false, std::nullopt, use_ex);
                image = scale_surface_value(image);
                resource_cache_store_needed=!animated_image&&static_cast<bool>(image);
            }}
        if (resource_cache_store_needed && resource_cacheable && m_is_cacheable_typed) save_resource_cache(path, image);
    }else if(m_loadcache_typed>=1&&m_loadcache_typed<=8){
        std::shared_ptr<JpyCache> cache;
        if(const auto* p=std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage());p&&*p)cache=std::dynamic_pointer_cast<JpyCache>(*p);
        image=cache?cache->load_image(m_loadcache_typed):make_rgb_surface(0,0);
    }else if(auto* back=dynamic_cast<JpyBackGroundImage*>(this)){
        const I width=back->m_width_bg_typed>0?back->m_width_bg_typed:area_w();const I height=back->m_height_bg_typed>0?back->m_height_bg_typed:area_h();image=make_rgb_surface(width,height,back->m_backcolor_typed);
    }else if(auto* parts=dynamic_cast<JpyPartsImage*>(this);parts&&parts->m_height_parts_typed>=0&&parts->m_width_parts_typed>=0){
        const I width=parts->m_width_parts_typed>0?parts->m_width_parts_typed:area_w();const I height=parts->m_height_parts_typed>0?parts->m_height_parts_typed:area_h();image=make_rgb_surface(width,height,parts->m_color_typed);
    }else image=make_rgb_surface(0,0);
    Image::m_image=image?image:make_rgb_surface(0,0);
    if (auto* back = dynamic_cast<JpyBackGroundImage*>(this)) {
        const I img_w = Image::m_image->size.width;
        const I img_h = Image::m_image->size.height;
        const I width = back->m_width_bg_typed >= 0 ? back->m_width_bg_typed : (img_w > 0 ? img_w : area_w());
        const I height = back->m_height_bg_typed >= 0 ? back->m_height_bg_typed : (img_h > 0 ? img_h : area_h());
        if (width != img_w || height != img_h) {
            if (width == 0 || height == 0) Image::m_image = make_rgb_surface(0, 0);
            // Py と同様、空のキャッシュ画像をサイズ変更する場合は、新しい黒い描画面を作る。
            else if (img_w == 0 || img_h == 0) Image::m_image = make_rgb_surface(width, height);
            else Image::m_image = ::cw::image::smoothscale(Image::m_image, {width, height}, back->m_smooth_typed, false);
        }
    }
    m_image=V(std::static_pointer_cast<::cw::lib::Object>(Image::m_image));
}

void _JpySubImage::retouch()
{
    auto image = Image::m_image;
    if (!image) return;
    ensure_rgba(image);
    if (image->size.width <= 0 || image->size.height <= 0) {
        if (m_savecache_typed >= 1 && m_savecache_typed <= 8)
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage()); p && *p)
                if (auto cache = std::dynamic_pointer_cast<JpyCache>(*p)) cache->save_image(m_savecache_typed, image);
        return;
    }

    if (m_exchange_typed && 1 <= *m_exchange_typed && *m_exchange_typed <= 5) {
        static const std::array<S, 6> models{"rgb", "gbr", "brg", "grb", "bgr", "rbg"};
        const I model = *m_exchange_typed;
        image = ::cw::imageretouch::exchange_rgbcolor(image, models[static_cast<std::size_t>(model)]);
    }

    m_can_mask_typed = true;
    if (m_filter_typed == 1) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_shape(image);
    } else if (m_filter_typed == 2) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_sharpness(image);
    } else if (m_filter_typed == 3) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_sunpower(image);
    } else if (m_filter_typed == 4) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_coloremboss(image);
    } else if (m_filter_typed == 5) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_darkemboss(image);
    } else if (m_filter_typed == 6) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_electrical(image);
    } else if (m_filter_typed == 7) {
        const auto base = std::tuple<I, I, I>{image->rgba[0], image->rgba[1], image->rgba[2]};
        image = ::cw::imageretouch::to_binaryformat(image, -1, base);
    } else if (m_filter_typed == 8) {
        image = ::cw::imageretouch::spread_pixels(image);
    } else if (m_filter_typed == 9) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::to_negative(image);
    } else if (m_filter_typed == 10) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::filter_emboss(image);
    }

    if (m_colormap_typed) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        static const std::array<std::tuple<I, I, I>, 16> tones{{
            {0, 0, 0}, {0, 0, 0}, {30, 0, -30}, {255, 0, 30}, {255, 0, 0}, {0, 255, 0},
            {0, 0, 255}, {191, 191, 0}, {191, 0, 191}, {0, 191, 191},
            {0, -255, -255}, {-255, 0, -255}, {-255, -255, 0}, {0, 0, -255},
            {0, -255, 0}, {-255, 0, 0}}};
        // Python と同様、不明な値では何も変更しない。ただし上のマスク無
        // 効化は行う。 15 に収めると描画色が変わるため、その補正はしない。
        if (1 <= m_colormap_typed && m_colormap_typed <= 15) {
            const I tone = m_colormap_typed;
            image = tone == 1 ? ::cw::imageretouch::to_grayscale(image)
                              : ::cw::imageretouch::to_sepiatone(image, tones[static_cast<std::size_t>(tone)]);
        }
    }

    if (m_mirror_typed || m_flip_typed) {
        const auto old = image;
        image = copy_surface(old); // pygame.transform.flip と同様、新しい描画面を返す。
        for (I y = 0; y < image->size.height; ++y) for (I x = 0; x < image->size.width; ++x) {
            const I sx = m_mirror_typed ? image->size.width - 1 - x : x;
            const I sy = m_flip_typed ? image->size.height - 1 - y : y;
            const auto si = static_cast<std::size_t>((sy * image->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((y * image->size.width + x) * 4);
            if (si + 3 < old->rgba.size() && di + 3 < image->rgba.size()) std::copy_n(old->rgba.data() + si, 4, image->rgba.data() + di);
        }
    }

    if (m_noise_typed == 1) {
        if (m_noisepoint_typed != 0 && m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::add_lightness(image, m_noisepoint_typed);
    } else if (m_noise_typed == 2) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        if (m_noisepoint_typed < 0) {
            ensure_rgba(image);
            // pygame の SRCALPHA に対する Surface.fill((255,255,255))
            // と同様、アルファも不透明へ戻す。 古いアルファを保持すると、EffectBooster
            // の二値化フィルターで CardWirthPy と結果が異なる。
            ::cw::lib::simd_image::fill_rgba(
                image->rgba.data(), image->rgba.size() / 4U, 255, 255, 255, 255);
        } else image = ::cw::imageretouch::to_binaryformat(image, m_noisepoint_typed, {255, 255, 255});
    } else if (m_noise_typed == 3) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::add_noise(image, m_noisepoint_typed, false);
    } else if (m_noise_typed == 4) {
        if (m_paintmode_typed != 3) m_can_mask_typed = false;
        image = ::cw::imageretouch::add_noise(image, m_noisepoint_typed, true);
    } else if (m_noise_typed == 5 && m_filter_typed != 7) {
        image = ::cw::imageretouch::add_mosaic(image, m_noisepoint_typed);
    }

    if (m_transparent_typed && m_can_mask_typed) {
        I keyx = 0, keyy = 0;
        if (m_clip_typed) { keyx = m_clip_typed->x; keyy = m_clip_typed->y; }
        if (keyx >= 0 && keyy >= 0 && keyx < image->size.width && keyy < image->size.height) {
            const auto p = static_cast<std::size_t>((keyy * image->size.width + keyx) * 4);
            if (p + 2 < image->rgba.size()) m_colorkey_typed = {image->rgba[p], image->rgba[p + 1], image->rgba[p + 2], 255};
        }
    } else m_colorkey_typed.reset();
    image->has_colorkey = m_colorkey_typed.has_value();
    if (m_colorkey_typed) {
        image->colorkey = {m_colorkey_typed->r, m_colorkey_typed->g, m_colorkey_typed->b};
    }
    if (m_colorkey_typed) {
        const auto key = *m_colorkey_typed;
        ::cw::lib::simd_image::colorkey_alpha_zero(
            image->rgba.data(), image->rgba.size() / 4U, key.r, key.g, key.b);
    }

    if (m_turn_typed == 1 || m_turn_typed == 2) {
        const auto old = image;
        const I nw = old->size.height, nh = old->size.width;
        image = copy_surface(old);
        image->size = {nw, nh};
        for (I y = 0; y < nh; ++y) for (I x = 0; x < nw; ++x) {
            // Py と同様、turn=1 は時計回りの rotate(270)、turn=2 は rotate(90) とする。
            const I sx = m_turn_typed == 1 ? y : old->size.width - 1 - y;
            const I sy = m_turn_typed == 1 ? old->size.height - 1 - x : x;
            const auto si = static_cast<std::size_t>((sy * old->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((y * nw + x) * 4);
            if (si + 3 < old->rgba.size()) std::copy_n(old->rgba.data() + si, 4, image->rgba.data() + di);
        }
    }
    if (m_clip_typed) {
        const auto r = *m_clip_typed;
        image = crop_surface(image, r.x, r.y, r.width, r.height);
    }
    if (auto* parts = dynamic_cast<JpyPartsImage*>(this)) {
        const I width = parts->m_width_parts_typed >= 0 ? parts->m_width_parts_typed : image->size.width;
        const I height = parts->m_height_parts_typed >= 0 ? parts->m_height_parts_typed : image->size.height;
        if (width <= 0 || height <= 0) {
            image = make_surface(0, 0);
            image->has_pixel_alpha = false; // Py の Surface((0,0)).convert() に相当する。
        }
        else if (width != image->size.width || height != image->size.height)
            image = ::cw::image::smoothscale(image, {width, height}, parts->m_smooth_typed, false);
    }
    if (m_mask_typed == 1) image = ::cw::imageretouch::add_transparentline(image, true, false, std::nullopt, m_transparent_typed && !m_can_mask_typed);
    else if (m_mask_typed == 2) image = ::cw::imageretouch::add_transparentline(image, false, true, std::nullopt, m_transparent_typed && !m_can_mask_typed);
    else if (m_mask_typed == 3) image = ::cw::imageretouch::add_transparentmesh(image, std::nullopt, m_transparent_typed && !m_can_mask_typed);
    if (m_paintmode_typed == 3) {
        ensure_rgba(image);
        const I alpha = std::clamp<I>(m_alpha_typed, 0, 255);
        // Surface.set_alpha() は、pygame の SRCALPHA ピクセル形式とは独立して扱う。
        if (image->has_pixel_alpha && image->pixel_bits >= 32) {
            // pygame 1.9.6 の BLEND_RGBA_MULT と同様、255
            // を掛けた RGB も含め、 全チャンネルで 8 ビット右シフトを使う。
            ::cw::lib::simd_image::multiply_rgba_alpha(
                image->rgba.data(), image->rgba.size() / 4U, static_cast<std::uint8_t>(alpha), image->has_pixel_alpha);
        } else {
            // SRCALPHA でない場合、set_alpha は保存済みのピクセルを変更しない。
            image->surface_alpha = static_cast<std::uint8_t>(alpha);
            image->surface_alpha_enabled = true;
        }
    }
    if (m_savecache_typed >= 1 && m_savecache_typed <= 8)
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage()); p && *p)
            if (auto cache = std::dynamic_pointer_cast<JpyCache>(*p)) cache->save_image(m_savecache_typed, image);
    Image::m_image = image;
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
}

void _JpySubImage::draw2back(std::shared_ptr<JpyBackGroundImage> back, bool mask)
{
    if (!back || !m_visible_typed) return;
    auto image = Image::m_image;
    auto destination = back->Image::m_image;
    if (!image || !destination) return;
    const I x = std::get<0>(m_position_typed);
    const I y = std::get<1>(m_position_typed);
    if (m_paintmode_typed == 1) {
        m_is_cacheable_typed = false;
        blit_surface(destination, image, x, y, 1);
    } else if (m_paintmode_typed == 2) {
        m_is_cacheable_typed = false;
        blit_surface(destination, image, x, y, 2);
    } else if (m_paintmode_typed != 4) {
        blit_surface(destination, image, x, y);
        if (mask && m_mask_typed) {
            const auto rect = std::optional<std::tuple<I, I, I, I>>{std::tuple<I, I, I, I>{x, y, image->size.width, image->size.height}};
            if (m_mask_typed == 1) destination = ::cw::imageretouch::add_transparentline(destination, true, false, rect, true);
            else if (m_mask_typed == 2) destination = ::cw::imageretouch::add_transparentline(destination, false, true, rect, true);
            // Python と同様、mask=3 は合成済み背景全体に適用する
            // 。 行マスク 1/2 と異なり、意図的に部品の矩形を渡さない。
            else if (m_mask_typed == 3) destination = ::cw::imageretouch::add_transparentmesh(destination, std::nullopt, true);
            back->Image::m_image = destination;
        }
    }
}

std::shared_ptr<Surface> _JpySubImage::clip_tempimg(std::shared_ptr<Surface> image, std::tuple<I,I> pos)
{
    if (!image || !m_animeclip_typed) return image;
    auto result=make_surface(image->size.width,image->size.height);
    ::cw::lib::simd_image::fill_rgba(result->rgba.data(),result->rgba.size()/4U,0,0,0,0);
    const auto r=*m_animeclip_typed;const auto [px,py]=pos;
    // pygame.Rect はより狭い整数を使う。 C++ の端点が桁あ
    // ふれする場合は拒否し、符号付き桁あふれでクリッピングの形を変えない。
    const auto end=[](I start,I length) {
        if ((length>0 && start>std::numeric_limits<I>::max()-length) ||
            (length<0 && start<std::numeric_limits<I>::min()-length))
            throw std::out_of_range("EffectBooster animeclip: rectangle endpoint overflow");
        return start+length;
    };
    const I right=end(px,image->size.width),bottom=end(py,image->size.height);
    const I cr=end(r.x,r.width),cb=end(r.y,r.height);
    // pygame 1.9.6 と同様、ゼロ以下の幅・高さを正規化・拒否
    // せず、元の矩形の端点を比較する。 負のクリップでも交差する場合が
    // あり、その後の subsurface で負の交差寸法を拒否する。
    if (!(px < cr && py < cb && right > r.x && bottom > r.y)) return result;
    const I left=std::max(px,r.x),top=std::max(py,r.y);
    const I clipped_right=std::min(right,cr),clipped_bottom=std::min(bottom,cb);
    if (clipped_right<left || clipped_bottom<top)
        throw std::invalid_argument("EffectBooster animeclip: negative subsurface extent");
    const auto clipped=crop_surface(image,left-px,top-py,clipped_right-left,clipped_bottom-top);
    blit_surface(result,clipped,left-px,top-py);
    return result;
}

void _JpySubImage::_drawtemp_impl(std::shared_ptr<AnimationCounter> anime,std::shared_ptr<Surface> background,std::tuple<I,I> pos,bool redraw,bool is_anime,std::optional<I> waittime,bool nowait)
{
    if (!background) return;
    std::shared_ptr<JpyCache> cache;
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage()); p && *p)
        cache = std::dynamic_pointer_cast<JpyCache>(*p);
    if (cache) cache->restore();
    auto image = clip_tempimg(Image::m_image, pos);
    if (!image) return;
    const auto [x, y] = pos;
    const I wt = waittime.value_or(m_waittime_typed);
    const I blend = m_paintmode_typed == 1 ? 1 : m_paintmode_typed == 2 ? 2 : 0;
    const I left = std::max<I>(0, x);
    const I top = std::max<I>(0, y);
    const I right = std::min<I>(background->size.width, x + image->size.width);
    const I bottom = std::min<I>(background->size.height, y + image->size.height);
    const bool visible = right > left && bottom > top;
    if (visible && m_paintmode_typed != 4) {
        if (redraw) {
            if (cache && m_animation_typed != 1) {
                cache->m_before = crop_surface(background, left, top,
                                               std::max<I>(0, right - left),
                                               std::max<I>(0, bottom - top));
                cache->m_beforeback = background;
                cache->m_beforerect_typed = ::cw::lib::Rect{left, top,
                    std::max<I>(0, right - left), std::max<I>(0, bottom - top)};
            }
            blit_surface(background, image, x, y, blend);
            if (!nowait && anime && anime->countup() && wt != 0) add_background_lazydraw();
        } else if (m_animation_typed == 1) {
            blit_surface(background, image, x, y, blend);
            if (!nowait && anime && anime->countup() && wt != 0) add_background_lazydraw();
        }
    }
    if (!nowait) wait(anime, is_anime, wt);
}

void _JpySubImage::drawtemp(std::shared_ptr<AnimationCounter> anime)
{
    if (!anime) anime = std::make_shared<AnimationCounter>();
    if (m_animation_typed == 4) {
        if (anime->countup() && m_waittime_typed != 0) add_background_lazydraw();
        wait(anime, false, m_waittime_typed);
        return;
    }
    if (!m_animation_typed) return;
    std::shared_ptr<JpyCache> cache;
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&m_cache.storage()); p && *p)
        cache = std::dynamic_pointer_cast<JpyCache>(*p);
    if (!cache) cache = std::make_shared<JpyCache>();
    std::tuple<I, I> pos_noscale;
    if (m_animeposition_noscale_typed && m_animemove_noscale_typed) {
        pos_noscale = {std::get<0>(*m_animeposition_noscale_typed) + std::get<0>(*m_animemove_noscale_typed),
                       std::get<1>(*m_animeposition_noscale_typed) + std::get<1>(*m_animemove_noscale_typed)};
    } else if (m_animeposition_noscale_typed) {
        pos_noscale = *m_animeposition_noscale_typed;
    } else if (m_animemove_noscale_typed) {
        const auto current = cache->load_position_noscale();
        pos_noscale = {std::get<0>(current) + std::get<0>(*m_animemove_noscale_typed),
                       std::get<1>(current) + std::get<1>(*m_animemove_noscale_typed)};
    } else {
        pos_noscale = m_position_noscale_typed;
    }
    const I animespeed = ::cw::util::numwrap(m_animespeed_typed, 0, 255);
    auto background = ensure_temporal_background(cache);
    if (!animespeed) {
        _drawtemp_impl(anime, background, scale_xy(pos_noscale), true, false, m_waittime_typed, false);
    } else if (!anime->m_all_cut) {
        const auto goal = pos_noscale;
        pos_noscale = cache->load_position_noscale();
        I x = std::get<0>(pos_noscale);
        I y = std::get<1>(pos_noscale);
        I rest_x = std::get<0>(goal) - x;
        I rest_y = std::get<1>(goal) - y;
        const bool xdir = rest_x > -1;
        const bool ydir = rest_y > -1;
        constexpr I SPF = 8;
        I i = 0;
        while (rest_x || rest_y) {
            m_is_cacheable_typed = false;
            m_is_animated_typed = true;
            auto n = std::sqrt(static_cast<double>(rest_x * rest_x + rest_y * rest_y));
            n /= static_cast<double>(animespeed);
            if (n == 0) n = 1;
            // Py の round() は中間値を偶数へ丸めるが、nearbyint
            // は現在の丸めモードに従う。 製品コードはそのモードを変えない
            // が、別のモードでは最終位置が同じでも途中の位置が変わり得る。
            if (rest_x) {
                x = static_cast<I>(std::get<0>(pos_noscale) + std::nearbyint(rest_x / n));
                rest_x = std::get<0>(goal) - x;
                if ((rest_x < 0 && xdir) || (rest_x > 0 && !xdir)) {
                    x = std::get<0>(goal);
                    rest_x = 0;
                }
            }
            if (rest_y) {
                y = static_cast<I>(std::get<1>(pos_noscale) + std::nearbyint(rest_y / n));
                rest_y = std::get<1>(goal) - y;
                if ((rest_y < 0 && ydir) || (rest_y > 0 && !ydir)) {
                    y = std::get<1>(goal);
                    rest_y = 0;
                }
            }
            pos_noscale = {x, y};
            if (m_waittime_typed <= 0 || SPF <= m_waittime_typed) {
                _drawtemp_impl(anime, background, scale_xy(pos_noscale), true, true, m_waittime_typed, false);
            } else if (SPF <= i) {
                _drawtemp_impl(anime, background, scale_xy(pos_noscale), true, true, m_waittime_typed * SPF, false);
                i %= SPF;
            } else {
                if (m_animation_typed == 1)
                    _drawtemp_impl(anime, background, scale_xy(pos_noscale), true, true, m_waittime_typed, true);
                ++i;
            }
            m_starttick_typed = now_tick();
        }
    }
    cache->save_position_noscale(pos_noscale);
}

void _JpySubImage::wait(std::shared_ptr<AnimationCounter> anime,bool is_anime,std::optional<I> waittime)
{
    const I value = waittime.value_or(m_waittime_typed);
    (void)is_anime;
    if (value > 0) {
        m_is_cacheable_typed = false;
        m_is_animated_typed = true;
        if (!anime || anime->countup()) {
            const I cut = _cut_waittime(value);
            if (cut) wait_effectbooster(cut, anime);
            else if (::cw::g_cwpy && ::cw::g_cwpy->is_running())
                // Py の wait_frame(0) と同様、canskip=True と設定された FPS を使う。
                ::cw::g_cwpy->wait_frame(0, true, std::nullopt, 0);
        }
    } else if (value < 0) {
        m_is_cacheable_typed = false;
        m_is_animated_typed = true;
        if (!anime || anime->countup()) wait_effectbooster(0, anime);
    }
}

I _JpySubImage::_cut_waittime(I waittime)
{
    if (waittime < 0) return waittime;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_tick() - m_starttick_typed).count();
    if (elapsed <= 0) return waittime;
    return waittime - std::min<I>(waittime, elapsed);
}

void JpyBackGroundImage::initialize(std::shared_ptr<EffectBoosterConfig> config,std::shared_ptr<JpyCache> cache,bool mask)
{
    _JpySubImage::initialize(config,"init",cache);
    m_backcolor_typed = config ? config->get_color("init","backcolor",{0,0,0}) : std::tuple<I,I,I>{0,0,0};
    const auto width_opt = config ? config->get_int_3("init","backwidth",std::nullopt) : std::nullopt;
    m_width_bg_typed = width_opt ? scale_i(*width_opt) : -1;
    m_vanish_anime_typed = width_opt.has_value() && m_width_bg_typed < 0;
    m_height_bg_typed = scale_i(config ? config->get_int("init","backheight",-1) : -1);
    m_transparent_bg_typed = config ? config->get_bool("init","transparent",false) : false;
    m_dirdepth_typed = std::max<I>(0, config ? config->get_int("init","dirdepth",0) : 0);
    m_configpath_bg_typed = config ? ::cw::lib::encoding::path_to_utf8(std::filesystem::absolute(std::filesystem::u8path(config->m_path_typed))) : S{};
    m_configpath_typed = m_configpath_bg_typed;
    m_configdepth_typed = m_dirdepth_typed;
    m_position_noscale = {0,0};
    m_position_noscale_typed = {0,0};
    m_position_typed = scale_xy({0,0});
    m_visible_typed = false;
    m_transparent_typed = m_transparent_bg_typed;
    m_savecache_typed = 0;
    m_backcolor = V(std::to_string(std::get<0>(m_backcolor_typed))+","+std::to_string(std::get<1>(m_backcolor_typed))+","+std::to_string(std::get<2>(m_backcolor_typed)));
    m_transparent = V(m_transparent_bg_typed);
    m_vanish_anime = m_vanish_anime_typed;
    m_visible = false;
    (void)mask;
}

void JpyPartsImage::initialize(std::shared_ptr<EffectBoosterConfig> config,S section,std::shared_ptr<JpyCache> cache,bool mask)
{
    _JpySubImage::initialize(config,section,cache);
    m_height_parts_typed = scale_i(config ? config->get_int(section,"height",-1) : -1);
    const auto width_opt = config ? config->get_int_3(section,"width",std::nullopt) : std::nullopt;
    m_haswidth_parts_typed = width_opt.has_value();
    m_width_parts_typed = width_opt ? scale_i(*width_opt) : -1;
    m_haswidth = m_haswidth_parts_typed;
    m_color_typed = config ? config->get_color(section,"color",{0,0,0}) : std::tuple<I,I,I>{0,0,0};
    if (config) {
        const auto pos = config->get_ints(section,"position",V(2),{0,0});
        m_position_parts_noscale_typed = pos;
        m_position_noscale_typed = pos;
        m_position_typed = scale_xy(pos);
        m_savecache_parts_typed = config->get_int(section,"savecache",0);
        m_transparent_parts_typed = config->get_bool(section,"transparent",true);
        m_visible_parts_typed = config->get_bool(section,"visible",true);
    }
    m_width = m_width_parts_typed;
    m_height = m_height_parts_typed;
    m_color = V(std::to_string(std::get<0>(m_color_typed))+","+std::to_string(std::get<1>(m_color_typed))+","+std::to_string(std::get<2>(m_color_typed)));
    m_position = V(std::to_string(std::get<0>(m_position_typed))+","+std::to_string(std::get<1>(m_position_typed)));
    m_position_noscale = V(std::to_string(std::get<0>(m_position_noscale_typed))+","+std::to_string(std::get<1>(m_position_noscale_typed)));
    m_savecache = V(m_savecache_parts_typed);
    m_transparent = V(m_transparent_parts_typed);
    m_visible = V(m_visible_parts_typed);
    m_savecache_typed = m_savecache_parts_typed;
    m_transparent_typed = m_transparent_parts_typed;
    m_visible_typed = m_visible_parts_typed;
    (void)mask;
}


void main() { /* Python 版も何もしない単体実行用エントリーポイント。 */ }
}

