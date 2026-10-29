#include "cw/sprite/message.hpp"

#include "cw/animation.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/nctype.hpp"
#include "cw/eventhandler.hpp"
#include "cw/data.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/event.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/animatedimage.hpp"
#include "cw/binary/image.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/bill.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/rgba_storage_size.hpp"
#include "app/debug_control_server.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <unordered_set>

namespace cw::sprite::message {
namespace {
using I = ::cw::lib::Integer;
using S = ::cw::lib::String;
using V = ::cw::lib::Value;

void adjust_status_dialog_message_position(std::optional<std::tuple<I,I>>& pos,
    const std::optional<std::tuple<I,I>>& size, I default_height,
    std::size_t choice_count, I columns, bool backlog)
{
    if (backlog || !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
        !::cw::g_cwpy->m_setting->m_show_all_status_dialog) return;
    constexpr I row_height = 25;
    const I cols = std::max<I>(1, columns);
    const I rows = (static_cast<I>(choice_count) + cols - 1) / cols;
    auto origin = pos.value_or(std::make_tuple<I,I>(81,50));
    const I height = size ? std::get<1>(*size) : default_height;
    if (rows <= 1 || std::get<1>(origin) + height + rows * row_height <
        std::get<1>(::cw::g_SIZE_AREA)) return;
    // Python版への拡張として、ステータスダイアログ使用時は背の高いメッセ
    // ージ・選択肢を 選択肢の半行分だけ上へ移す。移動量は論理ピクセルへ丸める
    // 。 生成時だけ行い、倍率変更やバックログの再表示時には重ねて適用しない。
    std::get<1>(origin) -= (row_height + 1) / 2;
    pos = origin;
}

I message_scr(I arg_value)
{
    // 小数の丸めと桁あふれの扱いが異なる lround ではなく、 共通の Py
    // 互換 int(value * scale) 変換と範囲検査を使う。
    const auto scaled = ::cw::s(V(arg_value));
    return std::get<I>(scaled.storage());
}

std::tuple<I, I> message_scr(std::tuple<I, I> arg_value)
{
    return {message_scr(std::get<0>(arg_value)), message_scr(std::get<1>(arg_value))};
}

std::tuple<I, I, I, I> message_wincolour(bool arg_backlog)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
        return arg_backlog ? std::tuple<I, I, I, I>{80, 80, 80, 180} : std::tuple<I, I, I, I>{0, 0, 80, 180};
    return arg_backlog ? ::cw::g_cwpy->m_setting->get_blwincolour() : ::cw::g_cwpy->m_setting->get_mwincolour();
}

std::tuple<I, I, I, I> message_framecolour(bool arg_backlog)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
        return std::tuple<I, I, I, I>{128, 128, 128, 255};
    return arg_backlog ? ::cw::g_cwpy->m_setting->get_blwinframecolour()
                       : ::cw::g_cwpy->m_setting->get_mwinframecolour();
}

struct ResolverResult : ::cw::lib::Object { std::optional<S> value{}; I index{}; };
struct ResolverContext : ::cw::lib::Object {
    I full{}; S update_type{"All"}; std::map<S,V>* name_table{};
    std::optional<std::vector<std::shared_ptr<NameListItem>>>* base_list{};
    std::vector<std::shared_ptr<NameListItem>>* name_list{}; I start_index{};
    std::set<I>* special_info{}; I name_list_index{};
    std::optional<std::tuple<S,S>> scenario_info{}; I stack{};
};

std::vector<S> chars(const S& text)
{
    std::vector<S> out;
    for (std::size_t i=0;i<text.size();) { const auto c=static_cast<unsigned char>(text[i]); std::size_t n=(c&0x80u)==0?1:(c&0xe0u)==0xc0u?2:(c&0xf0u)==0xe0u?3:(c&0xf8u)==0xf0u?4:1; if(i+n>text.size())n=1;out.emplace_back(text.substr(i,n));i+=n; }
    return out;
}
I length(const S& text){return static_cast<I>(chars(text).size());}
bool is_unicode_space(const S& ch)
{
    if (ch.empty()) return false;
    const auto first = static_cast<unsigned char>(ch[0]);
    if ((first & 0x80u) == 0) return std::isspace(first) != 0;
    std::uint32_t codepoint = 0;
    if (ch.size() >= 3 && (first & 0xf0u) == 0xe0u)
        codepoint = (static_cast<std::uint32_t>(first & 0x0fu) << 12) |
                    (static_cast<std::uint32_t>(static_cast<unsigned char>(ch[1]) & 0x3fu) << 6) |
                    static_cast<std::uint32_t>(static_cast<unsigned char>(ch[2]) & 0x3fu);
    else if (ch.size() >= 2 && (first & 0xe0u) == 0xc0u)
        codepoint = (static_cast<std::uint32_t>(first & 0x1fu) << 6) |
                    static_cast<std::uint32_t>(static_cast<unsigned char>(ch[1]) & 0x3fu);
    else
        return false;
    switch (codepoint) {
    case 0x0085: case 0x00A0: case 0x1680: case 0x2028: case 0x2029:
    case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return codepoint >= 0x2000 && codepoint <= 0x200A;
    }
}
S lower(S text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;}
std::optional<I> integer(const V& v){if(const auto*p=std::get_if<I>(&v.storage()))return*p;if(const auto*p=std::get_if<::cw::lib::Real>(&v.storage()))return static_cast<I>(*p);if(const auto*p=std::get_if<S>(&v.storage()))try{std::size_t used=0;const auto result=std::stoll(*p,&used);if(used==p->size())return result;}catch(...){}return std::nullopt;}
std::optional<bool> boolean(const V& v){if(const auto*p=std::get_if<bool>(&v.storage()))return*p;if(const auto n=integer(v))return*n!=0;return std::nullopt;}

S to_string(const V& v)
{
    if(const auto*p=std::get_if<S>(&v.storage()))return*p;if(const auto*p=std::get_if<bool>(&v.storage()))return*p?"True":"False";if(const auto*p=std::get_if<I>(&v.storage()))return std::to_string(*p);if(const auto*p=std::get_if<::cw::lib::Real>(&v.storage()))return std::to_string(*p);
    if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p){if(auto o=std::dynamic_pointer_cast<NameListItem>(*p))return o->m_name_text;if(auto o=std::dynamic_pointer_cast<::cw::data::Step>(*p))return o->get_valuename(std::nullopt);if(auto o=std::dynamic_pointer_cast<::cw::data::Flag>(*p))return o->get_valuename(std::nullopt);if(auto o=std::dynamic_pointer_cast<::cw::data::Variant>(*p))return o->string_value();if(auto o=std::dynamic_pointer_cast<::cw::character::Character>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::data::Party>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::data::YadoData>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::header::CardHeader>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<_NameGetter>(*p))return to_string(o->get_name());}return{};
}
V obj(const std::shared_ptr<::cw::lib::Object>& p){return p?V(p):V{};}
std::shared_ptr<::cw::lib::Surface> surface(I w,I h,::cw::lib::Color c={0,0,0,0}){auto p=std::make_shared<::cw::lib::Surface>();p->size={std::max<I>(0,w),std::max<I>(0,h)};if(p->size.width>0&&p->size.height>0){p->rgba.resize(::cw::lib::checked_rgba_byte_count(static_cast<std::uint64_t>(p->size.width),static_cast<std::uint64_t>(p->size.height),p->rgba.max_size()));for(std::size_t i=0;i+3<p->rgba.size();i+=4){p->rgba[i]=c.r;p->rgba[i+1]=c.g;p->rgba[i+2]=c.b;p->rgba[i+3]=c.a;}}return p;}
bool valid_surface(const std::shared_ptr<::cw::lib::Surface>& p){if(!p||p->size.width<=0||p->size.height<=0)return false;try{return p->rgba.size()>=::cw::lib::checked_rgba_byte_count(static_cast<std::uint64_t>(p->size.width),static_cast<std::uint64_t>(p->size.height),p->rgba.max_size());}catch(const std::length_error&){return false;}}
void put_pixel(const std::shared_ptr<::cw::lib::Surface>& p,I x,I y,::cw::lib::Color c){if(!valid_surface(p)||x<0||y<0||x>=p->size.width||y>=p->size.height)return;const auto n=static_cast<std::size_t>((y*p->size.width+x)*4);p->rgba[n]=c.r;p->rgba[n+1]=c.g;p->rgba[n+2]=c.b;p->rgba[n+3]=c.a;}
void blend_surface(const std::shared_ptr<::cw::lib::Surface>& dst,const std::shared_ptr<::cw::lib::Surface>& src,I dx,I dy,std::optional<::cw::lib::Rect> src_rect=std::nullopt){if(!valid_surface(dst)||!valid_surface(src))return;auto r=src_rect.value_or(::cw::lib::Rect{0,0,src->size.width,src->size.height});r.x=std::clamp<I>(r.x,0,src->size.width);r.y=std::clamp<I>(r.y,0,src->size.height);r.width=std::clamp<I>(r.width,0,src->size.width-r.x);r.height=std::clamp<I>(r.height,0,src->size.height-r.y);for(I y=0;y<r.height;++y)for(I x=0;x<r.width;++x){const I tx=dx+x,ty=dy+y;if(tx<0||ty<0||tx>=dst->size.width||ty>=dst->size.height)continue;const auto si=static_cast<std::size_t>(((r.y+y)*src->size.width+r.x+x)*4),di=static_cast<std::size_t>((ty*dst->size.width+tx)*4);const unsigned a=src->rgba[si+3];for(int k=0;k<3;++k)dst->rgba[di+k]=dst->rgba[di+3]==0?src->rgba[si+k]:static_cast<std::uint8_t>((static_cast<int>(dst->rgba[di+k])*256+(static_cast<int>(src->rgba[si+k])-dst->rgba[di+k])*static_cast<int>(a)+src->rgba[si+k])/256);dst->rgba[di+3]=static_cast<std::uint8_t>(a+dst->rgba[di+3]-(a*dst->rgba[di+3])/255);}}
std::shared_ptr<::cw::lib::Surface> copy_rect(const std::shared_ptr<::cw::lib::Surface>& src, ::cw::lib::Rect r)
{
    auto out = surface(std::max<I>(0, r.width), std::max<I>(0, r.height));
    if (!valid_surface(src) || !valid_surface(out)) return out;
    // subsurface は入力の RGBA をそのままコピーし、アルファ合成は行わない。
    for (I y = 0; y < out->size.height; ++y) {
        const I sy = r.y + y;
        if (sy < 0 || sy >= src->size.height) continue;
        for (I x = 0; x < out->size.width; ++x) {
            const I sx = r.x + x;
            if (sx < 0 || sx >= src->size.width) continue;
            const auto si = static_cast<std::size_t>((sy * src->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((y * out->size.width + x) * 4);
            std::copy_n(src->rgba.data() + si, 4, out->rgba.data() + di);
        }
    }
    return out;
}
void draw_line(const std::shared_ptr<::cw::lib::Surface>& p,I x0,I y0,I x1,I y1,::cw::lib::Color c){I dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;for(;;){put_pixel(p,x0,y0,c);if(x0==x1&&y0==y1)break;const I e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}}
std::shared_ptr<::cw::lib::Surface> fallback_text(const S& text,I pixels,std::tuple<I,I,I> colour){const I h=std::max<I>(8,std::abs(pixels)),w=std::max<I>(1,length(text))*std::max<I>(4,h/2);auto out=surface(w,h);const ::cw::lib::Color c{static_cast<std::uint8_t>(std::get<0>(colour)),static_cast<std::uint8_t>(std::get<1>(colour)),static_cast<std::uint8_t>(std::get<2>(colour)),255};I ox=0;for(const auto& ch:chars(text)){if(ch!=" "){for(I x=1;x<std::max<I>(2,h/2)-1;++x){put_pixel(out,ox+x,1,c);put_pixel(out,ox+x,h-2,c);}for(I y=1;y<h-1;++y){put_pixel(out,ox+1,y,c);put_pixel(out,ox+std::max<I>(2,h/2)-2,y,c);}}ox+=std::max<I>(4,h/2);}return out;}

class CommonMessageUiBackend final : public MessageUiBackend {
public:
 std::shared_ptr<::cw::lib::Surface> render_text(const S& text,const S& face,I pixels,bool bold,bool italic,bool antialias,std::tuple<I,I,I> colour) override {try{auto f=std::make_shared<::cw::imageretouch::Font>();f->initialize(face.empty()?S("MS UI Gothic"):face,pixels,bold,italic);return bold?f->render_sbold(text,antialias,colour):f->render(text,antialias,colour);}catch(...){return fallback_text(text,pixels,colour);}}
 void invalidate(const ::cw::lib::Rect& rect) override {
  if(rect.width<=0||rect.height<=0||!::cw::g_cwpy)return;
  // ThreadBackend::invalidate を共通の表示境界とする。SDL
  // はソフトウェアの更新矩形を予約し、Windows は加えて InvalidateRect
  // を呼ぶ。 構築途中・画面なしの CWPy でも add_lazydraw が
  // 同じ領域を受け取り、文字送り・選択・話者のアニメーションを失わないようにする。
  if(::cw::g_cwpy->m_backend)::cw::g_cwpy->m_backend->invalidate(*::cw::g_cwpy,rect);
  else ::cw::g_cwpy->add_lazydraw_rect(rect);
 }
 void play_sound(const S& name) override {if(::cw::g_cwpy)::cw::g_cwpy->play_sound(name,true,100,1,0,0,false);}
};
std::tuple<std::shared_ptr<::cw::lib::Surface>,bool> stored_log_image{};
std::shared_ptr<::cw::lib::Surface> get_surface(const V& v){if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p){if(auto o=std::dynamic_pointer_cast<SurfaceValue>(*p))return o->value;if(auto seq=std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p);seq&&!seq->m_values.empty())return get_surface(seq->m_values.front());return std::dynamic_pointer_cast<::cw::lib::Surface>(*p);}return{};}
std::pair<std::shared_ptr<::cw::lib::Surface>,bool> specialchar_resource(const V& v)
{
    if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p){
        if(auto seq=std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p)){
            std::shared_ptr<::cw::lib::Surface> image;
            bool userfont=false;
            if(!seq->m_values.empty()) image=get_surface(seq->m_values.front());
            if(seq->m_values.size()>=2){if(const auto*b=std::get_if<bool>(&seq->m_values[1].storage())) userfont=*b;}
            return {image,userfont};
        }
    }
    return {get_surface(v),false};
}
I surface_logical_scale(const std::shared_ptr<::cw::lib::Surface>& image)
{
    if(!image) return 1;
    const auto meta=::cw::imageretouch::get_surface_metadata(image);
    const auto scale=meta.logical_scale>0.0?meta.logical_scale:1.0;
    return std::max<I>(1,static_cast<I>(std::lround(scale)));
}
void mark_scaled_surface(const std::shared_ptr<::cw::lib::Surface>& image,I scale)
{
    if(!image) return;
    auto meta=::cw::imageretouch::get_surface_metadata(image);
    meta.depth1=scale>1;
    meta.logical_scale=static_cast<::cw::lib::Real>(std::max<I>(1,scale));
    ::cw::imageretouch::set_surface_metadata(image,meta);
}
I encode_layer(I layer,I type,I index,I type2)
{
    try { return ::cw::layer_val({layer,type,index,type2}); } catch(...) { return layer; }
}
I message_window_layer(bool special)
{
    if(special) return encode_layer(std::get<0>(::cw::g_LAYER_SPMESSAGE),integer(std::get<1>(::cw::g_LAYER_SPMESSAGE)).value_or(1),std::get<2>(::cw::g_LAYER_SPMESSAGE),std::get<3>(::cw::g_LAYER_SPMESSAGE));
    return encode_layer(std::get<0>(::cw::g_LAYER_MESSAGE),integer(std::get<1>(::cw::g_LAYER_MESSAGE)).value_or(1),std::get<2>(::cw::g_LAYER_MESSAGE),std::get<3>(::cw::g_LAYER_MESSAGE));
}
I selectionbar_layer(bool special,bool pressed)
{
    if(special){
        const auto& t=pressed?(::cw::g_LAYER_SPSELECTIONBAR_2):(::cw::g_LAYER_SPSELECTIONBAR_1);
        return encode_layer(std::get<0>(t),integer(std::get<1>(t)).value_or(1),std::get<2>(t),std::get<3>(t));
    }
    const auto& t=pressed?(::cw::g_LAYER_SELECTIONBAR_2):(::cw::g_LAYER_SELECTIONBAR_1);
    return encode_layer(std::get<0>(t),integer(std::get<1>(t)).value_or(1),std::get<2>(t),std::get<3>(t));
}
bool special_area()
{
    if(!::cw::g_cwpy) return false;
    return (::cw::g_cwpy->m_background&&::cw::g_cwpy->m_background->m_curtain_all)||
           ::cw::g_cwpy->m_areaid==-1||::cw::g_cwpy->m_areaid==-2||::cw::g_cwpy->m_areaid==-3||
           ::cw::g_cwpy->m_areaid==-4||::cw::g_cwpy->m_areaid==-5;
}
void add_sprite_to_group(const V& group,const std::shared_ptr<::cw::lib::Object>& sprite,I layer)
{
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&group.storage());
    if(!object||!*object||!sprite) return;
    ::cw::lib::Arguments sprites;
    sprites.emplace_back(V(sprite));
    ::cw::add_layer(*object,sprites,layer);
}
V versionhint_value(const std::optional<std::tuple<S,S,bool,bool,bool>>& hint)
{
    if(!hint) return {};
    auto seq=std::make_shared<::cw::data::VariantSequence>();
    seq->m_values={V(std::get<0>(*hint)),V(std::get<1>(*hint)),V(std::get<2>(*hint)),V(std::get<3>(*hint)),V(std::get<4>(*hint))};
    return obj(seq);
}
std::tuple<I,I,I,I> message_tlayer(const std::tuple<I,V,I,I>& layer)
{
    return {std::get<0>(layer),integer(std::get<1>(layer)).value_or(1),std::get<2>(layer),std::get<3>(layer)};
}
void register_layered_sprite(::cw::sprite::base::CWPySprite& sprite,const V& group)
{
    if(!sprite.m_ui_backend) sprite.m_ui_backend=::cw::sprite::base::sprite_ui_backend();
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&group.storage());
    if(!object||!*object||!sprite.m_ui_backend) return;
    sprite.m_ui_backend->register_sprite(sprite,{*object});
}
void register_message_window(::cw::sprite::base::CWPySprite& sprite,bool backlog)
{
    if(!::cw::g_cwpy) return;
    if(backlog){
        sprite.m_tlayer=::cw::g_LAYER_LOG;
        register_layered_sprite(sprite,::cw::g_cwpy->m_backloggrp);
        return;
    }
    sprite.m_tlayer=special_area()?message_tlayer(::cw::g_LAYER_SPMESSAGE):message_tlayer(::cw::g_LAYER_MESSAGE);
    register_layered_sprite(sprite,::cw::g_cwpy->m_cardgrp);
}
void sync_sprite_visual(::cw::sprite::base::CWPySprite& sprite)
{
    if(!sprite.m_ui_backend) sprite.m_ui_backend=::cw::sprite::base::sprite_ui_backend();
    if(sprite.m_ui_backend) sprite.m_ui_backend->invalidate(sprite,sprite.m_rect);
}
void clear_live_selection_lists()
{
    if(!::cw::g_cwpy) return;
    ::cw::g_cwpy->m_list.clear();
    ::cw::g_cwpy->m_index=-1;
    if(auto* handler=::cw::eventhandler::runtime_context()){
        handler->selectable_list.clear();
        handler->index=-1;
    }
}
void tint_surface(const std::shared_ptr<::cw::lib::Surface>& p,std::tuple<I,I,I> colour)
{
    if(!valid_surface(p)) return;
    const I r=std::clamp<I>(std::get<0>(colour),0,255),g=std::clamp<I>(std::get<1>(colour),0,255),b=std::clamp<I>(std::get<2>(colour),0,255);
    for(std::size_t i=0;i+3<p->rgba.size();i+=4){
        p->rgba[i]=static_cast<std::uint8_t>((p->rgba[i]*r+127)/255);
        p->rgba[i+1]=static_cast<std::uint8_t>((p->rgba[i+1]*g+127)/255);
        p->rgba[i+2]=static_cast<std::uint8_t>((p->rgba[i+2]*b+127)/255);
    }
}
std::shared_ptr<::cw::lib::Surface> clone_surface(const std::shared_ptr<::cw::lib::Surface>& src)
{
    if(!valid_surface(src)) return src;
    auto out=surface(src->size.width,src->size.height);
    out->rgba=src->rgba;
    mark_scaled_surface(out,surface_logical_scale(src));
    return out;
}
S rstrip_ws(S text)
{
    while(!text.empty()){
        const auto c=static_cast<unsigned char>(text.back());
        if(c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f') text.pop_back();
        else break;
    }
    return text;
}
S strip_lf(S text)
{
    while(!text.empty()&&text.front()=='\n') text.erase(text.begin());
    while(!text.empty()&&text.back()=='\n') text.pop_back();
    return text;
}
std::shared_ptr<::cw::setting::ResourceTable> get_resources(const V& v){if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p)return std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*p);return{};}
std::shared_ptr<::cw::setting::ResourceTable> window_specialchars(const std::optional<V>& supplied,bool selection=false)
{
    if(auto table=get_resources(supplied.value_or(V{})))return table;
    if(selection)return std::make_shared<::cw::setting::ResourceTable>();
    const auto source=get_resources((::cw::g_cwpy&&::cw::g_cwpy->m_rsrc)?::cw::g_cwpy->m_rsrc->m_specialchars:V{});
    // Py と同様、既定表をコピーし、LazyResource の
    // 項目は共有する。 ウィンドウのキー置換・削除で Resource
    // の辞書を変えない。明示的な表は空でも同一性を維持する。
    return source?get_resources(source->copy()):nullptr;
}
std::shared_ptr<::cw::event::EventInterface> events(){return ::cw::g_cwpy?::cw::g_cwpy->m_event:nullptr;}
std::shared_ptr<::cw::data::ScenarioData> sdata(){if(!::cw::g_cwpy)return{};if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());p&&*p)return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*p);return{};}
S message(const S& key,const S& fallback){if(::cw::g_cwpy){if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage());p&&*p)if(auto m=std::dynamic_pointer_cast<::cw::setting::MsgDict>(*p)){auto r=m->get_item(key);if(!r.empty())return r;}}return fallback;}
std::shared_ptr<ResolverContext> ctx(const ::cw::lib::Arguments& a){if(a.empty())return{};if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&a.back().storage());p&&*p)return std::dynamic_pointer_cast<ResolverContext>(*p);return{};}
V result(std::optional<S> s,I i){auto p=std::make_shared<ResolverResult>();p->value=std::move(s);p->index=i;return obj(p);}
std::pair<std::optional<S>,I> decode(const V& v,I fallback){if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p)if(auto r=std::dynamic_pointer_cast<ResolverResult>(*p))return{r->value,r->index};if(v.is_none())return{std::nullopt,fallback};return{to_string(v),fallback};}
::cw::lib::Arguments args(const S& key,const std::shared_ptr<ResolverContext>& c){return{V(key),obj(c)};}
S item_name(const std::shared_ptr<NameListItem>& item){if(!item)return{};if(const auto*p=std::get_if<S>(&item->m_data.storage()))return*p;if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&item->m_data.storage());p&&*p){if(auto o=std::dynamic_pointer_cast<::cw::character::Character>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::data::Party>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::data::YadoData>(*p))return o->get_showingname();if(auto o=std::dynamic_pointer_cast<::cw::header::CardHeader>(*p))return o->get_showingname();}return item->m_name_text.empty()?to_string(item->m_name):item->m_name_text;}
void set_base(const std::optional<std::vector<std::shared_ptr<NameListItem>>>& base,I n,const V& value){if(!base||n<0||static_cast<std::size_t>(n)>=base->size()||!(*base)[static_cast<std::size_t>(n)])return;auto& i=(*base)[static_cast<std::size_t>(n)];i->m_name=value;i->m_name_text=to_string(value);i->m_name_integer=integer(value);i->m_name_bool=boolean(value);}
bool full(I n){return(n&g__SP_FULL)!=0;} bool sharp(I n){return full(n)||(n&g__SP_EXPAND_SHARPS)!=0;}
std::shared_ptr<::cw::data::Step> find_step(const S& key, bool local = true){auto d=sdata();if(!d)return{};auto e=events();auto v=d->find_step(key,false,(local&&e)?e->get_nowrunningevent():std::nullopt);return v&&*v?*v:nullptr;}
std::shared_ptr<::cw::data::Flag> find_flag(const S& key, bool local = true){auto d=sdata();if(!d)return{};auto e=events();auto v=d->find_flag(key,false,(local&&e)?e->get_nowrunningevent():std::nullopt);return v&&*v?*v:nullptr;}
std::shared_ptr<::cw::data::Variant> find_variant(const S& key,bool different, bool local = true){auto d=sdata();if(!d)return{};auto e=events();auto v=d->find_variant(key,different,(local&&e)?e->get_nowrunningevent():std::nullopt);return v&&*v?*v:nullptr;}
bool has_specialchar(const S& key){if(!::cw::g_cwpy||!::cw::g_cwpy->m_rsrc)return false;auto table=get_resources(::cw::g_cwpy->m_rsrc->m_specialchars);return table&&table->contains(V(key));}

