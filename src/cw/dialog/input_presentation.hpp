#pragma once
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit_input.hpp"
#include "cw/dialog/textedit_render.hpp"
#include "cw/lib/ui_surface.hpp"
#include <algorithm>
#include <cmath>

namespace cw::dialog::input_presentation {
// 共通の描画・座標ヘルパーであり、別のダイアログ
// やバックエンドは作らない。UI は一系統とし
// 、下の矩形、文字、キャレット、スクロール量、
// ポップアップの基準位置はすべて最終画素単位。
using I=::cw::lib::Integer;
using Font=::cw::lib::ui::FontStyle;
using Rect=::cw::lib::Rect;
inline I px(I value) {return ::cw::lib::ui::win_i(value);}
inline I cell_height(const Font& font) {
    const auto ink=::cw::lib::ui::render_label("Mg",font.pixels,font.bold,{0,0,0},font.face,font.italic);
    return ink?ink->size.height:font.pixels;
}
inline I text_width(const ::cw::lib::String& text,const Font& font) {
    return ::cw::lib::ui::label_width(text,font.pixels,font.bold,font.face,font.italic);
}
inline std::vector<::cw::lib::String> lines(const ::cw::lib::String& text) {
    std::vector<::cw::lib::String> result;
    std::size_t first=0;
    for(;;){
        const auto end=text.find('\n',first);
        auto row=text.substr(first,end==text.npos?text.npos:end-first);
        if(!row.empty() && row.back()=='\r')row.pop_back();
        result.push_back(std::move(row));
        if(end==text.npos)break;
        first=end+1;
    }
    return result;
}
inline std::shared_ptr<::cw::lib::Surface> canvas(I width,I height,bool caution) {
    namespace ui=::cw::lib::ui;
    auto surface=std::make_shared<::cw::lib::Surface>();surface->size={width,height};
    surface->rgba.assign(static_cast<std::size_t>(width*height)*4,0);
    ui::fill_rect(surface,{0,0,width,height},{240,240,240,255});
    if(caution) {
        const auto bg=ui::resource_surface_for_window(ui::resource_dialog_background("CAUTION"));
        if(bg && bg->size.width>0 && bg->size.height>0)
            for(I y=0;y<height;y+=bg->size.height)for(I x=0;x<width;x+=bg->size.width)
                ui::blend_surface(surface,bg,x,y);
    }
    return surface;
}
inline void button(DialogPresentation& view,const std::shared_ptr<::cw::lib::Surface>& surface,
                   const ::cw::lib::String& title,Rect rect,DialogPresentationAction action,
                   int index,bool enabled,const Font& font,bool paint=true) {
    namespace ui=::cw::lib::ui;
    if(paint) {
        ui::draw_button_face(surface,rect,{enabled});
        const auto ink=ui::render_label(title,font.pixels,font.bold,
            enabled?std::tuple<I,I,I>{0,0,0}:std::tuple<I,I,I>{128,128,128},font.face,font.italic);
        if(ink) {
            auto clip=std::make_shared<::cw::lib::Surface>();clip->size={rect.width,rect.height};
            clip->rgba.assign(static_cast<std::size_t>(rect.width*rect.height)*4,0);
            ui::blend_surface(clip,ink,(rect.width-ink->size.width)/2,(rect.height-ink->size.height)/2);
            ui::blend_surface(surface,clip,rect.x,rect.y);
        }
    }
    view.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
        static_cast<float>(rect.width),static_cast<float>(rect.height),enabled});
    view.controls.back().hover_highlight=true;
}
inline void field(const std::shared_ptr<::cw::lib::Surface>& surface,Rect rect,
                  textedit::Buffer& buffer,const Font& font,bool focused,bool bordered=true) {
    namespace ui=::cw::lib::ui;
    ui::fill_rect(surface,rect,{255,255,255,255});if(bordered)ui::draw_sunken_border(surface,rect);
    const I padx=px(bordered?4:2),pady=px(bordered?2:0);
    auto clip=std::make_shared<::cw::lib::Surface>();
    clip->size={std::max<I>(1,rect.width-2*padx),std::max<I>(1,rect.height-2*pady)};
    clip->rgba.assign(static_cast<std::size_t>(clip->size.width*clip->size.height)*4,255);
    textedit::Preedit preedit;
    if(focused)preedit.set(buffer.composition,buffer.composition_start,buffer.composition_length);
    const auto display=textedit::display_text(buffer.text,buffer.caret,buffer.anchor,preedit);
    textedit::draw_single_line(clip,display,font,static_cast<int>(std::max<I>(0,
        (clip->size.height-cell_height(font))/2)),focused,buffer.horizontal);
    ui::blend_surface(surface,clip,rect.x+padx,rect.y+pady);
}
inline std::size_t caret_at(const textedit::Buffer& buffer,Rect rect,const Font& font,float x,bool bordered=true) {
    const double target=x-rect.x-px(bordered?4:2)+buffer.horizontal;
    std::size_t pos=0;I left=0;
    while(pos<buffer.text.size()){
        const auto next=textedit::next(buffer.text,pos);
        const auto right=text_width(buffer.text.substr(0,next),font);
        if(target<(left+right)/2.0)break;
        pos=next;left=right;
    }
    return pos;
}
inline ::cw::lib::Point menu_anchor(const textedit::Buffer& buffer,Rect rect,const Font& font,bool bordered=true) {
    const I padx=px(bordered?4:2);
    const auto caret=text_width(buffer.text.substr(0,buffer.caret),font);
    return {rect.x+padx+std::clamp<I>(caret-buffer.horizontal,0,std::max<I>(0,rect.width-2*padx-1)),
            rect.y+rect.height};
}
} // namespace cw::dialog::input_presentation
