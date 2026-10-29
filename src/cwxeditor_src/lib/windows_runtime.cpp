#include "windows_runtime.hpp"
#include "windows_runtime_test.hpp"
#include "file_watch.hpp"
#include "storage.hpp"
#include "window_host.hpp"
#include "audio_loop.hpp"
#include "windows_bass_loop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#  include <psapi.h>
#  include <shellapi.h>
#  include <shlobj.h>

#  pragma comment(lib, "advapi32.lib")
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "psapi.lib")
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "winmm.lib")

#endif

namespace cwxeditor::lib::windows_runtime {

bool supports_native_shell_paths() noexcept {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

std::optional<std::uint32_t> process_handle_count() noexcept {
#if defined(_WIN32)
    DWORD value{};
    if (::GetProcessHandleCount(::GetCurrentProcess(), &value))
        return static_cast<std::uint32_t>(value);
#endif
    return {};
}

namespace {

#if defined(_WIN32)

class unique_handle final {
public:
    unique_handle() = default;
    explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}
    ~unique_handle() { reset(); }

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept : handle_(other.release()) {}
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE release() noexcept {
        HANDLE result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_{};
};

std::wstring utf8_to_wide(std::string_view value, std::string_view description) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string(description) + " is too long");
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error(std::string(description) + " is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length) {
        throw std::runtime_error(std::string(description) + " conversion failed");
    }
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
    if (length <= 0) {
        const int fallback_length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                                        static_cast<int>(value.size()), nullptr, 0,
                                                        nullptr, nullptr);
        if (fallback_length <= 0) return {};
        std::string fallback(static_cast<std::size_t>(fallback_length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), fallback.data(),
                            fallback_length, nullptr, nullptr);
        return fallback;
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length, nullptr,
                            nullptr) != length) {
        return {};
    }
    return result;
}

std::string windows_error(std::string_view operation, DWORD code = GetLastError()) {
    LPWSTR message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);
    std::string detail;
    if (length != 0 && message_buffer != nullptr) {
        std::wstring message(message_buffer, message_buffer + length);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
        detail = wide_to_utf8(message);
        LocalFree(message_buffer);
    }
    std::string result(operation);
    result += " failed (Windows error " + std::to_string(code) + ')';
    if (!detail.empty()) result += ": " + detail;
    return result;
}

void assign_error(std::string& error, std::string_view operation, DWORD code = GetLastError()) {
    error = windows_error(operation, code);
}

std::wstring quote_windows_argument(const std::wstring& value) {
    bool needs_quotes = value.empty();
    for (const wchar_t character : value) {
        if (character == L' ' || character == L'\t' || character == L'\n' || character == L'\v' ||
            character == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return value;

    std::wstring result;
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            result.push_back(character);
            backslashes = 0;
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

struct environment_less final {
    bool operator()(const std::wstring& left, const std::wstring& right) const noexcept {
        const std::size_t length = std::min(left.size(), right.size());
        for (std::size_t index = 0; index < length; ++index) {
            const wchar_t left_character = static_cast<wchar_t>(std::towlower(left[index]));
            const wchar_t right_character = static_cast<wchar_t>(std::towlower(right[index]));
            if (left_character != right_character) return left_character < right_character;
        }
        return left.size() < right.size();
    }
};

std::vector<wchar_t> make_environment_block(const ProcessLaunchRequest& request) {
    std::map<std::wstring, std::wstring, environment_less> values;
    if (request.inherit_environment) {
        LPWCH raw = GetEnvironmentStringsW();
        if (raw == nullptr) throw std::runtime_error(windows_error("GetEnvironmentStringsW"));
        for (const wchar_t* entry = raw; *entry != L'\0'; entry += std::wcslen(entry) + 1U) {
            const wchar_t* separator = std::wcschr(entry, L'=');
            if (separator == entry || separator == nullptr) continue;
            values.emplace(std::wstring(entry, separator), std::wstring(separator + 1));
        }
        FreeEnvironmentStringsW(raw);
    }
    for (const auto& [name, value] : request.environment) {
        if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos ||
            value.find('\0') != std::string::npos) {
            throw std::runtime_error("invalid process environment entry");
        }
        values[utf8_to_wide(name, "environment name")] = utf8_to_wide(value, "environment value");
    }

    std::vector<wchar_t> result;
    for (const auto& [name, value] : values) {
        result.insert(result.end(), name.begin(), name.end());
        result.push_back(L'=');
        result.insert(result.end(), value.begin(), value.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

class WindowsFileWatch final : public FileWatchHandle {
public:
    explicit WindowsFileWatch(WatchRequest request,
                              std::shared_ptr<MainDispatcher> dispatcher)
        : request_(std::move(request)), delivery_(request_, std::move(dispatcher)) {
        // UIコールバックを所有するのは通知側だけ。 ネイティブの解析はパス
        // ・設定だけを必要とし、stop() で保持中のUI状態を解放する。
        request_.on_changed = {};
        request_.on_error = {};
        const std::wstring directory = request_.directory.wstring();
        if (directory.empty()) throw std::runtime_error("watch directory is empty");

        directory_handle_ = CreateFileW(
            directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory_handle_ == INVALID_HANDLE_VALUE) {
            directory_handle_ = nullptr;
            throw std::runtime_error(windows_error("CreateFileW(directory)"));
        }
        stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        io_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop_event_ == nullptr || io_event_ == nullptr) {
            if (io_event_ != nullptr) {
                CloseHandle(io_event_);
                io_event_ = nullptr;
            }
            if (stop_event_ != nullptr) {
                CloseHandle(stop_event_);
                stop_event_ = nullptr;
            }
            if (directory_handle_ != nullptr) {
                CloseHandle(directory_handle_);
                directory_handle_ = nullptr;
            }
            throw std::runtime_error(windows_error("CreateEventW"));
        }
        try {
            worker_ = std::jthread([this](std::stop_token) { run(); });
        } catch (...) {
            close_handles();
            throw;
        }
    }

    ~WindowsFileWatch() override {
        stop();
        close_handles();
    }

    WindowsFileWatch(const WindowsFileWatch&) = delete;
    WindowsFileWatch& operator=(const WindowsFileWatch&) = delete;

    void stop() noexcept override {
        if (stopped_.exchange(true, std::memory_order_acq_rel)) return;
        delivery_.stop();
        if (stop_event_ != nullptr) SetEvent(stop_event_);
        if (directory_handle_ != nullptr) CancelIoEx(directory_handle_, nullptr);
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            worker_.join();
        }
    }

private:
    void close_handles() noexcept {
        if (io_event_ != nullptr) {
            CloseHandle(io_event_);
            io_event_ = nullptr;
        }
        if (stop_event_ != nullptr) {
            CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
        if (directory_handle_ != nullptr) {
            CloseHandle(directory_handle_);
            directory_handle_ = nullptr;
        }
    }

    void report_error(std::string message) const noexcept {
        (void)delivery_.error(std::move(message));
    }

    void dispatch(std::vector<FileChange> changes) const noexcept {
        (void)delivery_.publish(std::move(changes));
    }

    bool dispatch_overflow() const noexcept {
        try {
            return delivery_.publish({{FileChangeKind::Overflow, request_.directory, {}}});
        } catch (...) { return false; }
    }

    [[nodiscard]] std::vector<FileChange> parse_notifications(const std::byte* bytes,
                                                              std::size_t length,
                                                              std::optional<std::filesystem::path>&
                                                                  pending_rename) const {
        constexpr std::size_t fixed_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        std::vector<FileChange> changes;
        std::size_t offset = 0;
        while (offset < length) {
            if (length - offset < fixed_size) throw std::runtime_error("malformed directory notification");
            const auto* information = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes + offset);
            const std::size_t name_bytes = information->FileNameLength;
            if ((name_bytes % sizeof(wchar_t)) != 0 ||
                name_bytes > length - offset - fixed_size) {
                throw std::runtime_error("malformed directory notification name");
            }
            const std::wstring name(information->FileName, name_bytes / sizeof(wchar_t));
            const std::filesystem::path path = request_.directory / std::filesystem::path(name);
            const auto flush_pending = [&]() {
                if (pending_rename.has_value()) {
                    FileChange removed;
                    removed.kind = FileChangeKind::Removed;
                    removed.path = *pending_rename;
                    changes.push_back(std::move(removed));
                    pending_rename.reset();
                }
            };
            switch (information->Action) {
            case FILE_ACTION_ADDED: {
                flush_pending();
                FileChange change;
                change.kind = FileChangeKind::Created;
                change.path = path;
                changes.push_back(std::move(change));
                break;
            }
            case FILE_ACTION_REMOVED: {
                flush_pending();
                FileChange change;
                change.kind = FileChangeKind::Removed;
                change.path = path;
                changes.push_back(std::move(change));
                break;
            }
            case FILE_ACTION_MODIFIED: {
                flush_pending();
                FileChange change;
                change.kind = FileChangeKind::Modified;
                change.path = path;
                changes.push_back(std::move(change));
                break;
            }
            case FILE_ACTION_RENAMED_OLD_NAME:
                flush_pending();
                pending_rename = path;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME: {
                FileChange change;
                change.kind = FileChangeKind::Renamed;
                change.path = path;
                if (pending_rename.has_value()) {
                    change.old_path = *pending_rename;
                    pending_rename.reset();
                }
                changes.push_back(std::move(change));
                break;
            }
            default:
                flush_pending();
                throw std::runtime_error("unknown directory notification action");
            }

            const DWORD next = information->NextEntryOffset;
            if (next == 0) break;
            if (next < fixed_size || next > length - offset) {
                throw std::runtime_error("malformed directory notification offset");
            }
            offset += next;
        }
        return changes;
    }

    void run() noexcept {
        try {
            run_impl();
        } catch (const std::exception& exception) {
            report_error(exception.what());
        } catch (...) {
            report_error("directory watch failed with an unknown exception");
        }
    }

    void run_impl() {
        constexpr DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                         FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION |
                                         FILE_NOTIFY_CHANGE_SECURITY;
        std::vector<std::byte> buffer(64U * 1024U);
        std::optional<std::filesystem::path> pending_rename;
        bool initial_published{};

        while (!stopped_.load(std::memory_order_acquire)) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = io_event_;
            ResetEvent(io_event_);
            DWORD bytes = 0;
            const BOOL started = ReadDirectoryChangesW(
                directory_handle_, buffer.data(), static_cast<DWORD>(buffer.size()),
                request_.recursive ? TRUE : FALSE, notify_filter, nullptr, &overlapped, nullptr);
            if (!started) {
                const DWORD code = GetLastError();
                if (code != ERROR_IO_PENDING) {
                    if (!stopped_.load(std::memory_order_acquire)) report_error(windows_error("ReadDirectoryChangesW", code));
                    return;
                }
            }

            // ビューの初回読み取り後に登録するため、ネイティブ通
            // 知の有効化後に再走査を要求し、その間の変更を拾う。
            if (!initial_published) initial_published = dispatch_overflow();
            bool completed = GetOverlappedResult(directory_handle_, &overlapped, &bytes, FALSE) != FALSE;
            if (!completed && GetLastError() != ERROR_IO_INCOMPLETE) {
                const DWORD code = GetLastError();
                if (stopped_.load(std::memory_order_acquire) && code == ERROR_OPERATION_ABORTED) return;
                report_error(windows_error("GetOverlappedResult", code));
                return;
            }
            if (!completed) {
                const HANDLE events[] = {stop_event_, io_event_};
                const DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait_result == WAIT_OBJECT_0) {
                    CancelIoEx(directory_handle_, &overlapped);
                    GetOverlappedResult(directory_handle_, &overlapped, &bytes, TRUE);
                    return;
                }
                if (wait_result != WAIT_OBJECT_0 + 1U) {
                    if (!stopped_.load(std::memory_order_acquire)) report_error(windows_error("WaitForMultipleObjects"));
                    CancelIoEx(directory_handle_, &overlapped);
                    GetOverlappedResult(directory_handle_, &overlapped, &bytes, TRUE);
                    return;
                }
                if (!GetOverlappedResult(directory_handle_, &overlapped, &bytes, FALSE)) {
                    const DWORD code = GetLastError();
                    if (stopped_.load(std::memory_order_acquire) && code == ERROR_OPERATION_ABORTED) return;
                    if (code == ERROR_NOTIFY_ENUM_DIR) {
                        pending_rename.reset();
                        dispatch_overflow();
                        continue;
                    }
                    report_error(windows_error("GetOverlappedResult", code));
                    return;
                }
                completed = true;
            }
            if (!completed) continue;
            if (bytes == 0) {
                pending_rename.reset();
                dispatch_overflow();
                continue;
            }
            try {
                dispatch(parse_notifications(buffer.data(), bytes, pending_rename));
            } catch (const std::exception& exception) {
                pending_rename.reset();
                dispatch_overflow();
                report_error(exception.what());
            }
        }
    }

    WatchRequest request_;
    FileWatchDelivery delivery_;
    HANDLE directory_handle_{};
    HANDLE stop_event_{};
    HANDLE io_event_{};
    std::jthread worker_;
    std::atomic<bool> stopped_{};
};

class WindowsFileSystemBackend final : public FileSystemBackend {
public:
    explicit WindowsFileSystemBackend(std::shared_ptr<MainDispatcher> dispatcher = {})
        : dispatcher_(std::move(dispatcher)) {}

    [[nodiscard]] std::unique_ptr<FileWatchHandle> watch(WatchRequest request) override {
        const auto on_error = request.on_error;
        try {
            return std::make_unique<WindowsFileWatch>(std::move(request), dispatcher_);
        } catch (const std::exception& exception) {
            if (on_error) {
                try {
                    on_error(exception.what());
                } catch (...) {
                }
            }
            return {};
        }
    }

    bool durable_write(const std::filesystem::path& target, std::span<const std::byte> data,
                       std::string& error) override {
        return storage_api::write_native_file_atomic(target, data, error);
    }

    bool atomic_replace(const std::filesystem::path& temporary, const std::filesystem::path& target,
                        std::string& error) override {
        try {
        const std::wstring source_name = temporary.wstring();
        const std::wstring target_name = target.wstring();
        if (source_name.empty() || target_name.empty()) {
            error = "atomic replacement path is empty";
            return false;
        }
        if (!MoveFileExW(source_name.c_str(), target_name.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            assign_error(error, "MoveFileExW(atomic replace)");
            return false;
        }
        return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    bool move_to_trash(const std::vector<std::filesystem::path>& paths, std::string& error) override {
        try {
        if (paths.empty()) return true;
        std::vector<wchar_t> names;
        for (const auto& path : paths) {
            const std::wstring value = path.wstring();
            if (value.empty() || value.find(L'\0') != std::wstring::npos) {
                error = "trash path is empty or contains NUL";
                return false;
            }
            names.insert(names.end(), value.begin(), value.end());
            names.push_back(L'\0');
        }
        names.push_back(L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = names.data();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        const int result = SHFileOperationW(&operation);
        if (result != 0) {
            assign_error(error, "SHFileOperationW(FO_DELETE)", static_cast<DWORD>(result));
            return false;
        }
        if (operation.fAnyOperationsAborted != FALSE) {
            error = "SHFileOperationW(FO_DELETE) was cancelled";
            return false;
        }
        return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    [[nodiscard]] bool valid_filename_character(char32_t value) const override {
        if (value == 0 || value < 0x20) return false;
        switch (value) {
        case U'\\':
        case U'/':
        case U':':
        case U'*':
        case U'?':
        case U'"':
        case U'<':
        case U'>':
        case U'|':
            return false;
        default:
            return true;
        }
    }

    [[nodiscard]] std::string filename_identity(
        const std::filesystem::path& path) const override {
        try {
            const std::wstring value = normalized_path(path);
            if (value.empty()) return {};
            const auto exact_fallback = [&value] { return wide_to_utf8(value); };
            if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return exact_fallback();
            const int required = LCMapStringEx(
                LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(),
                static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
            if (required <= 0) return exact_fallback();
            std::wstring folded(static_cast<std::size_t>(required), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                              value.data(), static_cast<int>(value.size()),
                              folded.data(), required, nullptr, nullptr, 0) <= 0)
                return exact_fallback();
            return wide_to_utf8(folded);
        } catch (...) {
            // 変換に失敗した二つの値を、同じ空キーへまと
            // めない。 ネイティブの大小文字変換が使え
            // なくても、字句上のキーで識別を維持する。
            return generic_path_to_utf8(path.lexically_normal());
        }
    }

private:
    [[nodiscard]] static std::wstring normalized_path(const std::filesystem::path& path) {
        std::wstring value = path.wstring();
        if (value.empty()) return value;
        std::vector<wchar_t> buffer(32768U);
        DWORD length = GetFullPathNameW(value.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length != 0 && length < buffer.size()) {
            value.assign(buffer.data(), length);
        }
        std::replace(value.begin(), value.end(), L'/', L'\\');
        while (value.size() > 1U && value.back() == L'\\' &&
               !(value.size() == 3U && value[1] == L':')) {
            value.pop_back();
        }
        return value;
    }

    std::shared_ptr<MainDispatcher> dispatcher_;
};

class WindowsProcessHandle final : public ProcessHandle {
public:
    WindowsProcessHandle(unique_handle process, unique_handle thread, unique_handle stdout_read,
                         unique_handle stderr_read, ProcessId id,
                         const bool terminate_on_release)
        : process_(process.release()), thread_(thread.release()), stdout_read_(stdout_read.release()),
          stderr_read_(stderr_read.release()), id_(id),
          terminate_on_release_(terminate_on_release) {}

    ~WindowsProcessHandle() override {
        if (terminate_on_release_ && process_ != nullptr &&
            WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process_, 1U);
            WaitForSingleObject(process_, INFINITE);
        }
        finish_readers(true);
        close_handle(thread_);
        close_handle(process_);
    }

    WindowsProcessHandle(const WindowsProcessHandle&) = delete;
    WindowsProcessHandle& operator=(const WindowsProcessHandle&) = delete;

    void start_readers() {
        if (stdout_read_ != nullptr) {
            stdout_reader_ = std::jthread([this](std::stop_token) {
                read_pipe(stdout_read_, stdout_text_);
            });
        }
        if (stderr_read_ != nullptr) {
            stderr_reader_ = std::jthread([this](std::stop_token) {
                read_pipe(stderr_read_, stderr_text_);
            });
        }
    }

    [[nodiscard]] ProcessId id() const noexcept override { return id_; }

    [[nodiscard]] bool running() const override {
        if (process_ == nullptr) return false;
        DWORD code = 0;
        return GetExitCodeProcess(process_, &code) != FALSE && code == STILL_ACTIVE;
    }

    [[nodiscard]] std::optional<int> exit_code() const override {
        if (process_ == nullptr) return std::nullopt;
        DWORD code = 0;
        if (!GetExitCodeProcess(process_, &code) || code == STILL_ACTIVE) return std::nullopt;
        return static_cast<int>(code);
    }

    bool wait(std::chrono::milliseconds timeout) override {
        if (process_ == nullptr || timeout.count() < 0) return false;
        DWORD milliseconds = INFINITE;
        if (timeout != std::chrono::milliseconds::max()) {
            const auto count = timeout.count();
            milliseconds = count >= static_cast<long long>(INFINITE - 1U)
                               ? INFINITE - 1U
                               : static_cast<DWORD>(count);
        }
        const DWORD result = WaitForSingleObject(process_, milliseconds);
        if (result != WAIT_OBJECT_0) return false;
        finish_readers(false);
        return true;
    }

    void terminate() override {
        if (process_ != nullptr && running()) TerminateProcess(process_, 1U);
    }

    [[nodiscard]] std::string read_stdout() override {
        std::lock_guard lock(output_mutex_);
        return stdout_text_;
    }

    [[nodiscard]] std::string read_stderr() override {
        std::lock_guard lock(output_mutex_);
        return stderr_text_;
    }

private:
    static void close_handle(HANDLE& handle) noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }

    void read_pipe(HANDLE pipe, std::string& destination) noexcept {
        char buffer[32U * 1024U];
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) || read == 0) break;
            try {
                std::lock_guard lock(output_mutex_);
                destination.append(buffer, read);
            } catch (...) {
                break;
            }
        }
    }

    void cancel_reader(std::jthread& reader) noexcept {
        reader.request_stop();
        if (reader.joinable()) CancelSynchronousIo(reader.native_handle());
    }

    void finish_readers(bool cancel) noexcept {
        if (readers_finished_.exchange(true, std::memory_order_acq_rel)) return;
        if (cancel) {
            cancel_reader(stdout_reader_);
            cancel_reader(stderr_reader_);
        }
        if (stdout_reader_.joinable()) stdout_reader_.join();
        if (stderr_reader_.joinable()) stderr_reader_.join();
        close_handle(stdout_read_);
        close_handle(stderr_read_);
    }

    HANDLE process_{};
    HANDLE thread_{};
    HANDLE stdout_read_{};
    HANDLE stderr_read_{};
    ProcessId id_{};
    std::jthread stdout_reader_;
    std::jthread stderr_reader_;
    mutable std::mutex output_mutex_;
    std::string stdout_text_;
    std::string stderr_text_;
    std::atomic<bool> readers_finished_{};
    bool terminate_on_release_{true};
};

class WindowsProcessBackend final : public ProcessBackend {
public:
    [[nodiscard]] std::shared_ptr<ProcessHandle> launch(const ProcessLaunchRequest& request,
                                                        std::string& error) override {
        try {
            if (request.executable.empty()) throw std::runtime_error("process executable is empty");
            const std::wstring executable = request.executable.wstring();
            if (executable.empty()) throw std::runtime_error("process executable is empty");

            std::wstring command_line = quote_windows_argument(executable);
            for (const std::string& argument : request.arguments) {
                if (argument.find('\0') != std::string::npos) throw std::runtime_error("process argument contains NUL");
                command_line.push_back(L' ');
                command_line += quote_windows_argument(utf8_to_wide(argument, "process argument"));
            }
            std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
            command_buffer.push_back(L'\0');

            std::vector<wchar_t> environment;
            LPVOID environment_pointer = nullptr;
            if (!request.inherit_environment || !request.environment.empty()) {
                environment = make_environment_block(request);
                environment_pointer = environment.data();
            }
            const std::wstring working_directory = request.working_directory.wstring();

            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;
            unique_handle stdin_handle = make_nul_handle(GENERIC_READ, security, "stdin");
            unique_handle stdout_read;
            unique_handle stdout_write = make_output_handle(request.capture_output, stdout_read, security, "stdout");
            unique_handle stderr_read;
            unique_handle stderr_write = make_output_handle(request.capture_output, stderr_read, security, "stderr");

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = stdin_handle.get();
            startup.hStdOutput = stdout_write.get();
            startup.hStdError = stderr_write.get();
            PROCESS_INFORMATION information{};
            DWORD flags = CREATE_UNICODE_ENVIRONMENT;
            if (!request.show_console) flags |= CREATE_NO_WINDOW;
            const BOOL created = CreateProcessW(
                executable.c_str(), command_buffer.data(), nullptr, nullptr, TRUE, flags, environment_pointer,
                working_directory.empty() ? nullptr : working_directory.c_str(), &startup, &information);
            if (!created) {
                assign_error(error, "CreateProcessW");
                return {};
            }
            // 子は継承可能なハンドルを複製済み。 同
            // 期的に待つ前に親の複製を閉じ、パイプの
            // 読み手がEOFを検出できるようにする。
            stdin_handle.reset();
            stdout_write.reset();
            stderr_write.reset();
            unique_handle process(information.hProcess);
            unique_handle thread(information.hThread);
            auto handle = std::make_shared<WindowsProcessHandle>(
                std::move(process), std::move(thread), std::move(stdout_read), std::move(stderr_read),
                static_cast<ProcessId>(information.dwProcessId),
                request.terminate_on_handle_release);
            handle->start_readers();
            if (request.wait) handle->wait(std::chrono::milliseconds::max());
            return handle;
        } catch (const std::exception& exception) {
            error = exception.what();
            return {};
        }
    }

private:
    [[nodiscard]] static unique_handle make_nul_handle(DWORD access, SECURITY_ATTRIBUTES& security,
                                                       std::string_view description) {
        HANDLE handle = CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error(windows_error(std::string("CreateFileW(NUL ") +
                                                    std::string(description) + ')'));
        }
        return unique_handle(handle);
    }

    [[nodiscard]] static unique_handle make_output_handle(bool capture, unique_handle& read,
                                                          SECURITY_ATTRIBUTES& security,
                                                          std::string_view description) {
        if (!capture) return make_nul_handle(GENERIC_WRITE, security, description);
        HANDLE read_handle = nullptr;
        HANDLE write_handle = nullptr;
        if (!CreatePipe(&read_handle, &write_handle, &security, 0)) {
            throw std::runtime_error(windows_error(std::string("CreatePipe(") + std::string(description) + ')'));
        }
        unique_handle read_guard(read_handle);
        unique_handle write_guard(write_handle);
        if (!SetHandleInformation(read_guard.get(), HANDLE_FLAG_INHERIT, 0)) {
            throw std::runtime_error(windows_error("SetHandleInformation(pipe)"));
        }
        read = std::move(read_guard);
        return write_guard;
    }
};

class WindowsDynamicLibrary final : public DynamicLibrary {
public:
    explicit WindowsDynamicLibrary(HMODULE module) noexcept : module_(module) {}
    ~WindowsDynamicLibrary() override {
        if (module_ != nullptr) FreeLibrary(module_);
    }

    [[nodiscard]] void* symbol(std::string_view name) const override {
        if (module_ == nullptr || name.empty() || name.find('\0') != std::string_view::npos) return nullptr;
        const std::string symbol_name(name);
        return reinterpret_cast<void*>(GetProcAddress(module_, symbol_name.c_str()));
    }

private:
    HMODULE module_{};
};

std::filesystem::path known_folder(const KNOWNFOLDERID& identifier) {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(identifier, KF_FLAG_DEFAULT, nullptr, &value)) || value == nullptr) {
        return {};
    }
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
}