std::tuple<S,std::set<I>,std::vector<std::shared_ptr<NameListItem>>,I> replace(I flags,const S& update,const S& text,std::map<S,V>& table,const ::cw::lib::Callback& step,const ::cw::lib::Callback& flag,const ::cw::lib::Callback& variant,std::optional<std::vector<std::shared_ptr<NameListItem>>> base=std::nullopt,I start=0,std::optional<std::set<I>> info=std::nullopt,std::optional<std::vector<std::shared_ptr<NameListItem>>> list=std::nullopt,I list_index=0,std::optional<std::tuple<S,S>> scenario=std::nullopt,I stack=0,std::vector<std::shared_ptr<NameListItem>>* out_namelist=nullptr)
{
    if(!info) {
        _reset_nametable(table); // 最初のラベルでキャッシュした名前を再利用する。
        info=std::set<I>{};
    }
    if(!list) list=std::vector<std::shared_ptr<NameListItem>>{};
    auto c=std::make_shared<ResolverContext>();
    c->full=flags; c->update_type=update; c->name_table=&table; c->base_list=&base;
    c->name_list=&*list; c->start_index=start; c->special_info=&*info;
    c->name_list_index=list_index; c->scenario_info=scenario; c->stack=stack;
    const auto v=chars(text); S out; I pos=start;
    for(std::size_t i=0;i<v.size();++i){
        const auto& ch=v[i];
        if(ch=="#" && sharp(flags)){
            if(i+1>=v.size() || v[i+1]=="\n"){ out+=ch; ++pos; continue; }
            const S n=lower(v[i+1]);
            // リソース由来の #x は保持し、描画用の印を付ける。
            if(full(flags) && has_specialchar("#"+n)){ info->insert(pos); out+=ch; ++pos; continue; }
            const bool valid=full(flags)?(n=="m"||n=="r"||n=="u"||n=="c"||n=="i"||n=="t"||n=="y"):
                (n=="m"||n=="r"||n=="u"||n=="t"||n=="y"||((flags&g__SP_CARD_NAME)&&n=="c"));
            if(valid){
                S name;
                if(base && c->name_list_index<static_cast<I>(base->size())) name=item_name((*base)[static_cast<std::size_t>(c->name_list_index)]);
                else name=_get_namefromtable(n,update,table,*list);
                // Py と同様、記録済み一覧のカーソルを進めるのは basenamelist
                // を使う場合だけとする。 表から得た新しい名前は記録を追加するだけである。
                if(base) ++c->name_list_index;
                out+=name; pos+=length(name); ++i; continue;
            }
            out+=ch; ++pos; continue;
        }
        if(ch=="%"||ch=="$"||ch=="@"){
            const auto& cb=ch=="%"?flag:(ch=="$"?step:variant);
            std::size_t e=i+1; while(e<v.size()&&v[e]!=ch) ++e;
            if(e<v.size()){
                S key; for(std::size_t j=i+1;j<e;++j) key+=v[j];
                c->start_index=pos;
                auto [s,index]=decode(cb(args(key,c),{}),c->name_list_index); c->name_list_index=index;
                if(s){out+=*s;pos+=length(*s);i=e;continue;}
                if(!full(flags)&&ch!="@") continue; // CardWirth 1.50 との互換処理。
            }
        }
        if(full(flags)&&ch=="&") info->insert(pos);
        out+=ch; pos+=length(ch);
    }
    // Py と同様、namelist は戻り値とは独立して変更する。basenamelist
    // があれば戻り値はその一覧となる。 再帰的な展開も含め、ネ
    // イティブの値・出力引数の受け渡しでもこの区別を維持する。
    if(out_namelist)*out_namelist=*list;
    return{out,*info,base?*base:*list,c->name_list_index};
}

::cw::lib::Callback resolver(MessageWindow* w,int kind){return[w,kind](const ::cw::lib::Arguments& a,const ::cw::lib::KeywordArguments&)->V{auto c=ctx(a);if(!w||!c)return result(std::nullopt,0);const S key=a.empty()?S{}:to_string(a[0]);auto base=c->base_list?*c->base_list:std::optional<std::vector<std::shared_ptr<NameListItem>>>{};auto list=c->name_list?*c->name_list:std::vector<std::shared_ptr<NameListItem>>{};auto info=c->special_info?*c->special_info:std::optional<std::set<I>>{};std::tuple<std::optional<S>,I> r;if(kind==0)r=w->get_stepvalue(key,c->full,c->update_type,*c->name_table,base,c->start_index,info,list,c->name_list_index,c->scenario_info,c->stack,c->special_info,c->name_list);else if(kind==1)r=w->get_flagvalue(key,c->full,c->update_type,*c->name_table,base,c->start_index,info,list,c->name_list_index,c->scenario_info,c->stack,c->special_info,c->name_list);else r=w->get_variantvalue(key,c->full,c->update_type,*c->name_table,base,c->start_index,info,list,c->name_list_index,c->scenario_info,c->stack,c->special_info,c->name_list);c->name_list_index=std::get<1>(r);return result(std::get<0>(r),c->name_list_index);};}

class WindowRegistry final : public std::vector<MessageWindow*> {
public:
    void push_back(MessageWindow* window)
    {
        if (!window) return;
        erase(std::remove(begin(), end(), window), end());
        std::vector<MessageWindow*>::push_back(window);
    }
};
WindowRegistry windows;
bool compressed(){return ::cw::g_cwpy&&::cw::g_cwpy->m_setting&&::cw::g_cwpy->m_setting->m_messagelog_type==::cw::setting::g_LOG_COMPRESS;}

// ALLでは内容高を最低42pxとして計算
// する。ただし結果は元矩形の高さを超えない
// 。単体版ではこの内容高の下限を設けない。
I compressed_backlog_height(I rectangle_height, I content_height)
{
    if (::cw::dialog::is_all_mode()) content_height = std::max<I>(42, content_height);
    return std::min(rectangle_height, content_height);
}
bool old_colour(const std::optional<std::tuple<S,S,bool,bool,bool>>& hint){auto current=hint;if(!current){auto d=sdata();if(d)current=d->get_versionhint(::cw::g_HINT_CARD);}return ::cw::g_cwpy&&::cw::g_cwpy->m_sct&&::cw::g_cwpy->m_sct->lessthan("1.30",current);}
bool version_lessthan(const S& ver,std::optional<std::tuple<S,S,bool,bool,bool>> hint){auto current=hint;if(!current){auto d=sdata();if(d)current=d->get_versionhint(::cw::g_HINT_CARD);}return ::cw::g_cwpy&&::cw::g_cwpy->m_sct&&::cw::g_cwpy->m_sct->lessthan(ver,current);}
using ImgPathEntry=std::tuple<std::shared_ptr<::cw::image::ImageInfo>,bool,std::optional<V>,std::map<I,std::shared_ptr<::cw::lib::Surface>>>;
std::vector<ImgPathEntry> filter_message_imgpaths(const std::vector<ImgPathEntry>& imgpaths){
    if(!::cw::g_cwpy||!::cw::g_cwpy->m_setting||!::cw::g_cwpy->m_setting->m_face_variants||imgpaths.empty())return imgpaths;
    std::set<S> base_names;
    for(const auto& entry:imgpaths){auto info=std::get<0>(entry);if(!info||info->m_path.empty())continue;const auto fname=::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(info->m_path).filename());const auto dot=fname.find('.');base_names.insert(dot==S::npos?fname:fname.substr(0,dot));}
    if(base_names.size()!=1)return imgpaths;const S common=*base_names.begin();
    for(const auto& entry:imgpaths){auto info=std::get<0>(entry);if(!info)continue;const auto[stem,ext]=::cw::util::splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(info->m_path).filename()));(void)ext;if(stem==common+".message")return{entry};}
    for(const auto& entry:imgpaths){auto info=std::get<0>(entry);if(!info)continue;const auto[stem,ext]=::cw::util::splitext(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(info->m_path).filename()));(void)ext;if(stem==common)return{entry};}
    return imgpaths;
}
std::shared_ptr<::cw::lib::Surface> scale_message_surface(const std::shared_ptr<::cw::lib::Surface>& src){
    if(!valid_surface(src))return src;
    const I log_scale=surface_logical_scale(src);
    const I lw=std::max<I>(1,src->size.width/log_scale),lh=std::max<I>(1,src->size.height/log_scale);
    const I sw=message_scr(lw),sh=message_scr(lh);
    if(sw==src->size.width&&sh==src->size.height)return src;return ::cw::image::smoothscale(src,{sw,sh},true,false);
}
std::shared_ptr<::cw::lib::Surface> pick_scaled_talker(const std::map<I,std::shared_ptr<::cw::lib::Surface>>& scaled){
    if(scaled.empty())return{};
    std::shared_ptr<::cw::lib::Surface> chosen;
    if(const auto it=scaled.find(1);it!=scaled.end()&&it->second) chosen=it->second;
    else chosen=scaled.begin()->second;
    I scale=static_cast<I>(std::pow(2.0,std::floor(std::log(std::max<::cw::lib::Real>(::cw::g_UP_SCR,1.0))/std::log(2.0))));
    while(scale>=2){if(const auto it=scaled.find(scale);it!=scaled.end()&&it->second){chosen=it->second;break;}scale/=2;}
    return chosen;
}
std::optional<::cw::setting::FontSettingValue> effective_font_info(const S& role)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return std::nullopt;
    const auto types = ::cw::g_cwpy->m_setting->get_fonttypes();
    if (const auto it = types.find(role); it != types.end()) return it->second;
    return std::nullopt;
}
std::shared_ptr<::cw::imageretouch::Font> make_msg_font(const std::tuple<S,S,I,bool,bool,bool>& info, bool nobold = false){
    ::cw::setting::Resource fallback;
    auto resource = ::cw::g_cwpy ? ::cw::g_cwpy->m_rsrc : nullptr;
    if (!resource && ::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        fallback.m_setting = std::static_pointer_cast<::cw::lib::Object>(::cw::g_cwpy->m_setting);
    // 抽象ファミリーと拡大時の太字は、同じ Font 生成処理で解決する。
    auto* resolver = resource ? resource.get() : &fallback;
    return resolver->create_font("", std::get<0>(info), std::get<1>(info), 20,
        std::get<3>(info), std::get<4>(info), std::get<5>(info), 0, nobold, info);
}
std::shared_ptr<::cw::imageretouch::Font> get_message_exfont(const S& key){
    if(!::cw::g_cwpy||!::cw::g_cwpy->m_rsrc)return{};
#if defined(_WIN32)
    return ::cw::g_cwpy->m_rsrc->find_message_font(key);
#else
    // Python と同様、ここでは rsrc.msg_exfonts を読む。 特に
    // 既定の inherit は rsrc.fonts["message"] と全く同
    // じ Font オブジェクトとする。 LocalSetting から直接作り直すと
    // inherit を書体名と解釈し、Windows でゴシックへ戻ってしまう。
    if(const auto fonts=get_resources(::cw::g_cwpy->m_rsrc->m_msg_exfonts)){
        const auto item=fonts->get_item(V(key));
        if(const auto* p=std::get_if<::cw::lib::ObjectPtr>(&item.storage());p&&*p)
            if(auto font=std::dynamic_pointer_cast<::cw::imageretouch::Font>(*p))return font;
    }
    return{};
#endif
}
std::shared_ptr<::cw::imageretouch::Font> get_message_font_for_char(const S& ch){
    const S t=::cw::nctype::nctype(ch);S key;
    if(t==::cw::nctype::g_NC_SYMBOL)key="fw_symbol";else if(t==::cw::nctype::g_NC_NUMBER)key="fw_number";else if(t==::cw::nctype::g_NC_LATIN)key="fw_latin";
    else if(t==::cw::nctype::g_NC_HIRAGANA)key="hiragana";else if(t==::cw::nctype::g_NC_KATAKANA)key="katakana";else if(t==::cw::nctype::g_NC_HW_KATAKANA)key="hw_katakana";
    else if(t==::cw::nctype::g_NC_GREEK||t==::cw::nctype::g_NC_CYRILLIC)key="greek_and_cyrillic";else if(t==::cw::nctype::g_NC_JIS_KANJI_1)key="jis_kanji_1";else if(t==::cw::nctype::g_NC_JIS_KANJI_2)key="jis_kanji_2";
    else if(t==::cw::nctype::g_NC_JIS_KANJI_3||t==::cw::nctype::g_NC_JIS_KANJI_4||t==::cw::nctype::g_NC_CJK_KANJI)key="etc_kanji";else if(t==::cw::nctype::g_NC_ASCII_LATIN)key="latin";
    else if(t==::cw::nctype::g_NC_ASCII_NUMBER)key="number";else if(t==::cw::nctype::g_NC_ASCII_SYMBOL)key="symbol";else key="message";
    if (key != "message")
        if (auto f = get_message_exfont(key)) return f;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
#if defined(_WIN32)
        if (auto font = ::cw::g_cwpy->m_rsrc->find_message_font("message")) return font;
#else
        if (const auto fonts = get_resources(::cw::g_cwpy->m_rsrc->m_fonts)) {
            const auto item = fonts->get_item(V("message"));
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&item.storage()); p && *p) {
                if (auto font = std::dynamic_pointer_cast<::cw::imageretouch::Font>(*p)) return font;
            }
        }
