#pragma once
#include "cw/lib/pointer_capture.hpp"
#include <SDL3/SDL.h>
#include <cmath>

namespace cw::lib {
struct SdlGameTouchInput {
    enum Kind { NotTouch, Ignore, Down, Move, Up, Cancel } kind{NotTouch};
    int x{}, y{};
    std::uint64_t timestamp{}, finger{};
};

// SDL のイベント処理はメインスレッドだけで行う。Engine の二つの入力取
// 得アダプターは同じ入力列を使う。片方が DOWN、他方が UP を読む場合が
// あるため、バックエンド別に捕捉状態を持たず、取り消しで UP を合成しない。
inline SdlGameTouchInput translate_game_touch(const SDL_Event& event) {
    static thread_local WindowPointerCapture capture;
    SdlGameTouchInput out;
    out.timestamp = event.common.timestamp;
    switch (event.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        if (capture.cancel()) out.kind = SdlGameTouchInput::Cancel;
        return out;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (capture.cancel_window(event.window.windowID)) out.kind = SdlGameTouchInput::Cancel;
        return out;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        break;
    default:
        return out;
    }
    out.kind = SdlGameTouchInput::Ignore;
    const auto& finger = event.tfinger;
    // 共通ダイアログと同様に、物理マウス・ペンは独立した経路を使う。
    if (finger.touchID == SDL_MOUSE_TOUCHID || finger.touchID == SDL_PEN_TOUCHID) return out;
    if (event.type == SDL_EVENT_FINGER_DOWN) {
        if (!capture.begin(finger.windowID, finger.touchID, finger.fingerID)) return out;
        out.kind = SdlGameTouchInput::Down;
    } else if (event.type == SDL_EVENT_FINGER_MOTION) {
        if (!capture.owns(finger.windowID, finger.touchID, finger.fingerID)) return out;
        out.kind = SdlGameTouchInput::Move;
    } else {
        if (!capture.end(finger.windowID, finger.touchID, finger.fingerID)) return out;
        out.kind = event.type == SDL_EVENT_FINGER_UP ? SdlGameTouchInput::Up : SdlGameTouchInput::Cancel;
    }
    out.finger = finger.fingerID;
    if (out.kind != SdlGameTouchInput::Cancel) {
        int width = 1, height = 1;
        if (auto* window = SDL_GetWindowFromID(finger.windowID)) SDL_GetWindowSize(window, &width, &height);
        out.x = static_cast<int>(std::lround(finger.x * width));
        out.y = static_cast<int>(std::lround(finger.y * height));
    }
    return out;
}
}
