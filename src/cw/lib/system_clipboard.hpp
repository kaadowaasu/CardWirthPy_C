#pragma once

#include <string>

namespace cw::lib {

// Engine のダイアログと Explorer のテキスト編集で共有す
// る、唯一のシステムクリップボード境界。両関数はどのスレッドからでも呼べ
// るが、SDL のクリップボードへのアクセスは常にメインスレッドで行う。
[[nodiscard]] std::string system_clipboard_text();
void set_system_clipboard_text(const std::string& text);

}  // namespace cw::lib
