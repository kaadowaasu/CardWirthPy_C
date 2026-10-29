#include "cw/binary/battle.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/card_common.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/event.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"

#include <cmath>
#include <cctype>
#include <stdexcept>

namespace cw::binary::battle {
namespace {
using Base=::cw::binary::base::CWBinaryBase; using Element=::cw::data::CWPyElement;
using ElementPtr=std::shared_ptr<Element>; using Integer=::cw::lib::Integer; using String=::cw::lib::String;
Integer integer(const String& text) {
    return ::cw::lib::require_python_integer_for_cardwirth_pack(text);
}
Integer scale_integer(String text) {
    if (!text.empty() && text.back() == '%') text.pop_back();
    return integer(text);
}
String bool_text(bool value){return value?"True":"False";}
std::shared_ptr<Battle> self_battle(Battle& value){try{return std::dynamic_pointer_cast<Battle>(value.shared_from_this());}catch(const std::bad_weak_ptr&){return nullptr;}}
std::optional<std::shared_ptr<Base>> parent_option(const std::shared_ptr<Battle>& value){if(!value)return std::nullopt;return std::static_pointer_cast<Base>(value);}
void write_events(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,const ElementPtr& events){if(!events){file->write_dword(0);return;}file->write_dword(static_cast<Integer>(events->m_children.size()));for(const auto& event:events->m_children)::cw::binary::event::Event::unconv(file,event);}
}  // namespace

void Battle::initialize(::cw::lib::Value parent_value,
                        std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                        bool yadodata,bool nameonly,String materialdir,bool image_export){
    if(!file)throw std::invalid_argument("Battle requires binary input");
    const auto parent=::cw::binary::card_common::parent_from_value(std::optional<::cw::lib::Value>(parent_value));
    std::optional<std::shared_ptr<Base>> parent_arg;if(parent)parent_arg=parent;
    Base::initialize(parent_arg,file,yadodata,std::move(materialdir),image_export);m_xmltype="Battle";m_nameonly=nameonly;m_events.clear();m_ecards.clear();m_data.reset();
    m_type=file->byte();const auto marker=file->byte();Integer version=4;
    if(marker==static_cast<Integer>('B')){(void)file->read(69);m_name=file->string();const auto raw=file->dword();if(raw<=19999){version=0;m_id=raw;}else{version=2;m_id=raw-20000;}}
    else{(void)file->byte();(void)file->byte();(void)file->byte();m_name=file->string();m_id=file->dword()-40000;}
    if(nameonly)return;const auto event_count=file->dword();
    for(Integer i=0;i<event_count;++i){auto event=std::make_shared<::cw::binary::event::Event>();event->initialize(::cw::binary::card_common::self_value(*this),file,yadodata);m_events.push_back(std::move(event));}
    m_spreadtype=file->byte();const auto enemy_count=file->dword();const auto battle=self_battle(*this);
    for(Integer i=0;i<enemy_count;++i){auto enemy=std::make_shared<EnemyCard>();enemy->initialize(battle,file,yadodata);m_ecards.push_back(std::move(enemy));}
    m_bgm=version>0?file->string():"DefBattle.mid";
}

// Battle はローカルの直列化が成功してから m_data を公開する
// 。 Python版の早い self.data 代入とは異なり、後の変換失
// 敗で次回 get_data が不完全なキャッシュを使わないようにする。
std::shared_ptr<Element> Battle::get_data(){if(m_nameonly)throw std::logic_error("name-only Battle has no XML data");if(m_data&&*m_data)return *m_data;auto data=::cw::data::make_element("Battle");auto property=::cw::data::make_element("Property");
    property->append(::cw::data::make_element("Id",std::to_string(m_id)));property->append(::cw::data::make_element("Name",m_name));property->append(::cw::data::make_element("MusicPath",get_materialpath(m_bgm)));data->append(property);
    auto enemies=::cw::data::make_element("EnemyCards");enemies->set("spreadtype",conv_spreadtype(m_spreadtype));for(const auto& enemy:m_ecards)if(enemy)enemies->append(enemy->get_data());data->append(enemies);
    auto events=::cw::data::make_element("Events");for(const auto& event:m_events)if(event)events->append(event->get_data());data->append(events);m_data=data;return data;}

void Battle::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,ElementPtr data){if(!file||!data)throw std::invalid_argument("Battle::unconv requires file and data");
    String name,bgm;Integer id=0,spread=0;ElementPtr events,enemies;
    for(const auto& node:data->m_children)if(node){if(node->m_tag=="Property"){for(const auto& property:node->m_children)if(property){
        if(property->m_tag=="Id")id=integer(property->m_text);else if(property->m_tag=="Name")name=property->m_text;
        else if(property->m_tag=="MusicPath"){bgm=Base::materialpath(property->m_text);file->check_bgmoptions(property);if(property->getbool(".", ::cw::lib::Value("continue"), false))file->check_wsnversion("3","バトル開始時のBGM継続");}
        else if(property->m_tag=="RunAway"&&!property->getbool(".", ::cw::lib::Value(true), std::nullopt))file->check_wsnversion("3","逃走不可バトル");
    }}else if(node->m_tag=="PlayerCardEvents"){if(!node->m_children.empty())file->check_wsnversion("2","プレイヤーカードイベント");}
    else if(node->m_tag=="EnemyCards"){enemies=node;spread=Base::unconv_spreadtype(node->get("spreadtype",""));}else if(node->m_tag=="Events")events=node;}
    file->write_byte(1);file->write_dword(0);file->write_string(name);
    file->write_dword(::cw::lib::add_cardwirth_resource_id_bias(id, 40000));
    write_events(file,events);file->write_byte(spread);
    if(!enemies)file->write_dword(0);else{file->write_dword(static_cast<Integer>(enemies->m_children.size()));for(const auto& enemy:enemies->m_children)EnemyCard::unconv(file,enemy);}file->write_string(bgm);
}

