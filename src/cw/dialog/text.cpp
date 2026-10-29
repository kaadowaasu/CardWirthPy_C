// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/text.hpp"
#include "cw/dialog/textedit.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/dialog/richtext_scroll.hpp"
#include "cw/imageretouch.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <memory>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cw::dialog::text {
using S=::cw::lib::String;
using I=::cw::lib::Integer;
using B=::cw::lib::Bytes;

namespace {

// どの wins() の倍率でも、説明文画面のフッターを小さく保つ。 30px の移動
// ボタンの上下は各 2px で足りるため、従来の 45px の帯で空白を拡大しない。
constexpr I readme_footer_height=34+::cw::dialog::kDialogFooterExtraHeight;
// 下部バーの移動・閉じるボタンをバーに合わせて高くし、ダイアログ全体も同じ分だけ高くする。
constexpr I footer_nav_height=30+::cw::dialog::kDialogFooterExtraHeight;
constexpr I footer_button_height=24+::cw::dialog::kDialogFooterExtraHeight;

std::vector<std::pair<std::size_t,std::size_t>> readme_url_ranges(const S& text)
{
    std::vector<std::pair<std::size_t,std::size_t>> result;
    std::size_t offset=0;
    for (const auto& [segment,is_url] : ::cw::util::CWPyRichTextCtrl_set_text_get_urls(text)) {
        if (is_url) result.emplace_back(offset,offset+segment.size());
        offset+=segment.size();
    }
    return result;
}

S lower_text(S value)
{
    // 参照元の実行環境である Python 3.8/Unicode 12.1 の str.lower() に合わせる。
    return ::cw::lib::encoding::python_lower(value);
}

S remove_extension(S name)
{
    // ReadmeData は共通の Python 互換 splitext を使
    // い、表示キーでは文字としての「/」だけを分割する。 物理パスの正規化はフ
    // ァイルシステムの呼び出し側で行い、この公開の生の名前モデルでは行わない。
    return std::get<0>(::cw::util::splitext(std::move(name)));
}

std::vector<S> split_key(S name)
{
    name=lower_text(remove_extension(std::move(name)));
    std::vector<S> out;
    std::size_t pos=0;
    while (true) {
        const auto slash=name.find('/', pos);
        out.push_back(name.substr(pos, slash==S::npos ? slash : slash-pos));
        if (slash==S::npos) break;
        pos=slash+1;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool data_less(const std::shared_ptr<ReadmeData>& left, const std::shared_ptr<ReadmeData>& right)
{
    if (!left || !right) return static_cast<bool>(right);
    const auto count=std::min(left->m_noextname.size(), right->m_noextname.size());
    for (std::size_t i=0; i<count; ++i) {
        const auto comparison=::cw::util::logical_compare(left->m_noextname[i], right->m_noextname[i]);
        if (comparison!=0) return comparison<0;
    }
    return left->m_noextname.size()<right->m_noextname.size();
}

void require_readme(const Text& text, const char* method)
{
    if (!dynamic_cast<const Readme*>(&text)) throw std::logic_error(S("Text::") + method + " requires Readme");
}

S message(const S& key, const S& fallback)
{
    return ::cw::setting::Setting::s_msgs ? ::cw::setting::Setting::s_msgs->get(key, fallback) : fallback;
}

S menu_label(const S& value, const char mnemonic)
{
    if (!mnemonic) return value;
    // wx.Menu は「コピー(&C)」を C に下線を付けた「コピー(C)
    // 」として描く。 共通描画面にはネイティブのアクセスキー書式がないため、
    // 見えるキー表記は残し、意味上のキーを MenuEntry に保存する。
    return value + "(" + S(1, mnemonic) + ")";
}

S menu_visible_label(const S& value)
{
    // wx は「&」をアクセスキーの印に使う。共通描画面は文字をそのまま描くた
    // め、 その印を取り除きつつ、エスケープした「&&」は文字として保持する。
    S result;
    result.reserve(value.size());
    for (std::size_t i=0;i<value.size();++i) {
        if (value[i]!='&') { result.push_back(value[i]); continue; }
        if (i+1<value.size() && value[i+1]=='&') { result.push_back('&'); ++i; }
    }
    return result;
}


}

void ReadmeData::initialize(S name, B content)
{
    m_name=std::move(name);
    m_content=std::move(content);
    m_noextname=split_key(m_name);
}

Text::~Text()
{
    // 生存期間に結び付いた借用中の識別情報を解放する前に、登録を解除す
    // る。 親子の弱参照を、このウィンドウモデルの破棄後に残さない。
    try {
        detach_parent();
        const auto self=std::shared_ptr<::cw::lib::Window>(this,[](::cw::lib::Window*){});
        ::cw::util::set_window_state(self,{});
        for (const auto& child:std::array<std::shared_ptr<::cw::lib::Window>,6>{
                 m_toppanel_typed,m_panel_typed,m_closebtn_typed,m_leftbtn_typed,m_rightbtn_typed,m_combo_typed})
            if (child) ::cw::util::set_window_state(child,{});
    } catch (...) { /* ホスト終了時の破棄処理から例外を出さない。 */ }
}

void Text::detach_parent()
{
    if (!m_parent) return;
    if (auto state=::cw::util::get_window_state(m_parent)) {
        auto& children=state->children;
        children.erase(std::remove_if(children.begin(),children.end(),
            [this](const auto& child){const auto window=child.lock(); return !window || window.get()==this;}),children.end());
        ::cw::util::set_window_state(m_parent,std::move(*state));
    }
}

void Text::initialize(std::shared_ptr<::cw::lib::Window> parent, S name)
{
    dialog_presentation_pointer_cancel();
    m_page_controls.clear();
    dialog_retained_surface().reset();
    if (dynamic_cast<const Readme*>(this)) m_presentation_kind=TextPresentationKind::Readme;
    if (m_presentation_kind==TextPresentationKind::Readme) require_readme(*this, "initialize");
    // 単体の説明文ダイアログは、初期フォーカスを「閉じる」に置く。 通
    // 常の読み取り専用コントロールは、既存の本文優先の動作を維持する。
    m_type_search.reset();
    m_readme_viewport.reset();
    m_focus=m_presentation_kind==TextPresentationKind::Readme?TextFocus::Close:TextFocus::Body;
    m_popup_release_key.reset();
    m_input_modifiers=0;
    detach_parent();
    m_parent=std::move(parent);
    title=std::move(name);
    bounds={0,0,550,290+::cw::dialog::kDialogFooterExtraHeight};
    m_cwpy_debug=false;
    m_closed=false;
    m_cancelled=false;
    m_drag_scrolling=false;
    m_drag_start_y=0.0F;
    m_drag_start_line=0;
    m_current_text.clear();
    m_display_text.clear();
    m_url_ranges.clear();
    m_selection={0,0};
    m_searchmenu=m_presentation_kind==TextPresentationKind::Readme;
    m_editable=false; // Python版はSetEditable(False)でリードオンリー。
    m_newline_appended=false;
    m_combo_enabled=false;

    m_richtextctrl=std::make_shared<::cw::util::CWPyRichTextCtrl>();
    m_toppanel_typed=std::make_shared<::cw::lib::Panel>();
    m_panel_typed=std::make_shared<::cw::lib::Panel>();
    m_closebtn_typed=std::make_shared<::cw::lib::Control>();
    m_leftbtn_typed=std::make_shared<::cw::lib::Control>();
    m_rightbtn_typed=std::make_shared<::cw::lib::Control>();
    m_combo_typed=std::make_shared<::cw::lib::Control>();
    m_closebtn_typed->visible=true;
    m_leftbtn_typed->visible=true;
    m_rightbtn_typed->visible=true;
    m_combo_typed->visible=true;
    m_text_font_typed=std::make_shared<::cw::lib::Font>();
    m_text_font_typed->point_size=14.0;
    m_toppanel_typed->bounds={0,0,550,290+::cw::dialog::kDialogFooterExtraHeight-readme_footer_height};
    m_panel_typed->bounds={0,290+::cw::dialog::kDialogFooterExtraHeight-readme_footer_height,550,readme_footer_height};
    m_toppanel=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_toppanel_typed));
    m_panel=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_panel_typed));
    m_closebtn=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_closebtn_typed));
    m_leftbtn=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_leftbtn_typed));
    m_rightbtn=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_rightbtn_typed));
    m_combo=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_combo_typed));
    m_foreground=::cw::lib::Bytes{211,211,211,255};
    m_richtextctrl->initialize(m_toppanel_typed,-1,"",{550,220},0,m_searchmenu);
    m_richtextctrl->m_scroll_line_typed=0;
    m_drag_scrolling=false;

    static I next_key_id=1;
    m_leftpagekeyid=next_key_id++;
    m_rightpagekeyid=next_key_id++;
    m_upkeyid=next_key_id++;
    m_downkeyid=next_key_id++;
    m_closebtn_typed->id=-1;
    m_closebtn_typed->title=message("close","Close");
    m_leftbtn_typed->id=m_leftpagekeyid.as_integer();
    m_rightbtn_typed->id=m_rightpagekeyid.as_integer();
    m_combo_typed->id=100;
    auto combo_state=::cw::util::WindowState{};
    combo_state.parent=m_toppanel_typed;
    combo_state.choices=m_list;
    ::cw::util::set_window_state(m_combo_typed,std::move(combo_state));
    ::cw::util::adjust_dropdownwidth(m_combo_typed);

    if (!m_window_identity_typed)
        m_window_identity_typed=std::shared_ptr<::cw::lib::Window>(this,[](::cw::lib::Window*){});
    const auto self=m_window_identity_typed;
    parent_window=m_parent;
    auto top_state=::cw::util::get_window_state(m_toppanel_typed).value_or(::cw::util::WindowState{});
    top_state.parent=self;
    top_state.children.emplace_back(m_combo_typed);
    ::cw::util::set_window_state(m_toppanel_typed,std::move(top_state));
    m_toppanel_typed->parent_window=self;
    m_combo_typed->parent_window=m_toppanel_typed;
    auto footer_state=::cw::util::WindowState{};
    footer_state.parent=self;
    footer_state.children={m_closebtn_typed,m_leftbtn_typed,m_rightbtn_typed};
    ::cw::util::set_window_state(m_panel_typed,std::move(footer_state));
    m_panel_typed->parent_window=self;
    for (const auto& button : {m_closebtn_typed,m_leftbtn_typed,m_rightbtn_typed}) {
        auto child_state=::cw::util::WindowState{};
        child_state.parent=m_panel_typed;
        ::cw::util::set_window_state(button,std::move(child_state));
        button->parent_window=m_panel_typed;
    }
    auto dialog_state=::cw::util::WindowState{};
    dialog_state.modal=true;
    dialog_state.parent=m_parent;
    dialog_state.children={m_toppanel_typed,m_panel_typed};
    ::cw::util::set_window_state(self,std::move(dialog_state));
    if (m_parent) {
        auto parent_state=::cw::util::get_window_state(m_parent).value_or(::cw::util::WindowState{});
        parent_state.children.emplace_back(self);
        ::cw::util::set_window_state(m_parent,std::move(parent_state));
    }
    ::cw::util::set_acceleratortable(self,
        // 公開の旧形式の表は、Py/wx の Ctrl とキーの値を維持する。 共通
        // Presentation の入力は、別途、意味上のキーとして受け取る。
        {{2,314,m_leftpagekeyid.as_integer()},{2,316,m_rightpagekeyid.as_integer()},
         {2,315,m_upkeyid.as_integer()},{2,317,m_downkeyid.as_integer()},
         {2,static_cast<I>('C'),-2},{2,static_cast<I>('A'),-3}},{});
    if (!m_list2.empty()) {
        const auto index=std::clamp<I>(m_index2,0,I(m_list2.size()-1));
        m_index=m_index2=index;
        _set_text(m_list2.at(static_cast<std::size_t>(index)));
    } else {
        m_index=m_index2=0;
        _set_text({});
    }
    m_content_visible=!m_list2.empty();
    m_combo_enabled=m_content_visible;
    visible=true;
    _enable_btn();
    _do_layout();
}

