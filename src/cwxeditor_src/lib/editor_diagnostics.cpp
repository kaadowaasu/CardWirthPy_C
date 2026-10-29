#include "editor_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <cstdio>
#endif

#if defined(_WIN32) && defined(CWXEDITOR_FILE_LOGGING) && defined(CWXEDITOR_LOG_IN_RELEASE)
#  define CWXEDITOR_DIAGNOSTICS_ENABLED 1
#endif

namespace cwxeditor::lib {
namespace {

std::mutex log_mutex;
std::filesystem::path log_path;
std::ofstream log_stream;
std::string pending_log;
std::condition_variable log_condition;
std::thread log_worker;
bool log_worker_stop{};
std::atomic_bool log_active{};
std::uintmax_t log_bytes{};
bool log_limit_reported{};
constexpr std::uintmax_t maximum_log_bytes = 4U * 1024U * 1024U;
constexpr std::size_t flush_batch_bytes = 64U * 1024U;
constexpr auto flush_interval = std::chrono::milliseconds(250);

#if defined(_WIN32) && defined(CWXEDITOR_DIAGNOSTICS_ENABLED)
bool console_owned{};

void initialize_console() {
    if (!GetConsoleWindow()) {
        if (!AllocConsole()) return;
        console_owned = true;
    }
    SetConsoleTitleW(L"CWXEditor_C action log");
    SetConsoleOutputCP(CP_UTF8);
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(true);
}
#endif

std::string format_line(const std::string_view message) {
    const auto now = std::chrono::system_clock::now();
    const auto value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream line;
    line << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " ["
         << std::this_thread::get_id() << "] " << message << '\n';
    return line.str();
}

void flush_pending_locked() {
    if (pending_log.empty()) return;
    if (log_stream.is_open()) {
        log_stream.write(pending_log.data(), static_cast<std::streamsize>(pending_log.size()));
        log_stream.flush();
    }
#if defined(_DEBUG)
    std::cerr.write(pending_log.data(), static_cast<std::streamsize>(pending_log.size()));
    std::cerr.flush();
#endif
    pending_log.clear();
}

void run_log_worker() noexcept {
    try {
        std::unique_lock lock(log_mutex);
        for (;;) {
            (void)log_condition.wait_for(lock, flush_interval, [] {
                return log_worker_stop || pending_log.size() >= flush_batch_bytes;
            });
            const bool stopping = log_worker_stop;
            std::string batch;
            batch.swap(pending_log);
            // ファイル・コンソールへの書き込みは停止する場合があるため、生成側のミューテックス
            // を保持したまま行わない。 UI操作の記録は、メモリ上のキューへの追加だけにする。
            lock.unlock();
            if (!batch.empty()) {
                if (log_stream.is_open()) {
                    log_stream.write(batch.data(), static_cast<std::streamsize>(batch.size()));
                    log_stream.flush();
                }
#if defined(_DEBUG)
                std::cerr.write(batch.data(), static_cast<std::streamsize>(batch.size()));
                std::cerr.flush();
#endif
            }
            lock.lock();
            if (stopping) break;
        }
        flush_pending_locked();
    } catch (...) {
        // 診断処理でエディタの動作を変えない。
    }
}

void stop_log_worker() noexcept {
    std::thread worker;
    {
        std::scoped_lock lock(log_mutex);
        log_worker_stop = true;
        log_condition.notify_all();
        worker = std::move(log_worker);
    }
    if (worker.joinable()) worker.join();
}

}  // namespace

void initialize_editor_diagnostics(const std::filesystem::path& application_directory,
                                   const bool show_console) {
#if defined(CWXEDITOR_DIAGNOSTICS_ENABLED)
    log_active.store(false, std::memory_order_release);
    stop_log_worker();
    std::scoped_lock lock(log_mutex);
#if defined(_DEBUG)
    log_path = application_directory / "CWXEditor_C_debug.log";
#else
    log_path = application_directory / "CWXEditor_C_release.log";
#endif
    std::error_code error;
    std::filesystem::create_directories(log_path.parent_path(), error);
    if (log_stream.is_open()) log_stream.close();
    // 複数のエディタが別プロセスで動く場合がある。共有ログを切り詰め
    // ると、別の有効なセッションの記録が消え、 その書き込み再開時に
    // 穴ができるため、各まとまりを追記する。 生成側の4MiB上限は
    // セッションごとに保ち、ファイル操作をUIスレッドへ移さない。
    log_stream.open(log_path, std::ios::binary | std::ios::app);
    log_active.store(log_stream.is_open(), std::memory_order_release);
    pending_log.clear();
    log_bytes = 0;
    log_limit_reported = false;
    log_worker_stop = false;
#if defined(_WIN32) && defined(_DEBUG)
    if (show_console) initialize_console();
#else
    (void)show_console;
#endif
    log_worker = std::thread(run_log_worker);
#else
    (void)application_directory;
    (void)show_console;
#endif
}

void shutdown_editor_diagnostics() noexcept {
#if defined(CWXEDITOR_DIAGNOSTICS_ENABLED)
    log_active.store(false, std::memory_order_release);
    stop_log_worker();
    std::scoped_lock lock(log_mutex);
    flush_pending_locked();
    if (log_stream.is_open()) {
        log_stream.close();
    }
    log_path.clear();
#if defined(_WIN32)
    if (console_owned) {
        FreeConsole();
        console_owned = false;
    }
#endif
#endif
}

void editor_action_log(const std::string_view message) noexcept {
#if defined(CWXEDITOR_DIAGNOSTICS_ENABLED)
    try {
        std::scoped_lock lock(log_mutex);
        // ワーカーはこのミューテックスの外でストリームへ書く。 生成側
        // は公開済みの状態だけを確認し、ストリーム自体には触れない。
        if (log_path.empty() || !log_active.load(std::memory_order_acquire)) return;
        if (log_bytes >= maximum_log_bytes) {
            if (!log_limit_reported) {
                log_limit_reported = true;
                pending_log += "CWXEditor_C log reached 4 MiB; further logging is suppressed.\n";
                log_condition.notify_one();
            }
            return;
        }
        auto line = format_line(message);
        const auto remaining = maximum_log_bytes - log_bytes;
        if (line.size() > remaining) line.resize(static_cast<std::size_t>(remaining));
        pending_log.append(line);
        log_bytes += line.size();
#if defined(_DEBUG) && defined(_WIN32)
        OutputDebugStringA(line.c_str());
#endif
        log_condition.notify_one();
    } catch (...) {
        // 診断処理でエディタの動作を変えない。
    }
#else
    (void)message;
#endif
}

bool editor_diagnostics_enabled() noexcept {
#if defined(CWXEDITOR_DIAGNOSTICS_ENABLED)
    return log_active.load(std::memory_order_acquire);
#else
    return false;
#endif
}

void ensure_editor_diagnostics(const std::filesystem::path& directory) {
    if (!editor_diagnostics_enabled()) initialize_editor_diagnostics(directory, false);
}

EditorPerformanceScope::EditorPerformanceScope(const std::string_view operation,
                                               const std::uint64_t threshold_us) noexcept
    : operation_(operation), threshold_us_(threshold_us),
      enabled_(editor_diagnostics_enabled()) {
    if (enabled_) started_ = std::chrono::steady_clock::now();
}

EditorPerformanceScope::~EditorPerformanceScope() noexcept {
    if (!enabled_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started_).count();
    if (elapsed < static_cast<std::int64_t>(threshold_us_)) return;
    try {
        editor_action_log("PERF operation=" + std::string(operation_) +
                          " elapsed_us=" + std::to_string(elapsed));
    } catch (...) {}
}

}  // namespace cwxeditor::lib
