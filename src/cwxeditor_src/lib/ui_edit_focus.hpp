#pragma once

#include "ui_combo_edit.hpp"
#include "ui_number_edit.hpp"

namespace cwxeditor::lib {

// 編集とフォーカスの共通トランザクション。表示側は結果のフォーカスをIMEやアクセシビリ
// ティのホストへ渡すだけ。ネイティブのウィンドウやイベントの型はここには持ち込まない。
struct EditFocusContext {
    FocusStateMachine& focus;
    InlineEditStateMachine& inline_edit;
    NumberEditSession& number_edit;
    ComboEditSession& combo_edit;
    ComboPopupStateMachine& combo_popup;
};

void set_edit_focus(EditFocusContext context, const WidgetPtr& target,
                    const WidgetPtr& scope = {});
[[nodiscard]] FocusKeyResult dispatch_edit_focus_key(
    EditFocusContext context, const WidgetPtr& scope, Key key, Modifiers modifiers);

}  // namespace cwxeditor::lib
