#include "ui_decoration_presentation.hpp"
#include "ui_actions.hpp"
#include "ui_metrics.hpp"
#include "ui_slider.hpp"
#include "drawing_clip.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
namespace {
constexpr Color border{145,145,145,255},selection{49,106,197,255};
bool usable(Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
        std::isfinite(r.height) && r.width>0 && r.height>0;
}
void rectangle(DecorationPresentation& result,DecorationKind kind,Rect bounds,Color color) {
    if(usable(bounds)) result.push_back({kind,color,bounds,{}});
}
void ellipse(DecorationPresentation& result,Rect bounds,Color color) {
    if(!usable(bounds)) return;
    DecorationPrimitive primitive{DecorationKind::LineStrip,color};
    primitive.points.reserve(33);
    for(std::size_t i=0;i<=32;++i) {
        const float angle=static_cast<float>(i)*2.0F*3.14159265F/32.0F;
        primitive.points.push_back({bounds.x+bounds.width*.5F+std::cos(angle)*bounds.width*.5F,
                                    bounds.y+bounds.height*.5F+std::sin(angle)*bounds.height*.5F});
    }
    result.push_back(std::move(primitive));
}
}
DecorationPresentation prepare_toggle_decoration(const ChromePresentation& view) {
    DecorationPresentation result;
    if(!view.mark) return result;
    if(view.radio) {
        if(view.mark_fill) {
            const auto mark=*view.mark;
            for(float inset=1;inset<mark.width*.5F;inset+=1)
                ellipse(result,{mark.x+inset,mark.y+inset,mark.width-inset*2,mark.height-inset*2},*view.mark_fill);
        }
        ellipse(result,*view.mark,view.border_color);
        if(view.checked_mark && usable(*view.checked_mark)) {
            const auto dot=*view.checked_mark;
            for(float inset=0;inset<dot.width*.5F;inset+=1)
                ellipse(result,{dot.x+inset,dot.y+inset,dot.width-inset*2,dot.height-inset*2},view.mark_color);
        }
    } else {
        if(view.mark_fill) rectangle(result,DecorationKind::FillRect,*view.mark,*view.mark_fill);
        rectangle(result,DecorationKind::StrokeRect,*view.mark,view.border_color);
        if(view.checked_mark) rectangle(result,DecorationKind::FillRect,*view.checked_mark,view.mark_color);
    }
    return result;
}
DecorationPresentation prepare_chrome_decoration(const ChromePresentation& view, Rect bounds) {
    DecorationPresentation result;
    if(view.fill) rectangle(result,DecorationKind::FillRect,bounds,*view.fill);
    if(view.border && view.title_origin && usable(view.text_bounds)) {
        const float top = bounds.y + view.text_bounds.height * .5F;
        // StrokeRect は指定寸法の内側に遠い側の辺を描
        // く。 分割された GroupBox の輪郭も同じ最後の
        // 画素を使う。 x+width/y+height では右
        // ・下が一画素外へ出て、隣り合うグループがずれて見える。
        const float right = bounds.x + std::max(0.0F, bounds.width - 1.0F);
        const float bottom = bounds.y + std::max(0.0F, bounds.height - 1.0F);
        const float gap_padding = scaled_ui_metric(3.0F);
        const float gap_left = std::clamp(
            view.text_bounds.x - gap_padding, bounds.x, right);
        const float gap_right = std::clamp(
            view.text_bounds.x + view.text_bounds.width + gap_padding,
            bounds.x, right);
        const auto line = [&](std::initializer_list<Point> points) {
            if (points.size() < 2U) return;
            DecorationPrimitive primitive{DecorationKind::LineStrip,
                                          view.border_color};
            primitive.points.assign(points.begin(), points.end());
            result.push_back(std::move(primitive));
        };
        if (gap_left > bounds.x) line({{bounds.x,top},{gap_left,top}});
        if (right > gap_right) line({{gap_right,top},{right,top}});
        line({{bounds.x,top},{bounds.x,bottom}});
        line({{right,top},{right,bottom}});
        line({{bounds.x,bottom},{right,bottom}});
    } else if(view.border) {
        rectangle(result,DecorationKind::StrokeRect,bounds,view.border_color);
    }
    auto toggle=prepare_toggle_decoration(view);
    result.insert(result.end(),toggle.begin(),toggle.end());
    auto focus=prepare_cursor_decoration(view.focus_bounds);
    result.insert(result.end(),focus.begin(),focus.end());
    return result;
}
DecorationPresentation prepare_image_frame_decoration(Rect r, bool recessed) {
    DecorationPresentation result;
    if(!recessed || !usable(r) || !std::isfinite(r.x+r.width) || !std::isfinite(r.y+r.height)) return result;
    const auto w=std::min(1.0F,r.width),h=std::min(1.0F,r.height);
    // SWT.BORDER形式のくぼんだ画像表示領域。UI文字倍率には依存させない。
    rectangle(result,DecorationKind::FillRect,{r.x,r.y,r.width,h},Color{128,128,128,255});
    rectangle(result,DecorationKind::FillRect,{r.x,r.y,w,r.height},Color{128,128,128,255});
    rectangle(result,DecorationKind::FillRect,{r.x,r.y+r.height-h,r.width,h},Color{255,255,255,255});
    rectangle(result,DecorationKind::FillRect,{r.x+r.width-w,r.y,w,r.height},Color{255,255,255,255});
    return result;
}
DecorationPresentation prepare_number_decoration(const NumberEditLayout& geometry,
    bool increase_enabled,bool decrease_enabled,Color foreground) {
    DecorationPresentation result;
    const auto arrow=[&](Rect bounds,bool up,bool enabled) {
        if(!usable(bounds)) return;
        rectangle(result,DecorationKind::FillRect,bounds,{240,240,240,255});
        rectangle(result,DecorationKind::StrokeRect,bounds,border);
        const Color color=enabled ? foreground : Color{160,160,160,255};
        const float cx=bounds.x+bounds.width*.5F,cy=bounds.y+bounds.height*.5F;
        if (geometry.horizontal_steps) {
            const float radius=std::min({scaled_ui_metric(5),bounds.width*.3F,bounds.height*.3F});
            result.push_back({DecorationKind::LineStrip,color,{},{{cx-radius,cy},{cx+radius,cy}}});
            if (up) result.push_back({DecorationKind::LineStrip,color,{},{{cx,cy-radius},{cx,cy+radius}}});
            return;
        }
        const int radius=std::max(1,static_cast<int>(std::min({scaled_ui_metric(3),bounds.width*.3F,bounds.height*.3F})));
        for(int line=0;line<=radius;++line) {
            const float y=cy+(up ? line-radius*.5F : radius*.5F-line);
            result.push_back({DecorationKind::LineStrip,color,{},{{cx-line,y},{cx+line,y}}});
        }
    };
    arrow(geometry.increase,true,increase_enabled); arrow(geometry.decrease,false,decrease_enabled);
    return result;
}
DecorationPresentation prepare_cursor_decoration(std::optional<Rect> cursor) {
    DecorationPresentation result;
    if(!cursor || !usable(*cursor)) return result;
    const auto r=*cursor;
    DecorationPrimitive primitive{DecorationKind::Points,{80,80,80,255}};
    for(double offset=0;offset<r.width;offset+=2) {
        const float x=static_cast<float>(double(r.x)+offset);
        primitive.points.push_back({x,r.y}); primitive.points.push_back({x,r.y+r.height-1});
    }
    for(double offset=0;offset<r.height;offset+=2) {
        const float y=static_cast<float>(double(r.y)+offset);
        primitive.points.push_back({r.x,y}); primitive.points.push_back({r.x+r.width-1,y});
    }
    result.push_back(std::move(primitive));
    return result;
}
DecorationPresentation prepare_scroll_decoration(const ItemScrollGeometry& scroll,
    const Widget* owner) {
    DecorationPresentation result;
    const auto bar=[&](const HorizontalScrollGeometry& geometry,bool vertical) {
        if(!(geometry.maximum>0)) return;
        rectangle(result,DecorationKind::FillRect,geometry.track,{240,240,240,255});
        rectangle(result,DecorationKind::StrokeRect,geometry.track,border);
        // D版・SWTにはない追加仕様として、つかんだつまみを濃く表示する。
        const bool pressed=owner && scroll_thumb_pressed(*owner,vertical);
        rectangle(result,DecorationKind::FillRect,geometry.thumb,
            pressed ? kScrollThumbPressedColor : kScrollThumbColor);
        rectangle(result,DecorationKind::StrokeRect,geometry.thumb,border);
    };
    bar(scroll.horizontal,false); bar(scroll.vertical,true);
    if(scroll.horizontal.maximum>0 && scroll.vertical.maximum>0)
        rectangle(result,DecorationKind::FillRect,{scroll.vertical.track.x,scroll.horizontal.track.y,
            scroll.vertical.track.width,scroll.horizontal.track.height},{240,240,240,255});
    return result;
}
std::optional<DecorationPresentation> prepare_value_decoration(const Widget& widget,std::uint64_t now,bool focused) {
    DecorationPresentation result;
    const auto r=widget.bounds();
    if(const auto* well=dynamic_cast<const ColorWell*>(&widget)) {
        rectangle(result,DecorationKind::FillRect,r,well->value);
        rectangle(result,DecorationKind::StrokeRect,r,border);
    } else if(const auto* separator=dynamic_cast<const Separator*>(&widget)) {
        rectangle(result,DecorationKind::FillRect,separator->orientation==Orientation::Horizontal
            ? Rect{r.x,r.y+r.height*.5F,r.width,1} : Rect{r.x+r.width*.5F,r.y,1,r.height},border);
    } else if(const auto* slider=dynamic_cast<const Slider*>(&widget)) {
        const auto geometry=layout_slider(*slider);
        // 配置セル全体ではなく、くぼんだ溝だけを枠で囲む。
        rectangle(result,DecorationKind::FillRect,geometry.track,{214,214,214,255});
        const float edge=scaled_ui_metric(1);
        auto inner=geometry.track;
        inner.x+=edge; inner.y+=edge;
        inner.width=std::max(0.0F,inner.width-2*edge);
        inner.height=std::max(0.0F,inner.height-2*edge);
        rectangle(result,DecorationKind::FillRect,inner,{231,234,234,255});
        const double range=slider->maximum-slider->minimum;
        if(std::isfinite(range) && range>0 && std::isfinite(slider->tick_interval) &&
            slider->tick_interval>0 && geometry.last>geometry.first) {
            const bool horizontal=slider->orientation==Orientation::Horizontal;
            const auto tick=[&](double ratio) {
                const float axis=std::lerp(geometry.first,geometry.last,
                    static_cast<float>(horizontal?ratio:1-ratio));
                const float center=horizontal?r.y+r.height*.5F:r.x+r.width*.5F;
                // つまみのすぐ外に、各目盛りに一つずつ見やすい印を描く。
                const float offset=scaled_ui_metric(11), size=scaled_ui_metric(5);
                for(float side:{-1.0F,1.0F}) {
                    const float cross=center+side*offset-(side<0?size:0);
                    const Rect mark=horizontal?Rect{axis-edge*.5F,cross,edge,size}:
                        Rect{cross,axis-edge*.5F,size,edge};
                    rectangle(result,DecorationKind::FillRect,intersect_rectangles(r,mark),{120,120,120,255});
                }
            };
            tick(0); tick(1);
            const double count=range/slider->tick_interval;
            // 任意の範囲指定でも、無制限の処理や一画素未満の細かすぎる目盛りを避ける。
            const double limit=std::clamp(std::floor(static_cast<double>(geometry.last-geometry.first)/scaled_ui_metric(4)),1.0,256.0);
            if(std::isfinite(count)) {
                const double stride=std::max(1.0,std::ceil(count/limit));
                for(double i=stride;i<count;i+=stride) tick(i/count);
            }
        }
        rectangle(result,DecorationKind::FillRect,geometry.thumb,
            slider->enabled()?Color{0,122,217,255}:Color{160,160,160,255});
    } else if(const auto* bar=dynamic_cast<const ProgressBar*>(&widget)) {
        rectangle(result,DecorationKind::StrokeRect,r,border);
        const float inset=scaled_ui_metric(2);
        const Rect inner{r.x+inset,r.y+inset,std::max(0.0F,r.width-2*inset),std::max(0.0F,r.height-2*inset)};
        if(usable(inner)) {
            Rect fill=inner;
            if(bar->indeterminate) {
                const float block=std::max(scaled_ui_metric(18),inner.width*.25F);
                const float offset=static_cast<float>(std::fmod(static_cast<double>(now/16*16)*.08,
                    static_cast<double>(inner.width+block)))-block;
                fill=intersect_rectangles(inner,{inner.x+offset,inner.y,block,inner.height});
            } else {
                const double range=bar->maximum-bar->minimum;
                const double ratio=range>0 ? (bar->value-bar->minimum)/range : 0;
                fill.width*=static_cast<float>(std::isfinite(ratio)?std::clamp(ratio,0.0,1.0):0);
            }
            rectangle(result,DecorationKind::FillRect,fill,selection);
        }
    } else return {};
    if (focused && widget.enabled() && usable(r) &&
        (widget.kind()==ComponentKind::ColorWell || widget.kind()==ComponentKind::Slider)) {
        const float inset=scaled_ui_metric(3);
        auto focus=prepare_cursor_decoration(Rect{r.x+inset,r.y+inset,
            std::max(0.0F,r.width-2*inset),std::max(0.0F,r.height-2*inset)});
        result.insert(result.end(),focus.begin(),focus.end());
    }
    return result;
}
DecorationPresentation prepare_tab_header_decoration(Rect r,bool selected,bool hot) {
    DecorationPresentation result;
    rectangle(result,DecorationKind::FillRect,r,selected?Color{255,255,255,255}:
        hot?Color{238,244,252,255}:Color{238,238,238,255});
    rectangle(result,DecorationKind::StrokeRect,r,{150,150,150,255});
    if(selected) rectangle(result,DecorationKind::FillRect,
        {r.x+1,r.y+r.height-1,std::max(0.0F,r.width-2),2},{255,255,255,255});
    return result;
}
DecorationPresentation prepare_tab_navigation_decoration(Rect r) {
    return prepare_tab_header_decoration(r,false,false);
}
} // namespace cwxeditor::lib
