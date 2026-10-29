#include "file_watch.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cwxeditor::lib {
struct FileWatchDelivery::State {
    struct Callbacks {
        std::function<void(const std::vector<FileChange>&)> changed;
        std::function<void(std::string)> error;
    };
    std::weak_ptr<MainDispatcher> dispatcher;
    std::mutex mutex;
    std::shared_ptr<const Callbacks> callbacks;
    bool active() {
        std::scoped_lock lock(mutex);
        return bool(callbacks); // ワーカーは UI のクロージャーの所有権を取得しない。
    }
    std::shared_ptr<const Callbacks> acquire() {
        std::scoped_lock lock(mutex);
        return callbacks; // ロック中に、利用側が定義した呼び出し可能オブジェクトを複製しない。
    }
};

FileWatchDelivery::FileWatchDelivery(const WatchRequest& request,
                                   std::shared_ptr<MainDispatcher> dispatcher)
    : state_(std::make_shared<State>()) {
    if (!dispatcher) throw std::invalid_argument("file watch requires a UI dispatcher");
    state_->dispatcher = dispatcher;
    state_->callbacks = std::make_shared<State::Callbacks>(
        State::Callbacks{request.on_changed, request.on_error});
}
FileWatchDelivery::~FileWatchDelivery() { stop(); }

void FileWatchDelivery::stop() noexcept {
    std::shared_ptr<const State::Callbacks> released;
    {
        std::scoped_lock lock(state_->mutex);
        released = std::move(state_->callbacks);
    }
    // コールバック先のデストラクターは再入し得るため、ロック解除後に解放する。
}

bool FileWatchDelivery::publish(std::vector<FileChange> changes) const noexcept {
    try {
        const auto state = state_;
        const auto dispatcher = state->dispatcher.lock();
        if (!dispatcher || !state->active()) return false;
        if (changes.empty()) return true;
        dispatcher->post([state, changes = std::move(changes)] {
            const auto dispatcher = state->dispatcher.lock();
            if (!dispatcher || !dispatcher->is_main_thread()) return;
            const auto callbacks = state->acquire();
            if (!callbacks) return;
            try { if (callbacks->changed) callbacks->changed(changes); }
            catch (...) {
                const auto current = state->acquire();
                try { if (current && current->error) current->error("directory watch callback threw an exception"); }
                catch (...) {}
            }
        });
        return true;
    } catch (...) { return false; }
}

bool FileWatchDelivery::error(std::string message) const noexcept {
    try {
        const auto state = state_;
        const auto dispatcher = state->dispatcher.lock();
        if (!dispatcher || !state->active()) return false;
        dispatcher->post([state, message = std::move(message)] {
            const auto dispatcher = state->dispatcher.lock();
            if (!dispatcher || !dispatcher->is_main_thread()) return;
            const auto callbacks = state->acquire();
            try { if (callbacks && callbacks->error) callbacks->error(message); } catch (...) {}
        });
        return true;
    } catch (...) { return false; }
}

std::optional<FileWatchSnapshot> scan_file_watch_directory(
    const std::filesystem::path& root, bool recursive, std::stop_token stop) {
    if (stop.stop_requested()) return std::nullopt;
    FileWatchSnapshot result;
    if (!std::filesystem::exists(root)) return result;
    const auto add = [&](const std::filesystem::path& path) {
        const auto status = std::filesystem::symlink_status(path);
        FileWatchEntry entry;
        entry.type = status.type();
        if (std::filesystem::is_symlink(status)) entry.symlink_target = std::filesystem::read_symlink(path);
        else {
            entry.modified = std::filesystem::last_write_time(path);
            if (std::filesystem::is_regular_file(status)) entry.size = std::filesystem::file_size(path);
        }
        result.emplace(path, std::move(entry));
    };
    add(root);
    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (stop.stop_requested()) return std::nullopt;
            add(entry.path());
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (stop.stop_requested()) return std::nullopt;
            add(entry.path());
        }
    }
    if (stop.stop_requested()) return std::nullopt;
    return result;
}

std::vector<FileChange> diff_file_watch_snapshots(
    const FileWatchSnapshot& before, const FileWatchSnapshot& after) {
    std::vector<FileChange> changes;
    for (const auto& [path, value] : before)
        if (!after.contains(path)) changes.push_back({FileChangeKind::Removed, path, {}});
    for (const auto& [path, value] : after) {
        const auto old = before.find(path);
        if (old == before.end()) changes.push_back({FileChangeKind::Created, path, {}});
        else if (old->second != value) changes.push_back({FileChangeKind::Modified, path, {}});
    }
    return changes;
}

