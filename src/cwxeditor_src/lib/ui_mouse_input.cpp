#include "ui_mouse_input.hpp"
#include "ui_inline_edit.hpp"
#include <cmath>
#include <exception>

namespace cwxeditor::lib {

namespace {

// D版・ネイティブの部品と同様、コマンド型のクリックはボタンの解放
// 時に確定する。 共通の入力状態管理でこの規則を使い、ALL・単独
// 版・全OSで経路を揃える。 ButtonDownからモーダルを開
// くと、対応するButtonUpが新しいダイアログへ届いてしまう。
bool defers_activation_until_release(const WidgetPtr& widget) {
    return static_cast<bool>(std::dynamic_pointer_cast<Button>(widget)) ||
        static_cast<bool>(std::dynamic_pointer_cast<Link>(widget)) ||
        static_cast<bool>(std::dynamic_pointer_cast<ToolBar>(widget)) ||
        static_cast<bool>(std::dynamic_pointer_cast<ColorWell>(widget));
}

} // namespace

bool MouseInputStateMachine::live(const Contact& contact) {
    const auto scope = contact.scope.lock();
    return scope && scope->visible() && scope->enabled() &&
        (!contact.live || contact.live()) && scope->visible() && scope->enabled();
}

bool MouseInputStateMachine::owns_drag(const Contact& contact) {
    return contact.drag_id && contact.drag && contact.drag->session_id() == contact.drag_id;
}

std::shared_ptr<Window> MouseInputStateMachine::owner() const {
    if (capture_ && widget_in_input_scope(capture_->scope.lock(), capture_->widget))
        return capture_->scope.lock();
    // 成立したドロップは別ウィンドウを対象にできるが、 選択・スライ
    // ダーのドラッグと開始待ちのドラッグは、元のウィンドウに属する。
    return active_ && live(*active_) && !owns_drag(*active_) ? active_->scope.lock() : nullptr;
}

std::optional<PointerPressFeedback> MouseInputStateMachine::press_feedback(const std::shared_ptr<Window>& scope) const {
    const auto contact=active_;
    if (!contact || contact->scope.lock()!=scope || !live(*contact) || contact->suppressed ||
        contact->last.button!=PointerButton::Primary || owns_drag(*contact) ||
        !widget_in_input_scope(scope,contact->hit)) return {};
    return PointerPressFeedback{contact->hit,contact->start,contact->last.position};
}

bool MouseInputStateMachine::accepts_pointer(const PointerEvent& event) const {
    return !active_ || !live(*active_) ||
        (active_->last.pointer_type == event.pointer_type &&
         active_->last.pointer_id == event.pointer_id);
}

void MouseInputStateMachine::capture(const std::shared_ptr<Window>& scope,
    const WidgetPtr& widget, bool enabled) {
    if (!enabled) {
        if (!widget || (capture_ && capture_->widget == widget)) capture_.reset();
    } else if (widget_in_input_scope(scope, widget)) {
        capture_ = std::make_shared<Capture>(Capture{scope, widget});
    }
}

void MouseInputStateMachine::cancel(std::shared_ptr<Contact> contact,
                                    const std::shared_ptr<Capture>* expected_capture,
                                    bool cancel_clicks, PointerCancelReason reason) {
    if (!contact || active_ != contact) return;
    active_.reset(); // キャンセルのコールバックが作った置換後の状態を消さない。
    contact->number_repeat.cancel();
    if (cancel_clicks) clicks_.cancel_for(contact->scope.lock());
    const ReleaseKey release{contact->last.pointer_type, contact->last.pointer_id, contact->last.button};
    if (popup_release_ == release) popup_release_.reset();
    if (capture_ && (!expected_capture || capture_ == *expected_capture) &&
        capture_->scope.lock() == contact->scope.lock()) capture_.reset();
    std::exception_ptr failure;
    try {if (owns_drag(*contact)) contact->drag->cancel();}
    catch(...) {failure=std::current_exception();}
    try {
        if (!contact->suppressed && live(*contact) &&
            widget_in_input_scope(contact->scope.lock(), contact->hit)) {
            auto event = contact->last;
            event.action = PointerAction::Cancel;
            event.cancel_reason = reason;
            event.handled = false;
            (void)dispatch_widget_pointer(contact->hit, event);
        }
    } catch(...) {if(!failure) failure=std::current_exception();}
    if(failure) std::rethrow_exception(failure);
}

void MouseInputStateMachine::cancel_for(const WidgetPtr& scope) {
    prepare_cancel_for(scope)();
}

Action MouseInputStateMachine::prepare_cancel_for(const WidgetPtr& scope, PointerCancelReason reason) {
    const auto contact = active_ && (!scope || active_->scope.lock() == scope) ? active_ : nullptr;
    const auto capture = capture_ && (!scope || capture_->scope.lock() == scope) ? capture_ : nullptr;
    return [this, contact, capture, reason, cancel_clicks=clicks_.prepare_cancel_for(scope)] {
        cancel_clicks();
        if (capture && capture_ == capture) capture_.reset();
        cancel(contact, &capture, false, reason);
    };
}

Action MouseInputStateMachine::prepare_pointer_cancel(const std::shared_ptr<Window>& scope,
                                                       const PointerEvent& event) {
    if (event.pointer_type == PointerType::Touch) return {};
    if (const auto contact = active_) {
        if (contact->last.pointer_type != event.pointer_type || contact->last.pointer_id != event.pointer_id)
            return {};
        return [this, contact, capture=capture_, reason=event.cancel_reason] { cancel(contact, &capture, true, reason); };
    }
    const auto capture = capture_;
    if (!capture || capture->scope.lock() != scope) return {};
    return [this, capture, scope, event] {
        if (capture_ != capture || active_) return;
        capture_.reset();
        if (widget_in_input_scope(scope, capture->widget)) {
            auto canceled = event;
            canceled.action = PointerAction::Cancel;
            canceled.handled = false;
            (void)dispatch_widget_pointer(capture->widget, canceled);
        }
    };
}

void MouseInputStateMachine::discard_stale() {
    if (capture_ && !widget_in_input_scope(capture_->scope.lock(), capture_->widget)) capture_.reset();
    const auto contact = active_;
    if (contact && (!live(*contact) || (!contact->suppressed &&
        !widget_in_input_scope(contact->scope.lock(), contact->hit)))) cancel(contact);
}

MouseInputResult MouseInputStateMachine::advance(MouseInputContext context,
    const std::shared_ptr<Window>& scope) {
    const auto contact = active_;
    if (!contact || contact->scope.lock() != scope) return {};
    if (contact->hold_pending && !contact->number_repeat.active()) {
        const auto valid = [&] {
            return active_ == contact && live(*contact) && (!context.live || context.live()) &&
                widget_in_input_scope(scope, contact->hit);
        };
        if (!valid() || contact->suppressed || contact->text || contact->slider || owns_drag(*contact) ||
            context.activation.menu_popup.is_active() || context.activation.combo_popup.combo()) {
            contact->hold_pending = false;
            return {};
        }
        if (context.click_timing.now() - contact->pressed_at < std::chrono::milliseconds{650}) return {};
        contact->hold_pending = false;
        auto secondary = contact->last;
        const auto original = contact->hit;
        // 通常の副ボタンの経路を呼ぶ前に、主ボタンの捕捉を終了する。 実際の
        // 主ボタンの解放で、新しく開いたダイアログを操作しないようにする。
        cancel(contact);
        popup_release_ = ReleaseKey{secondary.pointer_type, secondary.pointer_id, PointerButton::Primary};
        if (active_ || !live(*contact) || (context.live && !context.live()) ||
            !widget_in_input_scope(scope, original)) return {true, true};
        secondary.button = PointerButton::Secondary;
        secondary.action = PointerAction::Down;
        secondary.handled = false;
        (void)dispatch(context, scope, secondary);
        if (context.live && !context.live()) return {true, true};
        secondary.action = PointerAction::Up;
        secondary.handled = false;
        (void)dispatch(context, scope, secondary);
        return {true, true};
    }
    if (!contact->number_repeat.active()) return {};
    const bool changed = contact->number_repeat.advance(context.activation.number_edit, context.click_timing.now(), [&] {
        return active_ == contact && live(*contact) && (!context.live || context.live()) &&
            widget_in_input_scope(scope, contact->hit) &&
            context.editing.editing.focus.revision() == contact->focus_revision &&
            context.activation.combo_popup.revision() == contact->combo_revision &&
            context.activation.menu_popup.revision() == contact->menu_revision;
    });
    return {true, changed};
}

MouseInputResult MouseInputStateMachine::dispatch(MouseInputContext context,
    std::shared_ptr<Window> scope, PointerEvent event, bool primary_down, unsigned clicks) {
    if (event.pointer_type == PointerType::Touch ||
        !std::isfinite(event.position.x) || !std::isfinite(event.position.y)) return {};
    if (event.action == PointerAction::Up && event.button == PointerButton::Primary &&
        active_ && active_->hold_pending && active_->last.pointer_id == event.pointer_id &&
        active_->last.pointer_type == event.pointer_type) {
        if (drag_threshold_reached(active_->start, event.position, context.drag_threshold))
            active_->hold_pending = false;
        else (void)advance(context, scope);
    }
    const ReleaseKey release_key{event.pointer_type, event.pointer_id, event.button};
    // つかんだコンボのポップアップのスクロールつまみは、ネイティ
    // ブのバーと同様、 外へ出ても解放までポインターに追従する。
    if (auto& popup = context.activation.combo_popup; popup.dragging_thumb()) {
        if (!popup.combo()) popup.end_thumb_drag();
        else if (event.action == PointerAction::Move) {
            if (scope) (void)popup.drag_thumb(popup.layout(scope->bounds()), event.position);
            return {true, true};
        } else if (event.action == PointerAction::Up ||
                   event.action == PointerAction::Cancel) {
            popup.end_thumb_drag();
        }
    }
    if (event.action == PointerAction::Up && popup_release_ == release_key) {
        popup_release_.reset();
        if (active_ && active_->suppressed && active_->last.pointer_type == event.pointer_type &&
            active_->last.pointer_id == event.pointer_id &&
            active_->last.button == event.button) active_.reset();
        return {true, true};
    }
    if (event.action == PointerAction::Down && popup_release_ == release_key) popup_release_.reset();
    const auto incoming_live = [&] {
        return scope && scope->visible() && scope->enabled() && (!context.live || context.live());
    };
    const auto hit_at = [&] {
        return hit_test_widget(scope, event.position, context.tab_height, context.expand_height);
    };
    const auto before_cleanup = active_;
    const auto before_capture = capture_;
    discard_stale();
    if (before_cleanup && before_cleanup != active_) return {true, true};
    if (before_capture && before_capture != capture_) {
        cancel(active_);
        return {true, true}; // 除去された捕捉対象の下へ、古い解放イベントを送らない。
    }
    if (!incoming_live()) return {true};
    if (!accepts_pointer(event)) return {true};

    if (event.action == PointerAction::Down) {
        if (context.activation.menu_popup.is_active() || context.activation.combo_popup.combo() ||
            event.button != PointerButton::Primary) clicks_.cancel_for();
        if (active_) {
            if (active_->last.button != event.button) return {true};
            cancel(active_);
            if (active_ || !incoming_live()) return {true, true};
        }
        auto contact = std::make_shared<Contact>();
        contact->scope = scope; contact->live = context.live;
        contact->hit = hit_at(); contact->last = event; contact->start = event.position;
        // SDLのイベント時刻と steady_clock は基準時刻が異な
        // る。 長押し時間はタッチと同様、advance() と同じ時計で測
        // る。 ネイティブ時刻は、イベント間のダブルクリック判定だけに使う。
        contact->pressed_at = context.click_timing.now();
        contact->hold_pending = event.button == PointerButton::Primary;
        contact->drag = context.drag_drop;
        active_ = contact;
        const auto current = [&] { return active_ == contact && live(*contact); };
        auto& activation = context.activation;
        activation.text_selection = [&](const auto& edit, Point point, Modifiers modifiers) {
            if (!current()) return;
            const auto owner = edit_input_owner(context.editing, edit);
            if (!widget_in_input_scope(scope, owner)) return;
            auto selection_context = context.editing;
            selection_context.live = [current, was_live = selection_context.live] {
                return current() && (!was_live || was_live());
            };
            if (!select_edit_text_at(selection_context, edit, point, modifiers) || !current()) return;
            contact->text = edit; contact->text_owner = owner;
            contact->focus_revision = context.editing.editing.focus.revision();
        };
        auto& menu = activation.menu_popup;
        if (menu.is_active()) {
            if (!widget_in_input_scope(scope, menu.owner())) menu.close();
            else {
                contact->suppressed = true;
                popup_release_ = release_key;
                const bool handled = menu.click(event.position, scope->bounds(),
                                                {event.modifiers,event.button,event.physical_control});
                if (!current() || handled) return {true, true};
                popup_release_.reset();
                contact->suppressed = false;
            }
        }
        auto& popup = activation.combo_popup;
        if (event.button == PointerButton::Primary) {
            if (const auto combo = popup.combo()) {
                const bool inline_choice = combo == activation.inline_edit.choice();
                if (!combo_in_scope(scope, combo, activation.inline_edit)) popup.close();
                else {
                    contact->suppressed = true;
                    popup_release_ = release_key;
                    const auto geometry = popup.layout(scope->bounds());
                    const auto result = inline_choice
                        ? click_inline_choice(activation.inline_edit, popup, geometry, event.position)
                        : popup.click(geometry, event.position);
                    if (!current()) return {true, true};
                    if (!inline_choice) activation.combo_edit.synchronize();
                    if (result == ComboPopupClick::Anchor &&
                        layout_combo_box(*combo).edits_text(*combo, event.position)) {
                        if (inline_choice) activation.text_selection(activation.inline_edit.editor(), event.position, event.modifiers);
                        else if (!activation.focus || activation.focus(combo)) {
                            if (!current() || !widget_in_input_scope(scope, combo)) return {true, true};
                            activation.combo_edit.begin(combo);
                            activation.text_selection(activation.combo_edit.editor(), event.position, event.modifiers);
                        }
                    }
                    if (result != ComboPopupClick::Outside) return {true, true};
                    popup_release_.reset();
                    contact->suppressed = false;
                }
            }
        }
        const auto hit = contact->hit;
        const auto valid_hit = [&] { return current() && widget_in_input_scope(scope, hit); };
        if (!valid_hit()) return {true};
        if (event.button == PointerButton::Secondary && activation.inline_edit.active() &&
            activation.inline_edit.owner() == hit) {
            const auto editor = activation.inline_edit.editor();
            if (point_inside(editor->bounds(), event.position) && editor->on_pointer) {
                const auto notify = editor->on_pointer;
                notify(event);
                if (!valid_hit()) return {true, true};
                if (event.handled) { contact->suppressed = true; return {true, true}; }
            }
        }
        if (event.button == PointerButton::Secondary) {
            (void)select_context_item(hit, event.position);
            if (!valid_hit()) return {true, true};
        }
        const bool consumed = dispatch_widget_pointer(hit, event);
        if (!valid_hit()) return {true,true}; // コールバックが作った新しい入力列を保持する。
        // 独自描画のシーン・イベント画面が押下を消費して
        // も、 キーボードフォーカス可能なら必ずそこへフ
        // ォーカスを移す。 activate_control
        // を通らないことで以前のペインが選択されたまま
        // となり、 Ctrl+Cがクリックしたメニュー
        // カードでなく古い対象をコピーすることを防ぐ。
        if (consumed && activation.focus && accepts_keyboard_focus(hit)) {
            (void)activation.focus(hit);
            if (!valid_hit()) return {true,true};
        }
        // D版のイベントパレットと同様、中クリックを操作として扱う経
        // 路は ToolBar に限定する。 他の主ボタン以外の入力
        // は、コンテキスト・補助操作を維持し、部品を有効化しない。
        const bool middle_toolbar = event.button == PointerButton::Middle &&
            static_cast<bool>(std::dynamic_pointer_cast<ToolBar>(hit));
        if (consumed || (event.button != PointerButton::Primary && !middle_toolbar)) {
            clicks_.cancel_for(scope); return {true, true};
        }
        const auto click_time=context.click_timing.event_time(event);
        if (!valid_hit()) return {true,true};
        const auto count = clicks ? clicks : clicks_.record(scope,hit,event,
            click_time,context.click_timing);
        if (!valid_hit()) return {true,true};
        const auto combo_revision = activation.combo_popup.revision();
        const auto menu_revision = activation.menu_popup.revision();
        contact->deferred_activation = defers_activation_until_release(hit);
        if (!contact->deferred_activation) {
            activate_control(scope, hit, event.position, event.modifiers, activation, context.live,
                             event.button, event.physical_control,
                             event.suppress_default_item_view_selection);
            if (!valid_hit()) return {true, true};
        }
        if (middle_toolbar) {
            clicks_.cancel_for(scope);
            if (!contact->deferred_activation) return {true, true, hit};
        }
        if (count >= 2) {
            // 選択は押下時に行うが、子ダイアログを開き
            // 得る有効化は、対応する解放時に完了する。
            contact->deferred_double_activation = true;
        }
        if (!valid_hit()) return {true, true};
        contact->candidate = contact->drag && contact->drag->has_source(hit) && !contact->text;
        contact->slider = std::dynamic_pointer_cast<Slider>(hit);
        if (const auto number = std::dynamic_pointer_cast<NumberEdit>(hit);
            number && activation.number_edit.active_for(number) &&
            combo_revision == activation.combo_popup.revision() && menu_revision == activation.menu_popup.revision()) {
            contact->number_repeat.arm(number, event.position, context.click_timing.now());
            contact->focus_revision = context.editing.editing.focus.revision();
            contact->combo_revision = combo_revision; contact->menu_revision = menu_revision;
        }
        return {true, true, hit};
    }

    const auto contact = active_;
    if (!contact && !capture_ && event.action == PointerAction::Up)
        return {true}; // ここへ押下が届いていないか、その操作はキャンセル済み。
    if (!contact && event.action == PointerAction::Cancel) {
        const auto capture = capture_;
        if (capture && capture->scope.lock() == scope) {
            capture_.reset(); // コールバックが新しい入力の捕捉を設定する場合がある。
            (void)dispatch_widget_pointer(capture->widget, event);
        }
        return {true, static_cast<bool>(capture)};
    }
    if (contact && (event.action == PointerAction::Up && event.button != contact->last.button)) return {true};
    if (contact) {
        if (drag_threshold_reached(contact->start,event.position,context.drag_threshold)) {
            contact->hold_pending = false;
            clicks_.cancel_for(contact->scope.lock());
        }
        // 開始ボタンは保持し、終了には最新の入力サンプルを使う。
        contact->last.position = event.position;
        contact->last.screen_position = event.screen_position;
        contact->last.screen_position_available = event.screen_position_available;
        contact->last.pressure = event.pressure;
        contact->last.modifiers = event.modifiers;
        if (event.action == PointerAction::Move)
            contact->number_repeat.move(event.position, context.click_timing.now());
    }
    if (contact && (event.action == PointerAction::Cancel ||
        (contact->scope.lock() != scope && !owns_drag(*contact)) ||
        (event.action == PointerAction::Move && contact->last.button == PointerButton::Primary && !primary_down))) {
        cancel(contact, nullptr, true, event.action == PointerAction::Cancel
            ? event.cancel_reason : PointerCancelReason::Explicit); return {true, true};
    }
    if (event.action != PointerAction::Move && event.action != PointerAction::Up) return {true};
    const auto hit = hit_at();
    const auto text_live = [&] {
        return !contact || !contact->text ||
            (edit_input_owner(context.editing, contact->text) == contact->text_owner &&
             context.editing.editing.focus.revision() == contact->focus_revision &&
             widget_in_input_scope(contact->scope.lock(), contact->text_owner));
    };
    auto target = hit;
    const auto capture = capture_;
    if (capture) target = capture->widget;
    else if (contact && contact->text) {
        if (!text_live()) {
            cancel(contact); return {true, true};
        }
        target = contact->text;
    } else if (contact && contact->slider) target = contact->slider;
    const bool release = event.action == PointerAction::Up;
    const bool preserve_table_double_click = release && contact &&
        contact->deferred_double_activation && !contact->suppressed &&
        !owns_drag(*contact) && target == contact->hit && hit == contact->hit &&
        incoming_live() && live(*contact) && widget_in_input_scope(scope, contact->hit);
    if (preserve_table_double_click) event.allow_deferred_double_click = true;
    if (release) {
        if (contact) { contact->number_repeat.cancel(); active_.reset(); }
        if (capture_ == capture) capture_.reset();
    }
    const bool consumed = (!contact || !contact->suppressed) && dispatch_widget_pointer(target, event);
    if (!contact) return {true, consumed};
    const auto valid = [&] {
        return incoming_live() && live(*contact) && (release ? !active_ : active_ == contact) &&
            widget_in_input_scope(contact->scope.lock(), contact->hit) && text_live();
    };
    // 表は範囲選択の後処理に PointerAction::Up
    // を消費するが、成立済みのドラッグはキャンセルしない。 転
    // 送元・先のウィジェットが同じイベントを処理していても、解放
    // は共通のドラッグ・ドロップ側が管理する。 通常の消費済み
    // 経路より先に完了し、所有関係を再確認して、 コールバック
    // が作った新しいドラッグを古い解放で完了しないようにする。
    if (release && owns_drag(*contact)) {
        if (valid() && hit_at() == hit && widget_in_input_scope(scope, hit))
            contact->drag->finish(hit, event.position, hit_test_item_view(hit, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
        else if (owns_drag(*contact)) contact->drag->cancel();
        return {true, true};
    }
    if (!valid() || consumed) {
        contact->number_repeat.cancel();
        if (release) { if (owns_drag(*contact)) contact->drag->cancel(); }
        else if (!valid()) cancel(contact);
        return {true, true};
    }
    if (release) {
        if (owns_drag(*contact)) {
            if (hit_at() == hit && widget_in_input_scope(scope, hit))
                contact->drag->finish(hit, event.position, hit_test_item_view(hit, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
            else contact->drag->cancel();
        } else if (hit == contact->hit && widget_in_input_scope(scope, contact->hit)) {
            const auto activated = contact->hit;
            if (contact->deferred_activation) {
                activate_control(scope, activated, event.position, event.modifiers,
                                 context.activation, context.live, event.button, event.physical_control);
            }
            if (contact->deferred_double_activation &&
                widget_in_input_scope(scope, activated)) {
                auto double_click = event;
                double_click.action = PointerAction::DoubleClick;
                double_click.handled = false;
                const bool handled = dispatch_widget_pointer(activated, double_click);
                if (!handled && widget_in_input_scope(scope, activated))
                    activate_control_double(scope, activated, event.position,
                                            context.activation, event.modifiers);
            }
            if (contact->deferred_activation || contact->deferred_double_activation)
                return {true, true, activated};
        }
        return {true, true};
    }
    if (contact->text) {
        auto selection_context = context.editing;
        selection_context.live = [valid, was_live = selection_context.live] {
            return valid() && (!was_live || was_live());
        };
        drag_edit_text_to(selection_context, contact->text, event.position);
    }
    if (contact->slider) (void)set_slider_value_from_point(*contact->slider, event.position);
    if (!valid()) { cancel(contact); return {true, true}; }
    if (contact->candidate && !contact->drag_id && contact->drag && !contact->drag->active() &&
        drag_threshold_reached(contact->start, event.position, context.drag_threshold)) {
        if (contact->drag->begin(contact->hit, contact->start, contact->scope.lock())) {
            contact->drag_id = contact->drag->session_id();
            if (!valid()) {
                if (owns_drag(*contact)) contact->drag->cancel();
                return {true, true};
            }
        }
    }
    if (owns_drag(*contact) && hit_at() == hit)
        contact->drag->update(hit, event.position, hit_test_item_view(hit, event.position), scope, DropOperation::None, event.modifiers, event.physical_control);
    return {true, true};
}

} // namespace cwxeditor::lib
