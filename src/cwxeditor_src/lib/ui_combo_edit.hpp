#pragma once

#include "ui_text_layout.hpp"

namespace cwxeditor::lib {

struct ComboBoxLayout {
    Rect text, arrow;
    [[nodiscard]] bool edits_text(const ComboBox& combo, Point point) const noexcept;
};
[[nodiscard]] ComboBoxLayout layout_combo_box(const ComboBox& combo) noexcept;
[[nodiscard]] std::string combo_display_text(const ComboBox& combo);

// 編集可能なコンボは、単一行の文字編集と共通の選択ポップアップから構成する。表
// 示側はこの投影を描画し、フォントとクリップボードの受け渡しだけを提供する。
class ComboEditSession {
public:
    void begin(const std::shared_ptr<ComboBox>& combo);
    void finish() noexcept;
    void synchronize();
    void publish_selection();
    [[nodiscard]] bool active_for(const WidgetPtr& widget) const { return target_.lock() == widget; }
    [[nodiscard]] WidgetPtr owner() const { return target_.lock(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    bool insert(std::string_view text);
    bool key(Key key, Modifiers modifiers, ComboPopupStateMachine& popup,
             std::size_t visible_rows, float line_height,
             const TextWidthMeasure& measure, const TextClipboardAccess& clipboard = {},
             const std::function<bool()>& live = {});
    [[nodiscard]] const std::shared_ptr<TextEdit>& editor() const { return editor_; }
    [[nodiscard]] TextEditInteractionState& state() { return state_; }
private:
    void reset_text();
    void publish();
    std::weak_ptr<ComboBox> target_;
    std::shared_ptr<TextEdit> editor_{std::make_shared<TextEdit>()};
    TextEditInteractionState state_;
    std::string observed_text_;
    std::optional<std::size_t> observed_selection_;
    std::optional<std::pair<std::size_t, std::size_t>> observed_edit_selection_;
    bool observed_edit_selection_reversed_{};
    std::uint64_t generation_{};
};

} // namespace cwxeditor::lib
