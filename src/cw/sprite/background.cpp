#include "cw/sprite/background.hpp"

#include "app/debug_control_server.hpp"
#include "cw/animatedimage.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/effectbooster.hpp"
#include "cw/image.hpp"
#include "cw/header.hpp"
#include "cw/imageretouch.hpp"
#include "cw/setting.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/sprite/message.hpp"
#include "cw/sprite/transition.hpp"
#include "cw/animation.hpp"
#include "cw/event.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::sprite::background {
namespace {
using CellData = ::cw::sprite::background::BackgroundCellData;
using I = ::cw::lib::Integer;
using S = ::cw::lib::String;
using V = ::cw::lib::Value;
using Entry = std::tuple<I, std::optional<V>>;
using Blit = std::tuple<I, V, S, I>;

I value_int(const V& value, I fallback);

std::filesystem::path native_path(const S& value)
{
    return ::cw::lib::encoding::path_from_utf8(value);
}

// Python の _BlitData は画像本体とセル位置/フラグを同じ tuple に保持する。
// compat::Value は tuple を直接表現しないため、画像と型付きセルを一つの
// Object に束ねて、foreground と背景合成の両方で位置情報を失わないようにする。
struct BlitPayload final : ::cw::lib::Object {
    std::shared_ptr<::cw::lib::Surface> image{};
    std::shared_ptr<CellData> cell{};
};

struct RectValue final : ::cw::lib::Object {
    ::cw::lib::Rect rect{};
};

std::shared_ptr<BlitPayload> blit_payload(const V& value)
{
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); p && *p)
        return std::dynamic_pointer_cast<BlitPayload>(*p);
    return {};
}

V make_blit_value(const std::shared_ptr<::cw::lib::Surface>& image,
                 const std::shared_ptr<CellData>& cell)
{
    auto payload = std::make_shared<BlitPayload>();
    payload->image = image;
    payload->cell = cell;
    return V(std::static_pointer_cast<::cw::lib::Object>(payload));
}

std::shared_ptr<::cw::lib::Surface> surface_value(const V& value)
{
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); p && *p)
        return std::dynamic_pointer_cast<::cw::lib::Surface>(*p);
    return {};
}

std::shared_ptr<::cw::lib::Surface> make_surface(I width, I height, ::cw::lib::Color color);
std::shared_ptr<::cw::lib::Surface> resize_surface(const std::shared_ptr<::cw::lib::Surface>& source,
                                                   I width, I height);
std::shared_ptr<::cw::lib::Surface> render_text_cell(const std::shared_ptr<CellData>& cell);

bool has_visible_pixels(const std::shared_ptr<::cw::lib::Surface>& image)
{
    if (!image) return false;
    for (std::size_t i=3; i<image->rgba.size(); i+=4)
        if (image->rgba[i]) return true;
    return false;
}

bool is_integer_scale(const std::shared_ptr<::cw::lib::Surface>& image,
                      I width, I height)
{
    if (!image || image->size.width <= 0 || image->size.height <= 0) return false;
    return width % image->size.width == 0 && height % image->size.height == 0;
}

std::shared_ptr<::cw::lib::Surface> resize_background_surface(
    const std::shared_ptr<::cw::lib::Surface>& image, I width, I height, const S& smoothing)
{
    if (!image) return {};
    bool smooth = false;
    if (smoothing == "True" || smoothing == "true" || smoothing == "1") smooth = true;
    else if (smoothing == "Default" && ::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        smooth = ::cw::g_cwpy->m_setting->m_smoothscale_bg;
    if (smooth && image->size.height > 1 && !is_integer_scale(image, width, height))
        return ::cw::image::smoothscale(image, {width, height}, true, false);
    return resize_surface(image, width, height);
}

std::shared_ptr<::cw::lib::Surface> scale_loaded_surface_for_screen(
    const std::shared_ptr<::cw::lib::Surface>& image)
{
    if (!image) return {};
    const auto screen_scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    const auto source_scale = std::isfinite(image->scr_scale) && image->scr_scale > 0.0
        ? image->scr_scale : 1.0;
    const auto factor = screen_scale / source_scale;
    if (std::abs(factor - 1.0) < 1e-9) return image;
    const auto width = static_cast<I>(static_cast<double>(image->size.width) * factor);
    const auto height = static_cast<I>(static_cast<double>(image->size.height) * factor);
    if (width <= 0 || height <= 0) return image;
    // Python の cw.s(Surface) と同様、
    // 整数倍率は最近傍補間、小数の画面倍率だけ平滑化を使う。
    if (std::abs(factor - std::round(factor)) < 1e-9)
        return resize_surface(image, width, height);
    return ::cw::image::smoothscale(image, {width, height}, true, false);
}

void fill_surface_rect(const std::shared_ptr<::cw::lib::Surface>& image, I x, I y, I width, I height,
                       ::cw::lib::Color color)
{
    if (!image || width<=0 || height<=0) return;
    const I left=std::clamp<I>(x,0,image->size.width), top=std::clamp<I>(y,0,image->size.height);
    const I right=std::clamp<I>(x+width,0,image->size.width), bottom=std::clamp<I>(y+height,0,image->size.height);
    for (I py=top; py<bottom; ++py) for (I px=left; px<right; ++px) {
        const auto offset=static_cast<std::size_t>((py*image->size.width+px)*4);
        if (offset+3>=image->rgba.size()) continue;
        image->rgba[offset]=color.r; image->rgba[offset+1]=color.g;
        image->rgba[offset+2]=color.b; image->rgba[offset+3]=color.a;
    }
}

std::shared_ptr<::cw::lib::Surface> resource_surface(const V& table_value, const S& key)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&table_value.storage());
    if (!object || !*object) return {};
    if (const auto direct=std::dynamic_pointer_cast<::cw::lib::Surface>(*object)) return direct;
    const auto table=std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object);
    if (!table) return {};
    // 任意のキーがない場合は get_item を呼ばない。 ResourceTable
    // の nokey コールバックは、必須リソースの欠落を意図的に通知する。
    if (!table->m_entries.contains(key)) return {};
    try {
        const auto value=table->get_item(V(key));
        const auto* surface_object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        return surface_object && *surface_object ? std::dynamic_pointer_cast<::cw::lib::Surface>(*surface_object) : nullptr;
    } catch (const std::exception&) {
        // LIFEGUAGE2 や一部の代替スキン素材は
        // 、Python のリソース表では任意である。
        // 欠落時もダイアログ全体の構築を中断せず、呼び出
        // し側の旧素材・ソフトウェア描画への代替を許す。
        return {};
    }
}

std::shared_ptr<::cw::lib::Surface> active_pc_card_surface(const std::shared_ptr<CellData>& cell)
{
    if (!cell || cell->pcnumber<=0 || !::cw::g_cwpy) return {};
    const auto index=static_cast<std::size_t>(cell->pcnumber-1);
    if (index>=::cw::g_cwpy->m_pcards.size() || !::cw::g_cwpy->m_pcards[index]) return {};
    const auto card=::cw::g_cwpy->m_pcards[index];
    try {
        const auto card_image=std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(card->get_cardimg());
        const auto image=card_image ? card_image->get_image() : nullptr;
        return has_visible_pixels(image) ? resize_surface(image,cell->size.width,cell->size.height) : nullptr;
    } catch (...) {
        return {};
    }
}

void fill_surface(const std::shared_ptr<::cw::lib::Surface>& target, const ::cw::lib::Color& color)
{
    if (!target) return;
    if (target->rgba.size() < static_cast<std::size_t>(std::max<I>(0, target->size.width) *
                                                        std::max<I>(0, target->size.height) * 4))
        target->rgba.resize(static_cast<std::size_t>(std::max<I>(0, target->size.width) *
                                                       std::max<I>(0, target->size.height) * 4));
    ::cw::lib::simd_image::fill_rgba(target->rgba.data(), target->rgba.size() / 4U,
                                     color.r, color.g, color.b, color.a);
}

void blit_surface(const std::shared_ptr<::cw::lib::Surface>& target,
                  const std::shared_ptr<::cw::lib::Surface>& source, I left, I top)
{
    if (!target || !source || target->size.width <= 0 || target->size.height <= 0 ||
        source->size.width <= 0 || source->size.height <= 0) return;
    const auto source_size = static_cast<std::size_t>(source->size.width * source->size.height * 4);
    if (source->rgba.size() < source_size) return;
    if (target->rgba.size() < static_cast<std::size_t>(target->size.width * target->size.height * 4))
        target->rgba.resize(static_cast<std::size_t>(target->size.width * target->size.height * 4));
    const I source_x = std::max<I>(0, -left);
    const I source_y = std::max<I>(0, -top);
    const I width = std::min<I>(source->size.width - source_x,
                                target->size.width - std::max<I>(0, left));
    const I height = std::min<I>(source->size.height - source_y,
                                 target->size.height - std::max<I>(0, top));
    if (width <= 0 || height <= 0) return;
    const auto source_offset = static_cast<std::size_t>(source_y * source->size.width + source_x) * 4U;
    const auto target_offset = static_cast<std::size_t>(
        std::max<I>(0, top) * target->size.width + std::max<I>(0, left)) * 4U;
    ::cw::lib::simd_image::alpha_blend_rgba(
        source->rgba.data() + source_offset, static_cast<std::size_t>(source->size.width) * 4U,
        target->rgba.data() + target_offset, static_cast<std::size_t>(target->size.width) * 4U,
        static_cast<std::size_t>(width), static_cast<std::size_t>(height), source->surface_alpha);
}

void blit_surface_region(const std::shared_ptr<::cw::lib::Surface>& target,
                         const std::shared_ptr<::cw::lib::Surface>& source,
                         I left, I top, std::optional<::cw::lib::Rect> clip,
                         const S& blend = "Normal")
{
    if (!target || !source || target->size.width <= 0 || target->size.height <= 0 ||
        source->size.width <= 0 || source->size.height <= 0) return;
    const ::cw::lib::Rect bounds{0, 0, target->size.width, target->size.height};
    const I draw_x = clip ? std::clamp<I>(clip->x, 0, target->size.width) : 0;
    const I draw_y = clip ? std::clamp<I>(clip->y, 0, target->size.height) : 0;
    const auto draw = clip ? ::cw::lib::Rect{draw_x, draw_y,
                                                std::min<I>(std::max<I>(0, target->size.width - draw_x), clip->width),
                                                std::min<I>(std::max<I>(0, target->size.height - draw_y), clip->height)} : bounds;
    if (draw.width <= 0 || draw.height <= 0) return;
    if (target->rgba.size() < static_cast<std::size_t>(target->size.width * target->size.height * 4))
        target->rgba.resize(static_cast<std::size_t>(target->size.width * target->size.height * 4));
    if (source->rgba.size() < static_cast<std::size_t>(source->size.width * source->size.height * 4)) return;
    // ピクセルループへ入る前に、出力先の更新矩形を入
    // 力座標へ変換する。 入力全ピクセルを走査してル
    // ープ内で除外すると、96×132 のカードだ
    // けの更新でも、 1264×840 の宿前景で
    // は約百万回の反復が必要になるため、先に絞る。
    const I source_x1 = std::max<I>(0, draw.x - left);
    const I source_y1 = std::max<I>(0, draw.y - top);
    const I source_x2 = std::min<I>(source->size.width, draw.x + draw.width - left);
    const I source_y2 = std::min<I>(source->size.height, draw.y + draw.height - top);
    if (source_x1 >= source_x2 || source_y1 >= source_y2) return;
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto blit_started = std::chrono::steady_clock::now();
#endif
    enum class BlendMode { Normal, Add, Subtract, Multiply };
    const BlendMode blend_mode = blend == "Add" ? BlendMode::Add
        : blend == "Subtract" ? BlendMode::Subtract
        : blend == "Multiply" ? BlendMode::Multiply
        : BlendMode::Normal;
    const auto wrap = [](unsigned value) {
        return static_cast<std::uint8_t>(std::min<unsigned>(255, (value + 128U) / 256U));
    };
    const auto source_stride = static_cast<std::size_t>(source->size.width) * 4U;
    const auto target_stride = static_cast<std::size_t>(target->size.width) * 4U;
    const auto row_pixels = static_cast<std::size_t>(source_x2 - source_x1);
    if (blend_mode == BlendMode::Normal) {
        const auto source_offset = static_cast<std::size_t>(source_y1) * source_stride +
            static_cast<std::size_t>(source_x1) * 4U;
        const auto target_offset = static_cast<std::size_t>(top + source_y1) * target_stride +
            static_cast<std::size_t>(left + source_x1) * 4U;
        ::cw::lib::simd_image::alpha_blend_rgba(
            source->rgba.data() + source_offset, source_stride,
            target->rgba.data() + target_offset, target_stride,
            row_pixels, static_cast<std::size_t>(source_y2 - source_y1), source->surface_alpha);
    } else for (I sy = source_y1; sy < source_y2; ++sy) {
        const I dy = top + sy;
        const auto* source_pixel = source->rgba.data() +
            static_cast<std::size_t>(sy) * source_stride + static_cast<std::size_t>(source_x1) * 4U;
        auto* target_pixel = target->rgba.data() +
            static_cast<std::size_t>(dy) * target_stride +
            static_cast<std::size_t>(left + source_x1) * 4U;
        for (std::size_t x = 0; x < row_pixels; ++x, source_pixel += 4, target_pixel += 4) {
            const unsigned alpha = source_pixel[3];
            switch (blend_mode) {
            case BlendMode::Add:
                for (int c = 0; c < 3; ++c) {
                    const unsigned dest = target_pixel[c], src = source_pixel[c];
                    target_pixel[c] = alpha == 255
                        ? static_cast<std::uint8_t>(std::min<unsigned>(255, dest + src))
                        : wrap(dest * (255U - alpha) + (dest + src) * alpha);
                }
                break;
            case BlendMode::Subtract:
                for (int c = 0; c < 3; ++c) {
                    const unsigned dest = target_pixel[c], src = source_pixel[c];
                    if (alpha == 255) {
                        target_pixel[c] = static_cast<std::uint8_t>(dest > src ? dest - src : 0);
                    } else {
                        const unsigned faded_dest = wrap(dest * (255U - alpha));
                        const unsigned faded_src = wrap(src * alpha);
                        const unsigned subtracted = dest > faded_src ? dest - faded_src : 0;
                        target_pixel[c] = static_cast<std::uint8_t>(std::max(faded_dest, subtracted));
                    }
                }
                break;
            case BlendMode::Multiply:
                for (int c = 0; c < 3; ++c) {
                    const unsigned multiplier = alpha == 255 ? source_pixel[c]
                        : wrap(source_pixel[c] * alpha + (255U - alpha) * 255U);
                    target_pixel[c] = wrap(target_pixel[c] * multiplier);
                }
                break;
            case BlendMode::Normal: break;
            }
        }
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "render.bgcell_blit", std::chrono::steady_clock::now() - blit_started,
        "source=" + std::to_string(source->size.width) + "x" + std::to_string(source->size.height) +
        " dest=" + std::to_string(left) + "," + std::to_string(top) +
        " clip=" + (clip ? (std::to_string(clip->x) + "," + std::to_string(clip->y) + "," +
                             std::to_string(clip->width) + "x" + std::to_string(clip->height)) : "full") +
        " draw=" + std::to_string(draw.x) + "," + std::to_string(draw.y) + "," +
        std::to_string(draw.width) + "x" + std::to_string(draw.height) +
        " scan=" + std::to_string(source_x1) + "," + std::to_string(source_y1) + "," +
        std::to_string(source_x2 - source_x1) + "x" + std::to_string(source_y2 - source_y1) +
        " blend=" + blend);
#endif
}

std::shared_ptr<::cw::lib::Surface> negative_surface(const std::shared_ptr<::cw::lib::Surface>& source)
{
    if (!source) return {};
    auto result = std::make_shared<::cw::lib::Surface>(*source);
    if (result->rgba.size() >= static_cast<std::size_t>(std::max<I>(0, result->size.width) *
                                                         std::max<I>(0, result->size.height) * 4))
        ::cw::lib::simd_image::invert_rgb(result->rgba.data(), result->rgba.size() / 4U);
    return result;
}

std::shared_ptr<::cw::lib::Surface> placeholder_cell_surface(const std::shared_ptr<CellData>& cell)
{
    if (!cell) return {};
    if (cell->type == g_BG_COLOR) {
        const auto& c = cell->color1;
        return make_surface(cell->size.width, cell->size.height,
            {static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(c), 0, 255)),
             static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(c), 0, 255)),
             static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(c), 0, 255)),
             static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(c), 0, 255))});
    }
    if (cell->type == g_BG_TEXT) return render_text_cell(cell);
    if (cell->type == g_BG_IMAGE && !cell->path.empty()) {
        try {
            S path = cell->path;
            if (cell->inusecard) {
                path = ::cw::util::join_yadodir(path);
                path = ::cw::util::get_materialpathfromskin(path, ::cw::g_M_IMG, true);
            } else {
                path = ::cw::util::get_materialpath(path, ::cw::g_M_IMG, {}, false, true);
            }
            if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc)
                path = ::cw::g_cwpy->m_rsrc->get_filepath(path);
            auto image=::cw::util::load_image(path,cell->mask,{0,0},std::nullopt,false,false,
                                              cell->scaledimage,false,std::nullopt,false);
            if (image) return resize_surface(image,cell->size.width,cell->size.height);
        } catch (...) {
            // 壊れた個別resourceでも、下のportable可視fallbackを描画する。
        }
    }
    if (cell->type == g_BG_PC) {
        if (auto image=active_pc_card_surface(cell)) return image;
    }

    // Resource欠落時も透明セルにはせず、カード枠と人物シルエットを保持する。
    auto image=make_surface(cell->size.width,cell->size.height,{38,42,48,230});
    const I border=std::max<I>(1,std::min(cell->size.width,cell->size.height)/30);
    fill_surface_rect(image,0,0,cell->size.width,border,{190,176,130,255});
    fill_surface_rect(image,0,cell->size.height-border,cell->size.width,border,{190,176,130,255});
    fill_surface_rect(image,0,0,border,cell->size.height,{190,176,130,255});
    fill_surface_rect(image,cell->size.width-border,0,border,cell->size.height,{190,176,130,255});
    if (cell->type==g_BG_PC) {
        const I head=std::max<I>(2,std::min(cell->size.width,cell->size.height)/5);
        fill_surface_rect(image,(cell->size.width-head)/2,cell->size.height/5,head,head,{218,202,164,255});
        fill_surface_rect(image,cell->size.width/4,cell->size.height/2,cell->size.width/2,
                          std::max<I>(1,cell->size.height/3),{92,112,145,255});
    }
    return image;
}

bool is_cell_type(::cw::lib::Integer type)
{
    return type == g_BG_IMAGE || type == g_BG_TEXT ||
           type == g_BG_COLOR || type == g_BG_PC;
}

std::shared_ptr<CellData> cell_data(const ::cw::lib::Value& value)
{
    if (value.is_none()) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<CellData>(*object);
}

::cw::lib::String normalized_image_path(::cw::lib::String path)
{
    path = ::cw::util::normalize_path(std::move(path));
    std::transform(path.begin(), path.end(), path.begin(), [](char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    const auto basename_begin = slash == ::cw::lib::String::npos ? 0 : slash + 1;
    if (dot != ::cw::lib::String::npos && dot > basename_begin) path.resize(dot);
    return path;
}

bool value_equal(const ::cw::lib::Value& lhs, const ::cw::lib::Value& rhs)
{
    if (lhs.storage().index() != rhs.storage().index()) return false;
    return std::visit([](const auto& left, const auto& right) -> bool {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) return false;
        else if constexpr (std::is_same_v<Left, ::cw::lib::ObjectPtr>) return left.get() == right.get();
        else return left == right;
    }, lhs.storage(), rhs.storage());
}

S cell_key(const CellData& d);

bool same_cell_data(::cw::lib::Integer bgtype, const CellData& lhs, const CellData& rhs)
{
    // Python のセルのタプル項目を直接比較する。区切り文字で結合し
    // たキーは文字・フォント名などに同じ文字があると衝突し、キャッシュし
    // たキーも後の変更を反映しない。 Python が無視する flag/cellname
    // や、別のセル型の描画専用項目は比較に含めない。
    if (std::tie(lhs.pos.x, lhs.pos.y, lhs.size.width, lhs.size.height,
                 lhs.visible, lhs.layer, lhs.battle) !=
        std::tie(rhs.pos.x, rhs.pos.y, rhs.size.width, rhs.size.height,
                 rhs.visible, rhs.layer, rhs.battle)) return false;
    if (bgtype == g_BG_IMAGE)
        return normalized_image_path(lhs.path) == normalized_image_path(rhs.path) &&
               std::tie(lhs.inusecard, lhs.scaledimage, lhs.mask, lhs.smoothing) ==
               std::tie(rhs.inusecard, rhs.scaledimage, rhs.mask, rhs.smoothing);
    if (bgtype == g_BG_COLOR)
        return std::tie(lhs.blend, lhs.color1, lhs.gradient, lhs.color2) ==
               std::tie(rhs.blend, rhs.color1, rhs.gradient, rhs.color2);
    if (bgtype == g_BG_PC)
        return std::tie(lhs.pcnumber, lhs.expand, lhs.smoothing) ==
               std::tie(rhs.pcnumber, rhs.expand, rhs.smoothing);
    if (bgtype == g_BG_TEXT) {
        const auto rgb = [](const auto& color) {
            return std::make_tuple(std::get<0>(color), std::get<1>(color), std::get<2>(color));
        };
        if (std::tie(lhs.text, lhs.face, lhs.text_size, lhs.bold, lhs.italic,
                     lhs.underline, lhs.strike, lhs.vertical, lhs.antialias,
                     lhs.border_type, lhs.border_width, lhs.loaded, lhs.update_type,
                     lhs.names_present, lhs.scenarioinfo_present) !=
            std::tie(rhs.text, rhs.face, rhs.text_size, rhs.bold, rhs.italic,
                     rhs.underline, rhs.strike, rhs.vertical, rhs.antialias,
                     rhs.border_type, rhs.border_width, rhs.loaded, rhs.update_type,
                     rhs.names_present, rhs.scenarioinfo_present) ||
            rgb(lhs.color1) != rgb(rhs.color1) || rgb(lhs.color2) != rgb(rhs.color2)) return false;
        // NameListItem には Python の値の等価比較がないため、一覧内で
        // は同一性で比較する。 同じ項目の名前・データがその場で変わった場合も同じとする。
        if (lhs.names_present && lhs.names != rhs.names) return false;
        return !lhs.scenarioinfo_present ||
               std::tie(lhs.scenario, lhs.author) == std::tie(rhs.scenario, rhs.author);
    }
    return false;
}

std::shared_ptr<::cw::data::SystemData> scenario_data()
{
    if (!::cw::g_cwpy) return {};
    const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    return p && *p ? std::dynamic_pointer_cast<::cw::data::SystemData>(*p) : nullptr;
}

bool flag_value(const S& flag)
{
    const auto data = scenario_data();
    return data ? data->get_flagvalue(flag) : true;
}

I area_width()
{
    const I n = std::get<0>(::cw::g_SIZE_AREA);
    return n > 0 ? n : 632;
}
I area_height()
{
    const I n = std::get<1>(::cw::g_SIZE_AREA);
    return n > 0 ? n : 420;
}

I scaled_length(I value)
{
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    return std::max<I>(0, static_cast<I>(static_cast<double>(value) * scale));
}

I scaled_coordinate(I value)
{
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    // Python の cw.s() と同様、int(value * UP_SCR)
    // を使う。 符号付き座標は維持し、小数値はゼロ方向へ切り捨てる。
    return static_cast<I>(static_cast<double>(value) * scale);
}

I canvas_width() { return scaled_length(area_width()); }
I canvas_height() { return scaled_length(area_height()); }

std::shared_ptr<::cw::lib::Surface> make_surface(I width, I height, ::cw::lib::Color color = {})
{
    auto surface = std::make_shared<::cw::lib::Surface>();
    surface->size = {std::max<I>(0, width), std::max<I>(0, height)};
    if (surface->size.width > 0 && surface->size.height > 0) {
        surface->rgba.resize(static_cast<std::size_t>(surface->size.width * surface->size.height * 4));
        for (std::size_t i = 0; i + 3 < surface->rgba.size(); i += 4) {
            surface->rgba[i] = color.r; surface->rgba[i + 1] = color.g;
            surface->rgba[i + 2] = color.b; surface->rgba[i + 3] = color.a;
        }
    }
    return surface;
}

std::shared_ptr<::cw::lib::Surface> resize_surface(const std::shared_ptr<::cw::lib::Surface>& source, I width, I height)
{
    if (!source) return {};
    if (source->size.width == width && source->size.height == height) return source;
    return ::cw::image::scale_nearest(source, {width, height});
}

S element_attr(const std::shared_ptr<::cw::data::CWPyElement>& e, const S& path, const S& attr, const S& fallback)
{
    return e ? e->getattr(path, attr, fallback) : fallback;
}
I element_int(const std::shared_ptr<::cw::data::CWPyElement>& e, const S& path, const S& attr, std::optional<I> fallback)
{
    if (!e) throw std::invalid_argument("background cell XML is missing");
    // 既定値付きの空属性は、Py の getint(path, integer_default) に対応する。
    const auto py_attr = attr.empty() && fallback ? V(*fallback) : V(attr);
    return e->getint(path, py_attr, fallback);
}
bool element_bool(const std::shared_ptr<::cw::data::CWPyElement>& e, const S& path, const S& attr, bool fallback)
{
    return e ? e->getbool(path, V(attr), fallback) : fallback;
}
S element_text(const std::shared_ptr<::cw::data::CWPyElement>& e, const S& path, const S& fallback)
{
    return e ? e->gettext(path, fallback) : fallback;
}
bool value_bool(const V& value, bool fallback = false)
{
    if (const auto* p = std::get_if<bool>(&value.storage())) return *p;
    if (const auto* p = std::get_if<I>(&value.storage())) return *p != 0;
    if (const auto* p = std::get_if<S>(&value.storage())) return *p == "True" || *p == "true" || *p == "1";
    return fallback;
}
bool background_doanime_bool(const V& value)
{
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); p && *p) return true;
    return value_bool(value, false);
}
std::shared_ptr<::cw::effectbooster::AnimationCounter> animation_counter_value(const V& value)
{
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); p && *p)
        return std::dynamic_pointer_cast<::cw::effectbooster::AnimationCounter>(*p);
    return {};
}
struct ReloadBeforeloadResult final : ::cw::lib::Object {
    bool animated{};
    std::vector<Blit> blitlist;
    bool update{};
    bool forcedraw{};
};
void remove_background_proxies_at_layer(const std::shared_ptr<::cw::lib::Object>& object,
                                        I layer);
