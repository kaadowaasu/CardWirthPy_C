#include "ui_activation.hpp"
#include "ui_metrics.hpp"
#include "ui_inline_edit.hpp"
#include "ui_edit_preferences.hpp"
#include "window_host.hpp"

namespace cwxeditor::lib {
bool activate_color_selection(WidgetPtr root, std::shared_ptr<ColorWell> well,
    std::function<std::optional<Color>(Color)> pick_color, std::function<bool()> live) {
    const auto valid=[&] {
        return (!live || live()) && widget_in_input_scope(root,well);
    };
    if (!pick_color || !valid()) return false;
    const auto initial=well->value;
    const auto selected=pick_color(initial);
    if (selected && valid() && well->value.red==initial.red &&
        well->value.green==initial.green && well->value.blue==initial.blue &&
        well->value.alpha==initial.alpha)
        (void)set_color_value(*well,*selected);
    return true;
}

bool activate_color_selection_async(WidgetPtr root, std::shared_ptr<ColorWell> well,
    AsyncColorPicker pick_color, std::function<bool()> live) {
    if (!pick_color || !well) return false;
    if ((live && !live()) || !widget_in_input_scope(root,well)) return false;
    const auto initial=well->value;
    std::weak_ptr<Widget> weak_root=root;
    std::weak_ptr<ColorWell> weak_well=well;
    return pick_color(initial,[weak_root,weak_well,initial,live](std::optional<Color> selected) {
        const auto current=weak_well.lock();
        const auto scope=weak_root.lock();
        if (!selected || !current || !scope) return;
        if ((live && !live()) || !widget_in_input_scope(scope,current)) return;
        if (current->value.red==initial.red && current->value.green==initial.green &&
            current->value.blue==initial.blue && current->value.alpha==initial.alpha)
            (void)set_color_value(*current,*selected);
    });
}

void activate_control(const WidgetPtr& root, const WidgetPtr& widget,
                      Point point, Modifiers modifiers, ControlActivationContext& context,
                      std::function<bool()> live, const PointerButton button,
                      const std::optional<bool> physical_control,
                      const bool suppress_default_item_view_selection) {
    const auto valid=[&] { return (!live || live()) && widget_in_input_scope(root,widget); };
    const auto open_combo=[&](const std::shared_ptr<ComboBox>& combo) {
        const auto rows = combo && combo->visible_item_count != 0U
            ? combo->visible_item_count : context.combo_visible_rows;
        (void)context.combo_popup.open(combo, rows);
    };
    const auto select_text=[&](const std::shared_ptr<TextEdit>& edit) {
        if (context.text_selection) context.text_selection(edit,point,modifiers);
    };
    if (!valid()) return;
    if (context.inline_edit.active()) {
        if (widget==context.inline_edit.owner() && context.inline_edit.layout(context.row_height)) {
            if (const auto stepper = prepare_inline_number_stepper(context.inline_edit, context.row_height)) {
                if (const int direction = stepper->geometry.step_at(point)) {
                    (void)step_inline_number(context.inline_edit, direction);
                    return;
                }
            }
            const auto combo=context.inline_edit.choice();
            if (combo && point_inside(combo->bounds(),point) &&
                !layout_combo_box(*combo).edits_text(*combo,point)) {
                open_combo(combo);
                return;
            }
            if (point_inside(context.inline_edit.editor()->bounds(),point)) {
                select_text(context.inline_edit.editor());
                context.inline_edit.publish_choice_selection();
                return;
            }
        }
        if (context.combo_popup.is_open_for(context.inline_edit.choice())) context.combo_popup.close();
        (void)context.inline_edit.commit();
    }
    if (!valid()) return;
    // NumberEdit の増減ボタンの領域は、編集可能なテキスト欄
    // ではなく操作ボタン。 汎用のフォーカス処理より先に扱い、Android
    // で増減を押しただけでソフトキーボードを開かないようにする。
    if (const auto number=std::dynamic_pointer_cast<NumberEdit>(widget)) {
        if (const auto direction=layout_number_edit(*number).step_at(point)) {
            context.number_edit.begin(number);
            if (!valid()) return;
            (void)context.number_edit.step(number,direction);
            return;
        }
    }
    if (context.focus && !context.focus(widget)) return;
    if (!valid()) return;
    if (const auto edit=std::dynamic_pointer_cast<TextEdit>(widget)) {
        select_text(edit);
        return;
    }
    if (const auto menu=std::dynamic_pointer_cast<Menu>(widget)) {
        if (const auto index=hit_test_menu_bar_item(*menu,point)) {
            context.combo_popup.close();
            (void)context.menu_popup.open_menu_bar_item(menu,*index,widget,{modifiers,button,physical_control});
        }
        return;
    }
    if (const auto toolbar=std::dynamic_pointer_cast<ToolBar>(widget)) {
        if (const auto index=hit_test_tool_bar_item(*toolbar,point)) {
            const auto& item=toolbar->items[*index];
            const auto geometry=tool_bar_item_geometry(*toolbar)[*index];
            if (item.enabled) {
                if (item.drop_down && item.drop_down_menu && point_inside(geometry.drop_down,point)) {
                    context.combo_popup.close();
                    context.menu_popup.open(item.drop_down_menu,widget,
                        {geometry.bounds.x,toolbar->bounds().y+toolbar->bounds().height});
                } else {
                    (void)activate_tool_item(*toolbar,*index,{modifiers,button,physical_control});
                }
            }
        }
        return;
    }
    if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
        if (activate_tab_header(*tabs,point) && valid() && context.relayout) context.relayout(tabs);
        return;
    }
    if (const auto button=std::dynamic_pointer_cast<Button>(widget)) {
        (void)activate_button_control(root,button);
        return;
    }
    if (const auto link=std::dynamic_pointer_cast<Link>(widget)) {
        (void)activate_link(*link);
        return;
    }
    if (const auto expand=std::dynamic_pointer_cast<ExpandBar>(widget)) {
        if (activate_expand_header(*expand,point) && valid() && context.relayout) context.relayout(expand);
        return;
    }
    if (const auto combo=std::dynamic_pointer_cast<ComboBox>(widget)) {
        if (layout_combo_box(*combo).edits_text(*combo,point)) {
            context.combo_popup.close();
            context.combo_edit.begin(combo);
            select_text(context.combo_edit.editor());
            context.combo_edit.publish_selection();
        } else if (context.combo_popup.is_open_for(combo)) {
            context.combo_popup.close();
        } else {
            open_combo(combo);
        }
        return;
    }
    if (const auto number=std::dynamic_pointer_cast<NumberEdit>(widget)) {
        context.number_edit.begin(number);
        if (!valid()) return;
        if (const auto direction=layout_number_edit(*number).step_at(point))
            (void)context.number_edit.step(number,direction);
        else select_text(context.number_edit.editor());
        return;
    }
    if (const auto slider=std::dynamic_pointer_cast<Slider>(widget)) {
        (void)set_slider_value_from_point(*slider,point);
        return;
    }
    if (const auto well=std::dynamic_pointer_cast<ColorWell>(widget)) {
        if (context.pick_color_async)
            (void)activate_color_selection_async(root,well,context.pick_color_async,live);
        else
            (void)activate_color_selection(root,well,context.pick_color,live);
        return;
    }
    if (!suppress_default_item_view_selection) {
        if (!valid()) return;
        const auto table = std::dynamic_pointer_cast<TableView>(widget);
        const SelectionInput selection_input{
            (static_cast<std::uint32_t>(modifiers) &
             static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Meta)) != 0,
            (static_cast<std::uint32_t>(modifiers) &
             static_cast<std::uint32_t>(Modifiers::Shift)) != 0};
        if (button == PointerButton::Primary && table &&
            table->edit_on_single_click) {
            (void)handle_item_view_single_click(widget, point,
                &context.inline_edit, modifiers, selection_input);
            if (context.inline_edit.active()) {
                context.number_edit.finish();
                if (!widget_in_input_scope(root, context.inline_edit.owner())) {
                    context.inline_edit.cancel();
                    return;
                }
                // 次の共通フレームで、D版と同じ範囲確認の後、 こ
                // の編集欄を表の公開の文字入力先として反映する。
                (void)context.inline_edit.layout(context.row_height);
            }
        } else {
            (void)handle_item_view_click(widget, point, selection_input);
        }
    }
}

void activate_control_double(const WidgetPtr& root, const WidgetPtr& widget,
                             Point point, ControlActivationContext& context,
                             const Modifiers modifiers) {
    if (!widget_in_input_scope(root,widget)) return;
    (void)handle_item_view_double_click(widget,point,&context.inline_edit,modifiers);
    if (!context.inline_edit.active()) return;
    context.number_edit.finish();
    if (!widget_in_input_scope(root,context.inline_edit.owner())) {
        context.inline_edit.cancel();
        return;
    }
    (void)context.inline_edit.layout(context.row_height);
    // D版の EditEnd.setFocus と同様、すべての編集状況で comboListVisible
    // を適用する。 ALL も同じ共有設定を使い、ホストモードで選択を上書きしない。
    if (combo_edit_popup_enabled()) {
        if (const auto combo = context.inline_edit.choice()) {
            const auto rows = combo->visible_item_count != 0U
                ? combo->visible_item_count : context.combo_visible_rows;
            (void)context.combo_popup.open(combo, rows);
        }
    }
}
} // namespace cwxeditor::lib
