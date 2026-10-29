#include "cw/binary/info.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/image.hpp"
#include "cw/data.hpp"
#include "cw/lib/python_integer.hpp"
#include <cctype>
#include <stdexcept>

namespace cw::binary::info {
namespace {
::cw::lib::Integer parse_integer(const ::cw::lib::String& text)
{
    return ::cw::lib::require_python_integer(text);
}
}

void InfoCard::initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, bool arg_yadodata, bool arg_nameonly, ::cw::lib::String arg_materialdir, bool arg_image_export)
{
    if(!arg_f)throw std::invalid_argument("InfoCard requires CWFile");std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> parent;if(auto p=std::get_if<::cw::lib::ObjectPtr>(&arg_parent.storage()))parent=std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*p);
    CWBinaryBase::initialize(parent,arg_f,arg_yadodata,std::move(arg_materialdir),arg_image_export);m_xmltype="InfoCard";m_data.reset();m_description.clear();m_imgpath.clear();m_nameonly=arg_nameonly;m_type=arg_f->byte();m_image=arg_f->image();m_name=arg_f->string();const auto id=arg_f->dword();m_id=id<=19999?id:(id<=39999?id-20000:id-40000);if(!arg_nameonly)m_description=arg_f->string(true);
}

std::shared_ptr<::cw::data::CWPyElement> InfoCard::get_data()
{
    if(m_nameonly)throw std::logic_error("name-only InfoCard has no XML data");if(m_data&&*m_data)return *m_data;if(m_image&&!m_image->empty())m_imgpath=get_image_export() ? export_image() : ::cw::binary::image::data_to_code(*m_image);auto data=::cw::data::make_element("InfoCard");auto p=::cw::data::make_element("Property");p->append(::cw::data::make_element("Id",std::to_string(m_id)));p->append(::cw::data::make_element("Name",m_name));p->append(::cw::data::make_element("ImagePath",m_imgpath));p->append(::cw::data::make_element("Description",m_description));data->append(p);m_data=data;return data;
}

void InfoCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if(!arg_f||!arg_data)throw std::invalid_argument("InfoCard::unconv null argument");::cw::lib::String name,description;::cw::lib::Integer id=0;std::optional<::cw::lib::Bytes> image;for(auto& e:arg_data->m_children)if(e&&e->m_tag=="Property")for(auto& p:e->m_children)if(p){if(p->m_tag=="Id")id=parse_integer(p->m_text);else if(p->m_tag=="Name")name=p->m_text;else if(p->m_tag=="Description")description=p->m_text;else if(p->m_tag=="ImagePath"||p->m_tag=="ImagePaths")image=::cw::binary::base::CWBinaryBase::import_image(arg_f,p,true,false,"TopLeft");}arg_f->write_byte(4);arg_f->write_image(image);arg_f->write_string(name);arg_f->write_dword(id+40000);arg_f->write_string(description,true);
}

void main()
{
}

}

