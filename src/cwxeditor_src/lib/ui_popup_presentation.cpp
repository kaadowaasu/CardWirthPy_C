#include "ui_popup_presentation.hpp"
#include "ui_metrics.hpp"
#include <algorithm>

namespace cwxeditor::lib {
namespace {
constexpr Color text_color{24,24,24,255},disabled_color{130,130,130,255};
constexpr Color selection_text_color{255,255,255,255};
constexpr Color selection_color{49,106,197,255},border_color{145,145,145,255};
}
PopupPresentation prepare_combo_presentation(const ComboBox& combo,
    const ComboPopupLayout& geometry,std::size_t top,std::size_t highlighted) {
    PopupPresentation result{geometry.bounds,{255,255,255,255},border_color,{}};
    result.font=effective_editor_widget_font(combo.font() ? &*combo.font() : nullptr);
    if(top>=combo.items.size()) return result;
    const auto count=std::min(geometry.visible_rows,combo.items.size()-top);
    if (const auto thumb = combo_popup_scroll_thumb(geometry, combo.items.size(), top)) {
        result.scroll_track = geometry.scroll_track;
        result.scroll_thumb = thumb;
    }
    const float padding=scaled_ui_metric(5);
    const float icon_size=scaled_ui_metric(16);
    const float icon_gap=scaled_ui_metric(1);
    for(std::size_t offset=0;offset<count;++offset) {
        const auto index=top+offset;
        const auto& item=combo.items[index];
        const auto bounds=geometry.row_bounds(offset);
        PopupRowPresentation row; row.index=index; row.bounds=bounds;
        if(index==highlighted) row.highlight=item.enabled ? selection_color : Color{226,226,226,255};
        const auto color = item.enabled && index == highlighted
            ? selection_text_color
            : (item.enabled ? text_color : disabled_color);
        float text_x=bounds.x+padding;
        if(item.icon) {
            row.icon=item.icon;
            row.icon_bounds={text_x,bounds.y+std::max(0.0F,(bounds.height-icon_size)*.5F),
                             icon_size,icon_size};
            text_x+=icon_size+icon_gap;
        }
        const float label_right = geometry.action_widths.empty()
            ? bounds.x + bounds.width : geometry.action_bounds(offset, 0).x;
        row.text.push_back({item.text,{text_x,bounds.y,
            std::max(0.0F,label_right-padding-text_x),bounds.height},TextAlignment::Start,
            color});
        for (std::size_t action = 0; action < geometry.action_widths.size() &&
                                      action < combo.popup_actions.size(); ++action) {
            const auto button = geometry.action_bounds(offset, action);
            row.buttons.push_back(button);
            row.text.push_back({combo.popup_actions[action], button, TextAlignment::Center,
                item.enabled ? text_color : disabled_color});
        }
        result.rows.push_back(std::move(row));
    }
    return result;
}
PopupPresentation prepare_menu_presentation(const Menu& menu,
    const MenuPopupLayout& geometry,std::optional<std::size_t> selected) {
    PopupPresentation result{geometry.bounds,{250,250,250,255},border_color,{}};
    result.font=effective_editor_widget_font(menu.font() ? &*menu.font() : nullptr);
    for(const auto& part:geometry.rows) {
        if(part.index>=menu.items.size()) continue;
        const auto& item=menu.items[part.index];
        PopupRowPresentation row; row.index=part.index; row.bounds=part.bounds;
        if(selected==part.index && item.enabled && !item.separator) row.highlight=selection_color;
        if(item.separator) {
            row.separator=Rect{part.bounds.x,part.bounds.y+part.bounds.height*.5F,part.bounds.width,1};
        } else {
            const auto color = item.enabled && selected == part.index
                ? selection_text_color
                : (item.enabled ? text_color : disabled_color);
            if(item.checked) row.text.push_back({item.radio ? "●":"✓",part.check,TextAlignment::Center,color});
            if(item.icon && part.icon.width>0) { row.icon=item.icon; row.icon_bounds=part.icon; }
            row.text.push_back({menu_item_label(item),part.label,TextAlignment::Start,color});
            if(!menu_item_shortcut(item).empty()) row.text.push_back({std::string(menu_item_shortcut(item)),part.shortcut,TextAlignment::End,color});
            if(item.has_submenu()) row.text.push_back({"▶",part.arrow,TextAlignment::Center,color});
        }
        result.rows.push_back(std::move(row));
    }
    return result;
}
} // namespace cwxeditor::lib
