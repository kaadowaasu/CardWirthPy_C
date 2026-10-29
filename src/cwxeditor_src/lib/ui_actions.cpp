#include "ui_actions.hpp"
#include "ui_child_order.hpp"
#include "ui_metrics.hpp"
#include "ui_layout.hpp"
#include "ui_combo_edit.hpp"
#include "ui_slider.hpp"

#include <algorithm>
#include <mutex>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <locale>
#include <numeric>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace cwxeditor::lib {
namespace {

std::vector<std::size_t> normalized_indices(std::vector<std::size_t> values,
                                            const std::size_t count) {
    values.erase(std::remove_if(values.begin(), values.end(),
        [count](const std::size_t index) { return index >= count; }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool same_choice_items(const std::vector<ChoiceItem>& left,
                       const std::vector<ChoiceItem>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.text != b.text || a.value != b.value || a.icon != b.icon ||
            a.enabled != b.enabled) return false;
    }
    return true;
}

bool same_list_items(const std::vector<ListItem>& left,
                     const std::vector<ListItem>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.text != b.text || a.value != b.value || a.icon != b.icon ||
            a.checked != b.checked || a.enabled != b.enabled ||
            a.editor != b.editor || a.editor_choices != b.editor_choices ||
            a.editor_value != b.editor_value || a.editor_choice_values != b.editor_choice_values ||
            a.editor_minimum != b.editor_minimum || a.editor_maximum != b.editor_maximum ||
            a.editor_step != b.editor_step)
            return false;
    }
    return true;
}

bool same_tab_pages(const std::vector<TabPage>& left,
                    const std::vector<TabPage>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.title != b.title || a.icon != b.icon || a.content != b.content ||
            a.closable != b.closable) return false;
    }
    return true;
}

bool same_menu_items(const std::vector<MenuItem>& left,
                     const std::vector<MenuItem>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        // 関数の呼び出し先には、意図的に等価比較を定義しない。 それ
        // 以外の公開済みメニューの状態全体は再帰的に比較し、 途中に
        // 入った有効な変更を更新・再構築で上書きしないようにする。
        if (a.text != b.text || a.command != b.command ||
            a.shortcut != b.shortcut || a.icon != b.icon ||
            a.enabled != b.enabled || a.checked != b.checked ||
            a.radio != b.radio || a.separator != b.separator ||
            static_cast<bool>(a.open_children) != static_cast<bool>(b.open_children) ||
            !same_menu_items(a.children, b.children))
            return false;
    }
    return true;
}

bool same_tool_items(const std::vector<ToolItem>& left,
                     const std::vector<ToolItem>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.text != b.text || a.command != b.command || a.icon != b.icon ||
            a.enabled != b.enabled || a.checked != b.checked ||
            a.separator != b.separator || a.drop_down != b.drop_down ||
            a.drop_down_menu != b.drop_down_menu || a.tooltip != b.tooltip)
            return false;
    }
    return true;
}

} // namespace

struct PreparedItemModelUpdate::State {
    std::function<bool()> valid;
    std::function<void()> apply;
    bool committed{};
};

bool PreparedItemModelUpdate::current() const {
    return state_ && !state_->committed && state_->valid && state_->valid();
}

bool PreparedItemModelUpdate::commit() {
    if (!current() || !state_->apply) return false;
    state_->apply();
    state_->committed = true;
    return true;
}

PreparedItemModelUpdate prepare_combo_items(const std::shared_ptr<ComboBox>& combo,
                         std::vector<ChoiceItem> items,
                         std::optional<std::size_t> selected,
                         std::optional<std::string> edit_text,
                         std::function<bool()> current) {
    if (!combo) return {};
    const auto revision = ++combo->item_update_revision;
    const auto before_items = combo->items;
    const auto before_selected = combo->selected_index;
    const auto before_text = combo->edit_text;
    const auto before_order = combo->item_order_revision;
    if (selected && *selected >= items.size()) selected.reset();
    PreparedItemModelUpdate update;
    update.state_ = std::make_shared<PreparedItemModelUpdate::State>();
    update.state_->valid = [combo, revision, before_items, before_selected,
                            before_text, before_order, current = std::move(current)] {
        return combo->item_update_revision == revision &&
               combo->item_order_revision == before_order &&
               combo->selected_index == before_selected && combo->edit_text == before_text &&
               same_choice_items(combo->items, before_items) &&
               (!current || current());
    };
    update.state_->apply = [combo, items = std::move(items), selected,
                            edit_text = std::move(edit_text)]() mutable noexcept {
        combo->items.swap(items);
        combo->selected_index = selected;
        if (edit_text) combo->edit_text.swap(*edit_text);
        ++combo->item_order_revision;
    };
    return update.current() ? std::move(update) : PreparedItemModelUpdate{};
}

PreparedItemModelUpdate prepare_list_items(const std::shared_ptr<ListView>& list,
                        std::vector<ListItem> items,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal,
                        std::function<bool()> current) {
    if (!list) return {};
    const auto revision = ++list->item_update_revision;
    const auto before_items = list->items;
    const auto before_selection = list->selection;
    const auto before_anchor = list->selection_anchor;
    const auto before_cursor = list->selection_cursor;
    const auto before_top = list->top_index;
    const auto before_order = list->item_order_revision;
    selection = normalized_indices(std::move(selection), items.size());
    if (!list->multi_select && selection.size() > 1U) selection.resize(1U);
    if (reveal && *reveal >= items.size()) reveal.reset();
    const auto old_top = list->top_index;
    std::size_t top = items.empty() ? 0U : std::min(old_top, items.size() - 1U);
    if (reveal) top = *reveal;
    const auto selected = [&](const std::optional<std::size_t> value) {
        return value && std::binary_search(selection.begin(), selection.end(), *value);
    };
    auto anchor = selected(list->selection_anchor) ? list->selection_anchor :
        (selection.empty() ? std::optional<std::size_t>{} : selection.front());
    auto cursor = selected(list->selection_cursor) ? list->selection_cursor :
        (selection.empty() ? std::optional<std::size_t>{} : selection.back());
    PreparedItemModelUpdate update;
    update.state_ = std::make_shared<PreparedItemModelUpdate::State>();
    update.state_->valid = [list, revision, before_items, before_selection,
                            before_anchor, before_cursor, before_top, before_order,
                            current = std::move(current)] {
        return list->item_update_revision == revision &&
               list->item_order_revision == before_order &&
               list->selection == before_selection &&
               list->selection_anchor == before_anchor &&
               list->selection_cursor == before_cursor && list->top_index == before_top &&
               same_list_items(list->items, before_items) &&
               (!current || current());
    };
    update.state_->apply = [list, items = std::move(items),
        selection = std::move(selection), anchor, cursor, top, old_top]() mutable noexcept {
        list->items.swap(items);
        list->selection.swap(selection);
        list->selection_anchor = anchor;
        list->selection_cursor = cursor;
        list->top_index = top;
        if (top != old_top) ++list->scroll_update_revision;
        ++list->item_order_revision;
    };
    return update.current() ? std::move(update) : PreparedItemModelUpdate{};
}

PreparedItemModelUpdate prepare_tab_pages(const std::shared_ptr<TabView>& tabs,
                       std::vector<TabPage> pages,
                       std::optional<std::size_t> selected,
                       std::function<bool()> current) {
    if (!tabs) return {};
    const auto revision = ++tabs->page_update_revision;
    const auto before_pages = tabs->pages;
    const auto before_selected = tabs->selected_index;
    const auto before_order = tabs->page_order_revision;
    const auto before_header = tabs->header_viewport;
    const auto before_header_revision = tabs->header_update_revision;
    if (selected && *selected >= pages.size()) selected.reset();
    PreparedItemModelUpdate update;
    update.state_ = std::make_shared<PreparedItemModelUpdate::State>();
    update.state_->valid = [tabs, revision, before_pages, before_selected,
                            before_order, before_header, before_header_revision,
                            current = std::move(current)] {
        return tabs->page_update_revision == revision &&
               tabs->page_order_revision == before_order &&
               tabs->selected_index == before_selected &&
               tabs->header_update_revision == before_header_revision &&
               tabs->header_viewport.first == before_header.first &&
               tabs->header_viewport.revealed_selection == before_header.revealed_selection &&
               tabs->header_viewport.measured_widths == before_header.measured_widths &&
               tabs->header_viewport.available_width == before_header.available_width &&
               same_tab_pages(tabs->pages, before_pages) &&
               (!current || current());
    };
    update.state_->apply = [tabs, pages = std::move(pages), selected]() mutable noexcept {
        tabs->pages.swap(pages);
        tabs->selected_index = selected;
        tabs->header_viewport.first = selected.value_or(0U);
        tabs->header_viewport.revealed_selection = selected;
        tabs->header_viewport.measured_widths.clear();
        tabs->header_viewport.available_width = 0.0F;
        ++tabs->header_update_revision;
        ++tabs->page_order_revision;
    };
    return update.current() ? std::move(update) : PreparedItemModelUpdate{};
}

PreparedItemModelUpdate prepare_menu_items(const std::shared_ptr<Menu>& menu,
                        std::vector<MenuItem> items,
                        std::function<bool()> current) {
    if (!menu) return {};
    const auto revision = ++menu->item_update_revision;
    const auto beforeItems = menu->items;
    const auto beforeOrder = menu->item_order_revision;
    PreparedItemModelUpdate update;
    update.state_ = std::make_shared<PreparedItemModelUpdate::State>();
    update.state_->valid = [menu, revision, beforeItems, beforeOrder,
                            current = std::move(current)] {
        return menu->item_update_revision == revision &&
               menu->item_order_revision == beforeOrder &&
               same_menu_items(menu->items, beforeItems) &&
               (!current || current());
    };
    update.state_->apply = [menu, items = std::move(items)]() mutable noexcept {
        menu->items.swap(items);
        ++menu->item_order_revision;
    };
    return update.current() ? std::move(update) : PreparedItemModelUpdate{};
}

PreparedItemModelUpdate prepare_tool_bar_items(const std::shared_ptr<ToolBar>& toolbar,
                             std::vector<ToolItem> items,
                             std::function<bool()> current) {
    if (!toolbar) return {};
    const auto revision = ++toolbar->item_update_revision;
    const auto beforeItems = toolbar->items;
    const auto beforeOrder = toolbar->item_order_revision;
    PreparedItemModelUpdate update;
    update.state_ = std::make_shared<PreparedItemModelUpdate::State>();
    update.state_->valid = [toolbar, revision, beforeItems, beforeOrder,
                            current = std::move(current)] {
        return toolbar->item_update_revision == revision &&
               toolbar->item_order_revision == beforeOrder &&
               same_tool_items(toolbar->items, beforeItems) &&
               (!current || current());
    };
    update.state_->apply = [toolbar, items = std::move(items)]() mutable noexcept {
        toolbar->items.swap(items);
        ++toolbar->item_order_revision;
    };
    return update.current() ? std::move(update) : PreparedItemModelUpdate{};
}

bool replace_combo_items(const std::shared_ptr<ComboBox>& combo,
                         std::vector<ChoiceItem> items,
                         std::optional<std::size_t> selected,
                         std::optional<std::string> edit_text,
                         std::function<bool()> current) {
    return prepare_combo_items(combo, std::move(items), selected,
        std::move(edit_text), std::move(current)).commit();
}

bool replace_list_items(const std::shared_ptr<ListView>& list,
                        std::vector<ListItem> items,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal,
                        std::function<bool()> current) {
    return prepare_list_items(list, std::move(items), std::move(selection), reveal,
        std::move(current)).commit();
}

bool replace_tab_pages(const std::shared_ptr<TabView>& tabs,
                       std::vector<TabPage> pages,
                       std::optional<std::size_t> selected,
                       std::function<bool()> current) {
    return prepare_tab_pages(tabs, std::move(pages), selected,
        std::move(current)).commit();
}

bool replace_menu_items(const std::shared_ptr<Menu>& menu,
                        std::vector<MenuItem> items,
                        std::function<bool()> current) {
    return prepare_menu_items(menu, std::move(items), std::move(current)).commit();
}

bool replace_tool_bar_items(const std::shared_ptr<ToolBar>& toolbar,
                            std::vector<ToolItem> items,
                            std::function<bool()> current) {
    return prepare_tool_bar_items(toolbar, std::move(items), std::move(current)).commit();
}

bool reorder_table_rows(TableView& table, const std::vector<std::size_t>& new_to_old) {
    const auto count = table.rows.size();
    if (new_to_old.size() != count) return false;
    std::vector<std::size_t> old_to_new(count, count);
    bool reordered{};
    for (std::size_t next = 0; next < count; ++next) {
        const auto old = new_to_old[next];
        if (old >= count || old_to_new[old] != count) return false;
        old_to_new[old] = next;
        reordered = reordered || old != next;
    }
    std::vector<TableRow> rows;
    rows.reserve(count);
    for (const auto old : new_to_old) rows.push_back(table.rows[old]);
    auto selection = table.selection;
    selection.erase(std::remove_if(selection.begin(), selection.end(),
        [count](std::size_t row) { return row >= count; }), selection.end());
    for (auto& row : selection) row = old_to_new[row];
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    const auto remap = [&](std::optional<std::size_t> row) -> std::optional<std::size_t> {
        return row && *row < count ? std::optional<std::size_t>(old_to_new[*row]) : std::nullopt;
    };
    const auto anchor = remap(table.selection_anchor), cursor = remap(table.selection_cursor);
    table.rows.swap(rows);
    table.selection.swap(selection);
    table.selection_anchor = anchor;
    table.selection_cursor = cursor;
    if (reordered) ++table.row_order_revision;
    return true;
}

std::optional<std::size_t> hit_test_tool_bar_item(const ToolBar& toolbar,
                                                const Point point) {
    if (!point_inside(toolbar.bounds(), point)) return std::nullopt;
    const auto geometry = tool_bar_item_geometry(toolbar);
    for (std::size_t index = 0; index < geometry.size(); ++index)
        if (!toolbar.items[index].separator &&
            point_inside(geometry[index].bounds, point)) return index;
    return std::nullopt;
}

std::optional<std::size_t> hit_test_menu_bar_item(const Menu& menu,
                                                const Point point) {
    if (!point_inside(menu.bounds(), point)) return std::nullopt;
    const auto geometry = menu_bar_item_geometry(menu);
    for (std::size_t index = 0; index < geometry.size(); ++index)
        if (!menu.items[index].separator && point_inside(geometry[index], point))
            return index;
    return std::nullopt;
}

std::string tool_bar_tooltip(const ToolBar& toolbar, const Point point) {
    if (const auto index = hit_test_tool_bar_item(toolbar, point)) {
        const auto& item = toolbar.items[*index];
        if (!item.tooltip.empty()) return item.tooltip;
        if (!item.text.empty()) return display_text_without_mnemonic(item.text);
    }
    return toolbar.tooltip();
}

std::size_t previous_utf8_boundary(const std::string_view value,
                                   std::size_t position) noexcept {
    position = std::min(position, value.size());
    if (position == 0U) return 0U;
    --position;
    while (position > 0U &&
           (static_cast<unsigned char>(value[position]) & 0xC0U) == 0x80U)
        --position;
    return position;
}

std::size_t next_utf8_boundary(const std::string_view value,
                               std::size_t position) noexcept {
    position = std::min(position, value.size());
    if (position >= value.size()) return value.size();
    ++position;
    while (position < value.size() &&
           (static_cast<unsigned char>(value[position]) & 0xC0U) == 0x80U)
        ++position;
    return position;
}

std::size_t clamp_utf8_boundary(const std::string_view value,
                                 std::size_t position) noexcept {
    position = std::min(position, value.size());
    while (position > 0U && position < value.size() &&
           (static_cast<unsigned char>(value[position]) & 0xC0U) == 0x80U)
        --position;
    return position;
}

std::size_t utf8_codepoint_count(const std::string_view value) noexcept {
    std::size_t result{};
    for (const unsigned char byte : value)
        if ((byte & 0xC0U) != 0x80U) ++result;
    return result;
}

void normalize_text_selection(TextEdit& edit) noexcept {
    edit.selection_start = std::min(edit.selection_start, edit.text.size());
    edit.selection_length = std::min(edit.selection_length,
                                     edit.text.size() - edit.selection_start);
    if (edit.selection_length == 0U) edit.selection_reversed = false;
}

bool set_text_selection(TextEdit& edit, std::size_t start,
                        const std::size_t length) noexcept {
    const std::size_t old_start = edit.selection_start;
    const std::size_t old_length = edit.selection_length;
    const bool old_reversed = edit.selection_reversed;
    start = clamp_utf8_boundary(edit.text, std::min(start, edit.text.size()));
    const std::size_t end = clamp_utf8_boundary(
        edit.text, std::min(edit.text.size(), start + length));
    edit.selection_start = start;
    edit.selection_length = end - start;
    edit.selection_reversed = false;
    return old_start != edit.selection_start ||
           old_length != edit.selection_length ||
           old_reversed != edit.selection_reversed;
}

void synchronize_text_edit_state(TextEdit& edit,
                                 TextEditInteractionState& state) noexcept {
    normalize_text_selection(edit);
    const std::size_t start = clamp_utf8_boundary(edit.text,
                                                   edit.selection_start);
    const std::size_t end = clamp_utf8_boundary(
        edit.text, std::min(edit.text.size(), start + edit.selection_length));
    if (state.composition && (state.composition->document != edit.text ||
        state.composition->replaced != TextSelection{start,end-start} || edit.read_only || !edit.enabled()))
        state.cancel_composition();
    const bool selection_reversed = edit.selection_length != 0U &&
                                    edit.selection_reversed;
    if (!state.initialized || state.anchor > edit.text.size() ||
        state.caret > edit.text.size() ||
        std::min(state.anchor, state.caret) != start ||
        std::max(state.anchor, state.caret) != end ||
        (state.anchor != state.caret &&
         (state.anchor > state.caret) != selection_reversed)) {
        state.anchor = selection_reversed ? end : start;
        state.caret = selection_reversed ? start : end;
        state.preferred_x = -1.0F;
        state.initialized = true;
    }
}

void commit_text_edit_state(TextEdit& edit,
                            TextEditInteractionState& state) noexcept {
    state.anchor = clamp_utf8_boundary(edit.text, state.anchor);
    state.caret = clamp_utf8_boundary(edit.text, state.caret);
    edit.selection_start = std::min(state.anchor, state.caret);
    edit.selection_length = std::max(state.anchor, state.caret) -
                            edit.selection_start;
    edit.selection_reversed = edit.selection_length != 0U &&
                              state.anchor > state.caret;
    state.initialized = true;
}

bool set_text_caret(TextEdit& edit, TextEditInteractionState& state,
                    std::size_t position, const bool keep_anchor) noexcept {
    synchronize_text_edit_state(edit, state);
    position = clamp_utf8_boundary(edit.text, position);
    const std::size_t old_anchor = state.anchor;
    const std::size_t old_caret = state.caret;
    if (!keep_anchor) state.anchor = position;
    state.caret = position;
    commit_text_edit_state(edit, state);
    return old_anchor != state.anchor || old_caret != state.caret;
}

bool select_all_text(TextEdit& edit, TextEditInteractionState& state) noexcept {
    synchronize_text_edit_state(edit, state);
    const bool changed = state.anchor != 0U || state.caret != edit.text.size();
    state.anchor = 0U;
    state.caret = edit.text.size();
    state.preferred_x = -1.0F;
    commit_text_edit_state(edit, state);
    return changed;
}

namespace {
bool replace_text_selection_impl(TextEdit& edit, TextEditInteractionState* state,
                                 const std::string_view input) {
    normalize_text_selection(edit);
    if (edit.read_only) return false;
    const auto replacement=edit.filterInsertedText(input);
    if (edit.max_length != 0U) {
        std::string resulting = edit.text;
        resulting.replace(edit.selection_start, edit.selection_length, replacement);
        if (utf8_codepoint_count(resulting) > edit.max_length) return false;
    }
    edit.text.replace(edit.selection_start, edit.selection_length, replacement);
    edit.selection_start += replacement.size();
    edit.selection_length = 0U;
    edit.selection_reversed = false;
    if (state) {
        state->anchor = state->caret = edit.selection_start;
        state->preferred_x = -1.0F;
        state->initialized = true;
        commit_text_edit_state(edit, *state);
    }
    const auto notify = edit.on_changed;
    const auto published_text = edit.text;
    if (notify) notify(published_text);
    return true;
}
} // namespace
bool replace_text_selection(TextEdit& edit, const std::string_view replacement) {
    return replace_text_selection_impl(edit, nullptr, replacement);
}

bool set_text_value(TextEdit& edit, std::string value, const TextValueOrigin origin) {
    if ((origin == TextValueOrigin::UserInput && edit.read_only) || edit.text == value)
        return false;
    value=edit.filterInsertedText(value);
    if(edit.text==value)return false;
    if (edit.max_length != 0U &&
        utf8_codepoint_count(value) > edit.max_length)
        return false;
    edit.text = std::move(value);
    normalize_text_selection(edit);
    const auto notify = edit.on_changed;
    const auto published_text = edit.text;
    if (notify) notify(published_text);
    return true;
}

bool replace_text_selection(TextEdit& edit, TextEditInteractionState& state,
                            const std::string_view replacement) {
    return replace_text_selection_impl(edit, &state, replacement);
}

bool erase_text_backward(TextEdit& edit, TextEditInteractionState& state) {
    synchronize_text_edit_state(edit, state);
    if (edit.read_only) return false;
    if (edit.selection_length != 0U)
        return replace_text_selection(edit, state, {});
    if (state.caret == 0U) return false;
    const std::size_t previous = previous_utf8_boundary(edit.text, state.caret);
    edit.selection_start = previous;
    edit.selection_length = state.caret - previous;
    return replace_text_selection(edit, state, {});
}

bool erase_text_forward(TextEdit& edit, TextEditInteractionState& state) {
    synchronize_text_edit_state(edit, state);
    if (edit.read_only) return false;
    if (edit.selection_length != 0U)
        return replace_text_selection(edit, state, {});
    if (state.caret >= edit.text.size()) return false;
    edit.selection_start = state.caret;
    edit.selection_length = next_utf8_boundary(edit.text, state.caret) - state.caret;
    return replace_text_selection(edit, state, {});
}

bool move_text_caret(TextEdit& edit, TextEditInteractionState& state,
                     const TextCaretMove movement, std::size_t line_start,
                     std::size_t line_end, const bool extend) noexcept {
    synchronize_text_edit_state(edit, state);
    line_start = clamp_utf8_boundary(edit.text,
                                     std::min(line_start, edit.text.size()));
    line_end = clamp_utf8_boundary(edit.text,
                                   std::min(line_end, edit.text.size()));
    if (line_end < line_start) std::swap(line_start, line_end);

    std::size_t position = state.caret;
    switch (movement) {
    case TextCaretMove::CharacterBackward:
        position = edit.selection_length != 0U && !extend
                       ? edit.selection_start
                       : previous_utf8_boundary(edit.text, state.caret);
        break;
    case TextCaretMove::CharacterForward:
        position = edit.selection_length != 0U && !extend
                       ? edit.selection_start + edit.selection_length
                       : next_utf8_boundary(edit.text, state.caret);
        break;
    case TextCaretMove::LineStart:
        position = line_start;
        break;
    case TextCaretMove::LineEnd:
        position = line_end;
        break;
    case TextCaretMove::DocumentStart:
        position = 0U;
        break;
    case TextCaretMove::DocumentEnd:
        position = edit.text.size();
        break;
    }
    state.preferred_x = -1.0F;
    return set_text_caret(edit, state, position, extend);
}

bool InlineEditStateMachine::begin(const std::shared_ptr<TableView>& table,
                                   const std::size_t row,
                                   const std::size_t column) {
    // 完了通知を持つ編集状態を置き換えると、アプリのコードが呼ばれ
    // る場合がある。 セル・ノードの参照を保持したり新しい編集を設
    // 定したりする前に終了し、 通知の再入で開かれた編集は残す。
    if (table_ && table_->completion) {
        cancel();
        if (active()) return false;
    }
    if (!table || !table->visible() || !table->enabled() || row >= table->rows.size() ||
        column >= table->rows[row].cells.size() ||
        !table->rows[row].cells[column].enabled ||
        !table->rows[row].cells[column].inline_editable)
        return false;
    const auto row_order_revision = table->row_order_revision;
    const auto row_update_revision = table->row_update_revision;
    const auto previous_editor = editor_;
    const auto previous_choice = choice_;
    const auto previous_owner = owner();
    const bool previous_active = active();
    // 開始コールバックがこの表を再構築したり自身を置き換えたりする場合があ
    // る。 ローカルの複製を呼び、その後で受け付け条件をすべて再検証する。
    const auto on_cell_edit_begin = table->on_cell_edit_begin;
    if (on_cell_edit_begin && !on_cell_edit_begin(row, column))
        return false;
    // コールバックが、この状態管理で同期的に別の編集を始める
    // 場合がある。 その新しい編集は保ち、古い編集が変わっ
    // ていない場合だけ、今回の通常の開始要求で置き換える。
    if (editor_ != previous_editor || choice_ != previous_choice ||
        owner() != previous_owner || active() != previous_active)
        return false;
    if (!table->visible() || !table->enabled() || row >= table->rows.size() ||
        column >= table->rows[row].cells.size() ||
        !table->rows[row].cells[column].enabled ||
        !table->rows[row].cells[column].inline_editable ||
        table->row_order_revision != row_order_revision ||
        table->row_update_revision != row_update_revision)
        return false;
    auto& cell = table->rows[row].cells[column];
    TableSession session{table, row, column, {}};
    session.row_order_revision = table->row_order_revision;
    session.original_editor_value = cell.editor_value;
    session.original_choice_values = cell.editor_choice_values;
    for (const auto& entry : table->rows[row].cells)
        session.original_row.emplace_back(entry.text, entry.value);
    table_ = std::move(session);
    tree_.reset();
    generic_.reset();
    initialize(cell.editor_value.value_or(cell.text), table);
    editor_->numeric_input = cell.editor == CellEditorKind::Number;
    if (cell.editor == CellEditorKind::Choice || cell.editor == CellEditorKind::EditableChoice) {
        choice_ = std::make_shared<ComboBox>();
        choice_->editable = cell.editor == CellEditorKind::EditableChoice;
        choice_->edit_text = cell.text;
        choice_->set_font(table->font());
        for (std::size_t i = 0; i < cell.editor_choices.size(); ++i) {
            const auto& text = cell.editor_choices[i];
            const auto& value = cell.editor_choice_values.size() == cell.editor_choices.size()
                ? cell.editor_choice_values[i] : text;
            if (value == cell.editor_value.value_or(cell.text) && !choice_->selected_index)
                choice_->selected_index = choice_->items.size();
            choice_->items.push_back({text, value});
        }
        editor_->read_only = !choice_->editable;
    }
    publish_choice_selection();
    if (table->on_inline_editor_created) {
        const auto editor = editor_;
        const auto choice = choice_;
        const auto callback = table->on_inline_editor_created;
        callback(row, column, editor, choice);
        if (editor->on_inline_edit_finished) {
            auto completion = std::make_shared<InlineCompletion>();
            completion->callback = std::move(editor->on_inline_edit_finished);
            if (matches(table, row, column) && editor_ == editor)
                table_->completion = std::move(completion);
            // 生成処理が編集状態を置き換えた場合は、ローカルのガードが元へ戻す。
        }
        // メニュー設定がアプリのコールバックを呼ぶ場合がある。 再入で開
        // かれた別の編集や、再構築済みの行モデルに対して成功を返さない。
        if (!matches(table, row, column) || editor_ != editor || choice_ != choice ||
            table->row_order_revision != table_->row_order_revision) return false;
    }
    return true;
}

bool InlineEditStateMachine::begin(const std::shared_ptr<TreeView>& tree,
                                   const std::size_t node_index) {
    // 完了通知を持つ編集状態を置き換えると、アプリのコードが呼ばれ
    // る場合がある。 セル・ノードの参照を保持したり新しい編集を設
    // 定したりする前に終了し、 通知の再入で開かれた編集は残す。
    if (table_ && table_->completion) {
        cancel();
        if (active()) return false;
    }
    if (!tree || !tree->visible() || !tree->enabled() || node_index >= tree->nodes.size() ||
        !tree_inline_edit_admitted(*tree, tree->nodes[node_index]))
        return false;
    const auto visible = visible_tree_indices(*tree);
    if (std::ranges::find(visible, node_index) == visible.end()) return false;
    tree_ = TreeSession{tree, tree->nodes[node_index].id, tree->nodes[node_index].text,
                        tree->node_order_revision};
    table_.reset();
    generic_.reset();
    initialize(tree->nodes[node_index].text, tree);
    return true;
}

bool tree_inline_edit_admitted(const TreeView& tree, const TreeNode& node) {
    return !tree.inline_edit_admission || tree.inline_edit_admission(node);
}

bool InlineEditStateMachine::begin(
    const WidgetPtr& owner, std::string text,
    std::function<std::optional<Rect>()> bounds,
    std::function<bool(std::string)> commit,
    std::vector<ChoiceItem> choices, const bool editable_choice,
    std::function<void(const std::shared_ptr<TextEdit>&,
                       const std::shared_ptr<ComboBox>&)> created,
    std::function<void()> cancelled) {
    // 完了通知を持つ編集状態を置き換えると、アプリのコードが呼ばれ
    // る場合がある。 セル・ノードの参照を保持したり新しい編集を設
    // 定したりする前に終了し、 通知の再入で開かれた編集は残す。
    if (table_ && table_->completion) {
        cancel();
        if (active()) return false;
    }
    if (!owner || !owner->visible() || !owner->enabled() || !bounds || !commit)
        return false;
    table_.reset();
    tree_.reset();
    generic_ = GenericSession{owner, std::move(bounds), std::move(commit), std::move(cancelled)};
    initialize(std::move(text), owner);
    if (!choices.empty()) {
        choice_ = std::make_shared<ComboBox>();
        choice_->editable = editable_choice;
        choice_->edit_text = editor_->text;
        choice_->set_font(owner->font());
        choice_->items = std::move(choices);
        for (std::size_t index = 0; index < choice_->items.size(); ++index) {
            if (choice_->items[index].value == editor_->text ||
                choice_->items[index].text == editor_->text) {
                choice_->selected_index = index;
                editor_->text = choice_->items[index].text;
                break;
            }
        }
        editor_->read_only = !editable_choice;
    }
    if (choice_) (void)select_all_text(*editor_, text_state_);
    publish_choice_selection();
    if (created) created(editor_, choice_);
    if (layout(scaled_ui_metric(19))) return true;
    cancel();
    return false;
}

bool InlineEditStateMachine::begin_tree_item(
    const std::shared_ptr<TreeView>& tree, const TreeItemId id) {
    if (!tree) return false;
    const auto found = std::ranges::find(tree->nodes, id, &TreeNode::id);
    return found != tree->nodes.end() &&
           begin(tree, static_cast<std::size_t>(
                           std::distance(tree->nodes.begin(), found)));
}

bool begin_presented_programmatic_table_inline_edit(
    InlineEditStateMachine& edit, const ProgrammaticInlineEditRequest& request,
    const std::function<bool(const WidgetPtr&)>& focus) {
    const auto table = request.table;
    if (edit.active() || !table || !request.matches_stable_row || !focus || !table->visible() ||
        !table->enabled() ||
        (request.focus_scope && !widget_in_input_scope(request.focus_scope, table)))
        return false;
    if (!table->inline_edit_column) return false;
    const auto column = *table->inline_edit_column;
    if (column >= table->columns.size()) return false;
    const auto revision = table->row_order_revision;
    const auto find_row = [&]() -> std::optional<std::size_t> {
        std::optional<std::size_t> result;
        const auto count = table->rows.size();
        for (std::size_t row = 0; row < count; ++row) {
            if (table->row_order_revision != revision || row >= table->rows.size()) return {};
            if (!request.matches_stable_row(row)) continue;
            // 安定した識別情報は一意でなければならない。 表示値が
            // たまたま同じ二行のうち、最初の行を勝手に編集しない。
            if (result) return {};
            result = row;
        }
        return table->row_order_revision == revision ? result : std::nullopt;
    };
    auto row = find_row();
    if (!row || *row >= table->rows.size() || column >= table->rows[*row].cells.size())
        return false;
    (void)update_table_selection(*table, *row);
    if (table->row_order_revision != revision || !table->visible() || !table->enabled() ||
        (request.focus_scope && !widget_in_input_scope(request.focus_scope, table))) return false;
    row = find_row();
    if (!row) return false;
    (void)reveal_table_row(*table, *row, request.row_height);
    if (table->row_order_revision != revision || !table->visible() || !table->enabled() ||
        (request.focus_scope && !widget_in_input_scope(request.focus_scope, table))) return false;
    row = find_row();
    if (!row || *row >= table->rows.size() || column >= table->rows[*row].cells.size()) return false;
    if (!focus(table)) return false;
    if (table->row_order_revision != revision || !table->visible() || !table->enabled() ||
        (request.focus_scope && !widget_in_input_scope(request.focus_scope, table))) return false;
    row = find_row();
    return row && *row < table->rows.size() && column < table->rows[*row].cells.size() &&
           edit.begin(table, *row, column);
}

