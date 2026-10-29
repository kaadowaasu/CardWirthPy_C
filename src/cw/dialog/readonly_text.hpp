#pragma once
#include "cw/dialog/textedit_menu.hpp"
#include "cw/lib/ui_surface.hpp"

namespace cw::dialog::textedit {
// 選択可能な読み取り専用テキストの共通処理。ホストとの境界はクリップボード
// だけで、配置、UTF-8 の選択、キー・メニューの方針は全ホストで同じ。
class ReadonlyText {
public:
    Buffer buffer;
    EditMenu menu;
    ::cw::lib::Rect area{};
    ::cw::lib::ui::FontStyle font;
    bool focused{},dragging{};
    void set(const std::string& value) {if(buffer.text!=value) buffer.reset(value,false,false);}
    void layout(::cw::lib::Rect rect,::cw::lib::ui::FontStyle style) {
        area=rect;font=std::move(style);line_step=std::max<::cw::lib::Integer>(1,font.pixels+1);
        lines=::cw::dialog::textedit::layout(buffer.text,0,[](auto){return 0;});
        origins.clear();const auto row_height=std::max<::cw::lib::Integer>(1,font.pixels+1);
        const auto top=area.y+std::max<::cw::lib::Integer>(0,(area.height-row_height*static_cast<::cw::lib::Integer>(lines.size()))/2);
        for(const auto& line:lines) origins.push_back({area.x+std::max<::cw::lib::Integer>(0,
            (area.width-measure(line.first,line.last))/2),top+row_height*static_cast<::cw::lib::Integer>(origins.size())});
    }
    // 複数行の wx 読み取り専用欄は、フッターと同じ選択・クリップボー
    // ドの方針を使う。行は左揃えで折り返し、表示領域をスクロールする。
    void layout_wrapped(::cw::lib::Rect rect,::cw::lib::ui::FontStyle style,
                        const std::vector<Line>& wrapped,::cw::lib::Integer step,
                        ::cw::lib::Integer scroll) {
        area=rect;font=std::move(style);lines=wrapped;origins.clear();
        line_step=std::max<::cw::lib::Integer>(1,step);
        for(std::size_t i=0;i<lines.size();++i)
            origins.push_back({area.x,area.y+(static_cast<::cw::lib::Integer>(i)-scroll)*line_step});
    }
    void translate(::cw::lib::Integer x,::cw::lib::Integer y) {
        area.x+=x;area.y+=y;for(auto& point:origins){point.x+=x;point.y+=y;}
    }
    std::size_t caret_row()const {
        std::size_t row=0;while(row+1<lines.size() && buffer.caret>lines[row].last)++row;
        return row;
    }
    bool contains(float x,float y)const{return x>=area.x && x<area.x+area.width && y>=area.y && y<area.y+area.height;}
    void blur(){focused=dragging=false;menu.close();}
    bool pointer(float x,float y,bool pressed,bool shift=false) {
        if(pressed && contains(x,y)){focused=dragging=true;menu.close();buffer.move(hit(x,y),shift);return true;}
        if(!pressed && dragging){buffer.move(hit(x,y),true);dragging=false;return true;}
        return false;
    }
    bool hover(float x,float y) {if(dragging){buffer.move(hit(x,y),true);return true;}return menu.hover(x,y,buffer);}
    bool key(DialogPresentationKey key,DialogPresentationModifiers modifiers,const EditMenu::Write& write) {
        if(menu.key(key,modifiers,buffer,[]{return std::string{};},write,0,true)) return true;
        if(!focused)return false;
        using Key=DialogPresentationKey;
        const bool ctrl=modifiers&DialogPresentationModifierControl,shift=modifiers&DialogPresentationModifierShift;
        if(modifiers&DialogPresentationModifierAlt)return false;
        if(key==Key::ContextMenu || (key==Key::F10 && shift)){menu.open(buffer,false,area.x,area.y,true,true);return true;}
        if(ctrl && key==Key::A){buffer.anchor=0;buffer.move(buffer.text.size(),true);return true;}
        if((ctrl && key==Key::C)||(ctrl && key==Key::Insert)) {
            if(buffer.caret!=buffer.anchor)write(buffer.text.substr(std::min(buffer.caret,buffer.anchor),
                std::max(buffer.caret,buffer.anchor)-std::min(buffer.caret,buffer.anchor)));
            return true;
        }
        if(key==Key::Home || key==Key::End || key==Key::Left || key==Key::Right || key==Key::Up || key==Key::Down) {
            auto pos=buffer.caret;
            if((key==Key::Home || key==Key::End)&&ctrl) pos=key==Key::Home?0:buffer.text.size();
            else {
                std::size_t row=0;while(row+1<lines.size() && buffer.caret>lines[row].last)++row;
                if(key==Key::Home || key==Key::End)pos=key==Key::Home?lines[row].first:lines[row].last;
                else if(key==Key::Left || key==Key::Right)pos=!shift && buffer.caret!=buffer.anchor?
                    (key==Key::Left?std::min(buffer.caret,buffer.anchor):std::max(buffer.caret,buffer.anchor)):
                    (key==Key::Left?previous(buffer.text,buffer.caret):next(buffer.text,buffer.caret));
                else if(!lines.empty()) {
                    const auto column=count(std::string_view(buffer.text).substr(lines[row].first,buffer.caret-lines[row].first));
                    row=key==Key::Up?(row?row-1:0):std::min(row+1,lines.size()-1);pos=lines[row].first;
                    for(std::size_t n=0;n<column && pos<lines[row].last;++n)pos=next(buffer.text,pos);
                }
            }
            buffer.move(pos,shift);return true;
        }
        if(key==Key::Escape || key==Key::Tab){blur();return false;}
        // wx の読み取り専用コントロールは、テキストを変えずに編集コマンドを消費する。
        return key==Key::Backspace || key==Key::Delete || key==Key::V || key==Key::X || key==Key::Z || key==Key::Y;
    }
    void context(float x,float y) {focused=true;dragging=false;menu.open(buffer,false,x,y,false,true);}
    bool action(DialogPresentationAction action,int index,const EditMenu::Write& write) {
        return menu.action(action,index,buffer,[]{return std::string{};},write,0,true);
    }
    void draw(const std::shared_ptr<::cw::lib::Surface>& surface,::cw::lib::Point origin={}) const {
        if(!surface || area.width<=0 || area.height<=0)return;namespace ui=::cw::lib::ui;
        auto layer=std::make_shared<::cw::lib::Surface>();layer->size={area.width,area.height};
        layer->rgba.assign(static_cast<std::size_t>(area.width)*area.height*4U,0);
        const auto first=std::min(buffer.caret,buffer.anchor),last=std::max(buffer.caret,buffer.anchor);
        for(std::size_t i=0;i<lines.size();++i) {
            const auto& line=lines[i];const auto start=std::clamp(first,line.first,line.last),end=std::clamp(last,line.first,line.last);
            const auto point=origins[i];
            if(point.y+line_step<=area.y || point.y>=area.y+area.height)continue;
            const auto a=measure(line.first,start),b=measure(line.first,end);
            if(focused && start!=end)ui::fill_rect(layer,{point.x+a-area.x,point.y-area.y,b-a,font.pixels+1},{0,120,215,255});
            const auto draw=[&](std::size_t from,std::size_t to,::cw::lib::Integer offset,bool selected) {
                ui::draw_label(layer,buffer.text.substr(from,to-from),point.x+offset-area.x,point.y-area.y,font.pixels,font.bold,
                    selected?std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{255,255,255}:
                        std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{0,0,0},font.face,font.italic);
            };
            draw(line.first,start,0,false);draw(start,end,a,focused);draw(end,line.last,b,false);
        }
        ui::blend_surface(surface,layer,area.x-origin.x,area.y-origin.y);
    }
    std::uint64_t signature()const {
        return std::hash<std::string>{}(buffer.text)^static_cast<std::uint64_t>(focused)^
            (buffer.caret<<1U)^(buffer.anchor<<3U)^menu.presentation_signature(buffer);
    }
private:
    std::vector<Line>lines;
    std::vector<::cw::lib::Point>origins;
    ::cw::lib::Integer line_step{1};
    ::cw::lib::Integer measure(std::size_t first,std::size_t last)const {
        return ::cw::lib::ui::label_width(buffer.text.substr(first,last-first),font.pixels,font.bold,font.face,font.italic);
    }
    std::size_t hit(float x,float y)const {
        if(lines.empty())return 0;
        const auto row=static_cast<std::size_t>(std::clamp<::cw::lib::Integer>(
            static_cast<::cw::lib::Integer>(std::floor((y-origins.front().y)/line_step)),0,lines.size()-1));
        const auto& line=lines[row];x-=origins[row].x;auto pos=line.first;
        while(pos<line.last){const auto end=next(buffer.text,pos);if(x<(measure(line.first,pos)+measure(line.first,end))*0.5F)return pos;pos=end;}
        return line.last;
    }
};
} // namespace cw::dialog::textedit
