#include "ui_image_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace cwxeditor::lib {
namespace {
bool valid_bounds(Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
        std::isfinite(r.height) && std::isfinite(r.x+r.width) &&
        std::isfinite(r.y+r.height) && r.width>0 && r.height>0;
}
}

ImageViewportPresentation prepare_image_viewport(const Rect bounds, const bool border) noexcept {
    ImageViewportPresentation result;
    if (!valid_bounds(bounds)) return result;
    result.outer = result.content = bounds;
    if (!border) return result;
    result.recessed_border = true;
    result.content.x += 1.0F;
    result.content.y += 1.0F;
    result.content.width = std::max(0.0F, result.content.width - 2.0F);
    result.content.height = std::max(0.0F, result.content.height - 2.0F);
    return result;
}

ImagePresentation prepare_image_presentation(std::shared_ptr<const ImageData> image,
    std::size_t requested_frame, Rect bounds, ImageScaleMode mode, Interpolation interpolation) {
    ImagePresentation result;
    result.interpolation=interpolation;
    if (!image || image->frames.empty() || !valid_bounds(bounds)) return result;
    const auto frame=std::min(requested_frame,image->frames.size()-1);
    const float width=static_cast<float>(image->frames[frame].pixel_width);
    const float height=static_cast<float>(image->frames[frame].pixel_height);
    if (width<=0 || height<=0) return result;
    result.image=std::move(image); result.frame=frame;
    if (mode==ImageScaleMode::Tile) {
        result.tiled_bounds=bounds; result.tile_size={width,height};
        return result;
    }
    if (mode==ImageScaleMode::Stretch) {
        result.single=ImageBlitPresentation{bounds,{}};
        return result;
    }
    float scale=1;
    if (mode==ImageScaleMode::Fit) scale=std::min(bounds.width/width,bounds.height/height);
    else if (mode==ImageScaleMode::Fill) scale=std::max(bounds.width/width,bounds.height/height);
    if (!(scale>0) || !std::isfinite(scale)) return {};
    if (mode==ImageScaleMode::Fill) {
        const float source_width=bounds.width/scale,source_height=bounds.height/scale;
        result.single=ImageBlitPresentation{bounds,Rect{(width-source_width)*.5F,
            (height-source_height)*.5F,source_width,source_height}};
    } else {
        const float target_width=width*scale,target_height=height*scale;
        const bool center=mode==ImageScaleMode::Center || mode==ImageScaleMode::Fit;
        result.single=ImageBlitPresentation{
            {center ? bounds.x+(bounds.width-target_width)*.5F : bounds.x,
             center ? bounds.y+(bounds.height-target_height)*.5F : bounds.y,
             target_width,target_height},{}};
    }
    return result;
}
ImagePresentation prepare_image_layer_presentation(std::shared_ptr<const ImageData> image,
    const ImageLayerSpec& layer, Rect bounds) {
    ImagePresentation result;
    if(!image || image->frames.empty() || !valid_bounds(bounds) ||
       !std::isfinite(layer.offset.x) || !std::isfinite(layer.offset.y) ||
       !std::isfinite(layer.scale) || layer.scale<=0) return result;
    const auto frame=std::min(layer.frame,image->frames.size()-1);
    const auto& pixels=image->frames[frame];
    if(!pixels.pixel_width || !pixels.pixel_height) return result;
    auto size=layer.logical_size;
    if(size.width==0 && size.height==0) {
        size=pixels.logical_size;
        if(size.width==0 && size.height==0)
            size={static_cast<float>(pixels.pixel_width),static_cast<float>(pixels.pixel_height)};
    }
    size.width*=layer.scale; size.height*=layer.scale;
    if(!valid_bounds({0,0,size.width,size.height})) return result;
    if(layer.shrink_to_fit) {
        const auto scale=std::min({1.0F,bounds.width/size.width,bounds.height/size.height});
        size.width=std::floor(size.width*scale); size.height=std::floor(size.height*scale);
    }
    // D版と同様、画像レイヤーが大きすぎる場合の負のオフセットも含め、整数除算を使う。
    const Rect destination{bounds.x+layer.offset.x+(layer.centered?std::trunc((bounds.width-size.width)*.5F):0),
        bounds.y+layer.offset.y+(layer.centered?std::trunc((bounds.height-size.height)*.5F):0),size.width,size.height};
    if(!valid_bounds(destination)) return result;
    result.image=std::move(image); result.frame=frame; result.interpolation=layer.interpolation;
    result.single=ImageBlitPresentation{destination,{}};
    return result;
}
void ImagePresentation::for_each_blit(
    const std::function<void(const ImageBlitPresentation&)>& draw) const {
    if (!image || !draw) return;
    if (single) { draw(*single); return; }
    if (!tiled_bounds || !valid_bounds(*tiled_bounds) ||
        !std::isfinite(tile_size.width) || !std::isfinite(tile_size.height) ||
        tile_size.width<=0 || tile_size.height<=0) return;
    const auto r=*tiled_bounds;
    // 大きな全体座標でも増分を失わないよう、ローカルのオフセットをdoubleで持つ。
    for (double y=0;y<r.height;) {
        for (double x=0;x<r.width;) {
            const float w=static_cast<float>(std::min<double>(tile_size.width,r.width-x));
            const float h=static_cast<float>(std::min<double>(tile_size.height,r.height-y));
            draw({{r.x+static_cast<float>(x),r.y+static_cast<float>(y),w,h},Rect{0,0,w,h}});
            const double next=x+tile_size.width;
            if (next<=x) break;
            x=next;
        }
        const double next=y+tile_size.height;
        if (next<=y) break;
        y=next;
    }
}
} // namespace cwxeditor::lib
