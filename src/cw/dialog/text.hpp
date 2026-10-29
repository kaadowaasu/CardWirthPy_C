#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/incrementalsearch.hpp"
#include "cw/dialog/button_repeat.hpp"
#include "cw/lib/ui_surface.hpp"

#include <map>

namespace cw::dialog::text {
class ReadmeData : public ::cw::lib::Object {
public:
    ::cw::lib::String m_name{};
    std::vector<::cw::lib::String> m_noextname{};
    ::cw::lib::Bytes m_content{};
    void initialize(::cw::lib::String name, ::cw::lib::Bytes content);
};
using ReadmeList = std::vector<std::shared_ptr<ReadmeData>>;

// Python の親はリストオブジェクトを返し、その場での並べ替えを外か
// ら観測できる。Readme::update_lists が親に届く前に
// 任意のプロバイダーへ複製せず、共通の親境界で同じオブジェクトを保つ。
class ReadmeSource {
public:
    virtual ~ReadmeSource() = default;
    virtual std::shared_ptr<ReadmeList> get_texts() = 0;
};
enum class TextPresentationKind { Readme, Comment, ErrorLog };
enum class TextFocus { Body, File, Close, Previous, Next };
class Text : public ::cw::lib::Dialog,
             public ::cw::dialog::DialogPresentationProvider {
public:
    ~Text() override;
    std::shared_ptr<::cw::lib::Window> m_parent{};
    // wx は親子グラフ内にウィンドウの識別情報を保つ。こ
    // の借用したネイティブ識別子はモデルの寿命中に保持す
    // るが、モデルを所有せず、自己所有の循環を作らない。
    std::shared_ptr<::cw::lib::Window> m_window_identity_typed{};
    bool m_cwpy_debug{false};
    // wx.Panel/Controlの実体はUI adapterが所有するためValueで保持する。
    // Python版Textの公開メンバ名と対応させ、状態層からも参照可能にする。
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_panel{};
    ::cw::lib::Value m_foreground{};
    ::cw::lib::Value m_closebtn{};
    ::cw::lib::Value m_leftbtn{};
    ::cw::lib::Value m_rightbtn{};
    ::cw::lib::Value m_combo{};
    ::cw::lib::Value m_leftpagekeyid{};
    ::cw::lib::Value m_rightpagekeyid{};
    ::cw::lib::Value m_upkeyid{};
    ::cw::lib::Value m_downkeyid{};
    std::vector<::cw::lib::String> m_list{};
    std::vector<::cw::lib::Bytes> m_list2{};
    ::cw::lib::Integer m_index{0}, m_index2{0};
    ::cw::lib::Bytes m_current_text{};
    ::cw::lib::String m_display_text{};
    std::vector<std::pair<std::size_t, std::size_t>> m_url_ranges{};
    std::pair<std::size_t, std::size_t> m_selection{0, 0};
    bool m_searchmenu{true};
    bool m_editable{false};
    bool m_newline_appended{false};
    std::shared_ptr<::cw::util::CWPyRichTextCtrl> m_richtextctrl{};
    std::shared_ptr<::cw::lib::Panel> m_toppanel_typed{};
    std::shared_ptr<::cw::lib::Panel> m_panel_typed{};
    std::shared_ptr<::cw::lib::Control> m_closebtn_typed{};
    std::shared_ptr<::cw::lib::Control> m_leftbtn_typed{};
    std::shared_ptr<::cw::lib::Control> m_rightbtn_typed{};
    std::shared_ptr<::cw::lib::Control> m_combo_typed{};
    std::shared_ptr<::cw::lib::Font> m_text_font_typed{};
    std::shared_ptr<::cw::lib::Bitmap> m_paint_surface_typed{};
    bool m_left_enabled{false}, m_right_enabled{false}, m_content_visible{false};
    bool m_combo_enabled{false};
    bool m_closed{false}, m_cancelled{false};
    bool m_drag_scrolling{false};
    float m_drag_start_y{};
    ::cw::lib::Integer m_drag_start_line{};
    std::function<void(bool)> m_navigate_scenario{};
    std::function<::cw::lib::Integer()> m_scenario_count{};
    std::function<void()> m_presentation_close_callback{};
    void initialize(std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String name);
    // テキスト・選択・スクロール・メニューは一つの実装を使い、スタイルだけをダイア
    // ログごとに変える。OS 固有コントロールや簡略化したテキスト表示は作らない。
    void initialize_plain(std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String title,
                          ::cw::lib::String text, TextPresentationKind kind);
    ::cw::lib::Size presentation_size();
    void dialog_presentation_resize(::cw::lib::Size size) override;
    void _set_text(::cw::lib::Bytes value);
    void OnCombobox(std::shared_ptr<::cw::lib::Window> event);
    void OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnClickRightBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> event);
    void OnUp(std::shared_ptr<::cw::lib::Window> event);
    void OnDown(std::shared_ptr<::cw::lib::Window> event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> event);
    bool dispatch_command(::cw::lib::Integer command_id, std::shared_ptr<::cw::lib::Window> event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> event);
    void _do_layout();
    void _enable_btn();
    void set_scenario_navigation(std::function<void(bool)> navigate, std::function<::cw::lib::Integer()> count);
    virtual void update_lists();
    void upddate_lists();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,
                                 bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                      int index, float x, float y,
                                      bool pressed) override;
    void dialog_presentation_hover(float x, float y) override;
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_wheel(float x, float y, ::cw::lib::Integer rotation);
    void dialog_presentation_pointer_cancel() override {
        m_page_capture=m_page_repeat=0;
        m_drag_scrolling=false;
        m_selecting=false;
        m_pointer_moved=false;
        request_dialog_presentation_refresh();
    }
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override {
        m_input_modifiers=modifiers;
    }
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text(const std::string& text, bool composition) override;
    [[nodiscard]] TextFocus presentation_focus() const noexcept { return m_focus; }
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual ::cw::dialog::ButtonRepeatTimer::Clock::time_point text_input_time() const noexcept;
    void repeat_navigation();
    std::vector<::cw::dialog::DialogPresentationControl> m_page_controls;
    // ホストは外部への操作だけを担当し、配置・選択・メニューは共通化する。
    virtual void copy_text(const ::cw::lib::String& text);
    virtual void open_text_url(const ::cw::lib::String& url);
