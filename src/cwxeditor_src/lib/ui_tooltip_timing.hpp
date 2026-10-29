#pragma once
#include "components.hpp"
#include <cstdint>
#include <optional>

namespace cwxeditor::lib {
// 単調増加のミリ秒時刻と論理UI単位を使う。ネイティブの設定
// から異なる時間を渡しても、別のツールチップ実装は作らない。
struct TooltipTiming {
    std::uint64_t initial_delay_ms{500}, reshow_delay_ms{100}, visible_duration_ms{5000};
    float movement_tolerance{4};
    // ホストは一つの項目に即時表示を選べる。その項目
    // を含むウィジェット全体の時間規則は変更しない。
    std::optional<bool> immediate_override;
    bool operator==(const TooltipTiming&) const = default;
};
struct TooltipTimingResult {
    bool visible{};
    Point anchor;
    std::optional<std::uint64_t> next_update;
};
class TooltipStateMachine {
public:
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // コピー上で準備し、完成フレームと一緒にだけ公開する。ネイティブ入力や
    // 再入コールバックは現在のリビジョンを変更し、古い処理結果を拒否する。
    bool publish(std::uint64_t expected_revision, TooltipStateMachine prepared);
    [[nodiscard]] TooltipTimingResult update(const WidgetPtr& scope,const WidgetPtr& target,
        std::optional<std::size_t> item,std::string text,Point pointer,bool balloon,
        std::uint64_t now,TooltipTiming timing={});
    // 閉じた後、動かない同じポインター位置では再表示しない。移動、新しい項目や文字
    // 列、または一度外れて再び入る操作だけが、新しいホバーセッションを開始できる。
    bool dismiss_for(const WidgetPtr& scope) noexcept;
    void cancel_for(const WidgetPtr& scope) noexcept;
    void cancel() noexcept;
private:
    enum class Phase { Idle, Waiting, Visible, Suppressed };
    std::weak_ptr<Widget> scope_,target_;
    std::optional<std::size_t> item_;
    std::string text_;
    Point anchor_{};
    bool balloon_{},immediate_{};
    std::uint64_t last_time_{};
    TooltipTiming timing_;
    std::optional<std::uint64_t> deadline_;
    Phase phase_{Phase::Idle};
    std::uint64_t revision_{};
};
} // namespace cwxeditor::lib
