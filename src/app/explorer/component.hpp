#pragma once

#include "explorer.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cardwirth::explorer {

struct ExplorerVisibleRow {
    std::size_t visible_row{};
    std::size_t absolute_index{};
    FileEntry entry;
    bool selected{};
};

// Windows/SDLのGUIが参照する、バックエンドに
// 依存しない全状態。 操作の可否とページ切り替えをここで管
// 理し、ホスト側にExplorerの規則を重複させない。
struct ExplorerViewState {
    std::filesystem::path current_directory;
    std::vector<ExplorerVisibleRow> rows;
    std::vector<std::filesystem::path> copy_sources;
    std::string status;
    std::size_t item_count{};
    std::size_t scroll_offset{};
    std::size_t visible_capacity{};
    std::size_t selected_count{};
    ExplorerSortColumn sort_column{ExplorerSortColumn::Name};
    ExplorerSortDirection sort_direction{ExplorerSortDirection::Ascending};
    bool can_go_back{};
    bool can_go_up{};
    bool can_open{};
    bool can_copy{};
    bool can_paste{};
    bool can_trash{};
    bool can_extract{};
    bool can_download{};
    bool can_restore{};
    bool can_delete_permanently{};
    bool can_rename{};
    bool can_create{};
    bool can_move{};
    bool move_pending{};
    bool can_scroll_up{};
    bool can_scroll_down{};
    bool in_trash{};
    bool at_trash_root{};
};

// バックエンドに依存しない、再利用可能な一覧部品の状態。 行の見た目はネイティブ/SDL
// 側で決めるが、ページ切り替えと選択インデックスはすべてのOSで共通にする。
class ExplorerComponent {
public:
    explicit ExplorerComponent(std::shared_ptr<FileSystem> filesystem,
                               std::size_t rows_per_page = 7, bool read_immediately = true);
    ExplorerComponent(std::shared_ptr<FileSystem> filesystem,
                      PreviewService previews,
                      std::size_t rows_per_page = 7, bool read_immediately = true);

    [[nodiscard]] ExplorerController& controller() noexcept { return controller_; }
    [[nodiscard]] const ExplorerController& controller() const noexcept { return controller_; }
    [[nodiscard]] std::size_t scroll_offset() const noexcept { return scroll_offset_; }
    [[nodiscard]] std::size_t rows_per_page() const noexcept { return rows_per_page_; }
    [[nodiscard]] ExplorerViewState view_state() const;

    void scroll_by(int rows);
    void reset_scroll() noexcept { scroll_offset_ = 0; }
    OperationResult select_visible_row(std::size_t row, bool extend_range = false);
    OperationResult navigate_to(const std::filesystem::path& directory);
    OperationResult go_back();
    OperationResult go_up();
    OperationResult refresh();
    void toggle_sort(ExplorerSortColumn column);
    OperationResult open_selected();
    void navigate_to_async(std::filesystem::path directory);
    void go_back_async();
    void go_up_async();
    void refresh_async();
    void open_selected_async(bool edit_text = false);
    std::size_t dispatch_async();
    [[nodiscard]] bool dispatching() const noexcept { return dispatch_depth_ != 0; }
    OperationResult begin_copy();
    OperationResult begin_move();
    OperationResult paste_copy();
    [[nodiscard]] std::shared_ptr<ExplorerTask> paste_copy_async(ExplorerCompletion completion = {});
    OperationResult import_external(const std::vector<std::filesystem::path>& sources,
                                    bool overwrite = false);
    [[nodiscard]] std::shared_ptr<ExplorerTask> import_external_async(
        std::vector<std::filesystem::path> sources,
        ExplorerCompletion completion = {}, bool overwrite = false);
    OperationResult export_selected(const std::filesystem::path& destination_directory);
    OperationResult export_selected_as(const std::filesystem::path& destination);
    OperationResult export_selected_as_zip(const std::filesystem::path& destination);
    [[nodiscard]] std::shared_ptr<ExplorerTask> export_selected_async(
        std::filesystem::path destination_directory, ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> export_selected_as_async(
        std::filesystem::path destination, ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> export_selected_as_zip_async(
        std::filesystem::path destination, ExplorerCompletion completion = {});
    OperationResult move_selected_to_trash();
    OperationResult restore_selected_from_trash();
    OperationResult delete_selected_permanently();
    OperationResult empty_trash();
    OperationResult rename_selected(const std::string& new_name);
    OperationResult create_entry(const std::string& name, bool folder);
    OperationResult extract_selected();
    [[nodiscard]] std::shared_ptr<ExplorerTask> extract_selected_async(
        ExplorerCompletion completion = {});
    OperationResult flatten_extracted_directory();
    void keep_extracted_directory() noexcept { controller_.keep_extracted_directory(); }
    OperationResult load_selected_text(TextDocument& document);
    OperationResult save_text_document(const TextDocument& document);
    void cancel_copy();
    void close_preview();
    void set_status(std::string status) { controller_.set_status(std::move(status)); }

private:
    ExplorerController controller_;
    unsigned dispatch_depth_{};
    std::size_t scroll_offset_{};
    std::size_t rows_per_page_{7};
};

}  // namespace cardwirth::explorer
