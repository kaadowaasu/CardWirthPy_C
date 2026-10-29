#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

namespace cardwirth::app {

// 埋め込みEngineがFrame.SetTitleの値をこのスナップショッ
// トへ書く。 ネイティブウィンドウを所有するALLシェルは、その値を使う。
inline void publish_hosted_engine_title(
    std::string& snapshot_title, const std::string_view engine_title) {
    snapshot_title = std::string(engine_title);
}

// ネイティブの最上位ウィンドウはALLが所有し、Engineは共通Frame
// のタイトルだけを提供する。 タイトルの選択とSDLへの適用を共
// 通化し、製品と埋め込みFrameの契約テストで同じ経路を使う。
inline void apply_hosted_engine_window_title(
    SDL_Window* window, const bool engine_page, const std::string_view engine_title) {
    if (!window) return;
    const std::string title = engine_page && !engine_title.empty()
        ? std::string(engine_title) : "CardWirthPy_C";
    const char* current_title = SDL_GetWindowTitle(window);
    if (!current_title || title != current_title)
        SDL_SetWindowTitle(window, title.c_str());
}

}  // namespace cardwirth::app