void remove_jpy_temporal_sprites()
{
    if (!::cw::g_cwpy) return;
    std::shared_ptr<::cw::lib::Object> topgrp;
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_topgrp.storage()); p && *p)
        topgrp = *p;
    remove_background_proxies_at_layer(topgrp, ::cw::g_LAYER_JPY_TEMPORAL);
}
void clear_background_forgrounds(BackGround& background)
{
    std::shared_ptr<::cw::lib::Object> cardgrp;
    if (::cw::g_cwpy) {
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
            cardgrp = *p;
    }
    for (const auto& old : background.m_foregrounds)
        if (old && background.m_ui_backend) background.m_ui_backend->remove_sprite(cardgrp, old);
    background.m_foregrounds.clear();
    background.m_foregroundlist.clear();
}
I value_int(const V& value, I fallback = 0)
{
    if (const auto* p = std::get_if<I>(&value.storage())) return *p;
    if (const auto* p = std::get_if<::cw::lib::Real>(&value.storage())) {
        const auto limit = std::ldexp(1.0, std::numeric_limits<I>::digits);
        if (std::isfinite(*p) && *p >= -limit && *p < limit) return static_cast<I>(*p);
    }
    if (const auto* p = std::get_if<S>(&value.storage()))
        if (const auto parsed = ::cw::lib::parse_python_integer(*p)) return *parsed;
    return fallback;
}

I layer_value_from_tlayer(const std::tuple<V, V, V, I>& tlayer)
{
    return ::cw::layer_val({value_int(std::get<0>(tlayer)), value_int(std::get<1>(tlayer)),
                            value_int(std::get<2>(tlayer)), std::get<3>(tlayer)});
}

I layer_value_from_tlayer(const std::tuple<I, I, I, I>& tlayer)
{
    return ::cw::layer_val(tlayer);
}
S cell_key(const CellData& d)
{
    // Python の PCCell のタプルにはパスがない。 C++ の
    // PCCell が保持する画像パスは描画専用なので、比較へ含めない。
    const auto path = d.type == g_BG_IMAGE ? normalized_image_path(d.path)
                    : d.type == g_BG_PC ? S{} : d.path;
    std::ostringstream out;
    out << d.type << '|' << path << '|' << d.pos.x << ',' << d.pos.y << '|' << d.size.width << ',' << d.size.height
        << '|' << d.visible << '|' << d.layer << '|' << d.battle
        << '|' << d.inusecard << '|' << d.scaledimage << '|' << d.mask << '|' << d.smoothing << '|'
        << d.blend << '|' << d.gradient << '|'
        << std::get<0>(d.color1) << ',' << std::get<1>(d.color1) << ',' << std::get<2>(d.color1) << ',' << std::get<3>(d.color1) << '|'
        << std::get<0>(d.color2) << ',' << std::get<1>(d.color2) << ',' << std::get<2>(d.color2) << ',' << std::get<3>(d.color2) << '|'
        << d.text << '|' << d.face << '|' << d.text_size << '|'
        << d.border_width << '|' << d.border_type << '|' << d.bold << d.italic << d.underline << d.strike
        << d.vertical << d.antialias << d.loaded << '|' << d.update_type << '|' << d.pcnumber << '|' << d.expand
        << '|' << d.scenario << '|' << d.author << '|' << d.names_key;
    return out.str();
}

std::shared_ptr<CellData> data_value(const V& value)
{
    return cell_data(value);
}

V make_value(const std::shared_ptr<CellData>& data)
{
    return V(std::static_pointer_cast<::cw::lib::Object>(data));
}

void set_base_data(const std::shared_ptr<CellData>& d,
                   std::tuple<std::tuple<I, I>, std::tuple<I, I>, S, bool, I, S> base,
                   I type)
{
    const auto& size = std::get<0>(base); const auto& pos = std::get<1>(base);
    d->type = type; d->size = {std::get<0>(size), std::get<1>(size)};
    d->pos = {std::get<0>(pos), std::get<1>(pos)}; d->flag = std::get<2>(base);
    d->visible = std::get<3>(base); d->layer = std::get<4>(base); d->cellname = std::get<5>(base);
}

I layer_value(I layer)
{
    return std::clamp<I>(layer, ::cw::g_LAYER_BACKGROUND, ::cw::g_LAYER_MAX);
}

bool collides_area(const ::cw::lib::Point& pos, const ::cw::lib::Size& size)
{
    return size.width != 0 && size.height != 0 && pos.x < area_width() && pos.y < area_height() &&
           pos.x + size.width > 0 && pos.y + size.height > 0;
}

S surface_cache_key(const S& path, const ::cw::lib::Size& size, bool mask, const S& smoothing, I mtime)
{
    // Python のタプルキーに相当する ValueMap の文字列表現では
    // 、 ファイル名・設定値に「|」が含まれても入力元の項目を曖昧にしない。
    return "BgSurface|" + std::to_string(path.size()) + ":" + path + "|" + std::to_string(size.width) + "," + std::to_string(size.height) + "|" +
           (mask ? "1" : "0") + "|" + smoothing + "|" + std::to_string(mtime);
}

struct BackgroundProxyRecord {
    std::shared_ptr<::cw::lib::Sprite> proxy{};
    std::weak_ptr<::cw::lib::Object> group{};
    ::cw::sprite::base::CWPySprite* owner{};
    // 通常の背景スプライトはモデルの集合が所有する。 JPY の
    // 一時スプライトはグループ項目がある間だけ存在するため、レ
    // イヤーを除くまでプロキシーとともにその所有者を保持する。
    std::shared_ptr<::cw::sprite::base::CWPySprite> retained_owner{};
    I layer{};
};

auto& background_proxies()
{
    // スプライトは Engine 全体の終了中に破棄される場合が
    // ある。 この小さな登録表をプロセス終了まで保ち、明示的な破
    // 棄フックが翻訳単位をまたぐ静的破棄と競合しないようにする。
    static auto* records = new std::unordered_map<
        const ::cw::sprite::base::CWPySprite*, BackgroundProxyRecord>();
    return *records;
}

struct BackgroundProxyLookup {
    ::cw::sprite::base::CWPySprite* owner{};
    I layer{};
};

auto& background_proxy_lookup()
{
    // pygame のグループは Sprite 自身を保持するが、ネ
    // イティブのグループは軽量なプロキシーを保持する。 所有者をキ
    // ーにした登録表とともに逆引き表を持ち、Group.update()
    // が各項目ごとに全背景セルを線形探索しないようにする。
    static auto* lookup = new std::unordered_map<
        const ::cw::lib::Sprite*, BackgroundProxyLookup>();
    return *lookup;
}

std::shared_ptr<::cw::lib::SpriteGroup> sprite_group(const std::shared_ptr<::cw::lib::Object>& object)
{
    return object ? std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(object) : nullptr;
}

void add_background_proxy(const std::shared_ptr<::cw::lib::Object>& object,
                          const std::shared_ptr<::cw::sprite::base::CWPySprite>& owner,
                          I layer)
{
    const auto group = sprite_group(object);
    if (!group || !owner) return;
    auto& record = background_proxies()[owner.get()];
    const auto previous_layer = record.layer;
    if (!record.proxy) record.proxy = std::make_shared<::cw::lib::Sprite>();
    ::cw::sprite::base::sync_sprite_proxy(*record.proxy, owner->m_image,
                                         owner->m_rect, owner->m_status != "hidden");
    record.group = group;
    record.owner = owner.get();
    if (dynamic_cast<Jpy1TemporalSprite*>(owner.get())) record.retained_owner = owner;
    record.layer = layer;
    const bool newly_registered =
        std::find(group->sprites.begin(), group->sprites.end(), record.proxy) == group->sprites.end();
    background_proxy_lookup()[record.proxy.get()] = {owner.get(), layer};
    group->layers[record.proxy.get()] = layer;
    if (newly_registered)
        ::cw::sprite::base::insert_sprite_group_by_layer(group, record.proxy);
    else if (previous_layer != layer)
        ::cw::sprite::base::reposition_sprite_group_by_layer(group, record.proxy);
}

void remove_background_proxy(const std::shared_ptr<::cw::lib::Object>& object,
                             const std::shared_ptr<::cw::sprite::base::CWPySprite>& owner)
{
    if (!owner) return;
    auto& records = background_proxies();
    const auto it = records.find(owner.get());
    if (it == records.end()) return;
    const auto group = sprite_group(object ? object : it->second.group.lock());
    auto retained_owner = std::move(it->second.retained_owner);
    if (group && it->second.proxy) {
        ::cw::sprite::base::remove_sprite_from_group(group, it->second.proxy);
    }
    if (it->second.proxy) background_proxy_lookup().erase(it->second.proxy.get());
    records.erase(it);
    // 記録を消してから所有者を解放する。 CWPySprite のデストラク
    // ターが同じ後処理フックを呼ぶため、プロキシーが既にない状態を見せる。
    retained_owner.reset();
}

void remove_background_proxies_for_owner_impl(
    const ::cw::sprite::base::CWPySprite* owner) noexcept
{
    if (!owner) return;
    auto& records = background_proxies();
    const auto it = records.find(owner);
    if (it == records.end()) return;
    auto retained_owner = std::move(it->second.retained_owner);
    if (const auto group = sprite_group(it->second.group.lock());
        group && it->second.proxy) {
        ::cw::sprite::base::remove_sprite_from_group(group, it->second.proxy);
    }
    if (it->second.proxy) background_proxy_lookup().erase(it->second.proxy.get());
    records.erase(it);
    retained_owner.reset();
}

void remove_background_proxies_at_layer(const std::shared_ptr<::cw::lib::Object>& object,
                                        I layer)
{
    const auto group = sprite_group(object);
    if (!group) return;
    auto& records = background_proxies();
    std::vector<std::shared_ptr<::cw::sprite::base::CWPySprite>> retained;
    for (auto it = records.begin(); it != records.end();) {
        const auto registered_group = sprite_group(it->second.group.lock());
        if (registered_group != group || it->second.layer != layer) {
            ++it;
            continue;
        }
        if (it->second.proxy) {
            ::cw::sprite::base::remove_sprite_from_group(group, it->second.proxy);
            background_proxy_lookup().erase(it->second.proxy.get());
        }
        if (it->second.retained_owner)
            retained.push_back(std::move(it->second.retained_owner));
        it = records.erase(it);
    }
    // 対応する登録項目をすべて取り除いてから、保持中の所有者を破棄す
    // る。 半分だけ消したレイヤーに対する再帰的な後処理を避ける。
    retained.clear();
}

void sync_background_proxies()
{
    auto& records = background_proxies();
    for (auto it = records.begin(); it != records.end();) {
        auto* owner = it->second.owner;
        if (!owner || !it->second.proxy) {
            // Python の Sprite と同様、C++ の基底
            // デストラクターがグループから明示的に取り除く。 不完全
            // なテスト用記録に備え、この防御的な後処理も維持する。
            if (const auto group = sprite_group(it->second.group.lock());
                group && it->second.proxy) {
                ::cw::sprite::base::remove_sprite_from_group(group, it->second.proxy);
            }
            if (it->second.proxy) background_proxy_lookup().erase(it->second.proxy.get());
            it = records.erase(it);
            continue;
        }
        ::cw::sprite::base::sync_sprite_proxy(*it->second.proxy, owner->m_image,
                                             owner->m_rect, owner->m_status != "hidden");
        ++it;
    }
}

std::shared_ptr<::cw::lib::Surface> render_text_cell(const std::shared_ptr<CellData>& cell)
{
    if (!cell) return {};
    auto image = make_surface(scaled_length(cell->size.width), scaled_length(cell->size.height), {0, 0, 0, 0});
    if (!image) return {};
    const auto colour = std::make_tuple(std::clamp<I>(std::get<0>(cell->color1), 0, 255),
                                        std::clamp<I>(std::get<1>(cell->color1), 0, 255),
                                        std::clamp<I>(std::get<2>(cell->color1), 0, 255));
    if (cell->border_type == "Inline") {
        // Python と同様、create_type2textcell へ
        // cw.s(tsize) と cw.s(size) を渡す。 返る描画
        // 面は透明であり、描画済みの背景へ文字とその内側の縁だけを合成する。
        return ::cw::image::create_type2textcell(
            cell->text, cell->face, scaled_length(cell->text_size), colour, cell->bold, cell->italic,
            cell->underline, cell->strike, cell->vertical, cell->antialias,
            {scaled_length(cell->size.width), scaled_length(cell->size.height)},
            {std::clamp<I>(std::get<0>(cell->color2), 0, 255),
             std::clamp<I>(std::get<1>(cell->color2), 0, 255),
             std::clamp<I>(std::get<2>(cell->color2), 0, 255)}, cell->border_width);
    }
    std::optional<std::tuple<I, I, I>> border;
    if (cell->border_type == "Outline")
        border = std::make_tuple(std::clamp<I>(std::get<0>(cell->color2), 0, 255),
                                 std::clamp<I>(std::get<1>(cell->color2), 0, 255),
                                 std::clamp<I>(std::get<2>(cell->color2), 0, 255));
    auto rect = std::make_shared<::cw::RectValue>(); rect->rect = {0, 0, image->size.width, image->size.height};
    ::cw::image::draw_textcell(image, rect, cell->text, cell->face, scaled_length(cell->text_size),
                               colour, cell->bold, cell->italic, cell->underline,
                               cell->strike, cell->vertical, cell->antialias, border);
    return image;
}

std::shared_ptr<::cw::lib::Surface> render_pc_cell(BackGround& background,
                                                    const std::shared_ptr<CellData>& cell)
{
    if (!cell) return {};
    const auto cached = background.put_pccache(cell->pcnumber - 1);
    const auto& paths = std::get<0>(cached);
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> infos;
    infos.reserve(paths.size());
    for (const auto& [unused_path, info] : paths) {
        (void)unused_path;
        if (info) infos.push_back(info);
    }
    // Python と同様、pick_up_basename_image() の既定の skip_xN=true
    // を使う。 明示的な portrait.x2.* は追加レイヤーではなく倍率別画像として扱い
    // 、 下の基本画像が load_image(scaledimage=true) を通して選ぶ。
    const auto filtered_infos = ::cw::util::pick_up_basename_image(std::move(infos), true);
    std::set<const ::cw::image::ImageInfo*> filtered;
    for (const auto& info : filtered_infos) if (info) filtered.insert(info.get());
    I base_width = scaled_length(cell->size.width), base_height = scaled_length(cell->size.height);
    if (cell->expand) {
        base_width = scaled_length(std::get<0>(::cw::g_SIZE_CARDIMAGE));
        base_height = scaled_length(std::get<1>(::cw::g_SIZE_CARDIMAGE));
    }
    auto result = make_surface(base_width, base_height, {0, 0, 0, 0});
    for (const auto& [path, info] : paths) {
        if (!info || path.empty() || !filtered.contains(info.get())) continue;
        std::shared_ptr<::cw::lib::Surface> image;
        try {
            image = ::cw::util::load_image(path, true, {0, 0}, std::nullopt, false, false,
                                           std::get<1>(cached), false, std::nullopt,
                                           background.m_use_excache);
        } catch (...) {
            continue;
        }
        if (!image) continue;
        // 最新 Reboot ではなく voicevox 版 Python
        // に合わせ、x2/x4 のピクセル寸法を論理寸法へ戻してか
        // ら配置する。 物理寸法で中央配置すると、画像が x2 で半
        // 分、x4 で 1.5 倍分だけ左上へずれ、右下だけが残る。
        const double logical_scale = std::isfinite(image->scr_scale) && image->scr_scale > 0.0
            ? image->scr_scale : 1.0;
        const auto logical_width = static_cast<I>(std::floor(image->size.width / logical_scale));
        const auto logical_height = static_cast<I>(std::floor(image->size.height / logical_scale));
        const auto base = info->calc_basecardposition({logical_width, logical_height}, true,
                                                         S("LargeCard"), S("NotCard"));
        // load_image は選択ファイルの論理倍率を scr_scale
        // に保持する。 合成前に Python の cw.s(bmp)
        // の規則を適用し、二倍表示では @2x を一対一で使い、 @2x
        // がなければ基本画像を拡大して半分のサイズにならないようにする。
        image = scale_loaded_surface_for_screen(image);
        blit_surface(result, image, scaled_coordinate(base.x), scaled_coordinate(base.y));
    }
    if (cell->expand)
        result = resize_background_surface(result, scaled_length(cell->size.width),
                                           scaled_length(cell->size.height), cell->smoothing);
    return result;
}
}

void remove_background_proxies_for_owner(
    const ::cw::sprite::base::CWPySprite* owner) noexcept
{
    remove_background_proxies_for_owner_impl(owner);
}

bool is_jpy_surface_cache_key(const S& key)
{
    // surface_cache_key と同じ長さ付き表現で入力元の
    // 項目を読み、 ファイル名内の区切り文字を構造として解釈しない。
    constexpr std::string_view prefix = "BgSurface|";
    if (!key.starts_with(prefix)) return false;
    const auto length_end = key.find(':', prefix.size());
    if (length_end == S::npos || length_end == prefix.size()) return false;
    std::size_t path_size{};
    const auto length = std::from_chars(key.data() + prefix.size(), key.data() + length_end, path_size);
    if (length.ec != std::errc{} || length.ptr != key.data() + length_end) return false;
    const auto path_start = length_end + 1;
    if (path_size >= key.size() - path_start || key[path_start + path_size] != '|') return false;
    S extension = std::get<1>(::cw::util::splitext(key.substr(path_start, path_size)));
    for (char& ch : extension) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return extension == ".jpy1";
}

::cw::sprite::base::CWPySprite* background_layer_sprite_owner(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_sprite) return nullptr;
    const auto it = background_proxy_lookup().find(arg_sprite.get());
    return it == background_proxy_lookup().end() ? nullptr : it->second.owner;
}

std::optional<::cw::lib::Integer> background_layer_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_sprite) return std::nullopt;
    const auto it = background_proxy_lookup().find(arg_sprite.get());
    return it == background_proxy_lookup().end()
        ? std::nullopt
        : std::optional<::cw::lib::Integer>(it->second.layer);
}

class CommonBackgroundUiBackend final : public BackgroundUiBackend {
    std::function<void(const S&, bool)> m_play_audio;

public:
    explicit CommonBackgroundUiBackend(std::function<void(const S&, bool)> arg_play_audio = {})
        : m_play_audio(std::move(arg_play_audio)) {}

    void bind(BackGround& arg_background) override { redraw(arg_background); }

    void invalidate(BackGround& arg_background, std::optional<::cw::lib::Rect> arg_rect) override
    {
        arg_background.m_ui_dirty = true;
        if (arg_rect) {
            arg_background.m_ui_dirty_rects.push_back(*arg_rect);
            // pygameのDirtySpriteと同じく、モデル側のdirty状態だけでなく
            // ゲーム画面の再合成キューへ更新矩形を通知する。
            if (::cw::g_cwpy)
                ::cw::g_cwpy->add_lazydraw_rect(*arg_rect);
        }
    }

    void redraw(BackGround& arg_background) override
    {
        // PythonのBackGround.imageは_load_after()が合成したSurfaceそのものを
        // 保持する。foregroundlistは前景Spriteの再生成用であり、背景セルを
        // 含まないため、ここから背景を再構築するとclear後のTable/Camp移動で
        // 黒Surfaceへ置き換わる。共通backendも合成済みSurfaceをそのまま公開する。
        auto canvas = static_cast<::cw::sprite::base::CWPySprite&>(arg_background).m_image;
        if (!canvas) canvas = make_surface(canvas_width(), canvas_height(), {0, 0, 0, 255});
        arg_background.m_ui_surface = canvas;
        static_cast<::cw::sprite::base::CWPySprite&>(arg_background).m_image = canvas;
        sync_background_proxies();
        arg_background.m_ui_dirty = false;
        arg_background.m_ui_dirty_rects.clear();
    }

