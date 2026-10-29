#pragma once
#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"
namespace cw::binary::cast {
class CastCard:public ::cw::binary::base::CWBinaryBase{public:CastCard()=default;~CastCard()override=default;
    ::cw::lib::Integer m_type{};::cw::lib::String m_imgpath{},m_description{};
    bool m_noeffect_weapon{},m_noeffect_magic{},m_undead{},m_automaton{},m_unholy{},m_constructure{},m_resist_fire{},m_resist_ice{},m_weakness_fire{},m_weakness_ice{};
    ::cw::lib::Integer m_level{},m_money{},m_life{},m_maxlife{},m_paralyze{},m_poison{},m_avoid{},m_resist{},m_defense{};
    ::cw::lib::Integer m_dex{},m_agl{},m_int{},m_str{},m_vit{},m_min{},m_aggressive{},m_cheerful{},m_brave{},m_cautious{},m_trickish{};
    ::cw::lib::Integer m_mentality{},m_duration_mentality{},m_duration_bind{},m_duration_silence{},m_duration_faceup{},m_duration_antimagic{};
    ::cw::lib::Integer m_enhance_action{},m_duration_enhance_action{},m_enhance_avoid{},m_duration_enhance_avoid{},m_enhance_resist{},m_duration_enhance_resist{},m_enhance_defense{},m_duration_enhance_defense{};
    std::vector<std::shared_ptr<::cw::binary::item::ItemCard>>m_items{};std::vector<std::shared_ptr<::cw::binary::skill::SkillCard>>m_skills{};std::vector<std::shared_ptr<::cw::binary::beast::BeastCard>>m_beasts{};std::vector<std::shared_ptr<::cw::binary::coupon::Coupon>>m_coupons{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>>m_data{};
    bool m_nameonly{};
    ::cw::lib::String export_image();
    void initialize(::cw::lib::Value,std::shared_ptr<::cw::binary::cwfile::CWFile>,bool=false,bool=false,::cw::lib::String="Material",bool=true);
    std::shared_ptr<::cw::data::CWPyElement>get_data()override;static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter>,std::shared_ptr<::cw::data::CWPyElement>);
};void main();}
