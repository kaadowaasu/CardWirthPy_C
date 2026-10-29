#include "explorer.hpp"

#include "cw/lib/archive.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

namespace cardwirth::explorer {
namespace {
void validate_upload_names(const std::shared_ptr<FileSystem>& filesystem,
    const std::filesystem::path& destination,const std::vector<std::filesystem::path>& sources,
    bool overwrite) {
    std::vector<std::string> existing;
    std::vector<std::string> selected;
    const auto key=[](const std::filesystem::path& path){
        const auto raw=path.filename().u8string();
        std::string name(reinterpret_cast<const char*>(raw.data()),raw.size());
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return name;
    };
    for(const auto& entry:filesystem->list(destination))existing.push_back(key(entry.path));
    for(const auto& source:sources){
        const auto name=key(source);
        if(name.empty()||std::find(selected.begin(),selected.end(),name)!=selected.end())
            throw std::runtime_error("アップロードする項目同士に同じ名前があります");
        if (!overwrite && std::find(existing.begin(),existing.end(),name)!=existing.end())
            throw std::runtime_error("同名の項目があります。上書き確認後に再実行してください");
        selected.push_back(name);
    }
}

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

}  // namespace

struct ExplorerIoService::Impl {
    struct Completion {
        ExplorerCompletion callback;
        OperationResult result;
    };

    struct Task final : ExplorerTask {
        Task(Impl& owner,
             std::function<OperationResult(ExplorerCancellationToken)> operation,
             ExplorerCompletion completion)
            : owner_(owner), operation_(std::move(operation)),
              completion_(std::move(completion)),
              cancelled_(std::make_shared<std::atomic_bool>(false)),
              worker_([this] { run(ExplorerCancellationToken(cancelled_)); }) {}

        ~Task() override {
            cancel();
            wait();
        }

        void cancel() noexcept override {
            cancelled_->store(true, std::memory_order_release);
        }

        [[nodiscard]] bool finished() const noexcept override {
            return finished_.load(std::memory_order_acquire);
        }

        void wait() override {
            if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
                worker_.join();
        }

    private:
        void run(const ExplorerCancellationToken token) noexcept {
            OperationResult result{false, "Explorer操作に失敗しました"};
            try {
                if (token.stop_requested()) {
                    result = {false, "Explorer操作をキャンセルしました"};
                } else if (operation_) {
                    result = operation_(token);
                    if (token.stop_requested())
                        result = {false, "Explorer操作をキャンセルしました"};
                }
            } catch (const std::exception& error) {
                result = {false, error.what()};
            } catch (...) {
                result = {false, "Explorer操作で不明なエラーが発生しました"};
            }
            if (completion_) owner_.enqueue(std::move(completion_), std::move(result));
            finished_.store(true, std::memory_order_release);
        }

        Impl& owner_;
        std::function<OperationResult(ExplorerCancellationToken)> operation_;
        ExplorerCompletion completion_;
        std::shared_ptr<std::atomic_bool> cancelled_;
        std::atomic_bool finished_{};
        std::thread worker_;
    };

    ~Impl() {
        std::vector<std::shared_ptr<Task>> tasks;
        {
            std::scoped_lock lock(mutex);
            stopping = true;
            tasks.swap(active);
            completions.clear();
        }
        for (const auto& task : tasks) task->cancel();
        for (const auto& task : tasks) task->wait();
    }

    void enqueue(ExplorerCompletion callback, OperationResult result) noexcept {
        try {
            std::scoped_lock lock(mutex);
            if (!stopping && callback)
                completions.push_back({std::move(callback), std::move(result)});
        } catch (...) {
            // 完了通知の処理によってワーカースレッドを異常終了させない。
        }
    }

    mutable std::mutex mutex;
    std::deque<Completion> completions;
    std::vector<std::shared_ptr<Task>> active;
    bool stopping{};
};

ExplorerIoService::ExplorerIoService() : impl_(std::make_unique<Impl>()) {}

ExplorerIoService::~ExplorerIoService() = default;
void ExplorerIoService::shutdown() noexcept { impl_.reset(); }

std::shared_ptr<ExplorerTask> ExplorerIoService::submit(
    std::function<OperationResult(ExplorerCancellationToken)> operation,
    ExplorerCompletion completion) {
    if (!operation) throw std::invalid_argument("Explorer非同期操作が空です");
    if (!impl_) throw std::runtime_error("Explorerは終了済みです");
    auto task = std::make_shared<Impl::Task>(*impl_, std::move(operation),
                                              std::move(completion));
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->stopping) {
            task->cancel();
            return task;
        }
        impl_->active.push_back(task);
    }
    return task;
}

std::size_t ExplorerIoService::dispatch(const std::size_t maximum) {
    if (!impl_) return 0;
    std::size_t count{};
    for (;;) {
        Impl::Completion completion;
        {
            std::scoped_lock lock(impl_->mutex);
            if (impl_->completions.empty() ||
                (maximum != 0U && count >= maximum)) break;
            completion = std::move(impl_->completions.front());
            impl_->completions.pop_front();
        }
        if (completion.callback) completion.callback(std::move(completion.result));
        ++count;
    }
    {
        std::scoped_lock lock(impl_->mutex);
        std::erase_if(impl_->active, [](const auto& task) {
            return task->finished() && task.use_count() == 1;
        });
    }
    return count;
}

void ExplorerIoService::cancel_all() noexcept {
    if (!impl_) return;
    try {
        std::vector<std::shared_ptr<Impl::Task>> tasks;
        {
            std::scoped_lock lock(impl_->mutex);
            tasks = impl_->active;
        }
        for (const auto& task : tasks) task->cancel();
    } catch (...) {
    }
}

