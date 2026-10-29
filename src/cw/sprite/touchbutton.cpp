// cw/sprite/touchbutton.py の移植。SDL 描画は Surface
// の背後へ分離し、状態・入力の動作は独立して使えるようにする。
#include "cw/sprite/touchbutton.hpp"
#include "cw/cw.hpp"
#include "cw/setting.hpp"
#include "cw/imageretouch.hpp"
#include "cw/thread.hpp"
#include "cw/eventhandler.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/sprite/statusbar.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
using I = ::cw::lib::Integer;

bool enabled(const ::cw::lib::Callback& f) {
    if (!f) return false;
    const auto value = f({}, {});
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p;
    if (const auto* p = std::get_if<::cw::lib::Integer>(&value.storage())) return *p != 0;
    if (const auto* p = std::get_if<::cw::lib::Real>(&value.storage())) return *p != 0.0;
    if (const auto* p = std::get_if<::cw::lib::String>(&value.storage())) return !p->empty();
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) return static_cast<bool>(*p);
    return false;
}

void select_touch_image(::cw::sprite::touchbutton::TouchButton& button, bool selected) {
    button.m_image=enabled(button.m_is_enabled)
        ? (selected?button.get_selectedimage():button.get_unselectedimage())
        : button.m__disabledimage;
}

I integer_value(const ::cw::lib::Value& value, I fallback = -1) {
    if (const auto* integer = std::get_if<I>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<I>(*real);
    if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        try { return decimal->to_integer(::cw::lib::DecimalRounding::Down); } catch (...) { return fallback; }
    }
    return fallback;
}

// Python の cw.s() と同様、int(n * UP_SCR) でゼロ方向へ切り捨てる。
I scr(I value) {
    const auto factor = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
    return static_cast<I>(static_cast<::cw::lib::Real>(value) * factor);
}

// Python の // と同じ、負の無限大方向への除算。
I floor_div(I numerator, I denominator) {
    if (denominator == 0) return 0;
    const I quotient = numerator / denominator;
    const I remainder = numerator % denominator;
    if (remainder != 0 && ((numerator < 0) != (denominator < 0))) return quotient - 1;
    return quotient;
}

// Python の round(x, 2) と同様、正確な二進 double を小数点以下二桁へ丸
// める。 先に 100 を掛けると、中間値の境界を丸めで越える場合があるため、その方式は使わない。
double python_round_2(double value) {
    char text[32];
    const auto written=std::to_chars(text,text+sizeof(text),value,std::chars_format::fixed,2);
    if(written.ec!=std::errc{})throw std::runtime_error("volume decimal rounding failed");
    // 元実装との差: Android NDK の libc++ には浮動小数の std::from_chars が無い。
    // 文字列は "[-]整数部.dd" の固定2桁なので、桁を整数として読み 100.0 で割る。
    // 整数/100.0 は正しく丸められた商で、from_chars の結果と同じ値になる。
    const char* cursor=text;
    const bool negative=cursor!=written.ptr && *cursor=='-';
    if(negative)++cursor;
    long long scaled=0;
    for(;cursor!=written.ptr;++cursor){
        if(*cursor=='.')continue;
        if(*cursor<'0'||*cursor>'9')throw std::runtime_error("volume decimal parsing failed");
        scaled=scaled*10+(*cursor-'0');
    }
    const double rounded=static_cast<double>(scaled)/100.0;
    return negative?-rounded:rounded;
}

I font_noscale(const ::cw::lib::String& role) {
    I size = 14;
    if (role == "sbarprogress") size = 16;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto fonts = ::cw::g_cwpy->m_setting->get_fonttypes();
        if (const auto it = fonts.find(role); it != fonts.end()) size = std::get<2>(it->second);
    }
    return std::max<I>(1, size);
}

I font_point_size(const ::cw::lib::String& role) {
    return std::max<I>(1, scr(font_noscale(role)));
}

bool font_bold(const ::cw::lib::String& role) {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto fonts = ::cw::g_cwpy->m_setting->get_fonttypes();
        if (const auto it = fonts.find(role); it != fonts.end())
            return ::cw::g_UP_SCR > 1.0 ? std::get<4>(it->second) : std::get<3>(it->second);
    }
    return role == "sbardesctitle" || role == "sbarprogress";
}

