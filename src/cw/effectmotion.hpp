#pragma once

// cw/effectmotion.pyの移植。Value
// メンバーは生成済みAPIとの互換用に残し、 効果処
// 理が使う実行時状態は下の型付きメンバーへ保持する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::effectmotion {
class Effect : public ::cw::lib::Object {
public:
    Effect() = default;
    virtual ~Effect() = default;
    inline static std::optional<std::shared_ptr<::cw::header::CardHeader>> s_inusecard{};
    inline static std::optional<::cw::lib::Value> s_user{};
    ::cw::lib::Value m__level{};
    ::cw::lib::Value m_absorbto{};
    ::cw::lib::Value m_battlespeed{};
    ::cw::lib::Value m_cardspeed{};
    ::cw::lib::Value m_channel{};
    ::cw::lib::Value m_effecttype{};
    ::cw::lib::Value m_fade{};
    ::cw::lib::Value m_inusecard{};
    bool m_is_enhance_act{};
    ::cw::lib::Value m_level{};
    ::cw::lib::Value m_loopcount{};
    ::cw::lib::Value m_motions{};
    ::cw::lib::Value m_overridecardspeed{};
    ::cw::lib::Value m_refability{};
    ::cw::lib::Value m_resisttype{};
    ::cw::lib::Value m_soundpath{};
    ::cw::lib::Value m_successrate{};
    ::cw::lib::Value m_user{};
    ::cw::lib::Value m_visualeffect{};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> m_vocation{};
    ::cw::lib::Value m_volume{};
    void initialize(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_motions, std::shared_ptr<::cw::effectmotion::EffectParams> arg_d, bool arg_battlespeed);
    void update_status(std::optional<std::shared_ptr<::cw::character::Character>> arg_selectedmember);
    ::cw::lib::Integer get_level();
    bool apply(::cw::lib::Value arg_target, bool arg_event, std::optional<std::shared_ptr<::cw::character::Character>> arg_selectedmember);
    bool apply_charactercard(::cw::lib::Value arg_target, bool arg_event, std::optional<std::shared_ptr<::cw::character::Character>> arg_selectedmember);
    ::cw::lib::Integer _get_cardspeed(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target);
    bool check_noeffect(std::shared_ptr<::cw::character::Character> arg_target);
    bool check_avoid(std::shared_ptr<::cw::character::Character> arg_target, std::optional<std::shared_ptr<::cw::character::Character>> arg_selectedmember);
    bool check_resist(std::shared_ptr<::cw::character::Character> arg_target, std::optional<std::shared_ptr<::cw::character::Character>> arg_selectedmember);
    void animate(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target, bool arg_update_image);
    void _animate_impl(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target, bool arg_update_image);
    bool check_enabledtarget(::cw::lib::Value arg_target, bool arg_event);
    bool has_addablebeast(::cw::lib::Value arg_target);
    bool has_removablebeast(::cw::lib::Value arg_target);
    bool has_motions(std::vector<::cw::lib::String> arg_motiontypes);
    bool has_motion(::cw::lib::String arg_motiontype);
    ::cw::lib::Integer count_motion(::cw::lib::String arg_motiontype);

private:
    std::shared_ptr<::cw::character::Character> m_user_typed{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_inusecard_typed{};
    std::vector<std::shared_ptr<::cw::effectmotion::EffectMotion>> m_motions_typed{};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> m_vocation_typed{};
};

class EffectMotion : public ::cw::lib::Object {
public:
    EffectMotion() = default;
    virtual ~EffectMotion() = default;
    ::cw::lib::Integer m__enhance_act{};
    ::cw::lib::Value m__level{};
    ::cw::lib::Value m__targetlevel{};
    ::cw::lib::Value m__vocation{};
    ::cw::lib::Integer m__vocation_level{};
    ::cw::lib::Integer m__vocation_val{};
    std::optional<::cw::lib::Value> m_absorber{};
    ::cw::lib::Value m_absorbto{};
    ::cw::lib::Value m_beasts{};
    ::cw::lib::Value m_cardheader{};
    ::cw::lib::Value m_damagetype{};
    ::cw::lib::Integer m_duration{};
    ::cw::lib::Value m_element{};
    bool m_is_enhance_act{};
    ::cw::lib::Value m_refability{};
    ::cw::lib::Value m_selectedmember{};
    ::cw::lib::Value m_type{};
    ::cw::lib::Value m_user{};
    ::cw::lib::Integer m_value{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, std::optional<::cw::lib::Value> arg_user, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, ::cw::lib::Integer arg_targetlevel, bool arg_refability, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_vocation, ::cw::lib::String arg_absorbto, std::optional<::cw::lib::Value> arg_selectedmember);
    void update_status(std::optional<::cw::lib::Value> arg_selectedmember);
    ::cw::lib::Integer get_vocation_val();
    ::cw::lib::Integer get_vocation_level();
    ::cw::lib::Integer get_enhance_act();
    ::cw::lib::Integer get_level();
    bool is_effectcontent();
    ::cw::lib::Integer calc_skillpowervalue();
    ::cw::lib::Integer calc_effectvalue(std::shared_ptr<::cw::character::Character> arg_target, bool arg_physical);
    ::cw::lib::Integer calc_durationvalue(std::shared_ptr<::cw::character::Character> arg_target, bool arg_enhance);
    ::cw::lib::Integer calc_defensedvalue(::cw::lib::Integer arg_value, std::shared_ptr<::cw::character::Character> arg_target);
    bool is_noeffect(std::shared_ptr<::cw::character::Character> arg_target);
    bool is_weakness(std::shared_ptr<::cw::character::Character> arg_target);
    bool can_apply(std::shared_ptr<::cw::character::Character> arg_target);
    bool has_addablebeast(::cw::lib::Value arg_target);
    bool has_removablebeast(::cw::lib::Value arg_target);
    bool apply(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool heal_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool damage_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool absorb_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool paralyze_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool disparalyze_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool poison_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dispoison_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool getskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool loseskillpower_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool mentality(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool sleep_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool confuse_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool overheat_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool brave_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool panic_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool normal_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool bind_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool disbind_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool silence_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dissilence_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool faceup_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool facedown_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool antimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool disantimagic_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool enhanceaction_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool enhanceavoid_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool enhanceresist_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool enhancedefense_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool vanishtarget_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool vanishcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool vanishbeast_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealpowerfulattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealcriticalattackcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealfeintcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealdefensecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealdistancecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealconfusecard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool dealskillcard_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool cancelaction_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool summonbeast_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);
    bool noeffect_motion(std::shared_ptr<::cw::character::Character> arg_target, bool arg_success_res);

private:
    std::shared_ptr<::cw::data::CWPyElement> m_data_typed{};
    std::shared_ptr<::cw::character::Character> m_user_typed{};
    std::shared_ptr<::cw::header::CardHeader> m_cardheader_typed{};
    std::optional<std::shared_ptr<::cw::character::Character>> m_selectedmember_typed{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m_beasts_typed{};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> m_vocation_typed{};
    ::cw::lib::String m_type_typed{};
    ::cw::lib::String m_element_typed{};
    ::cw::lib::String m_damagetype_typed{};
    ::cw::lib::Integer m_targetlevel_typed{};
    ::cw::lib::String m_absorbto_typed{};
};

class EffectParams : public ::cw::lib::Object {
public:
    EffectParams() = default;
    virtual ~EffectParams() = default;
    std::map<::cw::lib::String, bool> m_bool_params{};
    std::map<::cw::lib::String, std::optional<::cw::lib::Value>> m_ccard_params{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_int_params{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_inusecard{};
    std::map<::cw::lib::String, ::cw::lib::String> m_str_params{};
    void initialize();
    void set_item(::cw::lib::String arg_key, ::cw::lib::Value arg_value);
    ::cw::lib::String get(::cw::lib::String arg_key, ::cw::lib::String arg_defvalue);
    bool get_2(::cw::lib::String arg_key, bool arg_defvalue);
    ::cw::lib::Integer get_3(::cw::lib::String arg_key, ::cw::lib::Integer arg_defvalue);
    std::optional<::cw::lib::Value> get_4(::cw::lib::String arg_key, ::cw::lib::Value arg_defvalue);
    ::cw::lib::Value get_5(::cw::lib::String arg_key, ::cw::lib::Value arg_defvalue);
    ::cw::lib::Value get_6(::cw::lib::String arg_key, ::cw::lib::Value arg_defvalue);
};

extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_CAN_UNCONSCIOUS;
extern std::map<::cw::lib::String, std::tuple<::cw::lib::String, bool>> g_bonus_dict;

bool is_noeffect(::cw::lib::String arg_element, std::shared_ptr<::cw::character::Character> arg_target);
bool check_noeffect(::cw::lib::String arg_effecttype, std::shared_ptr<::cw::character::Character> arg_target, bool arg_ignore_antimagic);
::cw::lib::Integer get_vocation_val(std::shared_ptr<::cw::character::Character> arg_ccard, std::tuple<::cw::lib::String, ::cw::lib::String> arg_vocation, bool arg_enhance_act);
::cw::lib::Integer get_vocation_level(std::shared_ptr<::cw::character::Character> arg_ccard, std::tuple<::cw::lib::String, ::cw::lib::String> arg_vocation, bool arg_enhance_act);
std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> get_effectivetargets(std::shared_ptr<::cw::header::CardHeader> arg_header, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets);
void main();
}

