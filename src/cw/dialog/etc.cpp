// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/etc.hpp"
#include "cw/dialog/input_presentation.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/textedit_input.hpp"
#include "cw/dialog/textedit_render.hpp"
#include "cw/battle.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/frame.hpp"
#include "cw/header.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/scenariodb.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace cw::dialog::etc {
namespace {
using ::cw::lib::Integer;
using ::cw::lib::String;
using ::cw::lib::Value;

template <typename T>
std::shared_ptr<T> object_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

String value_string(const Value& value)
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return std::to_string(*real);
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
    return {};
}

bool value_bool(const Value& value, bool fallback = false)
{
    if (const auto* result = std::get_if<bool>(&value.storage())) return *result;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer != 0;
    return fallback;
}

String etc_message(const char* key, const char* fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

void play_etc_sound(const char* name)
{
    if (::cw::g_cwpy)
        ::cw::g_cwpy->play_sound(name, false, 100, 1, 0, 0, false);
}

Integer event_id(const std::shared_ptr<::cw::lib::Window>& event)
{
    return event ? event->id : -1;
}

String basename(const String& path)
{
    if (path.empty()) return {};
    return ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
}

String join_path(const String& left, const String& right)
{
    if (left.empty()) return right;
    return ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(left) / std::filesystem::u8path(right));
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    if (!::cw::g_cwpy) return nullptr;
    return object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
}

std::shared_ptr<::cw::setting::Setting> setting()
{
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
}

std::shared_ptr<::cw::battle::BattleEngine> battle()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_battle || !*::cw::g_cwpy->m_battle) return nullptr;
    return *::cw::g_cwpy->m_battle;
}

std::unordered_map<Integer, std::pair<String, Integer>> g_bookmark_item_projection;

using EtcToken = EtcUiBackend::Token;

class DefaultEtcUiBackend final : public EtcUiBackend {
public:
    void bind(Token arg_token, EtcUiCallbacks arg_callbacks) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks[arg_token] = std::move(arg_callbacks);
    }

    void unbind(Token arg_token) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.erase(arg_token);
        m_states.erase(arg_token);
    }

    void sync(Token arg_token, const EtcUiState& arg_state) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_states[arg_token] = arg_state;
    }

    void pump() override
    {
        std::deque<std::pair<Token, EtcUiEvent>> events;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            events.swap(m_events);
        }
        for (const auto& [token, event] : events) {
            EtcUiCallbacks callbacks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto it = m_callbacks.find(token);
                if (it != m_callbacks.end()) callbacks = it->second;
            }
            if (callbacks.on_event) callbacks.on_event(event);
        }
    }

    void post(Token arg_token, EtcUiEvent arg_event) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.emplace_back(arg_token, std::move(arg_event));
    }

private:
    mutable std::mutex m_mutex{};
    std::unordered_map<Token, EtcUiCallbacks> m_callbacks{};
    std::unordered_map<Token, EtcUiState> m_states{};
    std::deque<std::pair<Token, EtcUiEvent>> m_events{};
};

bool etc_action_enabled(const EtcUiState& state, const Integer id)
{
    const auto found = std::find(state.action_ids.begin(), state.action_ids.end(), id);
    if (found == state.action_ids.end()) return true;
    const auto index = static_cast<std::size_t>(std::distance(state.action_ids.begin(), found));
    return index >= state.action_enabled.size() || state.action_enabled[index];
}

void etc_presentation_button(::cw::dialog::DialogPresentation& result,
                             const std::shared_ptr<::cw::lib::Surface>& surface,
                             const String& label, const ::cw::lib::Rect rect,
                             const ::cw::dialog::DialogPresentationAction action,
                             const int index, const bool enabled)
{
    ::cw::lib::ui::draw_button_face(surface, rect, {enabled});
    const auto font = ::cw::lib::ui::configured_font("button", 14);
    const auto text_width = ::cw::lib::ui::label_width(label, font.pixels, font.bold, font.face);
    ::cw::lib::ui::draw_label(surface, label,
        rect.x + (std::max<Integer>)(3, (rect.width - text_width) / 2),
        rect.y + (std::max<Integer>)(2, (rect.height - font.pixels) / 2),
        font.pixels, font.bold, {0, 0, 0}, font.face);
    result.controls.push_back({action, index, static_cast<float>(rect.x),
        static_cast<float>(rect.y), static_cast<float>(rect.width),
        static_cast<float>(rect.height), enabled});
    result.controls.back().hover_highlight = true;
}

// ブックマークと逆変換で、最終ピクセル単位の描画処理を共有する。 モデ
// ル内の配置は論理座標のままとし、ホストで文字を再生成・再拡縮しない。
struct EtcWindowCanvas {
    double scale{(std::max)(1.0,::cw::g_UP_WIN)};
    ::cw::lib::ui::RetainedSurface& retained;
    std::shared_ptr<::cw::lib::Surface> surface;
    bool full{};
    std::map<String,bool> changed_components;
    Integer pixel(Integer value) const {return static_cast<Integer>(std::lround(value*scale));}
    ::cw::lib::Rect rect(::cw::lib::Rect r) const {
        return {pixel(r.x),pixel(r.y),pixel(r.x+r.width)-pixel(r.x),pixel(r.y+r.height)-pixel(r.y)};
    }
    EtcWindowCanvas(::cw::lib::ui::RetainedSurface& store,Integer width,Integer height,
        double output_scale=(std::max)(1.0,::cw::g_UP_WIN)):scale(output_scale),retained(store) {
        const ::cw::lib::Size size{pixel(width),pixel(height)};
        full=retained.needs_full(size);
        if(full) {
            retained.allocate(size,{240,240,240,255});
            surface=retained.surface;
            const auto bg=::cw::lib::ui::resource_surface_for_window(::cw::lib::ui::resource_dialog_background("CAUTION"));
            if(bg && bg->size.width>0 && bg->size.height>0)
                for(Integer y=0;y<surface->size.height;y+=bg->size.height)
                    for(Integer x=0;x<surface->size.width;x+=bg->size.width)
                        ::cw::lib::ui::blend_surface(surface,bg,x,y);
        } else surface=retained.surface;
    }
    bool changed(const String& key,::cw::lib::Rect logical,std::uint64_t signature) {
        const bool value=retained.component_changed(key,rect(logical),signature);
        changed_components[key]=value;
        return value;
    }
    bool changed(const String& key) const {
        const auto found=changed_components.find(key);
        return found!=changed_components.end() && found->second;
    }
    void prepare_dynamic() {
        if(full) retained.capture_base();
        retained.restore_dirty();
        surface=retained.surface;
    }
    void fill(::cw::lib::Rect r,::cw::lib::Color color) const {::cw::lib::ui::fill_rect(surface,rect(r),color);}
    void border(::cw::lib::Rect r) const {::cw::lib::ui::draw_sunken_border(surface,rect(r));}
    void label(const String& text,::cw::lib::Rect r,const ::cw::lib::ui::FontStyle& font,
               bool center=false,std::tuple<Integer,Integer,Integer> color={0,0,0}) const {
        const auto box=rect(r);
        if(box.width<=0 || box.height<=0)return;
        auto ink=std::make_shared<::cw::lib::Surface>();ink->size={box.width,box.height};
        ink->rgba.assign(static_cast<std::size_t>(box.width*box.height)*4,0);
        const auto glyph=::cw::lib::ui::render_label(text,font.pixels,font.bold,color,font.face,font.italic);
        if(glyph)::cw::lib::ui::blend_surface(ink,glyph,center?(box.width-glyph->size.width)/2:0,
                                             (box.height-glyph->size.height)/2);
        ::cw::lib::ui::blend_surface(surface,ink,box.x,box.y);
    }
    void button(::cw::dialog::DialogPresentation& result,const String& text,::cw::lib::Rect r,
                 ::cw::dialog::DialogPresentationAction action,int index,bool enabled,
                 const ::cw::lib::ui::FontStyle& font,bool paint=true) const {
        if(paint) {
            ::cw::lib::ui::draw_button_face(surface,rect(r),{enabled});label(text,r,font,true,enabled?std::tuple<Integer,Integer,Integer>{0,0,0}:
                                                   std::tuple<Integer,Integer,Integer>{128,128,128});
        }
        result.controls.push_back({action,index,static_cast<float>(r.x),static_cast<float>(r.y),
            static_cast<float>(r.width),static_cast<float>(r.height),enabled});
        result.controls.back().hover_highlight=true;
    }
    void finish(::cw::dialog::DialogPresentation& result) {
        for(auto& c:result.controls){
            c.x=static_cast<float>(std::lround(c.x*scale));c.y=static_cast<float>(std::lround(c.y*scale));
            c.width=static_cast<float>(std::lround(c.width*scale));c.height=static_cast<float>(std::lround(c.height*scale));
        }
        retained.finish();
        result.surface=surface;
        result.damage=retained.damage;
        result.surface_revision=retained.revision;
    }
};
Integer etc_logical_metric(Integer pixels) {
    return static_cast<Integer>(std::ceil(pixels/(std::max)(1.0,::cw::g_UP_WIN)));
}
Integer etc_font_width(const String& text,const ::cw::lib::ui::FontStyle& font) {
    return ::cw::lib::ui::label_width(text,font.pixels,font.bold,font.face,font.italic);
}
Integer etc_font_height(const ::cw::lib::ui::FontStyle& font) {
    const auto ink=::cw::lib::ui::render_label("Mg",font.pixels,font.bold,{0,0,0},font.face,font.italic);
    return ink?ink->size.height:font.pixels;
}
void etc_signature_mix(std::uint64_t& seed,std::uint64_t value) {
    seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
}
void etc_signature_mix(std::uint64_t& seed,const String& value) {
    etc_signature_mix(seed,static_cast<std::uint64_t>(std::hash<String>{}(value)));
}

struct ConvertYadoLayout {
    Integer width{},height{},row_height{},line_height{};
    ::cw::lib::Rect folder,browse,choice,ok,cancel;
    ::cw::lib::ui::FontStyle message_font,combo_font;
    std::vector<String> message;
};
const std::array<String,4> convert_targets{"CardWirth 1.50","CardWirth 1.30","CardWirth 1.29","CardWirth 1.28"};
ConvertYadoLayout convert_yado_layout(const ConvertYadoDialog& dialog) {
    namespace ui=::cw::lib::ui;
    ConvertYadoLayout layout;
    layout.message_font=dialog.dialog_font("dlgmsg",16);
    layout.combo_font=dialog.dialog_font("combo",16);
    const auto measure=[&](const String& text) {
        return etc_logical_metric(etc_font_width(text,layout.message_font));
    };
    layout.width=250;
    std::size_t first=0;
    do {
        const auto end=dialog.m_message.find('\n',first);
        auto line=dialog.m_message.substr(first,end==String::npos?String::npos:end-first);
        if(!line.empty() && line.back()=='\r') line.pop_back();
        layout.width=std::max<Integer>(layout.width,measure(line)+50);
        layout.message.push_back(std::move(line));
        if(end==String::npos) break;
        first=end+1;
    } while(first<=dialog.m_message.size());
    const Integer label=measure("対象エンジン:")+5;
    layout.width=std::max<Integer>(layout.width,label+190);
    const Integer field_height=std::max<Integer>(24,etc_logical_metric(etc_font_height(layout.message_font))+4);
    layout.line_height=etc_logical_metric(etc_font_height(layout.message_font));
    const Integer y=std::max<Integer>(50,20+layout.message.size()*layout.line_height);
    layout.folder={10+label,y,layout.width-label-40,field_height};
    layout.browse={layout.width-30,y,20,field_height};
    layout.row_height=std::max<Integer>(24,etc_logical_metric(etc_font_height(layout.combo_font))+4);
    layout.choice={10+label,y+field_height+5,layout.width-label-20,layout.row_height};
    const Integer button_y=layout.choice.y+layout.choice.height+10;
    layout.height=std::max<Integer>(156,button_y+40);
    const Integer margin=(layout.width-200)/3;
    layout.ok={margin,layout.height-40,100,30};
    layout.cancel={2*margin+100,layout.height-40,100,30};
    return layout;
}
::cw::lib::Rect convert_choice_popup(const ConvertYadoLayout& layout) {
    const Integer height=4*layout.row_height;
    Integer top=layout.choice.y+layout.choice.height;
    if(top+height>layout.height) top=std::max<Integer>(0,layout.choice.y-height);
    return {layout.choice.x,top,layout.choice.width,height};
}

struct InputDialogLayout {
    Integer width{}, height{}, line_height{};
    ::cw::lib::Rect input;
    ::cw::lib::ui::FontStyle message_font, input_font;
    std::vector<::cw::lib::ui::WrappedLabelRow> message;
};
InputDialogLayout input_dialog_layout(const InputDialog& dialog) {
    namespace ui=::cw::lib::ui;
    namespace input=::cw::dialog::input_presentation;
    InputDialogLayout layout;
    layout.width=input::px(std::max<Integer>(220,dialog.bounds.width));
    layout.message_font=dialog.dialog_font("system",12);
    layout.input_font=dialog.dialog_font("inputname",16);
    layout.line_height=input::cell_height(layout.message_font);
    layout.message=ui::wrap_label(dialog.m_message_text,layout.message_font,layout.width-input::px(20));
    const auto y=input::px(20)+static_cast<Integer>(layout.message.size())*layout.line_height;
    layout.input={input::px(10),y,layout.width-input::px(20),
        std::max<Integer>(input::px(24),input::cell_height(layout.input_font)+input::px(8))};
    // 配置はすべて最終ピクセル単位とする。モデル内の要求範囲は論理座標のまま保持する。
    layout.height=std::max<Integer>(input::px(std::max<Integer>(120,dialog.bounds.height)),
        layout.input.y+layout.input.height+input::px(50));
    return layout;
}

::cw::lib::Rect extension_item_rect(Integer width,Integer index) {
    return {10,10+index*26,width-260,24};
}


struct BookmarkLayout {
    ::cw::lib::Rect list;
    Integer button_width{}, button_height{}, row_height{}, width{}, height{}, rows{};
};
BookmarkLayout bookmark_layout(const BookmarkDialog& dialog) {
    const auto font = dialog.dialog_font("list",15);
    const auto button = dialog.dialog_font("button",14);
    Integer button_width = 70;
    for (const auto& label : {etc_message("delete","削除"), etc_message("decide","決定"),
                              etc_message("entry_cancel","中止")})
        button_width = (std::max)(button_width, etc_logical_metric(etc_font_width(label,button)) + 16);
    const Integer bh = std::max<Integer>(24,etc_logical_metric(etc_font_height(button))+8);
    const Integer height = std::max<Integer>(310,7*bh+40);
    const Integer rh = std::max<Integer>(18,etc_logical_metric(etc_font_height(font))+4);
    return {{5,5,250,height-10},button_width,bh,rh,265+button_width,height,(height-14)/rh};
}
constexpr int bookmark_scroll_control = 100;
::cw::lib::Rect bookmark_scroll_track(const BookmarkLayout& layout) {
    return {layout.list.x+layout.list.width-15,layout.list.y+2,13,layout.list.height-4};
}
::cw::lib::Rect bookmark_scroll_thumb(const BookmarkLayout& layout,Integer count,Integer first) {
    const auto track=bookmark_scroll_track(layout);
    const Integer height=std::max<Integer>(18,track.height*layout.rows/(std::max)(count,layout.rows));
    const Integer travel=track.height-height;
    return {track.x,track.y+travel*first/std::max<Integer>(1,count-layout.rows),track.width,height};
}
void bookmark_focus_outline(const std::shared_ptr<::cw::lib::Surface>& surface,::cw::lib::Rect rect) {
    const ::cw::lib::Color color{35,90,170,255};
    ::cw::lib::ui::fill_rect(surface,{rect.x,rect.y,rect.width,1},color);
    ::cw::lib::ui::fill_rect(surface,{rect.x,rect.y+rect.height-1,rect.width,1},color);
    ::cw::lib::ui::fill_rect(surface,{rect.x,rect.y,1,rect.height},color);
    ::cw::lib::ui::fill_rect(surface,{rect.x+rect.width-1,rect.y,1,rect.height},color);
}

