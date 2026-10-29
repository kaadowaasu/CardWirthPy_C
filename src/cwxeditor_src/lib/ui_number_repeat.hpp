#pragma once
#include "ui_number_edit.hpp"
#include <chrono>

namespace cwxeditor::lib {
// マウス、ペン、タッチで共有する。最初の通常の増減後に反復を開
// 始し、開始設定自体は値を公開しない。UIが滞った場合も一回の
// 再開につき最大一段だけ進め、たまった編集を一度に実行しない。
class NumberStepRepeat {
public:
    using Clock = std::chrono::steady_clock;
    bool arm(const std::shared_ptr<NumberEdit>& target, Point point, Clock::time_point now);
    void move(Point point, Clock::time_point now);
    bool advance(NumberEditSession& session, Clock::time_point now, const std::function<bool()>& live);
    void cancel();
    [[nodiscard]] bool active() const noexcept { return !target_.expired(); }
private:
    std::weak_ptr<NumberEdit> target_;
    Point point_{};
    int direction_{};
    bool inside_{};
    Clock::time_point next_{}, last_{};
    std::uint64_t revision_{};
    static constexpr std::chrono::milliseconds delay_{400}, interval_{60};
};
} // namespace cwxeditor::lib