bool begin_presented_programmatic_inline_edit(
    InlineEditStateMachine& edit, const ProgrammaticInlineEditRequest& request,
    const std::function<bool(const WidgetPtr&)>& focus) {
    const bool table_request = static_cast<bool>(request.table) ||
                               static_cast<bool>(request.matches_stable_row);
    const bool tree_request = static_cast<bool>(request.tree) ||
                               static_cast<bool>(request.matches_stable_tree_node);
    const bool generic_request = static_cast<bool>(request.generic_owner) ||
        static_cast<bool>(request.generic_bounds) ||
        static_cast<bool>(request.generic_commit);
    if (static_cast<int>(table_request) + static_cast<int>(tree_request) +
            static_cast<int>(generic_request) != 1)
        return false;
    if (generic_request) {
        if (edit.active() || !request.generic_owner || !request.generic_bounds ||
            !request.generic_commit || !focus || !request.generic_owner->visible() ||
            !request.generic_owner->enabled() ||
            (request.focus_scope &&
             !widget_in_input_scope(request.focus_scope, request.generic_owner)))
            return false;
        // 独自描画のセル（Canvas/Panel）自身は、キーボ
        // ードフォーカスを受けるコントロールではない。 表示側がそ
        // の上に生成した編集欄を置くため、 所有者へのフォーカス移
        // 動に失敗しただけで、ほかは有効な編集要求を拒否しない。
        (void)focus(request.generic_owner);
        return edit.begin(request.generic_owner, request.generic_text,
                          request.generic_bounds, request.generic_commit,
                          request.generic_choices,
                          request.generic_editable_choice,
                          request.generic_editor_created, request.generic_cancelled);
    }
    if (table_request)
        return begin_presented_programmatic_table_inline_edit(edit, request, focus);

    const auto tree = request.tree;
    if (edit.active() || !tree || !request.matches_stable_tree_node || !focus ||
        !tree->visible() || !tree->enabled() || !tree->on_item_edited ||
        (request.focus_scope && !widget_in_input_scope(request.focus_scope, tree)))
        return false;
    const auto revision = tree->node_order_revision;
    const auto find_node = [&]() -> std::optional<std::size_t> {
        std::optional<std::size_t> result;
        const auto count = tree->nodes.size();
        for (std::size_t index = 0; index < count; ++index) {
            if (tree->node_order_revision != revision || index >= tree->nodes.size())
                return {};
            if (!request.matches_stable_tree_node(tree->nodes[index])) continue;
            if (result) return {};
            result = index;
        }
        return tree->node_order_revision == revision ? result : std::nullopt;
    };
    const auto valid = [&]() {
        return tree->node_order_revision == revision && tree->visible() &&
               tree->enabled() && tree->on_item_edited &&
               (!request.focus_scope ||
                widget_in_input_scope(request.focus_scope, tree));
    };
    auto node = find_node();
    if (!node || !valid()) return false;
    auto visible = visible_tree_indices(*tree);
    if (std::ranges::find(visible, *node) == visible.end()) return false;
    auto id = tree->nodes[*node].id;
    (void)select_tree_items(*tree, {id});
    if (!valid() || !(node = find_node())) return false;
    visible = visible_tree_indices(*tree);
    if (std::ranges::find(visible, *node) == visible.end()) return false;
    id = tree->nodes[*node].id;
    // reveal_tree_itemはスクロールが変化したか
    // を返す。既に可視のノードも有効な対象なので、変化なしを
    // 失敗扱いせず、編集可否をスクロール状態に依存させない。
    (void)reveal_tree_item(*tree, id, request.row_height);
    if (!valid() || !(node = find_node()))
        return false;
    id = tree->nodes[*node].id;
    if (!focus(tree) || !valid() || !(node = find_node())) return false;
    id = tree->nodes[*node].id;
    return edit.begin_tree_item(tree, id);
}

bool InlineEditStateMachine::active() const noexcept {
    return table_.has_value() || tree_.has_value() || generic_.has_value();
}

bool InlineEditStateMachine::matches(const std::shared_ptr<TableView>& table,
                                     const std::size_t row,
                                     const std::size_t column) const noexcept {
    return table_ && table && table_->row_order_revision == table->row_order_revision &&
           table_->table.lock() == table && table_->row == row &&
           table_->column == column;
}

bool InlineEditStateMachine::matches(const std::shared_ptr<TreeView>& tree,
                                     const std::size_t node_index) const noexcept {
    return tree_ && tree && tree_->tree.lock() == tree &&
           tree_->node_order_revision == tree->node_order_revision && node_index < tree->nodes.size() &&
           tree_->id == tree->nodes[node_index].id;
}

const std::string& InlineEditStateMachine::buffer() const noexcept {
    return editor_->text;
}

void InlineEditStateMachine::synchronize_choice_selection() {
    if (!choice_ || !choice_->edit_selection) return;
    const auto [start, end] = *choice_->edit_selection;
    const auto selection_start = std::min(start, editor_->text.size());
    const auto selection_length = std::clamp(end, selection_start, editor_->text.size()) - selection_start;
    const bool selection_reversed = selection_length != 0U &&
                                    choice_->edit_selection_reversed;
    if (editor_->selection_start == selection_start &&
        editor_->selection_length == selection_length &&
        editor_->selection_reversed == selection_reversed) return;
    editor_->selection_start = selection_start;
    editor_->selection_length = selection_length;
    editor_->selection_reversed = selection_reversed;
    text_state_.anchor = selection_reversed
        ? selection_start + selection_length : selection_start;
    text_state_.caret = selection_reversed
        ? selection_start : selection_start + selection_length;
}

void InlineEditStateMachine::publish_choice_selection() {
    if (!choice_) return;
    choice_->selection_start = editor_->selection_start;
    choice_->selection_length = editor_->selection_length;
    choice_->edit_selection = std::pair{editor_->selection_start, editor_->selection_start + editor_->selection_length};
    choice_->edit_selection_reversed = editor_->selection_reversed;
}

bool InlineEditStateMachine::set_buffer(std::string text) {
    if (!active() || editor_->text == text) return false;
    editor_->text = std::move(text);
    return true;
}

bool InlineEditStateMachine::append(const std::string_view text) {
    if (!active() || text.empty()) return false;
    editor_->text.append(text);
    return true;
}

bool InlineEditStateMachine::erase_backward() {
    if (!active() || editor_->text.empty()) return false;
    editor_->text.erase(previous_utf8_boundary(editor_->text, editor_->text.size()));
    return true;
}

bool InlineEditStateMachine::clear_buffer() {
    if (!active() || editor_->text.empty()) return false;
    editor_->text.clear();
    return true;
}

bool InlineEditStateMachine::commit() {
    // 通知前に古い編集を終了する。コールバックがビューを再構築したり別の
    // 編集を始めたりしても、 今回の確定処理でそれをキャンセルしない。
    const auto table_session = table_;
    const auto tree_session = tree_;
    const auto generic_session = generic_;
    auto value = editor_->text;
    if (choice_ && !choice_->editable && choice_->selected_index &&
        *choice_->selected_index < choice_->items.size() &&
        choice_->items[*choice_->selected_index].value != choice_->items[*choice_->selected_index].text)
        value = choice_->items[*choice_->selected_index].value;
    reset_session(); // 確定後の終了処理で、仮の行を取り消して戻さない。
    bool changed{};
    if (table_session) {
        const auto& session = *table_session;
        if (const auto table = session.table.lock(); table && table->visible() && table->enabled() &&
            table->row_order_revision == session.row_order_revision &&
            table->on_cell_edited &&
            session.row < table->rows.size() &&
            session.column < table->rows[session.row].cells.size()) {
            const auto& cells = table->rows[session.row].cells;
            if (cells.size() != session.original_row.size()) return false;
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (session.original_row[i] != std::pair{cells[i].text, cells[i].value}) return false;
            const auto& cell = cells[session.column];
            if (cell.editor_value != session.original_editor_value ||
                cell.editor_choice_values != session.original_choice_values) return false;
            if (!cell.enabled) return false;
            const auto& choices = cell.editor_choice_values.size() == cell.editor_choices.size()
                ? cell.editor_choice_values : cell.editor_choices;
            if (cell.editor == CellEditorKind::Choice &&
                std::ranges::find(choices, value) == choices.end()) return false;
            if (cell.editor == CellEditorKind::Number) {
                try {
                    std::size_t used{};
                    const double raw = std::stod(value, &used);
                    if (used != value.size() || !std::isfinite(raw) ||
                        !std::isfinite(cell.editor_minimum) || !std::isfinite(cell.editor_maximum) ||
                        cell.editor_minimum > cell.editor_maximum) return false;
                    const double parsed = std::clamp(raw, cell.editor_minimum, cell.editor_maximum);
                    // 検証でユーザーの式のリテラルを丸めない。 実際
                    // に範囲内へ制限した場合だけ、表記を置き換える。
                    if (parsed != raw) {
                        // ここでは意図的に共通のストリーム整形を使う。 macOS
                        // 12を配布対象とするlibc++には、浮動小数
                        // 点の std::to_chars がないため。
                        std::ostringstream formatted;
                        formatted.imbue(std::locale::classic());
                        formatted << std::setprecision(std::numeric_limits<double>::max_digits10)
                                  << std::defaultfloat << parsed;
                        if (!formatted) return false;
                        value = formatted.str();
                    }
                } catch (...) {
                    value = cell.text;
                }
            }
            if (session.completion) session.completion->accepted = true;
            if (table->commit_unchanged_inline_edits &&
                cell.editor_value.value_or(cell.text) == value) {
                const auto callback = table->on_cell_edited;
                callback(session.row, session.column, std::move(value));
            } else {
                changed = edit_table_cell(*table, session.row, session.column,
                                          std::move(value));
            }
        }
    } else if (tree_session) {
        if (const auto tree = tree_session->tree.lock(); tree && tree->visible() && tree->enabled() &&
            tree->node_order_revision == tree_session->node_order_revision && tree->on_item_edited) {
            const auto node = std::ranges::find(tree->nodes, tree_session->id, &TreeNode::id);
            const auto visible = visible_tree_indices(*tree);
            if (node != tree->nodes.end() && node->text == tree_session->original_text &&
                std::ranges::find(visible, static_cast<std::size_t>(node-tree->nodes.begin())) != visible.end())
                changed = edit_tree_item_text(*tree, tree_session->id, std::move(value));
        }
    } else if (generic_session) {
        if (const auto owner = generic_session->owner.lock(); owner &&
            owner->visible() && owner->enabled() && generic_session->commit)
            changed = generic_session->commit(std::move(value));
    }
    return changed;
}

void InlineEditStateMachine::cancel() {
    // 先に古い編集欄を解除する。モデル側のコールバックがビューを再構築した
    // り 新しい編集を始めたりしても、今回のキャンセルでそれを消さない。
    const auto table_session = table_;
    const auto generic_session = generic_;
    reset_session();
    if (table_session) {
        const auto& session = *table_session;
        const auto table = session.table.lock();
        if (!table || table->row_order_revision != session.row_order_revision ||
            session.row >= table->rows.size()) return;
        const auto& cells = table->rows[session.row].cells;
        if (cells.size() != session.original_row.size()) return;
        for (std::size_t i = 0; i < cells.size(); ++i)
            if (session.original_row[i] != std::pair{cells[i].text, cells[i].value}) return;
        const auto callback = table->on_inline_edit_cancelled;
        if (callback) callback(session.row, session.column);
    } else if (generic_session && !generic_session->owner.expired()) {
        const auto callback = generic_session->cancelled;
        if (callback) callback();
    }
}

void InlineEditStateMachine::reset_session() noexcept {
    table_.reset();
    tree_.reset();
    generic_.reset();
    choice_.reset();
    editor_->text.clear();
    text_state_ = {};
}

WidgetPtr InlineEditStateMachine::owner() const noexcept {
    if (table_) return table_->table.lock();
    if (tree_) return tree_->tree.lock();
    if (generic_) return generic_->owner.lock();
    return {};
}

void InlineEditStateMachine::initialize(std::string text, const WidgetPtr& widget) {
    choice_.reset();
    editor_ = std::make_shared<TextEdit>();
    editor_->text = std::move(text);
    editor_->set_font(widget->font());
    text_state_ = {};
    (void)select_all_text(*editor_, text_state_);
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(widget)) {
        const auto created = tree->on_inline_editor_created;
        if (created) created(editor_);
    }
}

void InlineEditStateMachine::set_bounds(Rect bounds) {
    if (choice_) {
        choice_->set_bounds(bounds);
        bounds = layout_combo_box(*choice_).text;
    }
    editor_->set_bounds(bounds);
}

bool InlineEditStateMachine::insert(std::string_view text) {
    const auto widget = owner();
    if (!widget || !widget->enabled()) return false;
    synchronize_choice_selection();
    const auto editor = editor_;
    const auto choice = choice_;
    const bool changed = replace_text_selection(*editor, text_state_, text);
    if (changed && active() && owner() == widget && editor_ == editor && choice_ == choice && choice) {
        choice_->edit_text = editor_->text;
        choice_->selected_index.reset();
        publish_choice_selection();
    }
    return changed;
}

bool InlineEditStateMachine::commit_choice() {
    if (!choice_ || !choice_->selected_index || *choice_->selected_index >= choice_->items.size())
        return false;
    (void)set_buffer(choice_->items[*choice_->selected_index].text);
    return commit();
}

bool InlineEditStateMachine::layout(float row_height) {
    const auto widget = owner();
    if (!widget || !widget->visible() || !widget->enabled() || row_height <= 0) return false;
    row_height = item_row_height(*widget, row_height);
    const auto r = widget->bounds();
    if (table_) {
        const auto table = table_->table.lock();
        if (!table || table->row_order_revision != table_->row_order_revision) return false;
        if (table_->row < table->top_index || table_->row >= table->rows.size() ||
            table_->column >= table->columns.size() ||
            table_->column >= table->rows[table_->row].cells.size()) return false;
        const auto viewport = item_scroll_geometry(*table,row_height).viewport;
        const auto widths = resolved_table_column_widths(*table, viewport.width);
        float x = r.x;
        for (std::size_t c = 0; c < table_->column; ++c) x += widths[c];
        const auto row_rect = table_row_geometry(*table, table_->row, row_height);
        const float y = row_rect.y;
        row_height = row_rect.height;
        x -= table->horizontal_offset;
        if (y + row_height > viewport.y + viewport.height) return false;
        float leading = scaled_ui_metric(5);
        if (table->checkable && table_->column == 0U) leading += scaled_ui_metric(14);
        if (table->rows[table_->row].cells[table_->column].icon) leading += scaled_ui_metric(17);
        leading = std::min(leading, widths[table_->column]);
        const float left=std::max(viewport.x,x+leading);
        const float right=std::min(viewport.x+viewport.width,x+widths[table_->column]);
        if (right<=left) return false;
        set_bounds({left, y, right-left, row_height});
        return true;
    }
    if (tree_) {
        const auto tree = tree_->tree.lock();
        if (!tree || tree->node_order_revision != tree_->node_order_revision) return false;
        (void)set_item_horizontal_offset(*tree,tree->horizontal_offset);
        const auto visible = visible_tree_indices(*tree);
        for (std::size_t i = tree->top_index; i < visible.size(); ++i) {
            const auto index = visible[i];
            if (tree->nodes[index].id != tree_->id) continue;
            const float y = r.y + row_height * static_cast<float>(i - tree->top_index);
            const auto viewport=item_scroll_geometry(*tree,row_height).viewport;
            if (y + row_height > viewport.y + viewport.height) return false;
            const auto geometry = tree_row_geometry(*tree, viewport, index, y, row_height);
            if (!geometry) return false;
            const float left = std::max(r.x, geometry->text.x);
            const float right = std::min(viewport.x + viewport.width, geometry->text.x + geometry->text.width);
            if (right <= left) return false;
            set_bounds({left, y, right-left, row_height});
            return true;
        }
    }
    if (generic_) {
        const auto bounds = generic_->bounds ? generic_->bounds() : std::nullopt;
        if (!bounds || bounds->width <= 0.0F || bounds->height <= 0.0F)
            return false;
        set_bounds(*bounds);
        return true;
    }
    return false;
}

bool set_toggle_checked(ToggleButton& toggle, const bool checked) {
    auto* check=dynamic_cast<CheckBox*>(&toggle);
    const bool mixed=check && check->indeterminate;
    if (toggle.checked == checked && !mixed) return false;
    if(check) check->indeterminate=false;
    toggle.checked = checked;
    if (const auto notify=toggle.on_changed) notify(checked);
    return true;
}

bool select_radio_button(
    RadioButton& selected,
    const std::vector<std::shared_ptr<RadioButton>>& group) {
    const auto members = group; // 通知が、呼び出し元のモデル・グループを消去する場合がある。
    std::vector<std::pair<RadioButton*, bool>> changes;
    for (const auto& radio : members) {
        if (!radio || radio.get() == &selected || radio->group != selected.group || !radio->checked)
            continue;
        radio->checked = false;
        changes.emplace_back(radio.get(), false);
    }
    if (!selected.checked) {
        selected.checked = true;
        changes.emplace_back(&selected, true);
    }
    // コールバックがダイアログを再構築・別項目を選択する前に、排他
    // 的な選択状態全体を反映する。 再入による選択を上書きしない。
    for (const auto& [radio, value] : changes) {
        if (radio->checked == value && radio->on_changed) {
            const auto notify = radio->on_changed;
            notify(value);
        }
    }
    return !changes.empty();
}

std::optional<double> validated_number_value(const NumberEdit& number,const double value) noexcept {
    if (!std::isfinite(value) || !std::isfinite(number.minimum) ||
        !std::isfinite(number.maximum) || number.minimum > number.maximum) return {};
    return std::clamp(value,number.minimum,number.maximum);
}
bool set_number_value(NumberEdit& number, const double value,
                      const NumberValueOrigin origin) {
    const auto normalized=validated_number_value(number,value);
    if(!normalized)return false;
    const double clamped=*normalized;
    const bool changed=number.value!=clamped;
    const auto notify=origin==NumberValueOrigin::Preview && number.on_preview
        ? number.on_preview : origin==NumberValueOrigin::Committed && number.on_committed
        ? number.on_committed : number.on_changed;
    // 確定したプレビューはすでに表示値と同じでも、モデル
    // 側の一度の確定境界を通す必要がある。 既定では、従
    // 来どおり変更時だけ on_changed を呼ぶ。
    if(!changed && !(origin==NumberValueOrigin::Committed && number.on_committed))return false;
    number.value = clamped;
    if(notify)notify(clamped);
    return changed;
}

bool step_number_value(NumberEdit& number, const double steps) {
    if (!number.enabled() || number.read_only || steps == 0.0 || !std::isfinite(steps)) return false;
    const double increment = std::isfinite(number.step) && number.step > 0.0 ? number.step : 1.0;
    return set_number_value(number, number.value + increment * steps);
}

bool set_slider_value(Slider& slider, double value) {
    if (!std::isfinite(value) || !std::isfinite(slider.minimum) ||
        !std::isfinite(slider.maximum) || slider.minimum>slider.maximum) return false;
    value = std::clamp(value, slider.minimum, slider.maximum);
    if (std::isfinite(slider.step) && slider.step > 0.0) {
        const double steps=(value-slider.minimum)/slider.step;
        const double snapped=slider.minimum+std::round(steps)*slider.step;
        if (std::isfinite(snapped)) value=snapped;
    }
    value = std::clamp(value, slider.minimum, slider.maximum);
    if (slider.value == value) return false;
    slider.value = value;
    if (const auto notify=slider.on_changed) notify(value);
    return true;
}

bool set_slider_value_from_point(Slider& slider, const Point point) {
    const auto value=slider_value_at(slider,point);
    return value && set_slider_value(slider,*value);
}

bool set_color_value(ColorWell& well, const Color value) {
    if (well.value.red == value.red && well.value.green == value.green &&
        well.value.blue == value.blue && well.value.alpha == value.alpha)
        return false;
    well.value = value;
    if (const auto notify=well.on_changed) notify(value);
    return true;
}

bool select_combo_item(ComboBox& combo, std::optional<std::size_t> index,
                       const bool notify) {
    if (index && *index >= combo.items.size()) index.reset();
    if (index && !combo.items[*index].enabled) return false;
    const bool selection_changed = combo.selected_index != index;
    const std::string previous_text = combo.edit_text;
    const auto selection_callback = combo.on_selection_changed;
    const auto text_callback = combo.on_text_changed;
    combo.selected_index = index;
    if (combo.editable && index) combo.edit_text = combo.items[*index].text;
    if (combo.editable && index) {
        combo.selection_start = 0;
        combo.selection_length = combo.edit_text.size();
        combo.edit_selection = std::pair<std::size_t, std::size_t>{0, combo.edit_text.size()};
        combo.edit_selection_reversed = false;
    }
    const auto published_text = combo.edit_text;
    const bool text_changed = previous_text != published_text;
    if (notify && selection_changed && selection_callback) {
        selection_callback(index);
    } else if (notify && combo.editable && text_changed && text_callback) {
        text_callback(published_text);
    }
    return selection_changed || text_changed;
}

bool set_combo_edit_text(ComboBox& combo, std::string text,
                        std::optional<std::pair<std::size_t, std::size_t>> selection) {
    if (!combo.editable) return false;
    const auto previous_display = combo_display_text(combo);
    const bool selection_changed = combo.selected_index.has_value();
    const bool text_changed = previous_display != text;
    if (!selection_changed && !text_changed) return false;
    const auto selection_callback = combo.on_selection_changed;
    const auto text_callback = combo.on_text_changed;
    combo.selected_index.reset();
    combo.edit_text = std::move(text);
    const auto start = selection ? std::min(selection->first, combo.edit_text.size()) : 0U;
    const auto end = selection ? std::clamp(selection->second, start, combo.edit_text.size()) : 0U;
    combo.selection_start = start;
    combo.selection_length = end - start;
    combo.edit_selection = std::pair<std::size_t, std::size_t>{start, end};
    combo.edit_selection_reversed = false;
    // 再入による選択肢・文字列の置換をまたいで、値とコールバックを保持する。
    const auto published_text = combo.edit_text;
    if (selection_changed && selection_callback)
        selection_callback(std::nullopt);
    if (text_changed && text_callback && combo.editable && !combo.selected_index &&
        combo.edit_text == published_text)
        text_callback(published_text);
    return true;
}

std::optional<std::size_t> first_enabled_combo_item(
    const ComboBox& combo, std::size_t preferred) noexcept {
    if (combo.items.empty()) return std::nullopt;
    preferred = std::min(preferred, combo.items.size() - 1U);
    if (combo.items[preferred].enabled) return preferred;
    for (std::size_t distance = 1U; distance < combo.items.size(); ++distance) {
        if (preferred + distance < combo.items.size() &&
            combo.items[preferred + distance].enabled)
            return preferred + distance;
        if (distance <= preferred && combo.items[preferred - distance].enabled)
            return preferred - distance;
    }
    return std::nullopt;
}

std::optional<std::size_t> adjacent_combo_item(
    const ComboBox& combo, const std::size_t start, const int direction) noexcept {
    if (combo.items.empty() || direction == 0) return std::nullopt;
    auto position = static_cast<std::int64_t>(
        std::min(start, combo.items.size() - 1U));
    for (std::size_t attempt = 0; attempt < combo.items.size(); ++attempt) {
        const auto next = position + (direction < 0 ? -1 : 1);
        if (next < 0 || next >= static_cast<std::int64_t>(combo.items.size()))
            return std::nullopt;
        position = next;
        const auto index = static_cast<std::size_t>(position);
        if (combo.items[index].enabled) return index;
    }
    return std::nullopt;
}

bool ComboPopupStateMachine::open(const std::shared_ptr<ComboBox>& combo,
                                  const std::size_t visible_rows) {
    if (!combo || !combo->enabled() || combo->items.empty()) {
        close();
        return false;
    }
    const auto first = first_enabled_combo_item(
        *combo, combo->selected_index.value_or(0U));
    if (!first) {
        close();
        return false;
    }
    std::vector<ChoiceItem> items_snapshot;
    try {
        items_snapshot = combo->items;
    } catch (...) {
        close();
        throw;
    }
    ++generation_;
    combo_ = combo;
    items_snapshot_ = std::move(items_snapshot);
    selected_snapshot_ = combo->selected_index;
    highlighted_ = *first;
    top_ = 0U;
    requested_visible_rows_ = std::max<std::size_t>(1U,
        combo->visible_item_count != 0U ? combo->visible_item_count : visible_rows);
    ensure_visible(requested_visible_rows_);
    return true;
}

void ComboPopupStateMachine::close() noexcept {
    ++generation_;
    thumb_grab_.reset();
    combo_.reset();
    items_snapshot_.clear();
    selected_snapshot_.reset();
    highlighted_ = 0U;
    top_ = 0U;
}

std::shared_ptr<ComboBox> ComboPopupStateMachine::combo() const noexcept {
    const auto value = combo_.lock();
    if (!value || value->items != items_snapshot_ ||
        value->selected_index != selected_snapshot_)
        return {};
    return value;
}

bool ComboPopupStateMachine::is_open_for(
    const std::shared_ptr<ComboBox>& combo) const noexcept {
    return combo && this->combo() == combo;
}

bool ComboPopupStateMachine::set_highlight(const std::size_t index) {
    const auto value = combo();
    if (!value || index >= value->items.size() || !value->items[index].enabled)
        return false;
    if (highlighted_ == index) return false;
    highlighted_ = index;
    return true;
}

bool ComboPopupStateMachine::move_highlight(const int direction) {
    const auto value = combo();
    if (!value) return false;
    const auto next = adjacent_combo_item(*value, highlighted_, direction);
    return next && set_highlight(*next);
}

bool ComboPopupStateMachine::commit() {
    const auto value = combo();
    if (!value || highlighted_ >= value->items.size() ||
        !value->items[highlighted_].enabled) {
        if (!value) close();
        return false;
    }
    const std::size_t index = highlighted_;
    close();
    return select_combo_item(*value, index);
}

bool ComboPopupStateMachine::handle_key(
    const std::shared_ptr<ComboBox>& target, const Key key, const bool alt,
    const std::size_t visible_rows, const bool command) {
    if (!target || !target->enabled()) return false;
    if (command && key == Key::F4) return false; // タブ・ウィンドウを閉じるショートカットを維持する。
    const bool popup_open = is_open_for(target);
    if (key == Key::F4 || (alt && (key == Key::Down || key == Key::Up))) {
        if (popup_open) {
            if (alt && key == Key::Up) (void)commit();
            else close();
        } else (void)open(target, visible_rows);
        return true;
    }
    // 一覧が開いていない場合、これらのキーは選択肢の移動ではなく、
    // 編集可能なテキスト欄またはダイアログの既定ボタンが扱う。
    if (target->editable && !popup_open &&
        (key == Key::Home || key == Key::End || key == Key::Enter)) return false;
    if (key == Key::Escape && popup_open) {
        close();
        return true;
    }
    if ((key == Key::Enter || (!target->editable && key == Key::Space)) &&
        popup_open) {
        (void)commit();
        return true;
    }
    if ((key == Key::Enter || (!target->editable && key == Key::Space) ||
         (key == Key::Down && alt)) && !popup_open) {
        (void)open(target, visible_rows);
        return true;
    }
    if (key != Key::Up && key != Key::Down && key != Key::Home &&
        key != Key::End)
        return false;
    if (target->items.empty()) return true;

    std::optional<std::size_t> next;
    if (key == Key::Home) {
        next = first_enabled_combo_item(*target, 0U);
    } else if (key == Key::End) {
        for (std::size_t index = target->items.size(); index > 0U; --index)
            if (target->items[index - 1U].enabled) {
                next = index - 1U;
                break;
            }
    } else {
        const int direction = key == Key::Up ? -1 : 1;
        const std::size_t current = popup_open
            ? highlighted_
            : target->selected_index.value_or(
                  direction > 0 ? 0U : target->items.size() - 1U);
        if (!popup_open && !target->selected_index &&
            target->items[current].enabled)
            next = current;
        else
            next = adjacent_combo_item(*target, current, direction);
    }
    if (!next) return true;
    if (popup_open) {
        (void)set_highlight(*next);
        ensure_visible(visible_rows);
    } else {
        (void)select_combo_item(*target, *next);
    }
    return true;
}

void ComboPopupStateMachine::ensure_visible(const std::size_t visible_rows) noexcept {
    visible_rows_ = visible_rows;
    const auto value = combo();
    if (!value) return;
    const std::size_t visible = std::max<std::size_t>(1U,
        std::min(visible_rows, value->items.size()));
    const std::size_t maximum = value->items.size() > visible
        ? value->items.size() - visible : 0U;
    if (highlighted_ < top_) top_ = highlighted_;
    else if (highlighted_ >= top_ + visible)
        top_ = highlighted_ - visible + 1U;
    top_ = std::min(top_, maximum);
}

void ComboPopupStateMachine::set_top(const std::size_t top,
                                     const std::size_t visible_rows) noexcept {
    visible_rows_ = visible_rows;
    const auto value = combo();
    if (!value) return;
    top_ = clamp_top_index(top, value->items.size(),
                           std::max<std::size_t>(1U, visible_rows));
}

ComboPopupLayout ComboPopupStateMachine::layout(const Rect viewport) {
    const auto value = combo();
    if (!value || !value->visible() || !value->enabled() || value->items.empty()) {
        close();
        return {};
    }
    // 要求された上限と、このフレームに収まる行数を分けて保持する。 既定値
    // を使うと ALL のコンテントパレットが九行に減り、 visible_rows_
    // を使い回すと、縮めたポップアップが短いままになる。
    const auto geometry = layout_combo_popup(*value, viewport, requested_visible_rows_);
    if (visible_rows_ != geometry.visible_rows) ensure_visible(geometry.visible_rows);
    else set_top(top_, geometry.visible_rows);
    return geometry;
}

ComboPopupClick ComboPopupStateMachine::click(const ComboPopupLayout& geometry,
                                              const Point point) {
    const auto value = combo();
    if (!value) return ComboPopupClick::Outside;
    if (point_inside(geometry.bounds, point)) {
        if (geometry.scroll_track &&
            point_inside(*geometry.scroll_track, point)) {
            const auto page = std::max<std::size_t>(1U, geometry.visible_rows);
            const auto thumb = combo_popup_scroll_thumb(geometry, value->items.size(), top_);
            if (thumb && point.y >= thumb->y && point.y < thumb->y + thumb->height) {
                // つまみをつかみ、ポインター移動でドラッグする（drag_thumb）。
                thumb_grab_ = point.y - thumb->y;
                return ComboPopupClick::Consumed;
            }
            // 軌道の押下では、軌道の中央ではなく、つまみに対し
            // てポインターがある上下の方向へページ移動する。
            const auto middle = thumb ? thumb->y + thumb->height * 0.5F
                                      : geometry.scroll_track->y +
                                            geometry.scroll_track->height * 0.5F;
            scroll_rows(point.y < middle ? -static_cast<int>(page)
                                         : static_cast<int>(page),
                        page);
            return ComboPopupClick::Consumed;
        }
        if (const auto index = geometry.item_at(point, top_, value->items.size());
            index && value->items[*index].enabled) {
            for (std::size_t action = 0; action < geometry.action_widths.size(); ++action) {
                if (point_inside(geometry.action_bounds(*index - top_, action), point) && value->on_popup_action) {
                    auto callback = value->on_popup_action;
                    const auto key = value->items[*index].value;
                    close(); // アプリのコードが一覧・ドックを組み直す前に無効化する。
                    callback(key, action);
                    return ComboPopupClick::Consumed;
                }
            }
            (void)set_highlight(*index);
            (void)commit(); // 選択済みの項目をクリックした場合も閉じる。
        }
        return ComboPopupClick::Consumed;
    }
    const bool anchor = point_inside(value->bounds(), point);
    close();
    return anchor ? ComboPopupClick::Anchor : ComboPopupClick::Outside;
}

bool ComboPopupStateMachine::hover(const ComboPopupLayout& geometry, const Point point) {
    const auto value = combo();
    if (!value) return false;
    const auto index = geometry.item_at(point, top_, value->items.size());
    return index && set_highlight(*index);
}

bool ComboPopupStateMachine::wheel(const ComboPopupLayout& geometry,
                                   const Point point, const float delta) {
    const auto value = combo();
    if (!value || delta == 0.0F || (!point_inside(geometry.bounds, point) &&
                                  !point_inside(value->bounds(), point))) return false;
    (void)move_highlight(delta > 0.0F ? -1 : 1);
    ensure_visible(geometry.visible_rows);
    return true;
}

bool ComboPopupStateMachine::drag_thumb(const ComboPopupLayout& geometry, const Point point) {
    const auto value = combo();
    if (!value || !thumb_grab_ || !geometry.scroll_track || !std::isfinite(point.y))
        return false;
    const auto count = value->items.size();
    const auto thumb = combo_popup_scroll_thumb(geometry, count, top_);
    if (!thumb) return false;
    const auto maximum = count - geometry.visible_rows;
    const float travel = geometry.scroll_track->height - thumb->height;
    if (travel <= 0.0F || maximum == 0U) return false;
    const float offset = std::clamp(point.y - *thumb_grab_ - geometry.scroll_track->y,
                                    0.0F, travel);
    const auto top = static_cast<std::size_t>(
        std::lround(offset / travel * static_cast<float>(maximum)));
    if (top == top_) return false;
    set_top(top, geometry.visible_rows);
    return true;
}

void ComboPopupStateMachine::scroll_rows(const int rows, const std::size_t visible) noexcept {
    const auto value = combo();
    if (!value) return;
    auto top = top_;
    (void)scroll_top_index(top, rows, value->items.size(), visible);
    set_top(top, visible);
}

bool ComboPopupStateMachine::pan(const ComboPopupLayout& geometry,
                                  const float displacement, float& remainder) {
    const auto value = combo();
    if (!value || !std::isfinite(displacement) || !std::isfinite(remainder) ||
        !std::isfinite(geometry.row_height) || geometry.row_height <= 0 ||
        geometry.visible_rows == 0) return false;
    const double total = static_cast<double>(remainder) - displacement;
    const double steps = std::trunc(total / geometry.row_height);
    const int rows = static_cast<int>(std::clamp(steps,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max())));
    remainder = static_cast<float>(std::fmod(total, geometry.row_height));
    scroll_rows(rows, geometry.visible_rows);
    const auto maximum = value->items.size() > geometry.visible_rows
        ? value->items.size() - geometry.visible_rows : 0;
    if ((top_ == 0 && remainder < 0) || (top_ >= maximum && remainder > 0))
        remainder = 0; // 行き過ぎたスクロールによって、逆向きのドラッグを遅らせない。
    return true;
}

namespace {
bool visit_menu_scope(const WidgetPtr& widget, const std::function<bool(const WidgetPtr&)>& visitor,
                      const bool root = true, std::vector<WidgetPtr>* path = nullptr) {
    if (!widget || !widget->visible() || !widget->enabled() ||
        (!root && std::dynamic_pointer_cast<Window>(widget))) return false;
    if (path) path->push_back(widget);
    if (visitor(widget)) return true;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(widget)) {
        if (tabs->selected_index && *tabs->selected_index < tabs->pages.size() &&
            visit_menu_scope(tabs->pages[*tabs->selected_index].content, visitor, false, path)) return true;
    } else if (const auto container = std::dynamic_pointer_cast<Container>(widget)) {
        const auto children = container->children();
        const auto expand = std::dynamic_pointer_cast<ExpandBar>(widget);
        for (std::size_t i = 0; i < children.size(); ++i)
            if ((!expand || expand_child_is_open(*expand, children[i].get())) &&
                visit_menu_scope(children[i], visitor, false, path)) return true;
    }
    if (path) path->pop_back();
    return false;
}

std::shared_ptr<Menu> menu_bar_in_scope(const WidgetPtr& root) {
    std::shared_ptr<Menu> result;
    (void)visit_menu_scope(root, [&](const WidgetPtr& widget) {
        if (widget->kind() == ComponentKind::MenuBar) result = std::dynamic_pointer_cast<Menu>(widget);
        return bool(result);
    });
    return result;
}

char menu_mnemonic_key(const Key key) {
    if (key >= Key::A && key <= Key::Z)
        return static_cast<char>('A' + static_cast<int>(key) - static_cast<int>(Key::A));
    if (key >= Key::Digit0 && key <= Key::Digit9)
        return static_cast<char>('0' + static_cast<int>(key) - static_cast<int>(Key::Digit0));
    return '\0';
}