class WindowsSystemBackend final : public SystemBackend {
public:
    [[nodiscard]] Platform platform() const noexcept override { return Platform::Windows; }

    [[nodiscard]] std::filesystem::path executable_path() const override {
        std::vector<wchar_t> buffer(512U);
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return {};
            if (length + 1U < buffer.size()) {
                return std::filesystem::path(std::wstring(buffer.data(), length));
            }
            if (buffer.size() >= 32768U) return {};
            buffer.resize(std::min<std::size_t>(buffer.size() * 2U, 32768U));
        }
    }

    [[nodiscard]] std::filesystem::path special_directory(SpecialDirectory directory) const override {
        switch (directory) {
        case SpecialDirectory::Application: {
            const auto path = executable_path();
            return path.empty() ? std::filesystem::path{} : path.parent_path();
        }
        case SpecialDirectory::ApplicationData:
            return known_folder(FOLDERID_RoamingAppData);
        case SpecialDirectory::UserData:
            return known_folder(FOLDERID_LocalAppData);
        case SpecialDirectory::Documents:
            return known_folder(FOLDERID_Documents);
        case SpecialDirectory::Temporary: {
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;) {
                const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
                if (length == 0) return {};
                if (length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length));
                buffer.resize(static_cast<std::size_t>(length) + 1U);
            }
        }
        case SpecialDirectory::Home:
            return known_folder(FOLDERID_Profile);
        }
        return {};
    }

    [[nodiscard]] std::string locale_name() const override {
        wchar_t buffer[LOCALE_NAME_MAX_LENGTH]{};
        const int length = GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
        return length > 0 ? wide_to_utf8(std::wstring_view(buffer, static_cast<std::size_t>(length - 1))) : std::string{};
    }

    [[nodiscard]] ThemeMode theme_mode() const override {
        HIGHCONTRASTW contrast{};
        contrast.cbSize = sizeof(contrast);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
            return ThemeMode::HighContrast;
        }
        DWORD value = 1;
        DWORD size = sizeof(value);
        DWORD type = 0;
        const LONG result = RegGetValueW(HKEY_CURRENT_USER,
                                         L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, &type, &value, &size);
        if (result != ERROR_SUCCESS) return ThemeMode::System;
        return value == 0 ? ThemeMode::Dark : ThemeMode::Light;
    }

    [[nodiscard]] std::chrono::milliseconds double_click_time() const noexcept override {
        return std::chrono::milliseconds{::GetDoubleClickTime()};
    }

    [[nodiscard]] float scale_factor() const override {
        // ALL は全ホストで一つの固定論理キャンバスを使う（portable_runtime参照）。
        if (::cwxeditor::lib::editor_window_host_options().all_mode) return 1.0F;
        using get_dpi_for_system = UINT(WINAPI*)();
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr) {
            const auto function = reinterpret_cast<get_dpi_for_system>(GetProcAddress(user32, "GetDpiForSystem"));
            if (function != nullptr) {
                const UINT dpi = function();
                if (dpi != 0) return static_cast<float>(dpi) / 96.0F;
            }
        }
        HDC dc = GetDC(nullptr);
        if (dc == nullptr) return 1.0F;
        const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        return dpi > 0 ? static_cast<float>(dpi) / 96.0F : 1.0F;
    }

    [[nodiscard]] std::size_t working_memory() const override {
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE
                   ? static_cast<std::size_t>(counters.WorkingSetSize)
                   : 0U;
    }

    [[nodiscard]] std::unique_ptr<DynamicLibrary> load_library(const std::filesystem::path& path,
                                                                std::string& error) override {
        try {
            const std::wstring filename = path.wstring();
            if (filename.empty()) {
                error = "dynamic library path is empty";
                return {};
            }
            HMODULE module = LoadLibraryW(filename.c_str());
            if (module == nullptr) {
                assign_error(error, "LoadLibraryW");
                return {};
            }
            return std::make_unique<WindowsDynamicLibrary>(module);
        } catch (const std::exception& exception) {
            error = exception.what();
            return {};
        }
    }

    [[nodiscard]] std::vector<std::byte> executable_resource(const std::filesystem::path& executable,
                                                              std::string_view type, std::string_view name,
                                                              std::string& error) override {
        try {
            const std::wstring type_value = utf8_to_wide(type, "resource type");
            const std::wstring name_value = utf8_to_wide(name, "resource name");
            std::wstring type_storage;
            std::wstring name_storage;
            const LPCWSTR type_id = resource_id(type_value, type_storage);
            const LPCWSTR name_id = resource_id(name_value, name_storage);

            HMODULE module = nullptr;
            bool owned = false;
            if (executable.empty()) {
                module = GetModuleHandleW(nullptr);
            } else {
                const std::wstring filename = executable.wstring();
                // エディタのリソースは通常、実行中の実行ファイル内に
                // ある。 小さいアイコンごとに同じPEを再読み込み・
                // 解放すると、特に多数を使う設定一覧で負荷が大きい。
                // FindResource はプロセスのモジュール
                // を使えるため、両パスが同じファイルなら再利用する。
                const auto normalized_path = [](const std::wstring& input) {
                    if (input.empty()) return std::wstring{};
                    const DWORD needed = GetFullPathNameW(input.c_str(), 0,
                                                          nullptr, nullptr);
                    if (needed == 0U) return input;
                    std::wstring output(static_cast<std::size_t>(needed), L'\0');
                    const DWORD written = GetFullPathNameW(
                        input.c_str(), needed, output.data(), nullptr);
                    if (written == 0U || written >= needed) return input;
                    output.resize(written);
                    std::replace(output.begin(), output.end(), L'/', L'\\');
                    return output;
                };
                static const std::wstring current_filename = [] {
                    std::wstring value(32768U, L'\0');
                    const DWORD length = GetModuleFileNameW(
                        nullptr, value.data(), static_cast<DWORD>(value.size()));
                    if (length == 0U || length >= value.size()) return std::wstring{};
                    value.resize(length);
                    return value;
                }();
                const auto normalized_executable = normalized_path(filename);
                const auto normalized_current = normalized_path(current_filename);
                const bool current_module = !normalized_current.empty() &&
                    CompareStringOrdinal(normalized_executable.c_str(), -1,
                                         normalized_current.c_str(), -1,
                                         TRUE) == CSTR_EQUAL;
                if (current_module) {
                    module = GetModuleHandleW(nullptr);
                } else {
                    module = LoadLibraryExW(
                        filename.c_str(), nullptr,
                        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
                    owned = module != nullptr;
                }
            }
            if (module == nullptr) {
                assign_error(error, "LoadLibraryExW(resource module)");
                return {};
            }

            HRSRC resource = FindResourceW(module, name_id, type_id);
            // rc.exe は文字列リソースIDの引用符を残し
            // 、大文字小文字を区別せず検索する。 共通の呼び出
            // し元は引用符なしの自然なパスを使うため、 リソー
            // スコンパイラーが作るPE内の表記でも再試行する。
            std::wstring quoted_name;
            if (resource == nullptr && !name_storage.empty() &&
                (name_storage.size() < 2U || name_storage.front() != L'"' ||
                 name_storage.back() != L'"')) {
                quoted_name.reserve(name_storage.size() + 2U);
                quoted_name.push_back(L'"');
                quoted_name.append(name_storage);
                quoted_name.push_back(L'"');
                resource = FindResourceW(module, quoted_name.c_str(), type_id);
            }
            if (resource == nullptr) {
                const DWORD code = GetLastError();
                if (owned) FreeLibrary(module);
                assign_error(error, "FindResourceW", code);
                return {};
            }
            const HGLOBAL loaded = LoadResource(module, resource);
            const DWORD size = SizeofResource(module, resource);
            const void* data = loaded == nullptr ? nullptr : LockResource(loaded);
            if ((size != 0 && data == nullptr) || loaded == nullptr) {
                const DWORD code = GetLastError();
                if (owned) FreeLibrary(module);
                assign_error(error, "LoadResource", code);
                return {};
            }
            std::vector<std::byte> result(static_cast<std::size_t>(size));
            if (size != 0) std::memcpy(result.data(), data, size);
            if (owned) FreeLibrary(module);
            return result;
        } catch (const std::exception& exception) {
            error = exception.what();
            return {};
        }
    }

    void set_environment(std::string name, std::string value) override {
        try {
            const std::wstring wide_name = utf8_to_wide(name, "environment name");
            const std::wstring wide_value = utf8_to_wide(value, "environment value");
            if (!wide_name.empty()) SetEnvironmentVariableW(wide_name.c_str(), wide_value.c_str());
        } catch (...) {
        }
    }

    [[nodiscard]] std::optional<std::string> environment(std::string_view name) const override {
        try {
            const std::wstring wide_name = utf8_to_wide(name, "environment name");
            if (wide_name.empty()) return std::nullopt;
            std::vector<wchar_t> buffer(256U);
            for (;;) {
                SetLastError(ERROR_SUCCESS);
                const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), buffer.data(),
                                                               static_cast<DWORD>(buffer.size()));
                if (length == 0) {
                    return GetLastError() == ERROR_SUCCESS ? std::optional<std::string>(std::string{})
                                                            : std::nullopt;
                }
                if (length < buffer.size()) return wide_to_utf8(std::wstring_view(buffer.data(), length));
                buffer.resize(static_cast<std::size_t>(length) + 1U);
            }
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    [[nodiscard]] static LPCWSTR resource_id(const std::wstring& value, std::wstring& storage) {
        // リソーススクリプトはWindows標準の種類を名前で
        // 記すが、PE内には数値の RT_* で保存される。
        // 共通APIはRCDATAなどの読める名前を使い、FindResourceW
        // が求める表現へ変換する。
        if (_wcsicmp(value.c_str(), L"RCDATA") == 0) {
            return MAKEINTRESOURCEW(10);
        }
        if (value.size() > 1U && value.front() == L'#') {
            wchar_t* end = nullptr;
            const unsigned long id = std::wcstoul(value.c_str() + 1, &end, 10);
            if (end != value.c_str() + 1 && *end == L'\0' && id <= 0xFFFFU) {
                return MAKEINTRESOURCEW(static_cast<WORD>(id));
            }
        }
        storage = value;
        return storage.c_str();
    }
};

