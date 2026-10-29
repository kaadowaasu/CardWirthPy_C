#pragma once

#include "cw/frame.hpp"

namespace cw::frame {

std::shared_ptr<NativeFrameAdapter> make_sdl_native_adapter();
// ここではウィンドウの位置・寸法だけを取得し、PyのGetClientPosition相当の計算はFrameが行う。
std::optional<std::pair<::cw::lib::Rect, ::cw::lib::Size>>
query_sdl_frame_geometry(const Frame& frame);

} // namespace cw::frame
