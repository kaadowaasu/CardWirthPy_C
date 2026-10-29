#pragma once

// 単体版CWXEditorの実行時の境界。このヘッダ
// ーはMainWindowやネイティブGUI型を取
// り込まない。FrontendFactoryからフ
// ロントエンドを提供し、単体実行ファイルとALLの
// ページで同じEditor実行環境を使えるようにす
// る。MainWindowアダプターの接続先も、フ
// ァクトリーのコールバックオブジェクトに限定する。

#include "cwxeditor_src/lib/runtime_context.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace cwxeditor::editor {

/// Embeddedモードではホストが全体のサービスを所有する。Standaloneモードでは
/// Editorのネイティブなプラットフォームサービスを使用でき、コンテキストを必要としない。
enum class HostMode {
    Standalone,
    Embedded,
};

/// Editorセッションの終了理由。別名により、ネイティブのフロントエンドではWindowClosed
/// 、ALLホストではHome/HostClosedという名前を使える。
enum class ExitReason {
    None,
    Completed,
    Requested,
    HostRequested,
    HostClosed = HostRequested,
    WindowClosed,
    FrontendClosed = WindowClosed,
    StartupFailed,
    PumpFailed,
    Error = PumpFailed,
    Shutdown,
    Home = HostRequested,
};

/// フロントエンドのコールバック一式。値として保持し、アダプターがネイ
/// ティブのMainWindow、Webビュー、テスト代替をキャプチャ
/// しても、その型をEditorRuntimeの公開ABIに含めない。
struct FrontendCallbacks {
    /// フロントエンドを作成・表示する。falseを返すと起動を取り消す。
    std::function<bool()> start{};

    /// イベントループを一回処理する。フロントエンドが閉じた場合はfalseを返す
    /// 。waitフラグはGUI::pump_events(wait)と同じ意味。
    std::function<bool(bool wait)> pump{};

    /// フロントエンドに終了を要求する。このコールバック内で実行環境を破棄し
    /// てはならず、破棄はpump()またはshutdown()で完了する。
    std::function<void(ExitReason)> request_close{};

    /// フロントエンドが所有する資源を解放する。開始済みセッションご
    /// とに最大一度呼ぶ。コールバックだけのテストでは空にしてよい。
    std::function<void()> shutdown{};

    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(start) && static_cast<bool>(pump);
    }
};

/// FrontendFactoryに渡す値。runtime_contextは借用
/// であり、ホストはセッションの終了コールバックが戻るまで生存させる必要がある。
struct FrontendContext {
    HostMode host_mode{HostMode::Standalone};
    ::cwxeditor::lib::EditorRuntimeContext* runtime_context{};

    /// フロントエンドは、ネイティブの終了イベントやホストのHomeボタン
    /// からこのコールバックを使える。複数回呼んでも安全で、EditorRuntime
    /// は現在のセッションの最初の終了理由だけを保持する。
    std::function<void(ExitReason)> request_close{};
};

using RuntimeContext = ::cwxeditor::lib::EditorRuntimeContext;
using HostRuntimeContext = ::cwxeditor::lib::HostRuntimeContext;
using HomeCallback = std::function<void(ExitReason)>;
using ErrorCallback = std::function<void(std::string_view)>;
using FrontendFactory = std::function<FrontendCallbacks(const FrontendContext&)>;

/// EditorRuntimeの構築パラメーター。
struct RuntimeOptions {
    HostMode host_mode{HostMode::Standalone};
    RuntimeContext* runtime_context{};
    FrontendFactory frontend_factory{};
    HomeCallback return_home{};
    ErrorCallback report_error{};
};

/// 再起動可能なEditorセッションの調整役。コピー不
/// 可で、一つのUIスレッドから操作する。セッション中の
/// start()は拒否し、shutdown()後のstart()
/// では新しいフロントエンドセッションを作る。フロント
/// エンドのコールバックは開始中やpump中にrequest_close()
/// を呼べる。現在のコールバックが戻るま
/// で遷移を遅らせ、破棄を再入させない。
class EditorRuntime final {
public:
    using Mode = HostMode;
    using Reason = ExitReason;
    using Context = RuntimeContext;
    using Options = RuntimeOptions;
    using Frontend = FrontendCallbacks;
    using Factory = FrontendFactory;

    explicit EditorRuntime(RuntimeOptions options = {});
    EditorRuntime(HostMode mode, RuntimeContext* context,
                  FrontendFactory factory = {}, HomeCallback return_home = {},
                  ErrorCallback report_error = {});
    EditorRuntime(HostMode mode, RuntimeContext& context,
                  FrontendFactory factory = {}, HomeCallback return_home = {},
                  ErrorCallback report_error = {});

