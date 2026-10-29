#include "cw/lib/exception_origin.hpp"
#include "cw/lib/runtime_access.hpp"
#include "engine_runtime.hpp"
#include "cw/dialog/richtext_scroll.hpp"
#include "cw/effectbooster.hpp"
#include "hosted_status_image.hpp"

#define SDL_MAIN_HANDLED 1

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "hosted_window_title.hpp"

#include "cw/cw.hpp"
#include "cw/content.hpp"
#include "cw/argparser.hpp"
#include "cw/bassplayer.hpp"
#include "cw/battle.hpp"
#include "cw/debug/debugger.hpp"
#include "cw/debug/statusedit.hpp"
#include "cw/frame.hpp"
#include "cw/eventhandler.hpp"
#include "cw/eventrelay.hpp"
#include "cw/data.hpp"
#include "cw/image.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/thread.hpp"
#include "cw/novel.hpp"
#include "cw/sprite/statusbar.hpp"
#include "status_text_metrics.hpp"
#include "cw/setting.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/placement.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/header.hpp"
#include "cw/util.hpp"
#include "debug_control_server.hpp"
#include "platform_font.hpp"
#include "platform/storage/engine_encryption.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <deque>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace cardwirth::app {

namespace {

using DebugCommand = HostedDebuggerCommand;

int debugger_value_id(const ::cw::lib::Value& value) {
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&value.storage()))
        return static_cast<int>(*integer);
    return -1;
}

int debugger_command_id(const DebugCommand command) {
    using namespace ::cw::debug::debugger;
    switch (command) {
        case DebugCommand::CompStamp: return debugger_value_id(g_ID_COMPSTAMP);
        case DebugCommand::Gossip: return debugger_value_id(g_ID_GOSSIP);
        case DebugCommand::SavedVariables: return debugger_value_id(g_ID_SAVEDVARIABLES);
        case DebugCommand::SavedJPDCImage: return debugger_value_id(g_ID_SAVEDJPDCIMAGE);
        case DebugCommand::Money: return debugger_value_id(g_ID_MONEY);
        case DebugCommand::Card: return debugger_value_id(g_ID_CARD);
        case DebugCommand::Member: return debugger_value_id(g_ID_MEMBER);
        case DebugCommand::Coupon: return debugger_value_id(g_ID_COUPON);
        case DebugCommand::Status: return debugger_value_id(g_ID_STATUS);
        case DebugCommand::Recovery: return debugger_value_id(g_ID_RECOVERY);
        case DebugCommand::Refresh: return debugger_value_id(g_ID_UPDATE);
        case DebugCommand::Redisplay: return debugger_value_id(g_ID_REDISPLAY);
        case DebugCommand::Friend: return debugger_value_id(g_ID_FRIEND);
        case DebugCommand::Info: return debugger_value_id(g_ID_INFO);
        case DebugCommand::PartyEnvironment: return debugger_value_id(g_ID_PARTY_ENV);
        case DebugCommand::Round: return debugger_value_id(g_ID_ROUND);
        case DebugCommand::Save: return debugger_value_id(g_ID_SAVE);
        case DebugCommand::Load: return debugger_value_id(g_ID_LOAD);
        case DebugCommand::InitVariables: return debugger_value_id(g_ID_INIT_VARIABLES);
        case DebugCommand::LoadYado: return debugger_value_id(g_ID_LOAD_YADO);
        case DebugCommand::BreakScenario: return debugger_value_id(g_ID_BREAK);
        case DebugCommand::Editor: return debugger_value_id(g_ID_EDITOR);
        case DebugCommand::Area: return debugger_value_id(g_ID_AREA);
        case DebugCommand::Battle: return debugger_value_id(g_ID_BATTLE);
        case DebugCommand::Package: return debugger_value_id(g_ID_PACK);
        case DebugCommand::Selection: return debugger_value_id(g_ID_SELECTION);
        case DebugCommand::ShowParty: return debugger_value_id(g_ID_SHOW_PARTY);
        case DebugCommand::HideParty: return debugger_value_id(g_ID_HIDE_PARTY);
        case DebugCommand::Bgm: return debugger_value_id(g_ID_BGM);
        case DebugCommand::SelectedCard: return debugger_value_id(g_ID_SELECTEDCARD);
        case DebugCommand::StartEvent: return debugger_value_id(g_ID_STARTEVENT);
        case DebugCommand::StepReturn: return debugger_value_id(g_ID_STEPRETURN);
        case DebugCommand::StepOver: return debugger_value_id(g_ID_STEPOVER);
        case DebugCommand::StepInto: return debugger_value_id(g_ID_STEPIN);
        case DebugCommand::Pause: return debugger_value_id(g_ID_PAUSE);
        case DebugCommand::Stop: return debugger_value_id(g_ID_STOP);
        case DebugCommand::Breakpoint: return debugger_value_id(g_ID_BREAKPOINT);
        case DebugCommand::ClearBreakpoint: return debugger_value_id(g_ID_CLEAR_BREAKPOINT);
        case DebugCommand::StackTrace: return debugger_value_id(g_ID_SHOW_STACK_TRACE);
        case DebugCommand::QuitDebugMode: return debugger_value_id(g_ID_QUIT_DEBUG_MODE);
        case DebugCommand::MenuFile: return DebuggerPresentationMenuFile;
        case DebugCommand::MenuEdit: return DebuggerPresentationMenuEdit;
        case DebugCommand::MenuScenario: return DebuggerPresentationMenuScenario;
        case DebugCommand::MenuRun: return DebuggerPresentationMenuRun;
        default: return -1;
    }
}


std::uint64_t common_debugger_tool_state(
    const ::cw::debug::debugger::Debugger& debugger) {
    std::uint64_t result = 1469598103934665603ULL;
    for (const auto& [id, enabled] : debugger.m_tool_enabled) {
        result ^= static_cast<std::uint64_t>(id);
        result *= 1099511628211ULL;
        result ^= enabled ? 1ULL : 0ULL;
        result *= 1099511628211ULL;
    }
    return result;
}

} // namespace

namespace detail {

struct HostedEngineInput {
    enum class Kind {
        Motion, Button, Wheel, Key, TextInput, TextEditing, Focus, Suspend, Covered, PointerCancel,
        Close, DebuggerCommand, DebuggerAction, DebuggerKey, DebuggerContext, DebuggerHover, DebuggerScroll,
        DebuggerPointer, DebuggerPointerCancel, DebuggerText, DebuggerBlur,
        DialogAction, DialogPointer, DialogKey, DialogScroll, DialogWheel, DialogDropFiles,
        DialogHover, DialogContext, DialogMiddle,
        TestDialog, StatusAction
    };
    Kind kind{Kind::Motion};
    int x{};
    int y{};
    int index{-1};
    int value{};
    bool pressed{};
    bool touch{};
    std::uint32_t modifiers{};
    std::string text{};
    std::vector<std::string> paths{};
    int editing_length{};
    // この入力を生成したダイアログのスナップショット識別子。
    // 待ち行列で待っている間にUIコールバックがダイアログを置
    // き換える場合があるため、 古い子画面へのOKやキー入力
    // を、新たに現れた親や無関係の画面へ送らないようにする。
    std::uint64_t dialog_model_id{};
#if defined(__EMSCRIPTEN__)
    // ホストからEngineへの待ち行列での待機時間を、ダイアログ処理とは分け
    // て計測する。 ミューテックスの競合も含めるため、ロック取得前に記録する。
    std::chrono::steady_clock::time_point queued_at{};
#endif
};

struct HostedEngineSessionState {
    mutable std::mutex mutex{};
    std::deque<HostedEngineInput> inputs{};
    HostedEngineFrameSnapshot frame{};
    HostedDebuggerSnapshot debugger{};
    HostedStatusSnapshot status{};
    std::atomic_bool status_snapshot_pending{false};
    std::optional<HostedEngineModalSnapshot> active_dialog{};
    std::uint64_t screenshot_model_id{};
    ::cw::frame::ScreenshotWindows screenshot_host{};
    bool screenshot_debugger_focused{};
    std::string input_probe{"cwpy=starting"};
    std::string error{};
    std::atomic_bool running{false};
    std::atomic_bool finished{false};
    std::atomic_bool force_stop{false};
    std::atomic_bool close_cancelled{false};
    std::atomic_bool status_action_dispatching{false};
    std::atomic_int exit_code{-1};
    std::uint64_t next_frame_sequence{1};
    std::uint64_t next_modal_sequence{1};
    std::uint64_t next_debugger_sequence{1};
    std::uint64_t next_status_sequence{1};
    bool hosted_f9_enabled{};
    std::chrono::steady_clock::time_point hosted_f9_checked_at{};
    std::uintptr_t native_host_window{};
    // ALLのショートカットで、直前のScreenshot/CardScreenshot
    // が完了した時刻。 読み書きはCWPyのゲームスレッドに限定する。
    std::chrono::steady_clock::time_point screenshot_completed_at{};
    std::chrono::steady_clock::time_point card_screenshot_completed_at{};
};

HostedDropDispatchResult dispatch_hosted_dialog_drop_files(
    ::cw::frame::Frame& frame, const std::vector<std::string>& paths,
    const std::uint64_t input_dialog_model_id,
    const std::uint64_t published_dialog_model_id,
    const std::shared_ptr<::cw::dialog::DialogPresentationProvider>& published_provider) {
    const auto current = std::dynamic_pointer_cast<::cw::dialog::DialogPresentationProvider>(
        frame.dialog_presentation_model());
    if (input_dialog_model_id == 0) {
        // メイン画面へのドロップは、共通Frameへ届く前にダ
        // イアログが開いても、 メイン画面宛てのままとする。
        auto event = std::make_shared<::cw::lib::Window>();
        event->dropped_files = paths;
        frame.dispatch_native_event("DROPFILES", std::move(event));
        return HostedDropDispatchResult::MainWindow;
    }
    if (input_dialog_model_id != published_dialog_model_id || !current ||
        current != published_provider) {
        // 置換・終了済みのダイアログへの入力は破棄し、
        // メインFrameや別の表示元へ振り替えない。
        return HostedDropDispatchResult::StaleDialogInput;
    }
    ::cw::dialog::DialogPresentationInputGuard input_guard(*current);
    if (!input_guard) return HostedDropDispatchResult::DialogInputRejected;
    current->dialog_presentation_drop_files(paths);
    return HostedDropDispatchResult::DialogHandled;
}

}  // namespace detail

namespace {

// 右側のショートカットパネル（CardWirthPy_C_Allのみ
// ）。保存・読込はキャンプの メニューカードと同じSAVE/LOAD
// 画面を開くため、それらのカードを使える状態に限り有効にする。 宿
// またはシナリオ内で、イベントと戦闘の実行中ではない場合が対象。
bool hosted_save_load_available(::cw::thread::CWPy& runtime) {
    if (!runtime.m_ydata || !*runtime.m_ydata) return false;
    if (runtime.m_status != "Yado" && runtime.m_status != "Scenario") return false;
    return !runtime.is_runningevent() && !runtime.is_battlestatus();
}

// ALL仕様: スクリーンショット・手札のスクリーンショットのショートカットは、
// 撮影が完了してから2秒間「完了」と表示し、その間に押しても何もしない。
constexpr auto kScreenshotDoneDisplay = std::chrono::seconds(2);
bool screenshot_done_showing(const std::chrono::steady_clock::time_point completed_at) {
    return completed_at.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - completed_at < kScreenshotDoneDisplay;
}
bool screenshot_done_showing(const detail::HostedEngineSessionState& state) {
    return screenshot_done_showing(state.screenshot_completed_at);
}
bool card_screenshot_done_showing(const detail::HostedEngineSessionState& state) {
    return screenshot_done_showing(state.card_screenshot_completed_at);
}

// デバッガーの全回復は、デバッグモードでシナリオを実行中に使える。 Debugger::refresh_tools
// はis_playingscenario()で有効化を判定する。
bool hosted_full_recovery_available(::cw::thread::CWPy& runtime) {
    return runtime.is_debugmode() && runtime.is_playingscenario();
}

// EventHandler::f8key_eventと同じ条件で、シナリ
// オ（読み込み失敗で失われたものを含む）に 解説がある場合に有効とする。
bool hosted_instructions_available(::cw::thread::CWPy& runtime) {
    std::shared_ptr<::cw::data::SystemData> data;
    if (runtime.m_ydata && *runtime.m_ydata && (*runtime.m_ydata)->m_losted_sdata &&
        *(*runtime.m_ydata)->m_losted_sdata)
        data = *(*runtime.m_ydata)->m_losted_sdata;
    else if (runtime.is_playingscenario()) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&runtime.m_sdata.storage()))
            data = std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
    }
    return data && !data->m_instructions.empty();
}

class HostedFrameAdapter final : public ::cw::frame::NativeFrameAdapter {
public:
    explicit HostedFrameAdapter(std::shared_ptr<detail::HostedEngineSessionState> state)
        : state_(std::move(state)) {}

    bool is_hosted() const noexcept override { return true; }

    std::optional<::cw::lib::Point> map_client_to_surface(
        const ::cw::frame::Frame&, ::cw::lib::Point point) const override {
        // ALL側で表示領域からEngine画像への座標変換は済んでい
        // るため、 単独起動用のウィンドウ倍率補正を重ねて適用しない。
        return point;
    }

    void attach_surface(::cw::frame::Frame&, const std::shared_ptr<::cw::lib::Window>& panel) override {
        if (panel) {
            panel->visible = true;
            panel->native_handle = state_->native_host_window;
        }
    }

    ::cw::lib::Rect display_geometry(const ::cw::frame::Frame&) const override {
        const auto virtual_width = std::max<::cw::lib::Integer>(
            1, std::get<0>(::cw::g_SIZE_SCR) * 2);
        const auto virtual_height = std::max<::cw::lib::Integer>(
            1, std::get<1>(::cw::g_SIZE_SCR) * 2);
#ifdef _WIN32
        const HWND host = reinterpret_cast<HWND>(state_->native_host_window);
        RECT client{};
        if (host && IsWindow(host) && GetClientRect(host, &client)) {
            const auto width = static_cast<::cw::lib::Integer>(
                std::max<LONG>(1, client.right - client.left));
            const auto height = static_cast<::cw::lib::Integer>(
                std::max<LONG>(1, client.bottom - client.top));
            // ALLはEngine画像を均一な倍率で表示領域へ収められるため
            // 、 Engineの設定には少なくとも2倍の仮想画面（1280x960
            // ）を渡す。 Python互換の上限計算にはSIZE_SCR
            // 、ゲーム画面サイズの表示にはSIZE_GAMEを使う。
            return {0, 0, (std::max)(width, virtual_width),
                    (std::max)(height, virtual_height)};
        }
#endif
        return {0, 0, virtual_width, virtual_height};
    }

    void set_icon(::cw::frame::Frame&, const std::shared_ptr<::cw::lib::Window>&) override {}

    void set_title(::cw::frame::Frame&, const ::cw::lib::String& title) override {
        // Python版のFrame.SetTitleはCWPyを所有するウィンドウを更新する。 ALL
        // ではシェルのウィンドウを共有するため、ホスト用スナップショットへ同じタイトルを渡す。
        std::scoped_lock lock(state_->mutex);
        publish_hosted_engine_title(state_->frame.title, title);
    }

    void bind(::cw::frame::Frame&, EventSink sink) override {
        std::scoped_lock lock(sink_mutex_);
        sink_ = std::move(sink);
    }

    bool post_exec(::cw::frame::Frame&) override { return true; }

    void present(::cw::frame::Frame& frame,
                 const std::optional<::cw::lib::Rect>&) override {
        std::shared_ptr<const ::cw::lib::Surface> surface;
        {
            std::scoped_lock lock(frame.m_present_mutex);
            surface = frame.m_present_surface;
        }
        if (!surface) return;
        std::scoped_lock lock(state_->mutex);
        state_->frame.sequence = state_->next_frame_sequence++;
        state_->frame.surface = std::move(surface);
    }

    void adjust_position(::cw::frame::Frame&,
                         const std::shared_ptr<::cw::lib::Window>& window) override {
        if (!window) return;
        // ホスト内のウィンドウはモデルとして扱い、ネイティブの位置は使わない
        // 。 中央配置と表示範囲への制限はALLのダイアログ層で計算する。
        window->bounds.x = std::max<::cw::lib::Integer>(
            0, (std::get<0>(::cw::g_SIZE_GAME) - window->bounds.width) / 2);
        window->bounds.y = std::max<::cw::lib::Integer>(
            0, (std::get<1>(::cw::g_SIZE_GAME) - window->bounds.height) / 2);
        // EngineパネルにはALLホストのハンドルを保持する。 Windowsの専用ダイ
        // アログは、AllDialogLayerが配置を制限する前にこれを所有者として使う。
    }

