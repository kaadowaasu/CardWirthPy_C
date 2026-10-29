#pragma once
#include "platform/app_visibility.hpp"

// cw/frame.pyに対応する、ネイティブ画面
// との接続とCardWirthのFrameモデル。
#include "cw/lib/compat.hpp"
#include "cw/lib/exception_future.hpp"
#include "cw/fwd.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <cstdint>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace cw::frame {
class Frame;

// 画面取得の基本操作は実際のウィンドウを表す。対象の選別・配置・タイトル・保
// 存は、 ネイティブ・SDL・埋め込み表示で共通のFrameの規則に従う。
struct ScreenshotRect {
    ::cw::lib::Real x{}, y{}, width{}, height{};
};
struct ScreenshotWindow {
    std::uint64_t id{}, parent{};
    ::cw::lib::String title{};
    ScreenshotRect bounds{};
    std::shared_ptr<const ::cw::lib::Surface> client{};
    bool visible{}, iconized{}, debugger{}, touch_tools{}, top_level{true}, focused{};
};
struct ScreenshotWindows {
    std::uint64_t root{};
    ScreenshotRect frame_bounds{};
    std::vector<ScreenshotWindow> windows{};
};

struct ModalChoice {
    ::cw::lib::String label{};
    ::cw::lib::Integer id{};
    ::cw::lib::Integer width{-1};
    ::cw::lib::String description{};
};

// 選択されたスキンのドロップは、一つの要求としてゲームスレッドの受信箱へコピー
// する。 適用コールバックはヘッドレスの契約テストで結果を観測するためのもの。
// 製品では省略し、同じフィールドでCWPy::update_skinを呼ぶ。
struct DropSkinUpdateRequest {
    ::cw::lib::String skindirname{};
    bool changearea{true};
    bool restartop{true};
    std::optional<::cw::lib::Callback> afterfunc{};
    bool switch_skin{true};
    bool switch_yado{};
};
using DropSkinUpdateApply = std::function<void(
    ::cw::thread::CWPy&, const DropSkinUpdateRequest&)>;
[[nodiscard]] DropSkinUpdateRequest make_drop_skin_update_request(
    ::cw::lib::String arg_skindirname);
void queue_drop_skin_update(
    std::shared_ptr<::cw::thread::CWPy> arg_runtime,
    DropSkinUpdateRequest arg_request,
    DropSkinUpdateApply arg_apply = {});