    void add_sprite(std::shared_ptr<::cw::lib::Object> arg_group,
                    std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite,
                    I arg_layer) override
    {
        add_background_proxy(arg_group, arg_sprite, arg_layer);
    }

    void remove_sprite(std::shared_ptr<::cw::lib::Object> arg_group,
                       std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite) override
    {
        remove_background_proxy(arg_group, arg_sprite);
    }

    bool decode_effect_image(S arg_path, bool arg_mask,
                             std::shared_ptr<::cw::lib::Surface>& arg_image) override
    {
        const auto ext = native_path(arg_path).extension().generic_string();
        S lower = ext;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        try {
            if (lower == ".jptx") {
                auto decoder = std::make_shared<::cw::effectbooster::JptxImage>();
                decoder->initialize(arg_path, arg_mask);
                arg_image = decoder->get_image();
            } else if (lower == ".jpdc") {
                auto decoder = std::make_shared<::cw::effectbooster::JpdcImage>();
                decoder->initialize(arg_mask, arg_path);
                arg_image = decoder->get_image();
            } else if (lower == ".jpy1") {
                auto decoder = std::make_shared<::cw::effectbooster::JpyImage>();
                decoder->initialize(arg_path, arg_mask, std::nullopt, std::nullopt, std::nullopt);
                arg_image = decoder->get_image();
            } else {
                return false;
            }
        } catch (...) {
            arg_image.reset();
        }
        return static_cast<bool>(arg_image);
    }

    void play_audio(S arg_path, bool arg_loop) override
    {
        if (m_play_audio) m_play_audio(arg_path, arg_loop);
    }
};

std::shared_ptr<BackgroundUiBackend> make_background_ui_backend()
{
#ifdef _WIN32
    const auto play_audio = [](const S& arg_path, bool arg_loop) {
        if (arg_path.empty()) return;
        ::PlaySoundA(arg_path.c_str(), nullptr, SND_ASYNC | (arg_loop ? SND_LOOP : SND_PURGE));
    };
    return std::make_shared<CommonBackgroundUiBackend>(play_audio);
#else
    return std::make_shared<CommonBackgroundUiBackend>();
#endif
}

std::shared_ptr<::cw::sprite::base::CWPySprite> sprite_self(
    ::cw::sprite::base::CWPySprite* sprite)
{
    if (!sprite) return {};
    // 背景の登録表は生の識別情報を追跡し、CWPySprite
    // のデストラクターが解除する。 参照用の別名はアダプター
    // 呼び出し中だけに限り、二つ目の生存期間管理にはしない。
    return {sprite, [](auto*) {}};
}

std::shared_ptr<BackgroundUiBackend> background_ui_backend()
{
    static const auto backend = make_background_ui_backend();
    return backend;
}
void BackGround::initialize()
{
    // Python の __init__ と同様、_init だけを設定し、重いスプライト確保は init() まで遅らせる。
    m__init = false;
    bind_ui_backend();
}

void BackGround::bind_ui_backend()
{
    m_ui_backend = background_ui_backend();
    if (m_ui_backend) m_ui_backend->bind(*this);
}

void BackGround::invalidate_ui(std::optional<::cw::lib::Rect> arg_rect)
{
    m_ui_dirty = true;
    if (arg_rect) m_ui_dirty_rects.push_back(*arg_rect);
    if (m_ui_backend) m_ui_backend->invalidate(*this, arg_rect);
}

void BackGround::redraw_ui()
{
    if (!m_ui_backend) bind_ui_backend();
    if (m_ui_backend && m_ui_dirty) m_ui_backend->redraw(*this);
}

bool BackGround::is_initialized() { return m__init; }

void BackGround::init()
{
    ::cw::sprite::base::CWPySprite::initialize({});
    m__init = true;
    m_bgs.clear(); m__bgs.clear(); m__elements.clear(); m_foregrounds.clear(); m_foregroundlist.clear();
    m__in_playing = false; m__inhrt_index = 0; m__force_noinhrt = false;
    m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::effectbooster::AnimationCounter>()));
    m__ttype = {"None", V("None")};
    m_tlayer = {0, 0, 0, 0};
    ::cw::sprite::base::CWPySprite::m_image = make_surface(canvas_width(), canvas_height());
    ::cw::sprite::base::CWPySprite::m_rect = {0, 0, canvas_width(), canvas_height()};
    if (!m_ui_backend) bind_ui_backend();
    m_ui_surface = ::cw::sprite::base::CWPySprite::m_image;
    m_ui_dirty = true;
    m_curtained = false; m_curtain_all = false; m_reload_jpdcimage = true; m_has_jpdcimage = false;
    m__animated_owner = "Background:" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
}

void BackGround::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args; (void)arg_kwargs;
    if (_update_animated_images())
        reload(false, {"None", V("None")}, false, {}, {}, {}, false, true);
    redraw_ui();
}

std::tuple<S, bool> BackGround::_resolve_imagecell_path(V arg_d)
{
    const auto data = data_value(arg_d);
    if (!data || data->path.empty()) return {S{}, false};
    S path = data->path;
    bool inusecard = data->inusecard;
    {
        if (inusecard) {
            path = ::cw::util::join_yadodir(path);
            path = ::cw::util::get_materialpathfromskin(path, ::cw::g_M_IMG, true);
            // スキン・リソースの代替パスが宿の保存先・一時保存先のどちら
            // にも属さない場合、inusecardをfalseにする。
            if (!::cw::g_cwpy ||
                ((path.rfind(::cw::g_cwpy->m_tempdir, 0) != 0 || ::cw::g_cwpy->m_tempdir.empty()) &&
                 (path.rfind(::cw::g_cwpy->m_yadodir, 0) != 0 || ::cw::g_cwpy->m_yadodir.empty())))
                inusecard = false;
        } else {
            path = ::cw::util::get_materialpath(path, ::cw::g_M_IMG, {}, false, true);
        }
    }if (::cw::g_cwpy && !::cw::g_cwpy->m_in_actionevent && ::cw::g_cwpy->m_rsrc)
        path = ::cw::g_cwpy->m_rsrc->get_filepath(path);
    return {path, inusecard};
}

bool BackGround::_update_animated_images()
{
    bool updated = false;
    for (const auto& [cellkey, path] : _get_animated_paths()) {
        const auto frame = ::cw::animatedimage::get_frame_id(path,
            m_animated_instance_ids.contains(cellkey) ? std::optional<I>(m_animated_instance_ids[cellkey]) : std::nullopt);
        if (frame && m_animated_frame_ids[cellkey] != frame) { m_animated_frame_ids[cellkey] = frame; updated = true; }
    }
    return updated;
}

std::map<std::tuple<S, I>, S> BackGround::_get_animated_paths()
{
    std::map<std::tuple<S, I>, S> paths;
    std::map<S, I> counts;
    for (const auto& [type, value] : m_bgs) {
        if (type != g_BG_IMAGE || !value) continue;
        const auto [path, unused] = _resolve_imagecell_path(*value); (void)unused;
        if (path.empty() || !std::filesystem::is_regular_file(native_path(path))) continue;
        if (::cw::animatedimage::is_supported_path(path)) {
            const I n = counts[path]++;
            paths[{path, n}] = path;
            continue;
        }
        auto ext = native_path(path).extension().generic_string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (ext != ".jpy1") continue;
        const auto cellkey = std::make_tuple(path, counts[path]++);
        if (const auto found = m_effect_animated_paths.find(cellkey);
            found != m_effect_animated_paths.end()) {
            for (const auto& dependency : found->second)
                paths[_get_effect_animated_key(cellkey, dependency)] = dependency;
        }
    }
    return paths;
}

std::tuple<S, I> BackGround::_next_animated_cell_key(S arg_path)
{
    const I n = m__animated_load_counts[arg_path]++;
    return {arg_path, n};
}

S BackGround::_get_animated_owner(std::tuple<S, I> arg_cellkey)
{
    return m__animated_owner + ":" + std::get<0>(arg_cellkey) + ":" + std::to_string(std::get<1>(arg_cellkey));
}

std::tuple<S, I> BackGround::_get_effect_animated_key(std::tuple<S, I> arg_cellkey, const S& arg_path)
{
    // 外側の JPY1 セルと内包する動画を一意に関連付ける。 動画の再生自体は animatedimage
    // に委ね、このキーは所有権とフレーム ID の追跡だけに使う。
    return {std::get<0>(arg_cellkey) + "\x1e" "EffectBooster" "\x1e" + arg_path,
            std::get<1>(arg_cellkey)};
}

void BackGround::_sync_animated_paths()
{
    const auto paths = _get_animated_paths();
    for (const auto& key : m__animated_paths) if (!paths.contains(key)) {
        auto it = m__animated_owners.find(key);
        ::cw::animatedimage::release(it == m__animated_owners.end() ? m__animated_owner : it->second);
        m__animated_owners.erase(key); m_animated_frame_ids.erase(key); m_animated_instance_ids.erase(key); m_animated_surfaces.erase(key);
    }
    for (const auto& [key, path] : paths) if (!m_animated_instance_ids.contains(key)) {
        const S owner = _get_animated_owner(key);
        const auto id = ::cw::animatedimage::acquire(path, owner, true);
        if (id) { m_animated_instance_ids[key] = *id; m__animated_owners[key] = owner; }
    }
    m__animated_paths.clear(); for (const auto& [key, unused] : paths) { (void)unused; m__animated_paths.insert(key); }
}

void BackGround::release_animated_paths()
{
    for (const auto& [unused, owner] : m__animated_owners) { (void)unused; ::cw::animatedimage::release(owner); }
    m__animated_paths.clear(); m__animated_owners.clear(); m_animated_frame_ids.clear();
    m_animated_instance_ids.clear(); m_animated_surfaces.clear(); m__animated_load_counts.clear();
    m_effect_animated_paths.clear();
}

void BackGround::update_scale()
{
    if (m__in_playing && ::cw::g_cwpy) {
        const auto stored_bgs = m__bgs;
        const auto current_doanime = animation_counter_value(m__doanime);
        const auto stored_doanime = current_doanime ? current_doanime->get_reloadcounter() : nullptr;
        const auto stored_elements = m__elements;
        const auto stored_ttype = m__ttype;
        ::cw::g_cwpy->exec_func([this, stored_bgs, stored_doanime, stored_elements, stored_ttype](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            auto canvas = make_surface(canvas_width(), canvas_height(), {0, 0, 0, 255});
            ::cw::sprite::base::CWPySprite::m_image = canvas;
            ::cw::sprite::base::CWPySprite::m_rect = {0, 0, canvas_width(), canvas_height()};
            m_ui_surface = canvas;
            m_bgs = stored_bgs;
            auto cut = std::make_shared<::cw::effectbooster::CutAnimation>();
            cut->initialize();
            _reload(V(std::static_pointer_cast<::cw::lib::Object>(cut)), {"None", V("None")}, false, true, true, true, false, {}, {}, {}, false);
            if (!stored_elements.empty()) {
                V doanime = stored_doanime ? V(std::static_pointer_cast<::cw::lib::Object>(stored_doanime)) : V(true);
                load(stored_elements, std::move(doanime), stored_ttype, true, false, true);
            } else {
                V doanime = stored_doanime ? V(std::static_pointer_cast<::cw::lib::Object>(stored_doanime)) : V(true);
                _reload(std::move(doanime), stored_ttype, true, false, false, true, false, {}, {}, {}, false);
            }
            if (m_curtained) set_curtain(m_curtain_all);
            m__bgs = stored_bgs;
            m__elements = stored_elements;
            m__ttype = stored_ttype;
            return V{};
        });
        return;
    }
    const auto old = m_bgs;
    ::cw::sprite::base::CWPySprite::m_image = make_surface(canvas_width(), canvas_height());
    ::cw::sprite::base::CWPySprite::m_rect = {0, 0, canvas_width(), canvas_height()};
    m_ui_surface = ::cw::sprite::base::CWPySprite::m_image;
    invalidate_ui(::cw::sprite::base::CWPySprite::m_rect);
    m_bgs = old; m__bgs = old;
    auto cut = std::make_shared<::cw::effectbooster::CutAnimation>();
    cut->initialize();
    _reload(V(std::static_pointer_cast<::cw::lib::Object>(cut)), {"None", V("None")}, false, true, true, true, false, {}, {}, {}, false);
    if (m_curtained) set_curtain(m_curtain_all);
}

void BackGround::update_skin(S arg_oldskindir, S arg_newskindir)
{
    // Python と同様、現在のリソースキャッシュは
    // 意図的に変更しない。 スキンリソースを置き換え
    // るかは、次の明示的な背景再読み込みで判断する。
    (void)arg_oldskindir;
    (void)arg_newskindir;
}

void BackGround::set_curtain(bool arg_curtain_all)
{
    clear_curtain();
    m_curtained = true; m_curtain_all = arg_curtain_all;
    m_curtain_layers_shifted = false;
    m__curtains.clear();
    if (!::cw::g_cwpy) {
        // ヘッドレス構築ではgroupが無いため、カーテン状態だけ保持する。
        return;
    }
    std::shared_ptr<BackGround> self;
    { self = shared_from_this(); }std::shared_ptr<::cw::lib::Object> cardgrp;
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
        cardgrp = *p;
    if (arg_curtain_all) {
        const auto layer = std::make_tuple(value_int(::cw::g_LAYER_SPBACKGROUND), ::cw::g_LTYPE_BACKGROUND, I{0}, I{0});
        auto maincurtain = std::make_shared<Curtain>();
        maincurtain->initialize(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(self), cardgrp,
                                std::nullopt, layer, false, true, true);
        m__curtains.push_back(maincurtain);
        ::cw::g_cwpy->m_curtains.push_back(maincurtain);
        if (m_ui_backend) m_ui_backend->add_sprite(cardgrp, maincurtain, layer_value_from_tlayer(maincurtain->m_tlayer));
        for (const auto& pcard : ::cw::g_cwpy->m_pcards) {
            if (!pcard) continue;
            pcard->m_tlayer = {std::get<0>(pcard->m_tlayer) + ::cw::g_LAYER_SP_LAYER,
                               std::get<1>(pcard->m_tlayer), std::get<2>(pcard->m_tlayer), std::get<3>(pcard->m_tlayer)};
            ::cw::sprite::card::refresh_card_layer(*pcard);
        }
        m_curtain_layers_shifted = true;
    } else {
        // カーテンを追加する前に、Python と同じ描画の逆順で実体のグ
        // ループを複製する。 既存のカーテンもそれぞれの対象として含める。
        std::vector<std::shared_ptr<::cw::sprite::base::CWPySprite>> ordered;
        if (const auto group = sprite_group(cardgrp); group && !m_foregrounds.empty()) {
            const auto sprites = group->sprites;
            for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
                const auto owner = background_layer_sprite_owner(*it);
                if (!owner) continue;
                if (dynamic_cast<BgCell*>(owner)) {
                    const auto cell = std::find_if(m_foregrounds.begin(), m_foregrounds.end(),
                        [owner](const auto& value) { return value.get() == owner; });
                    if (cell != m_foregrounds.end()) ordered.push_back(*cell);
                } else if (dynamic_cast<Curtain*>(owner)) {
                    const auto curtain = std::find_if(::cw::g_cwpy->m_curtains.begin(), ::cw::g_cwpy->m_curtains.end(),
                        [owner](const auto& value) { return value.get() == owner; });
                    if (curtain != ::cw::g_cwpy->m_curtains.end()) ordered.push_back(*curtain);
                }
            }
        } else {
            // 画面なしのネイティブモデルにはグループがないため、前景マスクを維持する。
            ordered.assign(m_foregrounds.begin(), m_foregrounds.end());
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
                return layer_value_from_tlayer(lhs->m_tlayer) > layer_value_from_tlayer(rhs->m_tlayer);
            });
        }
        auto cutter = make_surface(canvas_width(), canvas_height(), {0, 0, 0, 0});
        bool has_cutter = false;
        auto accumulate_cutter = [&](const std::shared_ptr<Curtain>& curtain) {
            if (!curtain || !curtain->m_target || !cutter) return;
            const auto mask = curtain->create_mask();
            const auto source = mask ? *mask : nullptr;
            const auto rect = curtain->m_target->m_rect;
            const I source_width = source ? source->size.width : rect.width;
            const I source_height = source ? source->size.height : rect.height;
            for (I y = 0; y < source_height; ++y) {
                const I dy = rect.y + y;
                if (dy < 0 || dy >= cutter->size.height) continue;
                for (I x = 0; x < source_width; ++x) {
                    const I dx = rect.x + x;
                    if (dx < 0 || dx >= cutter->size.width) continue;
                    const auto di = static_cast<std::size_t>((dy * cutter->size.width + dx) * 4);
                    if (di + 3 >= cutter->rgba.size()) continue;
                    std::uint8_t alpha = 255;
                    if (source) {
                        const auto si = static_cast<std::size_t>((y * source->size.width + x) * 4);
                        if (si + 3 >= source->rgba.size()) continue;
                        alpha = source->rgba[si + 3];
                    }
                    cutter->rgba[di + 3] = std::max(cutter->rgba[di + 3], alpha);
                }
            }
            has_cutter = true;
        };
        for (const auto& sprite : ordered) {
            auto curtain = std::dynamic_pointer_cast<Curtain>(sprite);
            const bool existing = static_cast<bool>(curtain);
            if (!existing) {
                const auto bgcell = std::dynamic_pointer_cast<BgCell>(sprite);
                if (!bgcell) continue;
                const auto payload = blit_payload(bgcell->m_d);
                const auto cell = payload ? payload->cell : cell_data(bgcell->m_d);
                if (cell && cell->type == g_BG_COLOR &&
                    (cell->blend == "Add" || cell->blend == "Subtract" || cell->blend == "Multiply")) continue;
                curtain = std::make_shared<Curtain>();
                curtain->initialize(sprite, cardgrp,
                                    std::nullopt, std::nullopt, true, false, false);
            }
            if (!curtain->m_target) continue;
            if (has_cutter || existing) {
                curtain->m_cutter_surface = std::make_shared<::cw::lib::Surface>(*cutter);
                curtain->m_cutter = V(std::static_pointer_cast<::cw::lib::Object>(curtain->m_cutter_surface));
                curtain->m_cutter_pos = {-curtain->m_target->m_rect.x, -curtain->m_target->m_rect.y};
            }
            curtain->update_scale();
            if (!existing) {
                m__curtains.push_back(curtain);
                ::cw::g_cwpy->m_curtains.push_back(curtain);
                if (m_ui_backend) m_ui_backend->add_sprite(cardgrp, curtain, layer_value_from_tlayer(curtain->m_tlayer));
            }
            accumulate_cutter(curtain);
        }
        auto maincurtain = std::make_shared<Curtain>();
        maincurtain->initialize(std::static_pointer_cast<::cw::sprite::base::CWPySprite>(self), cardgrp,
                                std::nullopt, std::nullopt, false, true, true);
        if (has_cutter) {
            maincurtain->m_cutter_surface = cutter;
            maincurtain->m_cutter = V(std::static_pointer_cast<::cw::lib::Object>(cutter));
            maincurtain->m_cutter_pos = {0, 0};
            maincurtain->update_scale();
        }
        m__curtains.push_back(maincurtain);
        ::cw::g_cwpy->m_curtains.push_back(maincurtain);
        if (m_ui_backend) m_ui_backend->add_sprite(cardgrp, maincurtain, layer_value_from_tlayer(maincurtain->m_tlayer));
    }
    invalidate_ui(::cw::sprite::base::CWPySprite::m_rect);
}

void BackGround::clear_curtain()
{
    if (!m_curtained) return;
    m_curtained = false;
    if (m_curtain_all && m_curtain_layers_shifted && ::cw::g_cwpy) {
        for (const auto& pcard : ::cw::g_cwpy->m_pcards) {
            if (!pcard) continue;
            pcard->m_tlayer = {std::get<0>(pcard->m_tlayer) - ::cw::g_LAYER_SP_LAYER,
                               std::get<1>(pcard->m_tlayer), std::get<2>(pcard->m_tlayer), std::get<3>(pcard->m_tlayer)};
            ::cw::sprite::card::refresh_card_layer(*pcard);
        }
    }
    std::shared_ptr<::cw::lib::Object> cardgrp;
    if (::cw::g_cwpy) {
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
            cardgrp = *p;
        ::cw::g_cwpy->m_curtains.erase(
            std::remove_if(::cw::g_cwpy->m_curtains.begin(), ::cw::g_cwpy->m_curtains.end(),
                           [this](const auto& curtain) {
                               return std::find(m__curtains.begin(), m__curtains.end(), curtain) != m__curtains.end();
                           }),
            ::cw::g_cwpy->m_curtains.end());
    }
    for (const auto& curtain : m__curtains)
        if (curtain && m_ui_backend) m_ui_backend->remove_sprite(cardgrp, curtain);
    m_curtain_all = false; m_curtain_layers_shifted = false; m__curtains.clear();
    invalidate_ui(::cw::sprite::base::CWPySprite::m_rect);
}

void BackGround::store_filepath(S arg_path, ::cw::lib::Real arg_mtime)
{
    if (arg_path.empty()) return;
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return;
    const auto ext = native_path(arg_path).extension().generic_string();
    S lower = ext; for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == ".jpy1" || lower == ".jptx" || lower == ".jpdc") return;
    bool sound = false;
    std::apply([&](const auto&... value) {
        sound = ((lower == value) || ...);
    }, ::cw::g_EXTS_SND);
    if (sound) return;
    if (!::cw::g_cwpy->m_in_actionevent && !std::filesystem::is_regular_file(native_path(arg_path))) return;
    const auto scenario = scenario_data();
    if (!scenario) return;
    S temp_root;
    if (const auto* value = std::get_if<S>(&::cw::g_tempdir.storage()); value) temp_root = *value;
    const std::array<S, 2> roots{
        temp_root.empty() ? S{} : ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(temp_root) / "ScenarioLog/TempFile"),
        scenario->m_scedir};
    const auto absolute_path = std::filesystem::absolute(native_path(arg_path)).lexically_normal();
    for (const auto& root_text : roots) {
        if (root_text.empty()) continue;
        std::error_code ec;
        const auto root = std::filesystem::absolute(native_path(root_text)).lexically_normal();
        const auto relative = std::filesystem::relative(absolute_path, root, ec);
        if (ec || relative.empty() || relative == ".." ||
            ::cw::lib::encoding::path_to_utf8(relative).starts_with("../")) continue;
        const auto rel = ::cw::lib::encoding::path_to_utf8(relative);
        S key = rel;
        std::transform(key.begin(), key.end(), key.begin(), [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        scenario->m_background_image_mtime[key] = {rel, arg_mtime};
        m_stored_filepaths[arg_path] = {arg_path, arg_mtime};
        return;
    }
}

bool BackGround::is_modifiedfile()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->is_playingscenario()) return false;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto scenario = scenario_data();
    if (!scenario) return false;
    S temp_root;
    if (const auto* value = std::get_if<S>(&::cw::g_tempdir.storage()); value) temp_root = *value;
    const std::array<S, 2> roots{
        temp_root.empty() ? S{} : ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(temp_root) / "ScenarioLog/TempFile"),
        scenario->m_scedir};
    for (const auto& [unused_key, stored] : scenario->m_background_image_mtime) {
        (void)unused_key;
        const auto& relative = std::get<0>(stored);
        const auto old_mtime = std::get<1>(stored);
        for (const auto& root_text : roots) {
            if (root_text.empty()) continue;
            const auto path = native_path(root_text) / native_path(relative);
            if (!std::filesystem::is_regular_file(path)) continue;
            const auto now = static_cast<::cw::lib::Real>(
                std::filesystem::last_write_time(path).time_since_epoch().count());
            // 記録されたゼロも時刻として扱い、キャッシュ欠落の印にはしない。
            if (now != old_mtime) return true;
            break;
        }
    }
    return false;
}

