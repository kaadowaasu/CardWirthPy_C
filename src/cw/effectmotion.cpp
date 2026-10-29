#include "cw/effectmotion.hpp"
#include "cw/advlog.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/deck.hpp"
#include "cw/dice.hpp"
#include "cw/header.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/animation.hpp"
#include "cw/event.hpp"
#include "cw/lib/python_integer.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <set>

namespace cw::effectmotion {
namespace {
using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using Decimal = ::cw::lib::Decimal;
using String = ::cw::lib::String;
using Value = ::cw::lib::Value;
using Character = ::cw::character::Character;
using Element = ::cw::data::CWPyElement;

template<class T> std::shared_ptr<T> object_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

String value_string(const Value& value, String fallback = {})
{
    if (const auto* text = std::get_if<String>(&value.storage())) return *text;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return decimal->to_string(true);
    if (const auto* real = std::get_if<Real>(&value.storage())) {
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<Real>::max_digits10) << *real;
        return text.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
    return fallback;
}

Integer real_to_integer(Real value, Integer fallback = 0)
{
    if (!std::isfinite(value)) return fallback;
    const Real truncated = std::trunc(value);
    // この半開区間の外から浮動小数点を整数へ変換すると、MSVC
    // では未定義となる。 旧 Real の境界でも検査を維持し
    // 、下の Decimal 値は正確な任意精度の表現を使う。
    const Real max_exclusive = static_cast<Real>((std::numeric_limits<Integer>::max)()) + 1.0;
    const Real min_inclusive = static_cast<Real>((std::numeric_limits<Integer>::lowest)());
    if (truncated >= max_exclusive || truncated < min_inclusive) return fallback;
    return static_cast<Integer>(truncated);
}

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) {
        try { return decimal->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* real = std::get_if<Real>(&value.storage())) return real_to_integer(*real, fallback);
    const auto text = value_string(value);
    if (const auto parsed = ::cw::lib::parse_python_integer(text)) return *parsed;
    return fallback;
}

bool value_bool(const Value& value, bool fallback = false)
{
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer != 0;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) return !decimal->is_zero();
    if (const auto* real = std::get_if<Real>(&value.storage())) return std::isfinite(*real) ? *real != 0.0 : fallback;
    const auto text = value_string(value);
    if (text.empty()) return fallback;
    return text == "True" || text == "true" || text == "1";
}

Integer clamp65536(Integer value) { return std::clamp(value, Integer{-65536}, Integer{65536}); }

Integer checked_product(Integer a, Integer b, const char* field)
{
    const auto lo = (std::numeric_limits<Integer>::min)();
    const auto hi = (std::numeric_limits<Integer>::max)();
    if ((a > 0 && ((b > 0 && a > hi / b) || (b < 0 && b < lo / a))) ||
        (a < 0 && ((b > 0 && a < lo / b) || (b < 0 && a < hi / b))))
        throw std::overflow_error(String("EffectMotion ") + field + ": integer overflow");
    return a * b;
}

Integer py_div(Integer a, Integer b)
{
    if (b == 0) return 0;
    Integer q = a / b, r = a % b;
    if (r != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

Integer py_mod(Integer a, Integer b)
{
    if (b == 0) return 0;
    Integer r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

String title_case(String value)
{
    bool cap = true;
    for (char& c : value) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalpha(u)) {
            c = static_cast<char>(cap ? std::toupper(u) : std::tolower(u));
            cap = false;
        } else cap = true;
    }
    return value;
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    if (!::cw::g_cwpy) return {};
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage()); p && *p)
        return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*p);
    return {};
}

bool version_lessthan_card(const String& version)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_sct) return false;
    const auto data = scenario_data();
    if (!data) return false;
    return ::cw::g_cwpy->m_sct->lessthan(version, data->get_versionhint(::cw::g_HINT_CARD));
}

std::optional<std::shared_ptr<Element>> resolve_carddata(const std::shared_ptr<Element>& beast)
{
    if (!beast) return std::nullopt;
    if (!::cw::g_cwpy) return beast;
    const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!p || !*p) return beast;
    if (auto data = std::dynamic_pointer_cast<::cw::data::ScenarioData>(*p))
        return data->get_carddata(beast, true, std::nullopt);
    if (auto data = std::dynamic_pointer_cast<::cw::data::SystemData>(*p))
        return data->get_carddata(beast, true, std::nullopt);
    return beast;
}

::cw::advlog::AdventurerLogger* advlog()
{
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_advlog.get() : nullptr;
}

void log_effect_failed(const std::shared_ptr<Character>& target)
{
    if (auto* log = advlog(); log && target)
        log->effect_failed(Value(std::static_pointer_cast<::cw::lib::Object>(target)), false);
}

bool action_enabled(const std::shared_ptr<Character>& target, Integer id)
{
    if (auto* card = dynamic_cast<::cw::sprite::card::CWPyCard*>(target.get())) {
        const auto it = card->m_actions.find(id);
        if (it != card->m_actions.end() && !it->second) return false;
    }
    return true;
}

bool has_actiondata(const std::shared_ptr<Character>& target)
{
    if (!target) return false;
    if (target->m_action_header_typed && *target->m_action_header_typed) return true;
    if (target->m_action_target_typed && !target->m_action_target_typed->is_none()) return true;
    if (!target->m_action_beasts_typed.empty()) return true;
    return !std::get<0>(target->m_actiondata).is_none() || !std::get<1>(target->m_actiondata).is_none() ||
           !std::get<2>(target->m_actiondata).is_none();
}

std::shared_ptr<::cw::lib::Object> cardgrp_object()
{
    if (!::cw::g_cwpy) return {};
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage()); p && *p)
        return *p;
    return {};
}

bool in_unconscious_list(const String& type)
{
    return type == "Heal" || type == "Paralyze" || type == "DisParalyze" || type == "Poison" ||
           type == "DisPoison" || type == "GetSkillPower" || type == "LoseSkillPower" || type == "VanishTarget";
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> header_value(const Value& value)
{
    if (auto header = object_value<::cw::header::CardHeader>(value)) return header;
    return std::nullopt;
}

std::optional<std::shared_ptr<Character>> character_value(const Value& value)
{
    if (auto character = object_value<Character>(value)) return character;
    return std::nullopt;
}

void set_effect_value(Value& dst, Integer value) { dst = Value(value); }
void set_effect_value(Value& dst, const Decimal& value) { dst = Value(value); }
void set_effect_value(Value& dst, const String& value) { dst = Value(value); }
void set_effect_value(Value& dst, bool value) { dst = Value(value); }

Integer roll(Integer times, Integer sides)
{
    if (times <= 0 || sides <= 0) return 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_dice) return ::cw::g_cwpy->m_dice->roll(times, sides);
    return times * ((sides + 1) / 2);
}

Integer char_bonus(const std::shared_ptr<Character>& ccard,
                   const std::tuple<String, String>& vocation, bool enhance_act)
{
    return ccard ? ccard->get_bonus(vocation, enhance_act) : 6;
}

String lower(String value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}

std::tuple<String, String, String, String, String, String, String, String> g_CAN_UNCONSCIOUS = {
    "Heal", "Paralyze", "DisParalyze", "Poison", "DisPoison", "GetSkillPower", "LoseSkillPower", "VanishTarget"};
std::map<String, std::tuple<String, bool>> g_bonus_dict = {{"Heal", {"get_targetingbonus", true}}};

void EffectParams::initialize()
{
    m_inusecard.reset();
    m_str_params.clear(); m_int_params.clear(); m_bool_params.clear(); m_ccard_params.clear();
}

void EffectParams::set_item(String key, Value value)
{
    if (const auto* text = std::get_if<String>(&value.storage())) m_str_params[key] = *text;
    else if (const auto* integer = std::get_if<Integer>(&value.storage())) {
        m_bool_params.erase(key);
        m_int_params[key] = *integer;
    } else if (const auto* boolean = std::get_if<bool>(&value.storage())) {
        // Python の bool は int なので、両方を一つの論理的な格納領域と
        // して扱う。 ネイティブの型付きマップは bool の Value を保持するた
        // め表現を分けるが、置き換えと既定値の型による処理は同じ一領域の動作に合わせる。
        m_int_params.erase(key);
        m_bool_params[key] = *boolean;
    } else m_ccard_params[key] = value.is_none() ? std::optional<Value>{} : std::optional<Value>{value};
}

