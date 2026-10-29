#pragma once
#include <SDL3/SDL_keycode.h>

namespace cw::lib {
// デスクトップ・モバイル、単体・統合ホストで一つの Engine 入力方針を使う。SDL の
// Android は Escape ではなく AC_BACK を出す。Backspace は
// キーイベントのままにし、共通テキストモデルで修飾キーと IME の規則を一貫して適用する。
constexpr SDL_Keycode engine_sdl_key(SDL_Keycode key) noexcept {
    return key == SDLK_AC_BACK ? SDLK_ESCAPE : key;
}
constexpr bool engine_sdl_cancel(SDL_Keycode key, bool down, bool repeat) noexcept {
    // 戻るキーの長押しで、リピートによって子に続いて親まで閉じてはならない。
    return down && !repeat && engine_sdl_key(key) == SDLK_ESCAPE;
}
}