void Text::initialize_plain(std::shared_ptr<::cw::lib::Window> parent,S name,S content,TextPresentationKind kind)
{
    if(kind==TextPresentationKind::Readme) throw std::invalid_argument("plain text needs a plain presentation kind");
    m_presentation_kind=kind;
    m_system_metrics=::cw::lib::ui::system_dialog_metrics();
    m_list.clear();
    m_list2={B(content.begin(),content.end())};
    m_navigate_scenario={}; m_scenario_count={};
    Text::initialize(parent,std::move(name));
    parent_window=parent;
    native_handle=parent?parent->native_handle:0;
    cwpy_debug=m_cwpy_debug=kind==TextPresentationKind::ErrorLog;
    m_combo_enabled=false;
    m_left_enabled=m_right_enabled=false;
    if(m_combo_typed) m_combo_typed->visible=false;
    if(m_leftbtn_typed) m_leftbtn_typed->visible=false;
    if(m_rightbtn_typed) m_rightbtn_typed->visible=false;
    ensure_text_layout();
    bounds={0,0,m_surface_size.width,m_surface_size.height};
}

I Text::text_scale(I value) const
{
    return m_presentation_kind==TextPresentationKind::ErrorLog?
        m_system_metrics.scaled(value) : ::cw::lib::ui::win_i(value);
}

::cw::lib::Size Text::presentation_size()
{
    ensure_text_layout();
    return m_surface_size;
}

void Text::_set_text(B value)
{
    if (m_closed) return;
    if (!m_richtextctrl) throw std::logic_error("rich text is not initialized");
    m_current_text=value;
    m_newline_appended=false;
    if (m_presentation_kind==TextPresentationKind::Readme) {
        // Py と同様、リッチテキストの GetValue() を表示する。 描
        // 画・選択・URL の位置に、同じ共通のデコード・正規化済み結果を使う。
        m_richtextctrl->set_text(value,true);
        m_display_text=m_richtextctrl->m_text_typed;
        m_newline_appended=m_richtextctrl->m_newline_appended_typed;
    } else {
        m_display_text=S(value.begin(),value.end());
    }
    m_url_ranges.clear();
    // CWPyRichTextCtrl.set_text と同じ URL の分割規則を状態
    // 層にも保持する。 URL の装飾とバイト境界を守る当たり判定は、共通表示側で行う。
    if(m_presentation_kind==TextPresentationKind::Readme)
        m_url_ranges=readme_url_ranges(m_display_text);
    m_selection={0,0};
    m_anchor=m_caret=0;
    m_selecting=false;
    m_drag_scrolling=false;
    m_popup=0;
    m_rows.clear();
    if(m_presentation_kind!=TextPresentationKind::Readme) {
        // 既に UTF-8 なので、コピー用に CRLF・タブ・末尾改行の有無を保持する。
        // 通常の読み取り専用コントロールでは再デコードせず、URL を操作対象にも変えない。
        m_richtextctrl->m_text_typed=m_display_text;
        m_richtextctrl->m_url_segments_typed={{m_display_text,false}};
    }
    m_richtextctrl->m_selection_start_typed=0;
    m_richtextctrl->m_selection_end_typed=0;
    m_richtextctrl->m_scroll_line_typed=0;
    request_dialog_presentation_refresh();
}

void Text::OnCombobox(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed) return;
    require_readme(*this, "OnCombobox");
    // Python は空の文書一覧を添字で参照して例外を出す。ネ
    // イティブの選択画面はコンボを無効にするが、 直接・再構築時
    // の通知は届き得るため、古い選択表示を残さず文書を消去する。
    if (m_list2.empty()) {
        m_index=m_index2=0;
        _set_text({});
        m_content_visible=false;
        m_combo_enabled=false;
        return;
    }
    // Py と同様、コマンドイベントの ID・文字列ではなく、コンボの選択位置を読む。
    // 共通入力アダプターは、このハンドラーを呼ぶ前に m_index を更新する。
    (void)event;
    m_index2=m_index;
    // 言語差に対応し、Python の -1 の選択は末尾要素を指す。 無効
    // な位置には vector::at の範囲検査による失敗を対応させる。
    const I row=m_index2<0?static_cast<I>(m_list2.size())+m_index2:m_index2;
    _set_text(m_list2.at(static_cast<std::size_t>(row)));
    m_content_visible=true;
    m_combo_enabled=true;
}

void Text::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed) return;
    require_readme(*this, "OnClickLeftBtn");
    if (m_navigate_scenario) m_navigate_scenario(false);
    if (m_closed) return;
    _enable_btn();
    update_lists();
    m_index=m_index2=0;
    _set_text(m_list2.empty()?B{}:m_list2.front());
    m_content_visible=!m_list2.empty();
    m_combo_enabled=m_content_visible;
    if (m_combo_typed) {
        auto state=::cw::util::get_window_state(m_combo_typed).value_or(::cw::util::WindowState{});
        state.choices=m_list;
        ::cw::util::set_window_state(m_combo_typed,std::move(state));
        ::cw::util::adjust_dropdownwidth(m_combo_typed);
        m_combo_typed->visible=true;
    }
    (void)event;
}

void Text::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed) return;
    require_readme(*this, "OnClickRightBtn");
    if (m_navigate_scenario) m_navigate_scenario(true);
    if (m_closed) return;
    _enable_btn();
    update_lists();
    m_index=m_index2=0;
    _set_text(m_list2.empty()?B{}:m_list2.front());
    m_content_visible=!m_list2.empty();
    m_combo_enabled=m_content_visible;
    if (m_combo_typed) {
        auto state=::cw::util::get_window_state(m_combo_typed).value_or(::cw::util::WindowState{});
        state.choices=m_list;
        ::cw::util::set_window_state(m_combo_typed,std::move(state));
        ::cw::util::adjust_dropdownwidth(m_combo_typed);
        m_combo_typed->visible=true;
    }
    (void)event;
}

void Text::OnCancel(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed) return;
    dialog_presentation_pointer_cancel();
    // Python版は wx.Dialog の ID_CANCEL に閉じる処理を委譲している。
    // 状態層では同じ結果を明示的に保持し、実際の破棄はUI adapterに任せる。
    (void)event;
    m_selecting=false;
    m_popup=0;
    m_cancelled=true;
    m_closed=true;
    visible=false;
    const auto self=std::shared_ptr<::cw::lib::Window>(this,[](::cw::lib::Window*){});
    auto state=::cw::util::get_window_state(self).value_or(::cw::util::WindowState{});
    state.modal=false;
    ::cw::util::set_window_state(self,std::move(state));
    detach_parent();
}

void Text::OnUp(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed || m_list.size()<=1) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page",false,100,1,0,0,false);
    m_index=m_index<=0?I(m_list.size()-1):m_index-1;
    // Python と同様、コンボの選択を更新してから新しい選択イベントを送
    // る。 元のコマンドの ID・タイトルは説明文ファイルの選択ではない。
    (void)event;
    OnCombobox(nullptr);
}

void Text::OnDown(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed || m_list.size()<=1) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page",false,100,1,0,0,false);
    m_index=(m_index+1)%I(m_list.size());
    (void)event;
    OnCombobox(nullptr);
}

void Text::OnMouseWheel(std::shared_ptr<::cw::lib::Window> event)
{
    if (!m_richtextctrl) return;
    m_richtextctrl->OnMouseWheel(event);
    OnPaint(m_toppanel_typed);
}

bool Text::dispatch_command(I command_id, std::shared_ptr<::cw::lib::Window> event)
{
    if (m_closed) return false;
    if (command_id==m_leftpagekeyid.as_integer()) { OnClickLeftBtn(std::move(event)); return true; }
    if (command_id==m_rightpagekeyid.as_integer()) { OnClickRightBtn(std::move(event)); return true; }
    if (command_id==m_upkeyid.as_integer()) { OnUp(std::move(event)); return true; }
    if (command_id==m_downkeyid.as_integer()) { OnDown(std::move(event)); return true; }
    if (command_id==-2 && m_richtextctrl) {
        if (m_selection.first < m_selection.second && m_selection.second <= m_display_text.size())
            copy_text(m_display_text.substr(m_selection.first,m_selection.second-m_selection.first));
        return true;
    }
    if (command_id==-3 && m_richtextctrl) {
        select_text(0,m_display_text.size());
        return true;
    }
    if (command_id>=10000 && m_richtextctrl && m_searchmenu && m_selection.first<m_selection.second) {
        for (const auto& value:m_richtextctrl->m_search_engines) {
            const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
            const auto engine=object && *object ? std::dynamic_pointer_cast<::cw::util::CWPyRichTextCtrl_SearchEngine>(*object) : nullptr;
            if (!engine || engine->m_menuid_typed!=command_id) continue;
            if (m_selection.second>m_display_text.size()) return false;
            const auto selected=m_display_text.substr(m_selection.first,m_selection.second-m_selection.first);
            try {
                open_text_url(engine->format_url(selected));
            } catch (const std::exception&) {
                ::cw::util::print_ex(std::nullopt);
            }
            return true;
        }
    }
    return false;
}

