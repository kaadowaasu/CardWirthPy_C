#pragma once
#include "components.hpp"

namespace cwxeditor::lib {
// トラックの両端はつまみの中心位置。ホストのOSや描画面に依存せず
// 、すべてのUI倍率で描画とポインター入力に同じ座標情報を使う。
struct SliderLayout {
    Rect track{}, thumb{};
    Orientation orientation{Orientation::Horizontal};
    float first{}, last{};
    [[nodiscard]] std::optional<double> ratio_at(Point point) const noexcept;
};
[[nodiscard]] SliderLayout layout_slider(const Slider& slider) noexcept;
[[nodiscard]] std::optional<double> slider_value_at(const Slider& slider, Point point) noexcept;
} // namespace cwxeditor::lib
