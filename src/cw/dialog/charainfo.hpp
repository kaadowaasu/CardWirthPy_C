#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// Python cw/dialog/charainfo.py の状態モデルをSDL互換層へ移植した宣言。
#include "cw/lib/compat.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/fwd.hpp"

namespace cw::dialog::charainfo {
class CharaInfo;
class DeckDialog;
class CardPanel;

// キャラクター詳細は一つの共通モデル・Presentation 境界を使
// う。すべてのホストが同じ Surface を合成し、同じ操作を送る。
class CharainfoUiBackend : public ::cw::lib::Object {
public:
    CharainfoUiBackend() = default;
    virtual ~CharainfoUiBackend() = default;
    virtual void bind(CharaInfo& arg_info) = 0;
    virtual void layout(CharaInfo& arg_info) = 0;
    virtual void paint(CharaInfo& arg_info, std::shared_ptr<::cw::lib::Window> arg_event) = 0;
    virtual void paint_deck(DeckDialog& arg_dialog, std::shared_ptr<::cw::lib::Window> arg_event) = 0;
    virtual void pump(CharaInfo& arg_info) = 0;
    virtual void invalidate(CharaInfo& arg_info) = 0;
    virtual void dispatch_command(CharaInfo& arg_info, ::cw::lib::Integer arg_command) = 0;
    virtual void show_card_info(CardPanel& arg_panel, const ::cw::lib::Value& arg_header) = 0;
    virtual void open_deck(CardPanel& arg_panel) = 0;
    virtual void present(CharaInfo& arg_info) = 0;
    virtual void close_window(CharaInfo& arg_info) = 0;
    virtual void end_modal(CharaInfo& arg_info, ::cw::lib::Integer arg_result) = 0;
    virtual void clipboard(const ::cw::lib::String& arg_text) = 0;
};

class CommonCharainfoUiBackend : public CharainfoUiBackend {
public:
    void bind(CharaInfo& arg_info) override;
    void layout(CharaInfo& arg_info) override;
    void paint(CharaInfo& arg_info, std::shared_ptr<::cw::lib::Window> arg_event) override;
    void paint_deck(DeckDialog& arg_dialog, std::shared_ptr<::cw::lib::Window> arg_event) override;
    void pump(CharaInfo& arg_info) override;
    void invalidate(CharaInfo& arg_info) override;
    void dispatch_command(CharaInfo& arg_info, ::cw::lib::Integer arg_command) override;
    void show_card_info(CardPanel& arg_panel, const ::cw::lib::Value& arg_header) override;
    void open_deck(CardPanel& arg_panel) override;
    void present(CharaInfo& arg_info) override;
    void close_window(CharaInfo& arg_info) override;
    void end_modal(CharaInfo& arg_info, ::cw::lib::Integer arg_result) override;
    void clipboard(const ::cw::lib::String& arg_text) override;
};

std::shared_ptr<CharainfoUiBackend> get_charainfo_ui_backend();
// 最初の情報取得と後の移動で共用する、読み取り専用のキャラクターローダー。読めない XML
// やキャラクター以外の XML では例外を投げ、作業ディレクトリーの変更や保存はしない。
::cw::lib::Value read_adventurer_character(const std::shared_ptr<::cw::header::AdventurerHeader>& header);
using StandbySelectionCallback = std::function<void(std::shared_ptr<::cw::header::AdventurerHeader>)>;
void show_standby_charainfo_modal(std::shared_ptr<::cw::lib::Window> arg_parent,
                                  std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers,
                                  ::cw::lib::Value arg_ccard, ::cw::lib::Integer arg_index,
                                  ::cw::lib::Callback arg_redrawfunc, bool arg_is_playingscenario = false,
                                  std::optional<std::shared_ptr<::cw::data::Party>> arg_party = std::nullopt,
                                  StandbySelectionCallback selection_changed = {});
void show_standby_party_charainfo_modal(std::shared_ptr<::cw::lib::Window> arg_parent,
                                          std::shared_ptr<::cw::header::PartyHeader> arg_partyheader,
                                          ::cw::lib::Callback arg_redrawfunc);

class ActiveCharaInfo;

enum class CharacterPageEditor { None, Description, History, Status };

class CardPanel : public ::cw::lib::Object {
public:
    CardPanel() = default;
    virtual ~CardPanel() = default;
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_csize{};
    ::cw::lib::Value m_deck_btn{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_headers{};
    std::optional<std::shared_ptr<::cw::dialog::charainfo::HoldAll>> m_hold_all{};
    ::cw::lib::Integer m_pocket{};
    ::cw::lib::Value m_watermark{};
    std::shared_ptr<::cw::lib::Window> m_parent{};
    std::shared_ptr<::cw::character::Character> m_character{};
    // 単体版とホスト内表示で、同じモーダルの子を組み込む。
    std::shared_ptr<::cw::dialog::cardinfo::CardInfo> m_cardinfo_dialog{};
    bool m_child_open{};
    ::cw::lib::Point m_mousepos{};
    ::cw::lib::String m_detail_text{};
    std::vector<::cw::lib::Rect> m_card_rects{};
    static constexpr std::size_t cards_per_page = 10;
    std::size_t m_card_page{};
    [[nodiscard]] std::size_t card_page_count() const noexcept;
    void change_card_page(int direction);
    // DeckDialog の PC 移動で使う親 CharaInfo
    // 。ネイティブパネルには親としてキャラクターのウィンドウだけが渡る
    // ため、HWND から推測せずモデルの所有者を明示的に保持する。
    CharaInfo* m_charainfo_owner{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_pocket);
    void _update_debug();
    void update_debug();
    void OnDeck(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_size_deck(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void _switch_hold();
    bool _open_cardinfo();
    virtual void OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeave(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMove(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_mousepos(std::shared_ptr<::cw::lib::Window> arg_mousepos);
    void update_negaflag();
    void draw_header(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::Value arg_header);
    void up();
    void down();
    void _update_rects(std::shared_ptr<::cw::lib::DeviceContext> arg_dc);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw(bool arg_update);
    ::cw::lib::String _get_cardnum();
    ::cw::lib::String get_detailtext();
};

class CharaInfo : public ::cw::lib::Object,
                 public ::cw::dialog::DialogPresentationProvider
{
public:
    CharaInfo() = default;
    virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role,
                                                ::cw::lib::Integer logical_pixels) const;
    bool refresh_font_metrics();
    [[nodiscard]] bool font_metrics_changed() const;
    ::cw::lib::Real m_font_layout_scale{-1};
    std::vector<::cw::lib::ui::FontStyle> m_layout_fonts;
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    ::cw::dialog::ButtonRepeatTimer m_page_repeat_timer;
    int m_page_repeat{},m_page_capture{};
    bool m_published_controls_dirty{true};
    std::vector<::cw::dialog::DialogPresentationControl> m_published_controls;
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point character_input_time() const noexcept;
    virtual std::chrono::milliseconds page_repeat_interval() const noexcept;
    void dialog_presentation_pointer_cancel() override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    virtual ~CharaInfo();
    inline static ::cw::lib::Value s_ccard{};
    inline static ::cw::lib::Integer s_index{};
    inline static ::cw::lib::Value s_list{};
    std::shared_ptr<::cw::dialog::charainfo::BeastPanel> m_beastpanel{};
    std::vector<::cw::lib::Value> m_bottompanel{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_list{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_csize{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_descpanel{};
    ::cw::lib::Value m_downkeyid{};
    ::cw::lib::Value m_editpanel{};
    ::cw::lib::Value m_endkeyid{};
    ::cw::lib::Value m_enter{};
    ::cw::lib::Value m_historypanel{};
    ::cw::lib::Value m_homekeyid{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Integer m_page_index{};
    ::cw::lib::String m_copy_text{};
    std::shared_ptr<::cw::dialog::charainfo::ItemPanel> m_itempanel{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_leftpagekeyid{};
    ::cw::lib::Value m_notebook{};
    ::cw::lib::Value m_openinfo{};
    ::cw::lib::Value m_pagedownkeyid{};
    ::cw::lib::Value m_pageupkeyid{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_party{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_rightpagekeyid{};
    std::shared_ptr<::cw::dialog::charainfo::SkillPanel> m_skillpanel{};
    ::cw::lib::Value m_titlepanel{};
    std::shared_ptr<::cw::dialog::charainfo::TopPanel> m_toppanel{};
    ::cw::lib::Value m_upkeyid{};
    ::cw::lib::Value m_width{};
    std::shared_ptr<::cw::lib::Window> m_parent{};
    std::optional<::cw::lib::Callback> m_redrawfunc{};
    bool m_editable{};
    bool m_is_playingscenario{};
    // このダイアログが Frame::m_active_dialog_model
    // を所有するとき true。PlayerSelect から開くキャラクタ
    // ー情報は入れ子なので、終了時に親のモーダルスタックを減らしてはならない。
    bool m_frame_dialog_owned{};
    bool m_modal_ended{};
    bool m_closing{};
    bool m_child_open{};
    std::shared_ptr<int> m_callback_lifetime{std::make_shared<int>(0)};
    ::cw::lib::Integer m_modal_result{};
    ::cw::lib::String m_title_text{};
    std::shared_ptr<CharainfoUiBackend> m_ui_backend{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};
    ::cw::lib::Integer m_scroll_y{};
    std::vector<::cw::lib::Integer> m_page_scrolls{};
    // 共通の表示座標だけを保持し、ホスト全体のカーソル位置は問い合わせない。
    float m_last_hover_x{-1.0F};
    float m_last_hover_y{-1.0F};
    bool m_has_hover_position{};
    bool m_ui_invalidated{true};
    ::cw::lib::String m_ui_last_info{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::optional<::cw::lib::Callback> arg_redrawfunc, bool arg_editable, std::optional<std::shared_ptr<::cw::data::Party>> arg_party);
    void _bind();
    void OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event);
    void copy_detail();
    [[nodiscard]] bool dialog_presentation_can_copy_detail() const noexcept override { return true; }
    void dialog_presentation_copy_detail() override { copy_detail(); }
    [[nodiscard]] ::cw::lib::String detail_text() const;
    [[nodiscard]] bool can_change_character() const;
    [[nodiscard]] CharacterPageEditor page_editor() const;
    void open_page_editor(CharacterPageEditor kind);
    void refresh_after_editor();
    [[nodiscard]] ::cw::lib::Integer maximum_scroll() const;
    [[nodiscard]] ::cw::lib::Integer scroll_unit() const;
    void scroll_content(::cw::lib::Integer delta);
    void OnEnter(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOpenInfo(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPageUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnHome(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPageDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftKey(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightKey(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnEnd(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void update_debug();
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPageChanged(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPageChanging(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw(bool arg_update);
    void _do_layout();
    void process_ui_events();
    std::shared_ptr<CharainfoUiBackend> ui_backend() const;
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override {
        dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter, true, modifiers);
    }
    void dialog_presentation_hover(float x, float y) override;
    void apply_dialog_presentation_hover(float x, float y);
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
        int index, float x, float y, bool pressed) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class ActiveCharaInfo : public ::cw::dialog::charainfo::CharaInfo {
public:
    ActiveCharaInfo() = default;
    virtual ~ActiveCharaInfo() = default;
    std::vector<std::shared_ptr<::cw::character::Character>> m_cards{};
    std::shared_ptr<::cw::frame::Frame> m_parent{};
    bool m_closed{};
    void initialize(std::shared_ptr<::cw::frame::Frame> arg_parent,
                    std::shared_ptr<::cw::sprite::card::CWPyCard> arg_selection = {});
    virtual void update_debug();
    bool _update_list();
};

class DeckDialog : public ::cw::lib::Object,
                   public ::cw::dialog::DialogPresentationProvider {
public:
    DeckDialog() = default;
    virtual ~DeckDialog() = default;
    ::cw::lib::Value m__cancel_btn{};
    ::cw::lib::Value m__left_pc_btn{};
    ::cw::lib::Value m__left_toggle{};
    ::cw::lib::Value m__reg_type{};
    std::vector<::cw::lib::String> m__reg_types{};
    ::cw::lib::Value m__register_btn{};
    ::cw::lib::Value m__right_pc_btn{};
    ::cw::lib::Value m__right_toggle{};
    std::vector<std::shared_ptr<::cw::lib::Window>> m_area_buttons{};
    ::cw::lib::Value m_btn_font{};
    std::shared_ptr<::cw::character::Character> m_ccard{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Integer m_pocket{};
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> m_registrations{};
    ::cw::lib::Value m_scrolled{};
    std::vector<std::tuple<std::optional<std::shared_ptr<::cw::lib::Control>>, std::optional<std::shared_ptr<::cw::lib::Control>>, std::optional<std::shared_ptr<::cw::lib::Control>>>> m_small_buttons{};
    ::cw::lib::Value m_title{};
    ::cw::lib::Value m_vbox{};
    ::cw::lib::Value m_visible_registrations{};
    ::cw::lib::Value m_watermark{};
    std::shared_ptr<::cw::lib::Window> m_window_parent{};
    // 所有権を持たない CharaInfo の参照。共通のモーダルの子の寿命は常に親より短い。
    CharaInfo* m_source_info{};
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> m_visible_registration_values{};
    ::cw::lib::String m_reg_type{};
    ::cw::lib::String m_title_text{};
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> m_last_search{};
    std::shared_ptr<CharainfoUiBackend> m_ui_backend{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};
    bool m_ui_invalidated{true};
    bool m_modal_ended{};
    bool m_operation_pending{};
    // UP_WIN に依存しない論理画素。Python のスクロール単位は 30。
    ::cw::lib::Integer m_scroll_y{};
    enum class RowOperation { Rename, Delete, Update };
    using RowTarget = std::pair<std::size_t, RowOperation>;
    static constexpr int scrollbar_control_id = -1;
    [[nodiscard]] static int row_control_id(std::size_t row, RowOperation operation);
    [[nodiscard]] static std::optional<RowTarget> decode_row_control(int index) noexcept;
    using FocusTarget = std::pair<::cw::dialog::DialogPresentationAction, int>;
    std::optional<FocusTarget> m_focus_target, m_pointer_target, m_space_target;
    bool m_keyboard_focus{}, m_pointer_inside{}, m_enter_held{}, m_pointer_stale{};
    int m_type_repeat{};
    ::cw::dialog::ButtonRepeatTimer m_type_repeat_timer;
    ::cw::dialog::DialogPresentation m_cached_presentation;
    ::cw::lib::Real m_publication_scale{-1};
    std::vector<::cw::lib::ui::FontStyle> m_publication_fonts;
    bool m_publication_pending{}, m_publication_can_change{};
    virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role, ::cw::lib::Integer pixels) const;
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point deck_input_time() const noexcept;
    [[nodiscard]] ::cw::lib::Integer maximum_scroll() const noexcept;
    [[nodiscard]] std::vector<FocusTarget> focus_targets() const;
    [[nodiscard]] bool target_enabled(FocusTarget target) const;
    void focus_next(int direction);
    void reveal_focus();
    void invalidate_presentation();
    void retire_pointer_input();
    [[nodiscard]] bool can_change_character() const;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Integer arg_pocket, std::shared_ptr<::cw::character::Character> arg_ccard);
    void OnSize(std::shared_ptr<::cw::lib::Window> arg_event);
    void _layout();
    void _do_load_pc(::cw::lib::Value arg_target);
    void OnLoadLeftPC(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLoadRightPC(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void _rebuild_area_buttons();
    void _layout_area_buttons();
    bool _check_headers_for_limited_beast(::cw::lib::Value arg_headers);
    bool _match_header(::cw::lib::Value arg_header, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_scenario, ::cw::lib::String arg_author, bool arg_ms, bool arg_ma);
    void _remove_small_buttons();
    void OnRegTypeLeft(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRegTypeRight(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_change_reg_name(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx);
    void _on_delete_registration(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx);
    void _on_update_registration(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx);
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> _search_item_sources(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> arg_items, std::optional<::cw::lib::String> arg_rtype, bool arg_match_scenario, bool arg_match_author);
    void OnArea(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRegister(std::shared_ptr<::cw::lib::Window> arg_event, std::optional<::cw::lib::String> arg_preset_name, std::optional<::cw::lib::String> arg_preset_type, bool replace_existing = false);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                    int index, float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class DescPanel : public ::cw::lib::Object {
public:
    DescPanel() = default;
    virtual ~DescPanel() = default;
    bool m__editable{};
    std::function<void()> m_on_edit{};
    std::function<void()> m_on_cursor_changed{};
    ::cw::lib::Value m__ratey{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_csize{};
    ::cw::lib::String m_text{};
    ::cw::lib::Value m_watermark{};
    ::cw::lib::String m_parent_path{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Value arg_ccard, bool arg_editable);
    void update_cursor();
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void _init_view();
    void draw(bool arg_update);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String get_detailtext();
};

class EditButton : public ::cw::lib::Object {
public:
    EditButton() = default;
    virtual ~EditButton() = default;
    ::cw::lib::Value m_has_separator{};
    ::cw::lib::Value m_name{};
    bool m_negaflag{};
    ::cw::lib::Value m_subrect{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_textpos{};
    ::cw::lib::Integer m_type{};
    void initialize(::cw::lib::String arg_name, ::cw::lib::Integer arg_btype, bool arg_has_separator);
};

class EditPanel : public ::cw::lib::Object {
public:
    EditPanel() = default;
    virtual ~EditPanel() = default;
    bool m__destroy{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_csize{};
    std::vector<std::shared_ptr<::cw::dialog::charainfo::EditButton>> m_headers{};
    ::cw::lib::Value m_list{};
    ::cw::lib::Integer m_selected{};
    ::cw::lib::Value m_watermark{};
    std::shared_ptr<::cw::lib::Window> m_parent_window{};
    std::vector<std::shared_ptr<::cw::character::Player>> m_charalist{};
    // StandbyPartyCharaInfo の編集結果を対応する宿帳ヘッダへ反映するための元順序。
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_parentheaders{};
    bool m_party_mode{};
    ::cw::lib::Integer m_parent_index{};
    ::cw::lib::Point m_mousepos{};
    CharaInfo* m_charainfo_owner{};
    bool m_edit_child_open{};
    std::weak_ptr<int> m_owner_lifetime{};
    void initialize(::cw::lib::Value arg_parent, std::vector<::cw::lib::Value> arg_mlist, std::shared_ptr<::cw::character::Player> arg_ccard);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<std::shared_ptr<::cw::character::Player>> get_charalist();
    void update_charalist(std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeave(std::shared_ptr<::cw::lib::Window> arg_event);
    void update_negaflag();
    void OnMove(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_mousepos(std::shared_ptr<::cw::lib::Window> arg_mousepos);
    void draw_header(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::dialog::charainfo::EditButton> arg_header);
    void up();
    void down();
    void draw(bool arg_update);
    ::cw::lib::String get_detailtext();
};

class HistoryPanel : public ::cw::lib::Object {
public:
    HistoryPanel() = default;
    virtual ~HistoryPanel() = default;
    bool m__editable{};
    std::function<void()> m_on_edit{};
    std::function<void()> m_on_cursor_changed{};
    ::cw::lib::Value m__ratey{};
    ::cw::lib::Value m_black{};
    ::cw::lib::Value m_black_s{};
    ::cw::lib::Value m_bronze{};
    ::cw::lib::Value m_bronze_s{};
    ::cw::lib::Value m_ccard{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> m_coupons{};
    ::cw::lib::Value m_csize{};
    ::cw::lib::Value m_gold{};
    ::cw::lib::Value m_gold_s{};
    ::cw::lib::Value m_silver{};
    ::cw::lib::Value m_silver_s{};
    ::cw::lib::Value m_watermark{};
    ::cw::lib::String m_parent_path{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Value arg_ccard, bool arg_editable);
    std::shared_ptr<::cw::lib::Bitmap> _get_bmps(::cw::lib::String arg_name);
    void update_cursor();
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw(bool arg_update);
    void _init_view();
    bool is_hidden(::cw::lib::String arg_coupon);
    bool is_timed(::cw::lib::String arg_coupon);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String _append_text(::cw::lib::String arg_text, ::cw::lib::Integer arg_value);
    ::cw::lib::String get_detailtext();
};

class HoldAll : public ::cw::lib::Object {
public:
    HoldAll() = default;
    virtual ~HoldAll() = default;
    bool m_negaflag{};
    ::cw::lib::Value m_subrect{};
    void initialize();
};

class StandbyCharaInfo : public ::cw::dialog::charainfo::CharaInfo {
    friend class CommonCharainfoUiBackend;
public:
    StandbyCharaInfo() = default;
    virtual ~StandbyCharaInfo() = default;
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_headers{};
    StandbySelectionCallback m_selection_changed;
    bool change_character(::cw::lib::Integer delta);
protected:
    virtual void save_current_character();
    virtual ::cw::lib::Value read_character(const std::shared_ptr<::cw::header::AdventurerHeader>& header);
private:
    std::shared_ptr<void> m_character_change_token;
public:
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers, ::cw::lib::Value arg_ccard, ::cw::lib::Integer arg_index, ::cw::lib::Callback arg_redrawfunc, bool arg_is_playingscenario, std::optional<std::shared_ptr<::cw::data::Party>> arg_party);
};

class StandbyPartyCharaInfo : public ::cw::dialog::charainfo::StandbyCharaInfo {
public:
    StandbyPartyCharaInfo() = default;
    virtual ~StandbyPartyCharaInfo() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::header::PartyHeader> arg_partyheader, ::cw::lib::Callback arg_redrawfunc);
};

class StatusPanel : public ::cw::lib::Object {
public:
    StatusPanel() = default;
    virtual ~StatusPanel() = default;
    bool m__editable{};
    std::function<void()> m_on_edit{};
    std::function<void()> m_on_cursor_changed{};
    ::cw::lib::Value m__ratey{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_csize{};
    ::cw::lib::Value m_list{};
    ::cw::lib::Value m_watermark{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_charalist{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::character::Character>> arg_mlist, std::shared_ptr<::cw::character::Character> arg_ccard, bool arg_editable);
    void update_cursor();
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void _init_view();
    void draw(bool arg_update);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String get_detailtext();
    std::tuple<::cw::lib::Color, ::cw::lib::String> _get_life();
    ::cw::lib::String _get_poison();
    ::cw::lib::String _get_paralyze();
    ::cw::lib::String _get_petrified();
    std::tuple<::cw::lib::String, ::cw::lib::String> _get_mentality();
    ::cw::lib::String _get_bind();
    ::cw::lib::String _get_silence();
    ::cw::lib::String _get_faceup();
    ::cw::lib::String _get_antimagic();
    ::cw::lib::Integer _draw_status(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_msg, ::cw::lib::String arg_imgname, ::cw::lib::Integer arg_height);
    std::tuple<::cw::lib::Color, std::shared_ptr<::cw::lib::Bitmap>, ::cw::lib::String> _get_enhance(::cw::lib::String arg_enhname, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_dur, ::cw::lib::String arg_enhimage, ::cw::lib::String arg_pnlimage);
    ::cw::lib::Integer _draw_enhance(std::shared_ptr<::cw::lib::Window> arg_dc, ::cw::lib::String arg_enhname, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_dur, ::cw::lib::String arg_enhimage, ::cw::lib::String arg_pnlimage, ::cw::lib::Integer arg_height);
};

class TitlePanel : public ::cw::lib::Object {
public:
    TitlePanel() = default;
    virtual ~TitlePanel() = default;
    bool m_is_playingscenario{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_notebook{};
    ::cw::lib::Value m_text{};
    ::cw::lib::String m_text_value{};
    void initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::lib::Window> arg_notebook);
    void draw(bool arg_update);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
};

class TopPanel : public ::cw::lib::Object {
public:
    TopPanel() = default;
    virtual ~TopPanel() = default;
    ::cw::lib::Value m__animtimer{};
    ::cw::lib::String m_age{};
    ::cw::lib::Value m_baselevel{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_csize{};
    ::cw::lib::String m_ep{};
    std::shared_ptr<::cw::header::RaceHeader> m_race{};
    std::optional<::cw::lib::Callback> m_redrawfunc{};
    ::cw::lib::String m_sex{};
    ::cw::lib::Value m_wing{};
    ::cw::lib::Value m_yadodir{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::String m_detail_text{};
    bool m_animating{};
    void initialize(::cw::lib::Value arg_parent, ::cw::lib::Value arg_ccard, std::optional<::cw::lib::Callback> arg_redrawfunc);
    bool _has_animated_images();
    void OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw(bool arg_update);
    ::cw::lib::String get_detailtext();
};

class ItemPanel : public ::cw::dialog::charainfo::CardPanel {
public:
    ItemPanel() = default;
    virtual ~ItemPanel() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard);
};

class SkillPanel : public ::cw::dialog::charainfo::CardPanel {
public:
    SkillPanel() = default;
    virtual ~SkillPanel() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard);
};

class BeastPanel : public ::cw::dialog::charainfo::CardPanel {
public:
    BeastPanel() = default;
    virtual ~BeastPanel() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event);
};

extern ::cw::lib::Value g__N;
extern ::cw::lib::Value g__T;

void CharaInfo___init___onfocus(std::shared_ptr<::cw::lib::Window> arg_event);
::cw::lib::Value CharaInfo___init___lambda_line_164();
::cw::lib::Value CharaInfo___init___lambda_line_165();
::cw::lib::Value CharaInfo___init___lambda_line_166();
::cw::lib::Value CharaInfo___init___lambda_line_167();
::cw::lib::Value CharaInfo___init___lambda_line_168();
void CharaInfo___init___onfocus_2(std::shared_ptr<::cw::lib::Window> arg_event);
void CharaInfo__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void CharaInfo_OnDebugMode_func(::cw::lib::Value arg_self);
void CharaInfo_OnDebugMode_func_func(::cw::lib::Value arg_self);
void HistoryPanel_OnLeftUp_func(::cw::lib::Value arg_panel);
void HistoryPanel_OnLeftUp_func_func(::cw::lib::Value arg_panel);
void EditPanel_OnLeftUp_func(std::shared_ptr<::cw::lib::Panel> arg_panel);
void EditPanel_OnLeftUp_func_2(std::shared_ptr<::cw::lib::Panel> arg_panel);
void EditPanel_OnLeftUp_func2(std::shared_ptr<::cw::lib::Panel> arg_panel, std::shared_ptr<::cw::character::Character> arg_ccard);
void EditPanel_update_charalist_func(std::vector<::cw::lib::Value> arg_parentheaders, std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist);
void EditPanel_update_charalist_func2(::cw::lib::Integer arg_index, std::vector<::cw::lib::Value> arg_headers, std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist);
::cw::lib::Value DeckDialog___init___lambda_line_2362(::cw::lib::Value arg_evt);
::cw::lib::Value DeckDialog___init___lambda_line_2385(::cw::lib::Value arg_evt);
::cw::lib::Value DeckDialog___init___lambda_line_2388(::cw::lib::Value arg_evt);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2603(::cw::lib::Value arg_r);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2619(::cw::lib::Value arg_idx);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2619_2(::cw::lib::Value arg_evt);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2622(::cw::lib::Value arg_idx);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2622_2(::cw::lib::Value arg_evt);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2625(::cw::lib::Value arg_idx);
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2625_2(::cw::lib::Value arg_evt);
std::tuple<::cw::lib::String, ::cw::lib::String> DeckDialog__search_item_sources__hdr_key(::cw::lib::Value arg_hdr);
::cw::lib::String DeckDialog_OnArea__ppath_to_type(std::optional<::cw::lib::String> arg_ppath, ::cw::lib::String arg_fallback_type);
::cw::lib::Color get_bgcolor(::cw::lib::Value arg_ccard);
void apply_bgcolor(std::shared_ptr<::cw::lib::Window> arg_currentpanel, ::cw::lib::Value arg_ccard);
void main();
}

