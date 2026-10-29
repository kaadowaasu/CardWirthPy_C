#include "ui_modal_operation.hpp"
#include "ui_window_stack.hpp"
#include <exception>

namespace cwxeditor::lib {
namespace {
bool can_focus_in_modal_context(const ModalOperationContext& context,
                                const std::shared_ptr<Window>& window) {
    return window && window->visible() && window->enabled() &&
           (!context.live || context.live()) && context.modal.allows(window) &&
           (!context.surface_available || context.surface_available(window));
}

void focus_in_scope(ModalOperationContext& context, const std::shared_ptr<Window>& window,
                    const WidgetPtr& preferred) {
    const auto revision=context.focus_revision ? std::optional{context.focus_revision()} : std::nullopt;
    if (!context.set_focus || !can_focus_in_modal_context(context, window)) return;
    const auto target=focus_target_in_scope(window, preferred, true);
    if(revision && context.focus_revision()!=*revision) return;
    context.set_focus(target);
}

bool finish_modal_operation(ModalOperationContext& context,
    ModalStateMachine::SessionId id, const std::function<void()>& dismiss = {}) {
    // C++版の共通モーダルセッションは、非ブロッキングのホストにも対応する。 二度
    // 目の終了で、終了済みセッションの画面を解放したりフォーカスを戻したりしない。
    if (!context.modal.active(id)) return false;
    std::exception_ptr error;
    bool owner_live{};
    WidgetPtr before;
    std::optional<std::uint64_t> revision;
    bool observed=true;
    try {
        if(context.current_focus) before=context.current_focus();
        if(context.focus_revision) revision=context.focus_revision();
    } catch(...) {error=std::current_exception();observed=false;}
    // 終了コールバックが戻り先のフォーカスを選ぶ・同じ画面を開き
    // 直す前に、保存した有効状態を戻す。 入力制限を後まで残すと
    // 所有者へのフォーカスが拒否され、新しいセッションの下に古い
    // ものが残る場合がある。 ネイティブの解放は一度だけにする。
    std::optional<ModalStateMachine::Session> session;
    try { session=context.modal.finish(id); }
    catch(...) { if(!error) error=std::current_exception(); }
    if (!session && !error) return false; // 入れ子のセッションがまだ入力制限を所有する。 画面の
    // 解放が失敗しても、入力制限を解除不能なままにしない。
    try { if(dismiss) dismiss(); } catch(...) { if(!error) error=std::current_exception(); }
    try {
        if(session) {
            try {
                // 終了処理ですでに所有者へフォーカスが戻っている場合
                // も、 現在の画面の方針を確認してから受け入れる。コ
                // ールバックが再入して別の画面を選ぶ場合があるため。
                bool restore=observed && ModalStateMachine::owner_available(*session) &&
                    can_focus_in_modal_context(context,session->owner);
                if(restore && context.current_focus) {
                    const auto current=context.current_focus();
                    const bool in_dialog=window_contains_widget(session->dialog,current.get());
                    // このダイアログ外の新しいフォーカスは、古いモーダルの保
                    // 存先ではなくコールバックの選択に属する。 変更番号を取
                    // 得できなくても、少なくとも終了通知中の変更は維持する。
                    if(current && !in_dialog &&
                       (current!=before || (revision &&
                        context.focus_revision()!=*revision))) restore=false;
                    if(!current && revision) {
                        const auto expected=*revision+
                            (window_contains_widget(session->dialog,before.get()) ? 1U : 0U);
                        if(context.focus_revision()!=expected) restore=false;
                    }
                }
                if(restore) focus_in_scope(context,session->owner,session->focused);
            }
            catch(...) { if(!error) error=std::current_exception(); }
            try { if(context.restore) context.restore(*session); }
            catch(...) { if(!error) error=std::current_exception(); }
            try {
                // 反映処理自体が所有者を閉じる・親を変更する場合があ
                // る。 受理した結果を、その失効した範囲へ返さない。
                owner_live=(!context.live || context.live()) &&
                    ModalStateMachine::owner_available(*session);
            } catch(...) { if(!error) error=std::current_exception(); }
        }
    } catch(...) { if(!error) error=std::current_exception(); }
    if(error) std::rethrow_exception(error);
    return owner_live;
}
}

std::optional<ModalStateMachine::SessionId> begin_modal_dialog_operation(
    ModalOperationContext context, std::shared_ptr<Dialog> dialog,
    std::shared_ptr<Window> owner, ModalDialogOperation operation) {
    if (!dialog || !operation.present || (context.live && !context.live()))
        return std::nullopt;
    const auto id = context.modal.begin(dialog, owner, context.focused);
    if (!id) return std::nullopt;
    const auto live = [&] {
        return (!context.live || context.live()) &&
               context.modal.owner_available(*id);
    };
    try {
        if (context.suspend_input) context.suspend_input();
        if (!live()) {
            (void)finish_modal_operation(context, *id, operation.dismiss);
            return std::nullopt;
        }
        operation.present();
        // 表示処理が同期的にダイアログを完了する場合も、D版のモーダ
        // ルの戻り値と同様に結果を受け付ける。 所有者が失効した場合
        // 、または結果なしで非表示になった場合だけを表示失敗とする。
        if (!live() || (!context.modal.can_continue(*id) &&
                        dialog->result == DialogResult::None)) {
            (void)finish_modal_operation(context, *id, operation.dismiss);
            return std::nullopt;
        }
        const auto preferred = context.current_focus
            ? context.current_focus() : context.focused;
        // 同期的に完了したダイアログには、初期フォーカスを設定しない。
        if (dialog->focus_first_control_on_open && dialog->result == DialogResult::None)
            focus_in_scope(context, dialog, preferred);
        return id;
    } catch (...) {
        try {
            if (context.modal.active(*id))
                (void)finish_modal_operation(context, *id, operation.dismiss);
        } catch (...) {
        }
        throw;
    }
}

bool finish_modal_dialog_operation(ModalOperationContext context,
    ModalStateMachine::SessionId id, ModalDialogOperation operation) {
    return finish_modal_operation(context, id, operation.dismiss);
}

DialogResult run_modal_dialog_operation(ModalOperationContext context,
    std::shared_ptr<Dialog> dialog, std::shared_ptr<Window> owner,
    ModalDialogOperation operation) {
    if(!dialog || !operation.present || !context.iterate ||
       (context.live && !context.live())) return DialogResult::Cancel;
    const auto dismiss = operation.dismiss;
    const auto id=begin_modal_dialog_operation(
        context, dialog, owner, std::move(operation));
    if(!id) return DialogResult::Cancel;
    const auto live=[&] {
        return (!context.live || context.live()) && context.modal.owner_available(*id);
    };
    DialogResult result=DialogResult::Cancel;
    try {
        if(live()) {
            bool host_stopped{};
            while(live() && context.modal.can_continue(*id)) {
                if(!context.iterate()) { host_stopped=true; break; }
            }
            if(!host_stopped && live() && dialog->result!=DialogResult::None)
                result=dialog->result;
        }
        dialog->result=result;
    } catch(...) {
        dialog->result=DialogResult::Cancel;
        try {
            if (context.modal.active(*id))
                (void)finish_modal_operation(context,*id,dismiss);
        } catch(...) {}
        throw;
    }
    // 終了通知後に dialog->result を書き換えない。 コー
    // ルバックが同じモデルを新しい操作用に開き直している場合があるため。
    return finish_modal_operation(context,*id,dismiss) ? result : DialogResult::Cancel;
}

bool run_external_dialog_operation(ModalOperationContext context,
    std::shared_ptr<Window> owner, ExternalDialogOperation operation) {
    if (!operation.completed || !context.iterate || (context.live && !context.live())) return false;
    const auto id=context.modal.begin_external(owner,context.focused);
    if (!id) return false;
    bool completed{};
    bool cancellation_sent{};
    const auto cancel=[&] {
        if(cancellation_sent) return;
        cancellation_sent=true;
        if(operation.cancel) operation.cancel();
    };
    const auto finish=[&] {
        return finish_modal_operation(context,*id);
    };
    const auto valid=[&] {
        return (!context.live || context.live()) && context.modal.can_continue(*id);
    };
    try {
        if(context.suspend_input) context.suspend_input();
        if(valid()) {
            const auto deadline=std::chrono::steady_clock::now()+operation.timeout;
            if(operation.start) operation.start();
            while(valid()) {
                const bool ready=operation.completed();
                if(!valid()) break;
                if(ready) { completed=true; break; }
                if(std::chrono::steady_clock::now()>=deadline) break;
                if(operation.platform_poll) operation.platform_poll();
                if(!valid()) break;
                const bool polled_ready=operation.completed();
                if(!valid()) break;
                if(polled_ready) { completed=true; break; }
                if(!context.iterate()) break;
            }
        }
        if(!completed) cancel();
    } catch(...) {
        try { cancel(); } catch(...) {}
        try { finish(); } catch(...) {}
        throw;
    }
    try {
        completed=finish() && completed;
        if(!completed) cancel();
    } catch(...) {
        try { cancel(); } catch(...) {}
        throw;
    }
    return completed;
}
} // namespace cwxeditor::lib
