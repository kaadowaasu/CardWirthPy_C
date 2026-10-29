#pragma once
#include "cw/imageretouch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace cw::lib::text_raster {
// 共通 Presentation 用の CPU だけの文字画像の再
// サンプリング。OS/UI バックエンドは作らず、UI は一系統に
// する。Python の util.render_antialiasedtext
// は pygame・ゲームカードと異なる wx の画像
// 変換規則を使う。wx への依存や OS 別分岐は不要。
using I=::cw::lib::Integer;
using Surface=::cw::lib::Surface;
enum class ResizeQuality { Nearest, Bicubic, High };
inline std::shared_ptr<Surface> resized(const std::shared_ptr<Surface>& src,I width,I height,ResizeQuality quality) {
    if(!src || width<=0 || height<=0 || src->size.width<=0 || src->size.height<=0)return {};
    const auto sw=src->size.width,sh=src->size.height;
    const auto valid=[](I w,I h){
        const auto limit=(std::numeric_limits<std::size_t>::max)()/4;
        return static_cast<std::uint64_t>(w)<=limit/static_cast<std::uint64_t>(h);
    };
    if(!valid(sw,sh) || !valid(width,height) ||
        src->rgba.size()<static_cast<std::size_t>(sw)*static_cast<std::size_t>(sh)*4)return {};
    if(width==sw && height==sh)return std::make_shared<Surface>(*src);
    auto dst=std::make_shared<Surface>();dst->size={width,height};
    dst->rgba.assign(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4,0);
    const auto pixel=[&](I x,I y){return src->rgba.data()+
        (static_cast<std::size_t>(y)*static_cast<std::size_t>(sw)+static_cast<std::size_t>(x))*4;};
    // wx の HIGH は、縦横の両方を縮小するときだけ、アルファで重み付けしたボックスフィルターを使う。
    const bool high_box=quality==ResizeQuality::High && width<sw && height<sh;
    const bool filtered=quality!=ResizeQuality::Nearest && !high_box;
    const bool box=!filtered && sw>=width && sh>=height && sw%width==0 && sh%height==0;
    // wxImage::ResampleBicubic と同じ、端点をそろえ端を制限した標
    // 準の三次 B スプライン基底。アルファを補間してから RGB の乗算済み状態を戻す。
    struct Taps {std::array<I,4> pos;std::array<double,4> weight;};
    const auto taps=[](I count,I old){
        std::vector<Taps> result(static_cast<std::size_t>(count));
        for(I i=0;i<count;++i){
            const double at=count==1?(old-1)/2.0:static_cast<double>(i)*(old-1)/(count-1);
            const I base=static_cast<I>(at);
            for(I k=-1;k<=2;++k){
                const auto d=std::abs(k-(at-base));
                const double v=d<1?(2.0/3.0-d*d+d*d*d/2.0):d<2?std::pow(2-d,3)/6.0:0;
                result[static_cast<std::size_t>(i)].pos[static_cast<std::size_t>(k+1)]=std::clamp<I>(base+k,0,old-1);
                result[static_cast<std::size_t>(i)].weight[static_cast<std::size_t>(k+1)]=v;
            }
        }
        return result;
    };
    std::vector<Taps> xs,ys;
    if(filtered){xs=taps(width,sw);ys=taps(height,sh);}
    for(I y=0;y<height;++y)for(I x=0;x<width;++x){
        auto* out=dst->rgba.data()+(static_cast<std::size_t>(y)*width+x)*4;
        if(high_box){
            std::array<std::uint64_t,4> sum{};
            const I x0=x*sw/width,x1=((x+1)*sw+width-1)/width;
            const I y0=y*sh/height,y1=((y+1)*sh+height-1)/height;
            for(I j=y0;j<y1;++j)for(I i=x0;i<x1;++i){
                const auto* p=pixel(i,j);sum[3]+=p[3];
                for(std::size_t c=0;c<3;++c)sum[c]+=static_cast<std::uint64_t>(p[c])*p[3];
            }
            for(std::size_t c=0;c<3;++c)out[c]=sum[3]?static_cast<std::uint8_t>(sum[c]/sum[3]):0;
            out[3]=static_cast<std::uint8_t>(sum[3]/(static_cast<std::uint64_t>(x1-x0)*(y1-y0)));
        } else if(filtered){
            std::array<double,4> sum{};
            const auto& xt=xs[static_cast<std::size_t>(x)];
            const auto& yt=ys[static_cast<std::size_t>(y)];
            for(std::size_t j=0;j<4;++j)for(std::size_t i=0;i<4;++i){
                const auto* p=pixel(xt.pos[i],yt.pos[j]);
                const double a=p[3]*xt.weight[i]*yt.weight[j];
                sum[3]+=a;
                for(std::size_t c=0;c<3;++c)sum[c]+=p[c]*a;
            }
            for(std::size_t c=0;c<3;++c)out[c]=sum[3]>0?
                static_cast<std::uint8_t>(std::clamp(sum[c]/sum[3]+0.5,0.0,255.0)):0;
            out[3]=static_cast<std::uint8_t>(std::clamp(sum[3],0.0,255.0));
        } else if(box){
            std::array<std::uint64_t,4> sum{};
            const I dx=sw/width,dy=sh/height;
            for(I j=0;j<dy;++j)for(I i=0;i<dx;++i){
                const auto* p=pixel(x*dx+i,y*dy+j);
                if(p[3])for(std::size_t c=0;c<3;++c)sum[c]+=p[c];
                sum[3]+=p[3];
            }
            const auto count=static_cast<std::uint64_t>(dx)*dy;
            for(std::size_t c=0;c<4;++c)out[c]=static_cast<std::uint8_t>(sum[c]/count);
        } else {
            // wx の最近傍法は左端ではなく、16.16 固定小数点の中心サンプルを使う。
            const auto coord=[](I index,I old,I count){
                const auto delta=(static_cast<std::uint64_t>(old)<<16)/static_cast<std::uint64_t>(count);
                return std::min<I>(old-1,static_cast<I>((delta/2+static_cast<std::uint64_t>(index)*delta)>>16));
            };
            std::copy_n(pixel(coord(x,sw,width),coord(y,sh,height)),4,out);
        }
    }
    return dst;
}

