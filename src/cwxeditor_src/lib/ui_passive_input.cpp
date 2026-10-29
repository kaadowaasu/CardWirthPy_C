#include "ui_passive_input.hpp"
#include "ui_metrics.hpp"
#include "window_host.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cwxeditor::lib {
namespace {
struct WindowPointerObserverEntry {
    std::uint64_t id{};
    WindowPointerObserver observer;
    bool active{true};
};
struct WindowPointerObserverRegistry {
    std::uint64_t next{}, revision{};
    std::vector<std::shared_ptr<WindowPointerObserverEntry>> entries;
};
WindowPointerObserverRegistry& window_pointer_observers() {
    static WindowPointerObserverRegistry registry;
    return registry;
}

bool live(const PassiveInputContext& context, const WidgetPtr& scope) {
    const auto visible=[&] {return scope && scope->visible() && scope->enabled();};
    return visible() && (!context.live || context.live()) && visible();
}
bool finite(Point point) { return std::isfinite(point.x) && std::isfinite(point.y); }
bool menu_in_scope(const PassiveInputContext& context, const WidgetPtr& scope) {
    return context.menu.is_active() && widget_in_input_scope(scope, context.menu.owner());
}
}

std::uint64_t add_window_pointer_observer(WindowPointerObserver observer) {
    if (!observer) return 0U;
    auto& registry = window_pointer_observers();
    const auto id = ++registry.next;
    registry.entries.push_back(std::make_shared<WindowPointerObserverEntry>(
        WindowPointerObserverEntry{id, std::move(observer), true}));
    return id;
}

void remove_window_pointer_observer(const std::uint64_t id) noexcept {
    if (!id) return;
    auto& entries = window_pointer_observers().entries;
    for (const auto& entry : entries) {
        if (entry && entry->id == id) {
            entry->active = false;
            entry->observer = {};
        }
    }
    std::erase_if(entries, [id](const auto& entry) { return !entry || entry->id == id; });
}

std::uint64_t window_pointer_observer_limit() noexcept {
    return window_pointer_observers().next;
}

void notify_window_pointer_observers(const WidgetPtr& scope,
                                     const PointerEvent& event, const WidgetPtr& hit) {
    notify_window_pointer_observers(scope, event, hit,
        std::numeric_limits<std::uint64_t>::max());
}

void notify_window_pointer_observers(const WidgetPtr& scope,
                                     const PointerEvent& event, const WidgetPtr& hit,
                                     const std::uint64_t registration_limit) {
    auto& registry = window_pointer_observers();
    if (registry.entries.empty()) return;
    // コールバックが新しいポインター入力を送ったり、関連先を切り離したりす
    // る場合がある。 共有スロットでスナップショットを保持し、変更番号と有
    // 効フラグで、 再入・所有者の終了後に古いコールバックが動くのを防ぐ。
    const auto revision = ++registry.revision;
    const auto snapshot = registry.entries;
    for (const auto& entry : snapshot) {
        if (revision != registry.revision) return;
        if (!entry || entry->id > registration_limit || !entry->active || !entry->observer)
            continue;
        const auto observer = entry->observer;
        observer(scope, event, hit);
    }
}

