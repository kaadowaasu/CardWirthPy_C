#pragma once

#include <SDL3/SDL_keycode.h>
#include <cstdint>

#include "cw/eventhandler.hpp"
#include "cw/lib/sdl_key.hpp"

namespace cw::lib {

// ALL ホストの境界で SDL3 のキーコードを、全 OS の Frame::OnKey*
// と KeyEventRelay が受ける共通キーへ正規化する。Windows
// だけの VK 変換では、Linux/macOS/Android の特殊キーが
// SDL の上位ビット付きのまま残り、Python 互換の中継に無視される。
constexpr ::cw::lib::Integer engine_game_key_from_sdl(SDL_Keycode key) noexcept {
    key = engine_sdl_key(key);
    if (key >= SDLK_A && key <= SDLK_Z)
        return static_cast<::cw::lib::Integer>('A' + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9)
        return static_cast<::cw::lib::Integer>('0' + (key - SDLK_0));

    using namespace ::cw::eventhandler;
    switch (key) {
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_RETURN;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_BACKSLASH: return KEY_BACKSLASH;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_PAGEUP: return KEY_PAGEUP;
    case SDLK_PAGEDOWN: return KEY_PAGEDOWN;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_END: return KEY_END;
    // Python/wx は共通の Shift/Control を公開し、左右どちらの
    // 物理キーも pygame の左側の状態に対応させる。その互換性を意図的に保つ。
    case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_LSHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_LCTRL;
    case SDLK_PRINTSCREEN: return KEY_PRINT;
    default:
        if (key >= SDLK_F1 && key <= SDLK_F12)
            return KEY_F1 + static_cast<::cw::lib::Integer>(key - SDLK_F1);
        return static_cast<::cw::lib::Integer>(key);
    }
}

constexpr std::uint32_t engine_input_modifiers_from_sdl(SDL_Keymod modifiers) noexcept {
    std::uint32_t result = 0;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) result |= 1U;
    if ((modifiers & SDL_KMOD_CTRL) != 0
#if defined(__APPLE__)
        || (modifiers & SDL_KMOD_GUI) != 0
#endif
    ) result |= 2U;
    if ((modifiers & SDL_KMOD_ALT) != 0) result |= 4U;
    return result;
}

}  // namespace cw::lib