::cw::dialog::DialogPresentation etc_dialog_presentation(const EtcUiState& state,
                                                          const String& route,
                                                          const Integer page,
                                                          ::cw::lib::ui::RetainedSurface& retained)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    const Integer width = (std::max<Integer>)(220, state.width);
    const Integer height = (std::max<Integer>)(120, state.height);
    EtcWindowCanvas canvas(retained,width,height);
    ::cw::dialog::DialogPresentation result;
    result.route = route;
    result.title = state.title;
    switch (state.kind) {
    case EtcUiDialogKind::extension: {
        // Python と同様、24px のボタンを縦一列に 2px 間隔で並べ、
        // キャンセルを一番下に置く。 解説欄は 210×150、パネルの余白は 10px
        // とする。長い一覧をページに分割せず、共通モデルの寸法を合わせる。
        const Integer left_width = width - 260;
        const auto button_font=::cw::lib::ui::configured_font_scaled("button",14);
        const auto detail_font=::cw::lib::ui::configured_font_scaled("datadesc",14);
        const Integer detail_line_height=static_cast<Integer>(
            std::ceil(detail_font.pixels/canvas.scale))+2;
        const auto hash_font=[](const ::cw::lib::ui::FontStyle& font) {
            std::uint64_t value=std::hash<String>{}(font.face);
            value^=static_cast<std::uint64_t>(font.pixels+1)<<32U;
            value^=static_cast<std::uint64_t>(font.bold)<<48U;
            value^=static_cast<std::uint64_t>(font.italic)<<49U;
            return value;
        };
        std::vector<bool> paint_items(state.items.size());
        for (Integer index = 0; index < static_cast<Integer>(state.items.size()); ++index) {
            const bool enabled=state.visible &&
                (index >= static_cast<Integer>(state.enabled.size()) || state.enabled[static_cast<std::size_t>(index)]);
            std::uint64_t signature=std::hash<String>{}(state.items[static_cast<std::size_t>(index)]);
            signature^=hash_font(button_font);signature^=static_cast<std::uint64_t>(enabled)<<1U;
            signature^=static_cast<std::uint64_t>(!state.selected.empty()&&state.selected.front()==index)<<2U;
            paint_items[static_cast<std::size_t>(index)]=canvas.changed(
                "extension.item."+std::to_string(index),extension_item_rect(width,index),signature);
            canvas.button(result,state.items[static_cast<std::size_t>(index)],extension_item_rect(width,index),
                Action::Auxiliary,static_cast<int>(300+index),enabled,button_font,false);
            result.controls.back().label=state.items[static_cast<std::size_t>(index)];
        }
        const ::cw::lib::Rect panel{left_width+20,10,230,height-20};
        const bool paint_detail=canvas.changed("extension.detail",panel,
            std::hash<String>{}(state.detail)^hash_font(detail_font));
        const auto cancel_label=state.action_labels.empty()?etc_message("cancel","キャンセル"):state.action_labels.back();
        const ::cw::lib::Rect cancel_rect{10,height-34,left_width,24};
        const bool cancel_focus=!state.selected.empty()&&state.selected.front()<0;
        const bool paint_cancel=canvas.changed("extension.cancel",cancel_rect,
            std::hash<String>{}(cancel_label)^hash_font(button_font)^
            (static_cast<std::uint64_t>(state.visible)<<1U)^(static_cast<std::uint64_t>(cancel_focus)<<2U));
        canvas.button(result,cancel_label,cancel_rect,Action::Cancel,2,state.visible,button_font,false);
        canvas.prepare_dynamic();
        for(Integer index=0;index<static_cast<Integer>(state.items.size());++index) {
            if(!paint_items[static_cast<std::size_t>(index)])continue;
            const bool enabled=state.visible &&
                (index>=static_cast<Integer>(state.enabled.size())||state.enabled[static_cast<std::size_t>(index)]);
            const auto rect=extension_item_rect(width,index);
            canvas.button(result,state.items[static_cast<std::size_t>(index)],rect,Action::Auxiliary,
                static_cast<int>(300+index),enabled,button_font,true);
            result.controls.pop_back();
            if(enabled&&!state.selected.empty()&&state.selected.front()==index) {
                const auto box=canvas.rect({rect.x+2,rect.y+2,rect.width-4,rect.height-4});
                const ::cw::lib::Color color{45,110,190,255};
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y,box.width,1},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y+box.height-1,box.width,1},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y,1,box.height},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x+box.width-1,box.y,1,box.height},color);
            }
        }
        if(paint_detail) {
            canvas.fill(panel,{240,240,240,255});canvas.border(panel);
            Integer y=panel.y+10;
            for(std::size_t begin=0;begin<state.detail.size()&&y<panel.y+panel.height-10;) {
                const auto end=state.detail.find('\n',begin);
                auto line=state.detail.substr(begin,end==String::npos?String::npos:end-begin);
                if(!line.empty()&&line.back()=='\r')line.pop_back();
                canvas.label(line,{panel.x+10,y,210,detail_line_height},detail_font);
                if(end==String::npos)break;
                begin=end+1;y+=detail_line_height;
            }
        }
        if(paint_cancel) {
            canvas.button(result,cancel_label,cancel_rect,Action::Cancel,2,state.visible,button_font,true);
            result.controls.pop_back();
            if(cancel_focus) {
                const auto box=canvas.rect({cancel_rect.x+2,cancel_rect.y+2,cancel_rect.width-4,cancel_rect.height-4});
                const ::cw::lib::Color color{45,110,190,255};
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y,box.width,1},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y+box.height-1,box.width,1},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x,box.y,1,box.height},color);
                ::cw::lib::ui::fill_rect(canvas.surface,{box.x+box.width-1,box.y,1,box.height},color);
            }
        }
        break;
    }
    default:
        break;
    }

    canvas.finish(result);
    return result;
}
}

std::shared_ptr<EtcUiBackend> get_etc_ui_backend()
{
    static const auto backend = std::make_shared<DefaultEtcUiBackend>();
    return backend;
}

EtcUiBackend::Token ui_token(const void* arg_object)
{
    return reinterpret_cast<EtcUiBackend::Token>(arg_object);
}

std::shared_ptr<::cw::lib::Window> ui_event_window(const EtcUiEvent& arg_event, Integer arg_id = -1)
{
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = arg_id >= 0 ? arg_id : (arg_event.index >= 0 ? arg_event.index : arg_event.id);
    event->title = arg_event.text;
    event->bounds.x = arg_event.value;
    return event;
}

BattleCommand::~BattleCommand()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

BookmarkDialog::~BookmarkDialog()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

CommentDialog::~CommentDialog()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

ConvertYadoDialog::~ConvertYadoDialog()
{
    retire_folder_request();
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

ErrorLogDialog::~ErrorLogDialog()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

ExtensionDialog::~ExtensionDialog()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

InputDialog::~InputDialog()
{
    if (m_ui_backend) m_ui_backend->unbind(ui_token(this));
}

void AutoListCtrl::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style)
{
    (void)arg_parent;
    m_cid = arg_cid;
    m_size = arg_size;
    m_style = arg_style;
    m_items.clear();
    // BookmarkDialog はこのモデルをバックエンドの ListView
    // へ反映する。 同期のたびにネイティブの配置処理が幅と行の置き換えを再計算する。
}

void BattleCommand::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if (!accepts_input()) return;
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close) {
            OnCancel(nullptr);
        } else if (arg_event.kind == EtcUiEventKind::move) {
            if (arg_event.index >= 0 &&
                arg_event.index < static_cast<Integer>(m_cards.size())) {
                for (auto& card : m_cards) card->m_negaflag = false;
                m_cards[static_cast<std::size_t>(arg_event.index)]->m_negaflag = true;
                m_selected_index = arg_event.index;
                sync_ui();
            } else {
                OnLeave(nullptr);
            }
        } else if (arg_event.kind == EtcUiEventKind::button && arg_event.id >= 200 && arg_event.id < 300) {
            OnLeftUp(ui_event_window(arg_event, arg_event.id - 200));
        } else if (arg_event.kind == EtcUiEventKind::button &&
                   (arg_event.id == -1 || arg_event.id == 1 || arg_event.id == 10 ||
                    arg_event.id == 13 || arg_event.id == 37 || arg_event.id == 39)) {
            OnKeyDown(ui_event_window(arg_event));
        }
    } });
}

void BattleCommand::sync_ui()
{
    EtcUiState state;
    state.kind = EtcUiDialogKind::battle_command;
    state.parent_handle = native_handle;
    state.position = m_pre_pos;
    state.position_valid = true;
    state.title = etc_message("select_battle_action", "戦闘の行動を選択");
    state.message = state.title;
    state.visible = visible;
    state.enabled_window = m_enabled;
    // Py は scr2win_s 適用後の実際のカード背景矩形で配置するが、 描画し
    // た wx ビットマップは標準の NormalCard 寸法のままの場合がある。
    const auto gap=::cw::lib::ui::win_i(5);
    state.width=gap;state.height=gap*2;
    for (const auto& card:m_cards) if (card) {
        state.width=std::max(state.width,card->m_ui_rect.x+card->m_ui_rect.width+gap);
        state.height=std::max(state.height,card->m_ui_rect.y+card->m_ui_rect.height+gap);
    }
    state.pressed = m__clickedflag_index;
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        const auto& card = m_cards[i];
        state.items.push_back(card ? value_string(card->m_name) : String{});
        if (!card) {
            state.item_bitmaps.push_back(nullptr);
            continue;
        }
        auto bitmap = card->m_negaflag ? card->get_wxnegabmp() : card->get_wxbmp();
        if (m__clickedflag_index == static_cast<Integer>(i) && bitmap)
            bitmap = ::cw::image::CardImage::get_wxclickedbmp_static(bitmap);
        state.item_bitmaps.push_back(std::move(bitmap));
    }
    state.selected = {m_selected_index};
    for (Integer i = 0; i < static_cast<Integer>(m_cards.size()); ++i) {
        state.action_ids.push_back(200 + i);
        state.action_enabled.push_back(m_enabled);
    }
    auto& retained=dialog_retained_surface();
    EtcWindowCanvas canvas(retained,state.width,state.height,1.0);
    for(std::size_t index=0;index<m_cards.size();++index) {
        const auto& card=m_cards[index];
        if(!card) continue;
        std::uint64_t signature{};
        etc_signature_mix(signature,state.items[index]);
        etc_signature_mix(signature,static_cast<std::uint64_t>(m__clickedflag_index==static_cast<Integer>(index)));
        etc_signature_mix(signature,static_cast<std::uint64_t>(m_enabled));
        etc_signature_mix(signature,static_cast<std::uint64_t>(card->m_negaflag));
        canvas.changed("battle.card."+std::to_string(index),card->m_ui_rect,signature);
    }
    canvas.prepare_dynamic();
    m_ui_surface=canvas.surface;
    for (std::size_t index = 0; index < m_cards.size(); ++index) {
        const auto& card = m_cards[index];
        if (!card) continue;
        const auto& rect = card->m_ui_rect;
        const auto& bitmap = state.item_bitmaps[index];
        if (bitmap && canvas.changed("battle.card."+std::to_string(index))) {
            const auto pixel_rect=canvas.rect(rect);
            ::cw::lib::ui::blend_surface(m_ui_surface, bitmap,
                pixel_rect.x + (pixel_rect.width - bitmap->size.width) / 2,
                pixel_rect.y + (pixel_rect.height - bitmap->size.height) / 2);
        }
    }
    retained.finish();
    if (m_ui_backend) m_ui_backend->sync(ui_token(this), state);
}

::cw::dialog::DialogPresentation BattleCommand::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (!m_ui_surface) sync_ui();
    ::cw::dialog::DialogPresentation result;
    result.route = "BATTLECOMMAND";
    result.title = etc_message("select_battle_action", "戦闘の行動を選択");
    // sync_ui() は最終ピクセル単位で描画し、
    // カードの文字や画像をホストで二重に拡大させない。
    result.surface = m_ui_surface;
    result.damage=dialog_retained_surface().damage;
    result.surface_revision=dialog_retained_surface().revision;
    result.refresh_interval_ms = platform_refresh_interval(m__clickedflag_index >= 0 ? 16U : 0U);
    for (std::size_t index = 0; index < m_cards.size(); ++index) {
        if (!m_cards[index]) continue;
        const auto& rect = m_cards[index]->m_ui_rect;
        result.controls.push_back({Action::Select, static_cast<int>(index),
            static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height), accepts_input()});
    }
    return result;
}

void BattleCommand::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                               const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (!accepts_input()) return;
    if (action == Action::Cancel) OnCancel(nullptr);
    else if (action == Action::Accept) dialog_presentation_enter(0);
    else if (action == Action::Select) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = index;
        OnLeftUp(event);
    } else if (action == Action::Previous || action == Action::Next) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = action == Action::Previous ? -1 : 1;
        OnKeyDown(event);
    }
    if (visible) sync_ui();
}

void BattleCommand::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,
    bool pressed, ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key = ::cw::dialog::DialogPresentationKey;
    if (!pressed || modifiers || !accepts_input()) return;
    if (key == Key::Escape) { OnCancel(nullptr); return; }
    if (key == Key::Enter) { dialog_presentation_enter(modifiers); return; }
    if (key != Key::Left && key != Key::Right) return;
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = key == Key::Left ? -1 : 1;
    OnKeyDown(event);
}

void BattleCommand::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if (modifiers || !accepts_input()) return;
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = 13;
    OnKeyDown(event);
}

void BattleCommand::dialog_presentation_hover(float x, float y)
{
    if (!accepts_input()) return;
    // 負の位置は、共通ホストからの領域外への移動通知を表す。
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x >= static_cast<double>((std::numeric_limits<Integer>::max)()) ||
        y >= static_cast<double>((std::numeric_limits<Integer>::max)())) { OnLeave(nullptr); return; }
    auto event = std::make_shared<::cw::lib::Window>();
    event->bounds.x = static_cast<Integer>(x);
    event->bounds.y = static_cast<Integer>(y);
    OnMove(event);
}

bool BattleCommand::dialog_presentation_finished() const noexcept { return !visible; }
void BattleCommand::dialog_presentation_complete() {}

void BattleCommand::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    dialog_retained_surface().reset();
    if (!battle()) throw std::logic_error("BattleCommand requires an active battle");
    m_toppanel = Value(arg_parent);
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    visible = true;
    m__clickedflag_index = -1;
    m_selected_index = -1;
    m_input_pending = false;
    m_enabled = true;
    m_cards.clear();

    auto add_card = [this](const String& resource, const String& name, ::cw::lib::Callback callback) {
        auto info = std::make_shared<::cw::image::ImageInfo>();
        const String skin = ::cw::g_cwpy ? value_string(::cw::g_cwpy->m_skindir) : String{};
        const Integer ext = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
            ? ::cw::g_cwpy->m_rsrc->m_ext_img : ::cw::g_M_IMG;
        const String requested = join_path(skin, resource);
        // Python と同様、ここでは find_resource を使う。 拡張子の
        // ないスキンリソースや PNG/WebP などの代替形式も、同じ規則で解決する。
        const String resolved = ::cw::util::find_resource(requested, ext);
        info->initialize(resolved.empty() ? requested : resolved, 0,
                         std::nullopt, "Default", std::nullopt);
        auto card = std::make_shared<BattleCommandCard>();
        card->initialize({info}, "NORMAL", name, true, std::move(callback));
        const auto index = static_cast<Integer>(m_cards.size());
        const auto gap=::cw::lib::ui::win_i(5);
        const auto card_rect=object_value<::cw::lib::Surface>(card->m_rect);
        if (!card_rect) throw std::logic_error("Battle card rectangle is unavailable");
        const auto width=index ? m_cards.front()->m_ui_rect.width :
            std::get<Integer>(::cw::scr2win_s(Value(card_rect->size.width)).storage());
        const auto height=index ? m_cards.front()->m_ui_rect.height :
            std::get<Integer>(::cw::scr2win_s(Value(card_rect->size.height)).storage());
        card->m_ui_rect = {gap+index*(width+gap),gap,width,height};
        m_cards.push_back(std::move(card));
    };
    add_card("Resource/Image/Card/BATTLE", etc_message("start_action", "戦闘"), [this](const auto&, const auto&) { start(); return Value{}; });
    const auto sdata = scenario_data();
    if (battle()->m_possible_runaway && sdata && sdata->m_party_environment_runaway) {
        add_card("Resource/Image/Card/ACTION9", etc_message("runaway", "逃走"), [this](const auto&, const auto&) { runaway(); return Value{}; });
    }
    add_card("Resource/Image/Card/COMMAND1", etc_message("cancel", "取消"), [this](const auto&, const auto&) { cancel(); return Value{}; });
    float cursor_x{},cursor_y{};
    (void)SDL_GetGlobalMouseState(&cursor_x,&cursor_y);
    m_pre_pos = {static_cast<Integer>(std::lround(cursor_x))-::cw::lib::ui::win_i(50),
                 static_cast<Integer>(std::lround(cursor_y))-::cw::lib::ui::win_i(60)};
    _do_layout();
    _bind();
    bind_ui();
    sync_ui();
}

void BattleCommand::_do_layout()
{
    // NativeEtcUiBackend が現在のカード枚数に基づいてカードボタンを配置する。
    sync_ui();
}

void BattleCommand::_bind()
{
    // bind_ui() が Win32 と SDL ホスト共通の型付きイベントブリッジを設定する。
}

void BattleCommand::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && arg_event->id < 0) OnKeyDown(arg_event);
    else if (arg_event) OnKeyDown(arg_event);
}

