#include "drawing_clip.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cwxeditor::lib {
Rect intersect_rectangles(Rect a,Rect b) noexcept {
    const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,std::max(0.0F,std::min(a.x+a.width,b.x+b.width)-x),
                std::max(0.0F,std::min(a.y+a.height,b.y+b.height)-y)};
}
RasterClip prepare_raster_clip(Rect bounds,std::optional<RasterClip> inherited) noexcept {
    if(!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
       !std::isfinite(bounds.width) || !std::isfinite(bounds.height)) return {};
    // 原点は切り捨て、寸法は切り上げる既存の画素規則を維持する。
    const double x=std::floor(double(bounds.x)),y=std::floor(double(bounds.y));
    const double width=std::max(0.0,std::ceil(double(bounds.width)));
    const double height=std::max(0.0,std::ceil(double(bounds.height)));
    constexpr auto low=std::numeric_limits<std::int32_t>::min();
    constexpr auto high=std::numeric_limits<std::int32_t>::max();
    if(x<low || y<low || x>high || y>high || width>high || height>high ||
       x+width>high || y+height>high) return {};
    RasterClip result{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),
                      static_cast<std::int32_t>(width),static_cast<std::int32_t>(height)};
    if(!inherited) return result;
    const auto previous=*inherited;
    const auto left=std::max(previous.x,result.x),top=std::max(previous.y,result.y);
    const auto right=std::min(std::int64_t(previous.x)+std::max(0,previous.width),std::int64_t(result.x)+result.width);
    const auto bottom=std::min(std::int64_t(previous.y)+std::max(0,previous.height),std::int64_t(result.y)+result.height);
    return {left,top,static_cast<std::int32_t>(std::max<std::int64_t>(0,right-left)),
                     static_cast<std::int32_t>(std::max<std::int64_t>(0,bottom-top))};
}
} // namespace cwxeditor::lib