inline std::shared_ptr<Surface> resized(const std::shared_ptr<Surface>& src,I width,I height,bool filtered) {
    return resized(src,width,height,filtered?ResizeQuality::Bicubic:ResizeQuality::Nearest);
}

inline std::shared_ptr<Surface> dialog_card_name(
    const ::cw::lib::String& text,const std::shared_ptr<::cw::imageretouch::Font>& base_font,
    bool smoothing,bool white,I maxwidth,I padding) {
    if(text.empty() || !base_font || (maxwidth>0 && maxwidth<=2*padding))return {};
    auto font=base_font;
    const auto doubled=[&]{
        const auto* pixels=std::get_if<I>(&base_font->m_pixels.storage());
        const auto* face=std::get_if<::cw::lib::String>(&base_font->m_face.storage());
        if(!pixels || !face || *pixels>(std::numeric_limits<I>::max)()/2 ||
            *pixels<(std::numeric_limits<I>::min)()/2)return std::shared_ptr<::cw::imageretouch::Font>{};
        auto enlarged=std::make_shared<::cw::imageretouch::Font>();
        enlarged->initialize(*face,*pixels*2,base_font->get_bold(),base_font->get_italic());
        enlarged->set_underline(base_font->get_underline());
        return enlarged;
    };
    if(smoothing){font=doubled();if(!font)return {};}
    const auto color=white?std::tuple<I,I,I>{255,255,255}:std::tuple<I,I,I>{0,0,0};
    // wx は最近傍でサイズ変更するときもフォント DC で描く。平滑化設定はスーパー
    // サンプリング・サイズ変更の選択であり、文字画像を強制的に二値化するものではない。
    auto glyph=font->render(text,true,color);
    if(!glyph || glyph->size.width<=0 || glyph->size.height<=0)return {};
    I width=glyph->size.width,height=glyph->size.height;
    bool reduce=smoothing;
    if(!smoothing && maxwidth>0 && width>maxwidth){
        // Python の upfont は、はみ出す場合も元のテキストの文字領域の高さを保つ。
        font=doubled();if(!font)return {};
        glyph=font->render(text,true,color);reduce=true;
    } else if(smoothing){width/=2;height/=2;}
    if(!glyph)return {};
    if(maxwidth>0)width=std::min(width,maxwidth-2*padding);
    if(reduce || width!=glyph->size.width)
        return resized(glyph,std::max<I>(1,width),std::max<I>(1,height),smoothing);
    return std::make_shared<Surface>(*glyph);
}
} // namespace cw::lib::text_raster