String EffectParams::get(String key, String defvalue)
{
    const auto it = m_str_params.find(key); return it == m_str_params.end() ? defvalue : it->second;
}
bool EffectParams::get_2(String key, bool defvalue)
{
    // 生成済みの bool 返却オーバーロードは、ネイティブのスカラー境界である。 Python
    // の int/bool 共通領域から取得した整数は、真偽値に変換して返す。
    if (const auto it = m_int_params.find(key); it != m_int_params.end()) return it->second != 0;
    const auto it = m_bool_params.find(key); return it == m_bool_params.end() ? defvalue : it->second;
}
Integer EffectParams::get_3(String key, Integer defvalue)
{
    if (const auto it = m_int_params.find(key); it != m_int_params.end()) return it->second;
    // Python の bool 値に対しても、生成済みの整数の戻り値型を維持する。
    const auto it = m_bool_params.find(key); return it == m_bool_params.end() ? defvalue : static_cast<Integer>(it->second);
}
std::optional<Value> EffectParams::get_4(String key, Value defvalue)
{
    const auto result = get_5(std::move(key), std::move(defvalue));
    return result.is_none() ? std::optional<Value>{} : std::optional<Value>{result};
}
Value EffectParams::get_5(String key, Value defvalue)
{
    if (std::holds_alternative<String>(defvalue.storage())) {
        const auto it = m_str_params.find(key);
        return it == m_str_params.end() ? defvalue : Value(it->second);
    }
    if (std::holds_alternative<Integer>(defvalue.storage()) || std::holds_alternative<bool>(defvalue.storage())) {
        if (const auto it = m_int_params.find(key); it != m_int_params.end()) return Value(it->second);
        if (const auto it = m_bool_params.find(key); it != m_bool_params.end()) return Value(it->second);
        return defvalue;
    }
    const auto it = m_ccard_params.find(key);
    // 保存された None も存在する項目として扱い、None 以外の既定値で置き換えない。
    return it == m_ccard_params.end() ? defvalue : (it->second ? *it->second : Value{});
}
Value EffectParams::get_6(String key, Value defvalue) { return get_5(std::move(key), std::move(defvalue)); }

bool is_noeffect(String element, std::shared_ptr<Character> target)
{
    if (!target) return true;
    if (element == "Health") return target->m_feature["undead"];
    if (element == "Mind") return target->m_feature["automaton"];
    if (element == "Miracle") return !target->m_feature["unholy"];
    if (element == "Magic") return !target->m_feature["constructure"];
    if (element == "Fire") return target->m_resist["fire"];
    if (element == "Ice") return target->m_resist["ice"];
    return false;
}

bool check_noeffect(String effecttype, std::shared_ptr<Character> target, bool ignore_antimagic)
{
    if (!target) return true;
    const bool weapon = target->m_noeffect["weapon"];
    const bool magic = target->m_noeffect["magic"];
    const bool antimagic = !ignore_antimagic && target->is_antimagic();
    if (effecttype == "Physic") return weapon;
    if (effecttype == "Magic") return magic || antimagic;
    if (effecttype == "MagicalPhysic") return weapon && magic;
    if (effecttype == "PhysicalMagic") return weapon || magic || antimagic;
    return false;
}

void Effect::initialize(std::vector<std::shared_ptr<Element>> motions, std::shared_ptr<EffectParams> d, bool battlespeed)
{
    if (!d) throw std::invalid_argument("EffectParams is required");
    m_user_typed.reset(); m_inusecard_typed = d->m_inusecard; m_motions_typed.clear();
    if (const auto it = d->m_ccard_params.find("user"); it != d->m_ccard_params.end() && it->second)
        m_user_typed = object_value<Character>(*it->second);
    m_inusecard_typed = d->m_inusecard;
    m_user = m_user_typed ? Value(std::static_pointer_cast<::cw::lib::Object>(m_user_typed)) : Value{};
    m_inusecard = m_inusecard_typed ? Value(std::static_pointer_cast<::cw::lib::Object>(*m_inusecard_typed)) : Value{};
    m_level = Value(d->get_3("level", 0)); m_successrate = Value(d->get_3("successrate", 0));
    m_effecttype = Value(d->get("effecttype", "Physic")); m_resisttype = Value(d->get("resisttype", "Avoid"));
    m_soundpath = Value(d->get("soundpath", "")); m_volume = Value(d->get_3("volume", 100));
    m_loopcount = Value(d->get_3("loopcount", 1)); m_channel = Value(d->get_3("channel", 0));
    m_fade = Value(d->get_3("fadein", 0)); m_visualeffect = Value(d->get("visualeffect", "None"));
    m_battlespeed = Value(battlespeed); m_cardspeed = Value(d->get_3("cardspeed", -1));
    m_overridecardspeed = Value(d->get_2("overridecardspeed", false)); m_absorbto = Value(d->get("absorbto", "None"));
    m_refability = Value(d->get_2("refability", false)); m_vocation_typed.reset();
    if (value_bool(m_refability)) m_vocation_typed = {lower(d->get("physical", "Dex")), lower(d->get("mental", "Aggressive"))};
    m_vocation = m_vocation_typed; m_is_enhance_act = m_user_typed && m_inusecard_typed &&
        ((*m_inusecard_typed)->m_type == "ActionCard" || (*m_inusecard_typed)->m_type == "SkillCard");
    for (const auto& motion_data : motions) {
        if (!motion_data) continue;
        auto motion = std::make_shared<EffectMotion>();
        if (m_user_typed && m_inusecard_typed) motion->initialize(motion_data, Value(std::static_pointer_cast<::cw::lib::Object>(m_user_typed)), m_inusecard_typed, d->get_3("level", 0), value_bool(m_refability), m_vocation_typed, d->get("absorbto", "None"), std::nullopt);
        else motion->initialize(motion_data, std::nullopt, std::nullopt, d->get_3("level", 0), value_bool(m_refability), m_vocation_typed, d->get("absorbto", "None"), std::nullopt);
        m_motions_typed.push_back(motion);
    }
    m_motions = Value(std::make_shared<::cw::data::VariantSequence>());
    auto sequence = object_value<::cw::data::VariantSequence>(m_motions);
    if (sequence) for (const auto& motion : m_motions_typed) sequence->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(motion));
    // refabilityがfalse
    // の効果モーションは、両実装とも自
    // 身の初期化時に状態を更新する。
}

void Effect::update_status(std::optional<std::shared_ptr<Character>> selectedmember)
{
    m__level = Value(value_bool(m_refability) ? (selectedmember && *selectedmember ? (*selectedmember)->m_level : 0) : clamp65536(m_user_typed ? m_user_typed->m_level : value_integer(m_level)));
    for (const auto& motion : m_motions_typed) if (motion) motion->update_status(selectedmember ? std::optional<Value>(Value(std::static_pointer_cast<::cw::lib::Object>(*selectedmember))) : std::nullopt);
}

Integer Effect::get_level() { return value_integer(m__level); }

bool Effect::apply(Value target, bool event, std::optional<std::shared_ptr<Character>> selectedmember)
{
    auto character = object_value<Character>(target);
    return character && check_enabledtarget(target, event) ? apply_charactercard(target, event, selectedmember) : false;
}