void Text::OnPaint(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if(m_closed) return;
    // 互換用の描画要求も、同じ完全な Presentation を使う。 このコ
    // ールバックの背後に、折り返し・拡縮をしない二つ目の文字描画処理を残さない。
    const auto view=Text::dialog_presentation();
    if(!view.surface) return;
    m_paint_surface_typed=std::make_shared<::cw::lib::Bitmap>();
    static_cast<::cw::lib::Surface&>(*m_paint_surface_typed)=*view.surface;
    ::cw::lib::ui::inherit_surface_animation(m_paint_surface_typed,view.surface);
    if(m_toppanel_typed) {
        auto state=::cw::util::get_window_state(m_toppanel_typed).value_or(::cw::util::WindowState{});
        state.background=m_presentation_kind==TextPresentationKind::Readme?
            ::cw::lib::Color{0,0,128,255}: ::cw::lib::Color{255,255,255,255};
        state.target=m_paint_surface_typed;
        ::cw::util::set_window_state(m_toppanel_typed,std::move(state));
    }
    request_dialog_presentation_refresh();
}

void Text::_do_layout()
{
    const I width=std::max<I>(1,bounds.width);
    const I height=std::max<I>(1,bounds.height);
    const I footer=readme_footer_height;
    if (m_toppanel_typed) m_toppanel_typed->bounds={0,0,width,std::max<I>(1,height-footer)};
    if (m_panel_typed) m_panel_typed->bounds={0,std::max<I>(0,height-footer),width,footer};
    if (m_combo_typed) {
        // Py のサイザーと同様、コンボの配置前に最小幅を適用する。
        const I combo_width=std::max<I>(140,m_combo_typed->bounds.width);
        m_combo_typed->bounds={std::max<I>(5,width-combo_width-5),0,combo_width,20};
    }
    if (m_leftbtn_typed) m_leftbtn_typed->bounds={0,std::max<I>(0,(footer-footer_nav_height)/2),30,footer_nav_height};
    if (m_rightbtn_typed) m_rightbtn_typed->bounds={std::max<I>(0,width-30),std::max<I>(0,(footer-footer_nav_height)/2),30,footer_nav_height};
    if (m_closebtn_typed) m_closebtn_typed->bounds={std::max<I>(0,(width-85)/2),std::max<I>(0,(footer-footer_button_height)/2),85,footer_button_height};
    if (m_toppanel_typed) OnPaint(m_toppanel_typed);
}

void Text::copy_text(const S& text)
{
    ::cw::dialog::presentation_set_clipboard_text(text);
}

void Text::open_text_url(const S& url)
{
    if (m_richtextctrl) m_richtextctrl->go_url(url);
}

void Text::select_text(std::size_t anchor, std::size_t caret)
{
    const auto boundary=[&](std::size_t offset) {
        offset=std::min(offset,m_display_text.size());
        while (offset && offset<m_display_text.size() &&
               (static_cast<unsigned char>(m_display_text[offset])&0xc0U)==0x80U) --offset;
        return offset;
    };
    m_anchor=boundary(anchor);
    m_caret=boundary(caret);
    m_selection={std::min(m_anchor,m_caret),std::max(m_anchor,m_caret)};
    if (m_richtextctrl) {
        m_richtextctrl->m_selection_start_typed=static_cast<I>(m_selection.first);
        m_richtextctrl->m_selection_end_typed=static_cast<I>(m_selection.second);
    }
    request_dialog_presentation_refresh();
}

void Text::dialog_presentation_resize(::cw::lib::Size size)
{
    if (m_closed || m_presentation_kind!=TextPresentationKind::Readme ||
        size.width<=0 || size.height<=0) return;
    size.width=std::max(size.width,text_scale(155));
    size.height=std::max(size.height,text_scale(86));
    if (size.width==m_surface_size.width && size.height==m_surface_size.height) return;
    m_readme_viewport=size;
    m_popup=0;
    m_popup_release_key.reset();
    m_type_search.reset();
    dialog_presentation_pointer_cancel();
    ensure_text_layout();
    bounds.width=m_surface_size.width;
    bounds.height=m_surface_size.height;
    request_dialog_presentation_refresh();
}

void Text::ensure_text_layout()
{
    const auto scale=[this](I value){return text_scale(value);};
    const bool readme=m_presentation_kind==TextPresentationKind::Readme;
    const bool system=m_presentation_kind==TextPresentationKind::ErrorLog;
    const auto font=system?m_system_metrics.font:
        ::cw::lib::ui::configured_wx_font("datadesc",readme?14:11);
    if(readme) {
        m_surface_size=m_readme_viewport.value_or(::cw::lib::Size{scale(550),scale(290+::cw::dialog::kDialogFooterExtraHeight)});
        m_footer_top=m_surface_size.height-scale(readme_footer_height);
        m_text_field={0,0,m_surface_size.width,m_footer_top};
        m_body_rect={scale(5),scale(25),m_surface_size.width-scale(22),m_footer_top-scale(25)};
        m_scroll_track={m_surface_size.width-scale(14),scale(25),scale(14),m_body_rect.height};
        m_close_rect={(m_surface_size.width-scale(85))/2,
            m_footer_top+(m_surface_size.height-m_footer_top-scale(footer_button_height))/2,scale(85),scale(footer_button_height)};
    } else {
        const I width=scale(system?250:400);
        const I top=scale(system?200:260);
        auto button_font=system?m_system_metrics.font : ::cw::lib::ui::configured_wx_font("button",14);
        const I button_height=std::max<I>(scale(system?23:24),button_font.pixels+scale(8));
        const auto label=system?S("OK"):message("close","閉じる");
        const I button_width=std::min<I>(width-scale(10),std::max<I>(scale(system?75:85),
            ::cw::lib::ui::label_width(label,button_font.pixels,button_font.bold,button_font.face,button_font.italic)+scale(16)));
        m_surface_size={width,top+button_height+scale(10)};
        m_footer_top=top;
        m_text_field={scale(system?0:10),scale(system?0:10),scale(system?250:380),scale(system?200:240)};
        m_body_rect={m_text_field.x+scale(2),m_text_field.y+scale(2),
            m_text_field.width-scale(18),m_text_field.height-scale(4)};
        m_scroll_track={m_body_rect.x+m_body_rect.width,m_body_rect.y,scale(14),m_body_rect.height};
        m_close_rect={(width-button_width)/2,top+scale(5),button_width,button_height};
    }
    ::cw::imageretouch::Font metrics_font;
    metrics_font.initialize(font.face,-font.pixels,font.bold,font.italic);
    // TextCtrl は固定 16px の行間隔ではなく、選択フォントの行メトリクスを使う。
    m_row_height=system?std::max<I>(1,m_system_metrics.line_height):
        std::max<I>(1,metrics_font.get_linesize());
    m_visible_rows=std::max<I>(1,m_body_rect.height/m_row_height);
    if (!m_rows.empty() && m_layout_text==m_display_text && m_layout_width==m_body_rect.width &&
        font.face==m_body_font.face && font.pixels==m_body_font.pixels &&
        font.bold==m_body_font.bold && font.italic==m_body_font.italic) {
        const I maximum=std::max<I>(0,static_cast<I>(m_rows.size())-m_visible_rows);
        if(m_richtextctrl && m_richtextctrl->m_scroll_line_typed>maximum) scroll_text(maximum);
        return;
    }
    const bool same_text=m_layout_text==m_display_text;
    const I previous_first=m_richtextctrl?m_richtextctrl->m_scroll_line_typed:0;
    const std::size_t scroll_anchor=same_text && !m_rows.empty()
        ? m_rows[static_cast<std::size_t>(std::clamp<I>(previous_first,0,static_cast<I>(m_rows.size())-1))].offsets.front() : 0;
    m_body_font=font;
    m_layout_width=m_body_rect.width;
    m_layout_text=m_display_text;
    m_row_images.clear();
    m_rows=::cw::lib::ui::wrap_label(m_display_text,font,m_body_rect.width);
    I first=0;
    if(same_text) for(std::size_t i=0;i<m_rows.size();++i) {
        if(m_rows[i].offsets.front()>scroll_anchor) break;
        first=static_cast<I>(i);
    }
    scroll_text(first);
}

void Text::scroll_text(I first)
{
    if (!m_richtextctrl) return;
    m_richtextctrl->m_scroll_line_typed=std::clamp<I>(first,0,
        std::max<I>(0,static_cast<I>(m_rows.size())-m_visible_rows));
    request_dialog_presentation_refresh();
}

std::size_t Text::text_hit(float x, float y) const
{
    if (m_rows.empty()) return 0;
    const I first=m_richtextctrl ? m_richtextctrl->m_scroll_line_typed : 0;
    const I line=std::clamp<I>(first+static_cast<I>(std::floor((y-m_body_rect.y)/m_row_height)),
        0,static_cast<I>(m_rows.size())-1);
    const auto& row=m_rows[static_cast<std::size_t>(line)];
    x-=static_cast<float>(m_body_rect.x);
    for (std::size_t i=1;i<row.positions.size();++i)
        if (x<(row.positions[i-1]+row.positions[i])*0.5F) return row.offsets[i-1];
    return row.offsets.back();
}

