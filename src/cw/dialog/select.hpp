#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// D:/codes/cwxeditor_code/cardwirthpy-reboot/cw/dialog/select.py
// の選択状態遷移の移植。wx/SDL コントロー
// ルは共通の互換 Window 型で表し、ネイテ
// ィブ描画は select.cpp に明示する。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/filter_input.hpp"
#include "cw/dialog/deferred_update.hpp"
#include "cw/fwd.hpp"

namespace cw::dialog::select {
// 宿・パーティー・冒険者の絞り込み選択肢で、一つのポップアップ状態と描画器を共有する。
struct SelectChoiceState {
    int kind{};
    int cursor{};
};
// 操作上のフォーカスは共通モデルが管理し、OS のウィジェットには持たせない。
struct SelectInputState {
    ::cw::dialog::DeferredDialogUpdate narrow_update;
    bool top_focus{};
    bool initialized{};
    ::cw::dialog::DialogPresentationAction action{::cw::dialog::DialogPresentationAction::Accept};
    int index{};
    ::cw::dialog::textedit::FilterInput filter;
};
class MultiViewSelect : public ::cw::lib::Object {
public:
    MultiViewSelect() = default;
    virtual ~MultiViewSelect() = default;
    ::cw::lib::Value m__enterid{};
    ::cw::lib::Value m__lines{};
    ::cw::lib::ValueMap m__player_animation_regions{};
    ::cw::lib::ValueMap m__player_image_cache{};
    bool m__processing{};
    ::cw::lib::Value m__views{};
    ::cw::lib::Integer m_index{};
    std::vector<::cw::lib::Value> m_list{};
    ::cw::lib::Integer m_enterid_typed{};
    ::cw::lib::Integer m_max_views_typed{1};
    ::cw::lib::Integer m_lines_typed{2};
    bool m_decision_pending{};
    bool m_finished{};
    ::cw::lib::Integer m_modal_result{-1};
    std::uintptr_t m_native_handle{};
    std::uintptr_t m_native_parent_handle{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_viewbtn{};
    ::cw::lib::Integer m_views{};
    // Select の wx が保持する状態。生成された C++ ABI
    // は Python の多重継承を直接表現しないため、ここで保持する。
    std::shared_ptr<::cw::lib::Window> m_toppanel{};
    std::vector<std::shared_ptr<::cw::lib::Window>> m_buttonlist{};
    ::cw::lib::Integer m_clickmode{};
    bool m_cwpy_debug{};
    ::cw::lib::Integer m_downbutton{-1};
    bool m_reserved_narrowcondition{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::Integer arg_enterid, ::cw::lib::Integer arg_views, bool arg_show_multi, ::cw::lib::Integer arg_lines);
    bool can_clickside();
    virtual bool can_clickcenter();
    void OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
    void OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void change_view();
    void enable_btn();
    ::cw::lib::Integer get_page();
    ::cw::lib::Integer get_pagecount();
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> _get_player_images(std::shared_ptr<::cw::header::AdventurerHeader> arg_header);
    void _register_player_animation_region(::cw::lib::Rect arg_rect, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> arg_images);
    bool _refresh_player_animation(::cw::lib::Value arg_key);
    void save_views(bool arg_multi);
};

class PartySelect : public ::cw::lib::Object,
                    public std::enable_shared_from_this<PartySelect>,
                    public ::cw::dialog::DialogPresentationProvider
{
public:
    PartySelect() = default;
    SelectChoiceState m_filter_choice;
    SelectInputState m_input;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers mods) override {m_input.filter.modifiers=mods;}
    void dialog_presentation_pointer_cancel() override {m_input.filter.dragging=false;request_dialog_presentation_refresh();}
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    virtual ~PartySelect() = default;
    ::cw::lib::Value m__bg{};
    std::map<::cw::lib::String, std::tuple<std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::optional<std::shared_ptr<::cw::header::ScenarioHeader>>, std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> m__image_cache{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_editbtn{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_infobtn{};
    ::cw::lib::Value m_list{};
    std::vector<::cw::lib::String> m_names{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_partyrecordbtn{};
    ::cw::lib::Value m_sort{};
    ::cw::lib::Value m_sort_label{};
    std::vector<::cw::lib::Value> m_sortkeydown{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_viewbtn{};
    // MultiViewSelect/Select の状態をまとめて保持する。Python
    // は多重継承を使うが、共通 C++ ABI は状態をここに明示する。
    std::vector<std::shared_ptr<::cw::header::PartyHeader>> m_source_list{};
    std::vector<std::shared_ptr<::cw::header::PartyHeader>> m_typed_list{};
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    std::map<::cw::lib::String, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>>> m_player_image_cache{};
    ::cw::lib::ValueMap m_player_animation_regions{};
    std::shared_ptr<::cw::lib::Window> m_toppanel_window{};
    std::vector<std::shared_ptr<::cw::lib::Window>> m_buttonlist{};
    std::shared_ptr<::cw::lib::Window> m_left2btn{};
    std::shared_ptr<::cw::lib::Window> m_leftbtn{};
    std::shared_ptr<::cw::lib::Window> m_rightbtn{};
    std::shared_ptr<::cw::lib::Window> m_right2btn{};
    ::cw::lib::Value m_narrow{};
    ::cw::lib::Value m_narrow_type{};
    ::cw::lib::Integer m_views{1};
    ::cw::lib::Integer m_max_views{8};
    ::cw::lib::Integer m_lines{2};
    ::cw::lib::Integer m_enterid{1};
    ::cw::lib::Integer m_clickmode{};
    ::cw::lib::Integer m_downbutton{-1};
    bool m_processing{};
    bool m_cwpy_debug{};
    std::shared_ptr<int> m_debug_lifetime{std::make_shared<int>(0)};
    bool m_decision_pending{};
    bool m_finished{};
    ::cw::lib::Integer m_modal_result{-1};
    bool m_presentation_additionals{};
    std::uintptr_t m_native_handle{};
    std::uintptr_t m_native_parent_handle{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    bool can_clickside();
    void OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
    void OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void change_view();
    ::cw::lib::Integer get_page();
    ::cw::lib::Integer get_pagecount();
    void save_views(bool arg_multi);
    void update_additionals();
    virtual void _add_topsizer();
    void _on_narrowcondition();
    void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    bool queue_debug_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> change);
    void update_debug();
    virtual void update_narrowcondition();
    std::shared_ptr<::cw::lib::Bitmap> _get_bg();
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> _get_player_images(std::shared_ptr<::cw::header::AdventurerHeader> arg_header);
    void _register_player_animation_region(::cw::lib::Rect arg_rect, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> arg_images);
    bool _refresh_player_animation(::cw::lib::Value arg_key);
    void OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSort(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickInfoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickEditBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickPartyRecordBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    std::optional<std::shared_ptr<::cw::header::PartyHeader>> get_selected();
    std::shared_ptr<::cw::header::PartyHeader> take_selected_party();
    static bool confirm_missing_scenario(const std::shared_ptr<::cw::frame::Frame>& frame,
        const ::cw::lib::String& title, const ::cw::lib::String& message);
    static bool recover_missing_scenario(const std::function<bool()>& confirm,
        const std::function<void()>& load_party, const std::function<void()>& restore_log,
        const std::function<void()>& return_to_inn);
    void update_standbys(std::shared_ptr<::cw::header::PartyHeader> arg_selected);
    bool can_clickcenter();
    virtual void enable_btn();
    void draw(bool arg_update);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,
                                 bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class PlayerSelect : public ::cw::lib::Object,
                     public std::enable_shared_from_this<PlayerSelect>,
                     public ::cw::dialog::DialogPresentationProvider
{
public:
    PlayerSelect() = default;
    SelectChoiceState m_filter_choice;
    SelectInputState m_input;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers mods) override {m_input.filter.modifiers=mods;}
    void dialog_presentation_pointer_cancel() override {m_input.filter.dragging=false;request_dialog_presentation_refresh();}
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    virtual ~PlayerSelect() = default;
    ::cw::lib::Value m__bg{};
    ::cw::lib::ValueMap m__player_animation_regions{};
    std::map<::cw::lib::String, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>>> m__player_image_cache{};
    ::cw::lib::Value m__player_selection_basebmp{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m__player_target_overlay{};
    ::cw::lib::ValueMap m__player_target_rects{};
    bool m__processing{};
    ::cw::lib::Value m_addbtn{};
    // 内容を隠した識別子。キュー内の古い完了処理で、新しい要求を解除できないようにする。
    std::shared_ptr<void> m_enrollment_token{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_exbtn{};
    ::cw::lib::Value m_header{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_infobtn{};
    bool m_isalbum{};
    std::vector<::cw::lib::Value> m_list{};
    ::cw::lib::Value m_newbtn{};
    ::cw::lib::Value m_point{};
    ::cw::lib::Value m_sort{};
    ::cw::lib::Value m_sort_label{};
    std::vector<::cw::lib::Value> m_sortkeydown{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_viewbtn{};
    // Python の MultiViewSelect/Select 多重継承を、型付き状態として保持する。
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_source_list{};
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_typed_list{};
    std::shared_ptr<::cw::lib::Window> m_toppanel_window{};
    std::vector<std::shared_ptr<::cw::lib::Window>> m_buttonlist{};
    std::shared_ptr<::cw::lib::Window> m_left2btn{};
    std::shared_ptr<::cw::lib::Window> m_leftbtn{};
    std::shared_ptr<::cw::lib::Window> m_rightbtn{};
    std::shared_ptr<::cw::lib::Window> m_right2btn{};
    ::cw::lib::Value m_narrow{};
    ::cw::lib::Value m_narrow_type{};
    ::cw::lib::Integer m_views{1};
    ::cw::lib::Integer m_max_views{10};
    ::cw::lib::Integer m_lines{2};
    ::cw::lib::Integer m_enterid{1};
    ::cw::lib::Integer m_clickmode{};
    ::cw::lib::Integer m_downbutton{-1};
    bool m_cwpy_debug{};
    std::shared_ptr<int> m_debug_lifetime{std::make_shared<int>(0)};
    bool m_decision_pending{};
    bool m_finished{};
    ::cw::lib::Integer m_modal_result{-1};
    bool m_presentation_additionals{};
    std::uintptr_t m_native_handle{};
    std::uintptr_t m_native_parent_handle{};
    std::vector<bool> m_extension_enabled{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_bind_native_ui = true);
    void save_views(bool arg_multi);
    void update_additionals();
    virtual void _add_topsizer();
    void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    bool queue_debug_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> change);
    void update_debug();
    void _on_narrowcondition();
    virtual void update_narrowcondition();
    void OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void enable_btn();
    void OnSort(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual bool can_clickcenter();
    bool can_clickside();
    void OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
    void OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void change_view();
    ::cw::lib::Integer get_page();
    ::cw::lib::Integer get_pagecount();
    bool _select_player_in_current_page();
    bool _change_player_in_current_page(::cw::lib::Integer arg_index);
    virtual void OnClickNewBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _create_normal();
    virtual void _create_debug();
    void _create_common(::cw::lib::Value arg_dlg);
    std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> get_selected();
    void update_standbys(std::shared_ptr<::cw::header::AdventurerHeader> arg_selected);
    virtual void OnClickAddBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    static bool _add(std::shared_ptr<::cw::header::AdventurerHeader> arg_header);
    bool queue_enrollment(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game,
        std::function<bool()> work);
    bool queue_character_refresh(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game,
        std::shared_ptr<::cw::header::AdventurerHeader> original,
        std::function<std::shared_ptr<::cw::header::AdventurerHeader>()> work);
    static bool _random_team();
    static std::shared_ptr<::cw::header::AdventurerHeader> choose_random_member(
        std::vector<std::tuple<::cw::lib::Integer,std::shared_ptr<::cw::header::AdventurerHeader>>> needs,
        const std::function<std::vector<::cw::lib::Value>(std::vector<::cw::lib::Value>)>& shuffle,
        const std::function<::cw::lib::Integer(::cw::lib::Integer)>& roll);
    static std::vector<std::tuple<::cw::lib::Integer,std::shared_ptr<::cw::header::AdventurerHeader>>> calculate_needs(
        const std::vector<std::shared_ptr<::cw::header::AdventurerHeader>>& candidates,
        const std::vector<std::pair<::cw::lib::Integer,::cw::lib::String>>& members,
        ::cw::lib::Integer member_count, const std::vector<::cw::lib::String>& talents);
    void OnClickExBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void grow_adventurer();
    void delete_adventurer();
    enum class AdventurerChange { Grow, Retire, Delete, DeleteAlbum };
    void _move_allcards(std::shared_ptr<::cw::header::AdventurerHeader> arg_header);
    void _delete_adventurer(std::shared_ptr<::cw::header::AdventurerHeader> arg_header,
                            bool retirement = false);
    void select_partyrecord();
    void random_team();
    void create_randomadventurer();
    std::optional<::cw::lib::String> input_random_name(
        ::cw::lib::String initial, std::optional<::cw::lib::Callback> automatic);
    void OnClickInfoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void update_character();
    std::function<void(std::shared_ptr<::cw::header::AdventurerHeader>)> information_selection_callback();
    ::cw::lib::Callback information_refresh_callback();
    std::vector<std::tuple<::cw::lib::Integer, std::shared_ptr<::cw::header::AdventurerHeader>>> calc_needs(std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_mlist);
    std::shared_ptr<::cw::lib::Bitmap> _get_bg();
    void draw(bool arg_update);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,
                                 bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    // トランザクションの境界は一つとし、確認と通知には常に共通 UI を使う。
    virtual bool commit_adventurer_change(
        const std::shared_ptr<::cw::header::AdventurerHeader>& header, AdventurerChange change);
    void run_adventurer_change(const std::shared_ptr<::cw::header::AdventurerHeader>& header,
                               AdventurerChange change, const ::cw::lib::String& prompt);
private:
    bool queue_selector_work(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> work,
        std::function<void(PlayerSelect&)> finish);
    std::shared_ptr<void> m_adventurer_change_token;
    std::shared_ptr<void> m_information_open_token;
    std::shared_ptr<int> m_information_lifetime{std::make_shared<int>(0)};
};

class PlayerSelect_Pocket : public ::cw::lib::Object {
public:
    PlayerSelect_Pocket() = default;
    virtual ~PlayerSelect_Pocket() = default;
};

class Select : public ::cw::lib::Object, public std::enable_shared_from_this<Select> {
public:
    ::cw::dialog::DeferredDialogUpdate m_narrow_update;
    Select() = default;
    virtual ~Select() = default;
    inline static ::cw::lib::Integer s_index{};
    inline static std::vector<::cw::lib::Value> s_list{};
    inline static std::shared_ptr<::cw::lib::Panel> s_toppanel{};
    ::cw::lib::Value m__downbutton{};
    ::cw::lib::Point m_pointer_position{200,185};
    std::shared_ptr<::cw::lib::Window> m_focused_button{};
    virtual void process_navigation_command(bool left);
    bool m__processing{};
    bool m__reserved_narrowconditin{};
    ::cw::lib::Value m_accels{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_addctrlbtn{};
    std::vector<std::shared_ptr<::cw::lib::Control>> m_additionals{};
    std::vector<std::shared_ptr<::cw::lib::Window>> m_buttonlist{};
    ::cw::lib::Integer m_clickmode{};
    bool m_cwpy_debug{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_keyword_label{};
    std::shared_ptr<::cw::lib::Window> m_left2btn{};
    ::cw::lib::Value m_left2keyid{};
    std::shared_ptr<::cw::lib::Window> m_leftbtn{};
    ::cw::lib::Value m_leftkeyid{};
    std::vector<::cw::lib::Value> m_list{};
    bool m_decision_pending{};
    bool m_finished{};
    ::cw::lib::Integer m_modal_result{-1};
    std::uintptr_t m_native_handle{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_narrow{};
    ::cw::lib::Value m_narrow_label{};
    ::cw::lib::Value m_narrow_type{};
    ::cw::lib::Value m_nextid{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_previd{};
    std::shared_ptr<::cw::lib::Window> m_right2btn{};
    ::cw::lib::Value m_right2keyid{};
    std::shared_ptr<::cw::lib::Window> m_rightbtn{};
    ::cw::lib::Value m_rightkeyid{};
    ::cw::lib::Value m_topsizer{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name);
    void _bind();
    bool is_processing();
    void OnPrevButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNextButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMotion(std::shared_ptr<::cw::lib::Window> arg_evt);
    void _update_mousepos();
    virtual void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_evt);
    virtual void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void index_changed();
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectBase(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event);
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void draw(bool arg_update);
    void draw3(std::shared_ptr<::cw::lib::Window> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_dest, bool arg_update);
    void _do_layout();
    void _add_topsizer();
    void set_panelsizer();
    void _disable_btn(std::optional<std::vector<std::shared_ptr<::cw::lib::Control>>> arg_enables);
    void _enable_btn(std::optional<std::vector<std::shared_ptr<::cw::lib::Control>>> arg_disables);
    bool can_clickcenter();
    bool can_clickside();
    void _init_narrowpanel(std::vector<::cw::lib::String> arg_choices, ::cw::lib::String arg_narrowtext, ::cw::lib::Integer arg_narrowtype, bool arg_tworows);
    void OnFind(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNarrowCondition(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_narrowcondition();
    void create_addctrlbtn(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::lib::Bitmap> arg_bg, bool arg_show);
    void update_additionals();
    void update_additionals_impl(bool arg_show, std::vector<std::shared_ptr<::cw::lib::Control>> arg_additionals);
    void append_addctrlaccelerator(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_seq);
    void OnToggleAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event);
    void _additional_controls();
    void update_narrowcondition();
};

class YadoSelect : public ::cw::lib::Object,
                   public std::enable_shared_from_this<YadoSelect>,
                   public ::cw::dialog::DialogPresentationProvider {
public:
    YadoSelect() = default;
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    SelectChoiceState m_filter_choice;
    SelectInputState m_input;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers mods) override {m_input.filter.modifiers=mods;}
    void dialog_presentation_pointer_cancel() override {m_input.filter.dragging=false;request_dialog_presentation_refresh();}
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    virtual ~YadoSelect() = default;
    ::cw::lib::Value m__bg{};
    ::cw::lib::Value m__classic{};
    ::cw::lib::Value m__imgpaths{};
    ::cw::lib::Value m__isshortcuts{};
    std::optional<::cw::lib::String> m__lastbillskindir{};
    ::cw::lib::Value m__list{};
    ::cw::lib::Value m__list2{};
    ::cw::lib::Value m__names{};
    ::cw::lib::ValueMap m__player_animation_regions{};
    bool m__processing{};
    ::cw::lib::Value m__skins{};
    std::vector<bool> m_classic{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_exbtn{};
    std::vector<bool> m_extension_enabled{};
    // Python の list[list[ImageInfo]] に対応する型付きの橋渡し。
    // m_imgpaths は生成された Value 型のメンバーとの互換性のためだけに残す。
    std::vector<::cw::lib::Value> m_imgpaths{};
    std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> m_imgpaths_typed{};
    ::cw::lib::Integer m_index{};
    std::vector<::cw::lib::String> m_isshortcuts{};
    std::vector<::cw::lib::String> m_list{};
    std::vector<std::vector<::cw::lib::String>> m_list2{};
    std::vector<::cw::lib::String> m_names{};
    ::cw::lib::Value m_newbtn{};
    ::cw::lib::Value m_okbtn{};
    std::vector<::cw::lib::String> m_skins{};
    // Python は元の一覧（_names/_list など）
    // と絞り込み後の表示一覧を分離する。型付きの保持欄でその区
    // 別を保ち、互換 Value に一覧要素の型を失わせない。
    std::vector<bool> m__classic_typed{};
    std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> m__imgpaths_typed{};
    std::vector<::cw::lib::String> m__isshortcuts_typed{};
    std::vector<::cw::lib::String> m__list_typed{};
    std::vector<std::vector<::cw::lib::String>> m__list2_typed{};
    std::vector<::cw::lib::String> m__names_typed{};
    std::vector<::cw::lib::String> m__skins_typed{};
    ::cw::lib::Value m_sort{};
    ::cw::lib::Value m_sort_label{};
    std::vector<::cw::lib::Value> m_sortkeydown{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_viewbtn{};
    // Python では継承する MultiViewSelect の状態を、ここにまとめて保持する。
    ::cw::lib::Integer m_views{1};
    ::cw::lib::Integer m_max_views{6};
    ::cw::lib::Integer m_lines{3};
    ::cw::lib::Integer m_enterid{};
    ::cw::lib::Integer m_clickmode{};
    ::cw::lib::Integer m_downbutton{-1};
    bool m_decision_pending{};
    bool m_finished{};
    ::cw::lib::Integer m_modal_result{-1};
    std::uintptr_t m_native_handle{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_narrow{};
    ::cw::lib::Value m_narrow_type{};
    std::shared_ptr<::cw::lib::Window> m_toppanel_window{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void update_additionals();
    void _add_topsizer();
    void _on_narrowcondition();
    void update_narrowcondition();
    std::shared_ptr<::cw::lib::Bitmap> _get_bg();
    std::shared_ptr<::cw::lib::Bitmap> _get_draw_bg();
    void OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSort(std::shared_ptr<::cw::lib::Window> arg_event);
    void _sort_objs(std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>>& arg_objs);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>> _list_to_obj();
    void _obj_to_list(std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>> arg_objs);
    void save_views(bool arg_multi);
    void index_changed();
    bool can_clickcenter();
    void enable_btn();
    void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickExBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void rename_yado();
    void copy_yado();
    void trasnfer_yadodata();
    void delete_yado();
    void OnClickNewBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _conv_yado();
    using ConversionFolderRequest=std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&,::cw::lib::documents::Completion)>;
    bool request_conversion_folder(ConversionFolderRequest start={});
    void cancel_conversion_folder();
    [[nodiscard]] bool conversion_folder_pending() const noexcept {return bool(m_conversion_folder_token);}
    ::cw::lib::String m_conversion_folder_error;
    void _convert_current();
    void draw(bool arg_update);
    void conv_yado(::cw::lib::String arg_path, bool arg_ok, bool arg_moveconverted, ::cw::lib::String arg_deletepath);
    void unconv_yado();
    void update_list(::cw::lib::String arg_yadodir, bool arg_clear_narrowcondition);
    std::tuple<std::vector<::cw::lib::String>, std::vector<::cw::lib::String>, std::vector<std::vector<::cw::lib::String>>, std::vector<::cw::lib::String>, std::vector<bool>, std::vector<::cw::lib::String>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> get_yadolist();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(
        ::cw::dialog::DialogPresentationAction arg_action,
        int arg_index) override;
    void dialog_presentation_text(const std::string& arg_text,
                                  bool arg_composition) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual void convert_acquired_yado(const ::cw::lib::documents::Document& document);
    virtual void report_conversion_error(const ::cw::lib::String& error);
    virtual bool confirm_yado_conversion(const ::cw::lib::String& path);
private:
    std::shared_ptr<int> m_conversion_folder_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_conversion_folder_ticket;
    ::cw::lib::String m_conversion_root;
    void finish_conversion_folder(const std::shared_ptr<int>& token,::cw::lib::documents::Result result);
};

class _YadoObj : public ::cw::lib::Object {
public:
    _YadoObj() = default;
    virtual ~_YadoObj() = default;
    ::cw::lib::Value m_advnames{};
    ::cw::lib::Value m_classic{};
    ::cw::lib::Value m_imgpaths{};
    ::cw::lib::Value m_isshortcut{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_order{};
    ::cw::lib::Value m_skin{};
    ::cw::lib::Value m_yadodir{};
    ::cw::lib::String m_yadodirname{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_typed{};
    void initialize(::cw::lib::String arg_name, ::cw::lib::String arg_yadodir, std::vector<::cw::lib::String> arg_advnames, ::cw::lib::String arg_skin, bool arg_classic, ::cw::lib::String arg_isshortcut, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths);
};

class Album : public ::cw::dialog::select::PlayerSelect {
public:
    Album() = default;
    virtual ~Album() = default;
    ::cw::lib::Value m__bg{};
    ::cw::lib::ValueMap m__player_animation_regions{};
    std::map<::cw::lib::String, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>>> m__player_image_cache{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_delbtn{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_infobtn{};
    bool m_isalbum{};
    ::cw::lib::Value m_list{};
    ::cw::lib::Value m_narrow_type{};
    ::cw::lib::Value m_sort{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Integer m_views{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    bool can_clickcenter();
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void _add_topsizer();
    void update_narrowcondition();
    void OnClickDelBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void enable_btn();
    void OnSelect(std::shared_ptr<::cw::lib::Event> arg_event);
protected:
    bool commit_adventurer_change(
        const std::shared_ptr<::cw::header::AdventurerHeader>& header, AdventurerChange change) override;
};

// 選択画面のホストサービスをこのアダプターに分離し、各ホスト
// で同じ状態遷移を使えるようにする。配置とコントロールは各ダ
// イアログの共通 Presentation だけが所有する。
enum class SelectUiKind { multi_view, party, player, select, yado, album };

class SelectUiBackend : public ::cw::lib::Object {
public:
    SelectUiBackend() = default;
    virtual ~SelectUiBackend() = default;
    virtual void bind_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind);
    virtual void layout_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind);
    virtual void destroy_select_dialog(::cw::lib::Object* arg_dialog);
    virtual void refresh_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind, bool arg_layout);
    virtual void play_select_sound(::cw::lib::String arg_name);
    virtual std::shared_ptr<::cw::lib::Bitmap> load_select_bitmap(::cw::lib::String arg_path, bool arg_mask);
    virtual void open_party_editor(PartySelect* arg_dialog);
    virtual void open_party_members(PartySelect* arg_dialog);
    virtual void open_player_information(PlayerSelect* arg_dialog);
};

std::shared_ptr<SelectUiBackend> get_ui_backend();

extern ::cw::lib::Value g__T;
extern ::cw::lib::Value g__okid;

void Select__bind_empty(std::shared_ptr<::cw::lib::Window> arg_event);
void Select__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void Select_OnNarrowCondition_func();
bool YadoSelect_update_narrowcondition_has_advname();
std::shared_ptr<::cw::lib::Bitmap> YadoSelect_draw_get_playingbmp();
void PartySelect_OnDebugMode_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_self);
void PartySelect_OnDebugMode_func_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_self);
bool PartySelect_update_narrowcondition_has_membername();
bool PartySelect_update_narrowcondition_has_memberdesc();
bool PartySelect_update_narrowcondition_has_memberhistory();
bool PartySelect_update_narrowcondition_has_memberfeatures();
bool PartySelect_update_narrowcondition_has_memberlevel();
void PartySelect_OnClickEditBtn_redrawfunc();
void PartySelect_OnClickEditBtn_redrawfunc_func();
std::tuple<std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::optional<std::shared_ptr<::cw::header::ScenarioHeader>>, std::vector<std::shared_ptr<::cw::image::ImageInfo>>> PartySelect_draw_get_image(std::shared_ptr<::cw::header::PartyHeader> arg_header);
void PlayerSelect_OnDebugMode_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self);
void PlayerSelect_OnDebugMode_func_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self);
bool PlayerSelect_update_narrowcondition_has_history();
bool PlayerSelect_update_narrowcondition_has_features();
void PlayerSelect_OnClickAddBtn_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self, std::shared_ptr<::cw::header::AdventurerHeader> arg_header, ::cw::lib::Integer arg_index);
void PlayerSelect_OnClickAddBtn_func_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self);
void PlayerSelect_OnClickAddBtn_func_func_2(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self);
void PlayerSelect_random_team_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_panel);
void PlayerSelect_Pocket_random_team_func___init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::header::AdventurerHeader> arg_header, ::cw::lib::Integer arg_point);
void PlayerSelect_random_team_func_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_panel);
::cw::lib::String PlayerSelect_create_randomadventurer_random_name();
void PlayerSelect_update_character_func();
// 共通の wx.Choice 境界に渡す、現在
// の選択肢データとマウス情報。ALL で表示行
// 数を固定する場合も、選択肢のデータ件数を制限
// するものではない。両モードとも全選択肢と選択
// 通知を使い、単体版は Py に従う。このア
// ダプター自体は固定の表示件数を導入しない。
struct ComboChoiceControl : ::cw::lib::Window {
    std::vector<::cw::lib::String> choices;
    ::cw::lib::Integer selection{-1};
    std::function<void(::cw::lib::Integer)> on_selection_changed;
};
struct ComboWheelEvent : ::cw::lib::Window {
    ::cw::lib::Real pointer_x{}, pointer_y{};
    ::cw::lib::Integer rotation{};
};
bool change_combo(std::shared_ptr<::cw::lib::Window> arg_combo, std::shared_ptr<::cw::lib::Window> arg_event);
void main();

[[nodiscard]] ::cw::lib::String select_msg(
    const ::cw::lib::String& key,
    const ::cw::lib::String& fallback);

}

