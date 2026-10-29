#pragma once

#include "widget.hpp"
#include <algorithm>
#include <cmath>

namespace cardwirth::ui {

// ホーム・Explorer・ステータス領域などの固定UIは、ポイントサイズではな
// く 実測した画像の寸法で大きさを決める。ALL専用のホスト座標とは独立させる。
[[nodiscard]] inline Rect fit_text_rect(Rect available, float width, float height,
                                        float requested_scale, bool centered) noexcept {
    if (!std::isfinite(available.x) || !std::isfinite(available.y) ||
        !std::isfinite(available.width) || !std::isfinite(available.height) ||
        !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(requested_scale) ||
        available.width <= 0 || available.height <= 0 || width <= 0 || height <= 0 ||
        requested_scale <= 0) return {};
    const float scale = std::min({requested_scale, available.width / width, available.height / height});
    width *= scale;
    height *= scale;
    return {centered ? available.x + (available.width - width) * .5F : available.x,
            available.y + (available.height - height) * .5F, width, height};
}

} // namespace cardwirth::ui
