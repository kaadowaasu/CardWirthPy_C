#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"

#include "platform/storage/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cwxeditor::cwx::filesync {
namespace {

using ByteView = lib::ByteView;
using Path = std::filesystem::path;
using Storage = cardwirth::platform::Storage;

[[nodiscard]] Path pathFromUtf8(lib::String text) {
#if defined(__cpp_char8_t)
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return Path(std::u8string(begin, begin + text.size()));
#else
    return Path(std::move(text));
#endif
}

[[nodiscard]] Path appendSuffix(const Path& file, lib::String suffix) {
    const auto encoded = file.generic_u8string();
    lib::String text;
    if (!encoded.empty()) {
        text.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }
    return pathFromUtf8(text + std::move(suffix));
}

[[nodiscard]] bool pathExists(const Path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

[[nodiscard]] std::optional<Path> storageRelativePath(const Path& file,
                                                       const Storage& storage) {
    // 相対パスはすでに論理Storageパス。Webや仮想バックエ
    // ンドでも、ホストの作業ディレクトリを契約に含めず、Scenario/Area.xml
    // のようなパスを受け取れるようにする。
    if (!file.has_root_name() && !file.has_root_directory()) {
        const auto relative = file.lexically_normal();
        if (!relative.empty() && relative != ".") {
            bool has_parent = false;
            for (const auto& component : relative) {
                if (component == "..") {
                    has_parent = true;
                    break;
                }
            }
            if (!has_parent) return relative;
        }
    }

    const auto root = storage.root();
    if (root.empty()) return std::nullopt;

    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(root, error).lexically_normal();
    if (error) return std::nullopt;
    error.clear();
    const auto absolute_file = std::filesystem::absolute(file, error).lexically_normal();
    if (error) return std::nullopt;

    const auto relative = absolute_file.lexically_relative(absolute_root).lexically_normal();
    if (relative.empty() || relative == "." || relative.has_root_name() ||
        relative.has_root_directory()) {
        return std::nullopt;
    }
    for (const auto& component : relative) {
        if (component == "..") return std::nullopt;
    }
    return relative;
}

[[nodiscard]] std::span<const std::byte> storageBytes(ByteView data) noexcept {
    if (data.empty()) return {};
    return {reinterpret_cast<const std::byte*>(data.data()), data.size()};
}

[[nodiscard]] Path temporaryPath(const Path& file) {
    std::size_t index = 0;
    Path temporary;
    do {
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d101(__FILE__, __LINE__);
        ++index;
        temporary = appendSuffix(file, index == 1
                                           ? ".cwxeditor_temp"
                                           : ".cwxeditor_temp(" +
                                                 std::to_string(index) + ")");
    } while (pathExists(temporary));
    return temporary;
}

#ifdef _WIN32

[[nodiscard]] std::system_error windowsError(const char* operation, const Path& path) {
    const auto error = static_cast<int>(::GetLastError());
    return std::system_error(error, std::system_category(),
                             std::string(operation) + " " + path.generic_string());
}

void writeAndFlush(const Path& path, ByteView data) {
    const auto native = path.wstring();
    const HANDLE handle = ::CreateFileW(native.c_str(), GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw windowsError("cannot open", path);
    }

    try {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto remaining = data.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                remaining, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!::WriteFile(handle, data.data() + offset, chunk, &written, nullptr) ||
                written != chunk) {
                throw windowsError("cannot write", path);
            }
            offset += written;
        }
        if (!::FlushFileBuffers(handle)) {
            throw windowsError("cannot flush", path);
        }
        if (!::CloseHandle(handle)) {
            throw windowsError("cannot close", path);
        }
    } catch (...) {
        // WriteFileやFlushFileBuffers
        // が失敗してもCloseHandleを試みる。
        // 元のD実装は成功した各経路でハンドルを閉じ
        // 、キューのワーカーに所有したまま残さない。
        ::CloseHandle(handle);
        throw;
    }
}

