#include "ui_strip_presentation.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
namespace {
float metric(float value) { return scaled_ui_metric(value); }
Color text_color(bool enabled) { return enabled ? Color{24,24,24,255} : Color{130,130,130,255}; }
StripPresentation strip(Rect r,Color background) {
    return {r,{r.x,r.y+r.height-1,r.width,1},background,{}};
}
void prepare_strip_drawing(StripPresentation& result,bool baseline_first) {
    result.decoration={{DecorationKind::FillRect,result.background,result.bounds,{}}};
    const DecorationPrimitive baseline{DecorationKind::FillRect,{190,190,190,255},result.baseline,{}};
    (baseline_first?result.decoration:result.overlay).push_back(baseline);
    for(auto& item:result.items) {
        if(item.separator) {
            item.decoration.push_back({DecorationKind::FillRect,{170,170,170,255},*item.separator,{}});
            continue;
        }
        if(item.highlight) item.decoration.push_back({DecorationKind::FillRect,*item.highlight,item.bounds,{}});
        if(item.highlight_border) item.decoration.push_back({DecorationKind::StrokeRect,{135,165,205,255},item.bounds,{}});
        if(item.drop_down) item.arrow=DecorationTextPresentation{"▼",*item.drop_down,TextAlignment::Center,item.foreground};
    }
}
}
StripPresentation prepare_menu_bar(const Menu& menu,std::optional<std::size_t> selected,
    bool hovered,Point pointer) {
    auto result=strip(menu.bounds(),{248,248,248,255});
    const auto geometry=menu_bar_item_geometry(menu);
    for (std::size_t i=0;i<menu.items.size();++i) {
        const auto& item=menu.items[i];
        if (item.separator) continue;
        const auto bounds=geometry[i];
        StripItemPresentation row;
        row.model_index=i; row.bounds=bounds; row.icon=item.icon;
        row.text=menu_item_label(item); row.foreground=text_color(item.enabled);
        // SWTのメニューと同様、ラベルは縦中央に置く。 横方向は
        // 左端を維持し、共通フレームの準備で字形寸法を解決する。
        row.text_alignment=TextAlignment::Start;
        if (item.checked || selected==i || (hovered && point_inside(bounds,pointer)))
            row.highlight=Color{222,235,252,255};
        float x=bounds.x+metric(4);
        if (row.icon) {
            const auto size=metric(16);
            row.icon_bounds={bounds.x+metric(3),bounds.y+std::max(0.0F,(bounds.height-size)*.5F),size,size};
            x+=metric(19);
        }
        row.text_bounds={x,bounds.y,std::max(0.0F,bounds.width-(x-bounds.x)-metric(4)),bounds.height};
        // メニューの配置は、一つの計測済みラベルを確保する。 折り返すのはメ
        // ニュー項目全体であり、描画バックエンドで個々の字を折り返さない。
        result.items.push_back(std::move(row));
    }
    prepare_strip_drawing(result,true);
    return result;
}
StripPresentation prepare_tool_bar(const ToolBar& toolbar,bool hovered,Point pointer,
    const std::vector<PointerPressFeedback>& presses,bool keyboard_focused) {
    const auto r=toolbar.bounds();
    auto result=strip(r,{240,240,240,255});
    const auto geometry=tool_bar_item_geometry(toolbar);
    for (std::size_t i=0;i<toolbar.items.size();++i) {
        const auto& item=toolbar.items[i]; const auto bounds=geometry[i].bounds;
        if (bounds.y>=r.y+r.height || bounds.width<=0) continue;
        StripItemPresentation row; row.model_index=i; row.bounds=bounds;
        if (item.separator) {
            row.separator=Rect{bounds.x+metric(2),bounds.y+metric(4),1,
                std::max(0.0F,bounds.height-metric(8))};
        } else {
            const bool enabled=toolbar.enabled() && item.enabled;
            row.enabled=enabled;
            row.foreground=text_color(enabled);
            const bool pressed=enabled && std::any_of(presses.begin(),presses.end(),[&](const auto& press) {
                return press.widget.get()==&toolbar && point_inside(bounds,press.origin) && point_inside(bounds,press.position);
            });
            if (item.checked || (enabled && hovered && point_inside(bounds,pointer))) {
                row.highlight=item.checked ? Color{210,225,248,255} : Color{225,235,248,255};
                row.highlight_border=true;
            }
            if (pressed) { row.highlight=Color{204,224,247,255}; row.highlight_border=true; }
            if (keyboard_focused && toolbar.keyboard_navigation && enabled &&
                toolbar.keyboard_item_index == i) {
                row.highlight=Color{210,225,248,255};
                row.highlight_border=true;
            }
            row.icon=item.icon;
            // 表示と有効化で、同じ分割ボタンの領域を共有する。 別の固
            // 定画素幅を追加すると、150～200%で文字と重なる。
            const auto arrow=geometry[i].drop_down;
            const float body_width=item.drop_down ? arrow.x-bounds.x : bounds.width;
            if (row.icon) {
                // ツールバーのアイコン倍率で、ボタン中央に配置する。
                const auto size=metric(16)*std::max(1.0F,toolbar.icon_scale);
                row.icon_bounds={bounds.x+std::max(0.0F,(body_width-size)*.5F),
                    bounds.y+std::max(0.0F,(bounds.height-size)*.5F),size,size};
            } else {
                row.text=display_text_without_mnemonic(item.text); row.text_padding=metric(1);
                row.text_bounds={bounds.x+metric(2),bounds.y,
                    std::max(0.0F,body_width-metric(4)),bounds.height};
            }
            if (item.drop_down) row.drop_down=arrow;
        }
        result.items.push_back(std::move(row));
    }
    prepare_strip_drawing(result,false);
    return result;
}
std::optional<TooltipPresentation> prepare_tooltip(std::string_view value,Point pointer,
    Rect available,float line_height,const TextWidthMeasure& measure,bool balloon,float configured_width) {
    if (value.empty() || !measure || !std::isfinite(line_height) || line_height<=0 ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y) ||
        !std::isfinite(available.x) || !std::isfinite(available.y) ||
        !std::isfinite(available.width) || !std::isfinite(available.height)) return {};
    const float margin=metric(4), pad_x=metric(6), pad_y=metric(4);
    const float tail=balloon ? metric(8) : 0;
    const float authored_width=std::isfinite(configured_width) && configured_width>0 ? configured_width : metric(420);
    const float max_width=std::min(authored_width,available.width-2*margin);
    const float max_height=available.height-2*margin-tail;
    if (max_width<=2*pad_x || max_height<line_height+2*pad_y) return {};
    TextEdit text; text.text=value; text.multiline=true; text.wrap=true;
    const auto lines=layout_text_lines(text,max_width-2*pad_x,measure);
    float width{};
    for (const auto& line:lines) width=std::max(width,line.width);
    width=std::min(max_width,std::max(metric(40),width+2*pad_x));
    const auto count=std::min(lines.size(),static_cast<std::size_t>(std::floor((max_height-2*pad_y)/line_height)));
    const float body_height=static_cast<float>(count)*line_height+2*pad_y;
    const float height=body_height+tail;
    float x=pointer.x+metric(14), y=pointer.y+metric(18);
    if (y+height>available.y+available.height-margin) y=pointer.y-metric(8)-height;
    x=std::clamp(x,available.x+margin,available.x+available.width-margin-width);
    y=std::clamp(y,available.y+margin,available.y+available.height-margin-height);
    TooltipPresentation result; result.bounds={x,y,width,height}; result.truncated=count<lines.size();
    result.balloon=balloon;
    const bool tail_above=pointer.y<y+height*.5F;
    const float body_y=y+(tail_above ? tail : 0);
    result.body={x,body_y,width,body_height};
    result.font=effective_editor_widget_font();
    result.decoration={{DecorationKind::FillRect,result.background,result.body,{}},
                       {DecorationKind::StrokeRect,result.border,result.body,{}}};
    if (balloon) {
        const float half=std::min(metric(6),width*.2F);
        const float base_x=std::clamp(pointer.x,x+half+1,x+width-half-1);
        const float base_y=tail_above ? body_y : body_y+body_height-1;
        const float tip_y=tail_above ? y : y+height-1;
        const float direction=tail_above ? -1.0F : 1.0F;
        // OSに依存しない走査線で描き、二辺の輪郭より先に本体との継ぎ目を
        // 消す。 OSのツールチップやプラットフォーム固有の多角形は不要。
        for (float step=0;step<tail;step+=1) {
            const float radius=half*(1-step/tail);
            result.decoration.push_back({DecorationKind::FillRect,result.background,
                {base_x-radius,base_y+direction*step,std::max(1.0F,2*radius),1},{}});
        }
        result.decoration.push_back({DecorationKind::LineStrip,result.border,{},
            {{base_x-half,base_y},{base_x,tip_y},{base_x+half,base_y}}});
    }
    for (std::size_t i=0;i<count;++i) {
        auto label=text_display_range(text,lines[i].start,lines[i].end);
        if (result.truncated && i+1==count) {
            while (!label.empty() && measure(label+"…")>width-2*pad_x)
                label.resize(previous_utf8_boundary(label,label.size()));
            label+="…";
        }
        result.lines.push_back({{x+pad_x,body_y+pad_y+static_cast<float>(i)*line_height},std::move(label)});
    }
    return result;
}

