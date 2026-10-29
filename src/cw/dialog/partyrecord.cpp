// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/partyrecord.hpp"
#include "cw/header.hpp"
#include "cw/cw.hpp"
#include "cw/thread.hpp"
#include "cw/data.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/dialog/select.hpp"
#include "cw/dialog/message.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/util.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
namespace cw::dialog::partyrecord {
using I=::cw::lib::Integer;
using S=::cw::lib::String;

namespace {

S party_record_value_string(const ::cw::lib::Value& value)
{
    if (const auto* text = std::get_if<S>(&value.storage())) return *text;
    return {};
}

S party_message(const S& key, const S& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    if (::cw::setting::Setting::s_msgs) return ::cw::setting::Setting::s_msgs->get(key, fallback);
    return fallback;
}

S format_party_message(S message, const S& value)
{
    const auto position = message.find("%s");
    if (position != S::npos) message.replace(position, 2, value);
    return message;
}

std::shared_ptr<::cw::data::YadoData> active_yado()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return {};
    return *::cw::g_cwpy->m_ydata;
}

std::shared_ptr<::cw::data::YadoData> require_yado()
{
    const auto yado=active_yado();
    if (!yado) throw std::logic_error("SelectPartyRecord requires an active YadoData");
    return yado;
}

bool confirmed(SelectPartyRecord& panel, const S& prompt)
{
    // モーダルの確認中もイベントが処理されるため、先に操作を受付済みとする。 二回目の
    // クリックで選択記録が変わったり、同じ処理が二重に予約されたりしないようにする。
    panel.m_processing=true;
    panel.request_dialog_presentation_refresh();
    try {
        const bool accepted=panel.m_confirm ? panel.m_confirm(prompt) :
            ::cw::dialog::message::show_common_modal_message(
                party_message("message","メッセージ"),prompt,true).value_or(false);
        if(accepted) return true;
    } catch(...) {
        panel.m_processing=false;
        panel.request_dialog_presentation_refresh();
        throw;
    }
    panel.m_processing=false;
    panel.draw(true);
    return false;
}

std::vector<::cw::dialog::DialogPresentationControl> record_controls(const SelectPartyRecord& panel)
{
    using A = ::cw::dialog::DialogPresentationAction;
    const bool ready = !panel.input_blocked();
    // 下部バーのボタンを kDialogFooterExtraHeight だけ高くし、バーの高さも 30 から同じ分だけ増やす。
    constexpr float nav_h = 30 + ::cw::dialog::kDialogFooterExtraHeight, button_h = 24 + ::cw::dialog::kDialogFooterExtraHeight;
    std::vector<::cw::dialog::DialogPresentationControl> controls{
        {A::PreviousGroup,-1,0,280,30,nav_h,ready && panel.m_left2_enabled},
        {A::Previous,-1,30,280,30,nav_h,ready && panel.m_left_enabled},
        {A::Next,-1,400,280,30,nav_h,ready && panel.m_right_enabled},
        {A::NextGroup,-1,430,280,30,nav_h,ready && panel.m_right2_enabled},
        {A::Accept,0,68,283,75,button_h,ready && panel.m_restore_enabled},
        {A::Create,0,151,283,75,button_h,ready && panel.m_save_enabled},
        {A::Auxiliary,0,234,283,75,button_h,ready && panel.m_delete_enabled},
        {A::Cancel,-1,317,283,75,button_h,ready},
        {A::Previous,1,0,0,115,280,ready && panel.m_list.size()>1},
        {A::Activate,0,115,0,230,280,ready && panel.m_restore_enabled},
        {A::Next,1,345,0,115,280,ready && panel.m_list.size()>1},
    };
    // 最初の八項目は下部の押しボタンとしてホバー表示を行
    // う。 三つの大きなページ・記録領域には適用しない。
    for (std::size_t i = 0; i < 8 && i < controls.size(); ++i) controls[i].hover_highlight = true;
    for (auto& c : controls) {
        c.x=::cw::lib::ui::win_i(c.x); c.y=::cw::lib::ui::win_i(c.y);
        c.width=::cw::lib::ui::win_i(c.width); c.height=::cw::lib::ui::win_i(c.height);
    }
    return controls;
}
void render_party_record_surface(SelectPartyRecord& panel)
{
    namespace ui = ::cw::lib::ui;
    const auto w=[](I value){return ui::win_i(value);};
    auto& retained=panel.dialog_retained_surface();
    const ::cw::lib::Size size{w(460),w(310+::cw::dialog::kDialogFooterExtraHeight)};
    const bool full=retained.needs_full(size);
    if(full) {
        retained.allocate(size,{240,240,240,255});
        ui::fill_rect(retained.surface,{0,0,w(460),w(280)},{244,235,205,255});
    }
    auto surface=retained.surface;
    const S base=::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_skindir : S{};
    const S path=base.empty() ? S("Table/Book") :
        ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(base)/"Table/Book");
    // Python と同様、Table/Book は読み込み前に拡張子を解決する。 拡
    // 張子なしのパスを load_wxbmp に渡すと、画像欠落時の黒い描画面になる。
    const auto resolved=::cw::util::find_resource(path,::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? ::cw::g_cwpy->m_rsrc->m_ext_img : ::cw::g_M_IMG);
    if(full&&!resolved.empty()) if(const auto backend=::cw::dialog::select::get_ui_backend())
        if(const auto bitmap=backend->load_select_bitmap(resolved,false))
            ui::blend_surface(surface,bitmap,0,0,w(460),w(280));
    const bool header=panel.m_index>=0 && panel.m_index<static_cast<I>(panel.m_list.size()) &&
        panel.m_list[panel.m_index] && *panel.m_list[panel.m_index];
    const auto mix=[](std::uint64_t& seed,std::uint64_t value) {
        seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
    };
    std::uint64_t content_signature=static_cast<std::uint64_t>(header);
    mix(content_signature,std::hash<S>{}(panel.m_party_name));mix(content_signature,panel.m_money);
    mix(content_signature,panel.m_page_number);mix(content_signature,panel.m_page_count);
    for(const auto& value:panel.m_names)mix(content_signature,std::hash<S>{}(value));
    for(const auto value:panel.m_member_available)mix(content_signature,value);
    for(const auto& value:panel.m_card_names)mix(content_signature,std::hash<S>{}(value));
    for(const auto value:panel.m_card_available)mix(content_signature,value);
    for(const auto& role:std::array<std::pair<S,I>,3>{{{"dlgtitle",20},{"dlgtitle",14},{"dlglist",14}}}) {
        const auto font=ui::configured_font_scaled(role.first,role.second);
        mix(content_signature,std::hash<S>{}(font.face));mix(content_signature,font.pixels);
        mix(content_signature,font.bold);mix(content_signature,font.italic);
    }
    const ::cw::lib::Rect content_rect{0,0,w(460),w(280)};
    const bool paint_content=retained.component_changed("partyrecord.content",content_rect,content_signature);
    const auto controls=record_controls(panel);
    const S labels[]={"<<","<",">",">>",party_message("party_restore","復元"),party_message("party_save","記録"),
        party_message("delete","削除"),party_message("close","閉じる")};
    const S icons[]={"LJUMP","LMOVE","RMOVE","RJUMP"};
    std::array<bool,8> paint_buttons{};
    const auto button_font=ui::configured_font_scaled("button",14);
    for(std::size_t i=0;i<8;++i) {
        const auto& c=controls[i];
        const ::cw::lib::Rect r{I(c.x),I(c.y),I(c.width),I(c.height)};
        std::uint64_t signature=std::hash<S>{}(labels[i]);mix(signature,c.enabled);
        mix(signature,i>=4&&panel.m_button_focus==static_cast<int>(i-4));
        mix(signature,std::hash<S>{}(button_font.face));mix(signature,button_font.pixels);
        paint_buttons[i]=retained.component_changed("partyrecord.button."+std::to_string(i),r,signature);
    }
    if(full)retained.capture_base();
    retained.restore_dirty();surface=retained.surface;
    // Py の draw_adjusted と同様、名前全体をネイティブのテキスト欄ではなく列
    // の幅へ収める。 2 倍・4 倍表示でも文字が鮮明になるよう、最終ウィンドウ解像度で描く。
    const auto label=[&](const S& text,I x,I y,I width,I pixels,const S& role,
                         bool center=true,bool available=true) {
        const auto font=ui::configured_font_scaled(role,pixels);
        const auto glyph=ui::render_label(text,font.pixels,font.bold,
            available ? std::tuple<I,I,I>{0,0,0} : std::tuple<I,I,I>{128,128,128},font.face);
        if(!glyph) return;
        const I fit=std::min<I>(w(width),glyph->size.width);
        ui::blend_surface(surface,glyph,w(x)+(center?(w(width)-fit)/2:0),w(y),fit,glyph->size.height);
    };
    if(paint_content) {
        label(party_message("adventurers_team_record","パーティ記録"),0,25,460,14,"dlgtitle");
        label(header?panel.m_party_name:party_message("new_party_record","新規登録"),5,40,450,20,"dlgtitle");
        label(format_party_message(party_message("adventurers_money","所持金 %s sp"),
            header?std::to_string(panel.m_money):S("---")),0,60,460,14,"dlgtitle");
        for(std::size_t i=0;i<std::min<std::size_t>(6,panel.m_names.size());++i) {
            const I count=i<3?std::min<std::size_t>(3,panel.m_names.size()):std::min<std::size_t>(3,panel.m_names.size()-3);
            label(panel.m_names[i],(460-95*count)/2+95*(i%3)+3,i<3?85:105,89,14,"dlgtitle",true,
                i<panel.m_member_available.size() && panel.m_member_available[i]);
        }
        label(party_message("backpack_record","荷物袋の記録"),0,130,460,16,"dlgtitle");
        const auto columns=std::min<std::size_t>(4,panel.m_card_names.size());
        const auto count=std::min<std::size_t>(columns*6,panel.m_card_names.size());
        for(std::size_t i=0;i<count;++i)
            label(i+1==count && panel.m_card_names.size()>count?party_message("history_etc","その他..."):panel.m_card_names[i],
                (460-84*columns)/2+84*(i%columns)+10,150+16*(i/columns),78,14,"dlglist",false,
                i<panel.m_card_available.size() && panel.m_card_available[i]);
        label(std::to_string(panel.m_page_number)+"/"+std::to_string(panel.m_page_count),0,250,460,14,"dlgtitle");
    }
    for(std::size_t i=0;i<8;++i) {
        if(!paint_buttons[i])continue;
        const auto& c=controls[i];
        const ::cw::lib::Rect r{I(c.x),I(c.y),I(c.width),I(c.height)};
        ui::fill_rect(surface,r,{240,240,240,255});
        ui::draw_button_face(surface,r,{c.enabled});
        auto icon=i<4 && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ?
            ui::resource_surface_for_window(ui::resource_table_surface(::cw::g_cwpy->m_rsrc->m_buttons,icons[i])):nullptr;
        if(icon) {
            if(!c.enabled) {
                icon=std::make_shared<::cw::lib::Surface>(*icon);
                for(std::size_t p=0;p+3<icon->rgba.size();p+=4) {
                    const auto gray=static_cast<std::uint8_t>((icon->rgba[p]+icon->rgba[p+1]+icon->rgba[p+2])/3);
                    icon->rgba[p]=icon->rgba[p+1]=icon->rgba[p+2]=gray;
                    icon->rgba[p+3]/=2;
                }
            }
            ui::blend_surface(surface,icon,r.x+(r.width-icon->size.width)/2,r.y+(r.height-icon->size.height)/2);
        } else {
            auto glyph=ui::render_label(labels[i],button_font.pixels,button_font.bold,
                c.enabled?std::tuple<I,I,I>{0,0,0}:std::tuple<I,I,I>{128,128,128},button_font.face);
            if(glyph) ui::blend_surface(surface,glyph,r.x+(r.width-glyph->size.width)/2,r.y+(r.height-glyph->size.height)/2);
        }
        if(i>=4 && panel.m_button_focus==static_cast<int>(i-4) && c.enabled) {
            const I inset=w(3);
            for(I x=r.x+inset;x<r.x+r.width-inset;x+=2) {
                ui::fill_rect(surface,{x,r.y+inset,1,1},{0,0,0,255});
                ui::fill_rect(surface,{x,r.y+r.height-inset-1,1,1},{0,0,0,255});
            }
            for(I y=r.y+inset;y<r.y+r.height-inset;y+=2) {
                ui::fill_rect(surface,{r.x+inset,y,1,1},{0,0,0,255});
                ui::fill_rect(surface,{r.x+r.width-inset-1,y,1,1},{0,0,0,255});
            }
        }
    }
    retained.finish();panel.m_ui_surface=retained.surface;
}
void play_sound(SelectPartyRecord& panel, const S& name)
{
    if (panel.m_play_sound) { panel.m_play_sound(name); return; }
    if(const auto backend=::cw::dialog::select::get_ui_backend()) backend->play_select_sound(name);
}

