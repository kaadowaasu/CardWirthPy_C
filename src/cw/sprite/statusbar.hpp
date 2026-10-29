#pragma once

// cw/sprite/statusbar.py の C++ 移植。SDL・ネイティブ操作は StatusBarHostServices
// が提供し、このファイルは共通状態と RGBA モデルを保持する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::statusbar {

// StatusBar が一つの共通配置と RGBA モデルを所有する。ホス
// トはリソース、文字、合成、音声、入力の受け渡しサービスだけを提供する。
struct StatusBarHostServices {
    using SurfacePtr = std::shared_ptr<::cw::lib::Surface>;
    using ObjectPtr = std::shared_ptr<::cw::lib::Object>;
    std::function<SurfacePtr(const ::cw::lib::String&, ::cw::lib::Integer, ::cw::lib::Color)> load_resource{};
    std::function<SurfacePtr(const ::cw::lib::String&, const ::cw::lib::String&, ::cw::lib::Integer,
                             ::cw::lib::Color, bool)> render_text{};
    std::function<void(::cw::lib::Integer, const std::vector<ObjectPtr>&)> add_layer{};
    std::function<void(::cw::lib::Integer)> remove_layer{};
    std::function<void(const ::cw::lib::Rect&)> invalidate{};
    std::function<void(const SurfacePtr&, const ::cw::lib::Rect&, ::cw::lib::Integer)> draw_surface{};
    std::function<void(::cw::lib::Integer, ::cw::lib::Integer, bool)> draw_edge_pixel{};
    std::function<void(const ::cw::lib::String&)> play_sound{};
    std::function<void(::cw::lib::Integer)> inject_mouse_button_up{};
    struct TextMetrics {
        ::cw::lib::Size size{};
        ::cw::lib::Integer font_height{};
    };
    // Py の Font.size() と Font.get_height() は、描画済み Surface
    // の高さとは独立する。ホストはフォントの基本操作だけを提供し、Desc が配置を管理する。
    std::function<std::optional<TextMetrics>(const ::cw::lib::String&,
        const ::cw::lib::String&, ::cw::lib::Integer, bool)> measure_text{};
};

void set_host_services(std::shared_ptr<StatusBarHostServices> arg_services);
std::shared_ptr<StatusBarHostServices> host_services();