struct WindowsAudioTrack final {
    std::wstring alias;
    std::uint32_t device_id{};
    std::uint64_t play_generation{};
    std::uint64_t pending_play_generation{};
    AudioBus bus{AudioBus::SoundEffect};
    std::chrono::milliseconds duration{};
    std::chrono::milliseconds loop_start{};
    std::uint32_t repetitions_remaining{};
    bool repeat_forever{};
    bool repeat_failed{};
    bool terminal_stopped{};
};

class WindowsAudioBackend;

struct MciNotificationRequest final {
    WindowsAudioBackend* owner{};
    AudioHandle handle{};
    std::uint64_t generation{};
    std::uint32_t device_id{};
    HWND callback_window{};
    std::atomic_bool cancelled{};
    std::atomic_bool event_queued{};
    std::mutex creation_mutex;
    std::condition_variable created;
    bool creation_complete{};
};

struct MciNotificationWindowCreateContext final {
    std::shared_ptr<MciNotificationRequest>* owner{};
    bool transferred{};
};

struct MciNotificationEvent final {
    std::shared_ptr<MciNotificationRequest> request;
    std::uint32_t result{};
};

[[nodiscard]] std::string mci_error_text(MCIERROR code) {
    wchar_t buffer[256]{};
    if (mciGetErrorStringW(code, buffer, static_cast<UINT>(sizeof(buffer) / sizeof(buffer[0]))) != FALSE) {
        return wide_to_utf8(std::wstring_view(buffer));
    }
    return "MCI command failed (" + std::to_string(static_cast<unsigned long>(code)) + ')';
}

[[nodiscard]] bool mci_command(
    std::wstring_view command, std::wstring* response = nullptr,
    std::string* error = nullptr,
    const testing::MciCommandPrimitive& primitive = {},
    std::uintptr_t callback_window = 0U) {
    const std::wstring command_storage(command);
    wchar_t result[256]{};
    std::wstring injected_result;
    const auto send = [&]() {
        return primitive.send
            ? primitive.send(command_storage,
                             response == nullptr ? nullptr : &injected_result,
                             callback_window)
            : static_cast<std::uint32_t>(mciSendStringW(
                  command_storage.c_str(), response == nullptr ? nullptr : result,
                  response == nullptr
                      ? 0U
                      : static_cast<UINT>(sizeof(result) / sizeof(result[0])),
                  reinterpret_cast<HWND>(callback_window)));
    };
    const std::uint32_t raw_code = primitive.dispatch
        ? primitive.dispatch(send) : send();
    const MCIERROR code = static_cast<MCIERROR>(raw_code);
    if (code != 0U) {
        if (error != nullptr) *error = mci_error_text(code);
        return false;
    }
    if (response != nullptr)
        response->assign(primitive.send ? injected_result : std::wstring(result));
    return true;
}

[[nodiscard]] std::uint32_t mci_device_id(
    std::wstring_view alias,
    const testing::MciCommandPrimitive& primitive = {}) {
    const std::wstring alias_storage(alias);
    const auto get_device_id = [&]() {
        if (primitive.device_id) return primitive.device_id(alias_storage);
        return static_cast<std::uint32_t>(
            mciGetDeviceIDW(alias_storage.c_str()));
    };
    return primitive.dispatch
        ? primitive.dispatch(get_device_id) : get_device_id();
}

[[nodiscard]] std::wstring mci_mode(
    const std::wstring& alias,
    const testing::MciCommandPrimitive& primitive = {}) {
    std::wstring response;
    if (!mci_command(L"status " + alias + L" mode", &response, nullptr,
                     primitive)) return {};
    return response;
}

[[nodiscard]] std::chrono::milliseconds mci_time(
    const std::wstring& alias, std::wstring_view field,
    const testing::MciCommandPrimitive& primitive = {}) {
    std::wstring response;
    if (!mci_command(L"status " + alias + L' ' + std::wstring(field),
                     &response, nullptr, primitive)) return {};
    wchar_t* end = nullptr;
    const long long value = std::wcstoll(response.c_str(), &end, 10);
    if (end == response.c_str() || value < 0) return {};
    return std::chrono::milliseconds(value);
}

[[nodiscard]] float normalized_volume(float value) noexcept {
    if (!std::isfinite(value)) return 0.0F;
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] std::size_t audio_bus_index(AudioBus bus) noexcept {
    switch (bus) {
    case AudioBus::Bgm: return 0U;
    case AudioBus::SoundEffect: return 1U;
    case AudioBus::Preview: return 2U;
    }
    return 1U;
}

[[nodiscard]] std::wstring mci_type_for(const AudioSource& source, const std::filesystem::path& filename) {
    std::wstring hint = source.format_hint.empty() ? filename.extension().wstring() :
                                                     utf8_to_wide(source.format_hint, "audio format hint");
    std::transform(hint.begin(), hint.end(), hint.begin(), [](wchar_t value) { return std::towlower(value); });
    if (!hint.empty() && hint.front() != L'.') hint.insert(hint.begin(), L'.');
    if (hint == L".wav" || hint == L".wave") return L"waveaudio";
    if (hint == L".mp3" || hint == L".mp2") return L"mpegvideo";
    if (hint == L".mid" || hint == L".midi") return L"sequencer";
    return {};
}

class WindowsAudioBackend final : public AudioBackend {
    struct NotificationBarrier final {
        std::mutex mutex;
        std::condition_variable ready;
        bool complete{};
    };

    struct MciCommandRequest final {
        std::function<std::uint32_t()> operation;
        std::mutex mutex;
        std::condition_variable ready;
        bool complete{};
        std::uint32_t result{};
        std::exception_ptr error;
    };

public:
    WindowsAudioBackend()
        : WindowsAudioBackend({}, std::chrono::milliseconds(50)) {}

    WindowsAudioBackend(testing::MciCommandPrimitive primitive,
                        std::chrono::milliseconds monitor_interval)
        : backend_id_(next_backend_id_.fetch_add(1U, std::memory_order_relaxed)),
          primitive_(std::move(primitive)),
          monitor_interval_(std::max(monitor_interval,
                                     std::chrono::milliseconds(1))),
          command_worker_([this](std::stop_token token) {
              run_mci_command_owner(token);
          }),
          worker_([this](std::stop_token token) { monitor(token); }) {
        primitive_.dispatch = [this](std::function<std::uint32_t()> operation) {
            return dispatch_mci_command(std::move(operation));
        };
    }

    ~WindowsAudioBackend() override {
        // コマンド用ワーカーを止める前に、すべてのMCI
        // 別名を、その所有スレッド上で閉じる。
        stop_all();
        worker_.request_stop();
        wake_notification_worker();
        if (worker_.joinable()) worker_.join();
        command_worker_.request_stop();
        command_queue_ready_.notify_all();
        if (command_worker_.joinable()) command_worker_.join();
    }

