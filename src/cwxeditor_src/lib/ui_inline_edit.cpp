#include "ui_inline_edit.hpp"
#include "ui_combo_edit.hpp"
#include "window_host.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace cwxeditor::lib {
namespace {
std::optional<ListItem> inline_number_cell(const InlineEditStateMachine& edit) {
    if (editor_window_host_options().all_mode || !edit.active() || edit.choice()) return {};
    const auto table = std::dynamic_pointer_cast<TableView>(edit.owner());
    if (!table || !table->visible() || !table->enabled() || edit.editor()->read_only) return {};
    for (std::size_t row = 0; row < table->rows.size(); ++row) {
        for (std::size_t column = 0; column < table->rows[row].cells.size(); ++column) {
            if (!edit.matches(table, row, column)) continue;
            const auto& cell = table->rows[row].cells[column];
            if (!cell.enabled || !cell.inline_editable || cell.editor != CellEditorKind::Number ||
                !cell.editor_step || !std::isfinite(*cell.editor_step) || *cell.editor_step <= 0 ||
                !std::isfinite(cell.editor_minimum) || !std::isfinite(cell.editor_maximum) ||
                cell.editor_maximum < cell.editor_minimum) return {};
            return cell;
        }
    }
    return {};
}
std::optional<double> inline_number_value(std::string_view text) {
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    double value{}; char trailing{};
    if (!(input >> value) || (input >> trailing) || !std::isfinite(value)) return {};
    return value;
}
double inline_number_draft(const InlineEditStateMachine& edit, const ListItem& cell) {
    const double value = inline_number_value(edit.buffer()).value_or(
        inline_number_value(cell.editor_value.value_or(cell.text)).value_or(cell.editor_minimum));
    return std::clamp(value, cell.editor_minimum, cell.editor_maximum);
}
} // namespace

std::optional<InlineNumberStepper> prepare_inline_number_stepper(
    InlineEditStateMachine& edit, const float row_height) {
    const auto cell = inline_number_cell(edit);
    if (!cell || !edit.layout(row_height)) return {};
    NumberEdit geometry_source;
    geometry_source.set_bounds(edit.editor()->bounds());
    const auto geometry = layout_number_edit(geometry_source);
    const auto value = inline_number_draft(edit, *cell);
    edit.set_bounds(geometry.text);
    return InlineNumberStepper{geometry, value < cell->editor_maximum,
                               value > cell->editor_minimum};
}

bool step_inline_number(InlineEditStateMachine& edit, const int direction) {
    const auto cell = inline_number_cell(edit);
    if (!cell || direction == 0) return false;
    const auto value = inline_number_draft(edit, *cell);
    const double next = std::clamp(value + (direction > 0 ? *cell->editor_step : -*cell->editor_step),
                                   cell->editor_minimum, cell->editor_maximum);
    if (!std::isfinite(next)) return false;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(std::numeric_limits<double>::max_digits10) << next;
    const bool changed = edit.set_buffer(text.str());
    if (changed) (void)select_all_text(*edit.editor(), edit.text_state());
    return changed;
}


bool combo_in_scope(const WidgetPtr& scope, const std::shared_ptr<ComboBox>& combo,
                    const InlineEditStateMachine& inline_edit) {
    return combo && (widget_in_input_scope(scope, combo) ||
        (combo == inline_edit.choice() && widget_in_input_scope(scope, inline_edit.owner())));
}

