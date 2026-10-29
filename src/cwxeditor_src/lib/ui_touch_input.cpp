#include "ui_touch_input.hpp"
#include "ui_inline_edit.hpp"
#include "ui_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace cwxeditor::lib {

namespace {

// D版のマウス動作への意図的なタッチ操作の拡張として、印の周囲4論理画
// 素まで許可する。 範囲は行・コントロール内に制限し、描画は変えない。
std::function<bool()> prepare_check_touch(const WidgetPtr& widget, const Point point,
                                        const WidgetPtr& scope) {
    if (!widget || !widget->enabled() || !widget->visible()) return {};
    const auto near_mark = [&](const Rect mark, const Rect limit) {
        const float margin = scaled_ui_metric(4);
        return point_inside(limit, point) && point_inside(widget->bounds(), point) &&
            point_inside({mark.x-margin, mark.y-margin,
                          mark.width+2*margin, mark.height+2*margin}, point);
    };
    if (widget->kind() == ComponentKind::CheckBox ||
        widget->kind() == ComponentKind::RadioButton) {
        const auto button = std::dynamic_pointer_cast<Button>(widget);
        const auto bounds = widget->bounds();
        const float size = scaled_ui_metric(14);
        const Rect mark{bounds.x+scaled_ui_metric(5),
                        bounds.y+(bounds.height-size)*0.5F, size, size};
        if (button && near_mark(mark, bounds))
            return [scope, button] { return activate_button_control(scope, button); };
        return {};
    }
    const auto row = hit_test_item_view(widget, point);
    if (!row) return {};
    const float height = item_row_height(*widget, scaled_ui_metric(kEditorUiMetrics.row_height));
    if (const auto list = std::dynamic_pointer_cast<ListView>(widget)) {
        if (!list->checkable || !list->items[*row].enabled) return {};
        const float y = list->bounds().y + static_cast<float>(*row-list->top_index)*height;
        const auto mark = linear_row_check_box_geometry(list->bounds(), y, height,
            scaled_ui_metric(6), scaled_ui_metric(13));
        if (!mark || !near_mark(*mark, {list->bounds().x,y,list->bounds().width,height})) return {};
        const Point center{mark->x+mark->width*0.5F,mark->y+mark->height*0.5F};
        return [list, center, index=*row, order=list->item_order_revision, update=list->item_update_revision] {
            if (!list->checkable || list->item_order_revision!=order ||
                list->item_update_revision!=update || index>=list->items.size() ||
                !list->items[index].enabled) return false;
            if (hit_test_item_view(list,center)!=index) return false;
            return handle_item_view_click(list,center);
        };
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(widget)) {
        if (!table->checkable) return {};
        const auto rect = table_row_geometry(*table,*row,height);
        const auto mark = linear_row_check_box_geometry(
            {rect.x-table->horizontal_offset,table->bounds().y,rect.width,table->bounds().height},
            rect.y,rect.height,scaled_ui_metric(5),scaled_ui_metric(13));
        if (!mark || !near_mark(*mark,rect)) return {};
        const Point center{mark->x+mark->width*0.5F,mark->y+mark->height*0.5F};
        return [table, center, index=*row, order=table->row_order_revision, update=table->row_update_revision] {
            if (!table->checkable || table->row_order_revision!=order ||
                table->row_update_revision!=update || index>=table->rows.size()) return false;
            if (hit_test_item_view(table,center)!=index) return false;
            return handle_item_view_click(table,center);
        };
    }
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(widget)) {
        if (!tree->checkable) return {};
        const auto visible = visible_tree_indices(*tree);
        const auto position = std::ranges::find(visible,*row);
        if (position==visible.end()) return {};
        const float y = tree->bounds().y +
            static_cast<float>(position-visible.begin()-static_cast<std::ptrdiff_t>(tree->top_index))*height;
        const auto geometry = tree_row_geometry(*tree,tree->bounds(),*row,y,height);
        if (!geometry || !near_mark(geometry->check_box,geometry->row) ||
            (geometry->has_expander && point_inside(geometry->expander,point))) return {};
        const Point center{geometry->check_box.x+geometry->check_box.width*0.5F,
                           geometry->check_box.y+geometry->check_box.height*0.5F};
        return [tree, center, id=tree->nodes[*row].id, order=tree->node_order_revision,
                update=tree->node_update_revision] {
            if (!tree->checkable || tree->node_order_revision!=order ||
                tree->node_update_revision!=update) return false;
            const auto node=std::ranges::find(tree->nodes,id,&TreeNode::id);
            if (node==tree->nodes.end()) return false;
            if (hit_test_item_view(tree,center)!=static_cast<std::size_t>(node-tree->nodes.begin())) return false;
            return handle_item_view_click(tree,center);
        };
    }
    return {};
}

} // namespace

