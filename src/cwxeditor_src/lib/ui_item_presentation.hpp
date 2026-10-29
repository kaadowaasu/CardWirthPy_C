#pragma once
#include "ui_actions.hpp"
#include "ui_layout.hpp"
#include "ui_decoration_presentation.hpp"

#include <string_view>

namespace cwxeditor::lib {
// 可視行は可変モデルへの参照ではなく、値のコピー。座標情報は
// 選択、スクロール、インライン編集のヒット判定と共有する。
struct ItemCellPresentation {
    std::string text;
    Rect text_bounds{}, icon_bounds{}, trailing_icon_bounds{};
    ResourceId icon{}, trailing_icon{};
    TextAlignment alignment{TextAlignment::Start};
    float text_padding{};
    bool enabled{true}, inline_editor{}, check_grayed{};
    std::optional<Rect> grid, check_box, check_mark;
    DecorationPresentation decoration;
    Color foreground{24,24,24,255};
    std::optional<AlignedTextLayout> text_layout;
};
struct ItemRowPresentation {
    std::size_t model_index{};
    TreeItemId item_id{};
    std::optional<Rect> selection, expander, warning_bounds, comment_bounds;
    bool expanded{};
    ResourceId warning_icon{}, comment_icon{};
    std::string warning_text, comment_text;
    std::vector<ItemCellPresentation> cells;
    DecorationPresentation decoration;
    std::optional<DecorationTextPresentation> expander_label;
    std::optional<DecorationTextPresentation> warning_label;
};
struct ItemHeaderPresentation {
    std::string text;
    Rect bounds{}, divider{};
    TextAlignment alignment{TextAlignment::Start};
    DecorationPresentation decoration;
    Color foreground{24,24,24,255};
    float text_padding{5};
    std::optional<AlignedTextLayout> text_layout;
};
struct ItemViewPresentation {
    Rect bounds{};
    ItemScrollGeometry scroll;
    std::optional<Rect> highlight, cursor;
    std::vector<ItemHeaderPresentation> headers;
    std::vector<ItemRowPresentation> rows;
    DecorationPresentation decoration, content_decoration, cursor_decoration, scroll_decoration;
};
// 呼び出し側が先にモデルのスクロールとインライン状態を整合させる。この関数は読み
// 取りだけを行う。フレームの準備でアプリケーションのコールバックを発生させない。
[[nodiscard]] ItemViewPresentation prepare_item_view(
    const WidgetPtr& view, float row_height,
    const InlineEditStateMachine* inline_edit = nullptr, bool focused = false,
    std::optional<std::size_t> hovered_index = std::nullopt);
// ポインターが準備済みの目印の矩形内にある場合だけ、所有元の行のコメ
// ントヘルプを返す。共通フレーム表示側は行の警告より先にこれを使う。
[[nodiscard]] std::string item_comment_tooltip(
    const WidgetPtr& view, Point pointer,
    const ItemViewPresentation& presentation);

struct TabItemPresentation {
    std::size_t index{};
    Rect bounds{}, close{}, text_bounds{}, icon_bounds{};
    ResourceId icon{};
    std::string title;
    bool selected{}, hot{};
    DecorationPresentation decoration;
    std::vector<DecorationTextPresentation> labels;
};
struct TabNavigationPresentation {
    Rect bounds;
    DecorationPresentation decoration;
    DecorationTextPresentation label;
};
struct TabViewPresentation {
    Rect bounds{}, content{}, baseline{};
    TabHeaderLayout geometry;
    std::vector<TabItemPresentation> headers;
    WidgetPtr selected_page;
    DecorationPresentation decoration;
    std::vector<TabNavigationPresentation> navigation;
};
[[nodiscard]] TabViewPresentation prepare_tab_view(const TabView& tabs,
    bool hovered = false, Point pointer = {});
} // namespace cwxeditor::lib