    bool initialize(std::string& error) override {
        {
            std::unique_lock ready_lock(notification_ready_mutex_);
            if (!notification_ready_.wait_for(
                    ready_lock, std::chrono::seconds(5),
                    [this] { return notification_worker_ready_; })) {
                error = "WinMM notification window did not start";
                return false;
            }
            if (!notification_window_ready_) {
                error = "WinMM notification window could not be created";
                return false;
            }
        }
        std::lock_guard lock(mutex_);
        error.clear();
        initialized_ = true;
        return true;
    }

    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        std::string error;
        const bool ready = initialize(error);
        std::lock_guard lock(mutex_);
        configuration_ = {};
        configuration_.requested = request.preference;
        configuration_.effective = AudioBackendPreference::Native;
        configuration_.capabilities = ready
            ? AudioCapability::Playback | AudioCapability::Native | AudioCapability::Midi
            : AudioCapability::None;
        configuration_.used_fallback = ready &&
            request.preference != AudioBackendPreference::Automatic &&
            request.preference != AudioBackendPreference::Native;
        if (!ready) configuration_.error = std::move(error);
        if (ready && !request.allow_fallback &&
            request.preference != AudioBackendPreference::Automatic &&
            request.preference != AudioBackendPreference::Native) {
            configuration_.capabilities = AudioCapability::None;
            configuration_.error = "WinMM does not provide the requested backend";
        }
        return configuration_;
    }

    [[nodiscard]] AudioConfigurationResult configuration() const override {
        std::lock_guard lock(mutex_);
        return configuration_;
    }

    [[nodiscard]] AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) override {
        std::unique_lock lock(mutex_);
        if (!initialized_ || source.path.empty() || source.encoded_data.size() != 0U) return 0;

        std::wstring alias;
        bool opened = false;
        try {
            const std::wstring filename = source.path.wstring();
            if (filename.empty() || filename.find(L'\0') != std::wstring::npos || filename.find(L'"') != std::wstring::npos) {
                return 0;
            }
            AudioHandle handle = next_handle_;
            if (handle == 0U || handle == std::numeric_limits<AudioHandle>::max()) {
                handle = 1U;
            } else {
                ++handle;
            }
            if (tracks_.contains(handle)) return 0;
            next_handle_ = handle;
            alias = L"cwx_audio_" + std::to_wstring(backend_id_) + L"_" + std::to_wstring(handle);
            const std::wstring type = mci_type_for(source, source.path);
            std::wstring open_command = L"open \"" + filename + L"\"";
            if (!type.empty()) open_command += L" type " + type;
            open_command += L" alias " + alias;
            if (!mci_command(open_command, nullptr, nullptr, primitive_)) {
                if (type.empty()) return 0;
                close_alias(alias, primitive_);
                if (!mci_command(L"open \"" + filename + L"\" alias " + alias,
                                 nullptr, nullptr, primitive_)) return 0;
            }
            opened = true;
            if (!mci_command(L"set " + alias + L" time format milliseconds",
                             nullptr, nullptr, primitive_)) {
                close_alias(alias, primitive_);
                return 0;
            }
            const auto device_id = mci_device_id(alias, primitive_);
            if (device_id == 0U) {
                close_alias(alias, primitive_);
                return 0;
            }

            const auto duration = mci_time(alias, L"length", primitive_);
            const auto requested_start = std::max(options.start_position, std::chrono::milliseconds{});
            const auto start = duration.count() == 0 ? requested_start : std::min(requested_start, duration);
            WindowsAudioTrack track;
            track.alias = alias;
            track.device_id = device_id;
            track.bus = options.bus;
            track.duration = duration;
            // D版のMCI通知処理と同様、再生し直す前に必ずゼロへシークする。 ゼ
            // ロ以外の loop_start 情報はBASS・SDL経路で扱う。
            track.loop_start = std::chrono::milliseconds{};
            track.repeat_forever = options.loop_count == 0U ||
                                   options.loop_count == std::numeric_limits<std::uint32_t>::max();
            track.repetitions_remaining = track.repeat_forever || options.loop_count <= 1U
                                              ? 0U
                                              : options.loop_count - 1U;
            const bool was_inserted =
                tracks_.emplace(handle, std::move(track)).second;
            if (!was_inserted || !start_track(lock, start, handle)) {
                close_alias(alias, primitive_);
                tracks_.erase(handle);
                return 0;
            }
            return handle;
        } catch (...) {
            if (opened) close_alias(alias, primitive_);
            return 0;
        }
    }

    void pause(AudioHandle handle) override {
        std::lock_guard lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found != tracks_.end())
            (void)mci_command(L"pause " + found->second.alias, nullptr, nullptr,
                              primitive_);
    }

    void resume(AudioHandle handle) override {
        std::lock_guard lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found != tracks_.end())
            (void)mci_command(L"resume " + found->second.alias, nullptr, nullptr,
                              primitive_);
    }

    void stop(AudioHandle handle, std::chrono::milliseconds fade_out = {}) noexcept override {
        (void)fade_out;
        try {
            std::lock_guard lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            close_alias(found->second.alias, primitive_);
            tracks_.erase(found);
        } catch (...) {
        }
    }

    void stop_bus(AudioBus bus) noexcept override {
        try {
            std::lock_guard lock(mutex_);
            for (auto found = tracks_.begin(); found != tracks_.end();) {
                if (found->second.bus != bus) {
                    ++found;
                    continue;
                }
                close_alias(found->second.alias, primitive_);
                found = tracks_.erase(found);
            }
        } catch (...) {
        }
    }

    void stop_all() noexcept override {
        try {
            std::lock_guard lock(mutex_);
            for (const auto& [handle, track] : tracks_) {
                (void)handle;
                close_alias(track.alias, primitive_);
            }
            tracks_.clear();
        } catch (...) {
        }
    }

    void seek(AudioHandle handle, std::chrono::milliseconds position) override {
        std::unique_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end() || position.count() < 0) return;
        const auto bounded = found->second.duration.count() == 0
                                 ? position
                                 : std::min(position, found->second.duration);
        const std::wstring mode = mci_mode(found->second.alias, primitive_);
        if (!mci_command(L"seek " + found->second.alias + L" to " + std::to_wstring(bounded.count()),
                         nullptr, nullptr, primitive_)) return;
        if (mode == L"playing") {
            (void)start_track(lock, bounded, handle);
        } else if (mode == L"paused") {
            const auto alias = found->second.alias;
            if (start_track(lock, bounded, handle))
                (void)mci_command(L"pause " + alias, nullptr, nullptr,
                                  primitive_);
        }
    }

    [[nodiscard]] std::chrono::milliseconds position(AudioHandle handle) const override {
        std::lock_guard lock(mutex_);
        const auto found = tracks_.find(handle);
        return found == tracks_.end() ? std::chrono::milliseconds{} :
            mci_time(found->second.alias, L"position", primitive_);
    }

    [[nodiscard]] std::chrono::milliseconds duration(AudioHandle handle) const override {
        std::lock_guard lock(mutex_);
        const auto found = tracks_.find(handle);
        return found == tracks_.end() ? std::chrono::milliseconds{} : found->second.duration;
    }

    [[nodiscard]] bool playing(AudioHandle handle) const override {
        std::lock_guard lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end()) return false;
        // D版と同様、MCIのstopped以外の全モードを
        // 再生中とする。 停止済みのトラックは、成功の完了
        // メッセージがワーカーで未処理の間だけ有効に保つ。
        return (!found->second.terminal_stopped &&
                mci_mode(found->second.alias, primitive_) != L"stopped") ||
               repeat_pending(found->second);
    }

    // テスト用の処理も、通知方針を変えたり製品の間隔を待
    // ったりせず、同じワーカーのメッセージ処理を起こす。
    [[nodiscard]] bool advance_monitor_for_testing() noexcept {
        try {
            const auto id = next_test_barrier_.fetch_add(1U,
                                                         std::memory_order_relaxed);
            auto barrier = std::make_shared<NotificationBarrier>();
            {
                std::lock_guard lock(mutex_);
                testing_barriers_.emplace(id, barrier);
            }
            HWND window{};
            {
                std::lock_guard lock(notification_ready_mutex_);
                window = notification_window_;
            }
            if (window == nullptr ||
                !PostMessageW(window, kWakeNotificationWorkerMessage,
                              static_cast<WPARAM>(id), 0)) {
                std::lock_guard lock(mutex_);
                testing_barriers_.erase(id);
                return false;
            }
            std::unique_lock barrier_lock(barrier->mutex);
            return barrier->ready.wait_for(barrier_lock, std::chrono::seconds(2),
                                           [&] { return barrier->complete; });
        } catch (...) {
            return false;
        }
    }

    // ここではMCIの音量は実装しない。D版はMPEGVideoの異常終了に関す
    // る警告後、setaudioを無効にしている。 またWindowsの音量フラ
    // グはデジタルビデオ機器用なので、BASS・SDLは別の音量経路を維持する。
    void set_bus_volume(AudioBus bus, float value) noexcept override {
        (void)bus;
        (void)value;
    }

    void set_track_volume(AudioHandle handle, float value) noexcept override {
        (void)handle;
        (void)value;
    }

    [[nodiscard]] std::vector<std::string> supported_formats() const override {
        return {"wav", "mp3", "mid", "midi"};
    }

