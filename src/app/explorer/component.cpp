#include "component.hpp"

#include <algorithm>

namespace cardwirth::explorer {

ExplorerComponent::ExplorerComponent(std::shared_ptr<FileSystem> filesystem,
                                     std::size_t rows_per_page, bool read_immediately)
    : controller_(std::move(filesystem), ArchiveRegistry::with_builtin_formats(), read_immediately),
      rows_per_page_(std::max<std::size_t>(1, rows_per_page)) {}

ExplorerComponent::ExplorerComponent(std::shared_ptr<FileSystem> filesystem,
                                     PreviewService previews,
                                     std::size_t rows_per_page, bool read_immediately)
    : controller_(std::move(filesystem), ArchiveRegistry::with_builtin_formats(),
                  std::move(previews), read_immediately),
      rows_per_page_(std::max<std::size_t>(1, rows_per_page)) {}

ExplorerViewState ExplorerComponent::view_state() const {
    ExplorerViewState state;
    state.current_directory = controller_.current_directory();
    state.copy_sources = controller_.copy_sources();
    state.status = controller_.status();
    state.item_count = controller_.entries().size();
    state.scroll_offset = scroll_offset_;
    state.visible_capacity = rows_per_page_;
    state.selected_count = controller_.selected_count();
    state.sort_column = controller_.sort_column();
    state.sort_direction = controller_.sort_direction();
    state.can_go_back = controller_.can_go_back();
    state.can_go_up = controller_.can_go_up();
    state.can_paste = !state.copy_sources.empty();
    state.can_scroll_up = scroll_offset_ != 0;
    state.can_scroll_down = scroll_offset_ + rows_per_page_ < state.item_count;

    state.in_trash = controller_.current_is_in_trash_tree();
    state.at_trash_root = controller_.current_is_in_trash();
    state.can_create = !state.in_trash;
    state.move_pending = controller_.move_pending();
    const auto selected_index = controller_.selected_index();
    const auto& entries = controller_.entries();
    if (selected_index && *selected_index < entries.size()) {
        state.can_open = state.selected_count == 1;
        state.can_copy = controller_.selected_can_copy();
        state.can_trash = !state.in_trash && controller_.selected_can_move_to_trash();
        state.can_extract = controller_.selected_is_archive();
        state.can_download = controller_.selected_can_copy();
        state.can_restore = controller_.selected_can_restore();
        state.can_delete_permanently = controller_.selected_can_delete_permanently();
        state.can_rename = controller_.selected_can_rename();
        state.can_move = !state.in_trash && controller_.selected_can_move_to_trash();
    }
    for (std::size_t row = 0; row < rows_per_page_; ++row) {
        const auto absolute = scroll_offset_ + row;
        if (absolute >= entries.size()) break;
        state.rows.push_back(ExplorerVisibleRow{
            row, absolute, entries[absolute], controller_.is_selected(absolute)});
    }
    return state;
}

void ExplorerComponent::scroll_by(int rows) {
    const auto item_count = controller_.entries().size();
    const auto maximum = item_count > rows_per_page_ ? item_count - rows_per_page_ : 0;
    if (rows < 0) {
        const auto amount = static_cast<std::size_t>(-rows);
        scroll_offset_ = amount > scroll_offset_ ? 0 : scroll_offset_ - amount;
    } else {
        scroll_offset_ = std::min(maximum, scroll_offset_ + static_cast<std::size_t>(rows));
    }
}

OperationResult ExplorerComponent::select_visible_row(std::size_t row, bool extend_range) {
    if (row >= rows_per_page_) return {false, "表示行が範囲外です"};
    return controller_.select(scroll_offset_ + row, extend_range);
}

OperationResult ExplorerComponent::navigate_to(const std::filesystem::path& directory) {
    const auto result = controller_.navigate_to(directory);
    if (result.ok) reset_scroll();
    return result;
}

OperationResult ExplorerComponent::go_back() {
    const auto result = controller_.go_back();
    if (result.ok) reset_scroll();
    return result;
}

OperationResult ExplorerComponent::go_up() {
    const auto result = controller_.go_up();
    if (result.ok) reset_scroll();
    return result;
}

OperationResult ExplorerComponent::refresh() {
    const auto result = controller_.refresh();
    if (result.ok) reset_scroll();
    return result;
}

void ExplorerComponent::toggle_sort(ExplorerSortColumn column) {
    controller_.toggle_sort(column);
    reset_scroll();
}

OperationResult ExplorerComponent::open_selected() {
    const auto result = controller_.open_selected();
    if (result.ok && !controller_.preview()) reset_scroll();
    return result;
}

void ExplorerComponent::navigate_to_async(std::filesystem::path directory) {
    (void)controller_.navigate_to_async(std::move(directory));
}
void ExplorerComponent::go_back_async() {
    (void)controller_.go_back_async();
}
void ExplorerComponent::go_up_async() {
    (void)controller_.go_up_async();
}
void ExplorerComponent::refresh_async() {
    (void)controller_.refresh_async();
}
void ExplorerComponent::open_selected_async(bool edit_text) {
    (void)controller_.open_selected_async(edit_text);
}
std::size_t ExplorerComponent::dispatch_async() {
    ++dispatch_depth_;
    struct Exit { unsigned& depth; ~Exit() { --depth; } } exit{dispatch_depth_};
    const auto revision = controller_.navigation_revision();
    const auto result = controller_.dispatch_async();
    if (controller_.navigation_revision() != revision) reset_scroll();
    return result;
}

OperationResult ExplorerComponent::begin_copy() { return controller_.begin_copy(); }

OperationResult ExplorerComponent::begin_move() { return controller_.begin_move(); }

OperationResult ExplorerComponent::paste_copy() {
    const auto result = controller_.paste_copy();
    if (result.ok) scroll_by(0);
    return result;
}

std::shared_ptr<ExplorerTask> ExplorerComponent::paste_copy_async(ExplorerCompletion completion) {
    return controller_.paste_copy_async(
        [this, completion = std::move(completion)](OperationResult result) mutable {
            if (result.ok) scroll_by(0);
            if (completion) completion(std::move(result));
        });
}

OperationResult ExplorerComponent::import_external(
    const std::vector<std::filesystem::path>& sources, bool overwrite) {
    const auto result = controller_.import_external(sources, overwrite);
    if (result.ok) scroll_by(0);
    return result;
}

std::shared_ptr<ExplorerTask> ExplorerComponent::import_external_async(
    std::vector<std::filesystem::path> sources,
    ExplorerCompletion completion, bool overwrite) {
    return controller_.import_external_async(
        std::move(sources),
        [this, completion = std::move(completion)](OperationResult result) mutable {
            if (result.ok) scroll_by(0);
            if (completion) completion(std::move(result));
        }, overwrite);
}

OperationResult ExplorerComponent::export_selected(
    const std::filesystem::path& destination_directory) {
    return controller_.export_selected(destination_directory);
}

OperationResult ExplorerComponent::export_selected_as(
    const std::filesystem::path& destination) {
    return controller_.export_selected_as(destination);
}

OperationResult ExplorerComponent::export_selected_as_zip(
    const std::filesystem::path& destination) {
    return controller_.export_selected_as_zip(destination);
}

std::shared_ptr<ExplorerTask> ExplorerComponent::export_selected_async(
    std::filesystem::path destination_directory, ExplorerCompletion completion) {
    return controller_.export_selected_async(std::move(destination_directory),
                                             std::move(completion));
}

std::shared_ptr<ExplorerTask> ExplorerComponent::export_selected_as_async(
    std::filesystem::path destination, ExplorerCompletion completion) {
    return controller_.export_selected_as_async(std::move(destination),
                                                std::move(completion));
}

std::shared_ptr<ExplorerTask> ExplorerComponent::export_selected_as_zip_async(
    std::filesystem::path destination, ExplorerCompletion completion) {
    return controller_.export_selected_as_zip_async(std::move(destination),
                                                    std::move(completion));
}

OperationResult ExplorerComponent::move_selected_to_trash() {
    const auto result = controller_.delete_selected();
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::restore_selected_from_trash() {
    const auto result = controller_.restore_selected_from_trash();
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::delete_selected_permanently() {
    const auto result = controller_.delete_selected_permanently();
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::empty_trash() {
    const auto result = controller_.empty_trash();
    if (result.ok) reset_scroll();
    return result;
}

OperationResult ExplorerComponent::rename_selected(const std::string& new_name) {
    const auto result = controller_.rename_selected(new_name);
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::create_entry(const std::string& name, const bool folder) {
    const auto result = controller_.create_entry(name, folder);
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::extract_selected() {
    const auto result = controller_.extract_selected();
    if (result.ok) scroll_by(0);
    return result;
}

std::shared_ptr<ExplorerTask> ExplorerComponent::extract_selected_async(
    ExplorerCompletion completion) {
    return controller_.extract_selected_async(
        [this, completion = std::move(completion)](OperationResult result) mutable {
            if (result.ok) scroll_by(0);
            if (completion) completion(std::move(result));
        });
}

OperationResult ExplorerComponent::flatten_extracted_directory() {
    const auto result = controller_.flatten_extracted_directory();
    if (result.ok) scroll_by(0);
    return result;
}

OperationResult ExplorerComponent::load_selected_text(TextDocument& document) {
    return controller_.load_selected_text(document);
}

OperationResult ExplorerComponent::save_text_document(const TextDocument& document) {
    return controller_.save_text_document(document);
}

void ExplorerComponent::cancel_copy() { controller_.cancel_copy(); }

void ExplorerComponent::close_preview() { controller_.close_preview(); }

}  // namespace cardwirth::explorer
