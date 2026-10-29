#pragma once
#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"
namespace cw::binary::dialog {
class Dialog:public ::cw::binary::base::CWBinaryBase{public:Dialog()=default;~Dialog()override=default;::cw::lib::String m_coupons{},m_text{};std::optional<std::shared_ptr<::cw::data::CWPyElement>>m_data{};void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false);std::shared_ptr<::cw::data::CWPyElement>get_data()override;static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>);};
void main();}
