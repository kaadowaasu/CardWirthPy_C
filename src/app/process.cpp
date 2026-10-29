#include "process.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <system_error>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || defined(__APPLE__)
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
#define CARDWIRTH_POSIX_PROCESS 1
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

namespace cardwirth::app {
namespace {

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length)
        return {};
    return result;
}

std::wstring quote_windows_argument(const std::wstring& value) {
    // CommandLineToArgvWに対応する引用処理。引用符の
    // 直前と、 終端の引用符の前にあるバックスラッシュは二重にする。
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            result.append(slashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

class WindowsProcess final : public Process {
public:
    explicit WindowsProcess(PROCESS_INFORMATION information)
        : process_(information.hProcess), thread_(information.hThread) {}

    ~WindowsProcess() override {
        terminate();
        if (thread_) CloseHandle(thread_);
        if (process_) CloseHandle(process_);
    }

    [[nodiscard]] bool running() const noexcept override {
        poll();
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int exit_code() const noexcept override {
        poll();
        return exit_code_.load(std::memory_order_acquire);
    }

    void terminate() noexcept override {
        if (running() && process_) {
            // 停止要求を送れただけでは終了確認にならない。失敗・タイムアウト時は監視を続ける。
            if (!TerminateProcess(process_, 1)) stop_error_.store(GetLastError());
            const DWORD waited = WaitForSingleObject(process_, 2000);
            if (waited == WAIT_FAILED) stop_error_.store(GetLastError());
            else if (waited == WAIT_TIMEOUT && !stop_error_.load()) stop_error_.store(ERROR_TIMEOUT);
            poll();
        }
    }

    [[nodiscard]] std::error_code stop_error() const noexcept override {
        return {static_cast<int>(stop_error_.load()), std::system_category()};
    }

private:
    void poll() const noexcept {
        if (!running_.load(std::memory_order_acquire) || !process_) return;
        if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return;
        DWORD code = 0;
        if (GetExitCodeProcess(process_, &code)) exit_code_.store(static_cast<int>(code));
        running_.store(false, std::memory_order_release);
    }

    HANDLE process_{};
    std::atomic<DWORD> stop_error_{};
    HANDLE thread_{};
    mutable std::atomic<bool> running_{true};
    mutable std::atomic<int> exit_code_{0};
};
#elif defined(CARDWIRTH_POSIX_PROCESS)
class PosixProcess final : public Process {
public:
    explicit PosixProcess(pid_t pid) : pid_(pid) {}

    ~PosixProcess() override { terminate(); }

    [[nodiscard]] bool running() const noexcept override {
        std::lock_guard lock(mutex_); poll(); return running_.load();
    }
    [[nodiscard]] int exit_code() const noexcept override {
        std::lock_guard lock(mutex_); poll(); return exit_code_.load();
    }
    [[nodiscard]] std::error_code stop_error() const noexcept override {
        std::lock_guard lock(mutex_); return stop_error_;
    }
    void terminate() noexcept override {
        std::lock_guard lock(mutex_);
        poll();
        if (!owns_child_) return;
        stop_error_.clear();
        // WNOWAITで終了済みのリーダーも未回収にし、全グループシグナルの送信まで
        // PID/PGIDを保持する。 再利用されたグループIDへシグナルを送らない。
        if (::kill(-pid_, SIGTERM) < 0 && errno != ESRCH) {
            stop_error_ = std::error_code(errno, std::generic_category()); return;
        }
        // リーダーが終了しても補助プロセスが終了したとは限らない。 グループ全
        // 体に期限付きの猶予を与えた後、残った所有下の子孫プロセスを停止する。
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (::kill(-pid_, SIGKILL) < 0 && errno != ESRCH) {
            stop_error_ = std::error_code(errno, std::generic_category()); return;
        }
        int status = 0;
        pid_t result;
        do { result = ::waitpid(pid_, &status, 0); } while (result < 0 && errno == EINTR);
        if (result == pid_) {
            exit_code_.store(WIFEXITED(status) ? WEXITSTATUS(status) :
                WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
            running_.store(false);
            owns_child_ = false;
        } else {
            stop_error_ = std::error_code(errno, std::generic_category());
            if (errno == ECHILD) owns_child_ = false;
        }
    }

private:
    void poll() const noexcept {
        if (!owns_child_) return;
        siginfo_t info{};
        int result;
        do { result = ::waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT); }
        while (result < 0 && errno == EINTR);
        if (result < 0) {
            if (errno == ECHILD) {
                // 外部の回収処理によって所有を確認する識別情報が失わ
                // れたため、 以後はグループへシグナルを送らない。
                owns_child_ = false;
                running_.store(false);
                stop_error_ = std::error_code(ECHILD, std::generic_category());
            }
            return;
        }
        if (info.si_pid != pid_) return;
        exit_code_.store(info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status,
                         std::memory_order_release);
        running_.store(false,std::memory_order_release);
    }

    pid_t pid_{};
    mutable std::mutex mutex_;
    mutable bool owns_child_{true};
    mutable std::error_code stop_error_;
    mutable std::atomic<bool> running_{true};
    mutable std::atomic<int> exit_code_{0};
};
#endif

}  // namespace

ProcessPtr launch_process(const ProcessSpec& spec, std::string& error) {
    error.clear();
    if (spec.executable.empty()) {
        error = "実行ファイルが指定されていません";
        return {};
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(spec.executable, filesystem_error)) {
        error = "実行ファイルが見つかりません: " + path_to_utf8(spec.executable);
        return {};
    }

#ifdef _WIN32
    std::wstring executable = spec.executable.wstring();
    std::wstring command_line = quote_windows_argument(executable);
    for (const auto& argument : spec.arguments) {
        const auto wide = utf8_to_wide(argument);
        if (wide.empty() && !argument.empty()) {
            error = "子プロセス引数をUTF-16へ変換できません";
            return {};
        }
        command_line.push_back(L' ');
        command_line += quote_windows_argument(wide);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information{};
    const std::wstring working_directory = spec.working_directory.empty()
                                                ? spec.executable.parent_path().wstring()
                                                : spec.working_directory.wstring();
    if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr,
                        working_directory.empty() ? nullptr : working_directory.c_str(),
                        &startup, &information)) {
        error = "子プロセスを起動できません (Win32=" + std::to_string(GetLastError()) + ")";
        return {};
    }
    return std::make_shared<WindowsProcess>(information);
#elif defined(CARDWIRTH_POSIX_PROCESS)
    // forkの前にパスを解決し、C++の必要なメモリをすべて用意する。 子プロセスはexec
    // でアドレス空間を置き換えるまで、非同期シグナル安全なOS呼び出しだけを使う。
    const auto absolute_executable = std::filesystem::absolute(spec.executable, filesystem_error);
    if (filesystem_error) { error = filesystem_error.message(); return {}; }
    const auto absolute_directory = spec.working_directory.empty() ? absolute_executable.parent_path()
        : std::filesystem::absolute(spec.working_directory, filesystem_error);
    if (filesystem_error) { error = filesystem_error.message(); return {}; }
    const auto executable = absolute_executable.native();
    const auto directory = absolute_directory.native();
    std::vector<std::string> owned_arguments{executable};
    owned_arguments.insert(owned_arguments.end(), spec.arguments.begin(), spec.arguments.end());
    std::vector<char*> arguments;
    for (auto& argument : owned_arguments) arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    const char* executable_ptr = executable.c_str();
    const char* directory_ptr = directory.c_str();
    char* const* argv_ptr = arguments.data();
    int notify[2];
    if (::pipe(notify) != 0) { error = "起動通知パイプを作成できません: " + std::error_code(errno, std::generic_category()).message(); return {}; }
    if (::fcntl(notify[0], F_SETFD, FD_CLOEXEC) < 0 ||
        ::fcntl(notify[1], F_SETFD, FD_CLOEXEC) < 0) {
        const int code = errno;
        ::close(notify[0]); ::close(notify[1]);
        error = "起動通知を設定できません: " + std::error_code(code, std::generic_category()).message();
        return {};
    }
    struct LaunchFailure { int stage; int code; };
    const pid_t pid = ::fork();
    if (pid < 0) {
        const int code = errno;
        ::close(notify[0]); ::close(notify[1]);
        error = "子プロセスを起動できません: " + std::error_code(code, std::generic_category()).message();
        return {};
    }
    if (pid == 0) {
        ::close(notify[0]);
        LaunchFailure failure{0, 0};
        if (::setpgid(0, 0) != 0) { failure.stage = 1; failure.code = errno; }
        else if (::chdir(directory_ptr) != 0) { failure.stage = 2; failure.code = errno; }
        else {
            ::execv(executable_ptr, argv_ptr);
            failure.stage = 3; failure.code = errno;
        }
        const char* bytes = reinterpret_cast<const char*>(&failure);
        size_t remaining = sizeof(failure);
        while (remaining) {
            const ssize_t count = ::write(notify[1], bytes, remaining);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            bytes += count; remaining -= static_cast<size_t>(count);
        }
        ::_exit(127);
    }
    ::close(notify[1]);
    LaunchFailure failure{};
    size_t received = 0;
    int read_error = 0;
    while (received < sizeof(failure)) {
        const ssize_t count = ::read(notify[0], reinterpret_cast<char*>(&failure) + received, sizeof(failure) - received);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { read_error = errno; break; }
        if (count == 0) break; // execの成功により、通知用の記述子が閉じられた。
        received += static_cast<size_t>(count);
    }
    ::close(notify[0]);
    if (received || read_error) {
        if (read_error || received != sizeof(failure)) {
            (void)::kill(pid, SIGKILL);
            error = "子プロセスの起動結果を確認できません";
        } else {
            error = std::string(failure.stage == 2 ? "作業フォルダへ移動できません: " :
                failure.stage == 3 ? "実行ファイルを起動できません: " : "プロセスグループを作成できません: ") +
                std::error_code(failure.code, std::generic_category()).message();
        }
        while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        return {};
    }
    try { return std::make_shared<PosixProcess>(pid); }
    catch (...) {
        (void)::kill(-pid, SIGKILL);
        while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        throw;
    }
#else
    error="このプラットフォームでは外部プロセスを起動できません";
    return {};
#endif
}

}  // namespace cardwirth::app
