#include "cw/sprite/animationcell.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/image.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace cw::sprite::animationcell {
namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;
using Byte = std::uint8_t;

String value_string(const ::cw::lib::Value& value)
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    return {};
}

Dimension parse_dimension(const String& text)
{
    if (text.rfind("Ref:", 0) == 0) return text;
    // 共通の Unicode 数字・アンダースコアのパーサーで Py の int(str)
    // に合わせる。 ネイティブ寸法は固定幅 Integer なので
    // 、表現不能な整数は狭めたり折り返したりせず明示的に拒否する。
    if (const auto value = ::cw::lib::parse_python_integer(text)) return *value;
    throw std::runtime_error("アニメーション寸法の定義が正しくありません: " + text);
}

Integer floor_div2(Integer value)
{
    return value / 2 - (value < 0 && value % 2 != 0 ? 1 : 0);
}

Integer floor_division(Integer dividend, Integer divisor)
{
    if (divisor == 0) throw std::domain_error("animation frame divisor is zero");
    auto quotient = dividend / divisor;
    if (dividend % divisor != 0 && ((dividend < 0) != (divisor < 0))) --quotient;
    return quotient;
}

Integer floor_modulo(Integer dividend, Integer divisor)
{
    const auto quotient = floor_division(dividend, divisor);
    return dividend - quotient * divisor;
}

std::optional<Integer> dimension_integer(const Dimension& value)
{
    if (const auto* integer = std::get_if<Integer>(&value)) return *integer;
    if (const auto* real = std::get_if<Real>(&value)) return static_cast<Integer>(*real);
    return std::nullopt;
}

Integer require_dimension(const Dimension& value, const char* name)
{
    if (const auto result = dimension_integer(value)) return *result;
    throw std::runtime_error(String(name) + " は Ref:* のまま解決されていません。");
}

String join_skin(const String& path)
{
    if (path.empty() || !::cw::g_cwpy) return path;
    const auto skin = value_string(::cw::g_cwpy->m_skindir);
    if(skin.empty())return path;
    const auto native=[](const String& value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));};
    const auto combined=(native(skin)/native(path)).generic_u8string();
    return String(reinterpret_cast<const char*>(combined.data()),combined.size());
}

std::shared_ptr<::cw::lib::Surface> make_surface(Integer width, Integer height, Byte alpha = 255)
{
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    if (result->size.width > 0 && result->size.height > 0) {
        const auto pixel_count = static_cast<std::uint64_t>(result->size.width) *
                                 static_cast<std::uint64_t>(result->size.height);
        if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U)
            throw std::overflow_error("animation surface size overflow");
        result->rgba.resize(static_cast<std::size_t>(pixel_count * 4U), 0);
        for (std::size_t i=3;i<result->rgba.size();i+=4) result->rgba[i]=alpha;
        result->known_opaque = alpha == 255;
    }
    return result;
}

Integer scaled(Integer value)
{
    if (!std::isfinite(::cw::g_UP_SCR)) throw std::runtime_error("invalid screen scale");
    const auto result=static_cast<Real>(value)*::cw::g_UP_SCR;
    if(result>static_cast<Real>(std::numeric_limits<Integer>::max())||result<static_cast<Real>(std::numeric_limits<Integer>::lowest()))throw std::overflow_error("scaled animation dimension overflow");
    return static_cast<Integer>(result);
}

Integer scaled_fraction(Integer value, Integer factor, Integer divisor)
{
    if (divisor == 0) throw std::domain_error("animation scale divisor is zero");
    const auto result = static_cast<long double>(value) * static_cast<long double>(factor) /
                        static_cast<long double>(divisor);
    if (result > static_cast<long double>(std::numeric_limits<Integer>::max()) ||
        result < static_cast<long double>(std::numeric_limits<Integer>::lowest()))
        throw std::overflow_error("animation scale arithmetic overflow");
    return static_cast<Integer>(result);
}

std::shared_ptr<::cw::lib::Surface> scaled_surface(const std::shared_ptr<::cw::lib::Surface>& source,Integer width,Integer height,bool smooth)
{
    if(!source||width<=0||height<=0)return make_surface(0,0);
    if(source->size.width==width&&source->size.height==height)return source;
    return ::cw::image::smoothscale(source,{width,height},smooth,false);
}