std::vector<std::size_t> menu_mnemonic_matches(const Menu& menu, const Key key) {
    std::vector<std::size_t> result;
    const char mnemonic = menu_mnemonic_key(key);
    if (!mnemonic) return result;
    for (std::size_t index = 0; index < menu.items.size(); ++index) {
        const auto& item = menu.items[index];
        if (!item.enabled || item.separator) continue;
        const auto label = std::string_view(item.text).substr(0, item.text.find('\t'));
        for (std::size_t pos = 0; pos + 1U < label.size(); ++pos) {
            if (label[pos] != '&') continue;
            const char next = label[++pos];
            if (next == '&') continue;
            const char upper = next >= 'a' && next <= 'z' ? next - 'a' + 'A' : next;
            if (upper == mnemonic) { result.push_back(index); break; }
        }
    }
    return result;
}

std::optional<std::size_t> selectable_menu_item(const Menu& menu, const std::size_t start,
                                                const int direction, const bool include_start) {
    if (menu.items.empty()) return std::nullopt;
    auto index = std::min(start, menu.items.size() - 1U);
    for (std::size_t step = 0; step < menu.items.size(); ++step) {
        if (step != 0U || !include_start)
            index = direction > 0 ? (index + 1U) % menu.items.size()
                                  : (index == 0U ? menu.items.size() - 1U : index - 1U);
        if (menu.items[index].enabled && !menu.items[index].separator) return index;
    }
    return std::nullopt;
}
}

void MenuPopupStateMachine::open(const std::shared_ptr<Menu>& menu,
                                  const WidgetPtr& owner, const Point anchor) {
    close();
    if (!menu || !menu->enabled()) return;
    if (const auto callback=menu->on_open) callback();
    if (menu->items.empty()) return;
    owner_ = owner;
    anchor_ = anchor;
    levels_.push_back({menu, std::nullopt, 0U, false});
}

bool MenuPopupStateMachine::open_menu_bar_item(const std::shared_ptr<Menu>& menu,
                                                const std::size_t index, const WidgetPtr& owner,
                                                const CommandGesture gesture) {
    if (!menu || !menu->enabled() || !menu->visible()) return false;
    if (const auto callback=menu->on_open) callback();
    if (!menu->enabled() || !menu->visible() || index >= menu->items.size()) return false;
    auto item = menu->items[index];
    if (!item.enabled || item.separator) return false;
    if (item.open_children) {
        const auto before = generation_;
        item.children = item.open_children();
        if (generation_ != before) return false;
        if (item.children.empty()) { close(); return true; }
    }
    if (!item.has_submenu()) {
        close();
        return activate_menu_item(item, menu->on_command, menu->on_command_with_gesture, gesture);
    }
    auto popup = std::make_shared<Menu>(true);
    popup->set_font(menu->font());
    popup->items = item.children;
    popup->on_command = menu->on_command;
    popup->on_command_with_gesture = menu->on_command_with_gesture;
    const auto geometry = menu_bar_item_geometry(*menu);
    if (index >= geometry.size()) return false;
    open(popup, owner, {geometry[index].x, geometry[index].y + geometry[index].height});
    menu_bar_ = menu;
    menu_bar_index_ = index;
    return true;
}

void MenuPopupStateMachine::close() noexcept {
    ++generation_;
    levels_.clear();
    owner_.reset();
    menu_bar_.reset();
    alt_root_.reset();
    consumed_release_.reset();
    consumed_root_.reset();
}

void MenuPopupStateMachine::cancel_for(const WidgetPtr& root) {
    if (alt_root_.lock() == root || consumed_root_.lock() == root ||
        visit_menu_scope(root, [&](const WidgetPtr& widget) { return widget == owner(); })) close();
}

std::optional<std::size_t> MenuPopupStateMachine::selected_menu_bar_item(const Menu& menu) const noexcept {
    if (menu_bar_.lock().get() == &menu && menu_bar_index_ < menu.items.size()) return menu_bar_index_;
    return {};
}

bool MenuPopupStateMachine::focus_menu_bar(const std::shared_ptr<Menu>& menu) {
    close();
    if (!menu || !menu->visible() || !menu->enabled()) return false;
    if (const auto callback=menu->on_open) callback();
    if (!menu->visible() || !menu->enabled()) return false;
    const auto first = selectable_menu_item(*menu, 0, 1, true);
    if (!first) return false;
    menu_bar_ = menu;
    owner_ = menu;
    menu_bar_index_ = *first;
    return true;
}

bool MenuPopupStateMachine::handle_key_event(const WidgetPtr& root, const KeyEvent& event,
                                             const bool pressed) {
    const bool here = is_active() && visit_menu_scope(root, [&](const WidgetPtr& widget) {
        return widget == owner();
    });
    const auto mask = static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Alt |
                                                Modifiers::Shift | Modifiers::Meta);
    const auto mods = static_cast<std::uint32_t>(event.modifiers) & mask;
    if (!pressed) {
        if (event.key == Key::Alt && alt_root_.lock() == root) {
            alt_root_.reset();
            const bool activated = focus_menu_bar(menu_bar_in_scope(root));
            return activated;
        }
        if (consumed_release_ == event.key && consumed_root_.lock() == root) {
            consumed_release_.reset();
            consumed_root_.reset();
            return true;
        }
        return here;
    }
    if (event.key == Key::Alt && (mods == 0U || mods == static_cast<std::uint32_t>(Modifiers::Alt))) {
        if (event.repeat) return here || alt_root_.lock() == root;
        if (here) { close(); consumed_release_ = Key::Alt; consumed_root_ = root; return true; }
        if (menu_bar_in_scope(root)) { alt_root_ = root; return true; }
        return false;
    }
    alt_root_.reset(); // Alt+Tab、AltGr、ほかのキーの組み合わせでは、Altの解放時にメニューへフォーカスを移さない。
    bool handled{};
    if (event.key == Key::F10 && mods == 0U) {
        if (event.repeat) return here;
        if (here) { close(); handled = true; }
        else handled = focus_menu_bar(menu_bar_in_scope(root));
    } else if (mods == static_cast<std::uint32_t>(Modifiers::Alt) && menu_mnemonic_key(event.key)) {
        if (const auto menu = menu_bar_in_scope(root)) {
            if (const auto callback=menu->on_open) callback();
            const auto matches = menu_mnemonic_matches(*menu, event.key);
            if (!matches.empty()) {
                handled = open_menu_bar_item(menu, matches.front(), menu,
                                              {event.modifiers,{},event.physical_control});
                if (handled && is_open()) (void)handle_key(Key::Down, event.modifiers, event.physical_control);
            }
        }
    }
    if (!handled && here) handled = handle_key(event.key, event.modifiers, event.physical_control);
    if (handled) { consumed_release_ = event.key; consumed_root_ = root; }
    return handled;
}

bool MenuPopupStateMachine::switch_menu_bar(const int direction) {
    const auto bar = menu_bar_.lock();
    if (!bar || bar->items.empty()) return false;
    auto index = std::min(menu_bar_index_, bar->items.size() - 1U);
    for (std::size_t step = 0U; step < bar->items.size(); ++step) {
        index = direction > 0 ? (index + 1U) % bar->items.size()
                              : (index == 0U ? bar->items.size() - 1U : index - 1U);
        const auto& item = bar->items[index];
        if (item.enabled && !item.separator && item.has_submenu()) {
            if (!is_open()) { menu_bar_index_ = index; return true; }
            if (!open_menu_bar_item(bar, index, owner())) return false;
            (void)handle_key(Key::Down);
            return true;
        }
    }
    return false;
}

std::vector<MenuPopupLayout> MenuPopupStateMachine::layout(const Rect viewport) {
    std::vector<MenuPopupLayout> result;
    Point anchor = anchor_;
    std::optional<Rect> parent;
    for (std::size_t level = 0; level < levels_.size(); ++level) {
        auto& state = levels_[level];
        if (state.menu->items.empty()) { levels_.resize(level); break; }
        state.top = std::min(state.top, state.menu->items.size() - 1U);
        auto geometry = layout_menu_popup(*state.menu, anchor, viewport, state.top, parent);
        if (state.reveal_selection && state.selected) {
            if (*state.selected < state.top) state.top = *state.selected;
            while (!geometry.rows.empty() && geometry.rows.back().index < *state.selected &&
                   state.top < *state.selected) {
                ++state.top;
                geometry = layout_menu_popup(*state.menu, anchor, viewport, state.top, parent);
            }
            geometry = layout_menu_popup(*state.menu, anchor, viewport, state.top, parent);
            state.reveal_selection = false;
        }
        result.push_back(geometry);
        if (level + 1U == levels_.size()) break;
        const auto row = std::ranges::find(geometry.rows, state.selected.value_or(~std::size_t{}),
                                           &MenuPopupRow::index);
        if (row == geometry.rows.end()) { levels_.resize(level + 1U); break; }
        parent = Rect{geometry.bounds.x, row->bounds.y, geometry.bounds.width, row->bounds.height};
        anchor = {geometry.bounds.x + geometry.bounds.width, row->bounds.y};
    }
    return result;
}

bool MenuPopupStateMachine::activate(const std::size_t level, const std::size_t index,
                                      const bool open_only, const CommandGesture gesture) {
    if (level >= levels_.size()) return false;
    const auto menu = levels_[level].menu;
    if (index >= menu->items.size()) return false;
    auto item = menu->items[index]; // 操作がメニューを再構築したり、所有者を閉じたりする場合がある。
    if (!item.enabled || item.separator) return false;
    if (item.open_children) {
        const auto before = generation_;
        item.children = item.open_children();
        if (generation_ != before) return false;
        if (item.children.empty()) {
            levels_.resize(level + 1U);
            levels_[level].selected = index;
            return true;
        }
    }
    levels_.resize(level + 1U);
    levels_[level].selected = index;
    if (item.has_submenu()) {
        auto child = std::make_shared<Menu>(true);
        child->set_font(menu->font());
        child->items = item.children;
        child->on_command = menu->on_command;
        child->on_command_with_gesture = menu->on_command_with_gesture;
        levels_.push_back({child, std::nullopt, 0U, false});
        return true;
    }
    if (open_only) return true;
    const auto command = menu->on_command;
    const auto command_with_gesture = menu->on_command_with_gesture;
    close(); // コールバックが別のポップアップを開いた場合、後からそれを消さない。
    return activate_menu_item(item, command, command_with_gesture, gesture);
}

bool MenuPopupStateMachine::contains(const Point point, const Rect viewport) {
    if (const auto bar = menu_bar_.lock(); bar && hit_test_menu_bar_item(*bar, point)) return true;
    const auto geometry = layout(viewport);
    return std::ranges::any_of(geometry, [point](const MenuPopupLayout& value) {
        return point_inside(value.bounds, point);
    });
}

bool MenuPopupStateMachine::click(const Point point, const Rect viewport,
                                  const CommandGesture gesture) {
    alt_root_.reset();
    if (!is_active()) return false;
    if (const auto bar = menu_bar_.lock()) {
        if (const auto index = hit_test_menu_bar_item(*bar, point)) {
            if (*index == menu_bar_index_ && is_open()) close();
            else (void)open_menu_bar_item(bar, *index, owner(), gesture);
            return true;
        }
    }
    const auto geometry = layout(viewport);
    for (std::size_t i = geometry.size(); i > 0U; --i) {
        if (!point_inside(geometry[i - 1U].bounds, point)) continue;
        if (const auto index = geometry[i - 1U].item_at(point))
            (void)activate(i - 1U, *index, false, gesture);
        return true;
    }
    close();
    return false;
}

bool MenuPopupStateMachine::hover(const Point point, const Rect viewport) {
    if (const auto bar = menu_bar_.lock()) {
        if (const auto index = hit_test_menu_bar_item(*bar, point)) {
            if (*index != menu_bar_index_ && bar->items[*index].enabled &&
                bar->items[*index].has_submenu()) {
                if (is_open()) (void)open_menu_bar_item(bar, *index, owner());
                else menu_bar_index_ = *index;
            }
            return true;
        }
    }
    const auto geometry = layout(viewport);
    for (std::size_t i = geometry.size(); i > 0U; --i) {
        if (!point_inside(geometry[i - 1U].bounds, point)) continue;
        const auto index = geometry[i - 1U].item_at(point);
        if (index && levels_[i - 1U].selected != index) {
            if (!activate(i - 1U, *index, true)) {
                levels_.resize(i);
                levels_.back().selected.reset();
            }
        }
        return true;
    }
    return false;
}

bool MenuPopupStateMachine::wheel(const Point point, const float delta, const Rect viewport) {
    const auto geometry = layout(viewport);
    for (std::size_t i = geometry.size(); i > 0U; --i) {
        if (!point_inside(geometry[i - 1U].bounds, point)) continue;
        levels_.resize(i);
        auto& state = levels_.back();
        const auto visible = std::max<std::size_t>(1U, geometry[i - 1U].rows.size());
        (void)scroll_top_index(state.top, delta > 0.0F ? -1 : delta < 0.0F ? 1 : 0,
                              state.menu->items.size(), visible);
        state.reveal_selection = false;
        return true;
    }
    return false;
}

bool MenuPopupStateMachine::pan(const Point anchor, const float displacement,
                                const Rect viewport, float& remainder) {
    if (!std::isfinite(displacement) || !std::isfinite(remainder) ||
        (displacement == 0 && remainder == 0)) return false;
    const auto geometry = layout(viewport);
    for (std::size_t i = geometry.size(); i > 0; --i) {
        if (!point_inside(geometry[i - 1].bounds, anchor)) continue;
        levels_.resize(i); // 親を動かした場合は、その子孫だけを閉じる。
        auto& state = levels_.back();
        state.reveal_selection = false;
        double remaining = static_cast<double>(remainder) - displacement;
        auto current_geometry = geometry[i - 1];
        while (remaining != 0) {
            const bool forward = remaining > 0;
            const bool end = current_geometry.rows.empty() ||
                current_geometry.rows.back().index + 1 >= state.menu->items.size();
            if ((forward && end) || (!forward && state.top == 0)) {
                remaining = 0;
                break;
            }
            const auto index = forward ? state.top : state.top - 1;
            const float height = menu_popup_item_height(*state.menu, index);
            if (!std::isfinite(height) || height <= 0) { remaining = 0; break; }
            if (std::abs(remaining) < height) break;
            if (forward) { ++state.top; remaining -= height; }
            else { --state.top; remaining += height; }
            // 区切りも含め、行の高さは表示側から取得する。
            current_geometry = layout_menu_popup(*state.menu, anchor, viewport, state.top);
        }
        remainder = static_cast<float>(remaining);
        return true;
    }
    return false;
}

bool MenuPopupStateMachine::handle_key(const Key key, const Modifiers modifiers,
                                      const std::optional<bool> physical_control) {
    if (!is_active()) return false;
    if (!is_open()) {
        const auto bar = menu_bar_.lock();
        if (key == Key::Escape || key == Key::Tab) close();
        else if (key == Key::Left || key == Key::Right) (void)switch_menu_bar(key == Key::Left ? -1 : 1);
        else if (bar && (key == Key::Down || key == Key::Up || key == Key::Enter || key == Key::Space)) {
            if (open_menu_bar_item(bar, menu_bar_index_, owner(), {modifiers,{},physical_control}) && is_open())
                (void)handle_key(key == Key::Up ? Key::End : Key::Home, modifiers, physical_control);
        } else if (bar) {
            const auto matches = menu_mnemonic_matches(*bar, key);
            if (!matches.empty() && open_menu_bar_item(bar, matches.front(), owner(), {modifiers,{},physical_control}) && is_open())
                (void)handle_key(Key::Home, modifiers, physical_control);
        }
        return true;
    }
    if (key == Key::Escape || key == Key::Left) {
        if (levels_.size() > 1U) levels_.pop_back();
        else if (key == Key::Left && switch_menu_bar(-1)) {}
        else if (key == Key::Escape && !menu_bar_.expired()) levels_.clear();
        else close();
        return true;
    }
    if (key == Key::Tab) { close(); return true; }
    auto& state = levels_.back();
    if (key == Key::Up || key == Key::Down || key == Key::Home || key == Key::End) {
        const bool backwards = key == Key::Up || key == Key::End;
        const bool endpoint = key == Key::Home || key == Key::End;
        const auto start = (!state.selected || endpoint)
            ? (backwards && !state.menu->items.empty() ? state.menu->items.size() - 1U : 0U)
            : *state.selected;
        state.selected = selectable_menu_item(*state.menu, start, backwards ? -1 : 1,
                                               endpoint || !state.selected);
        state.reveal_selection = true;
        return true;
    }
    if (key == Key::Enter || key == Key::Space || key == Key::Right) {
        if (key == Key::Right &&
            (!state.selected || *state.selected >= state.menu->items.size() ||
             !state.menu->items[*state.selected].has_submenu()) && switch_menu_bar(1))
            return true;
        if (state.selected) {
            const auto level = levels_.size() - 1U;
            (void)activate(level, *state.selected, key == Key::Right, {modifiers,{},physical_control});
            if (levels_.size() > level + 1U) (void)handle_key(Key::Down, modifiers, physical_control);
        }
        return true;
    }
    if (menu_mnemonic_key(key)) {
        const auto matches = menu_mnemonic_matches(*state.menu, key);
        if (!matches.empty()) {
            const auto next = state.selected ? std::ranges::find_if(matches, [&](std::size_t index) {
                return index > *state.selected;
            }) : matches.end();
            state.selected = next == matches.end() ? matches.front() : *next;
            state.reveal_selection = true;
            if (matches.size() == 1U) {
                const auto level = levels_.size() - 1U;
                (void)activate(level, *state.selected, false, {Modifiers::None,{},physical_control});
                if (levels_.size() > level + 1U) (void)handle_key(Key::Down, Modifiers::None, physical_control);
            }
        }
    }
    return true; // 開いているメニューがキーボード入力を管理し、背後のコントロールを編集しない。
}

KeyEvent parse_menu_shortcut(const std::string_view shortcut) {
    const auto lower = [](std::string_view text) {
        std::string result(text);
        for (auto& character : result)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return result;
    };
    const auto modifier = [&](const std::string_view text) {
        const auto value = lower(text);
        if (value == "ctrl" || value == "control") return Modifiers::Control;
        if (value == "shift") return Modifiers::Shift;
        if (value == "alt") return Modifiers::Alt;
        if (value == "command") return Modifiers::Meta;
        return Modifiers::None;
    };
    KeyEvent result;
    auto token = shortcut;
    while (true) {
        const auto plus = token.find('+');
        if (plus == std::string_view::npos || plus == token.size() - 1U) break;
        result.modifiers = result.modifiers | modifier(token.substr(0, plus));
        token.remove_prefix(plus + 1U);
    }
    const auto final_modifier = modifier(token);
    if (final_modifier != Modifiers::None) {
        result.modifiers = result.modifiers | final_modifier;
        return result;
    }
    const auto key = lower(token);
    if (token.size() == 1U) {
        result.printable = std::string(token);
        if (key[0] >= 'a' && key[0] <= 'z')
            result.key = static_cast<Key>(static_cast<int>(Key::A) + key[0] - 'a');
        else if (key[0] >= '0' && key[0] <= '9')
            result.key = static_cast<Key>(static_cast<int>(Key::Digit0) + key[0] - '0');
        else if (key[0] == ' ') result.key = Key::Space;
        return result;
    }
    if (key.starts_with('f')) {
        int number{};
        const auto parsed = std::from_chars(key.data()+1,key.data()+key.size(),number);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data()+key.size() &&
            number >= 1 && number <= 15 && key == "f"+std::to_string(number)) {
            result.key = static_cast<Key>(static_cast<int>(Key::F1)+number-1);
            return result;
        }
    }
    static constexpr std::pair<std::string_view,Key> names[]{
        {"backspace",Key::Backspace},{"enter",Key::Enter},{"return",Key::Enter},
        {"delete",Key::Delete},{"escape",Key::Escape},{"esc",Key::Escape},
        {"tab",Key::Tab},{"space",Key::Space},{"insert",Key::Insert},
        {"home",Key::Home},{"end",Key::End},{"page_up",Key::PageUp},{"pageup",Key::PageUp},
        {"page_down",Key::PageDown},{"pagedown",Key::PageDown},
        {"arrow_up",Key::Up},{"arrowup",Key::Up},{"up",Key::Up},
        {"arrow_down",Key::Down},{"arrowdown",Key::Down},{"down",Key::Down},
        {"arrow_left",Key::Left},{"arrowleft",Key::Left},{"left",Key::Left},
        {"arrow_right",Key::Right},{"arrowright",Key::Right},{"right",Key::Right}};
    for (const auto& [name,value] : names) if (key == name) { result.key=value; break; }
    return result;
}

bool menu_shortcut_matches(const std::string_view shortcut, const Key key,
                            const Modifiers modifiers, const std::string_view printable) {
    if (shortcut.empty() || (key == Key::Unknown && printable.empty())) return false;
    const auto wanted = parse_menu_shortcut(shortcut);
    auto actual_key = key;
    auto actual_modifiers = modifiers;
    switch (key) {
    case Key::Control: actual_modifiers = modifiers | Modifiers::Control; actual_key=Key::Unknown; break;
    case Key::Shift: actual_modifiers = modifiers | Modifiers::Shift; actual_key=Key::Unknown; break;
    case Key::Alt: actual_modifiers = modifiers | Modifiers::Alt; actual_key=Key::Unknown; break;
    case Key::Meta: actual_modifiers = modifiers | Modifiers::Meta; actual_key=Key::Unknown; break;
    default: break;
    }
    const auto mask = static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Shift |
                                                Modifiers::Alt | Modifiers::Meta);
    if ((static_cast<std::uint32_t>(actual_modifiers) & mask) !=
        static_cast<std::uint32_t>(wanted.modifiers)) return false;
    // 句読点には専用の Key 列挙値がない。 Unknown に正規化されたControl
    // ・Shiftのイベントではなく、実際の印字可能キーと照合する。
    if (wanted.key == Key::Unknown && !wanted.printable.empty())
        return printable == wanted.printable;
    if (wanted.key == Key::Unknown) {
        const bool modifier_key = key == Key::Control || key == Key::Shift ||
            key == Key::Alt || key == Key::Meta;
        return modifier_key && wanted.modifiers != Modifiers::None;
    }
    return actual_key == wanted.key;
}

bool dispatch_menu_shortcut(const std::shared_ptr<Menu>& menu, const Key key,
                             const Modifiers modifiers, const std::string_view printable,
                             const std::optional<bool> physical_control) {
    if (!menu || !menu->enabled() || (key == Key::Unknown && printable.empty())) return false;
    const auto find = [&](const auto& self, const std::vector<MenuItem>& items,
                          const bool require_enabled) -> std::optional<MenuItem> {
        for (const auto& item : items) {
            if (item.separator || (require_enabled && !item.enabled)) continue;
            if (menu_shortcut_matches(menu_item_shortcut(item), key, modifiers, printable)) return item;
            if (const auto child = self(self, item.children, require_enabled)) return child;
        }
        return {};
    };
    if (!find(find, menu->items, false)) return false;
    // 更新でメニューツリー全体が置き換わる場合があるため、項目・参照をま
    // たいで保持しない。 選択した操作も、UIの変更を許す前に複製する。
    if (const auto callback=menu->on_open) callback();
    if (!menu->enabled()) return false;
    const auto item = find(find, menu->items, true);
    return item && activate_menu_item(*item, menu->on_command,
                                      menu->on_command_with_gesture,
                                      {modifiers, {}, physical_control, modifiers, true});
}

bool dispatch_menu_shortcuts(const WidgetPtr& root, const Key key, const Modifiers modifiers,
                             const std::string_view printable,
                             const std::optional<bool> physical_control) {
    if (key == Key::Unknown && printable.empty()) return false;
    std::vector<std::shared_ptr<Menu>> menus;
    (void)visit_menu_scope(root, [&](const WidgetPtr& widget) {
        if (const auto menu = std::dynamic_pointer_cast<Menu>(widget)) menus.push_back(menu);
        return false;
    });
    // コマンドがUIを置き換え・終了する場合があるため、先に対象メニューを複製する。
    for (const auto& menu : menus)
        if (dispatch_menu_shortcut(menu, key, modifiers, printable, physical_control)) return true;
    return false;
}

namespace {
void prepare_tab_header_viewport(TabLayoutSnapshot& tabs) {
    auto& state = tabs.header_viewport;
    auto geometry = layout_tab_headers(tabs);
    const bool changed = state.revealed_selection != tabs.selected_index ||
        state.measured_widths != geometry.natural_widths ||
        state.available_width != geometry.content.width;
    state.measured_widths = std::move(geometry.natural_widths);
    state.available_width = geometry.content.width;
    state.revealed_selection = tabs.selected_index;
    if (tabs.pages.empty()) { state.first = 0U; return; }
    state.first = std::min(state.first, tabs.pages.size() - 1U);
    if (geometry.previous.width == 0.0F) { state.first = 0U; return; }
    if (!changed || !tabs.selected_index || *tabs.selected_index >= tabs.pages.size()) return;
    const auto selected = *tabs.selected_index;
    if (selected < state.first) state.first = selected;
    double required{};
    for (auto i = state.first; i <= selected; ++i) required += state.measured_widths[i];
    while (state.first < selected && required > state.available_width + .01F)
        required -= state.measured_widths[state.first++];
}
}

bool update_tab_header_viewport(TabView& tabs) {
    ++tabs.header_update_revision;
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepare_tab_header_viewport(prepared);
    if(!tab_layout_is_current(before,tabs)) return false;
    tabs.header_viewport=std::move(prepared.header_viewport);
    return true;
}

TabHeaderViewport resolved_tab_header_viewport(const TabLayoutSnapshot& snapshot) {
    auto prepared=snapshot;
    prepare_tab_header_viewport(prepared);
    return std::move(prepared.header_viewport);
}

bool scroll_tab_headers(TabView& tabs, int direction) {
    if (!tabs.enabled() || !tabs.visible() || direction == 0) return false;
    ++tabs.header_update_revision;
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepare_tab_header_viewport(prepared);
    const auto layout=layout_tab_headers(prepared);
    if(!tab_layout_is_current(before,tabs)) return false;
    bool changed=false;
    if(direction<0 && layout.can_previous) { --prepared.header_viewport.first; changed=true; }
    if(direction>0 && layout.can_next) { ++prepared.header_viewport.first; changed=true; }
    tabs.header_viewport=std::move(prepared.header_viewport);
    return changed;
}

namespace {
enum class TabHeaderHitKind { None, Previous, Next, Page, Close };
struct TabHeaderHit {
    TabHeaderHitKind kind{TabHeaderHitKind::None};
    std::optional<std::size_t> index;
};

[[nodiscard]] TabHeaderHit tab_header_hit(const TabHeaderLayout& layout,
                                          const Point point) {
    const auto contains = [point](const Rect r) {
        return point.x >= r.x && point.x < r.x + r.width &&
               point.y >= r.y && point.y < r.y + r.height;
    };
    if (contains(layout.previous)) return {TabHeaderHitKind::Previous, {}};
    if (contains(layout.next)) return {TabHeaderHitKind::Next, {}};
    for (const auto& item : layout.items) {
        if (!contains(item.bounds)) continue;
        return {contains(item.close) ? TabHeaderHitKind::Close
                                     : TabHeaderHitKind::Page,
                item.index};
    }
    return {};
}
} // namespace

bool activate_tab_header(TabView& tabs, Point point) {
    if (!tabs.enabled() || !tabs.visible()) return false;
    ++tabs.header_update_revision;
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepare_tab_header_viewport(prepared);
    const auto layout=layout_tab_headers(prepared);
    if(!tab_layout_is_current(before,tabs)) return false;
    const auto hit = tab_header_hit(layout, point);
    if(hit.kind == TabHeaderHitKind::Previous ||
       hit.kind == TabHeaderHitKind::Next) {
        const bool previous=hit.kind == TabHeaderHitKind::Previous;
        const bool changed=previous?layout.can_previous:layout.can_next;
        if(changed) {
            if(previous) --prepared.header_viewport.first;
            else ++prepared.header_viewport.first;
        }
        tabs.header_viewport=std::move(prepared.header_viewport);
        return changed;
    }
    if (hit.index) {
        const auto item = std::find_if(layout.items.begin(), layout.items.end(),
            [&](const auto& value) { return value.index == *hit.index; });
        if (item == layout.items.end()) return false;
        if(hit.kind == TabHeaderHitKind::Close) {
            tabs.header_viewport=std::move(prepared.header_viewport);
            return request_close_tab_page(tabs,item->index);
        }
        if(prepared.selected_index!=item->index) return select_tab_page(tabs,item->index);
        const auto first=prepared.header_viewport.first;
        prepared.header_viewport.revealed_selection.reset();
        prepare_tab_header_viewport(prepared);
        if(!tab_layout_is_current(before,tabs)) return false;
        const bool changed=first!=prepared.header_viewport.first;
        tabs.header_viewport=std::move(prepared.header_viewport);
        const auto activated = tabs.on_selected_page_activated;
        // D版と同様、選択済みタブでも選択リスナーが動く。
        // フォーカス要求は、選択変更の購読先とは分ける。
        if (activated) activated(item->index);
        return changed;
    }
    tabs.header_viewport=std::move(prepared.header_viewport);
    return false;
}

bool select_tab_page(TabView& tabs, std::optional<std::size_t> index,
                     const bool notify) {
    if(index && *index>=tabs.pages.size()) index.reset();
    // 選択が同じでも、以前の実行中の要求を置き換える。
    ++tabs.header_update_revision;
    if(tabs.selected_index==index) return false;
    const auto callback=tabs.on_selection_changed;
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepared.selected_index=index;
    prepare_tab_header_viewport(prepared);
    if(!tab_layout_is_current(before,tabs)) return false;
    tabs.selected_index=index;
    tabs.header_viewport=std::move(prepared.header_viewport);
    if(notify && callback) callback(index);
    return true;
}

bool navigate_tab_page(TabView& tabs, const TabNavigation navigation,
                       const bool wrap) {
    if (tabs.pages.empty()) return false;
    std::size_t target{};
    switch (navigation) {
    case TabNavigation::First:
        target = 0U;
        break;
    case TabNavigation::Last:
        target = tabs.pages.size() - 1U;
        break;
    case TabNavigation::Previous:
        if (!tabs.selected_index || *tabs.selected_index >= tabs.pages.size())
            target = tabs.pages.size() - 1U;
        else if (*tabs.selected_index > 0U)
            target = *tabs.selected_index - 1U;
        else
            target = wrap ? tabs.pages.size() - 1U : 0U;
        break;
    case TabNavigation::Next:
        if (!tabs.selected_index || *tabs.selected_index >= tabs.pages.size())
            target = 0U;
        else if (*tabs.selected_index + 1U < tabs.pages.size())
            target = *tabs.selected_index + 1U;
        else
            target = wrap ? 0U : tabs.pages.size() - 1U;
        break;
    }
    return select_tab_page(tabs, target);
}

bool set_expand_state(ExpandBar& expand, const std::size_t index,
                      const bool expanded) {
    if (index >= expand.children().size() || !expand.children()[index]) return false;
    if (expand_item_is_open(expand,index) == expanded) return false;
    auto state=expand.expanded;
    const auto notify=expand.on_expansion_changed;
    if (index >= state.size()) state.resize(index+1U,true);
    state[index]=expanded;
    expand.expanded.swap(state);
    ++expand.expansion_update_revision;
    // 所有したコールバックを呼ぶ。 この ExpandBar を閉じる・破棄する場合があるため。
    if (notify) notify(index,expanded);
    return true;
}

bool expand_item_is_open(const ExpandBar& expand,
                         const std::size_t index) noexcept {
    return index >= expand.expanded.size() || expand.expanded[index];
}

Rect expand_item_header_bounds(const Widget& child) noexcept {
    auto header=child.bounds();
    header.height=std::max(0.0F,std::min(header.height,expand_item_header_height(child)));
    return header;
}

