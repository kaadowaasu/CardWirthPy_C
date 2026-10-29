#include "ui_number_repeat.hpp"
#include <cmath>

namespace cwxeditor::lib {
void NumberStepRepeat::cancel() {
    ++revision_;
    target_.reset(); direction_ = 0; inside_ = false;
}
bool NumberStepRepeat::arm(const std::shared_ptr<NumberEdit>& target, Point point, Clock::time_point now) {
    cancel();
    if (!target || !target->enabled() || target->read_only ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    direction_ = layout_number_edit(*target).step_at(point);
    if (!direction_) return false;
    target_ = target; point_ = point; inside_ = true;
    last_ = now; next_ = now + delay_;
    return true;
}
void NumberStepRepeat::move(Point point, Clock::time_point now) {
    const auto target = target_.lock();
    if (!target) return;
    if (now < last_) next_ = now + delay_;
    last_ = now; point_ = point;
    const bool inside = std::isfinite(point.x) && std::isfinite(point.y) &&
        layout_number_edit(*target).step_at(point) == direction_;
    if (inside && !inside_) next_ = now + delay_;
    inside_ = inside;
}
bool NumberStepRepeat::advance(NumberEditSession& session, Clock::time_point now,
    const std::function<bool()>& live) {
    const auto revision = revision_;
    const auto target = target_.lock();
    const bool alive = target && (!live || live());
    if (revision != revision_) return false; // コールバックが別の長押しを開始した。
    if (!alive || !target->enabled() || target->read_only || !session.active_for(target)) {
        cancel(); return false;
    }
    move(point_, now); // ポインター移動イベントがなくても、領域は変わり得る。
    if (!inside_ || now < next_) return false;
    next_ = now + interval_; // モデルのコールバック・再入より先に、実行予定時刻を反映する。
    const bool changed = session.step(target, direction_);
    if (revision == revision_ && (!target->enabled() || target->read_only || !session.active_for(target))) cancel();
    return changed;
}
} // namespace cwxeditor::lib
