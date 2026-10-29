#include "image_composition.hpp"
#include "drawing_clip.hpp"
#include "cw/lib/simd_image.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cwxeditor::lib {
namespace {
bool valid_image(const ImageFrame& frame) {
    return frame.pixel_width && frame.pixel_height &&
        frame.pixel_width<=static_cast<unsigned>(std::numeric_limits<int>::max()) &&
        frame.pixel_height<=static_cast<unsigned>(std::numeric_limits<int>::max()) &&
        (frame.pixel_format==PixelFormat::RGBA32 || frame.pixel_format==PixelFormat::BGRA32) &&
        frame.pixels.size()/4/frame.pixel_width>=frame.pixel_height;
}
}
RasterClip plan_image_layer_destination(const ImageLayerRequest& request) noexcept {
    const auto target=std::max(1U,request.target_scale),source=std::max(1U,request.source_scale);
    // C++の符号付き整数の未定義オーバーフローを避け、描画座標の表現範囲に収める。
    const auto bounded=[](std::int64_t value){
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(value,
            std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()));
    };
    const auto extent=[&](std::uint32_t pixels){
        return static_cast<std::int32_t>(std::clamp<std::uint64_t>(std::uint64_t(pixels)*target/source,
            1U,std::numeric_limits<std::int32_t>::max()));
    };
    const auto offset=[&](std::int32_t value){return bounded(std::int64_t(value)*target);};
    const auto left=offset(request.insets.left),top=offset(request.insets.top);
    const auto right=offset(request.insets.right),bottom=offset(request.insets.bottom);
    RasterClip result{left,top,extent(request.source_width),extent(request.source_height)};
    if(request.mode==ImageLayerScale::Scale){
        result.width=bounded(std::max<std::int64_t>(0,std::int64_t(request.canvas_width)-left-right));
        result.height=bounded(std::max<std::int64_t>(0,std::int64_t(request.canvas_height)-top-bottom));
    }else if(request.mode==ImageLayerScale::Center){
        result.x=bounded((std::int64_t(request.canvas_width)-result.width)/2);
        result.y=bounded((std::int64_t(request.canvas_height)-result.height)/2);
    }
    return result;
}
bool composite_image_pixels(ImageFrame& destination,const ImageFrame& source,Point origin,Rect clip) {
    if(!valid_image(destination) || !valid_image(source) ||
       !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
       std::abs(static_cast<double>(origin.x))>std::numeric_limits<int>::max() ||
       std::abs(static_cast<double>(origin.y))>std::numeric_limits<int>::max() ||
       !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
       !std::isfinite(clip.width) || !std::isfinite(clip.height) ||
       !std::isfinite(clip.x+clip.width) || !std::isfinite(clip.y+clip.height) ||
       clip.width<0 || clip.height<0) return false;
    if(&destination==&source) {
        const auto snapshot=source;
        return composite_image_pixels(destination,snapshot,origin,clip);
    }
    const auto scissor=prepare_raster_clip(clip,RasterClip{0,0,
        static_cast<int>(destination.pixel_width),static_cast<int>(destination.pixel_height)});
    const auto ox=static_cast<std::int64_t>(std::floor(origin.x));
    const auto oy=static_cast<std::int64_t>(std::floor(origin.y));
    const auto x0=std::max<std::int64_t>(scissor.x,ox),y0=std::max<std::int64_t>(scissor.y,oy);
    const auto x1=std::min<std::int64_t>(static_cast<std::int64_t>(scissor.x)+scissor.width,ox+source.pixel_width);
    const auto y1=std::min<std::int64_t>(static_cast<std::int64_t>(scissor.y)+scissor.height,oy+source.pixel_height);
    for(auto y=y0;y<y1 && x0<x1;++y) {
        auto* out=destination.pixels.data()+(static_cast<std::size_t>(y)*destination.pixel_width+x0)*4;
        const auto* in=source.pixels.data()+(static_cast<std::size_t>(y-oy)*source.pixel_width+x0-ox)*4;
        cw::lib::simd_image::composite_straight_alpha(
            reinterpret_cast<const std::uint8_t*>(in),reinterpret_cast<std::uint8_t*>(out),
            static_cast<std::size_t>(x1-x0),source.pixel_format!=destination.pixel_format);
    }
    return true;
}
std::vector<RasterClip> xor_rectangle_regions(Rect rectangle,float width,RasterClip clip) {
    std::vector<RasterClip> result;
    if(!std::isfinite(rectangle.x)||!std::isfinite(rectangle.y)||!std::isfinite(rectangle.width)||
       !std::isfinite(rectangle.height)||rectangle.width<0||rectangle.height<0||!std::isfinite(width)||width<0||
       clip.width<=0||clip.height<=0)return result;
    const double half=(width==0?1:double(width))/2;
    const auto coordinate=[](double value,double lo,double hi){return static_cast<int>(std::clamp(std::ceil(value),lo,hi));};
    const double cx=clip.x,cy=clip.y,cr=cx+clip.width,cb=cy+clip.height;
    const auto x0=coordinate(double(rectangle.x)-half,cx,cr),x1=coordinate(double(rectangle.x)+rectangle.width+half,cx,cr);
    const auto y0=coordinate(double(rectangle.y)-half,cy,cb),y1=coordinate(double(rectangle.y)+rectangle.height+half,cy,cb);
    if(x0>=x1||y0>=y1)return result;
    const auto ix0=coordinate(double(rectangle.x)+half,x0,x1),ix1=coordinate(double(rectangle.x)+rectangle.width-half,x0,x1);
    const auto iy0=coordinate(double(rectangle.y)+half,y0,y1),iy1=coordinate(double(rectangle.y)+rectangle.height-half,y0,y1);
    const auto add=[&](int x,int y,int right,int bottom){if(x<right&&y<bottom)result.push_back({x,y,right-x,bottom-y});};
    if(ix0>=ix1||iy0>=iy1)add(x0,y0,x1,y1);
    else{add(x0,y0,x1,iy0);add(x0,iy1,x1,y1);add(x0,iy0,ix0,iy1);add(ix1,iy0,x1,iy1);}
    return result;
}
bool xor_rectangle_pixels(ImageFrame& destination,Rect rectangle,Color foreground,float width) {
    if(!valid_image(destination)||!std::isfinite(rectangle.x)||!std::isfinite(rectangle.y)||
       !std::isfinite(rectangle.width)||!std::isfinite(rectangle.height)||rectangle.width<0||rectangle.height<0||
       !std::isfinite(width)||width<0)return false;
    // D版のGCの整数パスの端点は画素の中心を表す。 半開区間の被覆で
    // 、見た目の線幅1を正確に一画素とし、角のXORを一度だけにする。
    const bool bgra=destination.pixel_format==PixelFormat::BGRA32;
    for(const auto band:xor_rectangle_regions(rectangle,width,{0,0,static_cast<int>(destination.pixel_width),static_cast<int>(destination.pixel_height)}))
    for(int y=band.y;y<band.y+band.height;++y){
        auto* pixels=destination.pixels.data()+(std::size_t(y)*destination.pixel_width+band.x)*4;
        cw::lib::simd_image::xor_rgb(reinterpret_cast<std::uint8_t*>(pixels),band.width,
            bgra?foreground.blue:foreground.red,foreground.green,bgra?foreground.red:foreground.blue);
    }
    return true;
}
} // namespace cwxeditor::lib
