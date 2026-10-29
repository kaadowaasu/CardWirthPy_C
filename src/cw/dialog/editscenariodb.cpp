// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/editscenariodb.hpp"
#include "cw/dialog/progress.hpp"
#include "cw/dialog/message.hpp"
#include "cw/cw.hpp"
#include "cw/frame.hpp"
#include "cw/scenariodb.hpp"
#include "cw/thread.hpp"
#include "cw/lib/ui_surface.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>


namespace cw::dialog::editscenariodb {

namespace {
using ::cw::lib::Integer;
using ::cw::lib::String;
ConstructScenarioDB* g_active_dialog{};

template <typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

bool value_bool(const ::cw::lib::Value& value)
{
    if (const auto* result=std::get_if<bool>(&value.storage())) return *result;
    return false;
}

std::shared_ptr<::cw::scenariodb::Scenariodb> open_database_direct()
{
    if (::cw::g_cwpy) {
        if (const auto frame=object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame)) {
            const auto database=frame->open_scenariodb();
            if (database && *database) return *database;
        }
    }
    return nullptr;
}

std::optional<::cw::lib::Callback> cancel_callback(ConstructScenarioDB* dialog)
{
    return ::cw::lib::Callback([dialog](const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&) {
        return ::cw::lib::Value(dialog->is_cancelled());
    });
}
}

void ConstructScenarioDB::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ScenarioDbPaths arg_dpaths)
{
    dialog_retained_surface().reset();
    m_cwpy_debug=true;
    g_active_dialog=this;
    m_dpaths=std::move(arg_dpaths);
    (void)arg_parent;
    { std::lock_guard lock(m_state_mutex);m__message="フォルダの一覧を作成しています...";m__error_message.clear(); }
    m__curnum=0;
    m__complete=false;
    m__cancel=false;
    m__clear=false;
    m_clear=::cw::lib::Value(false);
    m__failed=false;
    m_started=false;
    m_closed=false;
    m_modal_active=false;
    m_focus_index=1;
    m_database.reset();
    m_open_database=open_database_direct;
    _do_layout();_bind();
    // すべてのホストで、モデル所有の Presentation を唯一の UI と
    // する。 OS やモードから渡すのは配置・拡大率・入力転送の指定だけとする。
    ::cw::dialog::publish_transient_dialog_presentation(shared_from_this());
}

void ConstructScenarioDB::_bind()
{
}

void ConstructScenarioDB::_do_layout()
{
}

ConstructScenarioDB::~ConstructScenarioDB()
{
    m__cancel=true;
    if(m_worker.joinable())m_worker.join();
    ::cw::dialog::clear_transient_dialog_presentation(this);
    if(g_active_dialog==this)g_active_dialog=nullptr;
}