    void set_client_size(::cw::frame::Frame&,
                         const std::shared_ptr<::cw::lib::Window>& panel,
                         ::cw::lib::Integer width,
                         ::cw::lib::Integer height) override {
        if (panel) {
            panel->bounds.width = width;
            panel->bounds.height = height;
        }
    }

    bool dispatch_menu_accelerator(::cw::frame::Frame&, ::cw::lib::Integer) override {
        return false;
    }

    ::cw::frame::ScreenshotWindows screenshot_windows(::cw::frame::Frame&, bool capture_clients) override {
        std::scoped_lock lock(state_->mutex);
        auto snapshot=state_->screenshot_host;
        if(!state_->active_dialog || state_->active_dialog->model_id!=state_->screenshot_model_id)
            snapshot.windows.clear();
        if(capture_clients) for(auto& window:snapshot.windows) if(window.client) {
            auto image=std::make_shared<::cw::lib::Surface>(*window.client);
            // ホストのテクスチャのアルファ値は描画用の表現。 wxのClientDC
            // による取得結果は、どの環境でも表示済みのRGBクライアント画像とする。
            image->has_pixel_alpha=false;
            for(std::size_t i=3;i<image->rgba.size();i+=4)image->rgba[i]=255;
            const ::cw::lib::Size size{static_cast<::cw::lib::Integer>(window.bounds.width),
                                      static_cast<::cw::lib::Integer>(window.bounds.height)};
            if(size.width>0 && size.height>0 && (image->size.width!=size.width || image->size.height!=size.height))
                image=::cw::image::smoothscale(image,{size.width,size.height},true,false);
            window.client=std::move(image);
        }
        ::cw::frame::ScreenshotWindow debug;
        // Windowsのmaxマクロと衝突しないよう、数値の番兵値の呼び出しを括弧で囲む。
        debug.id=(std::numeric_limits<std::uint64_t>::max)();debug.parent=snapshot.root;
        debug.debugger=true;debug.visible=state_->debugger.visible;
        debug.focused=state_->screenshot_debugger_focused && debug.visible;
        snapshot.windows.push_back(std::move(debug));
        return snapshot;
    }
    bool compose_dialogs(::cw::frame::Frame& frame, ::cw::lib::Bitmap& bitmap,
        ::cw::lib::Integer y, std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer> fore,
        std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer> back) override {
        frame._put_dlgscreenshots(std::shared_ptr<::cw::lib::Bitmap>(&bitmap,[](auto*){}),y,fore,back);
        return true;
    }

    void watch_left_button(::cw::frame::Frame&, ::cw::lib::Integer) override {}