std::shared_ptr<::cw::lib::Surface> alpha_surface(const std::shared_ptr<::cw::lib::Surface>& source,Integer alpha)
{
    if(!source)return{};auto result=std::make_shared<::cw::lib::Surface>(*source);const auto factor=static_cast<std::uint32_t>(std::clamp<Integer>(alpha,0,255));
    for(std::size_t i=3;i<result->rgba.size();i+=4)result->rgba[i]=static_cast<std::uint8_t>((static_cast<std::uint32_t>(result->rgba[i])*factor)/255U);
    return result;
}

bool rect_empty(const ::cw::lib::Rect& rect)
{
    return rect.width <= 0 || rect.height <= 0;
}

std::optional<::cw::lib::Rect> intersect_rect(const ::cw::lib::Rect& a, const ::cw::lib::Rect& b)
{
    const auto left = std::max(a.x, b.x);
    const auto top = std::max(a.y, b.y);
    const auto right = std::min(a.x + a.width, b.x + b.width);
    const auto bottom = std::min(a.y + a.height, b.y + b.height);
    if (left >= right || top >= bottom) return std::nullopt;
    return ::cw::lib::Rect{left, top, right - left, bottom - top};
}

::cw::lib::Rect union_rect(const ::cw::lib::Rect& a, const ::cw::lib::Rect& b)
{
    if (rect_empty(a)) return b;
    if (rect_empty(b)) return a;
    const auto left = std::min(a.x, b.x);
    const auto top = std::min(a.y, b.y);
    const auto right = std::max(a.x + a.width, b.x + b.width);
    const auto bottom = std::max(a.y + a.height, b.y + b.height);
    return {left, top, right - left, bottom - top};
}

void clear_opaque_black(const std::shared_ptr<::cw::lib::Surface>& surface, const ::cw::lib::Rect& rect)
{
    if (!surface || surface->size.width <= 0 || surface->size.height <= 0) return;
    const auto clipped = intersect_rect(rect, {0, 0, surface->size.width, surface->size.height});
    if (!clipped) return;
    for (Integer y = clipped->y; y < clipped->y + clipped->height; ++y) {
        auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->size.width) +
                       static_cast<std::size_t>(clipped->x)) * 4U;
        for (Integer x = 0; x < clipped->width; ++x, offset += 4U) {
            surface->rgba[offset] = 0;
            surface->rgba[offset + 1] = 0;
            surface->rgba[offset + 2] = 0;
            surface->rgba[offset + 3] = 255;
        }
    }
}

void alpha_blit(const std::shared_ptr<::cw::lib::Surface>& dest,
                const std::shared_ptr<::cw::lib::Surface>& source,
                const ::cw::lib::Rect& rect,
                const std::optional<::cw::lib::Rect>& clip = std::nullopt)
{
    if(!dest||!source||dest->size.width<=0||dest->size.height<=0||source->size.width<=0||source->size.height<=0)return;
    const auto visible = intersect_rect(rect, {0, 0, dest->size.width, dest->size.height});
    if (!visible) return;
    const auto clipped = clip ? intersect_rect(*visible, *clip) : visible;
    if (!clipped) return;
    const Integer sx_begin = clipped->x - rect.x;
    const Integer sy_begin = clipped->y - rect.y;
    const Integer sx_end = sx_begin + clipped->width;
    const Integer sy_end = sy_begin + clipped->height;
    for(Integer sy=sy_begin;sy<sy_end;++sy){const Integer dy=rect.y+sy;
        const auto si=(static_cast<std::size_t>(sy)*static_cast<std::size_t>(source->size.width)+static_cast<std::size_t>(sx_begin))*4U;
        const auto di=(static_cast<std::size_t>(dy)*static_cast<std::size_t>(dest->size.width)+static_cast<std::size_t>(clipped->x))*4U;
        const auto bytes=static_cast<std::size_t>(clipped->width)*4U;
        // pygame の確保済み描画面には途中で切れた行はない。 完全な SIMD
        // 範囲を渡す前に、この C++ のバッファー境界を検証する。
        if(sx_begin<0||sx_end>source->size.width||sy<0||sy>=source->size.height||
           si>source->rgba.size()||bytes>source->rgba.size()-si||
           di>dest->rgba.size()||bytes>dest->rgba.size()-di)
            throw std::runtime_error("animation surface buffer is truncated");
        ::cw::lib::simd_image::alpha_blend_rgba(source->rgba.data()+si,bytes,
            dest->rgba.data()+di,bytes,static_cast<std::size_t>(clipped->width),1,source->surface_alpha);
    }
}

