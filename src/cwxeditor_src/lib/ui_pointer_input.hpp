#pragma once
#include "ui_mouse_input.hpp"
#include "ui_touch_input.hpp"
#include "ui_chrome_input.hpp"
#include "ui_passive_input.hpp"
#include "window_host.hpp"

namespace cwxeditor::lib {
[[nodiscard]] std::vector<PointerPressFeedback> prepare_pointer_feedback(
    const std::shared_ptr<Window>& scope,const MouseInputStateMachine& mouse,const TouchInputStateMachine& touch);
// ホストの座標は配置済みUI座標に変換済み。組み込み
// ・単体版の違いは、この描画面の有無で表し、入力ディ
// スパッチャー内でOSやALLによる分岐を設けない。
struct PointerCaptionSurface {
    Rect* bounds{};
    Rect host;
    bool visible{}, movable{};
    bool resizable{true};
};
struct PointerInputContext {
    MouseInputStateMachine& mouse;
    TouchInputStateMachine& touch;
    ChromeInputStateMachine& chrome;
    HostedCaptionStateMachine& caption;
    ControlActivationContext activation;
    EditInputContext editing;
    PassiveInputContext passive;
    std::shared_ptr<DragDropStateMachine> drag_drop;
    float drag_threshold{}, touch_threshold{};
    std::function<bool()> live;
    std::optional<PointerCaptionSurface> surface;
    PointerClickTiming click_timing;
    std::chrono::milliseconds long_press_delay{500};
    bool long_press_is_context_click{};
};
struct PointerInputResult {
    bool handled{}, redraw{}, close_requested{}, refresh_focus{};
    std::optional<CursorKind> cursor;
    WidgetPtr activated;
    std::optional<Rect> window_bounds;
    bool minimize_requested{};
    // ALLのタイトル部の元に戻す・やり直しボタン。Ctrl+Z/Ctrl+Yと同じ動作。
    bool undo_requested{}, redo_requested{};
};
// フレームのキャプチャ、組み込みタイトル部、ポップアップ・ホバー、通常のマウス・
// タッチ操作の優先関係を管理する。ネイティブアダプターはイベントを解読し、結果
// のカーソル、再描画、終了、ネイティブフォーカス・IMEの要求を反映するだけ。
[[nodiscard]] PointerInputResult dispatch_pointer_input(PointerInputContext context,
    std::shared_ptr<Window> scope, PointerEvent event,
    bool primary_down = false, unsigned clicks = 1);
[[nodiscard]] PointerInputResult advance_pointer_input(PointerInputContext context,
    const std::shared_ptr<Window>& scope);
[[nodiscard]] bool pointer_timers_pending(const MouseInputStateMachine& mouse,
    const TouchInputStateMachine& touch) noexcept;
} // namespace cwxeditor::lib
