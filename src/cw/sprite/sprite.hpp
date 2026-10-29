#pragma once

// cw/sprite/__init__.py のimport registryと空のmainエントリ。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::sprite {
extern std::vector<::cw::lib::String> g_py_all;

void main();
}

