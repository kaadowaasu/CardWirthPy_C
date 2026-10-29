#pragma once

// cw/binary/image.py の移植。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::image {
extern ::cw::lib::String g_BINARY_HEADER;

bool path_is_code(::cw::lib::String arg_path);
::cw::lib::Bytes code_to_data(::cw::lib::String arg_code);
::cw::lib::String data_to_code(::cw::lib::Bytes arg_data);
}

