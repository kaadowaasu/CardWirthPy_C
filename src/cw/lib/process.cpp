#include "process.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#  include <cwchar>
#  include <cwctype>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <TargetConditionals.h>
#  endif
extern char** environ;
#endif

namespace cw::lib {
namespace {

constexpr std::size_t kDefaultOutputLimit = 16U * 1024U * 1024U;

std::mutex g_open_callback_mutex;
ExternalProcess::OpenCallback g_open_callback;

void validate_common_options(const ProcessOptions& options) {
    if (options.executable.empty()) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "process executable is empty");
    }
    if (options.executable.find('\0') != std::string::npos ||
        options.working_directory.find('\0') != std::string::npos) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "process path contains NUL");
    }
    if (options.timeout.count() < 0) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "process timeout is negative");
    }
    for (const std::string& argument : options.arguments) {
        if (argument.find('\0') != std::string::npos) {
            throw ProcessError(ProcessErrorKind::InvalidArgument, "process argument contains NUL");
        }
    }
    for (const auto& entry : options.environment) {
        if (entry.first.empty() || entry.first.find('=') != std::string::npos ||
            entry.first.find('\0') != std::string::npos || entry.second.find('\0') != std::string::npos) {
            throw ProcessError(ProcessErrorKind::InvalidArgument, "invalid process environment entry");
        }
    }
}

std::size_t output_limit(const ProcessOptions& options) {
    return options.max_output_bytes == 0 ? kDefaultOutputLimit : options.max_output_bytes;
}

void append_output(std::string& output, const char* data, std::size_t size,
                   std::size_t limit, std::atomic<bool>& truncated) {
    if (output.size() >= limit) {
        truncated.store(true, std::memory_order_relaxed);
        return;
    }
    const std::size_t available = limit - output.size();
    const std::size_t take = std::min(available, size);
    try {
        output.append(data, take);
    } catch (...) {
        truncated.store(true, std::memory_order_relaxed);
    }
    if (take != size) {
        truncated.store(true, std::memory_order_relaxed);
    }
}

} // namespace

ProcessError::ProcessError(ProcessErrorKind kind, const std::string& message)
    : std::runtime_error(message), m_kind(kind) {}

#if defined(_WIN32)

namespace {

std::wstring utf8_to_wide(const std::string& value, const char* label) {
    if (value.empty()) {
        return std::wstring();
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, std::string(label) + " is too long");
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, std::string(label) + " is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size) != size) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, std::string(label) + " conversion failed");
    }
    return result;
}

std::string windows_error(const char* operation, DWORD code = GetLastError()) {
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string detail;
    if (size != 0 && buffer != nullptr) {
        DWORD length = size;
        while (length > 0 && (buffer[length - 1] == L'\r' || buffer[length - 1] == L'\n' ||
                              buffer[length - 1] == L' ')) {
            buffer[--length] = L'\0';
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length),
                                               nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            detail.resize(static_cast<std::size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length),
                                detail.data(), bytes, nullptr, nullptr);
        }
        LocalFree(buffer);
    }
    std::string result(operation);
    result += " failed (Windows error " + std::to_string(code) + ")";
    if (!detail.empty()) result += ": " + detail;
    return result;
}

bool managed_userdata_session() noexcept {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"CARDWIRTHC_MANAGED_USERDATA_SESSION",
                                   value, 2U) == 1U &&
           value[0] == L'1';
}

bool is_managed_network_url(const std::string& value) noexcept {
    const auto starts_with = [&](const char* prefix) {
        std::size_t index = 0;
        for (; prefix[index] != '\0'; ++index) {
            if (index >= value.size()) return false;
            const auto fold = [](unsigned char character) {
                return character >= 'A' && character <= 'Z'
                    ? static_cast<unsigned char>(character - 'A' + 'a')
                    : character;
            };
            if (fold(static_cast<unsigned char>(value[index])) !=
                fold(static_cast<unsigned char>(prefix[index]))) return false;
        }
        return true;
    };
    return starts_with("http://") || starts_with("https://");
}

std::wstring quote_windows_argument(const std::wstring& value) {
    bool needs_quotes = value.empty();
    for (const wchar_t ch : value) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }
    std::wstring result;
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            result.push_back(ch);
            backslashes = 0;
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

