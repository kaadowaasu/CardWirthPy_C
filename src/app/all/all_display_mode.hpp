#pragma once

#include "app/all/all_layout.hpp"

#include <optional>

namespace cardwirth::app::all {

struct PixelSize {
    int width{};
    int height{};
};

struct AllWindowConfig {
    int width{};
    int height{};
    int minimum_width{};
    int minimum_height{};
    bool resizable{true};
    bool fullscreen{};
    bool high_pixel_density{true};
    std::optional<float> aspect_ratio{};
};

[[nodiscard]] AllWindowConfig make_native_pc_window_config() noexcept;
[[nodiscard]] PixelSize largest_native_pc_size(int available_width,
                                               int available_height) noexcept;

class AspectResizeGuard {
public:
    [[nodiscard]] std::optional<PixelSize> correction(int width, int height) noexcept;
    void acknowledge(int width, int height) noexcept;
    [[nodiscard]] bool correcting() const noexcept { return correcting_; }

private:
    bool correcting_{};
    int requested_width_{};
    int requested_height_{};
    int last_width_{kAllPcDesignWidth};
    int last_height_{kAllPcDesignHeight};
};

}  // namespace cardwirth::app::all
