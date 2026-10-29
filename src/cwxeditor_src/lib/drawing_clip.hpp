#pragma once
#include "types.hpp"

namespace cwxeditor::lib {
// 整数の切り取り座標を共通化し、アダプターはネイティブ描画の矩形型
// へ値を移すだけにする。 空の範囲を切り取り無効とは解釈しない。
struct RasterClip {
    std::int32_t x{},y{},width{},height{};
};
[[nodiscard]] Rect intersect_rectangles(Rect a,Rect b) noexcept;
[[nodiscard]] RasterClip prepare_raster_clip(Rect bounds,
    std::optional<RasterClip> inherited = {}) noexcept;
} // namespace cwxeditor::lib
