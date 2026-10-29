#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/lib/ui_surface.hpp"

namespace cw::debug::logging {
// cw.debug.logging.DebugLog: シナリオ終了時に表示する集計結果。
struct DebugLogDisplayLine {
    ::cw::lib::String text{};
    std::vector<::cw::lib::String> icons{};
    // プレミア・レアの印は、カード名と閉じる引用符の間に置く。
    // 先頭アイコンとは別に位置を保持し、ネイティブ描画器が翻
    // 訳済みの表示文字列から位置を推測せずに済むようにする。
    std::optional<std::pair<::cw::lib::String, std::size_t>> inline_icon{};
    ::cw::lib::Integer indent{};
    bool operator==(const DebugLogDisplayLine&) const = default;
};

class DebugLog : public ::cw::lib::Object {
public:
    DebugLog() = default;
    virtual ~DebugLog() = default;
    std::vector<std::tuple<::cw::lib::String, bool>> m_compstamp{};
    std::vector<::cw::lib::String> m_flags{};
    std::vector<::cw::lib::String> m_friend{};
    std::vector<std::tuple<::cw::lib::String, bool>> m_gossip{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>, ::cw::lib::Integer> m_got_card{};
    bool m_is_removevariables{};
    std::vector<::cw::lib::String> m_jpdc_image{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>, ::cw::lib::Integer> m_lost_card{};
    std::vector<std::tuple<::cw::lib::String, bool>> m_lost_player{};
    std::pair<::cw::lib::Integer, ::cw::lib::Integer> m_money{0, 0};
    std::optional<::cw::lib::Real> m_pausedtime{};
    std::vector<std::tuple<::cw::lib::String, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>>, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>>>> m_player{};
    ::cw::lib::String m_sname{};
    std::optional<::cw::lib::DateTime> m_startdatetime{};
    std::vector<::cw::lib::String> m_steps{};
    std::vector<::cw::lib::String> m_variants{};
    void initialize(::cw::lib::String arg_sname);
    void add_friend(std::shared_ptr<::cw::sprite::card::FriendCard> arg_fcard);
    void add_lostplayer(std::shared_ptr<::cw::character::Character> arg_ccard);
    void add_player(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_pcard, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> arg_got_coupons, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> arg_lost_coupons);
    void set_money(::cw::lib::Integer arg_before, ::cw::lib::Integer arg_after);
    void add_gotcard(::cw::lib::String arg_ctype, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_scenario, ::cw::lib::String arg_author, ::cw::lib::String arg_premium);
    void add_lostcard(::cw::lib::String arg_ctype, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_scenario, ::cw::lib::String arg_author, ::cw::lib::String arg_premium);
    void add_compstamp(::cw::lib::String arg_compstamp, bool arg_get);
    void add_gossip(::cw::lib::String arg_gossip, bool arg_get);
    void add_jpdcimage(::cw::lib::String arg_fname);
    void add_flag(::cw::lib::String arg_name);
    void add_step(::cw::lib::String arg_name);
    void add_variant(::cw::lib::String arg_name);
    void remove_variables();
    void set_times(std::optional<::cw::lib::DateTime> arg_startdatetime, ::cw::lib::Real arg_pausedtime);
};

// wx.RichTextCtrl の表示内容は互換Dialogへ保持し、ネイティブ側では
// デバッグ画像とインデントを含む表示行として描画する。表示アイコンは
// 生成済みの文章を再解析せず、DebugLogの意味情報から構築する。
class DebugLogDialog : public ::cw::lib::Dialog,
                       public ::cw::dialog::DialogPresentationProvider,
                       public std::enable_shared_from_this<DebugLogDialog> {
public:
    DebugLogDialog() = default;
    virtual ~DebugLogDialog() = default;
    bool m_cwpy_debug{};
    std::uintptr_t m_native_parent_handle{};
    ::cw::lib::Value m_okbtn{};
    std::vector<DebugLogDisplayLine> m_display_lines{};
    ::cw::lib::String m_plain_text{};
    std::shared_ptr<::cw::util::CWPyRichTextCtrl> m_text{};
    ::cw::lib::Value m_writetext{};
    ::cw::lib::String m_title{};
    ::cw::lib::Integer m_presentation_scroll{};
    bool m_presentation_finished{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::debug::logging::DebugLog> arg_debuglog);
    void _bind();
    void OnClose(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
        int index, float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override {
        m_selecting=false;
        m_scroll_drag=false;
        request_dialog_presentation_refresh();
    }
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_context(float x, float y) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
        ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    virtual void write_result(const ::cw::lib::String& text);
    virtual void copy_result(const ::cw::lib::String& text);
    virtual std::shared_ptr<::cw::lib::Surface> result_icon(const ::cw::lib::String& key);
private:
    struct DisplayRow {
        ::cw::lib::ui::WrappedLabelRow label;
        ::cw::lib::Integer indent{};
        std::size_t base{};
    };
    std::vector<DisplayRow> m_rows;
    std::vector<DebugLogDisplayLine> m_layout_lines;
    ::cw::lib::String m_copy_text;
    ::cw::lib::ui::FontStyle m_body_font;
    ::cw::lib::Rect m_viewport;
    ::cw::lib::Integer m_row_height{}, m_visible_rows{}, m_max_scroll{}, m_layout_width{};
    std::size_t m_anchor{}, m_caret{};
    bool m_selecting{}, m_scroll_drag{}, m_popup{}, m_close_notified{};
    float m_scroll_grab{}, m_popup_x{}, m_popup_y{};
    int m_focus{}, m_popup_row{};
    void ensure_layout();
    void scroll_to(::cw::lib::Integer first);
    std::size_t text_hit(float x,float y) const;
    void select_text(std::size_t anchor,std::size_t caret);
    void copy_selection();
};

// DebugLogDialogの共通Presentationとホスト通知を分離する。
// backendは別のcontrol階層を生成してはならない。
class DebugLogUiBackend : public ::cw::lib::Object {
public:
    virtual ~DebugLogUiBackend() = default;
    virtual void show(DebugLogDialog* arg_dialog, const ::cw::lib::String& arg_title,
                      const ::cw::lib::String& arg_text);
    virtual void layout(DebugLogDialog* arg_dialog);
    virtual void close(DebugLogDialog* arg_dialog);
    virtual void report_error(const ::cw::lib::String& arg_message);
};

std::shared_ptr<DebugLogUiBackend> get_ui_backend();

void DebugLogDialog___init___func();
::cw::lib::String DebugLogDialog___init___timestr(::cw::lib::Real arg_sec);
void main();
}

