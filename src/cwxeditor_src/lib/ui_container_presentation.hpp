#pragma once
#include "ui_actions.hpp"
#include "ui_chrome_presentation.hpp"
#include "ui_decoration_presentation.hpp"

namespace cwxeditor::lib {
struct ExpandItemPresentation {
    std::size_t index{};
    Rect bounds{}, header{}, body{}, arrow{}, title_bounds{};
    std::string title;
    WidgetStylePresentation style;
    bool open{};
    std::vector<WidgetPtr> children;
    DecorationPresentation body_decoration, decoration;
    std::vector<DecorationTextPresentation> labels;
};
struct ContainerPresentation {
    Rect viewport{};
    std::vector<WidgetPtr> children;
    std::optional<ItemScrollGeometry> scroll;
    std::vector<Rect> grips;
    bool expandable{};
    std::vector<ExpandItemPresentation> sections;
    DecorationPresentation decoration, scroll_decoration;
};
// 描画コールバックの実行前に、コンテナーの装飾と子の巡回情報を保持する。スクロ
// ールとヘッダーのヒット判定は、同じプラットフォーム非依存の座標情報を使う。
[[nodiscard]] ContainerPresentation prepare_container_presentation(const Container& container);
} // namespace cwxeditor::lib