std::shared_ptr<::cw::lib::Surface> clone(const std::shared_ptr<::cw::lib::Surface>& surface) {
    return surface ? std::make_shared<::cw::lib::Surface>(*surface) : nullptr;
}

std::shared_ptr<::cw::lib::Surface> make_surface(::cw::lib::Integer width, ::cw::lib::Integer height,
                                                  std::uint8_t r = 0, std::uint8_t g = 0,
                                                  std::uint8_t b = 0, std::uint8_t a = 192) {
    width = std::max<::cw::lib::Integer>(1, width);
    height = std::max<::cw::lib::Integer>(1, height);
    auto surface = std::make_shared<::cw::lib::Surface>();
    surface->size = {width, height};
    surface->rgba.resize(static_cast<std::size_t>(width * height * 4));
    for (std::size_t i = 0; i < surface->rgba.size(); i += 4) {
        surface->rgba[i] = r;
        surface->rgba[i + 1] = g;
        surface->rgba[i + 2] = b;
        surface->rgba[i + 3] = a;
    }
    return surface;
}

void fill_rect(const std::shared_ptr<::cw::lib::Surface>& surface, const ::cw::lib::Rect& rect,
               std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

std::shared_ptr<::cw::lib::Surface> render_text(const ::cw::lib::String& text,
                                                const ::cw::lib::String& role,
                                                bool bold,
                                                ::cw::lib::Color color) {
    const auto size = font_point_size(role);
    if (text.empty()) return make_surface(1, size, 0, 0, 0, 0);
    // 全ホストで Py とメッセージ文字と同じ Resource/Font の
    // 経路を使う。 旧ステータス文字のコールバックは用途を捨て、有効な書体や斜
    // 体設定を渡せなかった。 ホストは転送だけを担当し、フォントは管理しない。
    ::cw::setting::Resource fallback;
    auto resource = ::cw::g_cwpy ? ::cw::g_cwpy->m_rsrc : nullptr;
    if (resource) {
        const auto* value = std::get_if<::cw::lib::ObjectPtr>(&resource->m_fonts.storage());
        auto table = value && *value ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*value) : nullptr;
        if (table) {
            const auto item = table->get_item(::cw::lib::Value(role));
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&item.storage());
            if (auto font = object && *object ? std::dynamic_pointer_cast<::cw::imageretouch::Font>(*object) : nullptr)
                return font->render(text, true,
                                    {color.r, color.g, color.b});
        }
    }
    if (!resource && ::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        fallback.m_setting = std::static_pointer_cast<::cw::lib::Object>(::cw::g_cwpy->m_setting);
    auto* resolver = resource ? resource.get() : &fallback;
    auto font = resolver->create_font(role, "", "", font_noscale(role), bold, bold, false,
                                      0, false, std::nullopt);
    if (font) return font->render(text, true, {color.r, color.g, color.b});
    // ネイティブのフォント素材がなければ、既存の配置の代替処理を維持する。
    const auto cells = std::max(1, ::cw::lib::encoding::text_width(text));
    const auto cell = std::max<I>(1, size / 2);
    auto image = make_surface(static_cast<::cw::lib::Integer>(cells * cell), size, 0, 0, 0, 0);
    for (::cw::lib::Integer i = 0; i < cells; ++i) {
        const auto left = i * cell + 1;
        fill_rect(image, {left, std::max<I>(1, size / 5), bold ? cell - 2 : std::max<I>(1, cell - 3),
                          std::max<I>(1, size - size / 5 * 2)}, color.r, color.g, color.b, color.a);
        fill_rect(image, {left + 1, std::max<I>(2, size / 4), bold ? std::max<I>(1, cell - 4) : std::max<I>(1, cell - 5),
                          std::max<I>(1, size / 2)}, 0, 0, 0, 0);
    }
    return image;
}

I font_height(const ::cw::lib::String& role) {
    const auto sample = render_text("Ag", role, font_bold(role), {255, 255, 255, 255});
    return sample ? sample->size.height : font_point_size(role);
}

::cw::lib::Integer text_width(const ::cw::lib::String& text, const ::cw::lib::String& role = "sbardesc") {
    const auto image = render_text(text, role, font_bold(role), {255, 255, 255, 255});
    return image ? image->size.width : 0;
}

