#pragma once

#include "cw/lib/compat.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace cw::dialog {

// 共通スクロールバーの一軸。ホストは捕捉したポインターを転送するだけで、領域計算、
// ページ移動、つまみのドラッグ、リピートは Presentation が担当する。
class PresentationScrollAxis {
public:
    using Integer = ::cw::lib::Integer;
    using Clock = std::chrono::steady_clock;
    enum class Part { None, Before, After, PageBefore, PageAfter, Thumb };
    Integer length{}, page{}, extent{}, step{1}, position{};
    Integer arrow{}, track{}, thumb{}, thumb_start{};

    void configure(Integer new_length, Integer new_page, Integer new_extent,
                   Integer new_step, Integer new_position) {
        if(length!=new_length || page!=new_page || extent!=new_extent) {
            const bool was_captured=captured();cancel();suppressed_=was_captured;
        }
        length=(std::max<Integer>)(0,new_length);
        page=(std::max<Integer>)(1,new_page);
        extent=(std::max<Integer>)(page,new_extent);
        step=(std::max<Integer>)(1,new_step);
        position=std::clamp(new_position,Integer{0},maximum());
        arrow=(std::min<Integer>)(16,length/2);
        track=(std::max<Integer>)(0,length-2*arrow);
        thumb=(std::min)(track,(std::max<Integer>)(16,
            static_cast<Integer>(static_cast<double>(track)*page/extent)));
        update_thumb();
    }
    [[nodiscard]] Integer maximum() const { return (std::max<Integer>)(0,extent-page); }
    [[nodiscard]] bool visible() const { return maximum()>0 && length>0; }
    [[nodiscard]] Part hit(float p) const {
        if(!visible() || p<0 || p>=length) return Part::None;
        if(p<arrow) return Part::Before;
        if(p>=length-arrow) return Part::After;
        if(p<thumb_start) return Part::PageBefore;
        if(p>=thumb_start+thumb) return Part::PageAfter;
        return Part::Thumb;
    }
    void pointer(float p,bool pressed,Clock::time_point now=Clock::now(),bool cross_inside=true) {
        if(suppressed_) {if(!pressed) suppressed_=false;return;}
        if(!cross_inside && held_!=Part::Thumb) p=-1;
        if(!std::isfinite(p)) {cancel();return;}
        if(held_==Part::None) {
            if(!pressed) return;
            held_=hit(p);
            cursor_=p;
            grab_=p-static_cast<float>(thumb_start);
            if(held_!=Part::None && held_!=Part::Thumb) {
                advance(held_);
                deadline_=now+std::chrono::milliseconds(350);
            }
        } else {
            cursor_=p;
            if(held_==Part::Thumb && track>thumb) {
                const auto ratio=std::clamp((static_cast<double>(p)-grab_-arrow)/(track-thumb),0.0,1.0);
                position=static_cast<Integer>(std::llround(ratio*maximum()));
                update_thumb();
            }
        }
        if(!pressed) cancel();
    }
    [[nodiscard]] bool repeat_due(Clock::time_point now=Clock::now()) const {
        return held_!=Part::None && held_!=Part::Thumb && now>=deadline_ && hit(cursor_)==held_;
    }
    void tick(Clock::time_point now=Clock::now()) {
        if(repeat_due(now)) {
            advance(held_);
            deadline_=now+std::chrono::milliseconds(60);
        }
    }
    void cancel() noexcept {held_=Part::None;suppressed_=false;}
    [[nodiscard]] bool captured() const noexcept {return held_!=Part::None;}
private:
    void update_thumb() {
        thumb_start=arrow+(maximum()>0
            ?static_cast<Integer>(std::llround(static_cast<double>(track-thumb)*position/maximum())):0);
    }
    void advance(Part part) {
        const auto amount=(part==Part::Before || part==Part::After)?step:page;
        // 飽和演算で、非常に大きい仮想領域にも対応する。
        if(part==Part::Before || part==Part::PageBefore) position-= (std::min)(position,amount);
        else position+=(std::min)(maximum()-position,amount);
        update_thumb();
    }
    bool suppressed_{};
    Part held_{Part::None};
    float cursor_{},grab_{};
    Clock::time_point deadline_{};
};

} // namespace cw::dialog
