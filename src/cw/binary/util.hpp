#pragma once

// cw/binary/util.py の移植。Python 形式の可変引数の join_paths
// だけは、文字列引数を compat::Value で渡す。不正な非文字列
// 値を黙って変換せず、std::invalid_argument を投げる。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::util {
::cw::lib::String join_paths(const ::cw::lib::Arguments& arg_paths);
// ファイル名を扱う処理で共有する、Python の str.strip() と同じ空白の規則。
::cw::lib::String strip_python_spaces(const ::cw::lib::String& value);
::cw::lib::String check_filename(::cw::lib::String arg_name);
::cw::lib::String check_duplicate(::cw::lib::String arg_path);
::cw::lib::String repl_escapechar(::cw::lib::String arg_s);
::cw::lib::String repl_specialchar(::cw::lib::String arg_s);
::cw::lib::String repl_specialchar_repl_metachar(::cw::lib::Value arg_m);
void main();
}

