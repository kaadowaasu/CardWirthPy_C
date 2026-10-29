#pragma once

#include "ui_text_layout.hpp"

namespace cwxeditor::lib {

struct NumberEditLayout {
    Rect text, increase, decrease;
    bool horizontal_steps{};
    // ALLのテキストセルと＋/－ボタンの間にある、インラインの見出しセル。
    Rect label{};
    [[nodiscard]] int step_at(Point point) const noexcept;
};
[[nodiscard]] NumberEditLayout layout_number_edit(const NumberEdit& number) noexcept;
[[nodiscard]] std::string format_number_value(const NumberEdit& number);

// 全表示側で共有する一つの編集可能なテキスト状態。ネイティブ側はグリフ計測とク
// リップボードの受け渡しだけを提供し、このTextEditの投影を表示する。
class NumberEditSession {
public:
    void begin(const std::shared_ptr<NumberEdit>& number);
    // 表示処理はフォーカス喪失ではない。古い編集中の値は公開せずに破棄する。
    void project(const std::shared_ptr<NumberEdit>& number);
    void finish();
    void commit();
    void cancel();
    void discard(); // 編集中の値を取り消し、閉じたウィンドウから切り離す。
    void synchronize();
    bool insert(std::string_view value);
    bool key(Key key, Modifiers modifiers, float line_height,
             const TextWidthMeasure& measure, const TextClipboardAccess& clipboard = {},
             const std::function<bool()>& live = {});
    bool step(const std::shared_ptr<NumberEdit>& number, double amount);
    [[nodiscard]] bool active_for(const WidgetPtr& widget) const { return target_.lock() == widget; }
    [[nodiscard]] WidgetPtr owner() const { return target_.lock(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    [[nodiscard]] const std::shared_ptr<TextEdit>& editor() const { return editor_; }
    [[nodiscard]] TextEditInteractionState& state() { return state_; }
private:
    void reset_text();
    // この操作の投影だけを修復し、新しい編集を置き換えない。
    // 修復中のメモリ確保の失敗で、通知のエラーを隠さない。
    void repair_after_failure(std::uint64_t generation) noexcept;
    bool publish();
    std::weak_ptr<NumberEdit> target_;
    std::shared_ptr<NumberEdit::DraftState> draft_;
    std::shared_ptr<TextEdit> editor_{std::make_shared<TextEdit>()};
    TextEditInteractionState state_;
    double observed_value_{};
    unsigned observed_decimals_{};
    double baseline_value_{};
    bool preview_started_{};
    std::uint64_t generation_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

}  // namespace cwxeditor::lib