std::shared_ptr<::cw::lib::Surface> volume_icon() {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_pygamedialogs.storage());
        const auto table = object && *object
            ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object) : nullptr;
        if (table) {
            try {
                const auto value = table->get_item(::cw::lib::Value(::cw::lib::String("VOLUME")));
                const auto* surface_object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
                if (surface_object && *surface_object) {
                    if (const auto loaded = std::dynamic_pointer_cast<::cw::lib::Surface>(*surface_object)) {
                        auto icon = std::make_shared<::cw::lib::Surface>(*loaded);
                        if (icon && icon->rgba.size() >= 4 && icon->rgba[3] != 0) {
                            const auto key_r = icon->rgba[0];
                            const auto key_g = icon->rgba[1];
                            const auto key_b = icon->rgba[2];
                            for (std::size_t i = 0; i + 3 < icon->rgba.size(); i += 4) {
                                if (icon->rgba[i] == key_r && icon->rgba[i + 1] == key_g &&
                                    icon->rgba[i + 2] == key_b)
                                    icon->rgba[i + 3] = 0;
                            }
                        }
                        return icon;
                    }
                }
            } catch (const std::exception&) {
            }
        }
    }
    if (const auto backend = ::cw::sprite::statusbar::host_services(); backend && backend->load_resource) {
        if (auto loaded = backend->load_resource("VOLUME", font_point_size("sbarprogress"), {255, 255, 255, 255}))
            return loaded;
    }
    const auto side = scr(16);
    auto icon = make_surface(side, side, 0, 0, 0, 0);
    fill_rect(icon, {scr(2), scr(6), scr(4), scr(4)}, 255, 255, 255, 255);
    fill_rect(icon, {scr(6), scr(4), scr(3), scr(8)}, 255, 255, 255, 255);
    fill_rect(icon, {scr(10), scr(5), scr(2), scr(6)}, 192, 192, 192, 255);
    fill_rect(icon, {scr(13), scr(3), scr(1), scr(10)}, 128, 128, 128, 255);
    return icon;
}

void invalidate(const ::cw::lib::Rect& rect) {
    if (const auto backend = ::cw::sprite::statusbar::host_services(); backend && backend->invalidate)
        backend->invalidate(rect);
    if (::cw::g_cwpy) ::cw::g_cwpy->add_lazydraw_rect(rect);
}

void tint_rgb(const std::shared_ptr<::cw::lib::Surface>& surface, int delta) {
    if (!surface) return;
    for (std::size_t i = 0; i + 3 < surface->rgba.size(); i += 4) {
        const auto adjust = [delta](std::uint8_t value) {
            const auto adjusted = static_cast<int>(value) + delta;
            return static_cast<std::uint8_t>(std::clamp(adjusted, 0, 255));
        };
        surface->rgba[i] = adjust(surface->rgba[i]);
        surface->rgba[i + 1] = adjust(surface->rgba[i + 1]);
        surface->rgba[i + 2] = adjust(surface->rgba[i + 2]);
    }
}

void blit(const std::shared_ptr<::cw::lib::Surface>& dst,
          const std::shared_ptr<::cw::lib::Surface>& src,
          ::cw::lib::Integer left, ::cw::lib::Integer top) {
    if (!dst || !src) return;
    for (::cw::lib::Integer sy = 0; sy < src->size.height; ++sy) {
        const auto dy = top + sy;
        if (dy < 0 || dy >= dst->size.height) continue;
        for (::cw::lib::Integer sx = 0; sx < src->size.width; ++sx) {
            const auto dx = left + sx;
            if (dx < 0 || dx >= dst->size.width) continue;
            const auto si = static_cast<std::size_t>((sy * src->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((dy * dst->size.width + dx) * 4);
            if (si + 3 >= src->rgba.size() || di + 3 >= dst->rgba.size()) continue;
            const auto source_alpha = static_cast<unsigned>(src->rgba[si + 3]);
            const auto inverse = 255u - source_alpha;
            const auto destination_alpha = static_cast<unsigned>(dst->rgba[di + 3]);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto value = static_cast<unsigned>(src->rgba[si + channel]) * source_alpha +
                                   static_cast<unsigned>(dst->rgba[di + channel]) * inverse;
                dst->rgba[di + channel] = static_cast<std::uint8_t>((value + 127u) / 255u);
            }
            dst->rgba[di + 3] = static_cast<std::uint8_t>(std::min(255u, source_alpha +
                (destination_alpha * inverse + 127u) / 255u));
        }
    }
}

void fill_rect(const std::shared_ptr<::cw::lib::Surface>& surface, const ::cw::lib::Rect& rect,
               std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (!surface) return;
    const auto left = std::max<::cw::lib::Integer>(0, rect.x);
    const auto top = std::max<::cw::lib::Integer>(0, rect.y);
    const auto right = std::min(surface->size.width, rect.x + rect.width);
    const auto bottom = std::min(surface->size.height, rect.y + rect.height);
    for (auto y = top; y < bottom; ++y) {
        for (auto x = left; x < right; ++x) {
            const auto i = static_cast<std::size_t>((y * surface->size.width + x) * 4);
            if (i + 3 >= surface->rgba.size()) continue;
            surface->rgba[i] = r;
            surface->rgba[i + 1] = g;
            surface->rgba[i + 2] = b;
            surface->rgba[i + 3] = a;
        }
    }
}

void blend_rgba_sub_alpha(const std::shared_ptr<::cw::lib::Surface>& image, std::uint8_t amount) {
    if (!image) return;
    ::cw::lib::simd_image::subtract_alpha(
        image->rgba.data(), image->rgba.size() / 4U, amount);
}
}

