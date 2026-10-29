#pragma once
#include "cw/lib/compat.hpp"

namespace cw::lib::font_size {
// Engine の共通フォントサイズ境界。第二の UI やゲーム・ウ
// ィンドウ倍率ではない。全 OS で要求画素数を保ち、OS 固有の
// 代替フォントでフェイスが変わっても、要求サイズを黙って変えない。
enum class Target { Other, Android, Linux };
[[nodiscard]] constexpr Target build_target() noexcept {
#if defined(__EMSCRIPTEN__) || defined(_WIN32) || defined(__APPLE__)
    return Target::Other;
#elif defined(__ANDROID__)
    return Target::Android;
#elif defined(__linux__)
    return Target::Linux;
#else
    return Target::Other;
#endif
}

// 全 OS で、呼び出し側が要求した文字領域・文字の高さを保持する。
[[nodiscard]] constexpr Integer raster_pixels(
    Integer requested, Target target = build_target()) noexcept {
    (void)target;
    return requested;
}
} // namespace cw::lib::font_size