private:
    void run_mci_command_owner(std::stop_token token) noexcept {
        // 構築途中の巻き戻しでも起こす。 jthread のデストラクターが、バ
        // ックエンドのデストラクターの通知より先に停止を要求する場合があるため。
        std::stop_callback wake_on_stop(token, [this] { command_queue_ready_.notify_all(); });
        command_owner_thread_id_.store(GetCurrentThreadId(),
                                       std::memory_order_release);
        for (;;) {
            std::shared_ptr<MciCommandRequest> request;
            {
                std::unique_lock lock(command_queue_mutex_);
                command_queue_ready_.wait(lock, [&] {
                    return token.stop_requested() || !command_queue_.empty();
                });
                if (command_queue_.empty()) {
                    if (token.stop_requested()) break;
                    continue;
                }
                request = std::move(command_queue_.front());
                command_queue_.pop_front();
            }

            try {
                request->result = request->operation();
            } catch (...) {
                request->error = std::current_exception();
            }
            {
                std::lock_guard request_lock(request->mutex);
                request->complete = true;
            }
            request->ready.notify_all();
        }
        command_owner_thread_id_.store(0U, std::memory_order_release);
    }

    [[nodiscard]] std::uint32_t dispatch_mci_command(
        std::function<std::uint32_t()> operation) {
        // WinMMの実装によっては、MCI別名が開いたスレッドに結び付く。 す
        // べての送信と機器ID取得をこの所有スレッドで行う。 mutex_ は取
        // 得しないため、呼び出し元はバックエンドの状態をロックしたまま待てる。
        if (command_owner_thread_id_.load(std::memory_order_acquire) ==
            GetCurrentThreadId())
            return operation();

        auto request = std::make_shared<MciCommandRequest>();
        request->operation = std::move(operation);
        {
            std::lock_guard lock(command_queue_mutex_);
            if (command_worker_.get_stop_token().stop_requested())
                throw std::runtime_error("WinMM command owner is stopping");
            command_queue_.push_back(request);
        }
        command_queue_ready_.notify_one();

        std::unique_lock request_lock(request->mutex);
        request->ready.wait(request_lock, [&] { return request->complete; });
        const auto error = request->error;
        const auto result = request->result;
        request_lock.unlock();
        if (error) std::rethrow_exception(error);
        return result;
    }

    [[nodiscard]] static bool repeat_pending(
        const WindowsAudioTrack& track) noexcept {
        return !track.repeat_failed &&
               (track.repeat_forever || track.repetitions_remaining != 0U);
    }

    static void close_alias(
        const std::wstring& alias,
        const testing::MciCommandPrimitive& primitive) noexcept {
        try {
            (void)mci_command(L"stop " + alias, nullptr, nullptr, primitive);
            (void)mci_command(L"close " + alias, nullptr, nullptr, primitive);
        } catch (...) {
        }
    }

    [[nodiscard]] bool start_track(std::unique_lock<std::mutex>& lock,
                                   std::chrono::milliseconds start,
                                   AudioHandle handle) noexcept {
        std::shared_ptr<MciNotificationRequest> request;
        std::uint64_t previous_generation{};
        std::uint64_t generation{};
        bool lock_released{};
        try {
            auto found = tracks_.find(handle);
            if (found == tracks_.end()) return false;
            const auto alias = found->second.alias;
            if (start.count() == 0 &&
                !mci_command(L"seek " + alias + L" to 0", nullptr, nullptr,
                             primitive_)) return false;
            std::wstring command = L"play " + alias;
            if (start.count() > 0) command += L" from " + std::to_wstring(start.count());
            command += L" notify";

            previous_generation = found->second.play_generation;
            const auto generation_base = std::max(
                previous_generation, found->second.pending_play_generation);
            generation = generation_base + 1U;
            if (generation == 0U) generation = 1U;
            found->second.pending_play_generation = generation;

            request = std::make_shared<MciNotificationRequest>();
            request->owner = this;
            request->handle = handle;
            request->generation = generation;
            request->device_id = found->second.device_id;

            // 通知用 HWND はメッセージ処理のスレッド上で作る。
            // そのスレッドが要求を処理する間は経路のロックを外し
            // 、 pending_play_generation
            // で、準備中のシークより古い通知が優先されるのを防ぐ。
            const bool on_worker = is_notification_worker_thread();
            if (!on_worker) {
                lock.unlock();
                lock_released = true;
            }
            const bool window_ready = create_notification_request_window(request);
            if (!on_worker) {
                lock.lock();
                lock_released = false;
            }

            if (window_ready && request->callback_window != nullptr) {
                const auto [window_entry, inserted] =
                    notification_requests_.emplace(request->callback_window,
                                                   request);
                static_cast<void>(window_entry);
                if (!inserted) {
                    request->cancelled.store(true, std::memory_order_release);
                    schedule_destroy_notification_request(
                        request->callback_window);
                    return false;
                }
            }
            found = tracks_.find(handle);
            if (found == tracks_.end() ||
                found->second.play_generation != previous_generation ||
                found->second.pending_play_generation != generation) {
                request->cancelled.store(true, std::memory_order_release);
                schedule_destroy_notification_request(request->callback_window);
                return false;
            }
            auto& track = found->second;
            if (!window_ready || request->callback_window == nullptr) {
                track.pending_play_generation = 0U;
                request->cancelled.store(true, std::memory_order_release);
                schedule_destroy_notification_request(request->callback_window);
                return false;
            }

            if (!mci_command(command, nullptr, nullptr, primitive_,
                             reinterpret_cast<std::uintptr_t>(request->callback_window))) {
                track.pending_play_generation = 0U;
                request->cancelled.store(true, std::memory_order_release);
                schedule_destroy_notification_request(request->callback_window);
                return false;
            }
            track.play_generation = generation;
            track.pending_play_generation = 0U;
            track.repeat_failed = false;
            track.terminal_stopped = false;
            return true;
        } catch (...) {
            if (lock_released) {
                try { lock.lock(); }
                catch (...) { return false; }
            }
            const auto found = tracks_.find(handle);
            if (found != tracks_.end() &&
                found->second.pending_play_generation == generation)
                found->second.pending_play_generation = 0U;
            if (request) {
                request->cancelled.store(true, std::memory_order_release);
                schedule_destroy_notification_request(request->callback_window);
            }
            return false;
        }
    }

    static constexpr UINT kQueueMciNotifyMessage = WM_APP + 0x46AU;
    static constexpr UINT kWakeNotificationWorkerMessage = WM_APP + 0x46BU;
    static constexpr wchar_t kNotificationWindowClass[] = L"CardWirthC.MciNotifyWindow";
    static constexpr UINT kCreateNotificationRequestWindowMessage = WM_APP + 0x46CU;
    static constexpr UINT kDestroyNotificationRequestWindowMessage = WM_APP + 0x46DU;
    static constexpr wchar_t kNotificationRequestWindowClass[] =
        L"CardWirthC.MciNotifyRequestWindow";

    [[nodiscard]] static LRESULT CALLBACK notification_window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = reinterpret_cast<WindowsAudioBackend*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<WindowsAudioBackend*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr) {
            if (message == kCreateNotificationRequestWindowMessage) {
                auto* holder = reinterpret_cast<
                    std::shared_ptr<MciNotificationRequest>*>(lparam);
                if (holder == nullptr) return 0;
                std::shared_ptr<MciNotificationRequest> request = std::move(*holder);
                delete holder;
                const HWND callback_window =
                    create_callback_window_on_worker(request);
                {
                    std::lock_guard lock(request->creation_mutex);
                    request->callback_window = callback_window;
                    request->creation_complete = true;
                }
                request->created.notify_all();
                if (callback_window != nullptr &&
                    request->cancelled.load(std::memory_order_acquire))
                    DestroyWindow(callback_window);
                return 0;
            }
            if (message == kDestroyNotificationRequestWindowMessage) {
                const HWND callback_window = reinterpret_cast<HWND>(lparam);
                if (callback_window != nullptr)
                    self->destroy_notification_request_on_worker(callback_window);
                return 0;
            }
            if (message == kWakeNotificationWorkerMessage) {
                if (wparam != 0U)
                    self->queue_test_barrier(static_cast<std::uint64_t>(wparam));
                return 0;
            }
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    [[nodiscard]] static LRESULT CALLBACK notification_request_window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* request_holder = reinterpret_cast<
            std::shared_ptr<MciNotificationRequest>*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            auto* const context = static_cast<MciNotificationWindowCreateContext*>(
                create->lpCreateParams);
            if (context == nullptr || context->owner == nullptr) return FALSE;
            request_holder = context->owner;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(request_holder));
            context->transferred = true;
            return TRUE;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete request_holder;
            return DefWindowProcW(window, message, wparam, lparam);
        }
        if (request_holder != nullptr && *request_holder) {
            const auto request = *request_holder;
            if (request->owner != nullptr &&
                !request->cancelled.load(std::memory_order_acquire)) {
                if (message == MM_MCINOTIFY) {
                    // 経路ロック中の再生命令でもMCIが通知する場合があるため、
                    // バックエンドの状態を取得する前にメッセージを送り直す。
                    (void)PostMessageW(window, kQueueMciNotifyMessage, wparam,
                                       lparam);
                    return 0;
                }
                if (message == kQueueMciNotifyMessage) {
                    request->owner->enqueue_notification(
                        request, static_cast<std::uint32_t>(wparam),
                        static_cast<std::uint32_t>(lparam));
                    return 0;
                }
            }
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    [[nodiscard]] static HWND create_callback_window_on_worker(
        const std::shared_ptr<MciNotificationRequest>& request) noexcept {
        auto* const owner = new (std::nothrow)
            std::shared_ptr<MciNotificationRequest>(request);
        if (owner == nullptr) return nullptr;
        MciNotificationWindowCreateContext context{owner, false};
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const HWND window = CreateWindowExW(
            0, kNotificationRequestWindowClass, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, instance, &context);
        if (window == nullptr && !context.transferred) delete owner;
        return window;
    }

    [[nodiscard]] static HWND create_notification_window(
        WindowsAudioBackend* self) noexcept {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW request_class{};
        request_class.cbSize = sizeof(request_class);
        request_class.lpfnWndProc = notification_request_window_proc;
        request_class.hInstance = instance;
        request_class.lpszClassName = kNotificationRequestWindowClass;
        if (RegisterClassExW(&request_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = notification_window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kNotificationWindowClass;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        return CreateWindowExW(0, kNotificationWindowClass, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, instance, self);
    }

    [[nodiscard]] bool is_notification_worker_thread() const noexcept {
        std::lock_guard lock(notification_ready_mutex_);
        return notification_worker_thread_id_ != 0U &&
               notification_worker_thread_id_ == GetCurrentThreadId();
    }

    [[nodiscard]] bool create_notification_request_window(
        const std::shared_ptr<MciNotificationRequest>& request) noexcept {
        HWND worker_window{};
        DWORD worker_thread{};
        {
            std::lock_guard lock(notification_ready_mutex_);
            worker_window = notification_window_;
            worker_thread = notification_worker_thread_id_;
        }
        if (worker_window == nullptr || worker_thread == 0U) return false;
        if (GetCurrentThreadId() == worker_thread) {
            const HWND callback_window =
                create_callback_window_on_worker(request);
            {
                std::lock_guard lock(request->creation_mutex);
                request->callback_window = callback_window;
                request->creation_complete = true;
            }
            request->created.notify_all();
            return callback_window != nullptr;
        }

        auto* const holder = new (std::nothrow)
            std::shared_ptr<MciNotificationRequest>(request);
        if (holder == nullptr) return false;
        if (!PostMessageW(worker_window,
                          kCreateNotificationRequestWindowMessage, 0,
                          reinterpret_cast<LPARAM>(holder))) {
            delete holder;
            return false;
        }
        std::unique_lock creation_lock(request->creation_mutex);
        if (!request->created.wait_for(
                creation_lock, std::chrono::seconds(5),
                [&] { return request->creation_complete; })) {
            request->cancelled.store(true, std::memory_order_release);
            const HWND callback_window = request->callback_window;
            creation_lock.unlock();
            schedule_destroy_notification_request(callback_window);
            return false;
        }
        return request->callback_window != nullptr;
    }

    void schedule_destroy_notification_request(HWND callback_window) noexcept {
        if (callback_window == nullptr) return;
        HWND worker_window{};
        DWORD worker_thread{};
        {
            std::lock_guard lock(notification_ready_mutex_);
            worker_window = notification_window_;
            worker_thread = notification_worker_thread_id_;
        }
        if (worker_thread != 0U && GetCurrentThreadId() == worker_thread) {
            const auto found = notification_requests_.find(callback_window);
            if (found == notification_requests_.end())
                DestroyWindow(callback_window);
            else
                retire_notification_request_locked(found->second);
        } else if (worker_window != nullptr) {
            (void)PostMessageW(worker_window,
                               kDestroyNotificationRequestWindowMessage, 0,
                               reinterpret_cast<LPARAM>(callback_window));
        }
    }

    void destroy_notification_request_on_worker(HWND callback_window) noexcept {
        try {
            std::lock_guard lock(mutex_);
            const auto found = notification_requests_.find(callback_window);
            if (found == notification_requests_.end())
                DestroyWindow(callback_window);
            else
                retire_notification_request_locked(found->second);
        } catch (...) {
            DestroyWindow(callback_window);
        }
    }

    void wake_notification_worker() noexcept {
        HWND window{};
        {
            std::lock_guard lock(notification_ready_mutex_);
            window = notification_window_;
        }
        if (window != nullptr)
            (void)PostMessageW(window, kWakeNotificationWorkerMessage, 0, 0);
    }

    void publish_notification_worker_state(HWND window, bool ready) noexcept {
        {
            std::lock_guard lock(notification_ready_mutex_);
            notification_window_ = window;
            notification_window_ready_ = ready;
            notification_worker_thread_id_ = ready ? GetCurrentThreadId() : 0U;
            notification_worker_ready_ = true;
        }
        notification_ready_.notify_all();
    }

    void enqueue_notification(
        const std::shared_ptr<MciNotificationRequest>& request,
        std::uint32_t result, std::uint32_t device_id) noexcept {
        if (!request || request->device_id != device_id ||
            request->cancelled.load(std::memory_order_acquire)) return;
        bool expected = false;
        if (!request->event_queued.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) return;
        try {
            std::lock_guard lock(mutex_);
            if (request->cancelled.load(std::memory_order_acquire)) return;
            const auto found = notification_requests_.find(
                request->callback_window);
            if (found == notification_requests_.end() ||
                found->second.get() != request.get()) return;
            notifications_.push_back({request, result});
        } catch (...) {
        }
    }

    void retire_notification_request_locked(
        std::shared_ptr<MciNotificationRequest> request) noexcept {
        if (!request) return;
        request->cancelled.store(true, std::memory_order_release);
        const HWND callback_window = request->callback_window;
        if (callback_window == nullptr) return;
        notification_requests_.erase(callback_window);
        request->callback_window = nullptr;
        // ワーカーがイベントを消費した後で呼ぶため、要求
        // 用 HWND は作成したスレッドで破棄される。
        DestroyWindow(callback_window);
    }

    void destroy_remaining_notification_windows() noexcept {
        if (!is_notification_worker_thread()) return;
        try {
            std::lock_guard lock(mutex_);
            while (!notification_requests_.empty()) {
                auto found = notification_requests_.begin();
                const HWND callback_window = found->first;
                const auto request = found->second;
                request->cancelled.store(true, std::memory_order_release);
                notification_requests_.erase(found);
                request->callback_window = nullptr;
                if (callback_window != nullptr) DestroyWindow(callback_window);
            }
        } catch (...) {
        }
    }

    void queue_test_barrier(std::uint64_t id) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (testing_barriers_.contains(id)) pending_test_barriers_.push_back(id);
        } catch (...) {
        }
    }

    void complete_test_barriers() noexcept {
        std::vector<std::shared_ptr<NotificationBarrier>> completed;
        try {
            {
                std::lock_guard lock(mutex_);
                for (const auto id : pending_test_barriers_) {
                    const auto found = testing_barriers_.find(id);
                    if (found == testing_barriers_.end()) continue;
                    completed.push_back(found->second);
                    testing_barriers_.erase(found);
                }
                pending_test_barriers_.clear();
            }
            for (const auto& barrier : completed) {
                {
                    std::lock_guard lock(barrier->mutex);
                    barrier->complete = true;
                }
                barrier->ready.notify_all();
            }
        } catch (...) {
        }
    }

    void process_notifications() noexcept {
        try {
            {
                std::unique_lock lock(mutex_);
                while (!notifications_.empty()) {
                    const auto event = notifications_.front();
                    notifications_.pop_front();
                    const auto request = event.request;
                    if (!request) continue;
                    const auto found = tracks_.find(request->handle);
                    if (found != tracks_.end() &&
                        found->second.play_generation == request->generation &&
                        found->second.pending_play_generation == 0U) {
                        auto& track = found->second;
                        if (event.result != MCI_NOTIFY_SUCCESSFUL) {
                            // D版と同様、失敗・中止・後続に置換された結果は
                            // 無視し、成功した結果だけに反復方針を適用する。
                            track.repeat_failed = true;
                        } else if (!track.terminal_stopped) {
                            if (repeat_pending(track)) {
                                if (start_track(lock, track.loop_start,
                                                request->handle)) {
                                    if (!track.repeat_forever)
                                        --track.repetitions_remaining;
                                } else {
                                    const auto active = tracks_.find(request->handle);
                                    if (active != tracks_.end()) {
                                        active->second.repeat_failed = true;
                                        if (!active->second.terminal_stopped) {
                                            (void)mci_command(
                                                L"stop " + active->second.alias,
                                                nullptr, nullptr, primitive_);
                                            active->second.terminal_stopped = true;
                                        }
                                    }
                                }
                            } else {
                                (void)mci_command(L"stop " + track.alias, nullptr,
                                                  nullptr, primitive_);
                                track.terminal_stopped = true;
                            }
                        }
                    }
                    retire_notification_request_locked(request);
                }
            }
        } catch (...) {
        }
        complete_test_barriers();
    }

    void monitor(std::stop_token token) noexcept {
        HWND window = create_notification_window(this);
        publish_notification_worker_state(window, window != nullptr);
        if (window == nullptr) return;
        try {
            while (!token.stop_requested()) {
                const auto timeout = static_cast<DWORD>(std::clamp<std::int64_t>(
                    monitor_interval_.count(), 1, 60000));
                const DWORD wait = MsgWaitForMultipleObjectsEx(
                    0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (wait == WAIT_FAILED) break;
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (!token.stop_requested()) process_notifications();
            }
        } catch (...) {
        }
        // 破棄前に停止・終了処理がすべての有効な経路を除去済
        // み。 通知用 HWND が有効なうちにワーカー宛て
        // 通知を処理し、その後で所有スレッド上で破棄する。
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        process_notifications();
        destroy_remaining_notification_windows();
        DestroyWindow(window);
        publish_notification_worker_state(nullptr, false);
    }

    std::uint64_t backend_id_{};
    testing::MciCommandPrimitive primitive_;
    std::chrono::milliseconds monitor_interval_{};
    mutable std::mutex mutex_;
    mutable std::mutex notification_ready_mutex_;
    std::condition_variable notification_ready_;
    HWND notification_window_{};
    DWORD notification_worker_thread_id_{};
    bool notification_worker_ready_{};
    bool notification_window_ready_{};
    std::unordered_map<AudioHandle, WindowsAudioTrack> tracks_;
    std::unordered_map<HWND, std::shared_ptr<MciNotificationRequest>>
        notification_requests_;
    std::deque<MciNotificationEvent> notifications_;
    std::unordered_map<std::uint64_t, std::shared_ptr<NotificationBarrier>>
        testing_barriers_;
    std::vector<std::uint64_t> pending_test_barriers_;
    std::atomic<std::uint64_t> next_test_barrier_{1U};
    AudioHandle next_handle_{};
    bool initialized_{};
    AudioConfigurationResult configuration_{};
    std::mutex command_queue_mutex_;
    std::condition_variable command_queue_ready_;
    std::deque<std::shared_ptr<MciCommandRequest>> command_queue_;
    std::atomic<DWORD> command_owner_thread_id_{};
    std::jthread command_worker_;
    std::jthread worker_;
    inline static std::atomic<std::uint64_t> next_backend_id_{1U};
};

// BASSは意図的に実行時に解決する。単独エディタは任意のDLLが
// なくても導入可能にし、 Windowsで bass.dll/bassmidi.dll
// を同梱する場合は、元エディタのMIDI・サウンドフォ
// ント・ メモリ音源・パン・フェードの動作を維持する。
class WindowsBassAudioBackend final : public AudioBackend {
    using NativeHandle = std::uint32_t;
    using BassWord = std::uint32_t;
    using BassQword = std::uint64_t;

    static constexpr BassWord kUnicode = 0x80000000U;
    static constexpr BassWord kStreamStopBack = 0x00080000U;
    static constexpr BassWord kStreamPosReset = 0x00008000U;
    static constexpr BassWord kStreamPrescan = 0x00020000U;
    static constexpr BassWord kPosByte = 0U;
    static constexpr BassWord kAttribPan = 3U;
    static constexpr BassWord kAttribVolume = 2U;
    static constexpr BassWord kActiveStopped = 0U;
    static constexpr BassWord kBassTagOgg = 2U;
    static constexpr BassWord kMidiEventControl = 64U;
    static constexpr BassWord kSample8Bits = 1U;
    static constexpr BassWord kSampleFloat = 0x100U;

    struct MidiFont final {
        NativeHandle font{};
        int preset{-1};
        int bank{-1};
    };

    struct Api final {
        HMODULE bass{};
        HMODULE midi{};
        bool initialized{};
        std::optional<std::filesystem::path> loaded_root;

        using Init = int(WINAPI*)(int, BassWord, BassWord, HWND, const void*);
        using Free = int(WINAPI*)();
        using Start = int(WINAPI*)();
        using StreamCreateFile = NativeHandle(WINAPI*)(int, const void*, BassQword, BassQword,
                                                        BassWord);
        using MidiStreamCreateFile = NativeHandle(WINAPI*)(int, const void*, BassQword,
                                                             BassQword, BassWord, BassWord);
        using MidiFontInit = NativeHandle(WINAPI*)(const void*, BassWord);
        using MidiFontFree = int(WINAPI*)(NativeHandle);
        using MidiFontSetVolume = int(WINAPI*)(NativeHandle, float);
        using MidiStreamSetFonts = int(WINAPI*)(NativeHandle, const MidiFont*, BassWord);
        using StreamFree = int(WINAPI*)(NativeHandle);
        using ChannelPlay = int(WINAPI*)(NativeHandle, int);
        using ChannelPause = int(WINAPI*)(NativeHandle);
        using ChannelStop = int(WINAPI*)(NativeHandle);
        using ChannelSetAttribute = int(WINAPI*)(NativeHandle, BassWord, float);
        using ChannelSlideAttribute = int(WINAPI*)(NativeHandle, BassWord, float, BassWord);
        using ChannelSetPosition = int(WINAPI*)(NativeHandle, BassQword, BassWord);
        using ChannelGetPosition = BassQword(WINAPI*)(NativeHandle, BassWord);
        using ChannelGetLength = BassQword(WINAPI*)(NativeHandle, BassWord);
        using ChannelBytes2Seconds = double(WINAPI*)(NativeHandle, BassQword);
        using ChannelSeconds2Bytes = BassQword(WINAPI*)(NativeHandle, double);
        using ChannelIsActive = BassWord(WINAPI*)(NativeHandle);
        using SyncProc = void (CALLBACK*)(NativeHandle, NativeHandle, BassWord, void*);
        using ChannelSetSync = NativeHandle(WINAPI*)(NativeHandle, BassWord, BassQword,
                                                      SyncProc, void*);
        using ChannelRemoveSync = int(WINAPI*)(NativeHandle, NativeHandle);
        struct ChannelInfo {
            BassWord frequency{};
            BassWord channels{};
            BassWord flags{};
            BassWord type{};
            BassWord original_resolution{};
            NativeHandle plugin{};
            NativeHandle sample{};
            const char* filename{};
        };
        using ChannelGetInfo = int(WINAPI*)(NativeHandle, ChannelInfo*);
        using ChannelGetTags = const char*(WINAPI*)(NativeHandle, BassWord);
        struct MidiEvent {
            BassWord event{};
            BassWord param{};
            BassWord channel{};
            BassWord tick{};
            BassWord position{};
        };
        using MidiStreamGetEvents = BassWord(WINAPI*)(NativeHandle, int, BassWord, MidiEvent*);

        Init init{};
        Free free_library{};
        Start start{};
        StreamCreateFile stream_create_file{};
        MidiStreamCreateFile midi_stream_create_file{};
        MidiFontInit midi_font_init{};
        MidiFontFree midi_font_free{};
        MidiFontSetVolume midi_font_set_volume{};
        MidiStreamSetFonts midi_stream_set_fonts{};
        StreamFree stream_free{};
        ChannelPlay channel_play{};
        ChannelPause channel_pause{};
        ChannelStop channel_stop{};
        ChannelSetAttribute channel_set_attribute{};
        ChannelSlideAttribute channel_slide_attribute{};
        ChannelSetPosition channel_set_position{};
        ChannelGetPosition channel_get_position{};
        ChannelGetLength channel_get_length{};
        ChannelBytes2Seconds channel_bytes_to_seconds{};
        ChannelSeconds2Bytes channel_seconds_to_bytes{};
        ChannelIsActive channel_is_active{};
        ChannelSetSync channel_set_sync{};
        ChannelRemoveSync channel_remove_sync{};
        ChannelGetInfo channel_get_info{};
        ChannelGetTags channel_get_tags{};
        MidiStreamGetEvents midi_stream_get_events{};

        template <typename Function>
        static Function proc(HMODULE module, const char* name) {
            return module == nullptr ? nullptr : reinterpret_cast<Function>(GetProcAddress(module, name));
        }

        static std::vector<std::filesystem::path> candidate_directories(
            const std::span<const std::filesystem::path> roots) {
            std::vector<std::filesystem::path> result;
            const auto add = [&result](std::filesystem::path value) {
                if (value.empty()) return;
                value = value.lexically_normal();
                if (std::find(result.begin(), result.end(), value) == result.end())
                    result.push_back(std::move(value));
            };
            const auto architecture = sizeof(void*) == 8U ? L"x64" : L"x86";
            for (const auto& root : roots) {
                add(root);
                add(root / L"lib");
                add(root / L"lib" / architecture);
                add(root / architecture);
            }
            return result;
        }

        static HMODULE load_module(const wchar_t* name,
                                   const std::span<const std::filesystem::path> roots,
                                   std::optional<std::filesystem::path>& selected_root) {
            for (const auto& directory : candidate_directories(roots)) {
                const auto candidate = directory / name;
                if (HMODULE module = LoadLibraryW(candidate.c_str())) {
                    selected_root = directory;
                    return module;
                }
            }
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer,
                                                     static_cast<DWORD>(std::size(buffer)));
            if (length != 0U && length < std::size(buffer)) {
                const auto candidate = std::filesystem::path(buffer).parent_path() / name;
                if (HMODULE module = LoadLibraryW(candidate.c_str())) {
                    selected_root = candidate.parent_path();
                    return module;
                }
            }
            return LoadLibraryW(name);
        }

        bool load(const std::span<const std::filesystem::path> roots) {
            if (bass != nullptr) return true;
            bass = load_module(L"bass.dll", roots, loaded_root);
            if (bass == nullptr) return false;
            std::optional<std::filesystem::path> midi_root;
            const std::array preferred_root{loaded_root.value_or(std::filesystem::path{})};
            midi = load_module(L"bassmidi.dll", preferred_root, midi_root);
            init = proc<Init>(bass, "BASS_Init");
            free_library = proc<Free>(bass, "BASS_Free");
            start = proc<Start>(bass, "BASS_Start");
            stream_create_file = proc<StreamCreateFile>(bass, "BASS_StreamCreateFile");
            stream_free = proc<StreamFree>(bass, "BASS_StreamFree");
            channel_play = proc<ChannelPlay>(bass, "BASS_ChannelPlay");
            channel_pause = proc<ChannelPause>(bass, "BASS_ChannelPause");
            channel_stop = proc<ChannelStop>(bass, "BASS_ChannelStop");
            channel_set_attribute = proc<ChannelSetAttribute>(bass, "BASS_ChannelSetAttribute");
            channel_slide_attribute = proc<ChannelSlideAttribute>(bass, "BASS_ChannelSlideAttribute");
            channel_set_position = proc<ChannelSetPosition>(bass, "BASS_ChannelSetPosition");
            channel_get_position = proc<ChannelGetPosition>(bass, "BASS_ChannelGetPosition");
            channel_get_length = proc<ChannelGetLength>(bass, "BASS_ChannelGetLength");
            channel_bytes_to_seconds = proc<ChannelBytes2Seconds>(bass, "BASS_ChannelBytes2Seconds");
            channel_seconds_to_bytes = proc<ChannelSeconds2Bytes>(bass, "BASS_ChannelSeconds2Bytes");
            channel_is_active = proc<ChannelIsActive>(bass, "BASS_ChannelIsActive");
            channel_set_sync = proc<ChannelSetSync>(bass, "BASS_ChannelSetSync");
            channel_remove_sync = proc<ChannelRemoveSync>(bass, "BASS_ChannelRemoveSync");
            channel_get_info = proc<ChannelGetInfo>(bass, "BASS_ChannelGetInfo");
            channel_get_tags = proc<ChannelGetTags>(bass, "BASS_ChannelGetTags");
            if (midi != nullptr) {
                midi_stream_create_file = proc<MidiStreamCreateFile>(midi, "BASS_MIDI_StreamCreateFile");
                midi_stream_get_events = proc<MidiStreamGetEvents>(midi, "BASS_MIDI_StreamGetEvents");
                midi_font_init = proc<MidiFontInit>(midi, "BASS_MIDI_FontInit");
                midi_font_free = proc<MidiFontFree>(midi, "BASS_MIDI_FontFree");
                midi_font_set_volume = proc<MidiFontSetVolume>(midi, "BASS_MIDI_FontSetVolume");
                midi_stream_set_fonts = proc<MidiStreamSetFonts>(midi, "BASS_MIDI_StreamSetFonts");
            }
            if (init == nullptr || free_library == nullptr || stream_create_file == nullptr ||
                stream_free == nullptr || channel_play == nullptr || channel_stop == nullptr ||
                channel_set_attribute == nullptr || channel_set_position == nullptr ||
                channel_get_position == nullptr || channel_get_length == nullptr ||
                channel_bytes_to_seconds == nullptr || channel_seconds_to_bytes == nullptr ||
                channel_is_active == nullptr) {
                unload();
                return false;
            }
            return true;
        }

        void unload() noexcept {
            if (initialized && free_library != nullptr) (void)free_library();
            initialized = false;
            if (midi != nullptr) {
                FreeLibrary(midi);
                midi = nullptr;
            }
            if (bass != nullptr) {
                FreeLibrary(bass);
                bass = nullptr;
            }
            loaded_root.reset();
            *this = Api{};
        }
    };

    struct LoopSyncState final {
        Api::ChannelSetPosition set_position{};
        BassQword start_byte{};
        std::atomic<std::uint32_t> repetitions_remaining{};
        bool repeat_forever{};
        std::atomic<bool> closing{};
        std::atomic<std::uint32_t> active_callbacks{};
    };

    struct Track final {
        NativeHandle channel{};
        NativeHandle font{}; // 再生元ごとに明示されたサウンドフォントは、このストリームが所有する。
        std::vector<MidiFont> font_descriptors; // 明示的な記述情報を、ストリームとともに保持する。
        bool midi{};
        AudioBus bus{AudioBus::SoundEffect};
        float gain{1.0F};
        float pan{};
        std::chrono::milliseconds duration{};
        std::chrono::milliseconds loop_start{};
        std::uint32_t repetitions_remaining{};
        bool repeat_forever{};
        std::vector<NativeHandle> loop_sync_handles;
        std::uintptr_t loop_sync_token{};
        std::shared_ptr<LoopSyncState> loop_sync_state;
        bool fading_stop{};
        std::chrono::steady_clock::time_point stop_deadline{};
        std::vector<std::byte> encoded;
    };

public:
    WindowsBassAudioBackend() : worker_([this](std::stop_token token) { monitor(token); }) {}

    ~WindowsBassAudioBackend() override {
        stop_all();
        worker_.request_stop();
        wake_.notify_all();
        std::scoped_lock lock(mutex_);
        release_configured_sound_fonts_locked();
        api_.unload();
    }

    bool initialize(std::string& error) override {
        std::scoped_lock lock(mutex_);
        if (initialized_) {
            error.clear();
            return true;
        }
        if (!api_.load(configuration_request_.library_search_roots)) {
            error = "BASS DLL is not available";
            return false;
        }
        if (api_.init(-1, 44100U, 0U, nullptr, nullptr) == 0) {
            api_.unload();
            error = "BASS_Init failed";
            return false;
        }
        api_.initialized = true;
        if (api_.start != nullptr) (void)api_.start();
        initialized_ = true;
        error.clear();
        return true;
    }

    [[nodiscard]] bool defer_configuration(
        const AudioConfiguration& request) noexcept override {
        if (request.preference != AudioBackendPreference::Bass) return false;
        try {
            const std::filesystem::path directory = request.library_search_roots.empty()
                ? std::filesystem::path{} : request.library_search_roots.front();
            stop_all();
            {
                std::scoped_lock lock(mutex_);
                release_configured_sound_fonts_locked();
                api_.unload();
                initialized_ = false;
                configuration_request_ = request;
                configuration_ = {};
            }
            std::error_code bass_error;
            std::error_code midi_error;
            const bool files_present =
                std::filesystem::exists(directory / L"bass.dll", bass_error) &&
                !bass_error &&
                std::filesystem::exists(directory / L"bassmidi.dll", midi_error) &&
                !midi_error;
            return files_present;
        } catch (...) {
            return false;
        }
    }

    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        stop_all();
        {
            std::scoped_lock lock(mutex_);
            const bool keep_runtime = initialized_ && configuration_request_ == request;
            release_configured_sound_fonts_locked();
            // 遅延要求が、共通の initialize() ですでに同じ実行
            // 環境を初期化している場合がある。 二度目の BASS_Free/BASS_Init
            // を行わずにバンクを設定する。 明示的なバンク再読
            // み込みでは、下で全設定フォントを解放・再構築する。
            if (!keep_runtime) {
                api_.unload();
                initialized_ = false;
            }
            configuration_request_ = request;
        }
        std::string error;
        const bool ready = initialize(error);
        std::scoped_lock lock(mutex_);
        configuration_ = {};
        configuration_.requested = request.preference;
        configuration_.effective = AudioBackendPreference::Bass;
        configuration_.library_root = api_.loaded_root;
        configuration_.sound_fonts = request.sound_fonts;
        configuration_.capabilities = ready
            ? AudioCapability::Playback | AudioCapability::Native | AudioCapability::Bass |
                  AudioCapability::MemorySource | AudioCapability::Pan | AudioCapability::Fade
            : AudioCapability::None;
        if (ready && api_.midi_stream_create_file != nullptr) {
            configuration_.capabilities |= AudioCapability::Midi;
            if (load_configured_sound_fonts_locked())
                configuration_.capabilities |= AudioCapability::SoundFont;
        }
        if (!ready) configuration_.error = std::move(error);
        if (ready && !request.allow_fallback &&
            request.preference != AudioBackendPreference::Automatic &&
            request.preference != AudioBackendPreference::Bass) {
            configuration_.capabilities = AudioCapability::None;
            configuration_.error = "BASS does not provide the requested backend";
        }
        return configuration_;
    }

    [[nodiscard]] AudioConfigurationResult configuration() const override {
        std::scoped_lock lock(mutex_);
        return configuration_;
    }

    AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) override {
        std::scoped_lock lock(mutex_);
        if (!initialized_) return 0U;
        const bool midi = has_mthd_header(source);
        if (source.sound_font && !midi) return 0U;
        if (midi && !source.sound_font && configured_font_descriptors_.empty())
            return 0U;
        if (midi && api_.midi_stream_create_file == nullptr) return 0U;
        Track track;
        auto cleanup_track = [this](Track* value) noexcept {
            close_track(*value);
        };
        std::unique_ptr<Track, decltype(cleanup_track)> track_cleanup(
            &track, std::move(cleanup_track));
        NativeHandle channel = 0U;
        std::vector<std::byte> encoded;
        const void* pointer = nullptr;
        BassQword size = 0U;
        bool memory = false;
        std::wstring filename;
        if (!source.encoded_data.empty()) {
            encoded.assign(source.encoded_data.begin(), source.encoded_data.end());
            pointer = encoded.data();
            size = static_cast<BassQword>(encoded.size());
            memory = true;
        } else if (!source.path.empty()) {
            filename = source.path.wstring();
            if (filename.empty() || filename.find(L'\0') != std::wstring::npos) return 0U;
            pointer = filename.c_str();
        } else {
            return 0U;
        }

        const BassWord flags = kStreamStopBack | kStreamPosReset | kStreamPrescan |
                               kSampleFloat | (memory ? 0U : kUnicode);
        if (midi) {
            if (api_.midi_stream_create_file == nullptr) return 0U;
            channel = api_.midi_stream_create_file(memory ? 1 : 0, pointer, 0U, size, flags, 44100U);
        } else {
            channel = api_.stream_create_file(memory ? 1 : 0, pointer, 0U, size, flags);
        }
        if (channel == 0U) return 0U;

        track.channel = channel;
        if (midi && source.sound_font) {
            // 再生元ごとに明示されたサウンドフォントは、引き続きこのトラックが所有する。
            if (api_.midi_font_init == nullptr || api_.midi_stream_set_fonts == nullptr ||
                api_.midi_font_free == nullptr) {
                close_track(track);
                return 0U;
            }
            const auto font_path = resolve_sound_font(*source.sound_font);
            const std::wstring font_name = font_path.wstring();
            if (!font_name.empty()) track.font = api_.midi_font_init(font_name.c_str(), kUnicode);
            if (track.font == 0U) {
                close_track(track);
                return 0U;
            }
            track.font_descriptors.push_back(MidiFont{track.font, -1, -1});
            if (api_.midi_stream_set_fonts(
                    channel, track.font_descriptors.data(),
                    static_cast<BassWord>(track.font_descriptors.size())) == 0) {
                close_track(track);
                return 0U;
            }
        } else if (midi) {
            if (api_.midi_stream_set_fonts(
                    channel, configured_font_descriptors_.data(),
                    static_cast<BassWord>(configured_font_descriptors_.size())) == 0) {
                close_track(track);
                return 0U;
            }
        }

        const auto bytes = api_.channel_get_length(channel, kPosByte);
        const auto seconds = api_.channel_bytes_to_seconds(channel, bytes);
        const auto duration = std::isfinite(seconds) && seconds > 0.0
                                  ? std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0))
                                  : std::chrono::milliseconds{};
        track.midi = midi;
        track.bus = options.bus;
        track.gain = normalized_volume(options.volume);
        track.pan = std::isfinite(options.pan) ? std::clamp(options.pan, -1.0F, 1.0F) : 0.0F;
        track.duration = duration;
        track.loop_start = std::max(options.loop_start, std::chrono::milliseconds{});
        if (duration.count() > 0) track.loop_start = std::min(track.loop_start, duration);
        track.repeat_forever = options.loop_count == 0U ||
                               options.loop_count == std::numeric_limits<std::uint32_t>::max();
        track.repetitions_remaining = track.repeat_forever || options.loop_count <= 1U
                                          ? 0U
                                          : options.loop_count - 1U;
        const auto loop_plan = resolve_loop_plan(channel, midi, options);
        if (loop_plan) install_loop_syncs(track, *loop_plan);
        if (!track.loop_sync_handles.empty()) {
            track.repeat_forever = false;
            track.repetitions_remaining = 0U;
        }
        track.encoded = std::move(encoded);
        apply_attributes(track);
        const auto start = std::max(options.start_position, std::chrono::milliseconds{});
        if (duration.count() > 0 && start > duration) {
            close_track(track);
            return 0U;
        }
        if (start.count() > 0) {
            const auto position = api_.channel_seconds_to_bytes(channel, start.count() / 1000.0);
            if (api_.channel_set_position(channel, position, kPosByte) == 0) {
                close_track(track);
                return 0U;
            }
        }
        if (options.fade_in.count() > 0) {
            (void)api_.channel_set_attribute(channel, kAttribVolume, 0.0F);
        }
        if (api_.channel_play(channel, 0) == 0) {
            close_track(track);
            return 0U;
        }
        if (options.fade_in.count() > 0 && api_.channel_slide_attribute != nullptr) {
            const float target = effective_gain(track);
            (void)api_.channel_slide_attribute(channel, kAttribVolume, target,
                                               static_cast<BassWord>(std::min<std::int64_t>(
                                                   options.fade_in.count(), std::numeric_limits<BassWord>::max())));
        }
        AudioHandle handle = next_handle_++;
        if (handle == 0U) handle = next_handle_++;
        const auto [position, inserted] = tracks_.try_emplace(handle);
        if (!inserted) return 0U;
        // ALL・単独版とも、トラックのマップへ所有権を登
        // 録するまで、ローカルの後処理ガードを有効に保つ。
        position->second = std::move(track);
        track_cleanup.release();
        wake_.notify_all();
        return handle;
    }

    void pause(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (const auto found = tracks_.find(handle); found != tracks_.end())
            if (api_.channel_pause != nullptr) (void)api_.channel_pause(found->second.channel);
    }

    void resume(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (const auto found = tracks_.find(handle); found != tracks_.end())
            (void)api_.channel_play(found->second.channel, 0);
    }

    void stop(AudioHandle handle, std::chrono::milliseconds fade_out = {}) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            if (fade_out.count() <= 0 || api_.channel_slide_attribute == nullptr) {
                close_track(found->second);
                tracks_.erase(found);
                return;
            }
            const auto milliseconds = static_cast<BassWord>(std::min<std::int64_t>(
                fade_out.count(), std::numeric_limits<BassWord>::max()));
            (void)api_.channel_slide_attribute(found->second.channel, kAttribVolume, 0.0F,
                                               milliseconds);
            found->second.fading_stop = true;
            found->second.stop_deadline = std::chrono::steady_clock::now() + fade_out;
            wake_.notify_all();
        } catch (...) {
        }
    }

    void stop_bus(AudioBus bus) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            for (auto iterator = tracks_.begin(); iterator != tracks_.end();) {
                if (iterator->second.bus != bus) {
                    ++iterator;
                    continue;
                }
                close_track(iterator->second);
                iterator = tracks_.erase(iterator);
            }
        } catch (...) {
        }
    }

    void stop_all() noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            for (auto& [handle, track] : tracks_) {
                (void)handle;
                close_track(track);
            }
            tracks_.clear();
        } catch (...) {
        }
    }

    void seek(AudioHandle handle, std::chrono::milliseconds position) override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end() || position.count() < 0) return;
        const auto bounded = found->second.duration.count() > 0
                                 ? std::min(position, found->second.duration)
                                 : position;
        const auto bytes = api_.channel_seconds_to_bytes(found->second.channel,
                                                         bounded.count() / 1000.0);
        (void)api_.channel_set_position(found->second.channel, bytes, kPosByte);
    }

    [[nodiscard]] std::chrono::milliseconds position(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        if (found == tracks_.end()) return {};
        const auto seconds = api_.channel_bytes_to_seconds(
            found->second.channel, api_.channel_get_position(found->second.channel, kPosByte));
        return std::isfinite(seconds) && seconds >= 0.0
                   ? std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0))
                   : std::chrono::milliseconds{};
    }

    [[nodiscard]] std::chrono::milliseconds duration(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        return found == tracks_.end() ? std::chrono::milliseconds{} : found->second.duration;
    }

    [[nodiscard]] bool playing(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto found = tracks_.find(handle);
        return found != tracks_.end() && api_.channel_is_active(found->second.channel) != kActiveStopped;
    }

    void set_bus_volume(AudioBus bus, float value) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            bus_volumes_[audio_bus_index(bus)] = normalized_volume(value);
            for (auto& [handle, track] : tracks_) {
                (void)handle;
                if (track.bus == bus) apply_attributes(track);
            }
        } catch (...) {
        }
    }

    void cancel_fade_in(AudioHandle handle) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end() || api_.channel_slide_attribute == nullptr) return;
            (void)api_.channel_slide_attribute(found->second.channel, kAttribVolume,
                                               effective_gain(found->second), 0U);
        } catch (...) {
        }
    }

    void set_track_volume(AudioHandle handle, float value) noexcept override {
        try {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            found->second.gain = normalized_volume(value);
            apply_attributes(found->second);
        } catch (...) {
        }
    }

    [[nodiscard]] std::vector<std::string> supported_formats() const override {
        std::vector<std::string> formats{"wav", "mp3", "ogg", "flac", "mid", "midi", "mod", "xm", "it", "s3m"};
        if (api_.midi_stream_create_file == nullptr)
            formats.erase(std::remove(formats.begin(), formats.end(), "mid"), formats.end());
        if (api_.midi_stream_create_file == nullptr)
            formats.erase(std::remove(formats.begin(), formats.end(), "midi"), formats.end());
        return formats;
    }

