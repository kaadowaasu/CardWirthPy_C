#pragma once
#include "cw/lib/text_raster.hpp"
#include "cw/lib/ui_surface.hpp"

namespace cw::dialog::cardcontrol {
// UI の実装は共通の一系統とする。Python の CardControl.OnPaint2
// と同様に、描画ごとに最終画素サイズの価格用フォ
// ント・画像を一つ使う。ウィンドウ、実行時のトラ
// ンザクション、別の OS 描画経路は持たない。
class CardPricePresentation {
public:
    using I=::cw::lib::Integer;
    using Surface=::cw::lib::Surface;
    CardPricePresentation(const ::cw::lib::ui::FontStyle& style,I padding,I card_width)
        : padding_(padding) {
        if(style.pixels<=0 || style.pixels>(std::numeric_limits<I>::max)()/2 || padding<0)return;
        font_=std::make_shared<::cw::imageretouch::Font>();
        // get_wxfont("price", wins(configured_pixels)*2, adjustsizewx3=False).
        font_->initialize(style.face,-style.pixels*2,style.bold,style.italic);
        const auto zero=font_->render("0",true,{0,0,0});
        if(zero && zero->size.height>1 && card_width>4*padding_) {
            sheet_=std::make_shared<Surface>();
            sheet_->size={card_width-4*padding_,zero->size.height/2+2*padding_};
            sheet_->rgba.resize(static_cast<std::size_t>(sheet_->size.width)*sheet_->size.height*4,255);
            for(std::size_t i=3;i<sheet_->rgba.size();i+=4)sheet_->rgba[i]=160;
        }
    }
    const std::shared_ptr<Surface>& sheet() const {return sheet_;}
    void draw(const std::shared_ptr<Surface>& destination,const ::cw::lib::String& text,
              I x,I y,I width,I height) const {
        const I maxwidth=width-4*padding_;
        if(!destination || !sheet_ || maxwidth<=padding_)return;
        const auto raw=font_->render(text,true,{0,0,0});
        if(!raw || raw->size.width<2 || raw->size.height<2)return;
        const I pw=raw->size.width/2,ph=raw->size.height/2;
        // Python と同様に、圧縮前の幅と高さで中央に配置する。
        const I px=x+2*padding_+(maxwidth-std::min(maxwidth,pw))/2;
        const I py=y+height-ph-3*padding_;
        ::cw::lib::ui::blend_surface(destination,sheet_,x+2*padding_,py-padding_);
        const auto glyph=::cw::lib::text_raster::resized(raw,
            std::min(pw,maxwidth-padding_),ph,::cw::lib::text_raster::ResizeQuality::High);
        if(!glyph)return;
        auto outline=std::make_shared<Surface>(*glyph);
        for(std::size_t i=0;i<outline->rgba.size();i+=4)
            outline->rgba[i]=outline->rgba[i+1]=outline->rgba[i+2]=255;
        // Python は完全に不透明な白い縁を、物理一画素ずつずらした八方向に描く。
        for(I dy=-1;dy<=1;++dy)for(I dx=-1;dx<=1;++dx)
            if(dx || dy)::cw::lib::ui::blend_surface(destination,outline,px+dx,py+dy);
        ::cw::lib::ui::blend_surface(destination,glyph,px,py);
    }
private:
    I padding_{};
    std::shared_ptr<::cw::imageretouch::Font> font_;
    std::shared_ptr<Surface> sheet_;
};
} // namespace cw::dialog::cardcontrol
