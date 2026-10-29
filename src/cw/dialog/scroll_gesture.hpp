#pragma once
#include <algorithm>
#include <cmath>

namespace cw::dialog {
// 入力の受け渡しで共有するジェスチャー。ドラッグでは元の行を確定しない。ドロ
// ップダウンの選択、領域、スクロールの意味は引き続きプロバイダーが管理する。
class DialogScrollGesture {
public:
    // min_slop はタッチホストが渡す DPI 基準の移動許容量。スマートフ
    // ォンの指の揺れをドラッグと誤認しないために使う。0 は行だけを基準にする。
    void begin(float x,float y,float row_height=24.0F,float min_slop=0.0F) noexcept {
        active_=true;dragged_=false;x_=x;y_=last_y_=y;remainder_=0;
        row_=std::max(1.0F,row_height);
        slop_=std::max(row_*10.0F/24.0F,min_slop);
    }
    int move(float x,float y) noexcept {
        if(!active_ || !std::isfinite(x) || !std::isfinite(y)) return 0;
        if(std::abs(x-x_)>slop_ || std::abs(y-y_)>slop_) dragged_=true;
        remainder_+=last_y_-y;last_y_=y;
        if(!dragged_) return 0;
        const int lines=static_cast<int>(std::clamp(remainder_/row_,-128.0F,128.0F));
        remainder_-=lines*row_;
        return lines;
    }
    bool active() const noexcept {return active_;}
    bool dragged() const noexcept {return dragged_;}
    float x() const noexcept {return x_;}
    float y() const noexcept {return y_;}
    void reset() noexcept {*this={};}
private:
    bool active_{},dragged_{};
    float x_{},y_{},last_y_{},remainder_{},row_{24},slop_{10};
};
}