void EnemyCard::initialize(std::shared_ptr<Battle> parent,std::shared_ptr<::cw::binary::cwfile::CWFile> file,bool yadodata){if(!file)throw std::invalid_argument("EnemyCard requires binary input");Base::initialize(parent_option(parent),file,yadodata);m_xmltype="EnemyCard";m_events.clear();m_data.reset();
    m_cast_id=file->dword();const auto event_count=file->dword();for(Integer i=0;i<event_count;++i){auto event=std::make_shared<::cw::binary::event::Event>();event->initialize(::cw::binary::card_common::self_value(*this),file,yadodata);m_events.push_back(std::move(event));}
    m_flag=file->string();m_scale=file->dword();m_left=file->dword();m_top=file->dword();m_escape=file->boolean();}

// EnemyCard はローカルの直列化が成功してから m_data を公開
// する。 Python版の早い self.data 代入とは異なり、後の変換
// 失敗で次回 get_data が不完全なキャッシュを使わないようにする。
std::shared_ptr<Element> EnemyCard::get_data(){if(m_data&&*m_data)return *m_data;auto data=::cw::data::make_element("EnemyCard");data->set("escape",bool_text(m_escape));auto property=::cw::data::make_element("Property");property->append(::cw::data::make_element("Id",std::to_string(m_cast_id)));property->append(::cw::data::make_element("Flag",m_flag));
    auto location=::cw::data::make_element("Location");location->set("left",std::to_string(m_left));location->set("top",std::to_string(m_top));property->append(location);auto size=::cw::data::make_element("Size");size->set("scale",std::to_string(m_scale)+"%");property->append(size);data->append(property);
    auto events=::cw::data::make_element("Events");for(const auto& event:m_events)if(event)events->append(event->get_data());data->append(events);m_data=data;return data;}

// 数値の行動IDは、Python版と同じ CWPyElement.getint で読む。
// Pythonのfloatと受理する構文が異なる std::stod では解析しない。
void EnemyCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,ElementPtr data){if(!file||!data)throw std::invalid_argument("EnemyCard::unconv requires file and data");Integer cast_id=0,scale=0,left=0,top=0;String flag;bool escape=data->getbool(".", ::cw::lib::Value("escape"), false);ElementPtr events;
    for(const auto& node:data->m_children)if(node){if(node->m_tag=="Property"){for(const auto& property:node->m_children)if(property){const auto& tag=property->m_tag;
        if(tag=="Id")cast_id=integer(property->m_text);else if(tag=="Flag")flag=property->m_text;else if(tag=="Location"){left=integer(property->get("left",""));top=integer(property->get("top",""));}
        else if(tag=="Size")scale=scale_integer(property->get("scale",""));else if(tag=="Layer"&&integer(property->m_text)!=::cw::g_LAYER_MCARDS)file->check_wsnversion("1","レイヤ");
        else if(tag=="CardGroup"&&!property->m_text.empty())file->check_wsnversion("3","カードグループ");else if(tag=="DealingSpeed"&&property->m_text!="Default")file->check_wsnversion("4","カード速度指定");
        else if(tag=="Actions")for(const auto& action:property->m_children)if(action&&action->m_tag=="Action"){const auto id=action->getint(".", ::cw::lib::Value("id"), std::nullopt);if(id==7)escape=action->getbool(".", ::cw::lib::Value(false), std::nullopt);else if(id!=0)file->check_wsnversion("4","アクションの有無");}
    }}else if(node->m_tag=="Events")events=node;}
    file->write_dword(cast_id);write_events(file,events);file->write_string(flag);file->write_dword(scale);file->write_dword(left);file->write_dword(top);file->write_bool(escape);
}

void main(){}
}  // namespace cw::binary::battle
