#include "ui_chrome_input.hpp"
#include "ui_window_stack.hpp"
#include "ui_metrics.hpp"

namespace cwxeditor::lib {

bool ChromeInputStateMachine::valid(const Capture& capture) {
    return (!capture.live || capture.live()) &&
        editor_ui_text_scale() == capture.scale &&
        widget_in_input_scope(capture.scope.lock(), capture.owner);
}

void ChromeInputStateMachine::arrange(const Capture& capture) {
    if (capture.kind == Kind::Frame && valid(capture))
        arrange_widget(capture.owner, capture.owner->bounds());
}

WidgetPtr ChromeInputStateMachine::owner() const {
    return active_ ? active_->owner : WidgetPtr{};
}

std::shared_ptr<Window> ChromeInputStateMachine::scope() const {
    return active_ ? active_->scope.lock() : nullptr;
}

bool ChromeInputStateMachine::touch_active() const {
    return active_ && active_->type == PointerType::Touch;
}

void ChromeInputStateMachine::discard_stale() {
    if (active_ && !valid(*active_)) active_.reset();
}

void ChromeInputStateMachine::cancel_for(const WidgetPtr& root, bool commit_preview) {
    const auto ending = active_;
    if (!ending || (root && root != ending->scope.lock() &&
                    !contains_widget(root, ending->owner.get()))) return;
    active_.reset(); // 確定コールバックが、新しい入力の捕捉を始める場合がある。
    if (ending->kind == Kind::Scrollbar) ending->scrollbar.reset();
    if (commit_preview && valid(*ending) && ending->kind == Kind::Frame) {
        (void)ending->frame.finish();
        arrange(*ending);
    }
}

Action ChromeInputStateMachine::prepare_cancel_for(const WidgetPtr& root,
                                                    const bool commit_preview) {
    const auto capture=active_;
    if (!capture || (root && root != capture->scope.lock() &&
                     !contains_widget(root,capture->owner.get()))) return {};
    return [this,capture,commit_preview] {
        if (active_==capture) cancel_for({},commit_preview);
    };
}

Action ChromeInputStateMachine::prepare_pointer_cancel(const std::shared_ptr<Window>& scope,
                                                        const PointerEvent& event) {
    const auto capture = active_;
    if (!capture || capture->scope.lock() != scope || capture->type != event.pointer_type ||
        capture->pointer != event.pointer_id) return {};
    return [this, capture] {
        // 提供処理・サイズ変更のコールバックより前に解放する。例外の場合も同様。
        if (active_ == capture) cancel_for({}, true);
    };
}

ChromeInputResult ChromeInputStateMachine::dispatch(
    const std::shared_ptr<Window>& root, const PointerEvent& event,
    const ChromeInputContext& context) {
    ChromeInputResult result;
    if (const auto capture = active_) {
        result.handled = true;
        if (!valid(*capture)) {
            active_.reset();
            return result; // キャンセルされた操作の下にある対象を有効化しない。
        }
        result.cursor = capture->kind == Kind::Frame ? capture->frame.cursor() :
            capture->kind == Kind::Column ? CursorKind::SizeHorizontal : CursorKind::Default;
        if (event.pointer_type != capture->type || event.pointer_id != capture->pointer ||
            root != capture->scope.lock()) return result;
        if (event.action == PointerAction::Down) return result;
        if (event.action == PointerAction::Up && event.button != PointerButton::Primary) return result;
        if (event.action != PointerAction::Move && event.action != PointerAction::Up &&
            event.action != PointerAction::Cancel) return result;
        // 終了コールバックの前に解除し、無関係な新しい入力の捕捉を残す。
        const bool ending = event.action != PointerAction::Move;
        if (ending) active_.reset();
        if (event.action != PointerAction::Cancel) capture->last = event.position;
        switch (capture->kind) {
        case Kind::Frame:
            result.redraw = capture->frame.move(capture->last);
            if (result.redraw) arrange(*capture);
            // 確定コールバックには最終配置を見せる。 そこで開始された
            // 入力の捕捉を、後から今回の配置を適用して無効にしない。
            if (ending) result.redraw = capture->frame.finish() || result.redraw;
            if (!ending && active_ == capture && !capture->frame.owner()) active_.reset();
            break;
        case Kind::Column:
            if (event.action != PointerAction::Cancel)
                result.redraw = ending ? capture->column.finish(capture->last)
                                       : capture->column.move(capture->last);
            if (!ending && active_ == capture && !capture->column.owner()) active_.reset();
            break;
        case Kind::Scrollbar:
            if (event.action != PointerAction::Cancel)
                result.redraw = capture->scrollbar.move(capture->last);
            if (ending) {
                // つまみの押下色を解除し、通常の色で再描画する。
                capture->scrollbar.reset();
                result.redraw = true;
            }
            if (!ending && active_==capture && !capture->scrollbar.owner()) active_.reset();
            break;
        }
        return result;
    }
    if (!context.allow_begin || event.action != PointerAction::Down ||
        event.button != PointerButton::Primary || !root ||
        (context.live && !context.live())) return result;
    auto capture = std::make_shared<Capture>();
    capture->scope = root;
    capture->type = event.pointer_type;
    capture->pointer = event.pointer_id;
    capture->scale = editor_ui_text_scale();
    capture->last = event.position;
    capture->live = context.live;
    const auto hit = hit_test_widget(root, event.position,
        context.tab_height, context.expand_height);
    // 広めの仕切りのつかみ領域は、隣のコントロールの端と重なる。
    // 見えているスクロールバーを優先し、つまみのドラッグを枠のサイ
    // ズ変更にしない。 共通のスクロールバーの寿命検査も維持する。
    const bool scrollbar_started = widget_in_input_scope(root, hit) &&
        capture->scrollbar.begin(hit, event.position,
            text_scrollbar_access(std::dynamic_pointer_cast<TextEdit>(hit), context.text));
    // 文字計測はアプリのコードを呼び、入力範囲を置き換えたり新しい
    // 操作を始めたりする場合がある。 その置換先へ処理を続けない。
    if (active_ || (context.live && !context.live()) ||
        hit_test_widget(root, event.position, context.tab_height, context.expand_height) != hit ||
        (hit && !widget_in_input_scope(root, hit))) {
        result.handled = true;
        return result;
    }
    if (scrollbar_started) {
        capture->owner = hit;
        capture->kind = Kind::Scrollbar;
        result.redraw = true;
    } else if (capture->frame.begin(root, event.position)) {
        capture->owner = capture->frame.owner();
        capture->kind = Kind::Frame;
        result.cursor = capture->frame.cursor();
    } else {
        if (!widget_in_input_scope(root, hit)) return result;
        if (capture->column.begin(std::dynamic_pointer_cast<TableView>(hit),
                                        event.position, context.row_height)) {
            capture->owner = capture->column.owner();
            capture->kind = Kind::Column;
            result.cursor = CursorKind::SizeHorizontal;
        } else return result;
    }
    if (!valid(*capture)) { result.handled=true; return result; }
    active_ = std::move(capture);
    result.handled = result.began = true;
    return result;
}

} // namespace cwxeditor::lib
