#pragma once

#include "archive.hpp"
#include "filesystem.hpp"
#include "preview.hpp"
#include "text_document.hpp"

#include <cstddef>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cardwirth::explorer {

// Android NDK 28のlibc++ではC++20のstop_token
// が無効になっている。 Explorerのキャンセル処理はプラットフォームに依存
// させず、 非同期ファイル操作が特定の標準ライブラリ実装に依存しないようにする。
class ExplorerCancellationToken {
public:
    ExplorerCancellationToken() = default;
    explicit ExplorerCancellationToken(
        std::shared_ptr<const std::atomic_bool> cancelled)
        : cancelled_(std::move(cancelled)) {}

    [[nodiscard]] bool stop_requested() const noexcept {
        return cancelled_ && cancelled_->load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<const std::atomic_bool> cancelled_;
};

struct OperationResult {
    bool ok{};
    std::string message;
};

// ExplorerのI/Oを協調的に停止するためのワーカーハンドル。 ワー
// カーは完了コールバックを直接呼ばず、所有者のUIスレッドが ExplorerIoService::dispatch()
// で完了通知を処理する。
class ExplorerTask {
public:
    virtual ~ExplorerTask() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool finished() const noexcept = 0;
    virtual void wait() = 0;
};

using ExplorerCompletion = std::function<void(OperationResult)>;

class ExplorerIoService {
public:
    ExplorerIoService();
    ~ExplorerIoService();
    ExplorerIoService(const ExplorerIoService&) = delete;
    ExplorerIoService& operator=(const ExplorerIoService&) = delete;

    [[nodiscard]] std::shared_ptr<ExplorerTask> submit(
        std::function<OperationResult(ExplorerCancellationToken)> operation,
        ExplorerCompletion completion = {});
    // 所有者のUI・イベントループのスレッドから呼ぶ。実行したコールバック数を返す
    // 。 コールバックはExplorerControllerの状態を変更できる。
    std::size_t dispatch(std::size_t maximum = 0U);
    void cancel_all() noexcept;
    // モデルを破棄する前に、保留中の完了通知を取り消して所有するワーカーをjoinする。
    void shutdown() noexcept;
    [[nodiscard]] bool busy() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class ExplorerSortColumn {
    Name,
    Size,
    Modified,
};

enum class ExplorerSortDirection {
    Ascending,
    Descending,
};

// プラットフォームに依存しないExplorerの操作API。 SDL・ネイティブ
// の画面ではボタンをこれらのコマンドへ接続し、結果の状態を描画するだけにする。
class ExplorerController {
public:
    explicit ExplorerController(
        std::shared_ptr<FileSystem> filesystem,
        ArchiveRegistry archives = ArchiveRegistry::with_builtin_formats(), bool read_immediately = true);
    ExplorerController(std::shared_ptr<FileSystem> filesystem,
                       ArchiveRegistry archives,
                       PreviewService previews, bool read_immediately = true);
    ~ExplorerController();