String ref_name(const Dimension& value)
{
    if (const auto* text = std::get_if<String>(&value)) {
        if (text->rfind("Ref:", 0) == 0) return text->substr(4);
    }
    return {};
}
}
void _AnimationPart::initialize(std::shared_ptr<::cw::sprite::animationcell::AnimationCell> arg_parent, std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_startframe)
{
    if (!arg_parent || !arg_data) throw std::invalid_argument("AnimationPart requires parent and XML data");
    m_parent = arg_parent;
    m_startframe = arg_startframe;
    const auto tag = arg_data->get_tag();
    if (tag == "Animations") {
        m_type = "Group";
        m_parallel = arg_data->getbool(".", ::cw::lib::Value("parallel"), false);
        Integer next = m_startframe;
        for (const auto& child : arg_data->m_children) {
            auto part = std::make_shared<_AnimationPart>();
            part->initialize(arg_parent, child, next);
            m_parts.push_back(part);
            if (m_parallel) continue;
            if (part->m_type == "Group")
                throw std::runtime_error("nested sequential animation group has no spawn/duration");
            next += part->m_spawn + part->m_duration;
        }
        return;
    }
    if (tag != "Animation") throw std::runtime_error("Invalid animation: " + tag);
    m_type = "Part";
    m_id = arg_data->getattr(".", "id", "");
    if (!m_id.empty()) arg_parent->m_animation_table[m_id] = shared_from_this();
    m_imgpath = ::cw::util::validate_filepath(arg_data->gettext("ImagePath", ""));
    if (!m_imgpath.empty()) {
        m_imgpath = join_skin(m_imgpath);
        m_imgpath = ::cw::util::get_materialpathfromskin(m_imgpath, ::cw::g_M_IMG, true);
    }
    const auto left = arg_data->getattr("Location", "left", "0");
    const auto top = arg_data->getattr("Location", "top", "0");
    const auto width = arg_data->getattr("Size", "width", "Original");
    const auto height = arg_data->getattr("Size", "height", "Original");
    m__rect_str = {left, top, width, height};
    m_fill_color = std::make_tuple(arg_data->getint("Fill", ::cw::lib::Value("r"), 0), arg_data->getint("Fill", ::cw::lib::Value("g"), 0), arg_data->getint("Fill", ::cw::lib::Value("b"), 0));
    m__fill_color = *m_fill_color;
    m_mask = arg_data->getbool("Mask", std::nullopt, false);
    m_animation_type = arg_data->gettext("AnimationType", "None");
    m_animation_frame = arg_data->getint("AnimationType", ::cw::lib::Value("frame"), 20);
    m_repeat_count = arg_data->getint("Repeat", std::nullopt, 1);
    m_repeat_interval = arg_data->getint("Repeat", ::cw::lib::Value("interval"), 0);
    m_spawn = arg_data->getint("Spawn", std::nullopt, 0);
    m_duration = arg_data->getint("Duration", std::nullopt, 20);
    if (m_animation_type == "Rotate" && m_animation_frame > 0) {
        m_dealing_scales.reserve(static_cast<std::size_t>(m_animation_frame));
        for (Integer i = 0; i < m_animation_frame; ++i) {
            const auto degrees = 180.0 * static_cast<Real>(i) / static_cast<Real>(m_animation_frame);
            const auto radians = degrees * (3.1415926535897932384626433832795 / 180.0);
            m_dealing_scales.push_back(static_cast<Integer>(std::sin(radians) * 100.0));
        }
    }
    m_image = make_surface(0, 0);
    m_image_noscale = make_surface(0, 0);
}

