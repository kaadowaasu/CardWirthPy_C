#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 性能測定用のReleaseビルドでも、Debug CRTを有効にせ
// ず頻度制限付きの診断を使える。 localhostの制御サーバーは
// 、非アクティブの自動操作用に明示的に起動した場合のみ有効にする。
#if defined(_DEBUG) || defined(ENABLE_LOG_IN_RELEASE)
#define CARDWIRTH_LOG_ENABLED 1
#endif

namespace cardwirth::debug {

struct DebugControlLaunchOptions {
    bool enabled{};
    unsigned short port{49321};
};

// 通信を有効にするオプションのみを解析する。Release版は--communication-debug
// の明示指定が必要。 Debug版は従来どおり常時サーバーを有効にする。
[[nodiscard]] DebugControlLaunchOptions debug_control_launch_options(
    const std::vector<std::string>& arguments, bool debug_build);

void initialize_logger(const std::filesystem::path& application_directory);
// ログの終了処理を行い、ログ出力と
// 所有するコンソールを後始末する。
void shutdown_logger();
void log(const std::string& message);
// デバッグ用の頻度制限付き性能診断。3ms未満の呼び出
// しは記録せず、 各キーの出力は1秒に1回までとする。
void log_slow(const std::string& key,
              std::chrono::steady_clock::duration elapsed,
              const std::string& details = {});
void log_current_stacktrace(const std::string& reason);
// デバッグ用の致命的エラー診断。指定された理由と現在のネイティブスタック
// を記録する。 Windowsではログと同じ場所へミニダンプも保存する。
void log_crash_diagnostics(const std::string& reason);
void install_crash_handlers();

// 非アクティブ操作を明示した制御セッションの実行中だけ真となる。 ネイティブUI
// はこれを参照し、テストウィンドウの表示時に最前面のフォーカスを奪わない。
void set_nonactive_automation_enabled(bool enabled) noexcept;
[[nodiscard]] bool nonactive_automation_enabled() noexcept;
// SW_SHOWNOACTIVATEだけでは、所有者の有効・無効の切り替
// えや ダイアログ管理によるフォーカス移動を防げないため、自動操作用のウ
// ィンドウには Win32のアクティブ化を抑止する拡張スタイルも付ける。
void prepare_nonactive_window(std::uintptr_t native_handle) noexcept;
inline constexpr std::uint32_t nonactive_listview_double_click_message =
    0x8000U + 0x321U;  // WM_APPに自動操作用の専用オフセットを加えたメッセージ番号。
inline constexpr std::uint32_t nonactive_debugger_command_message =
    0x8000U + 0x322U;
inline constexpr std::uint32_t nonactive_pointer_input_message =
    0x8000U + 0x323U;
inline constexpr std::int64_t nonactive_pointer_event_id = 0x4E41;

enum class NonactivePointerInput : std::uintptr_t {
    motion = 0,
    left_down,
    left_up,
    middle_down,
    middle_up,
    right_down,
    right_up,
};

class DebugControlServer {
public:
    DebugControlServer();
    ~DebugControlServer();
    DebugControlServer(const DebugControlServer&) = delete;
    DebugControlServer& operator=(const DebugControlServer&) = delete;

    void start(unsigned short port = 49321, bool nonactive_automation = false);
    void stop();
    [[nodiscard]] unsigned short port() const noexcept;
    // シリアル化済みで通信方式に依存しないJSONスナップショットのみを受け取る。
    // ソケットのワーカーから実行中のEngineやUIオブジェクトは参照しない。
    void publish_clickable_areas(std::string json);
    [[nodiscard]] bool wants_clickable_areas() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// ツール・テスト用の最小限のループバッククライアント。最初にJSONのhello
// を送り、 この接続で受信するサーバーのデータチャネルを選ぶ。 以後はコ
// マンドと非同期データを同じJSON Linesストリームで送受信する。
class DebugControlClient {
public:
    DebugControlClient();
    ~DebugControlClient();
    DebugControlClient(const DebugControlClient&) = delete;
    DebugControlClient& operator=(const DebugControlClient&) = delete;

    void connect(unsigned short port,
                 std::vector<std::string> receive = {"clickable_areas"});
    void close();
    void send_command(std::string_view command);
    [[nodiscard]] std::string receive_json(std::chrono::milliseconds timeout);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// すべてのホストのUIループから呼び出す処理。clickable_areas
// の購読が始まるまでは 何も行わず、通常のRelease動
// 作でスプライトやダイアログの走査コストを発生させない。
void refresh_debug_control_clickable_areas();

// プロトコルの契約テストと外部クライアントで使う、副作用のない補助関数。
[[nodiscard]] std::string debug_control_hello_json(
    const std::vector<std::string>& receiving);
[[nodiscard]] std::vector<std::string> debug_control_receive_channels(
    std::string_view hello_json);

}  // namespace cardwirth::debug