StoredPartyPtr party_snapshot(SelectPartyRecord& panel)
{
    if (panel.m_party_snapshot) return panel.m_party_snapshot();
    if (::cw::g_cwpy) return ::cw::g_cwpy->get_partyrecord();
    return {};
}

void rebuild_from_yado(SelectPartyRecord& panel, PartyRecordPtr preferred={})
{
    const auto yado=active_yado();
    if (!yado) return;
    panel.m_list.clear();
    for (const auto& header:yado->m_partyrecord) panel.m_list.emplace_back(header);
    panel.m_list.emplace_back(std::nullopt);
    panel.m_restorable.assign(panel.m_list.size(),std::nullopt);
    panel.m_index=0;
    if (preferred) {
        for (std::size_t i=0;i+1<panel.m_list.size();++i) {
            if (panel.m_list[i] && (*panel.m_list[i]==preferred ||
                (party_record_value_string((*panel.m_list[i])->m_fpath)==
                     party_record_value_string(preferred->m_fpath) &&
                 (*panel.m_list[i])->m_name==preferred->m_name))) {
                panel.m_index=static_cast<I>(i);
                break;
            }
        }
    }
}

std::shared_ptr<::cw::frame::Frame> active_frame()
{
    if (!::cw::g_cwpy) return {};
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage());
    return object&&*object ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
}