bool activate_expand_header(ExpandBar& expand, Point point) {
    if (!expand.enabled() || !expand.visible() || !point_inside(expand.bounds(), point)) return false;
    const auto lifetime=expand.operation_lifetime();
    const auto revision=++expand.expansion_update_revision;
    const auto scale_generation=editor_ui_text_scale_generation();
    const auto bounds=expand.bounds();
    const auto states=expand.expanded;
    const auto children = expand.children();
    struct HeaderInput {
        Rect bounds;
        std::optional<FontSpec> font;
        bool visible{},enabled{};
        ExpandItemMeasurement metrics;
    };
    std::vector<HeaderInput> inputs;
    inputs.reserve(children.size());
    for (const auto& child:children) {
        if (!child) {inputs.push_back({}); continue;}
        inputs.push_back({child->bounds(),child->font(),child->visible(),child->enabled(),
            prepare_expand_item_measurement(*child)});
    }
    const auto same_number=[](float a,float b) {return a==b || (std::isnan(a)&&std::isnan(b));};
    const auto same_rect=[&](Rect a,Rect b) {
        return same_number(a.x,b.x)&&same_number(a.y,b.y)&&
            same_number(a.width,b.width)&&same_number(a.height,b.height);
    };
    const auto current=[&] {
        // 参照に触れる前に寿命を確認する。 同じアドレスで
        // の置換や、複製したコントロールの代入も考慮する。
        if (lifetime.expired()) return false;
        const auto owner=expand.operation_lifetime();
        if (lifetime.owner_before(owner)||owner.owner_before(lifetime)) return false;
        if (revision!=expand.expansion_update_revision || scale_generation!=editor_ui_text_scale_generation() ||
            !expand.enabled() || !expand.visible() || !same_rect(bounds,expand.bounds()) ||
            children!=expand.children() || states!=expand.expanded) return false;
        for (std::size_t i=0;i<children.size();++i) {
            const auto& child=children[i]; if (!child) continue;
            const auto& before=inputs[i]; const auto& font=child->font();
            if (!same_rect(before.bounds,child->bounds()) || before.visible!=child->visible() ||
                before.enabled!=child->enabled() || before.font.has_value()!=font.has_value()) return false;
            if (font) {
                const auto& a=*before.font; const auto& b=*font;
                if (a.family!=b.family || !same_number(a.points,b.points) || a.weight!=b.weight ||
                    a.italic!=b.italic || a.underline!=b.underline || a.strikeout!=b.strikeout ||
                    a.size_mode!=b.size_mode) return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& child = children[i];
        if (!child || !inputs[i].visible || !inputs[i].enabled ||
            std::dynamic_pointer_cast<Window>(child)) continue;
        auto header=inputs[i].bounds;
        header.height=std::max(0.0F,std::min(header.height,measure_expand_item(inputs[i].metrics).header_height));
        if (!current()) return false;
        if (point_inside(header, point))
            return set_expand_state(expand, i, !(i>=states.size() || states[i]));
    }
    return false;
}

bool expand_child_is_open(const ExpandBar& expand,
                          const Widget* child) noexcept {
    if (!child) return true;
    const auto& children = expand.children();
    const auto found = std::ranges::find_if(
        children, [child](const WidgetPtr& candidate) {
            return candidate.get() == child;
        });
    return found == children.end() ||
           expand_item_is_open(
               expand, static_cast<std::size_t>(found - children.begin()));
}

bool widget_is_visible_in(const WidgetPtr& root, const Widget* target) {
    if (!root || !root->visible()) return false;
    if (root.get() == target) return true;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(root)) {
        return tabs->selected_index && *tabs->selected_index < tabs->pages.size() &&
            widget_is_visible_in(tabs->pages[*tabs->selected_index].content, target);
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(root))
        for (const auto& child : container->children()) {
            const auto expand = std::dynamic_pointer_cast<ExpandBar>(root);
            if ((!expand || expand_child_is_open(*expand, child.get())) &&
                widget_is_visible_in(child, target)) return true;
        }
    return false;
}

bool move_cool_bar_item(CoolBar& cool_bar, const std::size_t from,
                        const std::size_t to) {
    const auto callback = cool_bar.on_item_moved;
    if (cool_bar.locked || !cool_bar.move_child(from, to)) return false;
    if (callback) callback(from, to);
    return true;
}

std::optional<FrameDragHit> hit_test_frame_drag(const WidgetPtr& root, const Point point) {
    if (!root || !root->visible() || !root->enabled() ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !point_inside(root->bounds(), point)) return std::nullopt;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(root)) {
        const auto snapshot=snapshot_tab_layout(*tabs);
        const auto content=layout_tab_content(snapshot);
        if (!tab_layout_is_current(snapshot,*tabs)) return std::nullopt;
        if (point_inside(content,point) && snapshot.selected_index &&
            *snapshot.selected_index<snapshot.pages.size()) {
            const auto hit=hit_test_frame_drag(snapshot.pages[*snapshot.selected_index].content,point);
            if (tab_layout_is_current(snapshot,*tabs)) return hit;
        }
        return std::nullopt;
    }
    if (const auto bar = std::dynamic_pointer_cast<CoolBar>(root))
        if (const auto index = cool_bar_item_at(*bar, point, true))
            return FrameDragHit{bar, *index, CursorKind::Hand};
    if (const auto container = std::dynamic_pointer_cast<Container>(root)) {
        const auto expand = std::dynamic_pointer_cast<ExpandBar>(root);
        for (auto it = container->children().rbegin(); it != container->children().rend(); ++it) {
            if (std::dynamic_pointer_cast<Window>(*it)) continue;
            if (expand && (!expand_child_is_open(*expand, it->get()) ||
                (*it && point_inside(expand_item_header_bounds(**it), point)))) continue;
            if (const auto hit = hit_test_frame_drag(*it, point)) return hit;
        }
    }
    if (const auto split = std::dynamic_pointer_cast<SplitView>(root))
        // 仕切りの細い表示は維持し、分割方向の両側
        // へ各8指定画素広げてつかめるようにする。
        if (const auto hit = split_sash_at(*split, point.x, point.y, scaled_ui_metric(8)))
            return FrameDragHit{split, hit->index, split->orientation == Orientation::Horizontal
                ? CursorKind::SizeHorizontal : CursorKind::SizeVertical};
    return std::nullopt;
}

void FrameDragStateMachine::reset() {
    ++generation_;
    root_.reset(); owner_.reset(); children_.clear(); weights_.clear();
    cursor_ = CursorKind::Default;
    changed_ = false;
}

bool FrameDragStateMachine::valid() const {
    const auto current = owner();
    if (!current || !current->visible() || !current->enabled() || scale_ != editor_ui_text_scale()) return false;
    const auto bounds = current->bounds();
    if (bounds.x != bounds_.x || bounds.y != bounds_.y ||
        bounds.width != bounds_.width || bounds.height != bounds_.height) return false;
    if (!visit_menu_scope(root_.lock(), [&](const WidgetPtr& child) { return child == current; })) return false;
    const auto& children = current->children();
    if (children.size() != children_.size()) return false;
    for (std::size_t i = 0; i < children.size(); ++i)
        if (children_[i].first.lock() != children[i] ||
            children_[i].second != (children[i] && children[i]->visible())) return false;
    if (const auto split = std::dynamic_pointer_cast<SplitView>(current))
        return split->orientation == orientation_ && split->weights == weights_;
    const auto bar = std::dynamic_pointer_cast<CoolBar>(current);
    return bar && !bar->locked && index_ < children.size() && children[index_] && children[index_]->visible();
}

bool FrameDragStateMachine::begin(const WidgetPtr& root, const Point point) {
    reset();
    const auto hit = hit_test_frame_drag(root, point);
    if (!hit) return false;
    root_ = root; owner_ = hit->owner; index_ = hit->index; cursor_ = hit->cursor;
    bounds_ = hit->owner->bounds(); scale_ = editor_ui_text_scale(); last_point_ = point;
    for (const auto& child : hit->owner->children()) children_.emplace_back(child, child && child->visible());
    if (const auto split = std::dynamic_pointer_cast<SplitView>(hit->owner)) {
        weights_ = split->weights; orientation_ = split->orientation;
        std::vector<WidgetPtr> visible;
        for (const auto& child : split->children()) if (child && child->visible()) visible.push_back(child);
        const auto first = visible[index_]->bounds();
        // 仕切りの中心ではなく、実際につかんだ仕切り内の位置を保持する。
        grab_offset_ = orientation_ == Orientation::Horizontal
            ? point.x - first.x - first.width : point.y - first.y - first.height;
    }
    return true;
}

bool FrameDragStateMachine::move(const Point point) {
    if (!valid() || !std::isfinite(point.x) || !std::isfinite(point.y)) { reset(); return false; }
    if (point.x == last_point_.x && point.y == last_point_.y) return false;
    const auto previous = last_point_;
    last_point_ = point;
    if (const auto split = std::dynamic_pointer_cast<SplitView>(owner())) {
        if (orientation_ == Orientation::Horizontal ? point.x == previous.x : point.y == previous.y) return false;
        const float coordinate = (orientation_ == Orientation::Horizontal ? point.x : point.y) - grab_offset_;
        const bool changed = resize_split_pair_at(
            *split, index_, coordinate,
            scaled_ui_metric(split->drag_minimum_first),
            scaled_ui_metric(split->drag_minimum_second),
            SplitResizePhase::Preview);
        weights_ = split->weights;
        changed_ |= changed;
        return changed;
    }
    const auto bar = std::dynamic_pointer_cast<CoolBar>(owner());
    if (!point_inside(bar->bounds(), point)) return false;
    const auto target = cool_bar_item_at(*bar, point);
    if (!target || *target == index_) return false;
    const auto generation = generation_;
    const auto from = index_;
    const auto child = children_[from];
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(from));
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(*target), child);
    index_ = *target;
    const bool changed = move_cool_bar_item(*bar, from, *target);
    // on_item_moved が、バーを再構築したり新しい操作を始めたりする場合がある。
    if (generation_ == generation && (!changed || !valid())) reset();
    return changed;
}

bool FrameDragStateMachine::finish(const Point point) {
    const auto generation = generation_;
    const bool moved = move(point);
    if (generation_ != generation) return moved;
    const auto split = std::dynamic_pointer_cast<SplitView>(owner());
    const bool commit = split && changed_ && valid();
    reset(); // コールバックが次のジェスチャーを開始する場合がある。
    if (commit) commit_split_resize(*split);
    return moved || commit;
}

bool activate_menu_item(
    const MenuItem& item,
    const std::function<void(std::string_view)>& on_command,
    const std::function<void(std::string_view, CommandGesture)>& on_command_with_gesture,
    const CommandGesture gesture) {
    if (!item.enabled || item.separator || item.has_submenu()) return false;
    const auto item_action_with_gesture = item.action_with_gesture;
    const auto item_action = item.action;
    const auto command_with_gesture = on_command_with_gesture;
    const auto command_action = on_command;
    const auto command = item.command;
    if (item_action_with_gesture) {
        item_action_with_gesture(gesture);
        return true;
    }
    if (item_action) {
        item_action();
        return true;
    }
    if (command.empty()) return false;
    if (command_with_gesture) command_with_gesture(command, gesture);
    else if (command_action) command_action(command);
    else return false;
    return true;
}

bool activate_tool_item(ToolBar& toolbar, const std::size_t index,
                         const CommandGesture gesture) {
    if (index >= toolbar.items.size()) return false;
    const auto& item = toolbar.items[index];
    if (!item.enabled || item.separator ||
        (!toolbar.on_command_with_gesture && !toolbar.on_command) || item.command.empty())
        return false;
    // ポインターによる有効化では、次のSpaceキーが使うフォーカス・現在
    // 項目も更新する。 コールバックがバーを再構築・無効化する前に行う。
    if (toolbar.keyboard_navigation) toolbar.keyboard_item_index = index;
    const auto command = item.command;
    const auto with_gesture = toolbar.on_command_with_gesture;
    const auto callback = toolbar.on_command;
    if (with_gesture) with_gesture(command, gesture);
    else callback(command);
    return true;
}

namespace {
bool keyboard_tool_item(const ToolBar& toolbar, const std::size_t index) {
    if (!toolbar.keyboard_navigation || index >= toolbar.items.size() ||
        (!toolbar.on_command_with_gesture && !toolbar.on_command)) return false;
    const auto& item = toolbar.items[index];
    return item.enabled && !item.separator && !item.command.empty();
}
std::optional<std::size_t> first_keyboard_tool_item(const ToolBar& toolbar) {
    for (std::size_t index = 0; index < toolbar.items.size(); ++index)
        if (keyboard_tool_item(toolbar, index)) return index;
    return {};
}
bool has_keyboard_tool_item(const ToolBar& toolbar) {
    return first_keyboard_tool_item(toolbar).has_value();
}
std::optional<std::size_t> normalize_keyboard_tool_item(ToolBar& toolbar) {
    if (toolbar.keyboard_item_index &&
        keyboard_tool_item(toolbar, *toolbar.keyboard_item_index))
        return toolbar.keyboard_item_index;
    toolbar.keyboard_item_index = first_keyboard_tool_item(toolbar);
    return toolbar.keyboard_item_index;
}
}

bool handle_tool_bar_key(ToolBar& toolbar, const Key key,
                         const Modifiers modifiers) {
    if (!toolbar.keyboard_navigation || !toolbar.visible() || !toolbar.enabled())
        return false;
    if (key == Key::Space) {
        // Win32のDWTと同様、VK_SPACEを消費し、対応するWM_CHAR
        // が空白なら現在項目を有効化する。 Shift+Spaceも空白になるが、
        // Control/Alt/Metaとの組み合わせは通常の空白操作ではない。
        if (modifiers != Modifiers::None && modifiers != Modifiers::Shift) return false;
        const auto current = normalize_keyboard_tool_item(toolbar);
        return current && activate_tool_item(toolbar, *current);
    }
    if (modifiers != Modifiers::None) return false;
    const bool previous = toolbar.keyboard_vertical ? key == Key::Up : key == Key::Left;
    const bool next = toolbar.keyboard_vertical ? key == Key::Down : key == Key::Right;
    if (!previous && !next) return false;
    const auto current = normalize_keyboard_tool_item(toolbar);
    if (!current) return false;
    if (previous) {
        auto index = *current;
        while (index > 0U) {
            --index;
            if (keyboard_tool_item(toolbar, index)) {
                toolbar.keyboard_item_index = index;
                break;
            }
        }
    } else {
        auto index = *current;
        while (++index < toolbar.items.size()) {
            if (keyboard_tool_item(toolbar, index)) {
                toolbar.keyboard_item_index = index;
                break;
            }
        }
    }
    // 軸方向の矢印は両端でも消費し、複合部品の外へフォーカスを移
    // さない。 端・循環の厳密な動作はネイティブの版に依存する。
    return true;
}

bool activate_button(Button& button) {
    if (!button.enabled() || !button.on_click) return false;
    const auto callback=button.on_click;
    callback();
    return true;
}

bool activate_button_control(const WidgetPtr& root,
                             const std::shared_ptr<Button>& button) {
    if (!widget_in_input_scope(root,button)) return false;
    if (const auto radio = std::dynamic_pointer_cast<RadioButton>(button)) {
        (void)select_radio_button(*radio, radio_button_group(root, *radio));
    } else if (const auto toggle =
                   std::dynamic_pointer_cast<ToggleButton>(button)) {
        const auto check=std::dynamic_pointer_cast<CheckBox>(toggle);
        (void)set_toggle_checked(*toggle, (check && check->indeterminate) || !toggle->checked);
    }
    // 変更コールバックが、コントロールを除去・非表示にしたりウィンドウを無効に
    // したりする場合がある。 二つ目のクリック通知は、その古いUIへ送らない。
    if (widget_in_input_scope(root,button)) (void)activate_button(*button);
    return true;
}

bool activate_text_enter(TextEdit& edit) {
    if (!edit.enabled() || edit.multiline || !edit.on_enter) return false;
    const auto callback = edit.on_enter;
    callback();
    return true;
}

bool activate_link(Link& link) {
    if (!link.enabled() || !link.on_activate) return false;
    const auto callback=link.on_activate;
    callback();
    return true;
}

bool activate_list_item(ListView& list, const std::size_t index,
                        const Modifiers modifiers) {
    if (!list.enabled() || index >= list.items.size() ||
        !list.items[index].enabled ||
        (!list.on_activate_with_modifiers && !list.on_activate))
        return false;
    const auto with_modifiers = list.on_activate_with_modifiers;
    const auto callback = list.on_activate;
    if (with_modifiers) with_modifiers(index,modifiers);
    else callback(index);
    return true;
}

std::optional<std::size_t> hit_test_tab_page(const TabView& tabs,Point point) {
    if (!tabs.enabled() || !tabs.visible()) return {};
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepare_tab_header_viewport(prepared);
    const auto layout=layout_tab_headers(prepared);
    if (!tab_layout_is_current(before,tabs)) return {};
    const auto hit=tab_header_hit(layout,point);
    return hit.kind==TabHeaderHitKind::Page || hit.kind==TabHeaderHitKind::Close
        ? hit.index : std::optional<std::size_t>{};
}

std::optional<std::size_t> hit_test_tab_close_page(const TabView& tabs,
                                                   const Point point) {
    if (!tabs.enabled() || !tabs.visible()) return {};
    const auto before=snapshot_tab_layout(tabs);
    auto prepared=before;
    prepare_tab_header_viewport(prepared);
    const auto layout=layout_tab_headers(prepared);
    if (!tab_layout_is_current(before,tabs)) return {};
    const auto hit=tab_header_hit(layout,point);
    return hit.kind==TabHeaderHitKind::Close ? hit.index
                                             : std::optional<std::size_t>{};
}

bool activate_table_cell(TableView& table, const std::size_t row,
                         const std::size_t column,
                         const Modifiers modifiers) {
    if (!table.enabled() || row >= table.rows.size() ||
        column >= table.rows[row].cells.size() ||
        !table.rows[row].cells[column].enabled ||
        (!table.on_cell_activate_with_modifiers && !table.on_cell_activate))
        return false;
    const auto with_modifiers = table.on_cell_activate_with_modifiers;
    const auto callback = table.on_cell_activate;
    if (with_modifiers) with_modifiers(row,column,modifiers);
    else callback(row, column);
    return true;
}

bool request_table_sort(TableView& table, const std::size_t column) {
    if (!table.enabled() || column >= table.columns.size() ||
        !table.columns[column].sortable || !table.on_sort_requested)
        return false;
    const auto callback = table.on_sort_requested;
    callback(column);
    return true;
}

bool activate_tree_item(TreeView& tree, const TreeItemId id,
                        const Modifiers modifiers) {
    if (!tree.enabled() ||
        std::ranges::find(tree.nodes, id, &TreeNode::id) == tree.nodes.end() ||
        (!tree.on_activate_with_modifiers && !tree.on_activate))
        return false;
    const auto with_modifiers = tree.on_activate_with_modifiers;
    const auto callback = tree.on_activate;
    if (with_modifiers) with_modifiers(id,modifiers);
    else callback(id);
    return true;
}

bool request_close_tab_page(TabView& tabs, const std::size_t index) {
    if (!tabs.enabled() || index >= tabs.pages.size() ||
        !tabs.pages[index].closable || !tabs.on_close_page)
        return false;
    const auto callback=tabs.on_close_page;
    callback(index);
    return true;
}