bool Effect::apply_charactercard(Value target, bool event, std::optional<std::shared_ptr<Character>> selectedmember)
{
    auto character = object_value<Character>(target); if (!character) return false;
    if (!event && character->is_reversed()) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->is_battlestatus())
            ::cw::g_cwpy->m_event->m_is_changestate = true;
        return false;
    }
    if (character->is_unconscious() && !has_motions({"Heal", "Paralyze", "DisParalyze", "Poison", "DisPoison", "GetSkillPower", "LoseSkillPower", "VanishTarget"}) && !has_addablebeast(target) && !has_removablebeast(target)) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event && ::cw::g_cwpy->is_battlestatus())
            ::cw::g_cwpy->m_event->m_is_changestate = true;
        return false;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->m_is_changestate = true;
    const Integer successrate = value_integer(m_successrate);
    bool allmissed = successrate <= -5, allsuccess = successrate >= 5, success_res = false, success_avo = false;
    auto absorb_name = value_string(m_absorbto); std::shared_ptr<Character> absorbto;
    if (absorb_name == "Selected") absorbto = selectedmember ? *selectedmember : nullptr;
    else if (absorb_name == "User") absorbto = m_user_typed;
    const Integer userlife = absorbto ? absorbto->m_life : 0;
    const String resisttype = value_string(m_resisttype, "Avoid");
    bool noeffect = check_noeffect(character);
    if (allmissed) {
        success_res = resisttype == "Resist" && character->is_resistable(); success_avo = resisttype == "Avoid" && character->is_avoidable();
    } else if (allsuccess) {
        if (noeffect) { success_res = resisttype == "Resist"; success_avo = resisttype == "Avoid"; }
    } else if (noeffect) {
        success_res = resisttype == "Resist"; success_avo = resisttype == "Avoid";
    } else {
        success_res = check_resist(character, selectedmember); success_avo = check_avoid(character, selectedmember);
    }
    if (!success_res && resisttype == "Resist" && !character->is_resistable(false)) allsuccess = true;
    if (!success_avo && resisttype == "Avoid" && !character->is_avoidable(false)) allsuccess = true;
    Integer damage_count = count_motion("damage") + count_motion("absorb");
    const auto card_target = object_value<::cw::sprite::card::CWPyCard>(target);
    // Python と同様、効果間で実体のポケットへの参照を維持する。 先の召
    // 喚や防御カードの消費で、次のダメージ効果までに内容が変わる場合がある。
    const auto& beast_cards = std::get<2>(character->m_cardpocket_typed);
    std::shared_ptr<::cw::header::CardHeader> guardcard;
    std::vector<std::shared_ptr<::cw::header::CardHeader>> consume;
    if (!allsuccess) {
        std::shared_ptr<::cw::header::CardHeader> used;
        if (character->m_action_header_typed) used = *character->m_action_header_typed;
        else if (auto header = object_value<::cw::header::CardHeader>(std::get<1>(character->m_actiondata))) used = header;
        else if (character->m_deck) if (auto current = character->m_deck->get_used()) used = *current;
        if (used) {
            const auto [avoid, resist, defense] = used->get_enhance_val_used();
            (void)defense;
            if ((resisttype == "Avoid" && avoid != 0) || (resisttype == "Resist" && resist != 0)) guardcard = used;
        }
        for (const auto& header : beast_cards) {
            if (!header) continue;
            const auto [avoid, resist, defense] = header->get_enhance_val();
            (void)defense;
            if ((resisttype == "Avoid" && avoid != 0) || (resisttype == "Resist" && resist != 0)) {
                if (!guardcard) guardcard = header;
                consume.push_back(header);
            }
        }
        if (guardcard && ::cw::g_cwpy && card_target) {
            ::cw::g_cwpy->play_sound("equipment", true, 100, 1, 0, 0, false);
            ::cw::g_cwpy->set_guardcardimg(card_target, guardcard);
            ::cw::g_cwpy->add_lazydraw_rect(card_target->m_rect);
            if (!::cw::g_cwpy->m_guardcards.empty())
                ::cw::g_cwpy->add_lazydraw(std::static_pointer_cast<::cw::lib::Object>(::cw::g_cwpy->m_guardcards.back()));
            const bool can_skip = ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_can_skipanimation;
            ::cw::g_cwpy->wait_frame((_get_cardspeed(card_target) + 1) * 2, can_skip, std::nullopt, 0);
            ::cw::g_cwpy->add_lazydraw_rect(card_target->m_rect);
            if (!::cw::g_cwpy->m_guardcards.empty())
                ::cw::g_cwpy->add_lazydraw(std::static_pointer_cast<::cw::lib::Object>(::cw::g_cwpy->m_guardcards.back()));
            ::cw::g_cwpy->clear_guardcardimg();
        }
        for (const auto& header : consume) if (header) header->set_uselimit(-1, false);
    }
    if (!success_avo && ::cw::g_cwpy)
        ::cw::g_cwpy->play_sound_with(value_string(m_soundpath), m_inusecard_typed,
                                      value_integer(m_volume, 100), value_integer(m_loopcount, 1),
                                      value_integer(m_channel), value_integer(m_fade));
    if (success_avo) {
        if (::cw::g_cwpy) {
            ::cw::g_cwpy->play_sound("avoid", true, 100, 1, 0, 0, false);
            if (card_target) ::cw::g_cwpy->add_lazydraw_rect(card_target->m_rect);
            ::cw::g_cwpy->wait_frame(1, ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_can_skipanimation,
                                     std::nullopt, 0);
        }
        if (!m_motions_typed.empty()) {
            if (noeffect) log_effect_failed(character);
            else if (auto* log = advlog()) log->avoid(character);
        }
        return false;
    }
    if (noeffect || (success_res && damage_count == 0)) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("ineffective", true, 100, 1, 0, 0, false);
        if (card_target) animate(card_target, true);
        if (!m_motions_typed.empty()) if (auto* log = advlog()) log->noeffect(character);
        return false;
    }
    bool effectual = false;
    for (const auto& motion : m_motions_typed) {
        if (!motion) continue;
        motion->m_absorber = absorbto ? std::optional<Value>(Value(std::static_pointer_cast<::cw::lib::Object>(absorbto))) : std::nullopt;
        effectual = motion->apply(character, success_res) || effectual;
        motion->m_absorber.reset();
        if ((lower(value_string(motion->m_type)) == "damage" || lower(value_string(motion->m_type)) == "absorb") && motion->can_apply(character)) {
            // 消費によって実体のポケットから削除される場合があるため、消費前に集める。
            consume.clear();
            for (const auto& header : beast_cards)
                if (header && std::get<2>(header->get_enhance_val()) != 0) consume.push_back(header);
            for (const auto& header : consume) header->set_uselimit(-1, false);
        }
    }
    if (!effectual) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("ineffective", true, 100, 1, 0, 0, false);
        if (!m_motions_typed.empty()) log_effect_failed(character);
    }
    if (!character->is_vanished()) {
        if (character->is_unconscious()) character->set_unconsciousstatus();
        if (card_target) animate(card_target, true);
    }
    if (absorbto && count_motion("absorb") > 0 && userlife < absorbto->m_life && !absorbto->is_vanished()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("bind", true, 100, 1, 0, 0, false);
        const auto absorb_card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(absorbto);
        if (absorb_card) {
            const auto saved_override = ::cw::g_cwpy ? ::cw::g_cwpy->m_override_dealspeed : Value{};
            const Integer saved_force = ::cw::g_cwpy ? ::cw::g_cwpy->m_force_dealspeed : -1;
            // Python の同行者の最初の表示アニメーションは、回転処理の try/finally
            // より前に行われる。 そこで失敗すると一時的な生命力とグルー
            // プ所属が残るため、元のその動作を回転の後処理と混同しない。
            if (std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(absorb_card)) {
                // Python は Sprite を直接追加するが、ネイティ
                // ブのカードは描画用プロキシーを使う。 最初の表示アニメーションが失敗する
                // 前に、同じ所属・レイヤー変更を共通レジストリーへ反映する。
                ::cw::sprite::card::refresh_card_layer(*absorb_card,
                    ::cw::layer_val(absorb_card->m_tlayer));
                const Integer newlife = absorbto->m_life;
                absorbto->m_life = userlife;
                ::cw::animation::animate_sprite(absorb_card, "deal", true, false, false, value_bool(m_battlespeed));
                absorbto->m_life = newlife;
            }
            const auto finish_absorb_rotation = [&] {
                if (std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(absorb_card) && ::cw::g_cwpy) {
                    ::cw::g_cwpy->wait_frame((_get_cardspeed(card_target) + 1) * 2,
                        ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_can_skipanimation, std::nullopt, 0);
                    ::cw::animation::animate_sprite(absorb_card, "hide", true, false, false, value_bool(m_battlespeed));
                    ::cw::sprite::card::remove_from_cardgrp(*absorb_card);
                }
                if (::cw::g_cwpy) { ::cw::g_cwpy->m_override_dealspeed = saved_override; ::cw::g_cwpy->m_force_dealspeed = saved_force; }
            };
            try {
                if (::cw::g_cwpy && value_integer(m_cardspeed, -1) != -1) {
                    if (value_bool(m_overridecardspeed)) ::cw::g_cwpy->m_force_dealspeed = value_integer(m_cardspeed);
                    else ::cw::g_cwpy->m_override_dealspeed = Value(value_integer(m_cardspeed));
                }
                absorb_card->m_hide_inusecardimg = false;
                ::cw::animation::animate_sprite(absorb_card, "hide", true, false, false, value_bool(m_battlespeed));
                absorb_card->m_hide_inusecardimg = true;
                // Python の update_image() の既定値は update_statusimg=False
                // である。 ここでは条件付きの顔画像レイヤーと生命力ゲー
                // ジへ効果結果を反映するため、全体の再構築が必要である。
                absorb_card->update_image(false, std::nullopt);
                ::cw::animation::animate_sprite(absorb_card, "deal", true, false, false, value_bool(m_battlespeed));
            } catch (...) {
                // Python の finally と同様、回転エラーでも
                // 後処理を行う。 後処理自身の失敗は先のエラーを置き換え、
                // 以後の復元を中断する。正常終了では両方の速度を復元する。
                finish_absorb_rotation();
                throw;
            }
            finish_absorb_rotation();
        }
    }
    return true;
}