PassiveInputResult dispatch_hover_input(PassiveInputContext context,
    const WidgetPtr& scope, PointerEvent event) {
    if (event.action != PointerAction::Move || !finite(event.position)) return {true};
    if (!live(context, scope)) return {true};
    const auto clear_overlay_hover = [&] {
        const auto previous = context.hover.hovered();
        const auto hover_revision = context.hover.revision();
        const auto menu_revision = context.menu.revision();
        const auto combo_revision = context.combo.revision();
        if (previous && event.pointer_type == PointerType::Mouse) {
            auto leave = event;
            leave.action = PointerAction::Leave;
            leave.handled = false;
            leave.item_hit_test_available = false;
            leave.item_index = -1;
            notify_window_pointer_observers(scope, leave, {});
            if (!live(context, scope) || context.hover.revision() != hover_revision ||
                context.menu.revision() != menu_revision ||
                context.combo.revision() != combo_revision) return;
        }
        context.hover.update(scope, {}, event);
    };
    if (menu_in_scope(context, scope) && context.menu.hover(event.position, scope->bounds())) {
        clear_overlay_hover();
        return {true, true};
    }
    if (const auto combo = context.combo.combo(); combo_in_scope(scope, combo, context.inline_edit)) {
        const auto geometry = context.combo.layout(scope->bounds());
        // つかんだポップアップのスクロールつまみが、ポインター移動を管理する（マウス入力参照）。
        if (context.combo.dragging_thumb()) {
            (void)context.combo.drag_thumb(geometry, event.position);
            clear_overlay_hover();
            return {true, true};
        }
        if (point_inside(geometry.bounds, event.position)) {
            (void)context.combo.hover(geometry, event.position);
            // 無効な行、枠、変化のない強調表示も背後の部品を覆うため
            // 、 背後のプレビュー・ツールチップを閉じる必要がある。
            clear_overlay_hover();
            return {true, true};
        }
    }
    const auto hit_at = [&] {
        return hit_test_widget(scope, event.position, context.tab_height, context.expand_height);
    };
    const auto hit = hit_at();
    const auto previous = context.hover.hovered();
    const auto point = context.hover.position();
    const auto revision = context.hover.revision();
    const auto menu_revision = context.menu.revision(), combo_revision = context.combo.revision();
    const auto observe_transition = [&](const PointerEvent& transition,
                                        const WidgetPtr& next) {
        if (transition.pointer_type != PointerType::Mouse) return true;
        notify_window_pointer_observers(scope, transition, next);
        // 監視先がポインター下のパレットを隠す、範囲を閉じる、
        // 別のポインターイベントを同期送信する場合がある。
        // ホバーの状態管理は、その後に古い対象へ通知しない。
        return live(context, scope) && context.hover.revision() == revision + 1U &&
            context.menu.revision() == menu_revision &&
            context.combo.revision() == combo_revision &&
            (!next || widget_in_input_scope(scope, next)) && hit_at() == next;
    };
    context.hover.update(scope, hit, event, observe_transition);
    // Leave/Enterの処理は、コントロールの置換、ネイティブ画面の再表示
    // 、 モーダル開始、ポップアップ表示、新しいポインター移動を行う場合がある。
    if (!live(context, scope) || context.hover.revision() != revision + 1 ||
        context.menu.revision() != menu_revision || context.combo.revision() != combo_revision ||
        (hit && !widget_in_input_scope(scope, hit)) || hit_at() != hit) return {true, true};
    PassiveInputResult result;
    result.redraw = previous != context.hover.hovered() ||
        point.x != event.position.x || point.y != event.position.y;
    if (const auto actual = context.hover.hovered()) {
        result.cursor = actual->cursor();
        if (actual->cursor() == CursorKind::Custom)
            set_active_custom_cursor(actual->custom_cursor());
    }
    // SWTと同様、自身のカーソルを持たないコントロールはウィンドウのカーソルを使う。 D版
    // の setWaitCursors はウィンドウへ CURSOR_WAIT を設定する。
    if (result.cursor == CursorKind::Default &&
        scope->cursor() != CursorKind::Default)
        result.cursor = scope->cursor();
    if (wait_cursor_active()) result.cursor = CursorKind::Wait;
    if (const auto table = std::dynamic_pointer_cast<TableView>(hit); table &&
        hit_test_table_resize_column(*table, event.position, context.row_height, context.resize_margin))
        result.cursor = CursorKind::SizeHorizontal;
    else if (const auto frame = hit_test_frame_drag(scope, event.position)) {
        // 広めの仕切りのつかみ領域が隣のスクロールバーと重なる場合、枠の捕捉
        // と同じ優先順にする。 文字計測は、その狭い重なり部分だけで行う。
        if (hit) {
            const auto access = text_scrollbar_access(std::dynamic_pointer_cast<TextEdit>(hit), context.text);
            const auto scroll = access.geometry ? access.geometry() : item_scroll_geometry(*hit);
            if (!live(context, scope) || hit_at() != hit ||
                context.hover.revision() != revision + 1 ||
                context.menu.revision() != menu_revision || context.combo.revision() != combo_revision)
                return {true, true};
            if (point_inside(scroll.vertical.track, event.position) ||
                point_inside(scroll.horizontal.track, event.position)) return result;
        }
        result.cursor = frame->cursor;
    }
    return result;
}