bool dispatch_inline_edit_key(InlineEditStateMachine& edit, ComboPopupStateMachine& popup,
    Key key, Modifiers modifiers, std::size_t visible_rows, float line_height,
    const TextWidthMeasure& measure, const TextClipboardAccess& clipboard, const std::function<bool()>& live) {
    if (live && !live()) return true;
    if (!edit.active()) return false;
    edit.synchronize_choice_selection();
    const auto owner = edit.owner();
    const auto editor = edit.editor(); // 開始・キャンセルのコールバックをまたいで、古い表示用データを保持する。
    if (!owner || !owner->enabled()) { popup.close(); edit.cancel(); return false; }
    const auto combo = edit.choice();
    const auto current = [&] {
        return (!live || live()) && edit.active() && edit.owner() == owner &&
            owner->enabled() && edit.editor() == editor && edit.choice() == combo;
    };
    // コンボの表示にも、通常のコントロールと同じ意味上のキー通知をホストへ送
    // る。 特にD版の読み取り専用のインクリメンタル検索フィルターで必要。
    if(combo && combo->on_key_down) {
        KeyEvent event{key,modifiers};
        const auto notify=combo->on_key_down;
        notify(event);
        if(!current() || event.handled)return true;
        key=event.key;modifiers=event.modifiers;
    }
    const bool alt = (static_cast<std::uint32_t>(modifiers) & static_cast<std::uint32_t>(Modifiers::Alt)) != 0U;
    const bool command = (static_cast<std::uint32_t>(modifiers) &
        static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Meta)) != 0U;
    if (!alt && !command && (key == Key::Up || key == Key::Down) && inline_number_cell(edit)) {
        (void)step_inline_number(edit, key == Key::Up ? 1 : -1);
        return true;
    }
    if (!alt && !command && (key == Key::Up || key == Key::Down)) {
        const auto table = std::dynamic_pointer_cast<TableView>(owner);
        if (table && table->quick_edit_arrow_target && !table->selection.empty()) {
            const auto row = table->selection.front();
            if (row >= table->rows.size()) return false;
            const auto revision = table->row_order_revision;
            const auto row_identity = table->rows[row].cells.empty()
                ? std::string{} : table->rows[row].cells.front().value;
            const auto row_selection = table->selection;
            const auto resolve_target = table->quick_edit_arrow_target(
                row, revision, row_identity, key);
            // D版と同様、隣の行がある場合だけ矢印キーを処理する。
            // 端ではテキスト編集の通常のカーソル動作を維持する。
            if (!resolve_target) return false;
            // 事前確認中にホストがモデルを変えた場合は、古い
            // 編集状態に確定せず、キーの処理を続行させる。
            if (table->row_order_revision != revision || table->selection != row_selection ||
                row >= table->rows.size() || table->rows[row].cells.empty() ||
                table->rows[row].cells.front().value != row_identity)
                return false;
            const auto committed_text = edit.editor()->text;
            (void)edit.commit();
            if (edit.active() || (live && !live()) || !table->visible() ||
                !table->enabled()) return true;
            const auto target = resolve_target(committed_text);
            if (target && *target < table->rows.size())
                (void)select_table_rows(*table, {*target});
            return true;
        }
    }
    if (key == Key::Escape) { popup.close(); edit.cancel(); return true; }
    if (key == Key::Enter && (!combo || !popup.is_open_for(combo))) {
        (void)edit.commit();
        return true;
    }
    if (combo && popup.handle_key(combo, key, alt, visible_rows, command)) {
        if (!current()) return true;
        if (!popup.is_open_for(combo) && (key == Key::Enter ||
            (key == Key::Space && !combo->editable) || (alt && key == Key::Up))) {
            (void)edit.commit_choice();
        } else if (!popup.is_open_for(combo)) {
            (void)edit.set_buffer(combo_display_text(*combo));
            (void)select_all_text(*edit.editor(), edit.text_state());
            edit.publish_choice_selection();
        }
        return true;
    }
    if (key == Key::Enter || key == Key::Tab) {
        popup.close();
        const auto popup_revision = popup.revision();
        (void)edit.commit();
        // Tabは今回の編集確定後に移動するが、通知で同期
        // 的に開いた別の行を確定・フォーカス解除しない。
        return key != Key::Tab || edit.active() || popup.revision() != popup_revision || (live && !live());
    }
    const auto previous = editor->text;
    const auto popup_revision = popup.revision();
    const auto current_input = [&] {return current() && popup.revision() == popup_revision;};
    bool committed{};
    const bool handled = dispatch_text_edit_key(*editor, edit.text_state(), key,
        modifiers, text_edit_inner_bounds(*editor), line_height, measure, clipboard, {current_input, {}, &committed});
    if (!current_input()) return true;
    edit.publish_choice_selection();
    if (committed && combo && editor->text != previous) {
        combo->edit_text = editor->text;
        combo->selected_index.reset();
        popup.close();
    }
    return handled;
}

ComboPopupClick click_inline_choice(InlineEditStateMachine& edit,
    ComboPopupStateMachine& popup, const ComboPopupLayout& layout, Point point) {
    const auto combo = edit.choice();
    if (!combo || !popup.is_open_for(combo)) return ComboPopupClick::Outside;
    const auto editor = edit.editor();
    const auto owner = edit.owner();
    const auto index = layout.item_at(point, popup.top(), combo->items.size());
    const bool accept = index && combo->items[*index].enabled;
    const auto result = popup.click(layout, point);
    if (accept && edit.active() && edit.owner() == owner && edit.editor() == editor && edit.choice() == combo)
        (void)edit.commit_choice();
    return result;
}
} // namespace cwxeditor::lib
