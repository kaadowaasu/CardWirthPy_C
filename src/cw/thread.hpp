#pragma once

// cw/thread.pyに対応する、Engine
// の実行状態とスレッド処理の宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/render/dirty_region.hpp"
#include "cw/lib/exception_future.hpp"

#include <cstddef>
#include <deque>
#include <future>
#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace cw::novel { class Workspace; }

namespace cw::thread {

class CWPy;

// 既存のget_titledicが返すObjectを公開し、スクリーンショット側が元
// の文字列と ファイル名の組を受け取れるようにする。タイトルと時刻の共通規則を保つ。
struct TitleDictionary : ::cw::lib::Object {
    std::map<::cw::lib::String, ::cw::lib::String> values{};
};

struct TitleDictionaryPair : ::cw::lib::Object {
    std::map<::cw::lib::String, ::cw::lib::String> values{};
    std::map<::cw::lib::String, ::cw::lib::String> filename_values{};
};


// Pythonのmain_loop(False)は、C++ではRuntimeUpdateMode::ModalPausedという別名で表す。
enum class RuntimeUpdateMode {
    Running,
    ModalPaused,
    FullyPaused,
    ShuttingDown
};

enum class DialogLifecycle {
    Opening,
    Visible,
    ClosingUi,
    ClosingGame,
    GameCommitted,
    PointerReleased,
    UiRestored,
    Closed
};

// Pythonのwxモーダルループはこれらの段階を同じスレ
// ッドで扱う。 C++ではダイアログごとに記録し、遅れた
// 終了通知で別のセッションを消費・復元しないようにする。
struct ModalSession {
    std::uint64_t id{};
    DialogLifecycle state{DialogLifecycle::Opening};
    bool game_committed{};
    bool pointer_released{};
    bool ui_restored{};
    ::cw::lib::String failure{};
};

// CWPy のゲーム状態と native host の境界。Python 版の wx/pygame 呼出しを
// thread.cpp 内で直接捨てず、SDL と Win32 が同じ実処理契約を共有できるように
// する。引数のモデル更新は CWPy 側で先に確定し、backend は表示・入力・終了を
// 実際の host に反映する。
class ThreadBackend {
public:
    virtual ~ThreadBackend() = default;
    virtual void attach(CWPy& arg_thread) = 0;
    virtual void set_fullscreen(CWPy& arg_thread, bool arg_fullscreen) = 0;
    virtual void set_clientsize(CWPy& arg_thread, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size) = 0;
    virtual void set_title(CWPy& arg_thread, const ::cw::lib::String& arg_title) = 0;
    virtual void set_cursor(CWPy& arg_thread, const ::cw::lib::String& arg_name) = 0;
    virtual void invalidate(CWPy& arg_thread, const ::cw::lib::Rect& arg_rect) = 0;
    virtual void draw(CWPy& arg_thread, const std::shared_ptr<::cw::lib::Surface>& arg_surface,
                      const std::optional<::cw::lib::Rect>& arg_clip) = 0;
    virtual void pump(CWPy& arg_thread) = 0;
    virtual void shutdown(CWPy& arg_thread) = 0;
    // Pythonのwx/pygame側でモーダル通知や
    // 例外記録を行う場面に使う、ホストのサービス。 Windows
    // で実際の処理を行いつつ、SDL・ヘッドレス
    // 側も自分の表示方法を提供できるようにする。
    virtual void notify(CWPy& arg_thread, const ::cw::lib::String& arg_title,
                        const ::cw::lib::String& arg_message, bool arg_error) = 0;
    virtual bool confirm(CWPy& arg_thread, const ::cw::lib::String& arg_title,
                         const ::cw::lib::String& arg_message) = 0;
    virtual void log_error(CWPy& arg_thread, const ::cw::lib::String& arg_message) = 0;
    virtual void refresh_debugger(CWPy& arg_thread) = 0;
    // pygame.display.get_init()はホストの表示のライフサイクル
    // を表す。 ソフトウェア画像を保持しているだけでは、表示環境が有効とはみなさない。
    virtual bool display_initialized(const CWPy&) const { return false; }
};

std::shared_ptr<ThreadBackend> create_sdl_thread_backend();
std::shared_ptr<ThreadBackend> create_windows_thread_backend();
std::shared_ptr<ThreadBackend> create_default_thread_backend();

class CWPy : public ::cw::lib::Thread {
public:
    struct PendingGameCall {
        ::cw::lib::Callback func{};
        ::cw::lib::Arguments args{};
        ::cw::lib::KeywordArguments kwargs{};
        std::shared_ptr<::cw::lib::Promise<::cw::lib::Value>> result{};
        // Pythonはexec_func（USEREVENT）とforce_exec_func
        // （FORCE_USEREVENT）を別の種別で扱う。 ネイ
        // ティブ側が一つのキューを処理する場合も、この区別を保つ。
        bool force{};
    };
    struct RenderSceneEntry {
        ::cw::lib::Integer layer{};
        std::size_t order{};
        std::shared_ptr<::cw::sprite::base::CWPySprite> sprite{};
    };
    struct RenderOverlayEntry {
        std::shared_ptr<::cw::lib::Surface> image{};
        ::cw::lib::Rect rect{};
        bool visible{};
    };
    struct RenderPlan {
        std::vector<RenderSceneEntry> scene{};
        std::vector<RenderOverlayEntry> top{};
        std::vector<RenderOverlayEntry> backlog{};
        std::shared_ptr<::cw::sprite::message::MessageWindow> message{};
        std::vector<std::shared_ptr<::cw::sprite::message::SelectionBar>> message_selections{};
    };
    // Pythonは__init__でライフサイクル状態をすべて初期化す
    // る。 Frame::initializeはinit()より前にCWPy
    // を作るため、外部の別呼び出しに頼らず、ここで同じ初期化を行う。
    CWPy() { initialize(); }
    virtual ~CWPy() = default;
    inline static ::cw::lib::Integer s__yesnoresult{};
    inline static std::shared_ptr<::cw::advlog::AdventurerLogger> s_advlog{};
    inline static ::cw::lib::Integer s_areaid{};
    inline static std::shared_ptr<::cw::lib::Object> s_backloggrp{};
    inline static ::cw::lib::Integer s_card_takenouttemporarily_personal_index{};
    inline static std::shared_ptr<::cw::lib::Object> s_cardgrp{};
    inline static ::cw::lib::String s_cursor{};
    inline static std::shared_ptr<::cw::event::EventInterface> s_event{};
    inline static bool s_file_updates_bg{};
    inline static ::cw::lib::Integer s_force_dealspeed{};
    inline static ::cw::lib::Integer s_index{};
    inline static std::tuple<bool, bool, bool> s_mousein{};
    inline static bool s_mousemotion{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_mousepos{};
    inline static std::shared_ptr<::cw::setting::MsgDict> s_msgs{};
    inline static std::vector<std::shared_ptr<::cw::util::MusicInterface>> s_music{};
    inline static ::cw::lib::Integer s_override_dealspeed{};
    inline static std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> s_pcards{};
    inline static std::shared_ptr<::cw::lib::Object> s_sbargrp{};
    inline static ::cw::lib::Integer s_settingtab{};
    inline static ::cw::lib::String s_skindir{};
    inline static ::cw::lib::Integer s_starttick{};
    inline static std::shared_ptr<::cw::lib::Object> s_topgrp{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_wheelmode_cursorpos{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_wxmousepos{};
    ::cw::lib::ValueMap m__actionevent_input{};
    bool m__after_update_mcardlist{};
    bool m__autospread{};
    bool m__clear_changed{};
    bool m__curtained{};
    bool m__dealing{};
    bool m_effectbooster_click_wait{};
    bool m__elapse_time{};
    bool m__forcegameover{};
    bool m__gameover{};
    bool m__init{};
    // バックログはゲームスレッド内で独自ループを回し、状態
    // 取得の要求を処理しない。 ALLホストのEngine
    // UIスレッドから直接読むため、アトミックにする。
    std::atomic<bool> m__is_showingbacklog{};
    // メッセージログから選んだステータス操作は、 入れ子
    // のバックログ入力ループが完全に戻ってから実行する。
    std::function<void()> m_after_backlog_action{};
    bool m__is_showingfcards{};
    ::cw::render::DirtyRegion m_dirty_region{};
    bool m__lazy_draw{};
    std::optional<::cw::lib::Value> m__log_handler{};
    std::map<::cw::lib::String, std::vector<::cw::lib::Value>> m__mcardtable{};
    bool m__need_disposition{};
    bool m__reloading{};
    std::atomic_bool m__running{false};
    bool m__show_allselectedcards{};
    std::atomic<::cw::lib::Integer> m__showingdlg{0};
    std::atomic<RuntimeUpdateMode> m_runtime_update_mode{RuntimeUpdateMode::Running};
    std::atomic_bool m_presentation_update_requested{false};
    // Pythonはwxモーダルの終了・pygameの選択解除・入力再開を
    // 一つのイベントスレッドで行う。 C++ではUIとゲームの間に明示的な
    // 世代と同期を設け、古い終了通知が 新しいダイアログの所有者を再開し
    // たり、後処理中にステータスのホバーを復活させたりしないようにする。
    std::atomic<std::uint64_t> m_modal_epoch{0};
    std::atomic_bool m_modal_close_pending{false};
    std::atomic_bool m_modal_pointer_release_pending{false};
    std::atomic<std::uint64_t> m_modal_pointer_epoch{0};
    // m__showingdlgはPython互換の参照可能な件数
    // として保つが、開いた画面ごとのIDから算出する。 遅れた終了
    // 通知で新しいダイアログのスタック項目を消費しないようにする。
    mutable std::mutex m_modal_session_mutex{};
    std::vector<std::uint64_t> m_modal_sessions{};
    std::set<std::uint64_t> m_modal_closing_sessions{};
    std::map<std::uint64_t, ModalSession> m_modal_session_states{};
    std::map<std::uint64_t, std::vector<::cw::lib::Callback>> m_modal_after_close{};
    std::optional<::cw::lib::Real> m__stop_animations{};
    std::optional<std::shared_ptr<::cw::thread::StoredParty>> m__stored_partyrecord{};
    ::cw::lib::Value m__yesnoresult{};
    std::shared_ptr<::cw::advlog::AdventurerLogger> m_advlog{};
    std::set<std::shared_ptr<::cw::sprite::base::CWPySprite>> m_animations{};
    ::cw::lib::Integer m_areaid{};
    std::shared_ptr<::cw::sprite::background::BackGround> m_background{};
    ::cw::lib::Value m_backloggrp{};
    std::optional<std::shared_ptr<::cw::battle::BattleEngine>> m_battle{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::set<::cw::lib::String>> m_breakpoint_table{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_card_takenouttemporarily{};
    ::cw::lib::Value m_card_takenouttemporarily_personal_index{};
    std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> m_card_takenouttemporarily_personal_owner{};
    ::cw::lib::Value m_cardgrp{};
    std::optional<std::shared_ptr<::cw::binary::cwscenario::CWScenario>> m_classicdata{};
    ::cw::lib::Value m_clock{};
    // pygameのClock内部の直前の時刻に対応する値。 ア
    // ニメーション・待機のstarttick属性とは独立させる。
    std::optional<::cw::lib::Integer> m_clock_lasttick{};
    ::cw::lib::String m_cursor{};
    std::vector<std::shared_ptr<::cw::sprite::background::Curtain>> m_curtains{};
    std::atomic_bool m_cut_animation{false};
    // キャンプ・テーブルの切り替え中も入力処理は続くため、 現在の場面再
    // 構成が完了するまでは別のステータスバーからの切り替えを拒否する。
    std::atomic_bool m_statusbar_area_switching{false};
    std::atomic_bool m_startup_animation_active{false};
    bool m_debug{};
    std::shared_ptr<::cw::dice::Dice> m_dice{};
    std::shared_ptr<::cw::event::EventInterface> m_event{};
    std::shared_ptr<::cw::eventhandler::EventHandler> m_eventhandler{};
    std::vector<std::shared_ptr<::cw::lib::Object>> m_events{};
    // pygame相当の元のイベントキュー。生成側はここへ送り、 input()が選
    // んだイベントをm_events（Pyのself.events）へ移す。 inputonly
    // では、通常のコールバックを完全なinput()取得までここへ戻す。
    std::vector<std::shared_ptr<::cw::lib::Object>> m_parked_pygame_events{};
    mutable std::recursive_mutex m_events_mutex{};
    // PythonのMouseWatcherはpygameのキューを消去し
    // ても残る。 全環境で同じネイティブの入力元を使い、操作の遷移は独立し
    // て保持する。 存在と内容はm_events_mutexで保護する。
    std::optional<std::deque<std::shared_ptr<::cw::lib::Object>>> m_action_pointer_events;
    // Pythonのpygame.USEREVENTに相当するゲームthread専用queue。
    // UI/worker threadからゲーム状態を直接変更しないため、mutexで所有する。
    std::mutex m_game_exec_mutex{};
    std::deque<PendingGameCall> m_game_exec_queue{};
    std::thread::id m_game_thread_id{};
    ::cw::lib::String m_expand_mode{};
    ::cw::lib::String m_expanding{};
    ::cw::lib::Integer m_expanding_cur{};
    ::cw::lib::Integer m_expanding_max{};
    ::cw::lib::Integer m_expanding_min{};
    std::set<std::shared_ptr<::cw::sprite::base::CWPySprite>> m_file_updates{};
    bool m_file_updates_bg{};
    ::cw::lib::Integer m_force_dealspeed{};
    ::cw::lib::Value m_frame{};
    std::vector<std::shared_ptr<::cw::sprite::background::InuseCardImage>> m_guardcards{};
    // Python cardgrp layer LAYER_TARGET_ARROW の論理所有リスト。
    std::vector<std::shared_ptr<::cw::sprite::background::TargetArrow>> m_targetarrows{};
    // 最上位グループの部品をここで所有し、ダイアログの再描画後に
    // 共通の背景処理が無効な借用スプライトを保持しないようにする。
    std::vector<std::shared_ptr<::cw::sprite::background::ClickableSprite>> m_poschangearrows{};
    std::vector<std::shared_ptr<::cw::sprite::background::NumberOfCards>> m_numberofcards{};
    std::vector<std::shared_ptr<::cw::sprite::background::ClickableSprite>> m_numbercardbuttons{};
    std::vector<std::shared_ptr<::cw::sprite::background::ClickableSprite>> m_personalpocketbuttons{};
    bool m_has_inputevent{};
    // 診断と例外時の巻き戻しのため、更新処理の入れ子の深さを記録する。 Pyと同じ
    // く各呼び出しが選択の遅延更新のリセット・確定を行い、深さを理由に 入れ子のclear_selection
    // やステータス更新を抑止しない。登録内容が変わっても グループ
    // を走査できるよう、代理オブジェクトのスナップショットを使う。
    ::cw::lib::Integer m_update_groups_depth{};
    ::cw::lib::Value m_header{};
    bool m_in_actionevent{};
    ::cw::lib::Integer m_index{-1};
    ::cw::lib::Value m_index1{};
    ::cw::lib::Integer m_index2{};
    std::optional<std::shared_ptr<::cw::eventhandler::EventHandler>> m_interrupt_eventhandler{};
    std::vector<std::shared_ptr<::cw::sprite::background::InuseCardImage>> m_inusecards{};
    bool m_is_debuggerprocessing{};
    bool m_is_decompressing{};
    bool m_is_mcardsselectable{};
    bool m_is_pcardsselectable{};
    bool m_is_processing{};
    bool m_is_showparty{};
    bool m_is_updating_skin{};
    bool m_isrun{};
    std::shared_ptr<::cw::eventrelay::KeyEventRelay> m_keyevent{};
    std::vector<::cw::lib::Integer> m_keyin{};
    std::vector<std::optional<std::shared_ptr<::cw::util::SoundInterface>>> m_lastsound_scenario{};
    std::optional<std::shared_ptr<::cw::util::SoundInterface>> m_lastsound_system{};
    std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> m_lazy_selection{};
    std::vector<std::shared_ptr<::cw::sprite::base::SelectableSprite>> m_list{};
    bool m_lock_menucards{};
    std::vector<::cw::lib::Value> m_mcards{};
    ::cw::lib::ValueSet m_mcards_expandspchars{};
    std::tuple<bool, bool, bool> m_mousein{};
    bool m_mousemotion{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_mousepos{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m_moved_mcards{};
    ::cw::lib::Value m_msgs{};
    ::cw::lib::Value m_music{};
    ::cw::lib::Value m_outer{};
    ::cw::lib::Value m_override_dealspeed{};
    ::cw::lib::Value m_pcard{};
    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> m_pcards{};
    std::optional<std::shared_ptr<::cw::sprite::touchbutton::PointableTile>> m_pointed_tile{};
    std::vector<std::tuple<::cw::lib::Integer, std::optional<std::shared_ptr<::cw::data::CWPyElement>>>> m_pre_areaids{};
    std::vector<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>> m_pre_dialogs{};
    std::vector<std::vector<::cw::lib::Value>> m_pre_mcards{};
    std::vector<std::shared_ptr<::cw::sprite::background::PriceOfCard>> m_pricesprites{};
    std::shared_ptr<::cw::setting::Resource> m_rsrc{};
    ::cw::lib::Value m_sbargrp{};
    ::cw::lib::Value m_scr{};
    ::cw::lib::Value m_scr_draw{};
    ::cw::lib::Value m_scr_fullscreen{};
    ::cw::lib::Value m_scr_pos{};
    ::cw::lib::Real m_scr_scale{};
    ::cw::lib::Value m_scr_size{};
    std::shared_ptr<::cw::setting::ScenarioCompatibilityTable> m_sct{};
    ::cw::lib::Value m_sdata{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_selectedheader{};
    // animate_sprite()は入れ子の入力処理を行うため
    // 、実行環境全体の保護が必要。 カード単位の保護だけでは、異な
    // る2体の敵が同じ選択中の戦闘カードを重ねて確定できてしまう。
    std::atomic_bool m_target_selection_dispatching{false};
    // CardEventがイベントスタックを所有する前にも、ネイティブのアニメーション処
    // 理が動く場合がある。 入れ子の使用処理全体にわたって、通常のカード入力を遮断する。
    unsigned m_card_use_depth{};
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> m_selectedscenario{};
    std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> m_selection{};
    std::shared_ptr<::cw::setting::Setting> m_setting{};
    ::cw::lib::Integer m_settingtab{};
    ::cw::lib::Value m_skindir{};
    ::cw::lib::Value m_skinsounds{};
    ::cw::lib::Value m_sounds{};
    ::cw::lib::Value m_starttick{};
    ::cw::lib::String m_status{};
    std::shared_ptr<::cw::sprite::statusbar::StatusBar> m_statusbar{};
    std::shared_ptr<::cw::setting::SystemCoupons> m_syscoupons{};
    ::cw::lib::String m_tempdir{};
    ::cw::lib::Value m_topgrp{};
    bool m_update_scaling{};
    bool m_wait_showcards{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_wheelmode_cursorpos{};
    std::optional<::cw::lib::Integer> m_winevent_areaid{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_wxmousepos{};
    ::cw::lib::String m_yadodir{};
    // 読み込み失敗時のパスの後処理でも、取得したロックの所有権を正確に保持する。
    ::cw::lib::String m_locked_yadodir{};
    std::optional<std::shared_ptr<::cw::data::YadoData>> m_ydata{};
    // アプリ単位のNovelモードでは、変更しない基準の宿と専用の作業用
    // の宿を分け、 明示的に選んだスロットだけへ保存する。 これらのフィ
    // ールドでゲームスレッドのモーダル要求と共通UIの表示を接続する。
    ::cw::lib::String m_novel_base_yadodir{};
    std::vector<::cw::lib::String> m_novel_slot_choices{};
    std::optional<int> m_novel_slot_result{};
    std::shared_ptr<::cw::novel::Workspace> m_novel_workspace;
    int m_novel_active_slot{1};
    std::shared_ptr<::cw::thread::ThreadBackend> m_backend{};
    void initialize();
    void init(std::shared_ptr<::cw::setting::Setting> arg_setting, std::optional<std::shared_ptr<::cw::frame::Frame>> arg_frame);
    bool to_bool();
    void init_pygame(std::shared_ptr<::cw::setting::Setting> arg_setting);
    void set_fullscreen(bool arg_fullscreen);
    void set_clientsize(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    void _load_breakpoints();
    void _save_breakpoints();
    bool _init_resources();
    void init_sounds();
    void _update_clip();
    void update_skin(::cw::lib::String arg_skindirname, bool arg_changearea, bool arg_restartop, std::optional<::cw::lib::Callback> arg_afterfunc, bool arg_switch_skin, bool arg_switch_yado);
    void update_yadoinitial();
    void update_titlebar();
    ::cw::lib::String create_title();
    ::cw::lib::Value get_titledic(bool arg_with_datetime, bool arg_for_fname);
    std::map<::cw::lib::String, ::cw::lib::String> get_titledic_filename_values(bool arg_with_datetime);
    void update_scale(::cw::lib::Real arg_scale, bool arg_changearea, bool arg_rsrconly, bool arg_udpatedrawsize, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_displaysize, std::optional<::cw::lib::Callback> arg_afterfunc);
    void update_messagestyle();
    void update_vocation120(bool arg_vocation120);
    void update_curtainstyle();
    void set_debug(bool arg_debug);
    void update_infocard();
    void run();
    void _run();
    void _drain_game_exec_queue(bool arg_forceonly = false);
    void process_game_commands(bool arg_forceonly = false);
    void stage_game_commands();
    bool has_pending_game_commands(bool arg_include_force = true);
    void update_simulation();
    void update_presentation();
    void publish_render_snapshot();
    void request_presentation_update();
    RuntimeUpdateMode runtime_update_mode() const;
    void assert_game_thread();
    std::shared_ptr<::cw::eventhandler::EventHandler> get_eventhandler();
    void set_interrupt_eventhandler(std::optional<std::shared_ptr<::cw::eventhandler::EventHandler>> handler);
    void main_loop(bool arg_update);
    void quit();
    void quit2();
    void _quit();
    void tick_clock(::cw::lib::Integer arg_framerate);
    bool wait_frame(::cw::lib::Integer arg_count, bool arg_canskip, std::optional<std::shared_ptr<::cw::sprite::base::StopTheWorld>> arg_stoptheworld, ::cw::lib::Integer arg_framerate);
    bool get_breakflag(bool arg_handle_wheel);
    bool get_animation_breakflag(bool arg_handle_wheel);
    std::optional<std::shared_ptr<::cw::lib::Object>> get_nextevent();
    void clear_inputevents();
    void input(bool arg_eventclear, bool arg_inputonly, bool arg_noinput);
    void update_allselectedcards();
    bool _in_partyarea(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_mousepos);
    bool update_mousepos();
    // キュー内のクライアント座標を、現在の入力と同じ変換で処理する。
    ::cw::lib::Point client_to_screen_point(::cw::lib::Point point) const;
    void update(bool arg_update_videos, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_videos();
    std::optional<std::shared_ptr<::cw::lib::Object>> update_statusimgs(bool arg_is_runningevent, std::optional<std::shared_ptr<::cw::lib::Object>> arg_clip);
    void update_groups(std::vector<std::shared_ptr<::cw::lib::Object>> arg_groups);
    void return_takenoutcard(bool arg_checkevent);
    void fix_updated_file(bool arg_force);
    void proc_animation();
    void stop_animation(std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite);
    RenderPlan build_render_plan();
    std::optional<::cw::lib::Rect> compose_to(
        std::shared_ptr<::cw::lib::Surface> arg_scr, bool arg_draw_desc,
        std::optional<::cw::lib::Rect> arg_requested_clip,
        const RenderPlan& arg_plan);
    void lazy_draw();
    // アニメーション・エリア操作を続ける前に、UIへの引き渡しを完了する。
    void flush_presentation();
    void add_lazydraw(std::shared_ptr<::cw::lib::Object> arg_clip);
    void add_lazydraw_full();
    void add_lazydraw_rect(::cw::lib::Rect arg_clip);
    void stop_the_world_with_iconized();
    void draw(bool arg_mainloop, std::optional<std::shared_ptr<::cw::lib::Object>> arg_clip);
    void init_fullscreenparams();
    void update_fullscreenbackground();
    std::shared_ptr<::cw::lib::Object> _get_fullclip();
    void change_cursor(::cw::lib::String arg_name, bool arg_force);
    ::cw::lib::Integer call_dlg(::cw::lib::String arg_name, const ::cw::lib::KeywordArguments& arg_kwargs);
    void call_modaldlg(::cw::lib::String arg_name, const ::cw::lib::KeywordArguments& arg_kwargs);
    void call_predlg();
    void kill_showingdlg(std::uint64_t arg_session);
    ::cw::lib::Integer _kill_showingdlg(std::uint64_t arg_session);
    std::uint64_t add_showingdlg();
    void defer_until_modal_closed(std::uint64_t arg_session,
                                  ::cw::lib::Callback arg_callback);
    bool begin_modal_close(std::uint64_t arg_session);
    bool finish_modal_close(std::uint64_t arg_epoch);
    bool finish_modal_pointer_release(std::uint64_t arg_epoch);
    bool finish_modal_ui_restore(std::uint64_t arg_session);
    bool is_modal_session_closed(std::uint64_t arg_session) const;
    void forget_modal_session(std::uint64_t arg_session);
    void record_modal_close_failure(std::uint64_t arg_session,
                                    ::cw::lib::String arg_failure);
    bool is_modal_generation_current(std::uint64_t arg_epoch) const;
    bool is_modal_input_blocked() const;
    void cancel_modal_input_sequence();
    void exec_func(::cw::lib::Callback arg_func);
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
    void force_exec_func(::cw::lib::Callback arg_func);
    void force_exec_func_2(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1);
    void force_exec_func_3(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2);
    void force_exec_func_4(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3);
    void force_exec_func_5(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4);
    void force_exec_func_6(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5);
    void force_exec_func_7(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6);
    void force_exec_func_8(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7);
    void force_exec_func_9(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8);
    void force_exec_func_10(::cw::lib::Callback arg_func, ::cw::lib::Value arg_arg1, ::cw::lib::Value arg_arg2, ::cw::lib::Value arg_arg3, ::cw::lib::Value arg_arg4, ::cw::lib::Value arg_arg5, ::cw::lib::Value arg_arg6, ::cw::lib::Value arg_arg7, ::cw::lib::Value arg_arg8, ::cw::lib::Value arg_arg9);
    void force_exec_func_11(::cw::lib::Callback arg_func, const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
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
    void set_expanded(bool arg_flag, ::cw::lib::String arg_expandmode, bool arg_force, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_displaysize);
    ::cw::lib::Value show_message(std::shared_ptr<::cw::sprite::message::MessageWindow> arg_mwin);
    bool has_backlog();
    void show_backlog(::cw::lib::Integer arg_n);
    void set_backlogmax(::cw::lib::Integer arg_backlogmax);
    void set_titlebar(::cw::lib::String arg_s);
    ::cw::lib::Integer get_yesnoresult();
    void set_status(::cw::lib::String arg_name);
    void startup(bool arg_loadyado);
    [[nodiscard]] bool is_novel_mode() const;
    [[nodiscard]] std::optional<::cw::lib::String> select_novel_save(
        ::cw::lib::String arg_base_yadodir);
    [[nodiscard]] bool save_novel_slot(int slot, bool overwrite, ::cw::lib::String& error);
    // novel.yamlが指定する宿を解決し、保存スロットの選択画面を
    // 開く。 Novelモードではタイトル画面のStart操作にも使う。
    bool load_novel_default_yado();
    void set_title(bool arg_init, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype);
    void _init_attrs();
    void set_yado();
    void start_scenario();
    void set_scenario(std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> arg_header, std::optional<std::vector<::cw::lib::String>> arg_lastscenario, ::cw::lib::String arg_lastscenariopath, bool arg_resume, bool arg_manualstart);
    void _set_scenario_impl(std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> arg_header, std::vector<::cw::lib::String> arg_lastscenario, ::cw::lib::String arg_lastscenariopath, bool arg_resume, bool arg_manualstart);
    void set_battle();
    void set_gameover();
    void set_gameoverstatus(bool arg_gameover, bool arg_force = true); // Pyのforce=Trueに対応する。
    void stop_allsounds(bool arg_skinfileonly);
    void f9(bool arg_load_failure, bool arg_loadyado);
    void _f9impl(bool arg_startotherscenario, bool arg_loadyado);
    void reload_yado();
    bool load_yado(::cw::lib::String arg_yadodir, bool arg_createmutex);
    bool _load_yado(::cw::lib::String arg_yadodir, bool arg_createmutex);
    bool _load_yado2(::cw::lib::String arg_yadodir);
    void deal_cards(bool arg_quickdeal, bool arg_updatelist, ::cw::lib::String arg_flag, bool arg_startbattle, bool arg_silent, bool arg_simultaneous = false);
    void hide_cards(bool arg_hideall, bool arg_hideparty, bool arg_quickhide, bool arg_updatelist, ::cw::lib::String arg_flag, bool arg_silent, bool arg_simultaneous = false);
    void _fix_updateimage(::cw::lib::Value arg_mcard);
    void vanished_card(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_mcard);
    void update_mcardlist();
    void _update_mcardlist();
    std::vector<::cw::lib::Value> update_pcimage(::cw::lib::Integer arg_pcnumber, bool arg_deal);
    void show_party();
    void _show_party();
    void hide_party();
    void set_pcards(bool arg_newparty);
    void set_sprites(bool arg_dealanime, bool arg_bginhrt, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_doanime, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, bool arg_nocheckvisible, bool arg_silent, bool arg_newparty);
    void add_fcardsprites(::cw::lib::String arg_status, std::optional<::cw::lib::Integer> arg_alpha);
    void clear_fcardsprites();
    void update_mcardnames();
    void set_autospread(std::vector<::cw::lib::Value> arg_mcards, ::cw::lib::Integer arg_maxcol, bool arg_campwithfriend, bool arg_anime);
    std::vector<::cw::lib::Value> set_mcards(std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::data::CWPyElement>>> arg_stype_and_elements, bool arg_dealanime, bool arg_addgroup, bool arg_setautospread, std::optional<bool> arg_splayer);
    void disposition_pcards();
    void change_area(::cw::lib::Integer arg_areaid, bool arg_eventstarting, bool arg_bginhrt, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_quickdeal, bool arg_specialarea, bool arg_startbattle, bool arg_doanime, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, bool arg_nocheckvisible, bool arg_clear_curtain, bool arg_force_updatebg, bool arg_silent, bool arg_newparty);
    void change_battlearea(::cw::lib::Integer arg_areaid);
    void clear_battlearea(bool arg_areachange, ::cw::lib::Integer arg_eventkeynum, bool arg_startnextbattle, bool arg_is_battlestarting);
    void change_specialarea(::cw::lib::Integer arg_areaid, bool arg_silent);
    void set_testaptitude(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
    void update_tradecards();
    void remove_pricesprites();
    void clear_specialarea(bool arg_redraw, bool arg_silent);
    void clean_specials(bool arg_redraw, bool arg_silent);
    void check_level(bool arg_fromscenario);
    void create_poschangearrow();
    void replace_pcardorder(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void show_numberofcards(::cw::lib::String arg_ctype);
    void clear_numberofcards();
    void clear_selection();
    void change_selection(std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> arg_sprite, std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> arg_forceredraw);
    void set_inusecardimg(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::String arg_status, bool arg_center, ::cw::lib::Integer arg_alpha, bool arg_fore);
    void clear_inusecardimg(std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_user);
    void clear_inusecardimgfromheader(std::shared_ptr<::cw::header::CardHeader> arg_header);
    void set_guardcardimg(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header);
    void clear_guardcardimg();
    void set_targetarrow(std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets);
    void clear_targetarrow();
    void update_selectablelist();
    void set_curtain(::cw::lib::String arg_target, bool arg_curtain_all, bool arg_redraw);
    void clear_curtain(bool arg_redraw);
    void cancel_cardcontrol();
    bool is_lockmenucards(std::optional<std::shared_ptr<::cw::sprite::base::SelectableSprite>> arg_sprite);
    void elapse_time(bool arg_playeronly, bool arg_fromevent);
    void interrupt_adventure();
    void load_party(std::optional<std::shared_ptr<::cw::header::PartyHeader>> arg_header, bool arg_chgarea, bool arg_newparty, bool arg_loadsprites);
    void dissolve_party(std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_pcard, bool arg_cleararea);
    // 動的なスキンコマンド引数の接続用。型付きの呼び出しも同じ本体を使う。
    void dissolve_party_value(::cw::lib::Value arg_pcard, bool arg_cleararea);
    std::shared_ptr<::cw::thread::StoredParty> get_partyrecord();
    void _store_partyrecord();
    void _save_partyrecord();
    void save_partyrecord();
    void set_mastervolume(::cw::lib::Integer arg_volume);
    void play_sound(::cw::lib::String arg_name, bool arg_from_scenario, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade, bool arg_material_override);
    bool _play_sound_with(::cw::lib::String arg_path, bool arg_from_scenario, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade);
    void play_sound_with(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade);
    bool has_sound(::cw::lib::String arg_path);
    void trade(::cw::lib::String arg_targettype, std::optional<::cw::lib::Value> arg_target, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, bool arg_from_event, std::shared_ptr<::cw::lib::Dialog> arg_parentdialog, ::cw::lib::Integer arg_toindex, ::cw::lib::Integer arg_insertorder, bool arg_sort, bool arg_sound, std::optional<std::shared_ptr<::cw::data::Party>> arg_party, bool arg_from_getcontent, bool arg_call_predlg, bool arg_clearinusecard, bool arg_update_image, ::cw::lib::Integer arg_insertorder_mode);
    void remove_xml(::cw::lib::Value arg_target);
    void remove_materials(::cw::lib::Value arg_data, bool force_yado = false,
                          ::cw::lib::String removed_card_path = {});
    void copy_materials(::cw::lib::Value arg_data, ::cw::lib::String arg_dstdir, bool arg_from_scenario, ::cw::lib::String arg_scedir, std::optional<::cw::lib::String> arg_yadodir, std::optional<::cw::lib::String> arg_toyado, bool arg_adventurer, std::optional<std::reference_wrapper<std::map<::cw::lib::String, ::cw::lib::String>>> arg_imgpaths, bool arg_importimage, bool arg_can_loaded_scaledimage);
    void _copy_material(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::String arg_dstdir, bool arg_from_scenario, ::cw::lib::String arg_scedir, std::map<::cw::lib::String, ::cw::lib::String>& arg_imgpaths, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_e, ::cw::lib::String arg_materialpath, ::cw::lib::Callback arg_set_material, std::optional<::cw::lib::String> arg_yadodir, std::optional<::cw::lib::String> arg_toyado, bool arg_can_loaded_scaledimage);
    bool is_running();
    bool is_runningstatus();
    bool is_playingscenario();
    bool is_runningevent();
    bool is_reloading();
    bool is_statusbarmask();
    bool is_showingdlg();
    bool is_expanded();
    bool is_curtained();
    bool is_dealing();
    bool is_autospread();
    bool is_gameover();
    bool is_forcegameover();
    bool is_showingmessage();
    std::optional<std::shared_ptr<::cw::debug::debugger::Debugger>> is_showingdebugger();
    bool is_showingbacklog();
    // 対象選択の幕がこのカードの側を覆っている間に真となる。敵対象カードでのPC
    // 、 味方対象カードでの戦闘中のメニュー・敵カードなどが該当する。 SelectableSprite::update_selection()
    // を参照。
    [[nodiscard]] bool is_curtain_blocked(const ::cw::sprite::base::SelectableSprite* arg_sprite) const;
    bool is_debugmode();
    bool is_battlestatus();
    std::optional<std::shared_ptr<::cw::sprite::background::InuseCardImage>> get_inusecardimg();
    std::optional<std::shared_ptr<::cw::sprite::background::InuseCardImage>> get_guardcardimg();
    std::optional<std::shared_ptr<::cw::sprite::message::MessageWindow>> get_messagewindow();
    std::vector<::cw::lib::Value> get_mcards(::cw::lib::Value arg_mode, ::cw::lib::String arg_flag);
    std::vector<::cw::lib::Value> get_mcards_2(::cw::lib::Value arg_mode, ::cw::lib::String arg_flag);
    ::cw::lib::Value get_mcards_3(::cw::lib::Value arg_mode, ::cw::lib::String arg_flag);
    std::vector<std::shared_ptr<::cw::sprite::card::EnemyCard>> get_ecards(::cw::lib::Value arg_mode);
    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> get_pcards(::cw::lib::Value arg_mode);
    std::vector<std::shared_ptr<::cw::sprite::card::FriendCard>> get_fcards(::cw::lib::Value arg_mode);
    std::optional<std::shared_ptr<::cw::sprite::card::MenuCard>> find_backpackcard();
    std::optional<std::shared_ptr<::cw::sprite::card::MenuCard>> find_storehousecard();
    ::cw::lib::Value clear_actionevent_input();
};

class CWPy_AddPersonalPocket : public ::cw::lib::Object {
public:
    CWPy_AddPersonalPocket() = default;
    virtual ~CWPy_AddPersonalPocket() = default;
};

class CWPy_Replace : public ::cw::lib::Object {
public:
    CWPy_Replace() = default;
    virtual ~CWPy_Replace() = default;
};

class CWPy_ReplaceCards : public ::cw::lib::Object {
public:
    CWPy_ReplaceCards() = default;
    virtual ~CWPy_ReplaceCards() = default;
};

class CWPy_Running : public ::cw::lib::Object {
public:
    CWPy_Running() = default;
    virtual ~CWPy_Running() = default;
};

class CWPyRunningError : public ::cw::lib::Object {
public:
    CWPyRunningError() = default;
    virtual ~CWPyRunningError() = default;
};

class DummyVersionInfo : public ::cw::lib::Object {
public:
    DummyVersionInfo() = default;
    virtual ~DummyVersionInfo() = default;
    ::cw::lib::String m_build_datetime{};
    void initialize();
    bool to_bool();
};

class StoredParty : public ::cw::lib::Object {
public:
    StoredParty() = default;
    virtual ~StoredParty() = default;
    ::cw::lib::Value m_backpack{};
    ::cw::lib::String m_fpath{};
    ::cw::lib::Value m_is_suspendlevelup{};
    ::cw::lib::Value m_members{};
    ::cw::lib::Value m_money{};
    ::cw::lib::Value m_name{};
    void initialize(std::shared_ptr<::cw::data::Party> arg_party);
};

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
extern ::cw::lib::Value g__dlg_mutex;
extern ::cw::lib::Value g__mutex_postevent;
extern std::shared_ptr<::cw::lib::Mutex> g_init_rsrc;
extern std::shared_ptr<::cw::thread::DummyVersionInfo> g_versioninfo;

void post_pygameevent(std::shared_ptr<::cw::lib::Object> arg_event);
void main();
}