void _AnimationPart::load_cell()
{
    const auto parent = m_parent.lock();
    if (!parent) throw std::runtime_error("AnimationPart parent was destroyed");
    auto [left, top, width, height] = m__rect_str;
    if (!m_imgpath.empty()) {
        const auto keypath=::cw::util::get_keypath(::cw::util::get_symlinktarget(m_imgpath));
        const auto key = std::make_tuple(keypath, ::cw::g_UP_SCR);
        const auto cached = parent->m_cache.find(key);
        if (cached != parent->m_cache.end()) m_image_noscale = cached->second;
        else {
            m_image_noscale = ::cw::util::load_image(m_imgpath, m_mask, {0, 0}, std::nullopt, true, false, true, false, std::nullopt, false);
            if (!m_image_noscale) m_image_noscale = make_surface(0, 0);
            parent->m_cache.emplace(key, m_image_noscale);
        }
        m_fill_color.reset();
        m__has_alpha = false;
        for(std::size_t i=3;i<m_image_noscale->rgba.size();i+=4)if(m_image_noscale->rgba[i]!=255){m__has_alpha=true;break;}
        // 選択した @2x/@4x 素材は Surface::size に物理ピクセル、
        // scr_scale に入力の論理倍率を持つ。 Original は論理単位で解
        // 決し、update_scale() が画面倍率を一度だけ適用するようにする。
        const auto source_scale = std::max<::cw::lib::Integer>(1, m_image_noscale->scr_scale);
        const auto logical_width = m_image_noscale->size.width / source_scale;
        const auto logical_height = m_image_noscale->size.height / source_scale;
        if (width == "Original") width = logical_width ? std::to_string(logical_width) : std::to_string(std::get<0>(parent->m_size_noscale));
        if (height == "Original") height = logical_height ? std::to_string(logical_height) : std::to_string(std::get<1>(parent->m_size_noscale));
    } else {
        m_image_noscale = make_surface(4, 4);
        m_mask = false;
        m__has_alpha = false;
        const auto [red, green, blue] = *m_fill_color;
        if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255)
            throw std::invalid_argument("invalid animation fill color");
        ::cw::lib::simd_image::fill_rgba(
            m_image_noscale->rgba.data(), m_image_noscale->rgba.size() / 4U,
            static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
            static_cast<std::uint8_t>(blue), 255);
        if (width == "Original") width = std::to_string(std::get<0>(parent->m_size_noscale));
        if (height == "Original") height = std::to_string(std::get<1>(parent->m_size_noscale));
    }
    if (width == "Max") width = std::to_string(std::get<0>(parent->m_size_noscale));
    if (height == "Max") height = std::to_string(std::get<1>(parent->m_size_noscale));
    const auto width_value = parse_dimension(width);
    const auto height_value = parse_dimension(height);
    if (left == "Center") {
        const auto numeric = dimension_integer(width_value);
        if (!numeric) throw std::runtime_error("left に Center を指定している場合、width に Ref:* を指定できません。");
        left = std::to_string(floor_div2(std::get<0>(parent->m_size_noscale) - *numeric));
    }
    if (top == "Center") {
        const auto numeric = dimension_integer(height_value);
        if (!numeric) throw std::runtime_error("top に Center を指定している場合、height に Ref:* を指定できません。");
        top = std::to_string(floor_div2(std::get<1>(parent->m_size_noscale) - *numeric));
    }
    m_pos_noscale = {parse_dimension(left), parse_dimension(top)};
    m_size_noscale = {width_value, height_value};
    if (!ref_name(std::get<0>(m_pos_noscale)).empty() || !ref_name(std::get<1>(m_pos_noscale)).empty() || !ref_name(std::get<0>(m_size_noscale)).empty() || !ref_name(std::get<1>(m_size_noscale)).empty()) parent->m_refs.push_back(shared_from_this());
}

void _AnimationPart::update_scale()
{
    if (!m_image_noscale) return;
    if (m_image_noscale->size.width == 0) {
        m__image = m_image_noscale;
        update_image();
        return;
    }
    const auto width = require_dimension(std::get<0>(m_size_noscale), "width");
    const auto height = require_dimension(std::get<1>(m_size_noscale), "height");
    // Python と同様、論理寸法が変わらない場合は cw.s(image_noscale)
    // を使う。 小数の画面倍率の平滑化は ExpandDrawing と独立させ
    // 、明示的に寸法変更した部品だけが smoothscale_bg を参照する。
    const auto source_scale = m_image_noscale->scr_scale > 0.0 ? m_image_noscale->scr_scale : 1.0;
    const bool original_size = width == static_cast<::cw::lib::Integer>(std::floor(m_image_noscale->size.width/source_scale)) &&
        height == static_cast<::cw::lib::Integer>(std::floor(m_image_noscale->size.height/source_scale));
    const bool smooth = original_size || (!m_imgpath.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                        ::cw::g_cwpy->m_setting->m_smoothscale_bg);
    m__image=scaled_surface(m_image_noscale,scaled(width),scaled(height),smooth);
    update_image();
}

