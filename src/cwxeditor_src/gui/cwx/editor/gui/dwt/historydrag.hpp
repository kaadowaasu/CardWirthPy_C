#pragma once

#include "cwxeditor_src/lib/drag_drop.hpp"
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::historydrag {

// Dの二つの履歴一覧ドラッグ経路で共用する操作元のスナップショット。
// 最初の選択行を基点とし、ネイティブ側のカーソル規則には依存しない。
class State final {
public:
    State() : pane_id_("history-list-" + std::to_string(++next_id_)) {}
    [[nodiscard]] const std::string& paneId() const noexcept { return pane_id_; }
    bool begin(const lib::TableView& table, const std::vector<std::size_t>& selected) {
        finish();
        if (selected.empty() || selected.back() >= table.rows.size()) return false;
        selected_ = selected;
        anchor_ = selected.front();
        row_revision_ = table.row_order_revision;
        return true;
    }
    [[nodiscard]] lib::DragOrigin origin() const {
        lib::DragOrigin result;
        result.pane_id = pane_id_;
        result.cursor_index = anchor_;
        return result;
    }
    [[nodiscard]] std::optional<std::size_t> anchor(const lib::DragEvent& event,
        const lib::TableView& table, const std::vector<std::size_t>& selected) const {
        // Dは操作元のTableItemを保持する。共通表はドラッ
        // グ中に再公開され得るため、 元の添字へ入った新しい値を移
        // 動せず、古くなった行・選択のスナップショットを拒否する。
        if (!event.origin.same_pane_as(pane_id_) || !anchor_ ||
            event.origin.cursor_index != anchor_ || selected != selected_ ||
            table.row_order_revision != row_revision_) return {};
        return anchor_;
    }
    void finish() noexcept { anchor_.reset(); selected_.clear(); }
private:
    inline static std::atomic<std::uint64_t> next_id_{};
    std::string pane_id_;
    std::optional<std::size_t> anchor_;
    std::vector<std::size_t> selected_;
    std::uint64_t row_revision_{};
};

} // namespace cwxeditor::cwx::editor::gui::dwt::historydrag