namespace cw::sprite::touchbutton {
namespace {
TouchRuntimeContext* g_runtime{};
PointableTile* g_pointed_tile{};

std::shared_ptr<PointableTile> alias_pointable(PointableTile* sprite) {
    if (!sprite) return {};
    return std::shared_ptr<PointableTile>(sprite, [](PointableTile*) {});
}

std::shared_ptr<::cw::sprite::base::SelectableSprite> alias_selectable(::cw::sprite::base::SelectableSprite* sprite) {
    if (!sprite) return {};
    return std::shared_ptr<::cw::sprite::base::SelectableSprite>(sprite, [](::cw::sprite::base::SelectableSprite*) {});
}

::cw::lib::Real master_volume() {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_vol_master;
    if (g_runtime && g_runtime->master_volume) return g_runtime->master_volume();
    return 1.0;
}

void set_master_volume(::cw::lib::Real volume) {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        ::cw::g_cwpy->m_setting->m_vol_master = volume;
        ::cw::g_cwpy->set_mastervolume(static_cast<::cw::lib::Integer>(volume * 100.0));
    }
    if (g_runtime && g_runtime->set_master_volume) g_runtime->set_master_volume(volume);
}

bool shiftup_enabled() {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_shiftup_touchbutton;
    return true;
}

void hide_touch_buttons() {
    if (g_runtime && g_runtime->hide_touchbuttons) {
        g_runtime->hide_touchbuttons();
        return;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
        ::cw::g_cwpy->m_statusbar->hide_touchbuttons(true);
}

void refresh_tiles() {
    if (g_runtime && g_runtime->update_tiles) {
        g_runtime->update_tiles();
        return;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_statusbar)
        ::cw::g_cwpy->m_statusbar->update_tiles();
}

bool lock_menu_cards(::cw::sprite::base::SelectableSprite* sprite) {
    auto* sprite_context = ::cw::sprite::base::runtime_context();
    if (sprite_context && sprite_context->lock_menu_cards)
        return sprite_context->lock_menu_cards(sprite);
    if (::cw::g_cwpy) return ::cw::g_cwpy->is_lockmenucards(alias_selectable(sprite));
    return false;
}
}
void set_runtime_context(TouchRuntimeContext* context){g_runtime=context;}
void TouchButton::initialize(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width)
{
    SelectableSprite::initialize({});
    m_selectable_on_event = true;
    m_is_statusctrl = true;
    m_status = "normal";
    m_frame = 0;
    m_icon = arg_icon.value_or(nullptr);
    m_name = std::move(arg_name);
    m_desc = std::move(arg_desc);
    m_hotkey = std::move(arg_hotkey);
    m_func = std::move(arg_func);
    m_is_enabled = std::move(arg_is_enabled);
    m_width = arg_width;
    m_shift_top = scr(0);
    m__shift_start_top.reset();
    update_scale();
}

void TouchButton::update_scale()
{
    if (m_name.empty()) throw std::invalid_argument("TouchButton requires a name");
    const auto title = m_hotkey.empty() ? m_name : m_name + "(" + m_hotkey + ")";
    const auto title_image = render_text(title, "sbardesctitle", font_bold("sbardesctitle"), {255, 255, 255, 255});
    const auto desc_h = font_height("sbardesc");
    const auto title_h = title_image ? title_image->size.height : font_height("sbardesctitle");
    const auto lines = ::cw::lib::encoding::splitlines(m_desc);
    const auto spx = scr(8);
    const auto spy = scr(2);
    ::cw::lib::Integer width = std::max<::cw::lib::Integer>(scr(1), m_width);
    ::cw::lib::Integer height = spy * 2 + scr(3) + desc_h;
    width = std::max(width, (title_image ? title_image->size.width : text_width(title, "sbardesctitle")) + spx * 2);
    for (const auto& line : lines) {
        const auto line_image = render_text(line, "sbardesc", font_bold("sbardesc"), {255, 255, 255, 255});
        const auto line_h = line_image ? line_image->size.height : desc_h;
        width = std::max(width, (line_image ? line_image->size.width : text_width(line, "sbardesc")) + spx * 2);
        height += line_h;
    }
    m_image = make_surface(width, height);
    m_rect = {0, 0, width, height};
    ::cw::lib::Integer y = spy;
    if (title_image) blit(m_image, title_image, spx, y);
    y += title_h + scr(1);
    fill_rect(m_image, {spx, y, std::max<I>(1, width - spx * 2), std::max<I>(1, scr(1))}, 255, 255, 255, 255);
    y += scr(2);
    for (const auto& line : lines) {
        if (const auto image = render_text(line, "sbardesc", font_bold("sbardesc"), {255, 255, 255, 255})) blit(m_image, image, spx, y);
        y += desc_h;
    }
    m__unselectedimage = m_image;
    m__selectedimage = clone(m_image);
    tint_rgb(m__selectedimage, 128);
    m__disabledimage = clone(m_image);
    tint_rgb(m__disabledimage, -64);
    // Py と同様、ここでは通常・無効画像を再構築する。 is_selection() はキ
    // ーボード位置を変更し得るため、選択の更新は update_image() だけで行う。
    if (!enabled(m_is_enabled)) m_image = m__disabledimage;
}

std::shared_ptr<::cw::lib::Surface> TouchButton::get_selectedimage()
{
    return m__selectedimage;
}

std::shared_ptr<::cw::lib::Surface> TouchButton::get_unselectedimage()
{
    return m__unselectedimage;
}

void TouchButton::update_image()
{
    // Py と同様、有効状態を先に確認する。is_selection() はキーボード位置を消す場合がある。
    if (!enabled(m_is_enabled)) m_image = m__disabledimage;
    else m_image = is_selection() ? get_selectedimage() : get_unselectedimage();
}

::cw::lib::Integer TouchButton::calc_width(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey)
{
    (void)arg_icon;
    const auto title = arg_hotkey.empty() ? arg_name : arg_name + "(" + arg_hotkey + ")";
    const auto spx = scr(8);
    ::cw::lib::Integer width = text_width(title, "sbardesctitle") + spx * 2;
    for (const auto& line : ::cw::lib::encoding::splitlines(arg_desc))
        width = std::max(width, text_width(line, "sbardesc") + spx * 2);
    return width + scr(2);
}

void TouchButton::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (m_status != "shiftup") m__shift_start_top.reset();
    SelectableSprite::update(arg_args, arg_kwargs);
}

