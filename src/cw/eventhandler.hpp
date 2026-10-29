#pragma once

// cw/eventhandler.pyの移植。元のpygame
// ・wxとの通信に使うcwpyグローバル変数を、 下の注
// 入可能な実行環境で表す。入力と状態遷移の処理を、 SDL
// の画面とヘッドレステストで共用できるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cw::eventhandler {

// ネイティブ入力はキューに積まれるため、配送時の当たり判定にはイベン
// ト自身の座標を使い、 UIスレッドが後から取得した位置は使わない。
std::optional<::cw::lib::Point> dispatch_pointer_position(const ::cw::thread::CWPy* runtime);

// クリックが始めたモーダル入力ループでは、開始時のクリック位置ではな
// く現在のポインター位置を読む。 入れ子で配送するクリックはそれぞれ
// 独自の座標スコープを持ち、ループ終了時に呼び出し元の状態へ戻す。
class LivePointerInputScope final {
    const ::cw::thread::CWPy* previous_runtime_;
    std::optional<::cw::lib::Point> previous_position_;
public:
    LivePointerInputScope();
    ~LivePointerInputScope();
    LivePointerInputScope(const LivePointerInputScope&) = delete;
    LivePointerInputScope& operator=(const LivePointerInputScope&) = delete;
};

// pygameの値は一か所で定義する。プラットフォーム側は、 InputEvent
// をキューへ積む前にSDLのネイティブ値をこれらへ変換できる。
enum InputEventType : ::cw::lib::Integer {
    EVENT_NONE = 0, EVENT_KEYDOWN = 1, EVENT_KEYUP = 2,
    EVENT_MOUSEBUTTONDOWN = 3, EVENT_MOUSEBUTTONUP = 4,
    // ホストのキャプチャ喪失は、ゲームの操作を実行するマウスUPとは区別する。
    EVENT_POINTERCANCEL = 26,
    EVENT_USEREVENT = 24, EVENT_FORCE_USEREVENT = 25
};
enum InputKey : ::cw::lib::Integer {
    KEY_RETURN = 13, KEY_BACKSPACE = 8, KEY_ESCAPE = 27,
    KEY_BACKSLASH = 92, KEY_PRINT = 1014,
    KEY_LEFT = 1000, KEY_RIGHT = 1001, KEY_UP = 1002, KEY_DOWN = 1003,
    KEY_PAGEUP = 1004, KEY_PAGEDOWN = 1005, KEY_HOME = 1006, KEY_END = 1007,
    KEY_LSHIFT = 1010, KEY_RSHIFT = 1011,
    KEY_LCTRL = 1012, KEY_RCTRL = 1013,
    KEY_F1 = 1101, KEY_F2 = 1102, KEY_F3 = 1103, KEY_F4 = 1104,
    KEY_F5 = 1105, KEY_F6 = 1106, KEY_F7 = 1107, KEY_F8 = 1108,
    KEY_F9 = 1109, KEY_D = 100, KEY_P = 112, KEY_C = 99
};

struct InputEvent : ::cw::lib::Object {
    ::cw::lib::Integer type{EVENT_NONE}, key{}, button{};
    ::cw::lib::Point pos{};
    // ネイティブのイベント時計によるミリ秒。複数のSDLイベン
    // トが1フレームに届いても フリックの時間を正しく測れるよ
    // う、ゲームループで測り直さずイベントに時刻を保持する。
    ::cw::lib::Real timestamp{};
    // ネイティブ入力は、採取された時点のモーダル世代に属する。 Python/wx
    // ではメイン画面のクリックをShowModalをまたいで保持しないため、 C++
    // の分離したUI・ゲームキューでも同じ規則になるよう明示的な世代を付ける。
    std::uint64_t modal_epoch{};
    bool modal_epoch_stamped{};
    bool ignore_up{}, flick{}, touch{};
    ::cw::lib::Callback func{};
    ::cw::lib::Arguments args{};
    ::cw::lib::KeywordArguments kwargs{};
};

