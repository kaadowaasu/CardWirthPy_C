#pragma once
#include "components.hpp"
namespace cwxeditor::lib {
// 既存の意味上の入力接触から得る、読み取り専用の表示フィードバック。コ
// ントロールを実行したり、別のジェスチャー状態機械を作ったりしない。
struct PointerPressFeedback {
    WidgetPtr widget;
    Point origin, position;
};
} // namespace cwxeditor::lib