void TouchButton::update_selection()
{
    SelectableSprite::update_selection();
    const auto before = m_image;
    const auto* context=::cw::sprite::base::runtime_context();
    const bool selected=::cw::g_cwpy
        ? (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection &&
           (*::cw::g_cwpy->m_selection).get()==this)
        : (context && context->selection==this);
    select_touch_image(*this,selected);
    if (before != m_image) {
        m_dirty = 1;
        invalidate(m_rect);
    }
}

void TouchButton::update_shiftup()
{
    constexpr ::cw::lib::Integer frames = 5;
    if (!m__shift_start_top) m__shift_start_top = m_rect.y;
    if (frames <= m_frame || !shiftup_enabled()) {
        m_status = m_old_status;
        m_frame = 0;
        m_rect.y = m_shift_top;
        m__shift_start_top.reset();
        return;
    }
    const auto delta = m_shift_top - *m__shift_start_top;
    m_rect.y = *m__shift_start_top + floor_div(delta * m_frame, frames);
}

void TouchButton::lclick_event()
{
    if(!enabled(m_is_enabled))return;hide_touch_buttons();if(m_func)m_func({},{});
}

void TouchButton::rclick_event()
{
    hide_touch_buttons();
}

void PointableTile::initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width)
{
    TouchButton::initialize(arg_icon,std::move(arg_name),std::move(arg_desc),std::move(arg_hotkey),std::move(arg_func),std::move(arg_is_enabled),arg_width);
}

