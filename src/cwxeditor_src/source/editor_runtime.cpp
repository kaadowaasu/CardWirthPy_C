#include "cwxeditor_src/source/editor_runtime.hpp"

#include <exception>
#include <string>
#include <utility>

namespace cwxeditor::editor {
namespace {

constexpr std::string_view kMissingFactory =
    "EditorRuntime requires a frontend factory";
constexpr std::string_view kInvalidFactory =
    "EditorRuntime frontend factory returned an incomplete callback set";
constexpr std::string_view kEmbeddedContext =
    "Embedded EditorRuntime requires a RuntimeContext";
constexpr std::string_view kReentrantStart =
    "EditorRuntime start() rejected while a session is active";
constexpr std::string_view kStartFailure =
    "EditorRuntime frontend startup returned false";
constexpr std::string_view kPumpFailure =
    "EditorRuntime frontend pump threw an exception";
constexpr std::string_view kShutdownFailure =
    "EditorRuntime frontend shutdown threw an exception";

std::string exception_message(const char* operation) {
    try {
        if (const auto current = std::current_exception()) {
            std::rethrow_exception(current);
        }
    } catch (const std::exception& error) {
        std::string result(operation);
        result += ": ";
        result += error.what();
        return result;
    } catch (...) {
        std::string result(operation);
        result += ": unknown exception";
        return result;
    }
    return operation;
}

}  // namespace

EditorRuntime::EditorRuntime(RuntimeOptions options)
    : options_(std::move(options)) {}

EditorRuntime::EditorRuntime(HostMode mode, RuntimeContext* context,
                             FrontendFactory factory,
                             HomeCallback return_home,
                             ErrorCallback report_error)
    : options_{mode, context, std::move(factory), std::move(return_home),
               std::move(report_error)} {}

EditorRuntime::EditorRuntime(HostMode mode, RuntimeContext& context,
                             FrontendFactory factory,
                             HomeCallback return_home,
                             ErrorCallback report_error)
    : EditorRuntime(mode, &context, std::move(factory),
                    std::move(return_home), std::move(report_error)) {}

EditorRuntime::~EditorRuntime() {
    destroying_ = true;
    shutdown();
}

bool EditorRuntime::active() const noexcept {
    return state_ != State::Stopped;
}

bool EditorRuntime::running() const noexcept {
    return state_ == State::Running || state_ == State::Pumping;
}

bool EditorRuntime::started() const noexcept {
    return active();
}

void EditorRuntime::report_error(const std::string_view message) const noexcept {
    if (!options_.report_error) return;
    try {
        options_.report_error(message);
    } catch (...) {
        // エラー報告はホストとの境界。診断コールバックが
        // 例外を出しても、確立済みの終了状態を戻さない。
    }
}

bool EditorRuntime::start() {
    if (destroying_ || active()) {
        if (destroying_) return false;
        report_error(kReentrantStart);
        return false;
    }

    // 停止済みの実行環境は再起動できる。利用側のコードを呼
    // ぶ前にセッションごとの全状態を消す。 コールバック
    // が同期的に終了や別の開始を要求する場合があるため。
    frontend_ = {};
    state_ = State::Starting;
    exit_reason_ = ExitReason::None;
    pending_reason_ = ExitReason::None;
    close_requested_ = false;
    close_callback_running_ = false;
    shutdown_callback_called_ = false;
    return_home_called_ = false;
    ++session_count_;

    if (options_.host_mode == HostMode::Embedded &&
        options_.runtime_context == nullptr) {
        report_error(kEmbeddedContext);
        finish(ExitReason::StartupFailed);
        return false;
    }
    if (!options_.frontend_factory) {
        report_error(kMissingFactory);
        finish(ExitReason::StartupFailed);
        return false;
    }

    FrontendContext context;
    context.host_mode = options_.host_mode;
    context.runtime_context = options_.runtime_context;
    context.request_close = [this](const ExitReason reason) {
        request_close(reason);
    };

    try {
        frontend_ = options_.frontend_factory(context);
    } catch (...) {
        report_error(exception_message("EditorRuntime frontend factory"));
        finish(ExitReason::StartupFailed);
        return false;
    }
    if (!frontend_.valid()) {
        report_error(kInvalidFactory);
        finish(ExitReason::StartupFailed);
        return false;
    }

    bool started_ok = false;
    try {
        started_ok = frontend_.start();
    } catch (...) {
        report_error(exception_message("EditorRuntime frontend start"));
    }
    if (!started_ok) {
        if (!close_requested_) report_error(kStartFailure);
        finish(close_requested_ && pending_reason_ != ExitReason::None
                   ? pending_reason_ : ExitReason::StartupFailed);
        return false;
    }

    // 開始コールバックが、渡した終了コールバックを呼んでいる場合が
    // ある。 その場合は一時的なRunningを公開せず、start()
    // が戻った直後、コールバックの外で終了を確定する。
    if (close_requested_) {
        finish(pending_reason_ == ExitReason::None
                   ? ExitReason::Requested : pending_reason_);
        return true;
    }
    state_ = State::Running;
    return true;
}

bool EditorRuntime::pump(const bool wait) {
    if (!running()) return false;
    if (close_requested_) {
        finish(pending_reason_ == ExitReason::None
                   ? ExitReason::Requested : pending_reason_);
        return false;
    }

    // フロントエンドは一つのUIスレッドから動かす前提。 このガードで
    // 、誤ってコールバックから pump() へ再帰しても安全にする。
    if (state_ == State::Pumping) return true;
    state_ = State::Pumping;
    bool keep_running = true;
    try {
        keep_running = frontend_.pump(wait);
    } catch (...) {
        report_error(exception_message("EditorRuntime frontend pump"));
        keep_running = false;
        close_requested_ = true;
        pending_reason_ = ExitReason::PumpFailed;
    }

    if (close_requested_) {
        finish(pending_reason_ == ExitReason::None
                   ? ExitReason::Requested : pending_reason_);
        return false;
    }
    if (!keep_running) {
        finish(ExitReason::WindowClosed);
        return false;
    }
    state_ = State::Running;
    return true;
}

void EditorRuntime::request_close(const ExitReason reason) {
    if (!active()) return;

    // 最初の理由を優先する。 同じイベント処理内でネイティ
    // ブの終了とホストのページ移動が競合する場合に必要。
    if (!close_requested_) {
        close_requested_ = true;
        pending_reason_ = reason == ExitReason::None
            ? ExitReason::Requested : reason;
    }

    if (close_callback_running_ || !frontend_.request_close) return;
    close_callback_running_ = true;
    try {
        frontend_.request_close(pending_reason_);
    } catch (...) {
        report_error(exception_message("EditorRuntime frontend close request"));
    }
    close_callback_running_ = false;
}

void EditorRuntime::finish(const ExitReason reason) noexcept {
    if (state_ == State::Stopped || state_ == State::ShuttingDown) return;

    state_ = State::ShuttingDown;
    const ExitReason final_reason =
        reason == ExitReason::None
            ? (pending_reason_ == ExitReason::None
                   ? ExitReason::Shutdown : pending_reason_)
            : reason;
    exit_reason_ = final_reason;
    close_requested_ = true;
    pending_reason_ = final_reason;

    // コールバックを外へ移してから呼ぶ。 ホストのホーム
    // 操作は start() を再び呼べるため、その時
    // 点で古いフロントエンドへ到達できないようにする。
    auto frontend = std::move(frontend_);
    if (frontend.shutdown && !shutdown_callback_called_) {
        shutdown_callback_called_ = true;
        try {
            frontend.shutdown();
        } catch (...) {
            report_error(kShutdownFailure);
        }
    }

    state_ = State::Stopped;
    close_requested_ = false;
    close_callback_running_ = false;
    pending_reason_ = ExitReason::None;

    if (return_home_called_ || !options_.return_home) return;
    return_home_called_ = true;
    try {
        options_.return_home(final_reason);
    } catch (...) {
        report_error("EditorRuntime return-home callback threw an exception");
    }
}

void EditorRuntime::shutdown() noexcept {
    if (!active()) return;
    finish(pending_reason_ == ExitReason::None
               ? ExitReason::Shutdown : pending_reason_);
}

bool EditorRuntime::set_frontend_factory(FrontendFactory factory) {
    if (active()) return false;
    options_.frontend_factory = std::move(factory);
    return true;
}

bool EditorRuntime::set_return_home(HomeCallback callback) {
    if (active()) return false;
    options_.return_home = std::move(callback);
    return true;
}

bool EditorRuntime::set_report_error(ErrorCallback callback) {
    if (active()) return false;
    options_.report_error = std::move(callback);
    return true;
}

bool EditorRuntime::set_runtime_context(RuntimeContext* context) {
    if (active()) return false;
    options_.runtime_context = context;
    return true;
}

bool EditorRuntime::set_return_home(std::function<void()> callback) {
    if (active()) return false;
    if (callback) {
        options_.return_home = [callback = std::move(callback)](ExitReason) {
            callback();
        };
    } else {
        options_.return_home = {};
    }
    return true;
}

}  // namespace cwxeditor::editor
