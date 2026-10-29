#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/cardcontrol.py の移植。wx コントロール
// は軽量な cw::lib の互換型で表し、一覧・ページ・絞り込み状態は型付
// きで保持して、ネイティブ表示側がダイアログモデルを再利用できるようにする。
#include <array>
#include <deque>
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/dialog/filter_input.hpp"
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::dialog::cardcontrol {
using HeaderItem = std::variant<std::shared_ptr<::cw::header::CardHeader>,
                                std::shared_ptr<::cw::header::InfoCardHeader>>;
using HeaderList = std::vector<HeaderItem>;

class CardControl;

// wx のイベント・描画をモデルから分離する共通 adapter。CardControl 本体は
// OS固有ウィンドウを参照せず、portableなRGBA Surfaceを公開する。
class CardControlUiBackend : public ::cw::lib::Object {
public:
    CardControlUiBackend() = default;
    virtual ~CardControlUiBackend() = default;
    virtual void bind(CardControl& arg_control) = 0;
    virtual void layout(CardControl& arg_control) = 0;
    virtual void update_debug(CardControl& arg_control) = 0;
    virtual void paint(CardControl& arg_control, std::shared_ptr<::cw::lib::Window> arg_event) = 0;
    virtual void draw_additionals(CardControl& arg_control, std::shared_ptr<::cw::lib::DeviceContext> arg_dc) = 0;
    virtual void pump(CardControl& arg_control) = 0;
    virtual void start_timer(CardControl& arg_control) = 0;
    virtual void stop_timer(CardControl& arg_control) = 0;
    virtual void invalidate(CardControl& arg_control) = 0;
    virtual std::optional<::cw::lib::Value> hit_test(const CardControl& arg_control,
                                                      ::cw::lib::Point arg_position) const = 0;
    virtual void post(CardControl& arg_control, ::cw::lib::Callback arg_callback) = 0;
    virtual void show_error(CardControl& arg_control, const ::cw::lib::String& arg_message) = 0;
    virtual void show_info(CardControl& arg_control, const ::cw::lib::Value& arg_header) = 0;
    virtual bool is_visible(const CardControl& arg_control) const noexcept { (void)arg_control; return true; }
    virtual void close(CardControl& arg_control) {}
};

// 全ホストが同じRGBA Surface、イベントキュー、hit-testを利用する。
class CommonCardControlUiBackend : public CardControlUiBackend {
public:
    void bind(CardControl& arg_control) override;
    void layout(CardControl& arg_control) override;
    void update_debug(CardControl& arg_control) override;
    void paint(CardControl& arg_control, std::shared_ptr<::cw::lib::Window> arg_event) override;
    void draw_additionals(CardControl& arg_control, std::shared_ptr<::cw::lib::DeviceContext> arg_dc) override;
    void pump(CardControl& arg_control) override;
    void start_timer(CardControl& arg_control) override;
    void stop_timer(CardControl& arg_control) override;
    void invalidate(CardControl& arg_control) override;
    std::optional<::cw::lib::Value> hit_test(const CardControl& arg_control,
                                              ::cw::lib::Point arg_position) const override;
    void post(CardControl& arg_control, ::cw::lib::Callback arg_callback) override;
    void show_error(CardControl& arg_control, const ::cw::lib::String& arg_message) override;
    void show_info(CardControl& arg_control, const ::cw::lib::Value& arg_header) override;
    bool is_visible(const CardControl& arg_control) const noexcept override;
    void close(CardControl& arg_control) override;
};

std::shared_ptr<CardControlUiBackend> get_cardcontrol_ui_backend();

class CardControl : public ::cw::lib::Object,
                    public ::cw::dialog::DialogPresentationProvider
{
public:
    CardControl() { initialize(nullptr); }
    virtual ~CardControl() = default;
    virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,
        ::cw::lib::Integer logical_pixels) const;
    inline static std::optional<::cw::lib::Callback> s__after_event{};
    inline static ::cw::lib::Integer s__cardpocketinfo{};
    inline static ::cw::lib::Integer s__combo_backpack{};
    inline static std::map<::cw::lib::Integer, ::cw::lib::Integer> s__combo_cast{};
    inline static std::map<::cw::lib::Integer, ::cw::lib::Integer> s__combo_personal{};
    inline static ::cw::lib::Integer s__combo_shelf{};
    inline static ::cw::lib::Integer s__combo_storehouse{};
    inline static ::cw::lib::Integer s__combo_trush{};
    inline static std::shared_ptr<::cw::lib::Window> s_beastbtn{};
    inline static ::cw::lib::Color s_bgcolour{};
    inline static ::cw::lib::String s_callname{};
    inline static std::shared_ptr<::cw::lib::Window> s_downbtn{};
    inline static ::cw::lib::Integer s_index{};
    inline static std::shared_ptr<::cw::lib::Window> s_itembtn{};
    inline static std::vector<::cw::lib::Value> s_list{};
    inline static std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> s_list2{};
    inline static std::shared_ptr<::cw::lib::Window> s_page{};
    inline static std::shared_ptr<::cw::lib::Window> s_personalbtn{};
    inline static std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> s_selection{};
    inline static std::shared_ptr<::cw::lib::Window> s_skillbtn{};
    inline static std::shared_ptr<::cw::lib::Window> s_upbtn{};
    ::cw::lib::Value m__after_event{};
    ::cw::lib::Integer m__animate_frame{};
    ::cw::lib::Value m__animtimer{};
    bool m__cancel_animation{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::image::CardImage>> m__dialog_anim_cardimgs{};
    ::cw::lib::String m__dialog_anim_owner{};
    std::set<::cw::lib::String> m__dialog_anim_paths{};
    ::cw::lib::ValueMap m__drawlist{};
    // Py の _drawlist はヘッダーの同一性をキーにし、描画済みのビットマップを保持する。
    std::map<const ::cw::lib::Object*, std::shared_ptr<::cw::lib::Bitmap>> m_draw_bitmaps{};
    std::optional<::cw::lib::Value> m__lastrepls{};
    std::optional<::cw::lib::Value> m__laststar{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> m__leftmarks{};
    ::cw::lib::Value m__price_sheet{};
    bool m__proc{};
    bool m__quit{};
    bool m__redraw{};
    bool m_window_active{true};
    void dialog_presentation_focus(bool focused) override { m_window_active=focused; if (!focused) dialog_presentation_pointer_cancel(); }
    std::shared_ptr<int> m_deferred_lifetime{std::make_shared<int>(0)};
    std::optional<std::shared_ptr<::cw::dialog::cardcontrol::ReplCardHolder>> m__replcardholder{};
    bool m__replclickedflag{};
    bool m__reserved_narrowconditin{};
    ::cw::lib::Value m__sizer_topbar{};
    bool m__starclickedflag{};
    std::vector<std::optional<std::shared_ptr<::cw::lib::Bitmap>>> m__typeicon_d{};
    std::vector<std::optional<std::shared_ptr<::cw::lib::Bitmap>>> m__typeicon_e{};
    ::cw::lib::Value m_addctrlbtn{};
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Control>, ::cw::lib::Callback>> m_additionals{};
    ::cw::lib::Integer m_areaid{};
    std::vector<std::shared_ptr<::cw::lib::Control>> m_change_bgs{};
    ::cw::lib::Value m_closebtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_downid{};
    ::cw::lib::Value m_downtargkeyid{};
    ::cw::lib::Value m_editstar{};
    ::cw::lib::Value m_infokeyid{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_leftbtn2{};
    ::cw::lib::Value m_leftkeyid{};
    ::cw::lib::Value m_leftpagekeyid{};
    std::vector<::cw::lib::Value> m_list{};
    std::vector<::cw::lib::Value> m_list2{};
    ::cw::lib::Value m_narrow{};
    ::cw::lib::Value m_narrow_type{};
    std::vector<::cw::lib::Value> m_narrowkeydown{};
    ::cw::lib::Value m_nostar{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_replace_arrow{};
    ::cw::lib::Value m_replace_arrow_light{};
    ::cw::lib::Value m_returnkeyid{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_rightbtn2{};
    ::cw::lib::Value m_rightkeyid{};
    ::cw::lib::Value m_rightpagekeyid{};
    // Python の selection 属性はすべての CardControl
    // 派生型で共有する。正規のフィールドをここに一つだけ置き、HANDVIEW
    // と共通のクリック・対象選択で異なるキャラクターを参照しないようにする。
    ::cw::lib::Value m_selection{};
    std::vector<std::optional<std::shared_ptr<::cw::lib::Window>>> m_show{};
    std::vector<std::shared_ptr<::cw::lib::Control>> m_smallctrls{};
    ::cw::lib::Value m_sort{};
    std::vector<::cw::lib::Value> m_sortkeydown{};
    ::cw::lib::Value m_sortwithstar{};
    ::cw::lib::Value m_star{};
    ::cw::lib::Value m_starlight{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_upid{};
    ::cw::lib::Value m_uptargkeyid{};
    // 共通の基底に保持する Python のインスタンス属性。
    ::cw::lib::String m_callname{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m__cardpocketinfo{};
    ::cw::lib::Value m__sendto{};
    std::optional<::cw::lib::Integer> m_sendto_selection{};
    std::shared_ptr<::cw::lib::Window> m_parent{};
    ::cw::lib::String m_title{};
    ::cw::lib::String m_narrow_text{};
    ::cw::lib::Integer m_narrow_selection{};
    ::cw::lib::Integer m_sort_selection{};
    bool m_sort_with_star{};
    bool m_sort_enabled{};
    bool m_edit_star{};
    bool m_show_additional_controls{};
    bool m_sendto{};
    std::array<bool, 3> m_show_cardtype{{true, true, true}};
    std::vector<::cw::lib::String> m_narrow_choices{};
    std::map<const ::cw::lib::Object*, ::cw::lib::Rect> m_card_rects{};
    std::vector<::cw::lib::Value> m_draw_headers{};
    std::shared_ptr<CardControlUiBackend> m_ui_backend{};
    std::deque<::cw::lib::Callback> m_ui_callbacks{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};
    std::set<const ::cw::lib::Object*> m_ui_dirty_cards{};
    std::shared_ptr<::cw::lib::Surface> m_ui_pad{};
    std::optional<::cw::lib::Value> m_ui_info_header{};
    // カード情報は共通モーダルホストの子。ここで寿命を保持して Python
    // の ShowModal/Destroy に合わせ、CardInfo の
    // 終了ハンドラーが所有元フレームのダイアログまで取り除かないようにする。
    std::shared_ptr<::cw::dialog::cardinfo::CardInfo> m_cardinfo_dialog{};
    ::cw::lib::String m_ui_last_error{};
    bool m_ui_invalidated{true};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void reconstruct(::cw::lib::String arg_callname, ::cw::lib::String arg_name, bool arg_sendto, bool arg_sort, std::optional<::cw::lib::Integer> arg_areaid);
    void _start_animtimer();
    void _stop_animtimer();
    void _sync_dialog_animated_paths();
    virtual void _bind();
    virtual bool _can_narrow();
    virtual void OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _do_layout();
    virtual void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void update_debug();
    void update_additionals();
    virtual void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnToggleAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event);
    void _additional_controls();
    void OnNarrowCondition(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSendTo(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void update_narrowcondition();
    virtual void OnSort(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnSortWithStar(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEditStar(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnShowSkill(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnShowItem(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnShowBeast(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _update_sortwithstar();
    virtual void _update_editstar();
    virtual void OnUp(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void update_cardpocketinfo_with(std::optional<::cw::lib::Value> arg_header);
    bool _can_sideclick();
    bool _cursor_in_ctrlsarea(std::shared_ptr<::cw::lib::Window> arg_mousepos);
    bool _is_cursorinleft();
    bool _is_cursorinright();
    virtual void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual bool _update_sortattr(bool arg_draw);
    void _replace_position(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2);
    virtual void _replace_position_impl(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2);
    void OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightUp2(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMove(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_move(std::shared_ptr<::cw::lib::Window> arg_mousepos);
    void OnEnter(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeave(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn2(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRightBtn2(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _draw_additionals(std::shared_ptr<::cw::lib::DeviceContext> arg_dc);
    bool _show_star(std::shared_ptr<::cw::header::CardHeader> arg_header);
    std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> _get_starrect(std::shared_ptr<::cw::header::CardHeader> arg_header);
    std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> _get_starrect(std::shared_ptr<::cw::header::CardHeader> arg_header,
                                                                                        ::cw::lib::Real arg_draw_x,
                                                                                        ::cw::lib::Real arg_draw_y,
                                                                                        ::cw::lib::Integer arg_bmp_w,
                                                                                        ::cw::lib::Integer arg_bmp_h);
    bool _can_repls();
    std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> _get_replsrect(::cw::lib::Value arg_header);
    void draw(bool arg_update);
    ::cw::lib::Integer get_mode();
    virtual void draw_cards(bool arg_update, ::cw::lib::Integer arg_mode);
    std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> _get_test_aptitude();
    void draw_card(::cw::lib::Value arg_header, bool arg_fromkeyevent);
    ::cw::lib::Integer get_posmode();
    void set_cardpos(::cw::lib::Integer arg_mode);
    virtual bool is_showpersonal();
    virtual std::vector<::cw::lib::Value> get_headers();
    virtual std::vector<::cw::lib::Value> get_beforepageheaders();
    void _after_animation_paint(::cw::lib::Callback arg_func);
    void animate_click(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func);
    void animate_starclick(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func);
    void animate_replsclick(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func);
    void _animate_dealhide(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func, std::vector<::cw::lib::Integer> arg_dealing_scales, ::cw::lib::Integer arg_lastscale);
    void animate_hide(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func);
    void animate_deal(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func);
    virtual void lclick_event(::cw::lib::Value arg_header);
    void rclick_event(::cw::lib::Value arg_header);
    void after_message();
    void process_ui_events();
    std::shared_ptr<CardControlUiBackend> ui_backend() const;
    void show_errordialog(::cw::lib::String arg_s);
    bool check_using(std::shared_ptr<::cw::character::Character> arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header);
    virtual void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_text(const std::string& arg_text, bool arg_composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers mods) override {
        m_narrow_input.modifiers=mods;m_page_editor.modifiers=mods;
    }
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
        int index, float x, float y, bool pressed) override;
    void dialog_presentation_hover(float x, float y) override;
    void dialog_presentation_context(float x, float y) override;
    std::optional<::cw::lib::Value> m_pointer_card;
    void dialog_presentation_pointer_cancel() override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    [[nodiscard]] bool navigation_enabled() const;
    virtual bool side_click_option() const;
    void append_side_controls(::cw::dialog::DialogPresentation& view);
    std::optional<::cw::dialog::DialogPresentationAction> side_action_at(float x,float y);
    void retire_side_input();
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    std::vector<::cw::dialog::DialogPresentationControl> m_side_controls,m_side_exclusions;
    std::optional<std::pair<float,float>> m_side_pointer;
    std::optional<::cw::dialog::DialogPresentationAction> m_side_capture;
    double m_side_layout_scale{-1},m_side_capture_scale{1};
    bool m_side_stale{};
    [[nodiscard]] bool chain_enabled(::cw::dialog::DialogPresentationAction action,int index) const;
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point card_input_time() const noexcept;
    std::optional<::cw::dialog::DialogPresentationControl> m_chain_capture;
    std::vector<::cw::dialog::DialogPresentationControl> m_chain_controls;
    ::cw::dialog::ButtonRepeatTimer m_chain_timer;
    bool m_chain_running{},m_chain_stale{},m_chain_dispatching{};
    double m_chain_scale{},m_presentation_scale{};
    void retire_chain_input();
    void dispatch_chain(::cw::dialog::DialogPresentationAction action,int index);
    [[nodiscard]] bool navigation_input_blocked() const;
    ::cw::lib::Rect m_pointer_bounds{};
    bool m_pointer_reorder{};
    int m_choice_popup{-1};
    int m_choice_cursor{};
    int m_choice_first{},m_choice_visible{1};
    ::cw::lib::Rect m_choice_scrollbar{},m_choice_thumb{};
    std::optional<float> m_choice_drag;
    bool m_choice_drag_stale{};
    double m_choice_drag_scale{};
    std::vector<::cw::dialog::DialogPresentationControl> m_choice_rows;
    double m_choice_rows_scale{};
    ::cw::lib::String m_choice_prefix,m_choice_composition;
    ::cw::dialog::ButtonRepeatTimer::Clock::time_point m_choice_prefix_time{};
    virtual std::shared_ptr<::cw::lib::Surface> combo_bitmap(const ::cw::lib::String& role) const;
    int m_input_focus{-1};
    using FocusTarget=std::pair<::cw::dialog::DialogPresentationAction,int>;
    std::optional<FocusTarget> m_focus_target;
    std::optional<::cw::dialog::DialogPresentationControl> m_space_target;
    std::optional<::cw::dialog::DialogPresentationControl> m_button_capture;
    bool m_button_inside{},m_button_stale{};
    double m_button_scale{-1};
    bool m_keyboard_focus{},m_space_held{};
    double m_keyboard_scale{-1};
    [[nodiscard]] std::vector<::cw::dialog::DialogPresentationControl> control_layout();
    [[nodiscard]] std::vector<FocusTarget> focus_targets();
    void focus_control(std::optional<FocusTarget> target,bool keyboard);
    void focus_next(int direction);
    void retire_keyboard_press();
    void decorate_keyboard_controls(::cw::dialog::DialogPresentation& view,
        const std::shared_ptr<::cw::lib::Surface>& surface);

    ::cw::lib::String m_page_input;
    ::cw::dialog::textedit::FilterInput m_narrow_input,m_page_editor;
    void sync_field_inputs();
    void retire_field_inputs();
    ::cw::dialog::textedit::FilterInput* active_field();
    bool edit_field(const std::function<bool(::cw::dialog::textedit::FilterInput&)>& operation,bool clamp_page=false);
    void publish_field_input();
    void commit_page_input();
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class CardHolder : public CardControl {
public:
    bool show_replacement(std::shared_ptr<::cw::sprite::card::PlayerCard> target,
                          std::shared_ptr<::cw::header::CardHeader> card, bool personal);
    void refresh_after_transfer();
    CardHolder() = default;
    virtual ~CardHolder() = default;
    inline static std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> s_index2{};
    bool m__can_open_backpack{};
    bool m__can_open_cardpocket{};
    bool m__can_open_storehouse{};
    // Python の取り消しフラグは CardControl のアニメーションキューと共
    // 有する。別の同名フィールドを持つと、取り消した所有者の操作が後から実行されてしまう。
    ::cw::lib::Value m__cardpocketinfo{};
    ::cw::lib::Integer m__combo_backpack{};
    ::cw::lib::ValueMap m__combo_cast{};
    ::cw::lib::Integer m__combo_manual{};
    ::cw::lib::ValueMap m__combo_personal{};
    ::cw::lib::Integer m__combo_shelf{};
    ::cw::lib::Integer m__combo_storehouse{};
    ::cw::lib::Integer m__combo_trush{};
    std::vector<::cw::lib::Value> m__fulllist{};
    bool m__proc_page{};
    bool m__redraw{};
    ::cw::lib::Value m__sendto{};
    ::cw::lib::Value m_areaid{};
    ::cw::lib::Value m_beastbtn{};
    ::cw::lib::Value m_bgcolour{};
    ::cw::lib::String m_callname{};
    ::cw::lib::Value m_downbtn{};
    ::cw::lib::Integer m_index{};
    HeaderList m_typed_list{};
    HeaderList m_typed_full_list{};
    ::cw::lib::Integer m_page_size{10};
    ::cw::lib::Integer m_page_count{1};
    // 絞り込みは CardControl の入力状態を使う。別の同名フィールドを持
    // つと、最初の検索語が残り続け、行の再構築時に選択条件が初期化されてしまう。
    ::cw::lib::Value m_index2{};
    ::cw::lib::Value m_itembtn{};
    std::vector<::cw::lib::Value> m_list{};
    std::vector<::cw::lib::Value> m_list2{};
    std::vector<::cw::lib::String> m_sendto_labels{};
    std::vector<::cw::lib::String> m_sendto_icons{};
    ::cw::lib::Value m_page{};
    ::cw::lib::Value m_personalbtn{};
    std::vector<::cw::lib::Integer> m_page_history{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_pre_pos{};
    ::cw::lib::Value m_skillbtn{};
    ::cw::lib::Value m_upbtn{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_card_list{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_card_full_list{};
    std::vector<std::shared_ptr<::cw::header::InfoCardHeader>> m_info_list{};
    std::vector<std::shared_ptr<::cw::header::InfoCardHeader>> m_info_full_list{};
    bool m_show_personal{};
    bool m_environment_backpack{};
    bool m_proc_page{};
    bool m_can_open_backpack{};
    bool m_can_open_cardpocket{};
    bool m_can_open_storehouse{};
    ::cw::lib::Integer m_last_cardpocket{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_callname);
    void reconstruct_cardholder(::cw::lib::String arg_callname, std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_selection, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>> arg_pre_info, std::optional<::cw::lib::Integer> arg_areaid);
    void initialize_inventory_list(::cw::lib::Value arg_status);
    virtual void _init_list(::cw::lib::Value arg_status);
    void _bind() override;
    void update_debug() override;
    void _load_index();
    void _store_index();
    void OnPageSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSort(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnSortWithStar(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void _update_sortwithstar();
    void _update_editstar();
    bool _update_sortattr(bool arg_draw) override;
    void OnShowSkill(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnShowItem(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnShowBeast(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _on_show(::cw::lib::Integer arg_cardtype);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void _clear_flags();
    virtual void _change_callname(::cw::lib::String arg_old_callname);
    void _update_cardpocketinfo(::cw::lib::Integer arg_pocket, bool arg_force);
    void update_cardpocketinfo_with(std::optional<::cw::lib::Value> arg_header);
    void _enable_updown();
    void _build_sendto_combo();
    void _show_controls();
    void _update_page();
    virtual void _set_backpacklist(bool arg_narrow);
    void lclick_event(::cw::lib::Value arg_header) override;
    void OnClickToggleBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUp(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnDown(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPageNum(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_pagenum(::cw::lib::Integer arg_page);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void draw_cards(bool arg_update, ::cw::lib::Integer arg_mode) override;
    virtual void _init_cardpocketlist();
    virtual std::vector<::cw::lib::Value> get_headers();
    virtual bool is_showpersonal();
    virtual std::vector<::cw::lib::Value> get_beforepageheaders();
    std::vector<::cw::lib::Value> _narrow(std::vector<::cw::lib::Value> arg_clist);
    void _clamp_page_index();
    void update_narrowcondition() override;
    void _replace_position_impl(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2) override;
};

class HandView : public CardControl {
public:
    HandView() = default;
    virtual ~HandView() = default;
    // CardControl のアニメーション取り消し状態を継承する。キュー処理も同じ状態を読む。
    ::cw::lib::Value m_bgcolour{};
    ::cw::lib::String m_callname{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_index2{};
    ::cw::lib::Value m_list{};
    std::vector<::cw::lib::Value> m_list2{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_card_list{};
    ::cw::lib::Value m_owner{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_pre_pos{};
    ::cw::lib::Value m_redeal{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void reconstruct_handview(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_selection, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>> arg_pre_info);
    void _do_layout() override;
    void _bind() override;
    void update_debug() override;
    void _update_cardlist(std::shared_ptr<::cw::character::Character> arg_selection);
    bool _update_enemylist(std::shared_ptr<::cw::character::Enemy> arg_selection);
    void OnReDeal(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void draw_cards(bool arg_update, ::cw::lib::Integer arg_mode) override;
    std::vector<::cw::lib::Value> get_headers() override;
    void _draw_additionals(std::shared_ptr<::cw::lib::DeviceContext> arg_dc) override;
};

class InfoView : public CardHolder {
public:
    InfoView() = default;
    virtual ~InfoView() = default;
    ::cw::lib::Value m_list{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void _init_list(::cw::lib::Value arg_status);
    void reconstruct_infoview();
    bool is_showpersonal();
    bool _update_sortattr(bool arg_draw);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
};

class ReplCardHolder : public CardControl {
public:
    ReplCardHolder() = default;
    virtual ~ReplCardHolder() = default;
    inline static std::shared_ptr<::cw::sprite::card::PlayerCard> s_selection{};
    ::cw::lib::Value m_bgcolour{};
    ::cw::lib::Value m_cardtype{};
    ::cw::lib::Value m_index2{};
    ::cw::lib::Value m_owner{};
    ::cw::lib::Value m_personal{};
    ::cw::lib::Value m_target{};
    std::shared_ptr<::cw::header::CardHeader> m_target_header{};
    // Python では置換画面は既存の CardControl
    // の子。この関係を明示し、置換画面を閉じて
    // も親ダイアログまで終了しないようにする。
    CardHolder* m_parent_holder{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_card_list{};
    bool m_personal_mode{};
    static constexpr ::cw::lib::Integer cards_per_page{12};
    [[nodiscard]] std::vector<::cw::lib::Value> all_candidate_headers() const;
    [[nodiscard]] ::cw::lib::Integer candidate_page_count() const;
    void change_candidate_page(::cw::lib::Integer delta);
    static bool needs_exchange(const std::shared_ptr<::cw::sprite::card::PlayerCard>& target,
                               const std::shared_ptr<::cw::header::CardHeader>& card, bool personal);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void reconstruct_replcardholder(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_selection, std::shared_ptr<::cw::header::CardHeader> arg_target, bool arg_personal);
    void _bind();
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void draw_cards(bool arg_update, ::cw::lib::Integer arg_mode) override;
    std::vector<::cw::lib::Value> get_headers() override;
    void lclick_event(::cw::lib::Value arg_header) override;
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event) override;
    virtual bool schedule_exchange(std::shared_ptr<::cw::header::CardHeader> card,
        std::shared_ptr<::cw::character::Player> selection, ::cw::lib::Integer index);
    void _replace_position_impl(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2) override;
};

class SelectCard : public CardHolder {
public:
    SelectCard() = default;
    virtual ~SelectCard() = default;
    // CardHolder を通じて、同じ取り消し状態を継承する。
    ::cw::lib::Value m_bgcolour{};
    ::cw::lib::Value m_list{};
    // Python は一つの list2 を継承する。CardHolder::m_list2
    // と同名の別フィールドを作らず、荷物袋・荷物置場の構築と継承し
    // た左右移動で、拡張ポケットも含む同じ PC 一覧を共有する。
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_callname);
    void _init_list(::cw::lib::Value arg_status) override;
    bool _update_sortattr(bool arg_draw) override;
    void _on_show(::cw::lib::Integer arg_cardtype) override;
    void _change_callname(::cw::lib::String arg_old_callname) override;
    void _set_backpacklist(bool arg_narrow) override;
    void _init_cardpocketlist() override;
    void lclick_event(::cw::lib::Value arg_header) override;
};

extern ::cw::lib::Integer g_CCMODE_BATTLE;
extern ::cw::lib::Integer g_CCMODE_MOVE;
extern ::cw::lib::Integer g_CCMODE_REPLACE;
extern ::cw::lib::Integer g_CCMODE_SHOW;
extern ::cw::lib::Integer g_CCMODE_USE;
extern ::cw::lib::Value g_CardHeaderType;

bool CardControl___init___can_sort();
::cw::lib::Value CardControl___init___lambda_line_148();
void CardControl_OnDebugMode_func(::cw::lib::Value arg_self);
void CardControl_OnDebugMode_func_func(::cw::lib::Value arg_self);
void CardControl__additional_controls_func();
void CardControl_OnNarrowCondition_func();
void CardControl_OnKeyDown_func();
void CardControl_OnKeyDown_func_2();
void CardControl_OnLeftUp_func();
void CardControl_OnLeftUp_func_2();
void CardControl_OnLeftUp_func_3();
void CardControl__replace_position_func();
::cw::lib::Value CardControl__replace_position_func_lambda_line_933();
void CardControl_OnRightUp_func();
void CardControl_OnPaint2_draw_price(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardControl_OnPaint2_draw_star(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardControl_animate_click_func2();
void CardControl_animate_click_func2_func3();
void CardControl_animate_starclick_func2();
void CardControl_animate_starclick_func2_func3();
void CardControl_animate_replsclick_func2();
void CardControl_animate_replsclick_func2_func3();
void CardControl__animate_dealhide_func2();
void CardControl__animate_dealhide_func2_func3();
void CardControl_lclick_event_func(std::shared_ptr<::cw::header::CardHeader> arg_header);
void CardControl_lclick_event_func_func();
void CardControl_lclick_event_func2(::cw::lib::Value arg_self);
void CardControl_lclick_event_func2_func(::cw::lib::Value arg_self);
void CardControl_lclick_event_append_predialogs(::cw::lib::String arg_callname, std::shared_ptr<::cw::sprite::card::CWPyCard> arg_index2, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
void CardControl_lclick_event_func3();
void CardControl_OnCancel_func();
::cw::lib::Value CardHolder___init___lambda_line_2064();
void CardHolder_reconstruct_cardholder_func();
void CardHolder_OnPageSetFocus_func();
bool CardHolder_OnMouseWheel_selcombo(::cw::lib::Value arg_combo);
bool CardHolder__narrow_has_personalowner(::cw::lib::Value arg_header);
::cw::lib::Value CardHolder__narrow_lambda_line_3084(::cw::lib::Value arg_header);
bool CardHolder__narrow_match_keycode();
std::shared_ptr<::cw::character::Character> HandView__update_cardlist_character(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_pcard);
bool ReplCardHolder_get_headers_is_attachment(::cw::lib::Value arg_header);
void ReplCardHolder_lclick_event_func(std::shared_ptr<::cw::header::CardHeader> arg_target, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Integer arg_index, std::shared_ptr<::cw::character::Player> arg_selection, bool arg_personal, bool arg_call_predlg);
::cw::lib::Integer get_spacer_x(::cw::lib::Integer arg_mode);
std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> get_poslist(::cw::lib::Integer arg_num, ::cw::lib::Integer arg_mode);
void main();
}

