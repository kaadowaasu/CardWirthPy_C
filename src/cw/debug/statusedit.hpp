#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/debug/statusedit.py の移植。wx ウィジェットは
// 共通の SDL 向け互換オブジェクトで表し、編集状態は型付きで保持する。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"
#include <cstdint>
#include <functional>

namespace cw::debug::statusedit {
class StatusEditDialog;
class StatusEditUiBackend : public ::cw::lib::Object {
public:
    virtual ~StatusEditUiBackend() = default;
    virtual void open(StatusEditDialog& arg_dialog) = 0;
    virtual void update(StatusEditDialog& arg_dialog) = 0;
    virtual void close(StatusEditDialog& arg_dialog, bool arg_accept) = 0;
    virtual bool run_modal() { return false; }
};
std::shared_ptr<StatusEditUiBackend> make_default_statusedit_ui_backend(void* arg_native_owner = nullptr);
std::shared_ptr<StatusEditUiBackend> statusedit_ui_backend();

class Status : public ::cw::lib::Object {
public:
    Status() = default;
    virtual ~Status() = default;
    ::cw::lib::Integer m_antimagic{};
    ::cw::lib::Integer m_bind{};
    ::cw::lib::Integer m_enhance_act{};
    ::cw::lib::Integer m_enhance_act_dur{};
    ::cw::lib::Integer m_enhance_avo{};
    ::cw::lib::Integer m_enhance_avo_dur{};
    ::cw::lib::Integer m_enhance_def{};
    ::cw::lib::Integer m_enhance_def_dur{};
    ::cw::lib::Integer m_enhance_res{};
    ::cw::lib::Integer m_enhance_res_dur{};
    ::cw::lib::Integer m_faceup{};
    ::cw::lib::Integer m_life{};
    ::cw::lib::String m_mentality{};
    ::cw::lib::Integer m_mentality_dur{};
    ::cw::lib::Integer m_paralyze{};
    ::cw::lib::Integer m_poison{};
    ::cw::lib::Integer m_silence{};
    void initialize(::cw::lib::Value arg_pcard);
    bool is_dead();
    bool is_unconscious();
    bool put_status(std::shared_ptr<::cw::character::Character> arg_pcard);
};

class StatusButton : public ::cw::lib::Object {
public:
    StatusButton() = default;
    virtual ~StatusButton() = default;
    std::optional<::cw::lib::Integer> m_duration{};
    ::cw::lib::Color m_color{192, 192, 192, 255};
    bool m_has_image_background{};
    bool m_enabled{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Callback m_is_dead{};
    ::cw::lib::Callback m_is_unconscious{};
    ::cw::lib::Integer m_mode{};
    ::cw::lib::String m_text1{};
    ::cw::lib::String m_text2{};
    std::optional<::cw::lib::Value> m_value{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_mode, ::cw::lib::Callback arg_is_dead, ::cw::lib::Callback arg_is_unconscious, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    void draw(bool arg_update);
    std::shared_ptr<::cw::lib::Surface> render_icon(const std::shared_ptr<::cw::lib::Surface>& source) const;
};

class StatusEditDialog : public ::cw::lib::Dialog,
                         public ::cw::dialog::DialogPresentationProvider,
                         public std::enable_shared_from_this<StatusEditDialog> {
public:
    StatusEditDialog() = default;
    virtual ~StatusEditDialog() = default;
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_action{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_antimagic{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_avoid{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_bind{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_defense{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_faceup{};
    ::cw::lib::Value m_leftbtn{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_life{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_mentality{};
    ::cw::lib::Value m_okbtn{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_paralyze{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_pcards{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_poison{};
    ::cw::lib::Value m_rcvbtn{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_resist{};
    ::cw::lib::Value m_restorebtn{};
    ::cw::lib::Value m_rightbtn{};
    std::shared_ptr<::cw::debug::statusedit::StatusButton> m_silence{};
    std::vector<std::shared_ptr<::cw::debug::statusedit::StatusButton>> m_statusbtns{};
    std::vector<std::shared_ptr<::cw::debug::statusedit::Status>> m_statuses{};
    std::vector<std::shared_ptr<::cw::debug::statusedit::Status>> m_statuses_backup{};
    ::cw::lib::Integer m_target_index{0};
    std::vector<::cw::lib::String> m_targets{};
    ::cw::lib::Value m_target{};
    std::shared_ptr<StatusEditUiBackend> m_ui_backend{};
    bool m_modal_result{};
    bool m_apply_pending{};
    bool m_prompt_pending{};
    bool m_presentation_finished{};
    bool m_target_popup{};
    ::cw::lib::Integer m_target_popup_index{};
    ::cw::lib::Integer m_presentation_focus{};
    std::uintptr_t m_native_parent_handle{};
    // CardWirthPy_C_All 専用で、CardWirthPy
    // にはないカスタム状態ショートカットの設定用登録モード。対
    // 象を全員に固定し、全快・復旧を隠す。登録時はパーティーに適
    // 用せず、編集状態を m_on_register へ渡す。
    bool m_register_mode{};
    std::function<void(const ::cw::lib::String&)> m_on_register{};
    void initialize_register(std::shared_ptr<::cw::lib::Window> arg_parent, const ::cw::lib::String& arg_status);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::character::Character>> arg_mlist, ::cw::lib::Integer arg_selected);
    bool show_modal();
    void _bind();
    void _do_layout();
    static std::optional<::cw::lib::Value> _value(std::optional<::cw::lib::Value> arg_oldvalue, std::optional<::cw::lib::Value> arg_newvalue, bool arg_force, std::optional<::cw::lib::Value> arg_defvalue);
    static std::optional<::cw::lib::Integer> _value_int(std::optional<::cw::lib::Integer> arg_oldvalue, ::cw::lib::Integer arg_newvalue, bool arg_force, std::optional<::cw::lib::Integer> arg_defvalue);
    void OnSelectTarget(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnFullRecovery(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRestore(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLife(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPoison(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnParalyze(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMentality(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBind(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSilence(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnFaceUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAntiMagic(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAction(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAvoid(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnResist(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDefense(std::shared_ptr<::cw::lib::Window> arg_event);
    void _select_target();
    bool _is_dead();
    bool _is_unconscious();
    void _update_status();
    std::vector<std::shared_ptr<::cw::debug::statusedit::Status>> _get_statuses();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

// CardWirthPy_C_All 専用のカスタム状態の保存・適用。空また
// は不正な文字列は、生命力 100% でほかの状態なしの通常状態として扱う。
[[nodiscard]] Status parse_custom_status(const ::cw::lib::String& arg_text);
[[nodiscard]] ::cw::lib::String format_custom_status(const Status& arg_status);
// ゲームスレッド専用。デバッガーの状態編集で全員を選んで決定
// した場合と同様に、全パーティーメンバーに状態を適用する。
void apply_custom_status_party(const ::cw::lib::String& arg_text);

void StatusEditDialog_OnOkBtn_func(std::vector<::cw::lib::Value> arg_pcards, std::vector<::cw::lib::Integer> arg_updates);
void main();
}