    [[nodiscard]] const std::filesystem::path& current_directory() const noexcept {
        return current_directory_;
    }
    [[nodiscard]] const FileSystem& filesystem() const noexcept { return *filesystem_; }
    [[nodiscard]] const std::vector<FileEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] const std::set<std::filesystem::path>& selected_paths() const noexcept {
        return selected_paths_;
    }
    [[nodiscard]] std::size_t selected_count() const noexcept { return selected_paths_.size(); }
    [[nodiscard]] bool is_selected(std::size_t index) const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& copy_sources() const noexcept {
        return copy_sources_;
    }
    [[nodiscard]] const std::optional<PreviewData>& preview() const noexcept { return preview_; }
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    [[nodiscard]] ExplorerIoService& io_service() noexcept { return io_service_; }
    [[nodiscard]] const ExplorerIoService& io_service() const noexcept { return io_service_; }
    [[nodiscard]] ExplorerSortColumn sort_column() const noexcept { return sort_column_; }
    [[nodiscard]] ExplorerSortDirection sort_direction() const noexcept { return sort_direction_; }
    void set_status(std::string status) { status_ = std::move(status); }
    [[nodiscard]] bool can_go_up() const noexcept { return !current_directory_.empty(); }
    [[nodiscard]] bool can_go_back() const noexcept { return !history_.empty(); }
    [[nodiscard]] bool selected_is_archive() const;
    [[nodiscard]] bool selected_is_editable_text() const;
    [[nodiscard]] bool selected_can_copy() const;
    [[nodiscard]] bool selected_can_move_to_trash() const;
    [[nodiscard]] bool selected_can_restore() const;
    [[nodiscard]] bool selected_can_delete_permanently() const;
    [[nodiscard]] bool selected_can_rename() const;
    [[nodiscard]] bool current_is_in_trash() const;
    [[nodiscard]] bool current_is_in_trash_tree() const;
    [[nodiscard]] bool has_flatten_candidate() const noexcept {
        return flatten_candidate_.has_value();
    }
    [[nodiscard]] std::string flatten_candidate_name() const;
    OperationResult refresh();
    void toggle_sort(ExplorerSortColumn column);
    OperationResult select(std::size_t index, bool extend_range = false);
    OperationResult navigate_to(const std::filesystem::path& directory);
    OperationResult open_selected();
    OperationResult go_back();
    OperationResult go_up();
    [[nodiscard]] std::shared_ptr<ExplorerTask> navigate_to_async(
        std::filesystem::path directory, ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> refresh_async(ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> go_back_async(ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> go_up_async(ExplorerCompletion completion = {});
    [[nodiscard]] std::shared_ptr<ExplorerTask> open_selected_async(
        bool edit_text = false, ExplorerCompletion completion = {});
    [[nodiscard]] std::optional<TextDocument> take_opened_text() {
        return std::exchange(opened_text_, std::nullopt);
    }
    [[nodiscard]] std::uint64_t navigation_revision() const noexcept { return navigation_revision_; }
    OperationResult begin_copy();
    // begin_copy()と同様だが、次の貼り付けで項目を移動する。
    OperationResult begin_move();
    [[nodiscard]] bool move_pending() const noexcept { return move_mode_; }
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
    void cancel_copy();
    OperationResult delete_selected();
    OperationResult restore_selected_from_trash();
    OperationResult delete_selected_permanently();
    OperationResult empty_trash();
    OperationResult rename_selected(const std::string& new_name);
    // 現在のディレクトリに空のファイルまたはフォルダーを作成し、選択する。
    OperationResult create_entry(const std::string& name, bool folder);
    OperationResult extract_selected();
    [[nodiscard]] std::shared_ptr<ExplorerTask> extract_selected_async(
        ExplorerCompletion completion = {});
    OperationResult flatten_extracted_directory();
    void keep_extracted_directory() noexcept { flatten_candidate_.reset(); }
    OperationResult load_selected_text(TextDocument& document);
    OperationResult save_text_document(const TextDocument& document);
    void close_preview();
    void set_audio_suspended(bool suspended) noexcept { previews_.set_audio_suspended(suspended); }

    // 大きくなり得るファイルシステム・アーカイブ処理を、UIコールバックの外で
    // 実行する。 処理は不変の入力を保持し、キャンセルトークンで協調的に停止す
    // る。 完了コールバックはdispatch()の呼び出し後にのみ実行する。
    [[nodiscard]] std::shared_ptr<ExplorerTask> submit_async(
        std::function<OperationResult(ExplorerCancellationToken)> operation,
        ExplorerCompletion completion = {});
    std::size_t dispatch_async(std::size_t maximum = 0U) {
        const bool audio_finished=previews_.collect_finished_audio();
        return io_service_.dispatch(maximum)+(audio_finished?1U:0U);
    }
    void cancel_async() noexcept { invalidate_read(); io_service_.cancel_all(); }

private:
    enum class ReadKind { Navigate, Refresh, Back };
    void invalidate_read() noexcept;
    [[nodiscard]] std::shared_ptr<ExplorerTask> read_directory_async(
        std::filesystem::path directory, ReadKind kind, ExplorerCompletion completion);
    [[nodiscard]] const FileEntry* selected() const noexcept;
    [[nodiscard]] std::vector<const FileEntry*> selected_entries() const;
    void clear_selection() noexcept;
    void prune_selection();
    void sort_entries();
    OperationResult fail(std::string message);
    OperationResult succeed(std::string message);
    OperationResult finish_paste(std::size_t completed, OperationResult result);
    bool paste_busy_{};

    std::shared_ptr<FileSystem> filesystem_;
    ArchiveRegistry archives_;
    PreviewService previews_;
    ExplorerIoService io_service_;
    std::shared_ptr<ExplorerTask> read_task_;
    std::uint64_t read_generation_{};
    std::uint64_t navigation_revision_{};
    std::optional<TextDocument> opened_text_;
    std::filesystem::path current_directory_;
    std::vector<std::filesystem::path> history_;
    std::vector<FileEntry> entries_;
    std::set<std::filesystem::path> selected_paths_;
    std::optional<std::filesystem::path> active_path_;
    std::optional<std::filesystem::path> selection_anchor_;
    std::vector<std::filesystem::path> copy_sources_;
    bool move_mode_{};
    std::optional<PreviewData> preview_;
    std::optional<std::filesystem::path> flatten_candidate_;
    std::string status_;
    ExplorerSortColumn sort_column_{ExplorerSortColumn::Name};
    ExplorerSortDirection sort_direction_{ExplorerSortDirection::Ascending};
};

}  // namespace cardwirth::explorer