bool TouchInputStateMachine::scope_live(const Contact& contact) {
    const auto scope = contact.scope.lock();
    return scope && scope->visible() && scope->enabled() &&
        (!contact.live || contact.live()) && scope->visible() && scope->enabled();
}

bool TouchInputStateMachine::current(std::uint64_t pointer,
                                    const std::shared_ptr<Contact>& contact) const {
    const auto found = contacts_.find(pointer);
    return found != contacts_.end() && found->second == contact;
}

bool TouchInputStateMachine::owns_drag(const Contact& contact) {
    return contact.drag_id && contact.drag && contact.drag->session_id() == contact.drag_id;
}

std::shared_ptr<Window> TouchInputStateMachine::owner(std::uint64_t pointer) const {
    const auto found = contacts_.find(pointer);
    return found != contacts_.end() && scope_live(*found->second)
        ? found->second->scope.lock() : nullptr;
}

std::vector<PointerPressFeedback> TouchInputStateMachine::press_feedback(const std::shared_ptr<Window>& scope) const {
    std::vector<PointerPressFeedback> result;
    for (const auto& [_,contact]:contacts_) {
        if (contact->scope.lock()!=scope || !scope_live(*contact) || contact->finishing || contact->moved ||
            contact->long_pressed ||
            contact->menu_hit || contact->combo_hit || owns_drag(*contact) ||
            !widget_in_input_scope(scope,contact->hit)) continue;
        result.push_back({contact->hit,contact->motion.start(),contact->last.position});
    }
    return result;
}

void TouchInputStateMachine::cancel(std::uint64_t pointer,
                                   std::shared_ptr<Contact> contact, bool cancel_clicks,
                                   bool notify_neutralized, bool retire_snapshot, PointerCancelReason reason) {
    const bool owns_identity = current(pointer, contact);
    if (!contact || (!owns_identity && !retire_snapshot) ||
        (contact->finishing && !owns_identity)) return;
    // この識別情報そのものを一度だけ完了する。 準備したキャンセルが再利
    // 用されたポインターIDより長く残っても、置換先を消去・通知しない。
    contact->finishing = true;
    if (owns_identity) contacts_.erase(pointer);
    contact->number_repeat.cancel();
    if (cancel_clicks) clicks_.cancel_for(contact->scope.lock());
    std::exception_ptr failure;
    try {if (owns_drag(*contact)) contact->drag->cancel();}
    catch(...) {failure=std::current_exception();}
    try {
        if ((notify_neutralized || !contact->multi_touch) && !contact->menu_hit &&
            !contact->long_pressed && scope_live(*contact)) {
            auto event = contact->last;
            event.action = PointerAction::Cancel;
            event.cancel_reason = reason;
            event.handled = false;
            (void)dispatch_widget_pointer(contact->hit, event);
        }
    } catch(...) {if(!failure) failure=std::current_exception();}
    if(failure) std::rethrow_exception(failure);
}