struct EnvironmentLess {
    bool operator()(const std::wstring& lhs, const std::wstring& rhs) const noexcept {
        const std::size_t length = std::min(lhs.size(), rhs.size());
        for (std::size_t i = 0; i < length; ++i) {
            const wchar_t a = static_cast<wchar_t>(std::towlower(lhs[i]));
            const wchar_t b = static_cast<wchar_t>(std::towlower(rhs[i]));
            if (a != b) return a < b;
        }
        return lhs.size() < rhs.size();
    }
};

std::vector<wchar_t> make_windows_environment(const ProcessOptions& options) {
    std::map<std::wstring, std::wstring, EnvironmentLess> environment;
    LPWCH raw = GetEnvironmentStringsW();
    if (raw == nullptr) {
        throw ProcessError(ProcessErrorKind::Spawn, windows_error("GetEnvironmentStringsW"));
    }
    for (const wchar_t* entry = raw; *entry != L'\0'; entry += std::wcslen(entry) + 1U) {
        const wchar_t* separator = std::wcschr(entry, L'=');
        if (separator == entry) {
            // ドライブ別の現在ディレクトリーを表す =C: などの疑似変数は、 環境
            // ブロックの置き換えが要求されていない場合だけ継承状態として保持する。
            continue;
        }
        if (separator != nullptr) {
            environment.emplace(std::wstring(entry, separator), std::wstring(separator + 1));
        }
    }
    FreeEnvironmentStringsW(raw);
    for (const auto& entry : options.environment) {
        environment[utf8_to_wide(entry.first, "environment key")] = utf8_to_wide(entry.second, "environment value");
    }
    std::vector<wchar_t> block;
    for (const auto& entry : environment) {
        block.insert(block.end(), entry.first.begin(), entry.first.end());
        block.push_back(L'=');
        block.insert(block.end(), entry.second.begin(), entry.second.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void close_handle(HANDLE& handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

} // namespace

struct ProcessHandle::Impl {
    ProcessOptions options;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE job = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
    std::thread stdout_reader;
    std::thread stderr_reader;
    std::mutex output_mutex;
    std::atomic<bool> truncated{false};
    ProcessResult result;
    bool finished = false;

    ~Impl() {
        if (process != nullptr && is_running()) {
            finished = false;
            terminate_process();
            // TerminateJobObject/TerminateProcess は強制終了なので
            // 、 パイプ読取スレッドの終了待ちより先にプロセスを回収し、継承された書き込み端を閉じる。
            WaitForSingleObject(process, INFINITE);
        }
        if (stdout_reader.joinable()) stdout_reader.join();
        if (stderr_reader.joinable()) stderr_reader.join();
        close_handle(stdout_read);
        close_handle(stderr_read);
        close_handle(thread);
        close_handle(process);
        close_handle(job);
    }

    void terminate_process() noexcept {
        result.terminated = true;
        if (job != nullptr) {
            TerminateJobObject(job, 1U);
        } else if (process != nullptr) {
            TerminateProcess(process, 1U);
        }
    }

    bool is_running() const noexcept {
        if (process == nullptr) return false;
        DWORD code = 0;
        return GetExitCodeProcess(process, &code) != 0 && code == STILL_ACTIVE;
    }

    ProcessResult wait_impl(std::chrono::milliseconds timeout) {
        if (finished) return result;
        if (timeout.count() < 0) {
            throw ProcessError(ProcessErrorKind::InvalidArgument, "process wait timeout is negative");
        }
        result.timed_out = false;
        std::chrono::milliseconds effective = timeout;
        if (effective.count() == 0) effective = options.timeout;
        DWORD milliseconds = INFINITE;
        if (effective.count() > 0) {
            milliseconds = effective.count() >= static_cast<long long>(INFINITE - 1U)
                               ? INFINITE - 1U
                               : static_cast<DWORD>(effective.count());
        }
        const DWORD wait_result = WaitForSingleObject(process, milliseconds);
        if (wait_result == WAIT_TIMEOUT) {
            result.timed_out = true;
            if (options.terminate_on_timeout) {
                terminate_process();
                WaitForSingleObject(process, 1000U);
                if (is_running()) terminate_process();
                WaitForSingleObject(process, INFINITE);
            } else {
                result.output_truncated = truncated.load(std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(output_mutex);
                return result;
            }
        } else if (wait_result == WAIT_FAILED) {
            throw ProcessError(ProcessErrorKind::Io, windows_error("WaitForSingleObject"));
        }
        DWORD exit_code = 1U;
        if (GetExitCodeProcess(process, &exit_code) != 0) {
            result.exit_code = static_cast<int>(exit_code);
        }
        finished = true;
        close_handle(stdout_read);
        close_handle(stderr_read);
        if (stdout_reader.joinable()) stdout_reader.join();
        if (stderr_reader.joinable()) stderr_reader.join();
        result.output_truncated = truncated.load(std::memory_order_relaxed);
        return result;
    }

    void detach() noexcept {
        close_handle(stdout_read);
        close_handle(stderr_read);
        if (stdout_reader.joinable()) stdout_reader.join();
        if (stderr_reader.joinable()) stderr_reader.join();
        close_handle(thread);
        close_handle(process);
        close_handle(job);
        finished = true;
    }
};

void read_windows_pipe(HANDLE pipe, std::string& output, std::size_t limit,
                       std::atomic<bool>& truncated, std::mutex& output_mutex) {
    char buffer[32U * 1024U];
    for (;;) {
        DWORD read = 0;
        if (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) == 0 || read == 0) {
            break;
        }
        std::lock_guard<std::mutex> lock(output_mutex);
        append_output(output, buffer, static_cast<std::size_t>(read), limit, truncated);
    }
}

ProcessHandle ExternalProcess::start(const ProcessOptions& options) {
    validate_common_options(options);
    const std::wstring executable = utf8_to_wide(options.executable, "executable");
    if (executable.empty()) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "executable is empty");
    }
    std::wstring command_line = quote_windows_argument(executable);
    for (const std::string& argument : options.arguments) {
        command_line.push_back(L' ');
        command_line += quote_windows_argument(utf8_to_wide(argument, "argument"));
    }
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');
    const std::wstring cwd = utf8_to_wide(options.working_directory, "working directory");
    std::vector<wchar_t> environment = make_windows_environment(options);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE stdin_handle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &security, OPEN_EXISTING, 0, nullptr);
    if (stdin_handle == INVALID_HANDLE_VALUE) {
        throw ProcessError(ProcessErrorKind::Spawn, windows_error("CreateFileW(NUL stdin)"));
    }
    HANDLE stdout_read = nullptr, stdout_write = nullptr;
    HANDLE stderr_read = nullptr, stderr_write = nullptr;
    auto make_pipe = [&](bool capture, HANDLE& read, HANDLE& write) {
        if (capture) {
            if (CreatePipe(&read, &write, &security, 0) == 0 ||
                SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0) == 0) {
                const DWORD error = GetLastError();
                close_handle(read); close_handle(write);
                throw ProcessError(ProcessErrorKind::Spawn,
                                   windows_error("CreatePipe/SetHandleInformation", error));
            }
        } else {
            write = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security, OPEN_EXISTING, 0, nullptr);
            if (write == INVALID_HANDLE_VALUE) {
                write = nullptr;
                throw ProcessError(ProcessErrorKind::Spawn, windows_error("CreateFileW(NUL output)"));
            }
        }
    };
    try {
        make_pipe(options.capture_stdout, stdout_read, stdout_write);
        make_pipe(options.capture_stderr, stderr_read, stderr_write);
    } catch (...) {
        close_handle(stdin_handle); close_handle(stdout_read); close_handle(stdout_write);
        close_handle(stderr_read); close_handle(stderr_write);
        throw;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_handle;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;
    PROCESS_INFORMATION information{};
    const BOOL created = CreateProcessW(executable.c_str(), command_buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_UNICODE_ENVIRONMENT, environment.data(),
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &information);
    close_handle(stdin_handle); close_handle(stdout_write); close_handle(stderr_write);
    if (created == 0) {
        const DWORD error = GetLastError();
        close_handle(stdout_read); close_handle(stderr_read);
        throw ProcessError(ProcessErrorKind::Spawn, windows_error("CreateProcessW", error));
    }
    auto impl = std::make_unique<ProcessHandle::Impl>();
    impl->options = options;
    impl->process = information.hProcess;
    impl->thread = information.hThread;
    impl->stdout_read = stdout_read;
    impl->stderr_read = stderr_read;
    impl->job = CreateJobObjectW(nullptr, nullptr);
    if (impl->job == nullptr || AssignProcessToJobObject(impl->job, impl->process) == 0) {
        const DWORD error = GetLastError();
        impl->terminate_process();
        WaitForSingleObject(impl->process, INFINITE);
        throw ProcessError(ProcessErrorKind::Spawn,
                           windows_error("CreateJobObjectW/AssignProcessToJobObject", error));
    }
    if (options.capture_stdout) {
        impl->stdout_reader = std::thread(read_windows_pipe, impl->stdout_read,
                                          std::ref(impl->result.stdout_text), output_limit(options),
                                          std::ref(impl->truncated), std::ref(impl->output_mutex));
    } else {
        close_handle(impl->stdout_read);
    }
    if (options.capture_stderr) {
        impl->stderr_reader = std::thread(read_windows_pipe, impl->stderr_read,
                                          std::ref(impl->result.stderr_text), output_limit(options),
                                          std::ref(impl->truncated), std::ref(impl->output_mutex));
    } else {
        close_handle(impl->stderr_read);
    }
    return ProcessHandle(std::move(impl));
}