std::vector<Text::MenuEntry> Text::text_menu() const
{
    std::vector<MenuEntry> result;
    if (m_popup==1) {
        for (std::size_t i=0;i<m_list.size();++i) result.push_back({m_list[i],static_cast<int>(i),true});
    } else if (m_popup==2) {
        const bool selected=m_selection.first<m_selection.second;
        // D の wx.Menu と同様、空のコントロールでも「すべて選
        // 択」を有効にし、二つの標準項目に C/A のアクセスキーを使う
        // 。 これらは説明文の wx.RichTextCtrl の操作
        // ラベルであり、スキン全体のメッセージ辞書からは取得しない。
        // 汎用の copy キーは「宿の複製」にも使われるためである。
        result={{menu_label("コピー",'C'),-2,selected,'C'},
                {menu_label("すべて選択",'A'),-3,true,'A'}};
        bool separator=false;
        if (m_searchmenu && m_richtextctrl) for (const auto& value:m_richtextctrl->m_search_engines) {
            const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
            const auto engine=object && *object ? std::dynamic_pointer_cast<::cw::util::CWPyRichTextCtrl_SearchEngine>(*object) : nullptr;
            if (engine) {
                const auto visible_label=menu_visible_label(engine->m_name_typed);
                // 互換性のため旧 Google 検索設定は読み取り
                // 可能にするが、共通表示のメニュー行には含めない。
                // このビルドで信頼できるブラウザー検索を提供で
                // きない場合、その外部操作は意図的に表示しない。
                if (visible_label=="Googleで検索") continue;
                result.push_back({visible_label,static_cast<int>(engine->m_menuid_typed),
                    selected,{},!separator});
                separator=true;
            }
        }
    }
    return result;
}

