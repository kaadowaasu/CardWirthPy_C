#pragma once

#include "text_document.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cardwirth::explorer {

// テキストエディターで一度に表示する文書の行数。
inline constexpr std::size_t kTextEditorVisibleLines = 20;

// テキストエディターの表示行。文書内のバイト位置で範囲を表す。
struct TextEditorLine {
    std::size_t number{};  // 0始まりの文書行番号。
    std::size_t start{};
    std::size_t end{};     // 改行を含まない。
};

// 1行の名前入力欄（RenameInput）の用途。
enum class ExplorerNameInputPurpose { Rename, NewFile, NewFolder };

enum class ExplorerModalKind {
    None,
    ConfirmMoveToTrash,
    ConfirmRestoreFromTrash,
    ConfirmDeletePermanently,
    ConfirmOpenText,
    TextEditor,
    ConfirmEmptyTrash,
    ChooseUploadType,
    DownloadProgress,
    ExtractProgress,
    ConfirmFlattenExtraction,
    ContextActions,
    MoreActions,
    MovePasteNotice,
    RenameInput,
    TabMenu,
};

class ExplorerModalState {
public:
    [[nodiscard]] ExplorerModalKind kind() const noexcept { return kind_; }
    [[nodiscard]] bool active() const noexcept { return kind_ != ExplorerModalKind::None; }
    [[nodiscard]] bool wants_text_input() const noexcept {
        return (kind_ == ExplorerModalKind::TextEditor && text_editor_focused_) ||
            kind_ == ExplorerModalKind::ConfirmEmptyTrash ||
            kind_ == ExplorerModalKind::RenameInput;
    }
    [[nodiscard]] bool text_editor_focused() const noexcept {
        return kind_ == ExplorerModalKind::TextEditor && text_editor_focused_;
    }
    void focus_text_editor();
    void blur_text_editor();
    [[nodiscard]] std::size_t affected_count() const noexcept { return affected_count_; }
    [[nodiscard]] const TextDocument& document() const noexcept { return document_; }
    [[nodiscard]] TextDocument& document() noexcept { return document_; }
    [[nodiscard]] bool dirty() const noexcept { return document_.text != original_text_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    [[nodiscard]] std::size_t scroll_line() const noexcept { return scroll_line_; }
    [[nodiscard]] const std::string& confirmation_input() const noexcept {
        return confirmation_input_;
    }
    [[nodiscard]] bool empty_trash_confirmed() const noexcept {
        return confirmation_input_ == "trash";
    }
    [[nodiscard]] const std::string& subject_name() const noexcept { return subject_name_; }
    [[nodiscard]] const std::string& composition() const noexcept { return composition_; }

    void show_move_to_trash(std::size_t count);
    void show_restore_from_trash(std::size_t count);
    void show_delete_permanently(std::size_t count);
    void show_open_text(TextDocument document);
    void begin_text_editor();
    void show_empty_trash();
    void show_upload_type();
    void show_download_progress();
    void show_extract_progress(std::string title = {});
    void show_flatten_extraction(std::string name);
    void show_context_actions();
    void show_more_actions();
    void show_move_paste_notice();
    void show_rename(std::string current_name);
    // 新しい空ファイル・フォルダーの名前入力（RenameInput種別）。
    void show_new_entry(bool folder, std::string initial_name);
    [[nodiscard]] ExplorerNameInputPurpose name_input_purpose() const noexcept {
        return name_purpose_;
    }
    void show_tab_menu();
    void dismiss();

    // 検索欄が開いている間は、文字入力を検索語へ送る。
    void insert(std::string_view text);
    void update_composition(std::string text, int start, int length);
    // 選択範囲がある場合は、その範囲を削除する。
    void backspace();
    void erase_forward();
    // extendがtrueなら選択の基点を保ったままカーソルを移動する。 Shift
    // ＋矢印・Shift＋クリック・マウスドラッグ・タッチの範囲選択で使う。
    void move_left(bool extend = false);
    void move_right(bool extend = false);
    void move_home(bool extend = false);
    void move_end(bool extend = false);
    void move_vertical(int lines, bool extend = false);
    void scroll(int lines, std::size_t visible_lines = kTextEditorVisibleLines);
    // 文書行内のバイト位置へカーソルを置
    // き、位置を書記素の境界へ合わせる。
    void place_cursor_at(std::size_t document_line, std::size_t byte_in_line,
                         bool extend = false);

    // 選択範囲は基点とカーソルの間とする。
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection_range() const noexcept;
    [[nodiscard]] std::string selected_text() const;
    void select_all();
    void clear_selection() noexcept;
    void delete_selection();
    [[nodiscard]] std::string cut_selection();
    // タッチによる範囲選択。有効な間は、有効化した時点のカーソ
    // ル位置を基点として カーソルの移動で選択範囲を広げる。
    [[nodiscard]] bool range_mode() const noexcept { return range_mode_; }
    void set_range_mode(bool value);

    // 検索欄。
    [[nodiscard]] bool search_active() const noexcept { return search_active_; }
    [[nodiscard]] const std::string& search_query() const noexcept { return search_query_; }
    void begin_search();
    void end_search();
    // 検索語の次または前の一致箇所を選択し、文書の端では反
    // 対側へ折り返す。 一致がなければfalseを返す。
    bool find(bool forward);

    [[nodiscard]] std::vector<TextEditorLine> visible_lines(
        std::size_t count = kTextEditorVisibleLines) const;

private:
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> line_ranges() const;
    void keep_cursor_visible(std::size_t visible_lines = kTextEditorVisibleLines);
    void begin_move(bool extend);
    void replace_selection(std::string_view text);

    ExplorerModalKind kind_{ExplorerModalKind::None};
    std::size_t affected_count_{};
    TextDocument document_;
    std::string original_text_;
    std::string confirmation_input_;
    std::string subject_name_;
    ExplorerNameInputPurpose name_purpose_{ExplorerNameInputPurpose::Rename};
    std::string composition_;
    int composition_start_{};
    int composition_length_{};
    std::size_t cursor_{};
    std::optional<std::size_t> anchor_;
    bool range_mode_{};
    bool text_editor_focused_{};
    bool search_active_{};
    std::string search_query_;
    std::size_t scroll_line_{};
};

}  // namespace cardwirth::explorer
