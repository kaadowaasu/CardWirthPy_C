#pragma once
#include "cw/lib/compat.hpp"
#include "cw/lib/python_integer_value.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"
#include "cw/binary/summary.hpp"
namespace cw::binary::party {
class BackpackCard : public ::cw::binary::base::CWBinaryBase { public:
 std::shared_ptr<::cw::binary::base::CWBinaryBase> m_data{}; ::cw::lib::String m_fname{}; ::cw::lib::Integer m_uselimit{}; bool m_mine{};
 void initialize(std::shared_ptr<PartyMembers>,std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>>,bool=false); void set_data(::cw::lib::Value); void set_data(std::shared_ptr<::cw::binary::base::CWBinaryBase>); std::shared_ptr<::cw::data::CWPyElement> get_data() override; ::cw::lib::String create_xml(::cw::lib::String) override; static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>,::cw::lib::String,bool);
};
class Party : public ::cw::binary::base::CWBinaryBase { public:
 ::cw::lib::Integer m_type{2},m_money{}; ::cw::lib::String m_fname{}; std::vector<::cw::lib::String> m_memberslist{}; bool m_nowadventuring{}; std::vector<std::shared_ptr<BackpackCard>> m_cards{},m_errorcards{}; std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
 void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false,::cw::lib::PythonInteger=10); std::shared_ptr<::cw::data::CWPyElement> get_data() override; ::cw::lib::String create_xml(::cw::lib::String) override; static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>,::cw::lib::String,std::map<::cw::lib::String,::cw::lib::String>,::cw::lib::String);
};
class PartyMembers : public ::cw::binary::base::CWBinaryBase { public:
 ::cw::lib::Integer m_type{3},m_areaid{},m_money{},m_money_beforeadventure{}; ::cw::lib::String m_fname{},m_scenariopath{},m_music{}; bool m_nowadventuring{};
 std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerWithImage>> m_adventurers{},m_vanisheds{}; std::vector<std::shared_ptr<BackpackCard>> m_cards{}; std::vector<std::shared_ptr<::cw::binary::bgimage::BgImage>> m_bgimgs{}; std::map<::cw::lib::String,::cw::lib::Integer> m_steps{}; std::map<::cw::lib::String,bool> m_flags{}; std::vector<::cw::lib::Integer> m_friendcards{},m_infocards{}; std::optional<std::shared_ptr<Party>> m_wpl{}; std::shared_ptr<::cw::binary::summary::Summary> m_summary{};
 void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false,::cw::lib::PythonInteger=10); std::map<::cw::lib::String,::cw::lib::Integer> split_variables(::cw::lib::String,bool); std::map<::cw::lib::String,bool> split_variables_bool(::cw::lib::String); std::vector<::cw::lib::Integer> split_ids(::cw::lib::String); ::cw::lib::String create_xml(::cw::lib::String) override; void create_vanisheds_xml(::cw::lib::String); static ::cw::lib::String join_variables(std::vector<std::shared_ptr<::cw::data::CWPyElement>>); static ::cw::lib::String join_ids(std::vector<std::shared_ptr<::cw::data::CWPyElement>>); static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::Party>,std::map<::cw::lib::String,std::tuple<::cw::lib::String,std::shared_ptr<::cw::data::CWPyElement>>>,::cw::lib::String);
};
std::tuple<std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>,std::vector<std::shared_ptr<::cw::binary::album::Album>>> load_album120(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>); void main();
}