void BattleCommand::OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!accepts_input() || m_cards.empty()) return;
    const Integer key = event_id(arg_event);
    Integer selected = m_selected_index;
    const auto count = static_cast<Integer>(m_cards.size());
    if (key == 13 || key == 10) {
        if (m_selected_index >= 0 && m_selected_index < static_cast<Integer>(m_cards.size())) {
            play_etc_sound("click");
            animate_click(m_cards[static_cast<std::size_t>(m_selected_index)]);
        }
        return;
    }
    const bool unselected = selected < 0 || selected >= count;
    if (key == 37 || key == -1) selected = unselected ? count - 1 : (selected + count - 1) % count;
    else if (key == 39 || key == 1) selected = unselected ? 0 : (selected + 1) % count;
    else return;
    for (auto& card : m_cards) card->m_negaflag = false;
    m_cards[static_cast<std::size_t>(selected)]->m_negaflag = true;
    m_selected_index = selected;
    draw(true);
    sync_ui();
}

void BattleCommand::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!accepts_input()) return;
    const Integer index = event_id(arg_event);
    if (index >= 0 && index < static_cast<Integer>(m_cards.size())) {
        play_etc_sound("click");
        m_selected_index = index;
        for (auto& card : m_cards) card->m_negaflag = false;
        m_cards[static_cast<std::size_t>(index)]->m_negaflag = true;
        animate_click(m_cards[static_cast<std::size_t>(index)]);
        sync_ui();
    }
}

void BattleCommand::queue_close()
{
    const auto runtime=::cw::g_cwpy;
    const auto lifetime=std::weak_ptr<int>(m_close_lifetime);
    auto close=[this,lifetime](const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&)->Value {
        if (lifetime.expired()) return {};
        visible=false;
        sync_ui();
        ::cw::frame::Frame_kill_dlg_if_model(this);
        return {};
    };
    if (runtime) {
        runtime->force_exec_func([runtime,lifetime,close](const auto&,const auto&)->Value {
            if (lifetime.expired()) return {};
            const auto frame=object_value<::cw::frame::Frame>(runtime->m_frame);
            if (frame) frame->exec_func(close);
            else close({},{});
            return {};
        });
    } else close({},{});
    m_enabled=false;
    sync_ui();
}

void BattleCommand::start()
{
    const auto active=battle();
    if (active && active->is_ready()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func([active](const auto&,const auto&)->Value {
            active->start(); return {};
        });
        else active->start();
    }
    queue_close();
}

void BattleCommand::runaway()
{
    if (!::cw::dialog::message::show_common_modal_message(
            etc_message("message", "確認"),
            etc_message("confirm_runaway", "戦闘から逃走しますか？"), true).value_or(false)) return;
    const auto active=battle();
    if (active) {
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func([active](const auto&,const auto&)->Value {
            active->runaway(); return {};
        });
        else active->runaway();
    }
    queue_close();
}

void BattleCommand::cancel()
{
    queue_close();
}

void BattleCommand::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (accepts_input()) {
        play_etc_sound("click");
        cancel();
    }
}

void BattleCommand::OnMove(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || !accepts_input()) return;
    const Integer x = arg_event->bounds.x;
    const Integer y = arg_event->bounds.y;
    Integer next = -1;
    for (Integer i = 0; i < static_cast<Integer>(m_cards.size()); ++i) {
        const auto& rect = m_cards[static_cast<std::size_t>(i)]->m_ui_rect;
        if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height) { next = i; break; }
    }
    if (next != m_selected_index) {
        for (auto& card : m_cards) card->m_negaflag = false;
        if (next >= 0) m_cards[static_cast<std::size_t>(next)]->m_negaflag = true;
        m_selected_index = next;
        draw(true);
    }
}

void BattleCommand::OnEnter(std::shared_ptr<::cw::lib::Window> arg_event)
{
    OnMove(std::move(arg_event));
}

void BattleCommand::OnLeave(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (accepts_input()) {
        for (auto& card : m_cards) card->m_negaflag = false;
        m_selected_index = -1;
        draw(true);
    }
}

void BattleCommand::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    draw(false);
}

std::shared_ptr<::cw::lib::DeviceContext> BattleCommand::draw(bool arg_update)
{
    (void)arg_update;
    auto dc = std::make_shared<::cw::lib::DeviceContext>();
    for (const auto& card : m_cards) draw_card(dc, card, false);
    // 実際の画像合成はプラットフォームのコントロールが行う。 この共通メ
    // ソッドでも全カードを走査し、ホバー・クリック状態を一貫して保つ。
    sync_ui();
    return dc;
}

void BattleCommand::draw_card(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::dialog::etc::BattleCommandCard> arg_header, bool arg_fromkeyevent)
{
    (void)arg_dc;
    (void)arg_fromkeyevent;
    if (!arg_header) return;
    // Win32 はカードを実際のボタンとして描き、SDL も m_negaflag/m__clickedflag_index
    // から同じカードを選択できる。 ここでは wx の DC は不要である。
}

void BattleCommand::animate_click(std::shared_ptr<::cw::dialog::etc::BattleCommandCard> arg_header)
{
    if (!accepts_input() || !arg_header) return;
    const auto it = std::find(m_cards.begin(), m_cards.end(), arg_header);
    if (it == m_cards.end()) return;
    // wait_frame や逃走確認中は入力が処理される場合がある。 保留中の
    // クリックはダイアログの終了ではないため、m_enabled とは別に扱う。
    m_input_pending = true;
    m__clickedflag_index = static_cast<Integer>(std::distance(m_cards.begin(), it));
    sync_ui();
    const auto runtime = ::cw::g_cwpy;
    const auto frame = runtime ? object_value<::cw::frame::Frame>(runtime->m_frame) : nullptr;
    const auto self = frame
        ? std::dynamic_pointer_cast<BattleCommand>(frame->dialog_presentation_model())
        : std::shared_ptr<BattleCommand>{};
    if (!runtime) {
        // 画面なしの公開 API の呼び出しでも、Py と同様、コマンドのコールバック前に選
        // 択解除を反映する。 製品のアニメーションでは、下の Frame 経由の処理を使う。
        arg_header->m_negaflag = false;
        m__clickedflag_index = -1;
        sync_ui();
        try { if (arg_header->m_func) arg_header->m_func(
            ::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{}); }
        catch (...) { m_input_pending=false; m__clickedflag_index=-1; sync_ui(); throw; }
        m_input_pending=false; m__clickedflag_index=-1; sync_ui();
        request_dialog_presentation_refresh();
        return;
    }
    if (!self) {
        arg_header->m_negaflag = false;
        m_input_pending=false; m__clickedflag_index=-1; sync_ui();
        request_dialog_presentation_refresh();
        return;
    }
    const auto operation = [self, runtime, frame, arg_header](const auto&, const auto&) -> ::cw::lib::Value {
        const auto finish = [self, arg_header](const auto&, const auto&) -> ::cw::lib::Value {
            if (!self) return {};
            arg_header->m_negaflag = false;
            self->m_input_pending = false;
            self->m__clickedflag_index = -1;
            self->sync_ui();
            self->request_dialog_presentation_refresh();
            return {};
        };
        try {
            if (runtime) runtime->wait_frame(4, false, std::nullopt, 0);
            // Py と同様、header.func より先に押下アニメーショ
            // ンを終了し、negaflag を解除する。 UI 所有の状態は
            // UI スレッドで復元し、その完了後にゲームコマンドを呼ぶ。
            // sync_exec は待機中に RuntimeAccess を
            // 解放し、このゲーム操作から二つ目の UI ループは実行しない。
            const auto restore = [self, arg_header](const auto&, const auto&) -> ::cw::lib::Value {
                arg_header->m_negaflag = false;
                self->m__clickedflag_index = -1;
                // コマンドが終わるまで input_pending を維持する。 逃走
                // 確認中に入力を処理しても、二回目のクリックを受け付けないようにする。
                self->sync_ui();
                self->request_dialog_presentation_refresh();
                return {};
            };
            if (frame) (void)frame->sync_exec(restore);
            else restore(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
            if (arg_header->m_func) arg_header->m_func(
                ::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
            if (frame) frame->exec_func(finish);
            else finish(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
        } catch (...) {
            // このコールバックは後からゲームスレッドで実行されるため、下の予
            // 約時の catch では実行失敗時の UI 保留状態を解除で
            // きない。 元のエラーを維持したまま、後処理を UI へ戻す。
            if (frame) frame->exec_func(finish);
            else finish(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
            throw;
        }
        return {};
    };
    try {
        if (runtime) runtime->exec_func(operation);
        else operation(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
    } catch (...) {
        arg_header->m_negaflag = false;
        m_input_pending = false;
        m__clickedflag_index = -1;
        sync_ui();
        request_dialog_presentation_refresh();
        throw;
    }
}

void BookmarkDialog::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if (!visible || m__processing) return;
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close) {
            OnCancel(nullptr);
            return;
        }
        if (arg_event.kind == EtcUiEventKind::list_selected) {
            if (arg_event.index >= 0) {
                if (arg_event.value != 0) OnItemSelected(ui_event_window(arg_event));
                else {
                    m_selectedindexes.erase(std::remove(m_selectedindexes.begin(), m_selectedindexes.end(), arg_event.index), m_selectedindexes.end());
                    _item_selected();
                }
            }
            return;
        }
        if (arg_event.kind != EtcUiEventKind::button) return;
        switch (arg_event.id) {
        case 10: OnRemoveBtn(nullptr); break;
        case 11: OnUp2Btn(nullptr); break;
        case 12: OnUpBtn(nullptr); break;
        case 13: OnDownBtn(nullptr); break;
        case 14: OnDown2Btn(nullptr); break;
        case 1: OnOkBtn(nullptr); break;
        default: break;
        }
    } });
}

void BookmarkDialog::sync_ui()
{
    if (!m_ui_backend) return;
    EtcUiState state;
    state.kind = EtcUiDialogKind::bookmark;
    state.parent_handle = native_handle;
    state.title = etc_message("arrange_bookmark", "ブックマークの整理");
    state.items = m_values ? m_values->m_items : std::vector<String>{};
    state.item_icons = m_item_icons;
    state.selected = get_selectedindexes();
    state.visible = visible;
    const auto layout=bookmark_layout(*this);
    state.width = layout.width;
    state.height = layout.height;
    state.action_ids = {10, 11, 12, 13, 14, 1, 2};
    state.action_enabled = {m_remove_enabled, m_up2_enabled, m_up_enabled, m_down_enabled,
                            m_down2_enabled, true, true};
    state.action_labels = {
        etc_message("delete", "削除"),
        etc_message("up_to_top", "最上段へ"), etc_message("up", "上へ"),
        etc_message("down", "下へ"), etc_message("down_to_bottom", "最下段へ"),
        etc_message("decide", "決定"), etc_message("entry_cancel", "中止")};
    m_ui_state = state;
    if (::cw::dialog::is_hosted_presentation_host()) return;
    m_ui_backend->sync(ui_token(this), state);
}

BookmarkPresentation resolve_bookmark(
    const Bookmark& bookmark, const String& scedir,
    const std::shared_ptr<::cw::scenariodb::Scenariodb>& db, const String& skintype)
{
    const auto& [parts, saved_path] = bookmark;
    const auto filename = [](const String& path) {
        return ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
    };
    BookmarkPresentation result;
    result.path = saved_path;
    String label = filename(saved_path);
    if (!parts.empty()) {
        result.path = scedir;
        for (const auto& part : parts) {
            label = part;
            if (!part.empty() && part.front() == '/') {
                result.path = saved_path;
                label = filename(saved_path);
                break;
            }
            result.path = ::cw::lib::encoding::path_to_utf8(
                std::filesystem::u8path(result.path) / std::filesystem::u8path(part));
            if (!::cw::util::path_exists(result.path)) {
                result.path = saved_path;
                label = filename(saved_path);
                break;
            }
            result.path = ::cw::util::get_linktarget(result.path);
        }
    }
    result.path = ::cw::util::get_linktarget(result.path);
    if (::cw::scenariodb::is_scenario(result.path)) {
        if (db) {
            const auto header = db->search_path(result.path, skintype);
            if (header) result.header = *header;
        }
    } else if (!::cw::util::path_is_directory(result.path)) {
        label = !parts.empty() && !parts.back().empty() ? parts.back() : filename(saved_path);
    }
    if (result.header) {
        result.label = value_string(result.header->m_name);
        result.icon = "SUMMARY";
    } else {
        result.enabled = !label.empty();
        // Python と同様、Windows のショートカット接尾辞だけを除き、POSIX のファイル名には適用しない。
#ifdef _WIN32
        if (std::filesystem::u8path(label).has_extension()) {
            auto extension = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(label).extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".lnk") label.resize(label.size() - 4);
        }
#endif
        result.label = result.enabled ? label : "[フォルダが見つかりません]";
    }
    return result;
}

void BookmarkDialog::set_bookmarks(std::vector<Bookmark> bookmarks, const BookmarkProjector& project)
{
    m_type_search.reset();
    m_bookmark = std::move(bookmarks);
    m_item_icons.clear();
    m_values->m_items.clear();
    for (const auto& item : m_bookmark) {
        const auto row = project(item);
        // 無効または見つからないメニュー項目も、ここでは編集・削除できるようにする。
        m_values->m_items.push_back(row.label);
        m_item_icons.push_back(row.icon);
    }
    m_selectedindexes.clear();
    m_list_focus = m_selection_anchor = -1;
    m_presentation_page = 0;
    _item_selected();
}

void BookmarkDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, String arg_scedir,
    std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, const BookmarkProjector& project)
{
    dialog_retained_surface().reset();
    m_parent_window = arg_parent;
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    visible = true;
    m__processing = false;
    m_scedir = std::move(arg_scedir);
    m_selectedindexes.clear();
    m_presentation_page = 0;
    m_list_focus = m_selection_anchor = -1;
    m_keyboard_focus = 0;
    m_type_search.reset();
    m_input_modifiers = 0;
    m_scroll_drag_offset.reset();
    m_bookmark.clear();
    m_item_icons.clear();
    m_values = std::make_shared<AutoListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        // 表示への変換関数は同期的に使い、子画面には保持させない。
        const BookmarkProjector fallback = [&](const Bookmark& bookmark) {
            return resolve_bookmark(bookmark, m_scedir, arg_db, setting() ? setting()->m_skintype : String{});
        };
        set_bookmarks((*::cw::g_cwpy->m_ydata)->m_bookmarks, project ? project : fallback);
    }
    bind_ui();
    _item_selected();
}

void BookmarkDialog::_bind()
{
    // bind_ui() が ListView の通知と全コマンドボタンを接続する。
}

void BookmarkDialog::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    sync_ui();
}

void BookmarkDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!visible || m__processing) return;
    play_etc_sound("click");
    m_selectedindexes.clear();
    visible = false;
    sync_ui();
}

void BookmarkDialog::_do_layout()
{
    sync_ui();
}

void BookmarkDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!visible || m__processing) return;
    play_etc_sound("dump");
    auto indexes = get_selectedindexes();
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
        if (*it >= 0 && *it < static_cast<Integer>(m_bookmark.size())) {
            m_bookmark.erase(m_bookmark.begin() + *it);
            if (m_values && *it < static_cast<Integer>(m_values->m_items.size())) m_values->m_items.erase(m_values->m_items.begin() + *it);
            if (*it < static_cast<Integer>(m_item_icons.size())) m_item_icons.erase(m_item_icons.begin() + *it);
        }
    }
    m_selectedindexes.clear();
    m_list_focus=std::min<Integer>(m_list_focus,static_cast<Integer>(m_bookmark.size())-1);
    m_selection_anchor=m_list_focus;
    ensure_selection_visible();
    _item_selected();
}

void BookmarkDialog::OnUpBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!visible || m__processing) return;
    const auto indexes = get_selectedindexes();
    if (indexes.empty() || indexes.front() < 1) return;
    play_etc_sound("page");
    for (const auto index : indexes) if (index > 0) _swap(index, index - 1);
    ensure_selection_visible();
    _item_selected();
}

void BookmarkDialog::OnDownBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!visible || m__processing) return;
    auto indexes = get_selectedindexes();
    if (indexes.empty() || indexes.back() + 1 >= static_cast<Integer>(m_bookmark.size())) return;
    play_etc_sound("page");
    std::sort(indexes.rbegin(), indexes.rend());
    for (const auto index : indexes) if (index + 1 < static_cast<Integer>(m_bookmark.size())) _swap(index, index + 1);
    ensure_selection_visible();
    _item_selected();
}