    std::optional<bool> modal_message(::cw::frame::Frame& frame,
                                       const ::cw::lib::String& title,
                                       const ::cw::lib::String& text,
                                       bool question, bool error) override {
    ::cw::lib::RuntimeAccess runtime_access;
        // 既存の共通Messageモデルをホスト内の入れ子ループで動かす。
        // アプリ独自のUIはDialogPresentationProvider
        // に集約し、 各ホストでは画面の合成と入力だけを受け持つ。
        (void)error;
#if !defined(__EMSCRIPTEN__) || CARDWIRTH_WEB_PTHREADS
        auto dialog = std::make_shared<::cw::dialog::message::Message>();
        auto parent = std::make_shared<::cw::lib::Window>();
        if (frame.m_active_dialog) parent->native_handle = frame.m_active_dialog->native_handle;
        const auto previous_model = frame.m_active_dialog_model;
        const auto previous_dialog = frame.m_active_dialog;
        const auto previous_session = frame.m_active_dialog_session;
        const auto previous_presentation = frame.dialog_presentation_model();
        const bool covered_transient = previous_presentation &&
            previous_presentation != previous_model;
        if (covered_transient)
            frame.clear_transient_dialog_model(previous_presentation.get());
        // initialize()より先に共通の表示元を登録する。
        // 移植可能なバックエンドはこの登録によって、接続済みのホスト
        // 内ダイアログと 実際にUIがないヘッドレス状態を区別する。
        frame.m_active_dialog_model = dialog;
        frame.m_active_dialog = dialog;
        frame.m_active_dialog->title = question ? "YESNO" : "MESSAGE";
        frame.m_active_dialog->visible = true;
        // この共通Messageは子画面なので、 親ダイアログやイベ
        // ントが所有するモーダルセッションのトークンを消費しない。
        frame.m_active_dialog_session = 0;
        dialog->initialize(parent, title, text, question ? 1 : 2, std::nullopt);
        while (!dialog->m_closed && !state_->force_stop.load()) {
            hosted_loop_step(frame);
            ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        if (!dialog->m_closed) dialog->close_with_result(::cw::dialog::message::kWxIdCancel);
        const bool accepted = dialog->m_return_code == ::cw::dialog::message::kWxIdOk;
        if (frame.m_active_dialog_model == dialog || !frame.m_active_dialog_model) {
            frame.m_active_dialog_model = previous_model;
            frame.m_active_dialog = previous_dialog;
            frame.m_active_dialog_session = previous_session;
            if (frame.m_active_dialog) frame.m_active_dialog->visible = true;
        }
        if (covered_transient)
            frame.set_transient_dialog_model(previous_presentation);
        return accepted;
#else
        return false;
#endif
    }

    std::optional<::cw::lib::Integer> modal_choices(
        ::cw::frame::Frame& frame, const ::cw::lib::String& title,
        const ::cw::lib::String& text,
        const std::vector<::cw::frame::ModalChoice>& choices) override {
    ::cw::lib::RuntimeAccess runtime_access;
#if !defined(__EMSCRIPTEN__) || CARDWIRTH_WEB_PTHREADS
        if (choices.empty()) return std::nullopt;
        auto dialog = std::make_shared<::cw::dialog::message::Message>();
        auto parent = std::make_shared<::cw::lib::Window>();
        if (frame.m_active_dialog) parent->native_handle = frame.m_active_dialog->native_handle;
        const auto previous_model = frame.m_active_dialog_model;
        const auto previous_dialog = frame.m_active_dialog;
        const auto previous_session = frame.m_active_dialog_session;
        const auto previous_presentation = frame.dialog_presentation_model();
        const bool covered_transient = previous_presentation &&
            previous_presentation != previous_model;
        if (covered_transient)
            frame.clear_transient_dialog_model(previous_presentation.get());
        frame.m_active_dialog_model = dialog;
        frame.m_active_dialog = dialog;
        frame.m_active_dialog->title = "MESSAGE_CHOICES";
        frame.m_active_dialog->visible = true;
        frame.m_active_dialog_session = 0;
        std::vector<::cw::dialog::message::MessageChoice> message_choices;
        message_choices.reserve(choices.size());
        for (const auto& choice : choices)
            message_choices.push_back({choice.label, choice.id, choice.width,
                                       choice.description});
        dialog->initialize(parent, title, text, 3, std::move(message_choices));
        while (!dialog->m_closed && !state_->force_stop.load()) {
            hosted_loop_step(frame);
            ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        if (!dialog->m_closed)
            dialog->close_with_result(::cw::dialog::message::kWxIdCancel);
        const auto selected = dialog->m_return_code;
        if (frame.m_active_dialog_model == dialog || !frame.m_active_dialog_model) {
            frame.m_active_dialog_model = previous_model;
            frame.m_active_dialog = previous_dialog;
            frame.m_active_dialog_session = previous_session;
            if (frame.m_active_dialog) frame.m_active_dialog->visible = true;
        }
        if (covered_transient)
            frame.set_transient_dialog_model(previous_presentation);
        return selected;
#else
        (void)frame;
        (void)title;
        (void)text;
        (void)choices;
        return std::nullopt;
#endif
    }

    std::optional<::cw::lib::String> modal_text_input(
        ::cw::frame::Frame& frame, const ::cw::lib::String& title,
        const ::cw::lib::String& message, const ::cw::lib::String& initial,
        const ::cw::lib::Integer max_length) override {
    ::cw::lib::RuntimeAccess runtime_access;
#if !defined(__EMSCRIPTEN__) || CARDWIRTH_WEB_PTHREADS
        auto dialog = std::make_shared<::cw::dialog::edit::InputTextDialog>();
        auto parent = std::make_shared<::cw::lib::Window>();
        if (frame.m_active_dialog) parent->native_handle = frame.m_active_dialog->native_handle;
        const auto previous_model = frame.m_active_dialog_model;
        const auto previous_dialog = frame.m_active_dialog;
        const auto previous_session = frame.m_active_dialog_session;
        const auto previous_presentation = frame.dialog_presentation_model();
        const bool covered_transient = previous_presentation &&
            previous_presentation != previous_model;
        if (covered_transient)
            frame.clear_transient_dialog_model(previous_presentation.get());
        frame.m_active_dialog_model = dialog;
        frame.m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        frame.m_active_dialog->title = "INPUT_TEXT";
        frame.m_active_dialog->visible = true;
        frame.m_active_dialog_session = 0;
        dialog->initialize(parent, title, message, initial,
                           std::max<::cw::lib::Integer>(1, max_length), {}, std::nullopt);
        while (!dialog->m_presentation_finished && !state_->force_stop.load()) {
            hosted_loop_step(frame);
            ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        const auto result = dialog->m_presentation_accepted
            ? std::optional<::cw::lib::String>(dialog->m_text) : std::nullopt;
        if (frame.m_active_dialog_model == dialog || !frame.m_active_dialog_model) {
            frame.m_active_dialog_model = previous_model;
            frame.m_active_dialog = previous_dialog;
            frame.m_active_dialog_session = previous_session;
            if (frame.m_active_dialog) frame.m_active_dialog->visible = true;
        }
        if (covered_transient)
            frame.set_transient_dialog_model(previous_presentation);
        return result;
#else
        (void)frame;
        (void)title;
        (void)message;
        (void)initial;
        (void)max_length;
        return std::nullopt;
#endif
    }

    bool modal_presentation(
        ::cw::frame::Frame& frame,
        std::shared_ptr<::cw::lib::Object> model) override {
    ::cw::lib::RuntimeAccess runtime_access;
#if !defined(__EMSCRIPTEN__) || CARDWIRTH_WEB_PTHREADS
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(model);
        if (!provider) return false;
        // ALL仕様: 親の大きさではなく、メイン画面を上限にする属性を孫まで引き継ぐ。
        const auto parent_provider=std::dynamic_pointer_cast<::cw::dialog::DialogPresentationProvider>(
            frame.dialog_presentation_model());
        provider->debugger_descendant=provider->debugger_descendant ||
            ::cw::dialog::current_dialog_input_from_debugger() ||
            (parent_provider && parent_provider->debugger_descendant);
        const auto previous_model = frame.m_active_dialog_model;
        const auto previous_dialog = frame.m_active_dialog;
        const auto previous_session = frame.m_active_dialog_session;
        const auto advertised_presentation = frame.dialog_presentation_model();
        // 共通ダイアログの中には、ホストが親の状態を取得す
        // る前にinitialize()で 自分を一時モデ
        // ルとして登録するものがある。新しい子自身を戻り先
        // にせず、 単独起動と同様に、まだ有効な親を使う。
        const bool model_already_transient = advertised_presentation == model &&
            previous_model && previous_model != model;
        const auto previous_presentation = model_already_transient
            ? previous_model : advertised_presentation;
        const bool covered_transient = previous_presentation &&
            previous_presentation != previous_model;
        const auto child_model = model;
        struct RestoreHostedParent final {
            std::function<void()> action;
            ~RestoreHostedParent() noexcept {
                try { action(); } catch (...) {}
            }
        } restore_parent{[&] {
            if (frame.m_active_dialog_model == child_model || !frame.m_active_dialog_model) {
                frame.m_active_dialog_model = previous_model;
                frame.m_active_dialog = previous_dialog;
                frame.m_active_dialog_session = previous_session;
                if (frame.m_active_dialog) frame.m_active_dialog->visible = true;
            }
            if (covered_transient) {
                try { frame.set_transient_dialog_model(previous_presentation); } catch (...) {}
            }
        }};
        if (covered_transient)
            frame.clear_transient_dialog_model(previous_presentation.get());
        frame.m_active_dialog_model = child_model;
        frame.m_active_dialog = std::make_shared<::cw::lib::Dialog>();
        frame.m_active_dialog->title = provider->dialog_presentation().title;
        frame.m_active_dialog->visible = true;
        frame.m_active_dialog_session = 0;
        while (!provider->dialog_presentation_finished() &&
               !state_->force_stop.load()) {
            // ホスト内表示と単独起動で、同じモーダル・更新規則を使う。
            if (::cw::g_cwpy) ::cw::g_cwpy->request_presentation_update();
            hosted_loop_step(frame);
            ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
        const bool finished = provider->dialog_presentation_finished();
        provider->dialog_presentation_complete();
        return finished;
#else
        (void)frame;
        (void)model;
        return false;
#endif
    }

    void hosted_loop_step(::cw::frame::Frame& frame) override {
    ::cw::lib::RuntimeAccess runtime_access;
        // ネイティブダイアログはこのEngineスレッドで作成される。 外側の
        // SDLループはALLシェルが管理するため、基本ダイアログモードでは
        // ここでこのスレッドのWin32メッセージを処理する。そうしないと描画
        // は続いても ボタンの解放やOKが処理されず、待ち続けることになる。
        ::cw::dialog::pump_all_mode_events();
        // ホスト内の入れ子モーダルでも、ワーカ
        // ーが発行した上書き確認などのUI要求
        // を処理できるよう、外側と同じFrame
        // のディスパッチャーを処理する。
        frame.drain_present_queue();
        frame.OnEXECFUNC(nullptr);
        std::deque<detail::HostedEngineInput> inputs;
        {
            std::scoped_lock lock(state_->mutex);
            inputs.swap(state_->inputs);
        }
        if (state_->force_stop.load()) {
            auto destroy = detail::HostedEngineInput{};
            destroy.kind = detail::HostedEngineInput::Kind::Close;
            destroy.value = 1;
            inputs.push_back(destroy);
        }

        for (const auto& input : inputs) {
            if (input.kind != detail::HostedEngineInput::Kind::StatusAction ||
                frame.dialog_presentation_model() || !::cw::g_cwpy) continue;
            const auto action = static_cast<HostedStatusAction>(input.value);
            // ホスト入力の消費はEngineのUIスレッドで行うが、ステ
            // ータス操作はCWPyのゲームスレッドに属する。 ここで非表
            // 示の横型ボタンのlclick_event()を呼ぶと、誤っ
            // たスレッドでスプライトの動作が始まり、 Engineが終了
            // してALLがホームへ戻る場合がある。解放操作をまとめ、 CWPy
            // のディスパッチャーから既存の操作処理だけを呼び出す。
            if (state_->status_action_dispatching.exchange(true)) continue;
            const auto session_state = state_;
            ::cw::g_cwpy->exec_func(
                [action, session_state](const ::cw::lib::Arguments&,
                                        const ::cw::lib::KeywordArguments&) {
                struct DispatchGuard final {
                    std::shared_ptr<detail::HostedEngineSessionState> state;
                    ~DispatchGuard() { state->status_action_dispatching = false; }
                } guard{session_state};
                const auto runtime = ::cw::g_cwpy;
                if (!runtime || !runtime->m_statusbar) return ::cw::lib::Value{};
                // ALL専用の右側ショートカット。キャンプのSAVE/LOAD
                // 、Print Screen、 シナリ
                // オ解説のF8と同じ既存の処理を使う。
                if (action == HostedStatusAction::ToggleShortcuts) {
                    if (runtime->m_setting) {
                        runtime->m_setting->m_all_status_shortcuts_folded =
                            !runtime->m_setting->m_all_status_shortcuts_folded;
                        runtime->m_setting->write();
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::Save || action == HostedStatusAction::Load) {
                    if (hosted_save_load_available(*runtime)) {
                        runtime->play_sound("click", false, 100, 1, 0, 0, false);
                        ::cw::content::PostEventContent::do_action(
                            "ShowDialog", ::cw::lib::String(
                                action == HostedStatusAction::Save ? "SAVE" : "LOAD"));
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::Screenshot) {
                    // ボタンが「完了」を表示している間は無視する。
                    if (screenshot_done_showing(*session_state)) return ::cw::lib::Value{};
                    // Print Screenと同じ撮影処理（EventHandler::printkey_event
                    // の can_input_sys()判定とcw.util.screenshot
                    // ）を直接呼び、 撮影が完了した場合だけラベルを切り替える。
                    if (const auto handler = runtime->get_eventhandler();
                        handler && handler->can_input_sys()) {
                        try {
                            ::cw::util::screenshot();
                            session_state->screenshot_completed_at = std::chrono::steady_clock::now();
                        } catch (const std::exception&) {}
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::CardScreenshot) {
                    // ボタンが「完了」を表示している間は無視する。
                    if (card_screenshot_done_showing(*session_state)) return ::cw::lib::Value{};
                    // Shift+Print Screenと同じ撮影処理（cw.util.card_screenshot）。
                    if (const auto handler = runtime->get_eventhandler();
                        handler && handler->can_input_sys() && !runtime->get_pcards(::cw::lib::Value{}).empty()) {
                        try {
                            ::cw::util::card_screenshot();
                            session_state->card_screenshot_completed_at = std::chrono::steady_clock::now();
                        } catch (const std::exception&) {}
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::Instructions) {
                    if (const auto handler = runtime->get_eventhandler())
                        handler->f8key_event();
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::FullRecovery) {
                    // デバッガーの全回復と同じ処理。
                    if (hosted_full_recovery_available(*runtime)) {
                        runtime->exec_func([](const auto&, const auto&) {
                            ::cw::debug::debugger::full_recovery_party();
                            return ::cw::lib::Value{};
                        });
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::CustomStatus) {
                    // 登録済みの状態を、デバッガーの状態編集で「全員」
                    // を選んで 「決定」した場合と同じように適用する。
                    if (hosted_full_recovery_available(*runtime) && runtime->m_setting) {
                        runtime->exec_func([text = runtime->m_setting->m_all_custom_status](
                                               const auto&, const auto&) {
                            ::cw::debug::statusedit::apply_custom_status_party(text);
                            return ::cw::lib::Value{};
                        });
                    }
                    return ::cw::lib::Value{};
                }
                if (action == HostedStatusAction::F9) {
                    if (const auto handler = runtime->get_eventhandler())
                        handler->f9key_event();
                    return ::cw::lib::Value{};
                }
                const auto activate = [&](const auto* tag) {
                    using Button = std::remove_cv_t<
                        std::remove_pointer_t<decltype(tag)>>;
                    for (const auto& object : runtime->m_statusbar->m_status_items) {
                        const auto button = std::dynamic_pointer_cast<Button>(object);
                        if (button && button->activate_from_host()) return true;
                    }
                    return false;
                };
                using namespace ::cw::sprite::statusbar;
                switch (action) {
                    case HostedStatusAction::CloseDialog:
                        if (runtime->m_setting) {
                            runtime->m_setting->m_show_all_status_dialog = false;
                            runtime->m_setting->write();
                        }
                        break;
                    case HostedStatusAction::Settings:
                        activate((SettingsButton*)nullptr); break;
                    case HostedStatusAction::Backlog:
                        activate((BacklogButton*)nullptr); break;
                    case HostedStatusAction::Debugger:
                        activate((DebuggerButton*)nullptr); break;
                    case HostedStatusAction::InfoCards:
                        activate((InfoCardsButton*)nullptr); break;
                    case HostedStatusAction::CampTable:
                        if (!activate((CampButton*)nullptr))
                            activate((TableButton*)nullptr);
                        break;
                    case HostedStatusAction::BattleAction:
                        activate((ActionButton*)nullptr); break;
                    case HostedStatusAction::BattleAuto:
                        activate((AutoStartButton*)nullptr); break;
                    case HostedStatusAction::BattleRunAway:
                        activate((RunAwayButton*)nullptr); break;
                    case HostedStatusAction::BattleFriendCards:
                        activate((ShowFriendCardsButton*)nullptr); break;
                    case HostedStatusAction::CancelCardControl:
                        activate((CancelButton*)nullptr); break;
                    case HostedStatusAction::F9:
                    case HostedStatusAction::ToggleShortcuts:
                    case HostedStatusAction::Save:
                    case HostedStatusAction::Load:
                    case HostedStatusAction::Screenshot:
                    case HostedStatusAction::Instructions:
                    case HostedStatusAction::CardScreenshot:
                    case HostedStatusAction::FullRecovery:
                    case HostedStatusAction::CustomStatus:
                    case HostedStatusAction::CancelBacklog:
                    case HostedStatusAction::OpenData: break;
                }
                return ::cw::lib::Value{};
            });
        }

        for (const auto& input : inputs) {
            if (input.kind != detail::HostedEngineInput::Kind::DebuggerCommand) continue;
            const auto command = static_cast<HostedDebuggerCommand>(input.value);
            if (command == HostedDebuggerCommand::Show) {
                if (::cw::g_cwpy) ::cw::g_cwpy->set_debug(true);
                frame.show_debugger(true);
                continue;
            }
            if (command == HostedDebuggerCommand::Close) {
                frame.close_debugger();
                continue;
            }
            if (!frame.m_debugger) continue;
            auto debugger = std::dynamic_pointer_cast<::cw::debug::debugger::Debugger>(
                std::static_pointer_cast<::cw::lib::Object>(*frame.m_debugger));
            if (!debugger) continue;
            // コマンドの準備と実行はモデルが管
            // 理する。ホストは意味に対応する
            // 共通のデバッガーIDへ変換するだ
            // けとし、別のUI実装を設けない。
            const auto command_id = debugger_command_id(command);
            if (command_id >= 0) {
                ::cw::dialog::DialogPresentationInputGuard guard(*debugger);
                if(guard) debugger->dispatch_command_id(command_id);
            }
        }
        for (const auto& input : inputs) {
            using Kind = detail::HostedEngineInput::Kind;
            if ((input.kind != Kind::DebuggerAction && input.kind != Kind::DebuggerKey &&
                 input.kind != Kind::DebuggerContext && input.kind != Kind::DebuggerHover &&
                 input.kind != Kind::DebuggerScroll && input.kind != Kind::DebuggerPointer &&
                 input.kind != Kind::DebuggerPointerCancel && input.kind != Kind::DebuggerText &&
                 input.kind != Kind::DebuggerBlur) || !frame.m_debugger) continue;
            const auto debugger = std::dynamic_pointer_cast<::cw::debug::debugger::Debugger>(
                std::static_pointer_cast<::cw::lib::Object>(*frame.m_debugger));
            if (!debugger || !debugger->m_visible) continue;
            // 入力順を保ち、保留中の右クリックでメニューを開いてから
            // 、 後続のキーや操作がそのメニューを使えるようにする。
            if (input.kind == Kind::DebuggerAction)
                debugger->dialog_presentation_action(
                    static_cast<::cw::dialog::DialogPresentationAction>(input.value), input.index);
            else if (input.kind == Kind::DebuggerKey)
                debugger->dialog_presentation_key(
                    static_cast<::cw::dialog::DialogPresentationKey>(input.value), input.pressed, input.modifiers);
            else if (input.kind == Kind::DebuggerText) {
                debugger->dialog_presentation_modifiers(input.modifiers);
                debugger->dialog_presentation_text_event({input.text,input.pressed,input.index,input.editing_length});
            }
            else if (input.kind == Kind::DebuggerBlur)
                debugger->dialog_presentation_focus(false);
            else if (input.kind == Kind::DebuggerContext)
                debugger->dialog_presentation_context(input.x, input.y);
            else if (input.kind == Kind::DebuggerHover)
                debugger->dialog_presentation_hover(input.x, input.y);
            else if (input.kind == Kind::DebuggerPointer) {
                debugger->dialog_presentation_modifiers(input.modifiers);
                debugger->dialog_presentation_pointer(
                    static_cast<::cw::dialog::DialogPresentationAction>(input.value),input.index,input.x,input.y,input.pressed);
            }
            else if (input.kind == Kind::DebuggerPointerCancel)
                debugger->dialog_presentation_pointer_cancel();
            else
                debugger->dialog_presentation_scroll(input.x, input.y, input.value);
        }
        for (const auto& input : inputs) {
            if (input.kind != detail::HostedEngineInput::Kind::TestDialog) continue;
            // 実際のメニュークリックと同じゲームスレッド・モーダルスタック
            // の経路を使う。 Frame::On*を直接呼ぶとネイティブ
            // 画面は作られるが、call_modaldlgの寿命・終了管
            // 理を 通らないため、ボタンのテストが誤って停止状態になる。
            const auto kind = static_cast<HostedEngineModalKind>(input.value);
            const auto route = hosted_engine_modal_route(kind);
            const auto name = ::cw::lib::String(route);
            if (!name.empty() && ::cw::g_cwpy) {
                ::cw::g_cwpy->exec_func([name](const auto&, const auto&) {
                    if (::cw::g_cwpy) ::cw::g_cwpy->call_modaldlg(name, {});
                    return ::cw::lib::Value{};
                });
            }
        }
        bool dialog_presentation_changed = false;
        for (const auto& input : inputs) {
            if (input.kind==detail::HostedEngineInput::Kind::PointerCancel ||
                (input.kind==detail::HostedEngineInput::Kind::Focus && !input.pressed) ||
                (input.kind==detail::HostedEngineInput::Kind::Suspend && input.pressed)) {
                const auto provider=std::dynamic_pointer_cast<::cw::dialog::DialogPresentationProvider>(
                    frame.dialog_presentation_model());
                const auto published=published_dialog_provider_.lock();
                // 現在のモデルと、以前公開した親の両方でキャプチャ
                // を停止する。 キャンセル時にUPを合成しない。
                if (provider) {::cw::dialog::cancel_dialog_utility_pointer(*provider);provider->dialog_presentation_pointer_cancel();}
                if (published && published!=provider) published->dialog_presentation_pointer_cancel();
                dialog_presentation_changed=true;
            }
            if (input.kind != detail::HostedEngineInput::Kind::DialogHover)
                dialog_presentation_changed = dialog_tooltip_.clear() || dialog_presentation_changed;
            const bool targets_dialog =
                input.kind == detail::HostedEngineInput::Kind::DialogAction ||
                input.kind == detail::HostedEngineInput::Kind::DialogPointer ||
                input.kind == detail::HostedEngineInput::Kind::DialogHover ||
                input.kind == detail::HostedEngineInput::Kind::DialogContext ||
                input.kind == detail::HostedEngineInput::Kind::DialogMiddle ||
                input.kind == detail::HostedEngineInput::Kind::DialogKey ||
                input.kind == detail::HostedEngineInput::Kind::DialogScroll ||
                input.kind == detail::HostedEngineInput::Kind::DialogWheel ||
                input.kind == detail::HostedEngineInput::Kind::TextInput ||
                input.kind == detail::HostedEngineInput::Kind::TextEditing;
            if (targets_dialog) {
                const auto current = std::dynamic_pointer_cast<
                    ::cw::dialog::DialogPresentationProvider>(frame.dialog_presentation_model());
                const auto published = published_dialog_provider_.lock();
                if (input.dialog_model_id == 0 ||
                    input.dialog_model_id != published_dialog_model_id_ ||
                    !current || current != published) {
                    // 待ち行列で待つ間に、対象のスナップショットが覆
                    // われたか閉じられた。 Python/wxではウ
                    // ィンドウとともに入力先が破棄されるため、 現
                    // 在のモデルへ振り替えず、この入力を破棄する。
                    continue;
                }
            }
            if (input.kind == detail::HostedEngineInput::Kind::DialogAction) {
                apply_dialog_action(frame, input);
                dialog_presentation_changed = true;
            } else if (input.kind == detail::HostedEngineInput::Kind::DialogPointer) {
                apply_dialog_pointer(frame, input);
                dialog_presentation_changed = true;
            } else if (input.kind == detail::HostedEngineInput::Kind::DialogHover ||
                       input.kind == detail::HostedEngineInput::Kind::DialogContext ||
                       input.kind == detail::HostedEngineInput::Kind::DialogMiddle) {
                const auto model = frame.dialog_presentation_model();
                const auto provider = std::dynamic_pointer_cast<::cw::dialog::DialogPresentationProvider>(model);
                if (provider) {
                    ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
                    if (!input_guard) continue;
                    if (input.kind == detail::HostedEngineInput::Kind::DialogHover) {
                        ::cw::dialog::update_dialog_utility_hover(*provider,published_dialog_presentation_,
                            static_cast<float>(input.x),static_cast<float>(input.y));
                        provider->dialog_presentation_hover(static_cast<float>(input.x), static_cast<float>(input.y));
                        if (published_dialog_provider_.lock()==provider)
                            dialog_presentation_changed = dialog_tooltip_.update(published_dialog_presentation_,
                                static_cast<float>(input.x),static_cast<float>(input.y)) || dialog_presentation_changed;
                    }
                    else if (input.kind == detail::HostedEngineInput::Kind::DialogMiddle)
                        provider->dialog_presentation_middle(static_cast<float>(input.x),static_cast<float>(input.y),input.pressed);
                    else provider->dialog_presentation_context(static_cast<float>(input.x), static_cast<float>(input.y));
                    dialog_presentation_changed = dialog_presentation_changed || provider->dialog_presentation_needs_refresh();
                    if (provider->dialog_presentation_finished()) {
                        provider->dialog_presentation_complete();
                        frame.clear_transient_dialog_model(model.get());
                        if (frame.m_active_dialog_model == model) frame.kill_dlg(std::nullopt, false, true);
                    }
                }
            } else if (input.kind == detail::HostedEngineInput::Kind::DialogKey) {
                apply_dialog_key(frame, input);
                dialog_presentation_changed = true;
            } else if (input.kind == detail::HostedEngineInput::Kind::DialogScroll ||
                       input.kind == detail::HostedEngineInput::Kind::DialogWheel) {
                apply_dialog_scroll(frame, input);
                dialog_presentation_changed = true;
            } else if (input.kind == detail::HostedEngineInput::Kind::DialogDropFiles) {
                const auto delivery = detail::dispatch_hosted_dialog_drop_files(
                    frame, input.paths, input.dialog_model_id,
                    published_dialog_model_id_, published_dialog_provider_.lock());
                // 期限切れの対象へのドロップは、更新を通知せず消費
                // する。 それ以外は従来のホスト更新規則に従う。
                if (delivery != detail::HostedDropDispatchResult::StaleDialogInput)
                    dialog_presentation_changed = true;
            } else if (input.kind == detail::HostedEngineInput::Kind::TextInput ||
                       input.kind == detail::HostedEngineInput::Kind::TextEditing) {
                apply_dialog_text(frame, input);
                dialog_presentation_changed = true;
            }
        }
        publish_active_dialog(frame, dialog_presentation_changed);
        publish_status();
        publish_debugger(frame);

        EventSink sink;
        {
            std::scoped_lock lock(sink_mutex_);
            sink = sink_;
        }
        if (!sink) return;
        for (const auto& input : inputs) {
            auto event = std::make_shared<::cw::lib::Window>();
            event->bounds.x = input.x;
            event->bounds.y = input.y;
            event->id = input.value;
            event->touch = input.touch;
            event->modifiers = input.modifiers;
            switch (input.kind) {
                case detail::HostedEngineInput::Kind::PointerCancel:
                    sink("POINTERCANCEL", event);
                    break;
                case detail::HostedEngineInput::Kind::Motion:
                    sink("MOTION", event);
                    break;
                case detail::HostedEngineInput::Kind::Button:
                    if (input.value == 1) sink(input.pressed ? "LEFTDOWN" : "LEFTUP", event);
                    else if (input.value == 2) sink(input.pressed ? "MIDDLEDOWN" : "MIDDLEUP", event);
                    else sink(input.pressed ? "RIGHTDOWN" : "RIGHTUP", event);
                    break;
                case detail::HostedEngineInput::Kind::Wheel:
                    sink("MOTION", event);
                    event->id = input.value;
                    sink("MOUSEWHEEL", event);
                    break;
                case detail::HostedEngineInput::Kind::Key:
                    sink(input.pressed ? "KEYDOWN" : "KEYUP", event);
                    break;
                case detail::HostedEngineInput::Kind::TextInput:
                case detail::HostedEngineInput::Kind::TextEditing:
                    break;
                case detail::HostedEngineInput::Kind::Focus:
                    sink(input.pressed ? "SETFOCUS" : "KILLFOCUS", event);
                    break;
                case detail::HostedEngineInput::Kind::Suspend:
                    event->id = input.pressed ? 1 : 0;
                    frame.OnIconize(event);
                    break;
                case detail::HostedEngineInput::Kind::Covered:
                    frame.set_host_covered(input.pressed);
                    break;
                case detail::HostedEngineInput::Kind::Close:
                    // 所有するEditorの終了をホストが待つ間に、終了確認
                    // がキャンセルされる場合がある。 モーダルが消えたこと
                    // だけではOKと区別できないため、選択結果を通知する。
                    sink(input.value != 0 ? "DESTROY" : "CLOSE", event);
                    if (input.value == 0 && !::cw::g_quit_app)
                        state_->close_cancelled.store(true, std::memory_order_release);
                    break;
                case detail::HostedEngineInput::Kind::DebuggerCommand:
                case detail::HostedEngineInput::Kind::DebuggerAction:
                case detail::HostedEngineInput::Kind::DebuggerKey:
                case detail::HostedEngineInput::Kind::DebuggerContext:
                case detail::HostedEngineInput::Kind::DebuggerHover:
                case detail::HostedEngineInput::Kind::DebuggerScroll:
                case detail::HostedEngineInput::Kind::DebuggerPointer:
                case detail::HostedEngineInput::Kind::DebuggerPointerCancel:
                case detail::HostedEngineInput::Kind::DebuggerText:
                case detail::HostedEngineInput::Kind::DebuggerBlur:
                case detail::HostedEngineInput::Kind::DialogAction:
                case detail::HostedEngineInput::Kind::DialogPointer:
                case detail::HostedEngineInput::Kind::DialogHover:
                case detail::HostedEngineInput::Kind::DialogContext:
                case detail::HostedEngineInput::Kind::DialogMiddle:
                case detail::HostedEngineInput::Kind::DialogKey:
                case detail::HostedEngineInput::Kind::DialogScroll:
                case detail::HostedEngineInput::Kind::DialogWheel:
                case detail::HostedEngineInput::Kind::DialogDropFiles:
                case detail::HostedEngineInput::Kind::TestDialog:
                case detail::HostedEngineInput::Kind::StatusAction:
                    break;
            }
        }
        publish_input_probe();
        ::cw::dialog::pump_all_mode_events();
    }

private:
    std::chrono::steady_clock::time_point status_requested_at_{};
    std::weak_ptr<::cw::dialog::DialogPresentationProvider> published_dialog_provider_{};
    std::uint64_t published_dialog_model_id_{};
    ::cw::dialog::DialogPresentation published_dialog_presentation_{};
    ::cw::dialog::DialogPresentationTooltip dialog_tooltip_{};
    std::chrono::steady_clock::time_point published_dialog_at_{};
    std::uint32_t published_dialog_refresh_interval_ms_{};
    bool published_dialog_throttle_refresh_{};
#if defined(__EMSCRIPTEN__)
    std::uint64_t dialog_profile_us_{}, dialog_profile_max_us_{}, dialog_profile_count_{};
    std::chrono::steady_clock::time_point dialog_profile_logged_at_{};
#endif

    void publish_input_probe() {
        // 共通ランタイムの排他制御で、このUIスナップショットの取得とゲーム側の書き込みを直列化する。
        ::cw::lib::RuntimeAccess runtime_access;
        std::string probe = "cwpy=none";
        if (::cw::g_cwpy) {
            const auto integer = [](::cw::lib::Value value) {
                try { return value.as_integer(); } catch (...) { return ::cw::lib::Integer{-1}; }
            };
            std::ostringstream out;
            out << "wx=" << integer(std::get<0>(::cw::g_cwpy->m_wxmousepos))
                << ',' << integer(std::get<1>(::cw::g_cwpy->m_wxmousepos))
                << " mouse=" << integer(std::get<0>(::cw::g_cwpy->m_mousepos))
                << ',' << integer(std::get<1>(::cw::g_cwpy->m_mousepos))
                << " index=" << ::cw::g_cwpy->m_index
                << " selection=" << (::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection ? 1 : 0)
                << " status=" << ::cw::g_cwpy->m_status
                << " area=" << ::cw::g_cwpy->m_areaid
                << " showingdlg=" << (::cw::g_cwpy->is_showingdlg() ? 1 : 0)
                << " up_win=" << ::cw::g_UP_WIN
                << " up_scr=" << ::cw::g_UP_SCR;
            // pygame互換の押下中キー番号を、既存のスレッド安全な
            // ホスト診断に含める。 SDLスレッドからEngineの
            // 可変状態へ直接触れず、SDL→ALLホスト→Frame
            // →KeyEventRelayの 入力経路全体を検証でき
            // るようにする。ハイフンは押下中のキーがないことを表す。
            out << " keys=";
            bool any_key = false;
            if (::cw::g_cwpy->m_keyevent) {
                const auto pressed = ::cw::g_cwpy->m_keyevent->get_pressed();
                for (std::size_t key = 0; key < pressed.size(); ++key) {
                    if (pressed[key] <= 0) continue;
                    if (any_key) out << ',';
                    out << key << ':' << pressed[key];
                    any_key = true;
                }
            }
            if (!any_key) out << '-';
            probe = std::move(out).str();
        }
        std::scoped_lock lock(state_->mutex);
        state_->input_probe = std::move(probe);
    }


    void publish_modal_snapshot(HostedEngineModalSnapshot next) {
        std::scoped_lock lock(state_->mutex);
        const auto same_surface = [](const auto& left, const auto& right) {
            if (left == right) return true;
            if (!left || !right || left->size.width != right->size.width ||
                left->size.height != right->size.height)
                return false;
            return left->rgba == right->rgba;
        };
        const auto same = state_->active_dialog &&
            state_->active_dialog->model_id == next.model_id &&
            state_->active_dialog->debugger_descendant == next.debugger_descendant &&
            state_->active_dialog->presentation_scale == next.presentation_scale &&
            state_->active_dialog->center_offset_x == next.center_offset_x &&
            state_->active_dialog->center_offset_y == next.center_offset_y &&
            state_->active_dialog->title == next.title &&
            state_->active_dialog->kind == next.kind &&
            state_->active_dialog->wants_text_input == next.wants_text_input &&
            state_->active_dialog->text_input_hardware_only == next.text_input_hardware_only &&
            state_->active_dialog->input_text == next.input_text &&
            state_->active_dialog->composition == next.composition &&
            state_->active_dialog->composition_start == next.composition_start &&
            state_->active_dialog->composition_length == next.composition_length &&
            state_->active_dialog->surface_revision == next.surface_revision &&
            state_->active_dialog->presentation_controls == next.presentation_controls &&
            same_surface(state_->active_dialog->presentation_surface,
                         next.presentation_surface);
        if (same) return;
#if defined(__ANDROID__)
        SDL_Log("CardWirthDialog common surface kind=%d size=%dx%d controls=%zu",
                static_cast<int>(next.kind),
                next.presentation_surface ? static_cast<int>(next.presentation_surface->size.width) : 0,
                next.presentation_surface ? static_cast<int>(next.presentation_surface->size.height) : 0,
                next.presentation_controls.size());
#endif
        next.sequence = state_->next_modal_sequence++;
        if(state_->screenshot_model_id!=next.model_id) {
            state_->screenshot_model_id=0;state_->screenshot_host.windows.clear();
        }
        state_->active_dialog = std::move(next);
    }

    void publish_active_dialog(::cw::frame::Frame& frame, const bool force_refresh) {
        const auto model = frame.dialog_presentation_model();
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(model);
        if (!provider) {
#if defined(__EMSCRIPTEN__)
            if (dialog_profile_count_) {
                SDL_Log("Web dialog summary: route=%s frames=%llu mean_ms=%.2f max_ms=%.2f",
                        published_dialog_presentation_.route.c_str(),
                        static_cast<unsigned long long>(dialog_profile_count_),
                        static_cast<double>(dialog_profile_us_) / dialog_profile_count_ / 1000.0,
                        dialog_profile_max_us_ / 1000.0);
                dialog_profile_us_ = dialog_profile_max_us_ = dialog_profile_count_ = 0;
            }
#endif
            if (const auto previous = published_dialog_provider_.lock()) {
                previous->dialog_presentation_pointer_cancel();
                ::cw::dialog::clear_dialog_debug_snapshot(previous.get());
            }
            published_dialog_provider_.reset();
            published_dialog_presentation_ = {};
            dialog_tooltip_.clear();
            published_dialog_refresh_interval_ms_ = 0;
            published_dialog_throttle_refresh_ = false;
            std::scoped_lock lock(state_->mutex);
            if (state_->active_dialog) {
                state_->active_dialog.reset();
                state_->screenshot_model_id=0;state_->screenshot_host.windows.clear();
            }
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool provider_changed = published_dialog_provider_.lock() != provider;
        const bool provider_refresh = provider->dialog_presentation_needs_refresh();
#if defined(__EMSCRIPTEN__)
        if (provider_changed) dialog_profile_us_ = dialog_profile_max_us_ = dialog_profile_count_ = 0;
#endif
        if (provider_changed) {
            if (const auto previous = published_dialog_provider_.lock())
                previous->dialog_presentation_pointer_cancel();
            dialog_tooltip_.clear();
        }
        if (published_dialog_throttle_refresh_ && !force_refresh &&
            !provider_changed && !provider_refresh) {
            if (published_dialog_refresh_interval_ms_ == 0) return;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - published_dialog_at_);
            if (elapsed.count() < published_dialog_refresh_interval_ms_) return;
        }

        provider->begin_dialog_presentation_refresh();
        auto common = provider->dialog_presentation();
        provider->debugger_descendant=provider->debugger_descendant || common.debugger;
        ::cw::dialog::decorate_dialog_touch_tools(common,*provider);
        if (!common.surface) {
            provider->dialog_presentation_pointer_cancel();
            std::scoped_lock lock(state_->mutex);
            state_->error =
                "Common dialog did not provide its presentation surface";
            state_->active_dialog.reset();
            state_->screenshot_model_id=0;state_->screenshot_host.windows.clear();
            return;
        }

        if (provider_changed) ++published_dialog_model_id_;
        published_dialog_provider_ = provider;
        published_dialog_at_ = now;
        published_dialog_refresh_interval_ms_ = common.refresh_interval_ms;
        published_dialog_throttle_refresh_ = common.throttle_refresh;
        published_dialog_presentation_ = common;
        ::cw::dialog::publish_dialog_debug_snapshot(provider.get(), common);
        common.surface = dialog_tooltip_.compose(common);

        HostedEngineModalSnapshot snapshot;
        snapshot.model_id = published_dialog_model_id_;
        if (const auto offset = ::cw::dialog::dialog_center_offset(model)) {
            snapshot.center_offset_x = offset->x;
            snapshot.center_offset_y = offset->y;
        }
        snapshot.debugger_descendant=provider->debugger_descendant;
        {
            // DialogPresentationTooltip::compose()と同じラスタ倍率の規則を使う。
            const double scale=common.debugger
                ? std::max<double>(1.0,static_cast<double>(::cw::g_dpi_level))
                : static_cast<double>(::cw::g_UP_WIN);
            snapshot.presentation_scale=std::isfinite(scale)&&scale>0.0
                ? static_cast<float>(scale) : 1.0F;
        }
        // Kindは診断・自動操作用の経路情報に限る。 ホスト側
        // ではこれを使ってダイアログの内容や操作部品を選ばない。
        snapshot.kind = HostedEngineModalKind::Message;
        for (int value = static_cast<int>(HostedEngineModalKind::Message);
             value <= static_cast<int>(HostedEngineModalKind::DebugLog); ++value) {
            const auto candidate = static_cast<HostedEngineModalKind>(value);
            const auto route = hosted_engine_modal_route(candidate);
            if (!route.empty() && route == common.route) {
                snapshot.kind = candidate;
                break;
            }
        }
        if (common.route == "DEBUGLOG")
            snapshot.kind = HostedEngineModalKind::DebugLog;
        snapshot.title = std::move(common.title);
        snapshot.wants_text_input = provider->dialog_presentation_text_input_requested(common);
        snapshot.text_input_hardware_only = common.text_input_hardware_only;
        snapshot.input_text = std::move(common.input_text);
        snapshot.composition = std::move(common.composition);
        snapshot.composition_start = common.composition_start;
        snapshot.composition_length = common.composition_length;
        snapshot.surface_revision = common.surface_revision;
        std::shared_ptr<const ::cw::lib::Surface> previous_surface;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->active_dialog &&
                state_->active_dialog->model_id == snapshot.model_id)
                previous_surface = state_->active_dialog->presentation_surface;
        }
        // ALLの合成処理は別のUIスレッドで動く。 表示元が再利用する画像へ
        // のconstポインターは、独立した表示スナップショットにはならない。
        snapshot.presentation_surface = freeze_hosted_image(common.surface, previous_surface);
        snapshot.presentation_controls = std::move(common.controls);
        publish_modal_snapshot(std::move(snapshot));
        provider->dialog_presentation_refreshed();
#if defined(__EMSCRIPTEN__)
        const auto completed_at = std::chrono::steady_clock::now();
        const auto elapsed_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            completed_at - now).count());
        dialog_profile_us_ += elapsed_us;
        dialog_profile_max_us_ = (std::max)(dialog_profile_max_us_, elapsed_us);
        ++dialog_profile_count_;
        if (elapsed_us >= 50000 && completed_at - dialog_profile_logged_at_ >= std::chrono::seconds(1)) {
            SDL_Log("Web dialog render: route=%s elapsed_ms=%lld", common.route.c_str(),
                    static_cast<long long>(elapsed_us / 1000));
            dialog_profile_logged_at_ = completed_at;
        }
#endif
    }

    void apply_dialog_text(::cw::frame::Frame& frame,
                           const detail::HostedEngineInput& input) {
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(
                frame.dialog_presentation_model());
        if (provider) {
            ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
            if (!input_guard) return;
            provider->dialog_presentation_text_event({input.text,
                input.kind == detail::HostedEngineInput::Kind::TextEditing,
                input.kind == detail::HostedEngineInput::Kind::TextEditing ? input.x : -1,
                input.kind == detail::HostedEngineInput::Kind::TextEditing ? input.editing_length : 0});
        }
    }

    void apply_dialog_action(::cw::frame::Frame& frame,
                              const detail::HostedEngineInput& input) {
#if defined(__EMSCRIPTEN__)
        const auto started = std::chrono::steady_clock::now();
#endif
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(
                frame.dialog_presentation_model());
        if (!provider) return;
        ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
        if (!input_guard) return;
        provider->note_dialog_presentation_user_input();

        provider->dialog_presentation_modifiers(input.modifiers);
        const auto action=static_cast<HostedEngineDialogAction>(input.value);
        if (!::cw::dialog::dispatch_dialog_utility_action(*provider,action))
            provider->dialog_presentation_action(action, input.index);
        if (provider->dialog_presentation_finished()) {
            const auto completed_model = frame.dialog_presentation_model();
            provider->dialog_presentation_complete();
            frame.clear_transient_dialog_model(completed_model.get());
            if (frame.m_active_dialog_model.get() == completed_model.get())
                frame.kill_dlg(std::nullopt, false, true);
        }
#if defined(__EMSCRIPTEN__)
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto queue_ms = input.queued_at == std::chrono::steady_clock::time_point{}
            ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(
                started - input.queued_at).count();
        if (elapsed_ms >= 50 || queue_ms >= 50 || provider->dialog_presentation_finished())
            SDL_Log("Web dialog action: action=%d closed=%d queue_ms=%lld elapsed_ms=%lld", input.value,
                    provider->dialog_presentation_finished() ? 1 : 0,
                    static_cast<long long>(queue_ms), static_cast<long long>(elapsed_ms));
#endif
    }

    void apply_dialog_pointer(::cw::frame::Frame& frame,
                              const detail::HostedEngineInput& input) {
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(
                frame.dialog_presentation_model());
        if (!provider) return;
        ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
        if (!input_guard) {
            provider->dialog_presentation_pointer_cancel();
            return;
        }
        if (input.pressed) provider->note_dialog_presentation_user_input();
        provider->dialog_presentation_modifiers(input.modifiers);
        const auto action=static_cast<HostedEngineDialogAction>(input.value);
        if (!::cw::dialog::dispatch_dialog_utility_pointer(*provider,action,
            static_cast<float>(input.x),static_cast<float>(input.y),input.pressed))
            provider->dialog_presentation_pointer(action,input.index,
                static_cast<float>(input.x),static_cast<float>(input.y),input.pressed);
    }

    void apply_dialog_key(::cw::frame::Frame& frame,
                          const detail::HostedEngineInput& input) {
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(
                frame.dialog_presentation_model());
        if (!provider) return;
        ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
        if (!input_guard) return;
        if (input.pressed) provider->note_dialog_presentation_user_input();
        if (input.pressed && static_cast<::cw::dialog::DialogPresentationKey>(input.value) ==
                ::cw::dialog::DialogPresentationKey::Enter) {
            provider->dialog_presentation_enter(input.modifiers);
            return;
        }
        if (input.pressed && static_cast<::cw::dialog::DialogPresentationKey>(input.value) ==
                ::cw::dialog::DialogPresentationKey::Escape) {
            provider->dialog_presentation_escape();
            return;
        }
        provider->dialog_presentation_key(
            static_cast<::cw::dialog::DialogPresentationKey>(input.value),
            input.pressed, input.modifiers);
    }

    void apply_dialog_scroll(::cw::frame::Frame& frame,
                             const detail::HostedEngineInput& input) {
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(
                frame.dialog_presentation_model());
        if (!provider) return;
        ::cw::dialog::DialogPresentationInputGuard input_guard(*provider);
        if (!input_guard) return;
        if (input.kind == detail::HostedEngineInput::Kind::DialogWheel)
            ::cw::dialog::dispatch_dialog_wheel(*provider,
                static_cast<float>(input.x),static_cast<float>(input.y),input.value);
        else
            provider->dialog_presentation_scroll(
                static_cast<float>(input.x), static_cast<float>(input.y), input.value);
    }

    void publish_status() {
        // このアダプターはEngineのUIスレッドで動く。 ステータスのスプ
        // ライトはゲームスレッドのStatusBar::change/clear
        // で削除されるため、 ここから生きたスプライトを直接参照しない。
        const auto runtime = ::cw::g_cwpy;
        if (!runtime || state_->force_stop.load() || state_->finished.load()) return;
        // メッセージログはゲームスレッド内の入れ子ループで動き、下のスナップ
        // ショット要求を処理しない。 このフラグだけはここで公開し、ステータ
        // スダイアログへのタッチでEscを送ってログを閉じられるようにする。
        {
            const bool backlog = runtime->m__is_showingbacklog.load();
            std::scoped_lock lock(state_->mutex);
            if (state_->status.backlog_showing != backlog) {
                state_->status.backlog_showing = backlog;
                state_->status.sequence = state_->next_status_sequence++;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (status_requested_at_.time_since_epoch().count() != 0 &&
            now - status_requested_at_ < std::chrono::milliseconds(33)) return;
        if (state_->status_snapshot_pending.exchange(true)) return;

        // 終了やイベントのキャンセルで未実行のコマンドが破棄される場合も、
        // 保留フラグを解除する。 アダプターやCWPyを自分の待ち行列で
        // 保持せず、コールバックがセッションより長生きしないようにする。
        struct PendingRequest final {
            std::weak_ptr<detail::HostedEngineSessionState> state;
            explicit PendingRequest(const std::shared_ptr<detail::HostedEngineSessionState>& s)
                : state(s) {}
            ~PendingRequest() {
                if (const auto s = state.lock()) s->status_snapshot_pending.store(false);
            }
        };
        try {
            auto pending = std::make_shared<PendingRequest>(state_);
            const std::weak_ptr<::cw::thread::CWPy> weak_runtime = runtime;
            // これは読み取り専用の表示状態の取得であり、ユーザー操作ではない。 強制
            // キューもゲームスレッドで動くがpending_userevent()か
            // らは除外し、 定期的な取得によってカード入力やF9をブロックしない。
            runtime->force_exec_func([pending, weak_runtime](const ::cw::lib::Arguments&,
                                                            const ::cw::lib::KeywordArguments&) {
                const auto state = pending->state.lock();
                const auto game = weak_runtime.lock();
                if (state && game && !state->force_stop.load() && !state->finished.load())
                    publish_status_on_game_thread(game, state);
                return ::cw::lib::Value{};
            });
            status_requested_at_ = now;
        } catch (...) {
            state_->status_snapshot_pending.store(false);
            throw;
        }
    }

    static void publish_status_on_game_thread(
        const std::shared_ptr<::cw::thread::CWPy>& runtime,
        const std::shared_ptr<detail::HostedEngineSessionState>& state) {
        // コマンドキューはゲームスレッドで処理し、ボタンの削除・再生成と
        // 直列化する。 ホストとの境界を渡るのは、独立した値だけとする。
        runtime->assert_game_thread();
        HostedStatusSnapshot previous;
        {
            std::scoped_lock lock(state->mutex);
            previous = state->status;
        }
        HostedStatusSnapshot next;
        if (runtime && runtime->m_effectbooster_click_wait)
            next.click_wait_notice = ::cw::effectbooster::click_wait_notice;
        // ALL仕様: 描画スレッドへは設定オブジェクトではなく色の値だけを渡す。
        if (runtime && runtime->m_setting) {
            const auto local = runtime->m_setting->get_drawsetting();
            if (local) {
                const auto [red,green,blue,alpha] = local->m_allengineframecolour;
                next.engine_frame_colour = {
                    static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(red,0,255)),
                    static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(green,0,255)),
                    static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(blue,0,255)),
                    static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(alpha,0,255))};
            }
        }
        next.visible = runtime && runtime->m_setting &&
            runtime->m_setting->m_show_all_status_dialog;
        next.buttons.push_back({HostedStatusAction::CloseDialog, "閉じる",
            true, false, true, {}});
#if defined(CARDWIRTH_ALL_HOST) && !defined(CARDWIRTH_ENGINE_ONLY)
        // ALL追加仕様。ゲーム状態の編集ではなく、ホストのデータ閲覧画面へ移動。
        next.buttons.push_back({HostedStatusAction::OpenData, "データ",
            true, false, true, {}});
#endif
        if (runtime && runtime->m_ydata && *runtime->m_ydata) {
            const auto yado = *runtime->m_ydata;
            next.yado_money_visible = runtime->m_status == "Yado";
            next.yado_money_enabled = next.yado_money_visible;
            next.yado_money = std::to_string(yado->m_money);
            if (yado->m_party && *yado->m_party) {
                next.party_money_enabled = true;
                next.party_money = std::to_string((*yado->m_party)->m_money);
            }
        }
        if (runtime) {
            // 画像が同じなら前のサーフェスを維持し、 ステータス通
            // 知のたびにホストがアイコンを再転送しないようにする。
            const auto icon = [](std::shared_ptr<const ::cw::lib::Surface> fresh,
                                 const std::shared_ptr<const ::cw::lib::Surface>& old) {
                if (fresh && old && fresh->size.width == old->size.width &&
                    fresh->size.height == old->size.height && fresh->rgba == old->rgba)
                    return old;
                return fresh;
            };
            next.yado_money_icon = icon(::cw::sprite::statusbar::status_money_icon(false),
                                        previous.yado_money_icon);
            next.party_money_icon = icon(::cw::sprite::statusbar::status_money_icon(true),
                                         previous.party_money_icon);
        }
        if (runtime && runtime->is_battlestatus() && runtime->m_battle && *runtime->m_battle) {
            next.battle_round_visible = true;
            next.battle_round = std::to_string((*runtime->m_battle)->m_round);
        }
        const auto add = [&](HostedStatusAction action, std::string label,
                             const auto* tag, bool full = false) {
            using Button = std::remove_cv_t<std::remove_pointer_t<decltype(tag)>>;
            if (!runtime || !runtime->m_statusbar) return false;
            for (const auto& object : runtime->m_statusbar->m_status_items) {
                const auto button = std::dynamic_pointer_cast<Button>(object);
                if (!button || !button->is_showing_for_host()) continue;
                const auto base = std::static_pointer_cast<
                    ::cw::sprite::statusbar::StatusBarButton>(button);
                const auto old_button = std::find_if(previous.buttons.begin(), previous.buttons.end(),
                    [action](const auto& old) { return old.action == action; });
                const auto old_image = old_button == previous.buttons.end()
                    ? nullptr : old_button->image;
                next.buttons.push_back({action, std::move(label),
                    base->m_enabled_typed, base->m_is_pushed, full,
                    // これらの部品はホストの枠と現在のラベルを使い、 そ
                    // の枠の中へ別の枠付きステータスボタンを重ねない。
                    hosted_status_text_button(action)
                        ? nullptr : freeze_hosted_status_image(base->m_image_typed, old_image)});
                return true;
            }
            return false;
        };
        using namespace ::cw::sprite::statusbar;
        add(HostedStatusAction::Settings, "設定", (SettingsButton*)nullptr);
        // F9には元となるステータスのスプライトがない。有効判定にはシナリオログの
        // パーティファイルの確認が含まれるため、共通の判定結果を短時間キャッシ
        // ュし、 ホストの毎フレーム処理でファイルシステムを調べないようにする。
        const auto now = std::chrono::steady_clock::now();
        if (state->hosted_f9_checked_at.time_since_epoch().count() == 0 ||
            now - state->hosted_f9_checked_at >= std::chrono::milliseconds(500)) {
            state->hosted_f9_enabled = runtime &&
                ::cw::eventhandler::EventHandler::can_f9();
            state->hosted_f9_checked_at = now;
        }
        if (!(runtime && runtime->is_novel_mode()))
            // ステータスダイアログのF9は横型ステー
            // タスバーと同じ操作だが、 ボタンにはキ
            // ー名ではなく日本語の操作名を表示する。
            next.buttons.push_back({HostedStatusAction::F9, "帰還",
                state->hosted_f9_enabled, false, false, {}});
        add(HostedStatusAction::Backlog, "会話履歴", (BacklogButton*)nullptr);
        add(HostedStatusAction::Debugger, "デバッガ", (DebuggerButton*)nullptr);
        add(HostedStatusAction::BattleAction, "行動開始", (ActionButton*)nullptr, true);
        add(HostedStatusAction::BattleAuto, "自動", (AutoStartButton*)nullptr);
        add(HostedStatusAction::BattleRunAway, "逃げる", (RunAwayButton*)nullptr, true);
        add(HostedStatusAction::BattleFriendCards, "同行者", (ShowFriendCardsButton*)nullptr);
        // ALLの縦型ステータスダイアログにも、Py版の横型ステータスバーに
        // 現れるカード選択／特殊エリア解除用のキャンセル操作を必ず転送する。
        // ALLでは戦闘開始・逃げると同じ一枚の全面幅ボタンとして表示する。
        // CancelButton自身の枠画像を重ねると二重ボタンになるため、ホストへは文字だけ渡す。
        add(HostedStatusAction::CancelCardControl, "キャンセル", (CancelButton*)nullptr, true);
        // 個数と更新通知は、縦型ステータスダイアログ上部の正方形
        // アイコンへ表示し、 設定・バックログのボタンと並べる。
        add(HostedStatusAction::InfoCards, "情報カード", (InfoCardsButton*)nullptr);
        if (runtime && runtime->m_status == "Scenario") {
            const bool camp = runtime->m_areaid == -4 || runtime->m_areaid == -5;
            // CampButtonは横型ステータスバーの二つの部品と同
            // じ双方向の切り替えを行う。 変わるのは表示ラベルだけ。
            if (!add(HostedStatusAction::CampTable,
                     camp ? "テーブル" : "キャンプ",
                     (CampButton*)nullptr, true))
                add(HostedStatusAction::CampTable,
                    camp ? "テーブル" : "キャンプ",
                    (TableButton*)nullptr, true);
        }
        next.backlog_showing = runtime && runtime->is_showingbacklog();
        if (runtime && runtime->m_setting) {
            next.shortcuts_folded = runtime->m_setting->m_all_status_shortcuts_folded;
            const bool save_load = hosted_save_load_available(*runtime);
            const bool instructions = hosted_instructions_available(*runtime);
            using Shortcut = ::cw::setting::AllStatusShortcut;
            for (const auto kind : ::cw::setting::parse_all_status_shortcuts(
                     runtime->m_setting->m_all_status_shortcuts)) {
                switch (kind) {
                case Shortcut::None: break;
                case Shortcut::Save:
                    next.shortcuts.push_back({HostedStatusAction::Save, "セーブ",
                                              save_load, false, true, {}});
                    break;
                case Shortcut::Load:
                    next.shortcuts.push_back({HostedStatusAction::Load, "ロード",
                                              save_load, false, true, {}});
                    break;
                case Shortcut::Screenshot:
                    next.shortcuts.push_back({HostedStatusAction::Screenshot,
                                              screenshot_done_showing(*state)
                                                  ? "完了" : "スクリーン\nショット",
                                              true, false, true, {}});
                    break;
                case Shortcut::CardScreenshot:
                    next.shortcuts.push_back({HostedStatusAction::CardScreenshot,
                                              card_screenshot_done_showing(*state)
                                                  ? "完了" : "手札の\nスクリーン\nショット",
                                              !runtime->get_pcards(::cw::lib::Value{}).empty(),
                                              false, true, {}});
                    break;
                case Shortcut::Instructions:
                    next.shortcuts.push_back({HostedStatusAction::Instructions,
                                              "解説", instructions, false, true, {}});
                    break;
                case Shortcut::FullRecovery:
                    next.shortcuts.push_back({HostedStatusAction::FullRecovery,
                                              "全回復\n(デバッグ機能)",
                                              hosted_full_recovery_available(*runtime),
                                              false, true, {}});
                    break;
                case Shortcut::CustomStatus:
                    // 全回復と同じ有効条件（デバッグモードでシナリオを実行中）。
                    next.shortcuts.push_back({HostedStatusAction::CustomStatus,
                                              "カスタム状態\n(デバッグ機能)",
                                              hosted_full_recovery_available(*runtime),
                                              false, true, {}});
                    break;
                }
            }
        }
        {
            std::scoped_lock lock(state->mutex);
            const auto& old = state->status;
            const auto same_list = [](const auto& left, const auto& right) {
                return left.size() == right.size() &&
                    std::equal(left.begin(), left.end(), right.begin(),
                        [](const auto& a, const auto& b) {
                            return a.action == b.action && a.label == b.label &&
                                a.enabled == b.enabled && a.pushed == b.pushed &&
                                a.full_width == b.full_width &&
                                a.image.get() == b.image.get();
                        });
            };
            const auto same_buttons = same_list(old.buttons, next.buttons) &&
                same_list(old.shortcuts, next.shortcuts) &&
                old.shortcuts_folded == next.shortcuts_folded &&
                old.backlog_showing == next.backlog_showing;
            if (old.visible == next.visible &&
                old.click_wait_notice == next.click_wait_notice &&
                old.engine_frame_colour == next.engine_frame_colour &&
                old.yado_money_visible == next.yado_money_visible &&
                old.yado_money_enabled == next.yado_money_enabled &&
                old.party_money_enabled == next.party_money_enabled &&
                old.yado_money == next.yado_money &&
                old.party_money == next.party_money &&
                old.yado_money_icon == next.yado_money_icon &&
                old.party_money_icon == next.party_money_icon &&
                old.battle_round_visible == next.battle_round_visible &&
                old.battle_round == next.battle_round && same_buttons) return;
            next.sequence = state->next_status_sequence++;
            state->status = std::move(next);
        }
    }

    void publish_debugger(::cw::frame::Frame& frame) {
        HostedDebuggerSnapshot next;
        std::shared_ptr<::cw::debug::debugger::Debugger> debugger_model;
        bool presentation_dirty = false;
        if (frame.m_debugger) {
            const auto debugger = std::dynamic_pointer_cast<::cw::debug::debugger::Debugger>(
                std::static_pointer_cast<::cw::lib::Object>(*frame.m_debugger));
            if (debugger && debugger->m_visible) {
                debugger_model = debugger;
                presentation_dirty = debugger->dialog_presentation_needs_refresh();
                next.visible = true;
                next.paused = debugger->m_pause_checked;
                next.waittime = static_cast<int>(debugger->m_waittime);
                next.area = debugger->m_area_label;
                next.selected_member = debugger->m_selectedmember_label;
                next.selected_card = debugger->m_selectedcard_label;
                if (debugger->m_view_tree) {
                    for (const auto& item : debugger->m_view_tree->m_itemlist) {
                        if (!item || item->m_text_typed.empty()) continue;
                        next.event_rows.push_back(item->m_text_typed);
                    }
                }
                if (debugger->m_view_var) {
                    const auto count = debugger->m_view_var->m_item_count;
                    for (::cw::lib::Integer row = 0; row < count; ++row) {
                        const auto name = debugger->m_view_var->OnGetItemText(row, 0);
                        const auto value = debugger->m_view_var->OnGetItemText(row, 1);
                        next.variable_rows.push_back(name + "  " + value);
                    }
                }
                next.status = debugger->m_last_error;
                next.tool_state = common_debugger_tool_state(*debugger);
            }
        }
        {
            std::scoped_lock lock(state_->mutex);
            const auto& old = state_->debugger;
            if (!presentation_dirty && old.visible == next.visible && old.paused == next.paused &&
                old.waittime == next.waittime &&
                old.tool_state == next.tool_state && old.area == next.area &&
                old.selected_member == next.selected_member &&
                old.selected_card == next.selected_card && old.status == next.status &&
                old.event_rows == next.event_rows && old.variable_rows == next.variable_rows)
                return;
        }
        if (debugger_model) {
            debugger_model->begin_dialog_presentation_refresh();
            const auto presentation = debugger_model->dialog_presentation();
            ::cw::dialog::publish_dialog_debug_snapshot(debugger_model.get(), presentation);
            std::shared_ptr<const ::cw::lib::Surface> previous_surface;
            {
                std::scoped_lock lock(state_->mutex);
                previous_surface = state_->debugger.presentation_surface;
            }
            next.presentation_surface = freeze_hosted_image(presentation.surface, previous_surface);
            next.wants_text_input = presentation.wants_text_input;
            if (presentation.surface) {
                next.preferred_width = static_cast<int>(presentation.surface->size.width);
                next.preferred_height = static_cast<int>(presentation.surface->size.height);
            }
            next.controls = presentation.controls;
            debugger_model->dialog_presentation_refreshed();
        } else if (const auto previous = published_debugger_provider_.lock()) {
            ::cw::dialog::clear_dialog_debug_snapshot(previous.get());
        }
        std::scoped_lock lock(state_->mutex);
        next.sequence = state_->next_debugger_sequence++;
        state_->debugger = std::move(next);
        published_debugger_provider_ = debugger_model;
    }

    std::shared_ptr<detail::HostedEngineSessionState> state_;
    std::weak_ptr<::cw::dialog::DialogPresentationProvider> published_debugger_provider_{};
    std::mutex sink_mutex_{};
    EventSink sink_{};
};

#if defined(_WIN32) && defined(_DEBUG)
void log_abort_stack(int) {
    ::cardwirth::debug::log_crash_diagnostics("SIGABRT");
    std::_Exit(3);
}
#endif

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

#ifdef _WIN32
std::filesystem::path executable_path_from_win32() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size())
        throw std::runtime_error("Failed to determine the executable path");
    buffer.resize(size);
    return std::filesystem::path(buffer);
}

std::vector<std::filesystem::path> user_data_from_win32_command_line(
        bool& malformed) {
    malformed = false;
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) {
        malformed = true;
        return {};
    }
    std::vector<std::filesystem::path> result;
    constexpr std::wstring_view assignment = L"--user-data=";
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--user-data") {
            if (index + 1 >= argument_count) {
                malformed = true;
                break;
            }
            const std::wstring_view value(arguments[++index]);
            if (value.empty()) {
                malformed = true;
                break;
            }
            result.emplace_back(std::wstring(value));
        } else if (argument.starts_with(assignment)) {
            const auto value = argument.substr(assignment.size());
            if (value.empty()) {
                malformed = true;
                break;
            }
            result.emplace_back(std::wstring(value));
        }
    }
    LocalFree(arguments);
    return result;
}

