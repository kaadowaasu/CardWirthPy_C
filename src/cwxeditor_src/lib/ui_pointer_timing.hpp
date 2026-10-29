#pragma once
#include "components.hpp"
#include <chrono>

namespace cwxeditor::lib {
struct PointerClickTiming {
    using Clock = std::chrono::steady_clock;
    std::chrono::milliseconds interval{500};
    // ネイティブ画素ではなく配置後の座標。UI倍率は呼び出し側が渡す。
    float distance{5};
    // 二回の指のタップは同じ画素に重ならないため、ダブルタップにはこの大き
    // めの許容距離を使う。配置後の座標で、UI倍率は呼び出し側が渡す。小
    // さい許容距離では、二回目のタップが新しい単タップと判定されやすい。
    float touch_distance{40};
    std::function<Clock::time_point()> clock;
    [[nodiscard]] Clock::time_point now() const { return clock ? clock() : Clock::now(); }
    [[nodiscard]] Clock::time_point event_time(const PointerEvent& event) const;
};
// 受付済みの主操作を分類する。マウス・ペンは押下時、タッチは移動せ
// ずにタップが完了した時点で実行する。ポップアップ、取り消した押下
// 、スクロールはダブルクリックを発生させず、連続操作を無効にする。
class PointerClickSequence {
public:
    unsigned record(const WidgetPtr& scope, const WidgetPtr& target,
        const PointerEvent& event, PointerClickTiming::Clock::time_point now,
        const PointerClickTiming& timing);
    void cancel_for(const WidgetPtr& scope = {});
    // 遅延した取消処理で、先行する終了コールバックが記録したクリック
    // を消してはならない。この連続操作の状態が生存している間に呼ぶ。
    [[nodiscard]] Action prepare_cancel_for(const WidgetPtr& scope = {});
private:
    std::uint64_t revision_{};
    std::weak_ptr<Widget> scope_, target_;
    PointerEvent previous_;
    PointerClickTiming::Clock::time_point time_{};
    std::optional<std::uint64_t> item_;
    bool pending_{};
};
} // namespace cwxeditor::lib
