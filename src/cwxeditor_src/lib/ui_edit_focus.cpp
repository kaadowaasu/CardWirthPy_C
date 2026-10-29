#include "ui_edit_focus.hpp"
#include "ui_layout.hpp"

namespace cwxeditor::lib {
namespace {

bool finish_edits(EditFocusContext context, const WidgetPtr& target) {
    const auto revision = context.focus.revision();
    const auto number_revision = context.number_edit.revision();
    const auto combo_revision = context.combo_edit.revision();
    const auto logical_focus_owner = target
        ? target->focus_transaction_owner.lock() : WidgetPtr{};
    if (context.inline_edit.active() && context.inline_edit.owner() != target &&
        context.inline_edit.owner() != logical_focus_owner) {
        if (context.combo_popup.is_open_for(context.inline_edit.choice()))
            context.combo_popup.close();
        const auto popup_revision = context.combo_popup.revision();
        (void)context.inline_edit.commit();
        // 確定処理は通知前に自身の編集欄を終了する。 ここに有効な編集
        // 欄や、新しい数値・コンボ・ポップアップの操作があれば、 古い
        // フォーカス要求ではなく、その通知で作られたものとして扱う。
        if (context.inline_edit.active() || context.number_edit.revision() != number_revision ||
            context.combo_edit.revision() != combo_revision ||
            context.combo_popup.revision() != popup_revision) return false;
    }
    // フォーカスのポインターだけの比較では、解除後の再フォーカスを見
    // 逃す。 通知コールバックが作った新しい編集状態を終了しない。
    if (context.focus.revision() != revision) return false;
    const auto inline_editor = context.inline_edit.active() ? context.inline_edit.editor() : nullptr;
    const auto popup_revision = context.combo_popup.revision();
    context.number_edit.finish();
    if (context.focus.revision() != revision || context.number_edit.owner() ||
        context.combo_edit.revision() != combo_revision || context.combo_popup.revision() != popup_revision ||
        (context.inline_edit.active() ? context.inline_edit.editor() : nullptr) != inline_editor) return false;
    context.combo_edit.finish();
    if (const auto combo = context.combo_popup.combo(); combo && combo != target)
        context.combo_popup.close();
    return true;
}

void begin_focused_edit(EditFocusContext context) {
    const auto actual = context.focus.focused();
    if (const auto number = std::dynamic_pointer_cast<NumberEdit>(actual)) {
        if (context.number_edit.active_for(number)) context.number_edit.synchronize();
        else context.number_edit.begin(number);
    }
    if (const auto combo = std::dynamic_pointer_cast<ComboBox>(actual)) {
        if (context.combo_edit.active_for(combo)) context.combo_edit.synchronize();
        else context.combo_edit.begin(combo);
    }
}

}  // namespace

void set_edit_focus(EditFocusContext context, const WidgetPtr& target,
                    const WidgetPtr& scope) {
    bool completed = true;
    (void)context.focus.set(target, scope, [context, target, &completed] {
        completed = finish_edits(context, target);
        return completed;
    });
    if (!completed) return;
    begin_focused_edit(context);
    const auto focused = context.focus.focused();
    if (!focused || focused->reveal_on_focus())
        reveal_window_control(scope, focused);
}

FocusKeyResult dispatch_edit_focus_key(EditFocusContext context,
    const WidgetPtr& scope, Key key, Modifiers modifiers) {
    const auto mods = static_cast<std::uint32_t>(modifiers);
    if (key == Key::Tab && !(mods & static_cast<std::uint32_t>(Modifiers::Alt | Modifiers::Meta))) {
        if (!finish_edits(context, {})) return {true, {}};
        // 確定コールバックが、フォーカスを明示的に変えずに範囲を除去する
        // 場合がある。 その操作部品へ、このキーの残りの処理を送らない。
        if (!scope || !scope->visible() || !scope->enabled()) {
            (void)context.focus.set({});
            return {true, {}};
        }
    }
    const auto result = context.focus.key(scope, key, modifiers);
    if (result.handled) {
        begin_focused_edit(context);
        const auto focused = context.focus.focused();
        if (!focused || focused->reveal_on_focus())
            reveal_window_control(scope, focused);
    }
    return result;
}

}  // namespace cwxeditor::lib
