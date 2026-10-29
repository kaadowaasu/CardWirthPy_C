#pragma once
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"
namespace cw::binary::cwyado {
// convert() 前に登録し、ワーカー上でだけ呼び、join
// 後に解除する。観測側は進捗を同期された状態へ複製し、変換器のフィ
// ールドを並行して読んだり UI ウィジェットを呼んだりしない。
using ProgressCallback=std::function<void(::cw::lib::Integer,const ::cw::lib::String&)>;
class CWYado : public ::cw::lib::Object { public:
 ProgressCallback m_progress_callback;
 void publish_progress() const {if(m_progress_callback)m_progress_callback(m_curnum,m_message);}
 ::cw::lib::Integer m_curnum{},m_curnum_n{},m_dataversion_int{},m_maxnum{1}; ::cw::lib::String m_name{},m_path{},m_dir{},m_skintype{},m_environmentpath{},m_errorlog{},m_message{};
 std::vector<::cw::lib::String>m_yadofiles{},m_cardfiles{},m_otherfiles{},m_otherdirs{};std::vector<std::shared_ptr<::cw::binary::base::CWBinaryBase>>m_datalist{};std::shared_ptr<::cw::binary::environment::Environment>m_wyd{};std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerHeader>>m_wchs{};std::vector<std::shared_ptr<::cw::binary::adventurer::AdventurerCard>>m_wcps{};std::vector<std::shared_ptr<::cw::binary::album::Album>>m_wrms{};std::vector<std::shared_ptr<::cw::binary::party::Party>>m_wpls{};std::vector<std::shared_ptr<::cw::binary::party::PartyMembers>>m_wpts{};std::vector<std::tuple<std::shared_ptr<::cw::binary::party::Party>,std::shared_ptr<::cw::binary::party::PartyMembers>>>m_nowadventuringparties{};
 void initialize(::cw::lib::String,::cw::lib::String,::cw::lib::String={});void write_errorlog(::cw::lib::String);bool is_convertible();::cw::lib::String convert();void load();std::shared_ptr<::cw::binary::base::CWBinaryBase>load_yadofile(::cw::lib::String);std::shared_ptr<::cw::binary::base::CWBinaryBase>load_cardfile(::cw::lib::String,const std::map<::cw::lib::String,::cw::lib::Integer>&);void create_log(std::shared_ptr<::cw::binary::party::Party>,std::shared_ptr<::cw::binary::party::PartyMembers>);
};
class UnconvCWYado : public ::cw::lib::Object { public:
 ProgressCallback m_progress_callback;
 void publish_progress() const {if(m_progress_callback)m_progress_callback(m_curnum,m_message);}
 std::shared_ptr<::cw::data::YadoData>m_ydata{};::cw::lib::Real m_targetengine{};::cw::lib::String m_name{},m_dir{},m_errorlog{},m_message{};::cw::lib::Integer m_curnum{},m_maxnum{};void initialize(std::shared_ptr<::cw::data::YadoData>,::cw::lib::String,::cw::lib::Real);void write_errorlog(::cw::lib::String);void convert();};void main();}