void ExternalProcess::start_detached(const ProcessOptions& options) {
    ProcessOptions detached = options;
    detached.capture_stdout = false;
    detached.capture_stderr = false;
    ProcessHandle handle = start(detached);
    handle.m_impl->detach();
}

#else // POSIX

namespace {

std::vector<std::string> make_posix_environment(const ProcessOptions& options) {
    std::map<std::string, std::string> environment;
    if (environ != nullptr) {
        for (char** entry = environ; *entry != nullptr; ++entry) {
            const char* separator = std::strchr(*entry, '=');
            if (separator != nullptr) {
                environment.emplace(
                    std::string(*entry, static_cast<std::size_t>(separator - *entry)),
                    std::string(separator + 1));
            }
        }
    }
    for (const auto& entry : options.environment) {
        environment[entry.first] = entry.second;
    }
    std::vector<std::string> result;
    result.reserve(environment.size());
    for (const auto& entry : environment) {
        result.push_back(entry.first + "=" + entry.second);
    }
    return result;
}

std::string environment_value(const std::vector<std::string>& environment, const std::string& key) {
    const std::string prefix = key + '=';
    for (const std::string& entry : environment) {
        if (entry.compare(0, prefix.size(), prefix) == 0) {
            return entry.substr(prefix.size());
        }
    }
    return std::string();
}

std::string resolve_executable(const std::string& executable,
                               const std::vector<std::string>& environment,
                               const std::string& working_directory) {
    if (executable.find('/') != std::string::npos) {
        return executable;
    }
    const std::string path = environment_value(environment, "PATH");
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t separator = path.find(':', begin);
        const std::string directory = separator == std::string::npos
                                          ? path.substr(begin)
                                          : path.substr(begin, separator - begin);
        const std::string candidate = (directory.empty() ? "." : directory) + "/" + executable;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        if (!working_directory.empty() && candidate.front() != '/') {
            const std::string working_candidate = working_directory + "/" + candidate;
            if (access(working_candidate.c_str(), X_OK) == 0) {
                return working_candidate;
            }
        }
        if (separator == std::string::npos) break;
        begin = separator + 1U;
    }
    return executable;
}