#endif
    }
    // Py と同様、本文のリソースは通常の太さとし、draw_char が合成の太字を適用する。
    if (const auto info = effective_font_info("message")) return make_msg_font(*info, true);
    return {};
}
std::shared_ptr<::cw::imageretouch::Font> get_selectionbar_font(){
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        if (const auto fonts = get_resources(::cw::g_cwpy->m_rsrc->m_fonts)) {
            const auto item = fonts->get_item(V("selectionbar"));
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&item.storage()); p && *p) {
                if (auto font = std::dynamic_pointer_cast<::cw::imageretouch::Font>(*p)) return font;
            }
        }
    }
    if (const auto info = effective_font_info("selectionbar")) return make_msg_font(*info);
    return make_msg_font({S("MS UI Gothic"),S{},14,true,false,false});
}
std::shared_ptr<::cw::imageretouch::Font> get_backlog_page_font(){
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        if (const auto fonts = get_resources(::cw::g_cwpy->m_rsrc->m_fonts)) {
            const auto item = fonts->get_item(V("backlog_page"));
            if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&item.storage()); p && *p) {
                if (auto font = std::dynamic_pointer_cast<::cw::imageretouch::Font>(*p)) return font;
            }
        }
    }
    if (const auto info = effective_font_info("logpage")) return make_msg_font(*info);
    return make_msg_font({S("MS UI Gothic"),S{},14,true,false,false});
}
std::pair<std::shared_ptr<::cw::lib::Surface>,std::shared_ptr<::cw::lib::Surface>> render_message_glyph(const S& ch,bool sbold,bool smooth,std::tuple<I,I,I> colour,MessageUiBackend& backend){
    if(auto font=get_message_font_for_char(ch)){if(sbold){return{font->render_sbold(ch,smooth,colour),font->render_sbold(ch,smooth,{0,0,0})};}return{font->render(ch,smooth,colour),font->render(ch,smooth,{0,0,0})};}
    auto style=MessageWindow::get_messagestyledata();const auto& fontinfo=std::get<1>(style);S face=std::get<1>(fontinfo).empty()?std::get<0>(fontinfo):std::get<1>(fontinfo);I pixels=std::get<2>(fontinfo);if(pixels==0)pixels=20;pixels=message_scr(pixels);
    return{backend.render_text(ch,face,pixels,sbold||std::get<3>(fontinfo),std::get<5>(fontinfo),smooth,colour),backend.render_text(ch,face,pixels,sbold||std::get<3>(fontinfo),std::get<5>(fontinfo),smooth,{0,0,0})};
}
}

std::shared_ptr<::cw::lib::Surface> decorate(std::shared_ptr<::cw::lib::Surface> p, I angle, std::tuple<I,I,I> basecolour);

std::shared_ptr<MessageUiBackend> make_default_message_ui_backend(){
 // メッセージの配置・文字描画・更新通知・効果音は一つの Engine 実装にまとめる
 // 。 ホストの違いは ThreadBackend より下で扱い、ここでは分岐しない。
 return std::make_shared<CommonMessageUiBackend>();
}
std::shared_ptr<MessageUiBackend> message_ui_backend(){static const auto backend=make_default_message_ui_backend();return backend;}

std::shared_ptr<::cw::sprite::base::SelectableSprite> find_owned_selectable(
    const ::cw::sprite::base::SelectableSprite* raw)
{
    if (!raw) return {};
    for (const auto* window : windows) {
        if (!window) continue;
        for (const auto& bar : window->m_selections)
            if (bar && bar.get() == raw) return bar;
    }
    return {};
}

void BacklogData::initialize(V base)
{
    const auto*p=std::get_if<::cw::lib::ObjectPtr>(&base.storage());
    auto w=p&&*p?std::dynamic_pointer_cast<MessageWindow>(*p):nullptr; if(!w)return;
    auto s=std::dynamic_pointer_cast<SelectWindow>(w); m_type=s?1:0;
    m_text_typed=w->m_text_typed; m_text=w->m_text; m_text_log=w->m_text_log;
    m_names_typed=w->m_names_typed; m_names_log_typed=w->m_names_log_typed; m_imgpaths_typed=w->m_imgpaths_typed;
    m_rect_noscale_typed=w->m_rect_noscale_typed; m_top_noscale=w->m_top_noscale; m_bottom_noscale=w->m_bottom_noscale;
    m_talker_top_noscale=w->m_talker_top_noscale; m_talker_bottom_noscale=w->m_talker_bottom_noscale;
    m_top_noscale_typed=w->m_top_noscale; m_bottom_noscale_typed=w->m_bottom_noscale;
    m_talker_top_noscale_typed=w->m_talker_top_noscale; m_talker_bottom_noscale_typed=w->m_talker_bottom_noscale;
    m_name_table_typed=w->m_name_table_typed; m_name_subtable_typed=w->m_name_subtable_typed;
    m_flag_table_typed=w->m_flag_table_typed; m_step_table_typed=w->m_step_table_typed; m_variant_table_typed=w->m_variant_table_typed;
    m_spcharinfo_typed=w->m_spcharinfo_typed; m_centering_x_typed=w->m_centering_x_typed; m_centering_y_typed=w->m_centering_y_typed;
    m_boundarycheck_typed=w->m_boundarycheck_typed; m_showing_result=w->m_showing_result;
    m_showing_result_typed=integer(w->m_showing_result).value_or(-1); m_columns=w->m_columns; m_columns_typed=integer(w->m_columns).value_or(1);
    m_versionhint=w->m_versionhint; m_versionhint_typed=w->m_versionhint_typed; m_talker_name_typed=w->m_talker_name_typed;
    m_specialchars_used_typed=w->m_specialchars_used_typed;
    if(w->m_specialchars_typed){
        auto copied=w->m_specialchars_typed->copy(); const auto* copied_object=std::get_if<::cw::lib::ObjectPtr>(&copied.storage());
        auto copy=copied_object&&*copied_object?std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*copied_object):nullptr;
        if(copy){for(const auto& key:copy->iterkeys()){const auto k=to_string(key);if(!m_specialchars_used_typed.contains(k))copy->remove(key);}m_specialchars_typed=copy;m_specialchars=obj(copy);}
    }
}
I BacklogData::get_height_noscale(){const I h=m_rect_noscale_typed.height;if(compressed()){if(!m_text_typed.empty()){const I n=std::max(m_talker_bottom_noscale_typed+9,m_bottom_noscale_typed)-std::min(m_talker_top_noscale_typed-9,m_top_noscale_typed);const I height=compressed_backlog_height(h,n);const bool ok=m_names_log_typed.size()==1&&m_columns_typed==1&&std::get<1>(m_names_log_typed.front())==message("ok","OK");return height+(ok?0:25);}return h+25;}const I c=std::max<I>(1,m_columns_typed);return h+((static_cast<I>(m_names_log_typed.size())+c-1)/c)*25;}
I BacklogData::get__from_index(){const I c=std::max<I>(1,m_columns_typed);const I q=m_showing_result_typed<0?(m_showing_result_typed-(c-1))/c:m_showing_result_typed/c;return q*c;}
I BacklogData::get__to_index(){return std::min<I>(static_cast<I>(m_names_log_typed.size()),get__from_index()+std::max<I>(1,m_columns_typed));}
V BacklogData::create_message(){
    const I columns=std::max<I>(1,m_columns_typed); const auto pos=std::make_tuple(m_rect_noscale_typed.x,m_rect_noscale_typed.y);
    const bool compact=compressed(); I trim=0, showing=m_showing_result_typed; I height=m_rect_noscale_typed.height;
    std::vector<std::tuple<I,S>> names=m_names_log_typed;
    if(compact){const I h=std::max(m_talker_bottom_noscale_typed+9,m_bottom_noscale_typed)-std::min(m_talker_top_noscale_typed-9,m_top_noscale_typed);height=compressed_backlog_height(height,h);trim=std::min(m_talker_top_noscale_typed-9,m_top_noscale_typed);const bool ok=(m_type!=1||!m_text_typed.empty())&&names.size()==1&&columns==1&&std::get<1>(names.front())==message("ok","OK");if(ok){names.clear();showing=-1;}else{const I from=get__from_index(),to=get__to_index();const I count=static_cast<I>(names.size()),first=std::clamp<I>(from<0?count+from:from,0,count),last=std::clamp<I>(to<0?count+to:to,0,count);if(first<last)names=std::vector<std::tuple<I,S>>(names.begin()+first,names.begin()+last);else names.clear();showing-=from;}}
    const auto size=std::make_tuple(m_rect_noscale_typed.width,height); const auto special=m_specialchars_typed?std::optional<V>(obj(m_specialchars_typed)):std::nullopt;
    if(m_type==1){
        I select_height=m_rect_noscale_typed.height;
        if(!compact) select_height=height;
        else select_height=m_rect_noscale_typed.height;
        const auto select_size=std::make_tuple(m_rect_noscale_typed.width,select_height);
        auto out=std::make_shared<SelectWindow>();out->initialize(names,m_text_typed,pos,select_size,true,std::nullopt,showing,columns,true,m_name_table_typed,m_name_subtable_typed,m_flag_table_typed,m_step_table_typed,m_variant_table_typed,m_versionhint_typed,special,m_spcharinfo_typed,m_centering_x_typed);return obj(out);
    }
    auto out=std::make_shared<MessageWindow>();out->initialize(m_text_typed,names,m_imgpaths_typed,std::nullopt,pos,size,m_name_table_typed,m_name_subtable_typed,m_flag_table_typed,m_step_table_typed,m_variant_table_typed,true,std::nullopt,showing,m_versionhint_typed,special,trim,columns,m_spcharinfo_typed,m_centering_x_typed,m_centering_y_typed,m_boundarycheck_typed);return obj(out);
}
void NameListItem::initialize(std::optional<V> d,std::optional<V> n){m_data=d.value_or(V{});m_name=n.value_or(V{});m_data_text=std::holds_alternative<S>(m_data.storage())?std::get<S>(m_data.storage()):S{};m_name_text=to_string(m_name);m_name_integer=integer(m_name);m_name_bool=boolean(m_name);}
void _NameGetter::initialize(::cw::lib::Callback f){m_func_typed=std::move(f);m_names.clear();m_count=0;}void _NameGetter::reset(){m_count=0;}V _NameGetter::get_name(){const auto i=static_cast<std::size_t>(std::max<I>(0,m_count));V v;if(i<m_names.size())v=m_names[i];else{if(m_func_typed&&*m_func_typed)v=(*m_func_typed)({},{});m_names.push_back(v);}++m_count;return v;}
void BacklogCurtain::initialize(std::shared_ptr<::cw::lib::Object> spritegrp,V layer,std::tuple<I,I> size,std::tuple<I,I> pos,std::optional<std::tuple<I,I,I,I>> color){
    base::CWPySprite::initialize({});
    std::tuple<I,I,I,I> c;
    if(color) c=*color;
    else if(::cw::g_cwpy&&::cw::g_cwpy->m_setting) c=::cw::g_cwpy->m_setting->get_blcurtaincolour();
    else c=std::make_tuple(0,0,0,128);
    auto seq=std::make_shared<::cw::data::VariantSequence>();
    seq->m_values={V(std::get<0>(c)),V(std::get<1>(c)),V(std::get<2>(c)),V(std::get<3>(c))};
    m_color=obj(seq);
    m_curtain_color=c;
    m_size_noscale=obj(std::make_shared<RectValue>(RectValue(::cw::lib::Rect{0,0,std::get<0>(size),std::get<1>(size)})));
    m_pos_noscale=obj(std::make_shared<RectValue>(RectValue(::cw::lib::Rect{std::get<0>(pos),std::get<1>(pos),0,0})));
    if(const auto* integer=std::get_if<I>(&layer.storage())) m_tlayer={*integer,0,0,0};
    else {
        // Value は Python のレイヤータプルを VariantSequence で表す。 共通
        // スプライトのバックエンドが、Py と同様に cw.layer_val でそのタプルを符号化する。
        const auto* value=std::get_if<::cw::lib::ObjectPtr>(&layer.storage());
        const auto tuple=value?std::dynamic_pointer_cast<::cw::data::VariantSequence>(*value):nullptr;
        if(!tuple || tuple->m_values.size()!=4)
            throw std::invalid_argument("Backlog curtain layer requires an integer or four integer components.");
        I components[4];
        for(std::size_t i=0;i<4;++i) {
            const auto* component=std::get_if<I>(&tuple->m_values[i].storage());
            if(!component) throw std::invalid_argument("Backlog curtain layer components must be integers.");
            components[i]=*component;
        }
        const auto tuple_layer=std::make_tuple(components[0],components[1],components[2],components[3]);
        (void)::cw::layer_val(tuple_layer);
        m_tlayer=tuple_layer;
    }
    update_scale();
    if(spritegrp) register_layered_sprite(*this,obj(spritegrp));
}
void BacklogCurtain::update_scale(){
    const auto c=m_curtain_color;
    I w=0,h=0,x=0,y=0;
    if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&m_size_noscale.storage());p&&*p)
        if(auto r=std::dynamic_pointer_cast<RectValue>(*p)){w=r->value.width;h=r->value.height;}
    if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&m_pos_noscale.storage());p&&*p)
        if(auto r=std::dynamic_pointer_cast<RectValue>(*p)){x=r->value.x;y=r->value.y;}
    const auto sw=message_scr(w),sh=message_scr(h),sx=message_scr(x),sy=message_scr(y);
    auto image=surface(sw,sh,{static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(c),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(c),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(c),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(c),0,255))});
    m_image=obj(image);base::CWPySprite::m_image=image;base::CWPySprite::m_rect={sx,sy,sw,sh};
}
void BacklogPage::initialize(I p,I m,std::shared_ptr<::cw::lib::Object> spritegrp){
    base::CWPySprite::initialize({});
    m_tlayer=::cw::g_LAYER_LOG_PAGE;
    update_page(p,m);
    if(spritegrp) register_layered_sprite(*this,obj(spritegrp));
}
void BacklogPage::update_page(I p,I m){m_page=p;m_max=m;update_scale();}
void BacklogPage::update_scale(){
 const S s=std::to_string(integer(m_page).value_or(0))+"/"+std::to_string(integer(m_max).value_or(0));
 auto font=get_backlog_page_font();
 std::shared_ptr<::cw::lib::Surface> fg,shadow;
 auto backend=message_ui_backend();
 if(font){fg=font->render(s,true,{255,255,255});shadow=font->render(s,true,{0,0,0});}
 else{fg=backend->render_text(s,"MS UI Gothic",14,true,false,true,{255,255,255});shadow=backend->render_text(s,"MS UI Gothic",14,true,false,true,{0,0,0});}
 const I w=std::max(fg?fg->size.width:0,shadow?shadow->size.width:0)+2,h=std::max(fg?fg->size.height:0,shadow?shadow->size.height:0)+2;auto image=surface(w,h);
 for(I x=0;x<3;++x)for(I y=0;y<3;++y)if(x!=1||y!=1)blend_surface(image,shadow,x,y);blend_surface(image,fg,1,1);
 m_image=obj(image);base::CWPySprite::m_image=image;
 const I left=(::cw::g_cwpy&&::cw::g_cwpy->m_setting&&::cw::g_cwpy->m_setting->is_logscrollable())?18:10;
 const I area_w=message_scr(std::get<0>(::cw::g_SIZE_AREA));
 base::CWPySprite::m_rect={std::max<I>(0,area_w-w-message_scr(left)),message_scr(10),w,h};
 if(backend)backend->invalidate(base::CWPySprite::m_rect);
 sync_sprite_visual(*this);
}

