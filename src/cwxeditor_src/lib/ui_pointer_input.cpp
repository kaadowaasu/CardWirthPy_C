#include "ui_pointer_input.hpp"
#include "window_host.hpp"
#include <exception>

namespace cwxeditor::lib {
std::vector<PointerPressFeedback> prepare_pointer_feedback(const std::shared_ptr<Window>& scope,
    const MouseInputStateMachine& mouse,const TouchInputStateMachine& touch) {
    auto result=touch.press_feedback(scope);
    if (auto press=mouse.press_feedback(scope)) result.push_back(std::move(*press));
    return result;
}
PointerInputResult dispatch_pointer_input(PointerInputContext context,
    std::shared_ptr<Window> scope, PointerEvent event, bool primary_down, unsigned clicks) {
    PointerInputResult result;
    if (event.action == PointerAction::Cancel) {
        // ホバー・寿命・サイズ変更のコールバック前に状態を取得する。 遅れ
        // たキャンセルを、同じポインターIDを使う新しい押下へ送らない。
        const auto mouse = context.mouse.prepare_pointer_cancel(scope, event);
        const auto touch = context.touch.prepare_pointer_cancel(event);
        const auto chrome = context.chrome.prepare_pointer_cancel(scope, event);
        (void)context.caption.cancel_pointer(scope, event);
        std::exception_ptr failure;
        const auto complete = [&](auto&& action) {
            try { action(); } catch (...) { if (!failure) failure = std::current_exception(); }
        };
        complete([&] { context.passive.hover.clear_pointer(scope, event); });
        complete([&] { if (chrome) chrome(); });
        complete([&] { if (mouse) mouse(); });
        complete([&] { if (touch) touch(); });
        if (failure) std::rethrow_exception(failure);
        result.handled = result.redraw = result.refresh_focus = true;
        return result;
    }
    const auto current=[&] { return scope && scope->visible() && (!context.live || context.live()); };
    const auto retired=[&] {
        if(current()) return false;
        result.handled=true;
        result.refresh_focus=false;
        result.activated.reset();
        return true;
    };
    if(retired()) return result;
    const bool observe_mouse_up = event.pointer_type == PointerType::Mouse &&
        event.action == PointerAction::Up;
    const bool observe_window_leave = event.pointer_type == PointerType::Mouse &&
        event.action == PointerAction::Leave;
    const auto up_observer_limit = observe_mouse_up
        ? window_pointer_observer_limit() : std::uint64_t{};
    const auto up_hit = observe_mouse_up
        ? hit_test_widget(scope, event.position, context.passive.tab_height,
                          context.passive.expand_height)
        : WidgetPtr{};
    const auto notify_mouse_up = [&] {
        if (!observe_mouse_up || retired()) return;
        // D版の表示フィルターはネイティブのリスナーより先に動くが
        // 、共通入力モデルではここで解放時の有効化を管理する。 解
        // 決した対象のコールバック後に通知し、パレットを隠す前にコ
        // マンドを完了させる。 登録状態はイベント開始時に取得する
        // 。このUpで新しく表示されたパレットの監視は次のイベン
        // トから始め、 自身を表示した解放イベントを消費しない。
        notify_window_pointer_observers(scope, event, up_hit, up_observer_limit);
        (void)retired();
    };
    if (observe_window_leave) {
        notify_window_pointer_observers(scope, event, {});
        if (retired()) return result;
    }
    if (event.pointer_type != PointerType::Touch && !context.mouse.accepts_pointer(event)) {
        result.handled = true;
        notify_mouse_up();
        return result;
    }
    if (event.action==PointerAction::Down)
        result.redraw|=context.passive.hover.tooltip().dismiss_for(scope);
    if (event.action == PointerAction::Leave) {
        // ネイティブのペンの接近状態の喪失と捕捉のキャンセルも、操作の
        // 後処理前に、 他のポインター機器と同じホバー終了処理を使う。
        context.passive.hover.clear_pointer(scope, event);
        result.redraw = true;
        if (retired()) return result;
    }
    const auto retire_body=[&] {
        // ホバー・キャンセル通知前に両方の機器の状態を取得する。 コールバックがウィンド
        // ウを閉じたり、同じIDを含む接触を置き換えたり、例外を出したりする場合がある。
        const auto mouse = context.mouse.prepare_cancel_for();
        const auto touch = context.touch.prepare_cancel_all();
        std::exception_ptr failure;
        const auto complete = [&](auto&& action) {
            try {action();} catch (...) {if (!failure) failure = std::current_exception();}
        };
        result.redraw = result.refresh_focus = true;
        complete([&] {(void)context.passive.hover.tooltip().dismiss_for(scope);
                     context.passive.hover.clear_pointer(scope, event);});
        complete(mouse);
        complete(touch);
        if (failure) std::rethrow_exception(failure);
    };
    const auto chrome=[&] {
        const auto handled=context.chrome.dispatch(scope,event,
            {context.passive.tab_height,context.passive.expand_height,context.passive.row_height,
             !context.activation.combo_popup.combo() && !context.passive.menu.is_active(),context.live,
             context.passive.text});
        result.redraw|=handled.redraw;
        // スクロール・サイズ変更で、フォーカス中の編集欄のネイティブIME領域が動く場合がある。
        result.refresh_focus|=handled.redraw;
        if(handled.handled) { result.handled=true; result.cursor=handled.cursor; }
        if(handled.began) retire_body();
        return handled.handled;
    };
    // 既存の仕切り・スクロールバー・列の捕捉は、ポインターが見出しを
    // 越えても優先する。 新しい捕捉では見出しのクリックを奪わない。
    if(context.chrome.owner() && chrome()) { notify_mouse_up(); return result; }
    if(retired()) { notify_mouse_up(); return result; }
    const bool body_capture=context.mouse.owner() || context.touch.has_contacts() ||
        (context.drag_drop && context.drag_drop->active());
    // 捕捉済みの部品・選択・ドロップをウィンドウの端を越えて動かしても
    // 、 移動をサイズ変更のホバーに変えたり、解放を捨てたりしない。
    const bool caption_candidate=context.caption.owner() ||
        (!body_capture || event.action==PointerAction::Down);
    if(caption_candidate && context.surface && context.surface->bounds && (context.caption.owner() ||
        (!context.activation.combo_popup.combo() && !context.passive.menu.is_active()))) {
        const auto& surface=*context.surface;
        const bool captured=static_cast<bool>(context.caption.owner());
        const auto caption=context.caption.handle(scope,*surface.bounds,surface.host,
            surface.visible,surface.movable,event,surface.resizable);
        result.redraw|=caption.moved;
        result.close_requested=caption.close_requested;
        result.minimize_requested=caption.minimize_requested;
        result.undo_requested=caption.undo_requested;
        result.redo_requested=caption.redo_requested;
        if(caption.cursor) result.cursor=caption.cursor;
        if(caption.moved) {
            result.refresh_focus=true;
            result.window_bounds=*surface.bounds;
            scope->set_bounds(*surface.bounds);
        }
        if(caption.consumed) {
            result.handled=true;
            if(!captured && context.caption.owner()) {
                retire_body();
                (void)retired();
                return result; // キャンセルのコールバックが作ったホバーを消さない。
            }
            // ホバープレビューは本体に属し、ウィンドウのサイズ変更のつかみ領域には属さない。
            const auto old_hover=context.passive.hover.hovered();
            result.redraw|=context.passive.hover.tooltip().dismiss_for(scope);
            context.passive.hover.clear_pointer(scope,event);
            result.redraw|=old_hover!=context.passive.hover.hovered();
            (void)retired();
            notify_mouse_up();
            return result;
        }
    }
    if (chrome()) { notify_mouse_up(); return result; }
    if (retired()) { notify_mouse_up(); return result; }
    if(event.pointer_type==PointerType::Touch) {
        const auto touch=context.touch.dispatch({context.activation,context.drag_drop,context.passive.text,
            context.passive.tab_height,context.passive.expand_height,context.touch_threshold,context.live,
            context.click_timing,context.long_press_delay,context.long_press_is_context_click},scope,event);
        // 空白上でも、振り分け済みのタッチは論理的なエディタ画
        // 面が管理する。 ALL の背後のエンジンへ通さない。
        result.handled=true;
        result.redraw|=touch.redraw;
        result.activated=touch.activated;
        result.refresh_focus=static_cast<bool>(touch.activated) || touch.redraw;
        (void)retired();
        return result;
    }
    if(event.action==PointerAction::Move) {
        context.mouse.discard_stale();
        if(retired()) return result;
        const auto hover=dispatch_hover_input(context.passive,scope,event);
        if(retired()) return result;
        result.redraw|=hover.redraw;
        result.cursor=hover.cursor;
        if(hover.consumed) {
            context.mouse.discard_stale();
            result.handled=true;
            return result;
        }
    }
    const bool captured_mouse=context.mouse.owner()==scope;
    const bool dragging_before=context.drag_drop && context.drag_drop->active();
    const auto mouse=context.mouse.dispatch({context.activation,context.editing,context.drag_drop,
        context.passive.tab_height,context.passive.expand_height,context.drag_threshold,context.live,context.click_timing},
        scope,event,primary_down,clicks);
    result.handled=mouse.handled;
    result.redraw|=mouse.redraw;
    result.activated=mouse.activated;
    result.refresh_focus=event.action==PointerAction::Down || static_cast<bool>(mouse.activated) ||
        (captured_mouse && mouse.redraw);
    // SWTと同様、ドラッグ中はホバー中の部品にかか
    // わらず、ポインター下の操作に対応するドロップカ
    // ーソルを表示する。 対象外ではドロップ不可と
    // し、終了後にホバー中の部品のカーソルへ戻す。
    if(context.drag_drop && context.drag_drop->active()) {
        if(const auto cursor=context.drag_drop->feedback_cursor()) result.cursor=cursor;
    } else if(dragging_before) {
        const auto hovered=context.passive.hover.hovered();
        result.cursor=hovered ? hovered->cursor() : CursorKind::Default;
        if(hovered && hovered->cursor()==CursorKind::Custom)
            set_active_custom_cursor(hovered->custom_cursor());
    }
    notify_mouse_up();
    (void)retired();
    return result;
}
bool pointer_timers_pending(const MouseInputStateMachine& mouse,const TouchInputStateMachine& touch) noexcept {
    return mouse.has_timed_input() || touch.has_contacts();
}
PointerInputResult advance_pointer_input(PointerInputContext context,
    const std::shared_ptr<Window>& scope) {
    MouseInputResult mouse;
    TouchInputResult result;
    std::exception_ptr failure;
    try {
        mouse=context.mouse.advance({context.activation, context.editing, context.drag_drop,
            context.passive.tab_height, context.passive.expand_height, context.drag_threshold,
            context.live, context.click_timing}, scope);
    } catch(...) {failure=std::current_exception();}
    // 各状態管理が接触・ネイティブ・モーダルの寿命を再確認する。 マ
    // ウスの反復失敗で、独立したタッチ接触の終了処理を飛ばさない。
    try {
        result=context.touch.advance({context.activation, context.drag_drop, context.passive.text,
            context.passive.tab_height, context.passive.expand_height, context.touch_threshold,
            context.live, context.click_timing, context.long_press_delay,
            context.long_press_is_context_click}, scope);
    } catch(...) {if(!failure) failure=std::current_exception();}
    if(failure) std::rethrow_exception(failure);
    PointerInputResult output;
    output.handled = mouse.handled || result.handled;
    output.redraw = mouse.redraw || result.redraw;
    output.refresh_focus = output.redraw;
    return output;
}
} // namespace cwxeditor::lib
