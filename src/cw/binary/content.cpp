#include "cw/binary/content.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/bgimage.hpp"
#include "cw/binary/coupon.hpp"
#include "cw/binary/dialog.hpp"
#include "cw/binary/effectmotion.hpp"
#include "cw/data.hpp"

#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace cw::binary::content {
namespace {
std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> parent_base(const ::cw::lib::Value& value) {
    if (const auto* object=std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        if (auto base=std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object)) return base;
    }
    return std::nullopt;
}
::cw::lib::Value self_value(Content& self) {
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(self.shared_from_this()));
}
struct Header { ::cw::lib::String tag,type,name; ::cw::lib::Integer version,count; };
Header read_header(Content& self,const std::shared_ptr<::cw::binary::cwfile::CWFile>& f) {
    auto [tag,type]=self.conv_contenttype(f->byte());
    auto name=f->string(); auto count=f->dword(); ::cw::lib::Integer version;
    if(count<=39999)version=2;else if(count<=49999){version=4;count-=40000;}else{version=5;count-=50000;}
    // 記録のプロパティを読む前に、不正な負の子数を拒否する。 Python
    // の range(negative_count) は子を読まずに続ける
    // が、 ネイティブ版では壊れたヘッダーを有効な空ツリーとして扱わない。
    if(count<0) throw std::invalid_argument("negative Content child count");
    return {tag,type,name,version,count};
}
::cw::lib::String value_string(const ::cw::lib::Value& v){const auto& s=v.storage();if(auto p=std::get_if<::cw::lib::String>(&s))return *p;if(auto p=std::get_if<::cw::lib::Integer>(&s))return std::to_string(*p);if(auto p=std::get_if<::cw::lib::Real>(&s))return std::to_string(*p);if(auto p=std::get_if<::cw::lib::Decimal>(&s))return p->to_string(true);if(auto p=std::get_if<bool>(&s))return *p?"True":"False";return {};}
::cw::lib::String attr(const std::shared_ptr<::cw::data::CWPyElement>& e,const ::cw::lib::String& k,const ::cw::lib::String& d={}){if(!e)throw std::invalid_argument("null element");if(auto it=e->m_attributes.find(k);it!=e->m_attributes.end())return it->second;return d;}
bool boolean(const ::cw::lib::String& s){auto normalized=s;for(auto& c:normalized)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(normalized=="true"||normalized=="1")return true;if(normalized=="false"||normalized=="0")return false;throw std::invalid_argument("invalid bool: "+s);}
::cw::lib::Integer integer(const ::cw::lib::String& s) {
    return ::cw::lib::require_python_integer(s);
}
std::shared_ptr<::cw::data::CWPyElement> child(const std::shared_ptr<::cw::data::CWPyElement>& e,const ::cw::lib::String& tag){if(e)for(auto& x:e->m_children)if(x&&x->m_tag==tag)return x;return {};}
}

void Content::initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, ::cw::lib::Integer arg_stratum, bool arg_read)
{
    ::cw::binary::base::CWBinaryBase::initialize(parent_base(arg_parent),arg_f);
    m_xmltype="Content"; m_children.clear(); m_bgimgs.clear();m_coupons.clear();m_dialogs.clear();m_motions.clear();m_castranges.clear();m_properties.clear();m_data.reset();m_text.clear();
    if(!arg_read)return;
    if(!arg_f)throw std::invalid_argument("Content requires CWFile");
    if(arg_stratum<=255){
        const auto h=read_header(*this,arg_f);
        const auto parent=self_value(*this);
        for(::cw::lib::Integer i=0;i<h.count;++i){auto child=std::make_shared<Content>();child->initialize(parent,arg_f,arg_stratum+1,true);m_children.push_back(std::move(child));}
        _read_properties(arg_f,h.tag,h.type,h.name,h.version);return;
    }
    std::vector<Header> stack; std::vector<std::shared_ptr<Content>> children;
    while(true){auto h=read_header(*this,arg_f);stack.push_back(h);if(h.count==0)break;if(h.count==1)continue;
        const auto parent=self_value(*this);for(::cw::lib::Integer i=0;i<h.count;++i){auto child=std::make_shared<Content>();child->initialize(parent,arg_f,arg_stratum,true);children.push_back(std::move(child));}break;}
    for(std::size_t i=0;i<stack.size();++i){const auto& h=stack[stack.size()-1-i];std::shared_ptr<Content> e;
        if(i+1==stack.size())e=std::static_pointer_cast<Content>(shared_from_this());else{e=std::make_shared<Content>();e->initialize(self_value(*this),arg_f,arg_stratum,false);}
        e->_read_properties(arg_f,h.tag,h.type,h.name,h.version);e->m_children=children;children={e};}
}

