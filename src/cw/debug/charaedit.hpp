#pragma once

// 冒険者編集のデータモデル。下の wx/SDL 向けパ
// ネル宣言はソース互換性を保ち、CharaInfo の
// メソッドが共通の編集状態と XML 更新を担当する。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/documents.hpp"
#include "cw/fwd.hpp"

#include <atomic>

namespace cw::debug::charaedit {
// すべてのホストでこの共通アダプターを使う。OS 固有コードは下位
// のファイル・入力の受け渡しを提供し、別のフォーム実装は持たない。
struct CharaEditUiAdapter {
    using String = ::cw::lib::String;
    using Integer = ::cw::lib::Integer;
    std::function<void(const String&)> bind{};
    std::function<void(const String&)> layout{};
    std::function<void(const String&, const std::vector<String>&, Integer, bool)> image_choices{};
    std::function<void(const String&, const std::vector<std::shared_ptr<::cw::image::ImageInfo>>&)> image_preview{};
    std::function<void(const String&, bool)> enabled{};
    std::function<void(const String&, Integer)> selection{};
    std::function<void(const String&, Integer)> checkbox{};
    std::function<std::optional<Integer>(const String&, Integer, Integer, Integer, Integer)> number_input{};
    std::function<std::optional<Integer>(const String&, const std::vector<String>&, Integer)> choice_input{};
    std::function<void(const String&, const std::vector<String>&)> dropped_files{};
    std::function<void(const String&, bool)> modal{};
    // Windows アダプターの状態は単純なデータとして公開し、このモデルを user32
    // に依存させずに、ホストから HWND のコントロールへ渡せるようにする。
    void* native_owner{};
    std::vector<String> bound_keys{};
    std::vector<String> layout_keys{};
    std::map<String, bool> enabled_state{};
    std::map<String, Integer> selection_state{};
    std::map<String, Integer> checkbox_state{};
    std::vector<String> image_choices_state{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> image_preview_state{};
    std::vector<String> dropped_files_state{};
    bool modal_result{};
};

std::shared_ptr<CharaEditUiAdapter> make_default_ui_adapter(void* arg_native_owner=nullptr);

class CharaInfo : public ::cw::lib::Object {
public:
    CharaInfo() = default;
    virtual ~CharaInfo() = default;
    ::cw::lib::String m_age{};
    ::cw::lib::Real m_aggressive{};
    ::cw::lib::Real m_agl{};
    ::cw::lib::Real m_brave{};
    bool m_can_loaded_scaledimage{};
    bool m_can_loaded_scaledimage_base{};
    ::cw::lib::Real m_cautious{};
    ::cw::lib::Real m_cheerful{};
    bool m_debug_coupon{};
    bool m_debug_coupon_init{};
    ::cw::lib::Real m_dex{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths_base{};
    ::cw::lib::String m_input_name{};
    ::cw::lib::Real m_py_int{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_levelmax{};
    std::set<::cw::lib::String> m_makings{};
    ::cw::lib::Integer m_maxagl{};
    ::cw::lib::Integer m_maxdex{};
    ::cw::lib::Integer m_maxint{};
    ::cw::lib::Integer m_maxmin{};
    ::cw::lib::Integer m_maxstr{};
    ::cw::lib::Integer m_maxvit{};
    std::map<::cw::lib::String, ::cw::lib::Real> m_mental{};
    ::cw::lib::Real m_min{};
    ::cw::lib::String m_name{};
    std::map<::cw::lib::String, ::cw::lib::Real> m_physical{};
    std::shared_ptr<::cw::header::RaceHeader> m_race{};
    bool m_recalc_coupons{};
    bool m_recalc_coupons_init{};
    bool m_recalc_maxlife{};
    bool m_recalc_maxlife_init{};
    bool m_recalc_parameter{};
    bool m_recalc_parameter_init{};
    ::cw::lib::String m_sex{};
    ::cw::lib::Real m_str{};
    ::cw::lib::String m_talent{};
    ::cw::lib::Real m_trickish{};
    std::optional<std::shared_ptr<::cw::features::SampleType>> m_type{};
    ::cw::lib::Real m_vit{};
    void initialize(std::optional<std::shared_ptr<::cw::character::Player>> arg_pcard, bool arg_debug_coupon=true);
    void set_randomfeatures();
    std::optional<std::shared_ptr<::cw::features::SampleType>> get_paramtype(std::shared_ptr<::cw::character::Player> arg_info);
    void _calc_params();
    bool put_params(std::shared_ptr<::cw::character::Player> arg_pcard);
    ::cw::lib::String create_adventurer(bool arg_setlevel=true);
    std::vector<::cw::lib::String> get_makingslist();
};

class CharaRequirementPanel : public ::cw::lib::Panel {
public:
    CharaRequirementPanel() = default;
    virtual ~CharaRequirementPanel() = default;
    std::vector<::cw::lib::String> m__dropfiles{};
    bool m__proc{};
    ::cw::lib::Value m_autobtn{};
    ::cw::lib::Value m_autoname{};
    ::cw::lib::Integer m_cindex{};
    ::cw::lib::Value m_create{};
    ::cw::lib::Value m_defaultface{};
    std::shared_ptr<::cw::util::CWPyStaticBitmap> m_img{};
    ::cw::lib::Value m_imgbox{};
    ::cw::lib::Value m_imgcentering{};
    ::cw::lib::Value m_imgcombo{};
    std::vector<::cw::lib::String> m_imgpathlist{};
    ::cw::lib::Value m_infos{};
    ::cw::lib::Value m_levelbtn{};
    ::cw::lib::Value m_lvlbox{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_namebox{};
    ::cw::lib::Value m_natures{};
    ::cw::lib::Value m_periods{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_race{};
    ::cw::lib::Value m_racebox{};
    ::cw::lib::Value m_ref_image{};
    ::cw::lib::Value m_sexes{};
    bool m_show_specialtalent{};
    ::cw::lib::Value m_typbox{};
    std::shared_ptr<::cw::lib::Window> m_type{};
    // 上の Value は wx コントロールを表し、これらの型付きメンバ
    // ーで UI 以外・ネイティブの実装から編集状態を扱えるようにする。
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> m_infos_typed{};
    std::vector<::cw::lib::String> m_dropfiles_typed{};
    bool m_create_typed{};
    bool m_proc_typed{};
    bool m_show_specialtalent_typed{};
    ::cw::lib::Integer m_cindex_typed{};
    // ネイティブUIが選択状態を渡すまで保持する画像候補と中央寄せ状態。
    // wx の ComboBox/CheckBox 自体は Value のままだが、画像選択のデータ更新は
    // この typed 状態を通じてUI非依存に実行する。
    std::vector<::cw::lib::String> m_imgpathlist_typed{};
    std::vector<::cw::lib::String> m_imgchoice_labels_typed{};
    ::cw::lib::Integer m_imgselection_typed{};
    bool m_imgcentering_typed{};
    bool m_ok_enabled_typed{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> arg_infos, bool arg_create);
    void _bind();
    void _do_layout();
    void OnName(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoName(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_okbtn();
    void OnLevelBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectImage(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnImgBoxDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void _put_image(std::vector<::cw::lib::String> arg_files);
    void OnRace(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectSex(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectAge(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectTalent(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _update_images(std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_img);
    void _select_image(::cw::lib::Real render_scale=1.0);
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> _get_infos();
    void select_target(::cw::lib::Integer arg_cindex);
    void set_random();
};

class CharaSelectablePanel : public ::cw::lib::Panel {
public:
    CharaSelectablePanel() = default;
    virtual ~CharaSelectablePanel() = default;
    ::cw::lib::Value m_autobtn{};
    ::cw::lib::Integer m_cindex{};
    ::cw::lib::Value m_clearbtn{};
    ::cw::lib::Value m_create{};
    ::cw::lib::Value m_infos{};
    std::vector<::cw::lib::Value> m_makings{};
    ::cw::lib::Value m_mkgbox{};
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> m_infos_typed{};
    std::vector<::cw::lib::String> m_makings_typed{};
    std::vector<::cw::lib::String> m_makinglabels_typed{};
    bool m_create_typed{};
    ::cw::lib::Integer m_cindex_typed{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> arg_infos, bool arg_create);
    void _bind();
    void _do_layout();
    void OnCheck(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClearBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> _get_infos();
    void select_target(::cw::lib::Integer arg_cindex);
    void set_random();
};

class CharacterEditDialog : public ::cw::lib::Dialog,
                            public ::cw::dialog::DialogPresentationProvider,
                            public std::enable_shared_from_this<CharacterEditDialog> {
public:
    CharacterEditDialog() = default;
    virtual ~CharacterEditDialog() = default;
    inline static ::cw::lib::String s_fpath{};
    ::cw::lib::Value m_autobtn{};
    ::cw::lib::Value m_cnclbtn{};
    ::cw::lib::Value m_create{};
    bool m_cwpy_debug{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_debug_coupon{};
    ::cw::lib::Value m_fpath{};
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> m_infos{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_note{};
    ::cw::lib::Value m_okbtn{};
    std::shared_ptr<::cw::debug::charaedit::CharaRequirementPanel> m_pane_req{};
    std::shared_ptr<::cw::debug::charaedit::CharaSelectablePanel> m_pane_sel{};
    ::cw::lib::Value m_pcards{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_recalc_coupons{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_recalc_maxlife{};
    std::optional<std::shared_ptr<::cw::lib::Window>> m_recalc_parameter{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_stdbtn{};
    ::cw::lib::Value m_target{};
    std::vector<::cw::lib::String> m_targets{};
    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> m_pcards_typed{};
    bool m_create_typed{};
    bool m_modal_closed{};
    bool m_modal_result_ok{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Integer m_selected_typed{-1};
    ::cw::lib::Integer m_presentation_tab{};
    ::cw::lib::Integer m_presentation_trait_scroll{};
    bool m_common_presentation_active{};
    bool m_presentation_operation_pending{};
    std::shared_ptr<int> m_document_token{};
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket{};
    // 取り込んだプロバイダー画像の利用権を、編集の確定・取り消しま
    // で保持する。Character::set_images が複
    // 製するのは確定時だけ。UI の実装は共通の一系統にまとめる。
    std::vector<::cw::lib::documents::Document> m_image_documents{};
    ::cw::lib::Integer m_presentation_focus{10};
    ::cw::lib::Integer m_presentation_popup_kind{};
    ::cw::lib::Integer m_presentation_popup_index{};
    std::vector<::cw::lib::String> m_presentation_popup_values{};
    ::cw::lib::String m_presentation_name_draft{};
    ::cw::lib::String m_presentation_composition{};
    bool m_presentation_name_select_all{};
    std::atomic_bool m_presentation_dirty{true};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Integer arg_selected, bool arg_create);
    void _bind();
    void _do_layout();
    std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> _get_infos();
    void OnReCalcMaxLife(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnReCalcParameter(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnReCalcCoupons(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDebugCoupon(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectTarget(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnStandardType(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    using ImageDocumentRequest = std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&, ::cw::lib::documents::Completion)>;
    bool request_image_document(ImageDocumentRequest start = {});
    void finish_image_document(const std::shared_ptr<int>& token,
                               ::cw::lib::documents::Result result);
    void cancel_document_request() noexcept;
    void select_target();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text,bool composition) override;
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_refreshed() noexcept override;
    void dialog_presentation_complete() override;
};

void CharacterEditDialog_OnOkBtn_func(std::vector<::cw::lib::Integer> arg_updates, std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_pcards);
void CharaInfo_put_params_put_coupons(::cw::lib::Value arg_period);
std::tuple<::cw::lib::String, ::cw::lib::String> CharaInfo_put_params_create_parentmatcher(::cw::lib::String arg_s);
void main();
}

