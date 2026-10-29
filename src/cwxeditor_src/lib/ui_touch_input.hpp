#pragma once

#include "ui_activation.hpp"
#include "ui_drag_drop.hpp"
#include "ui_scroll.hpp"
#include "ui_pointer_feedback.hpp"
#include "ui_pointer_timing.hpp"
#include "ui_number_repeat.hpp"
#include <unordered_map>

namespace cwxeditor::lib {

struct TouchInputContext {
    ControlActivationContext activation;
    std::shared_ptr<DragDropStateMachine> drag_drop;
    ScrollTextProvider text;
    float tab_height{}, expand_height{}, threshold{};
    // 元の表示面とモーダル範囲の寿命を確認する。同じ
    // モデルの代替描画面を有効と判定してはならない。
    std::function<bool()> live;
    PointerClickTiming click_timing;
    std::chrono::milliseconds long_press_delay{500};
    // スマートフォンやタブレットのALLには、ネイティブの副ボタン操作による代替経路
    // がない。そのためドラッグ元でも、長押しを直ちにコンテキストクリックへ変換する。
    bool long_press_is_context_click{};
};

struct TouchInputResult {
    bool handled{}, redraw{};
    WidgetPtr activated;
};

// 配置済み座標の意味上のタッチイベントを受け取る。このクラスではOSハ
// ンドル、合成マウスイベント、ネイティブフォーカスの判断を扱わない。
class TouchInputStateMachine {
public:
    TouchInputResult dispatch(TouchInputContext context,
                              std::shared_ptr<Window> scope, PointerEvent event);
    // タイマーの起動は時刻だけを提供する。所有関係、利用可否、操作は共通処理で
    // 扱う。呼び出し側は時刻を進める前に、待機中の入力を処理する必要がある。
    TouchInputResult advance(TouchInputContext context, const std::shared_ptr<Window>& scope);
    [[nodiscard]] bool has_contacts() const noexcept { return !contacts_.empty(); }
    [[nodiscard]] std::shared_ptr<Window> owner(std::uint64_t pointer) const;
    [[nodiscard]] std::vector<PointerPressFeedback> press_feedback(const std::shared_ptr<Window>& scope) const;
    void cancel_for(const WidgetPtr& scope);
    void cancel_all();
    // 一つの表示範囲が所有する接触だけを取得する。コールバッ
    // クがネイティブ世代を無効にした後でも、代替の接触を取り
    // 消さずにウィンドウの終了処理を完了できるようにする。
    [[nodiscard]] Action prepare_cancel_for(const WidgetPtr& scope,
        PointerCancelReason reason = PointerCancelReason::Explicit);
    // 別のデバイスが取消コールバックを呼ぶ前に、全接触の状態を取
    // 得する。再利用されたIDや新しい接触を後から追加しない。
    [[nodiscard]] Action prepare_cancel_all();
    // 再利用されるポインターIDだけでなく、現在の接触の同一
    // 性を使う。この状態機械が生存している間に同期的に呼ぶ。
    [[nodiscard]] Action prepare_pointer_cancel(const PointerEvent& event);

private:
    struct Contact {
        std::weak_ptr<Window> scope;
        WidgetPtr hit, scroll_target;
        std::shared_ptr<Slider> slider;
        PointerGestureStateMachine motion;
        UiScrollStateMachine scroll;
        std::function<bool()> live;
        std::shared_ptr<DragDropStateMachine> drag;
        PointerEvent last;
        NumberStepRepeat number_repeat;
        bool number_press{};
        PointerClickTiming::Clock::time_point pressed_at{};
        std::optional<std::size_t> pressed_item;
        // チェック印付近の押下はチェック専用の操作とし、行の実行操作にはしない。
        std::function<bool()> check_only;
        float remainder{};
        std::uint64_t combo_revision{}, menu_revision{}, drag_id{};
        bool combo_hit{}, menu_hit{}, handled{}, finishing{}, moved{}, multi_touch{}, cancel_pending{};
        bool long_pressed{}, hold_handled{}, drag_armed{};
    };
    bool current(std::uint64_t pointer, const std::shared_ptr<Contact>& contact) const;
    void cancel(std::uint64_t pointer, std::shared_ptr<Contact> contact, bool cancel_clicks = true,
                bool notify_neutralized = false, bool retire_snapshot = false,
                PointerCancelReason reason = PointerCancelReason::Explicit);
    // 二つ目の接触は、最初の接触の同一性を失効させずに、その排他
    // 的な操作を取り消す。その後は両方の解放を消費し、クリック・
    // パン・スライダーの再開には新しい単独の接触を必要とする。
    void neutralize_for_multi_touch(std::uint64_t pointer,
                                    const std::shared_ptr<Contact>& contact);
    [[nodiscard]] Action prepare_cancel_contacts(const WidgetPtr& scope, bool all,
        PointerCancelReason reason = PointerCancelReason::Explicit);
    static bool scope_live(const Contact& contact);
    static bool owns_drag(const Contact& contact);
    void context_click(std::uint64_t pointer, const std::shared_ptr<Contact>& contact,
                       ControlActivationContext& activation);
    std::unordered_map<std::uint64_t, std::shared_ptr<Contact>> contacts_;
    PointerClickSequence clicks_;
};

} // namespace cwxeditor::lib