void frame_exec(::cw::lib::Callback callback)
{
    if (const auto frame=active_frame()) frame->post_modal_completion(std::move(callback));
    else if (callback) callback({}, {});
}

S operation_error()
{
    try { throw; }
    catch (const std::exception& error) {
        return S("パーティ記録の操作に失敗しました。\n") + error.what();
    }
    catch (...) { return "パーティ記録の操作中に不明なエラーが発生しました。"; }
}

void release_operation(SelectPartyRecord& panel)
{
    panel.m_processing=false;
    const bool owned=std::exchange(panel.m_owns_parent_processing,false);
    panel.request_dialog_presentation_refresh();
    if (owned && panel.m_set_parent_processing) panel.m_set_parent_processing(false);
}

void finish_failed(const std::shared_ptr<SelectPartyRecord>& panel, const S& error)
{
    if (!panel) return;
    PartyRecordPtr preferred;
    if (panel->m_index>=0 && panel->m_index<static_cast<I>(panel->m_list.size())) {
        const auto& current=panel->m_list[static_cast<std::size_t>(panel->m_index)];
        if (current) preferred=*current;
    }
    panel->m_operation_error=error;
    // 共通のエラーダイアログを閉じるまで再入する入力を防ぐ。 エラ
    // ー表示自体が失敗した場合も、両モデルの保留状態を解除する。
    try { if (!panel->m_closed) panel->report_operation_error(error); } catch (...) {}
    if (!panel->m_closed) {
        // 記録の自動置き換えなど、前の段階が既に反映済みの場
        // 合がある。 次の操作を受け付ける前に正本の宿データ
        // から再構築し、古い UI ヘッダーを復活させない。
        try {
            rebuild_from_yado(*panel,preferred);
            if (const auto yado=active_yado())
                panel->m_has_current_party=yado->m_party.has_value();
        } catch (...) {}
    }
    try { release_operation(*panel); } catch (...) {}
    if (!panel->m_closed && panel->m_parent_selected && panel->m_update_parent_standbys) {
        try {
            const auto selected=panel->m_parent_selected();
            if(selected) panel->m_update_parent_standbys(*selected);
        } catch (...) {}
    }
    if (!panel->m_closed && !panel->m_list.empty()) {
        try { panel->draw(true); } catch (...) {}
    }
}

