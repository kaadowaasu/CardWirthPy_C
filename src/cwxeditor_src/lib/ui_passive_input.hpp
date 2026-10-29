#pragma once

#include "ui_inline_edit.hpp"
#include "ui_scroll.hpp"

#include <cstdint>
#include <functional>

namespace cwxeditor::lib {

struct PassiveInputContext {
    HoverStateMachine& hover;
    MenuPopupStateMachine& menu;
    ComboPopupStateMachine& combo;
    InlineEditStateMachine& inline_edit;
    UiScrollStateMachine& scroll;
    float row_height{}, tab_height{}, expand_height{}, resize_margin{};
    ScrollTextProvider text;
    // ホストは入力元の描画面の識別情報と、モーダル上の利用可否だけを提供する。
    std::function<bool()> live;
};

struct PassiveInputResult {
    bool consumed{}, redraw{};
    CursorKind cursor{CursorKind::Default};
    WidgetPtr changed_control;
};

// ネイティブSWTのMouseTrack動作に対応する、表示全体の共通ポインター
// フック。ホバーの境界通知は実際に対象が変わった場合だけ行い、MouseUpとネ
// イティブウィンドウのLeaveは共通ポインターディスパッチャーから通知する。
using WindowPointerObserver = std::function<void(
    const WidgetPtr& scope, const PointerEvent& event, const WidgetPtr& hit)>;
[[nodiscard]] std::uint64_t add_window_pointer_observer(WindowPointerObserver observer);
void remove_window_pointer_observer(std::uint64_t id) noexcept;
// ネイティブイベントの開始時に、登録済み監視先の上限を取得する。遅延し
// たMouseUp通知はこの上限を使い、同じ解放イベントのローカルな起
// 動コールバックが登録した監視先に、その起動元のUpを処理させない。
[[nodiscard]] std::uint64_t window_pointer_observer_limit() noexcept;
void notify_window_pointer_observers(const WidgetPtr& scope,
    const PointerEvent& event, const WidgetPtr& hit);
void notify_window_pointer_observers(const WidgetPtr& scope,
    const PointerEvent& event, const WidgetPtr& hit, std::uint64_t registration_limit);

// ポップアップの優先順位、ホバー遷移、カーソルの指定、既定のホイー
// ル配送は共通表示の規則。ホストは単位変換と出力の反映を担当する。
[[nodiscard]] PassiveInputResult dispatch_hover_input(PassiveInputContext context,
    const WidgetPtr& scope, PointerEvent event);
[[nodiscard]] PassiveInputResult dispatch_wheel_input(PassiveInputContext context,
    const WidgetPtr& scope, WheelEvent event);

} // namespace cwxeditor::lib