std::tuple<std::optional<std::shared_ptr<::cw::lib::Surface>>, bool, bool, std::optional<std::tuple<S, ::cw::lib::Real>>> BackGround::load_surface(S arg_path, bool arg_mask, S arg_smoothing, std::tuple<I, I> arg_size, S arg_flag, std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, bool arg_visible, bool arg_nocheckvisible, bool arg_can_loaded_scaledimage, std::optional<std::tuple<S, I>> arg_animated_cellkey)
{
    if ((!arg_nocheckvisible && !flag_value(arg_flag)) || (arg_nocheckvisible && !arg_visible)) return {std::nullopt, false, false, std::nullopt};
    if (arg_path.empty()) return {std::nullopt, false, false, std::nullopt};
    const auto scenario = scenario_data();
    const bool fast_cache = scenario && scenario->fastLoadCacheEnabled();
    I mtime = 0;
    const auto file = native_path(arg_path);
    if (!fast_cache && std::filesystem::is_regular_file(file))
        mtime = static_cast<I>(std::filesystem::last_write_time(file).time_since_epoch().count());
    const auto key = surface_cache_key(arg_path, {std::get<0>(arg_size), std::get<1>(arg_size)}, arg_mask, arg_smoothing, mtime);
    const auto ext = file.extension().generic_string();
    S lower = ext; for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool animated = false;
    bool cachable = true;
    if (lower == ".jpdc") { m_has_jpdcimage = true; m_reload_jpdcimage = false; }
    if (lower != ".jpdc" && ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        if (scenario) {
            if (const auto it = scenario->m_resource_cache.find(key); it != scenario->m_resource_cache.end()) {
                if (const auto cached = surface_value(it->second))
                    return {cached, false, false, std::make_tuple(arg_path, static_cast<::cw::lib::Real>(mtime))};
            }
        } else if (const auto it = m_surface_cache.find(key); it != m_surface_cache.end()) {
            return {it->second, false, false, std::make_tuple(arg_path, static_cast<::cw::lib::Real>(mtime))};
        }
        if (fast_cache) {
            const auto prefix = surface_cache_key(arg_path,
                {std::get<0>(arg_size), std::get<1>(arg_size)}, arg_mask, arg_smoothing, 0);
            const auto separator = prefix.rfind('|');
            const auto prefix_without_mtime = separator == S::npos ? prefix : prefix.substr(0, separator);
            const auto find_cached = [&](const auto& cache) -> std::shared_ptr<::cw::lib::Surface> {
                for (const auto& [cached_key, cached_value] : cache) {
                    const auto cached_separator = cached_key.rfind('|');
                    if (cached_separator != S::npos &&
                        cached_key.substr(0, cached_separator) == prefix_without_mtime) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(cached_value)>,
                                                      std::shared_ptr<::cw::lib::Surface>>)
                            return cached_value;
                        else if (const auto cached = surface_value(cached_value))
                            return cached;
                    }
                }
                return {};
            };
            if (const auto cached = scenario ? find_cached(scenario->m_resource_cache)
                                             : find_cached(m_surface_cache))
                return {cached, false, false,
                        std::make_tuple(arg_path, static_cast<::cw::lib::Real>(mtime))};
        }
    }
    std::shared_ptr<::cw::lib::Surface> image;
    try {
        if (lower == ".jptx") {
            auto decoder = std::make_shared<::cw::effectbooster::JptxImage>();
            decoder->initialize(arg_path, arg_mask);
            image = decoder->get_image();
        } else if (lower == ".jpdc") {
            auto decoder = std::make_shared<::cw::effectbooster::JpdcImage>();
            decoder->initialize(arg_mask, arg_path, std::nullopt, ::cw::effectbooster::jpdc_default_copy_mode,
                                arg_doanime ? std::optional<std::shared_ptr<::cw::effectbooster::AnimationCounter>>(arg_doanime)
                                            : std::nullopt);
            image = decoder->get_image();
            if (::cw::g_cwpy && ::cw::g_cwpy->m_is_processing && image) {
                // シナリオ読み込み中の JPDC は、読み
                // 込み後に再取得する。 準備前に取得した
                // 画像を表示せず、一時画像を透明に保つ。
                image = make_surface(image->size.width, image->size.height, {0, 0, 0, 255});
                image->has_pixel_alpha = false;
                image->has_colorkey = true;
                image->colorkey = {0, 0, 0};
            }
        } else if (lower == ".jpy1") {
            auto decoder = std::make_shared<::cw::effectbooster::JpyImage>();
            ::cw::effectbooster::AnimatedSurfaceLoader animated_loader;
            if (arg_animated_cellkey) {
                const auto cellkey = *arg_animated_cellkey;
                animated_loader = [this, cellkey](const S& dependency) {
                    m_effect_animated_paths[cellkey].insert(dependency);
                    const auto key = _get_effect_animated_key(cellkey, dependency);
                    if (!m_animated_instance_ids.contains(key) ||
                        !::cw::animatedimage::has_instance(dependency, m_animated_instance_ids[key])) {
                        const S owner = _get_animated_owner(key);
                        const auto id = ::cw::animatedimage::acquire(dependency, owner, true);
                        if (id) {
                            m_animated_instance_ids[key] = *id;
                            m__animated_owners[key] = owner;
                        }
                    }
                    const auto id = m_animated_instance_ids.contains(key)
                        ? std::optional<I>(m_animated_instance_ids[key]) : std::nullopt;
                    auto current = ::cw::animatedimage::get_surface(dependency, id);
                    auto image = std::get<0>(current);
                    if (!image && m_animated_surfaces.contains(key)) image = m_animated_surfaces[key];
                    if (image) {
                        m_animated_frame_ids[key] = std::get<1>(current);
                        m_animated_surfaces[key] = std::make_shared<::cw::lib::Surface>(**image);
                    }
                    return std::make_tuple(image, std::get<1>(current));
                };
            }
            decoder->initialize(arg_path, arg_mask, std::nullopt,
                                arg_doanime ? std::optional<std::shared_ptr<::cw::effectbooster::AnimationCounter>>(arg_doanime)
                                            : std::nullopt, std::nullopt, std::move(animated_loader));
            image = decoder->get_image();
            animated = decoder->m_is_animated;
            cachable = decoder->m_is_cacheable;
        } else if (::cw::animatedimage::is_supported_path(arg_path)) {
            const auto id = arg_animated_cellkey && m_animated_instance_ids.contains(*arg_animated_cellkey)
                ? std::optional<I>(m_animated_instance_ids[*arg_animated_cellkey]) : std::nullopt;
            auto current = ::cw::animatedimage::get_surface(arg_path, id);
            image = std::get<0>(current).value_or(nullptr);
            if (!image) {
                const auto cached = arg_animated_cellkey ? m_animated_surfaces.find(*arg_animated_cellkey) : m_animated_surfaces.end();
                if (cached != m_animated_surfaces.end() && cached->second)
                    image = std::make_shared<::cw::lib::Surface>(*cached->second);
                else {
                    image = make_surface(0, 0);
                    image->has_pixel_alpha = false; // Py の Surface((0,0)).convert() に相当する。
                }
            } else if (arg_animated_cellkey) {
                m_animated_frame_ids[*arg_animated_cellkey] = std::get<1>(current);
                m_animated_surfaces[*arg_animated_cellkey] = std::make_shared<::cw::lib::Surface>(*image);
            }
            if (arg_mask && (image->size.width != 0 || image->size.height != 0) && !image->has_colorkey) {
                // ネイティブの convert_maskpos は、Py の既定の (0,0) を空の Value で表す。
                const auto [mx, my] = ::cw::util::convert_maskpos(V{}, image->size.width, image->size.height);
                if (mx < 0 || my < 0 || mx >= image->size.width || my >= image->size.height)
                    throw std::out_of_range("animated background mask position");
                const auto offset = static_cast<std::size_t>((my * image->size.width + mx) * 4);
                if (offset + 2 >= image->rgba.size()) throw std::out_of_range("animated background mask pixel");
                image->colorkey = {image->rgba[offset], image->rgba[offset + 1], image->rgba[offset + 2]};
                image->has_colorkey = true;
            }
            cachable = false;
        } else {
            image = ::cw::util::load_image(arg_path, arg_mask, {0, 0}, std::nullopt, false, true,
                                           arg_can_loaded_scaledimage, false, std::nullopt, m_use_excache);
        }
    } catch (const ::cw::effectbooster::ScreenRescale&) {
        remove_jpy_temporal_sprites();
        m__in_playing = true;
        throw;
    } catch (const ::cw::event::EffectBreakError&) {
        // EffectBooster の待機は、シナリオ終了時に EffectBreakError
        // で直ちに巻き戻る。 画像デコード失敗として扱うと、古い ScenarioData
        // が背景読み込みを続けてしまうため、区別する。
        remove_jpy_temporal_sprites();
        throw;
    } catch (const std::exception& error) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("EffectBooster/background decode failed: path=" + arg_path +
                               " error=" + error.what());
#endif
        image.reset();
    } catch (...) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("EffectBooster/background decode failed: path=" + arg_path +
                               " error=unknown exception");
#endif
        image.reset();
    }
    if (!image &&
        lower != ".jpy1" && lower != ".jpdc" && lower != ".jptx" && m_ui_backend)
        m_ui_backend->decode_effect_image(arg_path, arg_mask, image);
    if (!image) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("Background image load failed: path=" + arg_path +
                               " exists=" + (std::filesystem::is_regular_file(file) ? "true" : "false"));
#endif
        return {std::nullopt, animated, false, std::nullopt};
    }
    const auto requested = ::cw::lib::Size{std::get<0>(arg_size), std::get<1>(arg_size)};
    // Py と同様、既にサイズが合う描画面と、空の (0,0) の描画面をどちらも維持する。
    if ((image->size.width != requested.width || image->size.height != requested.height) &&
        (image->size.width != 0 || image->size.height != 0)) {
        bool smooth = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_smoothscale_bg && image->size.height > 1;
        if (arg_smoothing != "Default") smooth = ::cw::util::str2bool(arg_smoothing);
        if (smooth && (image->size.width == 0 || image->size.height == 0))
            throw std::invalid_argument("background smoothscale: zero source dimension");
        if (smooth && !is_integer_scale(image, requested.width, requested.height)) {
            // 明示的な平滑化では高さ 1 でもこの分岐に入る場合がある。 Py
            // と同様、先に透過色付き RGB を変換し、その後 cw.image.smoothscale
            // の共通の高さ 1 の最近傍代替を使う。
            if (!image->has_pixel_alpha && image->has_colorkey)
                image = ::cw::lib::convert_surface_alpha(image);
            image = ::cw::image::smoothscale(image, arg_size, true, false);
        } else {
            image = resize_surface(image, requested.width, requested.height);
        }
    }
    if (!animated && cachable && ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario())
        if (scenario) {
            scenario->sweep_resourcecache(static_cast<I>(image->rgba.size()));
            scenario->m_resource_cache[key] =
                V(std::static_pointer_cast<::cw::lib::Object>(image));
        } else {
            m_surface_cache[key] = image;
        }
    if (arg_animated_cellkey && animated) m_animated_surfaces[*arg_animated_cellkey] = image;
    store_filepath(arg_path, static_cast<::cw::lib::Real>(mtime));
    return {image, animated, true, std::make_tuple(arg_path, static_cast<::cw::lib::Real>(mtime))};
}

void BackGround::clear_background()
{
    m__force_noinhrt = true; m_pc_cache.clear();
    if (!scenario_data()) m_surface_cache.clear();
    release_animated_paths();
    invalidate_ui(::cw::sprite::base::CWPySprite::m_rect);
}

bool BackGround::load(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_elements, V arg_doanime, std::tuple<S, V> arg_ttype, bool arg_bginhrt, bool arg_nocheckvisible, bool arg_redraw)
{
    if (m__in_playing) return false;
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    if (m__force_noinhrt) { m__force_noinhrt = false; arg_bginhrt = false; }
    if (!arg_bginhrt) { m_bgs.clear(); m_foregroundlist.clear(); }

    auto oldbgs = m_bgs;
    m__bgs = oldbgs;
    m__elements = arg_elements;
    if (::cw::g_cwpy && !::cw::g_cwpy->m_update_scaling) {
        // Python と同様、新しく背景を読む時は保留中のファイ
        // ル更新を消すが、 現在の場面の拡大率変更では維持する。
        ::cw::g_cwpy->m_file_updates_bg = false;
        m_use_excache = false;
        m_pc_cache.clear();
    }
    m_reload_jpdcimage = true;

    if (background_doanime_bool(arg_doanime)) {
        if (auto existing = animation_counter_value(arg_doanime)) m__doanime = arg_doanime;
        else {
            auto created = std::make_shared<::cw::effectbooster::AnimationCounter>();
            created->initialize();
            m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(created));
        }
    } else {
        auto cut = std::make_shared<::cw::effectbooster::CutAnimation>();
        cut->initialize();
        m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(cut));
    }
    m__ttype = arg_ttype;

    std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> ttype_o_sprite;
    std::optional<std::tuple<S, V>> ttype_o_tuple;
    if (!background_doanime_bool(arg_doanime)) {
        const auto transition = ::cw::sprite::transition::get_transition(arg_ttype);
        if (transition && *transition) ttype_o_sprite = *transition;
    } else {
        ttype_o_tuple = arg_ttype;
    }

    m__inhrt_index = 0;
    bool bginhrt2 = arg_bginhrt;
    if (bginhrt2 && !arg_elements.empty() && arg_elements.front() && arg_elements.front()->m_tag == "BgImage") {
        const auto& first = arg_elements.front();
        const auto [base, unused] = _create_bgbasedata(first); (void)unused;
        const auto& size = std::get<0>(base); const auto& pos = std::get<1>(base);
        const bool mask = element_bool(first, ".", "mask", false);
        const bool has_path = !element_text(first, "ImagePath", {}).empty();
        if (std::get<0>(pos) == 0 && std::get<1>(pos) == 0 && std::get<0>(size) == area_width() &&
            std::get<1>(size) == area_height() && !mask && has_path && std::get<5>(base).empty()) {
            bginhrt2 = false;
            // CardWirth 1.28～1.50 と同様、最初の全
            // 画面画像では Flag を無視する。 その要素は後で _add_imagecell
            // が再利用するため、継承の判断だけを変えず、
            // Python と同様に元ノードの値を消す。
            if (const auto flag = first->find("Flag"); flag && *flag) (*flag)->m_text.clear();
        }
    }
    if (bginhrt2) {
        bginhrt2 = false;
        if (!arg_nocheckvisible) {
            for (const auto& [type, value] : m_bgs)
                if (_is_flagchanged(type, value)) { bginhrt2 = true; break; }
        }
    } else {
        m_bgs.clear();
        m_foregroundlist.clear();
        m__inhrt_index = 0;
    }

    std::vector<Blit> blitlist;
    bool animated = false;
    bool update = false;
    bool forcedraw = false;
    const auto doanime_counter = animation_counter_value(m__doanime);

    if (bginhrt2) {
        const auto ret = _reload(m__doanime, {"None", V("None")}, false, false, false, false, true, {}, {}, {}, false);
        if (ret.is_none()) return false;
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&ret.storage());
        const auto result = object && *object ? std::dynamic_pointer_cast<ReloadBeforeloadResult>(*object) : nullptr;
        if (!result) return false;
        animated = result->animated;
        blitlist = std::move(result->blitlist);
        update = result->update;
        forcedraw = result->forcedraw;
    }

    bool afterseps = false;
    if (!m_bgs.empty() && std::get<0>(m_bgs.back()) != g_BG_SEPARATOR && arg_bginhrt)
        m_bgs.push_back({g_BG_SEPARATOR, std::nullopt});
    m__animated_load_counts.clear();
    for (const auto& e : arg_elements) {
        if (!e) continue;
        if (e->m_tag == "Redisplay") {
            m_bgs.push_back({g_BG_SEPARATOR, std::nullopt});
            if (!blitlist.empty()) {
                const auto after = _load_after(arg_bginhrt || afterseps, blitlist, background_doanime_bool(arg_doanime),
                                               animated, std::make_tuple(S("None"), V("None")), std::nullopt, oldbgs, m_bgs, false, true);
                blitlist = std::get<0>(after);
            } else {
                remove_jpy_temporal_sprites();
            }
            animated = false;
            afterseps = true;
            continue;
        }
        if (e->m_tag == "BgImage") {
            const auto d = _create_imagecelldata(e, false);
            if (!d) continue;
            try {
                const auto r = _add_imagecell(blitlist, m_bgs, oldbgs, *d, doanime_counter, arg_nocheckvisible, false);
                animated |= std::get<0>(r);
                arg_bginhrt = arg_bginhrt && std::get<2>(r);
                update |= std::get<1>(r);
            } catch (const ::cw::effectbooster::ScreenRescale&) {
                return false;
            }
        } else if (e->m_tag == "TextCell") {
            if (_add_textcell(blitlist, m_bgs, oldbgs, _create_textcelldata(e, false), arg_nocheckvisible, false))
                forcedraw = true;
        } else if (e->m_tag == "ColorCell") {
            if (_add_colorcell(blitlist, m_bgs, oldbgs, _create_colorcelldata(e, false), arg_nocheckvisible, false))
                forcedraw = true;
        } else if (e->m_tag == "PCCell") {
            if (_add_pccell(blitlist, m_bgs, oldbgs, _create_pccelldata(e, false), arg_nocheckvisible, false))
                forcedraw = true;
        }
    }

    update = update || !_equals_bgs(m_bgs, oldbgs, false);
    const bool redraw = arg_redraw && !_equals_bgs(m_bgs, oldbgs, true);

    bool transition = false;
    if (update) {
        const auto after = _load_after(arg_bginhrt || afterseps, blitlist, background_doanime_bool(arg_doanime), animated,
                                       ttype_o_tuple, ttype_o_sprite, oldbgs, m_bgs, redraw, false);
        transition = std::get<1>(after);
    } else if (forcedraw) {
        const auto after = _load_after(arg_bginhrt || afterseps, blitlist, background_doanime_bool(arg_doanime), animated,
                                       ttype_o_tuple, ttype_o_sprite, oldbgs, m_bgs, false, false);
        transition = std::get<1>(after);
    } else {
        remove_jpy_temporal_sprites();
    }

    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (update || forcedraw))
        (*::cw::g_cwpy->m_ydata)->changed();

    m__bgs.clear();
    m__elements.clear();
    m__doanime = V{};
    m__in_playing = false;
    m__ttype = {"None", V("None")};
    _sync_animated_paths();
    return update && redraw && !transition && !animated;
}

std::tuple<std::tuple<std::tuple<I, I>, std::tuple<I, I>, S, bool, I, S>, bool> BackGround::_create_bgbasedata(std::shared_ptr<::cw::data::CWPyElement> arg_e)
{
    const I left = element_int(arg_e, "Location", "left", std::nullopt), top = element_int(arg_e, "Location", "top", std::nullopt);
    const I width = element_int(arg_e, "Size", "width", std::nullopt), height = element_int(arg_e, "Size", "height", std::nullopt);
    const S flag = element_text(arg_e, "Flag", {}); const I layer = layer_value(element_int(arg_e, "Layer", {}, ::cw::g_LAYER_BACKGROUND));
    const S visible_s = element_attr(arg_e, ".", "visible", {}); const bool hasvisible = !visible_s.empty();
    bool visible = visible_s == "True" ? true : visible_s == "False" ? false : flag_value(flag) && collides_area({left, top}, {width, height});
    return {{{width, height}, {left, top}, flag, visible, layer, element_attr(arg_e, ".", "cellname", {})}, hasvisible};
}

std::optional<V> BackGround::_create_imagecelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster)
{
    const S path = ::cw::util::validate_filepath(element_text(arg_e, "ImagePath", {}));
    const auto ext = native_path(path).extension().generic_string();
    S lower = ext; for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (arg_ignoreeffectbooster && (lower == ".jpy1" || lower == ".jptx" || lower == ".jpdc")) return std::nullopt;
    bool inuse = element_bool(arg_e, "ImagePath", "inusecard", false);
    bool scaled = false;
    // Py と同様、明示カードの分岐は属性の有無ではなく解析した真偽値で選ぶ。
    // inusecard=False でも、現在使用中のカード素材を検索する。
    if (inuse) {
        scaled = element_bool(arg_e, "ImagePath", "scaledimage", false);
    } else {
        const auto inuse_path = ::cw::util::get_inusecardmaterialpath(path, ::cw::g_M_IMG, std::nullopt, true);
        inuse = !inuse_path.empty() && std::filesystem::is_regular_file(native_path(inuse_path));
        const auto scenario = scenario_data();
        scaled = scenario ? scenario->m_can_loaded_scaledimage : true;
        if (inuse && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
            const auto active = ::cw::g_cwpy->m_event->get_inusecard();
            if (active && *active) scaled = (*active)->get_can_loaded_scaledimage();
        }
    }
    const auto [base, hasvisible] = _create_bgbasedata(arg_e); (void)hasvisible;
    auto d = std::make_shared<CellData>(); set_base_data(d, base, g_BG_IMAGE);
    d->path = path; d->inusecard = inuse; d->scaledimage = scaled; d->mask = element_bool(arg_e, ".", "mask", false);
    d->smoothing = element_attr(arg_e, ".", "smoothing", "Default"); d->battle = element_bool(arg_e, ".", "battle", false);
    d->comparison_key = cell_key(*d); return make_value(d);
}

bool BackGround::remove_battle_background(V arg_doanime, std::tuple<S, V> arg_ttype, bool arg_ignoreeffectbooster)
{
    const auto old = m_bgs; m_bgs.erase(std::remove_if(m_bgs.begin(), m_bgs.end(), [](const Entry& e) {
        const auto d = std::get<1>(e); const auto data = d ? cell_data(*d) : nullptr; return data && data->battle;
    }), m_bgs.end());
    if (old.size() == m_bgs.size()) return false;
    _reload(std::move(arg_doanime), std::move(arg_ttype), true, true, true, true, false, {}, {}, {},
            arg_ignoreeffectbooster);
    return true;
}

std::tuple<I, I, I, I> BackGround::_getcolor(std::shared_ptr<::cw::data::CWPyElement> arg_e, S arg_xpath, I arg_r, I arg_g, I arg_b, I arg_a)
{
    return {element_int(arg_e, arg_xpath, "r", arg_r), element_int(arg_e, arg_xpath, "g", arg_g),
            element_int(arg_e, arg_xpath, "b", arg_b), element_int(arg_e, arg_xpath, "a", arg_a)};
}