// プラットフォームに依存しないウィンドウとの接続。ゲームとFrame
// のモデルはこのアダプターだけを使い、 Win32/SDLの実
// 装を変えてもゲーム処理へネイティブハンドルを持ち込まない。
class NativeFrameAdapter {
public:
    using EventSink = std::function<void(::cw::lib::String, std::shared_ptr<::cw::lib::Window>)>;
    virtual ~NativeFrameAdapter() = default;
    // ヘッドレス実装は明示的なテスト用で、製品の暗黙の代替
    // 先にはしない。 具体的なOS側の実装では既定のfalse
    // を保つ。テストは下のファクトリーを使い、 RTTI
    // や非公開の実装名に頼らず、この区別を確認できる。
    virtual bool is_headless() const noexcept { return false; }
    // 埋め込みアダプターは最上位のネイティブウィンドウを作らず、所
    // 有するアプリ内へ描く。 Frame::mainはこの情報か
    // ら、Windowsでも共通のディスパッチャーループを選ぶ。
    virtual bool is_hosted() const noexcept { return false; }
    // モデルが所有する共通Presentationを表示できる場合に真。
    // SDLの埋め込み実装とWin32の汎用ホストはいずれも真を返す。
    // ホストの能力を示すもので、ダイアログのレイアウト選択には使わない。
    virtual bool supports_common_presentation() const noexcept {
        return is_hosted();
    }
    // Pythonのdpi_levelの式を共通の起動処理で適
    // 用する前に、 wx.Frameの既定フォントのピクセル
    // 高さを取得する。 既定ではネイティブフォントの取得をホ
    // スト側へ隠し、ヘッドレステストでは固定の高さを渡せる。
    virtual std::optional<::cw::lib::Integer> default_font_pixel_height() const;
    // ネイティブのポインター入力は現在のクライアント・ウィンド
    // ウ座標で届くが、 共通の当たり判定はFrameが直前に表
    // 示した画像の寸法を使う。 具体的なウィンドウアダプター
    // がクライアント寸法を提供して座標を変換し、 ネイティブ
    // 画面を持たない場合は従来の倍率変換をCWPyへ任せる。
    virtual std::optional<::cw::lib::Point> map_client_to_surface(
        const Frame& arg_frame, ::cw::lib::Point arg_point) const {
        (void)arg_frame;
        (void)arg_point;
        return std::nullopt;
    }
    virtual void hosted_loop_step(Frame& arg_frame) { (void)arg_frame; }
    virtual void attach_surface(Frame& arg_frame, const std::shared_ptr<::cw::lib::Window>& arg_panel) = 0;
    // Engineが所有するネイティブ画面だけを解放する。 埋め込みEngine
    // がホームへ戻っても、SDL自体はALLホストが維持する。
    virtual void detach_surface(Frame& arg_frame) { (void)arg_frame; }
    virtual ::cw::lib::Rect display_geometry(const Frame& arg_frame) const = 0;
    virtual void set_icon(Frame& arg_frame, const std::shared_ptr<::cw::lib::Window>& arg_window) = 0;
    // 最上位のネイティブウィンドウを所有する実装だけがタイトルバーへ反映する。 ヘッド
    // レス・埋め込みFrameもモデルのタイトルは共有するが、タイトルバーは持たない。
    virtual void set_title(Frame& arg_frame, const ::cw::lib::String& arg_title) {
        (void)arg_frame; (void)arg_title;
    }
    virtual void bind(Frame& arg_frame, EventSink arg_sink) = 0;
    virtual void set_iconized(Frame& arg_frame, bool iconized) {
        (void)arg_frame; (void)iconized;
    }
    // 所有するUIループを起こし、Frameの保留コールバックを
    // 処理させる。 呼び出し元のワーカー上でキューを実行しない。
    virtual bool post_exec(Frame& arg_frame) { (void)arg_frame; return false; }
    // 現在の表示バッファーをネイティブウィンドウへ反映し、所有するUI
    // スレッドが描き終えるまで待つ。 オープニング中に使い、InvalidateRect
    // の統合でフレームが省略されるのを防ぐ。
    virtual void present_now(Frame& arg_frame) { (void)arg_frame; }
    // FrameのUI側の受信処理で用意した、不変のバッフ
    // ァーを表示する。 コールバックは必ず所有するUIスレ
    // ッドで呼び、ゲーム・ワーカースレッドからは呼ばない。
    virtual void present(Frame& arg_frame, const std::optional<::cw::lib::Rect>& arg_clip) {
        (void)arg_clip;
        present_now(arg_frame);
    }
    virtual void adjust_position(Frame& arg_frame, const std::shared_ptr<::cw::lib::Window>& arg_window) = 0;
    // メインのゲームパネルに対するwx.Frame.SetClientSize相当の操作。
    virtual void set_client_size(Frame& arg_frame, const std::shared_ptr<::cw::lib::Window>& arg_panel,
                                 ::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height) {
        (void)arg_frame;
        (void)arg_panel;
        (void)arg_width;
        (void)arg_height;
    }
    virtual bool dispatch_menu_accelerator(Frame& arg_frame, ::cw::lib::Integer arg_keycode) = 0;
    virtual ScreenshotWindows screenshot_windows(Frame& arg_frame, bool capture_clients);
    virtual bool compose_dialogs(Frame& arg_frame, ::cw::lib::Bitmap& arg_bitmap,
                                 ::cw::lib::Integer arg_content_y,
                                 std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_fore,
                                 std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_back) = 0;
    virtual void watch_left_button(Frame& arg_frame, ::cw::lib::Integer arg_delay_ms) = 0;
    virtual std::optional<bool> modal_message(Frame& arg_frame, const ::cw::lib::String& arg_title,
                                               const ::cw::lib::String& arg_text, bool arg_question,
                                               bool arg_error) = 0;
    // アプリ側のモーダル確認は、すべてのOSで同じ共通Messageモデルを使
    // う。 アダプターごとに変えられるのは、表示先への接続と合成の仕組みだけ。
    virtual std::optional<::cw::lib::Integer> modal_choices(
        Frame& arg_frame, const ::cw::lib::String& arg_title,
        const ::cw::lib::String& arg_text,
        const std::vector<ModalChoice>& arg_choices) {
        (void)arg_frame;
        (void)arg_title;
        (void)arg_text;
        (void)arg_choices;
        return std::nullopt;
    }
    virtual std::optional<::cw::lib::String> modal_text_input(
        Frame& arg_frame, const ::cw::lib::String& arg_title,
        const ::cw::lib::String& arg_message,
        const ::cw::lib::String& arg_initial,
        ::cw::lib::Integer arg_max_length) {
        (void)arg_frame;
        (void)arg_title;
        (void)arg_message;
        (void)arg_initial;
        (void)arg_max_length;
        return std::nullopt;
    }
    // 必要な機能を実装した共通DialogPresentationProvider
    // を、入れ子のモーダルとして実行する。配
    // 置・部品・状態遷移はモデルが所有する。
    virtual bool modal_presentation(
        Frame& arg_frame, std::shared_ptr<::cw::lib::Object> arg_model) {
        (void)arg_frame;
        (void)arg_model;
        return false;
    }
    // wx/SDLの埋め込み境界。Win32ではSDLの全画面化前に通常サイズの所有
    // 者を作る。 SDLホストでは、所有者をディスプレイに合わせたサイズにできる。
    virtual ::cw::lib::Size startup_fullscreen_client_size(
        const Frame&, ::cw::lib::Size display_size) const { return display_size; }
    virtual void set_fullscreen(Frame&, bool) {}
};

