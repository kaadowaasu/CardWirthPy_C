#pragma once

namespace cwxeditor::lib {
// F2でセル編集を開始したときに、選択一覧を自動的に開くかどうかのEditor
// 設定。ユーザーが明示的にポップアップを開く操作には影響しない。
void set_combo_edit_popup_enabled(bool value) noexcept;
[[nodiscard]] bool combo_edit_popup_enabled() noexcept;
} // namespace cwxeditor::lib
