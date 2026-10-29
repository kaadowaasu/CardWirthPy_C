#pragma once
#include "cw/dialog/textedit_menu.hpp"
#include "cw/dialog/textedit_render.hpp"

namespace cw::dialog::textedit {
// 選択画面の絞り込み用の共通編集欄。UI
// は一系統とし、位置・サイズ・フォント
// は最終画素単位。所有する選択画面だけが
// 、確定テキストを絞り込みモデルへ渡す。
struct FilterInput {
    Buffer edit;
    EditMenu menu;
    EditMenu::Read read=::cw::dialog::presentation_clipboard_text;
    EditMenu::Write write=::cw::dialog::presentation_set_clipboard_text;
    DialogPresentationModifiers modifiers{};
    bool dragging{}, visible{};
    ::cw::lib::Rect rect{};
    ::cw::lib::ui::FontStyle font{};
    int padding{},alignment{};
    double menu_scale{1.0};
    static bool menu_control(DialogPresentationAction action,int index) {
        return action==DialogPresentationAction::Auxiliary &&
            (index==EditMenu::dismiss_control ||
             (index>=EditMenu::command_base && index<EditMenu::command_base+EditMenu::Count));
    }
    void retire() {menu.close();edit.composition.clear();dragging=false;}
    void sync(const std::string& text) {
        if(edit.text!=text) {retire();edit.reset(text,false,false);}
    }
    void begin_frame() {visible=false;}
    bool contains(float x,float y) const {
        return visible && std::isfinite(x) && std::isfinite(y) &&
            x>=rect.x && x<rect.x+rect.width && y>=rect.y && y<rect.y+rect.height;
    }
    void draw(const std::shared_ptr<::cw::lib::Surface>& surface,const ::cw::lib::Rect& field,
              bool focused,int pad,const char* font_type="paneltitle2",int pixels=15,bool align_right=false,
              bool scale_font=true,bool paint=true) {
        namespace ui=::cw::lib::ui;
        rect=field;padding=pad;visible=true;
        font=scale_font?ui::configured_font_scaled(font_type,pixels):ui::configured_font(font_type,pixels);
        menu_scale=scale_font?static_cast<double>(ui::win_i(100))/100.0:1.0;
        // Python の get_wxfont の明示的な画素指定に対する wx3 補正は、wins() の後に適用する。
        font.pixels=std::max<::cw::lib::Integer>(1,font.pixels-1);
        const Preedit preedit{edit.composition,edit.composition_start,edit.composition_length};
        const auto display=display_text(edit.text,edit.caret,edit.anchor,focused?preedit:Preedit{});
        const auto width=std::max<::cw::lib::Integer>(1,rect.width-2*padding);
        alignment=align_right?static_cast<int>(std::max<::cw::lib::Integer>(0,width-
            ui::label_width(display.text,font.pixels,font.bold,font.face,font.italic)-1)):0;
        if(!paint)return;
        auto clipped=std::make_shared<::cw::lib::Surface>();
        clipped->size={width-alignment,std::max<::cw::lib::Integer>(1,rect.height-2)};
        clipped->rgba.assign(static_cast<std::size_t>(clipped->size.width*clipped->size.height*4),255);
        draw_single_line(clipped,display,font,static_cast<int>((clipped->size.height-font.pixels)/2),
                         focused,edit.horizontal);
        ui::blend_surface(surface,clipped,rect.x+padding+alignment,rect.y+1);
    }
    void finish_frame(DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,
                      bool focused,bool enabled) {
        if(!visible || !focused || !enabled) retire();
        result.wants_text_input=visible && focused && enabled && !menu.opened();
        if(result.wants_text_input) {
            result.composition=edit.composition;result.composition_start=edit.composition_start;
            result.composition_length=edit.composition_length;
        } else {result.composition.clear();result.composition_start=-1;result.composition_length=0;}
        if(enabled && visible && menu.opened())
            menu.draw(result,surface,edit,menu_scale);
    }
    bool text(const std::string& value,bool composing,int start=-1,int length=0) {
        if(menu.opened()) return false;
        if(composing) edit.set_composition(value,start,length);
        else {
            if(value=="\b" && !edit.composition.empty()) return false;
            edit.insert(value,0,false);
        }
        return true;
    }
    bool key(DialogPresentationKey key,DialogPresentationModifiers mods) {
        modifiers=mods;
        if(menu.key(key,mods,edit,read,write)) return true;
        if(!edit.composition.empty()) return true;
        if(key==DialogPresentationKey::ContextMenu ||
           (key==DialogPresentationKey::F10 && (mods & DialogPresentationModifierShift))) {
            const auto x=rect.x+padding+alignment+::cw::lib::ui::label_width(
                edit.text.substr(0,edit.caret),font.pixels,font.bold,font.face,font.italic)-edit.horizontal;
            open(static_cast<float>(std::clamp<::cw::lib::Integer>(x,rect.x,rect.x+rect.width-1)),
                 static_cast<float>(rect.y+rect.height),true);
            return true;
        }
        return single_line_key(edit,key,mods,0,read,write);
    }
    bool pointer(float x,float y,bool pressed) {
        if(!visible || menu.opened() || !edit.composition.empty() ||
           !std::isfinite(x) || !std::isfinite(y) || (!pressed && !dragging)) return false;
        const auto target=x-rect.x-padding-alignment+edit.horizontal;
        std::size_t pos=0;::cw::lib::Integer left=0;
        while(pos<edit.text.size()) {
            const auto end=next(edit.text,pos);
            const auto right=::cw::lib::ui::label_width(edit.text.substr(0,end),font.pixels,font.bold,font.face,font.italic);
            if(target<(left+right)/2.0f) break;
            pos=end;left=right;
        }
        edit.move(pos,dragging || (modifiers & DialogPresentationModifierShift));
        dragging=pressed;
        return true;
    }
    void open(float x,float y,bool keyboard=false) {
        if(!visible || !edit.composition.empty()) return;
        dragging=false;
        menu.open(edit,!read().empty(),x,y,keyboard);
    }
};
} // namespace cw::dialog::textedit