void MessageWindow::initialize(S text,std::vector<std::tuple<I,S>> names,std::optional<std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>,bool,std::optional<V>,std::map<I,std::shared_ptr<::cw::lib::Surface>>>>> img,std::optional<V> talker,std::optional<std::tuple<I,I>> pos,std::optional<std::tuple<I,I>> size,std::optional<std::map<S,V>> table,std::optional<std::map<S,V>> sub,std::optional<std::map<S,std::shared_ptr<::cw::data::Flag>>> flags,std::optional<std::map<S,std::shared_ptr<::cw::data::Step>>> steps,std::optional<std::map<S,std::shared_ptr<::cw::data::Variant>>> variants,bool backlog,std::optional<V> result_value,I showing,std::optional<std::tuple<S,S,bool,bool,bool>> hint,std::optional<V> special,I trim,I columns,std::optional<std::set<I>> info,bool cx,bool cy,bool boundary)
{
    adjust_status_dialog_message_position(pos, size, 180, names.size(), columns, backlog);
    base::CWPySprite::initialize({});m_text_typed=text;m_text=text;m_names_typed=std::move(names);m_names_log_typed.clear();m_text_log.clear();m_backlog_typed=backlog;m_backlog=backlog;m_trim_top_noscale=trim;m_trim_top_noscale_typed=trim;m_columns=columns;m_columns_typed=std::max<I>(1,columns);m_centering_x_typed=cx;m_centering_y_typed=cy;m_boundarycheck_typed=boundary;m_centering_x=cx;m_centering_y=cy;m_boundarycheck=boundary;m_showing_result=showing;m_showing_result_typed=showing;m_result=result_value;m_result_typed=integer(result_value.value_or(V{}));m_spcharinfo_typed=info.value_or(std::set<I>{});m_name_table_typed=table.value_or(std::map<S,V>{});m_name_subtable_typed=sub.value_or(std::map<S,V>{});m_flag_table_typed=flags.value_or(std::map<S,std::shared_ptr<::cw::data::Flag>>{});m_step_table_typed=steps.value_or(std::map<S,std::shared_ptr<::cw::data::Step>>{});m_variant_table_typed=variants.value_or(std::map<S,std::shared_ptr<::cw::data::Variant>>{});if(m_name_table_typed.empty())m_name_table_typed=_create_nametable(1,talker,true);if(m_name_subtable_typed.empty())m_name_subtable_typed=_create_nametable(0,talker,true);m_specialchars_typed=window_specialchars(special);m_specialchars=m_specialchars_typed?obj(m_specialchars_typed):V{};m_specialchars_used_typed.clear();m_imgpaths_typed=img.value_or(std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>,bool,std::optional<V>,std::map<I,std::shared_ptr<::cw::lib::Surface>>>>{});    m_talker=talker.value_or(V{});m_versionhint_typed=hint;m_versionhint=versionhint_value(hint);auto p=pos.value_or(std::make_tuple<I,I>(81,50));auto z=size.value_or(std::make_tuple<I,I>(470,180));m_rect_noscale_typed={std::get<0>(p),std::get<1>(p),std::get<0>(z),std::get<1>(z)};m_rect_noscale=std::make_shared<RectValue>(RectValue{m_rect_noscale_typed});m_top_noscale=m_rect_noscale_typed.height;m_bottom_noscale=0;m_talker_top_noscale=0;m_talker_bottom_noscale=m_rect_noscale_typed.height;if(talker&&!talker->is_none()){auto n=to_string(*talker);m_talker_name=n;m_talker_name_typed=n;}else{m_talker_name.reset();m_talker_name_typed.reset();}m__animated_talker_owner=S("MessageWindow:")+std::to_string(reinterpret_cast<std::uintptr_t>(this));_init_image(z,p);m_rect=obj(std::make_shared<RectValue>(RectValue{base::CWPySprite::m_rect}));m_speed=::cw::g_cwpy&&::cw::g_cwpy->m_setting?V(::cw::g_cwpy->m_setting->m_messagespeed):V(1);m_charimgs_typed=create_charimgs(std::nullopt,true);m_is_drawing=true;m_frame=0;    windows.push_back(this);
    if(::cw::g_cwpy&&::cw::g_cwpy->is_playingscenario())if(auto d=sdata())d->set_versionhint(::cw::g_HINT_MESSAGE,hint);
    if(!m_backlog_typed)clear_live_selection_lists();
    register_message_window(*this,m_backlog_typed);
#if defined(CARDWIRTH_LOG_ENABLED)
    {
        S scenario;
        if (::cw::g_cwpy) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
            if (object && *object) {
                if (const auto data = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object)) {
                    scenario = data->m_name;
                }
            }
        }
        ::cardwirth::debug::log("MessageWindow init: text_len=" + std::to_string(m_text_typed.size()) +
            " charimgs=" + std::to_string(m_charimgs_typed.size()) +
            " selections=" + std::to_string(m_names_typed.size()) +
            " backlog=" + (m_backlog_typed ? "true" : "false") +
            " is_drawing=" + (m_is_drawing ? "true" : "false") +
            " rect=" + std::to_string(m_rect_noscale_typed.x) + "," +
            std::to_string(m_rect_noscale_typed.y) + "," +
            std::to_string(m_rect_noscale_typed.width) + "x" +
            std::to_string(m_rect_noscale_typed.height) +
            " scenario=" + scenario);
    }