void Content::_read_properties(std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, ::cw::lib::String arg_tag, ::cw::lib::String arg_ctype, ::cw::lib::String arg_name, ::cw::lib::Integer arg_version)
{
    if(!arg_f)throw std::invalid_argument("Content properties require CWFile");
    m_tag=std::move(arg_tag);m_type=std::move(arg_ctype);m_name=std::move(arg_name);m_version=arg_version;
    if(m_version>=5)(void)arg_f->dword();
    // 型付きの補助フィールドは、全コンテント種類に存在する。 再読
    // み込み時に消し、この種類にない項目へ以前の値が漏れないように
    // する。 Python版は該当タグの分岐でだけ属性を設定し、
    // 直接の再読み込みでは古い属性・キャッシュが残る場合がある。
    m_properties.clear();m_bgimgs.clear();m_castranges.clear();m_coupons.clear();m_dialogs.clear();m_motions.clear();m_data.reset();m_text.clear();
    const auto parent=self_value(*this);
    auto item=[&]{m_properties["id"]=arg_f->dword();if(m_version<=2){m_properties["number"]=1;m_properties["targets"]=conv_target_scope(4);}else{m_properties["number"]=arg_f->dword();m_properties["targets"]=conv_target_scope(arg_f->byte());}};
    if(m_tag=="Start"&&m_type==""){}
    else if(m_tag=="Link"&&m_type=="Start")m_properties["link"]=arg_f->string();
    else if(m_tag=="Start"&&m_type=="Battle")m_properties["id"]=arg_f->dword();
    else if(m_tag=="End"&&m_type=="")m_properties["complete"]=arg_f->boolean();
    else if(m_tag=="End"&&m_type=="BadEnd"){}
    else if(m_tag=="Change"&&m_type=="Area")m_properties["id"]=arg_f->dword();
    else if(m_tag=="Talk"&&m_type=="Message"){m_properties["path"]=get_materialpath(arg_f->string());m_text=arg_f->string(true);}
    else if(m_tag=="Play"&&(m_type=="Bgm"||m_type=="Sound"))m_properties["path"]=get_materialpath(arg_f->string());
    else if(m_tag=="Change"&&m_type=="BgImage"){const auto n=arg_f->dword();for(::cw::lib::Integer i=0;i<n;++i){auto x=std::make_shared<::cw::binary::bgimage::BgImage>();x->initialize(parent,arg_f,false);m_bgimgs.push_back(x);}}
    else if(m_tag=="Wait"&&m_type=="")m_properties["value"]=arg_f->dword();
    else if(m_tag=="Effect"&&m_type==""){m_properties["level"]=arg_f->dword();m_properties["targetm"]=conv_target_member(arg_f->byte());m_properties["effecttype"]=conv_card_effecttype(arg_f->byte());m_properties["resisttype"]=conv_card_resisttype(arg_f->byte());m_properties["successrate"]=arg_f->dword();m_properties["sound"]=get_materialpath(arg_f->string());m_properties["visual"]=conv_card_visualeffect(arg_f->byte());m_properties["ignite"]=false;const auto n=arg_f->dword();for(::cw::lib::Integer i=0;i<n;++i){auto x=std::make_shared<::cw::binary::effectmotion::EffectMotion>();x->initialize(parent,arg_f,false,m_version);m_motions.push_back(x);}}
    else if(m_tag=="Branch"&&m_type=="Select"){m_properties["targetall"]=arg_f->boolean();m_properties["method"]=arg_f->boolean() ? ::cw::lib::Value("Random") : ::cw::lib::Value("Manual");}
    else if(m_tag=="Branch"&&m_type=="Ability"){m_properties["value"]=arg_f->dword();m_properties["targetm"]=conv_target_member(arg_f->byte());m_properties["physical"]=conv_card_physicalability(arg_f->dword());m_properties["mental"]=conv_card_mentalability(arg_f->dword());}
    else if(m_tag=="Branch"&&m_type=="Random")m_properties["value"]=arg_f->dword();
    else if(m_tag=="Branch"&&m_type=="Flag")m_properties["flag"]=arg_f->string();
    else if(m_tag=="Set"&&m_type=="Flag"){m_properties["flag"]=arg_f->string();m_properties["value"]=arg_f->boolean();}
    else if(m_tag=="Branch"&&m_type=="MultiStep")m_properties["step"]=arg_f->string();
    else if(m_tag=="Set"&&m_type=="Step"){m_properties["step"]=arg_f->string();m_properties["value"]=arg_f->dword();}
    else if(m_tag=="Branch"&&m_type=="Cast")m_properties["id"]=arg_f->dword();
    else if(m_tag=="Branch"&&(m_type=="Item"||m_type=="Skill"||m_type=="Beast"))item();
    else if(m_tag=="Branch"&&m_type=="Info")m_properties["id"]=arg_f->dword();
    else if(m_tag=="Branch"&&m_type=="Money")m_properties["value"]=arg_f->dword();
    else if(m_tag=="Branch"&&m_type=="Coupon"){m_properties["coupon"]=arg_f->string();(void)arg_f->dword();m_properties["targets"]=conv_target_scope_coupon(arg_f->byte());}
    else if(m_tag=="Get"&&m_type=="Cast"){m_properties["id"]=arg_f->dword();m_properties["startaction"]="NextRound";}
    else if((m_tag=="Get"||m_tag=="Lose")&&(m_type=="Item"||m_type=="Skill"||m_type=="Beast"))item();
    else if((m_tag=="Get"||m_tag=="Lose")&&m_type=="Info")m_properties["id"]=arg_f->dword();
    else if((m_tag=="Get"||m_tag=="Lose")&&m_type=="Money")m_properties["value"]=arg_f->dword();
    else if(m_tag=="Get"&&m_type=="Coupon"){m_properties["coupon"]=arg_f->string();m_properties["value"]=arg_f->dword();m_properties["targets"]=conv_target_scope(arg_f->byte());}
    else if(m_tag=="Lose"&&m_type=="Coupon"){m_properties["coupon"]=arg_f->string();(void)arg_f->dword();m_properties["targets"]=conv_target_scope(arg_f->byte());}
    else if(m_tag=="Lose"&&m_type=="Cast")m_properties["id"]=arg_f->dword();
    else if(m_tag=="Talk"&&m_type=="Dialog"){const auto member=conv_target_member_dialog(arg_f->byte());m_properties["targetm"]=member;if(member=="Valued"){const auto n=arg_f->dword();for(::cw::lib::Integer i=0;i<n;++i){auto x=std::make_shared<::cw::binary::coupon::Coupon>();x->initialize(std::static_pointer_cast<::cw::binary::base::CWBinaryBase>(shared_from_this()),arg_f,false,5);m_coupons.push_back(x);}if(!m_coupons.empty()&&m_coupons.front()->m_name.empty()){m_properties["initialValue"]=m_coupons.front()->m_value;m_coupons.erase(m_coupons.begin());}else m_properties["initialValue"]=0;}const auto n=arg_f->dword();for(::cw::lib::Integer i=0;i<n;++i){auto x=std::make_shared<::cw::binary::dialog::Dialog>();x->initialize(parent,arg_f,false);m_dialogs.push_back(x);}}
    else if(m_tag=="Set"&&(m_type=="StepUp"||m_type=="StepDown"))m_properties["step"]=arg_f->string();
    else if(m_tag=="Reverse"&&m_type=="Flag")m_properties["flag"]=arg_f->string();
    else if(m_tag=="Branch"&&m_type=="Step"){m_properties["step"]=arg_f->string();m_properties["value"]=arg_f->dword();}
    else if(m_tag=="Branch"&&m_type=="Level"){m_properties["average"]=arg_f->boolean();m_properties["value"]=arg_f->dword();}
    else if(m_tag=="Branch"&&m_type=="Status"){m_properties["status"]=conv_statustype(arg_f->byte());m_properties["targetm"]=conv_target_member(arg_f->byte());}
    else if(m_tag=="Branch"&&m_type=="PartyNumber")m_properties["value"]=arg_f->dword();
    else if(m_tag=="Call"&&m_type=="Start")m_properties["call"]=arg_f->string();
    else if(m_tag=="Link"&&m_type=="Package")m_properties["link"]=arg_f->dword();
    else if(m_tag=="Call"&&m_type=="Package")m_properties["call"]=arg_f->dword();
    else if((m_tag=="Branch"||m_tag=="Get"||m_tag=="Lose")&&m_type=="CompleteStamp")m_properties["scenario"]=arg_f->string();
    else if((m_tag=="Branch"||m_tag=="Get"||m_tag=="Lose")&&m_type=="Gossip")m_properties["gossip"]=arg_f->string();
    else if(m_tag=="Check"&&m_type=="Flag")m_properties["flag"]=arg_f->string();
    else if((m_tag=="Substitute"&&(m_type=="Step"||m_type=="Flag"))||(m_tag=="Branch"&&(m_type=="StepValue"||m_type=="FlagValue"))){m_properties["from"]=arg_f->string();m_properties["to"]=arg_f->string();}
    else if(m_tag=="Branch"&&m_type=="RandomSelect"){m_castranges=conv_castranges(arg_f->byte());const auto style=arg_f->byte();if(style&1){m_properties["minLevel"]=arg_f->dword();m_properties["maxLevel"]=arg_f->dword();}if(style&2)m_properties["status"]=conv_statustype(arg_f->byte());}
    else if(m_tag=="Branch"&&m_type=="KeyCode"){m_properties["targetkc"]=conv_keycoderange(arg_f->byte());const auto t=conv_effectcardtype(arg_f->byte());m_properties["skill"]=t=="All"||t=="Skill";m_properties["item"]=t=="All"||t=="Item";m_properties["beast"]=t=="All"||t=="Beast";m_properties["hand"]=t=="All"||t=="Item";m_properties["keyCode"]=arg_f->string();}
    else if(m_tag=="Check"&&m_type=="Step"){m_properties["step"]=arg_f->string();m_properties["value"]=arg_f->dword();m_properties["comparison"]=conv_comparison4(arg_f->byte());}
    else if(m_tag=="Branch"&&m_type=="Round"){m_properties["comparison"]=conv_comparison3(arg_f->byte());m_properties["round"]=arg_f->dword();}
    else if(!((m_tag=="Elapse"&&m_type=="Time")||(m_tag=="Show"&&m_type=="Party")||(m_tag=="Hide"&&m_type=="Party")||(m_tag=="Effect"&&m_type=="Break")||(m_tag=="Branch"&&(m_type=="Area"||m_type=="Battle"||m_type=="IsBattle"))||(m_tag=="Redisplay"&&m_type=="")))throw std::invalid_argument(m_tag+", "+m_type);
}

