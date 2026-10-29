#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// OS に依存しない、冒険者・宿の作成ダイアログの宣言。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"
#include <atomic>
#include <array>
#include "cw/dialog/textedit.hpp"
#include "cw/dialog/textedit_menu.hpp"
#include "cw/dialog/incrementalsearch.hpp"

namespace cw::dialog::create {
class AdventurerCreater : public ::cw::lib::Dialog,
                          public ::cw::dialog::DialogPresentationProvider,
                          public std::enable_shared_from_this<AdventurerCreater> {
public:
    AdventurerCreater() = default;
    ~AdventurerCreater() override;
    inline static ::cw::lib::String s_fpath{};
    ::cw::lib::Value m_autobtn{};
    ::cw::lib::Value m_closebtn{};
    bool m_cwpy_debug{};
    bool m_modal_closed{};
    bool m_modal_result_ok{};
    ::cw::lib::Value m_fpath{};
    ::cw::lib::Value m_header{};
    ::cw::lib::Value m_nextbtn{};
    ::cw::lib::Value m_page{};
    std::shared_ptr<::cw::dialog::create::NamePage> m_page1{};
    std::shared_ptr<::cw::dialog::create::RacePage> m_page2{};
    std::shared_ptr<::cw::dialog::create::RelationPage> m_page3{};
    std::shared_ptr<::cw::dialog::create::TalentPage> m_page4{};
    std::shared_ptr<::cw::dialog::create::AttrPage> m_page5{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_postbtn{};
    ::cw::lib::Value m_prevbtn{};
    ::cw::lib::Value m_shifttabkeyid{};
    std::uintptr_t m_native_parent_handle{};
    int m_presentation_text_field{};
    int m_creation_choice{}, m_creation_cursor{}, m_creation_first{};
    std::vector<::cw::lib::String> m_creation_choices{};
    std::weak_ptr<::cw::dialog::create::AdventurerCreaterPage> m_creation_choice_page{};
    ::cw::dialog::DialogPresentationAction m_creation_focus_action{::cw::dialog::DialogPresentationAction::Select};
    int m_creation_focus_index{};
    // 作成中の冒険者データを共有所有する。
    std::shared_ptr<::cw::dialog::create::AdventurerData> m_data{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent);
    void OnNUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _init_pages();
    void _do_layout();
    void _bind();
    void enable_btn();
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickNextBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickPrevBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickPostBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void create_adventurer();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    using ResourceLoader=std::function<std::shared_ptr<::cw::lib::Surface>(const ::cw::lib::String&)>;
    [[nodiscard]] ::cw::dialog::DialogPresentation render_presentation(
        const std::shared_ptr<::cw::lib::Surface>& background,bool show_auto,const ResourceLoader& resources={},
        std::shared_ptr<AdventurerCreaterPage> page_override={});
    std::tuple<std::shared_ptr<::cw::lib::Window>,std::shared_ptr<::cw::lib::Bitmap>>
        render_page(const std::shared_ptr<AdventurerCreaterPage>& page,bool update);
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text,
                                  bool composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
        float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers value) override {m_name_modifiers=value;}
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    [[nodiscard]] std::vector<::cw::lib::String> creation_choices(int kind) const;
    void open_creation_choice(int kind);
    void commit_creation_choice(int index);
    using ImageRequest=std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&,::cw::lib::documents::Completion)>;
    bool request_image_files(ImageRequest start={});
    [[nodiscard]] bool image_request_pending() const noexcept {return bool(m_image_request_token);}
    ::cw::lib::String m_image_error;
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    std::shared_ptr<int> m_image_request_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_image_ticket;
    std::shared_ptr<::cw::lib::Surface> m_background;
    void retire_pages() noexcept;
    void finish_image_request(const std::shared_ptr<int>& token,
        const std::shared_ptr<::cw::dialog::create::NamePage>& page,::cw::lib::documents::Result result);
    ::cw::dialog::textedit::Buffer m_name_edit;
    ::cw::dialog::textedit::EditMenu m_name_menu;
    ::cw::dialog::DialogPresentationModifiers m_name_modifiers{};
    bool name_menu_active();
    void open_name_menu(float x,float y,bool keyboard=false);
    bool m_name_dragging{};
    bool name_input_focused() const;
    void sync_name_input();
    void publish_name_input();
    void navigate_creation_focus(::cw::dialog::DialogPresentationKey key, bool backward);
protected:
    virtual ::cw::lib::String read_name_clipboard() const;
    virtual void write_name_clipboard(const ::cw::lib::String& text);
};