bool set_nonblocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

void close_descriptor(int& descriptor) noexcept {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}

void write_child_errno(int descriptor, int error) noexcept {
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&error);
    std::size_t written = 0;
    while (written < sizeof(error)) {
        const ssize_t count = write(descriptor, bytes + written, sizeof(error) - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

void drain_posix_fd(int& descriptor, std::string* output, std::size_t limit,
                    std::atomic<bool>& truncated) {
    char buffer[32U * 1024U];
    for (;;) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            if (output != nullptr) {
                append_output(*output, buffer, static_cast<std::size_t>(count), limit, truncated);
            }
            continue;
        }
        if (count == 0) {
            close_descriptor(descriptor);
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_descriptor(descriptor);
        return;
    }
}

} // namespace

struct ProcessHandle::Impl {
    ProcessOptions options;
    pid_t pid = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
    int exec_error_fd = -1;
    bool process_group = false;
    bool reaped = false;
    bool finished = false;
    bool child_spawn_error = false;
    int child_errno = 0;
    std::vector<std::string> argv_storage;
    std::vector<char*> argv;
    std::vector<std::string> environment_storage;
    std::vector<char*> environment;
    std::atomic<bool> truncated{false};
    ProcessResult result;

    ~Impl() {
        if (pid > 0 && (!finished || is_running())) {
            finished = false;
            terminate_process();
            const bool previous_terminate_policy = options.terminate_on_timeout;
            options.terminate_on_timeout = true;
            try {
                wait_impl(std::chrono::milliseconds(1000));
            } catch (...) {
                close_descriptor(stdout_fd);
                close_descriptor(stderr_fd);
                close_descriptor(exec_error_fd);
                if (process_group) kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
            }
            options.terminate_on_timeout = previous_terminate_policy;
        }
        close_descriptor(stdout_fd);
        close_descriptor(stderr_fd);
        close_descriptor(exec_error_fd);
    }

