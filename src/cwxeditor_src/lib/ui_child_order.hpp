#pragma once
#include "components.hpp"

namespace cwxeditor::lib {
// フレーム準備とポインターの対象判定で共有する、背面から前面への順
// 序。指定された配置順やタブ順は変えない。最上位ウィンドウは独自の
// 所有スタックを持ち、表示上は通常のコンテナーの子として扱わない。
[[nodiscard]] std::vector<WidgetPtr> presentation_children(const Container& owner);
} // namespace cwxeditor::lib