#endif
    if(backlog||integer(m_speed).value_or(1)==0)draw_all();
    if(!m_backlog_typed&&::cw::g_cwpy&&base::CWPySprite::m_rect.width>0&&base::CWPySprite::m_rect.height>0)
        ::cw::g_cwpy->add_lazydraw_rect(base::CWPySprite::m_rect);
}
bool is_live_message_window(const MessageWindow* arg_window)
{
    if(!arg_window)return false;
    return std::find(windows.begin(),windows.end(),arg_window)!=windows.end();
}
void unregister_selection_bars(const std::vector<std::shared_ptr<SelectionBar>>& bars)
{
    for(const auto& bar:bars){
        if(!bar)continue;
        if(!bar->base::CWPySprite::m_ui_backend)bar->base::CWPySprite::m_ui_backend=::cw::sprite::base::sprite_ui_backend();
        if(bar->base::CWPySprite::m_ui_backend)bar->base::CWPySprite::m_ui_backend->unregister_sprite(*bar);
    }
}
void MessageWindow::_init_image(std::tuple<I,I> z,std::tuple<I,I> p){
    m_ui_backend=message_ui_backend();m_dirty_rects.clear();release_animated_talkers();m_talker_image.clear();m_talker_image_typed.clear();
    const auto colour=message_wincolour(m_backlog_typed);const auto scaled_z=message_scr(z);const auto scaled_p=message_scr(p);
    const auto sw=std::get<0>(scaled_z),sh=std::get<1>(scaled_z);
    base::CWPySprite::m_image=surface(sw,sh,{static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(colour),0,255))});
    m_image=obj(base::CWPySprite::m_image);m_rect_noscale_typed={std::get<0>(p),std::get<1>(p),std::get<0>(z),std::get<1>(z)};m_rect_noscale=std::make_shared<RectValue>(RectValue{m_rect_noscale_typed});
    base::CWPySprite::m_rect={std::get<0>(scaled_p),std::get<1>(scaled_p),sw,sh};m_top_noscale=std::get<1>(z);m_bottom_noscale=0;m_talker_top_noscale=0;m_talker_bottom_noscale=std::get<1>(z);
    draw_frame(base::CWPySprite::m_image,scaled_z,std::make_tuple<I,I>(0,0),m_backlog_typed);
    const auto imgpaths=filter_message_imgpaths(m_imgpaths_typed);const bool face_variant_mode=imgpaths.size()!=m_imgpaths_typed.size()&&m_imgpaths_typed.size()>1;
    struct TalkerSize{::cw::lib::Size size{};std::shared_ptr<::cw::image::ImageInfo> info{};};std::vector<TalkerSize> talkersize_noscale;
    if(!imgpaths.empty()){
        for(const auto& entry:imgpaths){
            auto info=std::get<0>(entry);if(!info)continue;const bool can_scaled=std::get<1>(entry);const auto& scaled_dict=std::get<3>(entry);
            std::shared_ptr<::cw::lib::Surface> talker_noscale;I instance_id=0,frame_id=0;
            S path=info->m_path;
            if(!scaled_dict.empty())talker_noscale=pick_scaled_talker(scaled_dict);
            else{
                if(!::cw::binary::image::path_is_code(path)){const S lpath=lower(path);if(::cw::g_cwpy){if(!::cw::g_cwpy->m_yadodir.empty()&&lpath.rfind(lower(::cw::g_cwpy->m_yadodir),0)==0)path=::cw::util::get_yadofilepath(path);else if(!::cw::g_cwpy->m_tempdir.empty()&&lpath.rfind(lower(::cw::g_cwpy->m_tempdir),0)==0)path=::cw::util::get_yadofilepath(path);}}
                if(!::cw::binary::image::path_is_code(path)&&::cw::animatedimage::is_supported_path(path)){instance_id=::cw::animatedimage::acquire(path,m__animated_talker_owner).value_or(0);auto [surf,frame]=::cw::animatedimage::get_surface(path,instance_id);talker_noscale=surf&&*surf?*surf:nullptr;frame_id=frame;}
                if(!talker_noscale)talker_noscale=::cw::util::load_image(path,true,{0,0},std::nullopt,true,false,can_scaled,true,std::nullopt,false);
            }
            if(!valid_surface(talker_noscale)||talker_noscale->size.width<=0)continue;
            auto talker_image=scale_message_surface(talker_noscale);m_talker_image_typed.emplace_back(talker_image,info);
            const I log_scale=surface_logical_scale(talker_noscale);
            I w=std::max<I>(1,talker_noscale->size.width/log_scale),h=std::max<I>(1,talker_noscale->size.height/log_scale);talkersize_noscale.push_back({{w,h},info});
            // Py と同様、宿・一時ファイルを解決して実際に取得したパスを保持
            // する。 ワーカーのフレーム更新にも、その同じパスが必要である。
            if(instance_id>0){m__animated_talker_sources[static_cast<I>(m_talker_image_typed.size()-1)]={path,instance_id,frame_id};m__animated_talker_paths.insert(path);}
        }
    }
    m_talker_top_noscale=std::numeric_limits<I>::max();m_talker_bottom_noscale=std::numeric_limits<I>::min();
    for(const auto& item:talkersize_noscale){const I tih=item.size.height;const auto baserect=item.info->calc_basecardposition({item.size.width,item.size.height},true);(void)baserect;I y=(180-tih)/2;y-=m_trim_top_noscale_typed;m_top_noscale=std::max<I>(0,std::min(y-9,m_top_noscale));m_bottom_noscale=std::min<I>(std::get<1>(z),std::max(y+tih+9,m_bottom_noscale));m_talker_top_noscale=std::max<I>(0,std::min(y,m_talker_top_noscale));m_talker_bottom_noscale=std::min<I>(std::get<1>(z),std::max(y+tih,m_talker_bottom_noscale));}
    I xmove=0;for(const auto& [talker_image,info]:m_talker_image_typed){auto baserect=info->calc_basecardposition({talker_image->size.width,talker_image->size.height},false);if(info->m_basecardtype&&*info->m_basecardtype=="LargeCard")baserect.x-=message_scr(10);xmove=std::max(-baserect.x,xmove);}
    for(I i=0;i<static_cast<I>(m_talker_image_typed.size());++i){const auto& [talker_image,info]=m_talker_image_typed[static_cast<std::size_t>(i)];auto baserect=info->calc_basecardposition({talker_image->size.width,talker_image->size.height},false);if(info->m_basecardtype&&*info->m_basecardtype=="LargeCard")baserect.x-=message_scr(10);I y=(message_scr(180)-baserect.height)/2;y-=message_scr(m_trim_top_noscale_typed);I x=info->m_basecardtype&&!info->m_basecardtype->empty()?message_scr(15)+baserect.x:(info->m_postype=="Center"?(base::CWPySprite::m_rect.width-baserect.width)/2:message_scr(15));x+=xmove;y+=baserect.y;
        if(::cw::g_cwpy&&::cw::g_cwpy->m_setting&&::cw::g_cwpy->m_setting->m_face_variants&&face_variant_mode){
            for(const auto& entry:imgpaths)if(std::get<0>(entry)==info){const I center_y=message_scr(86);const I w=talker_image->size.width,h=talker_image->size.height;const I center_x=x+message_scr(std::get<0>(::cw::g_SIZE_CARDIMAGE))/2;const I blit_x=center_x-w/2,blit_y=center_y-h/2;_blit_talker_image(i,talker_image,info,blit_x,blit_y,colour);break;}
        }else _blit_talker_image(i,talker_image,info,x,y,colour);
    }
    m__back.reset();m__fore.reset();
}
void MessageWindow::update_scale(){
    m_speed=::cw::g_cwpy&&::cw::g_cwpy->m_setting?V(::cw::g_cwpy->m_setting->m_messagespeed):V(1);
    if(m_specialchars_typed)m_specialchars_typed->reset();
    _init_image({m_rect_noscale_typed.width,m_rect_noscale_typed.height},{m_rect_noscale_typed.x,m_rect_noscale_typed.y});
    m_rect=obj(std::make_shared<RectValue>(RectValue{base::CWPySprite::m_rect}));
    const std::size_t imgslen=m_charimgs_typed.size();
    m_charimgs_typed=create_charimgs(std::nullopt,false);
    const I msgspeed=integer(m_speed).value_or(1);
    if(m_is_drawing&&msgspeed>=1){
        m_frame=0;
        while(imgslen<m_charimgs_typed.size())draw_char();
    }else{
        m_is_drawing=true;
        m_frame=0;
        draw_all();
    }
}
// Py は七つの選択レイヤー全体を消すが、ネイティブ版では登録済みウ
// ィンドウが所有するバーを個別に消す。 共有所有者が生存させていても
// 全プロキシーグループから解除し、同じレイヤーの無関係なスプライト
// は残す。 それらの削除は各ウィンドウ・バーの生存期間で管理する。
void MessageWindow::clear_selections(){
    if(::cw::g_cwpy)::cw::g_cwpy->clear_selection();
    if(auto*c=base::runtime_context()){c->selection=nullptr;c->lazy_selection=nullptr;c->selectable_list.clear();c->index=-1;}
    for(auto*w:windows){
        if(!w)continue;
        for(const auto& bar:w->m_selections){
            if(!bar)continue;
            bar->base::SelectableSprite::m_image=bar->get_unselectedimage();
            if(::cw::g_cwpy&&bar->base::SelectableSprite::m_rect.width>0&&bar->base::SelectableSprite::m_rect.height>0)
                ::cw::g_cwpy->add_lazydraw(std::static_pointer_cast<::cw::lib::Object>(bar));
        }
        unregister_selection_bars(w->m_selections);
    }
}
void MessageWindow::update_all_scales(){
    if(!::cw::g_cwpy)return;
    const auto* object=std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage());
    if(!object||!*object)return;
    const auto group=std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(*object);
    if(!group)return;
    std::vector<MessageWindow*> found;
    std::unordered_set<MessageWindow*> seen;
    for(const auto& sprite:group->sprites){
        auto* owner=::cw::sprite::base::layered_group_sprite_owner(sprite);
        auto* window=dynamic_cast<MessageWindow*>(owner);
        if(!window||!seen.insert(window).second)continue;
        found.push_back(window);
    }
    for(auto* window:found)window->update_scale();
}
std::vector<::cw::lib::Rect> MessageWindow::teardown_display(){
    std::vector<::cw::lib::Rect> rects;
    if(base::CWPySprite::m_rect.width>0&&base::CWPySprite::m_rect.height>0)
        rects.push_back(base::CWPySprite::m_rect);
    for(const auto& bar:m_selections){
        if(!bar)continue;
        bar->base::SelectableSprite::m_image=bar->get_unselectedimage();
        if(bar->base::SelectableSprite::m_rect.width>0&&bar->base::SelectableSprite::m_rect.height>0)
            rects.push_back(bar->base::SelectableSprite::m_rect);
    }
    unregister_selection_bars(m_selections);
    m_selections.clear();
    m__fore.reset();
    m__back.reset();
    kill();
    return rects;
}
void MessageWindow::update(const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&){update_animated_talkers();if(m_is_drawing)draw_char();}
void MessageWindow::_blit_talker_image(I index,std::shared_ptr<::cw::lib::Surface> talker,std::shared_ptr<::cw::image::ImageInfo> info,I x,I y,std::tuple<I,I,I,I> colour){if(!talker||!base::CWPySprite::m_image)return;::cw::lib::Rect rect{x,y,talker->size.width,talker->size.height};const I right=std::min<I>(base::CWPySprite::m_image->size.width,rect.x+rect.width),bottom=std::min<I>(base::CWPySprite::m_image->size.height,rect.y+rect.height);rect.x=std::max<I>(0,rect.x);rect.y=std::max<I>(0,rect.y);rect.width=std::max<I>(0,right-rect.x);rect.height=std::max<I>(0,bottom-rect.y);if(auto it=m__animated_talker_sources.find(index);it!=m__animated_talker_sources.end()&&rect.width&&rect.height){const auto&[path,instance,frame]=it->second;m__animated_talker_blits.emplace_back(std::make_shared<RectValue>(rect),copy_rect(base::CWPySprite::m_image,rect),std::move(info),path,instance,frame);}::cw::imageretouch::blit_2bitbmp_to_message(base::CWPySprite::m_image,talker,{x,y},colour);if(m_ui_backend)m_ui_backend->invalidate({base::CWPySprite::m_rect.x+rect.x,base::CWPySprite::m_rect.y+rect.y,rect.width,rect.height});}
void MessageWindow::update_animated_talkers(){
 if(m__animated_talker_blits.empty())return;
 auto colour=message_wincolour(m_backlog_typed);
 for(auto& b:m__animated_talker_blits){auto& rectobj=std::get<0>(b);auto rect=std::dynamic_pointer_cast<RectValue>(rectobj);if(!rect)continue;const auto& path=std::get<3>(b);const I instance=std::get<4>(b),oldframe=std::get<5>(b);const I frame=::cw::animatedimage::get_frame_id(path,instance);if(frame<=0||frame==oldframe)continue;auto [image,newframe]=::cw::animatedimage::get_surface(path,instance);if(!image||!*image)continue;auto scaled=scale_message_surface(*image);_restore_rect(rectobj,std::get<1>(b));::cw::imageretouch::blit_2bitbmp_to_message(base::CWPySprite::m_image,scaled,{rect->value.x,rect->value.y},colour);std::get<5>(b)=newframe;if(m_ui_backend)m_ui_backend->invalidate({base::CWPySprite::m_rect.x+rect->value.x,base::CWPySprite::m_rect.y+rect->value.y,rect->value.width,rect->value.height});}
}
void MessageWindow::_restore_rect(std::shared_ptr<::cw::lib::Object> rectobj,std::shared_ptr<::cw::lib::Surface> background){auto r=std::dynamic_pointer_cast<RectValue>(rectobj);if(!r||!background||!base::CWPySprite::m_image)return;for(I y=0;y<r->value.height;++y)for(I x=0;x<r->value.width;++x){const I dx=r->value.x+x,dy=r->value.y+y;if(dx<0||dy<0||dx>=base::CWPySprite::m_image->size.width||dy>=base::CWPySprite::m_image->size.height||x>=background->size.width||y>=background->size.height)continue;const auto di=static_cast<std::size_t>((dy*base::CWPySprite::m_image->size.width+dx)*4),si=static_cast<std::size_t>((y*background->size.width+x)*4);std::copy_n(background->rgba.begin()+static_cast<std::ptrdiff_t>(si),4,base::CWPySprite::m_image->rgba.begin()+static_cast<std::ptrdiff_t>(di));}}
MessageWindow::~MessageWindow(){kill();}
void MessageWindow::release_animated_talkers(){if(!m__animated_talker_owner.empty())::cw::animatedimage::release(m__animated_talker_owner,std::nullopt);m__animated_talker_paths.clear();m__animated_talker_sources.clear();m__animated_talker_blits.clear();}
void MessageWindow::kill(){release_animated_talkers();windows.erase(std::remove(windows.begin(),windows.end(),this),windows.end());if(base::CWPySprite::m_ui_backend)base::CWPySprite::m_ui_backend->unregister_sprite(*this);}
bool MessageWindow::is_sbold()
{
    const auto info = effective_font_info("message");
    return info && (::cw::g_UP_SCR > 1.0 ? std::get<4>(*info) : std::get<3>(*info));
}
std::tuple<bool,std::tuple<S,S,I,bool,bool,bool>,::cw::lib::Real,bool> MessageWindow::get_messagestyledata()
{
    const auto info = effective_font_info("message").value_or(::cw::setting::FontSettingValue{});
    const bool decoration = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
        ? ::cw::g_cwpy->m_setting->get_decorationfont() : false;
    return {is_sbold(), info, ::cw::g_UP_SCR, decoration};
}
void MessageWindow::draw_all(){while(m_is_drawing)draw_char();}
void MessageWindow::draw_char(){
 if(!m_charimgs_typed.empty()&&!m__fore){
  I h_noscale=m_centering_y_typed?(m_blockbottom_noscale-m_blocktop_noscale):m_rect_noscale_typed.height;
  const I fw=message_scr(470),fh=message_scr(std::max<I>(1,h_noscale));
  m__fore=surface(fw,fh);
  if(m__fore)m__back=copy_rect(*m__fore,{0,0,fw,fh});
 }
 if(m_charimgs_typed.empty()){
  m_is_drawing=false;m_frame=0;
  if(::cw::g_cwpy)::cw::g_cwpy->m_has_inputevent=true;
  create_selectionbar();m__fore.reset();m__back.reset();return;
 }
 auto r=m_charimgs_typed.front();
 if(m_frame<r.frame){++m_frame;return;}
 m_charimgs_typed.erase(m_charimgs_typed.begin());
 const I shiftx=m_centering_x_typed?(base::CWPySprite::m_rect.width-r.line.width)/2:0;
 const I shifty=m_centering_y_typed?message_scr((180-(m_blockbottom_noscale-m_blocktop_noscale))/2)-message_scr(m_blocktop_noscale):0;
 const I tt=message_scr(m_trim_top_noscale_typed);
 const I bt=m_centering_y_typed?message_scr(m_blocktop_noscale):message_scr(m_trim_top_noscale_typed);
 const I px=r.pos.x,py=r.pos.y;
 I w=0,h=0;
 if(r.special){
  auto colour=message_wincolour(m_backlog_typed);
  const I sx=px+shiftx, sy=py-tt+shifty;
  ::cw::imageretouch::blit_2bitbmp_to_message(base::CWPySprite::m_image,r.special,{sx,sy},colour);
  w=r.special->size.width;h=r.special->size.height;
  if(::cw::g_cwpy&&w>0&&h>0)
   ::cw::g_cwpy->add_lazydraw_rect({base::CWPySprite::m_rect.x+sx,base::CWPySprite::m_rect.y+sy,w,h});
 }
 if(r.shadow&&m__back){
  for(I ox=-1;ox<=1;++ox)for(I oy=-1;oy<=1;++oy){
   const I sx=px+shiftx+ox,sy=py+oy-bt;
   blend_surface(*m__back,r.shadow,sx,sy);
   if(r.shadow->size.width>0&&r.shadow->size.height>0&&::cw::g_cwpy)
    ::cw::g_cwpy->add_lazydraw_rect({base::CWPySprite::m_rect.x+sx,base::CWPySprite::m_rect.y+sy,r.shadow->size.width,r.shadow->size.height});
  }
  w=r.shadow->size.width;h=r.shadow->size.height;
 }
 if(r.text&&m__fore){
  const I tx=px+shiftx,ty=py-bt;
  blend_surface(*m__fore,r.text,tx,ty);
  w=r.text->size.width;h=r.text->size.height;
 }
 if(w>0&&h>0&&m__fore&&m__back){
  ::cw::lib::Rect area1{px-1,py-1,w+3,h+2};
  ::cw::lib::Rect area2=area1;
  if(m_centering_y_typed){area2.y-=message_scr(m_blocktop_noscale);area1.y+=shifty;}
  else area2.y-=message_scr(m_trim_top_noscale_typed);
  area1.x+=shiftx;area2.x+=shiftx;area1.y-=tt;
  blend_surface(base::CWPySprite::m_image,*m__back,area1.x,area1.y,area2);
  blend_surface(base::CWPySprite::m_image,*m__fore,area1.x,area1.y,area2);
  if(::cw::g_cwpy)
   ::cw::g_cwpy->add_lazydraw_rect({base::CWPySprite::m_rect.x+area1.x,base::CWPySprite::m_rect.y+area1.y,area1.width,area1.height});
 }else if(w>0&&h>0){
  ::cw::lib::Rect dirty{base::CWPySprite::m_rect.x+px+shiftx-1,base::CWPySprite::m_rect.y+py-1,w+2,h+2};
  m_dirty_rects.push_back(dirty);
  if(m_ui_backend)m_ui_backend->invalidate(dirty);
 }
 ++m_frame;
}
void MessageWindow::create_selectionbar(){
 m_names_log.clear();m_names_log_typed.clear();
 unregister_selection_bars(m_selections);
 m_selections.clear();
 const I c=std::max<I>(1,m_columns_typed);
 I x=m_rect_noscale_typed.x,y=m_rect_noscale_typed.y+m_rect_noscale_typed.height;
 std::optional<std::tuple<S,S,bool,bool,bool>> version_hint;
 if(auto d=sdata())version_hint=d->get_versionhint(::cw::g_HINT_CARD);
 const bool use_bar_spchr=!m_backlog_typed&&m_barspchr_typed&&!(::cw::g_cwpy&&::cw::g_cwpy->m_sct&&::cw::g_cwpy->m_sct->lessthan("1.30",version_hint));
 for(I i=0;i<static_cast<I>(m_names_typed.size());++i){
  auto n=m_names_typed[static_cast<std::size_t>(i)];
  if(use_bar_spchr)std::get<1>(n)=std::get<0>(rpl_specialstr(V(false),std::get<1>(n),m_name_subtable_typed,true,true));
  const I w=m_rect_noscale_typed.width/c+((i%c)<(m_rect_noscale_typed.width%c)?1:0);
  auto b=std::make_shared<SelectionBar>();
  b->initialize(i,n,{x,y},{w,25},m_backlog_typed,m_names_typed.size()>1&&m_backlog_typed&&m_showing_result_typed==i);
  b->update({}, {});
  if(::cw::g_cwpy&&b->base::SelectableSprite::m_rect.width>0&&b->base::SelectableSprite::m_rect.height>0)
   ::cw::g_cwpy->add_lazydraw(std::static_pointer_cast<::cw::lib::Object>(b));
  m_selections.push_back(b);
  m_names_log_typed.push_back(n);m_names_log.push_back(n);
  if((i+1)%c==0){x=m_rect_noscale_typed.x;y+=25;}else x+=w;
 }
 if(!m_backlog_typed&&::cw::g_cwpy){
  ::cw::g_cwpy->m_list.clear();
  for(const auto& bar:m_selections)::cw::g_cwpy->m_list.push_back(bar);
  ::cw::g_cwpy->m_index=-1;
  if(auto*handler=::cw::eventhandler::runtime_context()){
   handler->selectable_list=::cw::g_cwpy->m_list;
   handler->index=-1;
  }
 }
}
std::vector<CharImageRecord> MessageWindow::create_charimgs(std::optional<std::tuple<I,I>> p,bool init){
    const std::tuple<I,I> pos_noscale=p.value_or(m_centering_x_typed?std::make_tuple<I,I>(-1,9):std::make_tuple<I,I>(14,9));
    I origin_x=message_scr(std::get<0>(pos_noscale)),origin_y=message_scr(std::get<1>(pos_noscale));I x=origin_x,y=origin_y;const I yp_noscale=std::get<1>(pos_noscale);
    if(!m_talker_image_typed.empty()){if(!m_backlog_typed&&init){auto r=rpl_specialstr(V(true),m_text_typed,std::nullopt,true,true);m_text_typed=std::get<0>(r);m_text=m_text_typed;m_spcharinfo_typed=std::get<1>(r).value_or(std::set<I>{});if(m_boundarycheck_typed)m_text_typed=::cw::util::wordwrap(m_text_typed,33,std::nullopt,{},{},&m_spcharinfo_typed);else m_text_typed=::cw::util::txtwrap(m_text_typed,2,0,{},false,&m_spcharinfo_typed);}I w=message_scr(74);if(version_lessthan("1.28",m_versionhint_typed)){w=0;for(const auto& [bmp,info]:m_talker_image_typed){(void)info;w=std::max<I>(w,bmp?bmp->size.width:0);}}if(m_centering_x_typed)origin_x=x=message_scr(-26)+w;else origin_x=x=origin_x+message_scr(25)+w;y=origin_y;}
    else if(!m_backlog_typed&&init){auto r=rpl_specialstr(V(true),m_text_typed,std::nullopt,true,true);m_text_typed=std::get<0>(r);m_text=m_text_typed;m_spcharinfo_typed=std::get<1>(r).value_or(std::set<I>{});if(m_boundarycheck_typed)m_text_typed=::cw::util::wordwrap(m_text_typed,43,std::nullopt,{},{},&m_spcharinfo_typed);else m_text_typed=::cw::util::txtwrap(m_text_typed,3,0,{},false,&m_spcharinfo_typed);}
    const I posp_x=origin_x,posp_y=origin_y;I cnt=0;bool skip=false;std::vector<CharImageRecord> out;const auto v=::cw::util::graphemes(m_text_typed);const I lineheight_noscale=22,lineheight=message_scr(lineheight_noscale);
    if(!m_ui_backend)m_ui_backend=message_ui_backend();const auto style=get_messagestyledata();const bool sbold=std::get<0>(style);const bool smooth=::cw::g_cwpy&&::cw::g_cwpy->m_setting?::cw::g_cwpy->m_setting->get_fontsmoothing_message():true;
    std::tuple<I,I,I> colour{255,255,255};::cw::lib::Real frame_base=0.0;const ::cw::lib::Real speed=static_cast<::cw::lib::Real>(integer(m_speed).value_or(1));I index=0;I y_noscale=yp_noscale;bool additional_wait=false,additional_wait_after_space=false;const I bottom_limit=m_rect_noscale_typed.height-yp_noscale-lineheight_noscale*7;
    // Py と同様、_init_image の顔画像の上下端を維持し、そこへテキスト
    // の範囲を合わせる。 空で始めるのはテキスト領域だけとし、Py の 32 ビット
    // の特別値を使って、空領域の減算もネイティブの int64 で定義済みにする。
    m_blocktop_noscale=0x7fffffff;m_blockbottom_noscale=-0x7fffffff-1;m_text_log.clear();m_specialchars_used_typed.clear();
    auto add_wait=[&](bool space,bool is_waitchar){
        if(::cw::g_cwpy&&::cw::g_cwpy->m_setting&&::cw::g_cwpy->m_setting->m_wait_after_punctuation_mark&&!is_waitchar){
            if(additional_wait||(additional_wait_after_space&&space))
                frame_base+=std::max<::cw::lib::Real>(4.0,speed*2.0);
        }
        additional_wait=false;
        additional_wait_after_space=false;
    };
    auto put_topbottom=[&](I yn,I height){m_blocktop_noscale=std::min(m_blocktop_noscale,yn);m_blockbottom_noscale=std::max(m_blockbottom_noscale,yn+height);m_top_noscale=std::min(yn-yp_noscale,m_top_noscale);m_bottom_noscale=std::max(yn+height+bottom_limit,m_bottom_noscale);m_top_noscale=std::max<I>(0,m_top_noscale);m_bottom_noscale=std::min<I>(m_rect_noscale_typed.height,m_bottom_noscale);};
    ::cw::lib::Rect line_rect{origin_x,0,0,1}; std::size_t line_start=0;
    auto put_xinfo=[&](I px,I width){
        if(line_rect.width<=0){line_rect={px,0,width,1};return;}
        const I left=std::min(line_rect.x,px); const I right=std::max(line_rect.x+line_rect.width,px+width);
        line_rect.x=left; line_rect.width=std::max<I>(0,right-left);
    };
    auto flush_line=[&](){
        for(std::size_t k=line_start;k<out.size();++k) out[k].line={line_rect.x,out[k].pos.y,line_rect.width,lineheight};
        line_start=out.size(); line_rect={origin_x,0,0,1};
    };
    auto join_line_glyph=[&](std::shared_ptr<::cw::lib::Surface> img){
        if(!valid_surface(img))return img;
        const I ow=img->size.width,oh=img->size.height;
        auto scaled=::cw::image::smoothscale(img,{ow+message_scr(20),oh},false,false);
        return copy_rect(scaled,{10,0,std::min(ow,message_scr(20)),oh});
    };
    for(std::size_t i=0;i<v.size();++i){const auto& ch=v[i];
        if(ch=="\n"){if(!m_spcharinfo_typed.contains(index))add_wait(true,false);flush_line();++cnt;x=posp_x;y=lineheight*cnt+posp_y;y_noscale=lineheight_noscale*cnt+yp_noscale;m_text_log+=ch;++index;if(cnt>6&&!m_centering_y_typed)break;continue;}
        if(skip){skip=false;index+=length(ch);continue;}
        const S orig_chars=i+1<v.size()?ch+v[i+1]:ch;const S chars=lower(orig_chars);
        if(m_specialchars_typed&&m_spcharinfo_typed.contains(index)&&chars.size()>=2&&chars[0]=='#'&&m_specialchars_typed->contains(V(chars))){
            m_specialchars_used_typed.insert(chars);
            auto [raw_image,userfont]=specialchar_resource(m_specialchars_typed->get_item(V(chars)));
            if(!raw_image) raw_image=m_ui_backend->render_text(chars,std::get<0>(std::get<1>(style)),message_scr(std::get<2>(std::get<1>(style))),sbold,std::get<5>(std::get<1>(style)),smooth,colour);
            const I log_scale=surface_logical_scale(raw_image);
            const I logical_h=std::max<I>(1,raw_image?raw_image->size.height/log_scale:lineheight_noscale);
            auto special_image=scale_message_surface(clone_surface(raw_image));
            const I w=std::max<I>(1,special_image?special_image->size.width:message_scr(20));
            if(userfont){
                add_wait(false,false);
                put_xinfo(x,w); put_topbottom(y_noscale+1,logical_h);
                frame_base+=speed; CharImageRecord r; r.pos={x+message_scr(1),y+message_scr(1)}; r.special=special_image; r.line=line_rect; r.frame=static_cast<I>(std::lround(frame_base)); out.push_back(r);
                x+=message_scr(20); skip=true; m_text_log+=orig_chars; index+=length(ch); continue;
            }
            add_wait(false,true);
            put_xinfo(x,w); put_topbottom(y_noscale-1,lineheight_noscale+2);
            tint_surface(special_image,colour);
            frame_base+=speed; CharImageRecord r; r.pos={x,y}; r.special=decorate(special_image,8,colour); r.line=line_rect; r.frame=static_cast<I>(std::lround(frame_base)); out.push_back(r);
            x+=message_scr(20); skip=true; additional_wait_after_space=true; m_text_log+=orig_chars; index+=length(ch); continue;
        }
        if(m_spcharinfo_typed.contains(index)&&chars.size()>=2&&chars[0]=='&'&&
           static_cast<unsigned char>(chars[1])>=0x20&&static_cast<unsigned char>(chars[1])<=0x7e){
            colour=get_fontcolour(S(1,chars[1]));skip=true;index+=length(ch);continue;
        }
        m_text_log+=ch;const I cwidth=::cw::util::get_strlen(ch)*message_scr(10);if(!ch.empty())add_wait(is_unicode_space(ch),g__WAIT_CHARS.find(ch)!=S::npos||g__WAIT_CHARS_BEFORE_SPACE.find(ch)!=S::npos);
        if(!ch.empty()&&!is_unicode_space(ch)){auto [text_image,shadow_image]=render_message_glyph(ch,sbold,smooth,colour,*m_ui_backend);text_image=decorate(text_image,8,colour);if(ch=="―"||ch=="─"||ch=="＿"||ch=="￣"){text_image=join_line_glyph(text_image);shadow_image=join_line_glyph(shadow_image);}put_xinfo(x,cwidth);put_topbottom(y_noscale-1,lineheight_noscale+2);I px=x,py=y;const I cheight=text_image?text_image->size.height:lineheight;if(text_image)px+=(cwidth-text_image->size.width+message_scr(2))/2;py+=(lineheight-cheight)/2;frame_base+=speed;CharImageRecord r;r.pos={px,py};r.text=text_image;r.shadow=shadow_image;r.line=line_rect;r.frame=static_cast<I>(std::lround(frame_base));out.push_back(r);if(g__WAIT_CHARS.find(ch)!=S::npos)additional_wait=true;else if(g__WAIT_CHARS_BEFORE_SPACE.find(ch)!=S::npos)additional_wait_after_space=true;}
        else if(!ch.empty()) put_xinfo(x,cwidth);
        x+=cwidth;index+=length(ch);
    }
    flush_line();
    if(m_centering_y_typed){const I top=(180-(m_blockbottom_noscale-m_blocktop_noscale))/2;m_top_noscale=top-9;m_bottom_noscale=top+(m_blockbottom_noscale-m_blocktop_noscale)+9;m_top_noscale=std::max<I>(0,m_top_noscale);m_bottom_noscale=std::min<I>(m_rect_noscale_typed.height,m_bottom_noscale);}
    if(m_blockbottom_noscale<=m_blocktop_noscale){m_top_noscale=9;m_bottom_noscale=m_rect_noscale_typed.height-bottom_limit;}
    if(m_bottom_noscale<=m_top_noscale){m_top_noscale=0;m_bottom_noscale=yp_noscale+bottom_limit;}
    return out;
}
std::tuple<S,std::optional<std::set<I>>> MessageWindow::rpl_specialstr(V f,S s,std::optional<std::map<S,V>> table,bool local,bool card){const bool b=boolean(f).value_or(integer(f).value_or(0)!=0);I n=b?g__SP_FULL:g__SP_EXPAND_SHARPS;if(local)n|=g__SP_LOCAL_VARIABLES;if(card)n|=g__SP_CARD_NAME;/* Py の if not nametable と同様、明示的な空辞書も含める。 */auto t=table&&!table->empty()?*table:m_name_table_typed;{auto r=replace(n,"All",s,t,resolver(this,0),resolver(this,1),resolver(this,2));return{std::get<0>(r),std::get<1>(r)};}}
S MessageWindow::rpl_specialstr_2(V f,S s,std::optional<std::map<S,V>> t,bool l,bool c){return std::get<0>(rpl_specialstr(f,std::move(s),std::move(t),l,c));}V MessageWindow::rpl_specialstr_3(bool f,S s,std::optional<std::map<S,V>> t,bool l,bool c){return V(std::get<0>(rpl_specialstr(V(f),std::move(s),std::move(t),l,c)));}

