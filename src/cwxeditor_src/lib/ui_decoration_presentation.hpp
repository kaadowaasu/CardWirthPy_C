#pragma once
#include "ui_chrome_presentation.hpp"
#include "ui_number_edit.hpp"

namespace cwxeditor::lib {
// 通常のUI装飾の画素描画処理。ラスタライズ側のアダプターは座標
// だけを受け渡し、コンポーネントの識別、有効・選択の規則、記号の
// 構築はここで扱う。カードや画像に指定された描画命令ではない。
enum class DecorationKind { FillRect, StrokeRect, LineStrip, Points };
struct DecorationPrimitive {
    DecorationKind kind;
    Color color;
    Rect bounds{};
    std::vector<Point> points;
};
using DecorationPresentation=std::vector<DecorationPrimitive>;
struct DecorationTextPresentation {
    std::string text;
    Rect bounds;
    TextAlignment alignment{TextAlignment::Start};
    Color color{24,24,24,255};
    float padding{};
    std::optional<AlignedTextLayout> text_layout;
};
// 値を持つコントロールの状態はアプリケーションの描画前に取得する。ラスタラ
// イズ側のアダプターは再生中に値、方向、有効状態、UI倍率を読み直さない。
[[nodiscard]] std::optional<DecorationPresentation> prepare_value_decoration(
    const Widget& widget, std::uint64_t now_ms = 0, bool focused = false);
[[nodiscard]] DecorationPresentation prepare_tab_header_decoration(
    Rect bounds, bool selected, bool hot);
[[nodiscard]] DecorationPresentation prepare_tab_navigation_decoration(Rect bounds);
[[nodiscard]] DecorationPresentation prepare_toggle_decoration(const ChromePresentation& view);
[[nodiscard]] DecorationPresentation prepare_chrome_decoration(const ChromePresentation& view, Rect bounds);
[[nodiscard]] DecorationPresentation prepare_number_decoration(const NumberEditLayout& geometry,
    bool increase_enabled,bool decrease_enabled,Color foreground);
[[nodiscard]] DecorationPresentation prepare_cursor_decoration(std::optional<Rect> cursor);
[[nodiscard]] DecorationPresentation prepare_image_frame_decoration(Rect bounds, bool recessed);
// ownerはスクロール対象のウィジェット。つかんだつまみは押
// 下色で描く（scroll_thumb_pressed参照）。
[[nodiscard]] DecorationPresentation prepare_scroll_decoration(const ItemScrollGeometry& scroll,
    const Widget* owner = nullptr);
// スクロールバーのつまみの、通常時と保持・ドラッグ中の塗り。
inline constexpr Color kScrollThumbColor{190,190,190,255};
inline constexpr Color kScrollThumbPressedColor{96,96,96,255};
} // namespace cwxeditor::lib
