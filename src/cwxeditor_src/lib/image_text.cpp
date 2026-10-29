#include "image_text.hpp"
#include "image_composition.hpp"
#include "cwxeditor_src/core/cwx/graphics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace cwxeditor::lib {
ImageFrame rasterize_image_text(std::shared_ptr<GraphicsBackend> graphics,std::string text,ImageTextStyle style) {
    if(!graphics || text.empty())return {};
    const auto current=[&]{return !style.current||style.current();};
    if(!current()||!std::isfinite(style.glyph_scale)||style.glyph_scale<=0)return {};
    // カード見出しは font2 を作るが元のフォントを選び、TextCellのアンチエイ
    // リアスは glyph_scale=2 を明示する。 D版の各呼び出し箇所と同様、どち
    // らもネイティブの字形のアンチエイリアスとは別に smoothResize を選ぶ。
    if (!style.preserve_font_antialias) style.font.antialias=false;
    style.font.points*=style.glyph_scale;
    if(!std::isfinite(style.font.points)||style.font.points<=0)return {};
    const auto extent=graphics->measure_text(text,style.font,0);
    if(!current())return {};
    if(!std::isfinite(extent.width) || !std::isfinite(extent.height) || extent.width<=0 || extent.height<=0)return {};
    const auto dimension=[](double value){
        if(!std::isfinite(value)||value>std::numeric_limits<int>::max()-2 || value<=0)throw std::length_error("Editor caption raster extent");
        return static_cast<unsigned>(std::ceil(value));
    };
    const auto makeFrame=[](unsigned w,unsigned h){
        if(std::uint64_t(w)*h>std::numeric_limits<std::size_t>::max()/4)throw std::length_error("Editor caption allocation");
        ImageFrame frame;frame.pixel_width=w;frame.pixel_height=h;frame.pixel_format=PixelFormat::RGBA32;
        frame.pixels.resize(static_cast<std::size_t>(w)*h*4);frame.logical_size={float(w),float(h)};return frame;
    };
    const auto width=dimension(extent.width),height=dimension(extent.height);
    ImageData raster;raster.frames={makeFrame(width,height)};
    // フォントのラスタライズは白の被覆を返し、D版の黒地の白い字形を createAlphaDataFrom
    // で変換する処理に相当する。 色の指定は別の方針として扱う。
    if(!graphics->draw_text_to_image(raster,0,text,{0,0,float(width),float(height)},style.font,{255,255,255,255}))return {};
    if(!current())return {};
    const auto target_width=std::isfinite(style.shrink_width) && style.shrink_width>0
        ? dimension(std::min(double(width),double(style.shrink_width))) : width;
    const auto out_width=style.target_size?dimension(style.target_size->width):target_width;
    const auto out_height=style.target_size?dimension(style.target_size->height):height;
    auto result=makeFrame(out_width+2,out_height+2);
    const auto coloredMask=[&](Color color){
        const auto& source=raster.frames.front();
        Bytes rgb(static_cast<std::size_t>(width)*height*4),alpha(static_cast<std::size_t>(width)*height);
        for(std::size_t i=0;i<alpha.size();++i)alpha[i]=static_cast<Byte>(std::to_integer<unsigned>(source.pixels[i*4+3])*color.alpha/255);
        std::size_t stride{};
        if(style.smoothing)(void)cwx::graphics::smoothResize(out_width,out_height,rgb,alpha,32,width,height,std::size_t(width)*4,stride);
        else (void)cwx::graphics::resize(out_width,out_height,rgb,alpha,32,width,height,std::size_t(width)*4,stride);
        auto output=makeFrame(out_width,out_height);
        for(std::size_t i=0;i<alpha.size();++i){output.pixels[i*4]=std::byte(color.red);output.pixels[i*4+1]=std::byte(color.green);output.pixels[i*4+2]=std::byte(color.blue);output.pixels[i*4+3]=std::byte(alpha[i]);}
        return output;
    };
    const Rect clip{0,0,float(result.pixel_width),float(result.pixel_height)};
    // D版の dutils.d と同様、先に両マスクのサイズを変える。
    // 細い見出しでも、中央の描画を含む輪郭幅は常に出力先の一画素にする。
    if(style.outline.alpha){
        const auto outline=coloredMask(style.outline);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
            (void)composite_image_pixels(result,outline,{float(1+x),float(1+y)},clip);
    }
    const auto foreground=coloredMask(style.foreground);
    (void)composite_image_pixels(result,foreground,{1,1},clip);
    return result;
}
} // namespace cwxeditor::lib
