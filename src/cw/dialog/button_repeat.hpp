#pragma once
#include <chrono>

namespace cw::dialog {
// 既定値は Python の SliderWithButton に合わせる。連続操作ボタンは待機・間隔の両
// 方に move_repeat を指定する。ホストは捕捉・再描画だけを担当し、OS のタイマーは使わない。
class ButtonRepeatTimer {
public:
    using Clock=std::chrono::steady_clock;
    void start(Clock::time_point now,std::chrono::milliseconds delay=std::chrono::milliseconds(500)) noexcept {next_=now+delay;}
    [[nodiscard]] bool due(Clock::time_point now) const noexcept {return now>=next_;}
    bool consume(Clock::time_point now,std::chrono::milliseconds interval=std::chrono::milliseconds(20)) noexcept {
        if(!due(now)) return false;
        // 描画が遅れた場合は一段だけ進め、失ったリピートをまとめて実行しない。
        next_=now+interval;return true;
    }
private:
    Clock::time_point next_{};
};
}