::cw::dialog::DialogPresentation Text::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    repeat_navigation();
    m_page_controls.clear();
    ensure_text_layout();
    normalize_focus();
    const bool readme=m_presentation_kind==TextPresentationKind::Readme;
    const bool system=m_presentation_kind==TextPresentationKind::ErrorLog;
    const auto scale = [this](const I value) { return text_scale(value); };
    const I width = m_surface_size.width;
    const I height = m_surface_size.height;
    const I top_height = m_footer_top;
    const I footer_height = height - top_height;
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full({width,height});
    if(full) {
        retained.allocate({width,height},{240,240,240,255});
        ::cw::lib::ui::fill_rect(retained.surface,{0,0,width,top_height},
            readme?::cw::lib::Color{0,0,128,255}: ::cw::lib::Color{240,240,240,255});
        if(!readme) {
            ::cw::lib::ui::fill_rect(retained.surface,m_text_field,{255,255,255,255});
            ::cw::lib::ui::draw_sunken_border(retained.surface,m_text_field);
        }
        ::cw::lib::ui::fill_rect(retained.surface,{0,top_height,width,footer_height},{235,235,235,255});
        retained.capture_base();
    }
    auto signature_mix=[](std::uint64_t& seed,std::uint64_t value) {
        seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
    };
    const auto title_font = ::cw::lib::ui::configured_wx_font("paneltitle",16,false,0,true);
    const auto combo_font = ::cw::lib::ui::configured_wx_font("combo",14);
    const auto button_font = system?m_system_metrics.font:
        ::cw::lib::ui::configured_wx_font("button",14,false,0,readme);
    const auto empty_font=::cw::lib::ui::configured_wx_font("paneltitle",20);
    std::uint64_t content_signature=std::hash<S>{}(m_display_text);
    // 共通表示側は設定フォントを既に再取得するため、保持画像
    // の識別にも同じ装飾情報を含める。 計測値が等しくても文
    // 字形が同じとは限らない。Python がリッチテキス
    // トコントロールに委ねる無効化を、共通表示で再現する。
    const auto mix_font=[&](std::uint64_t& signature,const ::cw::lib::ui::FontStyle& font) {
        signature_mix(signature,std::hash<S>{}(font.face));
        signature_mix(signature,static_cast<std::uint64_t>(font.pixels));
        signature_mix(signature,static_cast<std::uint64_t>(font.bold));
        signature_mix(signature,static_cast<std::uint64_t>(font.italic));
    };
    mix_font(content_signature,m_body_font);
    if(readme) {
        mix_font(content_signature,title_font);
        mix_font(content_signature,combo_font);
        if(m_list2.empty())mix_font(content_signature,empty_font);
    }
    signature_mix(content_signature,static_cast<std::uint64_t>(m_index+1));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_selection.first));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_selection.second));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_caret));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_richtextctrl?m_richtextctrl->m_scroll_line_typed:0));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_content_visible));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_focus));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_popup));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_popup_row+1));
    signature_mix(content_signature,static_cast<std::uint64_t>(m_list.size()));
    if(!m_list.empty()) signature_mix(content_signature,std::hash<S>{}(
        m_list[static_cast<std::size_t>(std::clamp<I>(m_index,0,static_cast<I>(m_list.size())-1))]));
    std::uint64_t footer_signature{};
    mix_font(footer_signature,button_font);
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_left_enabled));
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_right_enabled));
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_closed));
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_focus));
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_popup));
    signature_mix(footer_signature,static_cast<std::uint64_t>(m_popup_row+1));
    const bool paint_content=retained.component_changed("text.content",{0,0,width,top_height},content_signature);
    const bool paint_footer=retained.component_changed("text.footer",{0,top_height,width,footer_height},footer_signature);
    retained.restore_dirty();
    auto surface=retained.surface;

    const auto draw = [&](const S& value, const I x, const I y,
                          const ::cw::lib::ui::FontStyle& style,
                          const std::tuple<I, I, I> colour) {
        if(paint_content) ::cw::lib::ui::draw_label(surface, value, x, y, style.pixels,
                                                    style.bold, colour, style.face, style.italic);
    };

    ::cw::lib::Rect combo_rect;
    if(readme) {
    const S instructions = message("instructions", "Instructions");
    draw(instructions, scale(5), scale(2), title_font, {211, 211, 211});
    const S referencing = message("referencing_file", "Referencing file");
    const I combo_width = scale(140);
    const I referencing_width = ::cw::lib::ui::label_width(
        referencing, title_font.pixels, title_font.bold, title_font.face, title_font.italic);
    const I combo_x = std::max<I>(scale(5), width - combo_width - scale(5));
    draw(referencing, std::max<I>(scale(5), combo_x - referencing_width - scale(5)),
         scale(2), title_font, {211, 211, 211});
    combo_rect={combo_x, scale(0), combo_width, scale(21)};
    m_combo_rect=combo_rect;
    if(paint_content) {
        ::cw::lib::ui::fill_rect(surface, combo_rect, {255, 255, 255, 255});
        ::cw::lib::ui::draw_sunken_border(surface, combo_rect);
    }
    if (!m_list.empty()) {
        const I selected_index = std::clamp<I>(
            m_index, 0, static_cast<I>(m_list.size()) - 1);
        auto name=std::make_shared<::cw::lib::Surface>();
        name->size={combo_width-scale(22),combo_rect.height-scale(4)};
        name->rgba.assign(static_cast<std::size_t>(name->size.width*name->size.height)*4U,255);
        ::cw::lib::ui::draw_label(name,m_list[static_cast<std::size_t>(selected_index)],0,0,
            combo_font.pixels,combo_font.bold,{0,0,0},combo_font.face,combo_font.italic);
        if(paint_content) ::cw::lib::ui::blend_surface(surface,name,combo_x+scale(4),scale(2));
    }
    draw("▼",combo_x+combo_width-scale(17),scale(2),combo_font,{0,0,0});
    if(paint_content) ::cw::lib::ui::fill_rect(surface,{0,combo_rect.height,width,scale(2)},{211,211,211,255});
    if(m_list2.empty()) {
        const S empty="No Text File";
        const auto empty_image=::cw::lib::ui::render_label(empty,empty_font.pixels,empty_font.bold,
            {211,211,211},empty_font.face,empty_font.italic);
        const I text_width=empty_image?empty_image->size.width:0;
        const I text_height=empty_image?empty_image->size.height:empty_font.pixels;
        // Python と同様、負の奇数の差も整数除算で負の無限大方向へ切
        // り下げる。 大きすぎる見出しも全 OS で同じ中央配置にする。
        const auto half_floor=[](I value) { return value/2-(value<0 && value%2!=0?1:0); };
        const I x=half_floor(width-text_width),y=half_floor(top_height-text_height);
        if(paint_content && empty_image) ::cw::lib::ui::blend_surface(surface,empty_image,x,y);
        const ::cw::lib::Rect box{x-scale(30),y-scale(10),text_width+scale(60),text_height+scale(20)};
        const ::cw::lib::Color edge{211,211,211,255};
        if(paint_content) {
            ::cw::lib::ui::fill_rect(surface,{box.x,box.y,box.width,scale(1)},edge);
            ::cw::lib::ui::fill_rect(surface,{box.x,box.y+box.height-scale(1),box.width,scale(1)},edge);
            ::cw::lib::ui::fill_rect(surface,{box.x,box.y,scale(1),box.height},edge);
            ::cw::lib::ui::fill_rect(surface,{box.x+box.width-scale(1),box.y,scale(1),box.height},edge);
        }
    }
    }

    ensure_text_layout();
    ::cw::dialog::DialogPresentation result;
    const I first=m_richtextctrl ? m_richtextctrl->m_scroll_line_typed : 0;
    if (m_content_visible && !m_list2.empty()) {
        if(paint_content) {
        ::cw::imageretouch::Font text_font;
        text_font.initialize(m_body_font.face,-m_body_font.pixels,m_body_font.bold,m_body_font.italic);
        auto body=std::make_shared<::cw::lib::Surface>();
        body->size={m_body_rect.width,m_body_rect.height};
        body->rgba.assign(static_cast<std::size_t>(body->size.width*body->size.height)*4U,0);
        ::cw::lib::ui::fill_rect(body,{0,0,body->size.width,body->size.height},
            readme?::cw::lib::Color{0,0,128,255}: ::cw::lib::Color{255,255,255,255});
        for (I line=first,y=0;line<static_cast<I>(m_rows.size()) && y<body->size.height;++line,y+=m_row_height) {
            const auto& row=m_rows[static_cast<std::size_t>(line)];
            for (std::size_t i=1;i<row.offsets.size();++i)
                if (row.offsets[i]>m_selection.first && row.offsets[i-1]<m_selection.second)
                    ::cw::lib::ui::fill_rect(body,{row.positions[i-1],y,row.positions[i]-row.positions[i-1],m_row_height},
                        {0,100,200,255});
            if (row.next>row.offsets.back() && m_selection.first<=row.offsets.back() && m_selection.second>row.offsets.back())
                ::cw::lib::ui::fill_rect(body,{row.positions.back(),y,body->size.width-row.positions.back(),m_row_height},
                    {0,100,200,255});
            if(readme) {
                auto& image=m_row_images[row.text];
                if(!image) image=text_font.render(row.text,true,{255,255,255});
                ::cw::lib::ui::blend_surface(body,image,0,y);
            }
            else {
                const auto last=row.offsets.size()-1;
                const auto left=std::min<std::size_t>(last,
                    std::lower_bound(row.offsets.begin(),row.offsets.end(),m_selection.first)-row.offsets.begin());
                const auto right=std::min<std::size_t>(last,
                    std::lower_bound(row.offsets.begin(),row.offsets.end(),m_selection.second)-row.offsets.begin());
                const auto run=[&](std::size_t first,std::size_t end,bool selected) {
                    if(first>=end) return;
                    ::cw::lib::ui::blend_surface(body,text_font.render(
                        row.text.substr(row.columns[first],row.columns[end]-row.columns[first]),true,
                        selected?std::tuple<I,I,I>{255,255,255}:std::tuple<I,I,I>{0,0,0}),row.positions[first],y);
                };
                // 各文字列区間は一度だけ描画する。 選択文字の白
                // い字を、同じ文字の黒い複製に重ねて合成しない。
                run(0,left,false); run(left,right,true); run(right,last,false);
            }
            for (const auto& url:m_url_ranges) {
                const auto begin=std::lower_bound(row.offsets.begin(),row.offsets.end(),url.first);
                const auto end=std::lower_bound(row.offsets.begin(),row.offsets.end(),url.second);
                const auto left=static_cast<std::size_t>(begin-row.offsets.begin());
                const auto right=static_cast<std::size_t>(end-row.offsets.begin());
                if (left>=row.offsets.size()-1 || right<=left) continue;
                const auto finish=std::min(right,row.offsets.size()-1);
                ::cw::lib::ui::blend_surface(body,text_font.render(
                    row.text.substr(row.columns[left],row.columns[finish]-row.columns[left]),true,{255,132,0}),row.positions[left],y);
                ::cw::lib::ui::fill_rect(body,{row.positions[left],y+m_body_font.pixels,
                    row.positions[finish]-row.positions[left],std::max<I>(1,scale(1))},{255,132,0,255});
            }
        }
        ::cw::lib::ui::blend_surface(surface,body,m_body_rect.x,m_body_rect.y);
        if (readme && m_focus==TextFocus::Body && !m_popup && m_selection.first==m_selection.second) {
            for (I row=first;row<static_cast<I>(m_rows.size()) && row<first+m_visible_rows;++row) {
                const auto& line=m_rows[static_cast<std::size_t>(row)];
                if (m_caret<line.offsets.front() || m_caret>line.offsets.back()) continue;
                // 表示上の折り返し境界は同じバイト位置を共有する。 前の行ではなく、キーボー
                // ド移動と同じ「開始位置がキャレット以下である最後の行」の規則に合わせる。
                if (row+1<static_cast<I>(m_rows.size()) &&
                    m_rows[static_cast<std::size_t>(row+1)].offsets.front()<=m_caret) continue;
                const auto at=std::lower_bound(line.offsets.begin(),line.offsets.end(),m_caret)-line.offsets.begin();
                const auto x=line.positions[static_cast<std::size_t>(at)];
                if (x<m_body_rect.width)
                    ::cw::lib::ui::fill_rect(surface,{m_body_rect.x+x,m_body_rect.y+(row-first)*m_row_height,
                        std::max<I>(1,scale(1)),std::min(m_row_height,m_body_rect.height-(row-first)*m_row_height)},
                        {255,255,255,255});
                break;
            }
        }
        }
        const I maximum=std::max<I>(0,static_cast<I>(m_rows.size())-m_visible_rows);
        if (maximum) {
            const auto track=m_scroll_track;
            const I thumb=std::min(track.height,std::max(scale(16),track.height*m_visible_rows/static_cast<I>(m_rows.size())));
            if(paint_content) {
                ::cw::lib::ui::fill_rect(surface,track,{230,230,230,255});
                ::cw::lib::ui::fill_rect(surface,{track.x,track.y+(track.height-thumb)*first/maximum,track.width,thumb},
                    {150,150,150,255});
            }
            result.controls.push_back({Action::Auxiliary,201,static_cast<float>(track.x),static_cast<float>(track.y),
                static_cast<float>(track.width),static_cast<float>(track.height),true,true});
        }
    } // 空の Readme の見出しと枠は上で一度だけ描く。通常のテキストには設けない。

    const auto draw_button = [&](const S& label, const ::cw::lib::Rect rect,
                                 const Action action, const int index,
                                 const bool enabled) {
        const bool paint=rect.y<top_height?paint_content:paint_footer;
        if(paint) {
            ::cw::lib::ui::draw_button_face(surface, rect, {enabled});
        }
        const auto label_image=::cw::lib::ui::render_label(label,button_font.pixels,button_font.bold,
            enabled?std::tuple<I,I,I>{0,0,0}:std::tuple<I,I,I>{120,120,120},
            button_font.face,button_font.italic);
        const auto icon=(action==Action::Previous || action==Action::Next) && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
            ? ::cw::lib::ui::resource_surface_for_window(::cw::lib::ui::resource_table_surface(
                ::cw::g_cwpy->m_rsrc->m_buttons,action==Action::Previous ? "LMOVE" : "RMOVE")) : nullptr;
        if (paint && icon) ::cw::lib::ui::blend_surface(surface,icon,
            rect.x+(rect.width-icon->size.width)/2,rect.y+(rect.height-icon->size.height)/2);
        else if(paint && label_image) ::cw::lib::ui::blend_surface(surface,label_image,
            rect.x+(rect.width-label_image->size.width)/2,rect.y+(rect.height-label_image->size.height)/2);
        result.controls.push_back({action, index, static_cast<float>(rect.x),
            static_cast<float>(rect.y), static_cast<float>(rect.width),
            static_cast<float>(rect.height), enabled});
        result.controls.back().hover_highlight=true;
        if (action==Action::Previous || action==Action::Next) {
            result.controls.back().continuous_pointer=true;
            m_page_controls.push_back(result.controls.back());
        }
    };
    const auto focus_mark=[&](const ::cw::lib::Rect& r) {
        if(!(r.y<top_height?paint_content:paint_footer)) return;
        const ::cw::lib::Color c{35,90,170,255};
        ::cw::lib::ui::fill_rect(surface,{r.x+scale(2),r.y+scale(2),r.width-scale(4),scale(1)},c);
        ::cw::lib::ui::fill_rect(surface,{r.x+scale(2),r.y+r.height-scale(3),r.width-scale(4),scale(1)},c);
    };

    result.route = readme?"INSTRUCTIONS":system?"ERROR_LOG":"COMMENT";
    result.wants_text_input=!m_closed && readme && m_combo_enabled &&
        (m_popup==1 || (!m_popup && m_focus==TextFocus::File));
    result.text_input_hardware_only=true;
    result.title = title;
    result.resizable=readme && !m_closed;
    if(readme) result.minimum_size={scale(155),scale(86)};
    if(readme) {
        const I nav_y=top_height+(footer_height-scale(footer_nav_height))/2;
        const ::cw::lib::Rect previous_rect{0,nav_y,scale(30),scale(footer_nav_height)};
        const ::cw::lib::Rect next_rect{width-scale(30),nav_y,scale(30),scale(footer_nav_height)};
        draw_button("<",previous_rect,Action::Previous,0,m_left_enabled);
        draw_button(">",next_rect,Action::Next,0,m_right_enabled);
        result.controls.push_back({Action::Auxiliary,100,static_cast<float>(combo_rect.x),static_cast<float>(combo_rect.y),
            static_cast<float>(combo_rect.width),static_cast<float>(combo_rect.height),m_combo_enabled});
        if(m_focus==TextFocus::File) focus_mark(combo_rect);
        else if(m_focus==TextFocus::Previous) focus_mark(previous_rect);
        else if(m_focus==TextFocus::Next) focus_mark(next_rect);
    }
    draw_button(system?S("OK"):message("close","閉じる"),m_close_rect,Action::Cancel,0,!m_closed);
    if(m_focus==TextFocus::Close) focus_mark(m_close_rect);
    result.controls.push_back({Action::Auxiliary,200,
        static_cast<float>(readme?0:m_body_rect.x),static_cast<float>(readme?scale(24):m_body_rect.y),
        static_cast<float>(readme?width-scale(16):m_body_rect.width),
        static_cast<float>(readme?top_height-scale(24):m_body_rect.height),m_content_visible && !m_closed,true});
    if (m_popup) {
        const auto entries=text_menu();
        if (entries.empty()) m_popup=0;
        else {
            const auto menu_font=system?m_system_metrics.font : ::cw::lib::ui::configured_wx_font("menu",13);
            const I row_height=std::max<I>(scale(22),menu_font.pixels+scale(6));
            I menu_width=scale(140);
            for (const auto& entry:entries) menu_width=std::max<I>(menu_width,
                ::cw::lib::ui::label_width(entry.label,menu_font.pixels,menu_font.bold,menu_font.face,menu_font.italic)+scale(12));
            menu_width=std::min<I>(menu_width,width);
            const I count=std::min<I>(static_cast<I>(entries.size()),std::max<I>(1,height/row_height));
            m_popup_row=std::clamp(m_popup_row,0,static_cast<int>(entries.size())-1);
            const I first_row=std::clamp<I>(m_popup_row-count/2,0,static_cast<I>(entries.size())-count);
            const I left=std::clamp<I>(static_cast<I>(m_popup_x),0,width-menu_width);
            const I top=std::clamp<I>(static_cast<I>(m_popup_y),0,height-count*row_height);
            result.controls.clear(); // ポップアップから、覆われた親コントロールを実行しない。
            result.controls.push_back({Action::Auxiliary,-100,0,0,static_cast<float>(width),static_cast<float>(height),true});
            for (I row=0;row<count;++row) {
                const auto& entry=entries[static_cast<std::size_t>(first_row+row)];
                auto item=std::make_shared<::cw::lib::Surface>();
                item->size={menu_width,row_height};
                item->rgba.assign(static_cast<std::size_t>(menu_width*row_height)*4U,255);
                const bool selected=first_row+row==m_popup_row;
                const auto selection=::cw::lib::ui::system_selection_colours();
                ::cw::lib::ui::fill_rect(item,{0,0,menu_width,row_height},selected
                    ? selection.background : ::cw::lib::Color{250,250,250,255});
                const auto label=::cw::lib::ui::render_label(entry.label,menu_font.pixels,menu_font.bold,
                    entry.enabled ? (selected
                        ? std::tuple<I,I,I>{selection.text.r,selection.text.g,selection.text.b}
                        : std::tuple<I,I,I>{0,0,0}) : std::tuple<I,I,I>{128,128,128},
                    menu_font.face,menu_font.italic);
                if(label) ::cw::lib::ui::blend_surface(item,label,scale(4),(row_height-label->size.height)/2);
                if(paint_content || paint_footer)
                    ::cw::lib::ui::blend_surface(surface,item,left,top+row*row_height);
                if (entry.separator_before && row>0 && (paint_content || paint_footer)) {
                    const I separator_y=top+row*row_height-scale(1);
                    ::cw::lib::ui::fill_rect(surface,
                        {left+scale(4),separator_y,menu_width-scale(8),scale(1)},
                        {160,160,160,255});
                }
                // wx の無効なメニュー項目と同様、クリックを吸収し、
                // ポップアップを閉じない。 当たり判定用のコントロール
                // は有効に保ち、下の dialog_presentation_action
                // で意味上の有効状態を確認する。
                result.controls.push_back({Action::Select,static_cast<int>(first_row+row),static_cast<float>(left),
                    static_cast<float>(top+row*row_height),static_cast<float>(menu_width),static_cast<float>(row_height),true});
            }
            if(paint_content || paint_footer)
                ::cw::lib::ui::draw_sunken_border(surface,{left,top,menu_width,count*row_height});
        }
    }
    retained.finish();
    result.surface = surface;
    result.damage=retained.damage;
    result.surface_revision=retained.revision;
    result.refresh_interval_ms=m_page_repeat?static_cast<std::uint32_t>(
        std::min<std::int64_t>(::cw::dialog::dialog_page_repeat_interval().count(),
                              std::numeric_limits<std::uint32_t>::max())):0U;
    return result;
}

