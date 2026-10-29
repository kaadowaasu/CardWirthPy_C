#pragma once
#include "cw/dialog/textedit.hpp"
#include "cw/lib/ui_surface.hpp"

namespace cw::dialog::textedit {
// テキスト・選択・未確定入力の共通の切り取り描画器。呼び出し側は座標とフ
// ォントを、論理座標または倍率適用済み座標のどちらか一方に統一して渡す。
inline void draw_single_line(const std::shared_ptr<::cw::lib::Surface>& surface,
    const DisplayText& display,const ::cw::lib::ui::FontStyle& font,int top,
    bool focused,int& horizontal) {
    const auto pixels=[&](std::size_t end) {return static_cast<int>(::cw::lib::ui::label_width(
        display.text.substr(0,end),font.pixels,font.bold,font.face,font.italic));};
    // 論理単位で扱い、倍率付きの Surface::draw_scale が自身で変換する。
    const auto area=::cw::lib::ui::logical_size(surface);
    horizontal=horizontal_offset(horizontal,pixels(display.caret),
        static_cast<int>(area.width),pixels(display.text.size()));
    const int first=pixels(display.first),last=pixels(display.last);
    const auto draw=[&](std::size_t a,std::size_t b,int x,bool selected) {
        ::cw::lib::ui::draw_label(surface,display.text.substr(a,b-a),x-horizontal,top,font.pixels,font.bold,
            selected?std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{255,255,255}:
                std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{0,0,0},font.face,font.italic);
    };
    if(focused && display.first!=display.last) {
        ::cw::lib::ui::fill_rect(surface,{first-horizontal,0,last-first,area.height},{0,120,215,255});
        draw(0,display.first,0,false);draw(display.first,display.last,first,true);
        draw(display.last,display.text.size(),last,false);
    } else {
        draw(0,display.text.size(),0,false);
        if(focused) ::cw::lib::ui::fill_rect(surface,{pixels(display.caret)-horizontal,top,1,font.pixels},{0,0,0,255});
    }
    if(focused && display.composing) {
        const int left=pixels(display.preedit_first),right=pixels(display.preedit_last);
        ::cw::lib::ui::fill_rect(surface,{left-horizontal,
            std::min<int>(area.height-1,top+font.pixels),right-left,1},{0,0,0,255});
    }
}
} // namespace cw::dialog::textedit