Integer Effect::_get_cardspeed(std::shared_ptr<::cw::sprite::card::CWPyCard> target) { return target ? target->get_dealspeed(value_bool(m_battlespeed)) : 0; }
bool Effect::check_noeffect(std::shared_ptr<Character> target) { return ::cw::effectmotion::check_noeffect(value_string(m_effecttype, "Physic"), target, false); }
bool Effect::check_avoid(std::shared_ptr<Character> target, std::optional<std::shared_ptr<Character>> selectedmember)
{
    if (value_string(m_resisttype, "Avoid") != "Avoid" || !target || !target->is_avoidable()) return false;
    const auto bonus = target->get_enhance_avo(); if (bonus >= 10) return true; if (bonus <= -10) return false;
    Integer userbonus = 6;
    if (value_bool(m_refability)) userbonus = selectedmember && *selectedmember && m_vocation_typed ? char_bonus(*selectedmember, *m_vocation_typed, true) : 6;
    else if (m_user_typed && m_inusecard_typed) userbonus = m_user_typed->get_bonus({value_string(std::get<0>((*m_inusecard_typed)->m_vocation)), value_string(std::get<1>((*m_inusecard_typed)->m_vocation))}, m_is_enhance_act);
    return target->decide_outcome(m_user_typed ? m_user_typed->m_level : get_level(), {"agl", "cautious"}, userbonus, bonus, value_integer(m_successrate));
}
bool Effect::check_resist(std::shared_ptr<Character> target, std::optional<std::shared_ptr<Character>> selectedmember)
{
    if (value_string(m_resisttype, "Avoid") != "Resist" || !target || !target->is_resistable()) return false;
    const auto bonus = target->get_enhance_res(); if (bonus >= 10) return true; if (bonus <= -10) return false;
    Integer userbonus = 6;
    if (value_bool(m_refability)) userbonus = selectedmember && *selectedmember && m_vocation_typed ? char_bonus(*selectedmember, *m_vocation_typed, true) : 6;
    else if (m_user_typed && m_inusecard_typed) userbonus = m_user_typed->get_bonus({value_string(std::get<0>((*m_inusecard_typed)->m_vocation)), value_string(std::get<1>((*m_inusecard_typed)->m_vocation))}, m_is_enhance_act);
    return target->decide_outcome(m_user_typed ? m_user_typed->m_level : get_level(), {"min", "brave"}, userbonus, bonus, value_integer(m_successrate));
}

void Effect::animate(std::shared_ptr<::cw::sprite::card::CWPyCard> target, bool update_image)
{
    if (!target) return;
    const auto saved_override = ::cw::g_cwpy ? ::cw::g_cwpy->m_override_dealspeed : Value{};
    const Integer saved_force = ::cw::g_cwpy ? ::cw::g_cwpy->m_force_dealspeed : -1;
    try {
        if (::cw::g_cwpy && value_integer(m_cardspeed, -1) != -1) {
            if (value_bool(m_overridecardspeed)) ::cw::g_cwpy->m_force_dealspeed = value_integer(m_cardspeed);
            else ::cw::g_cwpy->m_override_dealspeed = Value(value_integer(m_cardspeed));
        }
        _animate_impl(std::move(target), update_image);
    } catch (...) {
        if (::cw::g_cwpy) { ::cw::g_cwpy->m_override_dealspeed = saved_override; ::cw::g_cwpy->m_force_dealspeed = saved_force; }
        throw;
    }
    if (::cw::g_cwpy) { ::cw::g_cwpy->m_override_dealspeed = saved_override; ::cw::g_cwpy->m_force_dealspeed = saved_force; }
}
void Effect::_animate_impl(std::shared_ptr<::cw::sprite::card::CWPyCard> target, bool update_image)
{
    if (!target) return;
    const bool battlespeed = value_bool(m_battlespeed);
    const bool can_skip = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_can_skipanimation;
    const auto invalidate = [&] {
        if (::cw::g_cwpy) ::cw::g_cwpy->add_lazydraw_rect(target->m_rect);
    };
    if (target->m_status == "hidden") {
        if (update_image) { target->update_image(false, std::nullopt); invalidate(); }
        if (::cw::g_cwpy && !value_string(m_soundpath).empty() && ::cw::g_cwpy->has_sound(value_string(m_soundpath)))
            ::cw::g_cwpy->wait_frame((_get_cardspeed(target) + 1) * 2, can_skip, std::nullopt, 0);
        return;
    }
    const String visual = value_string(m_visualeffect, "None");
    if (visual == "Horizontal") {
        ::cw::animation::animate_sprite(target, "lateralvibe", true, false, false, battlespeed);
        if (update_image) { target->update_image(false, std::nullopt); invalidate(); }
    } else if (visual == "Vertical") {
        ::cw::animation::animate_sprite(target, "axialvibe", true, false, false, battlespeed);
        if (update_image) { target->update_image(false, std::nullopt); invalidate(); }
    } else if (visual == "Reverse") {
        target->m_hide_inusecardimg = false;
        ::cw::animation::animate_sprite(target, "hide", true, false, false, battlespeed);
        target->m_hide_inusecardimg = true;
        if (update_image) target->update_image(false, std::nullopt);
        ::cw::animation::animate_sprite(target, "deal", true, false, false, battlespeed);
    } else {
        if (update_image) { target->update_image(false, std::nullopt); invalidate(); }
        if (::cw::g_cwpy) ::cw::g_cwpy->wait_frame(1, can_skip, std::nullopt, 0);
    }
}

bool Effect::check_enabledtarget(Value target, bool event)
{
    auto character = object_value<Character>(target);
    auto card = object_value<::cw::sprite::card::CWPyCard>(target);
    const auto status = card ? card->m_status : (character ? character->get_card_status() : String{});
    const bool player_or_friend = character && (dynamic_cast<::cw::character::Player*>(character.get()) ||
                                                dynamic_cast<::cw::character::Friend*>(character.get()));
    if (status == "hidden" && !player_or_friend) return false;
    if (!character) return true;
    if (character->is_vanished() || (!event && character->is_reversed())) return false;
    if (m_motions_typed.empty()) return true;
    for (const auto& motion : m_motions_typed) if (motion && (!character->is_unconscious() || in_unconscious_list(value_string(motion->m_type)) || motion->has_addablebeast(target) || motion->has_removablebeast(target))) return true;
    return false;
}
bool Effect::has_addablebeast(Value target) { for (const auto& m : m_motions_typed) if (m && m->has_addablebeast(target)) return true; return false; }
bool Effect::has_removablebeast(Value target) { for (const auto& m : m_motions_typed) if (m && m->has_removablebeast(target)) return true; return false; }
bool Effect::has_motions(std::vector<String> motiontypes) { for (const auto& t : motiontypes) if (has_motion(t)) return true; return false; }
bool Effect::has_motion(String motiontype) { motiontype = lower(std::move(motiontype)); for (const auto& m : m_motions_typed) if (m && lower(value_string(m->m_type)) == motiontype) return true; return false; }
Integer Effect::count_motion(String motiontype) { motiontype = lower(std::move(motiontype)); Integer n = 0; for (const auto& m : m_motions_typed) if (m && lower(value_string(m->m_type)) == motiontype) ++n; return n; }