private:
    ::cw::dialog::ButtonRepeatTimer m_page_timer;
    int m_page_capture{},m_page_repeat{};
    TextPresentationKind m_presentation_kind{TextPresentationKind::Readme};
    ::cw::lib::ui::SystemDialogMetrics m_system_metrics;
    ::cw::lib::Size m_surface_size;
    std::optional<::cw::lib::Size> m_readme_viewport;
    ::cw::lib::Rect m_text_field, m_scroll_track, m_close_rect;
    ::cw::lib::Integer m_footer_top{};
    TextFocus m_focus{TextFocus::Body};
    ::cw::dialog::IncrementalSearch m_type_search;
    std::optional<::cw::dialog::DialogPresentationKey> m_popup_release_key{};
    ::cw::lib::Rect m_combo_rect{};
    std::vector<TextFocus> focus_order() const;
    void normalize_focus();
    void advance_focus(bool backwards);
    ::cw::dialog::DialogPresentationModifiers m_input_modifiers{};
    ::cw::lib::Integer text_scale(::cw::lib::Integer value) const;
    struct MenuEntry {
        ::cw::lib::String label;
        int command;
        bool enabled;
        // Readme の wx.Menu は二つの標準コマ
        // ンドにアクセスキーを持つ。その意味情報を描画ラベル
        // と分離し、ネイティブメニューなしで共通 Presentation
        // のメニューが処理できるようにする。
        char mnemonic{};
        bool separator_before{};
    };
    std::vector<::cw::lib::ui::WrappedLabelRow> m_rows;
    // Python にはない性能上の拡張。Readme の描画済み行を保持し
    // 、スクロールでは新しく見える行だけを描く。再配置時にはすべて消去する。
    std::map<::cw::lib::String, std::shared_ptr<::cw::lib::Surface>> m_row_images;
    ::cw::lib::String m_layout_text;
    ::cw::lib::ui::FontStyle m_body_font;
    ::cw::lib::Rect m_body_rect;
    ::cw::lib::Integer m_row_height{}, m_visible_rows{}, m_layout_width{};
    std::size_t m_anchor{}, m_caret{}, m_pointer_down{};
    bool m_selecting{}, m_pointer_moved{};
    int m_popup{}, m_popup_row{};
    float m_popup_x{}, m_popup_y{};
    void ensure_text_layout();
    void scroll_text(::cw::lib::Integer first);
    std::size_t text_hit(float x, float y) const;
    void select_text(std::size_t anchor, std::size_t caret);
    std::vector<MenuEntry> text_menu() const;
    void detach_parent();
};
class Readme : public Text {
public:
    std::vector<std::shared_ptr<ReadmeData>> m_sources{};
    std::function<std::vector<std::shared_ptr<ReadmeData>>()> m_source_provider{};
    void initialize(std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String name,
                    ReadmeList& lists);
    void initialize(std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String name,
                    ReadmeList&& lists) { initialize(std::move(parent),std::move(name),lists); }
    void update_lists() override;
    void set_source_provider(std::function<std::vector<std::shared_ptr<ReadmeData>>()> provider);
private:
    void assign_lists(ReadmeList& lists);
};

// ホスト側も同じ完全な Readme のデータ・モデルを表示
// し、OS 専用の簡略画面は作らない。バックエンドは表示と入
// 力だけを担当し、表示前に配置と所有者基準の位置決めを完了す
// る。最初のフレームが所有者から外れて表示されることを防ぐ。
bool show_common_readme(
    std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String title,
    ReadmeList& lists, std::function<void(bool)> navigate = {},
    std::function<::cw::lib::Integer()> count = {},
    std::function<ReadmeList()> provider = {});
inline bool show_common_readme(
    std::shared_ptr<::cw::lib::Window> parent, ::cw::lib::String title,
    ReadmeList&& lists, std::function<void(bool)> navigate = {},
    std::function<::cw::lib::Integer()> count = {}, std::function<ReadmeList()> provider = {}) {
    return show_common_readme(std::move(parent),std::move(title),lists,
        std::move(navigate),std::move(count),std::move(provider));
}
void main();
}