void Text::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                      const int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_closed) return;
    dialog_presentation_pointer_cancel();
    if(m_type_search.composing && (action==Action::Cancel || action==Action::Accept)) {
        if(action==Action::Cancel)m_type_search.reset();
        return;
    }
    m_type_search.reset();
    request_dialog_presentation_refresh();
    if (m_popup) {
        const auto entries=text_menu();
        const int row=action==Action::Accept || action==Action::Activate ? m_popup_row : index;
        const bool choose=action==Action::Select || action==Action::Accept || action==Action::Activate;
        if (choose && row>=0 && row<static_cast<int>(entries.size())) {
            const auto entry=entries[static_cast<std::size_t>(row)];
            if (!entry.enabled) return;
            const int popup=m_popup;
            m_popup=0;
            if (popup==1) {
                auto event=std::make_shared<::cw::lib::Window>();
                event->id=entry.command;
                m_index=entry.command;
                OnCombobox(event);
            } else dispatch_command(entry.command,nullptr);
        } else m_popup=0;
        return;
    }
    if (action==Action::Cancel || (action==Action::Accept && m_presentation_kind!=TextPresentationKind::Readme)) OnCancel(nullptr);
    else if (action==Action::Previous && m_left_enabled) {m_focus=TextFocus::Previous;OnClickLeftBtn(nullptr);}
    else if (action==Action::Next && m_right_enabled) {m_focus=TextFocus::Next;OnClickRightBtn(nullptr);}
    else if (action==Action::Select && index>=0 && index<static_cast<int>(m_list2.size())) {
        auto event=std::make_shared<::cw::lib::Window>();
        event->id=index;
        m_index=index;
        OnCombobox(event);
    } else if (action==Action::Auxiliary && index==100 && m_combo_enabled && !m_list2.empty()) {
        m_focus=TextFocus::File;
        m_popup=1;
        m_popup_row=static_cast<int>(m_index);
        m_popup_x=static_cast<float>(m_combo_rect.x);
        m_popup_y=static_cast<float>(m_combo_rect.y+m_combo_rect.height);
    }
}

std::vector<TextFocus> Text::focus_order() const
{
    // wx と同様、上部パネルのリッチテキスト・コンボ、ボタンパネルの閉じる・左・
    // 右の順で扱う。 非表示・無効なコントロールは Tab の停止位置にしない。
    std::vector<TextFocus> order;
    if(m_content_visible) order.push_back(TextFocus::Body);
    if(m_presentation_kind==TextPresentationKind::Readme && m_combo_enabled) order.push_back(TextFocus::File);
    order.push_back(TextFocus::Close);
    if(m_left_enabled) order.push_back(TextFocus::Previous);
    if(m_right_enabled) order.push_back(TextFocus::Next);
    return order;
}

void Text::normalize_focus()
{
    const auto order=focus_order();
    if(std::find(order.begin(),order.end(),m_focus)==order.end()) m_focus=TextFocus::Close;
}

void Text::advance_focus(bool backwards)
{
    normalize_focus();
    const auto order=focus_order();
    const auto at=std::find(order.begin(),order.end(),m_focus)-order.begin();
    m_focus=order[(at+(backwards?order.size()-1:1))%order.size()];
    request_dialog_presentation_refresh();
}

void Text::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if (m_closed || m_popup_release_key || m_type_search.composing) return;
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_popup) {
        m_popup_release_key=::cw::dialog::DialogPresentationKey::Enter;
        dialog_presentation_action(Action::Accept,-1);
        return;
    }
    if(modifiers) return;
    normalize_focus();
    if(m_focus==TextFocus::Close) OnCancel(nullptr);
    else if(m_focus==TextFocus::File) {
        dialog_presentation_action(Action::Auxiliary,100);
        m_popup_release_key=::cw::dialog::DialogPresentationKey::Enter;
    }
    else if(m_focus==TextFocus::Previous) dialog_presentation_action(Action::Previous,0);
    else if(m_focus==TextFocus::Next) dialog_presentation_action(Action::Next,0);
    // 読み取り専用の複数行テキストでは Enter を消費し、ダイアログを閉じない。
}

void Text::dialog_presentation_text(const std::string& text,bool composition)
{
    if(m_closed || m_presentation_kind!=TextPresentationKind::Readme || !m_combo_enabled ||
       (m_popup!=1 && (m_popup || m_focus!=TextFocus::File))) return;
    // コントロールを開く・切り替えるキーを、前方一致検索の文字にも使わない。
    // Backspace の転送で、進行中の IME 変換を終了させない。
    if(!composition && (text=="\b" || (text==" " && !m_type_search.active())))return;
    m_type_search.composing=composition && !text.empty();
    if(composition) return;
    if(m_input_modifiers & (::cw::dialog::DialogPresentationModifierControl |
                            ::cw::dialog::DialogPresentationModifierAlt)) {m_type_search.reset();return;}
    const auto row=m_type_search.find(text,static_cast<int>(m_list.size()),
        m_popup==1?m_popup_row:static_cast<int>(m_index),[&](int i){return m_list[i];});
    if(!row) return;
    if(m_popup==1) m_popup_row=*row;
    else {
        auto event=std::make_shared<::cw::lib::Window>();event->id=*row;
        m_index=*row;
        OnCombobox(std::move(event));
    }
    request_dialog_presentation_refresh();
}