void prepareReplacement(const Path& target) {
    const auto native = target.wstring();
    const auto attributes = ::GetFileAttributesW(native.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        // cwx.utils.preRemoveに合わせ、読み取り専用の出
        // 力先を置換可能にする。ただしアトミックな移動の前に削除しない。
        ::SetFileAttributesW(native.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
}

void replaceFile(const Path& temporary, const Path& target) {
    const auto source = temporary.wstring();
    const auto destination = target.wstring();
    // Dは名前変更を一度だけ試す。Windowsの基本処理の境界では、FILE_SHARE_DELETE
    // なしの短時間のハンドルを許容する。両パスが書き込み可能でもMoveFileExW
    // がACCESS_DENIEDを返す場合があるため。再試行中は旧出
    // 力先を維持し、500ms後も拒否される場合はエラーを報告する。
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(500);
    for (;;) {
        if (::MoveFileExW(source.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return;
        }
        const auto error = ::GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) ||
            std::chrono::steady_clock::now() >= deadline) {
            throw std::system_error(static_cast<int>(error),
                std::system_category(), "cannot replace " + target.generic_string());
        }
        ::Sleep(10);
    }
}

#else

void writeAndFlush(const Path& path, ByteView data) {
    const auto native = path.c_str();
    const int descriptor = ::open(native, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "cannot open " + path.generic_string());
    }

    try {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto remaining = data.size() - offset;
            const auto written = ::write(descriptor, data.data() + offset, remaining);
            if (written < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(),
                                        "cannot write " + path.generic_string());
            }
            if (written == 0) {
                throw std::runtime_error("cannot write " + path.generic_string());
            }
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(descriptor) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "cannot flush " + path.generic_string());
        }
        if (::close(descriptor) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "cannot close " + path.generic_string());
        }
    } catch (...) {
        ::close(descriptor);
        throw;
    }
}

void prepareReplacement(const Path&) {}

void replaceFile(const Path& temporary, const Path& target) {
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        throw std::filesystem::filesystem_error("cannot replace", temporary, target, error);
    }
}

#endif

void reportException(const char* operation, const std::exception& error) {
    std::cerr << "cwx.filesync: " << operation << ": " << error.what() << '\n';
}

void reportUnknownException(const char* operation) {
    std::cerr << "cwx.filesync: " << operation << ": unknown exception\n";
}

void removeTemporary(const Path& temporary) noexcept {
    if (temporary.empty()) return;
    std::error_code error;
    std::filesystem::remove(temporary, error);
}

void writeFileAndSyncOrThrow(Path file, ByteView data, Storage* storage) {
    if (storage) {
        if (const auto relative = storageRelativePath(file, *storage)) {
            if (!storage->write_atomic(*relative, storageBytes(data), true)) {
                auto diagnostic = storage->last_error();
                if (diagnostic.empty()) diagnostic = "Storage::write_atomic failed";
                throw std::runtime_error("cannot save through Storage: " + diagnostic);
            }
            return;
        }
    }

    // 名前を付けて保存する場合と、注入されたStorageルー
    // ト外のパスはネイティブパスを維持する。置換前に出力先を削
    // 除しないため、書き込みに失敗しても前のファイルが残る。
    const auto target = std::move(file);
    Path temporary;
    try {
        temporary = temporaryPath(target);
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d105(__FILE__, __LINE__);
        writeAndFlush(temporary, data);
        prepareReplacement(target);
        replaceFile(temporary, target);
        // 置換操作は一時パスを消費する。下記の後処理も残し
        // 、この条件を明確にするとともに、ディレクトリエン
        // トリーを残すプラットフォーム実装にも対応する。
        removeTemporary(temporary);
    } catch (...) {
        // 後処理は可能な範囲で行う。元のI/O例外を
        // 隠さず、ここでは出力先自体にも触れない。
        removeTemporary(temporary);
        throw;
    }
}

}  // namespace