// ネイティブの入力・描画との境界。既定はSDLだが、
// Windowsなど別の画面実装へ変えても、 下のPython
// 互換の入力状態遷移は変更しない。描画メソッドは
// コールバックへ転送し、 SDL側で実際に処理す
// る。何もしない仮のインターフェースにはしない。
class EventHandlerBackend {
public:
    using EventSink = std::function<void(std::shared_ptr<InputEvent>)>;
    using LazyDraw = std::function<void(const ::cw::lib::Rect&)>;
    virtual ~EventHandlerBackend() = default;
    virtual void set_event_sink(EventSink arg_sink) = 0;
    virtual void set_lazy_draw_callback(LazyDraw arg_callback) = 0;
    virtual void set_remove_layer_callback(std::function<void(::cw::lib::Integer)> arg_callback) = 0;
    virtual void set_expanddrawing_callback(std::function<void(::cw::lib::Real)> arg_callback) = 0;
    virtual void set_kill_message_layers_callback(std::function<void()> arg_callback) = 0;
    virtual void set_quit_callback(std::function<void()> arg_callback) = 0;
    virtual void poll_events() = 0;
    virtual bool native_loop_step() = 0;
    virtual void request_lazy_draw(const ::cw::lib::Rect& arg_rect) = 0;
    virtual void remove_layer(::cw::lib::Integer arg_layer) = 0;
    virtual void apply_expanddrawing(::cw::lib::Real arg_scale) = 0;
    virtual void kill_message_layers() = 0;
};

// Pythonが参照するcwpy・keyevent・statusbar
// ・frame・スプライト群に対応する状態とコール
// バック。 接続を明示し、SDL/wxのサービスが
// 欠けているときに入力を黙って捨てないようにする。
struct EventHandlerRuntimeContext {
    std::deque<std::shared_ptr<::cw::lib::Object>> events{};
    bool has_inputevent{}, is_decompressing{}, showing_dialog{}, processing{};
    bool running_event{}, dealing{}, pointed_tile{}, showing_backlog{}, showing_message{};
    bool wait_showcards{}, autoenter_on_sprite{}, enabled_right_flick{};
    bool wheel_show_log{}, can_forward_message_with_wheel{}, can_skipwait_with_wheel{};
    bool lock_menu_cards{}, fullscreen{}, expanded{}, debug_mode{}, debug_log{};
    bool left_ctrl{}, right_ctrl{}, left_shift{}, right_shift{};
    bool key_return{}, key_left{}, key_right{}, key_up{}, key_down{}, key_pageup{}, key_pagedown{};
    bool mouse_left{}, mouse_right{}, mouse_middle{};
    bool statusctrl_selected{}, selectable_on_event{}, pcards_selectable{}, mcards_selectable{};
    bool statusbar_showbuttons{}, selected_header{}, curtained{}, camp{};
    bool scenario{}, yado{}, battle{}, battle_ready{}, endprocess{}, in_f9{};
    bool lost_scenario{}, instructions{}, show_round_auto_button{}, playing_scenario{};
    bool has_backlog{};
    ::cw::lib::Integer areaid{}, wheel_mode_x{-1}, wheel_mode_y{-1};
    ::cw::lib::Integer status_areaid{};
    ::cw::lib::Integer selection_group{-1}; // -1は対象なし、0・1はクリック可能なグループ、2はプレイヤーカード。
    ::cw::lib::Integer index{-1};
    ::cw::lib::Point mouse_pos{-1, -1};
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> selectable_list{};
    std::shared_ptr<::cw::sprite::base::SelectableSprite> selection{};
    std::shared_ptr<::cw::sprite::base::SelectableSprite> pointed_tile_selection{};
    ::cw::lib::String status{};
    ::cw::lib::String clipboard_text{};
    ::cw::lib::String messagelog_text{};
    ::cw::lib::Integer music_volume{}, scenario_volume{}, system_volume{}, volume_increment{5};
    // Pythonと同様、メッセージログの表示領域と4分の1ページのホイール移動量は 640x480
    // のSDL画面ではなく、SIZE_AREA（632x420）を基準にする。
    ::cw::lib::Integer scrollnum{105}, visible_height{420};
    ::cw::lib::Real event_border_time{};
    ::cw::lib::Integer flick_distance{30}, flick_time_msec{500};
    ::cw::lib::Real expanddrawing{1.0};
    bool flick_started{};
    ::cw::lib::Point flick_start_pos{};
    ::cw::lib::Real flick_start_time{};
    // ネイティブボタンの時刻は取得した状態と別に保持し、 一度の取得で複数のSDL
    // イベントを処理してもUserActionが押下を検出できるようにする。
    std::optional<::cw::lib::Real> current_down_time{};
    std::shared_ptr<::cw::sprite::base::SelectableSprite> flick_selection{};
    bool long_press_started{}, long_press_fired{};
    ::cw::lib::Point long_press_start_pos{};
    ::cw::lib::Real long_press_start_time{}, long_press_fired_time{};
    std::shared_ptr<::cw::sprite::base::SelectableSprite> long_press_selection{};
    std::shared_ptr<EventHandlerBackend> backend{};
    std::function<::cw::lib::Real()> ticks{};
    std::function<void()> tick_clock{};
    std::function<void()> clear_touchmenu{};
    std::function<void()> hide_touchmenu{};
    std::function<void(bool)> deal_cards{};
    std::function<void()> cancel_cardcontrol{};
    std::function<void()> clear_specialarea{};
    std::function<void(::cw::lib::Integer)> change_specialarea{};
    std::function<void(::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::Value>)> change_area{};
    // パーティの解放を要求する。標準の実装はPythonのbackground_event
    // と同様に、 その場で解放せずCWPyのキューへ送る。
    std::function<void()> load_party{};
    std::function<bool(::cw::lib::Integer)> call_package{};
    std::function<void(::cw::lib::String)> play_sound{};
    std::function<void(::cw::lib::String)> modal_action{};
    std::function<void(::cw::lib::Integer)> background_action{};
    // シナリオ・Frame側からBackgroundActionの情報を渡し、
    // 入力の状態遷移処理にwx/pygameのXMLオブジェクトを公開しない。
    ::cw::lib::String background_action_kind{};
    ::cw::lib::Integer background_action_id{};
    std::tuple<::cw::lib::String, ::cw::lib::Value> background_action_transition{
        ::cw::lib::String{}, ::cw::lib::Value{}};
    // UIとゲームを別スレッドにする前に、コールバックを一度だけ設定する。 以後はネ
    // イティブ入力の取得とゲーム側のrun()で、不変のコールバック群を共用する。
    bool backend_callbacks_configured{};
    std::function<bool(::cw::lib::Integer)> area_exists{};
    std::function<void(bool)> capture_screenshot_callback{};
    std::function<void(::cw::lib::String)> copy_to_clipboard{};
    std::function<::cw::lib::String()> get_messagelog{};
    std::function<bool()> clear_volume_bar{};
    std::function<void()> toggle_debug{};
    std::function<void()> toggle_expand{};
    std::function<void()> refresh_statusbar{};
    std::function<void()> update_battle{};
    std::function<void(::cw::lib::Integer)> set_area{};
    std::function<void()> show_help{};
    std::function<void()> show_backlog{};
    std::function<void()> show_debug_log{};
    std::function<void()> show_instructions{};
    std::function<void()> show_f9{};
    std::function<bool()> can_f9_callback{};
    std::function<void()> run_event_engine{};
    std::function<void(const std::shared_ptr<::cw::lib::Object>&)> execute_event{};
    std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()> get_mcards{};
    std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()> get_pcards{};
    std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()> get_etc{};
    std::function<std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>>()> get_etc2{};
};

