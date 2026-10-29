#include "text_path.hpp"
#include "image_composition.hpp"
#include "cw/lib/encoding.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace cwxeditor::lib {
namespace {
struct Edge { Point a,b; };
using Edges=std::vector<Edge>;
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
Point middle(Point a,Point b){return {(a.x+b.x)/2,(a.y+b.y)/2};}
double distance2(Point p,Point a,Point b){
    const double dx=double(b.x)-a.x,dy=double(b.y)-a.y;
    const double t=dx*dx+dy*dy?std::clamp(((double(p.x)-a.x)*dx+(double(p.y)-a.y)*dy)/(dx*dx+dy*dy),0.,1.):0.;
    const double x=double(p.x)-a.x-t*dx,y=double(p.y)-a.y-t*dy;return x*x+y*y;
}
void edge(Edges& output,Point a,Point b){
    if(!finite(a)||!finite(b))throw std::invalid_argument("non-finite glyph outline");
    // D版・SWTは大きすぎる Path を拒否する場合がある。共通の平坦化処理に
    // も上限を設け、 不正な提供データでプロセスの資源を使い切らないようにする。
    if(output.size()>=1048576)throw std::length_error("Inline contour budget");
    output.push_back({a,b});
}
void quadratic(Edges& out,Point a,Point c,Point b,int depth=0){
    if(depth>=16||distance2(c,a,b)<=1./4096){edge(out,a,b);return;}
    const auto ac=middle(a,c),cb=middle(c,b),m=middle(ac,cb);
    quadratic(out,a,ac,m,depth+1);quadratic(out,m,cb,b,depth+1);
}
void cubic(Edges& out,Point a,Point c,Point d,Point b,int depth=0){
    if(depth>=16||std::max(distance2(c,a,b),distance2(d,a,b))<=1./4096){edge(out,a,b);return;}
    const auto ac=middle(a,c),cd=middle(c,d),db=middle(d,b),l=middle(ac,cd),r=middle(cd,db),m=middle(l,r);
    cubic(out,a,ac,l,m,depth+1);cubic(out,m,r,db,b,depth+1);
}
void append(Edges& output,const GlyphPath& path,Point offset){
    Point start{},previous{};bool open{};
    const auto point=[&](Point p){if(!finite(p))throw std::invalid_argument("non-finite glyph outline");return Point{p.x+offset.x,p.y+offset.y};};
    const auto close=[&]{if(open){edge(output,previous,start);open=false;}};
    for(const auto& command:path.commands){
        if(command.kind==GlyphPathKind::Move){close();start=previous=point(command.points[0]);open=true;continue;}
        if(command.kind==GlyphPathKind::Close){close();continue;}
        if(!open)throw std::invalid_argument("glyph outline without move");
        switch(command.kind){
        case GlyphPathKind::Line:{const auto p=point(command.points[0]);edge(output,previous,p);previous=p;break;}
        case GlyphPathKind::Quadratic:{const auto p=point(command.points[1]);quadratic(output,previous,point(command.points[0]),p);previous=p;break;}
        case GlyphPathKind::Cubic:{const auto p=point(command.points[2]);cubic(output,previous,point(command.points[0]),point(command.points[1]),p);previous=p;break;}
        default:throw std::invalid_argument("unknown glyph path command");
        }
    }
    close();
}
void rectangle(Edges& out,float x,float y,float width,float height){
    const Point a{x,y},b{x+width,y},c{x+width,y+height},d{x,y+height};
    edge(out,a,b);edge(out,b,c);edge(out,c,d);edge(out,d,a);
}
unsigned dimension(float value){
    if(!std::isfinite(value)||value<=0)return 0;
    if(double(value)>double(std::numeric_limits<int>::max()-2))throw std::length_error("Inline raster extent");
    return static_cast<unsigned>(std::ceil(value));
}
ImageFrame frame(unsigned width,unsigned height){
    if(std::uint64_t(width)*height>std::numeric_limits<std::size_t>::max()/4)throw std::length_error("Inline raster allocation");
    ImageFrame result;result.pixel_width=width;result.pixel_height=height;result.logical_size={float(width),float(height)};
    result.pixel_format=PixelFormat::RGBA32;result.pixels.resize(std::size_t(width)*height*4);return result;
}
ImageFrame foreground(const Edges& edges,unsigned width,unsigned height,float shift,Color color){
    auto output=frame(width,height);std::vector<double> crossings;
    // SWTの新しい画像用GCと同様、標準の偶奇規則でパスを塗る
    // 。 字形の穴と装飾の矩形を、その一つの複合パスに含める。
    for(unsigned y=0;y<height;++y){
        crossings.clear();const double scan=double(y)+.5-shift;
        for(const auto& e:edges)if((e.a.y<=scan&&e.b.y>scan)||(e.b.y<=scan&&e.a.y>scan))
            crossings.push_back(e.a.x+(scan-e.a.y)*(double(e.b.x)-e.a.x)/(double(e.b.y)-e.a.y)+shift);
        std::sort(crossings.begin(),crossings.end());
        for(std::size_t i=0;i+1<crossings.size();i+=2){
            const auto left=unsigned(std::clamp(std::ceil(crossings[i]-.5),0.,double(width)));
            const auto right=unsigned(std::clamp(std::ceil(crossings[i+1]-.5),0.,double(width)));
            for(auto x=left;x<right;++x){const auto at=(std::size_t(y)*width+x)*4;output.pixels[at]=std::byte(color.red);output.pixels[at+1]=std::byte(color.green);output.pixels[at+2]=std::byte(color.blue);output.pixels[at+3]=std::byte(color.alpha);}
        }
    }
    return output;
}
ImageFrame border(const Edges& edges,unsigned width,unsigned height,double radius,bool aa,Color color){
    std::vector<std::uint16_t> coverage(std::size_t(width)*height);
    const int samples=aa?4:1;const double squared=radius*radius;
    // 端が丸い線分を合併すると、接合部も丸くなる。 D版の太い枠
    // の線分ごとの回避処理を保ち、重なったパスにOS固有の穴を作
    // らない。 スーパーサンプリングは共通の描画機能とするが、曲
    // 線の端の被覆はSWT・FreeTypeと異なる場合がある。
    for(const auto& e:edges){
        const int left=int(std::clamp(std::floor(std::min(e.a.x,e.b.x)-radius),0.,double(width)));
        const int right=int(std::clamp(std::ceil(std::max(e.a.x,e.b.x)+radius),0.,double(width)));
        const int top=int(std::clamp(std::floor(std::min(e.a.y,e.b.y)-radius),0.,double(height)));
        const int bottom=int(std::clamp(std::ceil(std::max(e.a.y,e.b.y)+radius),0.,double(height)));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){auto& mask=coverage[std::size_t(y)*width+x];
            for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx)
                if(distance2({float(x)+(sx+.5F)/samples,float(y)+(sy+.5F)/samples},e.a,e.b)<=squared)
                    mask|=std::uint16_t(1U<<(sy*samples+sx));
        }
    }
    auto output=frame(width,height);
    for(std::size_t i=0;i<coverage.size();++i){const auto alpha=std::popcount(coverage[i])*color.alpha/(samples*samples);output.pixels[i*4]=std::byte(color.red);output.pixels[i*4+1]=std::byte(color.green);output.pixels[i*4+2]=std::byte(color.blue);output.pixels[i*4+3]=std::byte(alpha);}
    return output;
}
} // namespace
std::optional<ImageFrame> rasterize_inline_text(std::shared_ptr<GraphicsBackend> graphics,const std::vector<std::string>& lines,Size extent,InlineTextStyle style){
    const auto current=[&]{return !style.current||style.current();};
    if(!graphics||!current())return ImageFrame{};
    const auto width=dimension(extent.width),height=dimension(extent.height);
    if(!width||!height||!std::isfinite(style.border_width)||style.border_width<0||
       !std::isfinite(style.line_height)||style.line_height<=0)return ImageFrame{};
    // D版と同様、見出しフォントの品質と枠マスクのアンチエイリアスは独立した設定。 通
    // 常の TextCell の setTitle は false を既定とし、公開
    // setTitle では true を選べる。 装飾フラグは意味上の指定に留める。
    style.font.decorations_in_policy=true;
    Edges path;float y{};
    for(const auto& line:lines){
        const auto validated=cw::lib::encoding::decode_utf8(cw::lib::Bytes(line.begin(),line.end()),cw::lib::encoding::ErrorPolicy::Strict,false);
        float x{};
        for(std::size_t i=0;i<validated.size();){
            const auto start=i;const auto first=static_cast<unsigned char>(validated[i++]);
            const unsigned count=first<128?1:first<224?2:first<240?3:4;
            char32_t scalar=first&(count==1?127:count==2?31:count==3?15:7);
            for(unsigned part=1;part<count;++part)scalar=(scalar<<6)|(static_cast<unsigned char>(validated[i++])&63);
            const auto glyph=graphics->glyph_path(scalar,style.font);if(!current())return ImageFrame{};
            if(!glyph)return std::nullopt;
            append(path,*glyph,{x,y});
            const auto measured=graphics->measure_text(std::string_view(validated).substr(start,count),style.font,0);if(!current())return ImageFrame{};
            if(!std::isfinite(measured.width)||measured.width<0)return ImageFrame{};
            x+=measured.width;if(!std::isfinite(x))return ImageFrame{};
        }
        if(style.font.underline)rectangle(path,0,y+style.underline,x,float(style.decoration_width));
        if(style.font.strikeout)rectangle(path,0,y+style.strike,x,float(style.decoration_width));
        y+=style.line_height;if(!std::isfinite(y))return ImageFrame{};
    }
    const double authored=std::floor(style.border_width);
    const float shift=std::fmod(authored,2.)==0?.5F:0.F;
    auto result=foreground(path,width,height,shift,style.foreground);
    // SWTの中央寄せの奇数幅 drawPath は線を半画素ずらす。 images.d
    // の hb は、fillPath に対応する補正を適用する。
    if(shift)for(auto& e:path){e.a.x+=shift;e.a.y+=shift;e.b.x+=shift;e.b.y+=shift;}
    const auto outline=border(path,width,height,(authored+1)/2,style.border_antialias,style.border);
    if (style.preserve_foreground_alpha) {
        // redToAlpha は前景の被覆を別に保存し、全RGB
        // 画素を指定の前景色で塗る。 GCは保持する alphaData
        // を広げずに、その画像へ枠を合成する。 ここではアル
        // ファをsource-over合成せず、太い輪郭が元の
        // 字形・下線の被覆外に不透明画素を作らないようにする。
        const unsigned rgb[]{style.foreground.red, style.foreground.green,
                             style.foreground.blue};
        for (std::size_t i=0;i<result.pixels.size();i+=4) {
            const unsigned alpha=static_cast<unsigned>(outline.pixels[i+3]);
            for (std::size_t channel=0;channel<3;++channel) {
                const unsigned border_color=static_cast<unsigned>(outline.pixels[i+channel]);
                result.pixels[i+channel]=std::byte(
                    (rgb[channel]*(255U-alpha)+border_color*alpha+127U)/255U);
            }
        }
    } else {
        // ALL と汎用の呼び出し元では、移植版のアルファを考慮する拡張を維持する。
        (void)composite_image_pixels(result,outline,{0,0},{0,0,float(width),float(height)});
    }
    return result;
}
} // namespace cwxeditor::lib
