#pragma once
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"
namespace cw::binary::cwscenario {
class CWScenario : public ::cw::lib::Object { public:
 ::cw::lib::Integer m_curnum{},m_maxnum{1}; ::cw::lib::String m_name{},m_path{},m_dir{},m_skintype{},m_materialdir{"Material"},m_message{},m_errorlog{},m_imgdir{}; bool m_hasmodeini{},m_image_export{true};
 std::vector<::cw::lib::String> m_cwfiles{},m_materials{},m_otherfiles{},m_otherdirs{}; std::vector<std::shared_ptr<::cw::binary::base::CWBinaryBase>> m_datalist{}; std::optional<::cw::lib::String> m_summarypath{}; std::optional<std::tuple<::cw::lib::String,::cw::lib::String,bool,bool,bool>> m_versionhint{};
 void initialize(::cw::lib::String,::cw::lib::String,::cw::lib::String,::cw::lib::String="Material",bool=true); void read_modeini(::cw::lib::String); bool is_convertible(); void write_errorlog(::cw::lib::String); bool is_yadodata(); void load(); std::tuple<std::optional<::cw::lib::Value>,std::optional<::cw::lib::Bytes>> load_file(::cw::lib::String,bool=false,bool=false); // ワーカーが所有する件数・メッセージの各変化を通知し、ポーリングする読み取り側との競合を避ける。
 ::cw::lib::String convert(const std::function<void(::cw::lib::Integer,const ::cw::lib::String&)>& progress = {});
}; void main(); }