private:

    [[nodiscard]] static bool has_mthd_header(const AudioSource& source) {
        constexpr std::string_view header{"MThd"};
        if (!source.encoded_data.empty()) {
            if (source.encoded_data.size() < header.size()) return false;
            const auto* data = reinterpret_cast<const char*>(source.encoded_data.data());
            return std::memcmp(data, header.data(), header.size()) == 0;
        }
        if (source.path.empty()) return false;
        std::ifstream input(source.path, std::ios::binary);
        std::array<char, 4U> actual{};
        input.read(actual.data(), static_cast<std::streamsize>(actual.size()));
        return input.gcount() == static_cast<std::streamsize>(actual.size()) &&
               std::memcmp(actual.data(), header.data(), header.size()) == 0;
    }

    [[nodiscard]] bool load_configured_sound_fonts_locked() {
        release_configured_sound_fonts_locked();
        if (configuration_request_.sound_fonts.empty() ||
            api_.midi_font_init == nullptr || api_.midi_font_free == nullptr ||
            api_.midi_font_set_volume == nullptr || api_.midi_stream_set_fonts == nullptr ||
            configuration_request_.sound_fonts.size() >
                static_cast<std::size_t>(std::numeric_limits<BassWord>::max()))
            return false;
        try {
            configured_sound_fonts_.reserve(configuration_request_.sound_fonts.size());
            configured_font_descriptors_.reserve(configuration_request_.sound_fonts.size());
            for (const auto& sound_font : configuration_request_.sound_fonts) {
                const auto path = resolve_sound_font(sound_font.path);
                const std::wstring font_name = path.wstring();
                if (font_name.empty()) continue;
                const NativeHandle handle = api_.midi_font_init(font_name.c_str(), kUnicode);
                if (handle == 0U) continue;
                configured_sound_fonts_.push_back(handle);
                // D版と同様、音量エラーはBASSが報告するが、有効なフォントは解放しない。
                (void)api_.midi_font_set_volume(
                    handle, static_cast<float>(sound_font.volume) / 100.0F);
                configured_font_descriptors_.push_back(MidiFont{handle, -1, 0});
            }
        } catch (...) {
            release_configured_sound_fonts_locked();
            return false;
        }
        return !configured_font_descriptors_.empty();
    }

    void release_configured_sound_fonts_locked() noexcept {
        if (api_.midi_font_free != nullptr) {
            for (const NativeHandle handle : configured_sound_fonts_)
                if (handle != 0U) (void)api_.midi_font_free(handle);
        }
        configured_sound_fonts_.clear();
        configured_font_descriptors_.clear();
    }

    [[nodiscard]] std::optional<windows_bass::LoopPlan> resolve_loop_plan(
        const NativeHandle channel, const bool midi,
        const AudioPlayOptions& options) const {
        if (!options.special_loop) return std::nullopt;
        if (api_.channel_get_info == nullptr) return std::nullopt;
        Api::ChannelInfo info{};
        if (api_.channel_get_info(channel, &info) == 0 || info.frequency == 0U ||
            info.frequency > static_cast<BassWord>(std::numeric_limits<int>::max()) ||
            info.channels == 0U)
            return std::nullopt;
        const std::uint32_t bytes_per_sample = (info.flags & kSampleFloat) != 0U ? 4U
            : (info.flags & kSample8Bits) != 0U ? 1U : 2U;

        std::vector<AudioMidiControlEvent> midi_events;
        std::vector<std::string_view> vorbis_comments;
        // D版と同様、有効な .sli のLinkから開始点が得られたら直
        // ちに返す。 その優先順を保ち、埋め込みのBASS情報を調べない。
        if (!options.loop_region) {
            if (midi && api_.midi_stream_get_events != nullptr) {
                const auto count = api_.midi_stream_get_events(
                    channel, -1, kMidiEventControl, nullptr);
                if (count != 0U) {
                    std::vector<Api::MidiEvent> native_events(count);
                    const auto copied = api_.midi_stream_get_events(
                        channel, -1, kMidiEventControl, native_events.data());
                    midi_events.reserve(std::min<std::size_t>(copied, native_events.size()));
                    for (std::size_t index = 0;
                         index < std::min<std::size_t>(copied, native_events.size()); ++index) {
                        midi_events.push_back({native_events[index].param & 0xffU,
                            static_cast<std::int64_t>(native_events[index].position)});
                    }
                }
            }

            if (api_.channel_get_tags != nullptr) {
                if (const char* tags = api_.channel_get_tags(channel, kBassTagOgg)) {
                    for (const char* current = tags; *current != '\0';) {
                        const std::string_view comment{current};
                        vorbis_comments.push_back(comment);
                        current += comment.size() + 1U;
                    }
                }
            }
        }
        return windows_bass::plan_loop(options, midi,
            {static_cast<int>(info.frequency), info.channels, bytes_per_sample},
            midi_events, vorbis_comments, api_.channel_set_sync != nullptr);
    }

    static void CALLBACK loop_sync_proc(
        NativeHandle, const NativeHandle channel, BassWord, void* user) noexcept {
        const auto token = reinterpret_cast<std::uintptr_t>(user);
        std::shared_ptr<LoopSyncState> state;
        {
            std::scoped_lock lock(loop_sync_states_mutex_);
            const auto found = loop_sync_states_.find(token);
            if (found == loop_sync_states_.end()) return;
            state = found->second;
            state->active_callbacks.fetch_add(1U, std::memory_order_acq_rel);
        }
        if (!state->closing.load(std::memory_order_acquire)) {
            bool repeat = state->repeat_forever;
            auto remaining = state->repetitions_remaining.load(std::memory_order_relaxed);
            while (!repeat && remaining != 0U) {
                if (state->repetitions_remaining.compare_exchange_weak(
                        remaining, remaining - 1U, std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    repeat = true;
                    break;
                }
            }
            if (repeat && state->set_position != nullptr)
                (void)state->set_position(channel, state->start_byte, kPosByte);
        }
        state->active_callbacks.fetch_sub(1U, std::memory_order_acq_rel);
    }

    void install_loop_syncs(Track& track, const windows_bass::LoopPlan& plan) {
        if (api_.channel_set_sync == nullptr || plan.registration_count == 0U) return;
        auto state = std::make_shared<LoopSyncState>();
        state->set_position = api_.channel_set_position;
        state->start_byte = plan.start_byte;
        state->repeat_forever = plan.repeat_forever;
        state->repetitions_remaining.store(plan.repetitions_remaining,
                                           std::memory_order_relaxed);
        track.loop_sync_handles.reserve(plan.registration_count);
        auto token = next_loop_sync_token_.fetch_add(1U, std::memory_order_relaxed);
        if (token == 0U)
            token = next_loop_sync_token_.fetch_add(1U, std::memory_order_relaxed);
        {
            std::scoped_lock lock(loop_sync_states_mutex_);
            loop_sync_states_.emplace(token, state);
        }
        track.loop_sync_state = state;
        track.loop_sync_token = token;
        const auto user = reinterpret_cast<void*>(token);
        for (std::size_t index = 0; index < plan.registration_count; ++index) {
            const auto& registration = plan.registrations[index];
            const auto sync = api_.channel_set_sync(
                track.channel, registration.flags, registration.position_bytes,
                &loop_sync_proc, user);
            if (sync != 0U) track.loop_sync_handles.push_back(sync);
        }
        if (track.loop_sync_handles.empty()) {
            std::scoped_lock lock(loop_sync_states_mutex_);
            loop_sync_states_.erase(token);
            track.loop_sync_state.reset();
            track.loop_sync_token = 0U;
        }
    }

    void release_loop_syncs(Track& track) const noexcept {
        const auto state = track.loop_sync_state;
        if (state) state->closing.store(true, std::memory_order_release);
        if (track.channel != 0U && api_.channel_remove_sync != nullptr) {
            for (const auto sync : track.loop_sync_handles)
                if (sync != 0U) (void)api_.channel_remove_sync(track.channel, sync);
        }
        if (track.loop_sync_token != 0U) {
            std::scoped_lock lock(loop_sync_states_mutex_);
            loop_sync_states_.erase(track.loop_sync_token);
        }
        while (state && state->active_callbacks.load(std::memory_order_acquire) != 0U)
            std::this_thread::yield();
        track.loop_sync_handles.clear();
        track.loop_sync_state.reset();
        track.loop_sync_token = 0U;
    }

    [[nodiscard]] std::filesystem::path resolve_sound_font(
        const std::filesystem::path& path) const {
        if (path.is_absolute()) return path;
        for (const auto& root : configuration_request_.library_search_roots) {
            for (const auto& candidate : {root / path, root / "SoundFont" / path})
                if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
        return path;
    }

    [[nodiscard]] float effective_gain(const Track& track) const noexcept {
        return normalized_volume(track.gain * bus_volumes_[audio_bus_index(track.bus)]);
    }

    void apply_attributes(const Track& track) const noexcept {
        if (api_.channel_set_attribute == nullptr) return;
        (void)api_.channel_set_attribute(track.channel, kAttribVolume, effective_gain(track));
        (void)api_.channel_set_attribute(track.channel, kAttribPan, track.pan);
    }

    void close_track(Track& track) const noexcept {
        if (track.channel != 0U) {
            release_loop_syncs(track);
            if (api_.channel_stop != nullptr) (void)api_.channel_stop(track.channel);
            if (api_.stream_free != nullptr) (void)api_.stream_free(track.channel);
        } else {
            release_loop_syncs(track);
        }
        if (api_.midi_font_free != nullptr) {
            if (track.font != 0U) (void)api_.midi_font_free(track.font);
        }
        track = {};
    }

    void monitor(std::stop_token token) noexcept {
        try {
            std::unique_lock lock(mutex_);
            while (!token.stop_requested()) {
                wake_.wait_for(lock, std::chrono::milliseconds(40), [&token] {
                    return token.stop_requested();
                });
                if (token.stop_requested()) break;
                const auto now = std::chrono::steady_clock::now();
                for (auto iterator = tracks_.begin(); iterator != tracks_.end();) {
                    auto& track = iterator->second;
                    if (track.fading_stop && now >= track.stop_deadline) {
                        close_track(track);
                        iterator = tracks_.erase(iterator);
                        continue;
                    }
                    if (!track.fading_stop && api_.channel_is_active(track.channel) == kActiveStopped) {
                        if (track.repeat_forever || track.repetitions_remaining != 0U) {
                            const auto position = api_.channel_seconds_to_bytes(
                                track.channel, track.loop_start.count() / 1000.0);
                            (void)api_.channel_set_position(track.channel, position, kPosByte);
                            if (api_.channel_play(track.channel, 0) != 0 &&
                                !track.repeat_forever && track.repetitions_remaining != 0U)
                                --track.repetitions_remaining;
                            ++iterator;
                        } else {
                            close_track(track);
                            iterator = tracks_.erase(iterator);
                        }
                    } else {
                        ++iterator;
                    }
                }
            }
        } catch (...) {
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Api api_;
    std::unordered_map<AudioHandle, Track> tracks_;
    std::array<float, 3> bus_volumes_{1.0F, 1.0F, 1.0F};
    AudioHandle next_handle_{1U};
    bool initialized_{};
    AudioConfiguration configuration_request_{};
    std::vector<NativeHandle> configured_sound_fonts_;
    std::vector<MidiFont> configured_font_descriptors_;
    AudioConfigurationResult configuration_{};
    std::jthread worker_;
    inline static std::atomic<std::uintptr_t> next_loop_sync_token_{1U};
    inline static std::mutex loop_sync_states_mutex_;
    inline static std::unordered_map<std::uintptr_t, std::shared_ptr<LoopSyncState>>
        loop_sync_states_;
};

class WindowsAudioRouter final : public AudioBackend {
public:
    WindowsAudioRouter(std::shared_ptr<AudioBackend> bass, std::shared_ptr<AudioBackend> mci)
        : bass_(std::move(bass)), mci_(std::move(mci)) {}

    bool initialize(std::string& error) override {
        std::string bass_error;
        std::string mci_error;
        bass_ready_ = bass_ && bass_->initialize(bass_error);
        mci_ready_ = mci_ && mci_->initialize(mci_error);
        if (bass_ready_ || mci_ready_) {
            error.clear();
            return true;
        }
        error = !bass_error.empty() ? bass_error : mci_error;
        return false;
    }

    [[nodiscard]] bool defer_configuration(
        const AudioConfiguration& request) noexcept override {
        if (request.preference != AudioBackendPreference::Bass || !bass_)
            return false;
        const bool files_present = bass_->defer_configuration(request);
        try {
            std::scoped_lock lock(mutex_);
            std::erase_if(tracks_, [this](const auto& entry) {
                return entry.second.backend == bass_;
            });
            bass_ready_ = false;
            configuration_request_ = request;
            configuration_ = {};
        } catch (...) {
            return false;
        }
        return files_present;
    }

    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        stop_all();
        std::array<AudioConfigurationResult, 2U> candidates{};
        candidates[0] = bass_ ? bass_->configure(request) : AudioConfigurationResult{};
        candidates[1] = mci_ ? mci_->configure(request) : AudioConfigurationResult{};
        bass_ready_ = candidates[0].available();
        mci_ready_ = candidates[1].available();
        auto selected = select_audio_configuration(request, candidates);
        std::scoped_lock lock(mutex_);
        configuration_request_ = request;
        configuration_ = std::move(selected);
        return configuration_;
    }

    [[nodiscard]] AudioConfigurationResult configuration() const override {
        std::scoped_lock lock(mutex_);
        return configuration_;
    }

    AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) override {
        std::shared_ptr<AudioBackend> selected;
        AudioHandle inner = 0U;
        bool bass_attempted = false;
        if (options.backend == AudioBackendPreference::Bass) {
            if (bass_ready_) {
                bass_attempted = true;
                inner = bass_->play(source, options);
                if (inner != 0U) selected = bass_;
            }
            if (inner == 0U && !configuration_request_.allow_fallback) return 0U;
        }
        // Windowsでは、メモリデータ、サウンドフォント指定、MCI
        // 外の形式を受け付けるのはBASSだけなので、読み込まれていれば
        // 優先する。 MCIは追加導入なしでWAV/MP3/MIDIを扱
        // う代替として残す。 明示的なBASSの失敗も、設定で許可され
        // ていればMCIへ進める。 一方、明示的なNativeはD版の
        // MCI選択を表すため、BASSがあっても、 保存済みのMainWindow
        // の代替フラグで要求されたMCIを上書きしない。
        if (inner == 0U && !bass_attempted && bass_ready_ &&
            options.backend != AudioBackendPreference::Native &&
            (configuration_request_.allow_fallback ||
             options.backend == AudioBackendPreference::Automatic ||
             options.backend == AudioBackendPreference::Bass)) {
            bass_attempted = true;
            inner = bass_->play(source, options);
            if (inner != 0U) selected = bass_;
        }
        const bool bass_only = !source.encoded_data.empty() || source.sound_font.has_value();
        if (inner == 0U && mci_ready_ && !bass_only &&
            (configuration_request_.allow_fallback ||
             options.backend == AudioBackendPreference::Automatic ||
             options.backend == AudioBackendPreference::Native)) {
            inner = mci_->play(source, options);
            selected = mci_;
        }
        if (inner == 0U || !selected) return 0U;
        std::scoped_lock lock(mutex_);
        AudioHandle handle = next_handle_++;
        if (handle == 0U) handle = next_handle_++;
        tracks_[handle] = Route{std::move(selected), inner, options.bus};
        return handle;
    }

    void pause(AudioHandle handle) override { with(handle, [](auto& backend, auto inner) { backend.pause(inner); }); }
    void resume(AudioHandle handle) override { with(handle, [](auto& backend, auto inner) { backend.resume(inner); }); }
    void stop(AudioHandle handle, std::chrono::milliseconds fade = {}) noexcept override {
        try {
            Route route;
            {
                std::scoped_lock lock(mutex_);
                const auto found = tracks_.find(handle);
                if (found == tracks_.end()) return;
                route = found->second;
                tracks_.erase(found);
            }
            route.backend->stop(route.inner, fade);
        } catch (...) {
        }
    }
    void stop_bus(AudioBus bus) noexcept override {
        try {
            if (bass_) bass_->stop_bus(bus);
            if (mci_) mci_->stop_bus(bus);
            std::scoped_lock lock(mutex_);
            std::erase_if(tracks_, [bus](const auto& item) { return item.second.bus == bus; });
        } catch (...) {
        }
    }
    void stop_all() noexcept override {
        try {
            if (bass_) bass_->stop_all();
            if (mci_) mci_->stop_all();
            std::scoped_lock lock(mutex_);
            tracks_.clear();
        } catch (...) {
        }
    }
    void seek(AudioHandle handle, std::chrono::milliseconds value) override {
        with(handle, [value](auto& backend, auto inner) { backend.seek(inner, value); });
    }
    [[nodiscard]] std::chrono::milliseconds position(AudioHandle handle) const override {
        return query<std::chrono::milliseconds>(handle, {}, [](auto& backend, auto inner) {
            return backend.position(inner);
        });
    }
    [[nodiscard]] std::chrono::milliseconds duration(AudioHandle handle) const override {
        return query<std::chrono::milliseconds>(handle, {}, [](auto& backend, auto inner) {
            return backend.duration(inner);
        });
    }
    [[nodiscard]] bool playing(AudioHandle handle) const override {
        return query<bool>(handle, false, [](auto& backend, auto inner) {
            return backend.playing(inner);
        });
    }
    void set_bus_volume(AudioBus bus, float value) noexcept override {
        try {
            if (bass_) bass_->set_bus_volume(bus, value);
            if (mci_) mci_->set_bus_volume(bus, value);
        } catch (...) {
        }
    }
    void cancel_fade_in(AudioHandle handle) noexcept override {
        try {
            with(handle, [](auto& backend, auto inner) { backend.cancel_fade_in(inner); });
        } catch (...) {}
    }
    void set_track_volume(AudioHandle handle, float value) noexcept override {
        try {
            with(handle, [value](auto& backend, const auto inner) {
                backend.set_track_volume(inner, value);
            });
        } catch (...) {
        }
    }
    [[nodiscard]] std::vector<std::string> supported_formats() const override {
        std::vector<std::string> result;
        for (const auto& backend : {bass_, mci_}) {
            if (!backend) continue;
            for (auto format : backend->supported_formats())
                if (std::find(result.begin(), result.end(), format) == result.end())
                    result.push_back(std::move(format));
        }
        return result;
    }

private:
    struct Route {
        std::shared_ptr<AudioBackend> backend;
        AudioHandle inner{};
        AudioBus bus{AudioBus::SoundEffect};
    };

    template <typename Function>
    void with(AudioHandle handle, Function&& function) {
        Route route;
        {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            route = found->second;
        }
        function(*route.backend, route.inner);
    }

    template <typename Result, typename Function>
    Result query(AudioHandle handle, Result fallback, Function&& function) const {
        Route route;
        {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return fallback;
            route = found->second;
        }
        return function(*route.backend, route.inner);
    }

    std::shared_ptr<AudioBackend> bass_;
    std::shared_ptr<AudioBackend> mci_;
    mutable std::mutex mutex_;
    std::unordered_map<AudioHandle, Route> tracks_;
    AudioHandle next_handle_{1U};
    bool bass_ready_{};
    bool mci_ready_{};
    AudioConfiguration configuration_request_{};
    AudioConfigurationResult configuration_{};
};

#endif  // defined(_WIN32)

}  // namespace