void _AnimationPart::clear_image()
{
    m_image = make_surface(0, 0);
    m_rect = {};
}

void _AnimationPart::update_image()
{
    const auto parent = m_parent.lock();
    if (!parent || !m__image || m__image->size.width <= 0 || m__image->size.height <= 0) { clear_image(); return; }
    Integer frame = parent->m_frame - m_startframe - m_spawn;
    if (frame < 0 || m_duration <= frame) { clear_image(); return; }
    if (m_animation_frame != 0) {
        const Integer aframe = m_repeat_count > 1 ? m_animation_frame + m_repeat_interval : m_animation_frame;
        if (aframe == 0) throw std::domain_error("animation frame divisor is zero");
        const Integer repeat = floor_division(frame, aframe);
        if (m_repeat_count > 1 && m_repeat_count <= repeat) { clear_image(); return; }
        if (m_repeat_count > 1) {
            frame = floor_modulo(frame, aframe);
            if (m_animation_frame <= frame) { clear_image(); return; }
        }
    } else frame = 0;
    const Integer pos_x = scaled(require_dimension(std::get<0>(m_pos_noscale), "left"));
    const Integer pos_y = scaled(require_dimension(std::get<1>(m_pos_noscale), "top"));
    const Integer width = scaled(require_dimension(std::get<0>(m_size_noscale), "width"));
    const Integer height = scaled(require_dimension(std::get<1>(m_size_noscale), "height"));
    if (m_animation_type == "Spawn") {
        m_image = m__image;
        m_rect = {pos_x, pos_y, width, height};
    } else if (m_animation_type == "FadeIn" || m_animation_type == "FadeOut") {
        m_rect = {pos_x, pos_y, width, height};
        if(m_animation_frame<=0)throw std::runtime_error("Fade animation frame must be positive");
        Integer alpha=std::min<Integer>(255,scaled_fraction(frame,255,m_animation_frame));if(m_animation_type=="FadeOut")alpha=255-alpha;
        // アルファ値に
        // 不透明度を掛
        // けて255で
        // 割り、完全不
        // 透明の場合は
        // 入力画像を再
        // 利用する。
        m_image = alpha >= 255 ? m__image : alpha_surface(m__image,alpha);
    } else if (m_animation_type == "Rotate") {
        if (m_dealing_scales.empty()) throw std::runtime_error("Rotate animation frame is zero");
        auto scale_index = frame;
        if (scale_index < 0) scale_index += static_cast<Integer>(m_dealing_scales.size());
        if (scale_index < 0 || scale_index >= static_cast<Integer>(m_dealing_scales.size()))
            throw std::out_of_range("Rotate animation frame exceeds the dealing scale table");
        const Integer scaled_width = scaled_fraction(width,m_dealing_scales[static_cast<std::size_t>(scale_index)],100);
        m_image = scaled_surface(m__image,scaled_width,height,true);
        m_rect = {pos_x + (width - scaled_width) / 2, pos_y, scaled_width, height};
    } else {
        throw std::runtime_error("Invalid AnimationType: " + m_animation_type);
    }
}

