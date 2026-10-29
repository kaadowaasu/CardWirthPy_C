#include "ui_tooltip_timing.hpp"
#include "ui_actions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cwxeditor::lib {
namespace {
std::optional<std::uint64_t> after(std::uint64_t now,std::uint64_t delay) {
    if (delay>std::numeric_limits<std::uint64_t>::max()-now) return {};
    return now+delay;
}
}
TooltipTimingResult TooltipStateMachine::update(const WidgetPtr& scope,const WidgetPtr& target,
    std::optional<std::size_t> item,std::string text,Point pointer,bool balloon,
    std::uint64_t now,TooltipTiming timing) {
    ++revision_;
    if (!scope || !scope->visible() || !widget_in_input_scope(scope,target) || text.empty() ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        // nullの範囲は、単なる別のホバー範囲ではなく、ネイティ
        // ブ画面の終了などの明示的な入力終了の境界。 古いタイマー
        // が、予定時刻まで無効なツールチップを残さないようにする。
        if (scope) cancel_for(scope); else cancel();
        return {};
    }
    timing.movement_tolerance=std::isfinite(timing.movement_tolerance)
        ? std::max(0.0F,timing.movement_tolerance) : 0.0F;
    const bool same=phase_!=Phase::Idle && scope_.lock()==scope && target_.lock()==target;
    const bool moved=same && (std::abs(pointer.x-anchor_.x)>timing.movement_tolerance ||
                             std::abs(pointer.y-anchor_.y)>timing.movement_tolerance);
    const bool immediate=timing.immediate_override.value_or(target->tooltip_immediate());
    const bool changed=!same || item_!=item || text_!=text || balloon_!=balloon ||
        immediate_!=immediate || !(timing_==timing) || moved || now<last_time_;
    if (changed) {
        // すでに見えているツールバー内の隣の項目は、案内を速やかに表示できる
        // 。 通常のポインター移動では、最初のホバー待ち時間から始め直す。
        const bool reshow=same && phase_==Phase::Visible && item_!=item && now>=last_time_;
        scope_=scope; target_=target; item_=item; text_=std::move(text);
        anchor_=pointer; balloon_=balloon; immediate_=immediate; timing_=timing;
        phase_=Phase::Waiting;
        deadline_=after(now,immediate ? 0 : reshow ? timing.reshow_delay_ms : timing.initial_delay_ms);
    }
    last_time_=now;
    if (phase_==Phase::Waiting && deadline_ && now>=*deadline_) {
        phase_=Phase::Visible;
        // D版の明示的な警告と同様、setVisible(true)/setAutoHide(false) を使う。
        deadline_=!immediate && timing.visible_duration_ms ? after(now,timing.visible_duration_ms) : std::nullopt;
    } else if (phase_==Phase::Visible && deadline_ && now>=*deadline_) {
        phase_=Phase::Suppressed; deadline_.reset();
    }
    return {phase_==Phase::Visible,anchor_,deadline_};
}
bool TooltipStateMachine::dismiss_for(const WidgetPtr& scope) noexcept {
    if (phase_==Phase::Idle || scope_.lock()!=scope) return false;
    ++revision_;
    const bool visible=phase_==Phase::Visible;
    phase_=Phase::Suppressed; deadline_.reset(); return visible;
}
void TooltipStateMachine::cancel_for(const WidgetPtr& scope) noexcept {
    if (scope_.lock()==scope) cancel();
}
void TooltipStateMachine::cancel() noexcept {
    ++revision_;
    scope_.reset(); target_.reset(); item_.reset(); text_.clear(); deadline_.reset(); phase_=Phase::Idle;
}
bool TooltipStateMachine::publish(std::uint64_t expected_revision,TooltipStateMachine prepared) {
    if (revision_!=expected_revision) return false;
    *this=std::move(prepared); ++revision_;
    return true;
}
} // namespace cwxeditor::lib