std::shared_ptr<EventHandlerBackend> create_sdl_eventhandler_backend();
std::shared_ptr<EventHandlerBackend> create_windows_eventhandler_backend();
std::shared_ptr<EventHandlerBackend> create_default_eventhandler_backend();

void set_runtime_context(EventHandlerRuntimeContext* arg_context);
EventHandlerRuntimeContext* runtime_context();
// CWPy::inputでネイティブイベントを取り込む。 inputonly/noinput
// で選別した後の実行は、選択中のハンドラーが行う。
void poll_input_events();
// CWPyの受信箱からゲームスレッドのキューへ移動済みの入力も破棄
// する。 モーダルを閉じる際は、両方のキューをまとめて取り消す。
void cancel_runtime_input_sequence();

class EventHandler : public ::cw::lib::Object {
public:
    EventHandler() = default;
    virtual ~EventHandler() = default;
    virtual void run();
    // 所有するメインスレッドでSDL・ホストのイベントを取得し、 InputEvent
    // をCWPyのスレッド安全なゲームキューへ送る。 この
    // メソッド自身はゲームの操作コールバックを実行しない。
    void native_loop_step();
    void clear_touchmenu();
    bool process_long_press();
    void dispatch_long_press();
    // 認識済みの長押しの左ボタン押下状態を終了する。リピート・フリックは発生させない。
    virtual void cancel_pointer_sequence();
    void discard_cancelled_pointer_events();
    bool check_puressedbutton(std::shared_ptr<::cw::lib::Object> arg_event);
    ::cw::lib::Integer calc_index(::cw::lib::Integer arg_value);
    bool can_input();
    bool can_input_sys();
    bool is_processing();
    virtual bool can_copytext();
    virtual void copy_text();
    virtual void dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool arg_pushing, bool arg_sidechange, bool arg_hidetouchmenu);
    std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> _update_selection(bool arg_is_runningevent);
    virtual void ldown_event();
    virtual void lclick_event();
    virtual void rclick_event(bool arg_flick);
    virtual void escapekey_event();
    void background_event();
    void f1key_event();
    void f2key_event();
    void f3key_event();
    virtual void f4key_event();
    void f5key_event();
    void f6key_event();
    void f7key_event();
    void f8key_event();
    void f9key_event();
    static bool can_f9();
    virtual void returnkey_event(bool arg_pushing = false);
    void printkey_event();
    virtual bool keydown_event(::cw::lib::Integer arg_key);
    bool keyup_event(::cw::lib::Integer arg_key);
    void capture_screenshot();
    bool change_volume(::cw::lib::Integer arg_val);
    virtual void wheel_event(::cw::lib::Integer arg_y);
    void executing_event(std::shared_ptr<::cw::lib::Object> arg_event);
};