std::filesystem::path user_data_from_win32_command_line() {
    bool malformed = false;
    auto values = user_data_from_win32_command_line(malformed);
    return malformed || values.empty() ? std::filesystem::path{} : values.front();
}
#endif

std::filesystem::path managed_user_data_from_environment() {
#ifdef _WIN32
    constexpr wchar_t name[] = L"CARDWIRTHC_USER_ROOT";
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (copied == 0 || copied >= required) return {};
    value.resize(copied);
    return std::filesystem::path(std::move(value));
#else
    const char* value = std::getenv("CARDWIRTHC_USER_ROOT");
    if (value == nullptr || *value == '\0') return {};
    try {
        return std::filesystem::u8path(value);
    } catch (...) {
        return {};
    }
#endif
}

bool managed_user_data_session_from_environment() noexcept {
#ifdef _WIN32
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"CARDWIRTHC_MANAGED_USERDATA_SESSION",
                                   value, 2U) == 1U && value[0] == L'1';
#else
    const char* value = std::getenv("CARDWIRTHC_MANAGED_USERDATA_SESSION");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
}

bool same_managed_path(const std::filesystem::path& left,
                       const std::filesystem::path& right) {
#ifdef _WIN32
    const auto left_text = left.lexically_normal().native();
    const auto right_text = right.lexically_normal().native();
    return CompareStringOrdinal(left_text.c_str(), -1, right_text.c_str(), -1, TRUE) ==
           CSTR_EQUAL;
#else
    return left.lexically_normal() == right.lexically_normal();
#endif
}

