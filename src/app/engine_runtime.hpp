#pragma once

#include <filesystem>
#include <array>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cw/dialog/presentation.hpp"
#include "platform/app_runtime_context.hpp"

namespace cardwirth::app {

namespace detail { class HostedEngineSessionImpl; }

}  // namespace cardwirth::app

namespace cw::lib { struct Surface; }
namespace cw::frame { class Frame; class NativeFrameAdapter; }

namespace cardwirth::app {

// 引数とパスは値型とし、ALLホストからEngineコアへWin32の wWinMain/CommandLineToArgvW
// への依存を持ち込まない。単独起動ではプロセスの エント
// リーポイント、またはengine_runtime.cpp
// のプラットフォーム別の代替処理から値を設定する。
enum class EngineHostMode {
    Standalone,
    All,
};

struct AppLaunchOptions {
    // 呼び出し元が選ぶのは外側のホストだけとし、ダイアログのモデル・
    // 配置・操作・検証は 共通のPresentation実装を使う。
    EngineHostMode host_mode{EngineHostMode::Standalone};
    std::filesystem::path executable_path{};
    std::filesystem::path application_directory{};
    std::filesystem::path user_data_root{};
    std::vector<std::string> arguments{};
};

namespace detail {

enum class HostedDropDispatchResult {
    MainWindow,
    DialogHandled,
    DialogInputRejected,
    StaleDialogInput,
};

// ホストの入力処理と契約テストで共用する。モデルIDが0ならメイン画面へのド
// ロップ。 終了済みのダイアログへのドロップや拒否されたドロップは消費する。
[[nodiscard]] HostedDropDispatchResult dispatch_hosted_dialog_drop_files(
    ::cw::frame::Frame& frame, const std::vector<std::string>& paths,
    std::uint64_t input_dialog_model_id, std::uint64_t published_dialog_model_id,
    const std::shared_ptr<::cw::dialog::DialogPresentationProvider>& published_provider);

}  // namespace detail

// Engineのストレージ移行の境界。現在も一部のパスは互換用UserData
// の作業ディレクトリ基準で読むが、 セッションは両方の論理マ
// ウントを保持する。各サービスをルート基準のI/Oへ移す際に
// 、 埋め込みホストとの境界を再変更せずに済むようにする。
struct EngineFileServices {
    ::cardwirth::platform::Storage* asset_storage{};
    ::cardwirth::platform::Storage* user_storage{};
    ::cardwirth::platform::AppPaths paths{};

