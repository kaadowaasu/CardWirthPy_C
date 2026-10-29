#pragma once
#include "events.hpp"
#include <SDL3/SDL_keycode.h>

namespace cwxeditor::lib::detail {
struct SdlModifierSnapshot {
    Modifiers modifiers{Modifiers::None};
    bool physical_control{};
};
// Commandの意味上の別名はショートカット用。Dのイベント配置を一時的に切り替える
// 処理では、Ctrl+Commandの同時押しも含め、物理的なCtrlを別に保持する。
[[nodiscard]] inline SdlModifierSnapshot sdl_modifier_snapshot(
    const SDL_Keymod value, const bool gui_as_control) noexcept {
    SdlModifierSnapshot result;
    result.physical_control = (value & SDL_KMOD_CTRL) != 0;
    if ((value & SDL_KMOD_SHIFT) != 0) result.modifiers = result.modifiers | Modifiers::Shift;
    if (result.physical_control) result.modifiers = result.modifiers | Modifiers::Control;
    if ((value & SDL_KMOD_ALT) != 0) result.modifiers = result.modifiers | Modifiers::Alt;
    if ((value & SDL_KMOD_GUI) != 0)
        result.modifiers = result.modifiers | (gui_as_control ? Modifiers::Control : Modifiers::Meta);
    if ((value & SDL_KMOD_CAPS) != 0) result.modifiers = result.modifiers | Modifiers::CapsLock;
    if ((value & SDL_KMOD_NUM) != 0) result.modifiers = result.modifiers | Modifiers::NumLock;
    return result;
}
} // namespace cwxeditor::lib::detail