bool ExplorerIoService::busy() const noexcept {
    if (!impl_) return false;
    std::scoped_lock lock(impl_->mutex);
    return std::any_of(impl_->active.begin(), impl_->active.end(),
                       [](const auto& task) { return !task->finished(); });
}

ExplorerController::ExplorerController(std::shared_ptr<FileSystem> filesystem,
                                       ArchiveRegistry archives, bool read_immediately)
    : ExplorerController(filesystem, std::move(archives),
                         PreviewService(filesystem ? filesystem->root() / "Data" :
                                        std::filesystem::path{}), read_immediately) {}

ExplorerController::ExplorerController(std::shared_ptr<FileSystem> filesystem,
                                       ArchiveRegistry archives,
                                       PreviewService previews, bool read_immediately)
    : filesystem_(std::move(filesystem)), archives_(std::move(archives)),
      previews_(std::move(previews)) {
    if (!filesystem_) throw std::invalid_argument("FileSystemがnullです");
    if (read_immediately) {
        const auto result = refresh();
        if (!result.ok) throw std::runtime_error(result.message);
    } else {
        (void)refresh_async();
    }
}

ExplorerController::~ExplorerController() {
    // 終了処理中は完了コールバックを呼ばない。ワーカーはファイルシステムへの入力と デ
    // コード結果だけを所有し、コントローラーやホストのモデルへのポインターを持たない。
    invalidate_read();
    io_service_.shutdown();
}

std::shared_ptr<ExplorerTask> ExplorerController::submit_async(
    std::function<OperationResult(ExplorerCancellationToken)> operation,
    ExplorerCompletion completion) {
    return io_service_.submit(std::move(operation), std::move(completion));
}

std::optional<std::size_t> ExplorerController::selected_index() const noexcept {
    if (!active_path_) return std::nullopt;
    const auto found = std::find_if(entries_.begin(), entries_.end(), [this](const FileEntry& entry) {
        return entry.path == *active_path_;
    });
    if (found == entries_.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(entries_.begin(), found));
}

bool ExplorerController::is_selected(std::size_t index) const noexcept {
    return index < entries_.size() && selected_paths_.contains(entries_[index].path);
}

const FileEntry* ExplorerController::selected() const noexcept {
    if (selected_paths_.size() != 1) return nullptr;
    const auto index = selected_index();
    return index ? &entries_[*index] : nullptr;
}

std::vector<const FileEntry*> ExplorerController::selected_entries() const {
    std::vector<const FileEntry*> result;
    for (const auto& entry : entries_) {
        if (selected_paths_.contains(entry.path)) result.push_back(&entry);
    }
    return result;
}

void ExplorerController::clear_selection() noexcept {
    selected_paths_.clear();
    active_path_.reset();
    selection_anchor_.reset();
}

void ExplorerController::prune_selection() {
    std::set<std::filesystem::path> available;
    for (const auto& entry : entries_) available.insert(entry.path);
    std::erase_if(selected_paths_, [&available](const auto& path) {
        return !available.contains(path);
    });
    if (active_path_ && !selected_paths_.contains(*active_path_)) active_path_.reset();
    if (selection_anchor_ && !available.contains(*selection_anchor_)) selection_anchor_.reset();
}

void ExplorerController::sort_entries() {
    const auto folded_name = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    };
    const auto compare_name = [&folded_name](const FileEntry& left, const FileEntry& right) {
        const auto left_name = folded_name(left.name);
        const auto right_name = folded_name(right.name);
        if (left_name != right_name) return left_name < right_name ? -1 : 1;
        if (left.name != right.name) return left.name < right.name ? -1 : 1;
        return left.path < right.path ? -1 : right.path < left.path ? 1 : 0;
    };
    const bool ascending = sort_direction_ == ExplorerSortDirection::Ascending;
    std::stable_sort(entries_.begin(), entries_.end(), [&](const FileEntry& left, const FileEntry& right) {
        // DataLoaderはすべての並べ替えでコンテナーを末端項目よ
        // り前に置く。 選択した列と昇順・降順は各グループ内だけに適用し
        // 、 降順でもファイルがディレクトリの前へ移らないようにする。
        const bool left_directory = left.type == EntryType::Directory;
        const bool right_directory = right.type == EntryType::Directory;
        if (left_directory != right_directory) return left_directory;
        int comparison = 0;
        switch (sort_column_) {
            case ExplorerSortColumn::Name:
                comparison = compare_name(left, right);
                break;
            case ExplorerSortColumn::Size:
                if (left.size != right.size) comparison = left.size < right.size ? -1 : 1;
                break;
            case ExplorerSortColumn::Modified:
                if (left.modified != right.modified) {
                    if (!left.modified) comparison = -1;
                    else if (!right.modified) comparison = 1;
                    else comparison = *left.modified < *right.modified ? -1 : 1;
                }
                break;
        }
        if (comparison == 0) comparison = compare_name(left, right);
        return ascending ? comparison < 0 : comparison > 0;
    });
}

void ExplorerController::toggle_sort(ExplorerSortColumn column) {
    if (sort_column_ == column) {
        sort_direction_ = sort_direction_ == ExplorerSortDirection::Ascending
            ? ExplorerSortDirection::Descending : ExplorerSortDirection::Ascending;
    } else {
        sort_column_ = column;
        sort_direction_ = ExplorerSortDirection::Ascending;
    }
    sort_entries();
}

bool ExplorerController::selected_is_archive() const {
    const auto* entry = selected();
    return entry && entry->type == EntryType::File && archives_.supports(entry->path);
}