std::shared_ptr<::cw::data::CWPyElement> Content::get_data()
{
    if(m_data&&*m_data)return *m_data;
    std::shared_ptr<::cw::data::CWPyElement> line;auto child=std::static_pointer_cast<Content>(shared_from_this());
    while(true){auto data=::cw::data::make_element(child->m_tag);child->m_data=data;if(!child->m_type.empty())data->set("type",child->m_type);data->set("name",child->m_name);for(const auto& k:child->m_properties.insertion_keys())data->set(k,value_string(child->m_properties.at(k)));
        if(child->m_tag=="Talk"&&child->m_type=="Message")data->append(::cw::data::make_element("Text",child->m_text));
        else if(child->m_tag=="Change"&&child->m_type=="BgImage"){auto e=::cw::data::make_element("BgImages");for(auto& x:child->m_bgimgs)if(x)e->append(x->get_data());data->append(e);}
        else if(child->m_tag=="Effect"&&child->m_type==""){auto e=::cw::data::make_element("Motions");for(auto& x:child->m_motions)if(x)e->append(x->get_data());data->append(e);}
        else if(child->m_tag=="Talk"&&child->m_type=="Dialog"){if(auto it=child->m_properties.find("targetm");it!=child->m_properties.end()&&value_string(it->second)=="Valued"){auto e=::cw::data::make_element("Coupons");for(auto& x:child->m_coupons)if(x)e->append(x->get_data());data->append(e);}auto e=::cw::data::make_element("Dialogs");for(auto& x:child->m_dialogs)if(x)e->append(x->get_data());data->append(e);}
        else if(child->m_tag=="Branch"&&child->m_type=="RandomSelect"){auto e=::cw::data::make_element("CastRanges");for(const auto& x:child->m_castranges)e->append(::cw::data::make_element("CastRange",x));data->append(e);}
        if(line)line->append(data);if(child->m_children.size()==1){if(!line){line=::cw::data::make_element("ContentsLine");line->append(data);}child=child->m_children.front();continue;}if(!child->m_children.empty()){auto e=::cw::data::make_element("Contents");for(auto& x:child->m_children)if(x)e->append(x->get_data());data->append(e);}break;}
    return line?line:(m_data?*m_data:nullptr);
}

