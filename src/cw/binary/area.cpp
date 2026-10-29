#include "cw/binary/area.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/bgimage.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/event.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/util.hpp"

#include <cctype>
#include <stdexcept>

namespace cw::binary::area {
namespace {
using Base = ::cw::binary::base::CWBinaryBase;
using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

Integer integer(String text) {
    return ::cw::lib::require_python_integer_for_cardwirth_pack(text);
}
String bool_text(bool value) { return value ? "True" : "False"; }
std::shared_ptr<Base> parent_from_value(const ::cw::lib::Value& value) {
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()))
        return std::dynamic_pointer_cast<Base>(*object);
    return nullptr;
}
::cw::lib::Value self_value(Base& value) {
    try { return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(value.shared_from_this())); }
    catch (const std::bad_weak_ptr&) { return {}; }
}
std::shared_ptr<Area> self_area(Area& value) {
    try { return std::dynamic_pointer_cast<Area>(value.shared_from_this()); }
    catch (const std::bad_weak_ptr&) { return nullptr; }
}
void write_events(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
                  const ElementPtr& events) {
    if (!events) { file->write_dword(0); return; }
    file->write_dword(static_cast<Integer>(events->m_children.size()));
    for (const auto& event : events->m_children) ::cw::binary::event::Event::unconv(file, event);
}
}  // namespace

void Area::initialize(::cw::lib::Value arg_parent,
                      std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                      bool yadodata, bool nameonly, String materialdir, bool image_export) {
    if (!file) throw std::invalid_argument("Area requires binary input");
    std::optional<std::shared_ptr<Base>> parent;
    if (auto value = parent_from_value(arg_parent)) parent = value;
    Base::initialize(parent, file, yadodata, std::move(materialdir), image_export);
    m_xmltype = "Area"; m_events.clear(); m_mcards.clear(); m_bgimgs.clear(); m_data.reset(); m_nameonly = nameonly;
    m_type = file->byte(); const auto marker = file->byte(); Integer version = 4;
    if (marker == static_cast<Integer>('B')) {
        (void)file->read(69); m_name = file->string(); const auto raw_id = file->dword();
        if (raw_id <= 19999) { version = 0; m_id = raw_id; }
        else { version = 2; m_id = raw_id - 20000; }
    } else {
        (void)file->byte(); (void)file->byte(); (void)file->byte();
        m_name = file->string(); m_id = file->dword() - 40000;
    }
    if (nameonly) return;
    const auto event_count = file->dword();
    for (Integer index=0; index<event_count; ++index) {
        auto event=std::make_shared<::cw::binary::event::Event>();
        event->initialize(self_value(*this), file, yadodata); m_events.push_back(std::move(event));
    }
    m_spreadtype=file->byte(); const auto card_count=file->dword(); const auto area=self_area(*this);
    for (Integer index=0; index<card_count; ++index) {
        auto card=std::make_shared<MenuCard>(); card->initialize(area,file,yadodata,version); m_mcards.push_back(std::move(card));
    }
    const auto image_count=file->dword();
    for (Integer index=0; index<image_count; ++index) {
        auto image=std::make_shared<::cw::binary::bgimage::BgImage>();
        image->initialize(self_value(*this),file,yadodata); m_bgimgs.push_back(std::move(image));
    }
}

// Area はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data 代
// 入とは異なり、変換失敗で不完全なツリーをキャッシュしない。
std::shared_ptr<Element> Area::get_data() {
    if (m_nameonly) throw std::logic_error("name-only Area has no XML data");
    if (m_data && *m_data) return *m_data;
    auto data=::cw::data::make_element("Area"); auto property=::cw::data::make_element("Property");
    property->append(::cw::data::make_element("Id",std::to_string(m_id)));
    property->append(::cw::data::make_element("Name",m_name)); data->append(property);
    auto images=::cw::data::make_element("BgImages"); for(const auto& image:m_bgimgs) if(image) images->append(image->get_data()); data->append(images);
    auto cards=::cw::data::make_element("MenuCards"); cards->set("spreadtype",conv_spreadtype(m_spreadtype));
    for(const auto& card:m_mcards) if(card) cards->append(card->get_data()); data->append(cards);
    auto events=::cw::data::make_element("Events"); for(const auto& event:m_events) if(event) events->append(event->get_data()); data->append(events);
    m_data=data; return data;
}

void Area::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, ElementPtr data) {
    if(!file||!data) throw std::invalid_argument("Area::unconv requires file and data");
    String name; Integer id=0, spread=0; ElementPtr events,cards,images;
    for(const auto& node:data->m_children) if(node) {
        if(node->m_tag=="Property") {
            for(const auto& property:node->m_children) if(property) {
                if(property->m_tag=="Id") id=integer(property->m_text); else if(property->m_tag=="Name") name=property->m_text;
            }
        } else if(node->m_tag=="BgImages") images=node;
        else if(node->m_tag=="PlayerCardEvents") { if(!node->m_children.empty()) file->check_wsnversion("2","プレイヤーカードイベント"); }
        else if(node->m_tag=="MenuCards") { cards=node; spread=Base::unconv_spreadtype(node->get("spreadtype","")); }
        else if(node->m_tag=="Events") events=node;
    }
    file->write_byte(0); file->write_dword(0); file->write_string(name);
    file->write_dword(::cw::lib::add_cardwirth_resource_id_bias(id, 40000));
    write_events(file,events); file->write_byte(spread);
    if(!cards) file->write_dword(0); else { file->write_dword(static_cast<Integer>(cards->m_children.size())); for(const auto& card:cards->m_children) MenuCard::unconv(file,card); }
    if(!images) file->write_dword(0); else { file->write_dword(static_cast<Integer>(images->m_children.size())); for(const auto& image:images->m_children) ::cw::binary::bgimage::BgImage::unconv(file,image); }
}