void TouchInputStateMachine::neutralize_for_multi_touch(
    const std::uint64_t pointer, const std::shared_ptr<Contact>& contact) {
    if (!current(pointer,contact)) return;
    // コールバック前に中立状態を反映する。 同じポインターIDが
    // 再利用されても、下の完了処理はすべて接触の識別情報で守る。
    contact->multi_touch=true;
    contact->handled=true;
    contact->number_repeat.cancel();
    clicks_.cancel_for(contact->scope.lock());
    std::exception_ptr failure;
    try { if (owns_drag(*contact)) contact->drag->cancel(); }
    catch (...) { failure=std::current_exception(); }
    try {
        if (current(pointer,contact) && !contact->menu_hit && !contact->long_pressed &&
            !contact->cancel_pending && scope_live(*contact)) {
            auto event=contact->last;
            event.action=PointerAction::Cancel;
            event.handled=false;
            (void)dispatch_widget_pointer(contact->hit,event);
        }
    } catch (...) { if(!failure) failure=std::current_exception(); }
    if (current(pointer,contact)) {
        contact->number_press=false;
        contact->slider.reset();
        contact->scroll_target.reset();
        contact->drag_armed=false;
        contact->drag_id=0;
        contact->motion.reset();
    }
    if (failure) std::rethrow_exception(failure);
}

void TouchInputStateMachine::cancel_for(const WidgetPtr& scope) {
    prepare_cancel_for(scope)();
}

void TouchInputStateMachine::cancel_all() {
    prepare_cancel_all()();
}

Action TouchInputStateMachine::prepare_cancel_for(const WidgetPtr& scope, PointerCancelReason reason) {
    return prepare_cancel_contacts(scope, false, reason);
}

Action TouchInputStateMachine::prepare_cancel_all() {
    return prepare_cancel_contacts({}, true);
}

Action TouchInputStateMachine::prepare_pointer_cancel(const PointerEvent& event) {
    if (event.pointer_type != PointerType::Touch) return {};
    const auto found = contacts_.find(event.pointer_id);
    if (found == contacts_.end()) return {};
    const auto contact=found->second;
    contact->cancel_pending=true;
    return [this, pointer=event.pointer_id, contact, reason=event.cancel_reason] {
        cancel(pointer, contact, true, true, true, reason); };
}

Action TouchInputStateMachine::prepare_cancel_contacts(const WidgetPtr& scope, bool all, PointerCancelReason reason) {
    auto snapshot = contacts_;
    if (!all) std::erase_if(snapshot, [&](const auto& entry) {return entry.second->scope.lock() != scope;});
    for (const auto& [_,contact]:snapshot) contact->cancel_pending=true;
    return [this, snapshot=std::move(snapshot), reason, cancel_clicks=clicks_.prepare_cancel_for(scope)] {
        cancel_clicks();
        std::exception_ptr failure;
        for (const auto& [pointer, contact] : snapshot) {
            try {cancel(pointer, contact, false, true, true, reason);}
            catch (...) {if (!failure) failure = std::current_exception();}
        }
        if (failure) std::rethrow_exception(failure);
    };
}

void TouchInputStateMachine::context_click(std::uint64_t pointer,
    const std::shared_ptr<Contact>& contact, ControlActivationContext& activation) {
    const auto valid = [&] {
        return current(pointer, contact) && scope_live(*contact) &&
            widget_in_input_scope(contact->scope.lock(), contact->hit) &&
            contact->combo_revision == activation.combo_popup.revision() &&
            contact->menu_revision == activation.menu_popup.revision();
    };
    if (!valid()) return;
    // 既存の製品コールバックは、副ボタンの押下・解放のどちらかを使う。 押下が処理済
    // み、またはポップアップを開く・置き換えた場合、コンテキスト操作は一度だけ送る。
    contact->hold_handled = true;
    auto event = contact->last;
    event.button = PointerButton::Secondary;
    event.action = PointerAction::Down;
    event.handled = false;
    if (dispatch_widget_pointer(contact->hit, event) || !valid()) return;
    event.action = PointerAction::Up;
    event.handled = false;
    (void)dispatch_widget_pointer(contact->hit, event);
}