void PointableTile::update_selection()
{
    if (m_status != "normal") return;
    if (lock_menu_cards(this)) return;
    const auto selected = is_selection();
    if (selected == m_is_pointed) return;

    m_is_pointed = selected;
    auto* sprite_context = ::cw::sprite::base::runtime_context();
    if (m_is_pointed) {
        m_image = get_selectedimage();
        g_pointed_tile = this;
        if (::cw::g_cwpy) ::cw::g_cwpy->m_pointed_tile = alias_pointable(this);
        if (sprite_context) sprite_context->pointed_tile = true;
        const auto index = ::cw::g_cwpy ? ::cw::g_cwpy->m_index
            : (sprite_context ? sprite_context->index : I{-1});
        if (index == -1) {
            if (::cw::g_cwpy) ::cw::g_cwpy->clear_selection();
            else if (sprite_context) sprite_context->selection = nullptr;
        }
    } else {
        m_image = get_unselectedimage();
        // Py と同様、pointed_tile がこのタイルの場合だけ消す。 ネイティ
        // ブのグループ処理はコンテキストを保持するため、その複製値にも同じ判定を使う。
        const bool owns_pointed_tile = ::cw::g_cwpy
            ? (::cw::g_cwpy->m_pointed_tile && *::cw::g_cwpy->m_pointed_tile &&
               (*::cw::g_cwpy->m_pointed_tile).get() == this)
            : g_pointed_tile == this;
        if (g_pointed_tile == this) g_pointed_tile = nullptr;
        if (owns_pointed_tile) {
            if (::cw::g_cwpy) ::cw::g_cwpy->m_pointed_tile.reset();
            if (sprite_context) sprite_context->pointed_tile = false;
        }
    }
    if (!enabled(m_is_enabled)) m_image = m__disabledimage;
    m_dirty = 1;
    invalidate(m_rect);
}

bool PointableTile::is_selection()
{
    auto* context = ::cw::sprite::base::runtime_context();
    ::cw::lib::Point point{-1, -1};
    if (::cw::g_cwpy) point = {integer_value(std::get<0>(::cw::g_cwpy->m_mousepos)),
                               integer_value(std::get<1>(::cw::g_cwpy->m_mousepos))};
    else if (context) point = context->mouse_pos;
    return point.x >= 0 && point.y >= 0 && point.x >= m_rect.x &&
           point.x < m_rect.x + m_rect.width && point.y >= m_rect.y &&
           point.y < m_rect.y + m_rect.height;
}

void VolumeTile::initialize(::cw::lib::Integer arg_width)
{
    ::cw::lib::Callback noop=[](const auto&,const auto&){return ::cw::lib::Value{};};
    ::cw::lib::Callback yes=[](const auto&,const auto&){return ::cw::lib::Value(true);};
    TouchButton::initialize(volume_icon(),"","","",noop,yes,arg_width);
}

void VolumeTile::update_scale()
{
    const auto font_h = font_height("sbarprogress");
    const auto spx = scr(10);
    const auto spx2 = scr(5);
    const auto spy = scr(5);
    const auto fspy = scr(1);
    const auto icon_h = m_icon ? m_icon->size.height : 0;
    const auto content_height = std::max<I>(scr(16), std::max<I>(font_h + fspy * 2, icon_h));
    const auto h = content_height + spy * 2;
    const auto image_height = h + scr(2);
    const auto width = std::max<::cw::lib::Integer>(1, m_width);
    m_image = make_surface(width, image_height);
    m_rect = {0, 0, width, image_height};
    if (m_icon) blit(m_image, m_icon, spx, floor_div(h - m_icon->size.height, 2));
    const auto padx = spx + (m_icon ? m_icon->size.width : 0) + spx2;
    const auto padw = std::max<::cw::lib::Integer>(1, width - padx - spx);
    m_padrect = {padx, spy, padw, content_height};
    m__unselectedimage = clone(m_image);
    m__selectedimage = clone(m_image);
    m__disabledimage = m__unselectedimage;
    m_volume_percent = static_cast<::cw::lib::Integer>(master_volume() * 100.0);
    update_image();
}