namespace {

template <class Value>
void normalize_unique(std::vector<Value>& values) {
    std::ranges::sort(values);
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <class Value, class Valid>
bool replace_selection(std::vector<Value>& current, std::vector<Value> next,
                       Valid valid) {
    std::erase_if(next, [&valid](const Value value) { return !valid(value); });
    normalize_unique(next);
    if (current == next) return false;
    current = std::move(next);
    return true;
}

[[nodiscard]] bool navigation_moves_backward(
    const SelectionNavigation navigation) noexcept {
    return navigation == SelectionNavigation::Previous ||
           navigation == SelectionNavigation::PagePrevious ||
           navigation == SelectionNavigation::Last;
}

template <class Enabled>
std::optional<std::size_t> navigation_target(
    const std::vector<std::size_t>& selection,
    const std::optional<std::size_t> cursor, const std::size_t item_count,
    const SelectionNavigation navigation, const std::size_t visible_count,
    Enabled enabled) {
    if (item_count == 0U) return std::nullopt;
    const bool backward = navigation_moves_backward(navigation);
    const bool has_current = (cursor && *cursor < item_count) || !selection.empty();
    std::size_t current = backward ? item_count : 0U;
    if (cursor && *cursor < item_count) {
        current = *cursor;
    } else if (!selection.empty()) {
        current = backward
                      ? *std::min_element(selection.begin(), selection.end())
                      : *std::max_element(selection.begin(), selection.end());
        current = std::min(current, item_count - 1U);
    }

    std::size_t candidate{};
    switch (navigation) {
    case SelectionNavigation::First:
        candidate = 0U;
        break;
    case SelectionNavigation::Last:
        candidate = item_count - 1U;
        break;
    case SelectionNavigation::Previous:
        candidate = current == item_count
                        ? item_count - 1U
                        : current > 0U ? current - 1U : 0U;
        break;
    case SelectionNavigation::Next:
        candidate = !has_current ? 0U
                                      : std::min(item_count - 1U, current + 1U);
        break;
    case SelectionNavigation::PagePrevious:
        candidate = current == item_count
                        ? item_count - 1U
                        : current > std::max<std::size_t>(1U, visible_count)
                              ? current - std::max<std::size_t>(1U, visible_count)
                              : 0U;
        break;
    case SelectionNavigation::PageNext:
        candidate = !has_current
                        ? 0U
                        : std::min(item_count - 1U,
                                   current + std::max<std::size_t>(1U,
                                                                   visible_count));
        break;
    }

    if (enabled(candidate)) return candidate;
    if (backward) {
        for (std::size_t index = candidate; index > 0U; --index)
            if (enabled(index - 1U)) return index - 1U;
        for (std::size_t index = candidate + 1U; index < item_count; ++index)
            if (enabled(index)) return index;
    } else {
        for (std::size_t index = candidate + 1U; index < item_count; ++index)
            if (enabled(index)) return index;
        for (std::size_t index = candidate; index > 0U; --index)
            if (enabled(index - 1U)) return index - 1U;
    }
    return std::nullopt;
}

bool reveal_selection(std::size_t& top_index, const std::size_t selected,
                      const std::size_t item_count,
                      const std::size_t visible_count) noexcept {
    const std::size_t visible = std::max<std::size_t>(1U, visible_count);
    const std::size_t previous = top_index;
    top_index = clamp_top_index(top_index, item_count, visible);
    if (selected < top_index) top_index = selected;
    else if (selected >= top_index + visible)
        top_index = selected - visible + 1U;
    top_index = clamp_top_index(top_index, item_count, visible);
    return previous != top_index;
}

}  // namespace

bool update_index_selection(std::vector<std::size_t>& selection,
                            const std::size_t index,
                            const std::size_t item_count,
                            const bool multi_select,
                            const SelectionInput input) {
    std::optional<std::size_t> anchor;
    if (!selection.empty()) anchor = selection.back();
    return update_index_selection(selection, anchor, index, item_count,
                                  multi_select, input);
}

bool update_index_selection(std::vector<std::size_t>& selection,
                            std::optional<std::size_t>& anchor,
                            const std::size_t index,
                            const std::size_t item_count,
                            const bool multi_select,
                            const SelectionInput input) {
    if (index >= item_count) return false;
    auto next = selection;
    std::erase_if(next, [item_count](const std::size_t value) {
        return value >= item_count;
    });
    normalize_unique(next);
    const auto previous_anchor = anchor;
    if (!multi_select) {
        next = {index};
        anchor = index;
    } else if (input.toggle && !input.extend) {
        const auto found = std::ranges::find(next, index);
        if (found == next.end()) next.push_back(index);
        else next.erase(found);
        anchor = index;
    } else if (input.extend) {
        if (!anchor || *anchor >= item_count)
            anchor = next.empty() ? index : next.back();
        if (!input.toggle) next.clear();
        const std::size_t low = std::min(*anchor, index);
        const std::size_t high = std::max(*anchor, index);
        next.reserve(high - low + 1U);
        for (std::size_t value = low; value <= high; ++value)
            next.push_back(value);
    } else {
        next = {index};
        anchor = index;
    }
    normalize_unique(next);
    if (selection == next && previous_anchor == anchor) return false;
    selection = std::move(next);
    return true;
}

bool update_list_selection(ListView& list, const std::size_t index,
                           const SelectionInput input) {
    auto next = list.selection;
    auto anchor = list.selection_anchor;
    if (index >= list.items.size() || !list.items[index].enabled) return false;
    const bool selection_changed = update_index_selection(next, anchor, index, list.items.size(),
                                list.multi_select, input);
    if (!selection_changed && list.selection_cursor == index)
        return false;
    const bool cursor_changed = list.selection_cursor != index;
    list.selection_anchor = anchor;
    list.selection_cursor = index;
    return select_list_rows(list, std::move(next), true) || cursor_changed;
}

bool navigate_list_selection(ListView& list,
                             const SelectionNavigation navigation,
                             const std::size_t visible_count,
                             const bool extend, const bool preserve_selection) {
    const auto target = navigation_target(
        list.selection, list.selection_cursor, list.items.size(), navigation,
        visible_count,
        [&list](const std::size_t index) {
            return index < list.items.size() && list.items[index].enabled;
        });
    if (!target) return false;
    const bool scrolled = reveal_selection(list.top_index, *target, list.items.size(), visible_count);
    if (preserve_selection && !extend && list.multi_select) {
        const bool changed = list.selection_cursor != *target;
        list.selection_cursor = *target;
        return changed || scrolled;
    }
    return update_list_selection(list, *target, {preserve_selection, extend}) || scrolled;
}

bool select_list_rows(ListView& list, std::vector<std::size_t> selection,
                      const bool preserve_focus, const bool notify) {
    auto next = list.selection;
    const bool changed = replace_selection(
        next, std::move(selection), [&list](const std::size_t index) {
            return index < list.items.size() && list.items[index].enabled;
        });
    // 通知前にコールバックを所有して保持する。 リスナーが自身を置換した
    // り選択を読み直したり、モデル全体を破棄したりする場合があるため。
    const auto callback = changed ? list.on_selection_changed : decltype(list.on_selection_changed){};
    const auto notified = callback ? next : std::vector<std::size_t>{};
    list.selection.swap(next);
    if (preserve_focus) {
        if (list.selection_anchor && *list.selection_anchor >= list.items.size()) list.selection_anchor.reset();
        if (list.selection_cursor && (*list.selection_cursor >= list.items.size() ||
            !list.items[*list.selection_cursor].enabled)) list.selection_cursor.reset();
    } else {
    if (list.selection.empty()) list.selection_anchor.reset();
    else if (!list.selection_anchor ||
             std::ranges::find(list.selection, *list.selection_anchor) ==
                 list.selection.end())
        list.selection_anchor = list.selection.back();
    if (list.selection.empty()) list.selection_cursor.reset();
    else if (!list.selection_cursor ||
             std::ranges::find(list.selection, *list.selection_cursor) ==
                 list.selection.end())
        list.selection_cursor = list.selection.back();
    }
    if (notify && callback) callback(notified);
    return changed;
}

std::vector<std::shared_ptr<RadioButton>> radio_button_group(
    const WidgetPtr& root, const RadioButton& selected) {
    std::vector<std::shared_ptr<RadioButton>> result;
    std::vector<std::pair<std::shared_ptr<RadioButton>, const Widget*>> candidates;
    std::vector<const Widget*> visited;
    const auto visit = [&](const auto& self, const WidgetPtr& widget, const Widget* parent) -> void {
        if (!widget || std::ranges::find(visited, widget.get()) != visited.end())
            return;
        if (widget != root && std::dynamic_pointer_cast<Window>(widget)) return;
        visited.push_back(widget.get());
        if (const auto radio = std::dynamic_pointer_cast<RadioButton>(widget);
            radio && radio->group == selected.group)
            candidates.emplace_back(radio, parent);
        if (const auto tabs = std::dynamic_pointer_cast<TabView>(widget))
            for (const auto& page : tabs->pages) self(self, page.content, widget.get());
        if (const auto container = std::dynamic_pointer_cast<Container>(widget))
            for (const auto& child : container->children()) self(self, child, widget.get());
    };
    visit(visit, root, nullptr);
    const auto member = std::ranges::find_if(candidates, [&](const auto& entry) {
        return entry.first.get() == &selected;
    });
    if (member == candidates.end()) return result;
    for (const auto& [radio, parent] : candidates)
        if (!selected.group.empty() || parent == member->second) result.push_back(radio);
    return result;
}

bool set_list_item_checked(ListView& list, const std::size_t index,
                           const bool checked) {
    if (!list.checkable || index >= list.items.size() ||
        !list.items[index].enabled || list.items[index].checked == checked)
        return false;
    list.items[index].checked = checked;
    return true;
}

namespace {
std::optional<std::size_t> selected_table_position(const std::vector<std::size_t>& selection,
                                                 std::optional<std::size_t> position) {
    if (selection.empty()) return std::nullopt;
    if (position && std::ranges::find(selection, *position) != selection.end()) return position;
    return selection.back();
}
}

namespace {
bool same_table_number(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}

// スナップショットはデータであり、別の選択・スクロールの状態管理ではない。 提
// 供処理への再入に備えて古い行を保持し、row_order_revision
// を進めない直接のモデル変更も含め、 すべてのセル・配置の入力を比較する。
bool same_table_publication_state(const TableView& a, const TableView& b, bool compare_rows=true) {
    const auto x=a.bounds(), y=b.bounds();
    if (!same_table_number(x.x,y.x) || !same_table_number(x.y,y.y) ||
        !same_table_number(x.width,y.width) || !same_table_number(x.height,y.height) ||
        a.visible()!=b.visible() || a.enabled()!=b.enabled() ||
        a.row_update_revision!=b.row_update_revision || a.row_order_revision!=b.row_order_revision ||
        a.selection!=b.selection || a.selection_anchor!=b.selection_anchor ||
        a.selection_cursor!=b.selection_cursor || a.top_index!=b.top_index ||
        !same_table_number(a.horizontal_offset,b.horizontal_offset) ||
        a.multi_select!=b.multi_select || a.checkable!=b.checkable || a.check_selected_rows!=b.check_selected_rows ||
        a.full_row_selection!=b.full_row_selection || a.virtualized!=b.virtualized ||
        a.edit_on_double_click!=b.edit_on_double_click ||
        a.edit_on_single_click!=b.edit_on_single_click ||
        a.header_visible!=b.header_visible ||
        a.grid_lines!=b.grid_lines || a.highlighted_column!=b.highlighted_column ||
        !same_table_number(a.row_height,b.row_height) || !same_table_number(a.header_height,b.header_height) ||
        a.columns.size()!=b.columns.size() || (compare_rows && a.rows.size()!=b.rows.size()) ||
        a.font().has_value()!=b.font().has_value()) return false;
    if(a.font()) {
        const auto& f=*a.font(); const auto& g=*b.font();
        if(f.family!=g.family || !same_table_number(f.points,g.points) || f.weight!=g.weight ||
           f.italic!=g.italic || f.underline!=g.underline || f.strikeout!=g.strikeout ||
           f.size_mode!=g.size_mode) return false;
    }
    for(std::size_t i=0;i<a.columns.size();++i) {
        const auto& p=a.columns[i]; const auto& q=b.columns[i];
        if(p.title!=q.title || !same_table_number(p.width,q.width) || p.alignment!=q.alignment ||
           p.resizable!=q.resizable || p.sortable!=q.sortable || !same_table_number(p.stretch,q.stretch)) return false;
    }
    if(!compare_rows) return true;
    for(std::size_t i=0;i<a.rows.size();++i) {
        const auto& p=a.rows[i]; const auto& q=b.rows[i];
        if(p.checked!=q.checked || p.grayed!=q.grayed ||
           p.cells.size()!=q.cells.size()) return false;
        for(std::size_t j=0;j<p.cells.size();++j) {
            const auto& c=p.cells[j]; const auto& d=q.cells[j];
            if(c.text!=d.text || c.value!=d.value || c.icon!=d.icon || c.checked!=d.checked ||
               c.enabled!=d.enabled || c.editor!=d.editor || c.editor_choices!=d.editor_choices ||
               c.editor_value!=d.editor_value || c.editor_choice_values!=d.editor_choice_values ||
               !same_table_number(c.editor_minimum,d.editor_minimum) ||
               !same_table_number(c.editor_maximum,d.editor_maximum) ||
               c.editor_step.has_value()!=d.editor_step.has_value() ||
               (c.editor_step && !same_table_number(*c.editor_step,*d.editor_step))) return false;
        }
    }
    return true;
}

TableView table_geometry_snapshot(const TableView& source) {
    TableView copy;
    copy.set_bounds(source.bounds()); copy.set_font(source.font());
    copy.set_enabled(source.enabled()); copy.set_visible(source.visible());
    copy.columns=source.columns;
    copy.row_update_revision=source.row_update_revision;
    copy.row_order_revision=source.row_order_revision;
    copy.selection=source.selection; copy.selection_anchor=source.selection_anchor;
    copy.selection_cursor=source.selection_cursor; copy.top_index=source.top_index;
    copy.horizontal_offset=source.horizontal_offset; copy.highlighted_column=source.highlighted_column;
    copy.multi_select=source.multi_select; copy.full_row_selection=source.full_row_selection;
    copy.virtualized=source.virtualized; copy.checkable=source.checkable;
    copy.check_selected_rows=source.check_selected_rows;
    copy.edit_on_double_click=source.edit_on_double_click;
    copy.edit_on_single_click=source.edit_on_single_click;
    copy.header_visible=source.header_visible;
    copy.grid_lines=source.grid_lines; copy.row_height=source.row_height; copy.header_height=source.header_height;
    return copy;
}
}

struct PreparedTableUpdate::State {
    std::shared_ptr<TableView> target;
    TableView before;
    TableView after;
    std::uint64_t scale{};
    bool committed{};
};

bool PreparedTableUpdate::current() const noexcept {
    return state_ && !state_->committed &&
        editor_ui_text_scale_generation() == state_->scale &&
        same_table_publication_state(*state_->target, state_->before);
}

bool PreparedTableUpdate::commit() noexcept {
    if (!current()) return false;
    auto& table = *state_->target;
    auto& prepared = state_->after;
    table.rows.swap(prepared.rows);
    table.selection.swap(prepared.selection);
    table.selection_anchor = prepared.selection_anchor;
    table.selection_cursor = prepared.selection_cursor;
    table.top_index = prepared.top_index;
    table.horizontal_offset = prepared.horizontal_offset;
    ++table.row_order_revision;
    state_->committed = true;
    return true;
}

PreparedTableUpdate prepare_table_rows(std::shared_ptr<TableView> table,
                        std::vector<TableRow> rows,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal,
                        std::function<bool()> current) {
    if (!table || (current && !current())) return {};
    ++table->row_update_revision;
    PreparedTableUpdate update;
    update.state_ = std::make_shared<PreparedTableUpdate::State>();
    update.state_->target = table;
    update.state_->before = table_geometry_snapshot(*table);
    update.state_->before.rows = table->rows;
    update.state_->scale = editor_ui_text_scale_generation();
    const auto& before = update.state_->before;
    auto& prepared = update.state_->after;
    prepared.set_bounds(before.bounds());
    prepared.set_font(before.font());
    prepared.columns = before.columns;
    prepared.header_visible = before.header_visible;
    prepared.header_height = before.header_height;
    prepared.row_height = before.row_height;
    prepared.checkable = before.checkable;
    prepared.rows = std::move(rows);
    (void)replace_selection(prepared.selection, std::move(selection),
        [&](const std::size_t index) { return index < prepared.rows.size(); });
    if (!before.multi_select && prepared.selection.size() > 1U)
        prepared.selection.resize(1U);
    prepared.selection_anchor = selected_table_position(
        prepared.selection, before.selection_anchor);
    prepared.selection_cursor = selected_table_position(
        prepared.selection, before.selection_cursor);
    const auto geometry = item_scroll_geometry(prepared);
    const auto count = prepared.rows.size();
    const auto maximum = count > geometry.visible_rows ? count - geometry.visible_rows : 0U;
    // 同じモデルの更新では、ネイティブ表示の最初の可視項目を保持す
    // る。 EnsureVisible が最終行を上方へ置いた場合
    // など、スクロールバーの計算上限を超えていても維持する。 モデ
    // ルの大きさが変わった場合は、新しいスクロール範囲に収める。
    prepared.top_index = count == 0U ? 0U :
        (count == before.rows.size()
             ? std::min(before.top_index, count - 1U)
             : std::min(before.top_index, std::min(maximum, count - 1U)));
    if (reveal && *reveal < count)
        (void)reveal_selection(prepared.top_index, *reveal, count,
            std::max<std::size_t>(1U, geometry.visible_rows));
    prepared.horizontal_offset = count != 0U && std::isfinite(before.horizontal_offset)
        ? std::clamp(before.horizontal_offset, 0.0F, geometry.horizontal.maximum) : 0.0F;
    if ((current && !current()) || !update.current()) return {};
    return update;
}

bool replace_table_rows(std::shared_ptr<TableView> table,
                        std::vector<TableRow> rows,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal,
                        std::function<bool()> current) {
    return prepare_table_rows(std::move(table), std::move(rows),
        std::move(selection), reveal, std::move(current)).commit();
}

struct PreparedTableAppend::State {
    std::shared_ptr<TableView> target;
    std::vector<TableRow> rows;
    std::uint64_t request_revision{};
    std::uint64_t row_order_revision{};
    std::uint64_t scale{};
    std::size_t row_count{};
    std::size_t top_index{};
    std::size_t visible_rows{1U};
    bool reveal_last{};
    bool committed{};
};

bool PreparedTableAppend::current() const noexcept {
    return state_ && !state_->committed && state_->target &&
        editor_ui_text_scale_generation() == state_->scale &&
        state_->target->row_update_revision == state_->request_revision &&
        state_->target->row_order_revision == state_->row_order_revision &&
        state_->target->rows.size() == state_->row_count &&
        state_->target->top_index == state_->top_index;
}

bool PreparedTableAppend::commit() noexcept {
    if (!current()) return false;
    auto& table = *state_->target;
    try {
        table.rows.reserve(table.rows.size() + state_->rows.size());
    } catch (...) {
        return false;
    }
    table.rows.insert(table.rows.end(),
        std::make_move_iterator(state_->rows.begin()),
        std::make_move_iterator(state_->rows.end()));
    if (state_->reveal_last && !table.rows.empty())
        (void)reveal_selection(table.top_index, table.rows.size() - 1U,
            table.rows.size(), state_->visible_rows);
    ++table.row_order_revision;
    state_->committed = true;
    return true;
}

PreparedTableAppend prepare_table_append(std::shared_ptr<TableView> table,
                                         std::vector<TableRow> rows,
                                         const bool reveal_last,
                                         std::function<bool()> current) {
    if (!table || rows.empty() || (current && !current())) return {};
    PreparedTableAppend update;
    update.state_ = std::make_shared<PreparedTableAppend::State>();
    update.state_->target = table;
    update.state_->rows = std::move(rows);
    update.state_->request_revision = ++table->row_update_revision;
    update.state_->row_order_revision = table->row_order_revision;
    update.state_->scale = editor_ui_text_scale_generation();
    update.state_->row_count = table->rows.size();
    update.state_->top_index = table->top_index;
    update.state_->visible_rows = std::max<std::size_t>(
        1U, item_scroll_geometry(*table).visible_rows);
    update.state_->reveal_last = reveal_last;
    if ((current && !current()) || !update.current()) return {};
    return update;
}

bool append_table_rows(std::shared_ptr<TableView> table,
                       std::vector<TableRow> rows,
                       const bool reveal_last,
                       std::function<bool()> current) {
    return prepare_table_append(std::move(table), std::move(rows), reveal_last,
                                std::move(current)).commit();
}

bool replace_table_rows(TableView& table, std::vector<TableRow> rows,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal, std::function<bool()> current,
                        const bool reset_top) {
    if(current && !current()) return false;
    ++table.row_update_revision;
    auto before=table_geometry_snapshot(table);
    before.rows=table.rows;
    const auto scale=editor_ui_text_scale_generation();
    TableView prepared;
    prepared.set_bounds(before.bounds());
    prepared.set_font(before.font());
    prepared.columns=before.columns;
    prepared.header_visible=before.header_visible;
    prepared.header_height=before.header_height;
    prepared.row_height=before.row_height;
    prepared.checkable=before.checkable;
    prepared.rows=std::move(rows);
    std::vector<std::size_t> selected;
    (void)replace_selection(selected, std::move(selection),
        [&](std::size_t index) { return index < prepared.rows.size(); });
    if(!before.multi_select && selected.size()>1) selected.resize(1);
    const auto anchor = selected_table_position(selected, before.selection_anchor);
    const auto cursor = selected_table_position(selected, before.selection_cursor);
    const auto geometry = item_scroll_geometry(prepared);
    const auto count=prepared.rows.size();
    const auto maximum = count > geometry.visible_rows ? count - geometry.visible_rows : 0U;
    // 表示領域が未配置なら、有効なモデルの位置を保持し、count() は使わない。
    auto top = reset_top || count==0 ? 0U
        : (count == before.rows.size()
               ? std::min(before.top_index, count-1U)
               : std::min(before.top_index, std::min(maximum, count-1U)));
    if(!reset_top && reveal && *reveal<count)
        (void)reveal_selection(top,*reveal,count,std::max<std::size_t>(1,geometry.visible_rows));
    const float horizontal = count!=0 && std::isfinite(before.horizontal_offset)
        ? std::clamp(before.horizontal_offset, 0.0F, geometry.horizontal.maximum) : 0.0F;
    if((current && !current()) || editor_ui_text_scale_generation()!=scale ||
       !same_table_publication_state(table,before)) return false;
    table.rows.swap(prepared.rows);
    table.selection.swap(selected);
    table.selection_anchor = anchor;
    table.selection_cursor = cursor;
    table.top_index = top;
    table.horizontal_offset = horizontal;
    ++table.row_order_revision;
    return true;
}

bool select_table_rows(TableView& table, std::vector<std::size_t> selection,
                       const bool preserve_focus, const bool notify) {
    auto next = table.selection;
    const bool changed = replace_selection(
        next, std::move(selection), [&table](const std::size_t index) {
            return index < table.rows.size();
        });
    // 通知前にコールバックを所有して保持する。 リスナーが自身を置換した
    // り選択を読み直したり、モデル全体を破棄したりする場合があるため。
    const auto callback = changed ? table.on_selection_changed : decltype(table.on_selection_changed){};
    const auto notified = callback ? next : std::vector<std::size_t>{};
    table.selection.swap(next);
    if (preserve_focus) {
        if (table.selection_anchor && *table.selection_anchor >= table.rows.size()) table.selection_anchor.reset();
        if (table.selection_cursor && *table.selection_cursor >= table.rows.size()) table.selection_cursor.reset();
    } else {
        table.selection_anchor = selected_table_position(table.selection, table.selection_anchor);
        table.selection_cursor = selected_table_position(table.selection, table.selection_cursor);
    }
    if (notify && callback) callback(notified);
    return changed;
}

bool update_table_selection(TableView& table, const std::size_t index,
                             const SelectionInput input) {
    auto next = table.selection;
    auto anchor = table.selection_anchor;
    if (index >= table.rows.size()) return false;
    const bool selection_changed = update_index_selection(next, anchor, index, table.rows.size(),
                                table.multi_select, input);
    if (!selection_changed && table.selection_cursor == index)
        return false;
    const bool cursor_changed = table.selection_cursor != index;
    table.selection_anchor = anchor;
    table.selection_cursor = index;
    return select_table_rows(table, std::move(next), true) || cursor_changed;
}

bool navigate_table_selection(TableView& table,
                              const SelectionNavigation navigation,
                              const std::size_t visible_count,
                              const bool extend, const bool preserve_selection) {
    const auto target = navigation_target(
        table.selection, table.selection_cursor, table.rows.size(), navigation,
        visible_count,
        [&table](const std::size_t index) { return index < table.rows.size(); });
    if (!target) return false;
    const bool scrolled = reveal_selection(table.top_index, *target, table.rows.size(), visible_count);
    if (preserve_selection && !extend && table.multi_select) {
        const bool changed = table.selection_cursor != *target;
        table.selection_cursor = *target;
        return changed || scrolled;
    }
    return update_table_selection(table, *target, {preserve_selection, extend}) || scrolled;
}

bool reveal_table_row(TableView& table, const std::size_t row, const float row_height) {
    if (row >= table.rows.size()) return false;
    // 行を見える位置へ移す操作では、セルのデータを通知しない。 カーソル移
    // 動のたびに大きな表の全セルを複製せず、配置と位置情報だけを取得する。
    const auto count=table.rows.size();
    const auto before=table_geometry_snapshot(table);
    const auto scale=editor_ui_text_scale_generation();
    const auto geometry = item_scroll_geometry(before, row_height, {}, count);
    auto top=before.top_index;
    const bool changed=reveal_selection(top,row,count,geometry.visible_rows);
    if(editor_ui_text_scale_generation()!=scale || count!=table.rows.size() ||
       !same_table_publication_state(table,before,false)) return false;
    if(changed) table.top_index=top;
    return changed;
}

bool set_table_row_checked(TableView& table, const std::size_t index,
                           const bool checked) {
    if (!table.checkable || index>=table.rows.size()) return false;
    std::vector<std::size_t> changed_rows;
    bool transition_checked = checked;
    bool transition_grayed = table.rows[index].grayed;
    const auto transition = table.on_check_transition;
    if (transition) transition(index, transition_checked, transition_grayed);
    const auto assign=[&](const std::size_t row, const bool copy_grayed) {
        if (row >= table.rows.size()) return;
        auto& value = table.rows[row];
        const bool changed = value.checked != transition_checked ||
            (copy_grayed && value.grayed != transition_grayed);
        if (!changed) return;
        value.checked = transition_checked;
        if (copy_grayed) value.grayed = transition_grayed;
        changed_rows.push_back(row);
    };
    // クリック行は必ずその行の灰色状態を含めて正規化する。
    assign(index, true);
    if (table.check_selected_rows && std::ranges::find(table.selection,index)!=table.selection.end())
        for (const auto row:table.selection)
            assign(row, table.check_selected_rows_copy_grayed);
    if (changed_rows.empty()) return false;
    std::ranges::sort(changed_rows);
    changed_rows.erase(std::unique(changed_rows.begin(),changed_rows.end()),changed_rows.end());
    ++table.row_update_revision;
    const auto batch_callback = table.on_check_batch_changed;
    if (batch_callback) {
        batch_callback(index, changed_rows);
        return true;
    }
    const auto callback=table.on_check_changed;
    if (callback)
        for (const auto row:changed_rows) callback(row,transition_checked);
    return true;
}

bool publish_table_cell_text(TableView& table, const std::size_t row,
                             const std::size_t column, std::string text) {
    if (row >= table.rows.size() || column >= table.rows[row].cells.size())
        return false;
    auto& cell = table.rows[row].cells[column];
    if (cell.text == text) return false;
    cell.text = std::move(text);
    ++table.row_update_revision;
    return true;
}

bool publish_table_cell_text_value(TableView& table, const std::size_t row,
                                   const std::size_t column, std::string text,
                                   std::string value) {
    if (row >= table.rows.size() || column >= table.rows[row].cells.size())
        return false;
    auto& cell = table.rows[row].cells[column];
    if (cell.text == text && cell.value == value) return false;
    cell.text = std::move(text);
    cell.value = std::move(value);
    ++table.row_update_revision;
    return true;
}

bool edit_table_cell(TableView& table, const std::size_t row,
                      const std::size_t column, std::string text) {
    if (row >= table.rows.size() || column >= table.rows[row].cells.size())
        return false;
    auto& cell = table.rows[row].cells[column];
    if (!cell.enabled || cell.editor_value.value_or(cell.text) == text) return false;
    const auto callback = table.on_cell_edited;
    const auto published = text;
    if (cell.editor_value) {
        cell.editor_value = std::move(text);
        ++table.row_update_revision;
    } else if (!publish_table_cell_text(table, row, column, std::move(text))) return false;
    if (callback) callback(row, column, published);
    return true;
}

bool cycle_table_cell_choice(TableView& table, const std::size_t row,
                             const std::size_t column) {
    if (row >= table.rows.size() || column >= table.rows[row].cells.size())
        return false;
    const auto& cell = table.rows[row].cells[column];
    if (!cell.enabled || cell.editor != CellEditorKind::Choice ||
        cell.editor_choices.empty())
        return false;
    const auto& choices = cell.editor_choice_values.size() == cell.editor_choices.size()
        ? cell.editor_choice_values : cell.editor_choices;
    const auto found = std::ranges::find(choices, cell.editor_value.value_or(cell.text));
    const std::size_t next = found == choices.end()
        ? 0U
        : (static_cast<std::size_t>(found - choices.begin()) + 1U) % choices.size();
    return edit_table_cell(table, row, column, choices[next]);
}

bool resize_table_column(TableView& table, const std::size_t column,
                         const float width) {
    if (column >= table.columns.size() || !table.columns[column].resizable ||
        !std::isfinite(width))
        return false;
    const float clamped = std::max(20.0F, width);
    if (table.columns[column].width == clamped) return false;
    const auto callback = table.on_column_resized;
    table.columns[column].width = clamped;
    if (callback) callback(column, clamped);
    return true;
}

bool select_tree_items(TreeView& tree, std::vector<TreeItemId> selection,
                       const bool preserve_focus, const bool notify) {
    auto next = tree.selection;
    const bool changed = replace_selection(
        next, std::move(selection), [&tree](const TreeItemId id) {
            return std::ranges::any_of(tree.nodes, [id](const TreeNode& node) {
                return node.id == id;
            });
        });
    // 通知前にコールバックを所有して保持する。 リスナーが自身を置換した
    // り選択を読み直したり、モデル全体を破棄したりする場合があるため。
    const auto callback = changed ? tree.on_selection_changed : decltype(tree.on_selection_changed){};
    const auto notified = callback ? next : std::vector<TreeItemId>{};
    tree.selection.swap(next);
    if (preserve_focus) {
        const auto valid = [&](TreeItemId id) { return std::ranges::find(tree.nodes,id,&TreeNode::id)!=tree.nodes.end(); };
        if (tree.selection_anchor && !valid(*tree.selection_anchor)) tree.selection_anchor.reset();
        if (tree.selection_cursor && !valid(*tree.selection_cursor)) tree.selection_cursor.reset();
    } else {
    if (tree.selection.empty()) tree.selection_anchor.reset();
    else if (!tree.selection_anchor ||
             std::ranges::find(tree.selection, *tree.selection_anchor) ==
                 tree.selection.end())
        tree.selection_anchor = tree.selection.back();
    if (tree.selection.empty()) tree.selection_cursor.reset();
    else if (!tree.selection_cursor ||
             std::ranges::find(tree.selection, *tree.selection_cursor) ==
                 tree.selection.end())
        tree.selection_cursor = tree.selection.back();
    }
    if (notify && callback) callback(notified);
    return changed;
}

namespace {
TreeView tree_publication_snapshot(const TreeView& source) {
    TreeView copy;
    copy.set_bounds(source.bounds()); copy.set_font(source.font());
    copy.set_enabled(source.enabled()); copy.set_visible(source.visible());
    copy.node_update_revision = source.node_update_revision;
    copy.node_order_revision = source.node_order_revision;
    copy.nodes = source.nodes; copy.selection = source.selection;
    copy.selection_anchor = source.selection_anchor; copy.selection_cursor = source.selection_cursor;
    copy.top_index = source.top_index; copy.horizontal_offset = source.horizontal_offset;
    copy.multi_select = source.multi_select; copy.checkable = source.checkable;
    copy.show_expanders = source.show_expanders; copy.flush_root_left = source.flush_root_left;
    return copy;
}

bool same_tree_publication_state(const TreeView& a, const TreeView& b) noexcept {
    const auto x = a.bounds(), y = b.bounds();
    if (!same_table_number(x.x,y.x) || !same_table_number(x.y,y.y) ||
        !same_table_number(x.width,y.width) || !same_table_number(x.height,y.height) ||
        a.enabled()!=b.enabled() || a.visible()!=b.visible() ||
        a.node_update_revision!=b.node_update_revision || a.node_order_revision!=b.node_order_revision ||
        a.nodes!=b.nodes || a.selection!=b.selection || a.selection_anchor!=b.selection_anchor ||
        a.selection_cursor!=b.selection_cursor || a.top_index!=b.top_index ||
        !same_table_number(a.horizontal_offset,b.horizontal_offset) ||
        a.multi_select!=b.multi_select || a.checkable!=b.checkable ||
        a.show_expanders!=b.show_expanders || a.flush_root_left!=b.flush_root_left ||
        a.font().has_value()!=b.font().has_value()) return false;
    if (a.font()) {
        const auto& f=*a.font(); const auto& g=*b.font();
        if (f.family!=g.family || !same_table_number(f.points,g.points) || f.weight!=g.weight ||
            f.italic!=g.italic || f.underline!=g.underline || f.strikeout!=g.strikeout ||
            f.size_mode!=g.size_mode) return false;
    }
    return true;
}
}

struct PreparedTreeUpdate::State {
    std::shared_ptr<TreeView> target;
    TreeView before;
    TreeView after;
    std::uint64_t scale{};
    bool committed{};
};

bool PreparedTreeUpdate::current() const noexcept {
    return state_ && !state_->committed &&
        editor_ui_text_scale_generation()==state_->scale &&
        same_tree_publication_state(*state_->target, state_->before);
}

bool PreparedTreeUpdate::commit() noexcept {
    if (!current()) return false;
    auto& tree=*state_->target;
    auto& prepared=state_->after;
    tree.nodes.swap(prepared.nodes);
    tree.selection.swap(prepared.selection);
    tree.selection_anchor=prepared.selection_anchor;
    tree.selection_cursor=prepared.selection_cursor;
    tree.top_index=prepared.top_index;
    tree.horizontal_offset=prepared.horizontal_offset;
    ++tree.node_order_revision;
    state_->committed=true;
    return true;
}

PreparedTreeUpdate prepare_tree_nodes(std::shared_ptr<TreeView> tree, std::vector<TreeNode> nodes,
                        std::vector<TreeItemId> selection,
                        std::optional<TreeItemId> reveal, std::function<bool()> current) {
    if (!tree || (current && !current())) return {};
    ++tree->node_update_revision;
    PreparedTreeUpdate update;
    update.state_=std::make_shared<PreparedTreeUpdate::State>();
    update.state_->target=tree;
    update.state_->before=tree_publication_snapshot(*tree);
    update.state_->scale=editor_ui_text_scale_generation();
    const auto& before=update.state_->before;
    std::unordered_map<TreeItemId, std::size_t> indices;
    indices.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].id || !indices.emplace(nodes[i].id, i).second)
            throw std::invalid_argument("Tree replacement requires unique nonzero IDs");
        if (nodes[i].suppress_expander) nodes[i].expanded = true;
    }
    // 親より先に子が来る入力も含め、再帰を使わずに検証する。
    std::vector<unsigned char> visited(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto id = nodes[i].id;
        while (id) {
            const auto it = indices.find(id);
            if (it == indices.end()) throw std::invalid_argument("Tree parent is absent");
            auto& mark = visited[it->second];
            if (mark == 2) break;
            if (mark == 1) throw std::invalid_argument("Tree parent cycle");
            mark = 1;
            id = nodes[it->second].parent;
        }
        id = nodes[i].id;
        while (id && visited[indices.at(id)] == 1) {
            visited[indices.at(id)] = 2;
            id = nodes[indices.at(id)].parent;
        }
    }
    auto& prepared = update.state_->after;
    prepared.set_bounds(before.bounds());
    prepared.set_font(before.font());
    prepared.checkable = before.checkable;
    prepared.multi_select = before.multi_select;
    prepared.show_expanders = before.show_expanders;
    prepared.flush_root_left = before.flush_root_left;
    prepared.nodes.swap(nodes);
    (void)replace_selection(prepared.selection, std::move(selection),
        [&](TreeItemId id) { return indices.contains(id); });
    if (!prepared.multi_select && prepared.selection.size() > 1) prepared.selection.resize(1);
    const auto focus = [&](std::optional<TreeItemId> value) -> std::optional<TreeItemId> {
        if (prepared.selection.empty()) return {};
        if (value && std::ranges::find(prepared.selection, *value) != prepared.selection.end()) return value;
        return prepared.selection.back();
    };
    prepared.selection_anchor = focus(before.selection_anchor);
    prepared.selection_cursor = focus(before.selection_cursor);
    if (reveal && indices.contains(*reveal)) {
        auto parent = prepared.nodes[indices.at(*reveal)].parent;
        while (parent) {
            auto& node = prepared.nodes[indices.at(parent)];
            node.expanded = true;
            parent = node.parent;
        }
    }
    const auto old_visible = visible_tree_indices(before);
    const auto visible = visible_tree_indices(prepared);
    prepared.top_index = before.top_index;
    if (before.top_index < old_visible.size()) {
        const auto id = before.nodes[old_visible[before.top_index]].id;
        const auto it = std::ranges::find_if(visible, [&](auto i) { return prepared.nodes[i].id == id; });
        if (it != visible.end()) prepared.top_index = static_cast<std::size_t>(it - visible.begin());
    }
    // フォントの計測はすべて専用モデル上で行い、消去途中の有効なツリーを使わない
    // 。 提供処理が別の反映を開始したり、UIを破棄したりする場合があるため。
    const auto geometry = item_scroll_geometry(prepared);
    const auto capacity = std::max<std::size_t>(1, geometry.visible_rows);
    (void)set_top_index(prepared.top_index, prepared.top_index, visible.size(), capacity);
    if (reveal) {
        const auto it = std::ranges::find_if(visible, [&](auto i) { return prepared.nodes[i].id == *reveal; });
        if (it != visible.end())
            (void)reveal_selection(prepared.top_index, static_cast<std::size_t>(it-visible.begin()), visible.size(), capacity);
    }
    prepared.horizontal_offset = !visible.empty() && std::isfinite(before.horizontal_offset)
        ? std::clamp(before.horizontal_offset, 0.0F, geometry.horizontal.maximum) : 0.0F;
    if ((current && !current()) || !update.current()) return {};
    return update;
}

bool replace_tree_nodes(std::shared_ptr<TreeView> tree, std::vector<TreeNode> nodes,
                        std::vector<TreeItemId> selection,
                        std::optional<TreeItemId> reveal, std::function<bool()> current) {
    auto prepared=prepare_tree_nodes(std::move(tree), std::move(nodes),
        std::move(selection), reveal, std::move(current));
    return prepared.commit();
}

bool update_tree_selection(TreeView& tree, const TreeItemId id,
                           const std::vector<TreeItemId>& visible_order,
                           const SelectionInput input) {
    const auto clicked = std::ranges::find(visible_order, id);
    if (clicked == visible_order.end()) return false;
    const std::size_t clicked_index = static_cast<std::size_t>(clicked - visible_order.begin());
    std::vector<std::size_t> indices;
    indices.reserve(tree.selection.size());
    for (const auto selected : tree.selection) {
        const auto found = std::ranges::find(visible_order, selected);
        if (found != visible_order.end())
            indices.push_back(static_cast<std::size_t>(found - visible_order.begin()));
    }
    std::optional<std::size_t> anchor;
    if (tree.selection_anchor) {
        const auto found = std::ranges::find(visible_order, *tree.selection_anchor);
        if (found != visible_order.end())
            anchor = static_cast<std::size_t>(found - visible_order.begin());
    }
    const bool selection_changed = update_index_selection(indices, anchor, clicked_index,
                                visible_order.size(), tree.multi_select, input);
    if (!selection_changed && tree.selection_cursor == id)
        return false;
    std::vector<TreeItemId> selection;
    selection.reserve(indices.size());
    for (const auto index : indices) selection.push_back(visible_order[index]);
    const bool cursor_changed = tree.selection_cursor != id;
    tree.selection_anchor = anchor ? std::optional<TreeItemId>(visible_order[*anchor])
                                   : std::nullopt;
    tree.selection_cursor = id;
    return select_tree_items(tree, std::move(selection), true) || cursor_changed;
}

bool navigate_tree_selection(TreeView& tree,
                             const SelectionNavigation navigation,
                             const std::size_t visible_count,
                             const bool extend, const bool preserve_selection) {
    const auto visible = visible_tree_indices(tree);
    std::vector<TreeItemId> order;
    order.reserve(visible.size());
    for (const auto index : visible) order.push_back(tree.nodes[index].id);
    std::vector<std::size_t> selected;
    selected.reserve(tree.selection.size());
    for (const auto id : tree.selection) {
        const auto found = std::ranges::find(order, id);
        if (found != order.end())
            selected.push_back(static_cast<std::size_t>(found - order.begin()));
    }
    const auto target = navigation_target(
        selected,
        tree.selection_cursor
            ? [&]() -> std::optional<std::size_t> {
                  const auto found = std::ranges::find(order,
                                                       *tree.selection_cursor);
                  if (found == order.end()) return std::nullopt;
                  return static_cast<std::size_t>(found - order.begin());
              }()
            : std::nullopt,
        order.size(), navigation, visible_count,
        [&order](const std::size_t index) { return index < order.size(); });
    if (!target) return false;
    const bool scrolled = reveal_selection(tree.top_index, *target, order.size(), visible_count);
    if (preserve_selection && !extend && tree.multi_select) {
        const bool changed = tree.selection_cursor != order[*target];
        tree.selection_cursor = order[*target];
        return changed || scrolled;
    }
    return update_tree_selection(tree, order[*target], order, {preserve_selection, extend}) || scrolled;
}

bool navigate_tree_horizontal(TreeView& tree, const bool forward,
                               const std::size_t visible_count) {
    if (tree.selection.empty() && !tree.selection_cursor) return false;
    const TreeItemId id = tree.selection_cursor ? *tree.selection_cursor : tree.selection.front();
    const auto found = std::ranges::find(tree.nodes, id, &TreeNode::id);
    if (found == tree.nodes.end()) return false;
    const bool has_child = found->has_lazy_children ||
        std::ranges::any_of(tree.nodes, [id](const TreeNode& node) {
            return node.parent == id;
        });
    const auto select_visible = [&](const TreeItemId target) {
        const auto visible = visible_tree_indices(tree);
        std::vector<TreeItemId> order;
        order.reserve(visible.size());
        for (const auto index : visible) order.push_back(tree.nodes[index].id);
        const auto position = std::ranges::find(order, target);
        if (position == order.end()) return false;
        // 選択通知の前にスクロール状態を反映する。 リスナーがモデル全体を
        // 置き換える場合があるため、古い行・位置をその後まで保持しない。
        const bool scrolled = reveal_selection(tree.top_index,
            static_cast<std::size_t>(position-order.begin()), order.size(), visible_count);
        return update_tree_selection(tree, target, order) || scrolled;
    };
    if (forward) {
        if (has_child && !found->expanded && !found->suppress_expander)
            return set_tree_expanded(tree, id, true);
        const auto child = std::ranges::find(tree.nodes, id, &TreeNode::parent);
        if (child == tree.nodes.end())
            return found->has_lazy_children && !found->expanded
                ? set_tree_expanded(tree, id, true) : false;
        return select_visible(child->id);
    }
    if (has_child && found->expanded && !found->suppress_expander)
        return set_tree_expanded(tree, id, false, false);
    if (found->parent == 0U) return false;
    return select_visible(found->parent);
}

std::size_t item_view_visible_row_count(const WidgetPtr& view, const float row_height) {
    if (const auto table = std::dynamic_pointer_cast<TableView>(view))
        return table_visible_row_count(*table,row_height);
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(view))
        return tree_visible_row_count(*tree,row_height);
    if (const auto list = std::dynamic_pointer_cast<ListView>(view))
        return item_scroll_geometry(*list,row_height).visible_rows;
    return 1;
}

std::optional<Rect> item_view_cursor_geometry(const WidgetPtr& view, float row_height) {
    if (!view || !view->visible() || !view->enabled() || row_height <= 0) return std::nullopt;
    row_height = item_row_height(*view, row_height);
    if (view->supports_item_view_text_input())
        return view->item_view_text_input_anchor(row_height);
    Rect viewport = item_scroll_geometry(*view,row_height).viewport, row{};
    if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        if (!list->selection_cursor || *list->selection_cursor >= list->items.size() ||
            *list->selection_cursor < list->top_index || !list->items[*list->selection_cursor].enabled) return std::nullopt;
        row={viewport.x,viewport.y+
            (*list->selection_cursor-list->top_index)*row_height,viewport.width,row_height};
    } else if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        if (!table->selection_cursor || *table->selection_cursor >= table->rows.size() ||
            *table->selection_cursor < table->top_index) return std::nullopt;
        viewport=table_horizontal_scroll_geometry(*table).viewport;
        row=table_row_geometry(*table,*table->selection_cursor,row_height);
        row=table_row_selection_geometry(*table,row.y,row.height);
    } else if (const auto tree = std::dynamic_pointer_cast<TreeView>(view)) {
        if (!tree->selection_cursor) return std::nullopt;
        const auto pos=tree_visible_position(*tree,*tree->selection_cursor);
        if (!pos || *pos < tree->top_index) return std::nullopt;
        viewport=tree_horizontal_scroll_geometry(*tree).viewport;
        row={viewport.x,viewport.y+(*pos-tree->top_index)*row_height,viewport.width,row_height};
    } else return std::nullopt;
    if (row.y < viewport.y || row.y+row.height > viewport.y+viewport.height) return std::nullopt;
    const float left=std::max(row.x,viewport.x)+1;
    const float right=std::min(row.x+row.width,viewport.x+viewport.width)-1;
    if (right<=left || row.height<=2) return std::nullopt;
    return Rect{left,row.y+1,right-left,row.height-2};
}

bool begin_selected_item_edit(InlineEditStateMachine& edit, const WidgetPtr& view,
                               float row_height, std::size_t column) {
    if (edit.active() || !view || !view->visible() || !view->enabled() ||
        !std::isfinite(row_height) || row_height <= 0) return false;
    if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        if (table->selection.empty() || column >= table->columns.size()) return false;
        const auto row = *std::ranges::min_element(table->selection);
        if (!edit.begin(table, row, column)) return false;
        const auto old_top = table->top_index;
        const auto old_x = table->horizontal_offset;
        const auto scroll = item_scroll_geometry(*table, row_height);
        (void)reveal_selection(table->top_index, row, table->rows.size(), scroll.visible_rows);
        const auto widths = resolved_table_column_widths(*table, scroll.viewport.width);
        float left = 0;
        for (std::size_t i=0; i<column; ++i) left += widths[i];
        if (left < table->horizontal_offset ||
            left >= table->horizontal_offset + scroll.viewport.width - scaled_ui_metric(24))
            (void)set_item_horizontal_offset(*table, left);
        if (edit.layout(row_height)) return true;
        edit.cancel(); table->top_index = old_top; table->horizontal_offset = old_x;
    } else if (const auto tree = std::dynamic_pointer_cast<TreeView>(view)) {
        if (tree->selection.size() != 1) return false;
        const auto visible = visible_tree_indices(*tree);
        const auto found = std::ranges::find_if(visible, [&](auto i) {
            return tree->nodes[i].id == tree->selection.front();
        });
        if (found == visible.end() || !edit.begin(tree, *found)) return false;
        const auto old_top = tree->top_index;
        const auto old_x = tree->horizontal_offset;
        const auto scroll = item_scroll_geometry(*tree, row_height);
        (void)reveal_selection(tree->top_index, static_cast<std::size_t>(found-visible.begin()),
                              visible.size(), scroll.visible_rows);
        const auto row = tree_row_geometry(*tree, tree->bounds(), *found, 0, row_height);
        if (row && (row->text.x < scroll.viewport.x ||
                    row->text.x >= scroll.viewport.x+scroll.viewport.width-scaled_ui_metric(24)))
            (void)set_item_horizontal_offset(*tree, old_x+row->text.x-scroll.viewport.x);
        if (edit.layout(row_height)) return true;
        edit.cancel(); tree->top_index = old_top; tree->horizontal_offset = old_x;
    }
    return false;
}

ItemViewKeyResult handle_item_view_key(const WidgetPtr& focused, const Key key,
                                       const std::size_t visible_count,
                                       const Modifiers modifiers) {
    ItemViewKeyResult result;
    const auto mods = static_cast<std::uint32_t>(modifiers);
    if (!focused || !focused->visible() || !focused->enabled() ||
        (mods & static_cast<std::uint32_t>(Modifiers::Alt))) return result;
    const bool extend = (mods & static_cast<std::uint32_t>(Modifiers::Shift)) != 0;
    const bool preserve = (mods & static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Meta)) != 0;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(focused)) {
        std::optional<TabNavigation> navigation;
        switch (key) {
        case Key::Left:
        case Key::Up: navigation = TabNavigation::Previous; break;
        case Key::Right:
        case Key::Down: navigation = TabNavigation::Next; break;
        case Key::Home: navigation = TabNavigation::First; break;
        case Key::End: navigation = TabNavigation::Last; break;
        default: return result;
        }
        result.handled = true;
        result.layout_changed = navigate_tab_page(*tabs, *navigation);
        return result;
    }

    const auto navigation = [&]() -> std::optional<SelectionNavigation> {
        switch (key) {
        case Key::Up: return SelectionNavigation::Previous;
        case Key::Down: return SelectionNavigation::Next;
        case Key::Home: return SelectionNavigation::First;
        case Key::End: return SelectionNavigation::Last;
        case Key::PageUp: return SelectionNavigation::PagePrevious;
        case Key::PageDown: return SelectionNavigation::PageNext;
        default: return std::nullopt;
        }
    }();
    const std::size_t page = std::max<std::size_t>(1U, visible_count);

    if (const auto list = std::dynamic_pointer_cast<ListView>(focused)) {
        const auto cursor = list->selection_cursor ? list->selection_cursor :
            list->selection.empty() ? std::nullopt : std::optional(list->selection.front());
        if (key == Key::A && preserve && list->multi_select) {
            std::vector<std::size_t> all;
            for (std::size_t i=0;i<list->items.size();++i) if (list->items[i].enabled) all.push_back(i);
            (void)select_list_rows(*list,std::move(all)); result.handled=true;
        } else if (key == Key::Enter && cursor) {
            (void)activate_list_item(*list, *cursor, modifiers);
            result.handled = true;
        } else if (key == Key::Space && cursor) {
            const std::size_t index = *cursor;
            if (list->checkable && !preserve && index < list->items.size())
                (void)set_list_item_checked(*list, index,
                                            !list->items[index].checked);
            else (void)update_list_selection(*list,index,{preserve,extend});
            result.handled = true;
        } else if (navigation) {
            (void)navigate_list_selection(*list, *navigation, page, extend, preserve);
            result.handled = true;
        }
        return result;
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(focused)) {
        const auto cursor = table->selection_cursor ? table->selection_cursor :
            table->selection.empty() ? std::nullopt : std::optional(table->selection.front());
        if (key == Key::A && preserve && table->multi_select) {
            std::vector<std::size_t> all;
            for (std::size_t i=0;i<table->rows.size();++i) all.push_back(i);
            (void)select_table_rows(*table,std::move(all)); result.handled=true;
        } else if (key == Key::Enter && cursor && table->activate_on_enter) {
            (void)activate_table_cell(*table, *cursor, 0U, modifiers);
            result.handled = true;
        } else if (key == Key::Space && cursor) {
            const std::size_t index = *cursor;
            if (table->checkable && !preserve && index < table->rows.size())
                (void)set_table_row_checked(*table, index,
                                            !table->rows[index].checked);
            else (void)update_table_selection(*table,index,{preserve,extend});
            result.handled = true;
        } else if (navigation) {
            (void)navigate_table_selection(*table, *navigation, page, extend, preserve);
            result.handled = true;
        }
        return result;
    }
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(focused)) {
        const auto cursor = tree->selection_cursor ? tree->selection_cursor :
            tree->selection.empty() ? std::nullopt : std::optional(tree->selection.front());
        if (key == Key::A && preserve && tree->multi_select) {
            std::vector<TreeItemId> all;
            for (const auto i : visible_tree_indices(*tree)) all.push_back(tree->nodes[i].id);
            (void)select_tree_items(*tree,std::move(all)); result.handled=true;
        } else if ((key == Key::Left || key == Key::Right) && !preserve) {
            (void)navigate_tree_horizontal(*tree, key == Key::Right, page);
            result.handled = true;
        } else if (key == Key::Enter && cursor) {
            (void)activate_tree_item(*tree, *cursor, modifiers);
            result.handled = true;
        } else if (key == Key::Space && cursor) {
            const TreeItemId id = *cursor;
            const auto found = std::ranges::find(tree->nodes, id, &TreeNode::id);
            if (tree->checkable && !preserve && found != tree->nodes.end())
                (void)set_tree_item_checked(*tree, id, !found->checked);
            else {
                std::vector<TreeItemId> order;
                for (const auto i : visible_tree_indices(*tree)) order.push_back(tree->nodes[i].id);
                (void)update_tree_selection(*tree,id,order,{preserve,extend});
            }
            result.handled = true;
        } else if (navigation) {
            (void)navigate_tree_selection(*tree, *navigation, page, extend, preserve);
            result.handled = true;
        }
    }
    return result;
}