class Desc : public ::cw::sprite::base::CWPySprite {
public:
    Desc() = default;
    virtual ~Desc() = default;
    ::cw::lib::Value m_desc{};
    ::cw::lib::Value m_hotkey{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_rect{};
    // 親・文章・矢印位置を描画バックエンドから独立して保持する。Python
    // のグラフは循環回収されるが、C++ では親が m__desc を
    // 所有するため Desc から親を所有しない。弱参照で結び、ステータ
    // スバーの再構築後に古いホバーのオブジェクトが残らないようにする。
    std::weak_ptr<::cw::lib::Object> m_parent_typed{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::String m_desc_typed{};
    ::cw::lib::String m_hotkey_typed{};
    std::optional<::cw::lib::Integer> m_arrowpos_typed{};
    void initialize(::cw::lib::Value arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey, std::optional<::cw::lib::Integer> arg_arrowpos);
};

class ProgressView : public ::cw::sprite::base::CWPySprite {
public:
    ProgressView() = default;
    virtual ~ProgressView() = default;
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m__last_params{};
    ::cw::lib::Integer m_current{};
    ::cw::lib::Integer m_max{};
    ::cw::lib::Integer m_min{};
    ::cw::lib::String m_text{};
    std::weak_ptr<::cw::sprite::statusbar::StatusBar> m_parent{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size, ::cw::lib::String arg_text, ::cw::lib::Integer arg_nmax, ::cw::lib::Integer arg_nmin, ::cw::lib::Integer arg_current);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_image();
};

class StatusBar : public ::cw::sprite::base::CWPySprite,
                  public std::enable_shared_from_this<StatusBar> {
public:
    StatusBar() = default;
    virtual ~StatusBar() = default;
    bool m__init{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::AutoStartButton>> m_autostart{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::BacklogButton>> m_backlog{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::DebuggerButton>> m_debugger{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::DebugLogButton>> m_debuglog{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::ShowFriendCardsButton>> m_friendcards{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::HelpButton>> m_help{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::InfoCardsButton>> m_infocards{};
    bool m_loading{};
    std::optional<bool> m_maskmode{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::PartyMoneyPanel>> m_partymoney{};
    // 現在配置したスプライトを、次の Python 形式の clear()
    // まで保持する。バックエンドはレイヤーごとの集合を使う。
    std::vector<std::shared_ptr<::cw::lib::Object>> m_status_items{};
    std::vector<std::shared_ptr<::cw::lib::Object>> m_status_progress{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::SettingsButton>> m_settings{};
    bool m_showbuttons{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::TouchMenuButton>> m_touchmenu{};
    std::shared_ptr<::cw::sprite::statusbar::VolumeBar> m_volumebar{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::YadoMoneyPanel>> m_yadomoney{};
    void initialize();
    bool is_initialized();
    void init();
    void _init_image();
    void update_scale() override;
    void change(bool arg_showbuttons, bool arg_encounter);
    void clear();
    void hide_descs();
    void _create_autostart(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_debugger(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_backlog(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_settings(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_help(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_touchmenu(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_yadomoney(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_partymoney(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_infocards(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_friendcards(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void _create_debuglog(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update_volumebar();
    bool clear_volumebar();
    bool is_showingvolumebar();
    std::vector<std::shared_ptr<::cw::lib::Object>> layered_draw_ex(
        std::shared_ptr<::cw::lib::Object> arg_layered_updates,
        std::shared_ptr<::cw::lib::Surface> arg_surface,
        bool arg_draw_desc,
        std::optional<::cw::lib::Rect> arg_requested_clip = std::nullopt);
    void hide_touchbuttons(bool arg_redraw);
    void update_tiles();
};

std::shared_ptr<::cw::sprite::base::SelectableSprite> find_retained_selectable(
    const StatusBar& bar, const ::cw::sprite::base::SelectableSprite* raw);

class VolumeBar : public ::cw::sprite::base::CWPySprite {
public:
    VolumeBar() = default;
    virtual ~VolumeBar() = default;
    ::cw::lib::Real m_upscr{};
    ::cw::lib::Real m_volume{};
    void initialize();
    void update_scale();
    bool clear_image();
};

class ExpandView : public ::cw::sprite::statusbar::ProgressView {
public:
    ExpandView() = default;
    virtual ~ExpandView() = default;
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
};

class StatusBarButton : public ::cw::sprite::base::SelectableSprite {
public:
    StatusBarButton() = default;
    virtual ~StatusBarButton() = default;
    bool m__blink_notice{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::Desc>> m__desc{};
    ::cw::lib::Real m__upscr{};
    std::map<std::tuple<::cw::lib::Integer, bool>, std::shared_ptr<::cw::lib::Surface>> m_btnimg{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::Value m_enabled{};
    ::cw::lib::Integer m_frame{};
    ::cw::lib::Value m_hotkey{};
    ::cw::lib::Value m_icon{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_is_emphasize{};
    bool m_is_pushed{};
    std::optional<::cw::lib::Callback> m_is_showing{};
    bool m_is_statusctrl{};
    std::optional<bool> m_maskmode{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_noimg{};
    ::cw::lib::Value m_notice{};
    ::cw::lib::Value m_number{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_sizetype{};
    ::cw::lib::String m_status{};
    std::shared_ptr<::cw::sprite::statusbar::StatusBar> m_parent_typed{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::Integer m_sizetype_typed{};
    std::shared_ptr<::cw::lib::Surface> m_icon_typed{};
    std::shared_ptr<::cw::lib::Surface> m_image_typed{};
    std::shared_ptr<::cw::lib::Surface> m_noimg_typed{};
    ::cw::lib::String m_desc_typed{};
    ::cw::lib::String m_hotkey_typed{};
    bool m_enabled_typed{true};
    bool m_is_emphasize_typed{};
    bool m_notice_typed{};
    std::optional<::cw::lib::Integer> m_number_typed{};
    std::function<bool()> m_is_showing_typed{};
    std::optional<::cw::lib::Rect> m_handling_rect_typed{};
    bool m_input_dispatching{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, ::cw::lib::String arg_name, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, ::cw::lib::Integer arg_sizetype, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, bool arg_enabled, bool arg_is_pushed, bool arg_notice, std::optional<::cw::lib::Integer> arg_number, bool arg_is_emphasize, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey);
    void _create_paneimg(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon);
    virtual std::shared_ptr<::cw::lib::Surface> get_icon();
    virtual void reset(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    // Py の通知フラグは一つ。ネイティブの型付き状態と互換表示を同期する。
    void set_notice(bool arg_notice);
    bool _is_notice();
    std::shared_ptr<::cw::lib::Surface> get_btnimg(::cw::lib::Integer arg_flags);
    std::shared_ptr<::cw::lib::Surface> get_unselectedimage();
    std::shared_ptr<::cw::lib::Surface> get_selectedimage();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_normal();
    void update_click();
    void update_blink();
    bool need_update(bool arg_blink_notice);
    void put_updatekey(bool arg_blink_notice);
    void set_desc(::cw::lib::String arg_desc);
    void hide_desc();
    bool is_selection();
    void update_image();
    bool can_activate() const;
    // ALL は別のステータスダイアログを描くが、操作
    // の実体はこれらの既存ボタンが保持する。この入口は
    // CWPy のゲームスレッドで呼び、非表示スプ
    // ライトのクリックアニメーションは意図的に省く。
    bool activate_from_host();
    [[nodiscard]] bool is_showing_for_host() const;
    // C++ の呼び出しはネイティブのモーダル終了をまたいで古いステータス
    // バースプライトを保持し得る。Python/wx の実行条件を上書き不
    // 能な一つの入口にまとめ、拒否後に派生操作が実行されないようにする。
    void lclick_event() override final;
    void rclick_event() override;

protected:
    virtual bool uses_click_animation() const;
    virtual void on_activate();
};

class StatusBarPanel : public ::cw::sprite::base::MouseHandlerSprite {
public:
    StatusBarPanel() = default;
    virtual ~StatusBarPanel() = default;
    ::cw::lib::Value m__color{};
    std::optional<std::shared_ptr<::cw::sprite::statusbar::Desc>> m__desc{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::Value m_handling_rect{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_icon{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_noimg{};
    ::cw::lib::Value m_panelimg{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_size{};
    std::shared_ptr<::cw::sprite::statusbar::StatusBar> m_parent_typed{};
    ::cw::lib::Color m_color_typed{};
    std::shared_ptr<::cw::lib::Surface> m_icon_typed{};
    std::shared_ptr<::cw::lib::Surface> m_panelimg_typed{};
    std::shared_ptr<::cw::lib::Surface> m_noimg_typed{};
    ::cw::lib::String m_desc_typed{};
    ::cw::lib::Size m_size_typed{};
    std::optional<::cw::lib::Rect> m_handling_rect_typed{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, ::cw::lib::String arg_desc);
    void _create_paneimg(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon);
    virtual std::shared_ptr<::cw::lib::Surface> get_icon();
    virtual void reset(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    void set_desc(::cw::lib::String arg_desc);
    void hide_desc();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    // Py の reset/update は、派生パネルの文字も含め self.update_image を呼ぶ。
    virtual void update_image();
    void set_backcolor(std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color);
    std::shared_ptr<::cw::lib::Surface> get_scaledimage(std::shared_ptr<::cw::lib::Surface> arg_image);
    bool is_shown();
};

class ActionButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    ActionButton() = default;
    virtual ~ActionButton() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void on_activate() override;
};

class AutoStartButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    AutoStartButton() = default;
    virtual ~AutoStartButton() = default;
    std::optional<std::shared_ptr<::cw::sprite::statusbar::ActionButton>> m_actionbtn{};
    ::cw::lib::Value m_desc{};
    bool m_is_pushed{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void reset(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    bool uses_click_animation() const override;
    void on_activate() override;
};

class BacklogButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    BacklogButton() = default;
    virtual ~BacklogButton() = default;
    ::cw::lib::Value m_enabled{};
    bool m_is_pushed{};
    ::cw::lib::Value m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void reset(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    bool uses_click_animation() const override;
    void on_activate() override;
};

class CampButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    CampButton() = default;
    virtual ~CampButton() = default;
    bool m_is_pushed{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    bool uses_click_animation() const override;
    void on_activate() override;
};

class CancelButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    CancelButton() = default;
    virtual ~CancelButton() = default;
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void on_activate() override;
};

class DebugLogButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    DebugLogButton() = default;
    virtual ~DebugLogButton() = default;
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void reset(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void on_activate() override;
};

class DebuggerButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    DebuggerButton() = default;
    virtual ~DebuggerButton() = default;
    bool m_is_pushed{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    bool uses_click_animation() const override;
    void on_activate() override;
};

class EncounterPanel : public ::cw::sprite::statusbar::StatusBarPanel {
public:
    EncounterPanel() = default;
    virtual ~EncounterPanel() = default;
    ::cw::lib::Value m_image{};
    std::optional<::cw::lib::String> m_text{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_image() override;
};

class HelpButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    HelpButton() = default;
    virtual ~HelpButton() = default;
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void on_activate() override;
};

class InfoCardsButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    InfoCardsButton() = default;
    virtual ~InfoCardsButton() = default;
    ::cw::lib::Value m_is_showing{};
    ::cw::lib::Value m_number{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void reset(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void on_activate() override;
};

class RunAwayButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    RunAwayButton() = default;
    virtual ~RunAwayButton() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void on_activate() override;
};

class SettingsButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    SettingsButton() = default;
    virtual ~SettingsButton() = default;
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void on_activate() override;
};

class ShowFriendCardsButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    ShowFriendCardsButton() = default;
    virtual ~ShowFriendCardsButton() = default;
    ::cw::lib::Value m_is_pushed{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    bool uses_click_animation() const override;
    void on_activate() override;
};

class TableButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    TableButton() = default;
    virtual ~TableButton() = default;
    bool m_is_pushed{};
    ::cw::lib::Value m_is_showing{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    bool uses_click_animation() const override;
    void on_activate() override;
};

class TouchMenuButton : public ::cw::sprite::statusbar::StatusBarButton {
public:
    TouchMenuButton() = default;
    virtual ~TouchMenuButton() = default;
    std::optional<std::tuple<::cw::lib::Real, std::map<::cw::lib::String, ::cw::lib::String>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>>> m__cachekey{};
    std::vector<::cw::lib::Value> m__touchbuttons{};
    std::vector<std::shared_ptr<::cw::lib::Object>> m_touchbuttons_typed{};
    // Python はレイヤー非表示中も _touchbuttons を再利用可
    // 能なキャッシュとして保持する。表示状態とキャッシュの所有を別に管理する。
    bool m_touchbuttons_visible{};
    bool m_is_pushed{};
    bool m_selectable_on_event{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    bool uses_click_animation() const override;
    std::tuple<::cw::lib::Real, std::map<::cw::lib::String, ::cw::lib::String>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>> _upscrmemo();
    void on_activate() override;
};

class YadoMoneyPanel : public ::cw::sprite::statusbar::StatusBarPanel {
public:
    YadoMoneyPanel() = default;
    virtual ~YadoMoneyPanel() = default;
    ::cw::lib::String m_currency{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::Integer m_frame{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_status{};
    std::optional<::cw::lib::String> m_text{};
    ::cw::lib::Real m_up_scr{};
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void reset(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    bool need_update(::cw::lib::String arg_text, ::cw::lib::String arg_currency);
    void put_updatekey(::cw::lib::String arg_text, ::cw::lib::String arg_currency);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    virtual ::cw::lib::String get_money();
    void update_image() override;
    virtual void update_color();
    void update_blink();
};

class PartyMoneyPanel : public ::cw::sprite::statusbar::YadoMoneyPanel {
public:
    PartyMoneyPanel() = default;
    virtual ~PartyMoneyPanel() = default;
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void reset(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    std::shared_ptr<::cw::lib::Surface> get_icon();
    ::cw::lib::String get_money();
    ::cw::lib::Integer _get_money_int();
    void update_color();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
};

class RoundCounterPanel : public ::cw::sprite::statusbar::YadoMoneyPanel {
public:
    RoundCounterPanel() = default;
    virtual ~RoundCounterPanel() = default;
    // Py の image 属性が一つであるように、継承した画像の保持欄を共有
    // する。text も継承した金額パネルの初期化と共有し、同名の第二のフィ
    // ールドを作らず YadoMoneyPanel::m_text を使う。
    void initialize(std::shared_ptr<::cw::sprite::statusbar::StatusBar> arg_parent, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_image() override;
};

extern ::cw::lib::Integer g_LAYER_BASE;
extern ::cw::lib::Integer g_LAYER_DESC;
extern ::cw::lib::Integer g_LAYER_MESSAGE;
extern ::cw::lib::Integer g_LAYER_MESSAGE_LOG;
extern ::cw::lib::Integer g_LAYER_MESSAGE_LOG_CURTAIN;
extern ::cw::lib::Integer g_LAYER_STATUS_ITEM;
extern ::cw::lib::Integer g_LAYER_STATUS_PROGRESS;
extern ::cw::lib::Integer g_LAYER_TOUCH_BUTTON;
extern ::cw::lib::Integer g_LAYER_VOLUME_BAR;

void _draw_edge(std::shared_ptr<::cw::lib::Surface> arg_image);
void _draw_edge_put(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y);
void _draw_edge_put_inside(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y);
::cw::lib::Value StatusBarButton___init___lambda_line_845();
bool DebugLogButton___init___has_debuglog();
::cw::lib::Value DebugLogButton___init___lambda_line_1565();
void TouchMenuButton_lclick_event_lclick();
void TouchMenuButton_lclick_event_rclick();
void TouchMenuButton_lclick_event_f4();
void TouchMenuButton_lclick_event_f8();
void TouchMenuButton_lclick_event_f9();
void TouchMenuButton_lclick_event_debug_mode();
void TouchMenuButton_lclick_event_copy_text();
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1681();
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1685();
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1689();
bool TouchMenuButton_lclick_event_copybtn_enabled();
bool TouchMenuButton_lclick_event_has_instructions();
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1713();
// ステータスバー外で同じ金額行を表示するホスト（ALL のステータスダイアログ）用
// のアイコン。party=false は YadoMoneyPanel、true
// は PartyMoneyPanel。ゲームスレッドで呼び、新しい複製を返す。
std::shared_ptr<::cw::lib::Surface> status_money_icon(bool party);
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1743();
::cw::lib::Value TouchMenuButton_lclick_event_lambda_line_1750();
void main();
}