void VolumeTile::update_image()
{
    const auto volume = master_volume();
    m_volume_percent = static_cast<::cw::lib::Integer>(volume * 100.0);
    // 前の非選択画像から始め、繰り返し更新しても
    // 選択色や古いバーのピクセルを蓄積させない。
    const auto background = m__unselectedimage ? m__unselectedimage : m_image;
    m__unselectedimage = clone(background);
    m__selectedimage = clone(background);
    const auto filled = static_cast<::cw::lib::Integer>(m_padrect.width * volume);
    const ::cw::lib::Rect bar = m_padrect;
    fill_rect(m__unselectedimage, bar, 0, 0, 0, 232);
    fill_rect(m__selectedimage, bar, 16, 16, 16, 232);
    fill_rect(m__unselectedimage, {bar.x, bar.y, filled, bar.height}, 0, 128, 128, 232);
    fill_rect(m__selectedimage, {bar.x, bar.y, filled, bar.height}, 64, 192, 192, 232);
    const auto percentage = std::to_string(m_volume_percent) + "%";
    const auto white = render_text(percentage, "sbarprogress", font_bold("sbarprogress"), {255, 255, 255, 255});
    auto shadow = render_text(percentage, "sbarprogress", font_bold("sbarprogress"), {0, 0, 0, 255});
    blend_rgba_sub_alpha(shadow, 96);
    const auto text_width_px = white ? white->size.width : 0;
    const auto text_height_px = white ? white->size.height : 0;
    const I tx = bar.x + floor_div(bar.width - text_width_px, 2);
    const I ty = bar.y + floor_div(bar.height - text_height_px, 2);
    for (I ox = -1; ox <= 1; ++ox) for (I oy = -1; oy <= 1; ++oy)
        if ((ox != 0 || oy != 0) && shadow) { blit(m__unselectedimage, shadow, tx + ox, ty + oy); blit(m__selectedimage, shadow, tx + ox, ty + oy); }
    if (white) { blit(m__unselectedimage, white, tx, ty); blit(m__selectedimage, white, tx, ty); }
    m__disabledimage = m__unselectedimage;
    m_image = is_selection() ? m__selectedimage : m__unselectedimage;
}

void VolumeTile::update_selection()
{
    TouchButton::update_selection();
    bool motion = false;
    bool selected = false;
    if (::cw::g_cwpy) {
        motion = ::cw::g_cwpy->m_mousemotion;
        selected = ::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection &&
            (*::cw::g_cwpy->m_selection).get() == this;
    } else if (auto* context = ::cw::sprite::base::runtime_context()) {
        motion = context->mouse_motion;
        selected = context->selection == this;
    }
    if (!motion || !selected) return;
    // Py と同様、現在の mousein[0] を確認する。 タッチメニュ
    // ーを開いた時の押下状態を使い続け、解除後も音量を変えないようにする。
    const auto primary_down = ::cw::g_cwpy && std::get<0>(::cw::g_cwpy->m_mousein);
    if (primary_down) _moved();
}

void VolumeTile::ldown_event()
{
    _moved();
}

void VolumeTile::_moved()
{
    auto* context = ::cw::sprite::base::runtime_context();
    if (m_padrect.width <= 0) return;
    I mouse_x = -1;
    if (::cw::g_cwpy) mouse_x = integer_value(std::get<0>(::cw::g_cwpy->m_mousepos));
    else if (context) mouse_x = context->mouse_pos.x;
    if (mouse_x < 0) return;
    auto x = mouse_x - m_rect.x - m_padrect.x;
    // 元の評価順を維持する。 x/width にまとめると、round(volume,
    // 2) が使う境界で二進丸めが変わる。
    auto volume = 1.0 - static_cast<double>(m_padrect.width - x) /
        static_cast<double>(m_padrect.width);
    volume = std::clamp(volume, 0.0, 1.0);
    volume = python_round_2(volume);
    const auto old_percent = static_cast<::cw::lib::Integer>(master_volume() * 100.0);
    const auto new_percent = static_cast<::cw::lib::Integer>(volume * 100.0);
    if (old_percent == new_percent) return;
    set_master_volume(volume);
    m_volume_percent = new_percent;
    update_image();
    invalidate(m_rect);
}