    void terminate_process() noexcept {
        result.terminated = true;
        if (pid > 0) {
            if (process_group) {
                if (kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);
            } else {
                kill(pid, SIGTERM);
            }
        }
    }

    bool is_running() const noexcept {
        if (reaped || pid <= 0) return false;
        return kill(pid, 0) == 0 || errno == EPERM;
    }

    void reap_nonblocking() {
        if (reaped || pid <= 0) return;
        int status = 0;
        const pid_t result_pid = waitpid(pid, &status, WNOHANG);
        if (result_pid == pid) {
            reaped = true;
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = 128 + WTERMSIG(status);
                result.terminated = true;
            }
        } else if (result_pid < 0 && errno != EINTR && errno != ECHILD) {
            throw ProcessError(ProcessErrorKind::Io, "waitpid failed: " + std::string(std::strerror(errno)));
        }
    }

    void read_exec_error() {
        if (exec_error_fd < 0) return;
        std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(&child_errno);
        std::size_t received = 0;
        for (;;) {
            const ssize_t count = read(exec_error_fd, bytes + received, sizeof(child_errno) - received);
            if (count > 0) {
                received += static_cast<std::size_t>(count);
                if (received == sizeof(child_errno)) {
                    child_spawn_error = true;
                    close_descriptor(exec_error_fd);
                    return;
                }
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            close_descriptor(exec_error_fd);
            return;
        }
    }

    ProcessResult wait_impl(std::chrono::milliseconds timeout) {
        if (finished) return result;
        if (timeout.count() < 0) {
            throw ProcessError(ProcessErrorKind::InvalidArgument, "process wait timeout is negative");
        }
        result.timed_out = false;
        std::chrono::milliseconds effective = timeout;
        if (effective.count() == 0) effective = options.timeout;
        const bool has_deadline = effective.count() > 0;
        const auto deadline = std::chrono::steady_clock::now() + effective;
        bool termination_sent = false;
        auto grace_deadline = deadline;
        for (;;) {
            reap_nonblocking();
            read_exec_error();
            if (stdout_fd >= 0) drain_posix_fd(stdout_fd, options.capture_stdout ? &result.stdout_text : nullptr,
                                               output_limit(options), truncated);
            if (stderr_fd >= 0) drain_posix_fd(stderr_fd, options.capture_stderr ? &result.stderr_text : nullptr,
                                               output_limit(options), truncated);
            if (reaped && stdout_fd < 0 && stderr_fd < 0 && exec_error_fd < 0) break;

            const auto now = std::chrono::steady_clock::now();
            if (has_deadline && now >= (termination_sent ? grace_deadline : deadline)) {
                result.timed_out = true;
                if (!termination_sent && !options.terminate_on_timeout) {
                    result.output_truncated = truncated.load(std::memory_order_relaxed);
                    return result;
                }
                if (!termination_sent && options.terminate_on_timeout && !reaped) {
                    terminate_process();
                    termination_sent = true;
                    grace_deadline = now + std::chrono::seconds(1);
                    continue;
                }
                if (!reaped) {
                    if (process_group && kill(-pid, SIGKILL) == 0) {
                        // プロセスグループは強制終了済みである。
                    } else {
                        kill(pid, SIGKILL);
                    }
                    termination_sent = true;
                    grace_deadline = now + std::chrono::seconds(1);
                    continue;
                }
                close_descriptor(stdout_fd);
                close_descriptor(stderr_fd);
                close_descriptor(exec_error_fd);
                break;
            }

            int wait_milliseconds = 25;
            if (has_deadline) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    (termination_sent ? grace_deadline : deadline) - now);
                wait_milliseconds = static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(25, remaining.count())));
            }
            struct pollfd descriptors[3]{};
            nfds_t count = 0;
            if (stdout_fd >= 0) descriptors[count++] = pollfd{stdout_fd, POLLIN | POLLHUP, 0};
            if (stderr_fd >= 0) descriptors[count++] = pollfd{stderr_fd, POLLIN | POLLHUP, 0};
            if (exec_error_fd >= 0) descriptors[count++] = pollfd{exec_error_fd, POLLIN | POLLHUP, 0};
            const int poll_result = poll(descriptors, count, wait_milliseconds);
            if (poll_result < 0 && errno != EINTR) {
                throw ProcessError(ProcessErrorKind::Io, "poll failed: " + std::string(std::strerror(errno)));
            }
        }
        if (!reaped) {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            reaped = true;
            if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) {
                result.exit_code = 128 + WTERMSIG(status);
                result.terminated = true;
            }
        }
        result.output_truncated = truncated.load(std::memory_order_relaxed);
        finished = true;
        close_descriptor(stdout_fd);
        close_descriptor(stderr_fd);
        close_descriptor(exec_error_fd);
        if (child_spawn_error) {
            throw ProcessError(ProcessErrorKind::Spawn,
                               "child setup/exec failed: " + std::string(std::strerror(child_errno)));
        }
        return result;
    }
};