void BookmarkDialog::OnUp2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // C++ の表示の生存期間に合わせ、閉じたダイアログには wx のイベント送信先がないものとして扱う。
    if (!visible) return;
    // Python の processing の状態と処理順を維持する。 空の入力による早期終了や、processing
    // が true のままになるエラー経路も含め、スコープ終了時に一律でリセットしない。
    m__processing = true;
    const auto indexes = get_selectedindexes();
    if (indexes.empty()) return;
    play_etc_sound("page");
    std::vector<Bookmark> selected;
    selected.reserve(indexes.size());
    for (const auto index : indexes) selected.push_back(m_bookmark.at(static_cast<std::size_t>(index)));
    std::vector<String> selected_labels;
    if (m_values) for (const auto index : indexes) if (index < static_cast<Integer>(m_values->m_items.size())) selected_labels.push_back(m_values->m_items[static_cast<std::size_t>(index)]);
    std::vector<String> selected_icons;
    for (const auto index : indexes) if (index < static_cast<Integer>(m_item_icons.size())) selected_icons.push_back(m_item_icons[static_cast<std::size_t>(index)]);
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) m_bookmark.erase(m_bookmark.begin() + *it);
    if (m_values) for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) if (*it < static_cast<Integer>(m_values->m_items.size())) m_values->m_items.erase(m_values->m_items.begin() + *it);
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) if (*it < static_cast<Integer>(m_item_icons.size())) m_item_icons.erase(m_item_icons.begin() + *it);
    m_bookmark.insert(m_bookmark.begin(), selected.begin(), selected.end());
    if (m_values) m_values->m_items.insert(m_values->m_items.begin(), selected_labels.begin(), selected_labels.end());
    m_item_icons.insert(m_item_icons.begin(), selected_icons.begin(), selected_icons.end());
    m_selectedindexes.clear();
    for (Integer i = 0; i < static_cast<Integer>(selected.size()); ++i) m_selectedindexes.push_back(i);
    m_list_focus=m_selection_anchor=m_selectedindexes.front();
    ensure_selection_visible();
    m__processing = false;
    _item_selected();
}

void BookmarkDialog::OnDown2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // C++ の表示の生存期間に合わせ、閉じたダイアログには wx のイベント送信先がないものとして扱う。
    if (!visible) return;
    // Python の processing の状態と処理順を維持する。 空の入力による早期終了や、processing
    // が true のままになるエラー経路も含め、スコープ終了時に一律でリセットしない。
    m__processing = true;
    const auto indexes = get_selectedindexes();
    if (indexes.empty()) return;
    play_etc_sound("page");
    std::vector<Bookmark> selected;
    selected.reserve(indexes.size());
    for (const auto index : indexes) selected.push_back(m_bookmark.at(static_cast<std::size_t>(index)));
    std::vector<String> selected_labels;
    if (m_values) for (const auto index : indexes) if (index < static_cast<Integer>(m_values->m_items.size())) selected_labels.push_back(m_values->m_items[static_cast<std::size_t>(index)]);
    std::vector<String> selected_icons;
    for (const auto index : indexes) if (index < static_cast<Integer>(m_item_icons.size())) selected_icons.push_back(m_item_icons[static_cast<std::size_t>(index)]);
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) m_bookmark.erase(m_bookmark.begin() + *it);
    if (m_values) for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) if (*it < static_cast<Integer>(m_values->m_items.size())) m_values->m_items.erase(m_values->m_items.begin() + *it);
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) if (*it < static_cast<Integer>(m_item_icons.size())) m_item_icons.erase(m_item_icons.begin() + *it);
    const auto base = static_cast<Integer>(m_bookmark.size());
    m_bookmark.insert(m_bookmark.end(), selected.begin(), selected.end());
    if (m_values) m_values->m_items.insert(m_values->m_items.end(), selected_labels.begin(), selected_labels.end());
    m_item_icons.insert(m_item_icons.end(), selected_icons.begin(), selected_icons.end());
    m_selectedindexes.clear();
    for (Integer i = 0; i < static_cast<Integer>(selected.size()); ++i) m_selectedindexes.push_back(base + i);
    m_list_focus=m_selection_anchor=m_selectedindexes.back();
    ensure_selection_visible();
    m__processing = false;
    _item_selected();
}

void BookmarkDialog::_swap(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2)
{
    if (arg_index1 < 0 || arg_index2 < 0 || arg_index1 >= static_cast<Integer>(m_bookmark.size()) || arg_index2 >= static_cast<Integer>(m_bookmark.size())) {
        throw std::out_of_range("bookmark swap index");
    }
    std::swap(m_bookmark[static_cast<std::size_t>(arg_index1)], m_bookmark[static_cast<std::size_t>(arg_index2)]);
    if (m_values && arg_index1 < static_cast<Integer>(m_values->m_items.size()) && arg_index2 < static_cast<Integer>(m_values->m_items.size())) {
        std::swap(m_values->m_items[static_cast<std::size_t>(arg_index1)], m_values->m_items[static_cast<std::size_t>(arg_index2)]);
    }
    if (arg_index1 < static_cast<Integer>(m_item_icons.size()) && arg_index2 < static_cast<Integer>(m_item_icons.size()))
        std::swap(m_item_icons[static_cast<std::size_t>(arg_index1)], m_item_icons[static_cast<std::size_t>(arg_index2)]);
    for (auto& index : m_selectedindexes) {
        if (index == arg_index1) index = arg_index2;
        else if (index == arg_index2) index = arg_index1;
    }
    for(auto* index : {&m_list_focus,&m_selection_anchor}) {
        if(*index==arg_index1) *index=arg_index2;
        else if(*index==arg_index2) *index=arg_index1;
    }
    std::sort(m_selectedindexes.begin(), m_selectedindexes.end());
}

void BookmarkDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!visible || m__processing) return;
    if (arg_event && arg_event->id >= 0 && arg_event->id < static_cast<Integer>(m_bookmark.size())) {
        if (std::find(m_selectedindexes.begin(), m_selectedindexes.end(), arg_event->id) == m_selectedindexes.end()) m_selectedindexes.push_back(arg_event->id);
        m_list_focus=m_selection_anchor=arg_event->id;
        ensure_selection_visible();
    }
    _item_selected();
}

std::vector<::cw::lib::Integer> BookmarkDialog::get_selectedindexes()
{
    auto indexes = m_selectedindexes;
    indexes.erase(std::remove_if(indexes.begin(), indexes.end(), [this](Integer index) { return index < 0 || index >= static_cast<Integer>(m_bookmark.size()); }), indexes.end());
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    return indexes;
}

void BookmarkDialog::_item_selected()
{
    const auto indexes = get_selectedindexes();
    m_selectedindexes=indexes;
    m_presentation_page=std::clamp<Integer>(m_presentation_page,0,
        std::max<Integer>(0,static_cast<Integer>(m_bookmark.size())-bookmark_layout(*this).rows));
    request_dialog_presentation_refresh();
    m_remove_enabled = !indexes.empty();
    m_up_enabled = !indexes.empty() && indexes.front() > 0;
    m_down_enabled = !indexes.empty() && indexes.back() + 1 < static_cast<Integer>(m_bookmark.size());
    const Integer count = static_cast<Integer>(indexes.size());
    const bool top = !indexes.empty() && indexes == [&] { std::vector<Integer> expected; for (Integer i = 0; i < count; ++i) expected.push_back(i); return expected; }();
    const bool bottom = !indexes.empty() && indexes == [&] { std::vector<Integer> expected; const Integer base = static_cast<Integer>(m_bookmark.size()) - count; for (Integer i = 0; i < count; ++i) expected.push_back(base + i); return expected; }();
    m_up2_enabled = !indexes.empty() && !top;
    m_down2_enabled = !indexes.empty() && !bottom;
    sync_ui();
}

void BookmarkDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!visible || m__processing) return;
    play_etc_sound("harvest");
    const auto bookmarks = m_bookmark;
    if (!::cw::g_cwpy) throw std::logic_error("runtime is required");
    ::cw::g_cwpy->exec_func([bookmarks](const auto&,const auto&)->Value {
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
            throw std::logic_error("inn data is required");
        (*::cw::g_cwpy->m_ydata)->set_bookmarks(bookmarks);
        return {};
    });
    visible = false;
    sync_ui();
}

void CommentDialog::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close || arg_event.kind == EtcUiEventKind::button) {
            ::cw::dialog::text::Text::OnCancel(nullptr);
            sync_ui();
        }
    } });
}

void CommentDialog::sync_ui()
{
    if (!m_ui_backend) return;
    EtcUiState state;
    state.kind = EtcUiDialogKind::comment;
    state.parent_handle = native_handle;
    state.title = etc_message("comment", "コメント");
    state.text = m_comment;
    state.visible = visible;
    const auto size=presentation_size();
    state.width = size.width;
    state.height = size.height;
    state.action_ids = {1};
    state.action_enabled = {true};
    state.action_labels = {etc_message("close", "閉じる")};
    m_ui_state = state;
    if (::cw::dialog::is_hosted_presentation_host()) return;
    m_ui_backend->sync(ui_token(this), state);
}

void ErrorLogDialog::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close || arg_event.kind == EtcUiEventKind::button) {
            ::cw::dialog::text::Text::OnCancel(nullptr);
            sync_ui();
        }
    } });
}

void ErrorLogDialog::sync_ui()
{
    if (!m_ui_backend) return;
    EtcUiState state;
    state.kind = EtcUiDialogKind::error_log;
    state.parent_handle = native_handle;
    state.title = etc_message("error_log", "エラーログ");
    state.text = m_log;
    state.visible = visible;
    const auto size=presentation_size();
    state.width = size.width;
    state.height = size.height;
    state.action_ids = {1};
    state.action_enabled = {true};
    state.action_labels = {"OK"};
    m_ui_state = state;
    if (::cw::dialog::is_hosted_presentation_host()) return;
    m_ui_backend->sync(ui_token(this), state);
}

void CommentDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_comment)
{
    m_comment=std::move(arg_comment);
    ::cw::dialog::text::Text::initialize_plain(arg_parent,etc_message("comment","コメント"),
        m_comment,::cw::dialog::text::TextPresentationKind::Comment);
    m_cwpy_debug=false;
    m_presentation_page=0;
    m_tc=Value(m_comment);
    bind_ui();
    sync_ui();
}

void CommentDialog::_do_layout()
{
    sync_ui();
}

void ConvertYadoDialog::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if(dialog_presentation_finished() || folder_request_pending()) return;
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close) {
            OnCancel(nullptr);
        } else if (arg_event.kind == EtcUiEventKind::input_changed) {
            m_folder_edit.reset(arg_event.text,false,false);
            publish_folder();
        } else if (arg_event.kind == EtcUiEventKind::choice_changed) {
            select_target(static_cast<int>(arg_event.index));
        } else if (arg_event.kind == EtcUiEventKind::button) {
            if (arg_event.id == 1) OnOk(nullptr);
            else if (arg_event.id == 2) OnCancel(nullptr);
            else if (arg_event.id == 3) request_folder();
        }
    } });
}

void ConvertYadoDialog::sync_ui()
{
    if (!m_ui_backend) return;
    EtcUiState state;
    state.kind = EtcUiDialogKind::convert_yado;
    state.parent_handle = native_handle;
    state.title = etc_message("convert_yado", "拠点の逆変換");
    state.message = m_message;
    state.text = m_dstpath;
    state.items = {"CardWirth 1.50", "CardWirth 1.30", "CardWirth 1.29", "CardWirth 1.28"};
    state.choice = m_target_index;
    state.visible = visible;
    state.modal = !m_modal_ok;
    const auto layout=convert_yado_layout(*this);
    state.width = layout.width;
    state.height = layout.height;
    state.processing=folder_request_pending();
    state.enabled_window=!dialog_presentation_finished() && !folder_request_pending();
    state.action_ids = {1, 2, 3};
    state.action_enabled = {state.enabled_window,state.enabled_window,state.enabled_window};
    state.action_labels = {
        etc_message("decide", "決定"), etc_message("entry_cancel", "中止"), "参照..."};
    m_ui_state = state;
    if (::cw::dialog::is_hosted_presentation_host()) return;
    m_ui_backend->sync(ui_token(this), state);
}

void ConvertYadoDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_yadoname)
{
    dialog_retained_surface().reset();
    retire_folder_request();m_destination_document.reset();
    m_closed=false;m_dragging=false;m_choice_open=false;m_focus=0;m_choice_cursor=0;m_modifiers=0;m_folder_menu.close();
    m_folder_error.clear();m_request_base.clear();m_yadoname=arg_yadoname;
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    visible = true;
    m_message = arg_yadoname + " を逆変換し、\n新規作成したフォルダへ格納します。";
    m_targetengine = 1.50;
    m_target_index = 0;
    m_modal_ok = false;
    m_presentation_page = 0;
    m_dstpath.clear();
    if (const auto current = setting()) m_dstpath = current->m_unconvert_targetfolder;
    m_folder = Value(m_dstpath);
    m_folder_edit.reset(m_dstpath,false,false);
    title=etc_message("convert_yado","拠点の逆変換");
    bounds = {0, 0, 420, 156};
    _do_layout();
    _bind();
    bind_ui();
    sync_ui();
}

void ConvertYadoDialog::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    play_etc_sound("signal");
    sync_folder();
    publish_folder();
    switch (m_target_index) {
    case 0: m_targetengine = 1.50; break;
    case 1: m_targetengine = 1.30; break;
    case 2: m_targetengine = 1.29; break;
    case 3: m_targetengine = 1.28; break;
    default: throw std::logic_error("invalid target engine index");
    }
    m_modal_ok = true;
    m_closed=true;m_dragging=false;
    visible = false;
    sync_ui();
    request_dialog_presentation_refresh();
}

void ConvertYadoDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m_closed=true;
    retire_folder_request();m_destination_document.reset();
    play_etc_sound("click");
    m_modal_ok = false;
    visible = false;
    sync_ui();
    request_dialog_presentation_refresh();
}

void ConvertYadoDialog::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    sync_ui();
}

void ConvertYadoDialog::_bind()
{
    // bind_ui() がフォルダー参照・選択・OK・キャンセル・終了イベントを接続する。
}

void ConvertYadoDialog::_do_layout()
{
    const auto layout=convert_yado_layout(*this);
    bounds={0,0,layout.width,layout.height};
    sync_ui();
}

void ErrorLogDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_log)
{
    m_log=std::move(arg_log);
    ::cw::dialog::text::Text::initialize_plain(arg_parent,etc_message("error_log","エラーログ"),
        m_log,::cw::dialog::text::TextPresentationKind::ErrorLog);
    m_cwpy_debug=true;
    m_presentation_page=0;
    m_tc=Value(m_log);
    bind_ui();
    sync_ui();
}

void ErrorLogDialog::_do_layout()
{
    sync_ui();
}

void ExtensionDialog::bind_ui()
{
    m_ui_backend = get_etc_ui_backend();
    m_ui_backend->bind(ui_token(this), { [this](const EtcUiEvent& arg_event) {
        if (arg_event.kind == EtcUiEventKind::cancel || arg_event.kind == EtcUiEventKind::close) {
            OnCancel(nullptr);
        } else if (arg_event.kind == EtcUiEventKind::enter) {
            OnEnter(Value(arg_event.index));
            sync_ui();
        } else if (arg_event.kind == EtcUiEventKind::leave) {
            OnLeave(Value{});
            sync_ui();
        } else if (arg_event.kind == EtcUiEventKind::button && arg_event.id >= 300) {
            OnBotton(ui_event_window(arg_event, arg_event.id - 300));
        }
    } });
}

void ExtensionDialog::sync_ui()
{
    if (!m_ui_backend) return;
    if(m_focus_index>=static_cast<Integer>(m_item_names.size()) ||
       (m_focus_index>=0 && m_focus_index<static_cast<Integer>(m_item_enabled.size()) &&
        !m_item_enabled[static_cast<std::size_t>(m_focus_index)])) m_focus_index=-1;
    EtcUiState state;
    state.kind = EtcUiDialogKind::extension;
    state.parent_handle = native_handle;
    state.title = title;
    state.items = m_item_names;
    state.descriptions = m_item_descriptions;
    state.enabled = m_item_enabled;
    state.detail = value_string(m_desc);
    state.selected={m_focus_index};
    state.visible = visible;
    state.modal = !m_modal_ok;
    const auto font=::cw::lib::ui::configured_font("button",14);
    Integer button_width=75;
    const auto measure=[&](const String& text) {
        button_width=(std::max)(button_width,::cw::lib::ui::label_width(text,font.pixels,font.bold,font.face)+20);
    };
    for(const auto& name:m_item_names) measure(name);
    measure(etc_message("cancel","キャンセル"));
    state.width=button_width+260;
    state.height=std::max<Integer>(190,static_cast<Integer>(m_item_names.size())*26+44);
    bounds.width=state.width; bounds.height=state.height;
    for (Integer i = 0; i < static_cast<Integer>(m_item_names.size()); ++i) {
        state.action_ids.push_back(300 + i);
        state.action_enabled.push_back(i >= static_cast<Integer>(m_item_enabled.size()) || m_item_enabled[static_cast<std::size_t>(i)]);
    }
    state.action_ids.push_back(2);
    state.action_enabled.push_back(true);
    state.action_labels = m_item_names;
    state.action_labels.push_back(etc_message("cancel", "キャンセル"));
    m_ui_state = state;
    if (::cw::dialog::is_hosted_presentation_host()) return;
    m_ui_backend->sync(ui_token(this), state);
}

void ExtensionDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, std::vector<::cw::lib::Value> arg_items)
{
    dialog_retained_surface().reset();
    m_parent_window = arg_parent;
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    title = std::move(arg_title);
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    m_items = Value(std::make_shared<::cw::lib::Object>());
    m_buttons = std::move(arg_items);
    m_item_names.clear();
    m_item_descriptions.clear();
    m_item_callbacks.clear();
    m_item_enabled.clear();
    m_hover_index = -1;
    m_focus_index = -1;
    m_pending_callback={};
    m_desc=Value{};
    m_presentation_page = 0;
    m_modal_ok = false;
    for (const auto& item : m_buttons) {
        String name;
        String description;
        ::cw::lib::Callback callback;
        bool enabled = true;
        if (const auto* text = std::get_if<String>(&item.storage())) {
            name = *text;
        } else if (const auto object = object_value<ExtensionItem>(item)) {
            name = object->m_name;
            description = object->m_description;
            callback = object->m_callback;
            enabled = object->m_enabled;
        } else if (const auto struct_value = object_value<::cw::data::StructVal>(item)) {
            if (!struct_value->m_member_values.empty()) {
                name = value_string(struct_value->m_member_values[0]);
                if (struct_value->m_member_values.size() > 1) description = value_string(struct_value->m_member_values[1]);
                if (struct_value->m_member_values.size() > 2) {
                    if (const auto callback_item = object_value<ExtensionItem>(struct_value->m_member_values[2]))
                        callback = callback_item->m_callback;
                }
                if (struct_value->m_member_values.size() > 3) enabled = value_bool(struct_value->m_member_values[3], true);
            }
        }
        m_item_names.push_back(std::move(name));
        m_item_descriptions.push_back(std::move(description));
        m_item_callbacks.push_back(std::move(callback));
        m_item_enabled.push_back(enabled);
    }
    for(Integer i=0;i<static_cast<Integer>(m_item_enabled.size());++i) if(m_item_enabled[static_cast<std::size_t>(i)]) {
        m_focus_index=i; break;
    }
    bounds = {0, 0, 400, 300};
    _bind();
    _do_layout();
    visible = true;
    bind_ui();
    if(m_focus_index>=0) OnEnter(Value(m_focus_index));
    sync_ui();
}

void ExtensionDialog::_bind()
{
    // bind_ui() がフォーカス・ホバー・ボタン・コンテキストメニュー・キャンセルのイベントを接続する。
}

void ExtensionDialog::_do_layout()
{
    sync_ui();
}

void ExtensionDialog::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    sync_ui();
}

void ExtensionDialog::OnEnter(::cw::lib::Value arg_event)
{
    if(!visible) return;
    Integer index = -1;
    if (const auto* value = std::get_if<Integer>(&arg_event.storage())) index = *value;
    else if (const auto event = object_value<::cw::lib::Window>(arg_event)) index = event->id;
    if(index<0 || index>=static_cast<Integer>(m_item_descriptions.size()) ||
       (index<static_cast<Integer>(m_item_enabled.size()) && !m_item_enabled[static_cast<std::size_t>(index)])) return;
    m_hover_index=index;
    m_desc = (m_hover_index >= 0 && m_hover_index < static_cast<Integer>(m_item_descriptions.size())) ? Value(m_item_descriptions[static_cast<std::size_t>(m_hover_index)]) : Value{};
    request_dialog_presentation_refresh();
}

void ExtensionDialog::OnLeave(::cw::lib::Value arg_event)
{
    (void)arg_event;
    if(!visible) return;
    m_hover_index = -1;
    m_desc = Value{};
    sync_ui();
    request_dialog_presentation_refresh();
}

void ExtensionDialog::OnBotton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if(!visible || m_modal_ok) return;
    const Integer index = event_id(arg_event);
    if (index < 0 || index >= static_cast<Integer>(m_item_enabled.size()) || !m_item_enabled[static_cast<std::size_t>(index)]) return;
    m_pending_callback={};
    if(index<static_cast<Integer>(m_item_callbacks.size())) {
        const auto callback=m_item_callbacks[static_cast<std::size_t>(index)];
        // Py と同様、有効な共通 Frame がモーダル完了前にコールバック
        // を受け取る。 C++ では Py のグローバル Frame がない
        // モデルも許可するため、既存の画面なしの操作を一回限りの完了処理とし
        // て維持する。 ボタンを受け付けたままコールバックを黙って捨てない。
        if (callback && !::cw::dialog::post_common_dialog_action(callback))
            m_pending_callback=callback;
    }
    m_modal_ok = true;
    visible = false;
    sync_ui();
    request_dialog_presentation_refresh();
}

void ExtensionDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(!visible) return;
    play_etc_sound("click");
    m_modal_ok = false;
    m_pending_callback={};
    visible = false;
    sync_ui();
    request_dialog_presentation_refresh();
}

void InputDialog::bind_ui()
{
    // InputDialog は常に DialogPresentationProvider を
    // 通して操作する。 このバックエンドは状態とイベントだけを扱い、コントロールは生成しない。
    m_ui_backend = get_etc_ui_backend();
}

void InputDialog::sync_ui()
{
    if (!m_ui_backend) return;
    EtcUiState state;
    state.kind = EtcUiDialogKind::input;
    state.parent_handle = native_handle;
    state.title = title;
    state.message = m_message_text;
    state.text = get_value();
    state.visible = visible;
    state.modal = !m__modal_ended;
    state.width = bounds.width;
    state.height = bounds.height;
    state.action_ids = {1, 2};
    state.action_enabled = {true, true};
    state.action_labels = {etc_message("decide", "決定"), etc_message("entry_cancel", "中止")};
    m_ui_state = state;
}

void InputDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_message, ::cw::lib::String arg_value, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size)
{
    dialog_retained_surface().reset();
    m_edit_menu.close();
    m_parent_window = arg_parent;
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    title = std::move(arg_title);
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    visible = true;
    m__modal_ended = false;
    m_modal_result = 0;
    m_message_text = std::move(arg_message);
    m_value = std::move(arg_value);
    m_message = Value(m_message_text);
    m_textctrl = Value(m_value);
    m_edit.reset(m_value,false,false);
    m_focus=0;
    m_dragging=false;
    m_modifiers=0;
    bounds.width = std::max<Integer>(220, std::get<0>(arg_size));
    bounds.height = std::max<Integer>(120, std::get<1>(arg_size));
    _do_layout();
    bind_ui();
    sync_ui();
}

void InputDialog::_do_layout()
{
    const auto layout=input_dialog_layout(*this);
    bounds.width=etc_logical_metric(layout.width);
    bounds.height=etc_logical_metric(layout.height);
    sync_ui();
}

void InputDialog::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(dialog_presentation_finished()) return;
    sync_input();
    if(!m_edit.composition.empty()) return;
    publish_input();
    _end_modal(1);
}

void InputDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _end_modal(0);
}

void InputDialog::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _end_modal(0);
}

void InputDialog::OnActivate(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(dialog_presentation_finished()) return;
    m_focus=0;
    request_dialog_presentation_refresh();
}

void InputDialog::_end_modal(::cw::lib::Integer arg_result)
{
    if (m__modal_ended) return;
    m_edit_menu.close();
    m__modal_ended = true;
    m_modal_result = arg_result;
    m_dragging=false;
    m_edit.set_composition("");
    visible=false;
    sync_ui();
    request_dialog_presentation_refresh();
}

::cw::lib::String InputDialog::get_value()
{
    if (const auto* text = std::get_if<String>(&m_textctrl.storage())) return *text;
    return m_value;
}

// UI の実装は共通の一つに保つ。これらの表示
// プロバイダーは、比較用に残した Win32
// 実装と同じ EtcUiState を公開
// する。 項目・操作・結果の一致を検証するま
// では、比較用のネイティブ実装を削除しない。
::cw::lib::ui::FontStyle BookmarkDialog::dialog_font(const String& role,Integer pixels) const {
    return ::cw::lib::ui::configured_wx_font(role,pixels);
}
::cw::dialog::DialogPresentation BookmarkDialog::dialog_presentation()
{
    namespace ui=::cw::lib::ui;
    using Action=::cw::dialog::DialogPresentationAction;
    sync_ui();
    const auto layout=bookmark_layout(*this);
    const auto count=static_cast<Integer>(m_bookmark.size());
    m_presentation_page=std::clamp<Integer>(m_presentation_page,0,std::max<Integer>(0,count-layout.rows));
    EtcWindowCanvas canvas(dialog_retained_surface(),layout.width,layout.height);
    const auto surface=canvas.surface;
    ::cw::dialog::DialogPresentation result;
    result.route="BOOKMARK";
    result.wants_text_input=visible && !m__processing && m_keyboard_focus==0 && count>0;
    result.text_input_hardware_only=true;
    result.title=m_ui_state.title;
    if(canvas.full) {
        canvas.fill(layout.list,{255,255,255,255});
        canvas.border(layout.list);
    }
    const bool scroll=count>layout.rows;
    const Integer row_width=layout.list.width-4-(scroll?15:0);
    const auto font=dialog_font("list",15);
    std::uint64_t list_signature{};
    etc_signature_mix(list_signature,static_cast<std::uint64_t>(count));
    etc_signature_mix(list_signature,static_cast<std::uint64_t>(m_presentation_page+1));
    etc_signature_mix(list_signature,static_cast<std::uint64_t>(m_list_focus+1));
    etc_signature_mix(list_signature,static_cast<std::uint64_t>(m_keyboard_focus+1));
    etc_signature_mix(list_signature,static_cast<std::uint64_t>(visible));
    for(const auto index:m_selectedindexes) etc_signature_mix(list_signature,static_cast<std::uint64_t>(index+1));
    if(m_values) for(const auto& item:m_values->m_items) etc_signature_mix(list_signature,item);
    for(const auto& icon:m_item_icons) etc_signature_mix(list_signature,icon);
    std::uint64_t commands_signature{};
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(m_keyboard_focus+1));
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(visible));
    for(std::size_t i=0;i<m_ui_state.action_ids.size();++i) {
        etc_signature_mix(commands_signature,static_cast<std::uint64_t>(m_ui_state.action_ids[i]+1));
        etc_signature_mix(commands_signature,static_cast<std::uint64_t>(etc_action_enabled(m_ui_state,m_ui_state.action_ids[i])));
        if(i<m_ui_state.action_labels.size()) etc_signature_mix(commands_signature,m_ui_state.action_labels[i]);
    }
    const ::cw::lib::Rect command_region{258,0,layout.width-258,layout.height};
    canvas.changed("bookmark.list",layout.list,list_signature);
    canvas.changed("bookmark.commands",command_region,commands_signature);
    canvas.prepare_dynamic();
    const auto selection=ui::system_selection_colours();
    for(Integer index=m_presentation_page;index<(std::min)(count,m_presentation_page+layout.rows);++index) {
        const ::cw::lib::Rect row{layout.list.x+2,layout.list.y+2+(index-m_presentation_page)*layout.row_height,
                                  row_width,layout.row_height};
        const bool selected=std::find(m_selectedindexes.begin(),m_selectedindexes.end(),index)!=m_selectedindexes.end();
        auto pixels=std::make_shared<::cw::lib::Surface>();
        pixels->size={canvas.pixel(row.width),canvas.pixel(row.height)};
        pixels->rgba.assign(static_cast<std::size_t>(pixels->size.width*pixels->size.height)*4,0);
        ui::fill_rect(pixels,{0,0,pixels->size.width,pixels->size.height},selected?
            selection.background : ::cw::lib::Color{255,255,255,255});
        if(index<static_cast<Integer>(m_item_icons.size())) {
            const auto icon=ui::resource_dialog_surface(m_item_icons[index]);
            if(icon) ui::blend_surface(pixels,icon,canvas.pixel(2),canvas.pixel((row.height-16)/2),canvas.pixel(16),canvas.pixel(16));
        }
        if(m_values && index<static_cast<Integer>(m_values->m_items.size())) {
            const auto glyph=ui::render_label(m_values->m_items[index],font.pixels,font.bold,
                selected?std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}:
                         std::tuple<Integer,Integer,Integer>{0,0,0},font.face,font.italic);
            if(glyph)ui::blend_surface(pixels,glyph,canvas.pixel(20),(pixels->size.height-glyph->size.height)/2);
        }
        // 合成前に各行を切り取り、長い UTF-8 の名前がス
        // クロールバーやコマンド列へはみ出さないようにする。
        if(canvas.changed("bookmark.list")) {
            ui::blend_surface(canvas.surface,pixels,canvas.pixel(row.x),canvas.pixel(row.y));
            if(m_keyboard_focus==0 && index==m_list_focus) bookmark_focus_outline(canvas.surface,canvas.rect(row));
        }
        result.controls.push_back({Action::Select,static_cast<int>(index),static_cast<float>(row.x),
            static_cast<float>(row.y),static_cast<float>(row.width),static_cast<float>(row.height),visible});
    }
    const Integer blank_y=layout.list.y+2+
        (std::min)(layout.rows,std::max<Integer>(0,count-m_presentation_page))*layout.row_height;
    if(blank_y<layout.list.y+layout.list.height-2)
        result.controls.push_back({Action::Select,-1,static_cast<float>(layout.list.x+2),static_cast<float>(blank_y),
            static_cast<float>(row_width),static_cast<float>(layout.list.y+layout.list.height-2-blank_y),visible});
    if(scroll) {
        const auto track=bookmark_scroll_track(layout);
        const auto thumb=bookmark_scroll_thumb(layout,count,m_presentation_page);
        if(canvas.changed("bookmark.list")) {
            canvas.fill(track,{235,235,235,255});
            canvas.fill(thumb,{165,165,165,255});
        }
        result.controls.push_back({Action::Auxiliary,bookmark_scroll_control,static_cast<float>(track.x),
            static_cast<float>(track.y),static_cast<float>(track.width),static_cast<float>(track.height),visible,true});
    }
    const std::array<const char*,4> icons{"UP_TO_TOP","UP","DOWN","DOWN_TO_BOTTOM"};
    for(std::size_t i=0;i<m_ui_state.action_ids.size();++i) {
        const auto id=static_cast<int>(m_ui_state.action_ids[i]);
        const Integer y=i<5?5+static_cast<Integer>(i)*(layout.button_height+5):
            layout.height-5-layout.button_height-(i==5?layout.button_height+5:0);
        const ::cw::lib::Rect rect{260,y,layout.button_width,layout.button_height};
        const bool enabled=visible && etc_action_enabled(m_ui_state,id);
        std::shared_ptr<::cw::lib::Surface> icon;
        if(i>=1 && i<=4 && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc)
            icon=ui::resource_surface_for_window(ui::resource_table_surface(::cw::g_cwpy->m_rsrc->m_buttons,icons[i-1]));
        canvas.button(result,icon?String{}:m_ui_state.action_labels[i],rect,
            id==1?Action::Accept:id==2?Action::Cancel:Action::Auxiliary,id,enabled,dialog_font("button",14),
            canvas.changed("bookmark.commands"));
        if(icon && canvas.changed("bookmark.commands")) {
            if(!enabled) {
                icon=std::make_shared<::cw::lib::Surface>(*icon);
                for(std::size_t p=3;p<icon->rgba.size();p+=4) icon->rgba[p]/=3;
            }
            ui::blend_surface(canvas.surface,icon,canvas.pixel(rect.x)+(canvas.pixel(rect.width)-icon->size.width)/2,
                              canvas.pixel(rect.y)+(canvas.pixel(rect.height)-icon->size.height)/2);
        }
        result.controls.back().label=m_ui_state.action_labels[i];
        result.controls.back().tooltip=m_ui_state.action_labels[i];
        if(canvas.changed("bookmark.commands") && m_keyboard_focus==id)
            bookmark_focus_outline(canvas.surface,canvas.rect({rect.x+2,rect.y+2,rect.width-4,rect.height-4}));
    }
    canvas.finish(result);
    return result;
}

void BookmarkDialog::ensure_selection_visible()
{
    const auto rows=bookmark_layout(*this).rows;
    if(m_list_focus>=0) {
        if(m_list_focus<m_presentation_page) m_presentation_page=m_list_focus;
        else if(m_list_focus>=m_presentation_page+rows) m_presentation_page=m_list_focus-rows+1;
    }
    m_presentation_page=std::clamp<Integer>(m_presentation_page,0,
        std::max<Integer>(0,static_cast<Integer>(m_bookmark.size())-rows));
}