bool set_tree_expanded(TreeView& tree, const TreeItemId id,
                       bool expanded, const bool request_children,
                       const bool notify) {
    const auto found = std::ranges::find(tree.nodes, id, &TreeNode::id);
    if (found == tree.nodes.end()) return false;
    if (found->suppress_expander) expanded = true;
    if (found->expanded == expanded) return false;
    TreeNode published = *found;
    published.expanded = expanded;
    const auto lifetime = tree.operation_lifetime();
    // 通知前に両コールバックを所有する。 展開リスナーが処理関数を置き換えたり、ノ
    // ードモデルを再構築したり、TreeView 自体を破棄したりする場合がある。
    const auto expansion_callback = notify ? tree.on_expansion_changed
                                           : decltype(tree.on_expansion_changed){};
    const auto children_callback = notify && expanded && request_children
        ? tree.on_request_children : decltype(tree.on_request_children){};
    found->expanded = expanded;
    // 折り畳みは次の配置より前に可視行数を変えるため、
    // 利用側へ通知する前に表示範囲を有効な状態へ収める。
    const auto visible = visible_tree_indices(tree);
    const auto page = tree_visible_row_count(tree, scaled_ui_metric(kEditorUiMetrics.row_height));
    tree.top_index = clamp_top_index(tree.top_index, visible.size(), page);
    if (expansion_callback) expansion_callback(id, expanded);
    if (!children_callback || lifetime.expired()) return true;
    const auto current = std::ranges::find(tree.nodes, id, &TreeNode::id);
    // 置き換わったノードや、展開コールバックが直ちに折
    // り畳み・正規化したノードには、子を要求しない。
    if (current != tree.nodes.end() && *current == published)
        children_callback(id);
    return true;
}

bool set_tree_item_checked(TreeView& tree, const TreeItemId id,
                           const bool checked) {
    if (!tree.checkable) return false;
    const auto found = std::ranges::find(tree.nodes, id, &TreeNode::id);
    if (found == tree.nodes.end() || found->checked == checked) return false;
    const auto callback = tree.on_check_changed;
    found->checked = checked;
    if (callback) callback(id, checked);
    return true;
}

bool edit_tree_item_text(TreeView& tree, const TreeItemId id,
                         std::string text) {
    const auto found = std::ranges::find(tree.nodes, id, &TreeNode::id);
    if (found == tree.nodes.end() || found->text == text) return false;
    const auto callback = tree.on_item_edited;
    found->text = std::move(text);
    const auto published = found->text;
    if (callback) callback(id, published);
    return true;
}

std::vector<std::size_t> visible_tree_indices(const TreeView& tree) {
    std::vector<std::size_t> result;
    result.reserve(tree.nodes.size());
    for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
        TreeItemId parent = tree.nodes[index].parent;
        bool visible = true;
        std::size_t ancestors = 0;
        while (parent != 0U) {
            // 不正・循環する所有関係で、配置や描画を停止させない。
            if (++ancestors > tree.nodes.size()) { visible = false; break; }
            const auto found = std::ranges::find(tree.nodes, parent, &TreeNode::id);
            if (found == tree.nodes.end()) break;
            if (!found->expanded && !found->suppress_expander) {
                visible = false;
                break;
            }
            parent = found->parent;
        }
        if (visible) result.push_back(index);
    }
    return result;
}

std::optional<std::size_t> hit_test_tree_row(
    const TreeView& tree, const Rect bounds, const Point point,
    float row_height, const float header_height) {
    row_height = item_row_height(tree, row_height);
    const auto visible = visible_tree_indices(tree);
    const auto viewport = item_scroll_geometry(tree,row_height,bounds).viewport;
    const auto row = hit_test_linear_row(viewport, point, row_height,
                                         tree.top_index, visible.size(),
                                         header_height);
    if (row && bounds.y + header_height + (*row-tree.top_index+1)*row_height <= viewport.y+viewport.height)
        return visible[*row];
    return std::nullopt;
}

std::optional<TreeRowGeometry> tree_row_geometry(
    const TreeView& tree, Rect bounds, std::size_t model_index,
    float row_y, float row_height) {
    row_height = item_row_height(tree, row_height);
    const float offset = std::isfinite(tree.horizontal_offset) ? std::max(0.0F, tree.horizontal_offset) : 0;
    auto result = tree_row_geometry(tree, {bounds.x-offset,bounds.y,bounds.width+offset,bounds.height}, model_index, row_y, row_height,
        scaled_ui_metric(5), scaled_ui_metric(18), scaled_ui_metric(16),
        scaled_ui_metric(13), scaled_ui_metric(16), scaled_ui_metric(1), scaled_ui_metric(1));
    if (result) result->row = {bounds.x,row_y,bounds.width,row_height};
    return result;
}

TreeWidthMeasurement prepare_tree_width_measurement(const TreeView& tree) {
    TreeWidthMeasurement input;
    input.font=tree.font(); input.trailing=scaled_ui_metric(5);
    for (const auto index : visible_tree_indices(tree)) {
        // 生の形状情報は、意図的に表示領域・スクロールに依存させない。
        const auto row = tree_row_geometry(tree, {0,0,0,scaled_ui_metric(20)}, index, 0, scaled_ui_metric(20),
            scaled_ui_metric(5), scaled_ui_metric(18), scaled_ui_metric(16),
            scaled_ui_metric(13), scaled_ui_metric(16), scaled_ui_metric(1), scaled_ui_metric(1));
        if (!row) continue;
        input.rows.push_back({tree.nodes[index].text,row->text.x});
    }
    return input;
}

float tree_content_width(TreeWidthMeasurement input, const float label_limit) {
    float width{};
    for (const auto& row:input.rows) {
        float label = ui_text_width(row.text, 0, input.font ? &*input.font : nullptr);
        if (label_limit >= 0) label = std::min(label, label_limit);
        width = std::max(width, row.leading + label + input.trailing);
    }
    return width;
}

float tree_content_width(const TreeView& tree, const float label_limit) {
    return tree_content_width(prepare_tree_width_measurement(tree),label_limit);
}

std::size_t tree_visible_row_count(const TreeView& tree, const float row_height) {
    return item_scroll_geometry(tree,row_height).visible_rows;
}

std::optional<TreeRowGeometry> tree_row_geometry(
    const TreeView& tree, const Rect bounds, const std::size_t model_index,
    const float row_y, const float row_height, const float leading_inset,
    const float indent_width, const float expander_width,
    const float check_box_size, const float icon_size,
    const float check_box_gap, const float icon_gap) {
    if (model_index >= tree.nodes.size() || row_height <= 0.0F) return std::nullopt;

    const auto& node = tree.nodes[model_index];
    std::size_t depth{};
    TreeItemId parent = node.parent;
    while (parent != 0U && depth < tree.nodes.size()) {
        ++depth;
        const auto found = std::ranges::find(tree.nodes, parent, &TreeNode::id);
        if (found == tree.nodes.end()) break;
        parent = found->parent;
    }

    TreeRowGeometry geometry;
    geometry.model_index = model_index;
    geometry.depth = depth;
    geometry.row = {bounds.x, row_y, bounds.width, row_height};
    float x = bounds.x + leading_inset + static_cast<float>(depth) * indent_width;
    geometry.has_expander = tree.show_expanders && !node.suppress_expander &&
        (node.has_lazy_children || std::ranges::any_of(
             tree.nodes, [&](const TreeNode& child) { return child.parent == node.id; }));
    if (geometry.has_expander) {
        geometry.expander = {x, row_y, expander_width, row_height};
        x += expander_width;
    }
    if (tree.checkable) {
        geometry.check_box = {
            x, row_y + (row_height - check_box_size) * 0.5F,
            check_box_size, check_box_size};
        x += check_box_size + check_box_gap;
    }
    if (node.icon != 0U) {
        geometry.icon = {x, row_y + (row_height - icon_size) * 0.5F,
                         icon_size, icon_size};
        x += icon_size + icon_gap;
    }
    geometry.text = {x, row_y,
                     std::max(0.0F, bounds.x + bounds.width - x - leading_inset),
                     row_height};
    return geometry;
}

std::optional<TreeRowPart> hit_test_tree_row_part(
    const TreeRowGeometry& geometry, const Point point) noexcept {
    const auto contains = [&](const Rect rect) {
        return rect.width > 0.0F && rect.height > 0.0F &&
               point.x >= rect.x && point.y >= rect.y &&
               point.x < rect.x + rect.width && point.y < rect.y + rect.height;
    };
    if (!contains(geometry.row)) return std::nullopt;
    if (geometry.has_expander && contains(geometry.expander))
        return TreeRowPart::Expander;
    if (contains(geometry.check_box)) return TreeRowPart::CheckBox;
    if (contains(geometry.icon)) return TreeRowPart::Icon;
    if (contains(geometry.text)) return TreeRowPart::Text;
    return TreeRowPart::Row;
}

std::optional<std::size_t> hit_test_item_view(const WidgetPtr& view, const Point point) {
    if (!view || !view->visible() || !view->enabled() ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    const float row_height = item_row_height(*view, scaled_ui_metric(kEditorUiMetrics.row_height));
    if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        const auto viewport=item_scroll_geometry(*list,row_height).viewport;
        const auto row=hit_test_linear_row(viewport, point, row_height,
            list->top_index, list->items.size(), 0.0F);
        if (row && viewport.y+(*row-list->top_index+1)*row_height <= viewport.y+viewport.height)
            return row;
        return std::nullopt;
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(view))
        return hit_test_table_row(*table, point, row_height);
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(view))
        return hit_test_tree_row(*tree, tree->bounds(), point, row_height, 0.0F);
    return std::nullopt;
}

static bool handle_item_view_activation(const WidgetPtr& view, const Point point,
                                        InlineEditStateMachine* edit,
                                        const Modifiers modifiers,
                                        const bool single_click) {
    const auto index = hit_test_item_view(view, point);
    if (!index) return false;
    const float row_height = item_row_height(
        *view, scaled_ui_metric(kEditorUiMetrics.row_height));
    if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        if (single_click) return false;
        if (list->checkable) {
            const float y = list->bounds().y +
                static_cast<float>(*index - list->top_index) * row_height;
            const auto check = linear_row_check_box_geometry(
                list->bounds(), y, row_height, scaled_ui_metric(6),
                scaled_ui_metric(13));
            if (check && point_inside(*check, point)) return false;
        }
        return activate_list_item(*list, *index, modifiers);
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        if (table->checkable) {
            const auto rect = table_row_geometry(*table, *index, row_height);
            const auto check = linear_row_check_box_geometry(
                {rect.x - table->horizontal_offset, table->bounds().y,
                 rect.width, table->bounds().height},
                rect.y, rect.height, scaled_ui_metric(5),
                scaled_ui_metric(13));
            if (check && point_inside(*check, point)) return false;
        }
        const auto column = hit_test_table_column(*table, point.x);
        if (!column || *column >= table->rows[*index].cells.size() ||
            !table->rows[*index].cells[*column].enabled) return false;
        // InlineEditStateMachine と同じ行の識別規則を使う。 on_cell_activate
        // をまたいで、行・セル内部への参照を保持しない。
        std::vector<std::pair<std::string, std::string>> original;
        const bool edit_enabled = single_click
            ? table->edit_on_single_click : table->edit_on_double_click;
        const bool wants_edit = edit && !edit->active() && edit_enabled &&
            table->on_cell_edited;
        if (wants_edit)
            for (const auto& cell : table->rows[*index].cells)
                original.emplace_back(cell.text, cell.value);
        const bool activated = single_click ? false
            : activate_table_cell(*table, *index, *column, modifiers);
        if (!wants_edit || edit->active() || !table->visible() || !table->enabled() ||
            !edit_enabled || *index >= table->rows.size()) return activated;
        const auto& cells = table->rows[*index].cells;
        if (cells.size() != original.size()) return activated;
        for (std::size_t i = 0; i < cells.size(); ++i)
            if (original[i] != std::pair{cells[i].text, cells[i].value}) return activated;
        return edit->begin(table, *index, *column) || activated;
    }
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(view)) {
        if (single_click) return false;
        const auto visible = visible_tree_indices(*tree);
        const auto position = std::ranges::find(visible, *index);
        if (position != visible.end()) {
            const auto geometry = tree_row_geometry(
                *tree, tree->bounds(), *index,
                tree->bounds().y +
                    static_cast<float>(position - visible.begin() -
                                       static_cast<std::ptrdiff_t>(tree->top_index)) *
                        row_height,
                row_height);
            if (geometry &&
                hit_test_tree_row_part(*geometry, point) == TreeRowPart::CheckBox)
                return false;
        }
        const auto node = tree->nodes[*index];
        bool expanded = false;
        if (tree->show_expanders || node.suppress_expander)
            expanded = set_tree_expanded(*tree, node.id, node.suppress_expander || !node.expanded);
        if (!tree->visible() || !tree->enabled()) return expanded;
        // 展開・遅延読み込みのコールバックが、ノードを除去・並べ替えする場合がある。
        return activate_tree_item(*tree, node.id, modifiers) || expanded;
    }
    return false;
}

bool handle_item_view_double_click(const WidgetPtr& view, const Point point,
                                  InlineEditStateMachine* edit,
                                  const Modifiers modifiers) {
    return handle_item_view_activation(view, point, edit, modifiers, false);
}

bool handle_item_view_single_click(const WidgetPtr& view, const Point point,
                                  InlineEditStateMachine* edit,
                                  const Modifiers modifiers,
                                  const SelectionInput input) {
    const auto table = std::dynamic_pointer_cast<TableView>(view);
    const auto row = hit_test_item_view(view, point);
    const auto column = table ? hit_test_table_column(*table, point.x)
                              : std::nullopt;
    if (!table || !row || !column || *row >= table->rows.size() ||
        *column >= table->rows[*row].cells.size() ||
        !table->rows[*row].cells[*column].enabled) {
        return handle_item_view_click(view, point, input);
    }

    const auto order_revision = table->row_order_revision;
    const auto update_revision = table->row_update_revision;
    std::vector<std::pair<std::string, std::string>> original;
    original.reserve(table->rows[*row].cells.size());
    for (const auto& cell : table->rows[*row].cells)
        original.emplace_back(cell.text, cell.value);
    const bool selected = handle_item_view_click(view, point, input);

    // 選択リスナーが行・列を置き換える場合がある。 編集前に、
    // ダブルクリックでの有効化と同じ行・セルの識別確認を行う。
    if (!table->visible() || !table->enabled() ||
        table->row_order_revision != order_revision ||
        table->row_update_revision != update_revision ||
        *row >= table->rows.size() ||
        hit_test_item_view(view, point) != row ||
        hit_test_table_column(*table, point.x) != column ||
        std::ranges::find(table->selection, *row) == table->selection.end())
        return selected;
    const auto& cells = table->rows[*row].cells;
    if (cells.size() != original.size()) return selected;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (original[i] != std::pair{cells[i].text, cells[i].value})
            return selected;
    return handle_item_view_activation(view, point, edit, modifiers, true) ||
           selected;
}

bool widget_in_input_scope(const WidgetPtr& root, const WidgetPtr& widget) {
    return widget && visit_menu_scope(root, [&](const WidgetPtr& candidate) { return candidate == widget; });
}

std::vector<WidgetPtr> input_scope_path(const WidgetPtr& root, const WidgetPtr& widget) {
    std::vector<WidgetPtr> path;
    if (widget) (void)visit_menu_scope(root, [&](const WidgetPtr& candidate) {
        return candidate == widget;
    }, true, &path);
    return path;
}

bool dispatch_widget_pointer(const WidgetPtr& target, PointerEvent& event) {
    if (!target) return false;
    if (std::dynamic_pointer_cast<TableView>(target) ||
        std::dynamic_pointer_cast<ListView>(target) || std::dynamic_pointer_cast<TreeView>(target) ||
        std::dynamic_pointer_cast<TabView>(target)) {
        event.item_hit_test_available = true;
        event.item_index = -1;
        if (const auto tabs=std::dynamic_pointer_cast<TabView>(target)) {
            if (const auto page=hit_test_tab_page(*tabs,event.position))
                event.item_index=static_cast<std::int64_t>(*page);
        } else if (const auto row = hit_test_item_view(target, event.position)) {
            if (const auto tree = std::dynamic_pointer_cast<TreeView>(target))
                event.item_index = static_cast<std::int64_t>(tree->nodes[*row].id);
            else event.item_index = static_cast<std::int64_t>(*row);
        }
    }
    if (target->on_pointer) {
        const auto notify = target->on_pointer;
        notify(event);
    }
    return event.handled;
}

WidgetPtr HoverStateMachine::hovered() const {
    const auto value = hovered_.lock();
    return widget_in_input_scope(scope_.lock(), value) ? value : WidgetPtr{};
}

void HoverStateMachine::update(const WidgetPtr& scope, const WidgetPtr& next,
                              PointerEvent event,
                              const TransitionObserver& before_transition) {
    update_impl(scope, next, event, before_transition, false);
}

void HoverStateMachine::update_impl(const WidgetPtr& scope, const WidgetPtr& next,
                                    PointerEvent event,
                                    const TransitionObserver& before_transition,
                                    const bool balance_previous) {
    const auto generation = ++generation_;
    if (pointer_type_!=event.pointer_type || pointer_id_!=event.pointer_id) tooltip_.cancel();
    pointer_type_=event.pointer_type; pointer_id_=event.pointer_id;
    position_ = event.position;
    const auto target = widget_in_input_scope(scope, next) ? next : WidgetPtr{};
    const auto previous = hovered_.lock();
    const auto previous_scope = scope_.lock();
    if (previous == target && (!target || scope_.lock() == scope)) {
        if (!target) scope_.reset();
        return;
    }
    hovered_.reset();
    scope_ = scope;
    tooltip_.cancel();
    const auto notify = [&](const WidgetPtr& widget, PointerAction action,
                            const bool balance_detached = false) {
        auto transition = event;
        transition.action = action;
        transition.handled = false;
        transition.item_hit_test_available = false;
        transition.item_index = -1;
        // Enter中に切り離されたウィジェットや、閉じるウィンドウの最
        // 後のホバーは、 入力範囲外でも対応する終了処理を行う。 これら
        // は寿命の後処理の遷移であり、表示先へのポインター入力ではない。
        if (!balance_detached && before_transition &&
            !before_transition(transition, target))
            return false;
        if (generation != generation_) return false;
        // 表示全体のフィルターが、ネイティブのLeave前に古いコ
        // ントロールを隠す場合がある。 無効になった対象へ入力を送
        // らない。全体の後処理やウィンドウ間移動で 移動先がnull
        // ・別の範囲になっても、Leaveは元の範囲に属する。
        const auto& notification_scope = action == PointerAction::Leave ? previous_scope : scope;
        if (!balance_detached && !widget_in_input_scope(notification_scope, widget))
            return action == PointerAction::Leave;
        (void)dispatch_widget_pointer(widget, transition);
        return generation == generation_;
    };
    if (previous && (balance_previous || widget_in_input_scope(previous_scope, previous)) &&
        !notify(previous, PointerAction::Leave, balance_previous)) {
        if (generation == generation_) scope_.reset();
        return;
    }
    if (generation != generation_) return;
    if (!widget_in_input_scope(scope, target)) { scope_.reset(); return; }
    hovered_ = target;
    scope_ = scope;
    if (target && !notify(target, PointerAction::Enter)) {
        if (generation == generation_) {
            hovered_.reset();
            scope_.reset();
        }
        return;
    }
    if (generation == generation_ && !widget_in_input_scope(scope, target)) {
        // Enter中に、除去・非表示になった対象が重ね表示を開く場合
        // がある。 開いたまま残さず、直ちに対応する終了処理を行う。
        hovered_.reset();
        scope_.reset();
        notify(target, PointerAction::Leave, true);
    }
}

void HoverStateMachine::clear_for(const WidgetPtr& scope, PointerEvent event) {
    // 以前の有効なホバー対象へLeaveを送る間は、元の範囲を保持する。
    if (scope_.lock() == scope) update(scope, {}, event);
}
void HoverStateMachine::clear_for_retirement(const WidgetPtr& scope, PointerEvent event) {
    if (scope_.lock() == scope) update_impl({}, {}, event, {}, true);
}
void HoverStateMachine::clear_pointer(const WidgetPtr& scope, PointerEvent event) {
    if (pointer_type_==event.pointer_type && pointer_id_==event.pointer_id)
        clear_for(scope,event);
}

bool select_context_item(const WidgetPtr& view, const Point point) {
    const auto row = hit_test_item_view(view, point);
    if (!row || !view->enabled() || !view->visible()) return false;
    if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        if (std::ranges::find(table->selection, *row) == table->selection.end())
            return select_table_rows(*table, {*row});
    } else if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        if (std::ranges::find(list->selection, *row) == list->selection.end())
            return select_list_rows(*list, {*row});
    } else if (const auto tree = std::dynamic_pointer_cast<TreeView>(view)) {
        const auto id = tree->nodes[*row].id;
        if (std::ranges::find(tree->selection, id) == tree->selection.end())
            return select_tree_items(*tree, {id});
    }
    return false;
}

bool handle_item_view_click(const WidgetPtr& view, const Point point, const SelectionInput input) {
    if (!view || !view->visible() || !view->enabled() ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !point_inside(view->bounds(), point)) return false;
    const float row_height = item_row_height(*view, scaled_ui_metric(kEditorUiMetrics.row_height));
    ItemScrollBarStateMachine scroll;
    if (scroll.begin(view, point)) return true;
    if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        const auto row = hit_test_item_view(view, point);
        if (!row || !list->items[*row].enabled) return false;
        const float y = list->bounds().y +
            static_cast<float>(*row-list->top_index) * row_height;
        const auto check = list->checkable ? linear_row_check_box_geometry(
            list->bounds(), y, row_height, scaled_ui_metric(6), scaled_ui_metric(13)) : std::nullopt;
        if (check && point_inside(*check, point))
            (void)set_list_item_checked(*list, *row, !list->items[*row].checked);
        (void)update_list_selection(*list, *row, input);
        return true;
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        if (const auto row = hit_test_item_view(view, point)) {
            const auto rect = table_row_geometry(*table, *row, row_height);
            const auto check = table->checkable ? linear_row_check_box_geometry(
                {rect.x-table->horizontal_offset, table->bounds().y, rect.width, table->bounds().height},
                rect.y, rect.height, scaled_ui_metric(5), scaled_ui_metric(13)) : std::nullopt;
            if (check && point_inside(*check, point)) {
                // チェックのリスナーは表を置き換えられる。 TreeView
                // と異なり汎用の行識別情報がないため、
                // 古い位置で新しい表の行を選択しない。
                const auto order_revision = table->row_order_revision;
                const auto update_revision = table->row_update_revision;
                (void)set_table_row_checked(*table, *row, !table->rows[*row].checked);
                if (table->row_order_revision != order_revision ||
                    table->row_update_revision != update_revision + 1U)
                    return true;
            }
            (void)update_table_selection(*table, *row, input);
            return true;
        }
        if (point.y < table->bounds().y + table_header_height(*table, row_height)) {
            if (const auto column = hit_test_table_column(*table, point.x)) {
                (void)request_table_sort(*table, *column);
                return true;
            }
        }
        return false;
    }
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(view))
        return click_tree_at(*tree, point, input);
    return false;
}

bool click_tree_at(TreeView& tree, const Point point, const SelectionInput input) {
    (void)set_item_horizontal_offset(tree,tree.horizontal_offset);
    if (!tree.visible() || !tree.enabled() ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    const auto bounds = tree.bounds();
    const float row_height = item_row_height(tree, scaled_ui_metric(kEditorUiMetrics.row_height));
    constexpr float inset = 0.0F;
    auto visible = visible_tree_indices(tree);
    const auto hit = hit_test_tree_row(tree,bounds,point,row_height,inset);
    if (!hit) return false;
    const auto index = *hit;
    const auto position=std::ranges::find(visible,index)-visible.begin();
    const auto geometry = tree_row_geometry(tree, bounds, index,
        bounds.y + inset + static_cast<float>(position - tree.top_index) * row_height,
        row_height);
    const auto part = geometry ? hit_test_tree_row_part(*geometry, point) : std::nullopt;
    if (!part) return false;
    const auto id = tree.nodes[index].id;
    if (part == TreeRowPart::Expander) {
        (void)set_tree_expanded(tree, id, !tree.nodes[index].expanded);
        return true;
    }
    if (part == TreeRowPart::CheckBox) {
        (void)set_tree_item_checked(tree, id, !tree.nodes[index].checked);
        // on_check_changed がクリック項目を除去・並べ替え・非表示にし
        // たり、ビューを閉じたりする場合がある。 古い行位置にある別の項目を選ばない。
        if (!tree.visible() || !tree.enabled()) return true;
        visible = visible_tree_indices(tree);
    }
    std::vector<TreeItemId> order;
    order.reserve(visible.size());
    for (const auto current : visible) order.push_back(tree.nodes[current].id);
    (void)update_tree_selection(tree, id, order, input);
    return true;
}

std::size_t clamp_top_index(const std::size_t current,
                            const std::size_t item_count,
                            const std::size_t visible_count) noexcept {
    const std::size_t maximum = item_count > visible_count
                                    ? item_count - visible_count : 0U;
    return std::min(current, maximum);
}

std::size_t visible_row_count(const float viewport_height,
                              const float row_height,
                              const std::size_t reserved_rows) noexcept {
    if (!std::isfinite(viewport_height) || !std::isfinite(row_height) ||
        viewport_height <= 0.0F || row_height <= 0.0F)
        return 1U;
    const auto total = static_cast<std::size_t>(
        std::floor(viewport_height / row_height));
    if (total <= reserved_rows) return 1U;
    return total - reserved_rows;
}

bool set_top_index(std::size_t& current, const std::size_t requested,
                   const std::size_t item_count,
                   const std::size_t visible_count) noexcept {
    const auto next = clamp_top_index(requested, item_count, visible_count);
    if (next == current) return false;
    current = next;
    return true;
}

bool scroll_top_index(std::size_t& current, const std::int64_t delta,
                      const std::size_t item_count,
                      const std::size_t visible_count) noexcept {
    const std::size_t maximum = item_count > visible_count
                                    ? item_count - visible_count : 0U;
    const auto requested = static_cast<std::int64_t>(current) + delta;
    const auto next = static_cast<std::size_t>(std::clamp<std::int64_t>(
        requested, 0, static_cast<std::int64_t>(maximum)));
    return set_top_index(current, next, item_count, visible_count);
}

bool scroll_item_view_rows(const WidgetPtr& view, const std::int64_t delta,
                           const float row_height) noexcept {
    if (!view || delta == 0 || !std::isfinite(row_height) ||
        row_height <= 0.0F)
        return false;
    if (const auto list = std::dynamic_pointer_cast<ListView>(view)) {
        const auto visible = item_scroll_geometry(*list,row_height).visible_rows;
        return scroll_top_index(list->top_index, delta, list->items.size(), visible);
    }
    if (const auto table = std::dynamic_pointer_cast<TableView>(view)) {
        const auto visible = table_visible_row_count(*table, row_height);
        return scroll_top_index(table->top_index, delta, table->rows.size(), visible);
    }
    if (const auto tree = std::dynamic_pointer_cast<TreeView>(view)) {
        const auto rows = visible_tree_indices(*tree);
        const auto visible = tree_visible_row_count(*tree, row_height);
        return scroll_top_index(tree->top_index, delta, rows.size(), visible);
    }
    return false;
}

bool set_item_view_top_index(Widget& view, const std::size_t requested,
                             const std::size_t visible_count) noexcept {
    if (auto* list = dynamic_cast<ListView*>(&view))
        return set_top_index(list->top_index, requested, list->items.size(),
                             visible_count);
    if (auto* table = dynamic_cast<TableView*>(&view)) {
        const bool moved = set_top_index(table->top_index, requested,
                                        table->rows.size(), visible_count);
        if (moved) {
            // リスナーが所有者を無効化し得る呼び出しの後は、表にアクセスしない。
            try { const auto notify = table->on_scroll_changed; if (notify) notify(); }
            catch (...) { /* スクロール位置は確定済み。 */ }
        }
        return moved;
    }
    if (auto* tree = dynamic_cast<TreeView*>(&view))
        return set_top_index(tree->top_index, requested,
                             visible_tree_indices(*tree).size(), visible_count);
    return false;
}

std::optional<std::size_t> tree_visible_position(const TreeView& tree,
                                                 const TreeItemId id) {
    const auto order = visible_tree_indices(tree);
    const auto found = std::ranges::find_if(order, [&](const std::size_t index) {
        return index < tree.nodes.size() && tree.nodes[index].id == id;
    });
    if (found == order.end()) return std::nullopt;
    return static_cast<std::size_t>(found - order.begin());
}

bool reveal_tree_item(TreeView& tree, const TreeItemId id, const float row_height) {
    const auto order = visible_tree_indices(tree);
    const auto found = std::ranges::find_if(order, [&](const auto index) { return tree.nodes[index].id == id; });
    if (found == order.end()) return false;
    const auto height = row_height > 0 ? row_height : scaled_ui_metric(20);
    return reveal_selection(tree.top_index, static_cast<std::size_t>(found - order.begin()),
                            order.size(), tree_visible_row_count(tree, height));
}

bool reveal_tree_item_with_ancestors(TreeView& tree, const TreeItemId id,
                                     const float row_height) {
    const auto target = std::ranges::find(tree.nodes, id, &TreeNode::id);
    if (target == tree.nodes.end()) return false;

    std::vector<TreeItemId> ancestors;
    TreeItemId parent = target->parent;
    for (std::size_t count = 0; parent != 0U; ++count) {
        // 不正・循環する所有関係で、フォーカス経路を停止させない。
        if (count >= tree.nodes.size()) return false;
        const auto found = std::ranges::find(tree.nodes, parent, &TreeNode::id);
        if (found == tree.nodes.end()) return false;
        ancestors.push_back(parent);
        parent = found->parent;
    }
    std::ranges::reverse(ancestors);

    const auto path_is_current = [&]() {
        const auto current_target =
            std::ranges::find(tree.nodes, id, &TreeNode::id);
        if (current_target == tree.nodes.end()) return false;
        TreeItemId expected_parent{};
        for (const auto ancestor_id : ancestors) {
            const auto current_ancestor = std::ranges::find(
                tree.nodes, ancestor_id, &TreeNode::id);
            if (current_ancestor == tree.nodes.end() ||
                current_ancestor->parent != expected_parent) return false;
            expected_parent = ancestor_id;
        }
        return current_target->parent == expected_parent;
    };
    const auto prefix_is_open = [&](const std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto ancestor = std::ranges::find(
                tree.nodes, ancestors[index], &TreeNode::id);
            if (ancestor == tree.nodes.end() ||
                (!ancestor->expanded && !ancestor->suppress_expander)) return false;
        }
        return true;
    };

    if (!path_is_current()) return false;
    for (std::size_t index = 0; index < ancestors.size(); ++index) {
        const auto ancestor_id = ancestors[index];
        if (!path_is_current()) return false;
        auto ancestor = std::ranges::find(tree.nodes, ancestor_id, &TreeNode::id);
        if (ancestor == tree.nodes.end()) return false;
        if (!ancestor->expanded && !ancestor->suppress_expander)
            (void)set_tree_expanded(tree, ancestor_id, true);
        if (!path_is_current()) return false;
        ancestor = std::ranges::find(tree.nodes, ancestor_id, &TreeNode::id);
        if (ancestor == tree.nodes.end() ||
            (!ancestor->expanded && !ancestor->suppress_expander) ||
            !prefix_is_open(index + 1U)) return false;
    }

    const auto visible = visible_tree_indices(tree);
    if (std::ranges::none_of(visible, [&](const auto index) {
            return tree.nodes[index].id == id;
        })) return false;
    (void)reveal_tree_item(tree, id, row_height);
    return true;
}

bool set_scroll_offset(ScrollView& scroll, Point offset,
                       const Size content_size, const Size viewport_size) {
    const float maximum_x = scroll.horizontal_scroll
        ? std::max(0.0F, content_size.width - viewport_size.width) : 0.0F;
    const float maximum_y = scroll.vertical_scroll
        ? std::max(0.0F, content_size.height - viewport_size.height) : 0.0F;
    offset.x = std::clamp(offset.x, 0.0F, maximum_x);
    offset.y = std::clamp(offset.y, 0.0F, maximum_y);
    if (scroll.scroll_offset.x == offset.x && scroll.scroll_offset.y == offset.y)
        return false;
    scroll.scroll_offset = offset;
    return true;
}

bool scroll_by(ScrollView& scroll, const Point delta,
               const Size content_size, const Size viewport_size) {
    return set_scroll_offset(scroll,
                             {scroll.scroll_offset.x + delta.x,
                              scroll.scroll_offset.y + delta.y},
                             content_size, viewport_size);
}

Size scroll_content_size(const ScrollView& scroll) noexcept {
    if (scroll.content_extent) return *scroll.content_extent;
    float right = scroll.bounds().x + scroll.bounds().width;
    float bottom = scroll.bounds().y + scroll.bounds().height;
    for (const auto& child : scroll.children()) {
        if (!child || !child->visible()) continue;
        const auto bounds = child->bounds();
        // 配置済みの ScrollView の子には、モデルの負のオフセ
        // ットが適用済み。 不変の内容寸法を計測する前に、その分を戻す。
        right = std::max(right, bounds.x + bounds.width + scroll.scroll_offset.x);
        bottom = std::max(bottom, bounds.y + bounds.height + scroll.scroll_offset.y);
    }
    return {
        std::max(0.0F, right - scroll.bounds().x),
        std::max(0.0F, bottom - scroll.bounds().y),
    };
}

