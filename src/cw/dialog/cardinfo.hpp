#pragma once

#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/fwd.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/sprite/card.hpp"

namespace cw::dialog::cardinfo {
using CardInfoItem = std::variant<
    std::shared_ptr<::cw::sprite::card::MenuCard>,
    std::shared_ptr<::cw::header::CardHeader>,
    std::shared_ptr<::cw::header::InfoCardHeader>>;

class CardInfo;

// CardInfoの状態モデルとnative/SDL表示を分離する描画・modal境界。
// 未登録時もget_ui_backend()は全OS共通のportable実装を返す。
class CardInfoUiBackend : public ::cw::lib::Object {
public:
    virtual ~CardInfoUiBackend() = default;
    virtual void open(CardInfo& arg_dialog, const std::shared_ptr<::cw::lib::Window>& arg_parent);
    virtual void refresh(CardInfo& arg_dialog);
    virtual void layout(CardInfo& arg_dialog);
    virtual void close(CardInfo& arg_dialog);
    virtual void present(CardInfo& arg_dialog);
    virtual ::cw::lib::Integer show_modal(CardInfo& arg_dialog);
};

std::shared_ptr<CardInfoUiBackend> get_ui_backend();

class CardInfo : public ::cw::lib::Dialog,
                 public ::cw::dialog::DialogPresentationProvider,
                 public std::enable_shared_from_this<CardInfo>
{
public:
    static constexpr ::cw::lib::Integer kIdOk = 1;
    static constexpr ::cw::lib::Integer kIdCancel = 0;

    CardInfo() = default;
    virtual ~CardInfo();
    bool m__modal_ended{false};
    ::cw::lib::Integer m_modal_result{kIdCancel};
    ::cw::lib::Integer m_index{};
    std::vector<CardInfoItem> m_list{};
    std::shared_ptr<::cw::lib::Timer> m__animtimer{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::image::CardImage>> m__dialog_anim_cardimgs{};
    ::cw::lib::String m__dialog_anim_owner{};
    std::set<::cw::lib::String> m__dialog_anim_paths{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_csize{};
    bool m_cwpy_debug{};
    // キャラクター情報からのカード詳細はモードレスの子として開く。そ
    // の子を閉じても、所有元フレームのダイアログを閉じてはならない。
    bool m_close_frame_dialog{true};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_leftpagekeyid{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_rightpagekeyid{};
    ::cw::lib::String m_scedir{};
    std::optional<CardInfoItem> m_selection{};
    bool m_animtimer_running{false};
    // UIの実装は一つに統一し、
    // 別経路の実装を設けない。
    ::cw::lib::Real m_layout_scale{-1};
    ::cw::lib::ui::FontStyle m_layout_font;
    ::cw::lib::Integer m_description_lineheight{};
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    ::cw::dialog::ButtonRepeatTimer m_page_repeat_timer;
    int m_page_repeat{},m_page_capture{},m_focus_button{1};
    bool m_keyboard_focus{};
    std::vector<::cw::dialog::DialogPresentationControl> m_published_controls;
    std::function<void(const ::cw::lib::String&)> m_copy_writer{::cw::dialog::presentation_set_clipboard_text};
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point card_input_time() const noexcept;
    virtual std::chrono::milliseconds page_repeat_interval() const noexcept;
    ::cw::lib::Integer m_textheight{};
    ::cw::lib::Integer m_textwidth{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::String m_copy_text{};
    std::shared_ptr<::cw::lib::Bitmap> m_rendered_card{};
    ::cw::lib::String m_rendered_name{};
    ::cw::lib::String m_rendered_desc{};
    ::cw::lib::String m_rendered_source{};
    std::function<void(const CardInfoItem&)> m_change_selection{};
    std::function<void()> m_parent_draw{};
    std::function<void(const CardInfoItem&)> m_update_cardpocketinfo{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_scedir = "");
    void OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event);
    void copy_detail();
    [[nodiscard]] bool dialog_presentation_can_copy_detail() const noexcept override { return true; }
    void dialog_presentation_copy_detail() override { copy_detail(); }
    ::cw::lib::String detail_text();
    virtual void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void dialog_presentation_context(float x, float y) override;
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCloseButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event);
    void _sync_dialog_animated_paths();
    void _stop_animtimer();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::String get_source();
    ::cw::lib::String get_desc();
    void draw(bool arg_update = false);
    void _do_layout();
    void set_parent_callbacks(std::function<void(const CardInfoItem&)> change_selection,
                              std::function<void()> parent_draw,
                              std::function<void(const CardInfoItem&)> update_cardpocketinfo = {});
    void move_page(::cw::lib::Integer arg_delta);
    void _end_modal(::cw::lib::Integer arg_result);
    [[nodiscard]] ::cw::lib::Integer get_modal_result() const;
    [[nodiscard]] ::cw::lib::String get_copy_text() const;
    [[nodiscard]] std::optional<CardInfoItem> get_selection() const;
    ::cw::lib::Integer show_modal();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
        float x,float y,bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};

class MenuCardInfo : public CardInfo {
public:
    MenuCardInfo() = default;
    virtual ~MenuCardInfo() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent,
                    std::optional<CardInfoItem> arg_selection = std::nullopt);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
};

class YadoCardInfo : public CardInfo {
public:
    YadoCardInfo() = default;
    virtual ~YadoCardInfo() = default;
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<CardInfoItem> arg_clist,
                    CardInfoItem arg_selection, ::cw::lib::String arg_scedir = "");
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<::cw::lib::Value> arg_clist,
                    ::cw::lib::Value arg_selection, ::cw::lib::String arg_scedir = "");
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) override;
};

void CardInfo___init___recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl);
void main();
}

