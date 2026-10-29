#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/message.py の移植。描画とネイティブのモ
// ーダル連携は UI アダプターが担当する。型付きの選択・チェック状態を
// ここに保持し、ヘッドレスの呼び出し側でも同じ動作を使えるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/ui_surface.hpp"

namespace cw::dialog::message {
// wxPython の標準コマンド ID。ダイアログモデルに同じ値を保持し
// 、ネイティブと Python の呼び出し側でモーダルの戻り値をそろえる。
inline constexpr ::cw::lib::Integer kWxIdOk = 5100;
inline constexpr ::cw::lib::Integer kWxIdCancel = 5101;
inline constexpr ::cw::lib::Integer kWxIdYes = 5103;
inline constexpr ::cw::lib::Integer kWxIdNo = 5104;

struct MessageChoice {
    ::cw::lib::String label{};
    ::cw::lib::Integer id{};
    // Py の両ダイアログは、明示的な選択肢の幅を最終画素単位で渡す
    // 。-1 は各ダイアログ自身の最適サイズの自動計算を要求する。
    ::cw::lib::Integer width{-1};
    ::cw::lib::String description{};
    bool enabled{true};
};
struct MessageCheckBox {
    ::cw::lib::String key{}, label{};
    bool value{};
};
class Message : public ::cw::lib::Dialog,
                public ::cw::dialog::DialogPresentationProvider
              {
public:
    Message() = default;
    virtual ~Message() = default;
    ::cw::lib::Integer m__textheight{};
    ::cw::lib::String m_basetext{},m_title{},m_detailtext{};
    ::cw::lib::String m_wrapped_text{};
    std::vector<::cw::lib::Value> m_buttons{};
    std::vector<MessageChoice> m_choices{};
    std::vector<::cw::lib::Integer> m_button_ids{};
    ::cw::lib::Value m_closebtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Integer m_mode{2};
    ::cw::lib::Value m_nobtn{};
    ::cw::lib::String m_text{};
    ::cw::lib::Value m_yesbtn{};
    ::cw::lib::Integer m_return_code{0};
    bool m_closed{};
    int m_focus_index{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, ::cw::lib::Integer arg_mode, std::optional<std::vector<::cw::lib::Value>> arg_choices);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, ::cw::lib::Integer arg_mode, std::vector<MessageChoice> arg_choices);
    void OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event);
    void copy_detail();
    [[nodiscard]] bool dialog_presentation_can_copy_detail() const noexcept override { return true; }
    void dialog_presentation_copy_detail() override { copy_detail(); }
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void _do_layout();
    void close_with_result(::cw::lib::Integer id);
    bool select_choice(::cw::lib::Integer id);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
    void dialog_presentation_key(DialogPresentationKey key, bool pressed,
                                 DialogPresentationModifiers modifiers) override;
protected:
    virtual void copy_text(const ::cw::lib::String& text);
};

