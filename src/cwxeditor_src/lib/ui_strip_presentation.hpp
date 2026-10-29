#pragma once
#include "ui_text_layout.hpp"
#include "ui_decoration_presentation.hpp"
#include "ui_pointer_feedback.hpp"

namespace cwxeditor::lib {
struct StripItemPresentation {
    std::size_t model_index{};
    Rect bounds, text_bounds, icon_bounds;
    std::string text;
    ResourceId icon{};
    Color foreground;
    std::optional<Color> highlight;
    bool highlight_border{}, top_aligned{}, enabled{true};
    TextAlignment text_alignment{TextAlignment::Center};
    float text_padding{};
    std::optional<Rect> separator, drop_down;
    DecorationPresentation decoration;
    std::optional<DecorationTextPresentation> arrow;
    std::optional<AlignedTextLayout> text_layout;
};
struct StripPresentation {
    Rect bounds, baseline;
    Color background;
    std::vector<StripItemPresentation> items;
    DecorationPresentation decoration, overlay;
};
// ヒット判定とコマンド実行と同じ帯状領域の座標情報を使う。これらの
// 関数はon_open、選択、コマンドのコールバックを呼ばない。
[[nodiscard]] StripPresentation prepare_menu_bar(const Menu& menu,
    std::optional<std::size_t> selected={},bool hovered=false,Point pointer={});
[[nodiscard]] StripPresentation prepare_tool_bar(const ToolBar& toolbar,
    bool hovered=false,Point pointer={},const std::vector<PointerPressFeedback>& presses={},
    bool keyboard_focused=false);
struct TooltipLinePresentation { Point origin; std::string text; };
struct TooltipTitlePresentation {
    std::string text;
    ResourceId icon{};
    Rect icon_bounds, text_clip;
    Point origin;
    FontSpec font;
};
struct TooltipPresentation {
    Rect bounds;
    Rect body;
    bool balloon{};
    std::optional<TooltipTitlePresentation> title;
    std::vector<TooltipLinePresentation> lines;
    bool truncated{};
    Color background{255,255,225,255}, border{90,90,90,255}, foreground{24,24,24,255};
    FontSpec font;
    DecorationPresentation decoration;
};
// 改行、UTF-8の折り返し、画面端での配置はプラットフォーム非依
// 存。バックエンドは既定UIフォントのグリフ計測だけを提供する。
[[nodiscard]] std::optional<TooltipPresentation> prepare_tooltip(
    std::string_view text,Point pointer,Rect available,float line_height,
    const TextWidthMeasure& measure,bool balloon=false,float configured_width=0.0F);
// Dのツールバーの説明ヘルプは、項目画像と太字の一行タイトルを使う。説明文
// だけをtoolTipWidthで折り返し、項目の位置を基準に配置する。
[[nodiscard]] std::optional<TooltipPresentation> prepare_toolbar_description_tooltip(
    std::string_view title,std::string_view body,ResourceId icon,Rect anchor,
    Rect available,float line_height,const TextWidthMeasure& measure,
    float configured_width=0.0F);
} // namespace cwxeditor::lib