void Content::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if(!arg_f||!arg_data)throw std::invalid_argument("Content::unconv null argument");const auto& c=arg_data->m_children;
    if(arg_data->m_tag=="ContentsLine"){if(c.empty())throw std::out_of_range("empty ContentsLine");for(std::size_t i=0;i+1<c.size();++i){_unconv_header(arg_f,c[i]);arg_f->write_dword(50001);}unconv(arg_f,c.back());for(std::size_t i=c.size()-1;i>0;--i)_unconv_properties(arg_f,c[i-1]);return;}
    _unconv_header(arg_f,arg_data);bool found=false;for(const auto& x:c)if(x&&x->m_tag=="Contents"){arg_f->write_dword(static_cast<::cw::lib::Integer>(x->m_children.size())+50000);for(const auto& y:x->m_children)unconv(arg_f,y);found=true;break;}if(!found)arg_f->write_dword(50000);_unconv_properties(arg_f,arg_data);
}

void Content::_unconv_header(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if(!arg_f||!arg_data)throw std::invalid_argument("Content header null argument");arg_f->write_byte(::cw::binary::base::CWBinaryBase::unconv_contenttype(arg_data->m_tag,attr(arg_data,"type"),arg_f));arg_f->write_string(attr(arg_data,"name"));
}

void Content::_unconv_properties(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if(!arg_f||!arg_data)throw std::invalid_argument("Content properties null argument");using Base=::cw::binary::base::CWBinaryBase;arg_f->write_dword(4);const auto& tag=arg_data->m_tag;const auto type=attr(arg_data,"type");
    auto dword=[&](const char* k){arg_f->write_dword(integer(attr(arg_data,k)));};auto string=[&](const char* k){arg_f->write_string(attr(arg_data,k));};
    auto has_attr=[&](const char* k){return arg_data->m_attributes.find(k)!=arg_data->m_attributes.end();};
    // Pythonの getbool(..., False) と同様、任意属性の未存在・空文字列はFalseにする。
    auto bool_attr=[&](const char* k,bool def=false){return arg_data->getbool(".", ::cw::lib::Value(::cw::lib::String(k)), def);};
    // 旧形式のKeyCodeフラグは、既定値なしの getbool に合わせ、空文字列をエラーにする。
    auto required_bool_attr=[&](const char* k){return arg_data->getbool(".", ::cw::lib::Value(::cw::lib::String(k)), std::nullopt);};
    auto nondefault=[&](const char* k,const ::cw::lib::String& def= "Default"){return attr(arg_data,k,def)!=def;};
    auto lower_ascii=[](::cw::lib::String value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
    if(tag=="Show"&&type=="Party"){if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");return;}
    if(tag=="Hide"&&type=="Party"){if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");return;}
    // Pythonの getbool("consumecard", True) と同様、ルート属性ではなく子のテキストを読む。
    if(tag=="Effect"&&type=="Break"){if(!arg_data->getbool("consumecard", ::cw::lib::Value(true), std::nullopt))arg_f->check_wsnversion("3","カード消費の抑止");return;}
    if(tag=="Redisplay"&&type==""){if(nondefault("transition"))arg_f->check_wsnversion("","背景切替方式の指定");return;}
    if((tag=="Start"&&type=="")||(tag=="End"&&type=="BadEnd")||(tag=="Elapse"&&type=="Time")||(tag=="Branch"&&(type=="Area"||type=="Battle"||type=="IsBattle")))return;
    if(tag=="Link"&&type=="Start"){string("link");return;}if(tag=="Start"&&type=="Battle"){dword("id");return;}if(tag=="End"&&type==""){arg_f->write_bool(boolean(attr(arg_data,"complete")));return;}if(tag=="Change"&&type=="Area"){if(nondefault("transition"))arg_f->check_wsnversion("","背景切替方式の指定");dword("id");return;}
    if(tag=="Talk"&&type=="Message"){
        if(arg_data->getint(".",::cw::lib::Value(::cw::lib::String("columns")),1)!=1)arg_f->check_wsnversion("1","複数列選択肢");
        if(bool_attr("centeringx"))arg_f->check_wsnversion("2","横方向の中央寄せ");
        if(bool_attr("centeringy"))arg_f->check_wsnversion("2","縦方向の中央寄せ");
        if(bool_attr("selecttalker"))arg_f->check_wsnversion("3","話者の選択");
        if(bool_attr("singleline"))arg_f->check_wsnversion("5","単行メッセージ");
        auto text=child(arg_data,"Text");::cw::lib::String imgpath;
        if(auto paths=child(arg_data,"ImagePaths")){
            if(paths->m_children.size()>1)arg_f->check_wsnversion("1","複合イメージ");
            if(auto image=child(paths,"ImagePath")){Base::check_imgpath(arg_f,image,"TopLeft");if(!image->m_text.empty())imgpath=Base::materialpath(image->m_text);}
        }else{Base::check_imgpath(arg_f,arg_data,"TopLeft");imgpath=attr(arg_data,"path");}
        arg_f->write_string(Base::materialpath(imgpath));arg_f->write_string(text?text->m_text:"",true);return;
    }
    if(tag=="Play"&&(type=="Bgm"||type=="Sound")){arg_f->write_string(Base::materialpath(attr(arg_data,"path")));if(type=="Bgm")arg_f->check_bgmoptions(arg_data);else arg_f->check_soundoptions(arg_data);return;}
    if(tag=="Change"&&type=="BgImage"){if(nondefault("transition"))arg_f->check_wsnversion("","背景切替方式の指定");auto list=child(arg_data,"BgImages");arg_f->write_dword(list?static_cast<::cw::lib::Integer>(list->m_children.size()):0);if(list)for(auto& x:list->m_children)::cw::binary::bgimage::BgImage::unconv(arg_f,x);return;}
    if(tag=="Wait"&&type==""){dword("value");return;}
    if(tag=="Effect"&&type==""){
        if(bool_attr("refability"))arg_f->check_wsnversion("2","選択メンバの能力参照");
        if(bool_attr("ignite"))arg_f->check_wsnversion("2","効果コンテントによるイベント発火");
        if(bool_attr("initialeffect"))arg_f->check_wsnversion("4","初期効果の有無");
        if(attr(arg_data,"absorbto","None")!="None")arg_f->check_wsnversion("4","吸収者の指定");
        dword("level");arg_f->write_byte(Base::unconv_target_member(attr(arg_data,"targetm"),arg_f,true));arg_f->write_byte(Base::unconv_card_effecttype(attr(arg_data,"effecttype")));arg_f->write_byte(Base::unconv_card_resisttype(attr(arg_data,"resisttype")));dword("successrate");arg_f->write_string(Base::materialpath(attr(arg_data,"sound")));arg_f->write_byte(Base::unconv_card_visualeffect(attr(arg_data,"visual")));arg_f->check_soundoptions(arg_data);auto list=child(arg_data,"Motions");arg_f->write_dword(list?static_cast<::cw::lib::Integer>(list->m_children.size()):0);if(list)for(auto& x:list->m_children)::cw::binary::effectmotion::EffectMotion::unconv(arg_f,x);return;
    }
    if(tag=="Branch"&&type=="Select"){arg_f->write_bool(boolean(attr(arg_data,"targetall")));if(has_attr("method")){const auto method=attr(arg_data,"method");if(method!="Manual"&&method!="Random")arg_f->check_wsnversion("1","メンバ選択分岐の評価条件");arg_f->write_bool(method=="Random");}else arg_f->write_bool(boolean(attr(arg_data,"random")));return;}
    if(tag=="Branch"&&type=="Ability"){if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");dword("value");arg_f->write_byte(Base::unconv_target_member(attr(arg_data,"targetm"),arg_f,false));arg_f->write_dword(Base::unconv_card_physicalability(attr(arg_data,"physical")));arg_f->write_dword(Base::unconv_card_mentalability(attr(arg_data,"mental")));return;}
    if(tag=="Branch"&&type=="Random"){dword("value");return;}if(tag=="Branch"&&type=="Flag"){string("flag");return;}if(tag=="Set"&&type=="Flag"){if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");string("flag");arg_f->write_bool(boolean(attr(arg_data,"value")));return;}if(tag=="Branch"&&type=="MultiStep"){string("step");return;}if(tag=="Set"&&type=="Step"){string("step");dword("value");return;}
    if(tag=="Branch"&&type=="Cast"){dword("id");return;}if(tag=="Branch"&&type=="Info"){dword("id");return;}if(tag=="Branch"&&type=="Money"){dword("value");return;}
    auto item=[&]{if(tag=="Branch"&&(type=="Item"||type=="Skill"||type=="Beast")){if(bool_attr("selectcard"))arg_f->check_wsnversion("3","カードの選択");if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");}dword("id");dword("number");arg_f->write_byte(Base::unconv_target_scope(attr(arg_data,"targets"),arg_f));};if((tag=="Branch"||tag=="Get"||tag=="Lose")&&(type=="Item"||type=="Skill"||type=="Beast")){item();return;}
    if((tag=="Get"||tag=="Lose")&&type=="Cast"){if(tag=="Get"&&attr(arg_data,"startaction","NextRound")!="NextRound")arg_f->check_wsnversion("2","行動タイミングの指定");dword("id");return;}if((tag=="Get"||tag=="Lose")&&type=="Info"){dword("id");return;}if((tag=="Get"||tag=="Lose")&&type=="Money"){dword("value");return;}
    if(tag=="Branch"&&type=="Coupon"){
        ::cw::lib::String coupon=attr(arg_data,"coupon",""),selected=coupon;std::vector<::cw::lib::String> names;if(!coupon.empty())names.push_back(coupon);
        if(auto list=child(arg_data,"Coupons"))for(const auto& x:list->m_children)if(x)names.push_back(x->m_text);
        if(names.size()>1)arg_f->check_wsnversion("2","複数クーポンの指定");else if(names.size()==1)selected=names.front();
        if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");if(bool_attr("spchars"))arg_f->check_wsnversion("4","クーポン内の特殊文字の展開");Base::check_coupon(arg_f,selected);arg_f->write_string(selected);arg_f->write_dword(0);arg_f->write_byte(Base::unconv_target_scope_coupon(attr(arg_data,"targets"),arg_f));return;
    }
    if((tag=="Get"||tag=="Lose")&&type=="Coupon"){
        if(bool_attr("spchars"))arg_f->check_wsnversion("4","クーポン内の特殊文字の展開");const auto coupon=attr(arg_data,"coupon");Base::check_coupon(arg_f,coupon);arg_f->write_string(coupon);arg_f->write_dword(tag=="Get"?integer(attr(arg_data,"value")):0);arg_f->write_byte(Base::unconv_target_scope(attr(arg_data,"targets"),arg_f));return;
    }
    if(tag=="Talk"&&type=="Dialog"){
        if(arg_data->getint(".",::cw::lib::Value(::cw::lib::String("columns")),1)!=1)arg_f->check_wsnversion("1","複数列選択肢");if(bool_attr("centeringx"))arg_f->check_wsnversion("2","横方向の中央寄せ");if(bool_attr("centeringy"))arg_f->check_wsnversion("2","縦方向の中央寄せ");if(bool_attr("selecttalker"))arg_f->check_wsnversion("3","話者の選択");
        const auto target = attr(arg_data, "targetm");
        arg_f->write_byte(Base::unconv_target_member_dialog(target, arg_f));
        if (target == "Valued") {
            std::vector<std::shared_ptr<::cw::data::CWPyElement>> coupons;
            coupons.push_back(::cw::data::make_element("Coupon", "",
                std::map<::cw::lib::String, ::cw::lib::String>{{"value", attr(arg_data, "initialValue", "0")}}));
            // Python版と同様、全Couponsコンテナーを文書順に集める。
            // find("Coupons") だけでは二つ目以降が失われる。
            for (const auto& entry : arg_data->m_children)
                if (entry && entry->m_tag == "Coupons")
                    coupons.insert(coupons.end(), entry->m_children.begin(), entry->m_children.end());
            arg_f->write_dword(static_cast<::cw::lib::Integer>(coupons.size()));
            for (const auto& coupon : coupons)
                ::cw::binary::coupon::Coupon::unconv(arg_f, coupon);
        }
        auto list = child(arg_data, "Dialogs");
        arg_f->write_dword(list ? static_cast<::cw::lib::Integer>(list->m_children.size()) : 0);
        if (list) for (auto& dialog : list->m_children)
            ::cw::binary::dialog::Dialog::unconv(arg_f, dialog);
        return;
    }
    if(tag=="Set"&&(type=="StepUp"||type=="StepDown")){string("step");return;}if(tag=="Reverse"&&type=="Flag"){if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");string("flag");return;}if(tag=="Branch"&&type=="Step"){string("step");dword("value");return;}if(tag=="Branch"&&type=="Level"){arg_f->write_bool(boolean(attr(arg_data,"average")));dword("value");return;}if(tag=="Branch"&&type=="Status"){if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");arg_f->write_byte(Base::unconv_statustype(attr(arg_data,"status"),arg_f));arg_f->write_byte(Base::unconv_target_member(attr(arg_data,"targetm"),arg_f,false));return;}if(tag=="Branch"&&type=="PartyNumber"){dword("value");return;}
    if(tag=="Call"&&type=="Start"){string("call");return;}if(tag=="Link"&&type=="Package"){dword("link");return;}if(tag=="Call"&&type=="Package"){dword("call");return;}if((tag=="Branch"||tag=="Get"||tag=="Lose")&&type=="CompleteStamp"){string("scenario");return;}if((tag=="Branch"||tag=="Get"||tag=="Lose")&&type=="Gossip"){if(bool_attr("spchars"))arg_f->check_wsnversion("4","ゴシップ内の特殊文字の展開");string("gossip");return;}if(tag=="Check"&&type=="Flag"){string("flag");return;}
    if(tag=="Substitute"&&type=="Step"){arg_f->check_version(1.30,"ステップ代入コンテント");const auto from=attr(arg_data,"from","");if(lower_ascii(from)=="??selectedplayer")arg_f->check_wsnversion("2","選択メンバ番号のステップ値への代入");string("from");string("to");return;}
    if(tag=="Substitute"&&type=="Flag"){arg_f->check_version(1.30,"フラグ代入コンテント");if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");string("from");string("to");return;}
    if(tag=="Branch"&&(type=="StepValue"||type=="FlagValue")){arg_f->check_version(1.30,type=="StepValue"?"ステップ比較分岐コンテント":"フラグ比較分岐コンテント");string("from");string("to");return;}
    if(tag=="Branch"&&type=="RandomSelect"){arg_f->check_version(1.30,"ランダム選択コンテント");if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");auto ranges=child(arg_data,"CastRanges");if(!ranges)throw std::invalid_argument("Branch, RandomSelect: CastRanges is not found.");arg_f->write_byte(Base::unconv_castranges(ranges));const auto lo=attr(arg_data,"minLevel","None"),hi=attr(arg_data,"maxLevel","None"),status=attr(arg_data,"status","None");int style=(lo!="None"&&hi!="None")?1:0;if(status!="None")style|=2;arg_f->write_byte(style);if(style&1){arg_f->write_dword(integer(lo));arg_f->write_dword(integer(hi));}if(style&2)arg_f->write_byte(Base::unconv_statustype(status,arg_f));return;}
    if(tag=="Branch"&&type=="KeyCode"){
        arg_f->check_version(1.50,"キーコード所持分岐コンテント");if(bool_attr("selectcard"))arg_f->check_wsnversion("3","カードの選択");if(bool_attr("invert"))arg_f->check_wsnversion("4","判定条件の反転");if(attr(arg_data,"condition","Has")!="Has")arg_f->check_wsnversion("5","キーコード不保有カードの検索");
        arg_f->write_byte(Base::unconv_keycoderange(attr(arg_data,"targetkc"),arg_f));auto etype=attr(arg_data,"effectCardType","All");bool skill=false,itemcard=false,beast=false,hand=false;if(etype=="All"){skill=itemcard=beast=true;}else if(etype=="Skill")skill=true;else if(etype=="Item")itemcard=true;else if(etype=="Beast")beast=true;if(has_attr("skill"))skill=required_bool_attr("skill");if(has_attr("item"))itemcard=required_bool_attr("item");if(has_attr("beast"))beast=required_bool_attr("beast");if(has_attr("hand"))hand=required_bool_attr("hand");if(skill&&itemcard&&beast&&hand)etype="All";else if(skill&&!itemcard&&!beast&&!hand)etype="Skill";else if(!skill&&itemcard&&!beast&&hand)etype="Item";else if(!skill&&!itemcard&&beast&&!hand)etype="Beast";else arg_f->check_wsnversion("2","キーコードを手札から検索");arg_f->write_byte(Base::unconv_effectcardtype(etype));string("keyCode");return;
    }
    if(tag=="Check"&&type=="Step"){arg_f->check_version(1.50,"ステップ判定コンテント");string("step");dword("value");arg_f->write_byte(Base::unconv_comparison4(attr(arg_data,"comparison")));return;}if(tag=="Branch"&&type=="Round"){arg_f->check_version(1.50,"ラウンド分岐コンテント");arg_f->write_byte(Base::unconv_comparison3(attr(arg_data,"comparison")));dword("round");return;}
    if(tag=="Move"&&type=="Card"){arg_f->check_wsnversion("3","カード再配置コンテント");if(nondefault("cardspeed"))arg_f->check_wsnversion("4","カード速度指定");return;}
    static const std::map<std::pair<::cw::lib::String,::cw::lib::String>,std::pair<::cw::lib::String,::cw::lib::String>> wsn{
        {{"Replace","BgImage"},{"1","背景置換コンテント"}},
        {{"Lose","BgImage"},{"1","背景削除コンテント"}},
        {{"Move","BgImage"},{"1","背景再配置コンテント"}},
        {{"Branch","MultiCoupon"},{"2","クーポン多岐分岐コンテント"}},
        {{"Branch","MultiRandom"},{"2","ランダム多岐分岐コンテント"}},
        {{"Change","Environment"},{"4","状況設定"}},
        {{"Set","Variant"},{"4","コモン設定コンテント"}},
        {{"Branch","Variant"},{"4","コモン分岐コンテント"}},
        {{"Check","Variant"},{"4","コモン判定コンテント"}}
    };
    if(auto it=wsn.find({tag,type});it!=wsn.end()){arg_f->check_wsnversion(it->second.first,it->second.second);return;}throw std::invalid_argument(tag+", "+type);
}

void main()
{
}

}