// Py の完了状態はキャンセルと成功を区別しない。ネイティブの complete
// はキャンセルも含めワーカーの終了を表し、 failed と cancel
// は別に保持する。共通の進捗画面は終了したワーカーを閉じつつ、
// キャンセル・失敗したデータベース構築を成功として表示しない。
void ConstructScenarioDB::construct_scenariodb()
{
    { std::lock_guard lock(m_state_mutex);m__message="フォルダの一覧を作成しています...";m__error_message.clear(); }
    m__curnum=0;
    m__complete=false;
    m__failed=false;
    {
        while(!::cw::scenariodb::ScenariodbUpdatingThread::is_finished() && !m__cancel.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (m__cancel.load()) { m__complete=true; return; }
        ScenarioDbPaths found;
        std::size_t index=0;
        std::size_t count=0;
        const auto is_cancel=cancel_callback(this);
        for (const auto& [skintype,paths]:m_dpaths) {
            if (m__cancel) break;
            ++index;
            { std::lock_guard lock(m_state_mutex);m__message="フォルダの一覧を作成しています... ("+std::to_string(index)+"/"+std::to_string(m_dpaths.size())+")"; }
            auto found_skin=std::find_if(found.begin(),found.end(),[&](const auto& item) {
                return item.first == skintype;
            });
            if (found_skin == found.end()) {
                found.emplace_back(skintype, std::vector<String>{});
                found_skin = std::prev(found.end());
            }
            auto& directories=found_skin->second;
            for (const auto& path:paths) {
                if (m__cancel) break;
                const auto discovered=::cw::scenariodb::find_alldirectories(path,is_cancel);
                count+=discovered.size();
                for (const auto& directory : discovered) {
                    if (std::find(directories.begin(),directories.end(),directory) == directories.end())
                        directories.push_back(directory);
                }
            }
            ++m__curnum;
        }
        if (m__cancel.load()) { m__complete=true; return; }
        if (!m_database && m_open_database) m_database=m_open_database();
        const auto database=m_database;
        if (!database) return;
        if (m__clear && !m__cancel) database->delete_all(false);
        std::size_t completed=0;
        for (const auto& [skintype,paths]:found) {
            if (m__cancel) break;
            for (const auto& path:paths) {
                if (m__cancel) break;
                { std::lock_guard lock(m_state_mutex);m__message="シナリオを登録しています... ("+std::to_string(completed+1)+"/"+std::to_string(count)+")"; }
                database->update(path,skintype,false,true);
                ++completed;
                m__curnum=static_cast<::cw::lib::Integer>(m_dpaths.size()+
                    (count ? (static_cast<double>(completed)/static_cast<double>(count))*100.0 : 0.0));
            }
        }
        if (!m__cancel) {
            database->commit();
            { std::lock_guard lock(m_state_mutex);m__message="データベース内の空領域を再編成しています..."; }
            database->vacuum(true);
        }
        m__curnum=static_cast<::cw::lib::Integer>(101+m_dpaths.size());
        m__complete=true;
    }}

void ConstructScenarioDB::OnClickOkBtn(std::shared_ptr<::cw::lib::Window>)
{
    if(m_closed || m_started) return;
    m_started=true;
    m__cancel=false;m__complete=false;m__failed=false;
    request_dialog_presentation_refresh();
    const auto self=shared_from_this();
    // 入力コールバックが戻ってから子画面を開く。 処理を開始するのは共通モ
    // ーダルホストが子を確認した時であり、モデルの生成だけでは開始しない。
    try {
        if(!::cw::dialog::post_common_dialog_action([self](const auto&,const auto&)->::cw::lib::Value {
            if(!self->m_closed) {
                try { self->run_construction(); }
                catch(const std::exception& error) {
                    {std::lock_guard lock(self->m_state_mutex);self->m__error_message=error.what();}
                    try { (void)self->show_result(true); } catch(...) {}
                    self->reset_prompt();
                } catch(...) { self->reset_prompt(); }
            }
            return {};
        })) reset_prompt();
    } catch(...) { reset_prompt(); throw; }
}

void ConstructScenarioDB::reset_prompt()
{
    m_started=false;
    m_focus_index=1;
    m__complete=false;m__cancel=false;m__failed=false;m__curnum=0;
    request_dialog_presentation_refresh();
}

bool ConstructScenarioDB::show_result(bool failed)
{
    auto notification=std::make_shared<::cw::dialog::message::SysMessage>();
    notification->initialize({},failed?"エラー":"メッセージ",
        failed ? ("シナリオデータベースの構築に失敗しました。\n"+error_message())
               : "データベースの構築が完了しました。",
        std::vector<::cw::dialog::message::MessageChoice>{{"OK",::cw::dialog::message::kWxIdOk}},
        std::vector<::cw::dialog::message::MessageCheckBox>{});
    return ::cw::dialog::show_common_modal_presentation(notification) &&
        notification->m_closed; // Python と同様、OK だけの通知をどの方法で閉じた場合も、確認画面を終了する。
}

void ConstructScenarioDB::run_construction()
{
    using Progress=::cw::dialog::progress::SysProgressDialog;
    auto progress=std::make_shared<Progress>();
    progress->initialize({},"シナリオデータベースの構築","",
        101+static_cast<Integer>(m_dpaths.size()),0,true);
    struct Session {std::mutex mutex;bool launched{},retired{};};
    const auto session=std::make_shared<Session>();
    const auto self=shared_from_this();
    const std::weak_ptr<Progress> weak=progress;
    m_modal_active=true;
    struct Retire {
        ConstructScenarioDB& owner;std::shared_ptr<Progress> progress;std::shared_ptr<Session> session;
        ~Retire() {
            {std::lock_guard lock(session->mutex);session->retired=true;}
            progress->set_modal_callbacks({},{});
            if(!owner.m__complete.load()) owner.m__cancel=true;
            if(owner.m_worker.joinable()) owner.m_worker.join();
            progress->close();owner.m_modal_active=false;
            if(!owner.m_closed) owner.reset_prompt();
        }
    } retire{*this,progress,session};
    progress->set_modal_callbacks([self,weak,session] {
        std::lock_guard lock(session->mutex);
        if(session->retired) return true;
        const bool first_poll=!session->launched;
        if(!session->launched) {
            if(self->m__cancel.load() || self->m_closed) return true;
            session->launched=true;
            self->start_worker();
        }
        if(const auto child=weak.lock()) child->UpdateProgress(self->current_progress(),self->message());
        // 単体版の ensure_window は描画面を生成する前に完
        // 了を確認する。 処理がすぐ終わっても初回の表示は許可し、完了済
        // みの操作をホストがダイアログの起動失敗と判定しないようにする。
        return !first_poll && self->is_complete();
    },[self]{self->set_cancel(true);});
    const bool shown=::cw::dialog::show_common_modal_presentation(progress);
    {std::lock_guard lock(session->mutex);session->retired=true;}
    progress->set_modal_callbacks({},{});
    if(!shown || !is_complete()) m__cancel=true;
    if(m_worker.joinable()) m_worker.join();
    progress->close();
    if(!shown || !session->launched || is_cancelled() || m_closed) return;
    const bool failed=m__failed.load();
    if(show_result(failed) && !failed) {m_closed=true;visible=false;}
}

void ConstructScenarioDB::start_worker()
{
    // Python のモーダル進捗画面と同様、開始の繰り返しや設定変更を防
    // ぐ。 コントロールだけでなく共通モデルでも、この操作の占有を守る。
    if (m_closed || m_worker.joinable()) return;
    m__complete=false;
    m__cancel=false;
    m__failed=false;
    m_started=true;
    m_focus_index=2;
    request_dialog_presentation_refresh();
    { std::lock_guard lock(m_state_mutex);m__error_message.clear();m__message="フォルダの一覧を作成しています..."; }
    if (!m_clear.is_none()) m__clear=value_bool(m_clear);
    const auto fail=[this](String error) {
        { std::lock_guard lock(m_state_mutex);m__error_message=std::move(error);
          m__message="シナリオデータベースの構築に失敗しました。"; }
        m__failed=true;
        m__complete=true;
    };
    try {
      m_worker=std::thread([this,fail] {
        try {
            construct_scenariodb();
        } catch (const std::exception& error) {
            fail(error.what());
        } catch (...) {
            fail("不明なエラー");
        }
      });
    } catch (const std::exception& error) { fail(error.what()); }
      catch (...) { fail("不明なエラー"); }
}

void ConstructScenarioDB::set_clear(bool arg_clear)
{
    if (m_started || m_closed) return;
    m__clear=arg_clear;
    m_clear=::cw::lib::Value(arg_clear);
    request_dialog_presentation_refresh();
}

void ConstructScenarioDB::set_cancel(bool arg_cancel)
{
    m__cancel=arg_cancel;
    request_dialog_presentation_refresh();
}

bool ConstructScenarioDB::is_complete() const
{
    return m__complete;
}

bool ConstructScenarioDB::is_cancelled() const
{
    return m__cancel;
}

::cw::lib::Integer ConstructScenarioDB::current_progress() const
{
    return m__curnum;
}

::cw::lib::String ConstructScenarioDB::message() const
{
    std::lock_guard lock(m_state_mutex);return m__message;
}

::cw::lib::String ConstructScenarioDB::error_message() const
{
    std::lock_guard lock(m_state_mutex);return m__error_message;
}

void ConstructScenarioDB::poll_ui()
{
    if(m_modal_active || !m__complete.load())return;
    if(m_worker.joinable())m_worker.join();
    if (m__cancel.load()) {
        {
            std::lock_guard lock(m_state_mutex);
            m__message = "フォルダの一覧を作成しています...";
            m__error_message.clear();
        }
        m__curnum = 0;
        m__complete = false;
        m__cancel = false;
        m__failed = false;
        m_started = false;
        m_focus_index = 1;
        request_dialog_presentation_refresh();
        return;
    }
}

::cw::dialog::DialogPresentation ConstructScenarioDB::dialog_presentation()
{
    using namespace ::cw::lib;
    using Action=::cw::dialog::DialogPresentationAction;
    // Python と同様、システムの StaticText/CheckBox/Button のフォントと sizer.Fit
    // を使い、ゲームの 2 倍・4 倍拡大は適用しない。 矩形はすべて最終ピクセル単位とする。
    const auto metrics=ui::system_dialog_metrics();
    const auto& font=metrics.font;
    const auto px=[&](Integer n){return metrics.scaled(n);};
    const auto text_width=[&](const String& text){return ui::label_width(text,font.pixels,font.bold,font.face);};
    static const std::array<String,5> lines{
        "シナリオフォルダを起点として発見できる全てのシナリオを",
        "シナリオデータベースに登録します。",
        "データベースに登録されたシナリオはシナリオ選択ダイアログで",
        "高速に表示できる他、検索で発見できるようになります。",
        "シナリオデータベースの構築を開始しますか？"};
    const String option="構築前にデータベースを初期化する";
    const Integer margin=px(10),gap=px(10),button_h=std::max(px(24),metrics.line_height+px(8));
    const Integer button_w=std::max({px(80),text_width("構築開始...")+px(24),text_width("キャンセル")+px(24)});
    const Integer check_h=std::max(px(20),metrics.line_height+px(4));
    const Integer check_w=px(20)+text_width(option);
    Integer content_w=std::max(check_w,button_w*2+gap);
    for(const auto& line:lines)content_w=std::max(content_w,text_width(line));
    const Integer width=content_w+margin*2;
    const Integer check_y=margin+static_cast<Integer>(lines.size())*metrics.line_height+gap;
    const Integer button_y=check_y+check_h+gap;
    const Integer height=button_y+button_h+margin;
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full({width,height});
    if(full) {retained.allocate({width,height},{240,240,240,255});retained.capture_base();}
    auto surface=retained.surface;
    bounds.width=width;bounds.height=height;
    ::cw::dialog::DialogPresentation result;result.route="SCENARIODB";result.title="シナリオデータベースの構築";
    const auto draw=[&](const String& text,Integer x,Integer y){
        ui::draw_label(surface,text,x,y,font.pixels,font.bold,
            m_started?std::tuple<Integer,Integer,Integer>{128,128,128}:std::tuple<Integer,Integer,Integer>{0,0,0},font.face);
    };
    const auto focus=[&](Rect r){
        const auto stroke=std::max<Integer>(1,px(1));const auto inset=px(3);
        r.x+=inset;r.y+=inset;r.width-=2*inset;r.height-=2*inset;
        for(Integer x=r.x;x<r.x+r.width;x+=stroke*2){
            ui::fill_rect(surface,{x,r.y,stroke,stroke},{40,40,40,255});
            ui::fill_rect(surface,{x,r.y+r.height-stroke,stroke,stroke},{40,40,40,255});
        }
        for(Integer y=r.y;y<r.y+r.height;y+=stroke*2){
            ui::fill_rect(surface,{r.x,y,stroke,stroke},{40,40,40,255});
            ui::fill_rect(surface,{r.x+r.width-stroke,y,stroke,stroke},{40,40,40,255});
        }
    };
    const Rect check{width-margin-check_w,check_y,check_w,check_h};
    const Rect box{check.x,check.y+(check.height-px(13))/2,px(13),px(13)};
    result.controls.push_back({Action::Toggle,0,float(check.x),float(check.y),float(check.width),float(check.height),!m_started&&!m_closed});
    const Rect first_button{width-margin-button_w*2-gap,button_y,button_w,button_h};
    const Rect second_button{width-margin-button_w,button_y,button_w,button_h};
    result.controls.push_back({Action::Accept,0,float(first_button.x),float(first_button.y),float(first_button.width),float(first_button.height),!m_started&&!m_closed});
    result.controls.back().hover_highlight=true;
    result.controls.push_back({Action::Cancel,0,float(second_button.x),float(second_button.y),float(second_button.width),float(second_button.height),!m_started&&!m_closed});
    result.controls.back().hover_highlight=true;
    const auto state_signature=(static_cast<std::uint64_t>(m_started)<<32U) ^
        (static_cast<std::uint64_t>(m_closed)<<24U) ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_focus_index+1));
    const bool body_changed=retained.component_changed("scenariodb.body",
        {margin,margin,content_w,static_cast<Integer>(lines.size())*metrics.line_height},
        static_cast<std::uint64_t>(m_started));
    const bool check_changed=retained.component_changed("scenariodb.clear",check,
        state_signature ^ (static_cast<std::uint64_t>(m__clear)<<40U));
    const bool first_changed=retained.component_changed("scenariodb.start",first_button,state_signature);
    const bool second_changed=retained.component_changed("scenariodb.cancel",second_button,state_signature);
    retained.restore_dirty();surface=retained.surface;
    if(body_changed) {
        Integer y=margin;for(const auto& line:lines){draw(line,margin,y);y+=metrics.line_height;}
    }
    if(check_changed) {
        ui::fill_rect(surface,box,{255,255,255,255});ui::draw_sunken_border(surface,box);
        if(m__clear){
            const auto stroke=std::max<Integer>(1,px(1));
            for(Integer p=0;p<px(4);++p)ui::fill_rect(surface,{box.x+px(2)+p,box.y+px(6)+p,stroke,stroke},{0,0,0,255});
            for(Integer p=0;p<px(6);++p)ui::fill_rect(surface,{box.x+px(5)+p,box.y+px(9)-p,stroke,stroke},{0,0,0,255});
        }
        draw(option,check.x+px(20),check.y+(check.height-font.pixels)/2);
        if(!m_started && !m_closed && m_focus_index==0)focus(check);
    }
    const auto button=[&](String label,int index,const Rect& r,bool changed){
        if(!changed)return;
        ui::draw_button_face(surface,r,{!m_started});
        draw(label,r.x+(r.width-text_width(label))/2,r.y+(r.height-font.pixels)/2);
        if(!m_started && !m_closed && m_focus_index==index)focus(r);
    };
    button("構築開始...",1,first_button,first_changed);
    button("キャンセル",2,second_button,second_changed);
    retained.finish();result.surface=std::move(surface);result.damage=retained.damage;
    result.surface_revision=retained.revision;return result;
}