void BookmarkDialog::select_bookmark(int index,::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(!visible || m__processing || index<0 || index>=static_cast<int>(m_bookmark.size())) return;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool control=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    if(shift) {
        if(m_selection_anchor<0 || m_selection_anchor>=static_cast<Integer>(m_bookmark.size()))
            m_selection_anchor=m_list_focus>=0?m_list_focus:index;
        if(!control) m_selectedindexes.clear();
        for(Integer i=std::min<Integer>(m_selection_anchor,index);i<=std::max<Integer>(m_selection_anchor,index);++i)
            m_selectedindexes.push_back(i);
    } else if(control) {
        const auto found=std::find(m_selectedindexes.begin(),m_selectedindexes.end(),index);
        if(found==m_selectedindexes.end()) m_selectedindexes.push_back(index);
        else m_selectedindexes.erase(found);
        m_selection_anchor=index;
    } else {
        m_selectedindexes={index};
        m_selection_anchor=index;
    }
    m_list_focus=index;
    m_keyboard_focus=0;
    ensure_selection_visible();
    _item_selected();
}

void BookmarkDialog::dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers)
{
    m_input_modifiers=modifiers;
}

void BookmarkDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(!visible || m__processing) return;
    if(m_type_search.composing && (action==Action::Cancel || action==Action::Accept)) {
        if(action==Action::Cancel)m_type_search.reset();
        return;
    }
    m_type_search.reset();
    if(action==Action::Cancel) OnCancel(nullptr);
    else if(action==Action::Accept) OnOkBtn(nullptr);
    else if(action==Action::Select) {
        if(index==-1) {
            if(!(m_input_modifiers & (::cw::dialog::DialogPresentationModifierControl |
                                     ::cw::dialog::DialogPresentationModifierShift))) m_selectedindexes.clear();
            m_keyboard_focus=0; _item_selected();
        } else select_bookmark(index,m_input_modifiers);
    } else if(action==Action::Previous || action==Action::Next) {
        const auto layout=bookmark_layout(*this);
        const float scale=static_cast<float>((std::max)(1.0,::cw::g_UP_WIN));
        dialog_presentation_scroll((layout.list.x+3)*scale,(layout.list.y+3)*scale,
                                   action==Action::Previous?-layout.rows:layout.rows);
    } else if(action==Action::Auxiliary && index!=bookmark_scroll_control && etc_action_enabled(m_ui_state,index)) {
        m_keyboard_focus=index;
        switch(index) {
        case 10: OnRemoveBtn(nullptr); break;
        case 11: OnUp2Btn(nullptr); break;
        case 12: OnUpBtn(nullptr); break;
        case 13: OnDownBtn(nullptr); break;
        case 14: OnDown2Btn(nullptr); break;
        default: return;
        }
    }
    if(visible) request_dialog_presentation_refresh();
}

void BookmarkDialog::dialog_presentation_text(const std::string& text,bool composition)
{
    if(!visible || m__processing || m_keyboard_focus!=0 || !m_values) return;
    // コントロールを開く・切り替えるためのキーを、前方一致検索の文字としても使わ
    // ない。 Backspace の転送で進行中の IME 変換を終了させない。
    if(!composition && (text=="\b" || (text==" " && !m_type_search.active())))return;
    m_type_search.composing=composition && !text.empty();
    if(composition) return;
    if(m_input_modifiers & (::cw::dialog::DialogPresentationModifierControl |
                            ::cw::dialog::DialogPresentationModifierAlt)) {m_type_search.reset();return;}
    const auto row=m_type_search.find(text,static_cast<int>(m_values->m_items.size()),
        static_cast<int>(m_list_focus),[&](int i){return m_values->m_items[i];});
    if(row) select_bookmark(*row,0);
}

void BookmarkDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                              ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    m_input_modifiers=modifiers;
    if(!visible || !pressed || m__processing) return;
    if(m_type_search.composing) return;
    const bool search_space=key==Key::Space && m_keyboard_focus==0 && m_type_search.active();
    if(search_space && !modifiers) return;
    if(!::cw::dialog::presentation_key_ascii(key) || modifiers &
        (::cw::dialog::DialogPresentationModifierControl | ::cw::dialog::DialogPresentationModifierAlt))
        m_type_search.reset();
    const bool control=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if(key==Key::Tab) {
        std::vector<int> order{0};
        for(const auto id:m_ui_state.action_ids) if(etc_action_enabled(m_ui_state,id)) order.push_back(static_cast<int>(id));
        const auto found=std::find(order.begin(),order.end(),m_keyboard_focus);
        auto at=found==order.end()?0:static_cast<int>(found-order.begin());
        at=(at+(shift?-1:1)+static_cast<int>(order.size()))%static_cast<int>(order.size());
        m_keyboard_focus=order[at];
    } else if(key==Key::Enter || (key==Key::Space && m_keyboard_focus!=0)) {
        if(m_keyboard_focus==2) OnCancel(nullptr);
        else if(m_keyboard_focus>=10) dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary,m_keyboard_focus);
        else OnOkBtn(nullptr);
    } else if(m_keyboard_focus==0 && key==Key::A && control) {
        m_selectedindexes.clear();
        for(Integer i=0;i<static_cast<Integer>(m_bookmark.size());++i) m_selectedindexes.push_back(i);
        if(m_list_focus<0 && !m_bookmark.empty()) m_list_focus=m_selection_anchor=0;
        _item_selected();
    } else if(m_keyboard_focus==0 && key==Key::Delete) OnRemoveBtn(nullptr);
    else if(m_keyboard_focus==0 && !m_bookmark.empty()) {
        Integer target=m_list_focus;
        const Integer last=static_cast<Integer>(m_bookmark.size())-1;
        if(key==Key::Space) select_bookmark(static_cast<int>(std::max<Integer>(0,target)),modifiers);
        else {
            switch(key) {
            case Key::Home: target=0; break;
            case Key::End: target=last; break;
            case Key::Up: target=target<0?0:target-1; break;
            case Key::Down: target=target<0?0:target+1; break;
            case Key::PageUp: target=target<0?0:target-bookmark_layout(*this).rows; break;
            case Key::PageDown: target=target<0?0:target+bookmark_layout(*this).rows; break;
            default: return;
            }
            target=std::clamp<Integer>(target,0,last);
            if(control && !shift) { m_list_focus=target; ensure_selection_visible(); }
            else select_bookmark(static_cast<int>(target),modifiers);
        }
    }
    request_dialog_presentation_refresh();
}

void BookmarkDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}

void BookmarkDialog::dialog_presentation_scroll(float x,float y,int lines)
{
    if(!visible || m__processing || !lines) return;
    const auto layout=bookmark_layout(*this);
    const auto scale=(std::max)(1.0,::cw::g_UP_WIN);
    x=static_cast<float>(x/scale); y=static_cast<float>(y/scale);
    if(x<layout.list.x || y<layout.list.y || x>=layout.list.x+layout.list.width || y>=layout.list.y+layout.list.height) return;
    m_presentation_page=std::clamp<Integer>(m_presentation_page+static_cast<Integer>(lines),0,
        std::max<Integer>(0,static_cast<Integer>(m_bookmark.size())-layout.rows));
    request_dialog_presentation_refresh();
}

void BookmarkDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
                                                float x,float y,bool pressed)
{
    if(action!=::cw::dialog::DialogPresentationAction::Auxiliary || index!=bookmark_scroll_control) {
        ::cw::dialog::DialogPresentationProvider::dialog_presentation_pointer(action,index,x,y,pressed);
        return;
    }
    if(!pressed || !visible || m__processing) {m_scroll_drag_offset.reset(); return;}
    const auto layout=bookmark_layout(*this);
    const auto count=static_cast<Integer>(m_bookmark.size());
    if(count<=layout.rows) return;
    const double py=y/(std::max)(1.0,::cw::g_UP_WIN);
    const auto thumb=bookmark_scroll_thumb(layout,count,m_presentation_page);
    const auto track=bookmark_scroll_track(layout);
    if(!m_scroll_drag_offset) {
        if(py<thumb.y || py>=thumb.y+thumb.height) {
            m_presentation_page=std::clamp<Integer>(m_presentation_page+(py<thumb.y?-layout.rows:layout.rows),0,count-layout.rows);
            const auto next=bookmark_scroll_thumb(layout,count,m_presentation_page);
            m_scroll_drag_offset=std::clamp(py-next.y,0.0,static_cast<double>(next.height));
            request_dialog_presentation_refresh();
            return;
        }
        m_scroll_drag_offset=py-thumb.y;
    }
    const double ratio=std::clamp((py-track.y-*m_scroll_drag_offset)/std::max<Integer>(1,track.height-thumb.height),0.0,1.0);
    m_presentation_page=static_cast<Integer>(ratio*(count-layout.rows)+0.5);
    request_dialog_presentation_refresh();
}

void BookmarkDialog::dialog_presentation_pointer_cancel()
{
    m_scroll_drag_offset.reset();
}

bool BookmarkDialog::dialog_presentation_finished() const noexcept { return !visible; }
void BookmarkDialog::dialog_presentation_complete() {visible=false; m_scroll_drag_offset.reset();}

::cw::dialog::DialogPresentation CommentDialog::dialog_presentation()
{
    sync_ui();
    return ::cw::dialog::text::Text::dialog_presentation();
}

void CommentDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,int index)
{
    ::cw::dialog::text::Text::dialog_presentation_action(action,index);
    sync_ui();
}

bool CommentDialog::dialog_presentation_finished() const noexcept
{
    return ::cw::dialog::text::Text::dialog_presentation_finished();
}
void CommentDialog::dialog_presentation_complete()
{
    ::cw::dialog::text::Text::OnCancel(nullptr);
    ::cw::dialog::text::Text::dialog_presentation_complete();
}

::cw::dialog::DialogPresentation ErrorLogDialog::dialog_presentation()
{
    sync_ui();
    return ::cw::dialog::text::Text::dialog_presentation();
}

void ErrorLogDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,int index)
{
    ::cw::dialog::text::Text::dialog_presentation_action(action,index);
    sync_ui();
}

bool ErrorLogDialog::dialog_presentation_finished() const noexcept
{
    return ::cw::dialog::text::Text::dialog_presentation_finished();
}
void ErrorLogDialog::dialog_presentation_complete()
{
    ::cw::dialog::text::Text::OnCancel(nullptr);
    ::cw::dialog::text::Text::dialog_presentation_complete();
}

::cw::lib::ui::FontStyle ConvertYadoDialog::dialog_font(const String& role,Integer pixels) const {
    return ::cw::lib::ui::configured_wx_font(role,pixels);
}

::cw::dialog::DialogPresentation ConvertYadoDialog::dialog_presentation()
{
    namespace ui=::cw::lib::ui;
    namespace edit=::cw::dialog::textedit;
    using Action=::cw::dialog::DialogPresentationAction;
    sync_folder();sync_ui();
    const auto layout=convert_yado_layout(*this);
    bounds={0,0,layout.width,layout.height};
    EtcWindowCanvas canvas(dialog_retained_surface(),layout.width,layout.height);
    const auto surface=canvas.surface;
    // DrawLabel は個々の行ではなく、複数行のまとまり全体を中央へ配置する。
    Integer message_width=0;
    for(const auto& line:layout.message)message_width=(std::max)(message_width,etc_font_width(line,layout.message_font));
    const Integer message_x=(surface->size.width-message_width)/2;
    if(canvas.full) {
        Integer y=canvas.pixel(10);
        for(const auto& line:layout.message){
            ui::draw_label(surface,line,message_x,y,layout.message_font.pixels,layout.message_font.bold,
                           {0,0,0},layout.message_font.face,layout.message_font.italic);
            y+=etc_font_height(layout.message_font);
        }
        canvas.label("生成先:",{10,layout.folder.y,layout.folder.x-15,layout.folder.height},layout.message_font);
        canvas.label("対象エンジン:",{10,layout.choice.y,layout.choice.x-15,layout.choice.height},layout.message_font);
    }
    const bool enabled=!dialog_presentation_finished() && !folder_request_pending();
    const bool focused=enabled && m_focus==0 && !m_choice_open;
    const bool editing=focused && !m_folder_menu.opened();
    std::uint64_t folder_signature{};
    etc_signature_mix(folder_signature,m_folder_edit.text);
    etc_signature_mix(folder_signature,m_folder_edit.composition);
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(m_folder_edit.caret));
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(m_folder_edit.anchor));
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(m_folder_edit.horizontal));
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(focused));
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(enabled));
    etc_signature_mix(folder_signature,static_cast<std::uint64_t>(m_choice_open));
    std::uint64_t commands_signature{};
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(m_target_index));
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(m_focus+1));
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(enabled));
    etc_signature_mix(commands_signature,static_cast<std::uint64_t>(m_choice_open));
    const auto popup=convert_choice_popup(layout);
    std::uint64_t popup_signature{};
    etc_signature_mix(popup_signature,static_cast<std::uint64_t>(m_choice_open));
    etc_signature_mix(popup_signature,static_cast<std::uint64_t>(m_choice_cursor+1));
    canvas.changed("convert.folder",layout.folder,folder_signature);
    canvas.changed("convert.commands",{0,layout.folder.y,
        layout.width,layout.height-layout.folder.y},commands_signature);
    canvas.changed("convert.popup",popup,popup_signature);
    canvas.prepare_dynamic();
    // コマンドの更新矩形は意図的にフォルダー行と重なる。 保持した背景
    // の復元で行が消えないよう、コマンドと一緒にその行も再描画する。
    const bool paint_commands=canvas.changed("convert.commands");
    const bool paint_folder=canvas.changed("convert.folder") || paint_commands;
    if(paint_folder) {
        canvas.fill(layout.folder,{255,255,255,255});canvas.border(layout.folder);
    }
    auto input=std::make_shared<::cw::lib::Surface>();
    input->size={canvas.pixel(layout.folder.width-8),canvas.pixel(layout.folder.height-4)};
    input->rgba.assign(static_cast<std::size_t>(input->size.width*input->size.height)*4,255);
    edit::Preedit preedit;if(editing)preedit.set(m_folder_edit.composition,m_folder_edit.composition_start,m_folder_edit.composition_length);
    edit::draw_single_line(input,edit::display_text(m_folder_edit.text,m_folder_edit.caret,m_folder_edit.anchor,preedit),
        layout.message_font,static_cast<int>((input->size.height-etc_font_height(layout.message_font))/2),
        focused,m_folder_edit.horizontal);
    if(paint_folder)
        ui::blend_surface(canvas.surface,input,canvas.pixel(layout.folder.x+4),canvas.pixel(layout.folder.y+2));
    ::cw::dialog::DialogPresentation result;
    result.route="CONVERT_YADO";result.title=title;
    result.controls.push_back({Action::Select,-1,static_cast<float>(layout.folder.x),static_cast<float>(layout.folder.y),
        static_cast<float>(layout.folder.width),static_cast<float>(layout.folder.height),enabled,true});
    const auto button_font=dialog_font("button",14);
    canvas.button(result,"...",layout.browse,Action::Auxiliary,3,enabled,button_font,paint_commands);
    if(paint_commands) { canvas.fill(layout.choice,{255,255,255,255});canvas.border(layout.choice); }
    const auto selected=std::clamp<int>(static_cast<int>(m_target_index),0,3);
    if(paint_commands) {
        canvas.label(convert_targets[selected],{layout.choice.x+4,layout.choice.y,layout.choice.width-23,layout.choice.height},layout.combo_font);
        for(Integer row=0;row<4;++row)
            canvas.fill({layout.choice.x+layout.choice.width-14+row,layout.choice.y+layout.choice.height/2-2+row,7-2*row,1},{0,0,0,255});
    }
    result.controls.push_back({Action::Auxiliary,4,static_cast<float>(layout.choice.x),static_cast<float>(layout.choice.y),
        static_cast<float>(layout.choice.width),static_cast<float>(layout.choice.height),enabled});
    canvas.button(result,etc_message("decide","決定"),layout.ok,Action::Accept,1,enabled,button_font,paint_commands);
    canvas.button(result,etc_message("entry_cancel","中止"),layout.cancel,Action::Cancel,2,enabled,button_font,paint_commands);
    if(paint_commands && enabled && m_focus){
        auto r=m_focus==1?layout.browse:m_focus==2?layout.choice:m_focus==3?layout.ok:layout.cancel;
        r.x+=2;r.y+=2;r.width-=4;r.height-=4;bookmark_focus_outline(canvas.surface,canvas.rect(r));
    }
    if(enabled && m_choice_open){
        result.controls.clear();
        result.controls.push_back({Action::Auxiliary,99,0,0,static_cast<float>(layout.width),static_cast<float>(layout.height),true});
        const auto selection=ui::system_selection_colours();
        for(int i=0;i<4;++i){
            const ::cw::lib::Rect r{popup.x,popup.y+i*layout.row_height,popup.width,layout.row_height};
            if(canvas.changed("convert.popup")) {
                canvas.fill(r,i==m_choice_cursor?selection.background: ::cw::lib::Color{255,255,255,255});
                canvas.label(convert_targets[i],{r.x+4,r.y,r.width-8,r.height},layout.combo_font,false,
                    i==m_choice_cursor?std::tuple<Integer,Integer,Integer>{selection.text.r,selection.text.g,selection.text.b}:
                        std::tuple<Integer,Integer,Integer>{0,0,0});
            }
            result.controls.push_back({Action::Select,i,static_cast<float>(r.x),static_cast<float>(r.y),
                static_cast<float>(r.width),static_cast<float>(r.height),true});
        }
        if(canvas.changed("convert.popup")) canvas.border(popup);
    }
    result.wants_text_input=editing;result.input_text=m_folder_edit.text;
    if(editing){result.composition=m_folder_edit.composition;result.composition_start=m_folder_edit.composition_start;result.composition_length=m_folder_edit.composition_length;}
    canvas.finish(result);
    if(enabled && m_folder_menu.opened()){
        result.controls.clear(); // 入力はポップアップで処理し、背後の変換操作を実行しない。
        m_folder_menu.draw(result,canvas.surface,m_folder_edit,canvas.scale);
    }
    return result;
}

void ConvertYadoDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                                    const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(dialog_presentation_finished() || folder_request_pending()) return;
    m_dragging=false;
    if(m_folder_menu.action(action,index,m_folder_edit,[this]{return read_folder_clipboard();},
        [this](const String& text){write_folder_clipboard(text);})){
        publish_folder();request_dialog_presentation_refresh();return;
    }
    if(::cw::dialog::textedit::EditMenu::is_control(action,index))return;
    if(m_choice_open) {
        if(action==Action::Accept || action==Action::Activate) select_target(m_choice_cursor);
        else if(action==Action::Select && index>=0 && index<4) select_target(index);
        else m_choice_open=false;
    } else if(action==Action::Cancel) {
        if(!m_folder_edit.composition.empty()) m_folder_edit.set_composition("");
        else OnCancel(nullptr);
    } else if(action==Action::Accept) OnOk(nullptr);
    else if(m_folder_edit.composition.empty()) {
        if(action==Action::Select && index==-1) m_focus=0;
        else if(action==Action::Select && index>=0 && index<4) select_target(index);
        else if(action==Action::Auxiliary && index==3) request_folder();
        else if(action==Action::Auxiliary && index==4) {
            m_focus=2;m_choice_open=true;m_choice_cursor=std::clamp<int>(static_cast<int>(m_target_index),0,3);
        }
    }
    request_dialog_presentation_refresh();
}

void ConvertYadoDialog::dialog_presentation_text(const std::string& text, const bool composition)
{
    if(dialog_presentation_finished() || folder_request_pending() || m_focus!=0 || m_choice_open || m_folder_menu.opened()) return;
    sync_folder();m_dragging=false;
    if(composition) m_folder_edit.set_composition(text);
    else {
        if(text=="\b" && !m_folder_edit.composition.empty()) return;
        m_folder_edit.insert(text,0,false);publish_folder();
    }
    request_dialog_presentation_refresh();
}
void ConvertYadoDialog::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    if(dialog_presentation_finished() || folder_request_pending() || m_focus!=0 || m_choice_open || m_folder_menu.opened()) return;
    ::cw::dialog::DialogPresentationProvider::dialog_presentation_text_event(event);
    if(event.composition) m_folder_edit.set_composition(event.text,event.start,event.length);
}
void ConvertYadoDialog::select_target(int index) {
    if(index<0 || index>=4) return;
    m_target_index=index;m_choice_cursor=index;m_choice_open=false;m_focus=2;
    sync_ui();request_dialog_presentation_refresh();
}
void ConvertYadoDialog::dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) {m_modifiers=modifiers;}
void ConvertYadoDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) {
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}
void ConvertYadoDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    using Key=::cw::dialog::DialogPresentationKey;
    if(!pressed || dialog_presentation_finished() || folder_request_pending()) return;
    sync_folder();m_dragging=false;
    if(!m_folder_edit.composition.empty()) return;
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool alt=(modifiers & ::cw::dialog::DialogPresentationModifierAlt)!=0;
    if(m_folder_menu.key(key,modifiers,m_folder_edit,[this]{return read_folder_clipboard();},
        [this](const String& text){write_folder_clipboard(text);})){
        publish_folder();request_dialog_presentation_refresh();return;
    }
    if(m_focus==0 && !ctrl && (key==Key::ContextMenu || (key==Key::F10 && shift))){
        open_folder_menu(0,0,true);return;
    }
    if(!ctrl && m_focus==2 && ((alt && (key==Key::Up || key==Key::Down)) || (!alt && key==Key::F4))) {
        if(m_choice_open) select_target(m_choice_cursor);
        else {m_choice_open=true;m_choice_cursor=std::clamp<int>(static_cast<int>(m_target_index),0,3);}
        request_dialog_presentation_refresh();return;
    }
    if(alt) return;
    if(m_choice_open) {
        if(ctrl) return;
        if(key==Key::Enter || key==Key::Space || key==Key::Tab) {
            select_target(m_choice_cursor);
            if(key!=Key::Tab) return;
        } else {
            if(key==Key::Down || key==Key::Right) m_choice_cursor=std::min<int>(3,m_choice_cursor+1);
            else if(key==Key::Up || key==Key::Left) m_choice_cursor=std::max<int>(0,m_choice_cursor-1);
            else if(key==Key::Home || key==Key::PageUp) m_choice_cursor=0;
            else if(key==Key::End || key==Key::PageDown) m_choice_cursor=3;
            request_dialog_presentation_refresh();return;
        }
    }
    if(!ctrl && key==Key::Tab) m_focus=(m_focus+(shift?4:1))%5;
    else if(!ctrl && (key==Key::Enter || (key==Key::Space && m_focus!=0))) {
        if(m_focus==1) request_folder();
        else if(m_focus==2 && key==Key::Space) {m_choice_open=true;m_choice_cursor=static_cast<int>(m_target_index);}
        else if(m_focus==4) OnCancel(nullptr);
        else OnOk(nullptr);
    } else if(m_focus==0) {
        ::cw::dialog::textedit::single_line_key(m_folder_edit,key,modifiers,0,
            [this]{return read_folder_clipboard();},[this](const String& value){write_folder_clipboard(value);});
        publish_folder();
    } else if(m_focus==2 && !ctrl) {
        if(key==Key::Down || key==Key::Right) select_target(std::min<int>(3,static_cast<int>(m_target_index)+1));
        else if(key==Key::Up || key==Key::Left) select_target(std::max<int>(0,static_cast<int>(m_target_index)-1));
        else if(key==Key::Home || key==Key::PageUp) select_target(0);
        else if(key==Key::End || key==Key::PageDown) select_target(3);
    }
    request_dialog_presentation_refresh();
}
void ConvertYadoDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed) {
    if(dialog_presentation_finished() || folder_request_pending()) return;
    if(m_folder_menu.opened() || m_choice_open || action!=::cw::dialog::DialogPresentationAction::Select || index!=-1) {
        if(!pressed) dialog_presentation_action(action,index);return;
    }
    if(!std::isfinite(x) || !std::isfinite(y)) return;
    sync_folder();
    if(!m_folder_edit.composition.empty() || (!pressed && !m_dragging)) return;
    const auto layout=convert_yado_layout(*this);
    const auto target=x-std::lround((layout.folder.x+4)*(std::max)(1.0,::cw::g_UP_WIN))+m_folder_edit.horizontal;
    std::size_t pos=0;Integer left=0;
    while(pos<m_folder_edit.text.size()) {
        const auto next=::cw::dialog::textedit::next(m_folder_edit.text,pos);
        const auto right=::cw::lib::ui::label_width(m_folder_edit.text.substr(0,next),layout.message_font.pixels,layout.message_font.bold,layout.message_font.face,layout.message_font.italic);
        if(target<(left+right)/2.0) break;
        pos=next;left=right;
    }
    m_folder_edit.move(pos,m_dragging || (m_modifiers & ::cw::dialog::DialogPresentationModifierShift));
    m_focus=0;m_dragging=pressed;request_dialog_presentation_refresh();
}
void ConvertYadoDialog::dialog_presentation_pointer_cancel() {m_dragging=false;}
void ConvertYadoDialog::dialog_presentation_context(float x,float y) {
    if(dialog_presentation_finished() || folder_request_pending() || !std::isfinite(x) || !std::isfinite(y)) return;
    m_dragging=false;
    if(m_choice_open) {m_choice_open=false;request_dialog_presentation_refresh();return;}
    const auto folder=convert_yado_layout(*this).folder;
    const auto scale=std::max<double>(1.0,::cw::g_UP_WIN);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(x>=folder.x && x<folder.x+folder.width && y>=folder.y && y<folder.y+folder.height){
        open_folder_menu(static_cast<float>(x*scale),static_cast<float>(y*scale),false);return;
    }
    dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,2);
}
void ConvertYadoDialog::dialog_presentation_scroll(float x,float y,int lines) {
    if(!lines || dialog_presentation_finished() || folder_request_pending() || !std::isfinite(x) || !std::isfinite(y)) return;
    if(m_folder_menu.scroll(lines,m_folder_edit)){request_dialog_presentation_refresh();return;}
    const auto layout=convert_yado_layout(*this);
    const auto rect=m_choice_open?convert_choice_popup(layout):layout.choice;
    const auto scale=std::max<double>(1.0,::cw::g_UP_WIN);x/=static_cast<float>(scale);y/=static_cast<float>(scale);
    if(x<rect.x || x>=rect.x+rect.width || y<rect.y || y>=rect.y+rect.height) return;
    if(m_choice_open) m_choice_cursor=std::clamp(m_choice_cursor+std::clamp(lines,-3,3),0,3);
    else if(m_focus==2) select_target(std::clamp(static_cast<int>(m_target_index)+std::clamp(lines,-3,3),0,3));
    request_dialog_presentation_refresh();
}

void ConvertYadoDialog::open_folder_menu(float x,float y,bool keyboard) {
    if(dialog_presentation_finished() || folder_request_pending() || m_choice_open)return;
    sync_folder();
    if(!m_folder_edit.composition.empty())return;
    const auto layout=convert_yado_layout(*this);
    const double scale=(std::max)(1.0,::cw::g_UP_WIN);
    if(keyboard){
        const auto width=etc_font_width(m_folder_edit.text.substr(0,m_folder_edit.caret),layout.message_font);
        x=static_cast<float>((layout.folder.x+4)*scale+
            std::clamp<double>(width-m_folder_edit.horizontal,0,(layout.folder.width-8)*scale));
        y=static_cast<float>((layout.folder.y+layout.folder.height)*scale);
    }
    m_focus=0;m_dragging=false;
    m_folder_menu.open(m_folder_edit,!read_folder_clipboard().empty(),x,y,keyboard);
    request_dialog_presentation_refresh();
}
void ConvertYadoDialog::dialog_presentation_hover(float x,float y) {
    if(!dialog_presentation_finished() && !folder_request_pending() && m_folder_menu.hover(x,y,m_folder_edit))
        request_dialog_presentation_refresh();
}

bool ConvertYadoDialog::dialog_presentation_finished() const noexcept { return m_closed || !visible; }
void ConvertYadoDialog::dialog_presentation_complete() {if(!m_closed) OnCancel(nullptr);}

void ConvertYadoDialog::sync_folder() {
    const auto value=value_string(m_folder);
    if(value!=m_folder_edit.text) {m_folder_menu.close();m_folder_edit.reset(value,false,false);m_destination_document.reset();}
}
void ConvertYadoDialog::publish_folder() {
    if(m_dstpath!=m_folder_edit.text) m_destination_document.reset();
    m_dstpath=m_folder_edit.text;m_folder=Value(m_dstpath);
    sync_ui();request_dialog_presentation_refresh();
}
String ConvertYadoDialog::read_folder_clipboard() const {return ::cw::dialog::presentation_clipboard_text();}
void ConvertYadoDialog::write_folder_clipboard(const String& text) {::cw::dialog::presentation_set_clipboard_text(text);}
void ConvertYadoDialog::report_folder_error(const String& error) {
    play_etc_sound("error");
    (void)::cw::dialog::message::show_common_modal_message("変換先フォルダ",error,false,true);
}

void ConvertYadoDialog::retire_folder_request() noexcept {
    // 終了・再利用・破棄で同じ処理を使い、選択結果が届く前に要求を取り消す。
    m_folder_request_token.reset();
    m_folder_ticket.reset();
}

bool ConvertYadoDialog::request_folder(FolderRequest start) {
    using namespace ::cw::lib::documents;
    if(dialog_presentation_finished() || folder_request_pending() || m_choice_open || m_folder_menu.opened() || weak_from_this().expired()) return false;
    sync_folder();
    if(!m_folder_edit.composition.empty()) return false;
    if(!start) start=::cw::dialog::request_presentation_document;
    m_focus=1;m_dragging=false;m_folder_error.clear();
    const auto token=std::make_shared<int>(0);m_folder_request_token=token;
    request_dialog_presentation_refresh();
    try {
        const auto base=std::filesystem::current_path();
        m_request_base=::cw::lib::encoding::path_to_utf8(base);
        const auto entered=std::filesystem::u8path(m_folder_edit.text);
        Request request;request.operation=Operation::OpenFolder;request.multiple=false;
        request.folder_intent=FolderIntent::Write;
        request.title=m_yadoname+" のデータをCardWirth用に逆変換します。\n変換先のフォルダを選択してください。";
        request.initial_location=::cw::lib::encoding::path_to_utf8((entered.is_absolute()?entered:base/entered).lexically_normal());
        const auto weak=weak_from_this();
        auto ticket=start(request,[weak,token](Result result) {
            if(const auto owner=weak.lock()) owner->finish_folder_request(token,std::move(result));
        });
        if(m_folder_request_token==token) {
            m_folder_ticket=std::move(ticket);
            if(!m_folder_ticket || !m_folder_ticket->pending())
                finish_folder_request(token,{Status::Failed,{},"フォルダ選択を開始できませんでした"});
        }
    } catch(const std::exception& error) {finish_folder_request(token,{Status::Failed,{},error.what()});}
      catch(...) {finish_folder_request(token,{Status::Failed,{},"フォルダ選択を開始できませんでした"});}
    return true;
}
void ConvertYadoDialog::finish_folder_request(const std::shared_ptr<int>& token,
    ::cw::lib::documents::Result result) {
    using namespace ::cw::lib::documents;
    if(m_folder_request_token!=token || dialog_presentation_finished()) return;
    // エラーの子画面を開いている間は入力を防ぐ。 入れ子のコールバック
    // 中に所有者が再初期化されても、新しい要求の保留状態を解除しない。
    struct Release {
        ConvertYadoDialog& owner;std::shared_ptr<int> token;
        ~Release(){if(owner.m_folder_request_token==token) {
            owner.m_folder_request_token.reset();owner.m_folder_ticket.reset();
            owner.sync_ui();owner.request_dialog_presentation_refresh();
        }}
    } release{*this,token};
    if(result.status==Status::Cancelled) return;
    try {
        if(result.status==Status::Success && result.documents.size()==1) {
            const auto& document=result.documents.front();
            if(document.kind!=LocationKind::NativePath)
                result.error="この変換処理ではプロバイダーURIを出力フォルダとして使用できません。ローカルフォルダを選択してください。";
            else if(document.location.empty() || document.location.find('\0')!=String::npos)
                result.error="変換先フォルダのパスが不正です。";
            else {
                const auto base=std::filesystem::u8path(m_request_base);
                if(std::filesystem::current_path()!=base) throw std::runtime_error("フォルダ選択中に作業場所が変更されました。選択し直してください。");
                const auto value=::cw::util::selected_document_path(document.location,m_request_base);
                m_folder_edit.reset(value,false,false);
                publish_folder();m_destination_document=document;
                return;
            }
        }
    } catch(const std::exception& error) {result.error=error.what();}
      catch(...) {result.error="変換先フォルダを取得できませんでした。";}
    m_folder_error=result.error.empty()?"変換先フォルダを取得できませんでした。":result.error;
    report_folder_error(m_folder_error);
}

::cw::dialog::DialogPresentation ExtensionDialog::dialog_presentation()
{
    sync_ui();
    return etc_dialog_presentation(m_ui_state,"EXTENSION",m_presentation_page,dialog_retained_surface());
}

void ExtensionDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                                  const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(!visible) return;
    if (action == Action::Cancel) OnCancel(nullptr);
    else if(action==Action::Accept || action==Action::Activate) dialog_presentation_enter(0);
    else if (action == Action::Select && index >= 0) {
        if(index<static_cast<int>(m_item_names.size()) &&
           (index>=static_cast<int>(m_item_enabled.size()) || m_item_enabled[static_cast<std::size_t>(index)])) {
            m_focus_index=index; OnEnter(Value(static_cast<Integer>(index))); sync_ui();
        }
    } else if (action == Action::Auxiliary && index >= 300) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = index - 300;
        OnBotton(std::move(event));
    }
}