ProcessHandle ExternalProcess::start(const ProcessOptions& options) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    (void)options;
    throw ProcessError(ProcessErrorKind::Unsupported,
                       "process spawning is unsupported on mobile/Web hosts");
#else
    validate_common_options(options);
    auto impl = std::make_unique<ProcessHandle::Impl>();
    impl->options = options;
    impl->environment_storage = make_posix_environment(options);
    const std::string executable = resolve_executable(options.executable, impl->environment_storage,
                                                      options.working_directory);
    impl->argv_storage.reserve(options.arguments.size() + 1U);
    impl->argv_storage.push_back(executable);
    for (const std::string& argument : options.arguments) impl->argv_storage.push_back(argument);
    for (std::string& argument : impl->argv_storage) impl->argv.push_back(argument.data());
    impl->argv.push_back(nullptr);
    for (std::string& entry : impl->environment_storage) impl->environment.push_back(entry.data());
    impl->environment.push_back(nullptr);

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    if (options.capture_stdout && pipe(stdout_pipe) != 0) {
        throw ProcessError(ProcessErrorKind::Spawn, "stdout pipe failed: " + std::string(std::strerror(errno)));
    }
    if (options.capture_stderr && pipe(stderr_pipe) != 0) {
        close_descriptor(stdout_pipe[0]); close_descriptor(stdout_pipe[1]);
        throw ProcessError(ProcessErrorKind::Spawn, "stderr pipe failed: " + std::string(std::strerror(errno)));
    }
    if (pipe(error_pipe) != 0) {
        close_descriptor(stdout_pipe[0]); close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[0]); close_descriptor(stderr_pipe[1]);
        throw ProcessError(ProcessErrorKind::Spawn, "exec error pipe failed: " + std::string(std::strerror(errno)));
    }
    const int flags = fcntl(error_pipe[1], F_GETFD, 0);
    if (flags < 0 || fcntl(error_pipe[1], F_SETFD, flags | FD_CLOEXEC) != 0 || !set_nonblocking(error_pipe[0])) {
        close_descriptor(stdout_pipe[0]); close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[0]); close_descriptor(stderr_pipe[1]);
        close_descriptor(error_pipe[0]); close_descriptor(error_pipe[1]);
        throw ProcessError(ProcessErrorKind::Spawn, "exec error pipe setup failed");
    }

    const pid_t child = fork();
    if (child < 0) {
        close_descriptor(stdout_pipe[0]); close_descriptor(stdout_pipe[1]);
        close_descriptor(stderr_pipe[0]); close_descriptor(stderr_pipe[1]);
        close_descriptor(error_pipe[0]); close_descriptor(error_pipe[1]);
        throw ProcessError(ProcessErrorKind::Spawn, "fork failed: " + std::string(std::strerror(errno)));
    }
    if (child == 0) {
        close_descriptor(error_pipe[0]);
        if (stdout_pipe[0] >= 0) close_descriptor(stdout_pipe[0]);
        if (stderr_pipe[0] >= 0) close_descriptor(stderr_pipe[0]);
        if (setpgid(0, 0) != 0) {}
        if (!options.working_directory.empty() && chdir(options.working_directory.c_str()) != 0) {
            write_child_errno(error_pipe[1], errno); _exit(127);
        }
        const int null_input = open("/dev/null", O_RDONLY);
        if (null_input < 0 || dup2(null_input, STDIN_FILENO) < 0) {
            write_child_errno(error_pipe[1], errno); _exit(127);
        }
        if (null_input > STDERR_FILENO) close(null_input);
        if (stdout_pipe[1] >= 0) {
            if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) { write_child_errno(error_pipe[1], errno); _exit(127); }
            close_descriptor(stdout_pipe[1]);
        } else {
            const int null_output = open("/dev/null", O_WRONLY);
            if (null_output < 0 || dup2(null_output, STDOUT_FILENO) < 0) { write_child_errno(error_pipe[1], errno); _exit(127); }
            if (null_output > STDERR_FILENO) close(null_output);
        }
        if (stderr_pipe[1] >= 0) {
            if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) { write_child_errno(error_pipe[1], errno); _exit(127); }
            close_descriptor(stderr_pipe[1]);
        } else {
            const int null_output = open("/dev/null", O_WRONLY);
            if (null_output < 0 || dup2(null_output, STDERR_FILENO) < 0) { write_child_errno(error_pipe[1], errno); _exit(127); }
            if (null_output > STDERR_FILENO) close(null_output);
        }
        execve(executable.c_str(), impl->argv.data(), impl->environment.data());
        write_child_errno(error_pipe[1], errno);
        _exit(127);
    }
    close_descriptor(error_pipe[1]);
    close_descriptor(stdout_pipe[1]);
    close_descriptor(stderr_pipe[1]);
    impl->pid = child;
    impl->stdout_fd = stdout_pipe[0];
    impl->stderr_fd = stderr_pipe[0];
    impl->exec_error_fd = error_pipe[0];
    impl->process_group = setpgid(child, child) == 0;
    if (impl->stdout_fd >= 0 && !set_nonblocking(impl->stdout_fd)) {
        impl->terminate_process();
        throw ProcessError(ProcessErrorKind::Spawn, "stdout pipe setup failed");
    }
    if (impl->stderr_fd >= 0 && !set_nonblocking(impl->stderr_fd)) {
        impl->terminate_process();
        throw ProcessError(ProcessErrorKind::Spawn, "stderr pipe setup failed");
    }
    return ProcessHandle(std::move(impl));
