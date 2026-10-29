#pragma once

#include "types.hpp"

namespace cwxeditor::lib {

struct ControlPalette {
    Color foreground{0U, 0U, 0U, 255U};
    Color background{240U, 240U, 240U, 255U};
};

// ネイティブのコントロール配色。ホストに対応する機能がない場合は共通の
// 既定値を使う。描画規則とウィジェットの明示的な指定は共通処理で扱う。
[[nodiscard]] ControlPalette system_control_palette() noexcept;

}  // namespace cwxeditor::lib