void post_record_finish(const std::shared_ptr<SelectPartyRecord>& panel,
                        ::cw::lib::Callback completion)
{
    panel->post_record_ui([panel,completion=std::move(completion)](const auto& a,const auto& k)->::cw::lib::Value {
        // 古い完了通知や重複した完了通知で、終了済みの操作を再開しない。
        if (!panel->m_processing) return {};
        if (panel->m_closed) { release_operation(*panel); return {}; }
        try { return completion(a,k); }
        catch (...) { finish_failed(panel,operation_error()); return {}; }
    });
}

bool queue_record_work(const std::shared_ptr<SelectPartyRecord>& panel,
                       ::cw::lib::Callback work)
{
    panel->m_operation_error.clear();
    panel->request_dialog_presentation_refresh();
    try {
        panel->post_record_game([panel,work=std::move(work)](const auto& a,const auto& k)->::cw::lib::Value {
            try { return work(a,k); }
            catch (...) {
                const auto error=operation_error();
                post_record_finish(panel,[panel,error](const auto&,const auto&)->::cw::lib::Value {
                    finish_failed(panel,error); return {};
                });
                return {};
            }
        });
        return true;
    } catch (...) { finish_failed(panel,operation_error()); return false; }
}

PartyRecordPtr save_record_data(const std::shared_ptr<SelectPartyRecord>& panel,
                                std::optional<PartyRecordPtr> old_header,
                                I index)
{
    if (!panel) return {};
    const auto yado=require_yado();
    play_sound(*panel,"harvest");
    const auto record=party_snapshot(*panel);
    if (!record) throw std::logic_error("party record save requires a current party");
    const auto saved=old_header && *old_header ? yado->set_partyrecord(index,record) : yado->add_partyrecord(record);
    if (!saved) throw std::runtime_error("failed to save party record");
    return saved;
}

void finish_save(const std::shared_ptr<SelectPartyRecord>& panel, PartyRecordPtr saved)
{
    if (!panel || !saved || panel->m_closed) return;
    rebuild_from_yado(*panel,saved);
    if (const auto yado=active_yado()) panel->m_has_current_party=yado->m_party.has_value();
    panel->m_processing=false;
    panel->draw(true);
}

void finish_restore(const std::shared_ptr<SelectPartyRecord>& panel,
                    const ::cw::lib::Value& parent,
                    const std::optional<::cw::lib::Value>& selected,
                    bool update_list)
{
    if (!panel || panel->m_closed) return;
    PartyRecordPtr preferred;
    if (panel->m_index>=0 && panel->m_index<static_cast<I>(panel->m_list.size())) {
        const auto& current=panel->m_list[static_cast<std::size_t>(panel->m_index)];
        if (current) preferred=*current;
    }
    if (update_list) rebuild_from_yado(*panel,preferred);
    if (const auto yado=active_yado()) panel->m_has_current_party=yado->m_party.has_value();
    panel->draw(true);
    if (selected && panel->m_update_parent_standbys) panel->m_update_parent_standbys(*selected);
    panel->m_processing=false;
    panel->m_owns_parent_processing=false;
    panel->request_dialog_presentation_refresh();
    if (panel->m_set_parent_processing) panel->m_set_parent_processing(false);
    (void)parent; // 親オブジェクトは、アダプターのコールバック群で表す。
}

}