class SysMessage : public ::cw::lib::Dialog,
                   public ::cw::dialog::DialogPresentationProvider {
public:
    SysMessage() = default;
    virtual ~SysMessage() = default;
    std::map<::cw::lib::String, bool> m__check_table{};
    std::vector<std::tuple<::cw::lib::String, std::shared_ptr<::cw::lib::Window>>> m__checkboxes{};
    ::cw::lib::Value m__st_text{};
    ::cw::lib::Value m__stl{};
    ::cw::lib::Integer m__textheight{};
    ::cw::lib::String m_basetext{},m_title{},m_detailtext{};
    ::cw::lib::String m_wrapped_text{};
    std::vector<::cw::lib::Value> m_buttons{};
    std::vector<MessageChoice> m_choices{};
    std::vector<MessageCheckBox> m_check_options{};
    ::cw::lib::String m_text{};
    ::cw::lib::Integer m_return_code{0};
    bool m_closed{};
    // Tab 順はチェックボックス、選択肢の順。すべてのホストでモデルに保持する。
    int m_focus_index{};
    bool m_cwpy_debug{};
    // 通常の SysMessage は Python のデスクトップ DPI の
    // 寸法に従う。一部のゲーム側の確認画面は、カードウィンドウ倍率を選択できる。
    bool m_game_window_scale{};
    ::cw::lib::ui::SystemDialogMetrics m_system_metrics{};
    ::cw::lib::Rect m_text_rect{}, m_separator_rect{};
    std::vector<::cw::lib::Rect> m_check_rects{}, m_choice_rects{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, std::optional<std::vector<::cw::lib::Value>> arg_choices, std::optional<std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, bool>>> arg_checkboxes);
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, std::vector<MessageChoice> arg_choices, std::vector<MessageCheckBox> arg_checkboxes = {});
    // initialize() より前に設定する。既定では Python
    // のデスクトップ形式の SysMessage のサイズを保つ。
    void set_game_window_scale(bool arg_enabled = true) noexcept { m_game_window_scale = arg_enabled; }
    bool get_check(::cw::lib::String arg_key);
    void OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event);
    void copy_detail();
    [[nodiscard]] bool dialog_presentation_can_copy_detail() const noexcept override { return true; }
    void dialog_presentation_copy_detail() override { copy_detail(); }
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCheck(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    void close_with_result(::cw::lib::Integer id);
    void set_check(const ::cw::lib::String& key, bool value);
    bool select_choice(::cw::lib::Integer id);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
    void dialog_presentation_key(DialogPresentationKey key, bool pressed,
                                 DialogPresentationModifiers modifiers) override;
protected:
    virtual void copy_text(const ::cw::lib::String& text);
    virtual ::cw::lib::ui::SystemDialogMetrics system_metrics() const;
};

class ErrorMessage : public ::cw::dialog::message::Message {
public:
    ErrorMessage() = default;
    virtual ~ErrorMessage() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_text);
};

class YesNoCancelMessage : public ::cw::dialog::message::Message {
public:
    YesNoCancelMessage() = default;
    virtual ~YesNoCancelMessage() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text);
};

class YesNoMessage : public ::cw::dialog::message::Message {
public:
    YesNoMessage() = default;
    virtual ~YesNoMessage() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text);
};

class MessageUiBackend : public ::cw::lib::Object {
public:
    virtual ~MessageUiBackend() = default;
    virtual void show_message(Message* arg_dialog);
    virtual void show_sys_message(SysMessage* arg_dialog);
    virtual void layout(::cw::lib::Object* arg_dialog);
    virtual void close(::cw::lib::Object* arg_dialog);
    virtual void play_sound(::cw::lib::String arg_name);
};

std::shared_ptr<MessageUiBackend> get_ui_backend();

// アクティブまたは明示した Frame を通して、一つの共通 Message モデルを表示
// する。ホストは Presentation を受け渡すだけで、別のダイアログを定義しない。
[[nodiscard]] std::optional<bool> show_common_modal_message(
    const ::cw::lib::String& arg_title,
    const ::cw::lib::String& arg_text,
    bool arg_question,
    bool arg_error = false);
[[nodiscard]] std::optional<bool> show_common_modal_message(
    const std::shared_ptr<::cw::frame::Frame>& arg_frame,
    const ::cw::lib::String& arg_title,
    const ::cw::lib::String& arg_text,
    bool arg_question,
    bool arg_error = false);
[[nodiscard]] std::optional<::cw::lib::Integer> show_common_modal_choices(
    const ::cw::lib::String& arg_title,
    const ::cw::lib::String& arg_text,
    const std::vector<MessageChoice>& arg_choices);

::cw::lib::Integer SysMessage___init___extent_w(::cw::lib::String arg_s);
void SysMessage___init___func(::cw::lib::String arg_key);
void SysMessage___init___func_OnCheck(std::shared_ptr<::cw::lib::Window> arg_event);
void main();
}