TouchInputResult TouchInputStateMachine::advance(TouchInputContext context,
    const std::shared_ptr<Window>& scope) {
    const auto snapshot = contacts_;
    TouchInputResult result;
    std::vector<std::shared_ptr<Contact>> prepared;
    prepared.reserve(snapshot.size());
    for (const auto& [pointer,contact]:snapshot) {
        if (!current(pointer,contact) || contact->scope.lock()!=scope || contact->cancel_pending) continue;
        contact->cancel_pending=true;
        prepared.push_back(contact);
    }
    const auto prepared_here=[&](const auto& contact) {
        return std::ranges::find(prepared,contact)!=prepared.end(); };
    std::exception_ptr failure;
    std::optional<PointerClickTiming::Clock::time_point> now;
    try {now=context.click_timing.now();}
    catch(...) {failure=std::current_exception();}
    for (const auto& [pointer, contact] : snapshot) {
      try {
        if (!current(pointer, contact) || contact->scope.lock() != scope) continue;
        if (!prepared_here(contact)) continue;
        const auto valid = [&] {
            return current(pointer, contact) && scope_live(*contact) &&
                (!context.live || context.live()) &&
                (contact->menu_hit ? widget_in_input_scope(scope, context.activation.menu_popup.owner()) :
                 contact->combo_hit ? combo_in_scope(scope, context.activation.combo_popup.combo(), context.activation.inline_edit) :
                 widget_in_input_scope(scope, contact->hit)) &&
                contact->combo_revision == context.activation.combo_popup.revision() &&
                contact->menu_revision == context.activation.menu_popup.revision() &&
                current(pointer,contact) && scope && scope->visible() && scope->enabled();
        };
        if (!valid()) { cancel(pointer, contact, true, true); result.redraw = true; continue; }
        contact->cancel_pending=false;
        if (contact->finishing) continue;
        if (!now) continue; // 時計がなくても、上の古い接触は終了する。
        if (contact->number_press) {
            if (contact->multi_touch || contact->handled) contact->number_repeat.cancel();
            result.redraw |= contact->number_repeat.advance(context.activation.number_edit, *now, valid);
            continue;
        }
        if (contact->long_pressed || contact->finishing || contact->moved || contact->multi_touch ||
            contact->slider || contact->menu_hit || contact->combo_hit || owns_drag(*contact) ||
            context.long_press_delay.count() < 0 ||
            *now < contact->pressed_at || *now - contact->pressed_at < context.long_press_delay) continue;
        if (hit_test_widget(scope, contact->last.position, context.tab_height, context.expand_height) != contact->hit ||
            hit_test_item_view(contact->hit, contact->last.position) != contact->pressed_item) {
            cancel(pointer, contact); result.redraw = true; continue;
        }
        // コールバック前に消費し、タイマーの再入で同じ長押しが二度発火しないようにする。
        contact->long_pressed = true;
        clicks_.cancel_for(scope);
        result.handled = result.redraw = true;
        auto event = contact->last;
        event.action = PointerAction::Cancel;
        event.handled = false;
        (void)dispatch_widget_pointer(contact->hit, event);
        if (!valid()) { cancel(pointer, contact); continue; }
        // コンテキストクリックの方針では、通常の副ボタンの押下・解
        // 放より先に、 別のLongPressコマンドを呼ばない。
        if (!context.long_press_is_context_click) {
            event.action = PointerAction::LongPress;
            event.handled = false;
            contact->hold_handled = dispatch_widget_pointer(contact->hit, event);
        }
        if (!valid()) { cancel(pointer, contact); continue; }
        if (contact->hold_handled) continue;
        // 転送元の提供処理とコンテキスト操作は、以前の選択では
        // なく押された行を対象にする。 副ボタン入力と同様、既
        // 存の複数選択にその行が含まれる場合は選択全体を保つ。
        (void)select_context_item(contact->hit, contact->last.position);
        if (!valid()) { cancel(pointer, contact); continue; }
        // ドラッグ可能な行は、移動・解放まで長押しを保留する。 移
        // 動なら親のスクロールではなくドラッグ・ドロップを始め、
        // 解放ならドラッグ不可の部品と同じコンテキストメニュー
        // を開く。 指を動かさず押しただけでデータを削除しない。
        contact->drag_armed = !context.long_press_is_context_click && contact->drag &&
            contact->drag->has_source(contact->hit);
        if (!contact->drag_armed) context_click(pointer, contact, context.activation);
      } catch(...) {
        // 適用済みの変更・予定時刻は残す。 このコールバックが作
        // った置換後の接触を消したり、操作を再実行したりしない。
        if(!failure) failure=std::current_exception();
      }
    }
    if(failure) std::rethrow_exception(failure);
    return result;
}

