#include "ui_combo_edit.hpp"
#include "ui_metrics.hpp"

#include <algorithm>

namespace cwxeditor::lib {

bool ComboBoxLayout::edits_text(const ComboBox& combo, Point point) const noexcept {
    return combo.editable && point_inside(text, point);
}

ComboBoxLayout layout_combo_box(const ComboBox& combo) noexcept {
    const auto r = combo.bounds();
    const float width = std::max(0.0F, r.width);
    const float arrow_width = std::min(width, scaled_ui_metric(25));
    return {{r.x, r.y, width - arrow_width, std::max(0.0F, r.height)},
            {r.x + width - arrow_width, r.y, arrow_width, std::max(0.0F, r.height)}};
}

std::string combo_display_text(const ComboBox& combo) {
    if (combo.selected_index && *combo.selected_index < combo.items.size())
        return combo.items[*combo.selected_index].text;
    return combo.edit_text;
}

void ComboEditSession::reset_text() {
    if (const auto combo = target_.lock()) {
        editor_->text = observed_text_ = combo_display_text(*combo);
        observed_selection_ = combo->selected_index;
        state_ = {};
        if (combo->edit_selection) {
            const auto [start, end] = *combo->edit_selection;
            editor_->selection_start = std::min(start, editor_->text.size());
            editor_->selection_length = std::clamp(end, editor_->selection_start, editor_->text.size()) - editor_->selection_start;
            editor_->selection_reversed = editor_->selection_length != 0U &&
                                          combo->edit_selection_reversed;
            state_.anchor = editor_->selection_reversed
                ? editor_->selection_start + editor_->selection_length
                : editor_->selection_start;
            state_.caret = editor_->selection_reversed
                ? editor_->selection_start
                : editor_->selection_start + editor_->selection_length;
        } else (void)select_all_text(*editor_, state_);
        publish_selection();
    }
}

void ComboEditSession::begin(const std::shared_ptr<ComboBox>& combo) {
    ++generation_;
    if (!combo || !combo->editable) { finish(); return; }
    if (target_.lock() != combo) {
        target_ = combo;
        reset_text();
    }
    synchronize();
}

void ComboEditSession::finish() noexcept { ++generation_; target_.reset(); }

void ComboEditSession::synchronize() {
    if (const auto combo = target_.lock()) {
        if (observed_text_ != combo_display_text(*combo) || observed_selection_ != combo->selected_index)
            reset_text();
        // D版のネイティブComboと同様、選択は一つとして扱う。C++
        // の表示側は、 新しい公開選択だけを取り込み、同じ編集欄の提
        // 供処理が変えたカーソル・変換状態に、前回の値を重ねない。
        else if (combo->edit_selection &&
                 (combo->edit_selection != observed_edit_selection_ ||
                  combo->edit_selection_reversed != observed_edit_selection_reversed_)) {
            const auto [start, end] = *combo->edit_selection;
            editor_->selection_start = std::min(start, editor_->text.size());
            editor_->selection_length = std::clamp(end, editor_->selection_start, editor_->text.size()) - editor_->selection_start;
            editor_->selection_reversed = editor_->selection_length != 0U &&
                                          combo->edit_selection_reversed;
            state_.anchor = editor_->selection_reversed
                ? editor_->selection_start + editor_->selection_length
                : editor_->selection_start;
            state_.caret = editor_->selection_reversed
                ? editor_->selection_start
                : editor_->selection_start + editor_->selection_length;
            observed_edit_selection_ = combo->edit_selection;
            observed_edit_selection_reversed_ = combo->edit_selection_reversed;
        }
        editor_->set_bounds(layout_combo_box(*combo).text);
        editor_->set_font(combo->font());
        editor_->read_only = !combo->editable;
        editor_->set_enabled(combo->enabled());
    }
}

void ComboEditSession::publish_selection() {
    if (const auto combo = target_.lock()) {
        combo->selection_start = editor_->selection_start;
        combo->selection_length = editor_->selection_length;
        combo->edit_selection = std::pair{editor_->selection_start, editor_->selection_start + editor_->selection_length};
        combo->edit_selection_reversed = editor_->selection_reversed;
        observed_edit_selection_ = combo->edit_selection;
        observed_edit_selection_reversed_ = combo->edit_selection_reversed;
    }
}

void ComboEditSession::publish() {
    if (const auto combo = target_.lock(); combo && combo->enabled() && combo->editable) {
        const auto generation = generation_;
        // 通知前に意図した値を保存する。 コールバックが選択肢・値を置
        // き換える場合があるため、同期処理はその外部変更を反映する。
        observed_text_ = editor_->text;
        observed_selection_.reset();
        // コールバック前に、値とともにカーソル位置を反映する。 監視
        // 先が文字列・選択を置き換えた場合は、その変更を保持する。
        try { (void)set_combo_edit_text(*combo, editor_->text,
            std::pair{editor_->selection_start, editor_->selection_start + editor_->selection_length}); }
        catch (...) {
            if (generation_ == generation) try { synchronize(); } catch (...) {}
            throw;
        }
        if (generation_ == generation) synchronize();
    }
}

bool ComboEditSession::insert(std::string_view text) {
    synchronize();
    const auto combo = target_.lock();
    if (!combo || !combo->editable || !combo->enabled()) return false;
    const auto generation = ++generation_;
    const auto selected = combo->selected_index;
    const auto old_text = combo->edit_text;
    const auto display = combo_display_text(*combo);
    const bool changed = replace_text_selection(*editor_, state_, text);
    if (generation_ != generation || target_.lock() != combo) return changed;
    if (combo->selected_index != selected || combo->edit_text != old_text || combo_display_text(*combo) != display) {
        synchronize();return changed;
    }
    if (changed) publish();
    return changed;
}

bool ComboEditSession::key(Key key, Modifiers modifiers, ComboPopupStateMachine& popup,
                           std::size_t visible_rows, float line_height,
                           const TextWidthMeasure& measure, const TextClipboardAccess& clipboard,
                           const std::function<bool()>& live) {
    if (live && !live()) return true;
    const auto combo = target_.lock();
    if (!combo || !combo->editable || !combo->enabled()) return false;
    synchronize();
    const auto generation = ++generation_;
    const bool alt = (static_cast<std::uint32_t>(modifiers) &
                      static_cast<std::uint32_t>(Modifiers::Alt)) != 0U;
    const bool command = (static_cast<std::uint32_t>(modifiers) &
                          static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Meta)) != 0U;
    try {
        if (popup.handle_key(combo, key, alt, visible_rows, command)) {
            if (generation_ == generation) synchronize();
            return true;
        }
    } catch (...) {
        if (generation_ == generation) try { synchronize(); } catch (...) {}
        throw;
    }
    const auto previous = editor_->text;
    const auto selected = combo->selected_index;
    const auto edit_text = combo->edit_text;
    const auto popup_revision = popup.revision();
    const auto current = [&] {
        return (!live || live()) && generation_ == generation && target_.lock() == combo &&
            combo->editable && combo->enabled() && combo->selected_index == selected &&
            combo->edit_text == edit_text && combo_display_text(*combo) == previous &&
            popup.revision() == popup_revision;
    };
    bool handled{}, committed{};
    try {
        handled = dispatch_text_edit_key(*editor_, state_, key, modifiers,
            text_edit_inner_bounds(*editor_), line_height, measure, clipboard, {current, {}, &committed});
    } catch (...) {
        if (generation_ == generation) try { synchronize(); } catch (...) {}
        throw;
    }
    if (!current()) {
        if (generation_ == generation) synchronize();
        return true; // 古いキー入力を新しいダイアログへ転送しない。
    }
    if (!handled) return false;
    publish_selection();
    if (committed && editor_->text != previous) {
        popup.close();
        publish();
    }
    return handled;
}

} // namespace cwxeditor::lib