std::filesystem::path argument_path(const std::vector<std::string>& arguments,
                                    std::string_view name) {
    const std::string assignment = std::string(name) + "=";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == name && index + 1 < arguments.size())
            return ::cw::lib::encoding::path_from_utf8(arguments[index + 1]);
        if (arguments[index].starts_with(assignment))
            return ::cw::lib::encoding::path_from_utf8(
                arguments[index].substr(assignment.size()));
    }
    return {};
}

std::vector<::cw::lib::String> engine_arguments(const AppLaunchOptions& options) {
    std::vector<::cw::lib::String> source = options.arguments;
    if (source.empty()) source = ::cw::argparser::application_arguments();

    std::vector<::cw::lib::String> result;
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto& argument = source[index];
        // これらはALL・Explorerホストの設定で、Engineの接続
        // 方式を選ぶ。 CardWirthのゲーム用引数としては扱わない。
        if (argument == "--mode" || argument == "--user-data" ||
            argument == "--encrypted-user-data" || argument == "--encryption-key" ||
            argument == "--engine-path" || argument == "--engine-backend") {
            // --encrypted-user-dataは値の省略も許し、その場合はUserData.data
            // を選ぶ。 後続の別のオプションを値として消費しない。
            if (index + 1 < source.size() &&
                (argument != "--encrypted-user-data" ||
                 !source[index + 1].starts_with("--"))) ++index;
            continue;
        }
        if (argument.starts_with("--user-data=") ||
            argument.starts_with("--encrypted-user-data=") ||
            argument.starts_with("--encryption-key=")) continue;
        if (argument == "--smoke-test" || argument == "--nonactive-debug" ||
            argument == "--communication-debug") continue;
        if (argument == "--communication-debug-port") {
            if (index + 1 < source.size()) ++index;
            continue;
        }
        if (argument.starts_with("--communication-debug-port=")) continue;
        result.push_back(argument);
    }
    return result;
}