#endif
}

void ExternalProcess::start_detached(const ProcessOptions& options) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    (void)options;
    throw ProcessError(ProcessErrorKind::Unsupported,
                       "detached process spawning is unsupported on mobile/Web hosts");
#else
    validate_common_options(options);
    auto environment_storage = make_posix_environment(options);
    const std::string executable = resolve_executable(options.executable, environment_storage,
                                                      options.working_directory);
    std::vector<std::string> argv_storage;
    argv_storage.push_back(executable);
    for (const std::string& argument : options.arguments) argv_storage.push_back(argument);
    std::vector<char*> argv;
    for (std::string& argument : argv_storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::vector<char*> environment;
    for (std::string& entry : environment_storage) environment.push_back(entry.data());
    environment.push_back(nullptr);
    const pid_t first = fork();
    if (first < 0) throw ProcessError(ProcessErrorKind::Spawn, "detached fork failed: " + std::string(std::strerror(errno)));
    if (first == 0) {
        const pid_t second = fork();
        if (second < 0) _exit(127);
        if (second > 0) _exit(0);
        setsid();
        if (!options.working_directory.empty()) chdir(options.working_directory.c_str());
        const int input = open("/dev/null", O_RDONLY);
        const int output = open("/dev/null", O_WRONLY);
        if (input >= 0) { dup2(input, STDIN_FILENO); if (input > STDERR_FILENO) close(input); }
        if (output >= 0) { dup2(output, STDOUT_FILENO); dup2(output, STDERR_FILENO); if (output > STDERR_FILENO) close(output); }
        execve(executable.c_str(), argv.data(), environment.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) {}
#endif
}

#endif // _WIN32

ProcessHandle::ProcessHandle(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

ProcessHandle::~ProcessHandle() = default;

ProcessHandle::ProcessHandle(ProcessHandle&& other) noexcept = default;

ProcessHandle& ProcessHandle::operator=(ProcessHandle&& other) noexcept = default;

bool ProcessHandle::running() const {
    if (!m_impl) return false;
    return m_impl->is_running();
}

void ProcessHandle::terminate() {
    if (!m_impl) return;
    m_impl->terminate_process();
}

ProcessResult ProcessHandle::wait(std::chrono::milliseconds timeout) {
    if (!m_impl) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "waiting on an empty process handle");
    }
    return m_impl->wait_impl(timeout);
}

