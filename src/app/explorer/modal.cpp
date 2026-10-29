#include "modal.hpp"

#include "cw/lib/regex_grapheme.hpp"

#include <algorithm>

namespace cardwirth::explorer {
namespace {

std::vector<std::size_t> grapheme_offsets(const std::string& text) {
    std::vector<std::size_t> result{0};
    for (const auto& cluster : ::cw::lib::regex_grapheme::grapheme_clusters(text))
        result.push_back(result.back() + cluster.size());
    return result;
}

std::size_t previous_grapheme(const std::string& text, std::size_t offset) {
    const auto offsets = grapheme_offsets(text);
    const auto current = std::lower_bound(offsets.begin(), offsets.end(), offset);
    return current == offsets.begin() ? 0 : *std::prev(current);
}

std::size_t next_grapheme(const std::string& text, std::size_t offset) {
    const auto offsets = grapheme_offsets(text);
    const auto next = std::upper_bound(offsets.begin(), offsets.end(), offset);
    return next == offsets.end() ? text.size() : *next;
}

std::size_t byte_offset_for_column(const std::string& text, std::size_t start,
                                   std::size_t end, std::size_t column) {
    auto offset = start;
    while (offset < end && column-- > 0) offset = next_grapheme(text, offset);
    return std::min(offset, end);
}

std::size_t snap_to_grapheme(const std::string& text, std::size_t offset) {
    const auto offsets = grapheme_offsets(text);
    const auto found = std::upper_bound(offsets.begin(), offsets.end(), offset);
    return found == offsets.begin() ? 0 : *std::prev(found);
}

}  // namespace

void ExplorerModalState::show_move_to_trash(std::size_t count) {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmMoveToTrash;
    affected_count_ = count;
}

void ExplorerModalState::show_restore_from_trash(std::size_t count) {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmRestoreFromTrash;
    affected_count_ = count;
}

void ExplorerModalState::show_delete_permanently(std::size_t count) {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmDeletePermanently;
    affected_count_ = count;
}

void ExplorerModalState::show_open_text(TextDocument document) {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmOpenText;
    document_ = std::move(document);
}

void ExplorerModalState::begin_text_editor() {
    if (kind_ != ExplorerModalKind::ConfirmOpenText) return;
    original_text_ = document_.text;
    cursor_ = 0;
    anchor_.reset();
    range_mode_ = false;
    text_editor_focused_ = true;
    search_active_ = false;
    search_query_.clear();
    scroll_line_ = 0;
    kind_ = ExplorerModalKind::TextEditor;
}

void ExplorerModalState::focus_text_editor() {
    if (kind_ == ExplorerModalKind::TextEditor) text_editor_focused_ = true;
}

void ExplorerModalState::blur_text_editor() {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    text_editor_focused_ = false;
    composition_.clear();
    composition_start_ = 0;
    composition_length_ = 0;
}

void ExplorerModalState::show_empty_trash() {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmEmptyTrash;
}

void ExplorerModalState::show_upload_type() {
    dismiss();
    kind_ = ExplorerModalKind::ChooseUploadType;
}

void ExplorerModalState::show_download_progress() {
    dismiss();
    kind_ = ExplorerModalKind::DownloadProgress;
}

void ExplorerModalState::show_extract_progress(std::string title) {
    dismiss();
    kind_ = ExplorerModalKind::ExtractProgress;
    subject_name_ = std::move(title);
}

void ExplorerModalState::show_move_paste_notice() {
    dismiss();
    kind_ = ExplorerModalKind::MovePasteNotice;
}

void ExplorerModalState::show_flatten_extraction(std::string name) {
    dismiss();
    kind_ = ExplorerModalKind::ConfirmFlattenExtraction;
    subject_name_ = std::move(name);
}

void ExplorerModalState::show_context_actions() {
    dismiss();
    kind_ = ExplorerModalKind::ContextActions;
}

void ExplorerModalState::show_more_actions() {
    dismiss();
    kind_ = ExplorerModalKind::MoreActions;
}

void ExplorerModalState::show_rename(std::string current_name) {
    dismiss();
    kind_ = ExplorerModalKind::RenameInput;
    name_purpose_ = ExplorerNameInputPurpose::Rename;
    subject_name_ = current_name;
    confirmation_input_ = std::move(current_name);
}

void ExplorerModalState::show_new_entry(const bool folder, std::string initial_name) {
    dismiss();
    kind_ = ExplorerModalKind::RenameInput;
    name_purpose_ = folder ? ExplorerNameInputPurpose::NewFolder
                           : ExplorerNameInputPurpose::NewFile;
    confirmation_input_ = std::move(initial_name);
}

void ExplorerModalState::show_tab_menu() {
    dismiss();
    kind_ = ExplorerModalKind::TabMenu;
}

void ExplorerModalState::dismiss() {
    kind_ = ExplorerModalKind::None;
    affected_count_ = 0;
    document_ = {};
    original_text_.clear();
    confirmation_input_.clear();
    subject_name_.clear();
    name_purpose_ = ExplorerNameInputPurpose::Rename;
    composition_.clear();
    composition_start_ = 0;
    composition_length_ = 0;
    cursor_ = 0;
    anchor_.reset();
    range_mode_ = false;
    text_editor_focused_ = false;
    search_active_ = false;
    search_query_.clear();
    scroll_line_ = 0;
}

void ExplorerModalState::insert(std::string_view text) {
    composition_.clear();
    composition_start_ = 0;
    composition_length_ = 0;
    if (kind_ == ExplorerModalKind::ConfirmEmptyTrash ||
        kind_ == ExplorerModalKind::RenameInput) {
        confirmation_input_.append(text);
        return;
    }
    if (kind_ != ExplorerModalKind::TextEditor || text.empty()) return;
    if (search_active_) {
        // 検索欄は1行入力とする。
        for (const char value : text)
            if (value != '\n' && value != '\r') search_query_.push_back(value);
        return;
    }
    replace_selection(text);
}

void ExplorerModalState::update_composition(std::string text, int start, int length) {
    if (!wants_text_input()) return;
    // SDLの開始位置と長さは変換中の文字数で表される。装飾付き描画に備
    // えてメタデータとして保持する。 共通モデルはUTF-8の未確定文
    // 字列を所有し、TEXT_INPUTが届くまでは文書へ挿入しない。
    (void)::cw::lib::regex_grapheme::grapheme_clusters(text);
    composition_ = std::move(text);
    composition_start_ = std::max(0, start);
    composition_length_ = std::max(0, length);
}

void ExplorerModalState::backspace() {
    if (kind_ == ExplorerModalKind::ConfirmEmptyTrash ||
        kind_ == ExplorerModalKind::RenameInput) {
        if (!confirmation_input_.empty())
            confirmation_input_.erase(previous_grapheme(confirmation_input_, confirmation_input_.size()));
        return;
    }
    if (kind_ != ExplorerModalKind::TextEditor) return;
    composition_.clear();
    if (search_active_) {
        if (!search_query_.empty())
            search_query_.erase(previous_grapheme(search_query_, search_query_.size()));
        return;
    }
    if (has_selection()) {
        delete_selection();
        return;
    }
    anchor_.reset();
    if (cursor_ == 0) return;
    const auto previous = previous_grapheme(document_.text, cursor_);
    document_.text.erase(previous, cursor_ - previous);
    cursor_ = previous;
    keep_cursor_visible();
}

void ExplorerModalState::erase_forward() {
    if (kind_ != ExplorerModalKind::TextEditor || search_active_) return;
    composition_.clear();
    if (has_selection()) {
        delete_selection();
        return;
    }
    anchor_.reset();
    if (cursor_ >= document_.text.size()) return;
    document_.text.erase(cursor_, next_grapheme(document_.text, cursor_) - cursor_);
}

void ExplorerModalState::begin_move(const bool extend) {
    composition_.clear();
    if (extend || range_mode_) {
        if (!anchor_) anchor_ = cursor_;
    } else {
        anchor_.reset();
    }
}

void ExplorerModalState::move_left(const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    // Shiftを押していなければ、選択範囲の先頭へカーソルを移して選択を解除する。
    if (!extend && !range_mode_ && has_selection()) {
        cursor_ = selection_range().first;
        anchor_.reset();
    } else {
        begin_move(extend);
        cursor_ = previous_grapheme(document_.text, cursor_);
    }
    keep_cursor_visible();
}

void ExplorerModalState::move_right(const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    // Shiftを押していなければ、選択範囲の末尾へカーソルを移して選択を解除する。
    if (!extend && !range_mode_ && has_selection()) {
        cursor_ = selection_range().second;
        anchor_.reset();
    } else {
        begin_move(extend);
        cursor_ = next_grapheme(document_.text, cursor_);
    }
    keep_cursor_visible();
}

std::vector<std::pair<std::size_t, std::size_t>> ExplorerModalState::line_ranges() const {
    std::vector<std::pair<std::size_t, std::size_t>> result;
    std::size_t start = 0;
    for (std::size_t offset = 0; offset < document_.text.size(); ++offset) {
        if (document_.text[offset] == '\n') {
            auto end = offset;
            if (end > start && document_.text[end - 1] == '\r') --end;
            result.emplace_back(start, end);
            start = offset + 1;
        }
    }
    result.emplace_back(start, document_.text.size());
    return result;
}

void ExplorerModalState::move_home(const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    begin_move(extend);
    const auto lines = line_ranges();
    for (const auto& [start, end] : lines) {
        if (cursor_ >= start && cursor_ <= end + 1) { cursor_ = start; break; }
    }
    keep_cursor_visible();
}

void ExplorerModalState::move_end(const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    begin_move(extend);
    const auto lines = line_ranges();
    for (const auto& [start, end] : lines) {
        if (cursor_ >= start && cursor_ <= end + 1) { cursor_ = end; break; }
    }
    keep_cursor_visible();
}

void ExplorerModalState::move_vertical(int direction, const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor || direction == 0) return;
    begin_move(extend);
    const auto lines = line_ranges();
    auto line = std::size_t{0};
    for (; line + 1 < lines.size() && cursor_ > lines[line].second; ++line) {}
    const auto column = grapheme_offsets(document_.text.substr(
        lines[line].first, std::min(cursor_, lines[line].second) - lines[line].first)).size() - 1;
    const auto target = direction < 0 ? (line == 0 ? 0 : line - 1) :
        std::min(lines.size() - 1, line + 1);
    cursor_ = byte_offset_for_column(document_.text, lines[target].first, lines[target].second,
                                     column);
    keep_cursor_visible();
}