void EffectMotion::initialize(std::shared_ptr<Element> data, std::optional<Value> user, std::optional<std::shared_ptr<::cw::header::CardHeader>> header, Integer targetlevel, bool refability, std::optional<std::tuple<String, String>> vocation, String absorbto, std::optional<Value> selectedmember)
{
    if (!data) throw std::invalid_argument("motion data is required");
    m_data_typed = data; m_type_typed = data->get("type", ""); m_element_typed = data->get("element", "All"); m_damagetype_typed = data->get("damagetype", "");
    // EffectMotion は値・持続時間を <Motion> の属性に保持する
    // 。Python は明示的に int(data.get(...)) を使い、 int(float(text))
    // を行う汎用 XML の getint() は使わない。 不正
    // な式や整数でない文字列を丸めず、イベントのエラー境界へ伝える。
    m_value = ::cw::lib::require_python_integer(data->get("value", "0"));
    m_duration = ::cw::lib::require_python_integer(data->get("duration", "0"));
    m_targetlevel_typed = targetlevel; m_absorbto_typed = absorbto;
    m_type = Value(m_type_typed); m_element = Value(m_element_typed); m_damagetype = Value(m_damagetype_typed); m_refability = Value(refability); m_absorbto = Value(absorbto); m_user = user.value_or(Value{});
    m_user_typed = user ? object_value<Character>(*user) : nullptr; m_cardheader_typed = header.value_or(nullptr); m_cardheader = m_cardheader_typed ? Value(std::static_pointer_cast<::cw::lib::Object>(m_cardheader_typed)) : Value{};
    m_is_enhance_act = m_cardheader_typed && (m_cardheader_typed->m_type == "ActionCard" || m_cardheader_typed->m_type == "SkillCard");
    m_vocation_typed = vocation; m__vocation = vocation ? Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::data::VariantSequence>())) : Value{};
    m_beasts_typed.clear(); if (const auto beasts = data->find("Beasts")) for (const auto& child : (*beasts)->m_children) if (child) m_beasts_typed.push_back(child);
    m_beasts = Value(std::make_shared<::cw::data::VariantSequence>());
    if (const auto seq = object_value<::cw::data::VariantSequence>(m_beasts)) for (const auto& beast : m_beasts_typed) seq->m_values.emplace_back(std::static_pointer_cast<::cw::lib::Object>(beast));
    m_selectedmember_typed = selectedmember ? character_value(*selectedmember) : std::nullopt;
    if (refability) m_vocation_typed = vocation; else update_status(std::nullopt);
}

void EffectMotion::update_status(std::optional<Value> selectedmember)
{
    m_selectedmember_typed = selectedmember ? character_value(*selectedmember) : std::nullopt;
    if (value_bool(m_refability)) {
        auto ccard = m_selectedmember_typed ? *m_selectedmember_typed : nullptr;
        m__enhance_act = 0; m__vocation_val = ccard && m_vocation_typed ? ::cw::effectmotion::get_vocation_val(ccard, *m_vocation_typed, true) : 6; m__vocation_level = ccard && m_vocation_typed ? ::cw::effectmotion::get_vocation_level(ccard, *m_vocation_typed, true) : 2; m__level = Value(ccard ? ccard->m_level : 0);
    } else {
        m__enhance_act = m_is_enhance_act && m_user_typed ? m_user_typed->get_enhance_act() : 0;
        m__vocation_val = m_cardheader_typed && m_user_typed ? m_cardheader_typed->get_vocation_val(m_user_typed, false) : 6;
        m__vocation_level = m_cardheader_typed && m_user_typed ? m_cardheader_typed->get_vocation_level(m_user_typed, m_is_enhance_act) : 2;
        m__level = Value(clamp65536(m_user_typed ? m_user_typed->m_level : m_targetlevel_typed));
    }
}
Integer EffectMotion::get_vocation_val() { return m__vocation_val; }
Integer EffectMotion::get_vocation_level() { return m__vocation_level; }
Integer EffectMotion::get_enhance_act() { return m__enhance_act; }
Integer EffectMotion::get_level() { return value_integer(m__level); }
bool EffectMotion::is_effectcontent() { return !m_cardheader_typed; }
Integer EffectMotion::calc_skillpowervalue() { return m_damagetype_typed == "Fixed" ? m_value : 999; }

// Python は任意精度のダメージ整数を返せるが、このネイティブ API と Character
// の生命力は符号付き 64 ビット Integer を使う。 表現不能な最終
// 値は検査付きの演算で拒否し、ダメージを折り返さない。 下の正確な Decimal
// の代替処理で、積を割った結果が表現可能な場合は利用できるようにする。
Integer EffectMotion::calc_effectvalue(std::shared_ptr<Character> target, bool physical)
{
    if (!target) return 0; Integer value = m_value; const Integer minvalue = (m_type_typed == "Heal" || m_type_typed == "Damage" || m_type_typed == "Absorb") ? 1 : 0;
    if (m_damagetype_typed == "Fixed") return std::max(minvalue, value);
    if (m_damagetype_typed == "Max") return target->m_maxlife;
    if (value <= 0) return minvalue;
    if (m_damagetype_typed == "LevelRatio") {
        Integer bonus = get_vocation_val() + get_enhance_act();
        bonus = py_div(bonus, 2) + py_mod(bonus, 2);
        const auto factor = get_level() + bonus;
        try {
            value = checked_product(value, factor, "Motion/@value LevelRatio");
            value = py_div(value, 2) + py_mod(value, 2);
        } catch (const std::overflow_error&) {
            // Python は任意精度の積を除算してから効果値を返すため
            // 、2^62 * 2 / 2 のような大きい中間積自体はエラ
            // ーではない。 この稀な経路だけで既存の正確な共通 Decimal
            // 実装を使い、 Windows/Wasm 向けに __int128
            // やプラットフォーム固有演算を必須にしない。
            ::cw::lib::DecimalContext exact;
            exact.precision = 0;
            const auto rounded = Decimal(value).multiply(Decimal(factor), exact)
                .divide(Decimal(Integer{2}), exact).ceil();
            // この後の弱点補正でも、ゼロを超えることはない。
            if (rounded.compare(Decimal(Integer{-10})) <= 0) return minvalue;
            value = rounded.to_integer();
        }
    }
    if (is_weakness(target)) {
        if (value > (std::numeric_limits<Integer>::max)() - 10)
            throw std::overflow_error("EffectMotion Motion/@value weakness: integer overflow");
        value += 10;
    }
    if (physical) return std::max(minvalue, value);
    Integer out = roll(py_div(value, 5), 10); const Integer sides = py_mod(value, 5) * 2; if (value > 0 && sides) out += roll(1, sides);
    return std::max(minvalue, out);
}
Integer EffectMotion::calc_durationvalue(std::shared_ptr<Character> target, bool enhance)
{
    const Integer minvalue = enhance && m_duration <= 0 ? 0 : 1; Integer level = get_vocation_level(); if (is_weakness(target)) ++level;
    // Python は任意精度整数で計算してから 999 に制限する。 最小の倍率
    // 50% でも、duration が 1998 以上なら同じ上限に達する。
    if (m_duration <= 0) return minvalue;
    if (m_duration >= 1998) return 999;
    const Integer percent = level <= 0 ? 50 : level == 1 ? 80 : level == 2 ? 100 : level == 3 ? 120 : 150;
    return std::clamp(m_duration * percent / 100, minvalue, Integer{999});
}
// Python の任意精度の防御計算の積は、ネイティブの符号付き 64
// ビットのダメージ範囲を超え得る。 表現可能な結果では切り下げ除算と余り
// を維持し、増幅した値が回復や無傷へ折り返す代わりに桁あふれを通知する。
Integer EffectMotion::calc_defensedvalue(Integer value, std::shared_ptr<Character> target)
{
    if (!value || !target) return 0;
    const auto enhance = target->get_enhance_def();
    if (enhance >= 10) return 0;
    if (enhance <= -10) return checked_product(value, 4, "Motion/@value defense");
    if (std::isnan(enhance)) throw std::invalid_argument("EffectMotion defense is NaN");
    const Integer percent = static_cast<Integer>(100 - enhance * 10);
    // 乗算より前に除算し、余りを保持して Python の切り下げ結果を維持する。
    const auto scaled = checked_product(py_div(value, 100), percent, "Motion/@value defense");
    const auto remainder = py_mod(value, 100) * percent / 100;
    if (scaled > (std::numeric_limits<Integer>::max)() - remainder)
        throw std::overflow_error("EffectMotion Motion/@value defense: integer overflow");
    return std::max<Integer>(1, scaled + remainder);
}
bool EffectMotion::is_noeffect(std::shared_ptr<Character> target) { return ::cw::effectmotion::is_noeffect(m_element_typed, target); }
bool EffectMotion::is_weakness(std::shared_ptr<Character> target) { return target && ((m_element_typed == "Fire" && target->m_weakness["fire"]) || (m_element_typed == "Ice" && target->m_weakness["ice"])); }
bool EffectMotion::can_apply(std::shared_ptr<Character> target) { return target && !is_noeffect(target) && (!target->is_unconscious() || in_unconscious_list(m_type_typed) || has_addablebeast(Value(std::static_pointer_cast<::cw::lib::Object>(target))) || has_removablebeast(Value(std::static_pointer_cast<::cw::lib::Object>(target)))); }
bool EffectMotion::has_addablebeast(Value target)
{
    if (m_type_typed != "SummonBeast") return false;
    for (const auto& beast : m_beasts_typed) {
        const auto resolved = resolve_carddata(beast);
        if (!resolved || !*resolved) continue;
        if (!::cw::header::is_removewithstatus(*resolved, target)) return true;
    }
    return false;
}
bool EffectMotion::has_removablebeast(Value target)
{
    if (m_type_typed != "VanishBeast") return false;
    auto character = object_value<Character>(target);
    if (!character) return false;
    for (const auto& beast : character->get_pocketcards(::cw::g_POCKET_BEAST))
        if (beast && !beast->m_attachment) return true;
    return false;
}

