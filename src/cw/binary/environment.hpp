#pragma once
#include "cw/lib/compat.hpp"
#include "cw/lib/python_integer_value.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"
namespace cw::binary::environment {
class Environment : public ::cw::binary::base::CWBinaryBase { public:
 ::cw::lib::Integer m_type{-1},m_yadotype{},m_drawcard_speed{},m_drawbg_speed{},m_message_speed{},m_viewtype_poster{},m_bgcolor_message{},m_changetype_bg{},m_money{};
 // Py の dataversion_int は、拒否される前も含めて任意精度を保持する。
 ::cw::lib::PythonInteger m_dataversion_int{};
 ::cw::lib::String m_dataversion{},m_cwpypartyname{},m_skintype{},m_skinname{},m_compstamps{},m_scenarioname{},m_gossips{},m_partyname{};
 bool m_play_bgm{},m_play_sound{},m_correct_scaledown{},m_correct_scaleup{},m_autoselect_party{},m_clickcancel{},m_effect_getmoney{},m_clickjump{},m_keep_levelmax{},m_bgeffectatselmode{true},m_use_decofont{},m_versiononly{};
 std::vector<std::shared_ptr<UnusedCard>> m_unusedcards{},m_errorcards{}; std::vector<std::shared_ptr<YadoCard>> m_yadocards{}; std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
 void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false,bool=false); std::shared_ptr<::cw::data::CWPyElement> get_data() override; std::map<::cw::lib::String,::cw::lib::Integer> get_cardtypedict();
 static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>,std::map<::cw::lib::String,::cw::lib::String>,std::vector<std::tuple<::cw::lib::String,std::shared_ptr<::cw::data::CWPyElement>>>,std::vector<std::tuple<::cw::lib::String,std::tuple<::cw::lib::String,std::shared_ptr<::cw::data::CWPyElement>>>>);
};
class UnusedCard : public ::cw::binary::base::CWBinaryBase { public: std::shared_ptr<::cw::binary::base::CWBinaryBase> m_data{};::cw::lib::String m_fname{};::cw::lib::Integer m_uselimit{};void initialize(std::optional<std::shared_ptr<Environment>>,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false);void set_data(std::shared_ptr<::cw::binary::base::CWBinaryBase>);std::shared_ptr<::cw::data::CWPyElement> get_data() override;::cw::lib::String create_xml(::cw::lib::String) override;::cw::lib::String create_xml2(::cw::lib::String,::cw::lib::Integer);static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>,::cw::lib::String);};
class YadoCard : public ::cw::binary::base::CWBinaryBase { public: ::cw::lib::String m_description{},m_fname{};::cw::lib::Integer m_type{},m_number{};void initialize(std::shared_ptr<Environment>,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false);static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>,::cw::lib::String);};
::cw::lib::Integer Environment_unconv_roundval(::cw::lib::Integer);void main(); }
