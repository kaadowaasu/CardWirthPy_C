#include "app/all/all_display_mode.hpp"

#include <algorithm>
#include <cmath>

namespace cardwirth::app::all {

AllWindowConfig make_native_pc_window_config() noexcept
{
    return {kAllPcDesignWidth, kAllPcDesignHeight,
            static_cast<int>(std::lround(kAllPcDesignWidth * kAllPcMinimumScale)),
            static_cast<int>(std::lround(kAllPcDesignHeight * kAllPcMinimumScale)),
            true, false, true, kAllPcAspect};
}

PixelSize largest_native_pc_size(int available_width, int available_height) noexcept
{
    if (available_width <= 0 || available_height <= 0) return {};
    const double scale = std::min(
        static_cast<double>(available_width) / kAllPcDesignWidth,
        static_cast<double>(available_height) / kAllPcDesignHeight);
    return {
        std::max(1, static_cast<int>(std::floor(kAllPcDesignWidth * scale))),
        std::max(1, static_cast<int>(std::floor(kAllPcDesignHeight * scale)))};
}

std::optional<PixelSize> AspectResizeGuard::correction(int width, int height) noexcept
{
    if (width <= 0 || height <= 0) return std::nullopt;
    if (correcting_) {
        const bool accepted = width == requested_width_ && height == requested_height_;
        const bool ratio_is_valid =
            std::abs(static_cast<float>(width) / static_cast<float>(height) -
                     kAllPcAspect) <= 0.001F;
        acknowledge(width, height);
        if (accepted || ratio_is_valid) return std::nullopt;
        // コンポジターまたはウィンドウマネージャーが前の要求を
        // 拒否、または別のサイズで上書きした。 ループ防止状態
        // を解除し、SDLが報告した実際の寸法から補正し直す。
        correcting_ = false;
    }

    const float actual = static_cast<float>(width) / static_cast<float>(height);
    if (std::abs(actual - kAllPcAspect) <= 0.001F) {
        last_width_ = width;
        last_height_ = height;
        return std::nullopt;
    }

    const int delta_width = std::abs(width - last_width_);
    const int delta_height = std::abs(height - last_height_);
    PixelSize target{width, height};
    if (delta_width >= delta_height) {
        target.height = std::max(1, static_cast<int>(std::lround(width / kAllPcAspect)));
    } else {
        target.width = std::max(1, static_cast<int>(std::lround(height * kAllPcAspect)));
    }
    target.width = std::max(target.width,
        static_cast<int>(std::lround(kAllPcDesignWidth * kAllPcMinimumScale)));
    target.height = std::max(target.height,
        static_cast<int>(std::lround(kAllPcDesignHeight * kAllPcMinimumScale)));
    requested_width_ = target.width;
    requested_height_ = target.height;
    correcting_ = true;
    return target;
}

void AspectResizeGuard::acknowledge(int width, int height) noexcept
{
    if (correcting_ && width == requested_width_ && height == requested_height_)
        correcting_ = false;
    else if (correcting_ &&
             std::abs(static_cast<float>(width) / std::max(1, height) - kAllPcAspect) <= 0.001F)
        correcting_ = false;
    last_width_ = width;
    last_height_ = height;
}

}  // namespace cardwirth::app::all