FileSync::FileSync(cardwirth::platform::Storage* storage)
    : storage_(storage) {
    // DのコンストラクターはrunをThreadへ登録するが、開始はしない。ここで
    // 開始すると、呼び出し側のstartより先にキューの書き込みを実行してしまう。
}

void FileSync::start() {
    {
        std::lock_guard lock(mutex_);
        // 別の呼び出し側がjoin_mutex_を持って完了コールバックを
        // 待つ間に、そのコールバックが重複したstartを要求する場合があ
        // る。join用のロックを取る前に、開始済みワーカーを拒否する。
        if (started_) throw std::logic_error("FileSync thread has already been started");
    }
    std::lock_guard join_lock(join_mutex_);
    std::lock_guard lock(mutex_);
    // 二つのロック取得の間に、別の呼び出し側が開始している場合がある。
    if (started_) throw std::logic_error("FileSync thread has already been started");
    started_ = true;
    worker_running_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread([this] { run(); });
        worker_id_ = worker_.get_id();
    } catch (...) {
        started_ = false;
        worker_running_.store(false, std::memory_order_release);
        throw;
    }
}

bool FileSync::isRunning() const noexcept {
    return worker_running_.load(std::memory_order_acquire);
}

FileSync::~FileSync() {
    quit();
    // DのGC所有のワーカーと異なり、C++のスレッドは所有元の破棄前に終了する必要がある。
    joinWorker();
}

void FileSync::quit() {
    {
        std::lock_guard lock(mutex_);
        quit_requested_ = true;
    }
    condition_.notify_all();
}

void FileSync::push(std::filesystem::path file,
                    lib::ByteView data,
                    lib::Callback<void()> after) {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d30(__FILE__, __LINE__);
    bool drain_on_caller = false;
    {
        std::lock_guard lock(mutex_);
        lib::Bytes copied_data;
        if (!data.empty()) copied_data.assign(data.begin(), data.end());
        files_.push_back(
            PendingFile{std::move(file), std::move(copied_data), std::move(after),
                        storage_.load(std::memory_order_acquire)});
        drain_on_caller = quit_requested_;
    }
    condition_.notify_all();

    if (drain_on_caller && !isWorkerThread()) {
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d34(__FILE__, __LINE__);
        joinWorker();
        writeFiles();
    }
}

void FileSync::sync() {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d45(__FILE__, __LINE__);
    (void)syncUntilCancelled({});
}

bool FileSync::syncUntilCancelled(const lib::Callback<bool()>& cancelled) {
    std::exception_ptr failure;
    {
        std::unique_lock lock(mutex_);
        if (!cancelled)
            condition_.wait(lock, [this] { return files_.empty() && !draining_; });
        while (!files_.empty() || draining_) {
            ::cwxeditor::cwx::perf::TraceScope trace_filesync_d46(__FILE__, __LINE__);
            // 取消は待機の間で確認し、キュー内の保存を捨てない。
            lock.unlock();
            const bool stop = cancelled && cancelled();
            lock.lock();
            if (stop) return false;
            condition_.wait_for(lock, std::chrono::milliseconds(20));
        }
        failure = failure_;
    }
    if (failure) std::rethrow_exception(failure);
    return !(cancelled && cancelled());
}

void FileSync::writeFiles() {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d53(__FILE__, __LINE__);
    {
        std::lock_guard lock(mutex_);
        if (draining_) return;
        draining_ = true;
    }

    while (true) {
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d54(__FILE__, __LINE__);
        PendingFile file;
        {
            std::lock_guard lock(mutex_);
            if (files_.empty()) {
                ::cwxeditor::cwx::perf::TraceScope trace_filesync_d59(__FILE__, __LINE__);
                draining_ = false;
                condition_.notify_all();
                return;
            }
            {
                ::cwxeditor::cwx::perf::TraceScope trace_filesync_d57(__FILE__, __LINE__);
                file = std::move(files_.front());
            }
            files_.pop_front();
        }

        try {
            writeFileAndSyncOrThrow(std::move(file.file), file.data, file.storage);
        } catch (...) {
            // 終了やsyncのデッドロックを避けるため、キューの処理
            // は継続する。例外は保持し、sync()で再送出する。
            rememberFailure(std::current_exception());
        }
        try {
            if (file.after) file.after();
        } catch (const std::exception& error) {
            reportException("completion callback", error);
        } catch (...) {
            reportUnknownException("completion callback");
        }

        condition_.notify_all();
    }
}