// 単体・ロジックテスト向けに明示して使う、何もしないアダプタ
// ー。 製品の構成元は具体的なSDL・ネイティブ実装を渡す。
// Windows以外で暗黙のアダプターを求めた場合は、UI
// なしで動作を続けずUnsupportedエラーにする。
std::shared_ptr<NativeFrameAdapter> make_headless_native_adapter();
std::shared_ptr<NativeFrameAdapter> make_headless_native_adapter(
    ::cw::lib::Integer arg_default_font_pixel_height);

// SDLとWin32のアダプターで共用する座標計算。
// ネイティブ側は現在のクライアント寸法だけを提供する。
::cw::lib::Point map_client_point_to_surface(
    ::cw::lib::Point arg_point,
    ::cw::lib::Integer arg_client_width,
    ::cw::lib::Integer arg_client_height,
    ::cw::lib::Integer arg_surface_width,
    ::cw::lib::Integer arg_surface_height);

// ネイティブの最上位ダイアログを所有者ウィンドウの中央へ配置
// する。 ハンドルの受け渡しはOSに依存させず、ダイアログ
// の公開インターフェースへWin32ヘッダーを要求しない。
void center_native_window(std::uintptr_t arg_window, std::uintptr_t arg_parent);

class Frame : public ::cw::lib::Object, public std::enable_shared_from_this<Frame> {
public:
    Frame();
    virtual ~Frame();
    ::cw::lib::Value m__EVTTYPE_EXECFUNC{};
    std::optional<std::shared_ptr<::cw::dialog::cardcontrol::SelectCard>> m__cardholder{};
    ::cw::lib::Real m__clock{};
    std::optional<std::shared_ptr<::cw::dialog::cardcontrol::HandView>> m__handview{};
    std::optional<std::shared_ptr<::cw::dialog::cardcontrol::InfoView>> m__infoview{};
    bool m__keybind{};
    std::optional<std::shared_ptr<::cw::dialog::cardcontrol::ReplCardHolder>> m__replcardholder{};
    ::cw::lib::Integer m__retry_count{};
    std::shared_ptr<::cw::setting::Setting> m__setting{};
    ::cw::lib::Value m__skindirname{};
    bool m__sync_running{};
    ::cw::lib::Value m_app{};
    std::optional<std::shared_ptr<::cw::scenariodb::Scenariodb>> m_db{};
    std::optional<std::shared_ptr<::cw::debug::debugger::Debugger>> m_debugger{};
    // FrameモデルはObjectなので、親・モーダルの共通問い合わせには
    // 別のUIオブジェクトではなく、一つの固定したウィンドウ識別子を公開する。
    std::shared_ptr<::cw::lib::Frame> m_window_identity{std::make_shared<::cw::lib::Frame>()};
    bool m_window_identity_initialized{};
    std::shared_ptr<::cw::lib::Window> window_identity();
    ::cw::lib::ValueMap m_dlgeventtypes{};
    std::optional<::cw::lib::Callback> m_filter_event{};
    ::cw::lib::Integer m_initialThreadCount{};
    std::atomic_bool m_is_iconized{false};
    ::cardwirth::platform::AppVisibility m_app_visibility;
    std::atomic_bool m_host_covered{false};
    bool m_audio_suspended{}; // 入力イベントごとではなく、Engineスレッドで適用する。
    bool m_audio_paused{}; // 最小化による消音と、別画面に覆われたことによる一時停止を区別する。
    std::vector<std::shared_ptr<::cw::lib::Dialog>> m_kill_list{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_style{};
    ::cw::lib::Value m_thread{};
    // UIディスパッチャーの状態。Python版のwxはコマンドイベン
    // トをポストする。 互換層も同じFIFOと確定の規則を保ち、SDL
    // ・ネイティブ側からwxヘッダーに依存せず処理できるようにする。
    struct PendingCall {
        ::cw::lib::Callback func{};
        ::cw::lib::Arguments args{};
        ::cw::lib::KeywordArguments kwargs{};
        std::shared_ptr<::cw::lib::Promise<::cw::lib::Value>> result{};
        bool modal_completion{}; // 既に受け付けられた操作だけが設定できる。

    };
    mutable std::mutex m_exec_mutex{};
    std::condition_variable m_exec_cv{};
    std::deque<PendingCall> m_exec_queue{};
    std::thread::id m_ui_thread{};
    bool m_dispatching{};
    // UIディスパッチャーとワーカー側の受信箱の両方から読
    // むため、アトミックにする。 表示の終了処理は、保留コ
    // ールバックとは別のキューミューテックスで調整される。
    std::atomic_bool m_exec_shutdown{false};
    // 単独EngineはWin32のメッセージループを所有するためWM_QUITを
    // 送る。 ALLに埋め込まれたEngineは自分の入れ子ループだけを終える。
    // WM_QUITを送るとEngine画面を閉じた後にホストまで終了してしまう。
    bool m_post_quit_message_on_destroy{true};
    std::thread m_game_thread{};
    // 起動時の索引ワーカーは完了まで所有する。 索引作成
    // の途中でALLからホームへ戻った場合も保持する。
    std::unique_ptr<::cw::scenariodb::ScenariodbUpdatingThread> m_scenariodb_updater{};
    bool m_has_focus{};
    bool m_mouse_active{};
    std::tuple<bool, bool, bool> m_mouse_buttons{};
    // キャプチャ喪失後に遅れて届いたネイティブUPは、新しいDOWNが来るまで無効とする。
    std::tuple<bool, bool, bool> m_cancelled_pointer_buttons{};
    bool m_modal_pointer_suppressed{};
    std::uint64_t m_modal_pointer_epoch{};
    std::uint64_t m_modal_game_committed_epoch{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_window_position{};
    std::shared_ptr<::cw::lib::Dialog> m_active_dialog{};
    // 設定・選択などの専用ダイアログは、互換Dialogで
    // はなくObjectのモデルを使う。 ネイティブウィン
    // ドウがイベント配送にモデルの生ポインターを使う間、
    // 汎用の有効ダイアログ情報と並べて所有権を保持する。
    std::shared_ptr<::cw::lib::Object> m_active_dialog_model{};
    // 進捗・処理中の画面は有効なモーダルを一時的に覆うが、
    // その所有権やモーダルセッションの識別子は置き換えない。
    mutable std::mutex m_dialog_presentation_mutex{};
    std::shared_ptr<::cw::lib::Object> m_transient_dialog_model{};
    // call_dlgがネイティブの表示処理を送る際に割り当てる。 終了コールバックは
    // m_modal_sessions.back()を推測せず、このIDを消費する。
    std::uint64_t m_active_dialog_session{};
    struct SuspendedDialogState {
        std::shared_ptr<::cw::lib::Dialog> dialog{};
        std::shared_ptr<::cw::lib::Object> model{};
        std::uint64_t session{};
    };
    std::map<std::uint64_t, SuspendedDialogState> m_suspended_dialog_sessions{};
    struct DeferredDialogClose {
        bool lock_menu_card{};
        bool redraw{true};
    };
    // 子が表示中のモーダルであっても、背後の親が非同期に完
    // 了する場合がある。 wxと同様に子が終わるまで親の
    // 終了を保留し、親のトークンを現在の子へ適用しない。
    std::map<std::uint64_t, DeferredDialogClose> m_deferred_dialog_closes{};
    // call_dlgはkwargsをここへコピーし、OnERROR/OnDATACOMPが Python
    // のevent.argsと同じようにccard/shutdownを読めるようにする。
    ::cw::lib::KeywordArguments m_dialog_kwargs{};
    std::vector<::cw::lib::String> m_dialog_history{};
    // ネイティブのドロップ処理は、一括操作が完了するまで借用されたパスをコピー
    // して保持する。 完了したデータは必ず共通のDROPFILES処理へ渡す。
    std::vector<::cw::lib::String> m_native_drop_files{};
    std::shared_ptr<NativeFrameAdapter> m_native_adapter{};
    std::map<::cw::lib::String, std::function<void(std::shared_ptr<::cw::lib::Window>)>> m_native_event_handlers{};
    std::uintptr_t m_native_window_handle{};
    mutable std::mutex m_present_mutex{};
    std::shared_ptr<::cw::lib::Surface> m_present_surface{};
    // Win32は不変のBGRAスナップショットを使う。 ゲームスレッド
    // はm_present_mutexを保持して変更部分だけを更新し、
    // WM_PAINTから可変のソフトウェアRGBA画像を直接読まない。
    std::vector<std::uint8_t> m_present_bgra{};
    ::cw::lib::Integer m_present_width{};
    ::cw::lib::Integer m_present_height{};
    std::mutex m_present_wait_mutex{};
    std::condition_variable m_present_wait_cv{};
    std::atomic<unsigned> m_present_seq{0};
    std::atomic<unsigned> m_present_acked{0};
    // ゲームスレッドの合成結果は、この受信箱から不変の画像としてUI
    // スレッドへ渡す。 通常フレームはまとめられるが、起動アニメーシ
    // ョンではwait_for_paintを使い、 各フレームがUpdateWindow
    // へ届いてからワーカーが次へ進むようにする。
    struct PendingPresent {
        std::shared_ptr<::cw::lib::Surface> surface{};
        std::optional<::cw::lib::Rect> clip{};
        bool wait_for_paint{};
        std::shared_ptr<::cw::lib::Promise<void>> completion{};
    };
    mutable std::mutex m_present_queue_mutex{};
    std::deque<PendingPresent> m_present_queue{};
    // 再利用可能な不変のスナップショット。プールだけが所有するとき（use_count
    // == 1）に限り書き込める。 保留中・UI・描画側の参照が
    // あれば再利用されないため、別の所有状態の管理機構は設けない。
    std::vector<std::shared_ptr<::cw::lib::Surface>> m_present_pool{};
    std::uint64_t m_present_replaced_count{};
    std::uint64_t m_present_fallback_count{};
    void initialize(std::shared_ptr<::cw::frame::MyApp> arg_app, ::cw::lib::String arg_skindirname);
    // 起動ダイアログで選んだスキンを適用する。空は明示選択なしを表し
    // 、 Settingが読み込み済みのスキン・資源をそのまま保つ。
    bool apply_startup_skin_selection();
    void set_transient_dialog_model(std::shared_ptr<::cw::lib::Object> arg_model);
    void clear_transient_dialog_model(const ::cw::lib::Object* arg_model);
    [[nodiscard]] std::shared_ptr<::cw::lib::Object> dialog_presentation_model() const;
    void apply_window_bounds();
    bool restore_expanded_window_if_needed(::cw::lib::Rect work_area,
                                           ::cw::lib::Size best_size);
    void set_fullscreen_mode(bool fullscreen);
    void resize_window_to_display();
    void _start_wx(std::shared_ptr<Frame> arg_self);
    void set_icon(std::shared_ptr<::cw::lib::Window> arg_win);
    void set_title(::cw::lib::String arg_title);
    std::shared_ptr<::cw::lib::Window> get_displaysize();
    void set_native_adapter(std::shared_ptr<NativeFrameAdapter> arg_adapter);
    void dispatch_native_event(::cw::lib::String arg_name, std::shared_ptr<::cw::lib::Window> arg_event);
    void update_dialogparams();
    void _bind();
    void _bind_customevent();
    // 最終表示の受け渡し。生成側はスレッド安全に送り、UIスレッドが受け取る。
    // queue_presentは呼び出し元でネイティブ描画を実行しない。
    void queue_present(std::shared_ptr<::cw::lib::Surface> arg_surface,
                       std::optional<::cw::lib::Rect> arg_clip,
                       bool arg_wait_for_paint);
    void drain_present_queue();
    void tick_clock(::cw::lib::Integer arg_framerate);
    void wait_frame(::cw::lib::Integer arg_count);
    void start_wait();
    void show_debugger(bool arg_refreshtree);
    void close_debugger();
    void exec_func(::cw::lib::Callback arg_func);
    void post_modal_completion(::cw::lib::Callback arg_func);
    // UIスレッド上でも必ずキューへ積み、終了中のモーダル処理が先に戻れるようにする。
    void post_func(::cw::lib::Callback arg_func);
    // wx.CallAfterの入力更新は共通FIFOのUIキューを使う
    // 。 後続のIMEイベントを抑止する入力利用権は保持しない。 コール
    // バックがない場合や、終了中で受理できない場合はfalseを返す。
    bool post_input_update(::cw::lib::Callback arg_func);
    void exec_func_2(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1);
    void exec_func_3(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2);
    void exec_func_4(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3);
    void exec_func_5(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4);
    void exec_func_6(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5);
    void exec_func_7(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6);
    void exec_func_8(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7);
    void exec_func_9(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8);
    void exec_func_10(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8, ::cw::lib::Value arg_arg9);
    void exec_func_11(::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void enqueue_exec(PendingCall arg_call, bool allow_inline = true);
    bool enqueue_exec_with_input_lease(PendingCall arg_call, bool allow_inline, bool retain_input);
    ::cw::lib::Value sync_exec(::cw::lib::Callback arg_func);
    ::cw::lib::Value sync_exec_2(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1);
    ::cw::lib::Value sync_exec_3(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2);
    ::cw::lib::Value sync_exec_4(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3);
    ::cw::lib::Value sync_exec_5(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4);
    ::cw::lib::Value sync_exec_6(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5);
    ::cw::lib::Value sync_exec_7(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6);
    ::cw::lib::Value sync_exec_8(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7);
    ::cw::lib::Value sync_exec_9(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8);
    ::cw::lib::Value sync_exec_10(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8, ::cw::lib::Value arg_arg9);
    std::optional<::cw::lib::Value> sync_exec_11(::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void OnEXECFUNC(std::shared_ptr<::cw::lib::Window> arg_event);
    void assert_ui_thread() const;
    void OnSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void update_keystate();
    void _update_mousepressed();
    [[nodiscard]] bool native_pointer_context_active(
        const std::shared_ptr<::cw::lib::Window>& arg_event) const;
    [[nodiscard]] bool native_pointer_input_blocked() const;
    bool prepare_native_pointer_button(
        const std::shared_ptr<::cw::lib::Window>& arg_event,
        int arg_button, bool arg_down);
    void OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPointerCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMotion(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMiddleUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMiddleDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_drop_files(std::vector<::cw::lib::String> arg_paths);
    bool show_drop_dialog(const std::shared_ptr<::cw::lib::Object>& arg_model,
                          const ::cw::lib::String& arg_title,
                          const std::function<void()>& arg_after_show = {});
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnIconize(std::shared_ptr<::cw::lib::Window> arg_event);
    void request_iconize(bool iconized);
    void set_host_covered(bool covered);
    void update_audio_suspension();
    void apply_audio_suspension(); // ゲームの一時停止中にも通る、Engineスレッドの安全な処理時点。
    void OnActivateApp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCloseFromFrame(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMove(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCLOSE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSETTINGS(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMENUCARDINFO(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnYADOSELECT(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNOVELSAVESELECT(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPARTYSELECT(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPLAYERSELECT(std::shared_ptr<::cw::lib::Window> arg_event);
    std::optional<std::shared_ptr<::cw::scenariodb::Scenariodb>> open_scenariodb();
    void OnSCENARIOSELECT(std::shared_ptr<::cw::lib::Window> arg_event);
    void ok_scenarioselect(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_dlg);
    void append_killlist(std::shared_ptr<::cw::lib::Dialog> arg_dlg);
    void check_killlist();
    void OnALBUM(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPARTYRECORD(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBACKPACK(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSTOREHOUSE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCARDPOCKETB(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCARDPOCKET(std::shared_ptr<::cw::lib::Window> arg_event);
    void _cardpocket_impl(::cw::lib::String arg_callname);
    void OnHANDVIEW(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCARDPOCKET_REPLACE(std::shared_ptr<::cw::lib::Window> arg_event);
    std::tuple<std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>>> _get_cardcontrolparams(::cw::lib::String arg_callname);
    void OnINFOVIEW(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCHARAINFO(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRETURNTITLE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSAVE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSAVED_MESSAGE(std::shared_ptr<::cw::lib::Window> arg_event);
    void _saved();
    void OnLOAD(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRUNAWAY(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUSECARD(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDATACOMP(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPARTYEDIT(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBATTLECOMMAND(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnINSTRUCTIONS(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF9(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnERROR(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_error(::cw::lib::String arg_text, std::optional<std::shared_ptr<::cw::lib::Window>> arg_parent, bool arg_shutdown);
    void OnNOTICE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMESSAGE(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnYESNO(std::shared_ptr<::cw::lib::Window> arg_event);
    void move_dlg(std::shared_ptr<::cw::lib::Dialog> arg_dlg, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_point);
    void kill_dlg(std::optional<std::shared_ptr<::cw::lib::Dialog>> arg_dlg,
                  bool arg_lockmenucard, bool arg_redraw,
                  std::uint64_t arg_session = 0);
    void begin_modal_pointer_barrier(std::uint64_t arg_epoch);
    bool poll_modal_pointer_release();
    void complete_modal_ui_restore(std::uint64_t arg_epoch);
    // Win32では、いずれかのダイアログがモーダルスタックを所有する間、メインFrameを無効にする。
    void set_background_enabled(bool arg_enabled);
    void begin_dialog_session(std::uint64_t arg_session);
    bool can_screenshot();
    std::optional<std::shared_ptr<::cw::lib::Dialog>> find_activedialog();
    bool save_screenshot();
    void _put_dlgscreenshots(std::shared_ptr<::cw::lib::Bitmap> arg_bmp, ::cw::lib::Integer arg_y, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_fore, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_back);
    void change_selection(std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_selection);
    ::cw::lib::Integer change_cardcontrolarea(::cw::lib::String arg_callname);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> GetClientPosition();
};

class MyApp : public ::cw::lib::Object {
public:
    MyApp() = default;
    virtual ~MyApp() = default;
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_flick_start_pos{};
    ::cw::lib::Real m_flick_start_time{};
    ::cw::lib::Value m_flick_status{};
    ::cw::lib::Value m_flick_window{};
    std::shared_ptr<::cw::dialog::skin::SkinConversionDialog> m_skindlg{};
    // ネイティブメッセージループの全期間、メインFrameを所有する。
    // ワーカーの開始前に、同じ所有ポインターをCWPyへ公開する。
    std::shared_ptr<Frame> m_frame_typed{};
    // 実装を注入する入口をMyAppへ置き、SDLホストがOnInit()
    // より前に ウィンドウ・描画・イベントのアダプターを渡せるようにする
    // 。 Windowsの単独起動では内蔵のWin32アダプターを使う。
    std::shared_ptr<NativeFrameAdapter> m_native_adapter{};
    bool m_embedded_session{};
    void initialize();
    void set_native_adapter(std::shared_ptr<NativeFrameAdapter> arg_adapter);
    bool OnInit();
    void OnCloseSkinDialog(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::Integer FilterEvent(std::shared_ptr<::cw::lib::Event> arg_event);
};

class NoFlick : public ::cw::lib::Object {
public:
    NoFlick() = default;
    virtual ~NoFlick() = default;
};

extern ::cw::lib::Integer g_FLICK_NONE;
extern ::cw::lib::Integer g_FLICK_START;
extern ::cw::lib::Value g__Arg1;
extern ::cw::lib::Value g__Arg2;
extern ::cw::lib::Value g__Arg3;
extern ::cw::lib::Value g__Arg4;
extern ::cw::lib::Value g__Arg5;
extern ::cw::lib::Value g__Arg6;
extern ::cw::lib::Value g__Arg7;
extern ::cw::lib::Value g__Arg8;
extern ::cw::lib::Value g__Arg9;
extern ::cw::lib::Value g__Result;
extern ::cw::lib::Value g__killlist_mutex;

void Frame___init___set_bounds();
void Frame___init___set_bounds_adjust_position();
void Frame_update_dialogparams_func(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame_update_dialogparams_func_func1(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame_update_dialogparams_func_func2(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame_update_dialogparams_func_func3(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame_update_dialogparams_func_func4(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame__bind_panel_setfocus(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame__bind_panel_killfocus(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame__bind_panel_close(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame__bind_activate(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame__bind_activate_app(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame__bind_activate_app_func();
void Frame__bind_activate_app_func_2();
void Frame_show_debugger_func();
void Frame_close_debugger_func();
void Frame_sync_exec_func2(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
::cw::lib::Value Frame_OnDropFiles_func(std::vector<::cw::lib::String> arg_paths);
void Frame__on_drop_files_update_skin(::cw::lib::String arg_skindirname);
void Frame_OnIconize_func();
void Frame_OnIconize_func_2();
void Frame_set_app_suspended(bool suspended,
    ::cardwirth::platform::AppVisibility::Reason reason = ::cardwirth::platform::AppVisibility::Background);
void Frame_OnPARTYSELECT_func();
void Frame_OnPARTYSELECT_func_2();
void Frame_OnPLAYERSELECT_func();
void Frame_ok_scenarioselect_func(std::shared_ptr<::cw::header::ScenarioHeader> arg_header, std::vector<::cw::lib::String> arg_sel, ::cw::lib::String arg_selpath);
void Frame_check_killlist_func();
void Frame_OnINFOVIEW_func();
void Frame_OnSAVE_func();
void Frame_OnSAVED_MESSAGE_func();
void Frame_OnUSECARD_func(::cw::lib::Value arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets);
void Frame_OnUSECARD_use_card(std::shared_ptr<::cw::character::Character> arg_owner, ::cw::lib::Value arg_targets, std::shared_ptr<::cw::header::CardHeader> arg_header);
void Frame_OnF9_func();
void Frame_OnF9_func_stop();
void Frame_move_dlg_OnIconize(std::shared_ptr<::cw::lib::Window> arg_event);
void Frame_kill_dlg_func(bool arg_lockmenucard, bool arg_redraw);
void Frame_kill_dlg_if_model(::cw::lib::Object* arg_model);
bool Frame_poll_modal_pointer_release();
// スタックにダイアログが残っていない場合だけ、ネイティブの所有者HWNDを再び有効にする。
void restore_dialog_owner(std::uintptr_t arg_native_handle);
void Frame_save_screenshot_func(std::shared_ptr<::cw::frame::Frame> arg_self);
void Frame_change_cardcontrolarea_func(::cw::lib::Integer arg_areaid);
void Frame_change_cardcontrolarea_func_func(::cw::lib::Integer arg_areaid);
void Frame_change_cardcontrolarea_func2();
void Frame_change_cardcontrolarea_func2_func();
::cw::lib::Integer MyApp_FilterEvent_end_flick();
::cw::lib::Value MyApp_FilterEvent_end_flick_lambda_line_1797();
void MyApp_FilterEvent_watch_mousebutton();
void MyApp_FilterEvent_func(bool arg_updatedebug_on_dlg);
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_skincount();
void main();
}