void ExplorerModalState::scroll(int lines, std::size_t visible_lines) {
    if (kind_ != ExplorerModalKind::TextEditor || lines == 0) return;
    const auto count = line_ranges().size();
    const auto maximum = count > visible_lines ? count - visible_lines : 0;
    if (lines < 0) {
        const auto amount = static_cast<std::size_t>(-lines);
        scroll_line_ = amount > scroll_line_ ? 0 : scroll_line_ - amount;
    } else {
        scroll_line_ = std::min(maximum, scroll_line_ + static_cast<std::size_t>(lines));
    }
}

void ExplorerModalState::place_cursor_at(std::size_t document_line,
                                         std::size_t byte_in_line, const bool extend) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    const auto lines = line_ranges();
    if (lines.empty()) return;
    document_line = std::min(document_line, lines.size() - 1);
    begin_move(extend);
    const auto [start, end] = lines[document_line];
    cursor_ = snap_to_grapheme(document_.text, start + std::min(byte_in_line, end - start));
    cursor_ = std::clamp(cursor_, start, end);
    keep_cursor_visible();
}

bool ExplorerModalState::has_selection() const noexcept {
    return anchor_ && *anchor_ != cursor_;
}

std::pair<std::size_t, std::size_t> ExplorerModalState::selection_range() const noexcept {
    if (!anchor_) return {cursor_, cursor_};
    return {std::min(*anchor_, cursor_), std::max(*anchor_, cursor_)};
}