bool ExplorerController::selected_is_editable_text() const {
    const auto* entry = selected();
    if (!entry || entry->type != EntryType::File) return false;
    // 選択したバイナリーを毎回テキスト読み込みで判別しない。メディア
    // の誤判定に加え、 ブラウザーではPreviewServiceが
    // 必要なデータだけを読む前にファイル全体の取得が発生してしまう。
    if (!TextDocumentService::has_editable_extension(entry->path)) return false;
    try {
        (void)TextDocumentService::load(filesystem_->absolute(entry->path), entry->path);
        return true;
    } catch (...) {
        return false;
    }
}

bool ExplorerController::selected_can_copy() const {
    const auto selected = selected_entries();
    return !selected.empty() && std::ranges::all_of(selected, [](const FileEntry* entry) {
        return entry->type != EntryType::Unsupported;
    });
}

bool ExplorerController::selected_can_move_to_trash() const {
    const auto selected = selected_entries();
    return !selected.empty() && std::ranges::all_of(selected, [this](const FileEntry* entry) {
        return entry->type != EntryType::Unsupported && entry->path != filesystem_->trash() &&
            !filesystem_->is_in_trash(entry->path);
    });
}

bool ExplorerController::selected_can_restore() const {
    const auto selected = selected_entries();
    return !selected.empty() && std::ranges::all_of(selected, [this](const FileEntry* entry) {
        return entry->type != EntryType::Unsupported && filesystem_->is_in_trash(entry->path);
    });
}

bool ExplorerController::selected_can_delete_permanently() const {
    const auto selected = selected_entries();
    return !selected.empty() && std::ranges::all_of(selected, [this](const FileEntry* entry) {
        return entry->type != EntryType::Unsupported && filesystem_->is_in_trash(entry->path);
    });
}

bool ExplorerController::selected_can_rename() const {
    if (selected_paths_.size() != 1) return false;
    const auto* entry = selected();
    return entry && entry->type != EntryType::Unsupported && entry->path != filesystem_->trash() &&
        !filesystem_->is_in_trash(entry->path);
}

OperationResult ExplorerController::rename_selected(const std::string& new_name) {
    if (!selected_can_rename()) return fail("名前を変更する項目を1つ選択してください");
    const auto source = selected()->path;
    try {
        const auto renamed = filesystem_->rename_entry(source, std::filesystem::u8path(new_name));
        clear_selection();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed("名前を変更しました: " + path_text(renamed.filename()));
    } catch (const std::exception& error) {
        return fail(std::string("名前を変更できません: ") + error.what());
    }
}

OperationResult ExplorerController::create_entry(const std::string& name, const bool folder) {
    if (current_is_in_trash_tree()) return fail("trash内には作成できません");
    try {
        const auto created = filesystem_->create_entry(
            current_directory_, std::filesystem::u8path(name), folder);
        clear_selection();
        const auto result = refresh();
        if (!result.ok) return result;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (entries_[index].path == created) {
                (void)select(index);
                break;
            }
        }
        return succeed((folder ? "フォルダを作成しました: " : "ファイルを作成しました: ") +
                       path_text(created.filename()));
    } catch (const std::exception& error) {
        return fail(std::string("作成できません: ") + error.what());
    }
}

bool ExplorerController::current_is_in_trash() const {
    return current_directory_ == filesystem_->trash();
}

bool ExplorerController::current_is_in_trash_tree() const {
    return filesystem_->is_in_trash(current_directory_);
}

std::string ExplorerController::flatten_candidate_name() const {
    return flatten_candidate_ ? path_text(flatten_candidate_->filename()) : std::string{};
}

OperationResult ExplorerController::fail(std::string message) {
    status_ = message;
    return {false, std::move(message)};
}

OperationResult ExplorerController::succeed(std::string message) {
    status_ = message;
    return {true, std::move(message)};
}