PassiveInputResult dispatch_wheel_input(PassiveInputContext context,
    const WidgetPtr& scope, WheelEvent event) {
    if (!finite(event.position) || !finite({event.delta_x, event.delta_y}) ||
        !live(context, scope)) return {true};
    const bool dismissed=context.hover.tooltip().dismiss_for(scope);
    if (menu_in_scope(context, scope) && context.menu.wheel(event.position,event.delta_y,scope->bounds()))
        return {true, true};
    if (const auto combo = context.combo.combo(); combo_in_scope(scope, combo, context.inline_edit)) {
        const auto geometry = context.combo.layout(scope->bounds());
        if (point_inside(geometry.bounds,event.position) || point_inside(combo->bounds(),event.position)) {
            (void)context.combo.wheel(geometry,event.position,event.delta_y);
            return {true, true};
        }
    }
    const auto hit_at = [&] {
        return hit_test_widget(scope,event.position,context.tab_height,context.expand_height);
    };
    const auto hit = hit_at();
    if(const auto tabs=std::dynamic_pointer_cast<TabView>(hit);
       tabs && tab_wheel_switch_enabled() && tabs->header_visible && tabs->enabled() &&
       tabs->pages.size()>1U && event.delta_y!=0.0F) {
        const auto content=layout_tab_content(*tabs);
        if(event.position.y<content.y) {
            const bool changed=navigate_tab_page(*tabs,event.delta_y<0.0F ?
                TabNavigation::Next : TabNavigation::Previous,true);
            return {true,changed || dismissed,CursorKind::Default,
                live(context,scope) && widget_in_input_scope(scope,tabs) ? tabs : WidgetPtr{}};
        }
    }
    if (const auto number = std::dynamic_pointer_cast<NumberEdit>(hit);
        number && number_edit_wheel_enabled() && !number->read_only &&
        number->enabled() && event.delta_y != 0.0F) {
        const auto direction = event.delta_y > 0.0F ? 1.0 : -1.0;
        const auto next = std::clamp(number->value + direction * number->step,
                                     number->minimum, number->maximum);
        if (next != number->value) {
            number->value = next;
            if (number->on_changed) number->on_changed(next);
        }
        return {true, true, CursorKind::Default, number};
    }
    if (const auto combo = std::dynamic_pointer_cast<ComboBox>(hit); combo &&
        widget_in_input_scope(scope,combo) && !combo->items.empty() && event.delta_y != 0) {
        context.combo.close();
        const bool handled = wheel_combo_selection(combo,event.delta_y);
        return {handled, handled || dismissed, CursorKind::Default,
            live(context,scope) && widget_in_input_scope(scope,combo) && hit_at() == combo ? combo : WidgetPtr{}};
    }
    const auto menu_revision = context.menu.revision(), combo_revision = context.combo.revision();
    if (hit && hit->on_wheel) {
        const auto callback = hit->on_wheel;
        callback(event);
    }
    if (!live(context,scope) || hit_at() != hit || (hit && !widget_in_input_scope(scope,hit)) ||
        context.menu.revision() != menu_revision || context.combo.revision() != combo_revision)
        return {true, true};
    if (event.handled) return {true, true};
    const auto target = hit_test_scrollable_widget(scope,event.position,context.tab_height,context.expand_height);
    const bool moved = context.scroll.wheel_in_scope(scope,target,{event.delta_x,event.delta_y},
        event.modifiers,context.row_height,context.text);
    return {moved, moved || dismissed};
}

} // namespace cwxeditor::lib