class EventHandlerForBacklog : public ::cw::eventhandler::EventHandler {
public:
    EventHandlerForBacklog() = default;
    virtual ~EventHandlerForBacklog() = default;
    std::vector<::cw::lib::Value> m__bottom_noscale{};
    std::shared_ptr<::cw::sprite::message::BacklogCurtain> m__curtain{};
    std::optional<std::shared_ptr<::cw::sprite::message::BacklogCurtain>> m__curtain2{};
    std::vector<::cw::lib::Value> m__height_noscale{};
    bool m__in_scroll{};
    bool m__swipe_active{};
    bool m_input_dispatching{};
    bool m__swipe_dragged{};
    bool m__swipe_touch{};
    bool m__current_pointer_touch{};
    ::cw::lib::Integer m__swipe_start_y_noscale{};
    ::cw::lib::Integer m__swipe_last_y_noscale{};
    ::cw::lib::Value m__lock_menucards{};
    ::cw::lib::Value m__messagelog_type{};
    std::vector<std::optional<::cw::lib::Value>> m__mwins{};
    std::shared_ptr<::cw::sprite::message::BacklogPage> m__page{};
    std::vector<::cw::lib::Value> m__pos_noscale{};
    std::optional<std::shared_ptr<::cw::sprite::message::SelectionBar>> m__sbarbar{};
    std::shared_ptr<::cw::sprite::scrollbar::ScrollBar> m__scrollbar{};
    ::cw::lib::Integer m__scrollnum_noscale{};
    ::cw::lib::Integer m__space_noscale{};
    ::cw::lib::Value m__upscr{};
    ::cw::lib::Value m_backlog{};
    ::cw::lib::Value m_backlog_all{};
    ::cw::lib::Integer m_index{};
    // EventHandlerForBacklogが選択状態を一時的に
    // 所有する。 呼び出し元の一覧を残すのはCWPyがない場合だけとし
    // 、 実行中なら復帰したハンドラーの現在の一覧を正式な状態とする。
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> m__saved_selectable_list{};
    ::cw::lib::Integer m__saved_index{-1};
    std::optional<::cw::lib::Value> m_mwin{};
    std::shared_ptr<::cw::sprite::base::StopTheWorld> m_stw{};
    void initialize(std::vector<::cw::lib::Value> arg_backlog, ::cw::lib::Integer arg_index);
    // Pyのsdata.backlogと同様に、同じ可変の一覧とその所有者を保持する。
    void initialize_shared_backlog(std::shared_ptr<std::vector<::cw::lib::Value>> arg_backlog,
                                   ::cw::lib::Integer arg_index);
    void _update_posdata(bool arg_init);
    void run() override;
    std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> _update_backlog_selection();
    bool _pointer_in_backlog_view() const;
    bool _pointer_on_statusbar() const;
    bool _pointer_on_scrollbar() const;
    ::cw::lib::Integer _pointer_y_noscale() const;
    void _begin_swipe();
    void cancel_pointer_sequence() override;
    void _update_swipe();
    bool _end_swipe();
    void ldown_event() override;
    void lclick_event() override;
    void mclick_event();
    void rclick_event(bool arg_flick) override;
    void escapekey_event() override;
    void returnkey_event(bool arg_pushing) override;
    void dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool arg_pushing, bool arg_sidechange, bool arg_hidetouchmenu) override;
    void wheel_event(::cw::lib::Integer arg_y) override;
    void _wheel_event(::cw::lib::Integer arg_y, bool arg_key);
    void _check_updatesettings();
    bool is_showing();
    void _clear_sprites();
    void exit_backlog(bool arg_playsound);
    // バックログが所有権を手放してから、共有のキーボード・ホバー状態を復元
    // する。 中断したメッセージハンドラーと再作成したSelectionBars
    // を戻した後で CWPyから再度呼べるよう、公開メソッドにする。
    void restore_selection_context();
    bool keydown_event(::cw::lib::Integer arg_key) override;
    bool can_copytext() override;
    void copy_text() override;
    ::cw::lib::Integer _get_maxpage();
    void update_sprites(bool arg_clearcache);
};

