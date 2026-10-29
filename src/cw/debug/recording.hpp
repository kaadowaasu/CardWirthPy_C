#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::debug::recording {
// Python cw.debug.recording.save/load の公開エントリポイント。
::cw::lib::String save(::cw::lib::String arg_path);
std::vector<std::uint8_t> serialize();
void load(::cw::lib::String arg_path);

// load() が cwpy のイベントキューへ渡す型付きコールバックの橋渡し。
void load_func(::cw::lib::Integer arg_areaid, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data);

// Python版と同じくモジュール実行時の処理は持たない。
void main();
}