ProcessResult ExternalProcess::run(const ProcessOptions& options) {
    ProcessHandle handle = start(options);
    return handle.wait(options.timeout);
}

void ExternalProcess::set_platform_open_callback(OpenCallback callback) {
    std::lock_guard<std::mutex> lock(g_open_callback_mutex);
    g_open_callback = std::move(callback);
}

void ExternalProcess::open_url(const std::string& utf8_url) {
    if (utf8_url.empty() || utf8_url.find('\0') != std::string::npos) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "URL is empty or contains NUL");
    }
#if defined(_WIN32)
    if (managed_userdata_session() && !is_managed_network_url(utf8_url)) {
        throw ProcessError(
            ProcessErrorKind::Unsupported,
            "opening non-HTTP URLs through the Windows shell is disabled in a managed UserData session");
    }
#endif
    OpenCallback callback;
    {
        std::lock_guard<std::mutex> lock(g_open_callback_mutex);
        callback = g_open_callback;
    }
    if (callback) {
        callback(utf8_url);
        return;
    }
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    throw ProcessError(ProcessErrorKind::Unsupported,
                       "open_url requires a platform callback on mobile/Web hosts");
#elif defined(_WIN32)
    const std::wstring url = utf8_to_wide(utf8_url, "URL");
    const INT_PTR status = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (status <= 32) {
        throw ProcessError(ProcessErrorKind::Io, windows_error("ShellExecuteW(open URL)", static_cast<DWORD>(status)));
    }
#elif defined(__APPLE__)
    ProcessOptions options;
    options.executable = "/usr/bin/open";
    options.arguments = {utf8_url};
    start_detached(options);
#else
    ProcessOptions options;
    options.executable = "xdg-open";
    options.arguments = {utf8_url};
    start_detached(options);
#endif
}

void ExternalProcess::open_file(const std::string& utf8_path) {
    if (utf8_path.empty() || utf8_path.find('\0') != std::string::npos) {
        throw ProcessError(ProcessErrorKind::InvalidArgument, "file path is empty or contains NUL");
    }
    OpenCallback callback;
    {
        std::lock_guard<std::mutex> lock(g_open_callback_mutex);
        callback = g_open_callback;
    }
    if (callback) {
        callback(utf8_path);
        return;
    }
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    throw ProcessError(ProcessErrorKind::Unsupported,
                       "open_file requires a platform callback on mobile/Web hosts");
#elif defined(_WIN32)
    if (managed_userdata_session()) {
        // ShellExecute は管理する Job Object の子を作
        // らず、既存の Explorer・関連アプリへパスを渡す場合がある。
        // そのプロセスが、保管庫の起動処理による保存・削除後もセッションの平
        // 文を保持・変更し得るため、 ファイルの受け渡しは安全側で拒否する。
        throw ProcessError(ProcessErrorKind::Unsupported,
                           "opening files through the Windows shell is disabled in a managed UserData session");
    }
    const std::wstring path = utf8_to_wide(utf8_path, "file path");
    const INT_PTR status = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (status <= 32) {
        throw ProcessError(ProcessErrorKind::Io, windows_error("ShellExecuteW(open file)", static_cast<DWORD>(status)));
    }
#elif defined(__APPLE__)
    ProcessOptions options;
    options.executable = "/usr/bin/open";
    options.arguments = {utf8_path};
    start_detached(options);
#else
    ProcessOptions options;
    options.executable = "xdg-open";
    options.arguments = {utf8_path};
    start_detached(options);
#endif
}

} // namespace cw::lib
