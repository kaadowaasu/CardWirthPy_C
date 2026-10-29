// pygame - Python Game Library
// Copyright (C) 2000-2003  Pete Shinners
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
// 
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the Free
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// 
// Pete Shinners
// pete@shinners.org
// Cursor bitmap constants from pygame 1.9.6 src_py/cursors.py.
// https://github.com/pygame/pygame/blob/1.9.6/src_py/cursors.py
#pragma once
#include <array>
#include <cstdint>
namespace cw::setting::pygame_cursor_data {
inline constexpr std::array<std::uint8_t,32> diamond_data{0,0,1,0,3,128,7,192,14,224,28,112,56,56,112,28,56,56,28,112,14,224,7,192,3,128,1,0,0,0,0,0};
inline constexpr std::array<std::uint8_t,32> diamond_mask{1,0,3,128,7,192,15,224,31,240,62,248,124,124,248,62,124,124,62,248,31,240,15,224,7,192,3,128,1,0,0,0};
inline constexpr std::array<std::uint8_t,32> broken_x_data{0,0,96,6,112,14,56,28,28,56,12,48,0,0,0,0,0,0,0,0,12,48,28,56,56,28,112,14,96,6,0,0};
inline constexpr std::array<std::uint8_t,32> broken_x_mask{224,7,240,15,248,31,124,62,62,124,30,120,14,112,0,0,0,0,14,112,30,120,62,124,124,62,248,31,240,15,224,7};
inline constexpr std::array<std::uint8_t,32> tri_left_data{0,0,96,0,120,0,62,0,63,128,31,224,31,248,15,254,15,254,7,128,7,128,3,128,3,128,1,128,1,128,0,0};
inline constexpr std::array<std::uint8_t,32> tri_left_mask{224,0,248,0,254,0,127,128,127,224,63,248,63,254,31,255,31,255,15,254,15,192,7,192,7,192,3,192,3,192,1,128};
inline constexpr std::array<std::uint8_t,32> tri_right_data{0,0,0,6,0,30,0,124,1,252,7,248,31,248,127,240,127,240,1,224,1,224,1,192,1,192,1,128,1,128,0,0};
inline constexpr std::array<std::uint8_t,32> tri_right_mask{0,7,0,31,0,127,1,254,7,254,31,252,127,252,255,248,255,248,127,240,3,240,3,224,3,224,3,192,3,192,1,128};
}