std::tuple<std::optional<S>,I> MessageWindow::get_stepvalue(S key,I full_flags,S up,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::set<I>* out_spcharinfo,std::vector<std::shared_ptr<NameListItem>>* out_namelist)
{
    std::shared_ptr<::cw::data::Step> step;
    if (m_backlog_typed) {
        const auto found = m_step_table_typed.find(key);
        if (found == m_step_table_typed.end()) return {std::nullopt,index};
        step = found->second;
    } else {
        step = find_step(key);
        if (!step) {
            auto r = _get_spstep(key,full_flags,up,base,list,index,list?&*list:nullptr);
            index = std::get<1>(r);
            if (!std::get<0>(r)) return {std::nullopt,index};
            step = *std::get<0>(r);
        }
    }
    if (!step) return {std::nullopt,index};
    m_step_table_typed[key] = step;
    // Py の Window と同様、変数オブジェクトを保持して現在
    // 値を読む。 記録値と一覧位置の追加の増分はモジュールの _get_*value
    // ヘルパー専用とする。 特殊ステップの展開は、ここへ来
    // る前に自身の記録済み Number・名前を処理する。
    S value = step->get_valuename(std::nullopt);
    if (stack<=0 && step->m_spchars) {
        auto r=replace(full_flags,up,value,table,resolver(this,0),resolver(this,1),resolver(this,2),base,start,info,list,index,scenario,stack+1,list?&*list:nullptr);
        value=std::get<0>(r);index=std::get<3>(r);
        if(info)*info=std::get<1>(r);
    }
    if(out_spcharinfo&&info)*out_spcharinfo=*info;
    if(out_namelist&&list)*out_namelist=*list;
    return {value,index};
}
std::tuple<std::optional<S>,I> MessageWindow::get_flagvalue(S key,I full_flags,S up,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::set<I>* out_spcharinfo,std::vector<std::shared_ptr<NameListItem>>* out_namelist)
{
    const auto found = m_flag_table_typed.find(key);
    auto flag = m_backlog_typed ? (found==m_flag_table_typed.end()?nullptr:found->second) : find_flag(key);
    if (!flag) return {std::nullopt,index};
    m_flag_table_typed[key] = flag;
    S value = flag->get_valuename(std::nullopt);
    if (stack<=0 && flag->m_spchars) {
        auto r=replace(full_flags,up,value,table,resolver(this,0),resolver(this,1),resolver(this,2),base,start,info,list,index,scenario,stack+1,list?&*list:nullptr);
        value=std::get<0>(r);index=std::get<3>(r);
        if(info)*info=std::get<1>(r);
    }
    if(out_spcharinfo&&info)*out_spcharinfo=*info;
    if(out_namelist&&list)*out_namelist=*list;
    return {value,index};
}
std::tuple<std::optional<S>,I> MessageWindow::get_variantvalue(S key,I full_flags,S up,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::set<I>* out_spcharinfo,std::vector<std::shared_ptr<NameListItem>>* out_namelist)
{
    const auto found = m_variant_table_typed.find(key);
    auto variant = m_backlog_typed ? (found==m_variant_table_typed.end()?nullptr:found->second) : find_variant(key,_is_differentscenario(scenario));
    if (!variant) return {std::nullopt,index};
    m_variant_table_typed[key] = variant;
    if(out_spcharinfo&&info)*out_spcharinfo=*info;
    if(out_namelist&&list)*out_namelist=*list;
    return {variant->string_value(),index};
}
std::tuple<I,I,I> MessageWindow::get_fontcolour(S s){if(s=="r")return{255,0,0};if(s=="g")return{0,255,0};if(s=="b")return{0,255,255};if(s=="y")return{255,255,0};if(s=="w")return{255,255,255};if(!old_colour(std::nullopt)){if(s=="o")return{255,165,0};if(s=="p")return{204,136,255};if(s=="l")return{169,169,169};if(s=="d")return{105,105,105};}return{255,255,255};}

void SelectWindow::initialize(std::vector<std::tuple<I,S>> n,S text,std::optional<std::tuple<I,I>> p,std::optional<std::tuple<I,I>> z,bool backlog,std::optional<V> result_value,I showing,I columns,bool barspchr,std::optional<std::map<S,V>> t,std::optional<std::map<S,V>> sub,std::optional<std::map<S,std::shared_ptr<::cw::data::Flag>>> f,std::optional<std::map<S,std::shared_ptr<::cw::data::Step>>> st,std::optional<std::map<S,std::shared_ptr<::cw::data::Variant>>> va,std::optional<std::tuple<S,S,bool,bool,bool>> hint,std::optional<V> special,std::optional<std::set<I>> info,bool cx){
    adjust_status_dialog_message_position(p, z, 40, n.size(), columns, backlog);
    m_barspchr_typed=barspchr;m__barspchr=barspchr;if(text.empty())text=message("select_message","Select");
    base::CWPySprite::initialize({});const auto pos=p.value_or(std::make_tuple<I,I>(81,50));const auto size=z.value_or(std::make_tuple<I,I>(470,40));
    m_text_typed=text;m_text=text;m_names_typed=std::move(n);m_names_log_typed.clear();m_text_log.clear();m_backlog_typed=backlog;m_backlog=backlog;m_trim_top_noscale=m_trim_top_noscale_typed=0;m_columns=columns;m_columns_typed=std::max<I>(1,columns);m_centering_x_typed=cx;m_centering_y_typed=false;m_boundarycheck_typed=false;m_centering_x=cx;m_centering_y=false;m_boundarycheck=false;m_showing_result=showing;m_showing_result_typed=showing;m_result=result_value;m_result_typed=integer(result_value.value_or(V{}));m_spcharinfo_typed=info.value_or(std::set<I>{});m_name_table_typed=t.value_or(std::map<S,V>{});m_name_subtable_typed=sub.value_or(std::map<S,V>{});m_flag_table_typed=f.value_or(std::map<S,std::shared_ptr<::cw::data::Flag>>{});m_step_table_typed=st.value_or(std::map<S,std::shared_ptr<::cw::data::Step>>{});m_variant_table_typed=va.value_or(std::map<S,std::shared_ptr<::cw::data::Variant>>{});if(m_name_table_typed.empty())m_name_table_typed=_create_nametable(1,std::nullopt,true);if(m_name_subtable_typed.empty())m_name_subtable_typed=_create_nametable(0,std::nullopt,true);m_specialchars_typed=window_specialchars(special,true);m_specialchars=m_specialchars_typed?obj(m_specialchars_typed):V{};m_specialchars_used_typed.clear();m_imgpaths_typed.clear();m_talker_image.clear();m_talker_image_typed.clear();m_versionhint_typed=hint;m_versionhint=versionhint_value(hint);m_blocktop_noscale=0;m_blockbottom_noscale=std::get<1>(size);m_talker_top_noscale=0;m_talker_bottom_noscale=std::get<1>(size);_init_image(size,pos);m_rect=obj(std::make_shared<RectValue>(RectValue{base::CWPySprite::m_rect}));m_speed=::cw::g_cwpy&&::cw::g_cwpy->m_setting?V(::cw::g_cwpy->m_setting->m_messagespeed):V(1);m_charimgs_typed=create_charimgs(std::make_tuple<I,I>(14,9),true);m_is_drawing=true;m_frame=0;m_selections.clear();windows.push_back(this);register_message_window(*this,m_backlog_typed);draw_all();if(!m_backlog_typed&&::cw::g_cwpy&&base::CWPySprite::m_rect.width>0&&base::CWPySprite::m_rect.height>0)::cw::g_cwpy->add_lazydraw(std::make_shared<RectValue>(base::CWPySprite::m_rect));
}
void SelectWindow::_init_image(std::tuple<I,I> z,std::tuple<I,I> p){m_ui_backend=message_ui_backend();m_dirty_rects.clear();const auto colour=message_wincolour(m_backlog_typed);const auto scaled_z=message_scr(z);const auto scaled_p=message_scr(p);const auto sw=std::get<0>(scaled_z),sh=std::get<1>(scaled_z);base::CWPySprite::m_image=surface(sw,sh,{static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(colour),0,255))});m_image=obj(base::CWPySprite::m_image);m_rect_noscale_typed={std::get<0>(p),std::get<1>(p),std::get<0>(z),std::get<1>(z)};m_rect_noscale=std::make_shared<RectValue>(RectValue{m_rect_noscale_typed});base::CWPySprite::m_rect={std::get<0>(scaled_p),std::get<1>(scaled_p),sw,sh};m_top_noscale=std::get<1>(z);m_bottom_noscale=0;draw_frame(base::CWPySprite::m_image,scaled_z,std::make_tuple<I,I>(0,0),m_backlog_typed);MessageWindow::m__back.reset();MessageWindow::m__fore.reset();}
void SelectWindow::update_scale(){
    _init_image({m_rect_noscale_typed.width,m_rect_noscale_typed.height},{m_rect_noscale_typed.x,m_rect_noscale_typed.y});
    m_rect=obj(std::make_shared<RectValue>(RectValue{base::CWPySprite::m_rect}));
    m_charimgs_typed=create_charimgs(std::make_tuple<I,I>(14,9),false);
    unregister_selection_bars(m_selections);
    m_selections.clear();
    m_is_drawing=true;
    m_frame=0;
    draw_all();
}
void SelectWindow::update(const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&){/* Python版もpass。選択窓はSelectionBar側だけを更新する。 */}
void SelectionBar::initialize(I showing,std::tuple<I,S> name,std::tuple<I,I> pos,std::tuple<I,I> size,bool backlog,bool selected){base::SelectableSprite::initialize({});m_ui_backend=message_ui_backend();m_selectable_on_event=true;m_showing_index_typed=showing;m_index_typed=std::get<0>(name);m_name_typed=std::get<1>(name);m_pos_noscale_typed={std::get<0>(pos),std::get<1>(pos)};m_size_noscale_typed={std::get<0>(size),std::get<1>(size)};m_backlog_typed=backlog;m_selected_typed=selected;m_backlog=backlog;m_index=m_index_typed;m_showing_index=showing;m_name=m_name_typed;const auto scaled_size=message_scr({m_size_noscale_typed.width,m_size_noscale_typed.height});const auto scaled_pos=message_scr({m_pos_noscale_typed.x,m_pos_noscale_typed.y});m_image_typed=get_image(scaled_size);m__image=obj(m_image_typed);m_image=obj(m_image_typed);base::SelectableSprite::m_image=m_image_typed;base::SelectableSprite::m_rect={std::get<0>(scaled_pos),std::get<1>(scaled_pos),std::get<0>(scaled_size),std::get<1>(scaled_size)};m_rect_noscale=std::make_shared<RectValue>(RectValue{{m_pos_noscale_typed.x,m_pos_noscale_typed.y,m_size_noscale_typed.width,m_size_noscale_typed.height}});m_status="normal";m_frame=0;
    const I area_h=message_scr(std::get<1>(::cw::g_SIZE_AREA));
    const I special=special_area()?1:0;
    if(m_backlog_typed){
        m_tlayer=::cw::g_LAYER_LOG_BAR;
        m_group=::cw::g_cwpy?(::cw::g_cwpy->m_backloggrp):V{};
    }else{
        m_tlayer=special?message_tlayer(::cw::g_LAYER_SPSELECTIONBAR_1):message_tlayer(::cw::g_LAYER_SELECTIONBAR_1);
        m_group=::cw::g_cwpy?(::cw::g_cwpy->m_cardgrp):V{};
    }
    if(!m_group.is_none()) register_layered_sprite(*this,m_group);
    if(area_h<=base::SelectableSprite::m_rect.y+base::SelectableSprite::m_rect.height && base::SelectableSprite::m_rect.y<=area_h){
        if(::cw::g_cwpy){
            const auto saved=m_tlayer;
            if(m_backlog_typed){
                if(::cw::g_cwpy->m_setting&&::cw::g_cwpy->m_setting->m_messagelog_type==::cw::setting::g_LOG_SINGLE){
                    m_tlayer={::cw::sprite::statusbar::g_LAYER_MESSAGE_LOG,0,0,0};
                    register_layered_sprite(*this,::cw::g_cwpy->m_sbargrp);
                }
            }else{
                m_tlayer={::cw::sprite::statusbar::g_LAYER_MESSAGE,0,0,0};
                register_layered_sprite(*this,::cw::g_cwpy->m_sbargrp);
            }
            m_tlayer=saved;
        }
    }
    if(area_h<base::SelectableSprite::m_rect.y){base::SelectableSprite::m_rect.height=0;if(auto rect=std::dynamic_pointer_cast<RectValue>(m_rect_noscale))rect->value.height=0;}
    if(m_ui_backend)m_ui_backend->invalidate(base::SelectableSprite::m_rect);
    sync_sprite_visual(*this);
}
std::shared_ptr<::cw::lib::Surface> SelectionBar::get_unselectedimage(){return m_image_typed;}std::shared_ptr<::cw::lib::Surface> SelectionBar::get_selectedimage(){return ::cw::imageretouch::to_negative(m_image_typed);}
bool SelectionBar::is_selection(){
    return !m_backlog_typed&&base::SelectableSprite::is_selection();
}

void MessageWindow::set_backlog_position(I y_noscale)
{
    const auto scaled_y = message_scr(y_noscale);
    m_rect_noscale_typed.y = y_noscale;
    if (auto rect = std::dynamic_pointer_cast<RectValue>(m_rect_noscale))
        rect->value = m_rect_noscale_typed;
    base::CWPySprite::m_rect.y = scaled_y;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_rect.storage());
        object && *object) {
        if (auto rect = std::dynamic_pointer_cast<RectValue>(*object))
            rect->value = base::CWPySprite::m_rect;
    }

    const I columns = std::max<I>(1, m_columns_typed);
    for (std::size_t index = 0; index < m_selections.size(); ++index) {
        const auto& bar = m_selections[index];
        if (!bar) continue;
        const I row = static_cast<I>(index) / columns;
        const I height = std::max<I>(0, bar->m_size_noscale_typed.height);
        bar->m_pos_noscale_typed.y =
            m_rect_noscale_typed.y + m_rect_noscale_typed.height + row * height;
        if (auto rect = std::dynamic_pointer_cast<RectValue>(bar->m_rect_noscale)) {
            rect->value = {bar->m_pos_noscale_typed.x, bar->m_pos_noscale_typed.y,
                           bar->m_size_noscale_typed.width, height};
        }
        bar->base::SelectableSprite::m_rect.y = message_scr(bar->m_pos_noscale_typed.y);
        bar->base::SelectableSprite::m_rect.height = message_scr(height);
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&bar->m_rect.storage());
            object && *object) {
            if (auto rect = std::dynamic_pointer_cast<RectValue>(*object))
                rect->value = bar->base::SelectableSprite::m_rect;
        }
        bar->m_tlayer = ::cw::g_LAYER_LOG_BAR;
        sync_sprite_visual(*bar);
    }
    sync_sprite_visual(*this);
}
void SelectionBar::update_scale(){/* Python版もpass。MessageWindow::update_scaleがSelectionBarを作り直す。 */}void SelectionBar::update(const ::cw::lib::Arguments&a,const ::cw::lib::KeywordArguments&k){if(m_backlog_typed)return;if(m_status=="normal"){
// Python と同様、ホバー操作全体を SelectableSprite/CWPy
// へ委ねる。 ここで画像を先に切り替えると change_selection()
// と競合し、保持型の合成で選択・非選択が交互に表示される。
base::SelectableSprite::update(a,k);}else update_click();}void SelectionBar::update_click(){
    const auto layer1=special_area()?message_tlayer(::cw::g_LAYER_SPSELECTIONBAR_1):message_tlayer(::cw::g_LAYER_SELECTIONBAR_1);
    const auto layer2=special_area()?message_tlayer(::cw::g_LAYER_SPSELECTIONBAR_2):message_tlayer(::cw::g_LAYER_SELECTIONBAR_2);
    if(::cw::g_cwpy){
        for(I i=0;i<static_cast<I>(::cw::g_cwpy->m_list.size());++i)
            if(::cw::g_cwpy->m_list[static_cast<std::size_t>(i)].get()==this){::cw::g_cwpy->m_index=i;break;}
        if(auto*handler=::cw::eventhandler::runtime_context()) handler->index=::cw::g_cwpy->m_index;
    }
    if(m_frame<6&&!m_clicking_typed){
        m_clicking_typed=true;m_status="click";
        base::SelectableSprite::m_rect.y+=message_scr(1);
        m_tlayer=layer2; sync_sprite_visual(*this);
    }else if(m_frame>=6){
        m_clicking_typed=false;m_status="normal";
        base::SelectableSprite::m_rect.y-=message_scr(1);
        m_frame=0; m_tlayer=layer1; sync_sprite_visual(*this); return;
    }
    ++m_frame;
    if(m_ui_backend)m_ui_backend->invalidate(base::SelectableSprite::m_rect);
}std::shared_ptr<::cw::lib::Surface> SelectionBar::get_image(std::tuple<I,I> z){auto colour=message_wincolour(m_backlog_typed);auto image=surface(std::get<0>(z),std::get<1>(z),{static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(colour),0,255)),static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(colour),0,255))});draw_frame(image,z,std::make_tuple<I,I>(0,0),m_backlog_typed);if(!m_ui_backend)m_ui_backend=message_ui_backend();const bool smooth=::cw::g_cwpy&&::cw::g_cwpy->m_setting?::cw::g_cwpy->m_setting->get_fontsmoothing_message():true;auto font=get_selectionbar_font();std::shared_ptr<::cw::lib::Surface> fg,sh;if(font){fg=decorate(font->render(m_name_typed,smooth,{255,255,255}),16,{255,255,255});sh=font->render(m_name_typed,smooth,{0,0,0});}else{const I label_pixels=message_scr(16);fg=decorate(m_ui_backend->render_text(m_name_typed,"MS UI Gothic",label_pixels,true,false,smooth,{255,255,255}),label_pixels,{255,255,255});sh=m_ui_backend->render_text(m_name_typed,"MS UI Gothic",label_pixels,true,false,smooth,{0,0,0});}const I max_w=std::get<0>(z)-message_scr(10);if(fg&&fg->size.width>max_w&&max_w>0){fg=::cw::image::smoothscale(fg,{max_w,fg->size.height},true,false);if(sh)sh=::cw::image::smoothscale(sh,{max_w,sh->size.height},true,false);}const I w=fg?fg->size.width:0,h=fg?fg->size.height:0,x=(message_scr(m_size_noscale_typed.width)-w)/2,y=(message_scr(m_size_noscale_typed.height)-h)/2;blend_surface(image,sh,x+1,y);blend_surface(image,sh,x-1,y);blend_surface(image,sh,x,y+1);blend_surface(image,sh,x,y-1);blend_surface(image,fg,x,y);return m_selected_typed?::cw::imageretouch::to_negative(image):image;}
void SelectionBar::lclick_event(){lclick_event(false);}
void SelectionBar::lclick_event(bool skip) {
    if (m_backlog_typed || !::cw::g_cwpy) return;
    const auto runtime = ::cw::g_cwpy;
    const auto w = runtime->get_messagewindow();
    if (!w || !*w || (*w)->m_selection_committing ||
        ((*w)->m_result && !(*w)->m_result->is_none())) return;
    // 入れ子のアニメーション入力ループをまたいで、実際に所有された選択肢を保持する。
    // 削除しない shared_ptr(this) では生存期間を維持できない。
    std::shared_ptr<SelectionBar> self;
    for (const auto& bar : (*w)->m_selections)
        if (bar.get() == this) { self = bar; break; }
    if (!self) return;
    (*w)->m_selection_committing = true;
    struct EndCommit {
        std::shared_ptr<MessageWindow> window;
        ~EndCommit() { window->m_selection_committing = false; }
    } commit{*w};
    if (!m_ui_backend) m_ui_backend = message_ui_backend();
    m_ui_backend->play_sound("click");
    if (!skip) {
        m_status = "click"; m_clicking_typed = false; m_frame = 0;
        (void)::cw::animation::animate_sprite(self, "click", true, false, false, false);
    }
    const auto current = runtime->get_messagewindow();
    if (::cw::g_cwpy != runtime || !current || *current != *w ||
        ((*w)->m_result && !(*w)->m_result->is_none())) return;
    auto member=std::dynamic_pointer_cast<MemberSelectWindow>(*w);if(member){if(m_index_typed>=0&&m_index_typed<static_cast<I>(member->m_selectmembers_typed.size())){if(auto e=events())e->set_selectedmember(member->m_selectmembers_typed[static_cast<std::size_t>(m_index_typed)]);(*w)->m_result=V(0);(*w)->m_result_typed=0;}else{(*w)->m_result=V(1);(*w)->m_result_typed=1;}}else{(*w)->m_result=V(m_index_typed);(*w)->m_result_typed=m_index_typed;}(*w)->m_showing_result=m_showing_index_typed;(*w)->m_showing_result_typed=m_showing_index_typed;
#if defined(CARDWIRTH_LOG_ENABLED)
::cardwirth::debug::log("Message selection: index=" + std::to_string(m_index_typed) + " text=" + m_name_typed + " result=" + std::to_string((*w)->m_result_typed.value_or(-1)));
#endif
}
void MemberSelectWindow::initialize(std::vector<std::shared_ptr<::cw::character::Character>> p,std::optional<std::tuple<I,I>> pos,std::optional<std::tuple<I,I>> size){m_selectmembers_typed=p;std::vector<std::tuple<I,S>> n;for(I i=0;i<static_cast<I>(p.size());++i)n.emplace_back(i,p[static_cast<std::size_t>(i)]?p[static_cast<std::size_t>(i)]->get_showingname():S{});n.emplace_back(static_cast<I>(n.size()),message("cancel","Cancel"));SelectWindow::initialize(std::move(n),message("select_member_message","Select member"),pos,size,false,std::nullopt,-1,1,false,std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt,false);}

