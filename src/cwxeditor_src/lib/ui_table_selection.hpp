#pragma once

#include "cwxeditor_src/lib/components.hpp"

#include <algorithm>
#include <optional>

namespace cwxeditor::lib {

// DWTのWindows版Table.getSelectionIndexは、
// フォーカスを持つ選択行を優先する。なければモデル順で最初の選択行を使う。
[[nodiscard]] inline std::optional<std::size_t> table_selection_index(
    const TableView& table) noexcept {
    const auto is_selected = [&table](const std::size_t row) {
        return row < table.rows.size() &&
               std::find(table.selection.begin(), table.selection.end(), row) !=
                   table.selection.end();
    };
    if (table.selection_cursor && is_selected(*table.selection_cursor))
        return table.selection_cursor;
    std::optional<std::size_t> first;
    for (const auto row : table.selection) {
        if (row >= table.rows.size()) continue;
        if (!first || row < *first) first = row;
    }
    return first;
}

}  // namespace cwxeditor::lib