namespace {
struct OwnedWatchThread {
    std::shared_ptr<std::atomic_bool> finished = std::make_shared<std::atomic_bool>(false);
    std::jthread thread;
};

// 終了した走査は、不変のファイルシステム入力と無効化した通知状態だけを保持する。
// 管理側が全スレッドを所有して終了待ちし、切り離したワーカーは作らない。 ワー
// カー上でUI・DirectoryWindow のポインターを生かし続けない。
class WatchRetirement final {
public:
    WatchRetirement() : collector_([this] { collect(); }) {}
    ~WatchRetirement() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        collector_.join();
    }
    void retire(std::unique_ptr<OwnedWatchThread> worker) noexcept {
        if (!worker) return;
        try {
            std::scoped_lock lock(mutex_);
            workers_.push_back(std::move(worker));
        } catch (...) {
            // 確保失敗時も、スレッドを放置せず所有権を維持して終了を
            // 待つ。 通常のUI経路では既存の状態を移すだけにする。
            return;
        }
        changed_.notify_all();
    }
private:
    void collect() {
        for (;;) {
            std::unique_ptr<OwnedWatchThread> completed;
            {
                std::unique_lock lock(mutex_);
                changed_.wait_for(lock, std::chrono::milliseconds(20), [&] { return stopping_; });
                if (stopping_ && workers_.empty()) return;
                for (auto it = workers_.begin(); it != workers_.end(); ++it) {
                    if (stopping_ || (*it)->finished->load(std::memory_order_acquire)) {
                        completed = std::move(*it);
                        workers_.erase(it);
                        break;
                    }
                }
            }
            // 回収側では確保を行わず、登録用ロックの外で終了を待つ。
            completed.reset();
        }
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::unique_ptr<OwnedWatchThread>> workers_;
    bool stopping_{};
    std::thread collector_;
};

WatchRetirement& watch_retirement() {
    static WatchRetirement retirement;
    return retirement;
}

class PollingFileWatch final : public FileWatchHandle {
public:
    PollingFileWatch(WatchRequest request, std::shared_ptr<MainDispatcher> dispatcher, FileWatchScan scan)
        : delivery_(std::make_shared<FileWatchDelivery>(request, dispatcher)) {
        if (!dispatcher->is_main_thread()) throw std::invalid_argument("start file watch on the UI thread");
        if (request.directory.empty()) throw std::invalid_argument("watch directory is empty");
        if (!scan) throw std::invalid_argument("watch scanner is empty");
        // ワーカーより先に所有者を構築し、ウィンドウを破棄す
        // るときに新しい管理スレッドを起動する必要をなくす。
        (void)watch_retirement();
        worker_ = std::make_unique<OwnedWatchThread>();
        request.directory = std::filesystem::absolute(request.directory).lexically_normal();
        const auto interval = std::max(request.poll_interval, std::chrono::milliseconds{1});
        worker_->thread = std::jthread([delivery = delivery_, root = std::move(request.directory),
                                finished = worker_->finished,
                                recursive = request.recursive, interval, scan = std::move(scan)](std::stop_token stop) {
            struct Completion {
                std::shared_ptr<std::atomic_bool> state;
                ~Completion() { state->store(true, std::memory_order_release); }
            } completion{finished};
            FileWatchSnapshot previous;
            bool initialized{};
            std::string last_error;
            std::mutex mutex;
            std::condition_variable_any condition;
            while (!stop.stop_requested()) {
                try {
                    auto snapshot = scan(root, recursive, stop);
                    if (!snapshot) return;
                    auto changes = initialized ? diff_file_watch_snapshots(previous, *snapshot)
                        : std::vector<FileChange>{{FileChangeKind::Overflow, root, {}}};
                    // 初回のオーバーフロー通知で、登録と初期状態の取得の間の競合を
                    // 埋める。 通知処理が受け付けなければ、同じ差分を再試行する。
                    if (delivery->publish(std::move(changes))) {
                        previous.swap(*snapshot);
                        initialized = true;
                    }
                    last_error.clear();
                } catch (const std::exception& exception) {
                    try {
                        if (last_error != exception.what()) {
                            if (delivery->error(exception.what())) last_error = exception.what();
                        }
                    } catch (...) {}
                } catch (...) { delivery->error("directory scan failed"); }
                std::unique_lock lock(mutex);
                condition.wait_for(lock, stop, interval, [] { return false; });
            }
        });
    }
    ~PollingFileWatch() override { stop(); }
    void stop() noexcept override {
        delivery_->stop();
        if (!worker_) return;
        worker_->thread.request_stop();
        watch_retirement().retire(std::move(worker_));
    }
private:
    std::shared_ptr<FileWatchDelivery> delivery_;
    std::unique_ptr<OwnedWatchThread> worker_;
};
} // namespace

std::unique_ptr<FileWatchHandle> make_polling_file_watch(
    WatchRequest request, std::shared_ptr<MainDispatcher> dispatcher, FileWatchScan scan) {
    return std::make_unique<PollingFileWatch>(std::move(request), std::move(dispatcher), std::move(scan));
}
} // namespace cwxeditor::lib
