#pragma once

// cwx/editor/gui/dwt/flagdirtree.d
// に対応する互換用のヘッダー。 実際の共通FlagDirTree
// はFlagPaneの一部としてflagspane.cpp
// で一度だけ実装する。 モジュール対応
// とソース互換性のために残すが、ここ
// に別の表示・状態管理を追加しない。

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::flagdirtree {

namespace pane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;

using String = pane::String;
using StringView = pane::StringView;
using Parent = pane::Parent;
using PanelPtr = pane::PanelPtr;
using FlagPtr = pane::FlagPtr;
using StepPtr = pane::StepPtr;
using VariantPtr = pane::VariantPtr;
using FlagDirPtr = pane::FlagDirPtr;
using UseCounterPtr = pane::UseCounterPtr;
using Commons = pane::Commons;
using Props = pane::Props;
using FlagTable = pane::FlagTable;
using UndoManager = pane::UndoManager;
using SelectionEvent = pane::SelectionEvent;
using FlagDirTree = pane::FlagDirTree;

}  // namespace cwxeditor::cwx::editor::gui::dwt::flagdirtree
