#pragma once

// cardwirthpy-reboot/cw/debug/debugger.py
// に対応するデバッガーモデルと共通 Presentation の宣言。す
// べてのホストが同じモデルを表示し、OS ごとに UI 経路を分けない。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/documents.hpp"
#include "cw/dialog/presentation_scrollbar.hpp"
#include "cw/dialog/filter_input.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/dialog/incrementalsearch.hpp"
#include "cw/fwd.hpp"

#include <atomic>
#include <array>
#include <set>

namespace cw::debug::debugger {

inline constexpr int DebuggerPresentationMenuFile = -1001;
inline constexpr int DebuggerPresentationMenuEdit = -1002;
inline constexpr int DebuggerPresentationMenuScenario = -1003;
inline constexpr int DebuggerPresentationMenuRun = -1004;
inline constexpr int DebuggerPresentationWaitTime = -1005;
inline constexpr int DebuggerPresentationDismissMenu = -1006;
inline constexpr int DebuggerPresentationWaitUp = -1007;
inline constexpr int DebuggerPresentationWaitDown = -1008;
inline constexpr int DebuggerPresentationWaitMenuBase = -1400;
inline constexpr int DebuggerPresentationWaitMenuDismiss = -1410;
inline constexpr int DebuggerPresentationVariableMenu = 4;
inline constexpr int DebuggerPresentationBreakpointBase = 40000;
inline constexpr int DebuggerPresentationEventHorizontal = -1101;
inline constexpr int DebuggerPresentationEventVertical = -1102;

// 各 OS の両デバッガーペインで共有する仮想レポートリストの処理。ネイティ
// ブホストも同じコントロールに入力を渡し、OS 別のリスト配置は持たない。
class DebuggerListViewport {
public:
    using Integer=::cw::lib::Integer;
    using Text=std::function<::cw::lib::String(Integer,Integer)>;
    using Icon=std::function<std::shared_ptr<::cw::lib::Surface>(Integer)>;
    DebuggerListViewport(int rows,int horizontal,int vertical,int resize=-1)
        : row_base(rows), horizontal_id(horizontal), vertical_id(vertical), resize_base(resize) {}
    ::cw::dialog::PresentationScrollAxis horizontal, vertical;
    Integer visible_rows{1}, row_height{18}, header_height{}, client_width{}, client_height{};
    Integer column_count{}, row_count{}, bar_thickness{};
    int row_base, horizontal_id, vertical_id, resize_base;
    [[nodiscard]] bool owns(int id) const;
    void cancel() noexcept;
    [[nodiscard]] bool repeat_due() const noexcept;
    [[nodiscard]] ::cw::dialog::DialogPresentation render(::cw::lib::Size size,
        const std::vector<::cw::lib::String>& columns,const std::vector<Integer>& widths,
        Integer count,Integer selected,bool focused,Integer& first_row,
        const Text& text,const Icon& icon,bool auto_width=false);
    void pointer(int id,float x,float y,bool pressed,std::vector<Integer>& widths,Integer& first_row);
    bool autosize(int id,std::vector<Integer>& widths,
        const std::vector<::cw::lib::String>& columns,Integer count,const Text& text);
    bool navigate(::cw::dialog::DialogPresentationKey key,Integer& selected,Integer& first_row) const;
private:
    int resizing{-1};
    float resize_x{};
    Integer resize_width{};
};

// ワーカーが生成する表示値。イベント・コンテントのポインターは移動先の識別にだけ
// 使い、UI はスナップショットの適用中にその変更可能なフィールドを読まない。
struct StackTraceRow {
    using Entry=std::tuple<std::shared_ptr<::cw::event::Event>,
                          std::optional<std::shared_ptr<::cw::data::CWPyElement>>>;
    Entry entry;
    ::cw::lib::String text, icon_name;
    ::cw::lib::Integer icon{4};
    std::shared_ptr<::cw::lib::Surface> bitmap;
};
struct StackTraceSnapshot {
    // 元の空スロットも保持し、フレームを省略しても replace/pop のインデックスをずらさない。
    std::vector<std::optional<StackTraceRow>> frames;
    std::optional<StackTraceRow> active;
};

// UI に依存しないデバッガーのモデル。
class Debugger : public ::cw::lib::Object,
                 public ::cw::dialog::DialogPresentationProvider {
public:
    Debugger() = default;
    virtual ~Debugger() = default;
    // モードレスウィンドウと、そのモーダルな親の連なりを識別する共通オブジェクト。
    std::shared_ptr<::cw::lib::Frame> m_window_identity{std::make_shared<::cw::lib::Frame>()};
    bool m_window_identity_initialized{};
    inline static std::shared_ptr<::cw::debug::debugger::EventView> s_view_tree{};
    inline static std::shared_ptr<::cw::debug::debugger::VariableListCtrl> s_view_var{};
    ::cw::lib::String m__currentfpath{};
    ::cw::lib::Value m__mgr{};
    ::cw::lib::Value m__mi_area_index{};
    bool m__recovering{};
    bool m__showhiddencards{};
    ::cw::lib::Value m_ctrl_d_keyid{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_f1keyid{};
    ::cw::lib::Value m_f2keyid{};
    ::cw::lib::Value m_f3keyid{};
    ::cw::lib::Value m_f4keyid{};
    ::cw::lib::Value m_f5keyid{};
    ::cw::lib::Value m_f6keyid{};
    ::cw::lib::Value m_f7keyid{};
    ::cw::lib::Value m_f8keyid{};
    ::cw::lib::Value m_f9keyid{};
    ::cw::lib::Value m_mi_area{};
    ::cw::lib::Value m_mi_battle{};
    ::cw::lib::Value m_mi_bgm{};
    ::cw::lib::Value m_mi_break{};
    ::cw::lib::Value m_mi_breakpoint{};
    ::cw::lib::Value m_mi_card{};
    ::cw::lib::Value m_mi_clear_breakpoint{};
    ::cw::lib::Value m_mi_comp{};
    ::cw::lib::Value m_mi_coupon{};
    ::cw::lib::Value m_mi_editor{};
    ::cw::lib::Value m_mi_friend{};
    ::cw::lib::Value m_mi_gossip{};
    ::cw::lib::Value m_mi_hideparty{};
    ::cw::lib::Value m_mi_info{};
    ::cw::lib::Value m_mi_initvars{};
    ::cw::lib::Value m_mi_load{};
    ::cw::lib::Value m_mi_loadyado{};
    ::cw::lib::Value m_mi_member{};
    ::cw::lib::Value m_mi_money{};
    ::cw::lib::Value m_mi_pack{};
    ::cw::lib::Value m_mi_party_env{};
    ::cw::lib::Value m_mi_pause{};
    ::cw::lib::Value m_mi_quit_debugmode{};
    ::cw::lib::Value m_mi_recovery{};
    ::cw::lib::Value m_mi_redisplay{};
    ::cw::lib::Value m_mi_round{};
    ::cw::lib::Value m_mi_save{};
    ::cw::lib::Value m_mi_savedjpdcimage{};
    ::cw::lib::Value m_mi_savedvariables{};
    ::cw::lib::Value m_mi_select{};
    ::cw::lib::Value m_mi_selectedcard{};
    ::cw::lib::Value m_mi_showparty{};
    ::cw::lib::Value m_mi_showstacktrace{};
    ::cw::lib::Value m_mi_startevent{};
    ::cw::lib::Value m_mi_status{};
    ::cw::lib::Value m_mi_stepin{};
    ::cw::lib::Value m_mi_stepover{};
    ::cw::lib::Value m_mi_stepreturn{};
    ::cw::lib::Value m_mi_stop{};
    ::cw::lib::Value m_mi_update{};
    ::cw::lib::Value m_sc_waittime{};
    ::cw::lib::Value m_st_area{};
    ::cw::lib::Value m_st_select{};
    ::cw::lib::Value m_st_selectedcard{};
    ::cw::lib::Value m_statusbar{};
    ::cw::lib::Value m_tb1{};
    ::cw::lib::Value m_tb2{};
    ::cw::lib::Value m_tb_area{};
    ::cw::lib::Value m_tb_event{};
    ::cw::lib::Value m_tb_select{};
    ::cw::lib::Value m_tb_selectedcard{};
    ::cw::lib::Value m_tl_area{};
    ::cw::lib::Value m_tl_battle{};
    ::cw::lib::Value m_tl_bgm{};
    ::cw::lib::Value m_tl_break{};
    ::cw::lib::Value m_tl_breakpoint{};
    ::cw::lib::Value m_tl_card{};
    ::cw::lib::Value m_tl_clear_breakpoint{};
    ::cw::lib::Value m_tl_comp{};
    ::cw::lib::Value m_tl_coupon{};
    ::cw::lib::Value m_tl_editor{};
    ::cw::lib::Value m_tl_friend{};
    ::cw::lib::Value m_tl_gossip{};
    ::cw::lib::Value m_tl_hideparty{};
    ::cw::lib::Value m_tl_info{};
    ::cw::lib::Value m_tl_initvars{};
    ::cw::lib::Value m_tl_load{};
    ::cw::lib::Value m_tl_loadyado{};
    ::cw::lib::Value m_tl_member{};
    ::cw::lib::Value m_tl_money{};
    ::cw::lib::Value m_tl_pack{};
    ::cw::lib::Value m_tl_party_env{};
    ::cw::lib::Value m_tl_pause{};
    ::cw::lib::Value m_tl_recovery{};
    ::cw::lib::Value m_tl_redisplay{};
    ::cw::lib::Value m_tl_round{};
    ::cw::lib::Value m_tl_save{};
    ::cw::lib::Value m_tl_savedjpdcimage{};
    ::cw::lib::Value m_tl_savedvariables{};
    ::cw::lib::Value m_tl_select{};
    ::cw::lib::Value m_tl_selectedcard{};
    ::cw::lib::Value m_tl_showparty{};
    ::cw::lib::Value m_tl_showstacktrace{};
    ::cw::lib::Value m_tl_startevent{};
    ::cw::lib::Value m_tl_status{};
    ::cw::lib::Value m_tl_stepin{};
    ::cw::lib::Value m_tl_stepover{};
    ::cw::lib::Value m_tl_stepreturn{};
    ::cw::lib::Value m_tl_stop{};
    ::cw::lib::Value m_tl_update{};
    std::optional<std::shared_ptr<::cw::debug::debugger::StackTraceView>> m_view_stacktrace{};
    std::shared_ptr<::cw::debug::debugger::EventView> m_view_tree{};
    std::shared_ptr<::cw::debug::debugger::VariableListCtrl> m_view_var{};
    // wx/SDL の表示が利用できなくても保持する、UI に依存しないデバッガー
    // 状態。Python のフレームのイベント・キャッシュ状態に対応する。Debugger
    // 自体は enable_shared_from_this ではないため、
    // 所有権のない別名参照を保持し、子ビューが親への弱参照を持てるようにする。
    std::shared_ptr<::cw::debug::debugger::Debugger> m_self{};
    // Frame も enable_shared_from_this ではない。Frame::show_debugger
    // は何も削除しない削除子を持つ別名参照を渡すため、ここでそれを保持する。保
    // 持しないと initialize() から戻ると同時に m_parent
    // が失効し、Engine から UI へのデバッガー通知がすべて失われる。
    std::shared_ptr<::cw::frame::Frame> m_parent_alias{};
    std::weak_ptr<::cw::frame::Frame> m_parent{};
    bool m_initialized{};
    bool m_visible{};
    bool m_bound{};
    bool m_area_battletool{};
    bool m_show_stacktrace{};
    bool m_pause_checked{};
    ::cw::lib::Integer m_waittime{};
    // UI は共通の一系統とする。Python
    // のツールバーの SpinCtrl は、共
    // 通の編集・メニュー・リピート処理を使う。
    ::cw::dialog::textedit::FilterInput m_wait_input;
    ::cw::lib::Integer m_wait_synced{-1};
    ::cw::lib::Rect m_wait_up_rect{},m_wait_down_rect{};
    ::cw::dialog::ButtonRepeatTimer m_wait_repeat_timer;
    int m_wait_repeat{};
    bool m_wait_repeat_inside{};
    void sync_wait_input();
    void commit_wait_input();
    void leave_wait_input();
    void step_wait_input(int delta);
    void retire_wait_input();
    void validate_wait_edit(const ::cw::dialog::textedit::Buffer& before);
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point wait_input_time() const noexcept;
    ::cw::lib::Integer m_areaid{};
    ::cw::lib::String m_area_label{};
    ::cw::lib::String m_selectedmember_label{};
    ::cw::lib::String m_selectedcard_label{};
    ::cw::lib::String m_selectedcard_type{};
    ::cw::lib::String m_bgm_path{};
    ::cw::lib::String m_last_error{};
    ::cw::lib::String m_recording_path{};
    std::shared_ptr<void> m_recording_access{};
    std::shared_ptr<int> m_document_token{};
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket{};
    ::cw::lib::String m_editor_path{};
    std::vector<::cw::lib::Integer> m_forwarded_keys{};
    // 世代番号が奇数なら開いているスタックペインを購読し、偶数なら通知を無効にする。
    std::shared_ptr<std::atomic<std::uint64_t>> m_stack_epoch{std::make_shared<std::atomic<std::uint64_t>>(0)};
    std::map<::cw::lib::Integer, bool> m_tool_enabled{};
    std::vector<::cw::lib::String> m_operation_log{};
    std::atomic_bool m_presentation_dirty{true};
    ::cw::lib::Integer m_presentation_variable_scroll{};
    ::cw::lib::Integer m_presentation_stack_scroll{};
    int m_presentation_menu{-1};
    int m_presentation_menu_selection{-1};
    ::cw::lib::Rect m_variable_rect{};
    ::cw::lib::Point m_context_menu_anchor{};
    int m_menu_hit_owner{-1};
    std::vector<std::pair<int, ::cw::dialog::DialogPresentationControl>> m_menu_hits{};
    std::vector<::cw::dialog::DialogPresentationControl> m_menu_bar_hits;
    ::cw::dialog::IncrementalSearch m_list_search;
    int m_list_search_focus{-1};
    bool m_list_has_host_focus{true};
    void open_variable_menu(float x, float y);
    std::optional<::cw::dialog::DialogPresentationKey> m_menu_release_key{};
    // 1 は状態変数一覧、2 はイベントツリー、3 はスタック、4 は待機時間の編集欄。モデル
    // が操作対象のフォーカスを管理し、ホストはどの公開コントロールを選んだかだけを報告する。
    int m_presentation_focus{};
    // ALL仕様: 状態変数とイベントツリーは同時表示せず、同じ領域を切り替える。
    bool m_all_variables_view{};
    void initialize(std::shared_ptr<::cw::frame::Frame> arg_parent);
    void refresh_all();
    void _bind();
    void OnF1KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF2KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF3KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF4KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF5KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF6KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF7KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF8KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnF9KeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnWaitTime(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBreakTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpdateTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRedisplayTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnGossipTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSavedJPDCImageTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSavedVariablesTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMoneyTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCardTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPartyEnvTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRoundTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEditorTool(std::shared_ptr<::cw::lib::Window> arg_event);
    static void exec_editor(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_content);
    void OnShowStackTraceTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void append_stackinfo_cwpy(::cw::lib::Value arg_item);
    void pop_stackinfo_cwpy();
    void replace_stackinfo_cwpy(::cw::lib::Integer arg_index, ::cw::lib::Value arg_item);
    void replace_stackinfo_at_cwpy(::cw::lib::Integer index, ::cw::lib::Value item);
    void clear_stackinfo_cwpy();
    void refresh_stackinfo();
    bool queue_stack_refresh(std::function<StackTraceSnapshot()> collect,
        std::function<void(::cw::lib::Callback)> enqueue = {});
    void OnQuitDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSaveTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLoadTool(std::shared_ptr<::cw::lib::Window> arg_event);
    bool request_recording_document(bool save);
    void finish_recording_document(const std::shared_ptr<int>& token, bool save,
                                   ::cw::lib::documents::Result result);
    void begin_recording_write(const std::shared_ptr<int>& token,
                               ::cw::lib::documents::Document target);
    void finish_recording_write(const std::shared_ptr<int>& token,
                                ::cw::lib::documents::Document target,
                                ::cw::lib::documents::Result result);
    void cancel_document_request() noexcept;
    void OnLoadYadoTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCompStampTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMemberTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCouponTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStatusTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRecoveryTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInfoTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnFriendTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBattleTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPackageTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAreaTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectionTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectedCardTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnShowPartyTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnHidePartyTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void iconize_event(std::shared_ptr<::cw::lib::Dialog> arg_dlg);
    void OnBgmTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStartEventTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStepReturnTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStepOverTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStepInTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPauseTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void pause(bool arg_paused);
    void refresh_pausetool();
    void _refresh_pausetool();
    void OnStopTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBreakpointTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void refresh_breakpointtool();
    void OnClearBreakpointTool(std::shared_ptr<::cw::lib::Window> arg_event);
    void refresh_clearbreakpointtool();
    void OnInitVariables(std::shared_ptr<::cw::lib::Window> arg_event);
    void refresh_areaname();
    void _refresh_areaname(bool arg_force);
    void refresh_selectedmembername();
    void refresh_selectedcardname();
    void refresh_tools();
    void _refresh_tools();
    void refresh_showpartytools();
    void _refresh_showpartytools();
    void dispatch_command_id(int command_id);
    bool dispatch_menu_accelerator(::cw::lib::Integer keycode,
                                   ::cw::dialog::DialogPresentationModifiers modifiers);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,
                                 bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text(const std::string& text,bool composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_focus(bool focused) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                     int index, float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_hover(float x, float y) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_refreshed() noexcept override;
    void dialog_presentation_complete() override;
};

class EventView : public ::cw::lib::Object {
public:
    struct ItemState {
        std::optional<std::shared_ptr<::cw::data::CWPyElement>> parent{};
        std::shared_ptr<::cw::data::CWPyElement> content{};
        std::vector<std::shared_ptr<::cw::data::CWPyElement>> nextdata{};
        ::cw::lib::String cwxpath{};
        ::cw::lib::String text{};
        ::cw::lib::Integer x{};
        ::cw::lib::Integer y{};
        ::cw::lib::Integer width{};
        ::cw::lib::Integer height{};
        bool branch{};
    };

    EventView();
    virtual ~EventView() = default;
    ::cw::lib::Integer m__linenumwidth{};
    std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> m_activeitem{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_current_content{};
    std::optional<std::shared_ptr<::cw::event::Event>> m_current_event{};
    std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> m_current_tree{};
    std::vector<std::shared_ptr<::cw::debug::debugger::EventViewItem>> m_itemlist{};
    std::map<std::shared_ptr<::cw::data::CWPyElement>, std::shared_ptr<::cw::debug::debugger::EventViewItem>> m_items{};
    std::map<std::shared_ptr<::cw::debug::debugger::EventViewItem>, ItemState> m_itemstates{};
    ::cw::lib::Integer m_leftbarwidth{};
    ::cw::lib::Integer m_lineheight{};
    ::cw::lib::Integer m_maxheight{};
    ::cw::lib::Integer m_maxwidth{};
    bool m_processing{};
    ::cw::lib::Integer m_scrollrate_x{};
    ::cw::lib::Integer m_scrollrate_y{};
    ::cw::lib::Integer m_selectionindex{};
    std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> m_selectionitem{};
    std::weak_ptr<::cw::debug::debugger::Debugger> m_parent{};
    bool m_initialized{};
    bool m_bound{};
    bool m_has_focus{};
    bool m_paint_pending{};
    bool m_refresh_pending{};
    ::cw::lib::Integer m_client_width{};
    ::cw::lib::Integer m_client_height{};
    ::cw::lib::Integer m_view_start_x{};
    ::cw::lib::Integer m_view_start_y{};
    ::cw::lib::Integer m_virtual_width{1};
    ::cw::lib::Integer m_virtual_height{1};
    ::cw::dialog::PresentationScrollAxis m_horizontal_scroll{}, m_vertical_scroll{};
    ::cw::lib::Integer m_scroll_gutter{}, m_scrollbar_thickness{};
    void event_scroll_pointer(int index, float x, float y, bool pressed);
    void event_scroll_cancel() noexcept;
    [[nodiscard]] bool event_scroll_repeat_due() const noexcept;
    ::cw::lib::String m_status_text{};
    ::cw::lib::String m_last_error{};
    void initialize(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent);
    void _bind();
    bool AcceptsFocus();
    bool AcceptsFocusFromKeyboard();
    void OnSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    // 単体版と ALL は、すべての環境で
    // 同じ CPU 描画の表示処理を共有
    // する。UI の経路は一系統とする。
    [[nodiscard]] ::cw::dialog::DialogPresentation event_presentation(
        ::cw::lib::Size viewport, const std::set<::cw::lib::String>& breakpoints = {});
    bool enable_eventview();
    std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> get_item(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    ::cw::lib::Integer get_index(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void set_selectionitem(std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> arg_item);
    void update_selection_status();
    void switch_breakpoint(std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> arg_item);
    void click_row(const std::shared_ptr<EventViewItem>& item);
    void OnLeftDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    void set_activeitem(std::shared_ptr<::cw::debug::debugger::EventViewItem> arg_item);
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void show_item(std::shared_ptr<::cw::debug::debugger::EventViewItem> arg_item);
    void refresh_activeitem();
    void refresh_tree();
    void set_event(std::optional<std::shared_ptr<::cw::event::Event>> arg_event, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_selection);
    void _refresh_tree(std::optional<std::shared_ptr<::cw::event::Event>> arg_nowrunning, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> arg_trees, std::optional<std::vector<::cw::lib::String>> arg_treekeys);
    void create_item(std::optional<std::shared_ptr<::cw::debug::debugger::EventViewItem>> arg_parentitem, std::shared_ptr<::cw::data::CWPyElement> arg_contents, ::cw::lib::Integer arg_shiftx, std::shared_ptr<::cw::lib::DeviceContext> arg_dc);
};

class EventViewItem : public ::cw::lib::Object {
public:
    EventViewItem() = default;
    virtual ~EventViewItem() = default;
    ::cw::lib::Value m_content{};
    ::cw::lib::Value m_cwxpath{};
    ::cw::lib::Integer m_height{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_nextdata{};
    ::cw::lib::Integer m_nextlen{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_text{};
    ::cw::lib::Value m_width{};
    // Python EventViewItem の描画非依存状態。wx.Bitmap/DC は Value のまま
    // 保持し、SDL/native UI 移行時にここから描画へ投影する。
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_parent_element{};
    std::shared_ptr<::cw::data::CWPyElement> m_content_element{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m_nextdata_elements{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_pos_typed{};
    ::cw::lib::String m_cwxpath_typed{};
    ::cw::lib::String m_text_typed{};
    ::cw::lib::Integer m_width_typed{};
    bool m_branch{};
    bool m_initialized{};
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_parent, std::shared_ptr<::cw::event::Event> arg_event, std::shared_ptr<::cw::data::CWPyElement> arg_content, std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_nextdata, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_lineheight, std::shared_ptr<::cw::lib::DeviceContext> arg_dc);
    bool is_branch();
    bool is_contains(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
};

class StackTraceView : public ::cw::lib::Object {
public:
    StackTraceView();
    virtual ~StackTraceView() = default;
    struct StackTraceFrame : public ::cw::lib::Object {
        StackTraceFrame() = default;
        virtual ~StackTraceFrame() = default;
        std::shared_ptr<::cw::event::Event> event{};
        std::optional<std::shared_ptr<::cw::data::CWPyElement>> content{};
        ::cw::lib::Integer line_index{};
    };
    using StackEntry = std::tuple<std::shared_ptr<::cw::event::Event>, std::optional<std::shared_ptr<::cw::data::CWPyElement>>>;
    StackTraceSnapshot m_stack_snapshot;
    std::vector<::cw::lib::Integer> m_row_sources; // -1 は現在のコンテントの末尾を表す。
    static StackTraceSnapshot capture_snapshot();
    static std::optional<StackTraceRow> capture_item(::cw::lib::Value item);
    static std::optional<StackTraceRow> capture_active(std::shared_ptr<::cw::event::Event> event,
        std::optional<std::shared_ptr<::cw::data::CWPyElement>> content,
        std::optional<std::shared_ptr<::cw::event::Event>> status_event = std::nullopt);
    void apply_snapshot(StackTraceSnapshot snapshot);
    void rebuild_rows();
    void append_row(std::optional<StackTraceRow> row);
    void replace_row(::cw::lib::Integer index, std::optional<StackTraceRow> row);
    void apply_active(std::optional<StackTraceRow> row);
    bool m__has_curcontent{};
    ::cw::lib::Integer m_imgidx_area{};
    ::cw::lib::Integer m_imgidx_battle{};
    ::cw::lib::Integer m_imgidx_beast{};
    ::cw::lib::Integer m_imgidx_call_package{};
    ::cw::lib::Integer m_imgidx_call_start{};
    ::cw::lib::Integer m_imgidx_card{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_imgidx_contents{};
    ::cw::lib::Integer m_imgidx_effect{};
    ::cw::lib::Integer m_imgidx_event{};
    ::cw::lib::Integer m_imgidx_item{};
    ::cw::lib::Integer m_imgidx_link_package{};
    ::cw::lib::Integer m_imgidx_link_start{};
    ::cw::lib::Integer m_imgidx_package{};
    ::cw::lib::Integer m_imgidx_skill{};
    ::cw::lib::Value m_imglist{};
    std::vector<StackEntry> m_list{};
    std::weak_ptr<::cw::debug::debugger::Debugger> m_parent{};
    std::vector<::cw::lib::String> m_rows{};
    std::vector<::cw::lib::Integer> m_row_images{};
    DebuggerListViewport m_list_view{30000,-1301,-1302};
    std::vector<::cw::lib::Integer> m_column_widths{400};
    std::map<::cw::lib::Integer,std::shared_ptr<::cw::lib::Surface>> m_icons{};
    [[nodiscard]] ::cw::dialog::DialogPresentation stack_presentation(
        ::cw::lib::Size size,bool focused,::cw::lib::Integer& first_row);
    ::cw::lib::Integer m_selected_row{-1};
    bool m_initialized{};
    bool m_bound{};
    bool m_refresh_pending{};
    ::cw::lib::String m_last_error{};
    void initialize(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent);
    void _bind();
    void OnDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    bool enable_eventview();
    void refresh_stackinfo();
    std::tuple<std::optional<::cw::lib::String>, std::optional<::cw::lib::Integer>, std::optional<std::tuple<std::shared_ptr<::cw::event::Event>, std::optional<std::shared_ptr<::cw::data::CWPyElement>>>>> _get_item(::cw::lib::Value arg_evt);
    std::tuple<::cw::lib::String, ::cw::lib::Integer> _get_info(
        std::shared_ptr<::cw::data::CWPyElement> arg_cur_content,
        std::optional<std::shared_ptr<::cw::event::Event>> arg_event = std::nullopt);
    void append_stackinfo_cwpy(::cw::lib::Value arg_item);
    void pop_stackinfo_cwpy();
    void replace_stackinfo_cwpy(::cw::lib::Integer arg_index, ::cw::lib::Value arg_item);
    void clear_stackinfo_cwpy();
    void refresh_activeitem(std::shared_ptr<::cw::event::Event> arg_nowrunning, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_cur_content);
};

class VariableListCtrl : public ::cw::lib::Object,
                         public std::enable_shared_from_this<VariableListCtrl> {
public:
    using Variable = std::variant<
        std::shared_ptr<::cw::data::Flag>,
        std::shared_ptr<::cw::data::Step>,
        std::shared_ptr<::cw::data::Variant>>;
    struct VariableDisplay {
        ::cw::lib::String name, value;
        ::cw::lib::Value raw_value;
        std::vector<::cw::lib::String> choices;
        bool different_scenario{};
        bool operator==(const VariableDisplay& other) const {
            return name == other.name && value == other.value &&
                raw_value.storage() == other.raw_value.storage() &&
                choices == other.choices && different_scenario == other.different_scenario;
        }
    };
    struct VariableEntry {
        Variable variable{};
        bool local{};
        bool editable{};
        std::optional<VariableDisplay> display{};
        bool operator==(const VariableEntry&) const = default;
    };
    using Snapshot = std::vector<VariableEntry>;
    using SnapshotSource = std::function<Snapshot()>;
    static VariableEntry snapshot_entry(VariableEntry entry);
    static Snapshot collect_variables(std::shared_ptr<::cw::event::Event> event,
        std::shared_ptr<::cw::data::SystemData> global, bool local_editable);
    bool queue_refresh(SnapshotSource collect, bool whole_list = true,
                       std::function<void(::cw::lib::Callback)> enqueue = {});

    VariableListCtrl();
    virtual ~VariableListCtrl() = default;
    inline static std::shared_ptr<::cw::lib::Window> s_mi_initvars{};
    ::cw::lib::Integer m_imgidx_flag{};
    ::cw::lib::Integer m_imgidx_flag_l{};
    ::cw::lib::Integer m_imgidx_step{};
    ::cw::lib::Integer m_imgidx_step_l{};
    ::cw::lib::Integer m_imgidx_variant{};
    ::cw::lib::Integer m_imgidx_variant_l{};
    ::cw::lib::Value m_imglist{};
    std::vector<VariableEntry> m_list{};
    ::cw::lib::Value m_mi_initvars{};
    ::cw::lib::Value m_popup_menu{};
    std::weak_ptr<::cw::debug::debugger::Debugger> m_parent{};
    bool m_initialized{};
    bool m_bound{};
    std::weak_ptr<::cw::frame::Frame> m_refresh_frame{};
    std::atomic<std::uint64_t> m_refresh_epoch{};
    bool m_refresh_pending{};
    ::cw::lib::Integer m_selected_row{-1};
    ::cw::lib::Integer m_last_refreshed_row{-1};
    ::cw::lib::Integer m_item_count{};
    std::vector<::cw::lib::String> m_columns{"名称", "現在値"};
    std::vector<::cw::lib::Integer> m_column_widths{120, 80};
    std::array<std::shared_ptr<::cw::lib::Bitmap>, 6> m_icons{};
    bool m_icons_loaded{};
    ::cw::lib::Integer m_visible_rows{1};
    DebuggerListViewport m_list_view{10000,-1201,-1202,-1210};
    bool autosize_column(int index);
    [[nodiscard]] ::cw::dialog::DialogPresentation variable_presentation(
        ::cw::lib::Size size, bool focused, ::cw::lib::Integer& first_row);
    bool navigate(::cw::dialog::DialogPresentationKey key, ::cw::lib::Integer& first_row);
    std::vector<::cw::lib::String> m_edit_choices{};
    bool m_last_different_scenario{};
    ::cw::lib::String m_last_error{};
    std::vector<::cw::lib::String> m_operation_log{};
    void initialize(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent);
    void _bind();
    void OnContextMenu(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInitVariables(std::shared_ptr<::cw::lib::Window> arg_event);
    void init_variables();
    bool queue_reset(std::function<void(::cw::lib::Callback)> enqueue = {},
                     std::function<void(bool)> after_reset = {});
    static void _update_variablesowner();
    // Python は受理した編集を CWPy のキューに入れ、表示の完了処理だけを Frame へ返す。
    bool queue_edit(VariableEntry entry, ::cw::lib::Value value,
                    std::function<void(::cw::lib::Callback)> enqueue = {});
    void OnDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    void _edit_variant(std::shared_ptr<::cw::data::Variant> arg_variant, bool arg_local,
                       VariableDisplay display);
    ::cw::lib::String OnGetItemText(::cw::lib::Integer arg_row, ::cw::lib::Integer arg_col);
    ::cw::lib::Integer OnGetItemImage(::cw::lib::Integer arg_row);
    void refresh_variable(::cw::lib::Value arg_variable);
    void refresh_variablelist(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    void refresh_variablelist_impl(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

extern ::cw::lib::Value g_ID_AREA;
extern ::cw::lib::Value g_ID_BATTLE;
extern ::cw::lib::Value g_ID_BGM;
extern ::cw::lib::Value g_ID_BREAK;
extern ::cw::lib::Value g_ID_BREAKPOINT;
extern ::cw::lib::Value g_ID_CARD;
extern ::cw::lib::Value g_ID_CLEAR_BREAKPOINT;
extern ::cw::lib::Value g_ID_COMPSTAMP;
extern ::cw::lib::Value g_ID_COUPON;
extern ::cw::lib::Value g_ID_EDITOR;
extern ::cw::lib::Value g_ID_FRIEND;
extern ::cw::lib::Value g_ID_GOSSIP;
extern ::cw::lib::Value g_ID_HIDE_PARTY;
extern ::cw::lib::Value g_ID_INFO;
extern ::cw::lib::Value g_ID_INIT_VARIABLES;
extern ::cw::lib::Value g_ID_LOAD;
extern ::cw::lib::Value g_ID_LOAD_YADO;
extern ::cw::lib::Value g_ID_MEMBER;
extern ::cw::lib::Value g_ID_MONEY;
extern ::cw::lib::Value g_ID_PACK;
extern ::cw::lib::Value g_ID_PARTY_ENV;
extern ::cw::lib::Value g_ID_PAUSE;
extern ::cw::lib::Value g_ID_QUIT_DEBUG_MODE;
extern ::cw::lib::Value g_ID_RECOVERY;
extern ::cw::lib::Value g_ID_REDISPLAY;
extern ::cw::lib::Value g_ID_ROUND;
extern ::cw::lib::Value g_ID_SAVE;
extern ::cw::lib::Value g_ID_SAVEDJPDCIMAGE;
extern ::cw::lib::Value g_ID_SAVEDVARIABLES;
extern ::cw::lib::Value g_ID_SELECTEDCARD;
extern ::cw::lib::Value g_ID_SELECTION;
extern ::cw::lib::Value g_ID_SHOW_PARTY;
extern ::cw::lib::Value g_ID_SHOW_STACK_TRACE;
extern ::cw::lib::Value g_ID_STARTEVENT;
extern ::cw::lib::Value g_ID_STATUS;
extern ::cw::lib::Value g_ID_STEPIN;
extern ::cw::lib::Value g_ID_STEPOVER;
extern ::cw::lib::Value g_ID_STEPRETURN;
extern ::cw::lib::Value g_ID_STOP;
extern ::cw::lib::Value g_ID_UPDATE;
extern std::shared_ptr<::cw::lib::Mutex> g_mutex;

void Debugger_OnWaitTime_func(::cw::lib::Integer arg_waittime);
void Debugger_OnClose_func();
void Debugger_OnDestroy_func();
void Debugger_OnUpdateTool_func();
void Debugger_OnRedisplayTool_func();
void Debugger_OnSavedJPDCImageTool_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnSavedJPDCImageTool_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnSavedVariablesTool_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnSavedVariablesTool_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnMoneyTool_func(std::shared_ptr<::cw::data::Party> arg_party, ::cw::lib::Integer arg_value);
void Debugger_OnPartyEnvTool_func();
void Debugger_OnRoundTool_func(::cw::lib::Integer arg_value);
void Debugger_exec_editor_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_parent, std::vector<::cw::lib::String> arg_seq);
void Debugger_OnShowStackTraceTool_OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
void Debugger_OnQuitDebugMode_func();
void Debugger_OnSaveTool_func(::cw::lib::String arg_path);
void Debugger_OnLoadTool_func(::cw::lib::String arg_path);
void Debugger_OnLoadTool_func_func();
// Debugger 全回復 (OnRecoveryTool) の本体。ALLのステータスダイアログの
// ショートカットと共通。シナリオ中のみ動作し、ゲームスレッドで呼ぶ。
void full_recovery_party();
void Debugger_OnRecoveryTool_recovery_all(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnRecoveryTool_recovery_all_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnFriendTool_func(std::set<::cw::lib::Integer> arg_friendids, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> arg_seq, std::vector<::cw::lib::Integer> arg_indices);
void Debugger_OnBattleTool_func(::cw::lib::Integer arg_resid);
void Debugger_OnPackageTool_func(::cw::lib::Integer arg_resid);
void Debugger_OnAreaTool_func2(::cw::lib::Integer arg_resid);
void Debugger_OnSelectedCardTool_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnSelectedCardTool_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self, std::vector<std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::header::CardHeader>>>> arg_ccards, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_selectedcard);
void Debugger_OnSelectedCardTool_func_func_func(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
void Debugger_iconize_event_OnIconize(std::shared_ptr<::cw::lib::Window> arg_event);
void Debugger_OnBgmTool_func(::cw::lib::String arg_path);
void Debugger_OnStartEventTool_func(::cw::lib::Callback arg_start);
void Debugger_OnStopTool_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnStopTool_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnClearBreakpointTool_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnClearBreakpointTool_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnClearBreakpointTool_func_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger_OnClearBreakpointTool_func_func_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger__refresh_tools_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void Debugger__refresh_tools_func_func(std::shared_ptr<::cw::debug::debugger::Debugger> arg_self);
void EventView_refresh_activeitem_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self);
void EventView_refresh_activeitem_func_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self, std::optional<std::shared_ptr<::cw::event::Event>> arg_nowrunning, std::optional<std::shared_ptr<::cw::event::Event>> arg_event, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_cur_content);
void EventView_refresh_tree_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self);
void EventView_refresh_tree_func_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self, std::optional<std::shared_ptr<::cw::event::Event>> arg_nowrunning, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> arg_trees, std::optional<std::vector<::cw::lib::String>> arg_treekeys);
void EventView_set_event_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self, std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
void EventView_set_event_func_func(std::shared_ptr<::cw::debug::debugger::EventView> arg_self, std::optional<std::shared_ptr<::cw::event::Event>> arg_event, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> arg_trees, std::optional<std::vector<::cw::lib::String>> arg_treekeys);
std::tuple<std::optional<std::shared_ptr<::cw::data::CWPyElement>>, ::cw::lib::Integer, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> EventView_create_item_update_pos();
std::optional<std::shared_ptr<::cw::lib::Bitmap>> get_contenticon(std::shared_ptr<::cw::data::CWPyElement> arg_content);
void StackTraceView_refresh_stackinfo_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self);
void StackTraceView_refresh_stackinfo_func_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self, std::optional<std::shared_ptr<::cw::event::Event>> arg_nowrunning, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_cur_content, std::vector<std::optional<::cw::lib::Value>> arg_stackinfo);
void StackTraceView_append_stackinfo_cwpy_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self, ::cw::lib::Value arg_item);
void StackTraceView_pop_stackinfo_cwpy_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self);
void StackTraceView_replace_stackinfo_cwpy_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self, ::cw::lib::Integer arg_index, ::cw::lib::Value arg_item);
void StackTraceView_clear_stackinfo_cwpy_func(std::shared_ptr<::cw::debug::debugger::StackTraceView> arg_self);
void main();
}