V g__NameData{};I g__SP_CARD_NAME{0x10},g__SP_EXPAND_SHARPS{0x1},g__SP_FULL{0x2},g__SP_LOCAL_VARIABLES{0x8},g__SP_NO_SHARPS{0x4};S g__WAIT_CHARS{"、。，．？！｡､!?"};S g__WAIT_CHARS_BEFORE_SPACE{"・´｀：；ー―～…‥’”）〕］｝〉》」』】＞′″≫｣･\"'),.:;>]`}"};std::map<std::tuple<I,I,std::tuple<I,I,I>>,std::shared_ptr<::cw::lib::Surface>> g__decorate_cache{};::cw::lib::Real g__decorate_cache_upscr{};
I MessageWindow_create_charimgs_calc_w(std::tuple<std::shared_ptr<::cw::lib::Surface>,std::shared_ptr<::cw::image::ImageInfo>> p){return std::get<0>(p)?std::get<0>(p)->size.width:0;}void MessageWindow_create_charimgs_put_xinfo(I,I){/* 入れ子のヘルパーの処理は MessageWindow::create_charimgs に統合済み。 */}void MessageWindow_create_charimgs_put_topbottom(I,I){/* 入れ子のヘルパーの処理は MessageWindow::create_charimgs に統合済み。 */}std::tuple<::cw::lib::Real,bool,bool> MessageWindow_create_charimgs_add_wait(bool,bool){return{0.0,false,false};}
std::shared_ptr<::cw::lib::Surface> decorate(std::shared_ptr<::cw::lib::Surface> p,
    I angle, std::tuple<I,I,I> basecolour)
{
    if (!valid_surface(p)) return p;
    if (g__decorate_cache_upscr != ::cw::g_UP_SCR) {
        g__decorate_cache_upscr = ::cw::g_UP_SCR;
        g__decorate_cache.clear();
    }
    if (!(::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
          ::cw::g_cwpy->m_setting->get_decorationfont())) return p;
    if (!p->has_pixel_alpha && !p->surface_alpha_enabled) {
        // Py は RGB 入力を別のアルファ付き描画面へ変換する。ネイティブのパック RGBA
        // は変換後のピクセルに透過色を反映する。 Py の SRCALPHA フラグ
        // は全体アルファ付き RGB にも立つが、その場合は RGB のままとする。
        p = std::make_shared<::cw::lib::Surface>(*p);
        for (std::size_t n = 0; n + 3 < p->rgba.size(); n += 4) {
            const bool key = p->has_colorkey &&
                std::tuple<I,I,I>{p->rgba[n],p->rgba[n+1],p->rgba[n+2]} == p->colorkey;
            if (key) p->rgba[n] = p->rgba[n+1] = p->rgba[n+2] = p->rgba[n+3] = 0;
            else p->rgba[n+3] = p->surface_alpha_enabled ? p->surface_alpha : 255;
        }
        p->has_pixel_alpha = true;
        p->pixel_bits = 32;
        p->has_colorkey = false;
        p->surface_alpha = 255;
        p->surface_alpha_enabled = false;
    }
    const I h = p->size.height, mid = h / 2;
    const auto key = std::make_tuple(h, angle, basecolour);
    auto it = g__decorate_cache.find(key);
    if (it == g__decorate_cache.end()) {
        const std::array<I,3> base{std::get<0>(basecolour), std::get<1>(basecolour), std::get<2>(basecolour)};
        auto backing = surface(h, h);
        // Py と同様、range(1, mid) だけを減算する。 行 0 と
        // 奇数高の最後の行は基本色を保持し、高さ×高さの内部領域で列も制限する。
        for (I y = 0; y < h; ++y) {
            const I distance = std::abs(y - mid);
            I c = 0;
            if (distance > 0 && distance < mid) {
                c = std::max<I>(0, distance - message_scr(1)) * angle;
                if (::cw::g_UP_SCR != 1.0) c = static_cast<I>(double(c) / ::cw::g_UP_SCR);
                c = std::min<I>(255, c);
                if (c < 0) throw std::invalid_argument("negative decoration colour");
            }
            for (I x = 0; x < h; ++x) {
                const auto n = static_cast<std::size_t>((y * h + x) * 4);
                for (int k = 0; k < 3; ++k) {
                    const I bright = base[0] + base[1] + base[2] < 384 ? std::min<I>(255, base[k] + 16) : base[k];
                    backing->rgba[n+k] = static_cast<std::uint8_t>(std::clamp<I>(bright-c,0,255));
                }
                backing->rgba[n+3] = 255;
            }
        }
        it = g__decorate_cache.emplace(key, std::move(backing)).first;
    }
    const auto& backing = it->second;
    for (I y = 0; y < h; ++y) for (I x = 0; x < std::min<I>(p->size.width, h); ++x) {
        const auto to = static_cast<std::size_t>((y*p->size.width+x)*4);
        const auto from = static_cast<std::size_t>((y*h+x)*4);
        // Py と同様、RGBA_MIN は完全透明なピクセルの RGB にも作用する。
        for (int k = 0; k < 4; ++k) p->rgba[to+k] = std::min(p->rgba[to+k], backing->rgba[from+k]);
    }
    return p;
}
void draw_frame(std::shared_ptr<::cw::lib::Surface> image, std::tuple<I,I> size,
                std::optional<std::tuple<I,I>> pos, bool backlog)
{
    if (!image) return;
    // Py と同様、渡された画像自身の座標で描く。 公開引数の pos
    // は維持するが、この三つの枠の位置をずらすためには使わない。
    (void)pos;
    const auto frame = message_framecolour(backlog);
    const ::cw::lib::Color black{0,0,0,255}, middle{
        static_cast<std::uint8_t>(std::clamp<I>(std::get<0>(frame),0,255)),
        static_cast<std::uint8_t>(std::clamp<I>(std::get<1>(frame),0,255)),
        static_cast<std::uint8_t>(std::clamp<I>(std::get<2>(frame),0,255)),
        static_cast<std::uint8_t>(std::clamp<I>(std::get<3>(frame),0,255))};
    const auto box = [&](I inset, ::cw::lib::Color colour) {
        const auto points = get_pointlist(
            {std::get<0>(size)-inset, std::get<1>(size)-inset}, {inset,inset});
        std::apply([&](const auto&... point) {
            const std::array<std::tuple<I,I>,5> line{point...};
            for (std::size_t i=1; i<line.size(); ++i)
                draw_line(image, std::get<0>(line[i-1]), std::get<1>(line[i-1]),
                          std::get<0>(line[i]), std::get<1>(line[i]), colour);
        }, points);
    };
    box(0, black);
    box(std::get<I>(::cw::s(V(1)).storage()), middle);
    box(std::get<I>(::cw::s(V(2)).storage()), black);
}
std::tuple<std::tuple<I,I>,std::tuple<I,I>,std::tuple<I,I>,std::tuple<I,I>,std::tuple<I,I>>
get_pointlist(std::tuple<I,I> size, std::tuple<I,I> pos)
{
    const auto [x,y]=pos;
    const auto [w,h]=size;
    const auto edge=std::get<I>(::cw::s(V(1)).storage());
    return {{x,y},{x,h-edge},{w-edge,h-edge},{w-edge,y},{x,y}};
}

std::map<S,V> _create_nametable(I full_flags,std::optional<V> talker,bool show_card){std::map<S,V> t;auto e=events();auto selected=e&&e->has_selectedmember()?e->get_selectedmember():std::optional<std::shared_ptr<::cw::character::Character>>{};auto unselected=e?e->get_unselectedmember():std::optional<std::shared_ptr<::cw::character::Character>>{};t["#m"]=selected&&*selected?obj(*selected):V{};auto getter=std::make_shared<_NameGetter>();getter->initialize([](const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&)->V{auto e=events();auto r=e?e->get_randommember():std::optional<std::shared_ptr<::cw::character::Character>>{};return r&&*r?obj(*r):V{};});t["#r"]=obj(getter);t["#u"]=unselected&&*unselected?obj(*unselected):V{};std::shared_ptr<::cw::data::YadoData> yd;if(::cw::g_cwpy&&::cw::g_cwpy->m_ydata&&*::cw::g_cwpy->m_ydata)yd=*::cw::g_cwpy->m_ydata;t["#y"]=obj(yd);t["#t"]=yd&&yd->m_party&&*yd->m_party?obj(*yd->m_party):V{};if(full_flags||show_card){auto c=e?e->get_selectedcard():std::optional<std::shared_ptr<::cw::header::CardHeader>>{};t["#c"]=c&&*c?obj(*c):V{};}if(full_flags){t["#i"]=talker.value_or(V{});if(::cw::g_cwpy&&::cw::g_cwpy->m_rsrc){const auto* p=std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_specialchars.storage());if(p&&*p)if(auto special=std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*p))for(const auto&[key,value]:special->m_entries){(void)value;t.erase(key);}}}return t;}
std::optional<std::shared_ptr<::cw::character::Character>> _create_nametable_get_random(){auto e=events();return e?e->get_randommember():std::nullopt;}
void _reset_nametable(std::map<S,V> t){for(auto&[k,v]:t){(void)k;if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&v.storage());p&&*p)if(auto g=std::dynamic_pointer_cast<_NameGetter>(*p))g->reset();}}
std::tuple<I,S> _get_namefromlist(I i,std::vector<std::shared_ptr<NameListItem>> n){return i>=0&&static_cast<std::size_t>(i)<n.size()?std::make_tuple(i+1,item_name(n[static_cast<std::size_t>(i)])):std::make_tuple(i,S{});}
S _get_namefromtable(S nc,S,std::map<S,V> t,std::vector<std::shared_ptr<NameListItem>>& n){V d;auto i=t.find("#"+lower(nc));if(i!=t.end())d=i->second;if(const auto*p=std::get_if<::cw::lib::ObjectPtr>(&d.storage());p&&*p)if(auto g=std::dynamic_pointer_cast<_NameGetter>(*p))d=g->get_name();auto x=std::make_shared<NameListItem>();x->m_data=d;auto s=item_name(x);x->m_name=s;x->m_data_text=std::holds_alternative<S>(d.storage())?s:S{};x->m_name_text=s;n.push_back(x);return s;}

// Python の部分文字列による所属判定を維持し、空・一部分の SelectedPlayer
// 名も識別子の完全一致へ変えない。 汎用の ?? は空の Step
// 名とし、認識された SelectedPlayer/Player1
// ～6 の Step だけが識別子を保持する。
std::tuple<std::optional<std::shared_ptr<::cw::data::Step>>,I> _get_spstep(S name,I flags,S,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::vector<std::shared_ptr<NameListItem>>* out){auto d=sdata();
    // Python と同様、特殊ステップは実行中カードの WSN バージョンを使う。
    std::optional<S> card_version;
    if (auto e = events(); e && e->m_in_inusecardevent) {
        if (auto card = e->get_inusecard(); card && *card)
            card_version = to_string((*card)->m_wsnversion);
    }
    if(!d||!d->is_wsnversion("2",card_version))return{std::nullopt,index};const auto n=lower(name);if((flags&g__SP_NO_SHARPS)==0&&S("??selectedplayer").find(n)!=S::npos){const I v=_get_spstep_get_selectednum();if(base)set_base(base,index,V(v));else if(list){auto x=std::make_shared<NameListItem>();x->m_data="Number";x->m_name=V(v);x->m_name_text=std::to_string(v);list->push_back(x);if(out)*out=*list;}auto s=std::make_shared<::cw::data::Step>();s->m_name=name;s->m_value=v;s->m_valuenames={"0","1","2","3","4","5","6"};return{s,index+(base?1:0)};}/* Python と同様、_SP_NO_SHARPS を確認するのは ??SelectedPlayer だけとする。 ??Player1～6 と他の ?? 名は、expandsharps=False のメニューカード名でも展開する。 */if(n.rfind("??player",0)==0){I v=0;I party=::cw::g_cwpy?static_cast<I>(::cw::g_cwpy->m_pcards.size()):0;if(n.size()==9&&n[8]>='1'&&n[8]<='6'){const I idx=n[8]-'0';if(idx>=1&&idx<=6){if(idx<=party)v=idx;}}else if(n.rfind("??player",0)==0&&n.size()>8){/* ??PlayerABC と ??Player7 以降は、汎用の ?? の処理へ進む。 */}else{auto s=std::make_shared<::cw::data::Step>();s->m_name.clear();s->m_valuenames={""};return{s,index};}if(n.size()==9&&n[8]>='1'&&n[8]<='6'){auto s=std::make_shared<::cw::data::Step>();s->m_name=name;s->m_value=v;s->m_valuenames={""};if(::cw::g_cwpy)for(const auto& p : ::cw::g_cwpy->m_pcards)if(p)s->m_valuenames.push_back(p->get_showingname());return{s,index};}}if(n.rfind("??",0)==0){auto s=std::make_shared<::cw::data::Step>();s->m_name.clear();s->m_valuenames={""};return{s,index};}return{std::nullopt,index};}
std::optional<std::shared_ptr<::cw::data::Step>> get_spstep(S n){return std::get<0>(_get_spstep(n,g__SP_EXPAND_SHARPS,"Fixed",std::nullopt,std::nullopt,0));}I _get_spstep_get_selectednum(){if(!::cw::g_cwpy)return 0;auto e=events();auto selected=e&&e->has_selectedmember()?e->get_selectedmember():std::optional<std::shared_ptr<::cw::character::Character>>{};if(!selected||!*selected)return 0;for(I i=0;i<static_cast<I>(::cw::g_cwpy->m_pcards.size());++i){auto p=::cw::g_cwpy->m_pcards[static_cast<std::size_t>(i)];if(p&&std::static_pointer_cast<::cw::character::Player>(p)==*selected)return i+1;}return 0;}V _get_spstep_lambda_line_1719(V p){return V(to_string(p));}