V BackGround::_create_textcelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster)
{
    (void)arg_ignoreeffectbooster; const auto [base, hasvisible] = _create_bgbasedata(arg_e);
    auto d = std::make_shared<CellData>(); set_base_data(d, base, g_BG_TEXT); d->text = element_text(arg_e, "Text", {});
    d->face = element_text(arg_e, "Font", {}); d->text_size = element_int(arg_e, "Font", "size", 12);
    const auto color = _getcolor(arg_e, "Color", 0, 0, 0, 255); d->color1 = color;
    d->bold = element_bool(arg_e, "Font", "bold", false); d->italic = element_bool(arg_e, "Font", "italic", false);
    d->underline = element_bool(arg_e, "Font", "underline", false); d->strike = element_bool(arg_e, "Font", "strike", false);
    d->vertical = element_bool(arg_e, "Vertical", {}, false); d->antialias = element_bool(arg_e, "Antialias", {}, false);
    d->border_type = element_attr(arg_e, "Bordering", "type", "None"); d->color2 = _getcolor(arg_e, "Bordering/Color", 255, 255, 255, 255);
    d->border_width = element_int(arg_e, "Bordering", "width", 1); d->loaded = element_bool(arg_e, ".", "loaded", hasvisible ? true : false);
    d->update_type = element_text(arg_e, "UpdateType", "Variables"); d->battle = element_bool(arg_e, ".", "battle", false);
    const auto scenario_element = arg_e ? arg_e->find("Scenario") : std::nullopt;
    const auto author_element = arg_e ? arg_e->find("Author") : std::nullopt;
    if (scenario_element && *scenario_element && author_element && *author_element) {
        d->scenarioinfo_present = true;
        d->scenario = (*scenario_element)->m_text;
        d->author = (*author_element)->m_text;
    } else if (::cw::g_cwpy && ::cw::g_cwpy->m_event &&
               ::cw::g_cwpy->m_event->m_in_inusecardevent) {
        const auto active = ::cw::g_cwpy->m_event->get_inusecard();
        if (active && *active) {
            d->scenarioinfo_present = true;
            d->scenario = (*active)->m_scenario;
            d->author = (*active)->m_author;
        }
    }
    if (!d->scenarioinfo_present && ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        const auto scenario = scenario_data();
        if (scenario) {
            d->scenarioinfo_present = true;
            d->scenario = scenario->m_name;
            d->author = scenario->m_author;
        }
    }
    if (const auto names = arg_e ? arg_e->find("Names") : std::nullopt; names && *names) {
        d->names_present = true;
        try { for (const auto& child : (*names)->m_children) if (child) {
            const S type = element_attr(child, ".", "type", {});
            d->names_key += type + ":" + child->m_text + ":" +
                            element_attr(child, ".", "number", {}) + ":" + element_attr(child, ".", "flag", {}) +
                            element_attr(child, ".", "step", {}) + element_attr(child, ".", "variant", {}) + ";";
            std::optional<V> data;
            V name = child->m_text;
            const auto scenario = (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario())
                ? scenario_data() : nullptr;
            std::shared_ptr<::cw::data::YadoData> yado;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata)
                yado = *::cw::g_cwpy->m_ydata;
            if (type == "Yado") {
                if (yado) data = V(std::static_pointer_cast<::cw::lib::Object>(yado));
            } else if (type == "Party") {
                if (yado && yado->m_party && *yado->m_party)
                    data = V(std::static_pointer_cast<::cw::lib::Object>(*yado->m_party));
            } else if (type == "Player") {
                const I number = element_int(child, ".", "number", 0) - 1;
                if (::cw::g_cwpy && number >= 0 &&
                    number < static_cast<I>(::cw::g_cwpy->m_pcards.size()) &&
                    ::cw::g_cwpy->m_pcards[static_cast<std::size_t>(number)])
                    data = V(std::static_pointer_cast<::cw::lib::Object>(
                        std::static_pointer_cast<::cw::sprite::card::CWPyCard>(
                            ::cw::g_cwpy->m_pcards[static_cast<std::size_t>(number)])));
            } else if (type == "Flag") {
                name = V(::cw::util::str2bool(child->m_text));
                if (scenario) {
                    const auto key = element_attr(child, ".", "flag", {});
                    const auto it = scenario->m_flags.find(key);
                    if (it != scenario->m_flags.end() && it->second)
                        data = V(std::static_pointer_cast<::cw::lib::Object>(it->second));
                }
            } else if (type == "Step") {
                name = V(::cw::lib::require_python_integer(child->m_text));
                const auto key = element_attr(child, ".", "step", {});
                if (scenario) {
                    const auto it = scenario->m_steps.find(key);
                    if (it != scenario->m_steps.end() && it->second)
                        data = V(std::static_pointer_cast<::cw::lib::Object>(it->second));
                }
                // ScenarioLog の Names には、Py の特別な Step
                // 所有者を維持する。 表示専用の代替にすると、再保存時に型とキーを失う。
                if (!data) {
                    const auto step = ::cw::sprite::message::get_spstep(key);
                    if (step && *step)
                        data = V(std::static_pointer_cast<::cw::lib::Object>(*step));
                }
            } else if (type == "Variant") {
                const auto key = element_attr(child, ".", "variant", {});
                if (element_attr(child, ".", "valuetype", "None") != "None")
                    name = ::cw::data::Variant::value_from_element(child, "valuetype", std::nullopt);
                else
                    name = V{};
                if (scenario) {
                    const auto it = scenario->m_variants.find(key);
                    if (it != scenario->m_variants.end() && it->second)
                        data = V(std::static_pointer_cast<::cw::lib::Object>(it->second));
                }
            } else if (type == "Number") {
                name = V(::cw::lib::require_python_integer(child->m_text));
                data = V("Number");
            }
            auto item = std::make_shared<::cw::sprite::message::NameListItem>();
            item->initialize(data, name);
            d->names.push_back(std::move(item));
        } } catch (...) {
            // Python と同様、一つでも動的な値をデコードできなければ
            // Names 全体を捨てる。 黙って整数ゼロへ置き換えない。
            d->names.clear();
        }
    }
    d->comparison_key = cell_key(*d); return make_value(d);
}

V BackGround::_create_colorcelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster)
{
    (void)arg_ignoreeffectbooster; const auto [base, unused] = _create_bgbasedata(arg_e); (void)unused;
    auto d = std::make_shared<CellData>(); set_base_data(d, base, g_BG_COLOR); d->blend = element_text(arg_e, "BlendMode", "Normal");
    d->color1 = _getcolor(arg_e, "Color", 255, 255, 255, 255); d->gradient = element_attr(arg_e, "Gradient", "direction", "None");
    d->color2 = _getcolor(arg_e, "Gradient/EndColor", 0, 0, 0, 255); d->battle = element_bool(arg_e, ".", "battle", false);
    d->comparison_key = cell_key(*d); return make_value(d);
}

V BackGround::_create_pccelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster)
{
    (void)arg_ignoreeffectbooster; const auto [base, unused] = _create_bgbasedata(arg_e); (void)unused;
    auto d = std::make_shared<CellData>(); set_base_data(d, base, g_BG_PC); d->pcnumber = element_int(arg_e, "PCNumber", {}, 0);
    d->expand = element_bool(arg_e, ".", "expand", false); d->smoothing = element_attr(arg_e, ".", "smoothing", "Default");
    d->battle = element_bool(arg_e, ".", "battle", false); d->comparison_key = cell_key(*d); return make_value(d);
}

bool BackGround::reload(bool arg_doanime, std::tuple<S, V> arg_ttype, bool arg_redraw,
                        std::vector<S> arg_cellname_list,
                        std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> arg_repldata_list,
                        std::vector<std::optional<std::tuple<S, I, I, S, I, I>>> arg_movedata_list,
                        bool arg_ignoreeffectbooster, bool arg_nocheckvisible)
{
    // Python の公開 reload() と同様、常に redisplay=False
    // を使う。 True は RedisplayContent 専用で古い前景
    // 一覧を維持するため、消去・移動・置換に使うと削除した前景セルが画面に残る。
    return value_bool(_reload(V(arg_doanime), std::move(arg_ttype), arg_redraw, false, arg_nocheckvisible, false,
                              false, std::move(arg_cellname_list), std::move(arg_repldata_list),
                              std::move(arg_movedata_list), arg_ignoreeffectbooster));
}

V BackGround::_reload(V arg_doanime, std::tuple<S, V> arg_ttype, bool arg_redraw, bool arg_force,
                      bool arg_nocheckvisible, bool arg_redisplay, bool arg_beforeload,
                      std::vector<S> arg_cellname_list,
                      std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> arg_repldata_list,
                      std::vector<std::optional<std::tuple<S, I, I, S, I, I>>> arg_movedata_list,
                      bool arg_ignoreeffectbooster)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto oldbgs = m_bgs;
    std::vector<Entry> bgs2;
    if (arg_cellname_list.empty()) bgs2 = oldbgs;
    else {
        if (arg_repldata_list.size() < arg_cellname_list.size()) arg_repldata_list.resize(arg_cellname_list.size());
        if (arg_movedata_list.size() < arg_cellname_list.size()) arg_movedata_list.resize(arg_cellname_list.size());
        std::vector<bool> found(arg_cellname_list.size(), false);
        std::vector<Entry> retained;
        using Insertion = std::pair<std::size_t, std::vector<Entry>>;
        std::unordered_map<std::size_t, std::vector<Insertion>> insertions;
        // Py の実際の _reload の対応付けに合わせ、要求
        // 名に一致するすべての既存セルへ適用する。 None で
        // ない置き換えは移動より優先し、同じ位置の置き換えは要求
        // 順に処理する。 最初の一致で置き換え要求を消費しない。
        for (const auto& entry : oldbgs) {
            const I type = std::get<0>(entry); const auto& value = std::get<1>(entry);
            if (type == g_BG_IMAGE && arg_ignoreeffectbooster && value) {
                if (const auto data = cell_data(*value)) {
                    S ext = native_path(data->path).extension().generic_string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) {
                        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    });
                    if (ext == ".jpy1" || ext == ".jptx" || ext == ".jpdc") {
                        retained.push_back(entry);
                        continue;
                    }
                }
            }
            const S name = _get_cellname(type, value);
            const auto it = std::find(arg_cellname_list.begin(), arg_cellname_list.end(), name);
            if (name.empty() || it == arg_cellname_list.end()) { retained.push_back(entry); continue; }
            const std::size_t n = static_cast<std::size_t>(std::distance(arg_cellname_list.begin(), it));
            found[n] = true;
            if (arg_repldata_list[n]) {
                // Py と同様、repldata が None で
                // なければ空でも使う。 空の置き換えは一致セルを
                // 削除し、移動データが同時にある場合も優先する。
                std::vector<Entry> replacements;
                for (const auto& e : *arg_repldata_list[n]) {
                    if (!e) continue;
                    if (e->m_tag == "BgImage") { auto d = _create_imagecelldata(e, arg_ignoreeffectbooster); if (d) replacements.push_back({g_BG_IMAGE, *d}); }
                    else if (e->m_tag == "TextCell") replacements.push_back({g_BG_TEXT, _create_textcelldata(e, false)});
                    else if (e->m_tag == "ColorCell") replacements.push_back({g_BG_COLOR, _create_colorcelldata(e, false)});
                    else if (e->m_tag == "PCCell") replacements.push_back({g_BG_PC, _create_pccelldata(e, false)});
                }
                if (!replacements.empty()) insertions[retained.size()].push_back({n, std::move(replacements)});
            } else if (arg_movedata_list[n] && value) {
                retained.push_back({type, _move_bgdata(type, *value, *arg_movedata_list[n])});
            }
        }
        for (bool ok : found) if (!ok) return V(false);
        for (std::size_t position = 0; position <= retained.size(); ++position) {
            if (auto it = insertions.find(position); it != insertions.end()) {
                auto& requests = it->second;
                std::stable_sort(requests.begin(), requests.end(), [](const Insertion& left, const Insertion& right) {
                    return left.first < right.first;
                });
                for (const auto& request : requests)
                    bgs2.insert(bgs2.end(), request.second.begin(), request.second.end());
            }
            if (position < retained.size()) bgs2.push_back(retained[position]);
        }
    }

    if (::cw::g_cwpy && !::cw::g_cwpy->m_update_scaling) {
        ::cw::g_cwpy->m_file_updates_bg = false;
        m_use_excache = false;
        m_pc_cache.clear();
    }
    m_reload_jpdcimage = true;

    bool animated = false;
    std::vector<Blit> blitlist;
    bool bginhrt = true;
    bool update = arg_force;
    bool forcedraw = false;
    std::vector<Entry> bgs;
    std::vector<Entry> oldcopy = oldbgs;

    if (!arg_beforeload) {
        m__inhrt_index = 0;
        m__bgs = oldbgs;
        if (background_doanime_bool(arg_doanime)) {
            if (auto existing = animation_counter_value(arg_doanime)) m__doanime = arg_doanime;
            else {
                auto created = std::make_shared<::cw::effectbooster::AnimationCounter>();
                created->initialize();
                m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(created));
            }
            m__ttype = {"None", V("None")};
        } else {
            auto cut = std::make_shared<::cw::effectbooster::CutAnimation>();
            cut->initialize();
            m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(cut));
            m__ttype = arg_ttype;
        }
    }

    std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> ttype_o_sprite;
    std::optional<std::tuple<S, V>> ttype_o_tuple;
    if (!arg_beforeload && !background_doanime_bool(arg_doanime)) {
        const auto transition = ::cw::sprite::transition::get_transition(arg_ttype);
        if (transition && *transition) ttype_o_sprite = *transition;
    } else {
        ttype_o_tuple = arg_ttype;
    }

    const auto doanime_counter = animation_counter_value(m__doanime);
    m__animated_load_counts.clear();
    for (const auto& [bgtype, d] : bgs2) {
        if (bgtype == g_BG_IMAGE) {
            if (!d) continue;
            try {
                const auto r = _add_imagecell(blitlist, bgs, oldcopy, *d, doanime_counter, arg_nocheckvisible, true);
                animated |= std::get<0>(r);
                update |= std::get<1>(r);
                bginhrt &= std::get<2>(r);
            } catch (const ::cw::effectbooster::ScreenRescale&) {
                if (arg_beforeload) return V{};
                return V(false);
            }
        } else if (bgtype == g_BG_TEXT) {
            if (!d) continue;
            if (_add_textcell(blitlist, bgs, oldcopy, *d, arg_nocheckvisible, true)) forcedraw = true;
        } else if (bgtype == g_BG_COLOR) {
            if (!d) continue;
            if (_add_colorcell(blitlist, bgs, oldcopy, *d, arg_nocheckvisible, true)) forcedraw = true;
        } else if (bgtype == g_BG_PC) {
            if (!d) continue;
            if (_add_pccell(blitlist, bgs, oldcopy, *d, arg_nocheckvisible, true)) forcedraw = true;
        } else if (bgtype == g_BG_SEPARATOR) {
            if ((bgs.empty() || std::get<0>(bgs.back()) == g_BG_SEPARATOR) || !arg_redisplay) continue;
            bgs.push_back({bgtype, d});
            if (!blitlist.empty()) {
                const auto after = _load_after(true, blitlist, background_doanime_bool(arg_doanime), animated,
                                               std::make_tuple(S("None"), V("None")), std::nullopt, oldbgs, bgs, false, true);
                blitlist = std::get<0>(after);
            } else {
                remove_jpy_temporal_sprites();
            }
            animated = false;
        }
    }

    update = update || !_equals_bgs(m_bgs, bgs, false);
    const bool redraw_effective = arg_redraw && !_equals_bgs(m_bgs, bgs, true);
    if (bginhrt && blitlist.empty()) update = false;

    std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> transitspr;
    if (background_doanime_bool(arg_doanime)) {
        if (ttype_o_tuple) {
            const auto transition = ::cw::sprite::transition::get_transition(*ttype_o_tuple);
            if (transition && *transition) transitspr = *transition;
        }
    } else {
        transitspr = ttype_o_sprite;
    }

    bool transition = false;
    if (update) {
        m_bgs = bgs;
        if (!arg_beforeload) {
            clear_background_forgrounds(*this);
            const auto after = _load_after(true, blitlist, background_doanime_bool(arg_doanime), animated,
                                           std::nullopt, transitspr, oldbgs, m_bgs, redraw_effective, false);
            transition = std::get<1>(after);
        }
    } else if (forcedraw) {
        if (!arg_beforeload) {
            clear_background_forgrounds(*this);
            const auto after = _load_after(true, blitlist, background_doanime_bool(arg_doanime), animated,
                                           std::nullopt, transitspr, oldbgs, m_bgs, false, false);
            transition = std::get<1>(after);
        }
    } else if (!arg_beforeload) {
        remove_jpy_temporal_sprites();
    }

    if (arg_beforeload) {
        auto result = std::make_shared<ReloadBeforeloadResult>();
        result->animated = animated;
        result->blitlist = std::move(blitlist);
        result->update = update;
        result->forcedraw = forcedraw;
        return V(std::static_pointer_cast<::cw::lib::Object>(result));
    }

    m__bgs.clear();
    auto counter = std::make_shared<::cw::effectbooster::AnimationCounter>();
    counter->initialize();
    m__doanime = V(std::static_pointer_cast<::cw::lib::Object>(counter));
    m__ttype = {"None", V("None")};
    m__in_playing = false;
    _sync_animated_paths();
    return V(update && redraw_effective && !transition && !animated);
}

bool BackGround::_is_flagchanged(I arg_bgtype, std::optional<V> arg_d)
{
    if (!is_cell_type(arg_bgtype) || !arg_d) return false;
    const auto data = cell_data(*arg_d);
    return data ? data->visible != flag_value(data->flag) : false;
}

::cw::lib::String BackGround::_get_cellname(::cw::lib::Integer arg_bgtype, std::optional<::cw::lib::Value> arg_d)
{
    if (!is_cell_type(arg_bgtype) || !arg_d) return {};
    const auto data = cell_data(*arg_d);
    return data ? data->cellname : ::cw::lib::String{};
}

std::vector<::cw::lib::String> BackGround::get_cellnames(bool arg_unique)
{
    std::vector<S> names; std::set<S> seen;
    for (const auto& [type, value] : m_bgs) {
        const S name = _get_cellname(type, value);
        if (name.empty() || (arg_unique && !seen.insert(name).second)) continue;
        names.push_back(name);
    }
    return names;
}

::cw::lib::Value BackGround::_move_bgdata(::cw::lib::Integer arg_bgtype, ::cw::lib::Value arg_d, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer> arg_movedata)
{
    if (!is_cell_type(arg_bgtype)) return arg_d;
    const auto data = cell_data(arg_d);
    if (!data) return arg_d;

    const auto [position_type, x, y, size_type, width, height] = arg_movedata;
    const auto moved_pos = _calc_possize(position_type, {data->pos.x, data->pos.y}, {x, y}, false);
    const auto moved_size = _calc_possize(size_type, {data->size.width, data->size.height}, {width, height}, true);
    auto moved = std::make_shared<CellData>(*data);
    moved->pos = {std::get<0>(moved_pos), std::get<1>(moved_pos)};
    moved->size = {std::get<0>(moved_size), std::get<1>(moved_size)};
    moved->comparison_key = cell_key(*moved);
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(moved));
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> BackGround::_calc_possize(::cw::lib::String arg_ctype, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_vals, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_movevals, bool arg_miniszero)
{
    auto [x, y] = arg_vals;
    const auto [mx, my] = arg_movevals;
    const auto percentage = [](::cw::lib::Integer value, ::cw::lib::Integer percent) {
        const auto numerator = value * percent + 50;
        auto result = numerator / 100;
        if (numerator < 0 && numerator % 100 != 0) --result;
        return result;
    };
    if (arg_ctype == "Absolute") {
        x = mx;
        y = my;
    } else if (arg_ctype == "Relative") {
        x += mx;
        y += my;
    } else if (arg_ctype == "Percentage") {
        x = percentage(x, mx);
        y = percentage(y, my);
    }
    if (arg_miniszero) {
        x = std::max<::cw::lib::Integer>(0, x);
        y = std::max<::cw::lib::Integer>(0, y);
    }
    return {x, y};
}

std::tuple<bool, bool, bool> BackGround::_add_imagecell(std::vector<Blit>& arg_blitlist, std::vector<Entry>& arg_bgs, std::vector<Entry>& arg_oldbgs, V arg_d, std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, bool arg_nocheckvisible, bool arg_is_reload)
{
    const auto data = cell_data(arg_d); if (!data || data->path.empty()) return {false, false, true};
    const auto [path, inusecard] = _resolve_imagecell_path(arg_d);
    if (path.empty()) return {false, false, true};
    // Python と同様、行動イベント外で画像ファイルがない
    // セルは bgs に追加しない。 描画も保存も行わない。
    if (!(::cw::g_cwpy && ::cw::g_cwpy->m_in_actionevent) && !::cw::util::path_is_file(path))
        return {false, false, true};
    std::optional<std::tuple<S, I>> animated_key;
    auto ext = native_path(path).extension().generic_string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (::cw::animatedimage::is_supported_path(path) || ext == ".jpy1") {
        animated_key = _next_animated_cell_key(path); const S owner = _get_animated_owner(*animated_key);
        if (::cw::animatedimage::is_supported_path(path) && !m_animated_instance_ids.contains(*animated_key)) {
            const auto id = ::cw::animatedimage::acquire(path, owner, true);
            if (id) { m_animated_instance_ids[*animated_key] = *id; m__animated_owners[*animated_key] = owner; }
        }
        if (ext == ".jpy1") m_effect_animated_paths[*animated_key].clear();
    }
    auto counter = arg_doanime ? arg_doanime : std::make_shared<::cw::effectbooster::AnimationCounter>();
    auto loaded = load_surface(path, data->mask, data->smoothing,
                               {scaled_length(data->size.width), scaled_length(data->size.height)}, data->flag,
                               counter, data->visible, arg_nocheckvisible, data->scaledimage, animated_key);
    auto image = std::get<0>(loaded); const bool anime = std::get<1>(loaded), updated = std::get<2>(loaded);
    const bool flagok = arg_nocheckvisible ? data->visible : flag_value(data->flag);
    auto stored = std::make_shared<CellData>(*data);
    stored->inusecard = inusecard;
    if (!arg_is_reload) stored->layer = _add_splayerval(stored->layer);
    S ext_lower = ext;
    bool inheritance = true;
    // Py と同様、実際のピクセルの不透明度ではなく SRCALPHA・
    // 透過色の情報を確認する。 全ピクセルが不透明な RGBA でも継承
    // 背景を維持する。覆うかの判断には入力時の visible を使う。
    if (!anime && ext_lower != ".jpdc" && image && *image &&
        !(*image)->has_pixel_alpha && !(*image)->has_colorkey &&
        data->visible && !stored->mask && stored->flag.empty() &&
        stored->pos.x <= 0 && stored->pos.y <= 0 &&
        stored->size.width >= area_width() && stored->size.height >= area_height() &&
        // Rect.contains と同様、原点が負でも右端・下端へ届く必要がある
        // 。 pos + size の桁あふれを避け、非負の寸法を引いて判定する。
        stored->pos.x >= area_width() - stored->size.width &&
        stored->pos.y >= area_height() - stored->size.height) {
        if (m__inhrt_index > 0) {
            // Py のスライス削除と同様、終端以降の場合も一覧全体を除く。
            const auto count = std::min<std::size_t>(static_cast<std::size_t>(m__inhrt_index), arg_bgs.size());
            arg_bgs.erase(arg_bgs.begin(), arg_bgs.begin() + count);
            if (arg_blitlist.size() > 1)
                arg_blitlist.erase(arg_blitlist.begin(), arg_blitlist.end() - 1);
        }
        inheritance = false;
        m__inhrt_index = 0;
    }
    // Python と同様、読み込めた画像は visible=True、それ以
    // 外はフラグ値を記録する。 セルがエリア内にあるかは、その値に含めない。
    if (image && *image && (*image)->size.width > 0 && (*image)->size.height > 0) {
        stored->visible = true; stored->comparison_key = cell_key(*stored);
        if (flagok) arg_blitlist.push_back({g_BG_IMAGE, make_blit_value(*image, stored), stored->flag, stored->layer});
    } else {
        stored->visible = flagok; stored->comparison_key = cell_key(*stored);
        arg_oldbgs.push_back({g_BG_IMAGE, make_value(stored)});
    }
    arg_bgs.push_back({g_BG_IMAGE, make_value(stored)}); (void)arg_is_reload;
    return {anime, updated, inheritance};
}

