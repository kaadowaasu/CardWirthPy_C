#include "cw/binary/dialog.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"
#include <stdexcept>
namespace cw::binary::dialog {namespace{using Base=::cw::binary::base::CWBinaryBase;std::shared_ptr<Base>parent(const ::cw::lib::Value&v){if(const auto*o=std::get_if<::cw::lib::ObjectPtr>(&v.storage()))return std::dynamic_pointer_cast<Base>(*o);return nullptr;}}
void Dialog::initialize(::cw::lib::Value pv,std::shared_ptr<::cw::binary::cwfile::CWFile>f,bool y){if(!f)throw std::invalid_argument("Dialog requires input");std::optional<std::shared_ptr<Base>>p;if(auto q=parent(pv))p=q;Base::initialize(p,f,y);m_xmltype="Dialog";m_coupons=f->string(true);m_text=f->string(true);m_data.reset();}
std::shared_ptr<::cw::data::CWPyElement>Dialog::get_data(){if(m_data&&*m_data)return *m_data;auto d=::cw::data::make_element("Dialog");d->append(::cw::data::make_element("RequiredCoupons",m_coupons));d->append(::cw::data::make_element("Text",m_text));m_data=d;return d;}
void Dialog::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>f,std::shared_ptr<::cw::data::CWPyElement>d){if(!f||!d)throw std::invalid_argument("Dialog::unconv requires file and data");::cw::lib::String coupons,text;for(const auto&e:d->m_children)if(e){if(e->m_tag=="RequiredCoupons")coupons=e->m_text;else if(e->m_tag=="Text")text=e->m_text;}f->write_string(coupons,true);f->write_string(text,true);}
void main(){}
}