SelectPartyRecord::~SelectPartyRecord() = default;

void SelectPartyRecord::initialize(::cw::lib::Value parent) {
    dialog_retained_surface().reset();
    m_parent=std::move(parent);
    m_index=0;
    m_names.clear();
    m_processing=false;
    m_owns_parent_processing=false;
    m_operation_error.clear();
    m_closed=false;
    m_cancelled=false;
    m_has_current_party=false;
    m_parent_processing={};
    m_parent_selected={};
    m_update_parent_standbys={};
    m_set_parent_processing={};
    m_toppanel=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::lib::Panel>()));
    m_panel=::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::lib::Panel>()));
    const auto make_button=[&]() {
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::lib::Control>()));
    };
    m_restorebtn=make_button();
    m_savebtn=make_button();
    m_deletebtn=make_button();
    m_closebtn=make_button();
    m_buttonlist={m_restorebtn,m_savebtn,m_deletebtn,m_closebtn};
    // PythonのPartySelect親は型付きで自動接続する。明示callbackを設定した
    // 埋込みfrontendは、そのcallbackを後から上書きできる。
    if(const auto* object=std::get_if<::cw::lib::ObjectPtr>(&m_parent.storage());object&&*object){
        if(const auto party=std::dynamic_pointer_cast<::cw::dialog::select::PartySelect>(*object)){
            std::weak_ptr<::cw::dialog::select::PartySelect> weak=party;
            set_parent_callbacks(
                [weak](){const auto value=weak.lock();return value&&value->m_processing;},
                [weak]()->std::optional<::cw::lib::Value>{
                    const auto value=weak.lock(); if(!value)return std::nullopt;
                    const auto selected=value->get_selected();
                    if(!selected||!*selected)return std::nullopt;
                    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*selected));
                },
                [weak](::cw::lib::Value selected){
                    const auto value=weak.lock(); if(!value)return;
                    const auto* object2=std::get_if<::cw::lib::ObjectPtr>(&selected.storage());
                    if(object2&&*object2) if(const auto header=std::dynamic_pointer_cast<::cw::header::PartyHeader>(*object2)) value->update_standbys(header);
                },
                [weak](bool processing){if(const auto value=weak.lock())value->m_processing=processing;});
        } else if(const auto player=std::dynamic_pointer_cast<::cw::dialog::select::PlayerSelect>(*object)){
            std::weak_ptr<::cw::dialog::select::PlayerSelect> weak=player;
            set_parent_callbacks(
                [weak](){const auto value=weak.lock();return value&&value->m__processing;},
                [weak]()->std::optional<::cw::lib::Value>{
                    const auto value=weak.lock(); if(!value)return std::nullopt;
                    const auto selected=value->get_selected();
                    if(!selected||!*selected)return std::nullopt;
                    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*selected));
                },
                [weak](::cw::lib::Value selected){
                    const auto value=weak.lock(); if(!value)return;
                    const auto* object2=std::get_if<::cw::lib::ObjectPtr>(&selected.storage());
                    if(object2&&*object2) if(const auto header=std::dynamic_pointer_cast<::cw::header::AdventurerHeader>(*object2)) value->update_standbys(header);
                },
                [weak](bool processing){if(const auto value=weak.lock())value->m__processing=processing;});
        }
    }

    const auto yado=require_yado();
    m_list.clear();
    for (const auto& header:yado->m_partyrecord) m_list.emplace_back(header);
    m_list.push_back(std::nullopt);
    if (yado) m_has_current_party=yado->m_party.has_value();
    m_restorable.assign(m_list.size(),std::nullopt);
    enable_btn();
    draw(true);
}
void SelectPartyRecord::set_records(std::vector<PartyRecordPtr> records,bool has_party){
    if(m_closed || m_processing) return;
    m_list.clear();for(auto& h:records)if(h)m_list.emplace_back(std::move(h));m_list.push_back(std::nullopt);
    m_restorable.assign(m_list.size(),std::nullopt);m_has_current_party=has_party;
    m_index=std::clamp<I>(m_index,0,I(m_list.size()-1));draw(true);
}
void SelectPartyRecord::set_confirm_callback(std::function<bool(::cw::lib::String)> callback)
{
    m_confirm=std::move(callback);
}

void SelectPartyRecord::set_parent_callbacks(
    std::function<bool()> processing,
    std::function<std::optional<::cw::lib::Value>()> selected,
    std::function<void(::cw::lib::Value)> update_standbys,
    std::function<void(bool)> set_processing)
{
    m_parent_processing=std::move(processing);
    m_parent_selected=std::move(selected);
    m_update_parent_standbys=std::move(update_standbys);
    m_set_parent_processing=std::move(set_processing);
}

void SelectPartyRecord::set_party_snapshot(std::function<StoredPartyPtr()> snapshot)
{
    m_party_snapshot=std::move(snapshot);
}

void SelectPartyRecord::OnSelect(std::shared_ptr<::cw::lib::Event> event)
{
    (void)event;
    if (m_list.empty() || m_index<0 || m_index>=static_cast<I>(m_list.size()) ||
        !m_list[static_cast<std::size_t>(m_index)] || !m_restore_enabled) return;
    OnClickRestoreBtn(nullptr);
}