bool BackGround::_add_textcell(std::vector<Blit>& arg_blitlist, std::vector<Entry>& arg_bgs, std::vector<Entry>& arg_oldbgs, V arg_d, bool arg_nocheckvisible, bool arg_is_reload)
{
    const auto data = cell_data(arg_d); if (!data) return false;
    auto stored = std::make_shared<CellData>(*data);
    // Python と同様、描画はフラグが有効かつエリア内の場合に行うが、 記録する visible
    // はフラグ値だけとする。nocheckvisible の場合は入力値を使う。
    const bool draw = arg_nocheckvisible ? data->visible
                                         : flag_value(data->flag) && collides_area(data->pos, data->size);
    const bool flagok = arg_nocheckvisible ? draw : flag_value(data->flag);
    if (!arg_is_reload) stored->layer = _add_splayerval(stored->layer);
    stored->visible = flagok;
    auto names_for_rpl = stored->names;
    bool names_present = stored->names_present;
    if (!arg_nocheckvisible && !names_for_rpl.empty()) {
        if (stored->update_type == "All") {
            names_for_rpl.clear();
            names_present = false;
        }
        else if (stored->update_type == "Variables") {
            for (const auto& item : names_for_rpl) {
                if (!item) continue;
                const auto* object = std::get_if<::cw::lib::ObjectPtr>(&item->m_data.storage());
                if (!object || !*object) continue;
                if (std::dynamic_pointer_cast<::cw::data::Flag>(*object) ||
                    std::dynamic_pointer_cast<::cw::data::Step>(*object) ||
                    std::dynamic_pointer_cast<::cw::data::Variant>(*object)) {
                    item->m_name = V{};
                    item->m_name_text.clear();
                    item->m_name_integer.reset();
                }
            }
        }
    }
    S render_text = stored->text;
    if (flagok && !stored->loaded) {
        std::optional<std::tuple<S, S>> scenario;
        if (stored->scenarioinfo_present)
            scenario = std::make_tuple(stored->scenario, stored->author);
        render_text = ::cw::util::decodewrap(stored->text);
        auto replaced = ::cw::sprite::message::rpl_specialstr(
            render_text,
            names_present
                ? std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>>(names_for_rpl)
                : std::nullopt,
            true, stored->update_type, false, true, scenario);
        render_text = std::get<0>(replaced);
        names_for_rpl = std::get<1>(replaced);
        names_present = true;
    }
    stored->names = std::move(names_for_rpl);
    stored->names_present = names_present;
    stored->comparison_key = cell_key(*stored);
    arg_bgs.push_back({g_BG_TEXT, make_value(stored)});
    if (draw) {
        auto render_data = std::make_shared<CellData>(*stored);
        render_data->text = render_text;
        const auto image = render_text_cell(render_data);
        arg_blitlist.push_back({g_BG_TEXT, make_blit_value(image, stored), stored->flag, stored->layer});
    }
    else arg_oldbgs.push_back({g_BG_TEXT, make_value(stored)});
    (void)arg_is_reload;
    return draw;
}

bool BackGround::_add_colorcell(std::vector<Blit>& arg_blitlist, std::vector<Entry>& arg_bgs, std::vector<Entry>& arg_oldbgs, V arg_d, bool arg_nocheckvisible, bool arg_is_reload)
{
    const auto data = cell_data(arg_d); if (!data) return false;
    auto stored = std::make_shared<CellData>(*data);
    // Python と同様、描画はフラグが有効かつエリア内の場合に行い、記録はフラグ値だけとする。
    const bool draw = arg_nocheckvisible ? data->visible
                                         : flag_value(data->flag) && collides_area(data->pos, data->size);
    const bool flagok = arg_nocheckvisible ? draw : flag_value(data->flag);
    if (!arg_is_reload) stored->layer = _add_splayerval(stored->layer);
    stored->visible = flagok; stored->comparison_key = cell_key(*stored);
    arg_bgs.push_back({g_BG_COLOR, make_value(stored)});
    if (draw) {
        auto image = ::cw::image::create_colorcell({scaled_length(stored->size.width), scaled_length(stored->size.height)},
                                                   stored->color1, stored->gradient, stored->color2);
        arg_blitlist.push_back({g_BG_COLOR, make_blit_value(image, stored), stored->flag, stored->layer});
    } else arg_oldbgs.push_back({g_BG_COLOR, make_value(stored)});
    (void)arg_is_reload;
    return draw;
}

bool BackGround::_add_pccell(std::vector<Blit>& arg_blitlist, std::vector<Entry>& arg_bgs, std::vector<Entry>& arg_oldbgs, V arg_d, bool arg_nocheckvisible, bool arg_is_reload)
{
    const auto data = cell_data(arg_d); if (!data) return false;
    // Python版は宿/partyまたはPC番号が無いとPCCellを背景列へ追加しない。
    if (data->pcnumber <= 0 || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party) return false;
    const auto cached = put_pccache(data->pcnumber - 1);
    // Python と同様、描画はフラグが有効かつエリア内の場合に行う。 描いたセル
    // は visible=True、描かなかったセルは入力データをそのまま記録する。
    const bool draw = arg_nocheckvisible ? data->visible
                                         : flag_value(data->flag) && collides_area(data->pos, data->size);
    if (draw) {
        auto stored = std::make_shared<CellData>(*data);
        if (!arg_is_reload) stored->layer = _add_splayerval(stored->layer);
        stored->visible = true;
        auto image = render_pc_cell(*this, stored);
        if (!std::get<0>(cached).empty() && std::get<1>(std::get<0>(cached).front()))
            stored->path = std::get<1>(std::get<0>(cached).front())->m_path;
        stored->comparison_key = cell_key(*stored);
        arg_bgs.push_back({g_BG_PC, make_value(stored)});
        arg_blitlist.push_back({g_BG_IMAGE, make_blit_value(image, stored), stored->flag, stored->layer});
    } else {
        auto stored = std::make_shared<CellData>(*data);
        arg_bgs.push_back({g_BG_PC, make_value(stored)});
        arg_oldbgs.push_back({g_BG_PC, make_value(stored)});
    }
    (void)arg_is_reload;
    return draw;
}

std::tuple<std::vector<std::tuple<::cw::lib::String, std::shared_ptr<::cw::image::ImageInfo>>>, bool> BackGround::put_pccache(::cw::lib::Integer arg_pi)
{
    using Cached = std::tuple<std::vector<std::tuple<S, std::shared_ptr<::cw::image::ImageInfo>>>, bool>;
    if (const auto found = m_pc_cache.find(arg_pi + 1); found != m_pc_cache.end())
        return found->second;

    std::vector<std::tuple<S, std::shared_ptr<::cw::image::ImageInfo>>> paths;
    bool can_loaded_scaledimage = false;
    std::shared_ptr<::cw::data::Party> party;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        party = (*::cw::g_cwpy->m_ydata)->m_party ? *(*::cw::g_cwpy->m_ydata)->m_party : nullptr;
    if (party && arg_pi >= 0 && arg_pi < static_cast<I>(party->m_members.size())) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&party->m_members[static_cast<std::size_t>(arg_pi)].storage());
        std::shared_ptr<::cw::data::CWPyElement> member;
        if (object && *object) {
            member = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object);
            if (!member) {
                if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object))
                    member = tree->getroot();
            }
        }
        if (member) {
            can_loaded_scaledimage = member->getbool(".", V("scaledimage"), false);
            const auto property_node = member->find("Property");
            const auto property = property_node ? *property_node : nullptr;
            for (const auto& info : ::cw::image::get_imageinfos(property, false)) {
                if (!info || info->m_path.empty()) continue;
                S path = info->m_path;
                { path = ::cw::util::join_yadodir(path); }if (!path.empty()) paths.emplace_back(path, info);
            }
        }
    }
    Cached result{paths, can_loaded_scaledimage};
    m_pc_cache[arg_pi + 1] = result;
    return result;
}

std::tuple<std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>, bool> BackGround::_load_after(bool arg_bginhrt, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>> arg_blitlist, bool arg_doanime, bool arg_animated, std::optional<std::tuple<::cw::lib::String, ::cw::lib::Value>> arg_ttype, std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> arg_transitspr, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_oldbgs, const std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_newbgs, bool arg_redraw, bool arg_redisplay)
{
    std::shared_ptr<::cw::sprite::transition::Transition> transitspr;
    if (arg_transitspr && *arg_transitspr) transitspr = *arg_transitspr;
    else if (arg_ttype) {
        if (auto created = ::cw::sprite::transition::get_transition(*arg_ttype); created && *created)
            transitspr = *created;
    }

    // Python は redisplay 以外では旧 foreground を先に除去する。
    // compat 層に pygame.LayeredDirty はないため、所有リストを同じ順序で
    // 更新し、SDL グループへの登録は描画アダプタに引き渡す。
    if (!arg_redisplay) {
        std::shared_ptr<::cw::lib::Object> cardgrp;
        if (::cw::g_cwpy) {
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
                cardgrp = *p;
        }
        for (const auto& old : m_foregrounds)
            if (old && m_ui_backend) m_ui_backend->remove_sprite(cardgrp, old);
        std::vector<Blit> merged;
        merged.reserve(m_foregroundlist.size() + arg_blitlist.size());
        merged.insert(merged.end(), m_foregroundlist.begin(), m_foregroundlist.end());
        merged.insert(merged.end(), arg_blitlist.begin(), arg_blitlist.end());
        arg_blitlist = std::move(merged);
        m_foregrounds.clear();
        m_foregroundlist.clear();
    }
    if (!arg_bginhrt) {
        // pygame版では BackGround.image がそのまま背景セルの合成先かつ
        // 描画対象になる。別SurfaceをUI側へ残すと、redraw()時に合成済み
        // Surfaceが未描画の黒Surfaceで置き換わるため、全所有者で共有する。
        auto canvas = make_surface(canvas_width(), canvas_height(), {0, 0, 0, 255});
        ::cw::sprite::base::CWPySprite::m_image = canvas;
        m_ui_surface = canvas;
        ::cw::sprite::base::CWPySprite::m_rect = {0, 0, canvas_width(), canvas_height()};
    }

    std::vector<Blit> result;
    bool transition = false;
    for (std::size_t item_index = 0; item_index < arg_blitlist.size(); ++item_index) {
        const auto& item = arg_blitlist[item_index];
        const I bgtype = std::get<0>(item);
        const V& d = std::get<1>(item);
        const S& flag = std::get<2>(item);
        const I layer = std::get<3>(item);
        if (layer == ::cw::g_LAYER_BACKGROUND) {
            _draw_bgcell(::cw::sprite::base::CWPySprite::m_image, {bgtype, d}, std::nullopt);
            continue;
        }
        if (arg_redisplay) {
            result.push_back(item);
            continue;
        }
        auto sprite = std::make_shared<BgCell>();
        sprite->initialize(bgtype, d, flag, layer, static_cast<I>(item_index));
        m_foregrounds.insert(sprite);
        if (::cw::g_cwpy && m_ui_backend) {
            std::shared_ptr<::cw::lib::Object> cardgrp;
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
                cardgrp = *p;
            I encoded_layer = layer;
            { encoded_layer = layer_value_from_tlayer(sprite->m_tlayer); }m_ui_backend->add_sprite(cardgrp, sprite, encoded_layer);
        }
        m_foregroundlist.push_back(item);
    }
    m_has_jpdcimage = !m_reload_jpdcimage;
    remove_jpy_temporal_sprites();
    if (arg_redraw) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->refresh_activeitem();
        if ((!arg_animated || !arg_doanime) && transitspr && !_equals_bgs(arg_oldbgs, arg_newbgs, true)) {
            std::shared_ptr<::cw::lib::Object> cardgrp;
            if (::cw::g_cwpy) {
                if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
                    cardgrp = *p;
            }
            // 画面効果はフレームごとに変わる実体のスプライトなので、CWPySprite
            // のバックエンドへ登録する。 フェード・ディゾルブ・ブラインドの各段階後に invalidate()
            // がプロキシー画像を更新できるようにする。 一度だけの画像を保存
            // する背景用バックエンドでは、画面にアニメーションが反映されない。
            transitspr->m_tlayer = {
                value_int(std::get<0>(::cw::g_LAYER_TRANSITION)),
                std::get<1>(::cw::g_LAYER_TRANSITION),
                std::get<2>(::cw::g_LAYER_TRANSITION),
                std::get<3>(::cw::g_LAYER_TRANSITION)};
            if (!transitspr->m_ui_backend)
                transitspr->m_ui_backend = ::cw::sprite::base::sprite_ui_backend();
            if (transitspr->m_ui_backend && cardgrp)
                transitspr->m_ui_backend->register_sprite(*transitspr, {cardgrp});
            ::cw::animation::animate_sprite(transitspr, "transition", true, true);
            if (transitspr->m_ui_backend) transitspr->m_ui_backend->unregister_sprite(*transitspr);
            transition = true;
        } else if (::cw::g_cwpy) {
            ::cw::g_cwpy->add_lazydraw_rect(::cw::sprite::base::CWPySprite::m_rect);
        }
    } else if (::cw::g_cwpy) {
        ::cw::g_cwpy->add_lazydraw_rect(::cw::sprite::base::CWPySprite::m_rect);
    }
    m_ui_surface = ::cw::sprite::base::CWPySprite::m_image;
    invalidate_ui(::cw::sprite::base::CWPySprite::m_rect);
    redraw_ui();
    // PythonはRedisplay時だけ、まだSprite化していない前景セルを返す。
    // 通常描画ではforegroundlistを上で直接更新し、返り値は空のままにする。
    return {result, transition};
}

void BgCell::initialize(::cw::lib::Integer arg_bgtype, ::cw::lib::Value arg_d, ::cw::lib::String arg_flag, ::cw::lib::Integer arg_layer, ::cw::lib::Integer arg_index)
{
    m_bgtype = V(arg_bgtype); m_d = arg_d; m_flag = V(std::move(arg_flag));
    m_tlayer = {arg_layer, ::cw::g_LTYPE_BACKGROUND, arg_index, 0};
    m_status = "normal"; m_frame = 0;
    auto payload = blit_payload(arg_d);
    if (payload) {
        m_image = payload->image;
        ::cw::sprite::base::CWPySprite::m_image = payload->image;
        if (payload->cell) {
            const auto& c = *payload->cell;
            // Python と同様、前景 BgCell の矩形は cw.s(pos/size)
            // で作る。 デコードした画像は既にその寸法へ拡縮済み
            // なので、ネイティブの合成処理へ同じ座標を公開する。
            ::cw::sprite::base::CWPySprite::m_rect = {
                scaled_coordinate(c.pos.x), scaled_coordinate(c.pos.y),
                payload->image ? payload->image->size.width : scaled_length(c.size.width),
                payload->image ? payload->image->size.height : scaled_length(c.size.height)};
            m_rect_noscale = make_value(payload->cell);
        } else if (payload->image) {
            ::cw::sprite::base::CWPySprite::m_rect = {0, 0, payload->image->size.width, payload->image->size.height};
        }
    } else if (const auto image = surface_value(arg_d)) {
        m_image = image;
        ::cw::sprite::base::CWPySprite::m_image = image;
        ::cw::sprite::base::CWPySprite::m_rect = {0, 0, image->size.width, image->size.height};
    } else if (const auto cell = cell_data(arg_d)) {
        m_image = placeholder_cell_surface(cell);
        ::cw::sprite::base::CWPySprite::m_image = m_image;
        ::cw::sprite::base::CWPySprite::m_rect = {
            scaled_coordinate(cell->pos.x), scaled_coordinate(cell->pos.y),
            scaled_length(cell->size.width), scaled_length(cell->size.height)};
        m_rect_noscale = make_value(cell);
    }
    m_rect = V("bgcell-rect");
}

void Jpy1TemporalSprite::initialize(std::shared_ptr<::cw::lib::Surface> arg_background,
                                    std::shared_ptr<Jpy1TemporalSprite> arg_owner)
{
    m_image = arg_background ? V(std::static_pointer_cast<::cw::lib::Object>(arg_background)) : V{};
    ::cw::sprite::base::CWPySprite::m_image = std::move(arg_background);
    ::cw::sprite::base::CWPySprite::m_rect = {0, 0, canvas_width(), canvas_height()};
    m_rect = V("area");
    ::cw::sprite::base::CWPySprite::m_tlayer = {::cw::g_LAYER_JPY_TEMPORAL, 0, 0, 0};
    if (::cw::g_cwpy) {
        std::shared_ptr<::cw::lib::Object> topgrp;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_topgrp.storage()); p && *p)
            topgrp = *p;
        if (auto backend = background_ui_backend()) {
            auto self = arg_owner
                ? std::static_pointer_cast<::cw::sprite::base::CWPySprite>(std::move(arg_owner))
                : sprite_self(this);
            backend->add_sprite(topgrp, self, ::cw::g_LAYER_JPY_TEMPORAL);
        }
    }
}

void LifeBar::initialize(::cw::lib::Value arg_ccard)
{
    m_ccard = arg_ccard;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_ccard.storage()); object && *object)
        m_ccard_typed = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object);
    update_scale();
    if (::cw::g_cwpy) {
        std::shared_ptr<::cw::lib::Object> cardgrp;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
            cardgrp = *p;
        if (auto backend = background_ui_backend()) {
            auto self = sprite_self(this);
            backend->add_sprite(cardgrp, self, layer_value_from_tlayer(::cw::g_LAYER_FRONT_LIFEBAR));
        }
    }
}

void LifeBar::update_scale()
{
    I width = 80, height = 110;
    if (m_ccard_typed) {
        width = std::max<I>(1, m_ccard_typed->m_rect.width);
        height = std::max<I>(1, m_ccard_typed->m_rect.height);
    }
    std::shared_ptr<::cw::image::CharacterCardImage> card_image;
    if (m_ccard_typed) {
        { card_image=std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(m_ccard_typed->get_cardimg()); }}
    const auto rendered_card=card_image ? card_image->get_image() : nullptr;
    I source_width=rendered_card ? std::max<I>(1,rendered_card->size.width) : width;
    I source_height=rendered_card ? std::max<I>(1,rendered_card->size.height) : height;
    auto image=make_surface(source_width,source_height,{0,0,0,0});
    auto life=card_image ? card_image->m_lifeimg : nullptr;
    if (!life) {
        // CharacterCardImage が未更新なら、同じ状態リソースからゲージを組み立てる。
        const auto statuses=::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
            ? ::cw::g_cwpy->m_rsrc->m_statuses : V{};
        const auto gauge2=resource_surface(statuses,"LIFEGUAGE2");
        const auto mask=resource_surface(statuses,"LIFEGUAGE2_MASK");
        auto gauge=(gauge2 && mask) ? gauge2 : resource_surface(statuses,"LIFEGUAGE");
        auto bar=resource_surface(statuses,"LIFEBAR");
        if (!gauge) gauge=make_surface(std::max<I>(1,static_cast<I>(std::lround(48*::cw::g_UP_SCR))),
                                       std::max<I>(1,static_cast<I>(std::lround(7*::cw::g_UP_SCR))),
                                       {72,42,34,255});
        if (!bar) bar=make_surface(gauge->size.width,gauge->size.height,{196,32,32,255});
        I current=1,maximum=1;
        if (const auto character=std::dynamic_pointer_cast<::cw::character::Character>(m_ccard_typed)) {
            current=std::clamp<I>(character->m_life,0,std::max<I>(1,character->m_maxlife));
            maximum=std::max<I>(1,character->m_maxlife);
        }
        const I unit=static_cast<I>(::cw::g_UP_SCR);
        const I bar_x=static_cast<I>(
            static_cast<double>(current)*(gauge->size.width+unit)/maximum+0.5)
            -(gauge->size.width+unit);
        life=::cw::image::compose_lifebar(bar,gauge,gauge2 && mask ? mask : nullptr,bar_x,unit);
    }
    I life_x=std::max<I>(0,static_cast<I>(std::lround(8*::cw::g_UP_SCR)));
    I life_y=std::max<I>(0,source_height-static_cast<I>(std::lround(20*::cw::g_UP_SCR)));
    if (card_image) std::tie(life_x,life_y)=card_image->_bottom_left_position({8,110});
    ::cw::image::blit_surface_pixels(image, life, life_x, life_y);
    if (image->size.width!=width || image->size.height!=height) image=resize_surface(image,width,height);
    m_base_image=image;
    m__image = V(std::static_pointer_cast<::cw::lib::Object>(m_base_image));
    m_image = m__image;
    ::cw::sprite::base::CWPySprite::m_image = m_base_image;
    if (m_ccard_typed) {
        m_base_rect=m_ccard_typed->m_rect;
    } else {
        m_base_rect={0,0,width,height};
    }
    ::cw::sprite::base::CWPySprite::m_rect=m_base_rect;
    m__rect = V("card-rect"); m_rect = V("card-rect");
    if (m_ccard_typed && m_ccard_typed->m_status=="click") click();
}

void LifeBar::click()
{
    const auto base = m_base_image;
    if (!base) return;
    const auto width = m_base_rect.width * 9 / 10;
    const auto height = m_base_rect.height * 9 / 10;
    auto scaled = resize_surface(base, width, height);
    if (width == 0 || height == 0) {
        scaled = std::make_shared<::cw::lib::Surface>(*base);
        scaled->size = {width, height};
        scaled->rgba.clear();
    }
    ::cw::sprite::base::CWPySprite::m_image = scaled;
    m_image=V(std::static_pointer_cast<::cw::lib::Object>(scaled));
    auto rect=m_base_rect;
    rect.width=width; rect.height=height;
    rect.x=m_base_rect.x + m_base_rect.width / 2 - width / 2;
    rect.y=m_base_rect.y + m_base_rect.height / 2 - height / 2;
    ::cw::sprite::base::CWPySprite::m_rect=rect;
}