namespace testing {

std::shared_ptr<AudioBackend> make_mci_audio_backend(
    MciCommandPrimitive command, std::chrono::milliseconds monitor_interval) {
#if defined(_WIN32)
    return std::make_shared<WindowsAudioBackend>(std::move(command),
                                                 monitor_interval);
#else
    (void)command;
    (void)monitor_interval;
    return {};
#endif
}

bool tick_mci_audio_backend_for_testing(
    const std::shared_ptr<AudioBackend>& backend) noexcept {
#if defined(_WIN32)
    const auto native = std::dynamic_pointer_cast<WindowsAudioBackend>(backend);
    if (!native) return false;
    native->advance_monitor_for_testing();
    return true;
#else
    (void)backend;
    return false;
#endif
}

}  // namespace testing

std::shared_ptr<FileSystemBackend> make_filesystem() {
    return make_filesystem({});
}

std::shared_ptr<FileSystemBackend> make_filesystem(std::shared_ptr<MainDispatcher> dispatcher) {
#if defined(_WIN32)
    return std::make_shared<WindowsFileSystemBackend>(std::move(dispatcher));
#else
    (void)dispatcher;
    return {};
#endif
}

std::shared_ptr<ProcessBackend> make_process() {
#if defined(_WIN32)
    return std::make_shared<WindowsProcessBackend>();
#else
    return {};
#endif
}