struct EncryptedLaunchSelection {
    bool requested{};
    bool malformed{};
    std::filesystem::path data{};
    std::filesystem::path key{};
};

EncryptedLaunchSelection encrypted_launch_selection(
    const std::vector<std::string>& source,
    const std::filesystem::path& application_directory) {
    EncryptedLaunchSelection result;
#if defined(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION)
    // 暗号化専用の配布形式なので、引数なしの起動では 実行フ
    // ァイルの隣にある暗号化UserDataをマウントする。
    result.requested = true;
#endif
    constexpr std::string_view data_assignment = "--encrypted-user-data=";
    constexpr std::string_view key_assignment = "--encryption-key=";
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto& argument = source[index];
        if (argument == "--encrypted-user-data") {
            result.requested = true;
            if (index + 1 < source.size() && !source[index + 1].starts_with("--"))
                result.data = ::cw::lib::encoding::path_from_utf8(source[++index]);
        } else if (argument.starts_with(data_assignment)) {
            result.requested = true;
            const auto value = argument.substr(data_assignment.size());
            if (value.empty()) result.malformed = true;
            else result.data = ::cw::lib::encoding::path_from_utf8(value);
        } else if (argument == "--encryption-key") {
            if (index + 1 >= source.size() || source[index + 1].starts_with("--")) {
                result.malformed = true;
            } else {
                result.key = ::cw::lib::encoding::path_from_utf8(source[++index]);
            }
        } else if (argument.starts_with(key_assignment)) {
            const auto value = argument.substr(key_assignment.size());
            if (value.empty()) result.malformed = true;
            else result.key = ::cw::lib::encoding::path_from_utf8(value);
        }
    }
    if (!result.requested && !result.key.empty()) result.malformed = true;
    if (result.requested && result.data.empty())
        result.data = ::cardwirth::platform::default_encrypted_user_storage_path(
            application_directory);
    if (!result.data.empty() && result.data.is_relative())
        result.data = application_directory / result.data;
    if (!result.key.empty() && result.key.is_relative())
        result.key = application_directory / result.key;
    return result;
}

bool apply_engine_arguments(const AppLaunchOptions& options) {
    if (!::cw::g__argparser) return false;
    const auto parsed = ::cw::g__argparser->parse_args(engine_arguments(options));
    if (!parsed || !*parsed) return false;
    const auto value = ::cw::lib::Value(
        std::static_pointer_cast<::cw::lib::Object>(*parsed));
    ::cw::g__options = value;
    ::cw::g_OPTIONS = value;
    ::cw::g_SKIN_CONV_ARGS = ::cw::skin_conversion_arguments((*parsed)->m_leftovers);
    return true;
}

// 共有プラットフォームコンテキストをEngineセッションから扱うため
// の窓口。 まだStorageを持たない単独起動を共通のAppRuntimeContext
// から分離しつつ、 作業ディレクトリ基準のI/Oを段階的に
// Storageへ移すためのファイルサービス境界を保つ。
struct EngineSessionContext {
    EngineFileServices files{};
    std::filesystem::path application_directory{};
    std::filesystem::path user_data_root{};
    bool sdl_initialized{};
    bool ttf_initialized{};
    bool audio_initialized{};
    bool owns_sdl{true};
    bool owns_ttf{true};
    bool owns_audio{true};
    bool owns_com{true};
    bool owns_logger{true};
    bool owns_process_handlers{true};
    std::shared_ptr<::cw::frame::NativeFrameAdapter> native_adapter{};
    std::function<void(std::string_view)> report_error{};
};

std::string inspect_engine_storage(const EngineFileServices& files) {
    if (!files.configured()) return {};

    std::vector<std::string> missing;
    const auto inspect = [&missing](::cardwirth::platform::Storage* storage,
                                    const std::filesystem::path& path,
                                    std::string_view label) {
        if (!storage) {
            missing.emplace_back(std::string(label) + " storage is not configured");
            return;
        }
        try {
            const auto result = storage->stat(path);
            if (!result || !result->exists()) {
                std::string message(label);
                message += " is unavailable";
                if (!storage->last_error().empty()) {
                    message += ": ";
                    message += storage->last_error();
                }
                missing.push_back(std::move(message));
            }
        } catch (const std::exception& error) {
            missing.emplace_back(std::string(label) + " inspection failed: " + error.what());
        } catch (...) {
            missing.emplace_back(std::string(label) + " inspection failed");
        }
    };

    // 正式な読み取り専用資産と、書き込み可能なユーザーデータは別々に
    // 検査する。 UserData/Dataは、従来のEngineが
    // 作業ディレクトリ基準でDataを読む間だけ使う互換用コピー。
    inspect(files.asset_storage, files.paths.data_root, "Engine asset Data");
    inspect(files.user_storage, files.paths.user_root, "Engine user root");
    inspect(files.user_storage, files.paths.user_data_root, "Engine compatibility UserData/Data");
    if (missing.empty()) return {};

    std::string report = "Engine storage inspection: ";
    for (std::size_t index = 0; index < missing.size(); ++index) {
        if (index != 0) report += "; ";
        report += missing[index];
    }
    return report;
}

std::filesystem::path resolve_executable(const AppLaunchOptions& options) {
    if (!options.executable_path.empty()) return options.executable_path;
#ifdef _WIN32
    return executable_path_from_win32();
#else
    // Windows以外の呼び出し元は、AppLaunchOptionsでargv[0]
    // を渡す。 省略時もプラットフォーム固有の/procやargvグローバル変数
    // には依存せず、 ライブラリの直接呼び出しで一貫した結果になるようにする。
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path("CardWirthPy_C") : current / "CardWirthPy_C";
#endif
}

std::filesystem::path resolve_application_directory(const EngineSessionContext& context,
                                                    const AppLaunchOptions& options,
                                                    const std::filesystem::path& executable) {
    if (!context.application_directory.empty()) return context.application_directory;
    if (!options.application_directory.empty()) return options.application_directory;
    if (!executable.parent_path().empty()) return executable.parent_path();
    return std::filesystem::current_path();
}

std::filesystem::path resolve_user_data(const EngineSessionContext& context,
                                        const AppLaunchOptions& options) {
    if (managed_user_data_session_from_environment()) {
        const auto managed_root = managed_user_data_from_environment();
        if (managed_root.empty() || !managed_root.is_absolute()) {
            throw std::runtime_error(
                "managed UserData session has no absolute CARDWIRTHC_USER_ROOT");
        }
        const auto validate = [&managed_root](const std::filesystem::path& candidate) {
            if (candidate.empty()) return;
            if (!candidate.is_absolute() || !same_managed_path(candidate, managed_root)) {
                throw std::runtime_error(
                    "managed UserData root does not match CARDWIRTHC_USER_ROOT");
            }
        };
        validate(context.user_data_root);
        validate(options.user_data_root);
        const auto& managed_paths = context.files.paths;
        const bool has_logical_user_paths =
            !managed_paths.user_root.empty() ||
            !managed_paths.user_data_root.empty() ||
            !managed_paths.scenario_root.empty() ||
            !managed_paths.yado_root.empty() ||
            !managed_paths.config_root.empty() ||
            !managed_paths.database_root.empty() ||
            !managed_paths.temp_root.empty() ||
            !managed_paths.cache_root.empty() ||
            !managed_paths.trash_root.empty();
        if (context.files.user_storage || has_logical_user_paths) {
            const auto validate_storage_path = [](const std::filesystem::path& candidate,
                                                  const std::filesystem::path& expected,
                                                  std::string_view label) {
                if (candidate.empty() || !candidate.is_absolute() ||
                    !same_managed_path(candidate, expected)) {
                    throw std::runtime_error(
                        "managed UserData " + std::string(label) +
                        " does not match CARDWIRTHC_USER_ROOT");
                }
            };
            if (context.files.user_storage)
                validate_storage_path(context.files.user_storage->root(), managed_root,
                                      "storage root");
            validate_storage_path(managed_paths.user_root, managed_root,
                                  "logical user root");
            validate_storage_path(managed_paths.user_data_root,
                                  managed_root / "Data", "logical Data root");
            validate_storage_path(managed_paths.scenario_root,
                                  managed_root / "Scenario", "logical Scenario root");
            validate_storage_path(managed_paths.yado_root,
                                  managed_root / "Yado", "logical Yado root");
            validate_storage_path(managed_paths.config_root,
                                  managed_root / "Config", "logical Config root");
            validate_storage_path(managed_paths.database_root,
                                  managed_root / "Database", "logical Database root");
            validate_storage_path(managed_paths.temp_root,
                                  managed_root / "Temp", "logical Temp root");
            validate_storage_path(managed_paths.cache_root,
                                  managed_root / "Cache", "logical Cache root");
            validate_storage_path(managed_paths.trash_root,
                                  managed_root / "Trash", "logical Trash root");
        }
        constexpr std::string_view assignment = "--user-data=";
        for (std::size_t index = 0; index < options.arguments.size(); ++index) {
            if (options.arguments[index] == "--user-data") {
                if (index + 1 >= options.arguments.size()) {
                    throw std::runtime_error(
                        "managed UserData launch has a missing --user-data value");
                }
                const auto& value = options.arguments[++index];
                if (value.empty()) {
                    throw std::runtime_error(
                        "managed UserData launch has an empty --user-data value");
                }
                validate(::cw::lib::encoding::path_from_utf8(value));
            } else if (options.arguments[index].starts_with(assignment)) {
                const auto value = std::string_view(options.arguments[index]).substr(
                    assignment.size());
                if (value.empty()) {
                    throw std::runtime_error(
                        "managed UserData launch has an empty --user-data value");
                }
                validate(::cw::lib::encoding::path_from_utf8(std::string(value)));
            }
        }
        // 単独起動のEditorからEngineを開く経路では、従来は
        // シナリオ引数とEngineディレクトリの 作業ディレクトリだ
        // けを渡していた。認証済みランチャーの環境をこの経路の正式な代
        // 替情報とし、 明示されたホスト設定との整合性確認にも使う。
#ifdef _WIN32
        bool malformed_command_line = false;
        const auto command_line_roots = user_data_from_win32_command_line(
            malformed_command_line);
        if (malformed_command_line) {
            throw std::runtime_error(
                "managed UserData launch has a malformed process command line");
        }
        for (const auto& command_line_root : command_line_roots)
            validate(command_line_root);
#endif
        return managed_root.lexically_normal();
    }
    if (!context.user_data_root.empty()) return context.user_data_root;
    if (!options.user_data_root.empty()) return options.user_data_root;
    if (const auto value = argument_path(options.arguments, "--user-data"); !value.empty()) return value;
#ifdef _WIN32
    // wWinMainはargc/argvを持たない。このOS固有の扱い
    // は アダプターの境界にとどめ、Engineコアには解析させない。
    if (const auto value = user_data_from_win32_command_line(); !value.empty()) return value;
#endif
    return {};
}

class ScopedCurrentPath final {
public:
    ScopedCurrentPath() {
        std::error_code error;
        previous_ = std::filesystem::current_path(error);
        valid_ = !error;
    }

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

    ~ScopedCurrentPath() {
        if (!valid_) return;
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
    }

    void set(const std::filesystem::path& path, const char* description) {
        if (path.empty()) return;
        std::error_code error;
        std::filesystem::current_path(path, error);
        if (error) throw std::runtime_error(std::string("Failed to select the ") + description + ": " +
                                            path_to_utf8(path));
    }

private:
    std::filesystem::path previous_{};
    bool valid_{};
};

