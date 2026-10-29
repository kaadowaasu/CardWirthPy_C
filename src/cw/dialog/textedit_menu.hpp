#pragma once

#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit_input.hpp"
#include "cw/lib/ui_surface.hpp"
#include <array>
#include <cmath>
#include <optional>

namespace cw::dialog::textedit {

// 編集メニューの状態と操作を共通化し、UI
// は一系統とする。所有者が既存の Buffer
// とクリップボード境界を渡し、ネイティブ
// ウィンドウや第二のテキスト値は作らない。
class EditMenu {
public:
    enum Command { Undo, Cut, Copy, Paste, DeleteSelection, SelectAll, Count };
    static constexpr int command_base=41000,dismiss_control=40999;
    using Read=std::function<std::string()>;
    using Write=std::function<void(const std::string&)>;
    static bool is_control(DialogPresentationAction action,int index) noexcept {
        return action==DialogPresentationAction::Auxiliary &&
            (index==dismiss_control || (index>=command_base && index<command_base+Count));
    }
    bool opened() const noexcept {return opened_;}
    void close() noexcept {opened_=false;selected_=-1;first_=0;}
    void open(const Buffer& buffer,bool clipboard_available,float x,float y,bool keyboard=false,bool readonly=false) {
        if(!buffer.composition.empty() || !std::isfinite(x) || !std::isfinite(y)) return;
        readonly_=readonly;clipboard_available_=clipboard_available;anchor_={static_cast<::cw::lib::Integer>(x),static_cast<::cw::lib::Integer>(y)};
        opened_=true;first_=0;selected_=-1;hits_.clear();
        if(keyboard) move(buffer,1);
    }
    bool enabled(Command command,const Buffer& buffer) const {
        if(!buffer.composition.empty()) return false;
        if(readonly_ && command!=Copy && command!=SelectAll) return false;
        const bool selected=buffer.caret!=buffer.anchor;
        switch(command) {
        case Undo:return !buffer.undo.empty();
        case Cut:case Copy:case DeleteSelection:return selected;
        case Paste:return clipboard_available_;
        case SelectAll:return !buffer.text.empty();
        default:return false;
        }
    }
    bool action(DialogPresentationAction action,int index,Buffer& buffer,const Read& read,const Write& write,std::size_t limit=0,bool multiline=false) {
        if(!opened_) return false;
        if(action==DialogPresentationAction::Auxiliary && index>=command_base && index<command_base+Count)
            invoke(static_cast<Command>(index-command_base),buffer,read,write,limit,multiline);
        else close(); // メニューを閉じる入力を消費し、下の OK/Cancel へ渡さない。
        return true;
    }
    bool key(DialogPresentationKey key,DialogPresentationModifiers modifiers,Buffer& buffer,
             const Read& read,const Write& write,std::size_t limit=0,bool multiline=false) {
        if(!opened_) return false;
        using Key=DialogPresentationKey;
        if(key==Key::Escape || key==Key::Tab || key==Key::Left || key==Key::Right) close();
        else if(key==Key::Up || key==Key::Down) move(buffer,key==Key::Up?-1:1);
        else if(key==Key::Home || key==Key::End) {selected_=-1;move(buffer,key==Key::Home?1:-1);}
        else if(key==Key::Enter || key==Key::Space) {
            if(selected_>=0) invoke(static_cast<Command>(selected_),buffer,read,write,limit,multiline);
        } else if(!(modifiers & DialogPresentationModifierAlt)) {
            int command=-1;
            if(modifiers & DialogPresentationModifierControl) {
                if(key==Key::Z) command=Undo;
                if(key==Key::X) command=Cut;
                if(key==Key::C || key==Key::Insert) command=Copy;
                if(key==Key::V) command=Paste;
                if(key==Key::A) command=SelectAll;
            } else if(modifiers & DialogPresentationModifierShift) {
                if(key==Key::Insert) command=Paste;
                if(key==Key::Delete) command=Cut;
            } else {
                if(key==Key::U) command=Undo;
                if(key==Key::T) command=Cut;
                if(key==Key::C) command=Copy;
                if(key==Key::P) command=Paste;
                if(key==Key::D || key==Key::Delete) command=DeleteSelection;
                if(key==Key::A) command=SelectAll;
            }
            if(command>=0) invoke(static_cast<Command>(command),buffer,read,write,limit,multiline);
        }
        return true;
    }
    bool hover(float x,float y,const Buffer& buffer) {
        if(!opened_ || !std::isfinite(x) || !std::isfinite(y)) return false;
        const int before=selected_;
        selected_=-1;
        for(const auto& [command,rect]:hits_) if(x>=rect.x && x<rect.x+rect.width &&
                y>=rect.y && y<rect.y+rect.height && enabled(command,buffer)) {selected_=command;break;}
        return before!=selected_;
    }
    bool scroll(int lines,const Buffer& buffer) {
        if(!opened_) return false;
        for(int n=0;n<std::min<long long>(Count,std::abs(static_cast<long long>(lines)));++n) move(buffer,lines<0?-1:1);
        return true;
    }
    [[nodiscard]] std::uint64_t presentation_signature(const Buffer& buffer) const {
        std::uint64_t value=static_cast<std::uint64_t>(opened_);
        value^=static_cast<std::uint64_t>(selected_+2)<<8U;
        value^=static_cast<std::uint64_t>(first_+1)<<16U;
        value^=static_cast<std::uint64_t>(clipboard_available_)<<24U;
        for(int command=0;command<Count;++command)
            value^=static_cast<std::uint64_t>(enabled(static_cast<Command>(command),buffer))<<(32U+command);
        return value;
    }
    [[nodiscard]] ::cw::lib::Rect presentation_bounds(
        ::cw::lib::Size surface_size,double scale=1.0,
        std::optional<::cw::lib::ui::FontStyle> supplied_font=std::nullopt) const {
        if(!opened_ || surface_size.width<3 || surface_size.height<1)return {};
        namespace ui=::cw::lib::ui;using I=::cw::lib::Integer;
        const auto metric=[&](I value){return std::max<I>(1,static_cast<I>(std::lround(value*scale)));};
        const auto font=supplied_font.value_or(ui::configured_font("menu",metric(13)));
        I cell_height=font.pixels,width=metric(180);
        for(const auto& label:labels_)if(const auto glyph=ui::render_label(label,font.pixels,font.bold,{0,0,0},font.face,font.italic)) {
            cell_height=std::max(cell_height,glyph->size.height);width=std::max(width,glyph->size.width+metric(20));
        }
        const I row_height=std::max<I>(metric(18),cell_height+metric(4)),padding=metric(3);
        const int visible=static_cast<int>(std::clamp<I>((surface_size.height-2*padding)/row_height,1,Count));
        width=std::min(width,surface_size.width);const I height=std::min<I>(surface_size.height,visible*row_height+2*padding);
        return {std::clamp<I>(anchor_.x,0,surface_size.width-width),std::clamp<I>(anchor_.y,0,surface_size.height-height),width,height};
    }
    // 座標は所有者の描画面内。論理座標の面は倍率 1 を使い、倍率適用済
    // みの選択画面は計測用倍率だけを渡す。ビットマップは再拡大しない。
    void draw(DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,const Buffer& buffer,double scale=1.0,
              std::optional<::cw::lib::ui::FontStyle> supplied_font=std::nullopt,bool paint=true) {
        if(!opened_ || !surface || surface->size.width<3 || surface->size.height<1) return;
        namespace ui=::cw::lib::ui;
        using I=::cw::lib::Integer;
        const auto metric=[&](I value) {return std::max<I>(1,static_cast<I>(std::lround(value*scale)));};
        const auto font=supplied_font.value_or(ui::configured_font("menu",metric(13)));
        // 同じスタイル付き文字領域を計測・描画する。特に代替・斜体フ
        // ォントでは、要求画素サイズは文字領域の高さと一致しない。
        std::array<std::shared_ptr<::cw::lib::Surface>,Count> glyphs;
        I cell_height=font.pixels,width=metric(180);
        for(std::size_t i=0;i<glyphs.size();++i) {
            glyphs[i]=ui::render_label(labels_[i],font.pixels,font.bold,{0,0,0},font.face,font.italic);
            if(glyphs[i]) {
                cell_height=std::max(cell_height,glyphs[i]->size.height);
                width=std::max(width,glyphs[i]->size.width+metric(20));
            }
        }
        const I row_height=std::max<I>(metric(18),cell_height+metric(4)),padding=metric(3);
        const int visible=static_cast<int>(std::clamp<I>((surface->size.height-2*padding)/row_height,1,Count));
        if(selected_>=0) {
            if(selected_<first_) first_=selected_;
            if(selected_>=first_+visible) first_=selected_-visible+1;
        }
        first_=std::clamp(first_,0,Count-visible);
        width=std::min(width,surface->size.width);
        const I height=std::min<I>(surface->size.height,visible*row_height+2*padding);
        const I left=std::clamp<I>(anchor_.x,0,surface->size.width-width);
        const I top=std::clamp<I>(anchor_.y,0,surface->size.height-height);
        result.controls.push_back({DialogPresentationAction::Auxiliary,dismiss_control,0,0,
            static_cast<float>(surface->size.width),static_cast<float>(surface->size.height),true});
        if(paint) ui::fill_rect(surface,{left,top,width,height},{245,245,245,255});
        hits_.clear();
        for(int row=0;row<visible;++row) {
            const auto command=static_cast<Command>(first_+row);
            const bool available=enabled(command,buffer),highlight=selected_==command && available;
            const I row_y=std::min<I>(surface->size.height,top+padding+row*row_height);
            const ::cw::lib::Rect rect{left+1,row_y,width-2,std::min<I>(row_height,surface->size.height-row_y)};
            const auto selection=ui::system_selection_colours();
            if(paint) ui::fill_rect(surface,rect,highlight?selection.background: ::cw::lib::Color{245,245,245,255});
            const auto color=!available?std::tuple<I,I,I>{128,128,128}:highlight?
                std::tuple<I,I,I>{selection.text.r,selection.text.g,selection.text.b}:std::tuple<I,I,I>{0,0,0};
            if(const auto& glyph=glyphs[command];paint && glyph && rect.width>0 && rect.height>0) {
                auto tinted=std::make_shared<::cw::lib::Surface>(*glyph);
                for(std::size_t p=0;p<tinted->rgba.size();p+=4) {
                    tinted->rgba[p]=static_cast<std::uint8_t>(std::get<0>(color));
                    tinted->rgba[p+1]=static_cast<std::uint8_t>(std::get<1>(color));
                    tinted->rgba[p+2]=static_cast<std::uint8_t>(std::get<2>(color));
                }
                auto clip=std::make_shared<::cw::lib::Surface>();clip->size={rect.width,rect.height};
                clip->rgba.assign(static_cast<std::size_t>(rect.width)*rect.height*4,0);
                ui::blend_surface(clip,tinted,metric(8),(row_height-tinted->size.height)/2);
                ui::blend_surface(surface,clip,rect.x,rect.y);
            }
            // 無効な項目もクリックを受け止め、背後の領域を通じてメニ
            // ューを閉じたり、下のコントロールを実行したりしない。
            result.controls.push_back({DialogPresentationAction::Auxiliary,command_base+command,
                static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),
                static_cast<float>(std::min<I>(rect.height,surface->size.height-rect.y)),true});
            hits_.push_back({command,rect});
        }
        if(paint) ui::draw_sunken_border(surface,{left,top,width,height});
    }
private:
    bool readonly_{};
    bool opened_{},clipboard_available_{};
    int selected_{-1},first_{};
    ::cw::lib::Point anchor_{};
    std::vector<std::pair<Command,::cw::lib::Rect>> hits_;
    inline static const std::array<std::string,Count> labels_{
        "元に戻す (U)","切り取り (T)","コピー (C)","貼り付け (P)","削除 (D)","すべて選択 (A)"};
    void move(const Buffer& buffer,int delta) {
        int row=selected_<0?(delta>0?-1:0):selected_;
        for(int n=0;n<Count;++n) {
            row=(row+delta+Count)%Count;
            if(enabled(static_cast<Command>(row),buffer)) {selected_=row;return;}
        }
        selected_=-1;
    }
    void invoke(Command command,Buffer& buffer,const Read& read,const Write& write,std::size_t limit,bool multiline) {
        if(!enabled(command,buffer)) return;
        // 所有者へ再入し得るホスト操作を行う前に閉じる。
        close();
        const auto first=std::min(buffer.caret,buffer.anchor),last=std::max(buffer.caret,buffer.anchor);
        switch(command) {
        case Undo:buffer.restore(false);break;
        case Cut:
            if(write) write(buffer.text.substr(first,last-first));
            buffer.insert("",limit,multiline);break;
        case Copy:if(write) write(buffer.text.substr(first,last-first));break;
        case Paste:if(read) buffer.insert(read(),limit,multiline);break;
        case DeleteSelection:buffer.insert("",limit,multiline);break;
        case SelectAll:buffer.anchor=0;buffer.move(buffer.text.size(),true);break;
        default:break;
        }
    }
};

} // namespace cw::dialog::textedit
