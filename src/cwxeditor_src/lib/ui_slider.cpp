#include "ui_slider.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
namespace {
double value_ratio(const Slider& slider) noexcept {
    if (!std::isfinite(slider.minimum) || !std::isfinite(slider.maximum) ||
        !std::isfinite(slider.value) || slider.maximum<=slider.minimum) return 0;
    const double value=std::clamp(slider.value,slider.minimum,slider.maximum);
    // 範囲の半分ずつを引く方法で、DBL_MAXをまたぐ有限範囲も扱う。
    const double range=slider.maximum*.5-slider.minimum*.5;
    return range>0 ? std::clamp((value*.5-slider.minimum*.5)/range,0.0,1.0) : 0;
}
}
std::optional<double> SliderLayout::ratio_at(Point point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || last<=first) return {};
    const double axis=orientation==Orientation::Horizontal ? point.x : point.y;
    const double ratio=std::clamp((axis-first)/(last-first),0.0,1.0);
    return orientation==Orientation::Horizontal ? ratio : 1.0-ratio;
}
SliderLayout layout_slider(const Slider& slider) noexcept {
    SliderLayout result; result.orientation=slider.orientation;
    const auto r=slider.bounds();
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
        !std::isfinite(r.height) || r.width<=0 || r.height<=0) return result;
    const bool horizontal=slider.orientation==Orientation::Horizontal;
    const float length=horizontal?r.width:r.height, cross=horizontal?r.height:r.width;
    const float inset=std::min(scaled_ui_metric(3),length*.5F);
    // SWTのScaleと同様、配置セルが大きくなっても小さいつまみを中央に保つ。
    const float thumb_length=std::min(scaled_ui_metric(10),length-2*inset);
    const float thumb_cross=std::min(scaled_ui_metric(20),cross);
    const float cross_inset=(cross-thumb_cross)*.5F;
    const float thickness=std::min(scaled_ui_metric(4),cross);
    const float origin=horizontal?r.x:r.y;
    result.first=origin+inset+thumb_length*.5F;
    result.last=origin+length-inset-thumb_length*.5F;
    const float ratio=static_cast<float>(value_ratio(slider));
    const float position=std::lerp(result.first,result.last,horizontal?ratio:1-ratio);
    if (horizontal) {
        result.track={r.x+inset,r.y+(r.height-thickness)*.5F,length-2*inset,thickness};
        result.thumb={position-thumb_length*.5F,r.y+cross_inset,thumb_length,cross-2*cross_inset};
    } else {
        result.track={r.x+(r.width-thickness)*.5F,r.y+inset,thickness,length-2*inset};
        result.thumb={r.x+cross_inset,position-thumb_length*.5F,cross-2*cross_inset,thumb_length};
    }
    return result;
}
std::optional<double> slider_value_at(const Slider& slider, Point point) noexcept {
    if (!slider.enabled() || !std::isfinite(slider.minimum) || !std::isfinite(slider.maximum) ||
        slider.maximum<slider.minimum) return {};
    const auto ratio=layout_slider(slider).ratio_at(point);
    return ratio ? std::optional<double>{std::lerp(slider.minimum,slider.maximum,*ratio)} : std::nullopt;
}
} // namespace cwxeditor::lib
