#pragma once
#include "components.hpp"
#include "window_host.hpp"
#include "ui_text_layout.hpp"

namespace cwxeditor::lib {
struct WindowCaptionPresentation {
    HostedCaptionLayout geometry;
    std::string title;
    Point title_origin;
    Rect title_bounds;
    FontSpec font;
    std::string close_text{"×"};
    std::string minimize_text{"－"};
    std::optional<AlignedTextLayout> minimize_layout;
    // タイトル部の元に戻す・やり直しボタン（HostedCaptionLayout::undo_button参照）。
    std::string undo_text{"元に戻す"};
    std::string redo_text{"やり直し"};
    std::optional<AlignedTextLayout> undo_layout;
    std::optional<AlignedTextLayout> redo_layout;
    ResourceId undo_icon{}, redo_icon{};
    Rect undo_icon_bounds, redo_icon_bounds;
    Color background{250,250,250,255},border{205,205,205,255};
    Color foreground{24,24,24,255};
    std::optional<AlignedTextLayout> title_layout;
    std::optional<AlignedTextLayout> close_layout;
    ResourceId icon{};
    Rect icon_bounds;
};
struct WindowChromePresentation {
    Rect bounds,content;
    Color background{245,245,245,255};
    bool clear_surface{};
    bool transparent_background{};
    std::uint8_t opacity{255};
    std::optional<WindowCaptionPresentation> caption;
    float boundary_width{};
    Color boundary_color{139,90,43,255};
};
[[nodiscard]] WindowChromePresentation prepare_window_chrome(
    const Window& window,Rect bounds,bool embedded,bool show_caption);
// 準備済みの一フレーム用の、変更不可の通常ウィジェットのスタイル。指定さ
// れたCanvasフォントや画像資源は、このUI文字の契約には含めない。
struct WidgetStylePresentation {
    Rect bounds;
    Color foreground;
    std::optional<Color> background;
    std::optional<FontSpec> font;
    ComponentKind kind{ComponentKind::Panel};
};
struct ChromeInteraction {
    bool hovered{}, pressed{}, focused{};
};
struct ChromePresentation {
    std::string text;
    Rect text_bounds;
    TextAlignment alignment{TextAlignment::Start};
    float text_padding{};
    std::optional<Point> title_origin;
    std::optional<Color> fill;
    Color border_color{145,145,145,255}, mark_color{49,106,197,255};
    std::optional<Color> mark_fill, foreground;
    std::optional<Rect> focus_bounds;
    bool default_button{}, cancel_button{};
    bool border{}, radio{};
    std::optional<Rect> mark, checked_mark;
    ResourceId icon{};
    Rect icon_bounds;
    std::optional<AlignedTextLayout> text_layout;
    bool multiline{}, wrap{};
    std::optional<LabelTextPresentation> paragraph;
};
[[nodiscard]] WidgetStylePresentation prepare_widget_style(const Widget& widget);
[[nodiscard]] std::optional<ChromePresentation> prepare_widget_chrome(const Widget& widget,ChromeInteraction interaction={});
} // namespace cwxeditor::lib
