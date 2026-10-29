#pragma once

// cw/dialog/edit.py の C++ 移植。モデル・状態を UI
// から分離し、すべてのホストで同じ共通 Presentation を使
// う。OS 固有コードはサービスと倍率だけを提供し、別の配置は持たない。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit.hpp"
#include "cw/dialog/textedit_menu.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/fwd.hpp"
#include "cw/frame.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace cw::dialog::edit {

// wx.SingleChoiceDialog 相当の
// 共通モデル。すべてのホストで同じ一覧・選択・OK・
// Cancel を使い、UI の実装は一系統にする。
class SingleChoiceDialog final : public ::cw::lib::Object,
                                 public ::cw::dialog::DialogPresentationProvider {
public:
    SingleChoiceDialog(::cw::lib::String title, ::cw::lib::String message,
                       std::vector<::cw::lib::String> choices, int selected = 0);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return m_finished; }
    void dialog_presentation_complete() override {
        if (m_complete_callback) m_complete_callback(m_result);
    }
    [[nodiscard]] std::optional<int> result() const noexcept { return m_result; }
    void set_complete_callback(
        std::function<void(std::optional<int>)> callback) {
        m_complete_callback = std::move(callback);
    }
private:
    ::cw::lib::String m_title, m_message;
    std::vector<::cw::lib::String> m_choices;
    int m_selected{-1}, m_scroll{};
    bool m_finished{};
    std::optional<int> m_result;
    std::function<void(std::optional<int>)> m_complete_callback;
};

enum class EditUiKind {
    back_color,
    combo,
    combo_message,
    input_text,
    level,
    party,
    number,
    number2,
    number_combo,
};

struct EditUiSpec {
    EditUiKind kind{EditUiKind::number};
    std::uintptr_t parent_handle{};
    ::cw::lib::String title;
    ::cw::lib::String message;
    std::vector<::cw::lib::String> labels;
    std::vector<::cw::lib::String> choices;
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> choice_bitmaps;
    std::vector<::cw::lib::Integer> values;
    std::vector<::cw::lib::Integer> minimums;
    std::vector<::cw::lib::Integer> maximums;
    std::vector<::cw::lib::Integer> pages;
    ::cw::lib::String text;
    ::cw::lib::String addition;
    ::cw::lib::Integer maxlength{};
    bool checked{};
    bool has_addition{};
    std::array<::cw::lib::Integer, 3> rgb{};
    // BackColor は編集状態を HSV で保持する。丸めた RGB
    // プレビューから再構築すると、無彩色で選んだ色相が失われる。
    std::array<::cw::lib::Real, 3> hsv{};
    std::array<bool, 3> panel_focus{};
};

using EditUiEvent = std::function<void(::cw::lib::Integer, const ::cw::lib::String&)>;

// バックエンドはネイティブのコントロールハンドルと現在のコールバックだけを所有し、ダ
// イアログモデルは所有しない。置き換えても Python に対応する動作を変えない。
class EditUiBackend : public ::cw::lib::Object {
public:
    ~EditUiBackend() override = default;
    virtual void create(const EditUiSpec& spec, EditUiEvent event) = 0;
    virtual void update(const EditUiSpec& spec) = 0;
    virtual void close() = 0;
    virtual bool show_modal() { return false; }
    virtual void show_error(const ::cw::lib::String& title, const ::cw::lib::String& message) = 0;
    virtual bool confirm(const ::cw::lib::String& title, const ::cw::lib::String& message) = 0;
    [[nodiscard]] virtual bool is_open() const { return false; }
    [[nodiscard]] virtual std::uintptr_t native_handle() const { return 0; }
};

std::shared_ptr<EditUiBackend> get_edit_ui_backend();
[[nodiscard]] bool is_edit_ui_open();
[[nodiscard]] std::optional<::cw::lib::String> show_common_modal_text_input(
    const ::cw::lib::String& title, const ::cw::lib::String& message,
    const ::cw::lib::String& initial, ::cw::lib::Integer max_length = 255);