void ConstructScenarioDB::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    (void)index;
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_closed || m_started) return; // 処理をキャンセルできるのはアクティブな子画面だけとする。
    if(action==Action::Toggle){m_focus_index=0;set_clear(!m__clear);}
    else if(action==Action::Cancel){m_closed=true;visible=false;}
    else if(action==Action::Accept || action==Action::Select)OnClickOkBtn(nullptr);
}

bool ConstructScenarioDB::dialog_presentation_finished() const noexcept
{
    return m_closed;
}

void ConstructScenarioDB::dialog_presentation_key(
    ::cw::dialog::DialogPresentationKey key, bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    if(!pressed || m_closed || m_started) return;
    if(key==Key::Tab && !(modifiers & ~::cw::dialog::DialogPresentationModifierShift)) {
        m_focus_index=(m_focus_index+
            ((modifiers & ::cw::dialog::DialogPresentationModifierShift)?2:1))%3;
        request_dialog_presentation_refresh();return;
    }
    if(modifiers || (key!=Key::Space && key!=Key::Enter)) return;
    if(key==Key::Space && m_focus_index==0) dialog_presentation_action(Action::Toggle,0);
    else dialog_presentation_action(m_focus_index==2?Action::Cancel:Action::Accept,0);
}

void ConstructScenarioDB::dialog_presentation_enter(
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}

void ConstructScenarioDB::dialog_presentation_complete()
{
    m_closed=true;visible=false;
    if(!is_complete())m__cancel=true;
    ::cw::dialog::clear_transient_dialog_presentation(this);
}

::cw::lib::Value ConstructScenarioDB_construct_scenariodb_lambda_line_93()
{
    return ::cw::lib::Value(g_active_dialog&&g_active_dialog->is_cancelled());
}

void ConstructScenarioDB_OnClickOkBtn_progress()
{
    if(g_active_dialog)g_active_dialog->poll_ui();
}

void main()
{
}

}