    ~EditorRuntime();

    EditorRuntime(const EditorRuntime&) = delete;
    EditorRuntime& operator=(const EditorRuntime&) = delete;
    EditorRuntime(EditorRuntime&&) = delete;
    EditorRuntime& operator=(EditorRuntime&&) = delete;

    /// セッションを一つ開始する。すでに実行中、ファクトリーが未設定
    /// ・無効、フロントエンドの起動失敗の場合はfalseを返す。
    [[nodiscard]] bool start();

    /// フロントエンドを一回処理する。セッションが有効な間はtrue。
    /// 閉じたフロントエンドは終了処理を済ませてからfalseを返す。
    [[nodiscard]] bool pump(bool wait = true);

    /// 非同期の終了を要求する。フロントエンドに終了要求の処理があれば渡し
    /// 、実際の破棄は次のpump()またはshutdown()で行う。
    void request_close(ExitReason reason = ExitReason::Requested);

    /// 現在のセッションを直ちに終了する。繰り返しても安全で、フロントエ
    /// ンドのコールバック内からも呼べる。ホストのコールバックは、フロ
    /// ントエンドのすべてのコールバックと状態を解放した後に実行する。
    void shutdown() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] HostMode host_mode() const noexcept { return options_.host_mode; }
    [[nodiscard]] RuntimeContext* runtime_context() const noexcept {
        return options_.runtime_context;
    }
    [[nodiscard]] ExitReason exit_reason() const noexcept { return exit_reason_; }
    [[nodiscard]] std::size_t session_count() const noexcept {
        return session_count_;
    }

    /// 設定を変更できるのは停止中だけ。これらの設定関
    /// 数はコールバック実行中にfalseを返し、セッ
    /// ション途中の意図しない置換を一貫して拒否する。
    [[nodiscard]] bool set_frontend_factory(FrontendFactory factory);
    [[nodiscard]] bool set_return_home(HomeCallback callback);
    [[nodiscard]] bool set_report_error(ErrorCallback callback);
    [[nodiscard]] bool set_runtime_context(RuntimeContext* context);

    /// Homeコールバックで終了理由を必要
    /// としないホスト向けの簡易アダプター。
    [[nodiscard]] bool set_return_home(std::function<void()> callback);

private:
    enum class State {
        Stopped,
        Starting,
        Running,
        Pumping,
        ShuttingDown,
    };

    void report_error(std::string_view message) const noexcept;
    void finish(ExitReason reason) noexcept;
    [[nodiscard]] bool active() const noexcept;

    RuntimeOptions options_{};
    FrontendCallbacks frontend_{};
    State state_{State::Stopped};
    ExitReason exit_reason_{ExitReason::None};
    ExitReason pending_reason_{ExitReason::None};
    std::size_t session_count_{};
    bool close_requested_{};
    bool close_callback_running_{};
    bool shutdown_callback_called_{};
    bool return_home_called_{};
    bool destroying_{};
};

}  // namespace cwxeditor::editor

// 移植版で既存の二つの名前空間表記からAPIを利用できるよ
// うにする。別名だけを設け、実行環境の実装は一つに保つ。
namespace cwxeditor::runtime {
using ::cwxeditor::editor::ErrorCallback;
using ::cwxeditor::editor::ExitReason;
using ::cwxeditor::editor::FrontendCallbacks;
using ::cwxeditor::editor::FrontendContext;
using ::cwxeditor::editor::FrontendFactory;
using ::cwxeditor::editor::HomeCallback;
using ::cwxeditor::editor::HostMode;
using ::cwxeditor::editor::RuntimeContext;
using ::cwxeditor::editor::HostRuntimeContext;
using ::cwxeditor::editor::RuntimeOptions;
using ::cwxeditor::editor::EditorRuntime;
}  // namespace cwxeditor::runtime

namespace cwxeditor::cwxeditor {
using ::cwxeditor::editor::ErrorCallback;
using ::cwxeditor::editor::ExitReason;
using ::cwxeditor::editor::FrontendCallbacks;
using ::cwxeditor::editor::FrontendContext;
using ::cwxeditor::editor::FrontendFactory;
using ::cwxeditor::editor::HomeCallback;
using ::cwxeditor::editor::HostMode;
using ::cwxeditor::editor::RuntimeContext;
using ::cwxeditor::editor::HostRuntimeContext;
using ::cwxeditor::editor::RuntimeOptions;
using ::cwxeditor::editor::EditorRuntime;
}  // namespace cwxeditor::cwxeditor
