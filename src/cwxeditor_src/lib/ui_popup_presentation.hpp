#pragma once
#include "ui_layout.hpp"
#include "ui_text_layout.hpp"

namespace cwxeditor::lib {
struct PopupTextPresentation {
    std::string text;
    Rect bounds;
    TextAlignment alignment{TextAlignment::Start};
    Color color;
    std::optional<AlignedTextLayout> text_layout;
};
struct PopupRowPresentation {
    std::size_t index{};
    Rect bounds;
    std::optional<Color> highlight;
    std::optional<Rect> separator;
    ResourceId icon{};
    Rect icon_bounds;
    std::vector<PopupTextPresentation> text;
    std::vector<Rect> buttons;
};
// 描画時までコマンド、コールバック、借用したMenuItemのデータを残さない。
struct PopupPresentation {
    Rect bounds;
    Color background, border;
    std::vector<PopupRowPresentation> rows;
    std::optional<Rect> scroll_track;
    std::optional<Rect> scroll_thumb;
    // ポインターでつまみをドラッグ中。押下色で描く。
    bool scroll_thumb_pressed{};
    // 取得済みの実効フォント。ポップアップ描画中に、変更された所有元を読み直さない。
    std::optional<FontSpec> font;
};
[[nodiscard]] PopupPresentation prepare_combo_presentation(const ComboBox& combo,
    const ComboPopupLayout& geometry,std::size_t top,std::size_t highlighted);
[[nodiscard]] PopupPresentation prepare_menu_presentation(const Menu& menu,
    const MenuPopupLayout& geometry,std::optional<std::size_t> selected);
} // namespace cwxeditor::lib