void LifeBar::declick()
{
    ::cw::sprite::base::CWPySprite::m_image=m_base_image;
    if (m_base_image) m_image=V(std::static_pointer_cast<::cw::lib::Object>(m_base_image));
    ::cw::sprite::base::CWPySprite::m_rect=m_base_rect;
}

void NumberOfCards::initialize(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_pcard, ::cw::lib::Integer arg_cardtype, std::shared_ptr<::cw::lib::Object> arg_spritegrp)
{
    m_pcard_typed = std::move(arg_pcard); m_cardtype = V(arg_cardtype);
    if (m_pcard_typed)
        m_pcard = V(std::static_pointer_cast<::cw::lib::Object>(std::static_pointer_cast<::cw::sprite::card::CWPyCard>(m_pcard_typed)));
    update_scale();
    if (auto backend = background_ui_backend()) backend->add_sprite(arg_spritegrp,
        sprite_self(this), ::cw::g_LAYER_NUMBER_OF_CARDS);
}

void NumberOfCards::update_scale()
{
    I num = 0, cap = 0;
    const bool personal = value_int(m_cardtype) == ::cw::g_POCKET_PERSONAL;
    if (m_pcard_typed) {
        if (personal) {
            num = static_cast<I>(m_pcard_typed->m_personal_pocket.size());
            cap = m_pcard_typed->get_personalpocketspace();
        } else {
            const auto cards = m_pcard_typed->get_pocketcards(value_int(m_cardtype));
            num = static_cast<I>(cards.size());
            const auto spaces = m_pcard_typed->get_cardpocketspace();
            const I type = value_int(m_cardtype);
            cap = type == ::cw::g_POCKET_SKILL ? std::get<0>(spaces) :
                  type == ::cw::g_POCKET_ITEM ? std::get<1>(spaces) : std::get<2>(spaces);
        }
    }
    const auto face = personal ? "numcards_personal" : "numcards";
    std::shared_ptr<::cw::imageretouch::Font> font;
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::setting::Resource::s_fonts.storage());
        object && *object) {
        if (const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object)) {
            if (const auto value = table->get(face, std::nullopt)) {
                if (const auto font_object = std::get_if<::cw::lib::ObjectPtr>(&value->storage()); font_object && *font_object)
                    font = std::dynamic_pointer_cast<::cw::imageretouch::Font>(*font_object);
            }
        }
    }
    if (!font) {
        font = std::make_shared<::cw::imageretouch::Font>();
        font->initialize("ＭＳ Ｐゴシック", -18, false, false);
    }
    const auto number = std::to_string(num), maximum = std::to_string(cap);
    const auto [number_width, number_height] = font->size(number);
    const auto [slash_width, slash_height] = font->size("/");
    const auto [maximum_width, maximum_height] = font->size(maximum);
    const I width = std::max(number_width, maximum_width) * 2 + slash_width;
    const I height = font->get_height();
    auto text = make_surface(width, height, {0, 0, 0, 0});
    const auto slash_x = (width - slash_width) / 2;
    if (const auto rendered = font->render(number, true, {0, 0, 0}))
        blit_surface(text, rendered, slash_x - rendered->size.width, 0);
    if (const auto rendered = font->render("/", true, {0, 0, 0}))
        blit_surface(text, rendered, slash_x, 0);
    if (const auto rendered = font->render(maximum, true, {0, 0, 0}))
        blit_surface(text, rendered, slash_x + slash_width, 0);
    auto image = make_surface(width + 2, height + 2, {0, 0, 0, 0});
    for (I x = 0; x < 3; ++x)
        for (I y = 0; y < 3; ++y)
            if (x != 1 || y != 1) blit_surface(image, text, x, y);
    if (text->rgba.size() >= 4)
        for (std::size_t i = 0; i + 3 < text->rgba.size(); i += 4) {
            text->rgba[i] = 255; text->rgba[i + 1] = 255; text->rgba[i + 2] = 255;
        }
    blit_surface(image, text, 1, 1);
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    ::cw::sprite::base::CWPySprite::m_image = image;
    auto rect = ::cw::lib::Rect{0, 0, image->size.width, image->size.height};
    if (m_pcard_typed) {
        if (personal) {
            I panel_width = 0;
            if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::setting::Resource::s_pygamedialogs.storage());
                object && *object) {
                if (const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object)) {
                    if (const auto value = table->get("TO_PERSONAL_POCKET", std::nullopt))
                        if (const auto panel = surface_value(*value)) panel_width = panel->size.width;
                }
            }
            rect.y = std::min(m_pcard_typed->m_rect.y + m_pcard_typed->m_rect.height - scaled_length(12),
                              canvas_height() - rect.height - scaled_length(2));
            const auto difference = panel_width - rect.width;
            const auto half = difference / 2 - (difference < 0 && difference % 2 != 0 ? 1 : 0);
            rect.x = m_pcard_typed->m_rect.x - scaled_length(5) + half;
        } else {
            I card_width = m_pcard_typed->m_rect.width;
            if (num) {
                if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::setting::Resource::s_pygamedialogs.storage());
                    object && *object) {
                    if (const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object)) {
                        if (const auto value = table->get("REPLACE_CARDS", std::nullopt))
                            if (const auto replace = surface_value(*value)) card_width -= replace->size.width / 2;
                    }
                }
            }
            const auto half = card_width / 2 - (card_width < 0 && card_width % 2 != 0 ? 1 : 0);
            rect.x = m_pcard_typed->m_rect.x + half - rect.width / 2;
            rect.y = m_pcard_typed->m_rect.y - height - 1 - scaled_length(5);
        }
    }
    ::cw::sprite::base::CWPySprite::m_rect = rect;
    m_rect = V("number-rect");
}

void PriceOfCard::initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_mcard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, std::shared_ptr<::cw::lib::Object> arg_spritegrp)
{
    m_mcard_typed = std::move(arg_mcard); m_header_typed = std::move(arg_header);
    if (m_mcard_typed)
        m_mcard = V(std::static_pointer_cast<::cw::lib::Object>(m_mcard_typed));
    if (m_header_typed && *m_header_typed)
        m_header = V(std::static_pointer_cast<::cw::lib::Object>(*m_header_typed));
    update_scale();
    // 共通の描画計画は、下の旧 Value タプルではなく基底スプラ
    // イトのレイヤーを読む。 両方で価格を売却カードのすぐ上に置く。
    ::cw::sprite::base::CWPySprite::m_tlayer = m_mcard_typed
        ? m_mcard_typed->m_tlayer
        : std::tuple<I, I, I, I>{};
    ++std::get<3>(::cw::sprite::base::CWPySprite::m_tlayer);
    m_tlayer = std::make_tuple(V(m_mcard_typed ? std::get<0>(m_mcard_typed->m_tlayer) : 0),
                               V(m_mcard_typed ? std::get<1>(m_mcard_typed->m_tlayer) : 0),
                               V(m_mcard_typed ? std::get<2>(m_mcard_typed->m_tlayer) : 0),
                               m_mcard_typed ? std::get<3>(m_mcard_typed->m_tlayer) + 1 : 1);
    if (auto backend = background_ui_backend()) {
        I layer = ::cw::g_LAYER_FRONT;
        try { layer = layer_value_from_tlayer(m_tlayer); } catch (...) {}
        backend->add_sprite(arg_spritegrp, sprite_self(this), layer);
    }
}

void PriceOfCard::set_header(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    m_header_typed = std::move(arg_header);
    m_header = (m_header_typed && *m_header_typed)
        ? V(std::static_pointer_cast<::cw::lib::Object>(*m_header_typed)) : V{};
    update_scale();
}

void PriceOfCard::update_scale()
{
    if (!m_header_typed || !*m_header_typed || !m_mcard_typed) {
        ::cw::sprite::base::CWPySprite::m_image = make_surface(0, 0);
        ::cw::sprite::base::CWPySprite::m_rect = {};
        m_image = V(std::static_pointer_cast<::cw::lib::Object>(::cw::sprite::base::CWPySprite::m_image));
        return;
    }
    const auto header = *m_header_typed;
    const I padw = scaled_length(2), padh = scaled_length(2);
    const I margw = scaled_length(5), margh = scaled_length(5);
    const auto text = header->can_selling() ? std::to_string(header->get_sellingprice()) : "---";
    std::shared_ptr<::cw::imageretouch::Font> font;
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::setting::Resource::s_fonts.storage());
        object && *object) {
        if (const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object)) {
            if (const auto value = table->get("price", std::nullopt)) {
                if (const auto font_object = std::get_if<::cw::lib::ObjectPtr>(&value->storage()); font_object && *font_object)
                    font = std::dynamic_pointer_cast<::cw::imageretouch::Font>(*font_object);
            }
        }
    }
    if (!font) {
        font = std::make_shared<::cw::imageretouch::Font>();
        font->initialize("ＭＳ Ｐ明朝", -16, true, true);
    }
    const auto text_height = std::get<1>(font->size_withoutoverhang(text));
    const I width = std::max<I>(0, m_mcard_typed->m_rect.width - margw * 2);
    const I height = text_height + padh * 2;
    const I py = m_mcard_typed->m_rect.y + m_mcard_typed->m_rect.height - text_height - (margh + padh * 2);
    auto image = make_surface(width, height, {255, 255, 255, 160});
    auto black = font->render(text, true, {0, 0, 0});
    auto white = font->render(text, true, {255, 255, 255});
    if (black && white) {
        if (black->size.width > width - padw * 2 && width > padw * 2) {
            const auto size = std::make_tuple(width - padw * 2, black->size.height);
            black = ::cw::image::smoothscale_card(black, size, true);
            white = ::cw::image::smoothscale_card(white, size, true);
        }
        const I x = (width - black->size.width) / 2;
        for (const auto [dx, dy] : {std::pair<I, I>{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
                                    {0, 1}, {1, -1}, {1, 0}, {1, 1}})
            blit_surface(image, white, x + dx, padh + dy);
        blit_surface(image, black, x, padh);
    }
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    ::cw::sprite::base::CWPySprite::m_image = image;
    ::cw::sprite::base::CWPySprite::m_rect = {m_mcard_typed->m_rect.x + margw, py,
                                              width, height};
    m_rect = V("price-rect");
}

void TargetArrow::initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target)
{
    m_target_typed = std::move(arg_target);
    if (m_target_typed) m_target = V(std::static_pointer_cast<::cw::lib::Object>(m_target_typed));
    update_scale();
    if (::cw::g_cwpy) {
        // Python と同様、矢印自体を LAYER_TARGET_ARROW で cardgrp
        // に追加する。 共通描画計画はスプライト自身の tlayer を読むた
        // め、バックエンドのプロキシーにだけ設定すると矢印がカードの背後になる。
        m_tlayer = {
            value_int(std::get<0>(::cw::g_LAYER_TARGET_ARROW)),
            std::get<1>(::cw::g_LAYER_TARGET_ARROW),
            std::get<2>(::cw::g_LAYER_TARGET_ARROW),
            std::get<3>(::cw::g_LAYER_TARGET_ARROW)};
        std::shared_ptr<::cw::lib::Object> group;
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
            group = *p;
        if (auto backend = background_ui_backend()) backend->add_sprite(group,
            sprite_self(this), layer_value_from_tlayer(::cw::g_LAYER_TARGET_ARROW));
    }
}

void TargetArrow::update_scale()
{
    std::shared_ptr<::cw::lib::Surface> image;
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&::cw::setting::Resource::s_statuses.storage());
        object && *object) {
        if (const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object)) {
            if (const auto value = table->get("TARGET", std::nullopt))
                image = surface_value(*value);
        }
    }
    if (!image) image = make_surface(20, 20, {255, 80, 80, 255});
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    ::cw::sprite::base::CWPySprite::m_image = image;
    if (m_target_typed) {
        ::cw::sprite::base::CWPySprite::m_rect = {m_target_typed->m_rect.x + m_target_typed->m_rect.width - scaled_length(20),
                                                  m_target_typed->m_rect.y + m_target_typed->m_rect.height - scaled_length(20),
                                                  image->size.width, image->size.height};
    } else {
        ::cw::sprite::base::CWPySprite::m_rect = {};
    }
    m_rect = V("target-rect");
}

void ClickableSprite::initialize(::cw::lib::Callback arg_getimage, ::cw::lib::Callback arg_getselimage, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::shared_ptr<::cw::lib::Object> arg_spritegrp, std::optional<::cw::lib::Callback> arg_lclickevent, std::optional<::cw::lib::Callback> arg_rclickevent)
{
    m_getimage_cb = std::move(arg_getimage); m_getselimage_cb = std::move(arg_getselimage);
    m_lclickevent_cb = std::move(arg_lclickevent); m_rclickevent_cb = std::move(arg_rclickevent);
    m_pos_noscale_typed = arg_pos_noscale;
    m__pos_noscale = V(std::to_string(std::get<0>(arg_pos_noscale)) + "," + std::to_string(std::get<1>(arg_pos_noscale)));
    // 登録と後の解除の両方に使えるよう、呼び出し側のグループを保持す
    // る。 先に互換 Value へ移動すると arg_spritegrp
    // が空になり、個人ポケット対象を含む ClickableSprite
    // が共通 SpriteGroup に登録されない。
    m_spritegrp = V(arg_spritegrp);
    m_status = "normal"; m_old_status = "normal"; m_frame = 0; m__clicking = false;
    m_clickable_group = 0;
    update_scale();
    if (auto backend = background_ui_backend()) backend->add_sprite(arg_spritegrp,
        sprite_self(this), ::cw::g_LAYER_CLICKABLE_SPRITES);
}

void ClickableSprite::update_scale()
{
    V normal_value;
    if (m_getimage_cb) normal_value = m_getimage_cb({}, {});
    m__image = normal_value;
    auto normal = surface_value(normal_value);
    if (!normal) normal = make_surface(1, 1, {0, 0, 0, 0});
    const auto clicked = ::cw::image::rotozoom_zero(normal, 0.9);
    m__clickedimage = V(std::static_pointer_cast<::cw::lib::Object>(clicked));
    std::shared_ptr<::cw::lib::Surface> selected;
    if (m_getselimage_cb) selected = surface_value(m_getselimage_cb({}, {}));
    if (!selected) selected = negative_surface(normal);
    auto selected_clicked = m_getselimage_cb
        ? ::cw::image::rotozoom_zero(selected, 0.9)
        : negative_surface(clicked);
    m__selimage = V(std::static_pointer_cast<::cw::lib::Object>(selected));
    m__selclickedimage = V(std::static_pointer_cast<::cw::lib::Object>(selected_clicked));
    // Python の cw.s(_pos_noscale) と同様、リ
    // ソース画像が拡縮済みでも、 論理原点も UP_SCR で変換する。
    const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
        ? ::cw::g_UP_SCR : 1.0;
    const ::cw::lib::Rect base_rect{
        static_cast<::cw::lib::Integer>(std::get<0>(m_pos_noscale_typed) * scale),
        static_cast<::cw::lib::Integer>(std::get<1>(m_pos_noscale_typed) * scale),
        normal->size.width, normal->size.height};
    ::cw::sprite::base::CWPySprite::m_image = normal;
    ::cw::sprite::base::CWPySprite::m_rect = base_rect;
    if (auto clicked = surface_value(m__clickedimage)) {
        ::cw::lib::Rect clicked_rect{0, 0, clicked->size.width, clicked->size.height};
        // pygame の Rect.center と同様、それぞれの整数の半分を別々に使
        // う。 寸法差をまとめて割ると、押下時の寸法が奇数の場合に 1px 左へずれる。
        clicked_rect.x = base_rect.x + base_rect.width / 2 - clicked_rect.width / 2;
        clicked_rect.y = base_rect.y + base_rect.height / 2 - clicked_rect.height / 2;
        m__clickedrect = V(std::make_shared<::cw::RectValue>(clicked_rect));
    } else {
        m__clickedrect = V(std::make_shared<::cw::RectValue>(base_rect));
    }
    m_rect = V("clickable-rect");
}

std::shared_ptr<::cw::lib::Surface> ClickableSprite::get_unselectedimage()
{
    if (m_status == "click") return surface_value(m__clickedimage);
    return surface_value(m__image);
}

std::shared_ptr<::cw::lib::Surface> ClickableSprite::get_selectedimage()
{
    if (m_status == "click") return surface_value(m__selclickedimage);
    return surface_value(m__selimage);
}

void ClickableSprite::lclick_event()
{
    if (!m_lclickevent_cb) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    const auto shared = std::shared_ptr<ClickableSprite>(this, [](ClickableSprite*) {});
    (void)::cw::animation::animate_sprite(shared, "click", true, false, false, false);
    (*m_lclickevent_cb)({}, {});
}

void ClickableSprite::rclick_event()
{
    if (!m_rclickevent_cb) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    const auto shared = std::shared_ptr<ClickableSprite>(this, [](ClickableSprite*) {});
    (void)::cw::animation::animate_sprite(shared, "click", true, false, false, false);
    (*m_rclickevent_cb)({}, {});
}

void ClickableSprite::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args; (void)arg_kwargs;
    if (m_status == "normal") update_normal();
    else if (m_status == "click") update_click();
    else if (m_status == "hide") update_hide();
    else if (m_status == "delete") update_delete();
}

void ClickableSprite::update_normal()
{
    update_selection();
}

void ClickableSprite::update_click()
{
    if (m_frame < 3 && !m__clicking) {
        m__clicking = true;
        m_status = "click";
        ::cw::sprite::base::CWPySprite::m_image = get_selectedimage();
        if (const auto* clicked_rect = std::get_if<::cw::lib::ObjectPtr>(&m__clickedrect.storage());
            clicked_rect && *clicked_rect) {
            if (const auto typed = std::dynamic_pointer_cast<::cw::RectValue>(*clicked_rect))
                ::cw::sprite::base::CWPySprite::m_rect = typed->rect;
        }
    } else if (m_frame >= 3) {
        m_status = m_old_status.empty() ? "normal" : m_old_status;
        m__clicking = false;
        m_frame = 0;
        ::cw::sprite::base::CWPySprite::m_image = get_selectedimage();
        if (auto normal = surface_value(m__image)) {
            const auto scale = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0
                ? ::cw::g_UP_SCR : 1.0;
            ::cw::sprite::base::CWPySprite::m_rect = {
                static_cast<I>(std::get<0>(m_pos_noscale_typed) * scale),
                static_cast<I>(std::get<1>(m_pos_noscale_typed) * scale),
                normal->size.width, normal->size.height};
        }
        return;
    }
    ++m_frame;
}

void ClickableSprite::update_hide()
{
    std::vector<I> scales;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_setting->m_dealing_scales.storage());
            object && *object) {
            if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object))
                for (const auto& value : sequence->m_values) scales.push_back(value_int(value));
        }
    }
    if (scales.empty()) scales = {100, 80, 60, 40, 20};
    if (m_frame >= static_cast<I>(scales.size())) { m_status = "hidden"; m_frame = 0; return; }
    const auto old_rect = ::cw::sprite::base::CWPySprite::m_rect;
    const I percent = scales[static_cast<std::size_t>(m_frame)];
    I width;
    if (old_rect.width >= 0 && percent >= 0 &&
        (percent == 0 || old_rect.width <= (std::numeric_limits<I>::max)() / percent)) {
        width = old_rect.width * percent / 100;
    } else {
        // Python の整数の乗算・切り下げ除算は桁あふれしない
        // 。 通常のアニメーション経路は軽く保ちつつ、積が int64
        // を超えても商が表現可能な場合は正確な演算で扱う。
        const ::cw::lib::DecimalContext exact{0, ::cw::lib::DecimalRounding::Down};
        width = ::cw::lib::Decimal(old_rect.width)
            .multiply(::cw::lib::Decimal(percent), exact)
            .floor_divide(::cw::lib::Decimal(100), exact).to_integer();
    }
    if (width < 0 || old_rect.height < 0)
        throw std::invalid_argument("ClickableSprite hide: negative image size");
    const bool selected = ::cw::sprite::base::runtime_context() &&
        ::cw::sprite::base::runtime_context()->selection == this;
    auto image = selected ? get_selectedimage() : get_unselectedimage();
    if (!image) return;
    auto resized = resize_surface(image, width, old_rect.height);
    // pygame.transform.scale と同様、入力描画面の
    // 属性を維持する。 共通の背景拡縮を使う他の呼び出し側は変更しない。
    auto scaled = std::make_shared<::cw::lib::Surface>(*image);
    scaled->size = resized->size;
    scaled->rgba = resized->rgba;
    if (selected) scaled = ::cw::imageretouch::to_negative_for_card(scaled, 0);
    ::cw::sprite::base::CWPySprite::m_image = std::move(scaled);
    // Py と同様、直前の矩形を次の幅・中央位置の基準にする
    // 。 幅ゼロのフレームも、1px に固定せず維持する。
    ::cw::sprite::base::CWPySprite::m_rect = {
        old_rect.x + old_rect.width / 2 - width / 2,
        old_rect.y, width, old_rect.height};
    ++m_frame;
}

void ClickableSprite::update_delete()
{
    update_hide();
    if (m_status == "hidden") {
        if (auto backend = background_ui_backend()) {
            std::shared_ptr<::cw::lib::Object> group;
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&m_spritegrp.storage()); p && *p)
                group = *p;
            backend->remove_sprite(group, sprite_self(this));
        }
    }
}

void Curtain::initialize(std::shared_ptr<::cw::sprite::base::CWPySprite> arg_target, std::shared_ptr<::cw::lib::Object> arg_spritegrp, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_color, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_layer, bool arg_cut_bgs, bool arg_is_selectable, bool arg_initialize)
{
    m_target = std::move(arg_target); m__is_selectable = V(arg_is_selectable);
    if (arg_color) m_color_typed = *arg_color;
    else if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        m_color_typed = ::cw::g_cwpy->m_setting->get_curtaincolour();
    m_color = V(std::to_string(std::get<0>(m_color_typed)) + "," + std::to_string(std::get<1>(m_color_typed)) + "," +
                std::to_string(std::get<2>(m_color_typed)) + "," + std::to_string(std::get<3>(m_color_typed)));
    if (arg_layer) m_tlayer = *arg_layer;
    else if (m_target) {
        // Python と同様、選択可能なカーテンは対象の一つ上の副
        // レイヤーに置く。 同じレイヤーでは、プロキシーの登録順に
        // よって、覆われた無効な戦闘対象がホバー判定で優先される。
        m_tlayer = {std::get<0>(m_target->m_tlayer),
                    std::get<1>(m_target->m_tlayer),
                    std::get<2>(m_target->m_tlayer), 1};
    }
    m_cutter = arg_cut_bgs ? V("target-mask") : V{};
    m_cutter_surface.reset();
    m_cutter_pos = {0, 0};
    if (arg_initialize) update_scale();
    if (auto backend = background_ui_backend()) {
        I layer = ::cw::g_LAYER_FRONT;
        try { layer = layer_value_from_tlayer(m_tlayer); } catch (...) {}
        backend->add_sprite(arg_spritegrp, sprite_self(this), layer);
    }
}