void show_startup_error(const std::string& message) {
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message.data(),
                                         static_cast<int>(message.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message.data(),
                                      static_cast<int>(message.size()), wide.data(), size);
    MessageBoxW(nullptr, wide.empty() ? L"CardWirthPy_C startup failed" : wide.c_str(),
                L"CardWirthPy_C", MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
    SDL_Log("CardWirthPy_C startup failed: %s", message.c_str());
#endif
}

std::shared_ptr<::cw::lib::Surface> render_status_text(TTF_Font* font, std::mutex& mutex,
                                                       const std::string& text, int point_size,
                                                       ::cw::lib::Color color, bool bold) {
    if (!font || text.empty()) return {};
    std::scoped_lock lock(mutex);
    const float old_size = TTF_GetFontSize(font);
    const auto old_style = TTF_GetFontStyle(font);
    if (!TTF_SetFontSize(font, static_cast<float>(std::clamp(point_size, 1, 256)))) return {};
    auto style = old_style;
    if (bold) style = static_cast<TTF_FontStyleFlags>(style | TTF_STYLE_BOLD);
    else style = static_cast<TTF_FontStyleFlags>(style & ~TTF_STYLE_BOLD);
    TTF_SetFontStyle(font, style);
    const SDL_Color foreground{color.r, color.g, color.b, color.a};
    SDL_Surface* rendered = TTF_RenderText_Blended(font, text.c_str(), text.size(), foreground);
    std::shared_ptr<::cw::lib::Surface> result;
    if (rendered) {
        SDL_Surface* rgba = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32);
        if (rgba) {
            result = std::make_shared<::cw::lib::Surface>();
            result->size = {rgba->w, rgba->h};
            result->rgba.resize(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4U);
            const auto* source = static_cast<const std::uint8_t*>(rgba->pixels);
            for (int y = 0; y < rgba->h; ++y)
                std::memcpy(result->rgba.data() + static_cast<std::size_t>(y * rgba->w) * 4U,
                            source + static_cast<std::size_t>(y * rgba->pitch),
                            static_cast<std::size_t>(rgba->w) * 4U);
            SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(rendered);
    }
    TTF_SetFontStyle(font, old_style);
    TTF_SetFontSize(font, old_size);
    return result;
}

void report_startup_error(const EngineSessionContext& context, const std::string& message) {
    if (context.report_error) context.report_error(message);
    else show_startup_error(message);
}

int run_engine_session(const EngineSessionContext& requested_context,
                       const AppLaunchOptions& options, bool embedded) {
    // プラットフォーム資源を取得する前に引数を解析する。ヘルプは
    // UIを開かず正常終了する。 Python版と同様に、オプシ
    // ョンの値が不足している場合も使い方を表示して正常終了する。
    if (!apply_engine_arguments(options)) return 0;
    const auto arguments = engine_arguments(options);
    if (std::find(arguments.begin(), arguments.end(), "--help") != arguments.end() ||
        std::find(arguments.begin(), arguments.end(), "-h") != arguments.end()) {
        if (::cw::g__argparser) ::cw::g__argparser->print_help();
        return 0;
    }
    EngineSessionContext context = requested_context;
    if (embedded) {
        // 埋め込み起動で、親のグローバルサービスを無断で引き継がな
        // い。 準備完了フラグがなければ呼び出し元の誤りとして扱い
        // 、任意のUIコールバックからSDL_Initを呼ばない。
        if (context.owns_sdl || context.owns_ttf || context.owns_audio ||
            !context.sdl_initialized || !context.ttf_initialized || !context.audio_initialized) {
            const std::string message =
                "Embedded Engine requires host-owned initialized SDL, TTF, and audio services";
            report_startup_error(context, message);
            return 2;
        }
    }

    // UIループからの例外終了時も含め、すべてのワーカーの解放とjoin
    // が済むまで セッションの作業ディレクトリを維持する。
    ScopedCurrentPath current_path;
    bool sdl_initialized_here = false;
    bool ttf_initialized_here = false;
#ifdef _WIN32
    bool com_initialized_here = false;
#endif
    TTF_Font* status_font = nullptr;
    std::mutex status_font_mutex;
    std::shared_ptr<::cw::frame::MyApp> application;
    std::unique_ptr<::cardwirth::platform::Storage> owned_user_storage;
    std::unique_ptr<::cardwirth::platform::ScopedUserStorageMount> user_storage_mount;
    bool encrypted_user_storage_active = false;
    const auto previous_tempdir_init = ::cw::g_tempdir_init;
    const auto previous_tempdir = ::cw::g_tempdir;
    std::unique_ptr<::cardwirth::debug::DebugControlServer> debug_server;
    std::string debug_server_error;
    const auto launch_arguments = options.arguments.empty()
        ? ::cw::argparser::application_arguments() : options.arguments;
    const bool nonactive_debug = std::find(launch_arguments.begin(), launch_arguments.end(),
                                           "--nonactive-debug") != launch_arguments.end();
    ::cardwirth::debug::set_nonactive_automation_enabled(nonactive_debug);
#if defined(_DEBUG)
    constexpr bool debug_build = true;
#else
    constexpr bool debug_build = false;
#endif
    const auto cleanup = [&] {
        // ダイアログのスケジューラーはバックグラウンドタイマーを持つ。 セッションの
        // FrameとUIディスパッチャーが有効な間に停止・joinし、 ホームへ戻
        // る際にワーカーがプロセス共通のCWPyポインターと競合しないようにする。
        ::cw::dialog::shutdown_dialog_video_updater();
        // ゲームワーカーを持つFrameは、グローバルなCWPyポインタ
        // ーを解放する前に破棄する。 埋め込み画面からホームへ戻る際に、
        // ワーカーが破棄途中の共有オブジェクトを参照しないようにする。
        if (application && application->m_frame_typed) {
            // Frameが所有するゲームワーカーを、Engineの共有資
            // 源やマウント済みStorageの破棄前に終了する。 UI・
            // 表示待ちを明示的に解除してjoinする。 通常のメインル
            // ープのOnDestroy後に繰り返し呼んでも安全にする。
            try {
                application->m_frame_typed->OnDestroy(nullptr);
            } catch (...) {
                // Frameのデストラクターも待機解除とjoinを
                // 行う。 最後のサブシステム停止コールバックでエラ
                // ーが起きても、Frameの破棄まで処理を続ける。
            }
        }
        // デバッグソケットも実行中の状態を参照する。Frameを残すだけでは足
        // りないため、 CWPyを参照不可にする前にデータの生成元を停止する。
        if (debug_server) {
            debug_server->stop();
            debug_server.reset();
        }
        application.reset();
        ::cw::g_cwpy.reset();
        // CWPyは互換用のインライン静的フィールドにも複数のセッ
        // ションオブジェクトを保持する。 単独プロセスではSDL
        // 終了後に破棄できたが、ALLではホストが動き続けるため
        // 、 制御を返す前に音声・UIへの参照をすべて解放する。
        ::cw::thread::CWPy::s_music.clear();
        ::cw::thread::CWPy::s_advlog.reset();
        ::cw::thread::CWPy::s_backloggrp.reset();
        ::cw::thread::CWPy::s_cardgrp.reset();
        ::cw::thread::CWPy::s_event.reset();
        ::cw::thread::CWPy::s_msgs.reset();
        ::cw::thread::CWPy::s_pcards.clear();
        ::cw::thread::CWPy::s_sbargrp.reset();
        ::cw::thread::CWPy::s_topgrp.reset();
        ::cw::dialog::clear_dialog_debug_snapshots();
        ::cw::g_tempdir_init = previous_tempdir_init;
        ::cw::g_tempdir = previous_tempdir;
        // 上のapplication.reset()ですべてのEngine
        // ワーカーは停止済み。 暗号化Storageの所有者がセッシ
        // ョンを離れる前に、グローバルな互換用の借用ポインターを解放
        // する。 暗号化セッションのログはDBのマウントが有効な間
        // に書き出す。 リセット後だと通常のUserDataへ保存
        // 先が切り替わり、単一コンテナー内に保存する規則に反する。
        if (context.owns_logger) ::cardwirth::debug::shutdown_logger();
        user_storage_mount.reset();
        ::cw::bassplayer::dispose_bass();
        ::cw::g_quit_app = false;
        if (debug_server) {
            debug_server->stop();
            debug_server.reset();
        }
        ::cardwirth::debug::set_nonactive_automation_enabled(false);
        ::cw::sprite::statusbar::set_host_services({});
        ::cw::dialog::set_all_mode(false);
        if (status_font) {
            TTF_CloseFont(status_font);
            status_font = nullptr;
        }
        if (ttf_initialized_here) TTF_Quit();
        if (sdl_initialized_here) SDL_Quit();
#ifdef _WIN32
        if (com_initialized_here) CoUninitialize();
#endif
    };

#ifdef _WIN32
    HRESULT com_result = RPC_E_CHANGED_MODE;
    if (context.owns_com) {
        com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com_initialized_here = SUCCEEDED(com_result);
    }
#endif

    try {
        const auto debug_control = ::cardwirth::debug::debug_control_launch_options(
            launch_arguments, debug_build);
        const auto executable = resolve_executable(options);
        const auto application_directory = resolve_application_directory(context, options, executable);
        const auto encrypted = encrypted_launch_selection(
            launch_arguments, application_directory);
        if (encrypted.malformed)
            throw std::runtime_error("Encrypted UserData command-line arguments are invalid");
        if (encrypted.requested) {
            auto logical_root = resolve_user_data(context, options);
            if (logical_root.empty()) logical_root = application_directory / "UserData";
            auto opened = ::cardwirth::platform::open_engine_user_storage({
                ::cardwirth::platform::UserStorageMode::Encrypted,
                logical_root, encrypted.data, encrypted.key, false});
            if (!opened) {
                throw std::runtime_error(opened.message.empty()
                    ? "Encrypted UserData could not be opened" : opened.message);
            }
            owned_user_storage = std::move(opened.storage);
            context.files.user_storage = owned_user_storage.get();
            context.user_data_root = logical_root;
            context.files.paths = ::cardwirth::platform::AppPaths::from_roots(
                application_directory, logical_root);
            user_storage_mount = std::make_unique<
                ::cardwirth::platform::ScopedUserStorageMount>(
                    *owned_user_storage, logical_root);
            encrypted_user_storage_active = true;
            const auto temp_root = logical_root / "Data" / "Temp" / "Global";
            if (!::cardwirth::platform::make_mounted_user_directory(temp_root, true))
                throw std::runtime_error("Encrypted UserData temporary root could not be created");
            ::cw::g_tempdir_init = path_to_utf8(temp_root);
            ::cw::g_tempdir = ::cw::lib::Value(::cw::g_tempdir_init);
        }
        const auto user_data = resolve_user_data(context, options);
        if (context.files.configured() && context.files.paths.app_root.empty()) {
            // 単独起動では通常Storageを渡さないが、Engine
            // のファイルAPIを移行する間は、 部分的に構成された
            // セッションでも動作できるよう、この代替処理を残す。
            context.files.paths = ::cardwirth::platform::AppPaths::from_roots(
                application_directory, user_data.empty() ? application_directory : user_data);
        }
        current_path.set(application_directory, "CardWirthPy_C application directory");
        ::cw::g_exepath = path_to_utf8(executable);
        // Release版のループバック専用制御サーバーは、明示的な自動
        // 操作オプションでのみ起動する。 --nonactive-debug
        // で任意のログコンソールのアクティブ化も防げるよう、 ロガーや
        // UIより先に起動する。ポートの競合は致命的エラーにしない。
        if (debug_control.enabled) {
            debug_server = std::make_unique<::cardwirth::debug::DebugControlServer>();
            try {
                debug_server->start(debug_control.port, nonactive_debug);
            } catch (const std::exception& error) {
                debug_server_error = std::string("Debug input server unavailable: ") + error.what();
                debug_server.reset();
            }
        }
        // --user-dataを指定した起動では、平文セ
        // ッションの保存確定後に 実行ファイルの隣へ診断フ
        // ァイルを残さない。UserDataルートの明示指
        // 定がなければ、 従来の直接起動時の保存先を使う。
        const auto diagnostics_directory =
            user_data.empty() ? application_directory : user_data;
        if (context.owns_logger) ::cardwirth::debug::initialize_logger(diagnostics_directory);
        if (!debug_server_error.empty()) ::cardwirth::debug::log(debug_server_error);
        if (const auto storage_diagnostic = inspect_engine_storage(context.files);
            !storage_diagnostic.empty()) {
            // 任意の互換データがなくても、既存の作業ディレ
            // クトリ基準の起動規則は変えない。 元の原因を
            // 失わずに後からStorage側で厳密な判定へ
            // 移せるよう、診断はホストやロガーへ通知する。
            ::cardwirth::debug::log(storage_diagnostic);
        }
#if defined(_DEBUG)
        if (context.owns_process_handlers) {
            std::set_terminate([] {
                std::string reason = "std::terminate invoked";
                if (const auto exception = ::cw::lib::capture_exception()) {
                    try {
                        ::cw::lib::rethrow_exception(exception);
                    } catch (const std::exception& error) {
                        reason += ": ";
                        reason += error.what();
                    } catch (...) {
                        reason += ": non-std exception";
                    }
                } else {
                    reason += ": no active exception";
                }
                ::cardwirth::debug::log_crash_diagnostics(reason);
                std::_Exit(3);
            });
#if defined(_WIN32)
            std::signal(SIGABRT, log_abort_stack);
            ::cardwirth::debug::install_crash_handlers();
#endif
        }
#endif
        ::cardwirth::debug::log(embedded ? "CardWirthPy_C embedded Engine startup"
                                         : "CardWirthPy_C process startup");

        // 移植済みEngineのData/Scenario/Yadoは、まだ相
        // 対パスで扱う。 スコープ内で論理ルートへ切り替えて互換性を保ち、埋め
        // 込み処理から戻る際は ALLホストの作業ディレクトリへ確実に戻す。
        if (!encrypted_user_storage_active)
            current_path.set(user_data, "UserData directory");

#ifdef _WIN32
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES |
                         ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS;
        InitCommonControlsEx(&controls);
#endif

        SDL_SetAppMetadata("CardWirthPy_C", "0.1.0", "jp.cardwirth.cardwirthc.engine");
        if (context.owns_sdl) {
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
                throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
            sdl_initialized_here = true;
        } else if ((SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) &
                    (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) !=
                   (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
            throw std::runtime_error("Host SDL lifecycle is not ready for Engine");
        }
        if (context.owns_ttf) {
            if (!TTF_Init()) throw std::runtime_error(std::string("TTF_Init failed: ") + SDL_GetError());
            ttf_initialized_here = true;
        } else if (!TTF_WasInit()) {
            throw std::runtime_error("Host SDL_ttf lifecycle is not ready for Engine");
        }

        const auto status_font_path = find_platform_font();
        if (!status_font_path.empty()) status_font = TTF_OpenFont(status_font_path.c_str(), 16.0F);
        auto status_backend = std::make_shared<::cw::sprite::statusbar::StatusBarHostServices>();
        status_backend->render_text = [status_font, &status_font_mutex](const ::cw::lib::String& text,
            const ::cw::lib::String&, ::cw::lib::Integer point_size, ::cw::lib::Color color, bool bold) {
            return render_status_text(status_font, status_font_mutex, text, static_cast<int>(point_size), color, bold);
        };
        status_backend->measure_text = [status_font, &status_font_mutex](const ::cw::lib::String& text,
            const ::cw::lib::String&, ::cw::lib::Integer points, bool bold) {
            std::scoped_lock lock(status_font_mutex);
            return ::cardwirth::app::measure_status_text(status_font, text, points, bold);
        };
        ::cw::sprite::statusbar::set_host_services(status_backend);

        const bool hosted_adapter = context.native_adapter && context.native_adapter->is_hosted();
        const bool all_host = options.host_mode == EngineHostMode::All;
        if (hosted_adapter != all_host) {
            throw std::runtime_error(
                "Engine host mode does not match the selected presentation adapter");
        }
        if (all_host) {
            ::cw::dialog::set_all_mode(true);
        }
        application = std::make_shared<::cw::frame::MyApp>();
        application->m_embedded_session = embedded;
        if (context.native_adapter)
            application->set_native_adapter(context.native_adapter);
        if (!application->OnInit())
            throw std::runtime_error("CardWirthPy_C application initialization was cancelled");
        if (encrypted_user_storage_active) {
            // 従来の起動時検査の一部は、ネイティブのファイルシステ
            // ムでUserDataルートの存在を確保する。 暗号化
            // マウント中は中身を作れないため、空の外枠だけを削除す
            // る。 再帰削除はせず、ユーザーの内容には触れない。
            std::error_code empty_root_error;
            const auto logical_root = context.user_data_root;
            if (std::filesystem::is_directory(logical_root, empty_root_error) &&
                std::filesystem::is_empty(logical_root, empty_root_error))
                (void)std::filesystem::remove(logical_root, empty_root_error);
        }
        if (context.native_adapter && context.native_adapter->is_hosted() &&
            !application->m_frame_typed)
            throw std::runtime_error(
                "Hosted Engine cannot open the native skin-conversion frontend");
        ::cw::frame::main();
        ::cardwirth::debug::log("CardWirthPy_C Engine shutdown");
        cleanup();
        return 0;
    } catch (const std::exception& error) {
        const std::string message = std::string(error.what()) + "\r\n\r\nStack trace:\r\n" +
                                    ::cw::thread::current_exception_stacktrace();
        ::cardwirth::debug::log(std::string("Startup error: ") + error.what());
        ::cardwirth::debug::log_current_stacktrace("Startup exception stack");
        if (std::find(launch_arguments.begin(), launch_arguments.end(),
                      "--smoke-test") == launch_arguments.end())
            report_startup_error(context, message);
        else
            std::cerr << "CardWirth Engine smoke startup failed: " << error.what() << '\n';
    } catch (...) {
        const std::string message = "Unknown exception during CardWirthPy_C startup\r\n\r\nStack trace:\r\n" +
                                    ::cw::thread::current_exception_stacktrace();
        ::cardwirth::debug::log("Startup error: unknown exception");
        ::cardwirth::debug::log_current_stacktrace("Unknown startup exception stack");
        if (std::find(launch_arguments.begin(), launch_arguments.end(),
                      "--smoke-test") == launch_arguments.end())
            report_startup_error(context, message);
        else
            std::cerr << "CardWirth Engine smoke startup failed: unknown exception\n";
    }
    cleanup();
    return 1;
}

}  // namespace

int run_cardwirth_engine(const AppLaunchOptions& options) {
    EngineSessionContext context;
    context.application_directory = options.application_directory;
    context.user_data_root = options.user_data_root;
    return run_engine_session(context, options, false);
}

int run_engine_embedded(const ::cardwirth::platform::AppRuntimeContext& host_context,
                        const AppLaunchOptions& options) {
    EngineSessionContext context;
    context.files.asset_storage = &host_context.asset_storage_ref();
    context.files.user_storage = &host_context.user_storage_ref();
    context.files.paths = host_context.paths;
    context.application_directory = options.application_directory.empty()
        ? host_context.paths.app_root : options.application_directory;
    context.user_data_root = options.user_data_root.empty()
        ? host_context.paths.user_root : options.user_data_root;
    context.sdl_initialized = host_context.sdl_initialized;
    context.ttf_initialized = host_context.ttf_initialized;
    context.audio_initialized = host_context.audio_initialized;
    context.owns_sdl = false;
    context.owns_ttf = false;
    context.owns_audio = false;
    context.owns_com = false;
    context.owns_logger = false;
    context.owns_process_handlers = false;
    context.report_error = host_context.report_error;
    return run_engine_session(context, options, true);
}

namespace detail {

class HostedEngineSessionImpl final {
public:
    ~HostedEngineSessionImpl() {
        force_stop_and_join();
    }

    bool start(const ::cardwirth::platform::AppRuntimeContext& host_context,
               const AppLaunchOptions& options,
               std::uintptr_t native_host_window) {
        if (state_ && state_->running.load()) return false;
        if (thread_.joinable()) thread_.join();
        // Engineワーカーが共通のシステム形式ダイアログを初期化する
        // 前に、 SDLホストのスレッドでシステムのUI密度を取得する。
        (void)::cw::lib::ui::system_dialog_metrics();
        state_ = std::make_shared<HostedEngineSessionState>();
        state_->native_host_window = native_host_window;
        const auto state = state_;
        state->running.store(true);
        const auto adapter = make_frame_adapter();
        const auto* context_ptr = &host_context;
        thread_ = std::thread([state, adapter, context_ptr, options] {
            EngineSessionContext context;
            context.files.asset_storage = &context_ptr->asset_storage_ref();
            context.files.user_storage = &context_ptr->user_storage_ref();
            context.files.paths = context_ptr->paths;
            context.application_directory = options.application_directory.empty()
                ? context_ptr->paths.app_root : options.application_directory;
            context.user_data_root = options.user_data_root.empty()
                ? context_ptr->paths.user_root : options.user_data_root;
            context.sdl_initialized = context_ptr->sdl_initialized;
            context.ttf_initialized = context_ptr->ttf_initialized;
            context.audio_initialized = context_ptr->audio_initialized;
            context.owns_sdl = false;
            context.owns_ttf = false;
            context.owns_audio = false;
            context.owns_com = true;
#if defined(CARDWIRTH_ENGINE_PRIVATE_ENCRYPTION) && defined(CARDWIRTH_ENGINE_ONLY)
            // 暗号化された単一製品のホストでは、平文のロガーを
            // 開かない。 Engineは暗号化ユーザーStorage
            // のマウント後にロガーを初期化し、書き出す。
            context.owns_logger = true;
#else
            context.owns_logger = false;
#endif
            context.owns_process_handlers = false;
            context.native_adapter = adapter;
            context.report_error = [state, original = context_ptr->report_error](
                                       std::string_view message) {
                {
                    std::scoped_lock lock(state->mutex);
                    state->error.assign(message);
                }
                if (original) original(message);
            };
            ::cw::dialog::set_all_mode(true, state->native_host_window);
            const int code = run_engine_session(context, options, true);
            state->exit_code.store(code);
            state->running.store(false);
            state->finished.store(true);
        });
        return true;
    }

    void request_stop(bool already_confirmed) {
        if (state_) state_->close_cancelled.store(false, std::memory_order_release);
        HostedEngineInput input{HostedEngineInput::Kind::Close};
        // Frameを所有するスレッドで通常の終了処理を使う。 ホームへ戻る
        // 確認は済んでいるが、通常のアプリ終了では引き続きCLOSEを送る。
        input.value = already_confirmed ? 1 : 0;
        enqueue(std::move(input));
    }


    void force_stop_and_join() {
        if (state_ && state_->running.load()) {
            state_->force_stop.store(true);
        }
        if (thread_.joinable()) thread_.join();
    }

    std::shared_ptr<::cw::frame::NativeFrameAdapter> make_frame_adapter() {
        if (!state_) state_ = std::make_shared<HostedEngineSessionState>();
        return std::make_shared<HostedFrameAdapter>(state_);
    }

    void enqueue(HostedEngineInput input) {
        if (!state_ || !state_->running.load()) return;
#if defined(__EMSCRIPTEN__)
        input.queued_at = std::chrono::steady_clock::now();
#endif
        std::scoped_lock lock(state_->mutex);
        const bool targets_dialog =
            input.kind == HostedEngineInput::Kind::DialogAction ||
            input.kind == HostedEngineInput::Kind::DialogPointer ||
            input.kind == HostedEngineInput::Kind::DialogHover ||
            input.kind == HostedEngineInput::Kind::DialogContext ||
            input.kind == HostedEngineInput::Kind::DialogMiddle ||
            input.kind == HostedEngineInput::Kind::DialogKey ||
            input.kind == HostedEngineInput::Kind::DialogScroll ||
            input.kind == HostedEngineInput::Kind::DialogWheel ||
            input.kind == HostedEngineInput::Kind::DialogDropFiles ||
            input.kind == HostedEngineInput::Kind::TextInput ||
            input.kind == HostedEngineInput::Kind::TextEditing;
        if (targets_dialog)
            input.dialog_model_id = displayed_dialog_model_id_.load(
                std::memory_order_acquire);
        state_->inputs.push_back(std::move(input));
    }

    std::shared_ptr<HostedEngineSessionState> state_{};
    std::thread thread_{};
    std::atomic<std::uint64_t> displayed_dialog_model_id_{};
};

}  // namespace detail

HostedEngineSession::HostedEngineSession()
    : impl_(std::make_unique<detail::HostedEngineSessionImpl>()) {}

HostedEngineSession::~HostedEngineSession() = default;

bool HostedEngineSession::start(
    const ::cardwirth::platform::AppRuntimeContext& context,
    const AppLaunchOptions& options,
    std::uintptr_t native_host_window) {
    return impl_->start(context, options, native_host_window);
}


void HostedEngineSession::request_stop(bool already_confirmed) { impl_->request_stop(already_confirmed); }

bool HostedEngineSession::close_cancelled() const noexcept {
    return impl_->state_ && impl_->state_->close_cancelled.load(std::memory_order_acquire);
}

bool HostedEngineSession::running() const noexcept {
    return impl_->state_ && impl_->state_->running.load();
}

bool HostedEngineSession::finished() const noexcept {
    return impl_->state_ && impl_->state_->finished.load();
}

int HostedEngineSession::exit_code() const noexcept {
    return impl_->state_ ? impl_->state_->exit_code.load() : -1;
}

std::string HostedEngineSession::error() const {
    if (!impl_->state_) return {};
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->error;
}

HostedEngineFrameSnapshot HostedEngineSession::frame_snapshot() const {
    if (!impl_->state_) return {};
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->frame;
}

std::shared_ptr<::cw::frame::NativeFrameAdapter>
HostedEngineSession::make_frame_adapter() {
    return impl_ ? impl_->make_frame_adapter() : nullptr;
}

std::optional<HostedEngineModalSnapshot> HostedEngineSession::modal_snapshot() const {
    if (!impl_->state_) return std::nullopt;
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->active_dialog;
}

void HostedEngineSession::displayed_dialog_model(
    const std::uint64_t model_id) noexcept {
    if (impl_)
        impl_->displayed_dialog_model_id_.store(model_id,
                                                std::memory_order_release);
}

void HostedEngineSession::publish_screenshot_geometry(
    const std::optional<HostedEngineModalSnapshot>& shown, std::array<float,4> frame,
    std::array<float,4> dialog, bool debugger_focused)
{
    if(!impl_ || !impl_->state_)return;
    const auto state=impl_->state_;
    std::scoped_lock lock(state->mutex);
    state->screenshot_debugger_focused=debugger_focused;
    state->screenshot_host.root=0;
    state->screenshot_host.frame_bounds={frame[0],frame[1],frame[2],frame[3]};
    state->screenshot_host.windows.clear();state->screenshot_model_id=0;
    if(!shown || !state->active_dialog || shown->model_id!=state->active_dialog->model_id ||
        !shown->presentation_surface || dialog[2]<=0 || dialog[3]<=0)return;
    ::cw::frame::ScreenshotWindow window;
    window.id=shown->model_id;
    // ホスト内のダイアログは、フォーカスがデバッガー領域の外へ移っても 実際
    // のデバッガーの所有関係を保つ。判定には共通Frameの親子関係を使う。
    window.parent=shown->debugger_descendant ? (std::numeric_limits<std::uint64_t>::max)() : 0;
    window.title=shown->title;
    window.bounds={dialog[0],dialog[1],dialog[2],dialog[3]};
    window.client=shown->presentation_surface;window.visible=true;
    window.focused=!debugger_focused;
    state->screenshot_model_id=shown->model_id;
    state->screenshot_host.windows.push_back(std::move(window));
}

HostedDebuggerSnapshot HostedEngineSession::debugger_snapshot() const {
    if (!impl_->state_) return {};
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->debugger;
}

HostedStatusSnapshot HostedEngineSession::status_snapshot() const {
    if (!impl_->state_) return {};
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->status;
}

void HostedEngineSession::status_action(HostedStatusAction action) {
    if (action == HostedStatusAction::OpenData) return; // ホスト側だけで扱う経路。
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::StatusAction;
    input.value = static_cast<int>(action);
    impl_->enqueue(std::move(input));
}

std::string HostedEngineSession::input_probe() const {
    if (!impl_->state_) return "cwpy=none";
    std::scoped_lock lock(impl_->state_->mutex);
    return impl_->state_->input_probe;
}


void HostedEngineSession::dialog_action(HostedEngineDialogAction action,
                                         int index, std::uint32_t modifiers) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogAction;
    input.index = index;
    input.value = static_cast<int>(action);
    input.modifiers = modifiers;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_pointer(HostedEngineDialogAction action,
                                         int index, float x, float y,
                                         bool pressed,
                                         const ::cw::dialog::DialogPresentationModifiers modifiers) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogPointer;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    input.value = static_cast<int>(action);
    input.index = index;
    input.pressed = pressed;
    input.modifiers = modifiers;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_key(
    HostedEngineDialogKey key, bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogKey;
    input.value = static_cast<int>(key);
    input.pressed = pressed;
    input.modifiers = modifiers;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_scroll(float x, float y, int lines) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogScroll;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    input.value = lines;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_wheel(float x, float y, int rotation) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogWheel;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    input.value = rotation;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_hover(float x, float y) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogHover;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_middle(float x, float y, bool pressed) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogMiddle;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    input.pressed = pressed;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_context(float x, float y) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogContext;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::show_dialog_for_test(HostedEngineModalKind kind) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::TestDialog;
    input.value = static_cast<int>(kind);
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::dialog_drop_files(std::vector<std::string> paths) {
    if (paths.empty()) return;
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DialogDropFiles;
    input.paths = std::move(paths);
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_command(HostedDebuggerCommand command) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerCommand;
    input.value = static_cast<int>(command);
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_action(
    const HostedEngineDialogAction action, const int index) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerAction;
    input.value = static_cast<int>(action);
    input.index = index;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_key(
    const HostedEngineDialogKey key, const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerKey;
    input.value = static_cast<int>(key);
    input.pressed = pressed;
    input.modifiers = modifiers;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_text(const std::string& text,bool composition,int start,int length,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    detail::HostedEngineInput input;
    input.kind=detail::HostedEngineInput::Kind::DebuggerText;
    input.text=text;input.pressed=composition;input.index=start;input.editing_length=length;
    input.modifiers=modifiers;
    impl_->enqueue(std::move(input));
}
void HostedEngineSession::debugger_blur() {
    detail::HostedEngineInput input;
    input.kind=detail::HostedEngineInput::Kind::DebuggerBlur;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_context(float x, float y) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerContext;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_hover(float x, float y) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerHover;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_pointer(HostedEngineDialogAction action,int index,float x,float y,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    detail::HostedEngineInput input;
    input.kind=detail::HostedEngineInput::Kind::DebuggerPointer;
    input.value=static_cast<int>(action);
    input.index=index;
    input.x=static_cast<int>(std::lround(x));
    input.y=static_cast<int>(std::lround(y));
    input.pressed=pressed;
    input.modifiers=modifiers;
    impl_->enqueue(std::move(input));
}
void HostedEngineSession::debugger_pointer_cancel() {
    detail::HostedEngineInput input;
    input.kind=detail::HostedEngineInput::Kind::DebuggerPointerCancel;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::debugger_scroll(float x, float y, int lines) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::DebuggerScroll;
    input.x = static_cast<int>(std::lround(x));
    input.y = static_cast<int>(std::lround(y));
    input.value = lines;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::pointer_motion(int x, int y, bool touch) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Motion;
    input.x = x;
    input.y = y;
    input.touch = touch;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::pointer_button(int x, int y, int button, bool pressed,
                                         bool touch) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Button;
    input.x = x;
    input.y = y;
    input.value = button;
    input.pressed = pressed;
    input.touch = touch;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::pointer_wheel(int x, int y, int delta) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Wheel;
    input.x = x;
    input.y = y;
    // 集成体の4番目のフィールドはvalueではなくindex。 Frame側はvalue
    // を符号付きのホイール移動量として読み、indexはダイアログ部品の識別に使う。
    input.value = delta;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::key(int native_key, bool pressed, std::uint32_t modifiers) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Key;
    input.value = native_key;
    input.pressed = pressed;
    input.modifiers = modifiers;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::text_input(std::string utf8) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::TextInput;
    input.text = std::move(utf8);
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::text_editing(std::string composition,
                                       int start, int length) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::TextEditing;
    input.text = std::move(composition);
    input.x = start;
    input.editing_length = length;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::focus(bool focused) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Focus;
    input.pressed = focused;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::pointer_cancel() {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::PointerCancel;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::suspend() {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Suspend;
    input.pressed = true;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::resume() {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Suspend;
    input.pressed = false;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::set_covered(bool covered) {
    detail::HostedEngineInput input;
    input.kind = detail::HostedEngineInput::Kind::Covered;
    input.pressed = covered;
    impl_->enqueue(std::move(input));
}

void HostedEngineSession::trim_memory() {
    if (!impl_->state_) return;
    std::scoped_lock lock(impl_->state_->mutex);
    impl_->state_->frame.surface.reset();
    impl_->state_->frame.sequence = impl_->state_->next_frame_sequence++;
}

}  // namespace cardwirth::app