class AdventurerCreaterPage : public ::cw::lib::Panel,
                              public std::enable_shared_from_this<AdventurerCreaterPage> {
public:
    AdventurerCreaterPage() = default;
    virtual ~AdventurerCreaterPage() = default;
    inline static std::vector<std::shared_ptr<::cw::image::ImageInfo>> s_imgpaths{};
    std::tuple<::cw::lib::Value, ::cw::lib::String, ::cw::lib::String> m__dropkey{};
    ::cw::lib::String m_age{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_ch_imgdpath{};
    std::vector<std::vector<std::optional<::cw::lib::Value>>> m_clickable_table{};
    ::cw::lib::ValueMap m_clickables{};
    ::cw::lib::Integer m_imgdpath{};
    std::vector<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>> m_imgdpaths{};
    std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> m_imgpathlist{};
    ::cw::lib::Value m_imgpaths{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_typed{};
    std::vector<std::shared_ptr<::cw::lib::Control>> m_last_ctrls{};
    std::optional<::cw::lib::Value> m_next{};
    std::optional<::cw::lib::Value> m_prev{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_selected_clickable{};
    ::cw::lib::String m_sex{};
    ::cw::lib::Value m_shifttabkeyid{};
    ::cw::lib::Value m_tabkeyid{};
    std::vector<std::tuple<::cw::lib::Rect, ::cw::lib::String>> m_tooltips{};
    // 作成画面が五ページすべてを所有する。ページから作成画面を所有すると
    // 、閉じた後もページ全体のグラフが残るため、逆向きの所有はしない。
    std::weak_ptr<::cw::dialog::create::AdventurerCreater> m_creator{};
    std::optional<::cw::lib::String> m_drop_path{};
    void initialize(::cw::lib::Value arg_parent, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size, bool arg_freeze);
    void OnSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _bind();
    virtual void _do_layout();
    void set_normalacceleratortable();
    virtual bool AcceptsFocus();
    virtual bool AcceptsFocusFromKeyboard();
    bool AcceptsFocusRecursively();
    void OnEraseBackground(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnMotion(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnNLeftKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnNRightKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnNUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnNDownKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void move_right();
    void move_left();
    void move_down();
    void move_up();
    bool is_selectionstart();
    bool is_selectionend();
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _find_nextclickable_h(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_current);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _find_prevclickable_h(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_current);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _find_nextclickable_v(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_current);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> _find_prevclickable_v(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_current);
    void OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void _put_image(std::vector<::cw::lib::String> arg_files);
    bool put_image_files(const std::vector<::cw::lib::String>& files);
    // 別のグループが選ばれていても、外部候補へのアクセス権は候補側で保持する。
    std::vector<std::shared_ptr<void>> m_image_access;
    virtual void _choice_imgdpath();
    void OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void draw_clickabletext(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_s, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Value arg_name, std::optional<::cw::lib::Callback> arg_method, std::optional<::cw::lib::Callback> arg_wheelmethod, std::optional<::cw::lib::String> arg_setname);
    void set_clickablearea(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Value arg_size, ::cw::lib::Value arg_name, std::optional<::cw::lib::Callback> arg_method, std::optional<::cw::lib::Callback> arg_wheelmethod);
    void draw_clickablebmp(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_bmp, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Value arg_name, ::cw::lib::Callback arg_method, std::optional<::cw::lib::Callback> arg_wheelmethod, bool arg_mask);
    void set_next(::cw::lib::Value arg_page);
    void set_prev(::cw::lib::Value arg_page);
    std::optional<::cw::lib::Value> get_next();
    std::optional<::cw::lib::Value> get_prev();
    virtual bool is_skip();
    void set_imgpathlist(bool arg_reset);
    void _update_imgdpaths();
    virtual std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    std::shared_ptr<::cw::lib::DeviceContext> draw(bool arg_update);
    virtual void select_autofeatures();
};

class AdventurerData : public ::cw::lib::Object {
public:
    AdventurerData() { initialize(); }
    virtual ~AdventurerData() = default;
    ::cw::lib::Real m_aggressive{};
    ::cw::lib::Real m_agl{};
    ::cw::lib::Integer m_antimagic{};
    bool m_automaton{};
    ::cw::lib::Integer m_avoid{};
    ::cw::lib::String m_beasts{};
    ::cw::lib::Integer m_bind{};
    ::cw::lib::Real m_brave{};
    ::cw::lib::Real m_cautious{};
    ::cw::lib::Real m_cheerful{};
    std::optional<::cw::lib::Integer> m_coeff_ep{};
    std::optional<::cw::lib::Real> m_coeff_level{};
    bool m_constructure{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_couponnames{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> m_coupons{};
    ::cw::lib::Integer m_defense{};
    ::cw::lib::String m_description{};
    ::cw::lib::Real m_dex{};
    ::cw::lib::Integer m_duration_enhance_action{};
    ::cw::lib::Integer m_duration_enhance_avoid{};
    ::cw::lib::Integer m_duration_enhance_defense{};
    ::cw::lib::Integer m_duration_enhance_resist{};
    ::cw::lib::Integer m_duration_mentality{};
    ::cw::lib::Integer m_enhance_action{};
    ::cw::lib::Integer m_enhance_avoid{};
    ::cw::lib::Integer m_enhance_defense{};
    ::cw::lib::Integer m_enhance_resist{};
    ::cw::lib::Integer m_faceup{};
    std::optional<std::shared_ptr<::cw::header::Gene>> m_gene{};
    bool m_has_parents{};
    ::cw::lib::String m_id{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::String m_indent{};
    ::cw::lib::Real m_py_int{};
    ::cw::lib::String m_items{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_life{};
    ::cw::lib::Integer m_maxagl{};
    ::cw::lib::Integer m_maxdex{};
    ::cw::lib::Integer m_maxint{};
    ::cw::lib::Integer m_maxlife{};
    ::cw::lib::Integer m_maxmin{};
    ::cw::lib::Integer m_maxstr{};
    ::cw::lib::Integer m_maxvit{};
    ::cw::lib::String m_mentality{};
    ::cw::lib::Real m_min{};
    ::cw::lib::String m_name{};
    bool m_noeffect_magic{};
    bool m_noeffect_weapon{};
    ::cw::lib::Integer m_paralyze{};
    ::cw::lib::Integer m_poison{};
    ::cw::lib::Integer m_resist{};
    bool m_resist_fire{};
    bool m_resist_ice{};
    ::cw::lib::Integer m_silence{};
    ::cw::lib::String m_skills{};
    ::cw::lib::Real m_str{};
    ::cw::lib::Real m_trickish{};
    bool m_undead{};
    bool m_unholy{};
    ::cw::lib::Real m_vit{};
    bool m_weakness_fire{};
    bool m_weakness_ice{};
    void initialize();
    std::map<::cw::lib::String, ::cw::lib::String> get_d();
    void set_coupon(::cw::lib::String arg_name, ::cw::lib::Integer arg_value);
    void set_name(::cw::lib::String arg_name);
    void set_images(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths);
    void set_sex(::cw::lib::String arg_sex);
    void set_age(::cw::lib::String arg_age);
    void set_level(::cw::lib::Integer arg_level);
    void calc_level();
    void set_race(std::shared_ptr<::cw::header::RaceHeader> arg_race);
    void set_parents(std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> arg_father, std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> arg_mother);
    ::cw::lib::String set_gene(::cw::lib::String arg_talent);
    void set_talent(::cw::lib::String arg_talent);
    void set_aging(::cw::lib::String arg_age);
    void set_attributes(std::set<::cw::lib::String> arg_attrs);
    void set_desc(::cw::lib::String arg_talent, std::set<::cw::lib::String> arg_attrs, ::cw::lib::String arg_desc);
    void set_specialcoupon();
    void set_life();
};

class AdventurerDataComp : public ::cw::lib::Dialog,
                           public ::cw::dialog::DialogPresentationProvider
{
public:
    AdventurerDataComp() = default;
    virtual ~AdventurerDataComp() = default;
    ::cw::lib::Value m_age{};
    std::shared_ptr<::cw::util::CWPyStaticBitmap> m_bmp{};
    ::cw::lib::Value m_box{};
    ::cw::lib::Value m_ccard{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_rb_age{};
    ::cw::lib::Value m_rb_sex{};
    ::cw::lib::Value m_sex{};
    ::cw::lib::Value m_text_caution{};
    ::cw::lib::Value m_text_message{};
    ::cw::lib::Value m_text_name{};
    bool m_presentation_finished{};
    bool m_presentation_accepted{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::sprite::card::FriendCard> arg_ccard);
    void _bind();
    void _do_layout();
    void OnClickOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRbSex(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickRbAge(std::shared_ptr<::cw::lib::Window> arg_event);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    using PortraitLoader=std::function<std::shared_ptr<::cw::lib::Surface>(
        const std::shared_ptr<::cw::image::ImageInfo>&,bool)>;
    [[nodiscard]] ::cw::dialog::DialogPresentation render_presentation(const PortraitLoader& load_portrait);
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    std::array<std::vector<std::pair<::cw::lib::String,::cw::lib::String>>,2> m_choices;
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_portrait_infos;
    bool m_portrait_scaled{},m_committing{};
    int m_focus_group{2};
    [[nodiscard]] bool group_enabled(int group) const;
    [[nodiscard]] int selected_choice(int group) const;
    void select_choice(int group,int index);
};

class AdventurerDesignDialog : public ::cw::lib::Dialog,
                               public std::enable_shared_from_this<AdventurerDesignDialog>,
                               public ::cw::dialog::DialogPresentationProvider {
public:
    AdventurerDesignDialog() = default;
    virtual ~AdventurerDesignDialog() = default;
    std::vector<::cw::lib::Value> m_buttonlist{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    std::shared_ptr<::cw::dialog::create::DesignPanel> m_toppanel{};
    bool m_presentation_finished{};
    bool m_presentation_accepted{};
    int m_presentation_text_field{};
    bool m_presentation_group_open{};
    int m_presentation_group_cursor{}, m_presentation_group_offset{};
    std::array<::cw::dialog::textedit::Buffer,2> m_text_edits;
    // コメント欄の表示中のスクロール位置を画素単位で保持する。ホイール・ドラッグ・
    // キャレット移動は m_text_edits[1].scroll を行単位で
    // 設定し、表示側は一行ずつ飛ばず、フレームごとに目標位置へ滑らかに近づける。
    double m_desc_scroll_px{};
    bool m_desc_scroll_initialized{};
    int m_design_focus{}; // 名前、説明、自動、顔画像、グループ、中央寄せ、OK、Cancel、参照。
    int m_text_drag{-1};
    ::cw::dialog::DialogPresentationModifiers m_text_modifiers{};
    std::shared_ptr<::cw::lib::Surface> m_background;
    // UI の状態は Frame に保持し、ワーカーには独立した値のスナップショットを渡す。
    struct DesignChange {
        std::shared_ptr<::cw::character::Character> character;
        ::cw::lib::String name, description;
        bool images_changed{};
        std::vector<std::shared_ptr<::cw::image::ImageInfo>> images;
        std::vector<std::shared_ptr<void>> image_access;
    };
    bool m_commit_pending{};
    // ファイル取得でホストのイベントが処理される場合があるため、その間は親の編集中の値を変更しない。
    bool choose_image_files(const std::function<std::vector<::cw::lib::String>()>& choose);
    using ImageRequest=std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&,::cw::lib::documents::Completion)>;
    bool request_image_files(ImageRequest start={});
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    bool queue_design_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game,
        std::function<void(const DesignChange&)> apply);
    using PortraitLoader=std::function<std::shared_ptr<::cw::lib::Surface>(
        const std::shared_ptr<::cw::image::ImageInfo>&,bool)>;
    [[nodiscard]] ::cw::dialog::DialogPresentation render_presentation(
        const std::shared_ptr<::cw::lib::Surface>& background,const PortraitLoader& load_portrait);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Player> arg_ccard);
    void _bind();
    void _do_layout();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text,
                                  bool composition) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
        float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override {
        if(design_input_blocked()) return;
        m_text_drag=-1;
        request_dialog_presentation_refresh();
    }
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override {
        if(!design_input_blocked()) m_text_modifiers=modifiers;
    }
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual ::cw::lib::String read_text_clipboard() const;
    virtual void write_text_clipboard(const ::cw::lib::String& text);
private:
    ::cw::dialog::textedit::EditMenu m_text_menu;
    int m_text_menu_field{-1};
    bool text_menu_active();
    void open_text_menu(int field,float x,float y,bool keyboard=false);
    std::shared_ptr<std::atomic_bool> m_design_commit_token;
    std::shared_ptr<int> m_image_request_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_image_ticket;
    bool m_image_last_changed{};
    void finish_image_request(const std::shared_ptr<int>& token,
        const std::shared_ptr<DesignPanel>& panel,::cw::lib::documents::Result result);
    bool design_input_blocked();
    bool design_composing() const;
    void retire_design_input();
    void sync_text_edits();
    void publish_text_edits();
    [[nodiscard]] ::cw::lib::Rect design_text_rect(int index) const;
    [[nodiscard]] std::vector<::cw::dialog::textedit::Line> design_text_lines(int index) const;
    void finish_design_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::shared_ptr<std::atomic_bool>& token, const ::cw::lib::String& error);
};

class AttrPage : public ::cw::dialog::create::AdventurerCreaterPage {
public:
    AttrPage() = default;
    virtual ~AttrPage() = default;
    std::vector<::cw::lib::Value> m_clickable_table{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, ::cw::lib::String> m_couponsdata{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerCreater> arg_parent);
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void draw_clickabletext(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_s, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_name, std::optional<::cw::lib::Callback> arg_method, std::optional<::cw::lib::Callback> arg_wheelmethod, std::optional<::cw::lib::String> arg_setname);
    void set_coupon(std::tuple<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_name);
    std::set<::cw::lib::String> get_coupons();
    void select_autofeatures();
};

class DesignPanel : public ::cw::lib::Object {
public:
    DesignPanel() = default;
    virtual ~DesignPanel() = default;
    ::cw::lib::Value m_age{};
    ::cw::lib::Value m_autoname{};
    bool m_can_loaded_scaledimage{};
    bool m_centering{true};
    ::cw::lib::Value m_ccard{};
    bool m_centering_shown{};
    std::shared_ptr<::cw::lib::Window> m_ch_imgdpath{};
    std::vector<std::vector<::cw::lib::String>> m_clickable_table{};
    ::cw::lib::Value m_ctrlleftkeyid{};
    ::cw::lib::Value m_ctrlrightkeyid{};
    ::cw::lib::Value m_desc{};
    std::shared_ptr<::cw::lib::Window> m_descctrl{};
    ::cw::lib::Value m_downkeyid{};
    ::cw::lib::Value m_imgdpath{};
    std::vector<::cw::lib::Value> m_imgpaths{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_namectrl{};
    ::cw::lib::Value m_ndownkeyid{};
    ::cw::lib::Value m_nleftkeyid{};
    ::cw::lib::Value m_nrightkeyid{};
    ::cw::lib::Value m_nupkeyid{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_oldimgpath{};
    // DesignPanelはwx版ではAdventurerCreaterPageを継承していたため、画像variant列を型付きで補う。
    std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> m_imgpathlist_typed{};
    std::vector<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>> m_imgdpaths_typed{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_current_typed{};
    std::vector<std::shared_ptr<void>> m_image_access;
    ::cw::lib::Value m_ref_image{};
    ::cw::lib::Value m_sex{};
    ::cw::lib::Value m_shifttabkeyid{};
    ::cw::lib::Value m_tabkeyid{};
    std::vector<std::tuple<::cw::lib::Value, ::cw::lib::Value>> m_tooltips{};
    ::cw::lib::Value m_upkeyid{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerDesignDialog> arg_parent, std::shared_ptr<::cw::character::Player> arg_ccard);
    void _set_acceleratortable(bool arg_leftright, bool arg_updown);
    bool is_selectionstart();
    bool is_selectionend();
    void OnShiftTab(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnTab(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCtrlLeftKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCtrlRightKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNLeftKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNRightKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    bool is_changedimgpath();
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> selected_imagepaths() const;
    bool put_image_files(const std::vector<::cw::lib::String>& paths);
    void _bind();
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _up_imgd();
    void OnDownKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _down_imgd();
    void OnInputName(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoName(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnChoiceImgDPath(std::shared_ptr<::cw::lib::Window> arg_event);
    void _choice_imgdpath();
    void OnCentering(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void on_mousewheel(::cw::lib::String arg_name, ::cw::lib::Integer arg_rotate);
    void set_nextimg(::cw::lib::String arg_name);
    void set_previmg(::cw::lib::String arg_name);
};

class NamePage : public ::cw::dialog::create::AdventurerCreaterPage {
public:
    NamePage() = default;
    virtual ~NamePage() = default;
    ::cw::lib::String m_age{};
    ::cw::lib::Value m_autoname{};
    bool m_centering{true};
    bool m_centering_shown{};
    ::cw::lib::Value m_ch_imgdpath{};
    std::vector<::cw::lib::Value> m_clickable_table{};
    ::cw::lib::Value m_ctrlleftkeyid{};
    ::cw::lib::Value m_ctrlrightkeyid{};
    ::cw::lib::Value m_downkeyid{};
    ::cw::lib::Integer m_imgdpath{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::String m_input_name{};
    std::vector<::cw::lib::Value> m_last_ctrls{};
    ::cw::lib::String m_name{};
    ::cw::lib::Value m_ndownkeyid{};
    ::cw::lib::Value m_nleftkeyid{};
    ::cw::lib::Value m_nrightkeyid{};
    ::cw::lib::Value m_nupkeyid{};
    ::cw::lib::Value m_ref_image{};
    ::cw::lib::String m_sex{};
    ::cw::lib::Value m_shifttabkeyid{};
    ::cw::lib::Value m_tabkeyid{};
    ::cw::lib::Value m_textctrl{};
    std::vector<std::tuple<::cw::lib::Value, ::cw::lib::Value>> m_tooltips{};
    ::cw::lib::Value m_upkeyid{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerCreater> arg_parent);
    // UP_WIN 適用後の最終画素。任意の背景もマスク付きの合成に含める。
    using PortraitLoader=std::function<std::shared_ptr<::cw::lib::Surface>(const ::cw::lib::String&)>;
    [[nodiscard]] std::shared_ptr<::cw::lib::Surface> render_portrait(PortraitLoader load={},
        const std::shared_ptr<::cw::lib::Surface>& background={}) const;
    void _set_acceleratortable(bool arg_leftright, bool arg_updown);
    void _bind();
    void OnCtrlLeftKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCtrlRightKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNDownKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNLeftKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNRightKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _up_imgd();
    void OnDownKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _down_imgd();
    void OnInputText(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoName(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnChoiceImgDPath(std::shared_ptr<::cw::lib::Window> arg_event);
    void _choice_imgdpath();
    void OnCentering(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void set_sex(::cw::lib::String arg_name);
    void _update_sex();
    void set_age(::cw::lib::String arg_name);
    void on_mousewheel(::cw::lib::String arg_name, ::cw::lib::Integer arg_rotate);
    void set_nextimg(::cw::lib::String arg_name);
    void set_previmg(::cw::lib::String arg_name);
    void select_autofeatures();
};

class RacePage : public ::cw::dialog::create::AdventurerCreaterPage {
public:
    RacePage() = default;
    virtual ~RacePage() = default;
    ::cw::lib::Value m_choice{};
    ::cw::lib::Value m_race{};
    ::cw::lib::String m_selected_race{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerCreater> arg_parent);
    bool AcceptsFocus();
    bool AcceptsFocusFromKeyboard();
    void _bind();
    void OnChoice(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    std::shared_ptr<::cw::header::RaceHeader> get_race();
    bool is_skip();
    void select_autofeatures();
};

class RelationPage : public ::cw::dialog::create::AdventurerCreaterPage {
public:
    RelationPage() = default;
    virtual ~RelationPage() = default;
    std::vector<std::vector<::cw::lib::String>> m_clickable_table{};
    std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> m_father{};
    std::vector<std::optional<std::shared_ptr<::cw::header::AdventurerHeader>>> m_fathers{};
    std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> m_mother{};
    std::vector<std::optional<std::shared_ptr<::cw::header::AdventurerHeader>>> m_mothers{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerCreater> arg_parent);
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void set_nextfather(::cw::lib::String arg_name);
    void set_prevfather(::cw::lib::String arg_name);
    void set_nextmother(::cw::lib::String arg_name);
    void set_prevmother(::cw::lib::String arg_name);
    void on_mousewheel(::cw::lib::String arg_name, ::cw::lib::Integer arg_rotate);
    void set_parents();
    void set_parents_from_headers(
        const std::vector<std::shared_ptr<::cw::header::AdventurerHeader>>& standbys,
        const std::vector<std::shared_ptr<::cw::header::AdventurerHeader>>& album);
    bool is_skip();
    void select_autofeatures();
};

class TalentPage : public ::cw::dialog::create::AdventurerCreaterPage {
public:
    TalentPage() = default;
    virtual ~TalentPage() = default;
    std::vector<::cw::lib::Value> m_clickable_table{};
    ::cw::lib::String m_talent{};
    void initialize(std::shared_ptr<::cw::dialog::create::AdventurerCreater> arg_parent);
    std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> draw2(bool arg_update);
    void set_talent(::cw::lib::String arg_name);
    void select_autofeatures();
};

class YadoCreater : public ::cw::lib::Dialog,
                    public std::enable_shared_from_this<YadoCreater>,
                    public ::cw::dialog::DialogPresentationProvider {
public:
    YadoCreater() = default;
    virtual ~YadoCreater() = default;
    ::cw::lib::Integer m__inputareaheight{};
    ::cw::lib::Value m__msg1{};
    ::cw::lib::Value m__msg2{};
    std::vector<::cw::lib::Value> m_cautions{};
    ::cw::lib::Value m_cnclbtn{};
    std::vector<::cw::lib::Value> m_command0s{};
    bool m_create{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_del_image{};
    ::cw::lib::Value m_imgpaths{};
    ::cw::lib::Value m_imgpaths_init{};
    bool m_is_autoloadparty{};
    ::cw::lib::String m_name{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_ref_image{};
    ::cw::lib::Value m_skin{};
    ::cw::lib::String m_skindirname{};
    std::vector<::cw::lib::Value> m_skindirnames{};
    ::cw::lib::Value m_textctrl{};
    std::optional<::cw::lib::String> m_yadodir{};
    // wxのコントロールに埋め込まれていた宿編集データを型付きで保持する。
    // m_* Value は既存の動的呼出し互換用、*_typed が保存処理の正本である。
    std::shared_ptr<::cw::data::CWPyElementTree> m_data_typed{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_typed{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_init_typed{};
    std::vector<::cw::lib::String> m_skindirnames_typed{};
    std::vector<::cw::lib::String> m_skinlabels_typed{};
    std::vector<::cw::lib::String> m_command0paths_typed{};
    std::vector<::cw::lib::String> m_cautionpaths_typed{};
    using ImageRequest=AdventurerDesignDialog::ImageRequest;
    bool request_signboard(ImageRequest start={});
    [[nodiscard]] bool image_request_pending() const noexcept {return bool(m_image_request_token);}
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    ::cw::lib::String m_image_error;
    ::cw::lib::String m_save_error;
    [[nodiscard]] bool save_pending() const noexcept {return m_save_pending;}
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
        float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers value) override {m_name_modifiers=value;}
    bool m_is_autoloadparty_typed{true};
    bool m_modal_closed{};
    bool m_modal_result_ok{};
    bool m_presentation_finished{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::optional<::cw::lib::String> arg_yadodir);
    void create_yado();
    void edit_yado();
    void _edit_yado_impl();
    void _move_dir(std::optional<::cw::lib::String> previous_name=std::nullopt);
    void _put_image(::cw::lib::String arg_fpath);
    void OnDelImage(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInput(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnChoice(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    std::shared_ptr<::cw::lib::Bitmap> _load_caution();
    void OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text,
                                  bool composition) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    std::shared_ptr<int> m_image_request_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_image_ticket;
    std::shared_ptr<void> m_signboard_access;
    bool set_signboard(const ::cw::lib::String& path,std::shared_ptr<void> access={});
    void finish_signboard_request(const std::shared_ptr<int>& token,::cw::lib::documents::Result result);
    bool m_save_pending{};
    std::optional<::cw::lib::String> m_initial_skindirname;
    bool m_skin_popup{};
    int m_skin_cursor{}, m_skin_offset{};
    ::cw::dialog::IncrementalSearch m_skin_search;
    std::vector<::cw::dialog::DialogPresentationControl> m_skin_hover_controls;
    bool yado_composing() const { return !m_name_edit.composition.empty() || m_skin_search.composing; }
    void open_skin_choice();
    void select_skin(int index);
    void move_skin_choice(int amount);
    ::cw::dialog::textedit::Buffer m_name_edit;
    ::cw::dialog::textedit::EditMenu m_name_menu;
    ::cw::dialog::DialogPresentationModifiers m_name_modifiers{};
    bool name_menu_active();
    void open_name_menu(float x,float y,bool keyboard=false);
    bool m_name_dragging{};
    int m_input_focus{};
    void sync_name_input();
    void publish_name_input();
protected:
    virtual void write_yado_environment(const std::shared_ptr<::cw::data::CWPyElementTree>& tree);
    virtual ::cw::lib::String read_name_clipboard() const;
    virtual void write_name_clipboard(const ::cw::lib::String& text);
};

extern ::cw::lib::Value g__KeyType;

::cw::lib::String create_description(::cw::lib::String arg_talent, const std::set<::cw::lib::String>& arg_attrs, ::cw::lib::String arg_desc);
bool AdventurerCreater_OnNUpKeyDown_focus_lastctrl();
std::vector<std::shared_ptr<::cw::image::ImageInfo>> path_to_imageinfo(::cw::lib::String arg_path);
void NamePage___init___OnTextCtrlSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void NamePage___init___OnChoiceSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void NamePage___init___OnSkipFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void NamePage___init___OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event);
::cw::lib::String get_randomname(::cw::lib::String arg_sex);
std::set<::cw::lib::String> _get_randomnamefromexample(::cw::lib::String arg_sex, ::cw::lib::String arg_skintype);
std::set<::cw::lib::String> _read_names(::cw::lib::String arg_fpath);
void RelationPage_draw2_draw_paths(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, bool arg_can_loaded_scaledimage);
void RelationPage_set_parents_append_header(std::shared_ptr<::cw::dialog::create::RelationPage> arg_self, std::shared_ptr<::cw::header::AdventurerHeader> arg_header);
std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, ::cw::lib::String> _get_randommakingsandpair();
std::set<::cw::lib::String> get_randommakings();
void YadoCreater__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void AdventurerDesignDialog__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void AdventurerDesignDialog_OnOk_func(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, bool arg_is_changedimgpath, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths);
void DesignPanel___init___OnLeftRightSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void DesignPanel___init___OnArrowsSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void DesignPanel___init___OnUpDownSetFocus(std::shared_ptr<::cw::lib::Window> arg_event);
void DesignPanel___init___OnKillFocus(std::shared_ptr<::cw::lib::Window> arg_event);
::cw::lib::Integer _index_of(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_imgpathlist);
void _set_nextimg(::cw::lib::Value arg_panel, ::cw::lib::String arg_name);
void _set_previmg(::cw::lib::Value arg_panel, ::cw::lib::String arg_name);
std::vector<std::shared_ptr<::cw::image::ImageInfo>> expand_basename_variants(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos, bool arg_add_default, bool arg_add_message, bool arg_add_life);
::cw::lib::Value expand_basename_variants_sort_key(std::shared_ptr<::cw::image::ImageInfo> arg_info);
void main();
}