void Curtain::update_scale()
{
    if (!m_target) return;
    const auto target_rect = m_target->m_rect;
    auto image = make_surface(target_rect.width, target_rect.height,
        {static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(m_color_typed), 0, 255)),
         static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(m_color_typed), 0, 255)),
         static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(m_color_typed), 0, 255)),
         static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(m_color_typed), 0, 255))});
    if (m_cutter_surface) {
        const auto [offset_x, offset_y] = m_cutter_pos;
        for (I y = 0; y < image->size.height; ++y) {
            for (I x = 0; x < image->size.width; ++x) {
                const I sx = x - offset_x, sy = y - offset_y;
                if (sx < 0 || sy < 0 || sx >= m_cutter_surface->size.width ||
                    sy >= m_cutter_surface->size.height) continue;
                const auto source_index = static_cast<std::size_t>(
                    (sy * m_cutter_surface->size.width + sx) * 4);
                const auto target_index = static_cast<std::size_t>(
                    (y * image->size.width + x) * 4);
                if (source_index + 3 >= m_cutter_surface->rgba.size() ||
                    target_index + 3 >= image->rgba.size()) continue;
                for (int channel = 0; channel < 4; ++channel)
                    image->rgba[target_index + channel] = static_cast<std::uint8_t>(
                        image->rgba[target_index + channel] > m_cutter_surface->rgba[source_index + channel]
                            ? image->rgba[target_index + channel] - m_cutter_surface->rgba[source_index + channel]
                            : 0);
            }
        }
    }
    // Python と同様、切り抜きの減算後に対象のアルファマ
    // スクを適用する。 重なる前景セルの有無とは独立して行う。
    {
        if (const auto mask = create_mask(); mask && *mask) {
            for (std::size_t i = 0; i + 3 < image->rgba.size() && i + 3 < (*mask)->rgba.size(); i += 4)
                image->rgba[i + 3] = std::min(image->rgba[i + 3], (*mask)->rgba[i + 3]);
        }
    }
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    ::cw::sprite::base::CWPySprite::m_image = image;
    ::cw::sprite::base::CWPySprite::m_rect = target_rect;
    m_rect = V("curtain-rect");
}

std::optional<std::shared_ptr<::cw::lib::Surface>> Curtain::create_mask()
{
    if (!m_target) return std::nullopt;
    std::shared_ptr<::cw::lib::Surface> source = m_target->m_image;
    if (!source) return std::nullopt;
    // 画像全体の不透明度は、Py が調べるピクセル単位のアルファマスクの代わりにはならない。
    if (!source->has_pixel_alpha) {
        if (!dynamic_cast<BgCell*>(m_target.get())) return std::nullopt;
        return ::cw::lib::convert_surface_alpha(source);
    }
    return std::make_shared<::cw::lib::Surface>(*source);
}

void Curtain::rclick_event()
{
    if (::cw::g_cwpy) ::cw::g_cwpy->cancel_cardcontrol();
}

bool Curtain::is_selection()
{
    if (!value_bool(m__is_selectable, true)) return false;
    return ::cw::sprite::base::SelectableSprite::is_selection();
}

void BattleCardImage::initialize()
{
    ::cw::sprite::card::CWPyCard::initialize("hidden", std::nullopt);
    m_highspeed = false; m_zoomsize_noscale = {56, 80};
    std::shared_ptr<::cw::lib::Surface> image;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto requested = ::cw::lib::encoding::path_to_utf8(
            native_path(::cw::g_cwpy->m_rsrc->m_skindir) /
            "Resource/Image/Card/BATTLE");
        const auto path = ::cw::util::find_resource(requested, ::cw::g_cwpy->m_rsrc->m_ext_img);
        if (!path.empty()) {
            try {
                // Python と同様、CardImage([ImageInfo(path)],
                // "NORMAL", ...) を使う。 通常カードの枠を
                // 合成し、入力の透過色・アルファマスクを適用して、倍率別素材
                // を一度だけ選ぶ。 BATTLE を直接読むと背景が不透明に
                // なり、拡縮済みの入力を未拡縮のカードとして扱ってしまう。
                auto info = std::make_shared<::cw::image::ImageInfo>();
                info->initialize(path, 0, std::nullopt, "Default", std::nullopt);
                ::cw::image::CardImage card_image;
                card_image.initialize({info}, "NORMAL", "", "", V(true),
                                      false, "", false, {-1, -1});
                image = card_image.get_image();
            } catch (...) {
                image.reset();
            }
        }
    }
    if (!image || image->size.width <= 0 || image->size.height <= 0)
        image = make_surface(80, 110, {40, 40, 40, 255});
    m__image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    m_image = V(std::static_pointer_cast<::cw::lib::Object>(image));
    m_image_unzoomed = m_image;
    m__rect = V("battle-rect"); m_rect = V("battle-rect");
    ::cw::sprite::card::CWPyCard::m__image = image;
    ::cw::sprite::card::CWPyCard::m__rect = {0, 0, image->size.width, image->size.height};
    ::cw::sprite::card::CWPyCard::m_image = image;
    ::cw::sprite::base::CWPySprite::m_image = image;
    ::cw::sprite::base::CWPySprite::m_rect = {0, 0, image->size.width, image->size.height};
    ::cw::sprite::card::CWPyCard::set_pos_noscale(std::nullopt, std::make_tuple(I{316}, I{142}));
    ::cw::sprite::card::CWPyCard::clear_image(false);
    m_tlayer = {value_int(std::get<0>(::cw::g_LAYER_BATTLE_START)),
                std::get<1>(::cw::g_LAYER_BATTLE_START),
                std::get<2>(::cw::g_LAYER_BATTLE_START),
                std::get<3>(::cw::g_LAYER_BATTLE_START)};
    // CWPyCard はカードグループのプロキシー登録表を通し
    // て描画する。 背景用バックエンドへ登録してもそのプロキシー
    // が更新されず、戦闘開始カードが見えなくなるため使わない。
    ::cw::sprite::card::refresh_card_layer(*this);
}

void BattleCardImage::update_battlestart()
{
    m_highspeed = true;
    const auto self = std::shared_ptr<::cw::sprite::card::CWPyCard>(this, [](auto*) {});
    ::cw::animation::animate_sprite(self, "deal", true, false, false, false);
    ::cw::animation::animate_sprite(self, "hide", true, false, false, false);
    for (const auto size : {std::make_tuple(I{8}, I{12}), std::make_tuple(I{28}, I{40})}) {
        m_zoomsize_noscale = size;
        ::cw::animation::animate_sprite(self, "zoomin", true, false, false, false);
        ::cw::animation::animate_sprite(self, "deal", true, false, false, false);
        ::cw::animation::animate_sprite(self, "hide", true, false, false, false);
    }
    // 待機中は、表示アニメーションの最終フレームを原寸で表示したままにする。
    m_zoomsize_noscale = {56, 80};
    ::cw::animation::animate_sprite(self, "zoomin", true, false, false, false);
    ::cw::animation::animate_sprite(self, "deal", true, false, false, false);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const I waitrate = (::cw::g_cwpy->m_setting->get_dealspeed(false) + 1) * 4;
        ::cw::g_cwpy->wait_frame(waitrate, ::cw::g_cwpy->m_setting->m_can_skipanimation,
                                 std::nullopt, 0);
    }
    m_highspeed = false;
}

std::shared_ptr<::cw::lib::Object> BattleCardImage::update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent)
{
    (void)arg_update_statusimg; (void)arg_is_runningevent;
    auto old = std::make_shared<RectValue>(); old->rect = ::cw::sprite::base::CWPySprite::m_rect;
    old->rect = {std::min(old->rect.x, ::cw::sprite::base::CWPySprite::m_rect.x),
                 std::min(old->rect.y, ::cw::sprite::base::CWPySprite::m_rect.y),
                 std::max(old->rect.x + old->rect.width, ::cw::sprite::base::CWPySprite::m_rect.x +
                     ::cw::sprite::base::CWPySprite::m_rect.width) - std::min(old->rect.x, ::cw::sprite::base::CWPySprite::m_rect.x),
                 std::max(old->rect.y + old->rect.height, ::cw::sprite::base::CWPySprite::m_rect.y +
                     ::cw::sprite::base::CWPySprite::m_rect.height) - std::min(old->rect.y, ::cw::sprite::base::CWPySprite::m_rect.y)};
    return old;
}

void BattleCardImage::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (m_status == "battlestart") {
        update_battlestart();
        return;
    }
    ::cw::sprite::card::CWPyCard::update(arg_args, arg_kwargs);
}

void BattleCardImage::update_selection()
{
    // 戦闘開始演出は選択対象外。selectionリストへ追加しない。
}

void InuseCardImage::initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_user, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::String arg_status, bool arg_center, std::optional<::cw::lib::Integer> arg_alpha, bool arg_fore)
{
    ::cw::sprite::card::CWPyCard::initialize(arg_status, std::nullopt);
    m_user_typed = std::move(arg_user); m_header_typed = std::move(arg_header);
    if (m_header_typed) {
        if (const auto card_image = m_header_typed->get_cardimg())
            m__cardimg = card_image;
    }
    m_center_typed = arg_center; m_alpha_typed = arg_alpha; m_zoomsize_noscale = {48, 66};
    if (m_user_typed) m_user = V(std::static_pointer_cast<::cw::lib::Object>(m_user_typed));
    if (m_header_typed) m_header = V(std::static_pointer_cast<::cw::lib::Object>(m_header_typed));
    m_center = V(arg_center); m_alpha = arg_alpha ? V(*arg_alpha) : V{};
    update_scale();
    m_group = V("cardgrp");
    if (::cw::g_cwpy) {
        std::tuple<I, I, I, I> layer{
            value_int(std::get<0>(::cw::g_LAYER_FRONT_INUSECARD)),
            std::get<1>(::cw::g_LAYER_FRONT_INUSECARD),
            std::get<2>(::cw::g_LAYER_FRONT_INUSECARD),
            std::get<3>(::cw::g_LAYER_FRONT_INUSECARD)};
        if (m_user_typed && !m_center_typed && !arg_fore) {
            try {
                if (const auto friend_card = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(m_user_typed)) {
                    layer = {std::get<0>(friend_card->m_tlayer_t),
                             std::get<1>(friend_card->m_tlayer_t),
                             std::get<2>(friend_card->m_tlayer_t),
                             std::get<3>(friend_card->m_tlayer_t) + 1};
                } else {
                    layer = {std::get<0>(m_user_typed->m_tlayer),
                             std::get<1>(m_user_typed->m_tlayer),
                             std::get<2>(m_user_typed->m_tlayer),
                             std::get<3>(m_user_typed->m_tlayer) + 1};
                }
            } catch (...) {}
        }
        m_tlayer = layer;
        ::cw::sprite::card::refresh_card_layer(*this);
    }
}

void InuseCardImage::update_scale()
{
    std::shared_ptr<::cw::lib::Surface> image;
    // Python と同様、使用中の重ね合わせを作る前にダイア
    // ログ・ホバーの反転状態を消す。 戦闘へ持ち越すと表示カ
    // ードが変わり、別に生成された反転画像を選ぶ場合もある。
    if (m_header_typed) {
        m_header_typed->m_negaflag = false;
        // Py と同様、update_scale は現在のカード画像を読む。 アニメーションのフ
        // レームが進んだかの判断と、その後の再構築は update_videos が管理する。
        image = m_header_typed->get_cardimg_2();
    }
    // Python と同様、ここでは完全なカード画像を取得する
    // 。 画像がなければ通常・大型カードの外形を維持する。48
    // ×66 は拡大差分であり、カードの基本寸法ではない。
    if (!image) {
        const bool large = m_header_typed && m_header_typed->get_bgtype() == "LARGE";
        image = make_surface(large ? 95 : 80, large ? 130 : 110, {0, 0, 0, 0});
    }
    else image = std::make_shared<::cw::lib::Surface>(*image);
    const auto base_image = image;
    if (m_user_typed && !m_center_typed && m_user_typed->m_scale != 100)
        image = ::cw::image::zoomcard(image, m_user_typed->m_scale / 100.0);
    image->surface_alpha_enabled = m_alpha_typed.has_value();
    image->surface_alpha = static_cast<std::uint8_t>(m_alpha_typed
        ? std::clamp<I>(*m_alpha_typed, 0, 255) : 255);
    m_image = image;
    ::cw::sprite::card::CWPyCard::m__image = base_image;
    ::cw::sprite::card::CWPyCard::m__rect = {0, 0, image->size.width, image->size.height};
    ::cw::sprite::card::CWPyCard::m_rect = {0, 0, image->size.width, image->size.height};
    ::cw::sprite::base::CWPySprite::m_image = image;
    if (m_center_typed) {
        ::cw::sprite::card::CWPyCard::set_pos_noscale(
            std::nullopt, std::make_tuple(I{316}, I{142}));
    } else if (m_user_typed) {
        ::cw::sprite::card::CWPyCard::set_pos(std::nullopt, std::make_tuple(
            m_user_typed->m_rect.x + m_user_typed->m_rect.width / 2,
            m_user_typed->m_rect.y + m_user_typed->m_rect.height / 2));
    } else {
        // ネイティブの呼び出し側は所有者なしの重ね合わせを作れる。
        // 所有者付きと同じ位置決め経路で、その原点も同期する。
        ::cw::sprite::card::CWPyCard::set_pos(std::make_tuple(I{0}, I{0}), std::nullopt);
    }
    // 継承した描画・アニメーションの姿勢は、set_pos で同期済みである。
    if (::cw::sprite::card::CWPyCard::m_status == "hidden")
        ::cw::sprite::card::CWPyCard::clear_image(true);
}

std::shared_ptr<::cw::lib::Object> InuseCardImage::update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent)
{
    (void)arg_update_statusimg; (void)arg_is_runningevent;
    // Py と同様、アニメーション状態に関係なく重ね合わせを必ず再構築する。
    auto old = std::make_shared<RectValue>(); old->rect = ::cw::sprite::base::CWPySprite::m_rect;
    update_scale();
    const auto current = ::cw::sprite::base::CWPySprite::m_rect;
    old->rect = {std::min(old->rect.x, current.x), std::min(old->rect.y, current.y),
                 std::max(old->rect.x + old->rect.width, current.x + current.width) -
                     std::min(old->rect.x, current.x),
                 std::max(old->rect.y + old->rect.height, current.y + current.height) -
                     std::min(old->rect.y, current.y)};
    return old;
}

void InuseCardImage::update_selection()
{
    // 使用中カード画像は表示overlayであり、選択リストには入れない。
}

::cw::lib::Value g_CellData{};
::cw::lib::Value g_ColorCellData{};
::cw::lib::Value g_ImageCellData{};
::cw::lib::Value g_PCCellData{};
::cw::lib::Value g_TextCellData{};
::cw::lib::Value g__BlitData{};

void BackGround_update_scale_func()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background)
        ::cw::g_cwpy->m_background->update_scale();
}

::cw::lib::Value BackGround__reload_lambda_line_1021(::cw::lib::Value arg_item)
{
    // Python のクロージャーはセルのタプルの layer だけを書き換える。 compat::Value
    // に動的タプルがないため、型付き CellData では同じコピーを返す。
    if (const auto data = cell_data(arg_item)) {
        auto copy = std::make_shared<CellData>(*data);
        copy->layer = _add_splayerval(copy->layer);
        copy->comparison_key = cell_key(*copy);
        return make_value(copy);
    }
    return arg_item;
}

void BackGround__reload_clear_forgrounds()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        ::cw::g_cwpy->m_background->m_foregrounds.clear();
        ::cw::g_cwpy->m_background->m_foregroundlist.clear();
    }
}

::cw::lib::Integer _add_splayerval(::cw::lib::Integer arg_layer)
{
    bool splayer = false;
    if (::cw::g_cwpy) {
        splayer = ::cw::g_cwpy->m_background && ::cw::g_cwpy->m_background->m_curtain_all;
        if (!splayer) {
            for (const auto& value : {std::get<0>(::cw::g_AREAS_SP), std::get<1>(::cw::g_AREAS_SP),
                                       std::get<2>(::cw::g_AREAS_SP), std::get<3>(::cw::g_AREAS_SP),
                                       std::get<4>(::cw::g_AREAS_SP)}) {
                if (value_int(value, std::numeric_limits<I>::min()) == ::cw::g_cwpy->m_areaid) {
                    splayer = true; break;
                }
            }
        }
    }
    if (splayer && arg_layer != ::cw::g_LAYER_BACKGROUND) arg_layer += ::cw::g_LAYER_SP_LAYER;
    return arg_layer;
}

bool _equals_bgs(std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_bgs1, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_bgs2, bool arg_visibleonly)
{
    using Entry = std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>;
    const auto has_cell = [arg_visibleonly](const Entry& entry) {
        const auto& value = std::get<1>(entry);
        if (!value.has_value() || value->is_none()) return false;
        const auto data = cell_data(*value);
        return !arg_visibleonly || !data || data->visible;
    };
    std::size_t index1 = 0;
    std::size_t index2 = 0;
    while (true) {
        while (index1 < arg_bgs1.size() && !has_cell(arg_bgs1[index1])) ++index1;
        while (index2 < arg_bgs2.size() && !has_cell(arg_bgs2[index2])) ++index2;
        const bool end1 = index1 == arg_bgs1.size();
        const bool end2 = index2 == arg_bgs2.size();
        if (end1 || end2) return end1 && end2;

        const auto type1 = std::get<0>(arg_bgs1[index1]);
        const auto type2 = std::get<0>(arg_bgs2[index2]);
        if (type1 != type2) return false;
        const auto& value1 = *std::get<1>(arg_bgs1[index1]);
        const auto& value2 = *std::get<1>(arg_bgs2[index2]);
        const auto data1 = cell_data(value1);
        const auto data2 = cell_data(value2);

        if (data1 || data2) {
            if (!data1 || !data2 || !same_cell_data(type1, *data1, *data2)) return false;
        } else if (!value_equal(value1, value2)) {
            return false;
        }
        ++index1;
        ++index2;
    }
}

::cw::lib::Value _equals_bgs_lambda_line_1618(::cw::lib::Value arg_t)
{
    return arg_t;
}

::cw::lib::Value _equals_bgs_lambda_line_1619(::cw::lib::Value arg_t)
{
    return arg_t;
}

bool _equals_bgs_is_visible(std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>> arg_t)
{
    const auto& value = std::get<1>(arg_t);
    const auto data = value ? cell_data(*value) : nullptr;
    return data && data->visible;
}

std::shared_ptr<::cw::lib::Object> _draw_bgcell(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Value> arg_bgdata, std::optional<std::shared_ptr<::cw::lib::Object>> arg_allclip)
{
    auto rect_value = std::make_shared<RectValue>();
    if (!arg_surface) return rect_value;
    const I bgtype = std::get<0>(arg_bgdata);
    const V& value = std::get<1>(arg_bgdata);
    auto payload = blit_payload(value);
    auto image = payload ? payload->image : surface_value(value);
    auto cell = payload ? payload->cell : cell_data(value);
    if (cell) {
        rect_value->rect = {scaled_coordinate(cell->pos.x), scaled_coordinate(cell->pos.y),
                            image ? image->size.width : scaled_length(cell->size.width),
                            image ? image->size.height : scaled_length(cell->size.height)};
        if (!image) image = cell->type == g_BG_TEXT ? render_text_cell(cell) : placeholder_cell_surface(cell);
    } else if (image) {
        rect_value->rect = {0, 0, image->size.width, image->size.height};
    }
    const auto source_rect = rect_value->rect;
    std::optional<::cw::lib::Rect> clip;
    if (arg_allclip) {
        if (const auto clip_value = std::dynamic_pointer_cast<RectValue>(*arg_allclip)) {
            clip = clip_value->rect;
        } else if (const auto clip_value = std::dynamic_pointer_cast<::cw::RectValue>(*arg_allclip)) {
            clip = clip_value->rect;
        } else if (const auto clip_value = std::dynamic_pointer_cast<::cw::sprite::message::RectValue>(*arg_allclip)) {
            clip = clip_value->value;
        }
    }
    if (image) {
        const auto blend = cell && bgtype == g_BG_COLOR ? cell->blend : S("Normal");
        blit_surface_region(arg_surface, image, source_rect.x, source_rect.y, clip, blend);
    }
    return rect_value;
}

std::vector<std::shared_ptr<::cw::lib::Object>> layered_draw_ex(std::shared_ptr<::cw::lib::Object> arg_layered_updates, std::shared_ptr<::cw::lib::Surface> arg_surface)
{
    std::vector<std::shared_ptr<::cw::lib::Object>> rects;
    if (!arg_layered_updates || !arg_surface) return rects;
    if (const auto group = std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(arg_layered_updates)) {
        struct Entry { I layer{}; std::size_t order{}; std::shared_ptr<::cw::lib::Sprite> sprite{}; };
        std::vector<Entry> entries;
        entries.reserve(group->sprites.size());
        for (std::size_t index = 0; index < group->sprites.size(); ++index) {
            const auto& sprite = group->sprites[index];
            const auto found = sprite ? group->layers.find(sprite.get()) : group->layers.end();
            entries.push_back({found == group->layers.end() ? 0 : found->second, index, sprite});
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
            return left.layer != right.layer ? left.layer < right.layer : left.order < right.order;
        });
        for (const auto& entry : entries) {
            const auto& sprite = entry.sprite;
            auto* owner = sprite ? ::cw::sprite::base::layered_group_sprite_owner(sprite) : nullptr;
            if (!sprite || !sprite->visible || !sprite->image || (owner && owner->m_status == "hidden")) continue;
            bool drawn_as_bgcell = false;
            if (const auto background_owner = background_layer_sprite_owner(sprite)) {
                if (const auto bgcell = dynamic_cast<BgCell*>(background_owner)) {
                    _draw_bgcell(arg_surface, {value_int(bgcell->m_bgtype), bgcell->m_d}, std::nullopt);
                    auto rect = std::make_shared<RectValue>(); rect->rect = sprite->rect;
                    rects.push_back(rect);
                    drawn_as_bgcell = true;
                }
            }
            if (!drawn_as_bgcell) {
                blit_surface(arg_surface, sprite->image, sprite->rect.x, sprite->rect.y);
                auto rect = std::make_shared<RectValue>(); rect->rect = sprite->rect; rects.push_back(rect);
            }
        }
    }
    return rects;
}

std::vector<std::shared_ptr<::cw::lib::Sprite>> sprites_from_layer(std::shared_ptr<::cw::lib::Object> arg_group, I arg_layer)
{
    std::vector<std::shared_ptr<::cw::lib::Sprite>> result;
    const auto group = sprite_group(arg_group);
    if (!group) return result;
    for (const auto& sprite : group->sprites) {
        // pygame の get_sprites_from_layer と同様、表示状態
        // に関係なく所属を調べる。 非表示の一時スプライトも検索・再利用できるようにする。
        if (!sprite) continue;
        I sprite_layer = 0;
        bool found = false;
        if (const auto layer = group->layers.find(sprite.get()); layer != group->layers.end()) {
            sprite_layer = layer->second;
            found = true;
        }
        if (const auto layer = background_layer_sprite_layer(sprite)) {
            sprite_layer = *layer;
            found = true;
        }
        if (found && sprite_layer == arg_layer) result.push_back(sprite);
    }
    return result;
}

void main()
{
    // Python版mainもpass。モジュールimport時にゲーム起動などの副作用はない。
}

}