void SelectPartyRecord::OnCancel(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (input_blocked()) return;
    m_cancelled=true;
    m_closed=true;
    play_sound(*this,"click");
    ::cw::frame::Frame_kill_dlg_if_model(this);
}

void SelectPartyRecord::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (input_blocked() || m_list.size()<=1) return;
    if (m_index<=0) m_index=static_cast<I>(m_list.size())-1;
    else --m_index;
    play_sound(*this,"page");
    draw(true);
}

void SelectPartyRecord::OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (input_blocked() || m_list.size()<=1) return;
    if (m_index<=0) m_index=static_cast<I>(m_list.size())-1;
    else m_index=std::max<I>(0,m_index-10);
    play_sound(*this,"page");
    draw(true);
}

void SelectPartyRecord::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (input_blocked() || m_list.size()<=1) return;
    const auto last=static_cast<I>(m_list.size())-1;
    if (m_index>=last) m_index=0;
    else ++m_index;
    play_sound(*this,"page");
    draw(true);
}

void SelectPartyRecord::OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (input_blocked() || m_list.size()<=1) return;
    const auto last=static_cast<I>(m_list.size())-1;
    if (m_index>=last) m_index=0;
    else m_index=std::min<I>(last,m_index+10);
    play_sound(*this,"page");
    draw(true);
}

