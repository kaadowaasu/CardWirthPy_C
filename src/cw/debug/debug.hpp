#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/debug/__init__.py のimport registryと空のmainエントリ。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::debug {
extern std::vector<::cw::lib::String> g_py_all;

void main();
}