void Text::dialog_presentation_key(const ::cw::dialog::DialogPresentationKey key,
                                   bool pressed, ::cw::dialog::DialogPresentationModifiers modifiers)
{
    m_input_modifiers=modifiers;
    if (m_closed) return;
    if (pressed) dialog_presentation_pointer_cancel();
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_popup_release_key && *m_popup_release_key==key) {
        if(!pressed) m_popup_release_key.reset();
        return;
    }
    if(!pressed) return;
    if(m_type_search.composing) return;
    if(key==Key::Space && (m_focus==TextFocus::File || m_popup==1) &&
       m_type_search.active() && !modifiers) return;
    if(!::cw::dialog::presentation_key_ascii(key) || modifiers &
        (::cw::dialog::DialogPresentationModifierControl | ::cw::dialog::DialogPresentationModifierAlt))
        m_type_search.reset();
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool alt=(modifiers & ::cw::dialog::DialogPresentationModifierAlt)!=0;
    const bool context_menu=(key==Key::ContextMenu || (key==Key::F10 && shift)) && !ctrl && !alt;
    if(key==Key::Escape) {dialog_presentation_action(Action::Cancel,-1);return;}
    if (m_popup) {
        if(key==Key::Tab && !ctrl && !alt) {m_popup=0;advance_focus(shift);return;}
        const auto entries=text_menu();
        const int count=static_cast<int>(entries.size());
        if (key==Key::Enter || key==Key::Space) {
            m_popup_release_key=key;
            dialog_presentation_action(Action::Accept,-1);
        }
        else if(key==Key::F4 || (alt && (key==Key::Up || key==Key::Down))) m_popup=0;
        else if(context_menu) {
            // wx のポップアップメニューと同様、繰り返されたコンテキストメニ
            // ューキーも自身で扱う。 選択行を実行せず、その入力を消費する。
        }
        else if(count && !ctrl && !shift && !alt && (key==Key::C || key==Key::A)) {
            const char mnemonic=key==Key::C?'C':'A';
            for(int row=0;row<count;++row) {
                if(entries[static_cast<std::size_t>(row)].mnemonic!=mnemonic) continue;
                // 通常の選択操作を経由し、無効なアクセスキーは何
                // も実行せず、ポップアップを開いたままにする。
                dialog_presentation_action(Action::Select,row);
                break;
            }
        }
        else if (count && (key==Key::Up || key==Key::Down || key==Key::Home || key==Key::End ||
                          key==Key::PageUp || key==Key::PageDown)) {
            const int direction=key==Key::Up || key==Key::End || key==Key::PageUp ? -1 : 1;
            int row=key==Key::Home ? -1 : key==Key::End ? count : m_popup_row;
            if (key==Key::PageUp || key==Key::PageDown) row=std::clamp(row+direction*9,0,count-1)-direction;
            for (int i=0;i<count;++i) {
                row=(row+count+direction)%count;
                if (entries[static_cast<std::size_t>(row)].enabled) {m_popup_row=row;break;}
            }
        }
        request_dialog_presentation_refresh();
        return;
    }
    const bool plain=m_presentation_kind!=TextPresentationKind::Readme;
    normalize_focus();
    if(!plain && m_focus==TextFocus::Body && context_menu) {
        ensure_text_layout();
        if (!m_content_visible) return;
        I first=m_richtextctrl ? m_richtextctrl->m_scroll_line_typed : 0;
        I row=0;
        if (!m_rows.empty()) {
            for(std::size_t i=1;i<m_rows.size();++i)
                if(m_rows[i].offsets.front()<=m_caret) row=static_cast<I>(i);
            row=std::clamp<I>(row,0,static_cast<I>(m_rows.size())-1);
            if(row<first) {scroll_text(row);first=row;}
            else if(row>=first+m_visible_rows) {
                first=std::max<I>(0,row-m_visible_rows+1);
                scroll_text(first);
            }
        }
        m_popup=2;
        m_popup_row=m_selection.first<m_selection.second ? 0 : 1;
        m_popup_x=static_cast<float>(m_body_rect.x);
        m_popup_y=static_cast<float>(m_body_rect.y);
        if(!m_rows.empty()) {
            const auto& line=m_rows[static_cast<std::size_t>(row)];
            if (!line.positions.empty()) {
                const auto it=std::lower_bound(line.offsets.begin(),line.offsets.end(),m_caret);
                const auto column=std::min<std::size_t>(static_cast<std::size_t>(it-line.offsets.begin()),
                    line.positions.size()-1);
                m_popup_x=static_cast<float>(m_body_rect.x+line.positions[column]);
            }
            m_popup_y=static_cast<float>(m_body_rect.y+(row-first)*m_row_height);
        }
        request_dialog_presentation_refresh();
        return;
    }
    if(key==Key::Tab && !ctrl && !alt) {
        advance_focus(shift);
        return;
    }
    if(plain && m_focus==TextFocus::Close) {
        if(key==Key::Enter || key==Key::Space) OnCancel(nullptr);
        return;
    }
    if (ctrl) {
        if (key==Key::C) {dispatch_command(-2,nullptr);return;}
        if (key==Key::A) {dispatch_command(-3,nullptr);return;}
        if (!plain && key==Key::Left) {if (m_left_enabled) OnClickLeftBtn(nullptr);return;}
        if (!plain && key==Key::Right) {if (m_right_enabled) OnClickRightBtn(nullptr);return;}
        if (key==Key::Up || key==Key::Down) {
            if(plain) {
                ensure_text_layout();
                scroll_text((m_richtextctrl?m_richtextctrl->m_scroll_line_typed:0)+(key==Key::Up?-1:1));
            } else if(key==Key::Up) OnUp(nullptr); else OnDown(nullptr);
            return;
        }
    }
    if (key==Key::Enter) {dialog_presentation_enter(modifiers);return;}
    if(!plain && m_focus!=TextFocus::Body) {
        if(m_focus==TextFocus::File && !ctrl) {
            if(key==Key::Space || key==Key::F4 || (alt && key==Key::Down)) {
                dialog_presentation_action(Action::Auxiliary,100);
                m_popup_release_key=key;
            }
            else if(!alt && !m_list2.empty() && (key==Key::Up || key==Key::Down || key==Key::Home || key==Key::End)) {
                auto event=std::make_shared<::cw::lib::Window>();
                event->id=key==Key::Home?0:key==Key::End?static_cast<I>(m_list2.size())-1:
                    std::clamp<I>(m_index+(key==Key::Up?-1:1),0,static_cast<I>(m_list2.size())-1);
                if(event->id!=m_index) {
                    m_index=event->id;
                    OnCombobox(std::move(event));
                }
            }
        } else if(key==Key::Space && !modifiers) dialog_presentation_enter(0);
        return;
    }
    if (!m_content_visible) return;
    ensure_text_layout();
    I row=0;
    for (std::size_t i=1;i<m_rows.size();++i)
        if (m_rows[i].offsets.front()<=m_caret) row=static_cast<I>(i);
    auto caret=m_caret;
    if(plain && ctrl && (key==Key::Left || key==Key::Right)) {
        const auto space=[&](std::size_t at) {
            const auto c=static_cast<unsigned char>(m_display_text[at]);
            return c<128 && (std::isspace(c) || std::ispunct(c));
        };
        if(key==Key::Left) {
            while(caret && space(::cw::dialog::textedit::previous(m_display_text,caret)))
                caret=::cw::dialog::textedit::previous(m_display_text,caret);
            while(caret && !space(::cw::dialog::textedit::previous(m_display_text,caret)))
                caret=::cw::dialog::textedit::previous(m_display_text,caret);
        } else {
            while(caret<m_display_text.size() && !space(caret)) caret=::cw::dialog::textedit::next(m_display_text,caret);
            while(caret<m_display_text.size() && space(caret)) caret=::cw::dialog::textedit::next(m_display_text,caret);
        }
    } else if (key==Key::Left) {
        if (!shift && m_selection.first!=m_selection.second) caret=m_selection.first;
        else if (caret) {
            --caret;
            while (caret && (static_cast<unsigned char>(m_display_text[caret])&0xc0U)==0x80U) --caret;
        }
    } else if (key==Key::Right) {
        if (!shift && m_selection.first!=m_selection.second) caret=m_selection.second;
        else if (caret<m_display_text.size()) {
            ++caret;
            while (caret<m_display_text.size() && (static_cast<unsigned char>(m_display_text[caret])&0xc0U)==0x80U) ++caret;
        }
    } else if (key==Key::Home) caret=ctrl ? 0 : m_rows[static_cast<std::size_t>(row)].offsets.front();
    else if (key==Key::End) caret=ctrl ? m_display_text.size() : m_rows[static_cast<std::size_t>(row)].offsets.back();
    else if (key==Key::Up || key==Key::Down || key==Key::PageUp || key==Key::PageDown) {
        const auto& current=m_rows[static_cast<std::size_t>(row)];
        const auto column=std::lower_bound(current.offsets.begin(),current.offsets.end(),caret)-current.offsets.begin();
        const I x=current.positions[std::min<std::size_t>(column,current.positions.size()-1)];
        const I step=key==Key::PageUp ? -m_visible_rows : key==Key::PageDown ? m_visible_rows : key==Key::Up ? -1 : 1;
        row=std::clamp<I>(row+step,0,static_cast<I>(m_rows.size())-1);
        const auto& target=m_rows[static_cast<std::size_t>(row)];
        caret=target.offsets.back();
        for (std::size_t i=1;i<target.positions.size();++i)
            if (x<(target.positions[i-1]+target.positions[i])/2) {caret=target.offsets[i-1];break;}
    } else return;
    select_text(shift ? m_anchor : caret,caret);
    row=0;
    for (std::size_t i=1;i<m_rows.size();++i)
        if (m_rows[i].offsets.front()<=caret) row=static_cast<I>(i);
    const I first=m_richtextctrl ? m_richtextctrl->m_scroll_line_typed : 0;
    if (row<first) scroll_text(row);
    else if (row>=first+m_visible_rows) scroll_text(row-m_visible_rows+1);
}

void Text::dialog_presentation_context(float x,float y)
{
    dialog_presentation_pointer_cancel();
    if (m_closed) return;
    if (m_popup) {
        // wx の PopupMenu と同様、次のコンテキストクリックを
        // 自身で受け取り閉じる。 新しい起点を決める前にその動作を行い、テ
        // キスト外の右クリックで古い独自ポップアップが残らないようにする。
        m_popup=0;
        m_popup_release_key.reset();
        m_type_search.reset();
        m_selecting=false;
        request_dialog_presentation_refresh();
        return;
    }
    ensure_text_layout();
    // 右クリックはリッチテキストのメニュー操作とし、ダイアログのキャンセルには使わない。
    const auto context_rect=m_presentation_kind==TextPresentationKind::Readme
        ? ::cw::lib::Rect{0,text_scale(24),m_surface_size.width,m_footer_top-text_scale(24)}
        : m_body_rect;
    if (!std::isfinite(x) || !std::isfinite(y) || x<context_rect.x ||
        x>=context_rect.x+context_rect.width || y<context_rect.y ||
        y>=context_rect.y+context_rect.height || !m_content_visible) return;
    m_focus=TextFocus::Body;
    m_selecting=false;
    m_popup=2;
    m_popup_row=m_selection.first<m_selection.second ? 0 : 1;
    m_popup_x=x; m_popup_y=y;
    request_dialog_presentation_refresh();
}

void Text::dialog_presentation_hover(float x,float y)
{
    if (m_closed || !m_popup) return;
    ensure_text_layout();
    const auto entries=text_menu();
    if (entries.empty()) return;
    const bool system=m_presentation_kind==TextPresentationKind::ErrorLog;
    const auto menu_font=system?m_system_metrics.font:
        ::cw::lib::ui::configured_wx_font("menu",13);
    const I row_height=std::max<I>(text_scale(22),menu_font.pixels+text_scale(6));
    I menu_width=text_scale(140);
    for (const auto& entry:entries) menu_width=std::max<I>(menu_width,
        ::cw::lib::ui::label_width(entry.label,menu_font.pixels,menu_font.bold,
            menu_font.face,menu_font.italic)+text_scale(12));
    const I width=m_surface_size.width;
    const I height=m_surface_size.height;
    menu_width=std::min<I>(menu_width,width);
    const I count=std::min<I>(static_cast<I>(entries.size()),
        std::max<I>(1,height/row_height));
    const int selected=std::clamp(m_popup_row,0,static_cast<int>(entries.size())-1);
    const I first_row=std::clamp<I>(selected-count/2,0,
        static_cast<I>(entries.size())-count);
    const I left=std::clamp<I>(static_cast<I>(m_popup_x),0,width-menu_width);
    const I top=std::clamp<I>(static_cast<I>(m_popup_y),0,height-count*row_height);
    if (!std::isfinite(x) || !std::isfinite(y) || x<left || x>=left+menu_width ||
        y<top || y>=top+count*row_height) return;
    const I offset=static_cast<I>((y-top)/row_height);
    const int row=static_cast<int>(first_row+std::clamp<I>(offset,0,count-1));
    if (row<0 || row>=static_cast<int>(entries.size()) || !entries[static_cast<std::size_t>(row)].enabled ||
        m_popup_row==row) return;
    m_popup_row=row;
    request_dialog_presentation_refresh();
}

