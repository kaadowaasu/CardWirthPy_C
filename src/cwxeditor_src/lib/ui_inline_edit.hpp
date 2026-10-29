#pragma once
#include "ui_text_layout.hpp"
#include "ui_number_edit.hpp"

namespace cwxeditor::lib {
struct InlineNumberStepper {
    NumberEditLayout geometry;
    bool increase_enabled{}, decrease_enabled{};
};
// 単独Editorで明示的に対象に指定された数値表セルだけを返
// す。配置計算は、幅を狭めた入力欄ではなくセル全体から始める。
[[nodiscard]] std::optional<InlineNumberStepper> prepare_inline_number_stepper(
    InlineEditStateMachine& edit, float row_height);
[[nodiscard]] bool step_inline_number(InlineEditStateMachine& edit, int direction);

[[nodiscard]] bool combo_in_scope(const WidgetPtr& scope, const std::shared_ptr<ComboBox>& combo,
                                  const InlineEditStateMachine& inline_edit);
// Editorとネイティブ表示側で、編集中の値・選択・確定の規則を共有する。ホストは
// グリフの計測値とクリップボードの受け渡しを提供し、独自の編集ロジックは持たない。
[[nodiscard]] bool dispatch_inline_edit_key(InlineEditStateMachine& edit,
    ComboPopupStateMachine& popup, Key key, Modifiers modifiers,
    std::size_t visible_rows, float line_height, const TextWidthMeasure& measure,
    const TextClipboardAccess& clipboard = {}, const std::function<bool()>& live = {});
[[nodiscard]] ComboPopupClick click_inline_choice(InlineEditStateMachine& edit,
    ComboPopupStateMachine& popup, const ComboPopupLayout& layout, Point point);
} // namespace cwxeditor::lib