class EventHandlerForEffectBooster : public ::cw::eventhandler::EventHandler {
public:
    EventHandlerForEffectBooster() = default;
    virtual ~EventHandlerForEffectBooster() = default;
    bool m_running{};
    void initialize();
    void run() override;
    void lclick_event() override;
    void mclick_event();
    void rclick_event(bool arg_flick) override;
    void wheel_event(::cw::lib::Integer arg_y) override;
    void escapekey_event() override;
    void returnkey_event(bool arg_pushing) override;
    void f4key_event() override;
};

class EventHandlerForMessageWindow : public ::cw::eventhandler::EventHandler {
public:
    EventHandlerForMessageWindow() = default;
    virtual ~EventHandlerForMessageWindow() = default;
    ::cw::lib::Value m_mwin{};
    bool m_message_hidden{};
    ::cw::lib::Value initialize(std::shared_ptr<::cw::sprite::message::MessageWindow> arg_mwin);
    void run() override;
    void mouse_event(::cw::lib::Integer arg_button);
    void ldown_event() override;
    void lclick_event() override;
    bool _has_message();
    void mclick_event();
    void rclick_event(bool arg_flick) override;
    void f4key_event() override;
    void returnkey_event(bool arg_pushing) override;
    void dirkey_event(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool arg_pushing, bool arg_sidechange, bool arg_hidetouchmenu) override;
    bool keydown_event(::cw::lib::Integer arg_key) override;
    bool can_copytext() override;
    void copy_text() override;
    void wheel_event(::cw::lib::Integer arg_y) override;
    void shiftkey_event(bool arg_down, bool arg_redraw);
};

class EventHandlerForUserAction : public ::cw::eventhandler::EventHandler {
public:
    EventHandlerForUserAction() = default;
    virtual ~EventHandlerForUserAction() = default;
    std::optional<::cw::lib::Real> m_event_boarder_time{};
    std::optional<std::shared_ptr<::cw::event::EventEngine>> m_event_engine{};
    std::optional<::cw::lib::Real> m_last_known_left_down_time{};
    std::optional<::cw::lib::Real> m_last_known_left_up_time{};
    std::optional<::cw::lib::Real> m_latest_left_up_status_time{};
    bool m_running{};
    bool m_skip_card_click_event{};
    // PythonのMouseWatcherと同じく、未消費の最新の押下だけを保持する。 操
    // 作フレームごとに一つずつ処理するFIFOでは、遅いフレームで古いクリックが蓄積する。
    std::optional<::cw::lib::Point> m_pending_touch;
    unsigned m_action_buttons{};
    void run() override;
    std::optional<::cw::lib::Point> take_touch();
    void cancel_touch();
    void initialize();
    void set_event_boarder_time(::cw::lib::Integer arg_plus);
    bool detect_left_click_press();
    void run_init();
    bool run_user_action();
};

std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_mcards();
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_pcards();
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_etc();
std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> EventHandler_dirkey_event_get_etc2();
void EventHandler_background_event_func(::cw::lib::Integer arg_resid);
void EventHandler_f1key_event_func();
void EventHandler_f6key_event_func(std::shared_ptr<::cw::debug::logging::DebugLog> arg_debuglog);
void main();
}