std::string ExplorerModalState::selected_text() const {
    const auto [start, end] = selection_range();
    return document_.text.substr(start, end - start);
}

void ExplorerModalState::select_all() {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    composition_.clear();
    anchor_ = 0;
    cursor_ = document_.text.size();
    keep_cursor_visible();
}

void ExplorerModalState::clear_selection() noexcept {
    anchor_.reset();
    range_mode_ = false;
}

void ExplorerModalState::delete_selection() {
    if (kind_ != ExplorerModalKind::TextEditor || !has_selection()) return;
    const auto [start, end] = selection_range();
    document_.text.erase(start, end - start);
    cursor_ = start;
    anchor_.reset();
    range_mode_ = false;
    keep_cursor_visible();
}

std::string ExplorerModalState::cut_selection() {
    auto text = has_selection() ? selected_text() : std::string{};
    delete_selection();
    return text;
}

void ExplorerModalState::replace_selection(std::string_view text) {
    if (has_selection()) {
        const auto [start, end] = selection_range();
        document_.text.erase(start, end - start);
        cursor_ = start;
    }
    anchor_.reset();
    range_mode_ = false;
    document_.text.insert(cursor_, text);
    cursor_ += text.size();
    keep_cursor_visible();
}

void ExplorerModalState::set_range_mode(const bool value) {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    range_mode_ = value;
    if (value && !anchor_) anchor_ = cursor_;
}