bool EffectMotion::apply(std::shared_ptr<Character> target, bool success_res)
{
    if (!can_apply(target)) return false;
    const auto type = lower(m_type_typed);
    if (type == "heal") return heal_motion(target, success_res); if (type == "damage") return damage_motion(target, success_res); if (type == "absorb") return absorb_motion(target, success_res);
    if (type == "paralyze") return paralyze_motion(target, success_res); if (type == "disparalyze") return disparalyze_motion(target, success_res); if (type == "poison") return poison_motion(target, success_res); if (type == "dispoison") return dispoison_motion(target, success_res);
    if (type == "getskillpower") return getskillpower_motion(target, success_res); if (type == "loseskillpower") return loseskillpower_motion(target, success_res); if (type == "sleep") return sleep_motion(target, success_res); if (type == "confuse") return confuse_motion(target, success_res); if (type == "overheat") return overheat_motion(target, success_res); if (type == "brave") return brave_motion(target, success_res); if (type == "panic") return panic_motion(target, success_res); if (type == "normal") return normal_motion(target, success_res);
    if (type == "bind") return bind_motion(target, success_res); if (type == "disbind") return disbind_motion(target, success_res); if (type == "silence") return silence_motion(target, success_res); if (type == "dissilence") return dissilence_motion(target, success_res); if (type == "faceup") return faceup_motion(target, success_res); if (type == "facedown") return facedown_motion(target, success_res); if (type == "antimagic") return antimagic_motion(target, success_res); if (type == "disantimagic") return disantimagic_motion(target, success_res);
    if (type == "enhanceaction") return enhanceaction_motion(target, success_res); if (type == "enhanceavoid") return enhanceavoid_motion(target, success_res); if (type == "enhanceresist") return enhanceresist_motion(target, success_res); if (type == "enhancedefense") return enhancedefense_motion(target, success_res);
    if (type == "vanishtarget") return vanishtarget_motion(target, success_res); if (type == "vanishcard") return vanishcard_motion(target, success_res); if (type == "vanishbeast") return vanishbeast_motion(target, success_res); if (type == "dealattackcard") return dealattackcard_motion(target, success_res); if (type == "dealpowerfulattackcard") return dealpowerfulattackcard_motion(target, success_res); if (type == "dealcriticalattackcard") return dealcriticalattackcard_motion(target, success_res); if (type == "dealfeintcard") return dealfeintcard_motion(target, success_res); if (type == "dealdefensecard") return dealdefensecard_motion(target, success_res); if (type == "dealdistancecard") return dealdistancecard_motion(target, success_res); if (type == "dealconfusecard") return dealconfusecard_motion(target, success_res); if (type == "dealskillcard") return dealskillcard_motion(target, success_res); if (type == "cancelaction") return cancelaction_motion(target, success_res); if (type == "summonbeast") return summonbeast_motion(target, success_res); if (type == "noeffect") return noeffect_motion(target, success_res);
    return false;
}