    [[nodiscard]] bool configured() const noexcept {
        return asset_storage != nullptr || user_storage != nullptr;
    }
    [[nodiscard]] ::cardwirth::platform::Storage* assets() const noexcept {
        return asset_storage;
    }
    [[nodiscard]] ::cardwirth::platform::Storage* user() const noexcept {
        return user_storage;
    }
    [[nodiscard]] ::cardwirth::platform::Storage* for_root(
        ::cardwirth::platform::LogicalRoot root) const noexcept {
        switch (root) {
            case ::cardwirth::platform::LogicalRoot::App:
            case ::cardwirth::platform::LogicalRoot::Data:
            case ::cardwirth::platform::LogicalRoot::Skin:
                return asset_storage;
            case ::cardwirth::platform::LogicalRoot::User:
            case ::cardwirth::platform::LogicalRoot::UserData:
            case ::cardwirth::platform::LogicalRoot::Scenario:
            case ::cardwirth::platform::LogicalRoot::Yado:
            case ::cardwirth::platform::LogicalRoot::Config:
            case ::cardwirth::platform::LogicalRoot::Database:
            case ::cardwirth::platform::LogicalRoot::Temp:
            case ::cardwirth::platform::LogicalRoot::Cache:
            case ::cardwirth::platform::LogicalRoot::Trash:
                return user_storage;
        }
        return nullptr;
    }
};

// 既存のCardWirthのFrameとメッセージループを実行する、単
// 独起動・外部プロセス用の入口。 既定のコンテキストがすべてのグロー
// バルサービスを所有し、Windowsでの従来のプロセス動作を保つ。
int run_cardwirth_engine(const AppLaunchOptions& options = {});

// ホストのSDL・TTF・音声のライフサイクルを借り、呼び出し元
// のUIスレッドでEngineを実行する。 現在のEngineは
// ネイティブの最上位Frameと入れ子のメッセージループを所有す
// るため、同期呼び出しとする。 ホスト側からFrameを提供する
// 実装へ移行しても、起動設定や寿命の管理主体は変更せずに済む。
int run_engine_embedded(const ::cardwirth::platform::AppRuntimeContext& context,
                        const AppLaunchOptions& options = {});

struct HostedEngineFrameSnapshot {
    std::uint64_t sequence{};
    std::shared_ptr<const ::cw::lib::Surface> surface{};
    // CWPyが共通のFrame経由で通知するタイトル。 埋め込みホス
    // トは、Engineを表示しているネイティブウィンドウへ適用する。
    std::string title{};
};

enum class HostedEngineModalKind {
    Message,
    Settings,
    YadoSelect,
    YadoCreate,
    ScenarioSelect,
    CharacterInfo,
    CardInfo,
    // SDLホストで表示されていなかった共通モデルの全機
    // 能。 ホストは描画と入力だけを変換し、Android
    // 専用の縮小された項目・コマンドへ置き換えない。
    PartySelect,
    PlayerSelect,
    Album,
    PartyRecord,
    CardControl,
    AdventurerCreate,
    PartyEdit,
    BattleCommand,
    Instructions,
    FileSelect,
    TextInput,
    Document,
    // 共通の提供元は、同じ表示契約でEditorやDebugの画面
    // も公開できる。 ホスト側で独自の表示モデルを組み立てない。
    DebugEdit,
    DebugLog
};

// ホスト内の表示種別を、ダイアログ全体のモデルを所有す
// るFrameコマンドへ対応付ける。 Android/iOS
// ホストは倍率・配置・入力の変換を行えるが、 モ
// バイル専用の別のダイアログ実装へ切り替えない。
[[nodiscard]] constexpr std::string_view hosted_engine_modal_route(
    const HostedEngineModalKind kind) noexcept {
    switch (kind) {
        case HostedEngineModalKind::Message: return "YESNO";
        case HostedEngineModalKind::Settings: return "SETTINGS";
        case HostedEngineModalKind::YadoSelect: return "YADOSELECT";
        case HostedEngineModalKind::ScenarioSelect: return "SCENARIOSELECT";
        case HostedEngineModalKind::CharacterInfo: return "CHARAINFO";
        case HostedEngineModalKind::CardInfo: return "MENUCARDINFO";
        case HostedEngineModalKind::PartySelect: return "PARTYSELECT";
        case HostedEngineModalKind::PlayerSelect: return "PLAYERSELECT";
        case HostedEngineModalKind::Album: return "ALBUM";
        case HostedEngineModalKind::PartyRecord: return "PARTYRECORD";
        case HostedEngineModalKind::CardControl: return "BACKPACK";
        case HostedEngineModalKind::AdventurerCreate: return "DATACOMP";
        case HostedEngineModalKind::PartyEdit: return "PARTYEDIT";
        case HostedEngineModalKind::BattleCommand: return "BATTLECOMMAND";
        case HostedEngineModalKind::Instructions: return "INSTRUCTIONS";
        // これらは別のモデルの子、またはサービスから開くダイアログ
        // なので、 独立した最上位のFrameコマンドを持たない。
        case HostedEngineModalKind::YadoCreate:
        case HostedEngineModalKind::FileSelect:
        case HostedEngineModalKind::TextInput:
        case HostedEngineModalKind::Document:
        case HostedEngineModalKind::DebugEdit:
        case HostedEngineModalKind::DebugLog:
            return {};
    }
    return {};
}

using HostedEngineDialogAction = ::cw::dialog::DialogPresentationAction;
using HostedEngineDialogKey = ::cw::dialog::DialogPresentationKey;

// 共通ダイアログモデルが画面全体とともに公開する操作対
// 象。 各ホストでは画面と同じ座標変換だけを適用する。
using HostedEngineDialogControlSnapshot =
    ::cw::dialog::DialogPresentationControl;

struct HostedEngineModalSnapshot {
    std::uint64_t sequence{};
    // 共通Presentationが保持する画像の更新番号
    // 。 論理レイアウトを変えずに部分描画できるため、画
    // 像オブジェクトの同一性だけでは更新を判定できない。
    std::uint64_t surface_revision{};
    // 同じ共通ダイアログモデルでは、袋→保管庫→PCの持ち物などに表
    // 示先や種別が変わっても一定。 UI側の生ポインターは使わない。
    std::uint64_t model_id{};
    bool debugger_descendant{};
    // presentation_surfaceの論理1ピクセルあたりの画像ピクセ
    // ル数。 ツールチップ画像と同じ規則で、DebuggerはDPI倍率、それ以
    // 外はUP_WINを使う。 ALLはこの値で画像の寸法を論理サイズへ戻す。
    float presentation_scale{1.0F};
    // 共通モデルの配置APIが公開するピクセル座標での要
    // 求。 ホストはダイアログ画像と同じ倍率で変換する。
    double center_offset_x{}, center_offset_y{};
    std::string title{};
    HostedEngineModalKind kind{HostedEngineModalKind::Message};
    bool wants_text_input{};
    bool text_input_hardware_only{};
    std::string input_text{};
    std::string composition{};
    int composition_start{-1}, composition_length{};
    // ダイアログの全画像と意味を持つ操作対象は共通モデルが所
    // 有する。 ホスト側は画面の変換と操作の中継だけを行う。
    std::shared_ptr<const ::cw::lib::Surface> presentation_surface{};
    std::vector<HostedEngineDialogControlSnapshot> presentation_controls{};
};

enum class HostedDebuggerCommand {
    Show,
    Refresh,
    CompStamp,
    Gossip,
    SavedVariables,
    SavedJPDCImage,
    Money,
    Card,
    Member,
    Coupon,
    Status,
    Recovery,
    Redisplay,
    Friend,
    Info,
    PartyEnvironment,
    Round,
    Save,
    Load,
    InitVariables,
    LoadYado,
    BreakScenario,
    Editor,
    Area,
    Battle,
    Package,
    Selection,
    ShowParty,
    HideParty,
    Bgm,
    SelectedCard,
    StartEvent,
    Pause,
    Stop,
    StepInto,
    StepOver,
    StepReturn,
    Breakpoint,
    ClearBreakpoint,
    StackTrace,
    QuitDebugMode,
    MenuFile,
    MenuEdit,
    MenuScenario,
    MenuRun,
    Close
};

// デバッガーの操作部品には、モデル本来のPresentationコ
// ントロールを使う。 ALL専用のコマンド体系へ変換せず、行の選
// 択・実行や将来追加される部品も すべてのホストでそのまま扱う。
using HostedDebuggerControlSnapshot = ::cw::dialog::DialogPresentationControl;

// 既存デバッガーモデル全体を、表示方式に依存せず公開する。 すべて
// のホストはモデルの画像を描画し、公開された操作をセッションへ返す
// 。 プラットフォームごとに別のデバッガーレイアウトを持たない。
struct HostedDebuggerSnapshot {
    std::uint64_t sequence{};
    std::uint64_t tool_state{};
    bool visible{};
    bool paused{};
    int waittime{};
    bool wants_text_input{};
    int preferred_width{680};
    int preferred_height{720};
    int minimum_width{360};
    int minimum_height{420};
    std::string area{};
    std::string selected_member{};
    std::string selected_card{};
    std::string status{};
    // デバッガーの全画像と操作対象は共通の実装が所有
    // する。 ホストは拡縮できるが、機能を減らした専
    // 用ツールバーや簡易ダイアログへ置き換えない。
    std::shared_ptr<const ::cw::lib::Surface> presentation_surface{};
    std::vector<HostedDebuggerControlSnapshot> controls{};
    std::vector<std::string> event_rows{};
    std::vector<std::string> variable_rows{};
};

enum class HostedStatusAction {
    Settings, F9, Backlog, Debugger, InfoCards, CampTable,
    BattleAction, BattleAuto, BattleRunAway, BattleFriendCards, CancelCardControl,
    CloseDialog,
    // 右側のショートカットパネル（CardWirthPy_C_Allのみ。CardWirthPy
    // にはない）。 ToggleShortcutsは折り畳み・展開を切り替え、それ以外は ::cw::setting::AllStatusShortcut
    // のショートカット種別に対応する。
    ToggleShortcuts, Save, Load, Screenshot, Instructions,
    CardScreenshot,  // パーティカードのスクリーンショット（Python版のShift+Print Screen）。
    FullRecovery,    // デバッガーの全回復（デバッグモードでシナリオを実行中）。
    CustomStatus,    // 登録済みのカスタム状態をパーティへ適用する（デバッグモードでシナリオを実行
    // 中）。 ALLのホスト側のみで扱う操作として、メッセージログ表示中にステー
    // タスダイアログの ボタン以外を触れるとログを閉じる。両方のログ表示モードで
    // 共通のEscをホストから送り、 この列挙値自体はEngineへ渡さない。
    CancelBacklog,
    // ALL追加仕様: データ画面への移動はホストが処理し、ゲーム入力にしない。
    OpenData
};

// 横幅全体を使うコントロールの枠はALL側で描く。 ステータスバーのス
// プライトには別の枠が含まれているため、単体のアイコンとして使わない。
inline constexpr bool hosted_status_text_button(HostedStatusAction action) noexcept {
    return action == HostedStatusAction::CampTable ||
        action == HostedStatusAction::BattleAction ||
        action == HostedStatusAction::BattleRunAway ||
        action == HostedStatusAction::CancelCardControl;
}

struct HostedStatusButtonSnapshot {
    HostedStatusAction action{HostedStatusAction::Settings};
    std::string label{};
    bool enabled{true};
    bool pushed{};
    // 1なら90pxの行全体、0なら45pxの1セルを使う。
    bool full_width{};
    // 複製するボタンには、押下・通知・無効状態を含むスキン適用済みの画像を独立して
    // 渡す。 F9には元となるスプライトがない。画像はゲームスレッドで作り、動作中
    // のボタンの可変画像を共有しない。 CampTable・BattleAction
    // ・BattleRunAway・CancelCardControlでは枠付きの
    // 元画像を使わず、 ホスト側で現在の文字列を持つ横幅全体のボタンを一つ描く。
    std::shared_ptr<const ::cw::lib::Surface> image{};
};

struct HostedStatusSnapshot {
    std::string click_wait_notice;
    std::array<std::uint8_t,4> engine_frame_colour{128,0,0,255};
    std::uint64_t sequence{};
    bool visible{};
    bool yado_money_visible{};
    bool yado_money_enabled{};
    bool party_money_enabled{};
    std::string yado_money{"0"};
    std::string party_money{"0"};
    // ステータスバーの所持金パネルと同じアイコン（MONEYY/MONEYP）。
    // ゲームスレッドで独立したコピーを作り、画像が変わらない間は再利用する。
    std::shared_ptr<const ::cw::lib::Surface> yado_money_icon{};
    std::shared_ptr<const ::cw::lib::Surface> party_money_icon{};
    bool battle_round_visible{};
    std::string battle_round{"0"};
    std::vector<HostedStatusButtonSnapshot> buttons{};
    // 右側のショートカットパネル。Noneを除いた設定済みスロットを順
    // 番に保持し、 折り畳み・展開ボタンだけを表示する状態も保持する。
    bool shortcuts_folded{};
    std::vector<HostedStatusButtonSnapshot> shortcuts{};
    // メッセージログ（バックログ）が表示されている。
    bool backlog_showing{};
};

// CardWirthPy_C_All用の、処理をブロックしないEngineライフサイ
// クル。 Engineはゲーム用ワーカーと専用UIディスパッチャースレッドを持つが
// 、 ネイティブウィンドウやSDLイベントループは持たない。 ウィンドウ・イベント
// ・描画はALLだけが管理し、この境界で不変の画面画像と正規化した入力を交換する。
class HostedEngineSession final {
public:
    HostedEngineSession();
    ~HostedEngineSession();
    HostedEngineSession(const HostedEngineSession&) = delete;
    HostedEngineSession& operator=(const HostedEngineSession&) = delete;