void VolumeTile::lclick_event()
{
    // Python版のpassと同じく、音量は押下・ドラッグ時だけ更新する。
    return;
}

void SimplePointableTile::initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::String arg_name, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width)
{
    PointableTile::initialize(std::move(arg_icon),std::move(arg_name),"","",std::move(arg_func),std::move(arg_is_enabled),arg_width);
}

void SimplePointableTile::update_scale()
{
    const auto height = _calc_singlelinetileheight();
    const auto gap = scr(2);
    const auto icon_width = m_icon ? m_icon->size.width + gap : 0;
    const auto title_image = render_text(m_name, "sbardesctitle", font_bold("sbardesctitle"), {255, 255, 255, 255});
    const auto content_width = icon_width + (title_image ? title_image->size.width : text_width(m_name, "sbardesctitle"));
    const auto width = std::max<::cw::lib::Integer>(1, m_width);
    m_image = make_surface(width, height);
    m_rect = {0, 0, width, height};
    const auto left = floor_div(width - content_width, 2);
    if (m_icon) blit(m_image, m_icon, left, floor_div(height - m_icon->size.height, 2));
    if (title_image) blit(m_image, title_image, left + icon_width, floor_div(height - title_image->size.height, 2));
    m__unselectedimage = m_image;
    m__selectedimage = clone(m_image);
    tint_rgb(m__selectedimage, 128);
    m__disabledimage = clone(m_image);
    tint_rgb(m__disabledimage, -64);
    if (!enabled(m_is_enabled)) m_image = m__disabledimage;
}

void SwitchSpriteTile::initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::Integer arg_move_count, ::cw::lib::Integer arg_width)
{
    ::cw::lib::Callback noop=[](const auto&,const auto&){return ::cw::lib::Value{};};::cw::lib::Callback selectable=[](const auto&,const auto&){return ::cw::lib::Value(can_selectsprite());};PointableTile::initialize(std::move(arg_icon),"","","",noop,selectable,arg_width);m_move_count=arg_move_count;
}

void SwitchSpriteTile::update_scale()
{
    if (!m_icon) throw std::invalid_argument("switch tile icon");
    const auto height = _calc_singlelinetileheight();
    const auto width = std::max<::cw::lib::Integer>(1, m_width);
    m_image = make_surface(width, height);
    m_rect = {0, 0, width, height};
    blit(m_image, m_icon, floor_div(width - m_icon->size.width, 2),
         floor_div(height - m_icon->size.height, 2));
    m__unselectedimage = m_image;
    m__selectedimage = clone(m_image);
    tint_rgb(m__selectedimage, 128);
    m__disabledimage = clone(m_image);
    tint_rgb(m__disabledimage, -64);
    if (!enabled(m_is_enabled)) m_image = m__disabledimage;
}

void SwitchSpriteTile::lclick_event()
{
    if (!enabled(m_is_enabled)) return;
    if (g_runtime && g_runtime->move_selection) {
        g_runtime->move_selection(m_move_count);
    } else if (::cw::g_cwpy) {
        const auto handler = (::cw::g_cwpy->m_interrupt_eventhandler && *::cw::g_cwpy->m_interrupt_eventhandler)
            ? *::cw::g_cwpy->m_interrupt_eventhandler : ::cw::g_cwpy->m_eventhandler;
        if (handler) handler->dirkey_event(m_move_count, 0, true, true, false);
    }
    m_is_pointed = false;
    update_selection();
    refresh_tiles();
}

::cw::lib::Integer _calc_singlelinetileheight()
{
    const auto spy = scr(4);
    const auto separator = scr(3);
    const auto line_height = font_height("sbardesc");
    return spy * 2 + separator + line_height + line_height;
}

bool can_selectsprite()
{
    if (::cw::g_cwpy) {
        if (::cw::g_cwpy->is_showingbacklog()) return false;
        if (::cw::g_cwpy->is_showingmessage()) return true;
        return !::cw::g_cwpy->is_runningevent();
    }
    return g_runtime && g_runtime->can_select && g_runtime->can_select();
}

void main()
{
    // モジュール単体実行時に行う初期化はPython版にも存在しない。
    return;
}

}