bool EffectMotion::heal_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const Integer orig = calc_effectvalue(t, false);
    const Integer oldlife = t->m_life;
    const auto value = t->set_life(orig);
    if (value > 0) if (auto* log = advlog()) log->heal_motion(t, orig, t->m_life, oldlife);
    return value > 0;
}
bool EffectMotion::damage_motion(std::shared_ptr<Character> t, bool r)
{
    Integer v = calc_effectvalue(t, false);
    if (r) v = v / 2 + v % 2;
    if (m_damagetype_typed != "Fixed" && (m_damagetype_typed != "Max" || version_lessthan_card("1.20")))
        v = calc_defensedvalue(v, t);
    const Integer oldlife = t->m_life;
    const Integer orig = v;
    t->set_life(-v);
    const bool dissleep = t->is_sleep();
    if (dissleep) t->set_mentality("Normal", 0);
    if (v > 0) if (auto* log = advlog()) log->damage_motion(t, orig, t->m_life, oldlife, dissleep);
    return v > 0;
}
bool EffectMotion::absorb_motion(std::shared_ptr<Character> t, bool r)
{
    Integer v = calc_effectvalue(t, false);
    if (r) v = v / 2 + v % 2;
    if (m_damagetype_typed != "Fixed" && (m_damagetype_typed != "Max" || version_lessthan_card("1.20")))
        v = calc_defensedvalue(v, t);
    const Integer oldlife = t->m_life;
    const Integer orig = v;
    const auto dealt = -(t->set_life(-v));
    const bool dissleep = t->is_sleep();
    if (dissleep) t->set_mentality("Normal", 0);
    auto absorber = m_absorber ? object_value<Character>(*m_absorber) : nullptr;
    Integer ulife = 0, oldulife = 0;
    if (absorber && !absorber->is_vanished()) {
        oldulife = absorber->m_life;
        absorber->set_life(dealt);
        ulife = absorber->m_life;
    }
    if (dealt > 0) if (auto* log = advlog())
        log->absorb_motion(absorber ? std::optional<std::shared_ptr<Character>>(absorber) : std::nullopt,
                           dealt, ulife, oldulife, t, orig, t->m_life, oldlife, dissleep);
    return dealt > 0;
}
bool EffectMotion::paralyze_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    Integer v = m_damagetype_typed == "Max" ? 40 : calc_effectvalue(t, true);
    const auto oldvalue = t->m_paralyze;
    t->set_paralyze(v);
    if (v > 0) if (auto* log = advlog()) log->paralyze_motion(t, t->m_paralyze, oldvalue);
    return v > 0;
}
bool EffectMotion::disparalyze_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    Integer v = m_damagetype_typed == "Max" ? 40 : calc_effectvalue(t, true);
    const auto oldvalue = t->m_paralyze;
    v = t->set_paralyze(-v);
    if (v < 0) if (auto* log = advlog()) log->disparalyze_motion(t, t->m_paralyze, oldvalue);
    return v < 0;
}
bool EffectMotion::poison_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    Integer v = m_damagetype_typed == "Max" ? 40 : calc_effectvalue(t, true);
    const auto oldvalue = t->m_poison;
    t->set_poison(v);
    if (v > 0) if (auto* log = advlog()) log->poison_motion(t, t->m_poison, oldvalue);
    return v > 0;
}
bool EffectMotion::dispoison_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    Integer v = m_damagetype_typed == "Max" ? 40 : calc_effectvalue(t, true);
    const auto oldvalue = t->m_poison;
    v = t->set_poison(-v);
    if (v < 0) if (auto* log = advlog()) log->dispoison_motion(t, t->m_poison, oldvalue);
    return v < 0;
}
bool EffectMotion::getskillpower_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto value = calc_skillpowervalue();
    t->set_skillpower(value);
    if (auto* log = advlog()) log->getskillpower_motion(t, value);
    return true;
}
bool EffectMotion::loseskillpower_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto value = calc_skillpowervalue();
    t->set_skillpower(-value);
    if (auto* log = advlog()) log->loseskillpower_motion(t, value);
    return true;
}
bool EffectMotion::mentality(std::shared_ptr<Character> t, bool r)
{
    if (r || t->m_reversed) return false;
    const auto name = title_case(m_type_typed);
    const auto oldmentality = t->m_mentality;
    const auto oldduration = t->m_mentality_dur;
    const auto duration = name == "Normal" ? 0 : calc_durationvalue(t, false);
    bool eff = false;
    if (name == "Normal" || duration == 0) {
        eff = t->m_mentality != "Normal";
        t->set_mentality("Normal", 0);
    } else {
        eff = t->m_mentality != name || t->m_mentality_dur < duration;
        t->set_mentality(name, duration, false);
    }
    if (eff) if (auto* log = advlog()) log->mentality_motion(t, name, duration, oldmentality, oldduration);
    return eff;
}
bool EffectMotion::sleep_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::confuse_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::overheat_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::brave_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::panic_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::normal_motion(std::shared_ptr<Character> t, bool r) { return mentality(t, r); }
bool EffectMotion::bind_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = calc_durationvalue(t, false);
    const bool eff = t->m_bind < d;
    const auto oldvalue = t->m_bind;
    t->set_bind(d, false);
    if (eff) if (auto* log = advlog()) log->bind_motion(t, t->m_bind, oldvalue);
    return eff;
}
bool EffectMotion::disbind_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = t->m_bind;
    t->set_bind(0);
    if (d > 0) if (auto* log = advlog()) log->disbind_motion(t, t->m_bind, d);
    return d > 0;
}
bool EffectMotion::silence_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = calc_durationvalue(t, false);
    const bool eff = t->m_silence < d;
    const auto oldvalue = t->m_silence;
    t->set_silence(d, false);
    if (eff) if (auto* log = advlog()) log->silence_motion(t, t->m_silence, oldvalue);
    return eff;
}
bool EffectMotion::dissilence_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = t->m_silence;
    t->set_silence(0);
    if (d > 0) if (auto* log = advlog()) log->dissilence_motion(t, t->m_silence, d);
    return d > 0;
}
bool EffectMotion::faceup_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = calc_durationvalue(t, false);
    const bool eff = t->m_faceup < d;
    const auto oldvalue = t->m_faceup;
    t->set_faceup(d, false);
    if (eff) if (auto* log = advlog()) log->faceup_motion(t, t->m_faceup, oldvalue);
    return eff;
}
bool EffectMotion::facedown_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = t->m_faceup;
    t->set_faceup(0);
    if (d > 0) if (auto* log = advlog()) log->facedown_motion(t, t->m_faceup, d);
    return d > 0;
}
bool EffectMotion::antimagic_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = calc_durationvalue(t, false);
    const bool eff = t->m_antimagic < d;
    const auto oldvalue = t->m_antimagic;
    t->set_antimagic(d, false);
    if (eff) if (auto* log = advlog()) log->antimagic_motion(t, t->m_antimagic, oldvalue);
    return eff;
}
bool EffectMotion::disantimagic_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto d = t->m_antimagic;
    t->set_antimagic(0);
    if (d > 0) if (auto* log = advlog()) log->disantimagic_motion(t, t->m_antimagic, d);
    return d > 0;
}
bool EffectMotion::enhanceaction_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto v = ::cw::util::numwrap(m_value, Integer{-10}, Integer{10});
    const auto d = v ? calc_durationvalue(t, true) : 0;
    const auto oldvalue = t->m_enhance_act;
    const bool eff = t->m_enhance_act != v || t->m_enhance_act_dur < d;
    if (eff) { t->set_enhance_act(v, d); if (auto* log = advlog()) log->enhanceaction_motion(t, t->m_enhance_act, oldvalue); }
    return eff;
}
bool EffectMotion::enhanceavoid_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto v = ::cw::util::numwrap(m_value, Integer{-10}, Integer{10});
    const auto d = v ? calc_durationvalue(t, true) : 0;
    const auto oldvalue = t->m_enhance_avo;
    const bool eff = t->m_enhance_avo != v || t->m_enhance_avo_dur < d;
    if (eff) { t->set_enhance_avo(v, d); if (auto* log = advlog()) log->enhanceavoid_motion(t, t->m_enhance_avo, oldvalue); }
    return eff;
}
bool EffectMotion::enhanceresist_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto v = ::cw::util::numwrap(m_value, Integer{-10}, Integer{10});
    const auto d = v ? calc_durationvalue(t, true) : 0;
    const auto oldvalue = t->m_enhance_res;
    const bool eff = t->m_enhance_res != v || t->m_enhance_res_dur < d;
    if (eff) { t->set_enhance_res(v, d); if (auto* log = advlog()) log->enhanceresist_motion(t, t->m_enhance_res, oldvalue); }
    return eff;
}
bool EffectMotion::enhancedefense_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto v = ::cw::util::numwrap(m_value, Integer{-10}, Integer{10});
    const auto d = v ? calc_durationvalue(t, true) : 0;
    const auto oldvalue = t->m_enhance_def;
    const bool eff = t->m_enhance_def != v || t->m_enhance_def_dur < d;
    if (eff) { t->set_enhance_def(v, d); if (auto* log = advlog()) log->enhancedefense_motion(t, t->m_enhance_def, oldvalue); }
    return eff;
}
bool EffectMotion::vanishtarget_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    t->set_vanish(m_cardheader_typed && ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus());
    bool runaway = false;
    if (m_cardheader_typed && ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
        const auto key = ::cw::g_cwpy->m_setting->m_msgs->get("runaway_keycode", "逃走");
        const auto keys = m_cardheader_typed->get_keycodes(false);
        runaway = std::find(keys.begin(), keys.end(), key) != keys.end();
    }
    if (auto* log = advlog()) log->vanishtarget_motion(t, runaway);
    return true;
}
bool EffectMotion::vanishcard_motion(std::shared_ptr<Character> t, bool r)
{
    if (r || t->is_inactive() || !t->m_deck || !::cw::g_cwpy || !::cw::g_cwpy->m_battle) return false;
    if (auto* log = advlog()) log->vanishcard_motion(t, t->is_inactive(), ::cw::g_cwpy->is_battlestatus());
    t->m_deck->throwaway();
    return true;
}
bool EffectMotion::vanishbeast_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    const auto eff = t->set_beast(std::nullopt, true, false);
    if (eff) if (auto* log = advlog()) log->vanishbeast_motion(t);
    return eff;
}