    bool start(const ::cardwirth::platform::AppRuntimeContext& context,
               const AppLaunchOptions& options = {},
               std::uintptr_t native_host_window = 0);
    // ホスト側でホームへ戻る確認が明示的に済んだ場合だけ設定する。
    void request_stop(bool already_confirmed = false);
    [[nodiscard]] bool close_cancelled() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] int exit_code() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] HostedEngineFrameSnapshot frame_snapshot() const;
    // start()が設定したものと同じアダプターを
    // 返す。 埋め込み側はゲームワーカーの起動前から
    // 、共通Frame操作をこのアダプターへ渡せる。
    [[nodiscard]] std::shared_ptr<::cw::frame::NativeFrameAdapter> make_frame_adapter();
    [[nodiscard]] std::optional<HostedEngineModalSnapshot> modal_snapshot() const;
    // ホストは実際に表示した不変モデルを通知する。 ダ
    // イアログへの入力はその世代を対象とし、ワーカーへ
    // 処理を積んだ時点の最新モデルへは振り替えない。
    void displayed_dialog_model(std::uint64_t model_id) noexcept;
    // 既存のALLレイアウトで実際に配置したダイアログを通知する。 識
    // 別子で終了・置換済みのモーダル画面の座標や画像を除外する。 この
    // 通知処理自体はレイアウトやスクリーンショットの方針を決めない。
    void publish_screenshot_geometry(const std::optional<HostedEngineModalSnapshot>& shown,
        std::array<float,4> frame_bounds, std::array<float,4> dialog_bounds,
        bool debugger_focused);
    [[nodiscard]] HostedDebuggerSnapshot debugger_snapshot() const;
    [[nodiscard]] HostedStatusSnapshot status_snapshot() const;
    void status_action(HostedStatusAction action);
    // Engineのゲームスレッドで採取する読み取り専用の統
    // 合診断。 通常の入力経路と分離し、テストで配信・座標
    // 変換・選択のどこが失敗したかを区別できるようにする。
    [[nodiscard]] std::string input_probe() const;
    void dialog_action(HostedEngineDialogAction action, int index = -1,
                       std::uint32_t modifiers = 0);
    void dialog_pointer(HostedEngineDialogAction action, int index,
                        float x, float y, bool pressed,
                        ::cw::dialog::DialogPresentationModifiers modifiers = 0U);
    void dialog_key(HostedEngineDialogKey key, bool pressed,
                    ::cw::dialog::DialogPresentationModifiers modifiers = 0U);
    void dialog_scroll(float x, float y, int lines);
    void dialog_wheel(float x, float y, int rotation);
    void dialog_hover(float x, float y);
    void dialog_context(float x, float y);
    void dialog_middle(float x, float y, bool pressed);
    void dialog_drop_files(std::vector<std::string> paths);
    // デスクトップのカーソル位置に依存しない統合テスト用の入口。 製
    // 品UIは通常のEngineスプライトやステータスボタンから開き
    // 、 テストはそれらと同じFrameハンドラーを直接要求できる。
    void show_dialog_for_test(HostedEngineModalKind kind);
    void debugger_command(HostedDebuggerCommand command);
    void debugger_action(HostedEngineDialogAction action, int index = -1);
    void debugger_context(float x, float y);
    void debugger_hover(float x, float y);
    void debugger_scroll(float x, float y, int lines);
    void debugger_pointer(HostedEngineDialogAction action,int index,float x,float y,bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers=0);
    void debugger_pointer_cancel();
    void debugger_text(const std::string& text,bool composition=false,int start=-1,int length=0,
        ::cw::dialog::DialogPresentationModifiers modifiers=0);
    void debugger_blur();
    void debugger_key(HostedEngineDialogKey key, bool pressed,
                      ::cw::dialog::DialogPresentationModifiers modifiers = 0U);

    void pointer_motion(int x, int y, bool touch = false);
    // キャプチャのキャンセルでは、ボタン解放や実行のイベントを生成しない。
    void pointer_cancel();
    void pointer_button(int x, int y, int button, bool pressed, bool touch = false);
    void pointer_wheel(int x, int y, int delta);
    void key(int native_key, bool pressed, std::uint32_t modifiers = 0);
    void text_input(std::string utf8);
    void text_editing(std::string composition, int start, int length);
    void focus(bool focused);
    void suspend();
    void resume();
    // ALLのEngineが他のページに覆われている場合は、OSの最
    // 小化設定とは別に、 再表示されるまでゲーム内の時計を停止する。
    void set_covered(bool covered);
    void trim_memory();

private:
    std::unique_ptr<detail::HostedEngineSessionImpl> impl_;
};

}  // namespace cardwirth::app
