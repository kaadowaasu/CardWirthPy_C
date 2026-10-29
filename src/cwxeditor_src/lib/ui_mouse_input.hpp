#pragma once

#include "ui_activation.hpp"
#include "ui_drag_drop.hpp"
#include "ui_edit_input.hpp"
#include "ui_pointer_feedback.hpp"
#include "ui_pointer_timing.hpp"
#include "ui_number_repeat.hpp"

namespace cwxeditor::lib {

struct MouseInputContext {
    ControlActivationContext activation;
    EditInputContext editing;
    std::shared_ptr<DragDropStateMachine> drag_drop;
    float tab_height{}, expand_height{}, drag_threshold{};
    // ホストが提供する、入力元の描画面の識別情報とモーダル上の利用可否。
    std::function<bool()> live;
    PointerClickTiming click_timing;
};

struct MouseInputResult {
    bool handled{}, redraw{};
    WidgetPtr activated;
};

// 配置済みの表示座標で行うマウス・ペンの選択とジェスチャー。ネイティブ
// イベントの解読、カーソルの実表示、IMEの受け渡しはホストで扱う。
class MouseInputStateMachine {
public:
    // clicks==0なら共通の分類処理を使う。0以外は、すでに
    // イベントを分類した呼び出し側が明示する意味上のクリック回数。
    MouseInputResult dispatch(MouseInputContext context,
        std::shared_ptr<Window> scope, PointerEvent event,
        bool primary_down = false, unsigned clicks = 1);
    void capture(const std::shared_ptr<Window>& scope, const WidgetPtr& widget, bool enabled);
    MouseInputResult advance(MouseInputContext context, const std::shared_ptr<Window>& scope);
    [[nodiscard]] bool has_timed_input() const noexcept {
        return active_ && (active_->number_repeat.active() || active_->hold_pending);
    }
    [[nodiscard]] std::shared_ptr<Window> owner() const;
    [[nodiscard]] bool accepts_pointer(const PointerEvent& event) const;
    [[nodiscard]] std::optional<PointerPressFeedback> press_feedback(const std::shared_ptr<Window>& scope) const;
    void cancel_for(const WidgetPtr& scope = {});
    // コールバックを呼ばずに、既存の本体のジェスチャーとキャプチャの状態を取得す
    // る。この同期的な完了処理は、ほかのデバイスの終了処理と組み合わせられる。
    [[nodiscard]] Action prepare_cancel_for(const WidgetPtr& scope = {},
        PointerCancelReason reason = PointerCancelReason::Explicit);
    // プロバイダーを呼ばずに状態を取得する。同期的な完了処理はこの接触
    // ・キャプチャだけを終了し、途中のコールバックで作られたものは終了
    // しない。状態機械は返された完了処理より長く生存する必要がある。
    [[nodiscard]] Action prepare_pointer_cancel(const std::shared_ptr<Window>& scope,
                                                const PointerEvent& event);
    void discard_stale();

private:
    struct Contact {
        std::weak_ptr<Window> scope;
        WidgetPtr hit, text_owner;
        std::shared_ptr<TextEdit> text;
        std::shared_ptr<Slider> slider;
        std::shared_ptr<DragDropStateMachine> drag;
        std::function<bool()> live;
        PointerEvent last;
        PointerClickTiming::Clock::time_point pressed_at;
        bool hold_pending{};
        Point start;
        NumberStepRepeat number_repeat;
        std::uint64_t combo_revision{}, menu_revision{};
        std::uint64_t drag_id{}, focus_revision{};
        bool suppressed{}, candidate{}, deferred_activation{}, deferred_double_activation{};
    };
    struct Capture { std::weak_ptr<Window> scope; WidgetPtr widget; };
    static bool live(const Contact& contact);
    static bool owns_drag(const Contact& contact);
    void cancel(std::shared_ptr<Contact> contact,
                const std::shared_ptr<Capture>* expected_capture = nullptr,
                bool cancel_clicks = true,
                PointerCancelReason reason = PointerCancelReason::Explicit);
    std::shared_ptr<Contact> active_;
    std::shared_ptr<Capture> capture_;
    struct ReleaseKey {
        PointerType type{};
        std::uint64_t id{};
        PointerButton button{};
        bool operator==(const ReleaseKey&) const = default;
    };
    std::optional<ReleaseKey> popup_release_;
    PointerClickSequence clicks_;
};

} // namespace cwxeditor::lib