void AnimationCell::initialize(::cw::lib::String arg_path, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::shared_ptr<::cw::lib::Object> arg_spritegrp, ::cw::lib::Integer arg_layer)
{
    if (!arg_spritegrp) throw std::invalid_argument("AnimationCell requires a sprite group");
    // Python と同様、グループなしで SelectableSprite を初期化
    // し、XML を解析して update_scale() で画像・矩形を設定してから
    // add_layer する。 新しいネイティブオブジェクトにはまだ画像・矩形がな
    // いため、先に登録すると 0×0 のプロキシーが公開され、Opening が何も合
    // 成しなくなる。 Py の DirtySprite 初期化は既存の形を保持する。
    if (m_ui_backend) {
        m_ui_backend->unregister_sprite(*this);
        m_ui_backend.reset();
    }
    ::cw::sprite::base::SelectableSprite::initialize({});
    m_selectable_on_event = true;
    m_status = "normal";
    m_animation_table.clear();
    m_refs.clear();
    m_cache.clear();
    m_self_alias.reset();
    m_size_noscale = arg_size_noscale;
    m_pos_noscale = arg_pos_noscale;
    m_end_frame = 0;
    const auto data = ::cw::data::xml2element(arg_path, "", std::nullopt, std::nullopt, false, std::nullopt);
    if (!data) throw std::runtime_error("AnimationCell XMLを読み込めません: " + arg_path);
    m_animations = std::make_shared<_AnimationPart>();
    std::shared_ptr<AnimationCell> owner;
    try { owner=shared_from_this(); }
    catch(const std::bad_weak_ptr&) {
        // スタック所有の C++ オブジェクトも Python インス
        // タンスと同じ生存関係を持ち、部品はこのセルが所有する。 shared_ptr
        // 専用の構築を要求せず、非所有の参照を使う。
        owner=std::shared_ptr<AnimationCell>(this, [](AnimationCell*) {});
        m_self_alias=owner;
    }
    m_animations->initialize(owner, data, 0);
    for (const auto& anime : _iter_animes(m_animations)) if (anime) m_end_frame = std::max(m_end_frame, anime->m_startframe + anime->m_spawn + anime->m_duration);
    update_scale();
    m_tlayer={arg_layer,0,0,0};
    ::cw::lib::Arguments groups;
    groups.emplace_back(std::static_pointer_cast<::cw::lib::Object>(arg_spritegrp));
    if (!m_ui_backend) m_ui_backend = ::cw::sprite::base::sprite_ui_backend();
    if (m_ui_backend) {
        m_ui_backend->unregister_sprite(*this);
        m_ui_backend->register_sprite(*this, groups);
        m_ui_backend->invalidate(*this, m_rect);
    }
}

AnimationCell::~AnimationCell()
{
    m_refs.clear();m_animation_table.clear();m_animations.reset();m_cache.clear();m_image.reset();
}

std::vector<std::shared_ptr<::cw::sprite::animationcell::_AnimationPart>> AnimationCell::_iter_animes(std::shared_ptr<::cw::sprite::animationcell::_AnimationPart> arg_anime)
{
    std::vector<std::shared_ptr<_AnimationPart>> result;
    if (!arg_anime) return result;
    if (arg_anime->m_type == "Part") result.push_back(arg_anime);
    for (const auto& part : arg_anime->m_parts) {
        auto children = _iter_animes(part);
        result.insert(result.end(), children.begin(), children.end());
    }
    return result;
}

void AnimationCell::update_scale()
{
    if (!m_animations) return;
    for (const auto& anime : _iter_animes(m_animations)) if (anime) anime->load_cell();
    for (const auto& anime : m_refs) {
        if (!anime) continue;
        auto resolve = [this](Dimension value, const char* name) -> Dimension {
            const auto ref = ref_name(value);
            if (ref.empty()) return value;
            const auto found = m_animation_table.find(ref);
            if (found == m_animation_table.end() || !found->second) throw std::out_of_range(String("参照アニメーションが存在しません: ") + ref);
            const auto& target = found->second;
            if (std::string(name) == "left") return std::get<0>(target->m_pos_noscale);
            if (std::string(name) == "top") return std::get<1>(target->m_pos_noscale);
            if (std::string(name) == "width") return std::get<0>(target->m_size_noscale);
            return std::get<1>(target->m_size_noscale);
        };
        anime->m_pos_noscale = {resolve(std::get<0>(anime->m_pos_noscale), "left"), resolve(std::get<1>(anime->m_pos_noscale), "top")};
        anime->m_size_noscale = {resolve(std::get<0>(anime->m_size_noscale), "width"), resolve(std::get<1>(anime->m_size_noscale), "height")};
    }
    m_refs.clear();
    m_rect = {scaled(std::get<0>(m_pos_noscale)), scaled(std::get<1>(m_pos_noscale)), scaled(std::get<0>(m_size_noscale)), scaled(std::get<1>(m_size_noscale))};
    m_image = make_surface(m_rect.width,m_rect.height);
    m_frame_dirty = m_rect;
    for (const auto& anime : _iter_animes(m_animations)) if (anime) anime->update_scale();
    m_dirty=2;if(m_ui_backend)m_ui_backend->invalidate(*this,m_rect);
}

