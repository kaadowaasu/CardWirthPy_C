#pragma once
#include "cw/lib/input_clock.hpp"
#include <SDL3/SDL.h>
#include <chrono>

namespace cw::lib {
inline double sdl_input_timestamp_ms(std::uint64_t timestamp) noexcept {
    // 両 SDL 入力取得アダプターは、Relay と長押しタイマーと同じ単
    // 調時計を使う。配信時刻ではなく、キュー内のイベントの経過時間を保つ。
    const auto source_now = SDL_GetTicksNS();
    const auto target_now = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return input_timestamp_ms(timestamp, source_now, target_now);
}
}
