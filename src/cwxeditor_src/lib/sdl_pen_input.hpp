#pragma once
#include "events.hpp"
#include <SDL3/SDL_events.h>
#include <optional>
#include <unordered_map>

namespace cwxeditor::lib {
struct SdlPenInput {
    SDL_WindowID surface{};
    PointerEvent pointer;
    bool primary_down{};
};
// SDLが別々に通知する軸・接近・ボタンの状態をまとめる。ヒット判定、フォー
// カス、選択、ジェスチャーのしきい値、ウィジェットの操作はここでは扱わない。
class SdlPenNormalizer {
public:
    [[nodiscard]] std::optional<SdlPenInput> decode(const SDL_Event& event);
    void retire(SDL_WindowID surface) noexcept;
private:
    struct Sample {
        SDL_WindowID surface{};
        Point position{};
        float pressure{};
        PointerButton side_button{PointerButton::None};
        bool positioned{}, pressure_known{}, tip_down{}, tip_pressed{};
    };
    std::unordered_map<SDL_PenID, Sample> samples_;
};
} // namespace cwxeditor::lib
