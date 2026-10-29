#include "sdl_pen_input.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
void SdlPenNormalizer::retire(SDL_WindowID surface) noexcept {
    std::erase_if(samples_, [surface](const auto& entry) { return entry.second.surface == surface; });
}
std::optional<SdlPenInput> SdlPenNormalizer::decode(const SDL_Event& event) {
    if (event.type == SDL_EVENT_PEN_PROXIMITY_IN) {
        if (event.pproximity.which)
            samples_.insert_or_assign(event.pproximity.which, Sample{event.pproximity.windowID});
        return {};
    }
    if (event.type == SDL_EVENT_PEN_PROXIMITY_OUT) {
        const auto found = samples_.find(event.pproximity.which);
        if (found == samples_.end()) return {};
        const auto sample = found->second;
        samples_.erase(found);
        if (!sample.positioned || !sample.surface) return {};
        PointerEvent pointer;
        pointer.pointer_type = PointerType::Pen; pointer.pointer_id = event.pproximity.which;
        pointer.action = sample.tip_pressed || sample.side_button != PointerButton::None
            ? PointerAction::Cancel : PointerAction::Leave;
        pointer.button = sample.tip_pressed ? PointerButton::Primary : sample.side_button;
        pointer.position = pointer.screen_position = sample.position;
        if (event.common.timestamp) pointer.timestamp_ns=event.common.timestamp;
        return SdlPenInput{sample.surface, pointer, false};
    }
    SDL_PenID id{};
    SDL_WindowID surface{};
    SDL_PenInputFlags state{};
    Point position;
    PointerAction action = PointerAction::Move;
    PointerButton button = PointerButton::None;
    std::optional<float> pressure;
    switch (event.type) {
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
        id = event.ptouch.which; surface = event.ptouch.windowID; state = event.ptouch.pen_state;
        position = {event.ptouch.x, event.ptouch.y};
        action = event.type == SDL_EVENT_PEN_DOWN ? PointerAction::Down : PointerAction::Up;
        button = PointerButton::Primary;
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
        id = event.pbutton.which; surface = event.pbutton.windowID; state = event.pbutton.pen_state;
        position = {event.pbutton.x, event.pbutton.y};
        action = event.type == SDL_EVENT_PEN_BUTTON_DOWN ? PointerAction::Down : PointerAction::Up;
        switch (event.pbutton.button) {
        case 1: button = PointerButton::Secondary; break;
        case 2: button = PointerButton::Middle; break;
        case 3: button = PointerButton::Back; break;
        case 4: button = PointerButton::Forward; break;
        default: return {};
        }
        break;
    case SDL_EVENT_PEN_MOTION:
        id = event.pmotion.which; surface = event.pmotion.windowID; state = event.pmotion.pen_state;
        position = {event.pmotion.x, event.pmotion.y};
        break;
    case SDL_EVENT_PEN_AXIS:
        if (event.paxis.axis != SDL_PEN_AXIS_PRESSURE || !std::isfinite(event.paxis.value)) return {};
        id = event.paxis.which; surface = event.paxis.windowID; state = event.paxis.pen_state;
        position = {event.paxis.x, event.paxis.y};
        pressure = std::clamp(event.paxis.value, 0.0F, 1.0F);
        break;
    default: return {};
    }
    if (!id || !surface || !std::isfinite(position.x) || !std::isfinite(position.y)) return {};
    // フォーカス喪失・ネイティブの破棄で接触状態を無効化する。 遅れた解放を、
    // 新しいウィンドウや下のUIへの、押下と対にならないUpとして扱わない。
    const auto previous = samples_.find(id);
    if (event.type == SDL_EVENT_PEN_UP &&
        (previous == samples_.end() || !previous->second.tip_pressed)) return {};
    if (event.type == SDL_EVENT_PEN_BUTTON_UP &&
        (previous == samples_.end() || previous->second.side_button != button)) return {};
    auto& sample = samples_[id];
    sample.surface = surface; sample.position = position; sample.positioned = true;
    sample.tip_down = (state & SDL_PEN_INPUT_DOWN) != 0;
    if (event.type == SDL_EVENT_PEN_DOWN) sample.tip_down = sample.tip_pressed = true;
    if (event.type == SDL_EVENT_PEN_UP) sample.tip_down = sample.tip_pressed = false;
    if (pressure) { sample.pressure = *pressure; sample.pressure_known = true; }
    if (event.type == SDL_EVENT_PEN_BUTTON_DOWN) sample.side_button = button;
    if (event.type == SDL_EVENT_PEN_BUTTON_UP && sample.side_button == button)
        sample.side_button = PointerButton::None;
    PointerEvent pointer;
    pointer.pointer_type = PointerType::Pen; pointer.pointer_id = id;
    pointer.position = pointer.screen_position = position;
    pointer.action = action; pointer.button = button;
    if (event.common.timestamp) pointer.timestamp_ns=event.common.timestamp;
    pointer.pressure = sample.tip_down ? (sample.pressure_known ? sample.pressure : 1.0F) : 0.0F;
    return SdlPenInput{surface, pointer, sample.tip_down};
}
} // namespace cwxeditor::lib