void ExtensionDialog::dialog_presentation_hover(float x,float y)
{
    if(!visible) return;
    // ポインター移動のたびに再描画せず、同じ論理配置で当たり判定を行う。
    const auto scale=std::max<::cw::lib::Real>(1.0,::cw::g_UP_WIN);
    x=static_cast<float>(x/scale); y=static_cast<float>(y/scale);
    Integer hover=-1;
    for(Integer i=0;i<static_cast<Integer>(m_item_names.size());++i) {
        if(i<static_cast<Integer>(m_item_enabled.size()) && !m_item_enabled[static_cast<std::size_t>(i)]) continue;
        const auto c=extension_item_rect(m_ui_state.width,i);
        if(x>=c.x && x<c.x+c.width && y>=c.y && y<c.y+c.height) { hover=i; break; }
    }
    if(hover==m_hover_index) return;
    if(hover<0) OnLeave(Value{});
    else { OnEnter(Value(hover)); sync_ui(); }
}

void ExtensionDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    if(!visible || !pressed || (modifiers & (::cw::dialog::DialogPresentationModifierControl |
        ::cw::dialog::DialogPresentationModifierAlt))) return;
    if(key==Key::Enter || key==Key::Space) { dialog_presentation_enter(modifiers); return; }
    if(key!=Key::Tab && key!=Key::Up && key!=Key::Down && key!=Key::Home && key!=Key::End) return;
    std::vector<Integer> enabled;
    for(Integer i=0;i<static_cast<Integer>(m_item_names.size());++i)
        if(i>=static_cast<Integer>(m_item_enabled.size()) || m_item_enabled[static_cast<std::size_t>(i)]) enabled.push_back(i);
    enabled.push_back(-1);
    const auto found=std::find(enabled.begin(),enabled.end(),m_focus_index);
    const auto current=found==enabled.end()?Integer{0}:static_cast<Integer>(found-enabled.begin());
    const bool previous=key==Key::Up || (key==Key::Tab && (modifiers & ::cw::dialog::DialogPresentationModifierShift));
    const Integer count=static_cast<Integer>(enabled.size());
    const Integer next=key==Key::Home?0:key==Key::End?count-1:(current+count+(previous?-1:1))%count;
    m_focus_index=enabled[static_cast<std::size_t>(next)];
    if(m_focus_index<0) OnLeave(Value{}); else OnEnter(Value(m_focus_index));
    sync_ui(); request_dialog_presentation_refresh();
}

void ExtensionDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    if(!visible || (modifiers & (::cw::dialog::DialogPresentationModifierControl |
        ::cw::dialog::DialogPresentationModifierAlt))) return;
    sync_ui();
    if(m_focus_index<0) OnCancel(nullptr);
    else {
        auto event=std::make_shared<::cw::lib::Window>(); event->id=m_focus_index;
        OnBotton(std::move(event));
    }
}

bool ExtensionDialog::dialog_presentation_finished() const noexcept { return !visible; }
void ExtensionDialog::dialog_presentation_complete()
{
    if(visible || !m_modal_ok || !m_pending_callback) return;
    auto callback=std::move(m_pending_callback);
    m_pending_callback={};
    // Python は Frame に操作を予約してからモーダルを終了する。 こちらのモ
    // ーダルループが完了を確認してから送り、ゲームキューには送らない。 コールバックは別
    // のダイアログを開いたり例外や再入を起こしたりするため、先に要求を消費済みにする。
    if(::cw::dialog::post_common_dialog_action(callback)) return;
    callback({},{});
}

::cw::lib::ui::FontStyle InputDialog::dialog_font(const String& role,Integer pixels) const {
    // Python の StaticText は標準のシステムダイアログ用フォントを使う
    // 。 入力欄とボタンは明示的に get_wxfont とウィンドウの拡大率を使う。
    return role=="system" ? ::cw::lib::ui::system_dialog_metrics().font
                          : ::cw::lib::ui::configured_wx_font(role,pixels);
}

::cw::dialog::DialogPresentation InputDialog::dialog_presentation()
{
    namespace ui=::cw::lib::ui;
    namespace input=::cw::dialog::input_presentation;
    using Action=::cw::dialog::DialogPresentationAction;
    sync_input();sync_ui();
    const auto layout=input_dialog_layout(*this);
    const bool enabled=!dialog_presentation_finished();
    const auto& rect=layout.input;
    auto& retained=dialog_retained_surface();const bool full=retained.needs_full({layout.width,layout.height});
    if(full){retained.reset();retained.surface=input::canvas(layout.width,layout.height,false);}auto surface=retained.surface;
    const auto mix=[](std::uint64_t& value,std::uint64_t part){value^=part+0x9e3779b97f4a7c15ULL+(value<<6U)+(value>>2U);};
    const auto font_signature=[&](const auto& font){std::uint64_t value=std::hash<String>{}(font.face);mix(value,font.pixels);mix(value,font.bold);mix(value,font.italic);return value;};
    std::uint64_t message_signature=font_signature(layout.message_font);for(const auto& row:layout.message)mix(message_signature,std::hash<String>{}(row.text));mix(message_signature,layout.line_height);
    std::uint64_t field_signature=font_signature(layout.input_font);mix(field_signature,std::hash<String>{}(m_edit.text));mix(field_signature,std::hash<String>{}(m_edit.composition));mix(field_signature,m_edit.composition_start);mix(field_signature,m_edit.composition_length);mix(field_signature,m_edit.caret);mix(field_signature,m_edit.anchor);mix(field_signature,m_edit.horizontal);mix(field_signature,enabled);mix(field_signature,m_focus==0);
    const ::cw::lib::Rect message_rect{0,0,layout.width,rect.y};
    bool paint_message=retained.component_changed("input.message",message_rect,message_signature),paint_field=retained.component_changed("input.field",rect,field_signature);
    ::cw::dialog::DialogPresentation result;
    result.route="INPUT";result.title=title;
    const Integer left=(layout.width-input::px(208))/2;
    const auto button_font=dialog_font("button",14);
    struct ButtonPart{String key,label;::cw::lib::Rect rect;Action action;int index;bool focused;bool paint;};std::vector<ButtonPart> buttons;
    const auto add_button=[&](String key,String label,::cw::lib::Rect button_rect,Action action,int index,bool focused){std::uint64_t signature=std::hash<String>{}(label);mix(signature,font_signature(button_font));mix(signature,enabled);mix(signature,focused);buttons.push_back({std::move(key),std::move(label),button_rect,action,index,focused,false});buttons.back().paint=retained.component_changed(buttons.back().key,button_rect,signature);};
    add_button("input.accept",etc_message("decide","決定"),{left,layout.height-input::px(40),input::px(100),input::px(30)},Action::Accept,1,m_focus==1);
    add_button("input.cancel",etc_message("entry_cancel","中止"),{left+input::px(108),layout.height-input::px(40),input::px(100),input::px(30)},Action::Cancel,2,m_focus==2);
    const auto popup_bounds=m_edit_menu.presentation_bounds({layout.width,layout.height},std::max<::cw::lib::Real>(1.0,::cw::g_UP_WIN));bool paint_popup=false;
    if(m_edit_menu.opened())paint_popup=retained.component_changed("input.popup",popup_bounds,m_edit_menu.presentation_signature(m_edit));else (void)retained.remove_component("input.popup");
    const auto intersects=[&](const ::cw::lib::Rect area){return std::any_of(retained.dirty.begin(),retained.dirty.end(),[&](const auto& dirty){return dirty.x<area.x+area.width&&dirty.x+dirty.width>area.x&&dirty.y<area.y+area.height&&dirty.y+dirty.height>area.y;});};
    paint_message=paint_message||intersects(message_rect);paint_field=paint_field||intersects(rect);for(auto& button:buttons)button.paint=button.paint||intersects(button.rect);paint_popup=paint_popup||intersects(popup_bounds);
    if(full)retained.capture_base();retained.restore_dirty();surface=retained.surface;
    if(paint_message){Integer y=input::px(10);for(const auto& row:layout.message){ui::draw_label(surface,row.text,input::px(10),y,layout.message_font.pixels,layout.message_font.bold,{0,0,0},layout.message_font.face,layout.message_font.italic);y+=layout.line_height;}}
    if(paint_field)input::field(surface,rect,m_edit,layout.input_font,enabled&&m_focus==0);
    result.controls.push_back({Action::Select,0,static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,true});
    for(const auto& button:buttons){input::button(result,surface,button.label,button.rect,button.action,button.index,enabled,button_font,button.paint);if(button.paint&&button.focused&&enabled)bookmark_focus_outline(surface,{button.rect.x+input::px(2),button.rect.y+input::px(2),button.rect.width-input::px(4),button.rect.height-input::px(4)});}
    if(enabled && m_edit_menu.opened()) {
        result.controls.clear(); // ポインター入力はポップアップだけで受け取る。
        m_edit_menu.draw(result,surface,m_edit,std::max<::cw::lib::Real>(1.0,::cw::g_UP_WIN),std::nullopt,paint_popup);
    }
    retained.finish();result.surface=retained.surface;result.damage=retained.damage;result.surface_revision=retained.revision;
    result.wants_text_input=enabled && m_focus==0 && !m_edit_menu.opened();
    result.input_text=m_edit.text;
    if(result.wants_text_input) {
        result.composition=m_edit.composition;result.composition_start=m_edit.composition_start;
        result.composition_length=m_edit.composition_length;
    }
    return result;
}

void InputDialog::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                              const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(dialog_presentation_finished()) return;
    m_dragging=false;
    if(m_edit_menu.action(action,index,m_edit,[this]{return read_input_clipboard();},
        [this](const auto& text){write_input_clipboard(text);})) {
        publish_input();request_dialog_presentation_refresh();return;
    }
    if(index>=::cw::dialog::textedit::EditMenu::dismiss_control) return; // 古いポップアップに対する解除通知。
    if(action==Action::Select) m_focus=0;
    else if(action==Action::Accept) OnOk(nullptr);
    else if(action==Action::Cancel) {
        if(!m_edit.composition.empty()) m_edit.set_composition("");
        else OnCancel(nullptr);
    }
    request_dialog_presentation_refresh();
}

void InputDialog::dialog_presentation_text(const std::string& text, const bool composition)
{
    if(dialog_presentation_finished() || m_focus!=0 || m_edit_menu.opened()) return;
    sync_input();
    m_dragging=false;
    if(composition) m_edit.set_composition(text);
    else {
        if(text=="\b" && !m_edit.composition.empty()) return;
        m_edit.insert(text,0,false);
        publish_input();
    }
    request_dialog_presentation_refresh();
}

bool InputDialog::dialog_presentation_finished() const noexcept { return m__modal_ended || !visible; }

void InputDialog::sync_input() {
    const auto value=get_value();
    if(value!=m_edit.text) {m_edit_menu.close();m_edit.reset(value,false,false);}
}
void InputDialog::publish_input() {
    m_value=m_edit.text;
    m_textctrl=Value(m_value);
    sync_ui();
}
String InputDialog::read_input_clipboard() const {return ::cw::dialog::presentation_clipboard_text();}
void InputDialog::write_input_clipboard(const String& text) {::cw::dialog::presentation_set_clipboard_text(text);}

void InputDialog::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    if(dialog_presentation_finished() || m_focus!=0 || m_edit_menu.opened()) return;
    ::cw::dialog::DialogPresentationProvider::dialog_presentation_text_event(event);
    if(event.composition) m_edit.set_composition(event.text,event.start,event.length);
}
void InputDialog::dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) {
    m_modifiers=modifiers;
}
void InputDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) {
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}
void InputDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    using Key=::cw::dialog::DialogPresentationKey;
    if(!pressed || dialog_presentation_finished() || (modifiers & ::cw::dialog::DialogPresentationModifierAlt)) return;
    sync_input();
    m_dragging=false;
    // 候補の選択は IME が処理する。Enter で親のダイアログま
    // で確定させず、 Tab や移動操作で未確定入力を取り残さない。
    if(!m_edit.composition.empty()) return;
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if(m_edit_menu.key(key,modifiers,m_edit,[this]{return read_input_clipboard();},
        [this](const auto& text){write_input_clipboard(text);})) {
        publish_input();request_dialog_presentation_refresh();return;
    }
    if(m_focus==0 && !ctrl && (key==Key::ContextMenu || (key==Key::F10 && shift))) {
        open_edit_menu(-1,-1,true);return;
    }
    if(!ctrl && key==Key::Escape) {OnCancel(nullptr);return;}
    if(!ctrl && key==Key::Tab) m_focus=(m_focus+(shift?2:1))%3;
    else if(!ctrl && (key==Key::Enter || (key==Key::Space && m_focus!=0))) {
        if(m_focus==2) OnCancel(nullptr);else OnOk(nullptr);
    } else if(m_focus==0) {
        ::cw::dialog::textedit::single_line_key(m_edit,key,modifiers,0,
            [this]{return read_input_clipboard();},[this](const auto& text){write_input_clipboard(text);});
        publish_input();
    }
    request_dialog_presentation_refresh();
}
void InputDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed) {
    if(dialog_presentation_finished()) return;
    if(m_edit_menu.opened()) return; // 重ねて表示するコントロールは、意味上の解除操作を使う。
    if(action!=::cw::dialog::DialogPresentationAction::Select || index!=0) {
        if(!pressed) dialog_presentation_action(action,index);
        return;
    }
    if(!std::isfinite(x) || !std::isfinite(y)) return;
    sync_input();
    if(!m_edit.composition.empty() || (!pressed && !m_dragging)) return;
    const auto layout=input_dialog_layout(*this);
    const auto pos=::cw::dialog::input_presentation::caret_at(m_edit,layout.input,layout.input_font,x);
    m_focus=0;
    m_edit.move(pos,m_dragging || (m_modifiers & ::cw::dialog::DialogPresentationModifierShift));
    m_dragging=pressed;
    request_dialog_presentation_refresh();
}
void InputDialog::dialog_presentation_pointer_cancel() {m_dragging=false;}
void InputDialog::dialog_presentation_context(float x,float y) {
    dialog_presentation_pointer_cancel();
    open_edit_menu(x,y,x==-1 && y==-1);
}
void InputDialog::open_edit_menu(float x,float y,bool keyboard) {
    if(dialog_presentation_finished()) return;
    sync_input();
    if(!m_edit.composition.empty() || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto layout=input_dialog_layout(*this);
    const auto& field=layout.input;
    if(keyboard) {
        const auto anchor=::cw::dialog::input_presentation::menu_anchor(m_edit,field,layout.input_font);
        x=static_cast<float>(anchor.x);y=static_cast<float>(anchor.y);
    } else if(x<field.x || x>=field.x+field.width || y<field.y || y>=field.y+field.height) {
        m_edit_menu.close();request_dialog_presentation_refresh();return;
    }
    m_focus=0;m_dragging=false;
    m_edit_menu.open(m_edit,!read_input_clipboard().empty(),x,y,keyboard);
    request_dialog_presentation_refresh();
}
void InputDialog::dialog_presentation_hover(float x,float y) {
    if(dialog_presentation_finished()) return;
    if(m_edit_menu.hover(x,y,m_edit))
        request_dialog_presentation_refresh();
}
void InputDialog::dialog_presentation_scroll(float x,float y,int lines) {
    (void)x;(void)y;
    if(!dialog_presentation_finished() && m_edit_menu.scroll(lines,m_edit))
        request_dialog_presentation_refresh();
}

void InputDialog::dialog_presentation_complete() { _end_modal(0); }

void BattleCommandCard::initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, bool arg_can_loaded_scaledimage, ::cw::lib::Callback arg_func)
{
    ::cw::image::CardImage::initialize(std::move(arg_paths), std::move(arg_bgtype), std::move(arg_name), "", Value(arg_can_loaded_scaledimage), false, "", false, {0, 0});
    m_negaflag = false;
    m_func = std::move(arg_func);
}

void BattleCommand_start_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->start();
}

void BattleCommand_start_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->start();
}

void BattleCommand_runaway_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->runaway();
}

void BattleCommand_runaway_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->runaway();
}

void BattleCommand_cancel_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->cancel();
}

void BattleCommand_cancel_func_func(std::shared_ptr<::cw::dialog::etc::BattleCommand> arg_self)
{
    if (arg_self) arg_self->cancel();
}

void BookmarkDialog__swap_set_item(::cw::lib::Integer arg_index, ::cw::lib::String arg_string, ::cw::lib::Integer arg_image)
{
    g_bookmark_item_projection[arg_index] = {std::move(arg_string), arg_image};
}

void BookmarkDialog_OnOkBtn_func(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_bookmarks)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->set_bookmarks(std::move(arg_bookmarks));
}

void main()
{
    // Python版もエントリポイントでは何もしない。
}

}