void SelectPartyRecord::OnClickSaveBtn(std::shared_ptr<::cw::lib::Window>){
    if(input_blocked()) return;
    const auto index=m_index;
    const auto header=m_list.at(static_cast<std::size_t>(index));
    play_sound(*this,"signal");
    const S prompt=header && *header
        ? format_party_message(party_message("overwrite_party_record","「%s」の記録を上書きしますか？"),
                               (*header)->m_name)
        : party_message("save_party_record","現在のパーティを記録しますか？");
    if (!confirmed(*this,prompt)) return;
    SelectPartyRecord_OnClickSaveBtn_func(shared_from_this(),header,index);
}
void SelectPartyRecord::OnClickRestoreBtn(std::shared_ptr<::cw::lib::Window>){
    if(input_blocked() || m_index<0 || m_index>=static_cast<I>(m_list.size())) return;
    const auto h=m_list.at(static_cast<std::size_t>(m_index));
    if(!h || !*h) throw std::logic_error("party record is required");
    const auto header=*h;
    const auto yado=require_yado();
    if (yado->m_party) {
        play_sound(*this,"signal");
        if (!confirmed(*this,format_party_message(party_message(
                "restore_party","現在のパーティを記録し、「%s」を復元しますか？"),(*h)->m_name))) return;
    }
    try {
        const auto selected=m_parent_selected?m_parent_selected():std::optional<::cw::lib::Value>{};
        SelectPartyRecord_OnClickRestoreBtn_func(header,shared_from_this(),m_parent,selected);
    } catch (...) { finish_failed(shared_from_this(),operation_error()); }
}
void SelectPartyRecord::OnClickDeleteBtn(std::shared_ptr<::cw::lib::Window>){
    if(input_blocked() || m_list.empty()) return;
    const auto pos=static_cast<std::size_t>(m_index);
    if(pos>=m_list.size() || !m_list.at(pos) || !*m_list.at(pos)) return;
    const auto header=*m_list[pos];
    play_sound(*this,"signal");
    if (!confirmed(*this,format_party_message(party_message(
            "delete_party_record","「%s」の記録を削除しますか？"),header->m_name))) return;
    play_sound(*this,"dump");
    m_processing=true;
    const auto self=shared_from_this();
    const auto update=[self](const auto&,const auto&)->::cw::lib::Value {
        self->m_restorable.assign(self->m_list.size(),std::nullopt);
        if (const auto yado=active_yado()) self->m_has_current_party=yado->m_party.has_value();
        self->m_processing=false;
        self->draw(true);
        return {};
    };
    if (!queue_record_work(self,[self,header,update](const auto&,const auto&)->::cw::lib::Value {
        SelectPartyRecord_OnClickDeleteBtn_func(header);
        post_record_finish(self,update);
        return {};
    })) return;
    m_list.erase(m_list.begin()+static_cast<std::ptrdiff_t>(pos));
    const I last_record=m_list.size()>1?static_cast<I>(m_list.size())-2:0;
    m_index=std::max<I>(0,std::min(m_index,last_record));
    m_has_current_party=require_yado()->m_party.has_value();
    if(m_list.size()<=1 && !m_has_current_party) {
        m_processing=false;
        OnCancel(nullptr);
    }

}
bool SelectPartyRecord::can_clickcenter(){return m_restore_enabled;}
bool SelectPartyRecord::can_clickside(){return m_list.size()>1;}
void SelectPartyRecord::enable_btn(){
    if(m_list.empty())throw std::logic_error("empty party record list");
    (void)require_yado();
    if (const auto yado=active_yado()) m_has_current_party=yado->m_party.has_value();
    m_left_enabled=m_left2_enabled=m_right_enabled=m_right2_enabled=m_list.size()>1;
    _update_restorable();const auto p=std::size_t(m_index);const bool header=bool(m_list.at(p));
    m_save_enabled=m_has_current_party;m_delete_enabled=header;
    m_restore_enabled=header&&m_restorable.at(p)&&std::get<0>(*m_restorable[p]);
    m_button_focus=m_restore_enabled?0:m_save_enabled?1:m_delete_enabled?2:3;
}
void SelectPartyRecord::_update_restorable(){
    if(m_restorable.size()!=m_list.size())m_restorable.resize(m_list.size());
    const auto p=std::size_t(m_index);if(p>=m_list.size()||!m_list[p]||!*m_list[p]||m_restorable[p])return;
    bool can=false;std::map<::cw::lib::String,bool> members;std::vector<bool> cards;
    const auto& h=**m_list[p];
    const auto yado=active_yado();
    for(const auto& value:h.m_members){
        if(const auto* s=std::get_if<::cw::lib::String>(&value.storage())) {
            const bool available = yado && yado->can_restore(*s);
            members[*s]=available; can = can || available;
        }
    }
    const auto* recordpath=std::get_if<::cw::lib::String>(&h.m_fpath.storage());
    if(recordpath && !recordpath->empty()) {
        const auto root=::cw::data::yadoxml2element(*recordpath,"BackpackRecord",std::nullopt);
        std::vector<std::shared_ptr<::cw::header::CardHeader>> candidates;
        if(yado) {
            candidates.insert(candidates.end(),yado->m_storehouse.begin(),yado->m_storehouse.end());
            if(yado->m_party && *yado->m_party) candidates.insert(candidates.end(),(*yado->m_party)->m_backpack.begin(),(*yado->m_party)->m_backpack.end());
        }
        std::vector<std::shared_ptr<::cw::header::CardHeader>> used;
        if(root) {
            for(std::size_t i=0;i<root->m_children.size();++i){
                const auto& ce=root->m_children[i];
                if(i>=24) break;
                if(!ce || ce->m_tag!="CardRecord") continue;
                const auto name=ce->get("name",""); const auto desc=ce->get("desc",""); bool found=false;
                for(const auto& card:candidates) {
                    if(!card || std::find(used.begin(),used.end(),card)!=used.end()) continue;
                    if(card->m_name==name && card->m_desc==desc){used.push_back(card);found=true;break;}
                }
                cards.push_back(found);
            }
        }
    }
    m_restorable[p]=RestorableData{can,std::move(members),std::move(cards)};
}
void SelectPartyRecord::draw(bool update){
    if(m_list.empty())throw std::logic_error("empty party record list");
    m_index=std::clamp<I>(m_index,0,static_cast<I>(m_list.size())-1);
    m_names.clear();
    m_member_available.clear();
    m_card_names.clear();
    m_card_available.clear();
    m_money=0;
    m_party_name.clear();
    m_page_number=m_index+1;
    m_page_count=static_cast<I>(m_list.size());
    _update_restorable();
    if(const auto& item=m_list.at(std::size_t(m_index));item){
        auto& header=**item;
        m_names=header.get_membernames();
        m_money=header.m_money;
        m_party_name=header.m_name;
        m_card_names=header.m_backpack_names;
        const auto& restorable=m_restorable.at(std::size_t(m_index));
        if(restorable){
            const auto& available_members=std::get<1>(*restorable);
            for(std::size_t i=0;i<header.m_members.size();++i){
                const auto* member=std::get_if<::cw::lib::String>(&header.m_members[i].storage());
                m_member_available.push_back(member && available_members.contains(*member) && available_members.at(*member));
            }
            m_card_available=std::get<2>(*restorable);
        }
    }
    if(update)enable_btn();
    render_party_record_surface(*this);
    request_dialog_presentation_refresh();
}