OperationResult ExplorerController::refresh() {
    invalidate_read();
    try {
        entries_ = filesystem_->list(current_directory_);
        sort_entries();
        prune_selection();
        return succeed(std::to_string(entries_.size()) + "項目");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::select(std::size_t index, bool extend_range) {
    if (index >= entries_.size()) return fail("選択項目が範囲外です");
    invalidate_read();
    const auto& target = entries_[index];
    if (!extend_range || !selection_anchor_) {
        selected_paths_.clear();
        selected_paths_.insert(target.path);
        selection_anchor_ = target.path;
    } else {
        const auto anchor = std::find_if(entries_.begin(), entries_.end(), [this](const FileEntry& entry) {
            return entry.path == *selection_anchor_;
        });
        if (anchor == entries_.end()) {
            selected_paths_.clear();
            selected_paths_.insert(target.path);
            selection_anchor_ = target.path;
        } else {
            const auto anchor_index = static_cast<std::size_t>(std::distance(entries_.begin(), anchor));
            const auto first = std::min(anchor_index, index);
            const auto last = std::max(anchor_index, index);
            selected_paths_.clear();
            for (std::size_t current = first; current <= last; ++current)
                selected_paths_.insert(entries_[current].path);
        }
    }
    active_path_ = target.path;
    return succeed(selected_paths_.size() == 1 ? target.name :
        std::to_string(selected_paths_.size()) + "項目を選択");
}

OperationResult ExplorerController::navigate_to(const std::filesystem::path& directory) {
    invalidate_read();
    try {
        auto target = directory.lexically_normal();
        if (target == ".") target.clear();
        auto entries = filesystem_->list(target);
        previews_.stop_audio();
        previews_.stop_video();
        preview_.reset();
        if (target != current_directory_) history_.push_back(current_directory_);
        current_directory_ = std::move(target);
        entries_ = std::move(entries);
        sort_entries();
        clear_selection();
        return succeed(std::to_string(entries_.size()) + "項目");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::open_selected() {
    invalidate_read();
    if (selected_paths_.size() > 1) return fail("開く項目は1つだけ選択してください");
    const auto* entry = selected();
    if (!entry) return fail("項目を選択してください");
    try {
        if (entry->type == EntryType::Directory) {
            history_.push_back(current_directory_);
            current_directory_ = entry->path;
            preview_.reset();
            clear_selection();
            return refresh();
        }
        if (entry->type != EntryType::File) return fail("symlink等の特殊項目は開けません");
        previews_.stop_audio();
        previews_.stop_video();
        preview_ = previews_.load(filesystem_->absolute(entry->path));
        if (preview_->kind == PreviewKind::Audio)
            previews_.play_audio(preview_->path);
        else if (preview_->kind == PreviewKind::Video)
            previews_.play_video(preview_->path);
        return succeed("プレビュー: " + entry->name);
    } catch (const std::exception& error) {
        preview_.reset();
        return fail(error.what());
    }
}

OperationResult ExplorerController::go_back() {
    if (history_.empty()) return fail("Explorer内の戻り先がありません");
    previews_.stop_audio();
    previews_.stop_video();
    preview_.reset();
    current_directory_ = history_.back();
    history_.pop_back();
    clear_selection();
    return refresh();
}

OperationResult ExplorerController::go_up() {
    if (current_directory_.empty()) return fail("これ以上上へ移動できません");
    previews_.stop_audio();
    previews_.stop_video();
    preview_.reset();
    history_.push_back(current_directory_);
    current_directory_ = current_directory_.parent_path();
    if (current_directory_ == ".") current_directory_.clear();
    clear_selection();
    return refresh();
}

void ExplorerController::invalidate_read() noexcept {
    ++read_generation_;
    opened_text_.reset();
    if (read_task_) read_task_->cancel();
    // 要求が新しいものに置き換わっても、ExplorerIoServiceがワーカーを保持してjoinする。
    read_task_.reset();
}

std::shared_ptr<ExplorerTask> ExplorerController::read_directory_async(
    std::filesystem::path directory, ReadKind kind, ExplorerCompletion completion) {
    invalidate_read();
    const auto generation = read_generation_;
    directory = directory.lexically_normal();
    if (directory == ".") directory.clear();
    auto rows = std::make_shared<std::vector<FileEntry>>();
    status_ = "フォルダを読み込み中…";
    read_task_ = io_service_.submit(
        [filesystem = filesystem_, directory, rows](ExplorerCancellationToken token) {
            if (token.stop_requested()) return OperationResult{false, "読み込みを中止しました"};
            *rows = filesystem->list(directory);
            return OperationResult{true, std::to_string(rows->size()) + "項目"};
        },
        [this, generation, directory, kind, rows, completion = std::move(completion)](
            OperationResult result) mutable {
            if (generation != read_generation_) return;
            read_task_.reset();
            if (result.ok) {
                ++navigation_revision_;
                if (kind != ReadKind::Refresh) {
                    previews_.stop_audio();
                    previews_.stop_video();
                    preview_.reset();
                    if (kind == ReadKind::Back) history_.pop_back();
                    else if (directory != current_directory_) history_.push_back(current_directory_);
                    current_directory_ = directory;
                    clear_selection();
                }
                entries_ = std::move(*rows);
                sort_entries();
                prune_selection();
            }
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
    return read_task_;
}

std::shared_ptr<ExplorerTask> ExplorerController::navigate_to_async(
    std::filesystem::path directory, ExplorerCompletion completion) {
    return read_directory_async(std::move(directory), ReadKind::Navigate, std::move(completion));
}
std::shared_ptr<ExplorerTask> ExplorerController::refresh_async(ExplorerCompletion completion) {
    return read_directory_async(current_directory_, ReadKind::Refresh, std::move(completion));
}
std::shared_ptr<ExplorerTask> ExplorerController::go_back_async(ExplorerCompletion completion) {
    if (history_.empty()) {
        const auto result = fail("Explorer内の戻り先がありません");
        if (completion) completion(result);
        return {};
    }
    return read_directory_async(history_.back(), ReadKind::Back, std::move(completion));
}
std::shared_ptr<ExplorerTask> ExplorerController::go_up_async(ExplorerCompletion completion) {
    if (current_directory_.empty()) {
        const auto result = fail("これ以上上へ移動できません");
        if (completion) completion(result);
        return {};
    }
    return navigate_to_async(current_directory_.parent_path(), std::move(completion));
}

std::shared_ptr<ExplorerTask> ExplorerController::open_selected_async(
    bool edit_text, ExplorerCompletion completion) {
    const auto* entry = selected();
    if (selected_paths_.size() != 1 || !entry || entry->type == EntryType::Unsupported) {
        const auto result = fail("開く通常ファイルまたはフォルダを1つ選択してください");
        if (completion) completion(result);
        return {};
    }
    if (entry->type == EntryType::Directory)
        return navigate_to_async(entry->path, std::move(completion));
    const auto path = entry->path;
    const auto name = entry->name;
    invalidate_read();
    const auto generation = read_generation_;
    struct Decoded {
        std::optional<PreviewData> preview;
        std::optional<TextDocument> document;
    };
    auto decoded = std::make_shared<Decoded>();
    auto decoder = previews_.make_decoder();
    const bool try_text = edit_text && TextDocumentService::has_editable_extension(path);
    status_ = "読み込み中: " + name;
    read_task_ = io_service_.submit(
        [filesystem = filesystem_, path, name, decoded, decoder, try_text](ExplorerCancellationToken token) {
            const auto absolute = filesystem->absolute(path);
            if (token.stop_requested()) return OperationResult{false, "読み込みを中止しました"};
            if (try_text) {
                try { decoded->document = TextDocumentService::load(absolute, path); }
                catch (const std::exception&) { /* 同期的なテキスト判別と同じ代替処理。 */ }
            }
            if (!decoded->document && !token.stop_requested()) decoded->preview = decoder->load(absolute);
            return OperationResult{true, (decoded->document ? "テキストを読み込みました: " : "プレビュー: ") + name};
        },
        [this, generation, decoded, completion = std::move(completion)](
            OperationResult result) mutable {
            if (generation != read_generation_) return;
            read_task_.reset();
            if (result.ok) {
                if (decoded->document) {
                    status_ = result.message;
                    opened_text_ = std::move(decoded->document);
                } else {
                    try {
                        previews_.stop_audio();
                        previews_.stop_video();
                        preview_ = std::move(decoded->preview);
                        if (preview_ && preview_->kind == PreviewKind::Audio) previews_.play_audio(preview_->path);
                        else if (preview_ && preview_->kind == PreviewKind::Video) previews_.play_video(preview_->path);
                    } catch (const std::exception& error) {
                        preview_.reset();
                        result = {false, error.what()};
                    }
                }
            }
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
    return read_task_;
}

OperationResult ExplorerController::begin_copy() {
    if (paste_busy_) return fail("貼り付け処理中です");
    const auto selected = selected_entries();
    if (selected.empty()) return fail("コピーする項目を選択してください");
    if (std::ranges::any_of(selected, [](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported;
        })) return fail("特殊項目はコピーできません");
    copy_sources_.clear();
    move_mode_ = false;
    for (const auto* entry : selected) copy_sources_.push_back(entry->path);
    return succeed(std::to_string(copy_sources_.size()) +
        "項目をコピー待機中です。コピー先で「貼り付け」を押してください");
}

OperationResult ExplorerController::begin_move() {
    if (paste_busy_) return fail("貼り付け処理中です");
    const auto selected = selected_entries();
    if (selected.empty()) return fail("移動する項目を選択してください");
    if (std::ranges::any_of(selected, [this](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported ||
                entry->path == filesystem_->trash() ||
                filesystem_->is_in_trash(entry->path);
        })) return fail("この項目は移動できません");
    copy_sources_.clear();
    for (const auto* entry : selected) copy_sources_.push_back(entry->path);
    move_mode_ = true;
    return succeed(std::to_string(copy_sources_.size()) +
        "項目を移動待機中です。移動先で「貼り付け」を押してください");
}

namespace {
OperationResult perform_paste(const std::shared_ptr<FileSystem>& filesystem,
    const std::vector<std::filesystem::path>& sources,
    const std::filesystem::path& destination, bool move, std::size_t& completed,
    ExplorerCancellationToken cancellation = {}) {
    const std::string action = move ? "移動" : "コピー";
    try {
        for (const auto& source : sources) {
            if (cancellation.stop_requested()) throw std::runtime_error("操作をキャンセルしました");
            if (move) (void)filesystem->move_to(source, destination);
            else (void)filesystem->copy_to(source, destination);
            ++completed;
        }
        filesystem->sync();
        return {true, std::to_string(completed) + "項目を" + action + "しました"};
    } catch (const std::exception& error) {
        return {false, std::to_string(completed) + "項目を" + action +
            "後に失敗しました（残り" + std::to_string(sources.size() - completed) +
            "項目）: " + error.what()};
    }
}
}

OperationResult ExplorerController::finish_paste(std::size_t completed, OperationResult result) {
    copy_sources_.erase(copy_sources_.begin(), copy_sources_.begin() +
        static_cast<std::ptrdiff_t>(std::min(completed, copy_sources_.size())));
    if (copy_sources_.empty()) move_mode_ = false;
    clear_selection();
    const auto refreshed = refresh();
    if (result.ok && !refreshed.ok) result = refreshed;
    status_ = result.message;
    return result;
}

OperationResult ExplorerController::paste_copy() {
    if (paste_busy_) return fail("貼り付け処理中です");
    if (copy_sources_.empty()) return fail("コピー元が選択されていません");
    std::size_t completed = 0;
    auto result = perform_paste(filesystem_, copy_sources_, current_directory_, move_mode_, completed);
    return finish_paste(completed, std::move(result));
}

std::shared_ptr<ExplorerTask> ExplorerController::paste_copy_async(ExplorerCompletion completion) {
    if (paste_busy_ || copy_sources_.empty()) {
        auto result = fail(paste_busy_ ? "貼り付け処理中です" : "コピー元が選択されていません");
        if (completion) completion(std::move(result));
        return {};
    }
    // 従来の同期的な貼り付けと異なり、I/Oは既存のワーカーで行う。
    // クリップボードと表示の更新は、完了時にUIスレッドで行う。
    const auto filesystem = filesystem_;
    const auto sources = copy_sources_;
    const auto destination = current_directory_;
    const bool move = move_mode_;
    const auto completed = std::make_shared<std::size_t>(0);
    paste_busy_ = true;
    try {
        return io_service_.submit(
            [filesystem, sources, destination, move, completed](ExplorerCancellationToken token) {
                return perform_paste(filesystem, sources, destination, move, *completed, token);
            },
            [this, completed, completion = std::move(completion)](OperationResult result) mutable {
                paste_busy_ = false;
                result = finish_paste(*completed, std::move(result));
                if (completion) completion(std::move(result));
            });
    } catch (...) { paste_busy_ = false; throw; }
}

OperationResult ExplorerController::import_external(
    const std::vector<std::filesystem::path>& sources, bool overwrite) {
    if (sources.empty()) return fail("アップロードする項目が選択されていません");
    std::size_t copied_count = 0;
    try {
        validate_upload_names(filesystem_,current_directory_,sources,overwrite);
        for (const auto& source : sources) {
            (void)filesystem_->copy_from_external(source, current_directory_, overwrite);
            ++copied_count;
        }
        filesystem_->sync();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed(std::to_string(copied_count) + "項目をアップロードしました");
    } catch (const std::exception& error) {
        (void)refresh();
        return fail(std::to_string(copied_count) + "項目をアップロード後に失敗しました: " +
                    error.what());
    }
}

std::shared_ptr<ExplorerTask> ExplorerController::import_external_async(
    std::vector<std::filesystem::path> sources,
    ExplorerCompletion completion, bool overwrite) {
    const auto filesystem = filesystem_;
    const auto destination = current_directory_;
    return io_service_.submit(
        [filesystem, destination, sources = std::move(sources), overwrite](
            ExplorerCancellationToken cancellation) -> OperationResult {
            if (sources.empty()) return {false, "アップロードする項目が選択されていません"};
            std::size_t copied_count = 0;
            try {
                validate_upload_names(filesystem,destination,sources,overwrite);
                for (const auto& source : sources) {
                    if (cancellation.stop_requested())
                        return {false, "アップロードをキャンセルしました"};
                    (void)filesystem->copy_from_external(source, destination, overwrite);
                    ++copied_count;
                }
                filesystem->sync();
                return {true, std::to_string(copied_count) + "項目をアップロードしました"};
            } catch (const std::exception& error) {
                return {false, std::to_string(copied_count) +
                    "項目をアップロード後に失敗しました: " + error.what()};
            }
        },
        [this, completion = std::move(completion)](OperationResult result) mutable {
            // 大量のコピーはExplorerIoService
            // のワーカーで行い、モデルの変更は所
            // 有元のSDLスレッドに限定する。
            const auto refreshed = refresh();
            if (result.ok && !refreshed.ok) result = refreshed;
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
}

OperationResult ExplorerController::export_selected(
    const std::filesystem::path& destination_directory) {
    const auto selected = selected_entries();
    if (selected.empty()) return fail("ダウンロードする項目を選択してください");
    if (std::ranges::any_of(selected, [](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported;
        })) return fail("特殊項目はダウンロードできません");
    std::size_t copied_count = 0;
    try {
        for (const auto* entry : selected) {
            (void)filesystem_->copy_to_external(entry->path, destination_directory);
            ++copied_count;
        }
        return succeed(std::to_string(copied_count) + "項目をダウンロードしました");
    } catch (const std::exception& error) {
        return fail(std::to_string(copied_count) + "項目をダウンロード後に失敗しました: " +
                    error.what());
    }
}

OperationResult ExplorerController::export_selected_as(
    const std::filesystem::path& destination) {
    if (selected_paths_.size() > 1)
        return fail("名前を指定するダウンロードは1項目ずつ実行してください");
    const auto* entry = selected();
    if (!entry) return fail("ダウンロードする項目を選択してください");
    if (entry->type == EntryType::Unsupported)
        return fail("特殊項目はダウンロードできません");
    try {
        const auto output = filesystem_->copy_to_external_as(entry->path, destination);
        return succeed("ダウンロードしました: " + path_text(output.filename()));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::export_selected_as_zip(
    const std::filesystem::path& destination) {
    const auto selected = selected_entries();
    if (selected.empty()) return fail("ZIPにする項目を選択してください");
    if (std::ranges::any_of(selected, [](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported;
        })) return fail("特殊項目はZIPにできません");
    if (!destination.is_absolute() || destination.filename().empty())
        return fail("ZIPの保存先が不正です");
    std::error_code error;
    const auto parent = destination.parent_path();
    if (!std::filesystem::is_directory(parent, error) || error)
        return fail("ZIPの保存先フォルダがありません");
    const auto temporary = parent / (".cw-download-tmp-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        ".zip");
    try {
        auto writer = cw::lib::archive::Writer::create(temporary);
        for (const auto* entry : selected)
            writer.add_path(filesystem_->absolute(entry->path),
                            path_text(entry->path.filename()),
                            cw::lib::archive::Compression::Stored);
        writer.close();
        const bool destination_exists = std::filesystem::exists(destination, error);
        if (error) throw std::runtime_error("既存ZIPを確認できません: " + error.message());
        if (destination_exists) {
            if (!std::filesystem::is_regular_file(destination, error) || error)
                throw std::runtime_error("既存ZIPを置換できません");
        }
        if (!replace_file_atomically(temporary, destination, error))
            throw std::runtime_error("ZIPダウンロードを確定できません: " + error.message());
        return succeed(std::to_string(selected.size()) +
            "項目をZIPでダウンロードしました: " + path_text(destination.filename()));
    } catch (const std::exception& exception) {
        std::filesystem::remove(temporary, error);
        return fail(exception.what());
    }
}

std::shared_ptr<ExplorerTask> ExplorerController::export_selected_async(
    std::filesystem::path destination_directory, ExplorerCompletion completion) {
    const auto filesystem = filesystem_;
    std::vector<std::filesystem::path> selected;
    for (const auto* entry : selected_entries()) {
        if (entry->type == EntryType::Unsupported) {
            if (completion) completion({false, "特殊項目はダウンロードできません"});
            return {};
        }
        selected.push_back(entry->path);
    }
    if (selected.empty()) {
        if (completion) completion({false, "ダウンロードする項目を選択してください"});
        return {};
    }
    return io_service_.submit(
        [filesystem, selected = std::move(selected),
         destination_directory = std::move(destination_directory)](
            ExplorerCancellationToken cancellation) -> OperationResult {
            std::size_t copied = 0;
            try {
                for (const auto& path : selected) {
                    if (cancellation.stop_requested())
                        return {false, "ダウンロードをキャンセルしました"};
                    (void)filesystem->copy_to_external(path, destination_directory);
                    ++copied;
                }
                return {true, std::to_string(copied) + "項目をダウンロードしました"};
            } catch (const std::exception& error) {
                return {false, std::to_string(copied) +
                    "項目をダウンロード後に失敗しました: " + error.what()};
            }
        },
        [this, completion = std::move(completion)](OperationResult result) mutable {
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
}

std::shared_ptr<ExplorerTask> ExplorerController::export_selected_as_async(
    std::filesystem::path destination, ExplorerCompletion completion) {
    const auto filesystem = filesystem_;
    const auto* entry = selected();
    if (!entry || selected_paths_.size() > 1 || entry->type == EntryType::Unsupported) {
        if (completion) completion({false, !entry ? "ダウンロードする項目を選択してください" :
            selected_paths_.size() > 1 ? "名前を指定するダウンロードは1項目ずつ実行してください" :
            "特殊項目はダウンロードできません"});
        return {};
    }
    const auto source = entry->path;
    return io_service_.submit(
        [filesystem, source, destination = std::move(destination)](
            ExplorerCancellationToken cancellation) -> OperationResult {
            if (cancellation.stop_requested()) return {false, "ダウンロードをキャンセルしました"};
            try {
                const auto output = filesystem->copy_to_external_as(source, destination);
                return {true, "ダウンロードしました: " + path_text(output.filename())};
            } catch (const std::exception& error) {
                return {false, error.what()};
            }
        },
        [this, completion = std::move(completion)](OperationResult result) mutable {
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
}

std::shared_ptr<ExplorerTask> ExplorerController::export_selected_as_zip_async(
    std::filesystem::path destination, ExplorerCompletion completion) {
    const auto filesystem = filesystem_;
    std::vector<std::filesystem::path> selected;
    for (const auto* entry : selected_entries()) {
        if (entry->type == EntryType::Unsupported) {
            if (completion) completion({false, "特殊項目はZIPにできません"});
            return {};
        }
        selected.push_back(entry->path);
    }
    if (selected.empty()) {
        if (completion) completion({false, "ZIPにする項目を選択してください"});
        return {};
    }
    return io_service_.submit(
        [filesystem, selected = std::move(selected), destination = std::move(destination)](
            ExplorerCancellationToken cancellation) -> OperationResult {
            if (!destination.is_absolute() || destination.filename().empty())
                return {false, "ZIPの保存先が不正です"};
            std::error_code error;
            const auto parent = destination.parent_path();
            if (!std::filesystem::is_directory(parent, error) || error)
                return {false, "ZIPの保存先フォルダがありません"};
            const auto temporary = parent / (".cw-download-tmp-" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
                ".zip");
            try {
                auto writer = cw::lib::archive::Writer::create(temporary);
                for (const auto& path : selected) {
                    if (cancellation.stop_requested())
                        throw std::runtime_error("ダウンロードをキャンセルしました");
                    writer.add_path(filesystem->absolute(path), path_text(path.filename()),
                                    cw::lib::archive::Compression::Stored);
                }
                writer.close();
                const bool destination_exists = std::filesystem::exists(destination, error);
                if (error)
                    throw std::runtime_error("既存ZIPを確認できません: " + error.message());
                if (destination_exists) {
                    if (!std::filesystem::is_regular_file(destination, error) || error)
                        throw std::runtime_error("既存ZIPを置換できません");
                }
                if (!replace_file_atomically(temporary, destination, error))
                    throw std::runtime_error("ZIPダウンロードを確定できません: " +
                                             error.message());
                return {true, std::to_string(selected.size()) +
                    "項目をZIPでダウンロードしました: " + path_text(destination.filename())};
            } catch (const std::exception& exception) {
                std::filesystem::remove(temporary, error);
                return {false, exception.what()};
            }
        },
        [this, completion = std::move(completion)](OperationResult result) mutable {
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
}

void ExplorerController::cancel_copy() {
    if (paste_busy_) return;
    copy_sources_.clear();
    move_mode_ = false;
    status_ = "コピーをキャンセルしました";
}

OperationResult ExplorerController::delete_selected() {
    const auto selected = selected_entries();
    if (selected.empty()) return fail("削除する項目を選択してください");
    if (std::ranges::any_of(selected, [this](const FileEntry* entry) {
            return filesystem_->is_in_trash(entry->path);
        })) return fail("trash内の項目は個別に完全削除できません");
    std::vector<std::filesystem::path> paths;
    paths.reserve(selected.size());
    for (const auto* entry : selected) paths.push_back(entry->path);
    std::size_t moved_count = 0;
    try {
        for (const auto& path : paths) {
            (void)filesystem_->move_to_trash(path);
            ++moved_count;
        }
        clear_selection();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed(std::to_string(moved_count) + "項目をtrashへ移動しました");
    } catch (const std::exception& error) {
        (void)refresh();
        return fail(std::to_string(moved_count) + "項目をtrashへ移動後に失敗しました: " +
                    error.what());
    }
}

OperationResult ExplorerController::restore_selected_from_trash() {
    const auto selected = selected_entries();
    if (selected.empty()) return fail("復元する項目を選択してください");
    if (std::ranges::any_of(selected, [this](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported ||
                !filesystem_->is_in_trash(entry->path);
        })) return fail("trash内の通常項目だけを復元できます");
    std::vector<std::filesystem::path> paths;
    paths.reserve(selected.size());
    for (const auto* entry : selected) paths.push_back(entry->path);
    std::size_t restored_count = 0;
    try {
        for (const auto& path : paths) {
            (void)filesystem_->restore_from_trash(path);
            ++restored_count;
        }
        clear_selection();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed(std::to_string(restored_count) + "項目を復元しました");
    } catch (const std::exception& error) {
        (void)refresh();
        return fail(std::to_string(restored_count) + "項目を復元後に失敗しました: " +
                    error.what());
    }
}

OperationResult ExplorerController::delete_selected_permanently() {
    const auto selected = selected_entries();
    if (selected.empty()) return fail("完全削除する項目を選択してください");
    if (std::ranges::any_of(selected, [this](const FileEntry* entry) {
            return entry->type == EntryType::Unsupported ||
                !filesystem_->is_in_trash(entry->path);
        })) return fail("trash内の通常項目だけを完全削除できます");
    std::vector<std::filesystem::path> paths;
    paths.reserve(selected.size());
    for (const auto* entry : selected) paths.push_back(entry->path);
    std::size_t removed_count = 0;
    try {
        for (const auto& path : paths) {
            filesystem_->delete_permanently(path);
            ++removed_count;
        }
        clear_selection();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed(std::to_string(removed_count) + "項目を完全削除しました");
    } catch (const std::exception& error) {
        (void)refresh();
        return fail(std::to_string(removed_count) + "項目を完全削除後に失敗しました: " +
                    error.what());
    }
}

OperationResult ExplorerController::empty_trash() {
    if (!current_is_in_trash()) return fail("trashを開いてから実行してください");
    try {
        previews_.stop_audio();
        previews_.stop_video();
        preview_.reset();
        const auto removed = filesystem_->empty_trash();
        current_directory_ = filesystem_->trash();
        history_.clear();
        clear_selection();
        entries_ = filesystem_->list(current_directory_);
        sort_entries();
        return succeed(std::to_string(removed) + "項目を完全削除しました");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::extract_selected() {
    if (selected_paths_.size() > 1) return fail("展開する項目は1つだけ選択してください");
    const auto* entry = selected();
    if (!entry) return fail("展開するアーカイブを選択してください");
    if (!selected_is_archive()) return fail("対応アーカイブではありません");
    flatten_candidate_.reset();
    try {
        const auto destination = filesystem_->extract_archive(
            archives_, entry->path, current_directory_);
        filesystem_->sync();
        const auto extracted = filesystem_->list(destination);
        if (extracted.size() == 1 && extracted.front().type == EntryType::Directory &&
            extracted.front().name == path_text(destination.filename())) {
            flatten_candidate_ = destination;
        }
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed("展開しました: " + path_text(destination.filename()));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

std::shared_ptr<ExplorerTask> ExplorerController::extract_selected_async(
    ExplorerCompletion completion) {
    if (selected_paths_.size() > 1) {
        if (completion) completion({false, "展開する項目は1つだけ選択してください"});
        return {};
    }
    const auto* entry = selected();
    if (!entry) {
        if (completion) completion({false, "展開するアーカイブを選択してください"});
        return {};
    }
    if (!selected_is_archive()) {
        if (completion) completion({false, "対応アーカイブではありません"});
        return {};
    }

    struct ExtractionData {
        std::filesystem::path destination;
        bool flatten{};
    };
    const auto filesystem = filesystem_;
    const auto archives = archives_;
    const auto archive = entry->path;
    const auto destination_directory = current_directory_;
    const auto extracted = std::make_shared<ExtractionData>();
    flatten_candidate_.reset();
    return io_service_.submit(
        [filesystem, archives, archive, destination_directory, extracted](
            ExplorerCancellationToken cancellation) -> OperationResult {
            if (cancellation.stop_requested()) return {false, "展開をキャンセルしました"};
            try {
                const auto destination = filesystem->extract_archive(
                    archives, archive, destination_directory);
                filesystem->sync();
                if (cancellation.stop_requested()) return {false, "展開をキャンセルしました"};
                const auto children = filesystem->list(destination);
                if (cancellation.stop_requested()) return {false, "展開をキャンセルしました"};
                extracted->destination = destination;
                extracted->flatten = children.size() == 1U &&
                    children.front().type == EntryType::Directory &&
                    children.front().name == path_text(destination.filename());
                return {true, "展開しました: " + path_text(destination.filename())};
            } catch (const std::exception& error) {
                return {false, error.what()};
            }
        },
        [this, extracted, completion = std::move(completion)](
            OperationResult result) mutable {
            // モデルの更新はExplorerのUIスレッドで行う。アーカイブ処理の失敗時も
            // 部分的な展開先が残る場合があるため、成功・失敗のどちらでも表示を更新する。
            if (result.ok && extracted->flatten)
                flatten_candidate_ = extracted->destination;
            const auto refreshed = refresh();
            if (result.ok && !refreshed.ok) result = refreshed;
            status_ = result.message;
            if (completion) completion(std::move(result));
        });
}

OperationResult ExplorerController::flatten_extracted_directory() {
    if (!flatten_candidate_) return fail("まとめる展開先がありません");
    const auto candidate = *flatten_candidate_;
    try {
        (void)filesystem_->flatten_same_named_child(candidate);
        flatten_candidate_.reset();
        const auto result = refresh();
        if (!result.ok) return result;
        return succeed("二重フォルダをまとめました: " + path_text(candidate.filename()));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::load_selected_text(TextDocument& document) {
    if (selected_paths_.size() > 1) return fail("編集するテキストは1つだけ選択してください");
    const auto* entry = selected();
    if (!entry) return fail("編集するテキストを選択してください");
    if (entry->type != EntryType::File) return fail("この項目はテキスト編集できません");
    try {
        document = TextDocumentService::load(filesystem_->absolute(entry->path), entry->path);
        return succeed("テキストを読み込みました: " + entry->name);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult ExplorerController::save_text_document(const TextDocument& document) {
    try {
        TextDocumentService::save(filesystem_->absolute(document.relative_path), document);
        return succeed("保存しました: " + document.title);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

void ExplorerController::close_preview() {
    invalidate_read();
    previews_.stop_audio();
    previews_.stop_video();
    preview_.reset();
    status_ = "プレビューを閉じました";
}

}  // namespace cardwirth::explorer