void Text::dialog_presentation_scroll(float x,float y,int lines)
{
    if (m_closed || !lines) return;
    if (m_popup) {
        const int count=static_cast<int>(text_menu().size());
        m_popup_row=std::clamp(m_popup_row+lines,0,std::max(0,count-1));
        request_dialog_presentation_refresh();
        return;
    }
    ensure_text_layout();
    if(m_presentation_kind==TextPresentationKind::Readme && m_combo_enabled &&
        x>=m_combo_rect.x && x<m_combo_rect.x+m_combo_rect.width &&
        y>=m_combo_rect.y && y<m_combo_rect.y+m_combo_rect.height && !m_list2.empty()) {
        auto event=std::make_shared<::cw::lib::Window>();
        event->id=std::clamp<I>(m_index+(lines>0?1:-1),0,static_cast<I>(m_list2.size())-1);
        if(event->id!=m_index) {
            m_index=event->id;
            OnCombobox(std::move(event));
        }
        return;
    }
    if (!m_richtextctrl || !m_content_visible || x<m_body_rect.x || x>=m_scroll_track.x+m_scroll_track.width ||
        y<m_body_rect.y || y>=m_body_rect.y+m_body_rect.height) return;
    scroll_text(m_richtextctrl->m_scroll_line_typed+lines);
}

void Text::dialog_presentation_wheel(float x,float y,I rotation)
{
    if (m_closed) return;
    ensure_text_layout();
    // Py の文字用ホイール移動量は本文だけに適用する。 ポップアップ行とフ
    // ァイルコンボは固有の選択規則を維持し、指でのスクロールは行単位で扱う。
    if (m_popup || (m_presentation_kind==TextPresentationKind::Readme && m_combo_enabled &&
        x>=m_combo_rect.x && x<m_combo_rect.x+m_combo_rect.width &&
        y>=m_combo_rect.y && y<m_combo_rect.y+m_combo_rect.height)) {
        dialog_presentation_scroll(x,y,rotation>0?-3:3);
        return;
    }
    if (!m_richtextctrl || !m_content_visible || x<m_body_rect.x || x>=m_scroll_track.x+m_scroll_track.width ||
        y<m_body_rect.y || y>=m_body_rect.y+m_body_rect.height) return;
    scroll_text(m_richtextctrl->m_scroll_line_typed+::cw::dialog::richtext_wheel_scroll_delta(rotation));
}

void Text::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
                                       float x,float y,bool pressed)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_closed) {dialog_presentation_pointer_cancel();return;}
    if (!m_popup && index==0 && (action==Action::Previous || action==Action::Next)) {
        // Py の Resource.create_wxbutton(chain=True)
        // と同様、押下時に実行し、待機と間隔の双方に move_repeat を使
        // う。 領域外への移動・解除でタイマーを止め、押したまま戻っても再開しない。
        if (!pressed) {dialog_presentation_pointer_cancel();return;}
        const auto hit=std::find_if(m_page_controls.begin(),m_page_controls.end(),[&](const auto& control) {
            return control.action==action && control.index==index && control.enabled;
        });
        if (hit==m_page_controls.end() || !std::isfinite(x) || !std::isfinite(y) ||
            x<hit->x || y<hit->y || x>=hit->x+hit->width || y>=hit->y+hit->height ||
            !(action==Action::Previous?m_left_enabled:m_right_enabled)) {
            m_page_repeat=0;return;
        }
        if (m_page_capture) return;
        m_page_capture=m_page_repeat=action==Action::Previous?-1:1;
        m_type_search.reset();
        m_focus=action==Action::Previous?TextFocus::Previous:TextFocus::Next;
        if (action==Action::Previous) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
        if (!m_closed && m_page_capture)
            m_page_timer.start(text_input_time(),::cw::dialog::dialog_page_repeat_interval());
        request_dialog_presentation_refresh();
        return;
    }
    if (m_popup || action!=Action::Auxiliary || (index!=200 && index!=201)) {
        if (!pressed) dialog_presentation_action(action,index);
        return;
    }
    if (!m_richtextctrl || !m_content_visible) return;
    ensure_text_layout();
    if (index==201) {
        const I maximum=std::max<I>(0,static_cast<I>(m_rows.size())-m_visible_rows);
        const I thumb=std::min(m_body_rect.height,std::max(text_scale(16),
            m_body_rect.height*m_visible_rows/static_cast<I>(m_rows.size())));
        if (!pressed) {m_drag_scrolling=false;return;}
        const I travel=m_body_rect.height-thumb;
        if (!maximum || !travel) return;
        if (!m_drag_scrolling) {
            const float top=static_cast<float>(m_body_rect.y+travel*m_richtextctrl->m_scroll_line_typed/maximum);
            m_drag_start_y=y>=top && y<top+thumb ? y-top : thumb*0.5F;
            m_drag_scrolling=true;
        }
        scroll_text(static_cast<I>(std::lround((y-m_body_rect.y-m_drag_start_y)*maximum/travel)));
        return;
    }
    if (pressed && !m_selecting) {
        m_selecting=true; m_pointer_moved=false; m_focus=TextFocus::Body;
        const auto caret=text_hit(x,y);
        m_pointer_down=(m_input_modifiers & ::cw::dialog::DialogPresentationModifierShift)?m_anchor:caret;
        select_text(m_pointer_down,caret);
        return;
    }
    if (!m_selecting) return;
    const auto drag_delta=::cw::dialog::richtext_drag_scroll_delta(
        y-m_body_rect.y, m_body_rect.height);
    if (drag_delta) scroll_text(m_richtextctrl->m_scroll_line_typed+drag_delta);
    const auto caret=text_hit(x,y);
    m_pointer_moved=m_pointer_moved || caret!=m_pointer_down;
    select_text(m_pointer_down,caret);
    if (!pressed) {
        m_selecting=false;
        if (!m_pointer_moved) for (const auto& url:m_url_ranges)
            if (caret>=url.first && caret<url.second) {
                open_text_url(m_display_text.substr(url.first,url.second-url.first));
                break;
            }
    }
}

::cw::dialog::ButtonRepeatTimer::Clock::time_point Text::text_input_time() const noexcept
{
    return ::cw::dialog::ButtonRepeatTimer::Clock::now();
}

void Text::repeat_navigation()
{
    if (m_closed || m_popup || (m_page_repeat<0 && !m_left_enabled) ||
        (m_page_repeat>0 && !m_right_enabled)) {
        dialog_presentation_pointer_cancel();return;
    }
    if (!m_page_repeat || !m_page_timer.consume(text_input_time(),::cw::dialog::dialog_page_repeat_interval())) return;
    if (m_page_repeat<0) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
    request_dialog_presentation_refresh();
}

bool Text::dialog_presentation_needs_refresh() const noexcept
{
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        (!m_closed && !m_popup && m_page_repeat && m_page_timer.due(text_input_time()));
}

bool Text::dialog_presentation_finished() const noexcept
{
    return m_closed;
}

void Text::dialog_presentation_complete()
{
    dialog_presentation_pointer_cancel();
    // 入れ子の Readme は親を復元してから、HostedFrameAdapter
    // が完了した子にまだアクティブモデルの所有権があるかを判断する。
    if (m_presentation_close_callback) {
        auto callback = std::move(m_presentation_close_callback);
        callback();
    }
}

void Text::_enable_btn()
{
    // Python版では ScenarioSelect の list が2件以上の時だけ有効化する。
    m_left_enabled=m_right_enabled=m_scenario_count && m_scenario_count()>1;
    // Python版はボタンを残したままEnable/Disableする。
    if (m_leftbtn_typed) m_leftbtn_typed->visible=true;
    if (m_rightbtn_typed) m_rightbtn_typed->visible=true;
}

void Text::set_scenario_navigation(std::function<void(bool)> navigate,
                                   std::function<I()> count)
{
    m_navigate_scenario=std::move(navigate);
    m_scenario_count=std::move(count);
    _enable_btn();
}

void Text::update_lists()
{
    // Text 自体には実装がなく、Readme がデータ供給元を担当する。
}

void Text::upddate_lists()
{
    // Python版の綴りを保った互換フック。実際の更新は update_lists()。
}

void Readme::initialize(std::shared_ptr<::cw::lib::Window> parent, S name,
                        ReadmeList& lists)
{
    assign_lists(lists);
    Text::initialize(std::move(parent),std::move(name));
}

void Readme::update_lists()
{
    // 明示的な取得プロバイダーはネイティブサービスの拡張である。 省略時は Python と同
    // 様、必須の Parent.get_texts() 呼び出しとリストの同一性を維持する。
    if (m_source_provider) {
        auto lists=m_source_provider();
        assign_lists(lists);
    } else {
        auto* source=dynamic_cast<ReadmeSource*>(m_parent.get());
        if (!source) throw std::logic_error("Readme parent has no get_texts");
        auto lists=source->get_texts();
        if (!lists) throw std::logic_error("Readme parent get_texts returned no list");
        assign_lists(*lists);
    }
}

void Readme::assign_lists(ReadmeList& lists)
{
    std::stable_sort(lists.begin(),lists.end(),data_less);
    m_sources=lists;
    m_list.clear();
    m_list2.clear();
    for (const auto& source:m_sources) {
        if (!source) continue;
        m_list.push_back(source->m_name);
        m_list2.push_back(source->m_content);
    }
    m_index=m_index2=0;
}

void Readme::set_source_provider(std::function<std::vector<std::shared_ptr<ReadmeData>>()> provider)
{
    if (m_closed) return;
    m_source_provider=std::move(provider);
    update_lists();
    if (m_richtextctrl) OnCombobox(nullptr);
}

bool show_common_readme(
    std::shared_ptr<::cw::lib::Window> parent, S title, ReadmeList& lists,
    std::function<void(bool)> navigate, std::function<I()> count,
    std::function<ReadmeList()> provider)
{
    // ファイル選択とホストのイベント処理はサービ
    // スとして提供する。 Readme の配置
    // と操作モデルは全 Engine 製品・OS
    // で共有し、UI の実装を一つに保つ。
    auto dialog=std::make_shared<Readme>();
    dialog->initialize(std::move(parent),std::move(title),lists);
    if (provider) dialog->set_source_provider(std::move(provider));
    dialog->set_scenario_navigation(std::move(navigate),std::move(count));
    return ::cw::dialog::show_common_modal_presentation(dialog);
}

void main()
{
}

}
