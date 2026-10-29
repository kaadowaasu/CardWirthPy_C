#include "cw/binary/package.hpp"
#include "cw/binary/card_common.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/event.hpp"
#include "cw/data.hpp"
#include <stdexcept>

namespace cw::binary::package {
void Package::initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, bool arg_yadodata, bool arg_nameonly, ::cw::lib::String arg_materialdir, bool arg_image_export)
{
    if(!arg_f)throw std::invalid_argument("Package requires CWFile");auto parent=::cw::binary::card_common::parent_from_value(std::optional<::cw::lib::Value>(arg_parent));CWBinaryBase::initialize(parent,arg_f,arg_yadodata,std::move(arg_materialdir),arg_image_export);m_xmltype="Package";m_type=7;m_events.clear();m_data.reset();m_nameonly=arg_nameonly;(void)arg_f->dword();m_name=arg_f->string();m_id=arg_f->dword();if(arg_nameonly)return;const auto n=arg_f->dword();const auto self=::cw::binary::card_common::self_value(*this);for(::cw::lib::Integer i=0;i<n;++i){auto e=std::make_shared<::cw::binary::event::SimpleEvent>();e->initialize(self,arg_f,false);m_events.push_back(e);}
}

std::shared_ptr<::cw::data::CWPyElement> Package::get_data()
{
    if(m_nameonly)throw std::logic_error("name-only Package has no XML data");if(m_data&&*m_data)return *m_data;auto data=::cw::data::make_element("Package");auto p=::cw::data::make_element("Property");p->append(::cw::data::make_element("Id",std::to_string(m_id)));p->append(::cw::data::make_element("Name",m_name));data->append(p);auto events=::cw::data::make_element("Events");for(auto& e:m_events){if(!e)throw std::logic_error("Package contains null event");events->append(e->get_data());}data->append(events);m_data=data;return data;
}

void Package::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if(!arg_f||!arg_data)throw std::invalid_argument("Package::unconv null argument");::cw::lib::String name;::cw::lib::Integer id=0;std::shared_ptr<::cw::data::CWPyElement> events;for(auto& e:arg_data->m_children)if(e){if(e->m_tag=="Property"){for(auto& p:e->m_children)if(p){if(p->m_tag=="Id")id=::cw::binary::card_common::to_integer(p->m_text);else if(p->m_tag=="Name")name=p->m_text;}}else if(e->m_tag=="Events")events=e;}arg_f->write_dword(0);arg_f->write_string(name);arg_f->write_dword(id);arg_f->write_dword(events?static_cast<::cw::lib::Integer>(events->m_children.size()):0);if(events)for(auto& e:events->m_children)::cw::binary::event::SimpleEvent::unconv(arg_f,e);
}

void main()
{
}

}