std::shared_ptr<SystemBackend> make_system() {
#if defined(_WIN32)
    return std::make_shared<WindowsSystemBackend>();
#else
    return {};
#endif
}

std::shared_ptr<AudioBackend> make_audio() {
#if defined(_WIN32)
    return std::make_shared<WindowsAudioRouter>(
        std::make_shared<WindowsBassAudioBackend>(),
        std::make_shared<WindowsAudioBackend>());
#else
    return {};
#endif
}

std::shared_ptr<FileSystemBackend> make_filesystem_backend() {
    return make_filesystem();
}

std::shared_ptr<FileSystemBackend> make_filesystem_backend(
    std::shared_ptr<MainDispatcher> dispatcher) {
    return make_filesystem(std::move(dispatcher));
}

std::shared_ptr<ProcessBackend> make_process_backend() {
    return make_process();
}

std::shared_ptr<SystemBackend> make_system_backend() {
    return make_system();
}

std::shared_ptr<AudioBackend> make_audio_backend() {
    return make_audio();
}

}  // namespace cwxeditor::lib::windows_runtime

namespace cwxeditor::lib {


std::shared_ptr<FileSystemBackend> make_windows_filesystem() {
    return windows_runtime::make_filesystem();
}

std::shared_ptr<FileSystemBackend> make_windows_filesystem(
    std::shared_ptr<MainDispatcher> dispatcher) {
    return windows_runtime::make_filesystem(std::move(dispatcher));
}

std::shared_ptr<ProcessBackend> make_windows_process() {
    return windows_runtime::make_process();
}

std::shared_ptr<SystemBackend> make_windows_system() {
    return windows_runtime::make_system();
}

std::shared_ptr<AudioBackend> make_windows_audio() {
    return windows_runtime::make_audio();
}

std::shared_ptr<FileSystemBackend> make_windows_filesystem_backend() {
    return windows_runtime::make_filesystem();
}

std::shared_ptr<FileSystemBackend> make_windows_filesystem_backend(
    std::shared_ptr<MainDispatcher> dispatcher) {
    return windows_runtime::make_filesystem(std::move(dispatcher));
}

std::shared_ptr<ProcessBackend> make_windows_process_backend() {
    return windows_runtime::make_process();
}

std::shared_ptr<SystemBackend> make_windows_system_backend() {
    return windows_runtime::make_system();
}

std::shared_ptr<AudioBackend> make_windows_audio_backend() {
    return windows_runtime::make_audio();
}

}  // namespace cwxeditor::lib