void FileSync::run() {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d77(__FILE__, __LINE__);
    while (true) {
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d78(__FILE__, __LINE__);
        writeFiles();

        std::unique_lock lock(mutex_);
        if (quit_requested_ && files_.empty() && !draining_) break;
        condition_.wait_for(lock, std::chrono::milliseconds(1), [this] {
            return !files_.empty() || (quit_requested_ && !draining_);
        });
    }
    writeFiles();
    worker_running_.store(false, std::memory_order_release);
}

void FileSync::joinWorker() {
    if (isWorkerThread()) return;
    std::lock_guard lock(join_mutex_);
    if (worker_.joinable()) worker_.join();
}

bool FileSync::isWorkerThread() const noexcept {
    std::lock_guard lock(mutex_);
    return worker_id_ == std::this_thread::get_id();
}

void FileSync::setStorage(cardwirth::platform::Storage* storage) noexcept {
    storage_.store(storage, std::memory_order_release);
}

cardwirth::platform::Storage* FileSync::storage() const noexcept {
    return storage_.load(std::memory_order_acquire);
}

bool FileSync::failed() const noexcept {
    std::lock_guard lock(mutex_);
    return static_cast<bool>(failure_);
}

std::string FileSync::lastError() const {
    std::lock_guard lock(mutex_);
    return failure_message_;
}

void FileSync::clearError() noexcept {
    std::lock_guard lock(mutex_);
    failure_ = {};
    failure_message_.clear();
}

void FileSync::rememberFailure(std::exception_ptr failure) noexcept {
    if (!failure) return;
    try {
        std::lock_guard lock(mutex_);
        if (failure_) return;
        failure_ = failure;
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            failure_message_ = error.what();
        } catch (...) {
            failure_message_ = "unknown asynchronous file write failure";
        }
    } catch (...) {
        // 失敗の報告によってワーカーを終了させてはならない。メ
        // モリ確保とロックに成功すれば、例外自体は保持する。
    }
}

void writeFile(std::filesystem::path file,
               lib::ByteView data,
               lib::Owner<FileSync> fsync,
               lib::Callback<void()> after,
               cardwirth::platform::Storage* storage) {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d87(__FILE__, __LINE__);
    if (fsync) {
        ::cwxeditor::cwx::perf::TraceScope trace_filesync_d88(__FILE__, __LINE__);
        fsync->push(std::move(file), data, std::move(after));
        return;
    }

    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d90(__FILE__, __LINE__);
    try {
        writeFileAndSyncOrThrow(std::move(file), data, storage);
    } catch (...) {
        // Dのscope(exit)は、std.file.writeが例外を送出
        // してもコールバックを呼ぶ。直接書き込みの経路でも、この順序を維持する。
        if (after) after();
        throw;
    }
    if (after) after();
}

bool writeFileAndSync(std::filesystem::path file,
                      lib::ByteView data,
                      cardwirth::platform::Storage* storage) {
    ::cwxeditor::cwx::perf::TraceScope trace_filesync_d97(__FILE__, __LINE__);
    try {
        writeFileAndSyncOrThrow(std::move(file), data, storage);
        return true;
    } catch (const std::exception& error) {
        reportException("durable write", error);
    } catch (...) {
        reportUnknownException("durable write");
    }
    return false;
}

}  // namespace cwxeditor::cwx::filesync