namespace {
::cw::lib::Callback variable_resolver(int kind) {
    return [kind](const ::cw::lib::Arguments& a,const ::cw::lib::KeywordArguments&)->V {
        auto c=ctx(a);if(!c)return result(std::nullopt,0);
        const S key=a.empty()?S{}:to_string(a[0]);
        auto base=c->base_list?*c->base_list:std::optional<std::vector<std::shared_ptr<NameListItem>>>{};
        auto list=c->name_list?*c->name_list:std::vector<std::shared_ptr<NameListItem>>{};
        auto info=c->special_info?*c->special_info:std::optional<std::set<I>>{};
        auto invoke=[&](auto function){return function(key,c->full,c->update_type,*c->name_table,base,c->start_index,info,list,c->name_list_index,c->scenario_info,c->stack,c->name_list,c->special_info);};
        const auto r=kind==0?invoke(_get_stepvalue):kind==1?invoke(_get_flagvalue):invoke(_get_variantvalue);
        c->name_list_index=std::get<1>(r);return result(std::get<0>(r),c->name_list_index);
    };
}
void expand_variable_value(bool spchars,S& value,I& index,I flags,const S& update,std::map<S,V>& table,
    const std::optional<std::vector<std::shared_ptr<NameListItem>>>& base,I start,std::optional<std::set<I>>& info,
    std::optional<std::vector<std::shared_ptr<NameListItem>>>& list,const std::optional<std::tuple<S,S>>& scenario,I stack) {
    if(stack>0||!spchars)return;
    const auto r=replace(flags,update,value,table,variable_resolver(0),variable_resolver(1),variable_resolver(2),base,start,info,list,index,scenario,stack+1,list?&*list:nullptr);
    value=std::get<0>(r);info=std::get<1>(r);index=std::get<3>(r);
}
void publish_variable_names(const std::optional<std::vector<std::shared_ptr<NameListItem>>>& list,
                            std::vector<std::shared_ptr<NameListItem>>* out) {
    if (list && out) *out = *list;
}
}
std::tuple<std::optional<S>,I> _get_stepvalue(S key,I full_flags,S update,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::vector<std::shared_ptr<NameListItem>>* out,std::set<I>* out_info){
    auto s=find_step(key,(full_flags&g__SP_LOCAL_VARIABLES)!=0);
    if(!s){auto r=_get_spstep(key,full_flags,"All",base,list,index,list?&*list:nullptr);if(!std::get<0>(r))return{std::nullopt,std::get<1>(r)};s=*std::get<0>(r);index=std::get<1>(r);}
    S value;
    if(base&&index>=0&&index<static_cast<I>(base->size())){
        auto& remembered=(*base)[static_cast<std::size_t>(index)]->m_name;
        if(remembered.is_none())remembered=V(s->m_value);
        value=s->get_valuename(integer(remembered).value_or(s->m_value));
    }else{value=s->get_valuename(std::nullopt);if(list){auto x=std::make_shared<NameListItem>();x->m_data=obj(s);x->m_name=V(s->m_value);x->m_name_text=std::to_string(s->m_value);list->push_back(x);}}
    ++index;expand_variable_value(s->m_spchars,value,index,full_flags,update,table,base,start,info,list,scenario,stack);
    publish_variable_names(list,out);if(out_info&&info)*out_info=*info;return{value,index};
}
std::tuple<std::optional<S>,I> _get_flagvalue(S key,I full_flags,S update,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::vector<std::shared_ptr<NameListItem>>* out,std::set<I>* out_info){
    auto f=find_flag(key,(full_flags&g__SP_LOCAL_VARIABLES)!=0);if(!f)return{std::nullopt,index};
    std::optional<bool> old;
    if(base&&index>=0&&index<static_cast<I>(base->size())){
        auto& remembered=(*base)[static_cast<std::size_t>(index)]->m_name;
        if(remembered.is_none())remembered=V(f->m_value);
        old=boolean(remembered);
    }else if(list){auto x=std::make_shared<NameListItem>();x->m_data=obj(f);x->m_name=V(f->m_value);x->m_name_text=f->m_value?"True":"False";list->push_back(x);}
    S value=f->get_valuename(old);++index;expand_variable_value(f->m_spchars,value,index,full_flags,update,table,base,start,info,list,scenario,stack);
    publish_variable_names(list,out);if(out_info&&info)*out_info=*info;return{value,index};
}
std::tuple<std::optional<S>,I> _get_variantvalue(S key,I full_flags,S update,std::map<S,V> table,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack,std::vector<std::shared_ptr<NameListItem>>* out,std::set<I>* out_info){
    auto v=find_variant(key,_is_differentscenario(scenario),(full_flags&g__SP_LOCAL_VARIABLES)!=0);if(!v)return{std::nullopt,index};S value;
    if(base&&index>=0&&index<static_cast<I>(base->size())){auto& remembered=(*base)[static_cast<std::size_t>(index)]->m_name;if(remembered.is_none())remembered=v->m_value;value=::cw::data::Variant::value_to_str(remembered,false);}
    else{value=v->string_value();if(list){auto x=std::make_shared<NameListItem>();x->m_data=obj(v);x->m_name=v->m_value;x->m_name_text=value;list->push_back(x);}}
    publish_variable_names(list,out);if(out_info&&info)*out_info=*info;return{value,index+1};
}
bool _is_differentscenario(std::optional<std::tuple<S,S>> info){auto d=sdata();auto e=events();if(!d||!::cw::g_cwpy||!::cw::g_cwpy->is_playingscenario()||!e||!e->m_in_inusecardevent)return false;auto c=e->get_inusecard();if(!c||!*c)return false;auto x=info.value_or(std::make_tuple((*c)->m_scenario,(*c)->m_author));return std::make_tuple(to_string(d->m_name),to_string(d->m_author))!=x;}
std::tuple<S,std::set<I>,std::vector<std::shared_ptr<NameListItem>>,I> _rpl_specialstr(I f,S u,S s,std::map<S,V> t,::cw::lib::Callback a,::cw::lib::Callback b,::cw::lib::Callback c,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,I start,std::optional<std::set<I>> info,std::optional<std::vector<std::shared_ptr<NameListItem>>> list,I index,std::optional<std::tuple<S,S>> scenario,I stack){return replace(f,u,s,t,a,b,c,base,start,info,list,index,scenario,stack);}
std::tuple<I,I> _rpl_specialstr_get_varvalue(::cw::lib::Callback get,S c,I index){auto [s,i]=decode(get({V(c)},{}),index);return{s?1:0,i};}
std::tuple<S,std::vector<std::shared_ptr<NameListItem>>> rpl_specialstr(S s,std::optional<std::vector<std::shared_ptr<NameListItem>>> base,bool expand,S update,bool local,bool card,std::optional<std::tuple<S,S>> scenario){auto t=_create_nametable(0,std::nullopt,card);I f=expand?g__SP_EXPAND_SHARPS:g__SP_NO_SHARPS;if(local)f|=g__SP_LOCAL_VARIABLES;if(card)f|=g__SP_CARD_NAME;try{auto r=replace(f,update,s,t,variable_resolver(0),variable_resolver(1),variable_resolver(2),base,0,{},std::nullopt,0,scenario,0);return{std::get<0>(r),std::get<2>(r)};}catch(...){auto r=replace(f,update,s,t,variable_resolver(0),variable_resolver(1),variable_resolver(2),std::nullopt,0,{},std::nullopt,0,scenario,0);return{std::get<0>(r),std::get<2>(r)};}}

S get_messagelogtext(std::vector<V> values,bool last){std::vector<S> lines;for(std::size_t index=0;index<values.size();++index){const auto*p=std::get_if<::cw::lib::ObjectPtr>(&values[index].storage());if(!p||!*p)continue;if(auto bill=std::dynamic_pointer_cast<::cw::sprite::bill::Bill>(*p)){S detail;if(bill->m_header)detail=::cw::sprite::bill::get_detailtext(bill->m_header);if(index+1!=values.size())detail=rstrip_ws(std::move(detail));lines.push_back(detail);if(index+1==values.size())last=false;continue;}std::vector<std::tuple<I,S>> names;S textlog;S talker;bool talker_unspecified=true;I columns=1,showing=-1;std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>,bool,std::optional<V>,std::map<I,std::shared_ptr<::cw::lib::Surface>>>> paths;if(auto w=std::dynamic_pointer_cast<MessageWindow>(*p)){names=w->m_names_log_typed;textlog=w->m_text_log;talker_unspecified=!w->m_talker_name.has_value();talker=w->m_talker_name.value_or(S{});columns=w->m_columns_typed;showing=w->m_showing_result_typed;paths=w->m_imgpaths_typed;}else if(auto d=std::dynamic_pointer_cast<BacklogData>(*p)){names=d->m_names_log_typed;textlog=to_string(d->m_text_log);talker_unspecified=!d->m_talker_name_typed.has_value();talker=d->m_talker_name_typed.value_or(S{});columns=d->m_columns_typed;showing=d->m_showing_result_typed;paths=d->m_imgpaths_typed;}else continue;if(talker_unspecified)for(const auto&entry:paths)if(auto info=std::get<0>(entry);info&&!info->m_path.empty()){if(!talker.empty())talker+=' ';talker+=::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(info->m_path).filename());}S h=talker.empty()?"--":"--< "+talker+" >--";if(::cw::util::get_strlen(h)<::cw::g_LOG_SEPARATOR_LEN_SHORT)h+=S(static_cast<std::size_t>(::cw::g_LOG_SEPARATOR_LEN_SHORT-::cw::util::get_strlen(h)),'-');lines.push_back(h);textlog=strip_lf(std::move(textlog));lines.push_back(textlog);const bool default_ok=names.size()==1&&columns==1&&std::get<1>(names.front())==message("ok","OK");if(!names.empty()&&!default_ok){lines.emplace_back();for(std::size_t j=0;j<names.size();++j){S row=(static_cast<I>(j)==showing&&names.size()>1?">>[ ":"  [ ")+std::get<1>(names[j])+" ";if(::cw::util::get_strlen(row)<::cw::g_LOG_SEPARATOR_LEN_SHORT-1)row+=S(static_cast<std::size_t>(::cw::g_LOG_SEPARATOR_LEN_SHORT-1-::cw::util::get_strlen(row)),' ');lines.push_back(row+"]");}}}if(last){lines.emplace_back(S(static_cast<std::size_t>(::cw::g_LOG_SEPARATOR_LEN_SHORT),'-'));lines.emplace_back();}S out;for(std::size_t i=0;i<lines.size();++i){if(i)out+='\n';out+=lines[i];}return out;}
void update_scenariopath_for_log(S norm,S dst){
    auto rewrite_info=[&](std::shared_ptr<::cw::image::ImageInfo> info){
        if(!info||info->m_path.empty()||::cw::binary::image::path_is_code(info->m_path))return;
        const S rel=::cw::util::is_descendant(info->m_path,norm);
        if(!rel.empty())info->m_path=::cw::util::join_paths({V(dst),V(rel)});
    };
    auto rewrite_window=[&](MessageWindow* w){
        if(!w)return;
        for(auto& i:w->m_imgpaths_typed)rewrite_info(std::get<0>(i));
        update_scenariopath_for_spchars(w->m_specialchars_typed?obj(w->m_specialchars_typed):V{},norm,dst);
    };
    auto rewrite_log=[&](const std::shared_ptr<BacklogData>& log){
        if(!log)return;
        for(auto& i:log->m_imgpaths_typed)rewrite_info(std::get<0>(i));
        update_scenariopath_for_spchars(log->m_specialchars_typed?obj(log->m_specialchars_typed):V{},norm,dst);
    };
    std::unordered_set<MessageWindow*> rewritten;
    auto rewrite_once=[&](MessageWindow* w){if(w&&rewritten.insert(w).second)rewrite_window(w);};
    if(::cw::g_cwpy&&::cw::g_cwpy->is_showingmessage()){
        auto w=::cw::g_cwpy->get_messagewindow();
        if(w&&*w) rewrite_once(w->get());
    }
    // ネイティブ版は CWPy の現在ウィンドウ枠の外にも、ホスト所有の表示を
    // 登録している。 バックログの別名も含め、各ウィンドウを一度だけ処理する。
    for(auto*w:windows) rewrite_once(w);
    if(auto data=sdata())for(auto& value:data->m_backlog){
        const auto*p=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if(!p||!*p)continue;
        if(std::dynamic_pointer_cast<::cw::sprite::bill::Bill>(*p)) continue;
        rewrite_log(std::dynamic_pointer_cast<BacklogData>(*p));
        if(auto w=std::dynamic_pointer_cast<MessageWindow>(*p)) rewrite_once(w.get());
    }
}
void update_scenariopath_for_spchars(V restbl,S norm,S dst){auto table=get_resources(restbl);if(!table)return;table->reset();for(auto&[key,lazy]:table->m_entries){(void)key;if(!lazy||lazy->m_arglist.empty())continue;auto path=to_string(lazy->m_arglist.front());if(path.empty())continue;const S rel=::cw::util::is_descendant(path,norm);if(!rel.empty())lazy->m_arglist.front()=V(::cw::util::join_paths({V(dst),V(rel)}));}}
void store_messagelogimage(S path,bool can_scaled)
{
    if(::cw::g_fsync) ::cw::g_fsync->sync();
    stored_log_image={};
    if(!::cw::g_cwpy)return;
    const auto normalize=[](S value){return lower(::cw::util::normalize_path(std::move(value)));};
    if(const auto paths=::cw::util::resolve_yado_paths(path,::cw::g_cwpy->m_yadodir,::cw::g_cwpy->m_tempdir))
        path=paths->first;
    std::optional<std::map<I,std::shared_ptr<::cw::lib::Surface>>> loaded, special_loaded;
    auto load_with_scaled=[&](std::optional<std::map<I,std::shared_ptr<::cw::lib::Surface>>> d,std::map<I,std::shared_ptr<::cw::lib::Surface>>& scaled,bool can_scaled2)->std::map<I,std::shared_ptr<::cw::lib::Surface>>{
        scaled.clear();
        if(d){scaled=*d;return scaled;}
        S fpath=path;
        if(!::cw::binary::image::path_is_code(fpath)){
            const S lpath=lower(fpath);
            if((!::cw::g_cwpy->m_yadodir.empty()&&lpath.rfind(lower(::cw::g_cwpy->m_yadodir),0)==0)||
               (!::cw::g_cwpy->m_tempdir.empty()&&lpath.rfind(lower(::cw::g_cwpy->m_tempdir),0)==0))
                fpath=::cw::util::get_yadofilepath(fpath);
        }
        scaled[1]=::cw::util::load_image(fpath,true,{0,0},std::nullopt,true,false,can_scaled2,true,std::nullopt,false);
        mark_scaled_surface(scaled[1],1);
        if(can_scaled2){
            const auto split=::cw::util::splitext(fpath);
            for(const I scale:{std::get<0>(::cw::g_SCALE_LIST),std::get<1>(::cw::g_SCALE_LIST),std::get<2>(::cw::g_SCALE_LIST),std::get<3>(::cw::g_SCALE_LIST)}){
                const S candidate=std::get<0>(split)+".x"+std::to_string(scale)+std::get<1>(split);
                std::error_code ec;if(std::filesystem::is_regular_file(std::filesystem::u8path(candidate),ec)&&!ec){
                    scaled[scale]=::cw::util::load_image(candidate,true,{0,0},std::nullopt,true,false,false,true,std::nullopt,false);
                    mark_scaled_surface(scaled[scale],scale);
                }
            }
        }
        if(!scaled.empty())stored_log_image={scaled.contains(1)?scaled.at(1):scaled.begin()->second,can_scaled2};
        return scaled;
    };
    auto load_entry=[&](auto& entry){
        auto info=std::get<0>(entry);if(!info||normalize(info->m_path)!=normalize(path))return;
        auto& scaled=std::get<3>(entry);
        loaded=load_with_scaled(loaded,scaled,std::get<1>(entry));
        std::get<1>(entry)=can_scaled;
    };
    auto bind_special=[&](const std::shared_ptr<::cw::setting::ResourceTable>& table,bool old_can_scaled){
        if(!table||!::cw::g_cwpy->is_playingscenario())return;
        const S scedir=sdata()?sdata()->m_scedir:S{};
        for(const auto& key:table->iterkeys()){
            const auto name=to_string(key);
            if(name.size()<2)continue;
            auto it=table->m_entries.find(name);
            if(it==table->m_entries.end()||!it->second||it->second->m_arglist.empty())continue;
            S fpath=S("font_")+name.substr(1)+".bmp";
            fpath=::cw::util::get_materialpath(fpath,::cw::g_M_IMG,scedir,false,true);
            const bool match=normalize(fpath)==normalize(path)||normalize(to_string(it->second->m_arglist.front()))==normalize(path);
            if(!match)continue;
            auto scaled=std::make_shared<std::map<I,std::shared_ptr<::cw::lib::Surface>>>();
            special_loaded=load_with_scaled(special_loaded,*scaled,old_can_scaled);
            auto cb=[scaled](const ::cw::lib::Arguments&,const ::cw::lib::KeywordArguments&)->V{
                auto image=pick_scaled_talker(*scaled);
                auto seq=std::make_shared<::cw::data::VariantSequence>();
                seq->m_values={obj(std::make_shared<SurfaceValue>(image)),V(true)};
                return obj(seq);
            };
            table->set(V(name),cb,{},{});
        }
    };
    if(auto data=sdata())for(auto& value:data->m_backlog){
        const auto*p=std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if(!p||!*p)continue;
        if(std::dynamic_pointer_cast<::cw::sprite::bill::Bill>(*p))continue;
        if(auto log=std::dynamic_pointer_cast<BacklogData>(*p)){
            for(auto& entry:log->m_imgpaths_typed){const bool old_can_scaled=std::get<1>(entry);load_entry(entry);bind_special(log->m_specialchars_typed,old_can_scaled);}
        }
        if(auto w=std::dynamic_pointer_cast<MessageWindow>(*p)){
            for(auto& entry:w->m_imgpaths_typed){const bool old_can_scaled=std::get<1>(entry);load_entry(entry);bind_special(w->m_specialchars_typed,old_can_scaled);}
        }
    }
    // ホスト所有の登録済みウィンドウにも、バッ
    // クログ記録と同じ画像保持の規則を使う。
    for(auto*w:windows)if(w){
        for(auto&entry:w->m_imgpaths_typed){const bool old_can_scaled=std::get<1>(entry);load_entry(entry);bind_special(w->m_specialchars_typed,old_can_scaled);}
    }
}
std::map<I,std::shared_ptr<::cw::lib::Surface>> store_messagelogimage_load_with_scaled(std::optional<std::map<I,std::shared_ptr<::cw::lib::Surface>>> d,std::map<I,std::shared_ptr<::cw::lib::Surface>> scaled){if(d)scaled=*d;return scaled;}std::tuple<std::shared_ptr<::cw::lib::Surface>,bool> store_messagelogimage_load(){return stored_log_image;}void main(){/* Python 版も何もしない単体実行用エントリーポイント。 */}
}
