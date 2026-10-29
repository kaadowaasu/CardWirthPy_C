#pragma once

#include "cw/cw.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/lib/ui_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>

namespace cw::debug::retained_presentation {

// ALL仕様: ボタンの各辺は論理25px以上。DPIは描画・入力に共通のfinish()で適用する。
inline ::cw::lib::Integer button_extent(const ::cw::lib::Integer authored)
{
    return ::cw::dialog::is_all_mode() ? std::max<::cw::lib::Integer>(25, authored) : authored;
}

inline void mix(std::uint64_t& seed, const std::uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

inline void mix(std::uint64_t& seed, const ::cw::lib::String& value)
{
    mix(seed, static_cast<std::uint64_t>(std::hash<::cw::lib::String>{}(value)));
}

template<typename T>
inline void mix_value(std::uint64_t& seed, const T& value)
{
    mix(seed, static_cast<std::uint64_t>(std::hash<T>{}(value)));
}

inline std::shared_ptr<::cw::lib::Surface> layer(
    const ::cw::lib::Rect& rect,
    const ::cw::lib::Color color = {240, 240, 240, 255})
{
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = {std::max<::cw::lib::Integer>(1, rect.width),
                    std::max<::cw::lib::Integer>(1, rect.height)};
    const auto count = static_cast<std::size_t>(result->size.width) * result->size.height;
    result->rgba.resize(count * 4U);
    for (std::size_t index = 0; index < count; ++index) {
        result->rgba[index * 4U] = color.r;
        result->rgba[index * 4U + 1U] = color.g;
        result->rgba[index * 4U + 2U] = color.b;
        result->rgba[index * 4U + 3U] = color.a;
    }
    return result;
}

// 出力解像度のダイアログ全体のフレーム。dpi_scale() で作った ScaledRetainedCanvas
// と同じサイズ・倍率を使う。UI の基本描画処理には論理座標を渡
// し、composite_changed() が等倍で切り取る。
inline ::cw::lib::Real dpi_scale();
inline std::shared_ptr<::cw::lib::Surface> scaled_frame(
    const ::cw::lib::Integer width, const ::cw::lib::Integer height,
    const ::cw::lib::Color color = {245, 245, 245, 255}, ::cw::lib::Real scale = 0.0)
{
    if (!(scale > 0.0)) scale = dpi_scale();
    scale = std::max<::cw::lib::Real>(1.0, scale);
    auto result = layer({0, 0,
        static_cast<::cw::lib::Integer>(std::lround(width * scale)),
        static_cast<::cw::lib::Integer>(std::lround(height * scale))}, color);
    result->draw_scale = scale;
    return result;
}

inline std::shared_ptr<::cw::lib::Surface> crop(
    const std::shared_ptr<::cw::lib::Surface>& source,
    const ::cw::lib::Rect& rect,
    const ::cw::lib::Color background = {240, 240, 240, 255})
{
    auto result = layer({0, 0, rect.width, rect.height}, background);
    if (!source || source->size.width <= 0 || source->size.height <= 0) return result;
    const auto left = std::clamp<::cw::lib::Integer>(rect.x, 0, source->size.width);
    const auto top = std::clamp<::cw::lib::Integer>(rect.y, 0, source->size.height);
    const auto right = std::clamp<::cw::lib::Integer>(rect.x + rect.width, 0, source->size.width);
    const auto bottom = std::clamp<::cw::lib::Integer>(rect.y + rect.height, 0, source->size.height);
    for (::cw::lib::Integer y = top; y < bottom; ++y) {
        for (::cw::lib::Integer x = left; x < right; ++x) {
            const auto source_index = (static_cast<std::size_t>(y) * source->size.width + x) * 4U;
            const auto target_index = (static_cast<std::size_t>(y - rect.y) * result->size.width +
                                       (x - rect.x)) * 4U;
            for (std::size_t channel = 0; channel < 4U; ++channel)
                result->rgba[target_index + channel] = source->rgba[source_index + channel];
        }
    }
    return result;
}

inline std::uint64_t surface_signature(
    const std::shared_ptr<::cw::lib::Surface>& source,
    const ::cw::lib::Rect& rect)
{
    // このコンポーネントだけを FNV-1a でハッシ
    // ュ化する。隣のデバッグペインから独立させ、その更
    // 新でデバッガー全体の転送が発生しないようにする。
    std::uint64_t value = 1469598103934665603ULL;
    mix(value, static_cast<std::uint64_t>(rect.width));
    mix(value, static_cast<std::uint64_t>(rect.height));
    if (!source || source->size.width <= 0 || source->size.height <= 0) return value;
    // rect は論理座標。出力解像度のフレーム（draw_scale
    // ）は、対応するデバイス画素をハッシュ化する。
    const auto scale = std::isfinite(source->draw_scale) && source->draw_scale > 0.0
        ? source->draw_scale : 1.0;
    const auto px = [scale](const ::cw::lib::Integer v) {
        return static_cast<::cw::lib::Integer>(std::lround(v * scale));
    };
    const auto left = std::clamp<::cw::lib::Integer>(px(rect.x), 0, source->size.width);
    const auto top = std::clamp<::cw::lib::Integer>(px(rect.y), 0, source->size.height);
    const auto right = std::clamp<::cw::lib::Integer>(px(rect.x + rect.width), 0, source->size.width);
    const auto bottom = std::clamp<::cw::lib::Integer>(px(rect.y + rect.height), 0, source->size.height);
    for (::cw::lib::Integer y = top; y < bottom; ++y) {
        auto offset = (static_cast<std::size_t>(y) * source->size.width + left) * 4U;
        const auto end = (static_cast<std::size_t>(y) * source->size.width + right) * 4U;
        for (; offset < end; ++offset) {
            value ^= source->rgba[offset];
            value *= 1099511628211ULL;
        }
    }
    return value;
}

inline bool intersects(const ::cw::lib::Rect& lhs, const ::cw::lib::Rect& rhs)
{
    return lhs.width > 0 && lhs.height > 0 && rhs.width > 0 && rhs.height > 0 &&
        lhs.x < rhs.x + rhs.width && rhs.x < lhs.x + lhs.width &&
        lhs.y < rhs.y + rhs.height && rhs.y < lhs.y + lhs.height;
}

// デバッガーのコントロールと一覧・ツリーの行は、領域から各文字・ア
// イコン・チェックボックスを中央に配置する。計算をここで共有し、計
// 測値の違いで同じ見た目のコントロールが一画素ずれないようにする。
inline ::cw::lib::Integer centered_offset(const ::cw::lib::Integer extent,
                                           const ::cw::lib::Integer content_extent)
{
    return std::max<::cw::lib::Integer>(0, (extent - content_extent) / 2);
}

inline ::cw::lib::Integer centered_y(const ::cw::lib::Rect& rect,
                                      const ::cw::lib::Integer content_height)
{
    return rect.y + centered_offset(rect.height, content_height);
}

inline ::cw::lib::Integer centered_x(const ::cw::lib::Rect& rect,
                                      const ::cw::lib::Integer content_width)
{
    return rect.x + centered_offset(rect.width, content_width);
}

inline void composite_changed(::cw::lib::ui::ScaledRetainedCanvas& canvas,
                              const std::shared_ptr<::cw::lib::Surface>& logical_frame,
                              const ::cw::lib::Rect& rect,
                              const bool changed)
{
    if (!changed) return;
    // canvas.layer() のフレームは出力解像度なので、画素単
    // 位で切り取り、等倍で合成する。鮮明な文字を再サンプリングしない。
    if (logical_frame && std::abs(logical_frame->draw_scale - canvas.scale) < 1e-6 &&
        std::abs(canvas.scale - 1.0) > 1e-6) {
        auto part = crop(logical_frame, canvas.rect(rect));
        part->draw_scale = canvas.scale;
        canvas.composite(rect, part);
        return;
    }
    canvas.composite(rect, crop(logical_frame, rect));
}

inline void offset_last_control(::cw::dialog::DialogPresentation& presentation,
                                const ::cw::lib::Rect& component)
{
    if (presentation.controls.empty()) return;
    presentation.controls.back().x += static_cast<float>(component.x);
    presentation.controls.back().y += static_cast<float>(component.y);
}

// デバッガー表示の出力倍率。通常はデスクトップ DPI、ゲームのダ
// イアログから編集画面を開いた場合はゲームウィンドウ倍率を使う。
inline ::cw::lib::Real presentation_scale(const bool game_window_scale)
{
    return game_window_scale ? std::max<::cw::lib::Real>(::cw::g_UP_WIN, 1.0) : dpi_scale();
}

inline void finish(::cw::dialog::DialogPresentation& presentation,
                   ::cw::lib::ui::ScaledRetainedCanvas& canvas,
                   const bool game_window_scale = false)
{
    canvas.finish();
    const auto scale = canvas.scale;
    if (scale > 1.0 + 1e-9) {
        for (auto& control : presentation.controls) {
            control.x = static_cast<float>(control.x * scale);
            control.y = static_cast<float>(control.y * scale);
            control.width = static_cast<float>(control.width * scale);
            control.height = static_cast<float>(control.height * scale);
        }
    }
    // ゲームが所有する編集画面は、デバッガー領域ではなくゲームのダイアログとして配置する。
    presentation.debugger = !game_window_scale;
    presentation.surface = canvas.surface;
    presentation.damage = canvas.retained.damage;
    presentation.surface_revision = canvas.retained.revision;
}

inline ::cw::lib::Real dpi_scale()
{
    return std::max<::cw::lib::Real>(static_cast<::cw::lib::Real>(::cw::g_dpi_level), 1.0);
}

} // namespace cw::debug::retained_presentation
