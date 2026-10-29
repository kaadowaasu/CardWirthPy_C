#pragma once

// cwx.editor.gui.dwt.datawindow
// に対応するモジュールの印。 データ画
// 面の処理は、MainWindowに登
// 録されたareatable::AreaTable
// とflagspane::FlagsPane
// だけが担う。 ここには別のシナリ
// オモデル、選択・操作・遷移の状態
// 管理や表示実装を設けない。 D
// からC++へのモジュール対応を
// 保つため、ヘッダーだけを残す。

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areatable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::datawindow {

inline constexpr bool uses_registered_main_window_panes = true;

}  // namespace cwxeditor::cwx::editor::gui::dwt::datawindow