void MenuCard::initialize(std::shared_ptr<Area> parent,
                          std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                          bool yadodata,Integer version) {
    if(!file) throw std::invalid_argument("MenuCard requires binary input");
    std::optional<std::shared_ptr<Base>> parent_value; if(parent) parent_value=parent;
    Base::initialize(parent_value,file,yadodata); m_xmltype="MenuCard"; m_events.clear(); m_data.reset();
    (void)file->byte(); m_image=file->image(); m_name=file->string(); (void)file->dword();
    m_description=file->string(true); const auto event_count=file->dword();
    for(Integer index=0;index<event_count;++index) { auto event=std::make_shared<::cw::binary::event::Event>(); event->initialize(self_value(*this),file,yadodata); m_events.push_back(std::move(event)); }
    m_flag=file->string(); m_scale=file->dword(); m_left=file->dword(); m_top=file->dword();
    m_imgpath=version>2?file->string():"";
}

// MenuCard はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data 代
// 入とは異なり、変換失敗で不完全なツリーをキャッシュしない。
std::shared_ptr<Element> MenuCard::get_data() {
    if(m_data&&*m_data) return *m_data;
    m_imgpath=m_image&&!m_image->empty()?export_image():get_materialpath(m_imgpath);
    auto data=::cw::data::make_element("MenuCard"); auto property=::cw::data::make_element("Property");
    property->append(::cw::data::make_element("Name",m_name));
    if(m_imgpath.size()==1&&m_imgpath[0]>='1'&&m_imgpath[0]<='6') property->append(::cw::data::make_element("PCNumber",m_imgpath));
    else property->append(::cw::data::make_element("ImagePath",m_imgpath));
    property->append(::cw::data::make_element("Description",m_description)); property->append(::cw::data::make_element("Flag",m_flag));
    auto location=::cw::data::make_element("Location"); location->set("left",std::to_string(m_left)); location->set("top",std::to_string(m_top)); property->append(location);
    auto size=::cw::data::make_element("Size"); size->set("scale",std::to_string(m_scale)+"%"); property->append(size); data->append(property);
    auto events=::cw::data::make_element("Events"); for(const auto& event:m_events) if(event) events->append(event->get_data()); data->append(events); m_data=data; return data;
}

void MenuCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,ElementPtr data) {
    if(!file||!data) throw std::invalid_argument("MenuCard::unconv requires file and data");
    std::optional<::cw::lib::Bytes> image; String name,description,flag,imgpath; Integer scale=0,left=0,top=0; ElementPtr events;
    for(const auto& node:data->m_children) if(node) {
        if(node->m_tag=="Property") {
            for(const auto& property:node->m_children) if(property) {
            const auto& tag=property->m_tag;
            if(tag=="Name") { if(property->getbool(".", ::cw::lib::Value("spchars"), false)) file->check_wsnversion("4","特殊文字の展開"); name=property->m_text; }
            else if(tag=="ImagePath") { Base::check_imgpath(file,property,"TopLeft"); imgpath=Base::materialpath(property->m_text); }
            else if(tag=="ImagePaths") { if(property->m_children.size()>1) file->check_wsnversion("1","複合イメージ"); else { ElementPtr path; for(const auto& child:property->m_children) if(child&&child->m_tag=="ImagePath") { path=child; break; } if(path) { Base::check_imgpath(file,path,"TopLeft"); if(!path->m_text.empty()) imgpath=Base::materialpath(path->m_text); } } }
            else if(tag=="PCNumber") { file->check_version(1.50,"メニューカードへのプレイヤーキャラクター表示"); imgpath=property->m_text; }
            else if(tag=="Description") description=property->m_text; else if(tag=="Flag") flag=property->m_text;
            else if(tag=="Location") { left=integer(property->get("left","")); top=integer(property->get("top","")); }
            else if(tag=="Size") { auto scale_text=property->get("scale",""); if(!scale_text.empty()&&scale_text.back()=='%') scale_text.pop_back(); scale=integer(scale_text); }
            else if(tag=="Layer"&&integer(property->m_text)!=::cw::g_LAYER_MCARDS) file->check_wsnversion("1","レイヤ");
            else if(tag=="CardGroup"&&!property->m_text.empty()) file->check_wsnversion("3","カードグループ");
            else if(tag=="DealingSpeed"&&property->m_text!="Default") file->check_wsnversion("4","カード速度指定");
            }
        } else if(node->m_tag=="Events") events=node;
    }
    file->write_byte(0); file->write_image(image); file->write_string(name); file->write_dword(0); file->write_string(description,true);
    write_events(file,events); file->write_string(flag); file->write_dword(scale); file->write_dword(left); file->write_dword(top); file->write_string(imgpath);
}

void main() {}
}  // namespace cw::binary::area
