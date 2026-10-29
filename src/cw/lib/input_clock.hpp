#pragma once
#include <cstdint>

namespace cw::lib {
// 時計の基準時点をまたいでもイベントの経過時間を保つ。変換前に符号
// なしの日時を減算し、未来の日時でアンダーフローしないようにする。
inline double input_timestamp_ms(std::uint64_t event_ns, std::uint64_t source_now_ns,
                                 double target_now_ms) noexcept {
    if (!event_ns) return target_now_ms;
    return event_ns <= source_now_ns
        ? target_now_ms - static_cast<double>(source_now_ns - event_ns) / 1000000.0
        : target_now_ms + static_cast<double>(event_ns - source_now_ns) / 1000000.0;
}
}