bool point_inside(const Rect bounds, const Point point) noexcept {
    return point.x >= bounds.x && point.y >= bounds.y &&
           point.x < bounds.x + bounds.width &&
           point.y < bounds.y + bounds.height;
}

bool drag_threshold_reached(const Point start, const Point current,
                            const float threshold) noexcept {
    if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(current.x) || !std::isfinite(current.y) ||
        !std::isfinite(threshold))
        return false;
    const float distance = std::max(0.0F, threshold);
    const float dx = current.x - start.x;
    const float dy = current.y - start.y;
    return dx * dx + dy * dy >= distance * distance;
}

void PointerGestureStateMachine::begin(const Point point) noexcept {
    start_ = point;
    last_ = point;
    initialized_ = true;
    active_ = false;
}

PointerGestureUpdate PointerGestureStateMachine::update(
    const Point point, const float threshold, const bool eligible) noexcept {
    if (!initialized_) begin(point);
    PointerGestureUpdate result;
    result.delta = {point.x - last_.x, point.y - last_.y};
    if (!active_ && eligible && drag_threshold_reached(start_, point, threshold)) {
        active_ = true;
        result.began = true;
    }
    result.active = active_;
    last_ = point;
    return result;
}

void PointerGestureStateMachine::reset() noexcept {
    start_ = {};
    last_ = {};
    initialized_ = false;
    active_ = false;
}

std::optional<std::size_t> hit_test_linear_row(
    const Rect bounds, const Point point, const float row_height,
    const std::size_t top_index, const std::size_t item_count,
    const float header_height) noexcept {
    if (row_height <= 0.0F || !point_inside(bounds, point) ||
        point.y < bounds.y + header_height)
        return std::nullopt;
    const auto relative = point.y - bounds.y - header_height;
    const std::size_t index = top_index + static_cast<std::size_t>(
        std::floor(relative / row_height));
    return index < item_count ? std::optional<std::size_t>(index) : std::nullopt;
}

std::optional<Rect> linear_row_check_box_geometry(
    const Rect bounds, const float row_y, const float row_height,
    const float leading_inset, const float check_box_size) noexcept {
    if (row_height <= 0.0F || check_box_size <= 0.0F ||
        check_box_size > row_height || bounds.width <= 0.0F ||
        bounds.height <= 0.0F) {
        return std::nullopt;
    }
    const Rect check_box{
        bounds.x + std::max(0.0F, leading_inset),
        row_y + (row_height - check_box_size) * 0.5F,
        check_box_size,
        check_box_size,
    };
    if (check_box.x + check_box.width > bounds.x + bounds.width ||
        check_box.y < bounds.y ||
        check_box.y + check_box.height > bounds.y + bounds.height) {
        return std::nullopt;
    }
    return check_box;
}

float item_row_height(const Widget& view, float fallback) noexcept {
    const auto* font = view.font() ? &*view.font() : nullptr;
    if (const auto* table = dynamic_cast<const TableView*>(&view);
        table && std::isfinite(table->row_height) && table->row_height > 0)
        return ui_text_line_height(table->row_height, 4, font);
    if (!std::isfinite(fallback) || fallback <= 0)
        fallback = scaled_ui_metric(kEditorUiMetrics.row_height);
    return std::max(fallback, ui_text_line_height(kEditorUiMetrics.row_height, 4, font));
}

float table_row_height(const TableView& table, float fallback) noexcept {
    return item_row_height(table, fallback);
}

float table_header_height(const TableView& table, float fallback) noexcept {
    if (!table.header_visible) return 0;
    if (std::isfinite(table.header_height) && table.header_height > 0)
        return ui_text_line_height(table.header_height, 4, table.font() ? &*table.font() : nullptr);
    return table_row_height(table, fallback);
}

std::size_t table_visible_row_count(const TableView& table, float row_height) noexcept {
    return item_scroll_geometry(table,row_height).visible_rows;
}

Rect table_row_geometry(const TableView& table, std::size_t row, float row_height) noexcept {
    row_height = table_row_height(table, row_height);
    const float delta = row >= table.top_index ? static_cast<float>(row-table.top_index)
                                               : -static_cast<float>(table.top_index-row);
    return {table.bounds().x, table.bounds().y + table_header_height(table, row_height) + delta * row_height,
            table.bounds().width, row_height};
}

std::optional<std::size_t> hit_test_table_row(const TableView& table, Point point, float row_height) noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    const auto viewport = table_horizontal_scroll_geometry(table).viewport;
    const auto row = hit_test_linear_row(viewport, point, table_row_height(table, row_height),
                                         table.top_index, table.rows.size(), table_header_height(table, row_height));
    if (row) {
        const auto rect = table_row_geometry(table, *row, row_height);
        if (rect.y + rect.height <= viewport.y + viewport.height) return row;
    }
    return std::nullopt;
}
std::optional<std::size_t> hit_test_table_drop_row(
    const TableView& table, Point point, float row_height) noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    const auto viewport = table_horizontal_scroll_geometry(table).viewport;
    return hit_test_linear_row(viewport, point, table_row_height(table, row_height),
                               table.top_index, table.rows.size(),
                               table_header_height(table, row_height));
}

std::vector<float> resolved_table_column_widths(
    const TableView& table, const float available_width,
    const float fallback_width, const float minimum_width) {
    std::vector<float> result;
    result.reserve(table.columns.size());
    const bool relative = !table.columns.empty() &&
        std::ranges::all_of(table.columns, [](const TableColumn& column) {
            return column.width > 0.0F && column.width <= 1.0F;
        });
    const float relative_sum = relative
        ? std::accumulate(table.columns.begin(), table.columns.end(), 0.0F,
                          [](const float total, const TableColumn& column) {
                              return total + column.width;
                          })
        : 0.0F;
    const float scale = editor_ui_text_scale();
    float fixed = 0, weight = 0;
    for (const auto& column : table.columns) {
        if (column.stretch > 0) weight += column.stretch;
        else fixed += std::max(minimum_width, column.width > 0 ? column.width : fallback_width) * scale;
    }
    for (const auto& column : table.columns) {
        const float width = relative && relative_sum > 0.0F
            ? std::max(minimum_width * scale,
                       std::max(0.0F, available_width) * column.width / relative_sum)
            : column.stretch > 0 && weight > 0
                ? std::max(std::max(minimum_width, column.width) * scale, std::max(0.0F, available_width-fixed) * column.stretch / weight)
                : std::max(minimum_width, column.width > 0.0F ? column.width : fallback_width) * scale;
        result.push_back(width);
    }
    return result;
}

HorizontalScrollGeometry horizontal_scroll_geometry(const Rect bounds, const float total, const float value) {
    HorizontalScrollGeometry result;
    result.viewport = bounds;
    if (!std::isfinite(total) || total <= result.viewport.width + 0.5F ||
        result.viewport.width <= 0 || result.viewport.height <= 0) return result;
    result.maximum = total - result.viewport.width;
    const float height = std::min(result.viewport.height, editor_scrollbar_extent());
    result.viewport.height -= height;
    result.track = {result.viewport.x, result.viewport.y + result.viewport.height,
                    result.viewport.width, height};
    const float thumb_width = std::min(result.track.width,
        std::max(scaled_ui_metric(24), result.track.width * result.track.width / total));
    const float offset = std::isfinite(value) ? std::clamp(value, 0.0F, result.maximum) : 0.0F;
    result.thumb = {result.track.x + (result.track.width - thumb_width) * offset / result.maximum,
                    result.track.y, thumb_width, height};
    return result;
}

ItemScrollGeometry pixel_scroll_geometry(Rect bounds, Size content, Point offset,
                                         bool allow_horizontal, bool allow_vertical, bool reserve_gutters) {
    ItemScrollGeometry result;
    result.viewport = bounds;
    if (!std::isfinite(bounds.width) || !std::isfinite(bounds.height) || bounds.width <= 0 || bounds.height <= 0)
        return result;
    if (!std::isfinite(content.width)) content.width = 0;
    if (!std::isfinite(content.height)) content.height = 0;
    const float thickness = editor_scrollbar_extent();
    const float bw = std::min(bounds.width, thickness), bh = std::min(bounds.height, thickness);
    bool horizontal = reserve_gutters && allow_horizontal, vertical = reserve_gutters && allow_vertical;
    for (int pass = 0; pass < 3; ++pass) {
        horizontal = allow_horizontal && (horizontal || content.width > bounds.width - (vertical ? bw : 0) + 0.5F);
        vertical = allow_vertical && (vertical || content.height > bounds.height - (horizontal ? bh : 0) + 0.5F);
    }
    result.viewport.width -= vertical ? bw : 0;
    result.viewport.height -= horizontal ? bh : 0;
    const auto axis = [](Rect track, Rect viewport, float extent, float visible, float value, bool vertical) {
        HorizontalScrollGeometry bar;
        bar.viewport = viewport;
        bar.maximum = std::max(0.0F, extent - visible);
        if (bar.maximum <= 0) return bar;
        bar.track = track;
        const float length = vertical ? track.height : track.width;
        const float thumb = std::min(length, std::max(scaled_ui_metric(24), length * visible / extent));
        const float position = (length - thumb) * (std::isfinite(value) ? std::clamp(value, 0.0F, bar.maximum) : 0) / bar.maximum;
        bar.thumb = vertical ? Rect{track.x, track.y + position, track.width, thumb}
                             : Rect{track.x + position, track.y, thumb, track.height};
        return bar;
    };
    if (horizontal) result.horizontal = axis({bounds.x, bounds.y + result.viewport.height, result.viewport.width, bh},
        result.viewport, content.width, result.viewport.width, offset.x, false);
    if (vertical) result.vertical = axis({bounds.x + result.viewport.width, bounds.y, bw, result.viewport.height},
        result.viewport, content.height, result.viewport.height, offset.y, true);
    return result;
}

ItemScrollGeometry item_scroll_geometry(const Widget& view, float row_height, std::optional<Rect> requested_bounds,
                                       std::optional<std::size_t> planned_row_count) {
    if (const auto* scroll = dynamic_cast<const ScrollView*>(&view))
        return pixel_scroll_geometry(requested_bounds.value_or(view.bounds()), scroll_content_size(*scroll),
            scroll->scroll_offset, scroll->horizontal_scroll, scroll->vertical_scroll);
    if (const auto* window = dynamic_cast<const Window*>(&view); window && window->kind() == ComponentKind::Dialog)
        return pixel_scroll_geometry(requested_bounds.value_or(view.bounds()), window->content_extent, window->content_offset);
    ItemScrollGeometry result;
    const Rect bounds=requested_bounds.value_or(view.bounds());
    result.viewport=bounds;
    const auto* table=dynamic_cast<const TableView*>(&view);
    const auto* tree=dynamic_cast<const TreeView*>(&view);
    const auto* list=dynamic_cast<const ListView*>(&view);
    if (!table && !tree && !list) return result;
    row_height=item_row_height(view,row_height);
    const float header=table ? table_header_height(*table,row_height) : 0.0F;
    result.total_rows=planned_row_count ? *planned_row_count :
        table ? table->rows.size() : tree ? visible_tree_indices(*tree).size() : list->items.size();
    const auto top=table ? table->top_index : tree ? tree->top_index : list->top_index;
    const float offset=table ? table->horizontal_offset : tree ? tree->horizontal_offset : 0;
    if (!std::isfinite(bounds.width) || !std::isfinite(bounds.height) || bounds.width<=0 || bounds.height<=0)
        return result;
    const float vertical_extra = table ?
        scaled_ui_metric(std::max(0.0F, table->vertical_scrollbar_extra_width)) : 0.0F;
    const float bar_width=std::min(bounds.width,editor_scrollbar_extent()+vertical_extra);
    const float bar_height=std::min(bounds.height,editor_scrollbar_extent());
    const float tree_width=tree ? tree_content_width(*tree) : 0;
    bool vertical=false,horizontal=false;
    float total_width=0;
    for (int pass=0;pass<3;++pass) {
        const float width=bounds.width-(vertical ? bar_width : 0);
        if (table) {
            const auto widths=resolved_table_column_widths(*table,width);
            total_width=std::accumulate(widths.begin(),widths.end(),0.0F);
        } else total_width=tree_width;
        const bool horizontal_enabled=!table || table->horizontal_scroll_enabled;
        horizontal=horizontal_enabled && (horizontal || total_width>width+0.5F);
        const float height=bounds.height-(horizontal ? bar_height : 0);
        result.visible_rows=visible_row_count(std::max(0.0F,height-header),row_height);
        vertical=vertical || result.total_rows>result.visible_rows;
    }
    result.viewport={bounds.x,bounds.y,bounds.width-(vertical ? bar_width : 0),bounds.height-(horizontal ? bar_height : 0)};
    if (horizontal)
        result.horizontal=horizontal_scroll_geometry(
            {bounds.x,bounds.y,result.viewport.width,bounds.height},total_width,offset);
    result.horizontal.viewport=result.viewport;
    result.vertical.viewport=result.viewport;
    if (vertical) {
        auto& bar=result.vertical;
        bar.maximum=static_cast<float>(result.total_rows-result.visible_rows);
        bar.track={bounds.x+result.viewport.width,bounds.y+std::min(header,result.viewport.height),bar_width,
            std::max(0.0F,result.viewport.height-header)};
        const float length=std::min(bar.track.height,std::max(scaled_ui_metric(24),
            bar.track.height*static_cast<float>(result.visible_rows)/static_cast<float>(result.total_rows)));
        const float value=static_cast<float>(std::min(top,result.total_rows-result.visible_rows));
        bar.thumb={bar.track.x,bar.track.y+(bar.track.height-length)*value/bar.maximum,bar.track.width,length};
    }
    return result;
}

HorizontalScrollGeometry table_horizontal_scroll_geometry(const TableView& table) {
    return item_scroll_geometry(table).horizontal;
}

HorizontalScrollGeometry tree_horizontal_scroll_geometry(const TreeView& tree) {
    return item_scroll_geometry(tree).horizontal;
}

HorizontalScrollGeometry item_horizontal_scroll_geometry(const Widget& view) {
    if (const auto* table=dynamic_cast<const TableView*>(&view)) return table_horizontal_scroll_geometry(*table);
    if (const auto* tree=dynamic_cast<const TreeView*>(&view)) return tree_horizontal_scroll_geometry(*tree);
    return {view.bounds()};
}

namespace {
float* horizontal_offset_for(Widget& view) {
    if (auto* table=dynamic_cast<TableView*>(&view)) return &table->horizontal_offset;
    if (auto* tree=dynamic_cast<TreeView*>(&view)) return &tree->horizontal_offset;
    return nullptr;
}
std::optional<std::uint64_t> scroll_revision_for(const Widget& view) noexcept {
    if (const auto* container=dynamic_cast<const Container*>(&view))
        return container->scroll_update_revision;
    if (const auto* list=dynamic_cast<const ListView*>(&view))
        return list->scroll_update_revision;
    if (const auto* table=dynamic_cast<const TableView*>(&view))
        return table->scroll_update_revision;
    if (const auto* tree=dynamic_cast<const TreeView*>(&view))
        return tree->scroll_update_revision;
    return std::nullopt;
}
}

bool set_table_horizontal_offset(TableView& table, float value) {
    return set_item_horizontal_offset(table,value);
}

bool set_item_horizontal_offset(Widget& view, float value) {
    auto* offset=horizontal_offset_for(view);
    if (!offset) return false;
    if (!std::isfinite(value)) value = 0;
    value = std::clamp(value, 0.0F, item_horizontal_scroll_geometry(view).maximum);
    if (*offset == value) return false;
    *offset = value;
    if (auto* table = dynamic_cast<TableView*>(&view)) {
        try { const auto notify = table->on_scroll_changed; if (notify) notify(); }
        catch (...) { /* スクロール位置は確定済み。 */ }
    }
    return true;
}

namespace {
// つまみの押下状態は入力側が書き、フレーム準備中に読
// むため、 スレッドローカルではなく同期で保護する。
struct PressedScrollThumb {
    std::mutex mutex;
    std::weak_ptr<const void> token;
    std::weak_ptr<Widget> view;
    bool vertical{};
};
PressedScrollThumb& pressed_scroll_thumb_state() noexcept {
    static PressedScrollThumb state;
    return state;
}
} // namespace

bool scroll_thumb_pressed(const Widget& view, const bool vertical) noexcept {
    auto& state = pressed_scroll_thumb_state();
    const std::scoped_lock lock(state.mutex);
    if (state.token.expired() || state.vertical != vertical) return false;
    const auto pressed = state.view.lock();
    return pressed && pressed.get() == &view;
}

void ItemScrollBarStateMachine::reset() noexcept {
    pressed_token_.reset();
    view_.reset(); access_={}; grab_=scale_=maximum_=thumb_length_=expected_thumb_origin_=0;
    track_={}; vertical_=false; expected_revision_.reset(); container_generations_.clear(); mode_=Mode::None;
}

bool ItemScrollBarStateMachine::source_current(const WidgetPtr& view) const noexcept {
    if (!view || !view->enabled() || !view->visible() || scale_!=editor_ui_text_scale()) return false;
    try {
        if (access_.current && !access_.current()) return false;
    } catch (...) { return false; }
    const auto revision=scroll_revision_for(*view);
    if (expected_revision_ && (!revision || *revision!=*expected_revision_)) return false;
    for (const auto& generation:container_generations_) {
        const auto owner=generation.owner.lock();
        // 通常の再描画のための配置は、内容の置換ではない。
        if (!owner || owner->layout()!=generation.layout.lock() ||
            owner->scroll_update_revision!=generation.scroll ||
            owner->child_layout_provider_revision()!=generation.provider) return false;
        const auto bounds=owner->bounds();
        if ((generation.compare_bounds &&
             (bounds.x!=generation.bounds.x || bounds.y!=generation.bounds.y ||
              bounds.width!=generation.bounds.width || bounds.height!=generation.bounds.height)) ||
            owner->children().size()!=generation.children.size()) return false;
        for (std::size_t i=0;i<generation.children.size();++i)
            if (owner->children()[i]!=generation.children[i].lock()) return false;
    }
    return true;
}

bool ItemScrollBarStateMachine::remember_source(const WidgetPtr& view) noexcept {
    try {
        expected_revision_=view ? scroll_revision_for(*view) : std::nullopt;
        container_generations_.clear();
        std::unordered_set<const Widget*> visited;
        std::function<void(const WidgetPtr&)> gather=[&](const WidgetPtr& widget) {
            if (!widget || !visited.insert(widget.get()).second) return;
            if (const auto container=std::dynamic_pointer_cast<Container>(widget)) {
                ContainerGeneration generation;
                generation.owner=container;
                generation.scroll=container->scroll_update_revision;
                generation.provider=container->child_layout_provider_revision();
                generation.layout=container->layout();
                generation.bounds=container->bounds();
                generation.compare_bounds=widget==view;
                for (const auto& child:container->children()) generation.children.push_back(child);
                container_generations_.push_back(std::move(generation));
                for (const auto& child:container->children()) gather(child);
            }
            if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget))
                for (const auto& page:tabs->pages) gather(page.content);
        };
        gather(view);
        return true;
    } catch (...) { expected_revision_.reset(); container_generations_.clear(); return false; }
}

bool ItemScrollBarStateMachine::begin(const WidgetPtr& view, Point point, ScrollBarAccess access) {
    reset();
    if (!view || !view->enabled() || !view->visible() || !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    access_ = std::move(access);
    scale_=editor_ui_text_scale();
    try { if (access_.current && !access_.current()) { reset(); return false; } }
    catch (...) { reset(); return false; }
    ItemScrollGeometry scroll;
    try { scroll=access_.geometry ? access_.geometry() : item_scroll_geometry(*view); }
    catch (...) { reset(); return false; }
    vertical_=point_inside(scroll.vertical.track,point);
    const auto geometry=vertical_ ? scroll.vertical : scroll.horizontal;
    if (!point_inside(geometry.track, point)) {
        // 二つのスクロールバーが共有する角は、項目でも列見出しでもない。
        const bool corner=scroll.horizontal.maximum>0 && scroll.vertical.maximum>0 &&
            point_inside(view->bounds(),point) && point.x>=scroll.vertical.track.x && point.y>=scroll.horizontal.track.y;
        if (corner) {
            view_=view; mode_=Mode::Passive;
            if (!remember_source(view)) reset();
        }
        return corner;
    }
    view_=view;
    track_=geometry.track; maximum_=geometry.maximum;
    thumb_length_=vertical_ ? geometry.thumb.height : geometry.thumb.width;
    expected_thumb_origin_=vertical_ ? geometry.thumb.y : geometry.thumb.x;
    if (point_inside(geometry.thumb, point)) {
        mode_=Mode::Thumb;
        grab_ = vertical_ ? point.y-geometry.thumb.y : point.x - geometry.thumb.x;
        if (!remember_source(view)) { reset(); return true; }
        // つかんだつまみを押下色で描けるよう、状態を反映する。
        pressed_token_ = std::make_shared<int>(0);
        auto& state = pressed_scroll_thumb_state();
        const std::scoped_lock lock(state.mutex);
        state.token = pressed_token_;
        state.view = view;
        state.vertical = vertical_;
    } else {
        mode_=Mode::Page;
        if (access_.set_position) {
            const float page = vertical_ ? geometry.viewport.height : geometry.viewport.width;
            const bool before = vertical_ ? point.y < geometry.thumb.y : point.x < geometry.thumb.x;
            (void)access_.set_position(vertical_, before ? -page : page, true);
        } else if (const auto viewport = std::dynamic_pointer_cast<ScrollView>(view)) {
            auto offset = viewport->scroll_offset;
            if (vertical_) offset.y += point.y < geometry.thumb.y ? -geometry.viewport.height : geometry.viewport.height;
            else offset.x += point.x < geometry.thumb.x ? -geometry.viewport.width : geometry.viewport.width;
            (void)scroll_view_content(viewport, offset);
        } else if (const auto window = std::dynamic_pointer_cast<Window>(view)) {
            auto offset = window->content_offset;
            if (vertical_) offset.y += point.y < geometry.thumb.y ? -geometry.viewport.height : geometry.viewport.height;
            else offset.x += point.x < geometry.thumb.x ? -geometry.viewport.width : geometry.viewport.width;
            (void)scroll_window_content(window, offset);
        } else if (vertical_) {
            const auto page=static_cast<std::int64_t>(scroll.visible_rows);
            float remainder{};
            (void)scroll_item_view_by(view,{0,(point.y<geometry.thumb.y ? -page : page)*
                scaled_ui_metric(kEditorUiMetrics.row_height)},
                scaled_ui_metric(kEditorUiMetrics.row_height),remainder);
        } else if (auto* offset=horizontal_offset_for(*view)) {
            float remainder{};
            (void)scroll_item_view_by(view,{point.x < geometry.thumb.x ?
                -geometry.viewport.width : geometry.viewport.width,0},
                scaled_ui_metric(kEditorUiMetrics.row_height),remainder);
        }
        if (!remember_source(view)) { reset(); return true; }
    }
    return true;
}

bool ItemScrollBarStateMachine::move(Point point) {
    const auto view = owner();
    if (!source_current(view) || !std::isfinite(point.x) || !std::isfinite(point.y)) {
        reset(); return false;
    }
    if (mode_!=Mode::Thumb) return false;
    ItemScrollGeometry scroll;
    try { scroll=access_.geometry ? access_.geometry() : item_scroll_geometry(*view); }
    catch (...) { reset(); return false; }
    const auto geometry=vertical_ ? scroll.vertical : scroll.horizontal;
    if (track_.x != geometry.track.x || track_.y != geometry.track.y ||
        track_.width != geometry.track.width || track_.height != geometry.track.height ||
        maximum_!=geometry.maximum || thumb_length_!=(vertical_ ? geometry.thumb.height : geometry.thumb.width) ||
        expected_thumb_origin_!=(vertical_ ? geometry.thumb.y : geometry.thumb.x)) { reset(); return false; }
    const float travel = vertical_ ? geometry.track.height-geometry.thumb.height : geometry.track.width - geometry.thumb.width;
    if (travel <= 0) { reset(); return false; }
    const float value=std::clamp(((vertical_ ? point.y-geometry.track.y : point.x-geometry.track.x)-grab_)/travel,0.0F,1.0F)*geometry.maximum;
    bool moved=false;
    bool invalidated=false;
    // ポインター移動は一フレームに何度も届く。画素単位の
    // ScrollView やダイアログで毎回内容全体を配
    // 置・複製すると、 ホイールは軽くても、つまみのドラッ
    // グが遅くなる。 オフセットだけを記録し、次フレームの
    // prepare_layout_container
    // が一度適用することで、フレーム間の移動をまとめる。
    bool deferred=false;
    if (access_.set_position) moved=access_.set_position(vertical_,value,false);
    else if (const auto viewport = std::dynamic_pointer_cast<ScrollView>(view)) {
        auto offset = viewport->scroll_offset;
        if (vertical_) offset.y = value; else offset.x = value;
        moved=offset.x!=viewport->scroll_offset.x || offset.y!=viewport->scroll_offset.y;
        if (moved) { viewport->scroll_offset=offset; ++viewport->scroll_update_revision; }
        deferred=true;
    } else if (const auto window = std::dynamic_pointer_cast<Window>(view)) {
        auto offset = window->content_offset;
        if (vertical_) offset.y = value; else offset.x = value;
        moved=offset.x!=window->content_offset.x || offset.y!=window->content_offset.y;
        if (moved) { window->content_offset=offset; ++window->scroll_update_revision; }
        deferred=true;
    } else {
        float remainder{};
        if (vertical_) {
            const auto current=dynamic_cast<ListView*>(view.get()) ? dynamic_cast<ListView*>(view.get())->top_index :
                dynamic_cast<TableView*>(view.get()) ? dynamic_cast<TableView*>(view.get())->top_index :
                dynamic_cast<TreeView*>(view.get()) ? dynamic_cast<TreeView*>(view.get())->top_index : 0U;
            moved=scroll_item_view_by(view,{0,(value-static_cast<float>(current))*
                scaled_ui_metric(kEditorUiMetrics.row_height)},
                scaled_ui_metric(kEditorUiMetrics.row_height),remainder,&invalidated);
        } else if (const auto* offset=horizontal_offset_for(*view))
            moved=scroll_item_view_by(view,{value-*offset,0},
                scaled_ui_metric(kEditorUiMetrics.row_height),remainder,&invalidated);
    }
    // 確定したスクロールのコールバックが、所有者を無効化・置換する場合があ
    // る。 その新しい世代を、今回のドラッグに属するものとして保存しない。
    if (invalidated) { reset(); return moved; }
    if (deferred) {
        // ビュー自身のスクロール変更番号しか変わっていない場合は、 ポ
        // インター移動ごとの部分ツリー全体のスナップショットを避ける。
        expected_revision_=scroll_revision_for(*view);
        // この変更は、取得済みのスクロールビューに属する。新しいスクロール変更番
        // 号だけを受け入れ、 子孫・提供側・子の変更ではドラッグを終了する。 自
        // 身のオフセット変更を、C++のスナップショットで内容の置換と扱わない。
        for (auto& generation : container_generations_)
            if (generation.owner.lock() == view)
                generation.scroll = *expected_revision_;
    } else if (!remember_source(view)) { reset(); return moved; }
    if (!source_current(view)) { reset(); return false; }
    try { scroll=access_.geometry ? access_.geometry() : item_scroll_geometry(*view); }
    catch (...) { reset(); return false; }
    const auto next=vertical_ ? scroll.vertical : scroll.horizontal;
    if (track_.x!=next.track.x || track_.y!=next.track.y || track_.width!=next.track.width ||
        track_.height!=next.track.height || maximum_!=next.maximum ||
        thumb_length_!=(vertical_?next.thumb.height:next.thumb.width)) { reset(); return moved; }
    expected_thumb_origin_=vertical_ ? next.thumb.y : next.thumb.x;
    return moved;
}

namespace {
bool same_rect(const Rect a,const Rect b) noexcept {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
bool same_font(const std::optional<FontSpec>& a,const std::optional<FontSpec>& b) noexcept {
    if (a.has_value()!=b.has_value()) return false;
    return !a || (a->family==b->family && a->points==b->points && a->weight==b->weight &&
        a->italic==b->italic && a->underline==b->underline && a->strikeout==b->strikeout &&
        a->antialias==b->antialias && a->size_mode==b->size_mode);
}
bool same_scroll_widget(const Widget& live,const Widget& before) noexcept {
    return live.kind()==before.kind() && live.visible()==before.visible() &&
        live.enabled()==before.enabled() && same_rect(live.bounds(),before.bounds()) &&
        same_font(live.font(),before.font());
}
bool same_columns(const std::vector<TableColumn>& a,const std::vector<TableColumn>& b) noexcept {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i) {
        const auto& x=a[i]; const auto& y=b[i];
        if (x.title!=y.title || x.width!=y.width || x.alignment!=y.alignment ||
            x.resizable!=y.resizable || x.sortable!=y.sortable || x.stretch!=y.stretch) return false;
    }
    return true;
}
template<class View>
bool same_item_scroll_source(const View& live,const View& before,
                             const std::shared_ptr<const int>& lifetime,
                             const std::uint64_t revision) noexcept {
    const auto current_lifetime=live.operation_lifetime().lock();
    if (!lifetime || current_lifetime!=lifetime || live.scroll_update_revision!=revision ||
        !same_scroll_widget(live,before)) return false;
    if constexpr (std::is_same_v<View,ListView>)
        return live.items.size()==before.items.size() && live.top_index==before.top_index;
    if constexpr (std::is_same_v<View,TableView>)
        return live.rows.size()==before.rows.size() && live.top_index==before.top_index &&
            live.horizontal_offset==before.horizontal_offset && live.header_visible==before.header_visible &&
            live.row_height==before.row_height && live.header_height==before.header_height &&
            same_columns(live.columns,before.columns);
    if constexpr (std::is_same_v<View,TreeView>)
        return live.nodes==before.nodes && live.top_index==before.top_index &&
            live.horizontal_offset==before.horizontal_offset &&
            live.show_expanders==before.show_expanders && live.flush_root_left==before.flush_root_left;
    return false;
}
template<class View>
bool resolve_item_view_scroll(const std::shared_ptr<View>& view,const Point delta,
                              const float requested_row_height,float& row_remainder,
                              bool* invalidated,const std::function<bool()>& external_current) {
    if (!view || !view->visible() || !view->enabled() || !std::isfinite(delta.x) ||
        !std::isfinite(delta.y) || !std::isfinite(requested_row_height) ||
        requested_row_height<=0) return false;
    try {
        const auto lifetime=view->operation_lifetime().lock();
        const auto revision=++view->scroll_update_revision;
        const auto scale_generation=editor_ui_text_scale_generation();
        const View before=*view;
        const auto current=[&] {
            if (external_current && !external_current()) return false;
            return scale_generation==editor_ui_text_scale_generation() &&
                same_item_scroll_source(*view,before,lifetime,revision);
        };
        const auto metric_failure_generation=ui_text_measure_failure_generation();
        const auto geometry=item_scroll_geometry(before,requested_row_height);
        if (metric_failure_generation!=ui_text_measure_failure_generation() || !current())
            {if(invalidated)*invalidated=true;return false;}
        // 呼び出し元は、入力の一行分に相当する物理距
        // 離を渡す。 形状側は描画された行を計測して
        // 世代検証に使うが、 一行分のホイール・パ
        // ン移動を、行未満の保留量へ読み替えない。
        const auto row_height=requested_row_height;
        float next_remainder=std::isfinite(row_remainder) ? row_remainder : 0;
        next_remainder+=delta.y;
        const auto rows=static_cast<std::int64_t>(std::clamp(
            static_cast<double>(next_remainder/row_height),-1000000.0,1000000.0));
        next_remainder-=static_cast<float>(rows)*row_height;
        std::size_t next_top=before.top_index;
        (void)scroll_top_index(next_top,rows,geometry.total_rows,geometry.visible_rows);
        float next_horizontal=0;
        if constexpr (!std::is_same_v<View,ListView>)
            next_horizontal=std::clamp(before.horizontal_offset+delta.x,0.0F,geometry.horizontal.maximum);
        if (!current()) {if(invalidated)*invalidated=true;return false;}
        bool moved=next_top!=before.top_index;
        view->top_index=next_top;
        if constexpr (!std::is_same_v<View,ListView>) {
            moved=moved || next_horizontal!=before.horizontal_offset;
            view->horizontal_offset=next_horizontal;
        }
        row_remainder=next_remainder;
        if constexpr (std::is_same_v<View,TreeView> || std::is_same_v<View,TableView>) {
            if (moved) {
                try {
                    const auto notify = view->on_scroll_changed;
                    if (notify) {
                        const View committed = *view;
                        notify();
                        const bool external_valid = !external_current || external_current();
                        if (!external_valid ||
                            scale_generation != editor_ui_text_scale_generation() ||
                            metric_failure_generation != ui_text_measure_failure_generation() ||
                            !same_item_scroll_source(*view, committed, lifetime, revision)) {
                            if (invalidated) *invalidated = true;
                        }
                    }
                }
                catch (...) {
                    // 位置は確定済みなので通知する。 監視先が失敗
                    // した場合は、呼び出し元の操作状態を終了する。
                    if (invalidated) *invalidated = true;
                }
            }
        }
        return moved;
    } catch (...) {
        if (invalidated) *invalidated=true;
        return false;
    }
}
}

bool scroll_item_view_by(const WidgetPtr& view, const Point delta, const float row_height,
                         float& row_remainder, bool* invalidated,
                         const std::function<bool()>& current) {
    if (invalidated) *invalidated=false;
    if (const auto list=std::dynamic_pointer_cast<ListView>(view))
        return resolve_item_view_scroll(list,delta,row_height,row_remainder,invalidated,current);
    if (const auto table=std::dynamic_pointer_cast<TableView>(view))
        return resolve_item_view_scroll(table,delta,row_height,row_remainder,invalidated,current);
    if (const auto tree=std::dynamic_pointer_cast<TreeView>(view))
        return resolve_item_view_scroll(tree,delta,row_height,row_remainder,invalidated,current);
    return false;
}

struct ItemViewScrollStateMachine::State {
    std::weak_ptr<Widget> owner;
    float remainder{};
    std::uint64_t revision{};
};
ItemViewScrollStateMachine::ItemViewScrollStateMachine():state_(std::make_shared<State>()) {}
ItemViewScrollStateMachine::ItemViewScrollStateMachine(const ItemViewScrollStateMachine&):ItemViewScrollStateMachine() {}
ItemViewScrollStateMachine& ItemViewScrollStateMachine::operator=(const ItemViewScrollStateMachine&) {
    state_=std::make_shared<State>(); return *this;
}
ItemViewScrollStateMachine::~ItemViewScrollStateMachine()=default;
bool ItemViewScrollStateMachine::scroll(const WidgetPtr& view,const Point delta,const float row_height,
                                        bool* invalidated) {
    if (invalidated) *invalidated=false;
    if (!state_) state_=std::make_shared<State>();
    const auto state=state_;
    const auto revision=++state->revision;
    if (state->owner.lock()!=view) {state->owner=view;state->remainder=0;}
    float prepared_remainder=state->remainder;
    const auto current=[state,revision,view] {
        return state->revision==revision && state->owner.lock()==view;
    };
    bool stale=false;
    const bool moved=scroll_item_view_by(view,delta,row_height,prepared_remainder,&stale,current);
    if (!stale && current()) state->remainder=prepared_remainder;
    else stale=true;
    if (invalidated) *invalidated=stale;
    return moved;
}

bool materialize_table_column_widths(
    TableView& table, const float available_width,
    const float fallback_width, const float minimum_width) {
    const bool relative = !table.columns.empty() &&
        std::ranges::all_of(table.columns, [](const TableColumn& column) {
            return column.width > 0.0F && column.width <= 1.0F;
        });
    if (!relative && std::ranges::none_of(table.columns, [](const TableColumn& c) { return c.stretch > 0; })) return false;
    const auto widths = resolved_table_column_widths(
        table, available_width, fallback_width, minimum_width);
    for (std::size_t index = 0; index < table.columns.size(); ++index) {
        table.columns[index].width = widths[index] / editor_ui_text_scale();
        table.columns[index].stretch = 0;
    }
    return true;
}

std::optional<std::size_t> hit_test_table_column(
    const TableView& table, const float x, const float fallback_width) {
    const auto viewport=item_scroll_geometry(table).viewport;
    if (x<viewport.x || x>=viewport.x+viewport.width) return std::nullopt;
    float cursor = table.bounds().x - table.horizontal_offset;
    const auto widths = resolved_table_column_widths(
        table, viewport.width, fallback_width);
    for (std::size_t column = 0; column < widths.size(); ++column) {
        const float width = widths[column];
        if (x >= cursor && x < cursor + width) return column;
        cursor += width;
    }
    return std::nullopt;
}

std::optional<std::size_t> hit_test_table_resize_column(
    const TableView& table, const Point point, const float header_height,
    const float tolerance, const float fallback_width) {
    const Rect bounds = item_scroll_geometry(table).viewport;
    if (!point_inside(bounds, point) ||
        point.y >= bounds.y + table_header_height(table, header_height))
        return std::nullopt;
    float cursor = bounds.x - table.horizontal_offset;
    const auto widths = resolved_table_column_widths(
        table, bounds.width, fallback_width);
    for (std::size_t column = 0; column < widths.size(); ++column) {
        cursor += widths[column];
        if (table.columns[column].resizable &&
            std::abs(point.x - cursor) <= tolerance)
            return column;
    }
    return std::nullopt;
}

bool TableColumnResizeStateMachine::begin(const std::shared_ptr<TableView>& table,
                                         Point point, float row_height) {
    reset();
    if (!table || !table->visible() || !table->enabled()) return false;
    const auto column = hit_test_table_resize_column(*table, point, row_height,
                                                      scaled_ui_metric(4));
    if (!column) return false;
    const auto widths = resolved_table_column_widths(*table, item_scroll_geometry(*table).viewport.width);
    if (!std::isfinite(widths[*column])) return false;
    table_ = table;
    column_ = *column;
    column_count_ = table->columns.size();
    column_title_ = table->columns[column_].title;
    initial_x_ = point.x;
    scale_ = editor_ui_text_scale();
    initial_width_ = widths[column_] / scale_;
    expected_width_ = table->columns[column_].width;
    expected_stretch_ = table->columns[column_].stretch;
    column_alignment_ = table->columns[column_].alignment;
    column_sortable_ = table->columns[column_].sortable;
    materialized_ = false;
    return true;
}

bool TableColumnResizeStateMachine::move(Point point) {
    const auto table = owner();
    if (!table || !table->visible() || !table->enabled() || !table->header_visible ||
        table->columns.size() != column_count_ ||
        table->columns[column_].title != column_title_ ||
        table->columns[column_].width != expected_width_ ||
        table->columns[column_].stretch != expected_stretch_ ||
        table->columns[column_].alignment != column_alignment_ ||
        table->columns[column_].sortable != column_sortable_ ||
        !table->columns[column_].resizable || editor_ui_text_scale() != scale_) {
        reset();
        return false;
    }
    if (!std::isfinite(point.x)) return false;
    const float width = std::max(40.0F, initial_width_ + (point.x - initial_x_) / scale_);
    if ((!materialized_ && width == initial_width_) ||
        (materialized_ && width == expected_width_)) return false;
    if (!materialized_) {
        (void)materialize_table_column_widths(*table, item_scroll_geometry(*table).viewport.width);
        expected_stretch_ = table->columns[column_].stretch;
        materialized_ = true;
    }
    expected_width_ = width;
    // 利用側のコードを呼ぶ前に状態を設定する。 モデルの再構築や別のサイズ変更
    // が始まる場合があるため、コールバック後にその新しい状態を上書きしない。
    return resize_table_column(*table, column_, width);
}

bool TableColumnResizeStateMachine::finish(Point point) {
    auto ending = *this;
    reset();
    return ending.move(point);
}

std::optional<Rect> table_body_column_geometry(
    const TableView& table, const std::size_t column,
    const float header_height, const float fallback_width) {
    if (column >= table.columns.size()) return std::nullopt;
    const auto widths = resolved_table_column_widths(
        table, item_scroll_geometry(table).viewport.width, fallback_width);
    float x = table.bounds().x - table.horizontal_offset;
    for (std::size_t index = 0; index < column; ++index) x += widths[index];
    const float height = table_header_height(table, header_height);
    return Rect{x, table.bounds().y + height,
                widths[column],
                std::max(0.0F, table_horizontal_scroll_geometry(table).viewport.height - height)};
}

Rect table_row_selection_geometry(const TableView& table, const float row_y,
                                  const float row_height,
                                  const float fallback_width) {
    float width = item_scroll_geometry(table).viewport.width;
    if (!table.full_row_selection) {
        const auto widths = resolved_table_column_widths(
            table, width, fallback_width);
        width = widths.empty() ? 0.0F : widths.front();
    }
    return {table.bounds().x + 1.0F - (table.full_row_selection ? 0.0F : table.horizontal_offset), row_y,
            std::max(0.0F, width - 2.0F), std::max(0.0F, row_height)};
}

WidgetPtr hit_test_widget(const WidgetPtr& root, const Point point,
                          const float tab_header_height,
                          const float expand_header_height) {
    if (!root || !root->visible() || !point_inside(root->bounds(), point))
        return {};
    if (std::dynamic_pointer_cast<ScrollView>(root) &&
        !point_inside(item_scroll_geometry(*root).viewport, point)) return root;
    // 無効なホストもホバー対象には保つが、 無効なパネル
    // ・ダイアログの配下にある有効な子へ入力を送らない。
    if (!root->enabled()) return root;
    if (const auto window = std::dynamic_pointer_cast<Window>(root);
        window && window->kind() == ComponentKind::Dialog &&
        !point_inside(item_scroll_geometry(*window).viewport, point)) return root;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(root)) {
        const auto snapshot=snapshot_tab_layout(*tabs);
        const auto content=layout_tab_content(snapshot);
        if (!tab_layout_is_current(snapshot,*tabs)) return {};
        if (point_inside(content,point) && snapshot.selected_index &&
            *snapshot.selected_index<snapshot.pages.size()) {
            auto hit=hit_test_widget(snapshot.pages[*snapshot.selected_index].content,
                                     point,tab_header_height,expand_header_height);
            if (!tab_layout_is_current(snapshot,*tabs)) return {};
            if (hit) return hit;
        }
        return tabs;
    }
    if (const auto expand = std::dynamic_pointer_cast<ExpandBar>(root)) {
        const auto& children = expand->children();
        for (std::size_t reverse = children.size(); reverse > 0U; --reverse) {
            const std::size_t index = reverse - 1U;
            const auto& child = children[index];
            if (!child || !child->visible() || !point_inside(child->bounds(), point))
                continue;
            const bool open = index >= expand->expanded.size() ||
                              expand->expanded[index];
            if (point_inside(expand_item_header_bounds(*child), point) || !open)
                return expand;
            if (auto hit = hit_test_widget(child, point, tab_header_height,
                                           expand_header_height))
                return hit;
            return expand;
        }
        return expand;
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(root)) {
        const auto children=presentation_children(*container);
        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
            if (auto hit = hit_test_widget(*iterator, point, tab_header_height,
                                           expand_header_height))
                return hit;
    }
    return root;
}