bool deal_card(std::shared_ptr<Character> target, bool success_res, Integer id)
{
    if (success_res || !target || target->is_inactive()) return false;
    if (id != 0 && !action_enabled(target, id)) return false;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_battle || !target->m_deck) return false;
    if (auto* log = advlog()) {
        const bool inactive = target->is_inactive();
        const bool battle = ::cw::g_cwpy->is_battlestatus();
        switch (id) {
        case 1: log->dealattackcard_motion(target, inactive, battle); break;
        case 2: log->dealpowerfulattackcard_motion(target, inactive, battle); break;
        case 3: log->dealcriticalattackcard_motion(target, inactive, battle); break;
        case 4: log->dealfeintcard_motion(target, inactive, battle); break;
        case 5: log->dealdefensecard_motion(target, inactive, battle); break;
        case 6: log->dealdistancecard_motion(target, inactive, battle); break;
        case -1: log->dealconfusecard_motion(target, inactive, battle); break;
        case 0: log->dealskillcard_motion(target, inactive, battle); break;
        default: break;
        }
    }
    target->m_deck->set_nextcard(id);
    return true;
}
bool EffectMotion::dealattackcard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 1); }
bool EffectMotion::dealpowerfulattackcard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 2); }
bool EffectMotion::dealcriticalattackcard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 3); }
bool EffectMotion::dealfeintcard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 4); }
bool EffectMotion::dealdefensecard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 5); }
bool EffectMotion::dealdistancecard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 6); }
bool EffectMotion::dealconfusecard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, -1); }
bool EffectMotion::dealskillcard_motion(std::shared_ptr<Character> t, bool r) { return deal_card(t, r, 0); }
bool EffectMotion::cancelaction_motion(std::shared_ptr<Character> t, bool r)
{
    if (r || !t || t->is_inactive()) return false;
    if (auto* log = advlog(); log && ::cw::g_cwpy)
        log->cancelaction_motion(t, ::cw::g_cwpy->is_battlestatus());
    if (has_actiondata(t)) {
        const auto action_header = std::get<1>(t->m_actiondata);
        if (!action_header.is_none() && t->m_deck)
            if (auto header = object_value<::cw::header::CardHeader>(action_header))
                t->m_deck->set_used(header);
        t->clear_action();
    }
    return true;
}
bool EffectMotion::summonbeast_motion(std::shared_ptr<Character> t, bool r)
{
    if (r) return false;
    bool effectual = false;
    for (const auto& beast : m_beasts_typed) {
        const auto resolved = resolve_carddata(beast);
        if (!resolved || !*resolved) continue;
        auto e2 = *resolved;
        const auto cwxpath = e2->get_cwxpath();
        auto e = (e2.get() == beast.get()) ? ::cw::data::copydata(beast, true) : e2;
        if (!cwxpath.empty()) e->set("cwxpath", cwxpath);
        m_duration = e->getint("Property/UseLimit", std::nullopt, std::nullopt);
        String recycle_keycode = "リサイクル";
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
            recycle_keycode = ::cw::g_cwpy->m_setting->m_msgs->get("recycle_keycode", "リサイクル");
        const auto keycodes = ::cw::util::decodetextlist(e->gettext("Property/KeyCodes", ""));
        const bool recycle = std::find(keycodes.begin(), keycodes.end(), recycle_keycode) != keycodes.end();
        const auto duration = calc_durationvalue(t, recycle);
        e->find_exists("Property/UseLimit")->set_text(std::to_string(duration));
        auto header = m_cardheader_typed;
        if (!header && ::cw::g_cwpy && ::cw::g_cwpy->m_event &&
            (::cw::g_cwpy->m_event->m_in_inusecardevent || ::cw::g_cwpy->m_event->m_in_cardeffectmotion)) {
            if (auto inuse = ::cw::g_cwpy->m_event->get_inusecard(); inuse && *inuse)
                header = *inuse;
        }
        bool is_scenariocard = !header;
        if (header) {
            const auto carddata = object_value<Element>(header->m_carddata);
            const auto materials = carddata ? carddata->gettext("Property/Materials", "") : String{};
            is_scenariocard = header->m_scenariocard && carddata && materials.empty();
            if (!materials.empty() && !e->find("Property/Materials"))
                e->find_exists("Property")->append(::cw::data::make_element("Materials", materials));
        }
        if (t->set_beast(e, false, is_scenariocard)) {
            if (auto* log = advlog()) log->summonbeast_motion(t, e2);
            effectual = true;
        }
    }
    return effectual;
}
bool EffectMotion::noeffect_motion(std::shared_ptr<Character>, bool) { return true; }

Integer get_vocation_val(std::shared_ptr<Character> ccard, std::tuple<String, String> vocation, bool enhance_act)
{
    if (!ccard) return 6;
    const auto& physical = std::get<0>(vocation);
    auto mental = std::get<1>(vocation);
    const bool unmental = mental.rfind("un", 0) == 0;
    if (const auto offset = mental.find("un"); offset != String::npos) mental.erase(offset, 2);
    Integer p = 0, m = 0, action = 0;
    if (ccard->m_data_typed) {
        // Python は XML を getint で読むため、整数読み取り後
        // の小数の精神補正の分岐には到達しない。 公開 XML の整数値が 1
        // である時、Real 側の複製値 1.23 から 0 を生成しない。
        p = ccard->m_data_typed->getint("Property/Ability/Physical", physical, 0);
        m = ccard->m_data_typed->getint("Property/Ability/Mental", mental, 0);
        if (enhance_act) action = ccard->m_data_typed->getint("Property/Enhance/Action", std::nullopt, std::nullopt);
    } else {
        // C++ の追加の型付き専用 Character には XML
        // ツリーがない。 既存の複製値を、同じ整数変換の規則で扱う。
        if (const auto it = ccard->m_physical.find(physical); it != ccard->m_physical.end()) p = real_to_integer(it->second);
        if (const auto it = ccard->m_mental.find(mental); it != ccard->m_mental.end()) m = real_to_integer(it->second);
        if (enhance_act) action = ccard->m_enhance_act;
    }
    const auto sum = Decimal(p) + (unmental ? Decimal(0) - Decimal(m) : Decimal(m)) + Decimal(action);
    return sum < Decimal(-65536) ? -65536 : sum > Decimal(65536) ? 65536 : sum.to_integer();
}

Integer get_vocation_level(std::shared_ptr<Character> ccard, std::tuple<String, String> vocation, bool enhance_act)
{
    const auto value = get_vocation_val(std::move(ccard), std::move(vocation), enhance_act); if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_vocation120) { if (value < 3) return 0; if (value < 7) return 1; if (value < 11) return 2; if (value < 15) return 3; return 4; } if (value < 3) return 0; if (value < 9) return 1; if (value < 15) return 2; if (value < 21) return 3; return 4;
}
std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> get_effectivetargets(std::shared_ptr<::cw::header::CardHeader> header, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> targets)
{
    std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> result; if (!header) return result; auto carddata = object_value<Element>(header->m_carddata); if (!carddata) return result; const auto effecttype = carddata->gettext("Property/EffectType", ""); const auto motions = carddata->find("Motions"); if (!motions) return result;
    bool owner_is_player = false;
    if (const auto owner = header->get_owner(); owner)
        owner_is_player = static_cast<bool>(object_value<::cw::character::Player>(*owner)) ||
                          static_cast<bool>(object_value<::cw::sprite::card::PlayerCard>(*owner));
    const bool ignore = header->m_type == "BeastCard" || header->m_penalty || !owner_is_player;
    std::set<const ::cw::sprite::card::CWPyCard*> seen;
    for (const auto& card : targets) { auto target = std::dynamic_pointer_cast<Character>(card); if (!target || check_noeffect(effecttype, target, ignore)) continue; for (const auto& motion : (*motions)->m_children) if (motion && target->is_effective(header, motion)) { if (seen.insert(card.get()).second) result.push_back(card); break; } }
    return result;
}
void main()
{
    // Python版もpass。効果計算はEffect/EffectMotionの明示呼出しで実行する。
}
}