TouchInputResult TouchInputStateMachine::dispatch(TouchInputContext context,
    std::shared_ptr<Window> scope, PointerEvent event) {
    if (event.pointer_type != PointerType::Touch ||
        !std::isfinite(event.position.x) || !std::isfinite(event.position.y)) return {};
    auto& activation = context.activation;
    auto& combo_popup = activation.combo_popup;
    auto& menu = activation.menu_popup;
    const auto pointer = event.pointer_id;
    const auto hit_at = [&](Point point) {
        return hit_test_widget(scope, point, context.tab_height, context.expand_height);
    };
    const auto incoming_live = [&] {
        return scope && scope->visible() && scope->enabled() && (!context.live || context.live());
    };
    if (event.action == PointerAction::Down) {
        if (const auto found = contacts_.find(pointer); found != contacts_.end()) {
            if (found->second->finishing) {
                // Up/Cancelのコールバックが、ネイティブのポインターIDを同期的に再利用
                // する場合がある。 古い識別情報は完了中なので、そのマップ枠だけを無効化する。
                contacts_.erase(found);
            } else {
                cancel(pointer, found->second, true, true);
                if (contacts_.contains(pointer)) return {true, true};
            }
        }
        if (!incoming_live()) return {true};
        auto contact = std::make_shared<Contact>();
        std::exception_ptr multi_touch_failure;
        if (!contacts_.empty()) {
            // キャンセルのコールバック前に、古い識別情報を所有して保持する。 接触
            // が除去・置換・追加されても、このスナップショットだけを中立化する。
            const auto snapshot=contacts_;
            clicks_.cancel_for();
            contact->multi_touch=true;
            contact->handled=true;
            for (const auto& [other_pointer,other] : snapshot) {
                try { neutralize_for_multi_touch(other_pointer,other); }
                catch (...) { if(!multi_touch_failure) multi_touch_failure=std::current_exception(); }
            }
        }
        contact->scope = scope;
        contact->live = context.live;
        contact->drag = context.drag_drop;
        contact->last = event;
        contact->hit = hit_at(event.position);
        contact->pressed_at = context.click_timing.now();
        contact->pressed_item = hit_test_item_view(contact->hit, event.position);
        contact->menu_hit = widget_in_input_scope(scope, menu.owner()) &&
                            menu.contains(event.position, scope->bounds());
        if (!contact->menu_hit && menu.is_active()) menu.close();
        if (const auto combo = combo_popup.combo()) {
            if (combo_in_scope(scope, combo, activation.inline_edit)) {
                const auto geometry = combo_popup.layout(scope->bounds());
                contact->combo_hit = point_inside(geometry.bounds, event.position);
                if (contact->combo_hit) contact->hit = combo;
                else if (!point_inside(combo->bounds(), event.position)) combo_popup.close();
            } else combo_popup.close();
        }
        contact->combo_revision = combo_popup.revision();
        if (contact->combo_hit || contact->menu_hit) clicks_.cancel_for();
        contact->menu_revision = menu.revision();
        if (!contact->combo_hit && !contact->menu_hit)
            contact->scroll_target = hit_test_scrollable_widget(
                scope, event.position, context.tab_height, context.expand_height);
        if (!contact->combo_hit && !contact->menu_hit) {
            contact->check_only = prepare_check_touch(contact->hit,event.position,scope);
            if (contact->check_only) clicks_.cancel_for(scope);
        }
        contact->motion.begin(event.position);
        contacts_[pointer] = contact;
        if (multi_touch_failure) std::rethrow_exception(multi_touch_failure);
        // 複数タッチの組に加わる接触には、どのウィジェットにもDownを送ら
        // ない。 対応する解放は、その組を正しく終了するためだけに保持する。
        if (contact->multi_touch) return {true,true};
        if (!contact->menu_hit)
            contact->handled = dispatch_widget_pointer(contact->hit, event);
        if (current(pointer, contact) && (!scope_live(*contact) ||
            (!contact->menu_hit && !contact->combo_hit &&
             !widget_in_input_scope(scope, contact->hit)))) cancel(pointer, contact);
        if (current(pointer,contact) && !contact->handled && !contact->menu_hit && !contact->combo_hit &&
            contact->combo_revision==combo_popup.revision() && contact->menu_revision==menu.revision()) {
            if (const auto number = std::dynamic_pointer_cast<NumberEdit>(contact->hit);
                number && layout_number_edit(*number).step_at(event.position)) {
                contact->number_press = true;
                contact->scroll_target.reset();
                activate_control(scope, number, event.position, event.modifiers, activation, context.live,
                                 event.button, event.physical_control);
                if (current(pointer,contact) && scope_live(*contact) && widget_in_input_scope(scope,number) &&
                    activation.number_edit.active_for(number) && contact->combo_revision==combo_popup.revision() &&
                    contact->menu_revision==menu.revision())
                    contact->number_repeat.arm(number,event.position,context.click_timing.now());
                return {true,true,number};
            }
            contact->slider=std::dynamic_pointer_cast<Slider>(contact->hit);
            if (contact->slider) {
                // スライダーのドラッグがこの接触を管理し、スクロール可能な親には渡さない。
                contact->scroll_target.reset();
                if (!activation.focus || activation.focus(contact->slider)) {
                    if (current(pointer,contact) && scope_live(*contact) &&
                        widget_in_input_scope(scope,contact->slider) &&
                        contact->combo_revision==combo_popup.revision() && contact->menu_revision==menu.revision())
                        (void)set_slider_value_from_point(*contact->slider,event.position);
                } else contact->handled=true;
                if (current(pointer,contact) && (!scope_live(*contact) ||
                    !widget_in_input_scope(scope,contact->slider))) cancel(pointer,contact);
            }
        }
        return {true, true};
    }

    const auto found = contacts_.find(pointer);
    if (found == contacts_.end() || found->second->finishing) return {true};
    const auto contact = found->second;
    contact->last = event;
    if (event.action == PointerAction::Cancel || contact->scope.lock() != scope ||
        !scope_live(*contact) || !incoming_live()) {
        cancel(pointer, contact, true, false, false, event.action == PointerAction::Cancel
            ? event.cancel_reason : PointerCancelReason::Explicit);
        return {true, true};
    }
    if (event.action != PointerAction::Move && event.action != PointerAction::Up) return {true};
    const bool release = event.action == PointerAction::Up;
    contact->finishing = release;
    if (release) contact->number_repeat.cancel();
    else contact->number_repeat.move(event.position, context.click_timing.now());
    if (contact->multi_touch) {
        // 最初の接触には組が成立した時点でCancelを送信済みで、加わ
        // った接触にはDownを送っていない。 追加のCancel・ク
        // リックを生成せず、どちらの移動量も親のスクロールへ渡さない。
        if (release) contacts_.erase(pointer);
        return {true,true};
    }
    if (!contact->menu_hit && !contact->long_pressed)
        contact->handled = dispatch_widget_pointer(contact->hit, event) || contact->handled;
    if (!current(pointer, contact)) return {true, true};
    const auto valid = [&] {
        return scope_live(*contact) &&
            (contact->menu_hit ? widget_in_input_scope(scope, menu.owner()) :
             contact->combo_hit ? combo_in_scope(scope, combo_popup.combo(), activation.inline_edit) :
             widget_in_input_scope(scope, contact->hit));
    };
    if (!valid()) {
        cancel(pointer, contact);
        return {true, true};
    }
    // コールバックや別の接触が、同じポップアップを閉じて開き直す場合
    // がある。 古い指の解放で、その新しいポップアップを確定しない。
    const bool overlays_match = contact->combo_revision == combo_popup.revision() &&
                                contact->menu_revision == menu.revision();
    if (contact->number_press) {
        if (release) { contact->number_repeat.cancel(); contacts_.erase(pointer); }
        else if (!contact->handled && overlays_match && !contact->multi_touch)
            contact->number_repeat.move(event.position, context.click_timing.now());
        else contact->number_repeat.cancel();
        return {true, true}; // 矢印の最初の一段階は、解放時ではなく押下時に実行済み。
    }
    if (contact->slider) {
        if (release) contacts_.erase(pointer);
        if (!contact->handled && overlays_match)
            (void)set_slider_value_from_point(*contact->slider,event.position);
        if (!release && current(pointer,contact) && (!valid() || !overlays_match)) cancel(pointer,contact);
        return {true,true,contact->slider};
    }
    contact->moved |= drag_threshold_reached(contact->motion.start(), event.position, context.threshold);
    if (contact->long_pressed) {
        if (!overlays_match || contact->multi_touch) {
            cancel(pointer, contact); return {true, true};
        }
        if (!release && contact->drag_armed && contact->moved && !contact->drag->active()) {
            contact->drag_armed = false; // 一回の長押しにつき、開始は一度だけ試す。
            if (contact->drag->begin(contact->hit, contact->motion.start(), scope)) {
                contact->drag_id = contact->drag->session_id();
                if (!current(pointer, contact) || !valid()) {
                    if (owns_drag(*contact)) contact->drag->cancel();
                    return {true, true};
                }
            }
        }
        if (owns_drag(*contact)) {
            const auto target = hit_at(event.position);
            if (release) {
                contacts_.erase(pointer);
                contact->drag->finish(target, event.position, hit_test_item_view(target, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
            } else contact->drag->update(target, event.position, hit_test_item_view(target, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
        } else if (release) {
            if (!contact->moved && !contact->hold_handled) context_click(pointer, contact, activation);
            if (current(pointer, contact)) contacts_.erase(pointer);
        }
        return {true, true}; // 長押し後は主ボタンのクリック・ダブルクリックを行わない。
    }
    if (!release) {
        if (contact->handled) return {true, true};
        if (!overlays_match) { cancel(pointer, contact); return {true, true}; }
        const bool scroll_live = widget_in_input_scope(scope, contact->scroll_target);
        const auto motion = contact->motion.update(event.position, context.threshold,
            scroll_live || contact->combo_hit || contact->menu_hit);
        if (motion.active) {
            // 保留中のポインター移動量は、スクロールとして消費されていない
            // 。 その変位を一度だけ加え、汎用の操作差分の規則を維持する。
            const Point displacement = motion.began
                ? Point{event.position.x - contact->motion.start().x,
                        event.position.y - contact->motion.start().y}
                : motion.delta;
            const auto row_height = std::max(1.0F, activation.row_height);
            if (contact->menu_hit) {
                (void)menu.pan(contact->motion.start(), displacement.y,
                               scope->bounds(), contact->remainder);
            } else if (contact->combo_hit) {
                (void)combo_popup.pan(combo_popup.layout(scope->bounds()),
                                     displacement.y, contact->remainder);
            } else if (scroll_live) {
                (void)contact->scroll.pan_in_scope(scope, contact->scroll_target, displacement,
                                                  row_height, context.text);
            }
        } else if (contact->moved && !contact->scroll_target && contact->drag &&
                   !contact->drag_id && !contact->drag->active() &&
                   contact->drag->has_source(contact->hit)) {
            if (contact->drag->begin(contact->hit, contact->motion.start(), scope)) {
                contact->drag_id = contact->drag->session_id();
                if (!current(pointer, contact) || !valid()) {
                    if (owns_drag(*contact)) contact->drag->cancel();
                    return {true, true};
                }
            }
        }
        if (owns_drag(*contact)) {
            const auto target = hit_at(event.position);
            contact->drag->update(target, event.position,
                                 hit_test_item_view(target, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
        }
        return {true, true};
    }

    contacts_.erase(pointer); // 有効化・確定のコールバックより前に終了する。
    if (owns_drag(*contact)) {
        const auto target = hit_at(event.position);
        if (!contact->handled && overlays_match && widget_in_input_scope(scope, target))
            contact->drag->finish(target, event.position, hit_test_item_view(target, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
        else contact->drag->cancel();
        return {true, true};
    }
    if (contact->handled || contact->moved || !overlays_match) {
        // マウスと同様、独自描画の画面がタップを消費しても、その
        // 画面へフォーカスを移す。 キーボードショートカットとエ
        // ディタの選択ペインを、タップしたビューに追従させる。
        if (contact->handled && !contact->moved && overlays_match &&
            activation.focus && accepts_keyboard_focus(contact->hit) &&
            widget_in_input_scope(scope, contact->hit))
            (void)activation.focus(contact->hit);
        clicks_.cancel_for(scope); return {true,true};
    }
    if (contact->menu_hit) {
        (void)menu.click(event.position, scope->bounds(),
                         {event.modifiers, event.button, event.physical_control});
        return {true, true};
    }
    if (contact->combo_hit) {
        const auto geometry = combo_popup.layout(scope->bounds());
        if (combo_popup.combo() == activation.inline_edit.choice())
            (void)click_inline_choice(activation.inline_edit, combo_popup, geometry, event.position);
        else {
            (void)combo_popup.click(geometry, event.position);
            activation.combo_edit.synchronize();
        }
        return {true, true, contact->hit};
    }
    if (contact->check_only) {
        // 解放時の小さなずれでも、押下時の意図を維持する。 キャンセルさ
        // れた場合も含め、このタップを行のダブルクリックの列へ加えない。
        clicks_.cancel_for(scope);
        if (hit_at(event.position)==contact->hit &&
            hit_test_item_view(contact->hit,event.position)==contact->pressed_item) {
            if (activation.inline_edit.active()) {
                if (combo_popup.is_open_for(activation.inline_edit.choice())) combo_popup.close();
                (void)activation.inline_edit.commit();
            }
            if (valid() && (!activation.focus || activation.focus(contact->hit)) && valid())
                (void)contact->check_only();
        }
        return {true,true,contact->hit};
    }
    if (hit_at(event.position) == contact->hit) {
        const auto click_time=context.click_timing.event_time(event);
        if (!valid() || contacts_.contains(pointer)) return {true,true};
        const auto count=contact->multi_touch ? 1 : clicks_.record(scope,contact->hit,event,
            click_time,context.click_timing);
        activate_control(scope, contact->hit, event.position, event.modifiers, activation, context.live,
                         event.button, event.physical_control);
        if (count>=2 && !contacts_.contains(pointer) && valid() && contact->combo_revision==combo_popup.revision() &&
            contact->menu_revision==menu.revision()) {
            auto double_click=event; double_click.action=PointerAction::DoubleClick; double_click.handled=false;
            const bool handled=dispatch_widget_pointer(contact->hit,double_click);
            if (!handled && !contacts_.contains(pointer) && valid())
                activate_control_double(scope,contact->hit,event.position,activation,
                                        event.modifiers);
        }
        return {true, true, contact->hit};
    }
    clicks_.cancel_for(scope);
    return {true, true};
}
} // namespace cwxeditor::lib