namespace {

bool owns_scroll_state(const WidgetPtr& widget) {
    if (const auto window = std::dynamic_pointer_cast<Window>(widget);
        window && window->kind() == ComponentKind::Dialog) {
        const auto scroll = item_scroll_geometry(*window);
        return scroll.horizontal.maximum > 0 || scroll.vertical.maximum > 0;
    }
    if (std::dynamic_pointer_cast<ListView>(widget) ||
        std::dynamic_pointer_cast<TableView>(widget) ||
        std::dynamic_pointer_cast<TreeView>(widget) ||
        std::dynamic_pointer_cast<ScrollView>(widget))
        return true;
    const auto edit = std::dynamic_pointer_cast<TextEdit>(widget);
    return edit && edit->multiline;
}

WidgetPtr scrollable_widget_at(const WidgetPtr& root, const Point point,
                               const float tab_header_height,
                               const float expand_header_height) {
    if (!root || !root->visible() || !root->enabled() || !point_inside(root->bounds(), point))
        return {};
    if (std::dynamic_pointer_cast<ScrollView>(root) &&
        !point_inside(item_scroll_geometry(*root).viewport, point)) return root;
    if (const auto window = std::dynamic_pointer_cast<Window>(root);
        window && window->kind() == ComponentKind::Dialog &&
        !point_inside(item_scroll_geometry(*window).viewport, point)) return root;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(root)) {
        const auto snapshot=snapshot_tab_layout(*tabs);
        const auto content=layout_tab_content(snapshot);
        if (!tab_layout_is_current(snapshot,*tabs)) return {};
        if (point_inside(content,point) && snapshot.selected_index &&
            *snapshot.selected_index<snapshot.pages.size()) {
            auto hit=scrollable_widget_at(snapshot.pages[*snapshot.selected_index].content,
                                          point,tab_header_height,expand_header_height);
            if (tab_layout_is_current(snapshot,*tabs)) return hit;
        }
        return {};
    }
    if (const auto expand = std::dynamic_pointer_cast<ExpandBar>(root)) {
        const auto& children = expand->children();
        for (std::size_t reverse = children.size(); reverse > 0U; --reverse) {
            const std::size_t index = reverse - 1U;
            const auto& child = children[index];
            if (!child || !child->visible() || !point_inside(child->bounds(), point))
                continue;
            const bool open = index >= expand->expanded.size() ||
                              expand->expanded[index];
            if (!open || point_inside(expand_item_header_bounds(*child), point))
                return {};
            return scrollable_widget_at(child, point, tab_header_height,
                                        expand_header_height);
        }
        return {};
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(root)) {
        const auto children=presentation_children(*container);
        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
            if (auto target = scrollable_widget_at(*iterator, point,
                                                   tab_header_height,
                                                   expand_header_height))
                return target;
    }
    return owns_scroll_state(root) ? root : WidgetPtr{};
}

}  // namespace

WidgetPtr hit_test_scrollable_widget(const WidgetPtr& root, const Point point,
                                     const float tab_header_height,
                                     const float expand_header_height) {
    return scrollable_widget_at(root, point, tab_header_height,
                                expand_header_height);
}

bool accepts_keyboard_focus(const WidgetPtr& widget) {
    if (!widget || !widget->visible() || !widget->enabled()) return false;
    switch (widget->kind()) {
    case ComponentKind::Button:
    case ComponentKind::ToggleButton:
    case ComponentKind::CheckBox:
    case ComponentKind::RadioButton:
    case ComponentKind::TextEdit:
    case ComponentKind::NumberEdit:
    case ComponentKind::ComboBox:
    case ComponentKind::ListView:
    case ComponentKind::TableView:
    case ComponentKind::TreeView:
    case ComponentKind::TabView:
    case ComponentKind::Link:
    case ComponentKind::ColorWell:
    case ComponentKind::Slider:
        return true;
    case ComponentKind::ToolBar: {
        const auto toolbar = std::dynamic_pointer_cast<ToolBar>(widget);
        if (toolbar && toolbar->keyboard_navigation)
            return has_keyboard_tool_item(*toolbar);
        // この方式を選んでいないバー（ALLを含む）は、既存の独自キーの代替処
        // 理を維持する。 すべての ToolBar をフォーカス不能にしない。
        return static_cast<bool>(widget->on_key_down) ||
               static_cast<bool>(widget->on_key_up) ||
               static_cast<bool>(widget->on_text_input);
    }
    default:
        // 独自キャンバス・コンテナーは、エディタがキーボード・文字入
        // 力の動作を明示した場合だけフォーカス可能にする。 キーボー
        // ド操作するシーン・イベント画面を維持しつつ、 SDLで受動
        // 的なラベルや配置用パネルがフォーカスを奪わないようにする。
        return static_cast<bool>(widget->on_key_down) ||
               static_cast<bool>(widget->on_key_up) ||
               static_cast<bool>(widget->on_text_input);
    }
}

bool handle_control_key(const WidgetPtr& root, const WidgetPtr& focused,
                        const Key key) {
    if (!focused || !focused->visible() || !focused->enabled()) return false;
    if (const auto button = std::dynamic_pointer_cast<Button>(focused)) {
        if (key != Key::Space && key != Key::Enter) return false;
        return activate_button_control(root, button);
    }
    if (const auto link = std::dynamic_pointer_cast<Link>(focused)) {
        if (key != Key::Space && key != Key::Enter) return false;
        (void)activate_link(*link);
        return true;
    }
    if (const auto number = std::dynamic_pointer_cast<NumberEdit>(focused)) {
        if (number->read_only) return false;
        switch (key) {
        case Key::Home: (void)set_number_value(*number, number->minimum); return true;
        case Key::End: (void)set_number_value(*number, number->maximum); return true;
        case Key::PageUp: (void)step_number_value(*number, 10.0); return true;
        case Key::PageDown: (void)step_number_value(*number, -10.0); return true;
        case Key::Up: (void)step_number_value(*number, 1.0); return true;
        case Key::Down: (void)step_number_value(*number, -1.0); return true;
        default: return false;
        }
    }
    const auto slider = std::dynamic_pointer_cast<Slider>(focused);
    if (!slider) return false;
    double target = slider->value;
    const double step = std::isfinite(slider->step) && slider->step > 0.0 ? slider->step : 1.0;
    const double page = std::isfinite(slider->page_step) && slider->page_step > 0.0
        ? slider->page_step : step * 10.0;
    switch (key) {
    case Key::Home: target = slider->minimum; break;
    case Key::End: target = slider->maximum; break;
    case Key::PageUp: target += page; break;
    case Key::PageDown: target -= page; break;
    case Key::Left:
        if (slider->orientation != Orientation::Horizontal) return false;
        target -= step;
        break;
    case Key::Right:
        if (slider->orientation != Orientation::Horizontal) return false;
        target += step;
        break;
    case Key::Up:
        if (slider->orientation != Orientation::Vertical) return false;
        target += step;
        break;
    case Key::Down:
        if (slider->orientation != Orientation::Vertical) return false;
        target -= step;
        break;
    default: return false;
    }
    (void)set_slider_value(*slider, target);
    return true;
}

namespace {
struct FocusObserverRegistry {
    std::uint64_t next{};
    std::vector<std::pair<std::uint64_t, FocusObserver>> entries;
};
FocusObserverRegistry& focus_observer_registry() {
    static FocusObserverRegistry registry;
    return registry;
}
void notify_focus_observers(const WidgetPtr& focused) {
    // 監視先が関連部品を除去したり、新しいフォーカス遷移を確定したりす
    // る場合がある。 明示的な解除も含め、後の通知が状態を管理する。
    static std::uint64_t notification_revision{};
    const auto revision=++notification_revision;
    const auto entries = focus_observer_registry().entries;
    for (const auto& [id, observer] : entries) {
        if(revision!=notification_revision)return;
        const auto& current=focus_observer_registry().entries;
        if(std::ranges::none_of(current,[id](const auto& entry){return entry.first==id;}))continue;
        if (!observer) continue;
        try { observer(focused); } catch (...) {}
    }
}
}  // namespace

std::uint64_t add_focus_observer(FocusObserver observer) {
    auto& registry = focus_observer_registry();
    const auto id = ++registry.next;
    registry.entries.emplace_back(id, std::move(observer));
    return id;
}

void remove_focus_observer(const std::uint64_t id) noexcept {
    std::erase_if(focus_observer_registry().entries,
                  [id](const auto& entry) { return entry.first == id; });
}

bool FocusStateMachine::set(const WidgetPtr& requested, const WidgetPtr& scope,
                            std::function<bool()> before_change) {
    const auto generation=++generation_;
    const WidgetPtr next = accepts_keyboard_focus(requested) &&
        (!scope || focus_target_in_scope(scope, requested) == requested)
        ? requested : WidgetPtr{};
    const auto previous = focused_.lock();
    if (previous == next) return false;
    if (before_change && !before_change()) return generation_ != generation;
    if (generation_ != generation) return true;
    focused_.reset();
    if (previous && previous->on_blur) {
        const auto notify=previous->on_blur;
        notify();
    }
    // コールバックが別のコントロールへフォーカスを移したり、明示的に解除し
    // たりする場合がある。 新しい遷移を優先し、今回の要求を復活させない。
    if (generation_!=generation) return true;
    if (!accepts_keyboard_focus(next) ||
        (scope && focus_target_in_scope(scope, next) != next)) {
        notify_focus_observers({});
        return true;
    }
    focused_ = next;
    if (const auto toolbar = std::dynamic_pointer_cast<ToolBar>(next);
        toolbar && toolbar->keyboard_navigation)
        (void)normalize_keyboard_tool_item(*toolbar);
    if (next->on_focus) {
        const auto notify=next->on_focus;
        notify();
    }
    if (generation_==generation && (!accepts_keyboard_focus(next) ||
        (scope && focus_target_in_scope(scope, next) != next))) (void)set({});
    else if (generation_==generation && next) notify_focus_observers(next);
    return true;
}

namespace {

bool contains_active_tab_stop(const WidgetPtr& widget, const WidgetPtr& wanted,
                              const bool is_root = true) {
    if (!widget || !wanted || !widget->visible() || !widget->enabled()) return false;
    if (widget == wanted) return true;
    // モデルが親の子一覧に保持していても、子
    // ウィンドウは独立したタブ移動順を持つ。
    if (!is_root && std::dynamic_pointer_cast<Window>(widget)) return false;
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(widget)) {
        if (tabs->selected_index && *tabs->selected_index < tabs->pages.size())
            return contains_active_tab_stop(
                tabs->pages[*tabs->selected_index].content, wanted, false);
        return false;
    }
    if (const auto expand = std::dynamic_pointer_cast<ExpandBar>(widget)) {
        const auto& children = expand->children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            const bool open = index >= expand->expanded.size() ||
                              expand->expanded[index];
            if (open && contains_active_tab_stop(children[index], wanted, false))
                return true;
        }
        return false;
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(widget))
        for (const auto& child : container->children())
            if (contains_active_tab_stop(child, wanted, false)) return true;
    return false;
}

void append_focusable_widgets(const WidgetPtr& widget,
                              std::vector<WidgetPtr>& result,
                              const bool is_root = false);

void append_container_focus_order(const std::shared_ptr<Container>& container,
                                  const std::vector<WidgetPtr>& active_children,
                                  std::vector<WidgetPtr>& result) {
    if (container->tab_order().empty()) {
        for (const auto& child : active_children)
            append_focusable_widgets(child, result);
        return;
    }

    std::vector<WidgetPtr> ordered;
    for (const auto& weak_target : container->tab_order()) {
        const auto target = weak_target.lock();
        if (!target || target.get() == container.get() ||
            !accepts_keyboard_focus(target) ||
            !contains_active_tab_stop(container, target))
            continue;
        append_focusable_widgets(target, ordered);
    }

    std::vector<WidgetPtr> fallback;
    for (const auto& child : active_children)
        append_focusable_widgets(child, fallback);
    if (ordered.empty()) {
        result.insert(result.end(), fallback.begin(), fallback.end());
        return;
    }

    // D版と同様、明示的な一覧の移動先を先に並べる。 一覧
    // にないコントロールも、その時点の子ツリーの順に到達
    // でき、 一覧設定後に追加したコントロールも含める。
    const auto append_unique = [&result](const std::vector<WidgetPtr>& widgets) {
        for (const auto& item : widgets)
            if (std::ranges::find(result, item) == result.end())
                result.push_back(item);
    };
    append_unique(ordered);
    append_unique(fallback);
}

void append_focusable_widgets(const WidgetPtr& widget,
                              std::vector<WidgetPtr>& result,
                              const bool is_root) {
    if (!widget || !widget->visible() || !widget->enabled()) return;
    // エディタが所有者のモデルツリーに保持していても、 子ウィ
    // ンドウは自身のフォーカス・既定ボタンの有効範囲を持つ。
    if (!is_root && std::dynamic_pointer_cast<Window>(widget)) return;
    if (accepts_keyboard_focus(widget)) result.push_back(widget);
    if (const auto tabs = std::dynamic_pointer_cast<TabView>(widget)) {
        if (tabs->selected_index && *tabs->selected_index < tabs->pages.size())
            append_focusable_widgets(tabs->pages[*tabs->selected_index].content,
                                     result);
        return;
    }
    if (const auto expand = std::dynamic_pointer_cast<ExpandBar>(widget)) {
        const auto& children = expand->children();
        std::vector<WidgetPtr> active_children;
        active_children.reserve(children.size());
        for (std::size_t index = 0; index < children.size(); ++index) {
            const bool open = index >= expand->expanded.size() ||
                              expand->expanded[index];
            if (open) active_children.push_back(children[index]);
        }
        append_container_focus_order(expand, active_children, result);
        return;
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(widget))
        append_container_focus_order(container, container->children(), result);
}

}  // namespace

std::vector<WidgetPtr> focusable_widgets(const WidgetPtr& root) {
    std::vector<WidgetPtr> result;
    append_focusable_widgets(root, result, true);
    return result;
}

namespace {
bool contains_focus_control(const WidgetPtr& root, const WidgetPtr& wanted) {
    if (!root || !wanted) return false;
    if (root==wanted) return true;
    if (std::dynamic_pointer_cast<Window>(root)) return false;
    if (const auto tabs=std::dynamic_pointer_cast<TabView>(root)) {
        for (const auto& page:tabs->pages)
            if (contains_focus_control(page.content,wanted)) return true;
    } else if (const auto container=std::dynamic_pointer_cast<Container>(root)) {
        for (const auto& child:container->children())
            if (contains_focus_control(child,wanted)) return true;
    }
    return false;
}

std::shared_ptr<TabView> focus_tab_ancestor(const std::vector<WidgetPtr>& order, const WidgetPtr& preferred) {
    // フォーカス順は外側から内側なので、逆順で最も近いタブが見つかる。
    for (auto it=order.rbegin();it!=order.rend();++it) {
        const auto tabs=std::dynamic_pointer_cast<TabView>(*it);
        if (tabs && contains_focus_control(tabs,preferred)) return tabs;
    }
    return {};
}

std::vector<std::shared_ptr<RadioButton>> focusable_radio_group(
    const WidgetPtr& root, const RadioButton& selected, const std::vector<WidgetPtr>& order) {
    auto group = radio_button_group(root, selected);
    std::erase_if(group, [&](const auto& radio) {
        return std::ranges::find(order, radio) == order.end();
    });
    return group;
}

std::vector<WidgetPtr> tab_stop_widgets(const WidgetPtr& root) {
    const auto order = focusable_widgets(root);
    std::vector<WidgetPtr> result;
    for (const auto& widget : order) {
        if (const auto radio = std::dynamic_pointer_cast<RadioButton>(widget)) {
            const auto group = focusable_radio_group(root, *radio, order);
            if (group.empty()) continue;
            const auto checked = std::ranges::find_if(group, [](const auto& item) { return item->checked; });
            if (radio != (checked == group.end() ? group.front() : *checked)) continue;
        }
        result.push_back(widget);
    }
    return result;
}
}

WidgetPtr focus_target_in_scope(const WidgetPtr& root, const WidgetPtr& preferred,
                                bool fallback, bool prefer_active_descendant) {
    const auto order=focusable_widgets(root);
    if (preferred && std::ranges::find(order,preferred)!=order.end()) return preferred;
    // D版の Composite.setFocus と同様、ドックへの明示的なフォーカス要求では focusControl
    // を使う。 寿命管理上のフォーカス復元は従来どおりにするため、この優先動作は明示的に選ぶ。
    if (prefer_active_descendant && preferred) {
        for (const auto& candidate : order)
            if (candidate != preferred && contains_focus_control(preferred, candidate))
                return candidate;
    }
    if (const auto tabs=focus_tab_ancestor(order,preferred)) return tabs;
    const auto stops = fallback ? tab_stop_widgets(root) : std::vector<WidgetPtr>{};
    return !stops.empty() ? stops.front() : WidgetPtr{};
}

FocusKeyResult FocusStateMachine::key(const WidgetPtr& root, const Key key, const Modifiers modifiers) {
    FocusKeyResult result;
    if (modifiers == Modifiers::None &&
        (key == Key::Left || key == Key::Right || key == Key::Up || key == Key::Down)) {
        const auto radio = std::dynamic_pointer_cast<RadioButton>(focused());
        const auto order = focusable_widgets(root);
        if (!radio || std::ranges::find(order, radio) == order.end()) return result;
        const auto group = focusable_radio_group(root, *radio, order);
        const auto found = std::ranges::find(group, radio);
        if (found == group.end()) return result;
        result.handled = true;
        const auto index = static_cast<std::size_t>(found - group.begin());
        const bool backward = key == Key::Left || key == Key::Up;
        const auto target = group[(index + (backward ? group.size() - 1U : 1U)) % group.size()];
        const auto expected_generation = generation_ + 1U;
        (void)set(target, root);
        if (generation_ != expected_generation || focused() != target ||
            focus_target_in_scope(root, target) != target) return result;
        (void)select_radio_button(*target, radio_button_group(root, *target));
        if (generation_ == expected_generation && focus_target_in_scope(root, target) == target)
            (void)activate_button(*target);
        if (generation_ == expected_generation && focus_target_in_scope(root, target) != target)
            (void)set({});
        return result;
    }
    if (key!=Key::Tab) return result;
    const auto mods=static_cast<std::uint32_t>(modifiers);
    if (mods & static_cast<std::uint32_t>(Modifiers::Alt|Modifiers::Meta)) return result;
    const bool backward=(mods & static_cast<std::uint32_t>(Modifiers::Shift))!=0;
    if (!(mods & static_cast<std::uint32_t>(Modifiers::Control))) {
        result.handled=true;
        (void)move(root,backward);
        return result;
    }
    if (!tab_wheel_switch_enabled()) return result;
    const auto previous=focused();
    const auto tabs=focus_tab_ancestor(focusable_widgets(root),previous);
    if (!tabs || tabs->pages.empty()) return result;
    result.handled=true;
    const auto generation=generation_;
    if (!navigate_tab_page(*tabs,backward ? TabNavigation::Previous : TabNavigation::Next,true)) return result;
    result.changed_tabs=tabs;
    // 選択コールバックがウィンドウを閉じたり、フォーカスを明示的に選んだりする場合がある。
    if (generation_!=generation) return result;
    WidgetPtr target=tabs;
    if (previous!=tabs && tabs->selected_index && *tabs->selected_index<tabs->pages.size()) {
        const auto children=tab_stop_widgets(tabs->pages[*tabs->selected_index].content);
        if (!children.empty()) target=children.front();
    }
    (void)set(focus_target_in_scope(root,target), root);
    return result;
}

bool handle_dialog_key(const WidgetPtr& root, const Key key,
                       const Modifiers modifiers) {
    const auto dialog = std::dynamic_pointer_cast<Dialog>(root);
    if (!dialog || !dialog->visible() || !dialog->enabled() ||
        modifiers != Modifiers::None ||
        (key != Key::Enter && key != Key::Escape))
        return false;
    for (const auto& widget : focusable_widgets(dialog)) {
        const auto button = std::dynamic_pointer_cast<Button>(widget);
        if (button && (key == Key::Enter ? button->default_button
                                        : button->cancel_button)) {
            (void)activate_button_control(dialog, button);
            return true;
        }
    }
    if (key != Key::Escape) return false;
    if (dialog->on_close_requested) {
        // 終了コールバックがキャンセルを拒否したり、モデルの状態を解放したりする場合がある。
        const auto close = dialog->on_close_requested;
        close();
    } else {
        dialog->result = DialogResult::Cancel;
        dialog->set_visible(false);
    }
    return true;
}

bool FocusStateMachine::move(const WidgetPtr& root, const bool backward) {
    const auto order = focusable_widgets(root);
    if (order.empty()) return set({});
    const auto current = focused();
    const auto found = std::ranges::find(order, current);
    auto stops = tab_stop_widgets(root);
    if (stops.empty()) return set({});
    if (found == order.end()) {
        return set(backward ? stops.back() : stops.front(), root);
    }
    // ポインターやプログラムによる明示的なフォーカスは、未選択のラジオボタンにあ
    // る場合もある。 Tabでは選択済みの兄弟へ飛ばず、そのグループの外へ移る。
    if (const auto radio = std::dynamic_pointer_cast<RadioButton>(current)) {
        const auto group = focusable_radio_group(root, *radio, order);
        std::erase_if(stops, [&](const auto& widget) {
            return widget != current && std::ranges::find(group, widget) != group.end();
        });
    }
    const auto start = static_cast<std::size_t>(found - order.begin());
    for (std::size_t step = 1; step <= order.size(); ++step) {
        const auto index = backward ? (start + order.size() - step) % order.size()
                                    : (start + step) % order.size();
        if (std::ranges::find(stops, order[index]) != stops.end()) return set(order[index], root);
    }
    return false;
}

std::optional<ModalStateMachine::SessionId> ModalStateMachine::begin(
    std::shared_ptr<Dialog> dialog,
    std::shared_ptr<Window> owner,
    WidgetPtr focused) {
    if (!dialog || std::ranges::any_of(sessions_, [&dialog](const Session& item) {
            return item.dialog == dialog;
        }))
        return std::nullopt;
    // エディタのダイアログの中には、共通モデルに所有者を設定してか
    // ら show_modal(dialog, {}) を呼ぶもの
    // がある。 表示の境界でその所有者を消さず、SDL表示でのモー
    // ダル入力制限と、DWTの親を基準とした中央配置を維持する。
    const auto effective_owner = owner ? owner : dialog->owner.lock();
    std::unordered_set<const Window*> owner_chain;
    owner_chain.insert(dialog.get());
    std::vector<std::shared_ptr<Window>> owners;
    for (auto current=effective_owner; current; current=current->owner.lock()) {
        if (!current->visible() || !owner_chain.insert(current.get()).second) return std::nullopt;
        owners.push_back(current);
    }
    const SessionId id = next_id_++;
    const bool owner_was_enabled = effective_owner ? effective_owner->enabled() : true;
    // 所有者を無効化する前に、セッション全体を確保する。 確保失敗時に
    // 、終了できるセッションなしでウィンドウだけを操作不能にしない。
    sessions_.push_back({id, dialog, effective_owner, focused,
                         owner_was_enabled, false, std::move(owners)});
    dialog->owner = effective_owner;
    // 実際の所有者を常に優先する。なければ、最初のフレームを (0,0)
    // にせず、 共通の画面中央への代替配置を維持する。
    if (effective_owner) dialog->center_on_owner = true;
    dialog->result = DialogResult::None;
    if (effective_owner) effective_owner->set_enabled(false);
    return id;
}

std::optional<ModalStateMachine::SessionId> ModalStateMachine::begin_external(
    std::shared_ptr<Window> owner, WidgetPtr focused) {
    std::unordered_set<const Window*> visited;
    std::vector<std::shared_ptr<Window>> owners;
    for (auto current=owner; current; current=current->owner.lock()) {
        if (!current->visible() || !visited.insert(current.get()).second) return std::nullopt;
        owners.push_back(current);
    }
    const auto id=next_id_++;
    const bool was_enabled=owner ? owner->enabled() : true;
    sessions_.push_back({id,{},owner,focused,was_enabled,true,std::move(owners)});
    if(owner) owner->set_enabled(false);
    return id;
}

bool ModalStateMachine::has_external_session() const noexcept {
    return std::ranges::any_of(sessions_,[](const Session& session) { return session.external; });
}

std::optional<ModalStateMachine::Session> ModalStateMachine::finish(
    const SessionId id) {
    const auto found = std::ranges::find(sessions_, id, &Session::id);
    if (found == sessions_.end()) return std::nullopt;
    // モーダルセッションは入力のスタックを作る。 最上位以外の所
    // 有者を復元すると、子がまだ有効なのに入力可能となり、 その
    // 子の終了時に必要な元の有効状態も失われるため、順序を守る。
    if (found != std::prev(sessions_.end())) return std::nullopt;
    Session result = std::move(*found);
    sessions_.erase(found);
    if (result.owner) result.owner->set_enabled(result.owner_was_enabled);
    return result;
}

bool ModalStateMachine::active(const SessionId id) const noexcept {
    return std::ranges::any_of(sessions_, [id](const Session& item) {
        return item.id == id;
    });
}

bool ModalStateMachine::allows(const std::shared_ptr<Window>& window) const {
    if (sessions_.empty()) return true;
    return window && sessions_.back().dialog == window;
}

bool ModalStateMachine::can_continue(SessionId id) const {
    const auto found=std::ranges::find(sessions_,id,&Session::id);
    if (found==sessions_.end()) return false;
    if (!found->external && (!found->dialog || !found->dialog->visible() ||
        found->dialog->result!=DialogResult::None)) return false;
    return owner_available(id);
}

bool ModalStateMachine::owner_available(SessionId id) const {
    const auto found=std::ranges::find(sessions_,id,&Session::id);
    return found!=sessions_.end() && owner_available(*found);
}

bool ModalStateMachine::owner_available(const Session& session) {
    if (session.dialog && session.dialog->owner.lock()!=session.owner) return false;
    auto current=session.owner;
    for (const auto& captured:session.owner_chain) {
        if (!current || current!=captured || !current->visible()) return false;
        current=current->owner.lock();
    }
    return !current;
}

}  // namespace cwxeditor::lib
