#pragma once

// cw/skin/__init__.py の import registry と空の main エントリ。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::skin {
extern std::vector<::cw::lib::String> g_py_all;

void main();
}