[[nodiscard]] std::optional<::cw::lib::Integer> show_common_modal_number_input(
    const ::cw::lib::String& title, ::cw::lib::Integer initial,
    ::cw::lib::Integer minimum, ::cw::lib::Integer maximum, ::cw::lib::Integer page = 1);
// 作成済みの共通編集 Presentation を、ShowModal と同等に所有
// 者の操作を遮断して実行する。OS 固有部分は配置・入力の受け渡しだけを提供する。
void run_edit_ui_modal(std::uintptr_t parent_handle);

class BackColorEditDialog : public ::cw::lib::Object {
public:
    BackColorEditDialog() = default;
    virtual ~BackColorEditDialog() = default;
    bool m__destroyed{};
    bool m_modal_result_ok{};
    // ネイティブのモーダル結果は、編集がキューに入ったことだけを示す
    // 。モデルの完了は後で CWPy のゲームスレッドから届くため
    // 、保存・更新はこのコールバックが成功を通知するまで遅らせる。
    std::function<void(bool)> m_on_complete{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_cnclbtn{};
    ::cw::lib::Value m_colorchoice{};
    bool m_cwpy_debug{};
    bool m_dragging_huepanel{};
    bool m_dragging_saturationpanel{};
    bool m_dragging_valuepanel{};
    std::tuple<::cw::lib::Real, ::cw::lib::Real, ::cw::lib::Real> m_hsv{};
    std::shared_ptr<::cw::dialog::edit::BackColorEditDialog_NoFlickPanel> m_huepanel{};
    bool m_is_select_huepanel{};
    bool m_is_select_saturationpanel{};
    bool m_is_select_valuepanel{};
    ::cw::lib::Value m_okbtn{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m_presetcolor_list{};
    std::vector<::cw::lib::Value> m_presetcolor_names{};
    std::shared_ptr<::cw::dialog::edit::BackColorEditDialog_NoFlickPanel> m_saturationpanel{};
    std::shared_ptr<::cw::dialog::edit::BackColorEditDialog_NoFlickPanel> m_valuepanel{};
    // 動的 Value だけでは Player/XML を安全に辿れないため、編集対象を
    // 型付きで保持する。m_ccard は生成コード互換の公開ミラーとして残す。
    std::shared_ptr<::cw::character::Player> m_ccard_typed{};
    ::cw::lib::Integer m_preset_selection{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Player> arg_ccard);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> hsv2rgb(std::tuple<::cw::lib::Real, ::cw::lib::Real, ::cw::lib::Real> arg_hsv);
    std::tuple<::cw::lib::Real, ::cw::lib::Real, ::cw::lib::Real> rgb2hsv(std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_rgb);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_rgblist();
    void _bind();
    void _do_layout();
    void update_panels();
    void OnLeftClickHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnSetFocusHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKillFocusHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnDragHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnReleaseHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKeyDownHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnPaintHuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void draw_huepanel(bool arg_update);
    void OnLeftClickSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnSetFocusSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKillFocusSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnDragSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnReleaseSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKeyDownSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnPaintSaturationPanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void draw_saturationpanel(bool arg_update);
    ::cw::lib::Integer _x_wrap(::cw::lib::Integer arg_x);
    void OnLeftClickValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnSetFocusValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKillFocusValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnDragValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnReleaseValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnKeyDownValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnPaintValuePanel(std::shared_ptr<::cw::lib::Window> arg_evt);
    void draw_valuepanel(bool arg_update);
    void OnChoicePreset(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
};

class ComboEditDialog : public ::cw::lib::Object {
public:
    ComboEditDialog() = default;
    virtual ~ComboEditDialog() = default;
    std::optional<std::shared_ptr<::cw::lib::Panel>> m__combo_panel{};
    ::cw::lib::Value m_box{};
    ::cw::lib::Value m_cnclbtn{};
    std::shared_ptr<::cw::lib::Window> m_combo{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Integer m_selected{};
    std::vector<::cw::lib::String> m_choices{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> m_choice_bitmaps{};
    std::function<void(::cw::lib::Integer)> m_on_accept{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_label, std::vector<::cw::lib::String> arg_mlist, ::cw::lib::Integer arg_selected);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_label, ::cw::lib::Value arg_mlist, ::cw::lib::Integer arg_selected);
    void _bind();
    void _do_layout();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
};

class ComboEditDialog2 : public ::cw::lib::Object {
public:
    ComboEditDialog2() = default;
    virtual ~ComboEditDialog2() = default;
    ::cw::lib::Integer m__textheight{};
    ::cw::lib::Integer m__textwidth{};
    ::cw::lib::Value m_cnclbtn{};
    ::cw::lib::Value m_combo{};
    bool m_cwpy_debug{};
    ::cw::lib::String m_message{};
    std::vector<::cw::lib::String> m_choices{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Integer m_selected{};
    std::function<void(::cw::lib::Integer)> m_on_accept{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_message, std::vector<::cw::lib::String> arg_choices);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
};

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
class InputTextDialog : public ::cw::lib::Object,
                        public ::cw::dialog::DialogPresentationProvider {
public:
    InputTextDialog() = default;
    [[nodiscard]] virtual ::cw::lib::ui::FontStyle dialog_font(const ::cw::lib::String& role, ::cw::lib::Integer pixels) const;
    virtual ~InputTextDialog() = default;
    ::cw::lib::Value m__textheight{};
    ::cw::lib::String m_addition{};
    std::optional<::cw::lib::Callback> m_addition_func{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::String m_msg{};
    ::cw::lib::Integer m_maxlength{};
    bool m_ok_enabled{};
    ::cw::lib::String m_title{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::String m_text{};
    std::shared_ptr<::cw::lib::Window> m_textctrl{};
    bool m_presentation_finished{};
    bool m_presentation_accepted{};
    // 編集可能な状態は一つだけとし、m_text/m_textctrl は公開結果・受け渡しだけに使う。
    ::cw::dialog::textedit::Buffer m_edit;
    int m_focus{}; // テキスト、自動、承諾、取り消し。
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_msg, ::cw::lib::String arg_text, ::cw::lib::Integer arg_maxlength, ::cw::lib::String arg_addition, std::optional<::cw::lib::Callback> arg_addition_func);
    void OnInput(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAddition(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override {m_modifiers=modifiers;}
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
protected:
    virtual ::cw::lib::String read_input_clipboard() const;
    virtual void write_input_clipboard(const ::cw::lib::String& text);
private:
    ::cw::dialog::textedit::EditMenu m_edit_menu;
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    std::uint64_t m_input_generation{};
    bool m_dragging{},m_addition_running{};
    void sync_input();
    void publish_input();
    void open_edit_menu(float x,float y,bool keyboard);
};

class LevelEditDialog : public ::cw::lib::Object {
public:
    LevelEditDialog() = default;
    virtual ~LevelEditDialog() = default;
    bool m_modal_finished{};
    bool m_modal_result_ok{};
    // BackColorEditDialog::m_on_complete を参照。モーダ
    // ル・UI スレッドを止めず、Python と同じ非同期のゲームスレッド境界を保つ。
    std::function<void(bool)> m_on_complete{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_list{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_party{};
    ::cw::lib::Value m_rightbtn{};
    std::shared_ptr<::cw::dialog::edit::NumberEditor> m_slider{};
    ::cw::lib::Value m_target{};
    std::vector<::cw::lib::String> m_targets{};
    std::vector<std::shared_ptr<::cw::character::Player>> m_list_typed{};
    std::optional<std::shared_ptr<::cw::data::Party>> m_party_typed{};
    ::cw::lib::Integer m_target_index{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist, ::cw::lib::Integer arg_selected, std::optional<std::shared_ptr<::cw::data::Party>> arg_party);
    std::vector<std::shared_ptr<::cw::character::Player>> get_selected();
    ::cw::lib::Integer get_currentlevel();
    ::cw::lib::Integer get_maxlevel();
    void _select_target();
    void _bind();
    void _do_layout();
    void OnSelectTarget(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
};

class MoneyEditPanel : public ::cw::lib::Object {
public:
    std::shared_ptr<int> m_slider_update_lifetime{std::make_shared<int>(0)};
    MoneyEditPanel() = default;
    virtual ~MoneyEditPanel() = default;
    std::shared_ptr<::cw::util::CWPyStaticBitmap> m_bmp_pmoney{};
    std::shared_ptr<::cw::util::CWPyStaticBitmap> m_bmp_ymoney{};
    ::cw::lib::Value m_party{};
    std::shared_ptr<::cw::dialog::edit::SliderWithButton> m_slider{};
    ::cw::lib::Value m_spinctrl{};
    ::cw::lib::Value m_spinctrl2{};
    ::cw::lib::Value m_text_party{};
    ::cw::lib::Value m_text_yado{};
    ::cw::lib::Value m_value{};
    ::cw::lib::Integer m_spin_min{},m_spin_max{};
    void initialize(std::shared_ptr<::cw::dialog::edit::PartyEditor> arg_parent, std::shared_ptr<::cw::data::Party> arg_party);
    void _bind();
    void OnSlider(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSpinCtrl(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSpinCtrl2(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
};

class MoneyViewPanel : public ::cw::lib::Object {
public:
    MoneyViewPanel() = default;
    virtual ~MoneyViewPanel() = default;
    std::shared_ptr<::cw::util::CWPyStaticBitmap> m_bmp_pmoney{};
    ::cw::lib::Value m_text_party{};
    ::cw::lib::Value m_text_pmoney{};
    ::cw::lib::Value m_value{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::data::Party> arg_party);
    void _do_layout();
};

class Number2EditDialog : public ::cw::lib::Object {
public:
    Number2EditDialog() = default;
    virtual ~Number2EditDialog() = default;
    ::cw::lib::Value m_box1{};
    ::cw::lib::Value m_box2{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    std::shared_ptr<::cw::dialog::edit::NumberEditor> m_slider1{};
    std::shared_ptr<::cw::dialog::edit::NumberEditor> m_slider2{};
    ::cw::lib::Integer m_value1{};
    ::cw::lib::Integer m_value2{};
    std::function<void(::cw::lib::Integer, ::cw::lib::Integer)> m_on_accept{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_label1, ::cw::lib::Integer arg_value1, ::cw::lib::Integer arg_minvalue1, ::cw::lib::Integer arg_maxvalue1, ::cw::lib::Integer arg_page1, ::cw::lib::String arg_label2, ::cw::lib::Integer arg_value2, ::cw::lib::Integer arg_minvalue2, ::cw::lib::Integer arg_maxvalue2, ::cw::lib::Integer arg_page2);
    void _bind();
    void _do_layout();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
};

class NumberComboEditDialog : public ::cw::lib::Object {
public:
    NumberComboEditDialog() = default;
    virtual ~NumberComboEditDialog() = default;
    ::cw::lib::Value m__combo_panel{};
    ::cw::lib::Value m_box1{};
    ::cw::lib::Value m_box2{};
    ::cw::lib::Value m_cnclbtn{};
    ::cw::lib::Value m_combo{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Integer m_selected{};
    ::cw::lib::Integer m_combo_selection{-1};
    std::shared_ptr<::cw::dialog::edit::NumberEditor> m_slider{};
    ::cw::lib::Integer m_value{};
    std::function<void(::cw::lib::Integer, ::cw::lib::Integer)> m_on_accept{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_label1, ::cw::lib::Value arg_mlist, ::cw::lib::Integer arg_selected, ::cw::lib::String arg_label2, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_minvalue, ::cw::lib::Integer arg_maxvalue, ::cw::lib::Integer arg_page);
    void _bind();
    void _do_layout();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
};

class NumberEditDialog : public ::cw::lib::Object {
public:
    NumberEditDialog() = default;
    virtual ~NumberEditDialog() = default;
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    std::shared_ptr<::cw::dialog::edit::NumberEditor> m_slider{};
    ::cw::lib::Integer m_value{};
    // 呼び出し側にバックエンドのウィンドウやハンドルを意識させず、ネイティブホストで
    // wx.Dialog.ShowModal() と ID_OK 相当の結果を表す。
    std::function<void(::cw::lib::Integer)> m_on_accept{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_minvalue, ::cw::lib::Integer arg_maxvalue, ::cw::lib::Integer arg_page);
    void _bind();
    void _do_layout();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
};

class NumberEditor : public ::cw::lib::Object {
public:
    std::shared_ptr<int> m_slider_update_lifetime{std::make_shared<int>(0)};
    NumberEditor() = default;
    virtual ~NumberEditor() = default;
    std::shared_ptr<::cw::dialog::edit::SliderWithButton> m_slider{};
    ::cw::lib::Value m_spinctrl{};
    ::cw::lib::Value m_spinlabel{};
    ::cw::lib::Integer m_value{};
    ::cw::lib::Integer m_minvalue{};
    ::cw::lib::Integer m_maxvalue{};
    ::cw::lib::Integer m_page{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_minvalue, ::cw::lib::Integer arg_maxvalue, ::cw::lib::Integer arg_page);
    ::cw::lib::Integer get_value();
    void set_value(::cw::lib::Integer arg_value);
    void set_max(::cw::lib::Integer arg_value);
    void set_min(::cw::lib::Integer arg_value);
    void enable();
    void _bind();
    void _do_layout();
    void OnSlider(::cw::lib::Value arg_evt);
    void OnSpinCtrl(std::shared_ptr<::cw::lib::Window> arg_evt);
};

struct PartyEditChange {
    std::shared_ptr<::cw::data::Party> party;
    ::cw::lib::String name;
    std::optional<::cw::lib::Integer> money;
    bool suspend_levelup{}, retry_write{};
};

class PartyEditor : public ::cw::lib::Object,
                    public std::enable_shared_from_this<PartyEditor>,
                    public ::cw::dialog::DialogPresentationProvider
{
public:
    PartyEditor() = default;
    virtual ~PartyEditor() = default;
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_party{};
    // wx のチェックボックス値を SDL 前段の状態として保持する。
    bool m_suspend_levelup_value{};
    ::cw::lib::Value m_textctrl{};
    // 配置とモーダル終了の後処理は Frame が所有する。その UI キューへ一度だけ通知する。
    std::function<void()> m_close_callback{};
    std::function<void()> m_on_accept{};
    EditUiSpec m_presentation_spec{};
    bool m_presentation_finished{};
    int m_presentation_focus{}; // 0 は名前、8/9 は所持金、10 はスライダー、11 はチェックボックス、12/13 は OK/Cancel。
    ::cw::dialog::textedit::Buffer m_name_edit;
    bool m_name_dragging{};
    int m_money_repeat_key{-1};
    bool m_money_repeat_inside{};
    ::cw::dialog::ButtonRepeatTimer m_money_repeat_timer;
    // 現在の増減ボタンの長押し開始時刻。段階的な加速に使う。
    ::cw::dialog::ButtonRepeatTimer::Clock::time_point m_money_repeat_started{};
    bool m_presentation_accepted{}, m_commit_pending{};
    ::cw::lib::String m_commit_error;
    std::shared_ptr<std::atomic_bool> m_commit_token;
    bool queue_party_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::function<void(::cw::lib::Callback)>& post_game,
        std::function<void(const PartyEditChange&)> apply);
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer_cancel() override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    bool m_presentation_replace_text{true};
    ::cw::lib::String m_presentation_money_text{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::optional<std::shared_ptr<::cw::data::Party>> arg_party);
    void _bind();
    void _do_layout();
    void OnSuspendLevelUp(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    // スキンデータを明示的に指定して同じ描画器を使う。Engine
    // の作成・起動なしの CPU 回帰検査にも使える。
    [[nodiscard]] ::cw::dialog::DialogPresentation render_presentation(
        const std::shared_ptr<::cw::lib::Surface>& background);
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
        int index, float x, float y, bool pressed) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    void finish_party_change(const std::shared_ptr<::cw::frame::Frame>& frame,
        const std::shared_ptr<std::atomic_bool>& token,const ::cw::lib::String& error);
    void close_party_editor(bool accepted);
    ::cw::dialog::textedit::EditMenu m_name_menu;
    ::cw::dialog::DialogPresentationModifiers m_name_modifiers{};
    void open_name_menu(float x,float y,bool keyboard);
    void sync_name_input();
    void publish_name_input();
    void step_money_control(int key, bool request_refresh = true);
    void tick_money_repeat();
protected:
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point money_input_time() const noexcept;
    virtual ::cw::lib::String read_name_clipboard() const;
    virtual void write_name_clipboard(const ::cw::lib::String& text);
public:
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override {m_name_modifiers=modifiers;}
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
};

class SliderWithButton : public ::cw::lib::Object {
public:
    std::function<void()> m_on_slider_updated{};
    SliderWithButton() = default;
    virtual ~SliderWithButton() = default;
    inline static ::cw::lib::Integer s__repeat_first{500};
    inline static ::cw::lib::Integer s__repeat_second{20};
    ::cw::lib::Value m__timer{};
    ::cw::lib::Value m__timerbtn{};
    std::optional<::cw::lib::Callback> m__timerfunc{};
    bool m_is_enabled{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_slider{};
    ::cw::lib::Integer m_value{};
    ::cw::lib::Integer m_minvalue{};
    ::cw::lib::Integer m_maxvalue{};
    ::cw::lib::Integer m_page{};
    void initialize(std::shared_ptr<::cw::dialog::edit::MoneyEditPanel> arg_parent, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_minvalue, ::cw::lib::Integer arg_maxvalue, ::cw::lib::Integer arg_page, ::cw::lib::Integer arg_sliderwidth);
    void set_value(::cw::lib::Integer arg_value);
    void set_max(::cw::lib::Integer arg_value);
    void set_min(::cw::lib::Integer arg_value);
    void enable();
    void _bind();
    void _do_layout();
    void OnMouseDownBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnKillFocusBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseUpBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _end();
    void OnTimer1(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnTimer2(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _on_leftbtn();
    void _on_rightbtn();
};

class BackColorEditDialog_NoFlickPanel : public ::cw::frame::NoFlick {
public:
    BackColorEditDialog_NoFlickPanel() = default;
    virtual ~BackColorEditDialog_NoFlickPanel() = default;
};

void PartyEditor__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void PartyEditor_OnOk_func(std::shared_ptr<::cw::dialog::edit::PartyEditor> arg_self, std::shared_ptr<::cw::data::Party> arg_party, bool arg_suspend_levelup);
void PartyEditor_OnOk_func_func(std::shared_ptr<::cw::dialog::edit::PartyEditor> arg_self);
void NumberComboEditDialog__do_layout_func(std::shared_ptr<::cw::dialog::edit::NumberComboEditDialog> arg_self);
void SliderWithButton__end_func();
void ComboEditDialog__do_layout_func(std::shared_ptr<::cw::dialog::edit::ComboEditDialog> arg_self);
void LevelEditDialog__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void LevelEditDialog_OnOk_func(std::vector<std::shared_ptr<::cw::character::Player>> arg_seq, ::cw::lib::Integer arg_level, std::optional<std::shared_ptr<::cw::data::Party>> arg_party);
void BackColorEditDialog_NoFlickPanel___init_____init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::dialog::edit::BackColorEditDialog> arg_parent);
void BackColorEditDialog___init___func();
void BackColorEditDialog___init___func_end_drag(std::shared_ptr<::cw::dialog::edit::BackColorEditDialog> arg_self);
void BackColorEditDialog__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void BackColorEditDialog_OnOk_func(std::shared_ptr<::cw::character::Player> arg_ccard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_rgb);
void main();
}