std::optional<TooltipPresentation> prepare_toolbar_description_tooltip(
    std::string_view title,std::string_view body,ResourceId icon,Rect anchor,
    Rect available,float line_height,const TextWidthMeasure& measure,float configured_width) {
    if (title.empty() || !measure || !std::isfinite(line_height) || line_height<=0 ||
        !std::isfinite(anchor.x) || !std::isfinite(anchor.y) ||
        !std::isfinite(anchor.width) || !std::isfinite(anchor.height) ||
        !std::isfinite(available.x) || !std::isfinite(available.y) ||
        !std::isfinite(available.width) || !std::isfinite(available.height)) return {};
    const float margin=metric(4),pad_x=metric(6),pad_y=metric(4),icon_gap=metric(4);
    const float max_width=available.width-2*margin;
    const float max_height=available.height-2*margin;
    const float body_limit=std::min(
        std::isfinite(configured_width) && configured_width>0 ? configured_width : metric(420),
        max_width-2*pad_x);
    if (max_width<=2*pad_x || max_height<line_height+2*pad_y || body_limit<=0) return {};

    TextEdit text; text.text=std::string(body); text.multiline=true; text.wrap=true;
    const auto body_lines=body.empty() ? std::vector<TextVisualLine>{}
        : layout_text_lines(text,body_limit,measure);
    float body_width{};
    for (const auto& line:body_lines) body_width=std::max(body_width,line.width);
    body_width=std::min(body_limit,body_width);
    const float title_text_width=std::max(0.0F,measure(title));
    const float icon_size=icon ? metric(16) : 0.0F;
    const float title_height=std::max(line_height,icon_size);
    if (max_height<title_height+2*pad_y) return {};
    const float title_content_width=std::min(max_width-2*pad_x,
        title_text_width+icon_size+(icon ? icon_gap : 0.0F));
    const float content_width=std::max(title_content_width,body_width);
    const float width=content_width+2*pad_x;
    const auto body_count=body_lines.empty() ? 0U : std::min(body_lines.size(),
        static_cast<std::size_t>(std::floor((max_height-2*pad_y-title_height)/line_height)));
    if (body_count==0U && !body.empty()) return {};
    const float body_height=static_cast<float>(body_count)*line_height;
    const float panel_height=2*pad_y+title_height+body_height;
    float x=std::clamp(anchor.x,available.x+margin,
        available.x+available.width-margin-width);
    const bool below=anchor.y+anchor.height+panel_height<=available.y+available.height-margin;
    float y=below ? anchor.y+anchor.height : anchor.y-panel_height;
    y=std::clamp(y,available.y+margin,available.y+available.height-margin-panel_height);

    TooltipPresentation result;
    result.bounds={x,y,width,panel_height};
    result.body=result.bounds;
    result.truncated=body_count<body_lines.size();
    result.font=effective_editor_widget_font();
    result.decoration={{DecorationKind::FillRect,result.background,result.body,{}},
                       {DecorationKind::StrokeRect,result.border,result.body,{}}};
    const float title_y=y+pad_y;
    const auto title_icon_bounds=icon ? Rect{x+pad_x,title_y+(title_height-icon_size)*.5F,icon_size,icon_size} : Rect{};
    const float title_text_x=x+pad_x+icon_size+(icon ? icon_gap : 0.0F);
    FontSpec title_font=result.font;
    title_font.weight=700;
    result.title=TooltipTitlePresentation{std::string(title),icon,title_icon_bounds,
        {title_text_x,title_y,std::max(0.0F,x+width-pad_x-title_text_x),title_height},
        {title_text_x,title_y},title_font};
    const float body_y=title_y+title_height;
    for (std::size_t i=0;i<body_count;++i) {
        auto label=text_display_range(text,body_lines[i].start,body_lines[i].end);
        if (result.truncated && i+1==body_count) {
            while (!label.empty() && measure(label+"…")>body_limit)
                label.resize(previous_utf8_boundary(label,label.size()));
            label+="…";
        }
        result.lines.push_back({{x+pad_x,body_y+static_cast<float>(i)*line_height},std::move(label)});
    }
    return result;
}
} // namespace cwxeditor::lib