void ExplorerModalState::begin_search() {
    if (kind_ != ExplorerModalKind::TextEditor) return;
    composition_.clear();
    text_editor_focused_ = true;
    search_active_ = true;
    // 1行内の選択文字列を検索語の初期値に使う。
    if (has_selection()) {
        const auto selected = selected_text();
        if (selected.find('\n') == std::string::npos) search_query_ = selected;
    }
}

void ExplorerModalState::end_search() {
    composition_.clear();
    search_active_ = false;
}

bool ExplorerModalState::find(const bool forward) {
    if (kind_ != ExplorerModalKind::TextEditor || search_query_.empty()) return false;
    const auto& text = document_.text;
    const auto [selection_start, selection_end] = selection_range();
    std::size_t found = std::string::npos;
    if (forward) {
        found = text.find(search_query_, selection_end);
        if (found == std::string::npos) found = text.find(search_query_);
    } else {
        if (selection_start > 0)
            found = text.rfind(search_query_, selection_start - 1);
        if (found == std::string::npos) found = text.rfind(search_query_);
    }
    if (found == std::string::npos) return false;
    anchor_ = found;
    cursor_ = found + search_query_.size();
    range_mode_ = false;
    keep_cursor_visible();
    return true;
}

void ExplorerModalState::keep_cursor_visible(std::size_t visible_lines) {
    const auto lines = line_ranges();
    std::size_t cursor_line = 0;
    while (cursor_line + 1 < lines.size() && cursor_ > lines[cursor_line].second)
        ++cursor_line;
    if (cursor_line < scroll_line_) scroll_line_ = cursor_line;
    else if (cursor_line >= scroll_line_ + visible_lines)
        scroll_line_ = cursor_line - visible_lines + 1;
}

std::vector<TextEditorLine> ExplorerModalState::visible_lines(std::size_t count) const {
    std::vector<TextEditorLine> result;
    const auto lines = line_ranges();
    for (std::size_t index = scroll_line_; index < lines.size() && result.size() < count; ++index)
        result.push_back({index, lines[index].first, lines[index].second});
    return result;
}

}  // namespace cardwirth::explorer
