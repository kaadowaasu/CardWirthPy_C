#include "ui_pointer_timing.hpp"
#include "ui_actions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cwxeditor::lib {
PointerClickTiming::Clock::time_point PointerClickTiming::event_time(const PointerEvent& event) const {
    if (event.timestamp_ns && *event.timestamp_ns <=
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return Clock::time_point{std::chrono::duration_cast<Clock::duration>(
            std::chrono::nanoseconds{static_cast<std::int64_t>(*event.timestamp_ns)})};
    return now();
}
void PointerClickSequence::cancel_for(const WidgetPtr& scope) {
    if (!scope || scope_.lock() == scope) {
        ++revision_;
        pending_ = false; scope_.reset(); target_.reset(); item_.reset();
    }
}
Action PointerClickSequence::prepare_cancel_for(const WidgetPtr& scope) {
    return [this, scope, revision=revision_] {
        if (revision_ == revision) cancel_for(scope);
    };
}
unsigned PointerClickSequence::record(const WidgetPtr& scope, const WidgetPtr& target,
    const PointerEvent& event, PointerClickTiming::Clock::time_point now,
    const PointerClickTiming& timing) {
    if (!widget_in_input_scope(scope, target) || event.button != PointerButton::Primary ||
        !std::isfinite(event.position.x) || !std::isfinite(event.position.y)) {
        cancel_for(); return 1;
    }
    std::optional<std::uint64_t> item;
    if (const auto row = hit_test_item_view(target,event.position)) {
        if (const auto tree = std::dynamic_pointer_cast<TreeView>(target)) item=tree->nodes[*row].id;
        else item=*row;
    }
    const float slop=event.pointer_type==PointerType::Touch
        ? std::max(timing.distance,timing.touch_distance) : timing.distance;
    const float dx=event.position.x-previous_.position.x;
    const float dy=event.position.y-previous_.position.y;
    const bool same_device=event.pointer_type==previous_.pointer_type &&
        (event.pointer_type==PointerType::Touch || event.pointer_id==previous_.pointer_id);
    const bool repeated=pending_ && scope_.lock()==scope && target_.lock()==target && item_==item &&
        event.timestamp_ns.has_value()==previous_.timestamp_ns.has_value() &&
        same_device && event.button==previous_.button && event.modifiers==previous_.modifiers &&
        now>=time_ && timing.interval.count()>0 && now-time_<=timing.interval &&
        std::isfinite(slop) && slop>=0 &&
        std::hypot(dx,dy)<=slop;
    if (repeated) {cancel_for(); return 2;} // 三回目の有効化は、新しい組の開始とする。
    ++revision_;
    scope_=scope; target_=target; previous_=event; time_=now; item_=item; pending_=true;
    return 1;
}
} // namespace cwxeditor::lib