::cw::dialog::DialogPresentation SelectPartyRecord::dialog_presentation()
{
    render_party_record_surface(*this);
    ::cw::dialog::DialogPresentation view;
    view.route="PARTYRECORD";
    view.title=party_message("select_party_record","パーティ記録");
    view.surface=m_ui_surface;
    view.controls=record_controls(*this);
    const auto& retained=dialog_retained_surface();
    view.damage=retained.damage;view.surface_revision=retained.revision;
    return view;
}
bool SelectPartyRecord::input_blocked() const
{
    return m_closed || m_processing || (m_parent_processing && m_parent_processing());
}
void SelectPartyRecord::dialog_presentation_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using A=::cw::dialog::DialogPresentationAction;
    if(input_blocked()) return;
    if(action==A::Cancel) OnCancel(nullptr);
    else if(action==A::Previous) OnClickLeftBtn(nullptr);
    else if(action==A::PreviousGroup) OnClickLeft2Btn(nullptr);
    else if(action==A::Next) OnClickRightBtn(nullptr);
    else if(action==A::NextGroup) OnClickRight2Btn(nullptr);
    else if(action==A::Accept || action==A::Activate) OnSelect(nullptr);
    else if(action==A::Create) OnClickSaveBtn(nullptr);
    else if(action==A::Auxiliary && index==0) OnClickDeleteBtn(nullptr);
    request_dialog_presentation_refresh();
}
void SelectPartyRecord::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using K=::cw::dialog::DialogPresentationKey;
    using A=::cw::dialog::DialogPresentationAction;
    if(!pressed || input_blocked()) return;
    if((modifiers & ::cw::dialog::DialogPresentationModifierControl) && (key==K::Left || key==K::Right)) {
        const bool group=modifiers & ::cw::dialog::DialogPresentationModifierAlt;
        dialog_presentation_action(key==K::Left?(group?A::PreviousGroup:A::Previous):(group?A::NextGroup:A::Next),-1);
        return;
    }
    if(key==K::Tab || key==K::Left || key==K::Right) {
        const auto controls=record_controls(*this);
        const int step=key==K::Left || (key==K::Tab && (modifiers & ::cw::dialog::DialogPresentationModifierShift))?-1:1;
        for(int n=0;n<4;++n) {
            m_button_focus=(m_button_focus+step+4)%4;
            if(controls[4+m_button_focus].enabled) break;
        }
        request_dialog_presentation_refresh();
    } else if(key==K::Enter || key==K::Space) {
        const auto controls=record_controls(*this);
        const auto& c=controls[4+std::clamp(m_button_focus,0,3)];
        if(c.enabled) dialog_presentation_action(c.action,c.index);
    }
}
void SelectPartyRecord::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}
bool SelectPartyRecord::dialog_presentation_finished() const noexcept { return m_closed; }
void SelectPartyRecord::dialog_presentation_complete()
{
    m_closed=true;
    release_operation(*this);
}

void SelectPartyRecord::post_record_game(::cw::lib::Callback callback)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(std::move(callback));
    else callback({}, {});
}

void SelectPartyRecord::post_record_ui(::cw::lib::Callback callback)
{
    frame_exec(std::move(callback));
}

void SelectPartyRecord::report_operation_error(const S& error)
{
    (void)::cw::dialog::message::show_common_modal_message(
        party_message("error","エラー"),error,false,true);
}

void SelectPartyRecord_OnClickSaveBtn_func(std::shared_ptr<SelectPartyRecord> panel,
                                           std::optional<PartyRecordPtr> header,
                                           I index)
{
    if(!panel || panel->m_closed) return;
    panel->m_processing=true;
    queue_record_work(panel,[panel,header=std::move(header),index](const auto&,const auto&)->::cw::lib::Value {
        const auto saved=save_record_data(panel,header,index);
        post_record_finish(panel,[panel,saved](const auto&,const auto&)->::cw::lib::Value {
            finish_save(panel,saved); return {};
        });
        return {};
    });
}

void SelectPartyRecord_OnClickSaveBtn_func_func(std::shared_ptr<SelectPartyRecord> panel,
                                                PartyRecordPtr header)
{
    finish_save(panel,std::move(header));
}

void SelectPartyRecord_OnClickRestoreBtn_func(PartyRecordPtr header,
                                              std::shared_ptr<SelectPartyRecord> panel,
                                              ::cw::lib::Value parent,
                                              std::optional<::cw::lib::Value> selected)
{
    if(!panel || !header || panel->m_closed) return;
    panel->m_processing=true;
    try {
        if (panel->m_set_parent_processing) {
            panel->m_owns_parent_processing=true;
            panel->m_set_parent_processing(true);
        }
        queue_record_work(panel,[panel,header,parent,selected](const auto&,const auto&)->::cw::lib::Value {
            const auto yado=require_yado();
            play_sound(*panel,"harvest");
            const bool update_list=yado->m_party.has_value();
            if (update_list && ::cw::g_cwpy) ::cw::g_cwpy->save_partyrecord();
            (void)yado->restore_party(header);
            post_record_finish(panel,[panel,parent,selected,update_list](const auto&,const auto&)->::cw::lib::Value {
                finish_restore(panel,parent,selected,update_list); return {};
            });
            return {};
        });
    } catch (...) { finish_failed(panel,operation_error()); }
}

void SelectPartyRecord_OnClickRestoreBtn_func_func(std::shared_ptr<SelectPartyRecord> panel,
                                                   ::cw::lib::Value parent,
                                                   std::optional<::cw::lib::Value> selected,
                                                   bool update_list)
{
    finish_restore(panel,parent,selected,update_list);
}

void SelectPartyRecord_OnClickDeleteBtn_func(PartyRecordPtr header)
{
    if (!header) return;
    require_yado()->remove_partyrecord(header);
}

void SelectPartyRecord_OnClickDeleteBtn_func2(std::shared_ptr<SelectPartyRecord> panel)
{
    if(panel) panel->draw(true);
}

void SelectPartyRecord_OnClickDeleteBtn_func2_func(std::shared_ptr<SelectPartyRecord> panel)
{
    if(panel) panel->draw(true);
}
void main(){}
}