void AnimationCell::quit()
{
    const auto old=m_rect;
    m_status = "normal";
    m_rect = {};
    m_frame = 0;
    m_frame_dirty = old;
    m_dirty=1;if(m_ui_backend)m_ui_backend->invalidate(*this,old);
}

void AnimationCell::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args; (void)arg_kwargs;
    auto* context=::cw::sprite::base::runtime_context();
    if(!context||context->selection!=this)update_selection();
    if(context&&!context->selection){
        if (::cw::g_cwpy && !m_self_alias) {
            auto owner = m_self_alias;
            try { owner=shared_from_this(); } catch(const std::bad_weak_ptr&) {}
            if (!owner) throw std::logic_error("AnimationCell has no owner");
            ::cw::g_cwpy->change_selection(
                std::static_pointer_cast<::cw::sprite::base::SelectableSprite>(owner), std::nullopt);
        } else {
            const bool notify=context->lazy_selection!=this;
            context->selection=this;
            context->lazy_selection=this;
            if(notify&&context->selection_changed)context->selection_changed(nullptr,this);
        }
    }
    if (m_status == "normal") update_normal();
    else if (m_status == "animation") update_animation();
}

void AnimationCell::update_normal()
{
    auto* context=::cw::sprite::base::runtime_context();
    if(!context||context->selection!=this)update_selection();
    if(context&&!context->selection){
        if (::cw::g_cwpy && !m_self_alias) {
            auto owner = m_self_alias;
            try { owner=shared_from_this(); } catch(const std::bad_weak_ptr&) {}
            if (!owner) throw std::logic_error("AnimationCell has no owner");
            ::cw::g_cwpy->change_selection(
                std::static_pointer_cast<::cw::sprite::base::SelectableSprite>(owner), std::nullopt);
        } else {
            const bool notify=context->lazy_selection!=this;
            context->selection=this;
            context->lazy_selection=this;
            if(notify&&context->selection_changed)context->selection_changed(nullptr,this);
        }
    }
}

void AnimationCell::lclick_event()
{
    if (!::cw::g_cwpy) throw std::runtime_error("cwpy is unavailable");
    ::cw::g_cwpy->m_cut_animation = true;
}

void AnimationCell::rclick_event()
{
    if (!::cw::g_cwpy) throw std::runtime_error("cwpy is unavailable");
    ::cw::g_cwpy->m_cut_animation = true;
}

void AnimationCell::update_animation()
{
    if (m_end_frame <= m_frame || m_skipped || (::cw::g_cwpy && ::cw::g_cwpy->m_cut_animation)) { quit(); return; }
    if (rect_empty(m_rect)) { quit(); return; }
    if (!m_image || m_image->size.width != m_rect.width || m_image->size.height != m_rect.height)
        m_image = make_surface(m_rect.width, m_rect.height);
    const auto parts = m_animations ? _iter_animes(m_animations)
                                    : std::vector<std::shared_ptr<_AnimationPart>>{};

    // pygame版はAnimationCellの面を毎フレーム全面クリアしてから、全パーツを
    // XML順に再合成する。回転・拡縮画像を旧/新矩形だけで差分更新すると、
    // 補間画素や重なった別パーツが差分境界に残り、前フレームの軌跡になる。
    // AnimationCellは通常エリアの部分更新とは独立しているため、ここだけは
    // Pythonと同じ決定的な全面再合成を行う。
    const auto local_bounds = ::cw::lib::Rect{0, 0, m_image->size.width, m_image->size.height};
    clear_opaque_black(m_image, local_bounds);
    for (const auto& part : parts) {
        if (!part) continue;
        part->update_image();
        if (part->m_image && !rect_empty(part->m_rect))
            alpha_blit(m_image, part->m_image, part->m_rect, local_bounds);
    }
    m_frame_dirty = m_rect;
    m_dirty=1;
    if(m_ui_backend)m_ui_backend->invalidate(*this,m_rect);
    m_frame = get_frame();
    if (m_end_frame <= m_frame || m_skipped || (::cw::g_cwpy && ::cw::g_cwpy->m_cut_animation)) quit();
}

void main()
{
}

}
