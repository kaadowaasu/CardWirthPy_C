#include "cw/debug/charaedit.hpp"
#include <iterator>
#include "cw/debug/retained_presentation.hpp"
#include "cw/character.hpp"
#include "cw/animation.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/dialog/create.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dice.hpp"
#include "cw/features.hpp"
#include "cw/frame.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/xmlcreater.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <string>
namespace cw::debug::charaedit {
namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;

// ダイアログと公開の連携口で、ゲームスレッド上の一つの確定・更新経路を共有する。
void refresh_edited_cards(const std::vector<Integer>& updates,
                      const std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>>& pcards)
{
    if (updates.empty()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
        return;
    }
    for (const auto index : updates) {
        if (index < 0 || static_cast<std::size_t>(index) >= pcards.size()) continue;
        const auto& card = pcards[static_cast<std::size_t>(index)];
        if (!card) continue;
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
        // Python版と同様、フレームも含む非表示・更新・再表示の全手順を予約する。
        ::cw::animation::animate_sprite(card, "hide");
        if (const auto image = card->get_cardimg()) image->set_levelimg(card->m_level);
        card->update_image(false, std::nullopt);
        ::cw::animation::animate_sprite(card, "deal");
    }
}

template<class T>
std::shared_ptr<T> native_object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

Integer int_value(const ::cw::lib::Value& value, Integer fallback = 0)
{
    if (const auto* p = std::get_if<Integer>(&value.storage())) return *p;
    if (const auto* p = std::get_if<Real>(&value.storage())) return static_cast<Integer>(*p);
    if (const auto* p = std::get_if<String>(&value.storage())) {
        try { return std::stoll(*p); } catch (...) { return fallback; }
    }
    return fallback;
}

Real map_real(const std::map<String, Real>& values, const String& key, Real fallback = 0)
{
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}

bool map_bool(const std::map<String, bool>& values, const String& key, bool fallback = false)
{
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}

String trim_copy(String value)
{
    return ::cw::lib::python_strip(value);
}

std::shared_ptr<::cw::image::ImageInfo> copy_image(
    const std::shared_ptr<::cw::image::ImageInfo>& image)
{
    return image ? image->copy() : nullptr;
}

std::size_t dice_choice_index(std::size_t size)
{
    if (size <= 1) return 0;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_dice) return 0;
    std::vector<::cw::lib::Value> indexes;
    indexes.reserve(size);
    for (std::size_t i = 0; i < size; ++i)
        indexes.emplace_back(static_cast<Integer>(i));
    return static_cast<std::size_t>(int_value(::cw::g_cwpy->m_dice->choice_exists(std::move(indexes))));
}

std::size_t dice_roll_index(std::size_t size)
{
    if (size <= 1) return 0;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_dice) return 0;
    return static_cast<std::size_t>(
        std::clamp<Integer>(::cw::g_cwpy->m_dice->roll(1, static_cast<Integer>(size)) - 1, 0,
                            static_cast<Integer>(size - 1)));
}

template<class T>
T dice_choice(const std::vector<T>& values)
{
    if (values.empty()) return T{};
    return values[dice_choice_index(values.size())];
}

template<class T>
T dice_roll_choice(const std::vector<T>& values)
{
    if (values.empty()) return T{};
    return values[dice_roll_index(values.size())];
}

String no_change_text()
{
    return ::cw::setting::Setting::s_msgs
        ? ::cw::setting::Setting::s_msgs->get("no_change", "[変更しない]")
        : String("[変更しない]");
}

String path_basename(String path)
{
    const auto separator = path.find_last_of("/\\");
    return separator == String::npos ? path : path.substr(separator + 1);
}

String strip_coupon_prefix(const String& coupon)
{
    return coupon.rfind("＿", 0) == 0 ? coupon.substr(String("＿").size()) : coupon;
}

std::shared_ptr<::cw::data::CWPyElement> player_element(const ::cw::character::Player& player)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&player.m_data.storage());
    if (!object || !*object) return {};
    if (auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object)) return element;
    if (auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object)) return tree->getroot();
    return {};
}

void paint_character_portrait(const std::shared_ptr<CharaRequirementPanel>& panel,
    const std::shared_ptr<::cw::lib::Surface>& surface, ::cw::lib::Rect rect, Real scale)
{
    if (!panel) return;
    panel->_select_image(scale);
    auto background = std::make_shared<::cw::lib::Surface>();
    background->size = {rect.width, rect.height};
    background->rgba.assign(static_cast<std::size_t>(rect.width * rect.height) * 4, 255);
    ::cw::lib::ui::fill_rect(background, {0,0,rect.width,rect.height}, {230,230,230,255});
    if (panel->m_img) ::cw::lib::ui::blend_surface(surface,
        panel->m_img->render_surface(background, scale), rect.x, rect.y);
}

bool same_images(const std::vector<std::shared_ptr<::cw::image::ImageInfo>>& lhs,
                 const std::vector<std::shared_ptr<::cw::image::ImageInfo>>& rhs)
{
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] == rhs[i]) continue;
        if (!lhs[i] || !rhs[i] || lhs[i]->m_path != rhs[i]->m_path ||
            lhs[i]->m_pcnumber != rhs[i]->m_pcnumber || lhs[i]->m_postype != rhs[i]->m_postype) return false;
    }
    return true;
}

String race_name(const std::shared_ptr<::cw::header::RaceHeader>& race)
{
    return race ? race->m_name : String{};
}

std::shared_ptr<::cw::header::RaceHeader> default_race()
{
    if (::cw::setting::Setting::s_unknown_race) return ::cw::setting::Setting::s_unknown_race;
    auto race = std::make_shared<::cw::header::UnknownRaceHeader>();
    race->initialize(::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr);
    return race;
}

std::optional<Integer> coupon_value(const ::cw::character::Player& player,
                                    const String& name)
{
    const auto found = player.m_coupons.find(name);
    if (found == player.m_coupons.end()) return std::nullopt;
    return int_value(found->second);
}

std::vector<std::tuple<String, Integer>> xml_coupons(const ::cw::character::Player& player)
{
    std::vector<std::tuple<String, Integer>> result;
    if (auto root = player_element(player)) {
        if (auto coupons = root->find("Property/Coupons")) {
            for (const auto& element : (*coupons)->m_children) {
                if (!element) continue;
                result.emplace_back(element->get_text(),
                                    int_value(::cw::lib::Value(element->get("value", "0"))));
            }
        }
    }
    if (!result.empty()) return result;
    for (const auto& [name, value] : player.m_coupons)
        result.emplace_back(name, int_value(value));
    return result;
}

std::tuple<String, String> parent_matcher(const String& value)
{
    const auto index = value.find("%s");
    if (index == String::npos) {
        // プレースホルダーがない不正なメッセージでも、 Pythonの s[:s.find("%s")]
        // と s[s.find("%s") + 2:] の動作に合わせる。
        if (value.empty()) return {String{}, String{}};
        return {value.substr(0, value.size() - 1), value.size() > 1 ? value.substr(1) : String{}};
    }
    return {value.substr(0, index), value.substr(index + 2)};
}

template<class T>
void apply_feature(const T& feature, CharaInfo& info)
{
    info.m_dex += feature.m_dexbonus; info.m_agl += feature.m_aglbonus;
    info.m_py_int += feature.m_intbonus; info.m_str += feature.m_strbonus;
    info.m_vit += feature.m_vitbonus; info.m_min += feature.m_minbonus;
    info.m_aggressive += feature.m_aggressive; info.m_cheerful += feature.m_cheerful;
    info.m_brave += feature.m_brave; info.m_cautious += feature.m_cautious;
    info.m_trickish += feature.m_trickish;
}

template<class T>
const T* find_feature(const std::vector<std::shared_ptr<T>>& features,
                      const String& coupon)
{
    for (const auto& feature : features)
        if (feature && coupon == "＿" + feature->m_name) return feature.get();
    return nullptr;
}

}

std::shared_ptr<CharaEditUiAdapter> make_default_ui_adapter(void* arg_native_owner)
{
    auto adapter = std::make_shared<CharaEditUiAdapter>();
    adapter->native_owner = arg_native_owner;
    auto* state = adapter.get();
    adapter->bind = [state](const String& key) {
        state->bound_keys.push_back(key);
    };
    adapter->layout = [state](const String& key) {
        state->layout_keys.push_back(key);
    };
    adapter->image_choices = [state](const String&, const std::vector<String>& choices, Integer, bool) {
        state->image_choices_state = choices;
    };
    adapter->image_preview = [state](const String&, const std::vector<std::shared_ptr<::cw::image::ImageInfo>>& images) {
        state->image_preview_state = images;
    };
    adapter->enabled = [state](const String& key, bool value) {
        state->enabled_state[key] = value;
    };
    adapter->selection = [state](const String& key, Integer value) {
        state->selection_state[key] = value;
    };
    adapter->checkbox = [state](const String& key, Integer value) {
        state->checkbox_state[key] = value;
    };
    adapter->number_input = [](const String& key, Integer initial, Integer minimum,
                               Integer maximum, Integer page) -> std::optional<Integer> {
        const String title = key == "chara_requirement.level" ? String("レベルの設定")
            : key.empty() ? String("レベルの設定") : key;
        auto result=std::make_shared<std::optional<Integer>>();
        auto child=std::make_shared<::cw::dialog::edit::NumberEditDialog>();
        child->m_on_accept=[result](Integer value){*result=value;};
        child->initialize(nullptr,title,std::clamp(initial,minimum,maximum),minimum,maximum,page);
        ::cw::dialog::edit::run_edit_ui_modal(0);return *result;
    };
    adapter->choice_input = [](const String& key, const std::vector<String>& choices,
                               Integer) -> std::optional<Integer> {
        if (choices.empty()) return std::nullopt;
        const String title = key == "character_edit.standard_type" ? String("能力型")
            : key.empty() ? String("能力型") : key;
        std::vector<::cw::dialog::message::MessageChoice> items;
        items.reserve(choices.size());
        for (std::size_t i = 0; i < choices.size(); ++i)
            items.push_back({choices[i], static_cast<Integer>(i), -1, {}});
        return ::cw::dialog::message::show_common_modal_choices(
            title, "選択してください。", items);
    };
    adapter->modal = [state](const String&, bool result) {
        state->modal_result = result;
    };
    adapter->dropped_files = [state](const String&, const std::vector<String>& files) {
        state->dropped_files_state = files;
    };
    // UIの実装は一つに統一し、
    // 別経路の実装を設けない。
    return adapter;
}

namespace {
std::shared_ptr<CharaEditUiAdapter> ui_adapter()
{
    static const auto adapter = make_default_ui_adapter();
    return adapter;
}
}

// ネイティブの任意Playerがなければ新しい既定の編集状態を作
// り、存在すれば型付き状態へ読む。 種族の代替は未解決の種族情
// 報だけに限り、不正な親データをすべて受け付けるものではない。
void CharaInfo::initialize(std::optional<std::shared_ptr<::cw::character::Player>> arg_pcard, bool arg_debug_coupon)
{
    m_name.clear(); m_input_name.clear(); m_race.reset(); m_imgpaths.clear(); m_imgpaths_base.clear();
    m_can_loaded_scaledimage = true; m_can_loaded_scaledimage_base = true;
    m_level = 1; m_levelmax = 10; m_sex.clear(); m_age.clear(); m_talent.clear();
    m_makings.clear(); m_type.reset(); m_physical.clear(); m_mental.clear();
    m_recalc_maxlife = m_recalc_parameter = m_recalc_coupons = true;
    m_debug_coupon = arg_debug_coupon;
    m_dex = m_agl = m_py_int = m_str = m_vit = m_min = 0;
    m_aggressive = m_cheerful = m_brave = m_cautious = m_trickish = 0;
    m_maxdex = m_maxagl = m_maxint = m_maxstr = m_maxvit = m_maxmin = 6;

    if (arg_pcard && *arg_pcard) {
        auto& player = **arg_pcard;
        m_name = player.m_name; m_input_name = m_name; m_level = player.m_level;
        for (const auto& image : player.get_imagepaths()) {
            if (!image) continue;
            auto converted = std::make_shared<::cw::image::ImageInfo>();
            converted->initialize(::cw::util::join_yadodir(image->m_path), image->m_pcnumber,
                                  image, image->m_postype, String("LargeCard"));
            m_imgpaths.push_back(converted);
        }
        m_imgpaths_base = m_imgpaths;
        m_physical = player.m_physical; m_mental = player.m_mental;
        m_debug_coupon = player.has_coupon("＠デバグ") || player.m_coupons.count("＠デバグ") != 0;

        m_race = player.get_race();
        m_sex = player.get_sex(); m_age = player.get_age(); m_talent = player.get_talent();
        m_makings = player.get_makings();
        // 既存PCは XML 属性の既定値が False（新規作成時だけ True）。
        m_can_loaded_scaledimage = false;
        if (auto root = player_element(player))
            m_can_loaded_scaledimage = root->getbool(".", ::cw::lib::Value("scaledimage"), false);
        m_can_loaded_scaledimage_base = m_can_loaded_scaledimage;
        m_type = get_paramtype(*arg_pcard);
        m_dex = map_real(m_physical, "dex"); m_agl = map_real(m_physical, "agl"); m_py_int = map_real(m_physical, "int");
        m_str = map_real(m_physical, "str"); m_vit = map_real(m_physical, "vit"); m_min = map_real(m_physical, "min");
        m_aggressive = map_real(m_mental, "aggressive"); m_cheerful = map_real(m_mental, "cheerful");
        m_brave = map_real(m_mental, "brave"); m_cautious = map_real(m_mental, "cautious"); m_trickish = map_real(m_mental, "trickish");

        _calc_params();
        const Integer levelmax = player.get_levelmax();
        const Integer calculated_life = ::cw::character::calc_maxlife(
            static_cast<Integer>(map_real(player.m_physical, "vit")),
            static_cast<Integer>(map_real(player.m_physical, "min")), player.m_level);
        m_recalc_maxlife = player.m_maxlife == calculated_life;
        const auto eq = [](Real lhs, Real rhs) { return lhs == rhs; };
        m_recalc_parameter = eq(map_real(player.m_physical, "agl"), m_agl) && eq(map_real(player.m_physical, "dex"), m_dex) &&
            eq(map_real(player.m_physical, "int"), m_py_int) && eq(map_real(player.m_physical, "min"), m_min) &&
            eq(map_real(player.m_physical, "str"), m_str) && eq(map_real(player.m_physical, "vit"), m_vit) &&
            eq(map_real(player.m_mental, "aggressive"), m_aggressive) && eq(map_real(player.m_mental, "brave"), m_brave) &&
            eq(map_real(player.m_mental, "cautious"), m_cautious) && eq(map_real(player.m_mental, "cheerful"), m_cheerful) &&
            eq(map_real(player.m_mental, "trickish"), m_trickish) &&
            map_bool(player.m_feature, "automaton") == m_race->m_automaton && map_bool(player.m_feature, "constructure") == m_race->m_constructure &&
            map_bool(player.m_feature, "undead") == m_race->m_undead && map_bool(player.m_feature, "unholy") == m_race->m_unholy &&
            map_bool(player.m_noeffect, "magic") == m_race->m_noeffect_magic && map_bool(player.m_noeffect, "weapon") == m_race->m_noeffect_weapon &&
            map_bool(player.m_resist, "fire") == m_race->m_resist_fire && map_bool(player.m_resist, "ice") == m_race->m_resist_ice &&
            map_bool(player.m_weakness, "fire") == m_race->m_weakness_fire && map_bool(player.m_weakness, "ice") == m_race->m_weakness_ice &&
            (player.m_enhance.count("avoid") ? player.m_enhance.at("avoid") : 0) == m_race->m_avoid &&
            (player.m_enhance.count("resist") ? player.m_enhance.at("resist") : 0) == m_race->m_resist &&
            (player.m_enhance.count("defense") ? player.m_enhance.at("defense") : 0) == m_race->m_defense && levelmax == m_levelmax;

        std::map<String, Integer> initial_coupons;
        for (const auto& [name, value] : m_race->m_coupon_entries) initial_coupons[name] = value;
        for (const auto& period : ::cw::setting::Setting::s_periods)
            if (period && m_age == "＿" + period->m_name) {
                for (const auto& [name, value] : period->m_coupons) initial_coupons[name] = value;
                break;
            }
        m_recalc_coupons = true;
        for (const auto& [name, value] : initial_coupons)
            if (!coupon_value(player, name) || *coupon_value(player, name) != value) { m_recalc_coupons = false; break; }
    } else {
        m_race = default_race();
        if (!::cw::setting::Setting::s_sexcoupons.empty()) m_sex = ::cw::setting::Setting::s_sexcoupons.front();
        if (!::cw::setting::Setting::s_periodcoupons.empty()) m_age = ::cw::setting::Setting::s_periodcoupons.front();
        if (!::cw::setting::Setting::s_naturecoupons.empty()) m_talent = ::cw::setting::Setting::s_naturecoupons.front();
        _calc_params();
    }
    m_recalc_maxlife_init = m_recalc_maxlife; m_recalc_parameter_init = m_recalc_parameter;
    m_recalc_coupons_init = m_recalc_coupons; m_debug_coupon_init = m_debug_coupon;
}

void CharaInfo::set_randomfeatures()
{
    m_race = dice_choice(::cw::setting::Setting::s_races);
    if (!m_race) m_race = default_race();
    m_sex = dice_choice(::cw::setting::Setting::s_sexcoupons);
    m_age = dice_choice(::cw::setting::Setting::s_periodcoupons);
    m_imgpaths.clear();
    std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> faces;
    {
        for (const auto& [unused, values] : ::cw::util::get_facepaths(m_sex, m_age, true)) {
            (void)unused;
            faces.insert(faces.end(), values.begin(), values.end());
        }
    }if (!faces.empty()) {
        for (const auto& image : dice_choice(faces)) m_imgpaths.push_back(copy_image(image));
    }
    std::vector<std::shared_ptr<::cw::features::Nature>> natures;
    for (const auto& nature : ::cw::setting::Setting::s_natures) if (nature && !nature->m_special) natures.push_back(nature);
    if (!natures.empty()) m_talent = "＿" + dice_choice(natures)->m_name;
    m_makings = ::cw::dialog::create::get_randommakings();
    m_type.reset(); m_can_loaded_scaledimage = true; _calc_params();
}

std::optional<std::shared_ptr<::cw::features::SampleType>> CharaInfo::get_paramtype(std::shared_ptr<::cw::character::Player> arg_info)
{
    if (!arg_info || !m_race) return {};
    const auto physical = [&](const char* key, Real value) { return map_real(arg_info->m_physical, key) == value; };
    const auto mental = [&](const char* key, Real value) { return map_real(arg_info->m_mental, key) == value; };
    for (const auto& type : ::cw::setting::Setting::s_sampletypes) {
        if (!type) continue;
        if (physical("agl", m_race->m_agl + type->m_aglbonus) && physical("dex", m_race->m_dex + type->m_dexbonus) &&
            physical("int", m_race->m_py_int + type->m_intbonus) && physical("min", m_race->m_min + type->m_minbonus) &&
            physical("str", m_race->m_str + type->m_strbonus) && physical("vit", m_race->m_vit + type->m_vitbonus) &&
            mental("aggressive", m_race->m_aggressive + type->m_aggressive) && mental("brave", m_race->m_brave + type->m_brave) &&
            mental("cautious", m_race->m_cautious + type->m_cautious) && mental("cheerful", m_race->m_cheerful + type->m_cheerful) &&
            mental("trickish", m_race->m_trickish + type->m_trickish)) return type;
    }
    return {};
}

void CharaInfo::_calc_params()
{
    if (!m_race) m_race = default_race();
    m_maxdex = m_race->m_dex + 6; m_maxagl = m_race->m_agl + 6; m_maxint = m_race->m_py_int + 6;
    m_maxstr = m_race->m_str + 6; m_maxvit = m_race->m_vit + 6; m_maxmin = m_race->m_min + 6;
    m_agl = static_cast<Real>(m_race->m_agl); m_dex = static_cast<Real>(m_race->m_dex); m_py_int = static_cast<Real>(m_race->m_py_int);
    m_min = static_cast<Real>(m_race->m_min); m_str = static_cast<Real>(m_race->m_str); m_vit = static_cast<Real>(m_race->m_vit);
    m_aggressive = m_race->m_aggressive; m_brave = m_race->m_brave;
    m_cautious = m_race->m_cautious; m_cheerful = m_race->m_cheerful; m_trickish = m_race->m_trickish;
    if (m_type && *m_type) {
        const auto& type = **m_type;
        m_agl += type.m_aglbonus; m_dex += type.m_dexbonus; m_py_int += type.m_intbonus;
        m_min += type.m_minbonus; m_str += type.m_strbonus; m_vit += type.m_vitbonus;
        m_aggressive += type.m_aggressive; m_brave += type.m_brave; m_cautious += type.m_cautious;
        m_cheerful += type.m_cheerful; m_trickish += type.m_trickish;
    } else {
        if (const auto* f = find_feature(::cw::setting::Setting::s_sexes, m_sex)) apply_feature(*f, *this);
        if (const auto* f = find_feature(::cw::setting::Setting::s_periods, m_age)) apply_feature(*f, *this);
        if (const auto* f = find_feature(::cw::setting::Setting::s_natures, m_talent)) apply_feature(*f, *this);
        for (const auto& f : ::cw::setting::Setting::s_makings)
            if (f && m_makings.count("＿" + f->m_name)) apply_feature(*f, *this);
    }
    // キャラクター作成と同じPythonのnumwrapを使い、上限が1未満の場
    // 合も扱う。 std::clampは上下限の順序を要求するため同等ではない。
    ::cw::features::wrap_ability(*this);
    m_physical = {{"agl",m_agl},{"dex",m_dex},{"int",m_py_int},{"min",m_min},{"str",m_str},{"vit",m_vit}};
    m_mental = {{"aggressive",m_aggressive},{"brave",m_brave},{"cautious",m_cautious},{"cheerful",m_cheerful},{"trickish",m_trickish}};
    m_levelmax = 10;
    if (const auto* nature = find_feature(::cw::setting::Setting::s_natures, m_talent)) m_levelmax = nature->m_levelmax;
    for (const auto& race : ::cw::setting::Setting::s_races) {
        if (race != m_race) continue;
        for (const auto& [name, value] : race->m_coupon_entries) {
            if (name != "＠レベル上限") continue;
            m_levelmax = (std::max)(m_levelmax, value);
            break; // Python版と同様、レベル上限の称号は最初の一つだけを使い、重複をすべて適用しない。
        }
        break;
    }
}

// 同じ名前の種族でも、情報が異なる場合がある。 基本パラメーターの再構
// 築を決める際は、Python版のオブジェクトの同一性比較を維持する。
bool CharaInfo::put_params(std::shared_ptr<::cw::character::Player> arg_pcard)
{
    if (!arg_pcard) return false;
    auto& player = *arg_pcard;
    _calc_params();
    const auto old_physical = player.m_physical;
    const auto coupons_before = xml_coupons(player);
    const auto old_race = player.get_race();
    const auto old_sex = player.get_sex();
    const auto old_age = player.get_age();
    const auto old_talent = player.get_talent();
    const auto old_makings = player.get_makings();
    const bool type_changed = m_type != get_paramtype(arg_pcard);
    const bool updatebase = (m_recalc_parameter && !m_recalc_parameter_init) ||
        (m_recalc_coupons && !m_recalc_coupons_init) || (m_debug_coupon != m_debug_coupon_init) ||
        m_race != old_race || m_sex != old_sex || m_age != old_age ||
        m_talent != old_talent || m_makings != old_makings || type_changed;
    const bool image_changed = !same_images(m_imgpaths, m_imgpaths_base) ||
        m_can_loaded_scaledimage != m_can_loaded_scaledimage_base;
    const bool updateetc = m_name != player.m_name || image_changed || m_level != player.m_level;

    std::vector<std::tuple<String, Integer>> final_coupons = coupons_before;
    if (updatebase) {
        std::set<String> racecoupons;
        for (const auto& period : ::cw::setting::Setting::s_periods) if (period)
            for (const auto& [name, unused] : period->m_coupons) {
                (void)unused; if (name == "＠ＥＰ") break; racecoupons.insert(name);
            }
        for (const auto& race : ::cw::setting::Setting::s_races) if (race)
            for (const auto& [name, unused] : race->m_coupon_entries) {
                (void)unused; if (name == "＠ＥＰ") break; racecoupons.insert(name);
            }
        std::set<String> syscoupons(::cw::setting::Setting::s_sexcoupons.begin(), ::cw::setting::Setting::s_sexcoupons.end());
        syscoupons.insert(::cw::setting::Setting::s_periodcoupons.begin(), ::cw::setting::Setting::s_periodcoupons.end());
        syscoupons.insert(::cw::setting::Setting::s_naturecoupons.begin(), ::cw::setting::Setting::s_naturecoupons.end());
        syscoupons.insert(::cw::setting::Setting::s_makingcoupons.begin(), ::cw::setting::Setting::s_makingcoupons.end());
        std::vector<std::tuple<String, Integer>> etc_coupons;
        std::vector<std::tuple<String, Integer>> parent_coupons;
        bool passed_parents = false;
        bool set_levelmax = false;
        const auto matcher = [](const String& value, const std::optional<String>& pattern) {
            if (!pattern) return false;
            const auto [left, right] = parent_matcher(*pattern);
            return value.rfind(left, 0) == 0 && (right.empty() ||
                (value.size() >= right.size() && value.compare(value.size() - right.size(), right.size(), right) == 0));
        };
        const auto message_pattern = [](const char* key) -> std::optional<String> {
            if (!::cw::setting::Setting::s_msgs) return std::nullopt;
            const auto found = ::cw::setting::Setting::s_msgs->m__d.find(key);
            return found == ::cw::setting::Setting::s_msgs->m__d.end()
                ? std::nullopt : std::optional<String>(found->second);
        };
        const auto father_pattern = message_pattern("father_coupon");
        const auto mother_pattern = message_pattern("mother_coupon");
        for (const auto& [name, original_value] : coupons_before) {
            if (name == "＠デバグ") continue;
            if (syscoupons.count(name) || name.rfind("＠Ｒ", 0) == 0) {
                if (!(m_recalc_parameter && name == "＠レベル上限")) continue;
            }
            Integer value = original_value;
            if (m_recalc_parameter && name == "＠レベル上限") { value = m_levelmax; set_levelmax = true; }
            // Python版はメッセージの "%s" パターンに一致する間だけを親称号と
            // みなす。文字列中に「父」「母」があるだけでは分類しない。
            if (!passed_parents && (matcher(name, father_pattern) || matcher(name, mother_pattern))) {
                parent_coupons.emplace_back(name, value); continue;
            }
            if (name.rfind("＠", 0) != 0) passed_parents = true;
            etc_coupons.emplace_back(name, value);
        }
        if (!set_levelmax) {
            etc_coupons.emplace_back("＠レベル上限", m_recalc_parameter ? m_levelmax : player.get_levelmax());
        }

        const auto makings = get_makingslist();
        player.set_race(m_race); player.set_age(m_age); player.set_sex(m_sex);
        player.set_talent(m_talent); player.set_makings(makings);
        if (m_recalc_parameter) {
            for (const auto& [key, value] : m_physical) player.set_physical(key, value);
            for (const auto& [key, value] : m_mental) player.set_mental(key, value);
            player.set_feature("automaton", m_race->m_automaton); player.set_feature("constructure", m_race->m_constructure);
            player.set_feature("undead", m_race->m_undead); player.set_feature("unholy", m_race->m_unholy);
            player.set_noeffect("magic", m_race->m_noeffect_magic); player.set_noeffect("weapon", m_race->m_noeffect_weapon);
            player.set_resist("fire", m_race->m_resist_fire); player.set_resist("ice", m_race->m_resist_ice);
            player.set_weakness("fire", m_race->m_weakness_fire); player.set_weakness("ice", m_race->m_weakness_ice);
            player.set_enhance("avoid", m_race->m_avoid); player.set_enhance("resist", m_race->m_resist); player.set_enhance("defense", m_race->m_defense);
        }
        final_coupons = parent_coupons;
        final_coupons.emplace_back(m_sex, 0); final_coupons.emplace_back(m_age, 0);
        if (std::dynamic_pointer_cast<::cw::header::UnknownRaceHeader>(m_race) == nullptr)
            final_coupons.emplace_back("＠Ｒ" + race_name(m_race), 0);
        if (m_recalc_coupons) for (const auto& coupon : m_race->m_coupon_entries) final_coupons.push_back(coupon);
        final_coupons.emplace_back(m_talent, 0);
        for (const auto& making : makings) final_coupons.emplace_back(making, 0);
        if (m_debug_coupon) final_coupons.emplace_back("＠デバグ", 0);
        if (m_recalc_coupons) {
            for (const auto& period : ::cw::setting::Setting::s_periods)
                if (period && m_age == "＿" + period->m_name) { final_coupons.insert(final_coupons.end(), period->m_coupons.begin(), period->m_coupons.end()); break; }
            for (const auto& coupon : etc_coupons)
                if (!racecoupons.count(std::get<0>(coupon)) || std::get<0>(coupon) == "＠レベル上限") final_coupons.push_back(coupon);
        } else final_coupons.insert(final_coupons.end(), etc_coupons.begin(), etc_coupons.end());
        player.replace_allcoupons(final_coupons, std::nullopt);
        m_recalc_parameter_init = m_recalc_parameter;
        m_recalc_coupons_init = m_recalc_coupons;
    }

    if (m_name != player.m_name) { player.set_name(m_name); player.m_name = m_name; }
    if (image_changed) player.set_images(m_imgpaths);
    if (updatebase || m_level != player.m_level) {
        player.set_level(m_level, true, true, std::nullopt, true); player.m_level = m_level;
    }
    bool updatelife = false;
    if (m_recalc_maxlife) {
        const auto& physical_for_life = m_recalc_parameter ? player.m_physical : old_physical;
        const Integer life = ::cw::character::calc_maxlife(static_cast<Integer>(map_real(physical_for_life, "vit")),
                                                            static_cast<Integer>(map_real(physical_for_life, "min")), player.m_level);
        updatelife = player.m_maxlife != life;
        if (updatelife) player.set_maxlife(life);
    } else if (m_recalc_parameter) player.set_maxlife(player.m_maxlife);

    if (player_element(player)) {
        // 説明文の自動差し替えは Python 版と同じく初期情報を再計算したときだけ
        // 行う。名前/画像/レベルだけの変更で利用者の自由記述を触らない。
        if (!updatebase) {
            return updatebase || updateetc || updatelife;
        }
        const String desc_before = player.get_description();
        String desc_bef = desc_before;
        while (!desc_bef.empty() && desc_bef.back() == '\n') desc_bef.pop_back();
        const String old_default = ::cw::dialog::create::create_description(old_talent, old_makings, "");
        const auto new_makings_list = get_makingslist();
        const std::set<String> new_makings(new_makings_list.begin(), new_makings_list.end());
        const String new_default = ::cw::dialog::create::create_description(m_talent, new_makings, "");
        // 初期値はdesc_beforeとし、先頭一致の置換では末尾改行を除いたdesc_befを使う。
        String description = desc_before;
        std::size_t insertpos = String::npos;
        if (desc_bef.rfind(old_default, 0) == 0) {
            description = new_default + desc_bef.substr(old_default.size());
            insertpos = new_default.size();
        } else {
            const auto first_line_end = old_default.find('\n');
            const String old_first = first_line_end == String::npos ? old_default : old_default.substr(0, first_line_end);
            const String new_first = new_default.substr(0, new_default.find('\n'));
            if (!old_first.empty() && desc_bef.rfind(old_first, 0) == 0) description = new_first + desc_bef.substr(old_first.size());
        }
        if (m_recalc_parameter) {
            const String new_type_desc = m_type && *m_type ? trim_copy(::cw::util::txtwrap((*m_type)->m_description, 4, 0, {}, false, nullptr)) : String{};
            bool replaced_type = false;
            for (const auto& sample : ::cw::setting::Setting::s_sampletypes) if (sample) {
                const String old_type_desc = trim_copy(::cw::util::txtwrap(sample->m_description, 4, 0, {}, false, nullptr));
                const auto pos = description.find(old_type_desc);
                if (pos != String::npos) {
                    if (new_type_desc.empty()) {
                        const String before = description.substr(0, pos);
                        const String after = description.substr(pos + old_type_desc.size());
                        const String before_trimmed = [&before] {
                            String value = before;
                            while (!value.empty() && value.back() == '\n') value.pop_back();
                            return value;
                        }();
                        const String after_trimmed = [&after] {
                            std::size_t first = 0;
                            while (first < after.size() && after[first] == '\n') ++first;
                            return after.substr(first);
                        }();
                        if (!before_trimmed.empty() && !after_trimmed.empty())
                            description = before_trimmed + "\n\n" + after_trimmed;
                        else description = before_trimmed + after_trimmed;
                    } else description.replace(pos, old_type_desc.size(), new_type_desc);
                    replaced_type = true; break;
                }
            }
            if (!replaced_type && !new_type_desc.empty()) {
                if (insertpos == String::npos) {
                    while (!description.empty() && description.back() == '\n') description.pop_back();
                    description += "\n\n";
                    description += new_type_desc;
                } else {
                    String before = description.substr(0, insertpos);
                    String after = description.substr(insertpos);
                    while (!before.empty() && before.back() == '\n') before.pop_back();
                    std::size_t first = 0;
                    while (first < after.size() && after[first] == '\n') ++first;
                    after = after.substr(first);
                    description = before + "\n\n" + new_type_desc + "\n\n" + after;
                }
            }
        }
        if (description != desc_before) {
            player.set_description(description);
        }
    }
    return updatebase || updateetc || updatelife;
}

::cw::lib::String CharaInfo::create_adventurer(bool arg_setlevel)
{
    auto data = std::make_shared<::cw::dialog::create::AdventurerData>(); data->initialize();
    const auto making_list = get_makingslist();
    const auto makings = std::set<String>(making_list.begin(), making_list.end());
    data->set_name(m_name); data->set_parents(std::nullopt, std::nullopt); data->set_gene(m_talent);
    data->set_sex(m_sex); data->set_age(m_age); data->set_race(m_race);
    // 通常の作成処理と同様、簡易作成でも兄弟の表情画像を
    // 保持する。 外部の選択画面で選んだ画像も含める。
    auto images = ::cw::dialog::create::expand_basename_variants(m_imgpaths, true, true, true);
    if (!m_imgpaths.empty() && m_imgpaths.front()) {
        // 必須項目のパネルは、選択集合でこの値を共通に
        // する。 展開で見つかった兄弟にも複製する。
        const auto position = m_imgpaths.front()->m_postype;
        for (auto& image : images) if (image) image->m_postype = position;
    }
    data->set_images(std::move(images));
    data->set_talent(m_talent); data->set_attributes(makings); data->set_aging(m_age);
    if (arg_setlevel) data->set_level(m_level); else data->calc_level();
    if (m_type && *m_type) {
        const auto& type = **m_type; data->m_agl = static_cast<Real>(static_cast<Integer>(m_race->m_agl + type.m_aglbonus)); data->m_dex = static_cast<Real>(static_cast<Integer>(m_race->m_dex + type.m_dexbonus));
        data->m_py_int = static_cast<Real>(static_cast<Integer>(m_race->m_py_int + type.m_intbonus)); data->m_min = static_cast<Real>(static_cast<Integer>(m_race->m_min + type.m_minbonus));
        data->m_str = static_cast<Real>(static_cast<Integer>(m_race->m_str + type.m_strbonus)); data->m_vit = static_cast<Real>(static_cast<Integer>(m_race->m_vit + type.m_vitbonus));
        data->m_aggressive = m_race->m_aggressive + type.m_aggressive; data->m_brave = m_race->m_brave + type.m_brave; data->m_cautious = m_race->m_cautious + type.m_cautious; data->m_cheerful = m_race->m_cheerful + type.m_cheerful; data->m_trickish = m_race->m_trickish + type.m_trickish;
    }
    data->set_desc(m_talent, makings, m_type && *m_type ? (*m_type)->m_description : String{}); data->set_specialcoupon(); data->set_life();
    ::cw::features::wrap_ability(data); data->m_avoid = std::clamp(data->m_avoid, Integer{-10}, Integer{10}); data->m_resist = std::clamp(data->m_resist, Integer{-10}, Integer{10}); data->m_defense = std::clamp(data->m_defense, Integer{-10}, Integer{10});
    if (m_debug_coupon) data->set_coupon("＠デバグ", 0);
    // XML の全フィールド（係数、状態、空カード集合、画像コピーを含む）は
    // Python の xmlcreater.create_adventurer と共通実装へ委譲する。
    return ::cw::xmlcreater::create_adventurer(data);
}

std::vector<::cw::lib::String> CharaInfo::get_makingslist()
{
    std::vector<String> result;
    for (const auto& coupon : ::cw::setting::Setting::s_makingcoupons)
        if (m_makings.count(coupon)) result.push_back(coupon);
    return result;
}

void CharaRequirementPanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> arg_infos, bool arg_create)
{
    (void)arg_parent;
    m_infos_typed = std::move(arg_infos);
    m_create_typed = arg_create;
    m_cindex_typed = 0;
    m_proc_typed = false;
    m_dropfiles_typed.clear();
    m_imgpathlist_typed.clear();
    m_imgchoice_labels_typed.clear();
    m_imgselection_typed = 0;
    m_imgcentering_typed = false;
    m_ok_enabled_typed = !m_create_typed;
    m_show_specialtalent_typed = false;
    for (const auto& nature : ::cw::setting::Setting::s_natures) {
        if (!nature || !nature->m_special) continue;
        const String coupon = "＿" + nature->m_name;
        for (const auto& info : m_infos_typed) if (info && info->m_talent == coupon) {
            m_show_specialtalent_typed = true;
            break;
        }
        if (m_show_specialtalent_typed) break;
    }
    // 名前・画像・レベル・種族・性別・年代・素質は共通の表示モデルで扱う。
    // ここでは必要情報パネルの状態を初期化し、
    // 共通アダプターへ通知してOKボタンの有効状態を更新する。
    _bind();
    _do_layout();
    _update_okbtn();
}

void CharaRequirementPanel::_bind()
{
    if (ui_adapter()->bind) ui_adapter()->bind("chara_requirement");
    // 共通Presentationの操作は、これらのOn*モデル処理へ届く。
    // この接続通知では、別のプラットフォーム用コントロールツリーを作らない。
}

void CharaRequirementPanel::_do_layout()
{
    if (ui_adapter()->layout) ui_adapter()->layout("chara_requirement");
    // 配置の通知は共通モデルに属する。 画像選択・プレビューはアダプターの状
    // 態を更新し、ダイアログのPresentationが操作部品を配置する。
}

void CharaRequirementPanel::OnName(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_proc_typed) return;
    const String value = arg_event ? arg_event->title : String{};
    for (const auto& info : _get_infos()) if (info) { info->m_name = value; info->m_input_name = value; }
    _update_okbtn();
}

void CharaRequirementPanel::OnAutoName(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_proc_typed) return;
    m_proc_typed = true;
    String common;
    bool first = true;
    for (const auto& info : _get_infos()) {
        if (!info) continue;
        String generated;
        for (const auto& sex : ::cw::setting::Setting::s_sexes)
            if (sex && info->m_sex == "＿" + sex->m_name) { generated = ::cw::dialog::create::get_randomname(sex->m_subname); break; }
        if (!generated.empty()) { info->m_name = generated; info->m_input_name.clear(); }
        if (first) { common = info->m_name; first = false; }
        else if (common != info->m_name) common.clear();
    }
    m_proc_typed = false;
    _update_okbtn();
}

// Python版の不具合への意図的な修正。各名前でEnableを上
// 書きすると、最後のメンバーだけでOKの可否が決まる。 全編集名
// に空白以外があることを要求し、前の無名メンバーを保存させない。
void CharaRequirementPanel::_update_okbtn()
{
    m_ok_enabled_typed = true;
    for (const auto& info : _get_infos()) {
        if (!info || !::cw::lib::python_strip_has_text(info->m_name)) {
            m_ok_enabled_typed = false;
            break;
        }
    }
    if (ui_adapter()->enabled) ui_adapter()->enabled("ok", m_ok_enabled_typed);
    // adapter未登録時は親ダイアログの実コントロールへ通知できないため、
    // m_ok_enabled_typedを公開状態として保持する。
}

void CharaRequirementPanel::OnLevelBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    Integer level = arg_event ? arg_event->id : 0;
    if (arg_event && level <= 0 && !arg_event->title.empty()) {
        { level = std::stoi(arg_event->title); }}
    if (level <= 0) {
        if (ui_adapter()->number_input) {
            const auto infos = _get_infos();
            Integer initial = 1;
            bool first = true;
            for (const auto& info : infos) if (info) {
                if (first) { initial = info->m_level; first = false; }
                else if (initial != info->m_level) { initial = 1; break; }
            }
            const auto selected = ui_adapter()->number_input("chara_requirement.level", initial, 1, 15, 2);
            if (selected) level = std::clamp<Integer>(*selected, 1, 15);
        }
    }
    if (level <= 0) {
        // 入力キャンセルまたは未登録adapterではモデルを変更しない。
        return;
    }
    for (const auto& info : _get_infos()) if (info) info->m_level = std::clamp<Integer>(level, 1, 15);
    if (ui_adapter()->selection) ui_adapter()->selection("chara_requirement.level", std::clamp<Integer>(level, 1, 15));
}

void CharaRequirementPanel::OnSelectImage(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    // 通常の画像選択ではidを選択番号、titleを外部画像パスとして使う。
    // titleがCenterまたはDefaultなら中央寄せの切り替えとして扱い、
    // idは使わず現在の画像選択を維持する。
    const bool centering_event = arg_event->title == "Center" || arg_event->title == "Default";
    if (centering_event) {
        m_imgcentering_typed = arg_event->title == "Center";
    }
    const Integer selection = centering_event ? m_imgselection_typed : arg_event->id;
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> selected;
    if (!arg_event->title.empty() && arg_event->title != "Center" && arg_event->title != "Default") {
        auto image = std::make_shared<::cw::image::ImageInfo>();
        image->initialize(::cw::util::find_noscalepath(arg_event->title), 0, std::nullopt,
                          m_imgcentering_typed ? "Center" : "Default", std::nullopt);
        selected.push_back(image);
    } else if (selection > 0 && static_cast<std::size_t>(selection - 1) < m_imgpathlist_typed.size()) {
        auto image = std::make_shared<::cw::image::ImageInfo>();
        image->initialize(m_imgpathlist_typed[static_cast<std::size_t>(selection - 1)], 0,
                          std::nullopt, m_imgcentering_typed ? "Center" : "Default", std::nullopt);
        selected.push_back(image);
    }
    if (selection == 0 && (arg_event->title.empty() || arg_event->title == "Center" || arg_event->title == "Default")) {
        m_imgselection_typed = 0;
        for (const auto& info : _get_infos()) if (info) {
            info->m_imgpaths.clear();
            for (const auto& base : info->m_imgpaths_base) {
                auto copy = copy_image(base);
                if (!copy) continue;
                copy->m_postype = m_imgcentering_typed ? "Center" :
                    (copy->m_postype == "Center" ? "Default" : copy->m_postype);
                info->m_imgpaths.push_back(copy);
            }
            info->m_can_loaded_scaledimage = info->m_can_loaded_scaledimage_base;
        }
    } else if (!selected.empty()) {
        // ネイティブの表情画像対応では、Python版の単一画像と異なり
        // 、 キャラクター作成と同様に同じ基本名の兄弟画像をまとめる。
        selected = ::cw::dialog::create::expand_basename_variants(std::move(selected), true, true, true);
        m_imgselection_typed = selection;
        for (const auto& info : _get_infos()) if (info) {
            info->m_imgpaths.clear();
            for (const auto& image : selected) if (auto copy = copy_image(image)) {
                copy->m_postype = m_imgcentering_typed ? "Center" : "Default";
                info->m_imgpaths.push_back(std::move(copy));
            }
            info->m_can_loaded_scaledimage = true;
        }
    }
    _select_image();
}

void CharaRequirementPanel::OnImgBoxDropFiles(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event && !arg_event->title.empty()) {
        String paths = arg_event->title;
        std::vector<String> files;
        std::size_t begin = 0;
        while (begin <= paths.size()) {
            const auto end = paths.find_first_of(";\n\r", begin);
            const auto token = paths.substr(begin, end == String::npos ? String::npos : end - begin);
            if (!token.empty()) files.push_back(token);
            if (end == String::npos) break;
            begin = end + 1;
        }
        if (ui_adapter()->dropped_files) ui_adapter()->dropped_files("chara_requirement.image", files);
        _put_image(std::move(files));
    }
}

// 対応拡張子の選別は、存在・復号の検証ではない。 ネイティブ
// は対応パスが残らなくてもドロップ一覧を消すが、Python
// 版は選別結果が空でない場合だけ置き換える。 キャラクター画
// 像・選択肢の更新は、ここでも対応パスが残る場合だけ行う。
void CharaRequirementPanel::_put_image(std::vector<::cw::lib::String> arg_files)
{
    m_dropfiles_typed.clear();
    for (auto path : arg_files) {
        const auto dot = path.find_last_of('.');
        String ext = dot == String::npos ? String{} : path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        bool is_image = false;
        std::apply([&](const auto&... supported) { ((is_image = is_image || ext == supported), ...); }, ::cw::g_EXTS_IMG);
        if (is_image)
            m_dropfiles_typed.push_back(::cw::util::find_noscalepath(path));
    }
    if (!m_dropfiles_typed.empty()) {
        std::vector<std::shared_ptr<::cw::image::ImageInfo>> images;
        auto image = std::make_shared<::cw::image::ImageInfo>();
        image->initialize(m_dropfiles_typed.front(), 0, std::nullopt,
                          m_imgcentering_typed ? "Center" : "Default", std::nullopt);
        images = ::cw::dialog::create::expand_basename_variants({image}, true, true, true);
        for (const auto& variant : images) if (variant)
            variant->m_postype = m_imgcentering_typed ? "Center" : "Default";
        for (const auto& info : _get_infos()) if (info) { info->m_imgpaths = images; info->m_can_loaded_scaledimage = true; }
        _update_images(images);
    }
}

void CharaRequirementPanel::OnRace(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    std::shared_ptr<::cw::header::RaceHeader> selected;
    for (const auto& race : ::cw::setting::Setting::s_races)
        if (race && !arg_event->title.empty() && race->m_name == arg_event->title) { selected = race; break; }
    if (!selected && arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < ::cw::setting::Setting::s_races.size())
        selected = ::cw::setting::Setting::s_races[static_cast<std::size_t>(arg_event->id)];
    if (selected) for (const auto& info : _get_infos()) if (info) info->m_race = selected;
}

void CharaRequirementPanel::OnSelectSex(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    String selected = arg_event->title;
    if (selected.empty() && arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < ::cw::setting::Setting::s_sexcoupons.size())
        selected = ::cw::setting::Setting::s_sexcoupons[static_cast<std::size_t>(arg_event->id)];
    if (selected.empty()) return;
    for (const auto& info : _get_infos()) if (info)
        info->m_sex = selected.rfind("＿", 0) == 0 ? selected : "＿" + selected;
    _update_images(std::nullopt);
}

void CharaRequirementPanel::OnSelectAge(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    String selected = arg_event->title;
    if (selected.empty() && arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < ::cw::setting::Setting::s_periodcoupons.size())
        selected = ::cw::setting::Setting::s_periodcoupons[static_cast<std::size_t>(arg_event->id)];
    if (selected.empty()) return;
    for (const auto& info : _get_infos()) if (info)
        info->m_age = selected.rfind("＿", 0) == 0 ? selected : "＿" + selected;
    _update_images(std::nullopt);
}

void CharaRequirementPanel::OnSelectTalent(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    String selected = arg_event->title;
    if (selected.empty() && arg_event->id >= 0) {
        std::vector<String> talents;
        for (const auto& nature : ::cw::setting::Setting::s_natures)
            if (nature && (!nature->m_special || m_show_specialtalent_typed)) talents.push_back("＿" + nature->m_name);
        if (static_cast<std::size_t>(arg_event->id) < talents.size()) selected = talents[static_cast<std::size_t>(arg_event->id)];
    }
    if (selected.empty()) return;
    for (const auto& info : _get_infos()) if (info)
        info->m_talent = selected.rfind("＿", 0) == 0 ? selected : "＿" + selected;
    _update_images(std::nullopt);
}

void CharaRequirementPanel::OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    set_random();
}

void CharaRequirementPanel::_update_images(std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_img)
{
    String selected_path;
    if ((!arg_img || arg_img->empty()) && m_imgselection_typed > 0 &&
        static_cast<std::size_t>(m_imgselection_typed - 1) < m_imgpathlist_typed.size())
        selected_path = m_imgpathlist_typed[static_cast<std::size_t>(m_imgselection_typed - 1)];
    if (arg_img && arg_img->size() == 1 && arg_img->front())
        selected_path = arg_img->front()->m_path;

    m_imgpathlist_typed.clear();
    m_imgchoice_labels_typed.clear();
    std::vector<std::tuple<Integer, String, String>> choices;
    // Python版と同様、ドロップパスは重複も含め入力順を保つ。 重
    // 複を除いた順序・ラベル・パスの顔候補の組だけを自然順に並べる。
    for (const auto& path : m_dropfiles_typed)
        choices.emplace_back(-1, "<外部ファイル> " + path_basename(path), path);
    std::set<std::tuple<Integer, String, String>> face_candidates;
    for (const auto& info : _get_infos()) if (info) {
        const auto groups = ::cw::util::get_facepaths(info->m_sex, info->m_age, true);
        for (const auto& [dpaths, paths] : groups) {
            if (!dpaths) continue;
            const auto order = std::get<0>(*dpaths);
            const auto& directory = std::get<1>(*dpaths);
            for (const auto& group : paths) if (!group.empty()) {
                const auto& image = group.front();
                if (!image || image->m_path.empty()) continue;
                String label = directory;
                if (!label.empty() && label.back() != '/' && label.back() != '\\') label += '/';
                label += path_basename(image->m_path);
                face_candidates.emplace(order, std::move(label), image->m_path);
            }
        }
    }
    std::vector<std::tuple<Integer, String, String>> faces(face_candidates.begin(), face_candidates.end());
    std::stable_sort(faces.begin(), faces.end(), [](const auto& lhs, const auto& rhs) {
        if (std::get<0>(lhs) != std::get<0>(rhs)) return std::get<0>(lhs) < std::get<0>(rhs);
        const auto label = ::cw::util::logical_compare(std::get<1>(lhs), std::get<1>(rhs));
        if (label) return label < 0;
        return ::cw::util::logical_compare(std::get<2>(lhs), std::get<2>(rhs)) < 0;
    });
    choices.insert(choices.end(), faces.begin(), faces.end());
    for (auto& [unused, label, path] : choices) {
        (void)unused;
        m_imgchoice_labels_typed.push_back(std::move(label));
        m_imgpathlist_typed.push_back(std::move(path));
    }

    m_imgselection_typed = 0;
    if (!selected_path.empty()) {
        const auto found = std::find(m_imgpathlist_typed.begin(), m_imgpathlist_typed.end(), selected_path);
        if (found != m_imgpathlist_typed.end())
            m_imgselection_typed = static_cast<Integer>(std::distance(m_imgpathlist_typed.begin(), found)) + 1;
    }
    std::vector<String> display_choices{no_change_text()};
    display_choices.insert(display_choices.end(), m_imgchoice_labels_typed.begin(), m_imgchoice_labels_typed.end());
    if (ui_adapter()->image_choices)
        ui_adapter()->image_choices("chara_requirement.image", display_choices,
                                     m_imgselection_typed, m_imgcentering_typed);
    _select_image();
}

// 選択された画像集合と倍率フラグが共通の場合だけプレビューする。 編集値が混
// 在する場合は単一プレビューを出さず、この比較で画像バイトの検証はしない。
void CharaRequirementPanel::_select_image(::cw::lib::Real render_scale)
{
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> preview;
    const auto infos = _get_infos();
    bool first = true, scaled = false;
    for (const auto& info : infos) if (info) {
        if (first) { preview = info->m_imgpaths; scaled = info->m_can_loaded_scaledimage; first = false; }
        else if (!same_images(preview, info->m_imgpaths) || scaled != info->m_can_loaded_scaledimage) {
            preview.clear(); break;
        }
    }
    if (ui_adapter()->image_preview)
        ui_adapter()->image_preview("chara_requirement.image", preview);
    if (!m_img) m_img = std::make_shared<::cw::util::CWPyStaticBitmap>();
    auto selected = preview;
    if (::cw::util::is_face_variant_imageinfos(selected))
        selected = ::cw::util::pick_up_basename_image(std::move(selected), false);
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> bitmaps, keys;
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> loaded_infos;
    for (const auto& info : selected) if (info) {
        try {
            auto raw = ::cw::util::load_wxbmp(info->m_path, true, {}, {}, std::nullopt,
                true, true, true, std::nullopt);
            const auto normalized = ::cw::lib::ui::resource_surface_for_scale(raw, render_scale);
            if (!raw || !normalized) continue;
            auto bmp = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*bmp) = *normalized;
            ::cw::lib::ui::inherit_surface_animation(bmp, raw);
            bitmaps.push_back(bmp); keys.push_back(raw); loaded_infos.push_back(info);
        } catch (...) {}
    }
    if (selected.empty()) {
        auto fallback = native_object_value<::cw::lib::Bitmap>(m_defaultface);
        if (!fallback && ::cw::g_cwpy) try {
            const auto* name = std::get_if<String>(&::cw::g_cwpy->m_skindir.storage());
            const auto path = ::cw::util::find_resource(::cw::util::join_paths({name ? *name : String{},
                String("Resource/Image/Card/BATTLE")}), ::cw::g_M_IMG);
            fallback = ::cw::util::load_wxbmp(path, true, {}, {}, std::nullopt, true, true, true, std::nullopt);
            m_defaultface = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(fallback));
        } catch (...) {}
        if (fallback) {
            auto normalized = ::cw::lib::ui::resource_surface_for_scale(fallback, render_scale);
            auto bmp = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*bmp) = *normalized;
            ::cw::lib::ui::inherit_surface_animation(bmp, fallback);
            bitmaps.push_back(bmp); keys.push_back(fallback);
        }
    }
    m_img->SetBitmap(std::move(bitmaps), std::move(keys), std::move(loaded_infos));
}

std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> CharaRequirementPanel::_get_infos()
{
    if (m_cindex_typed == 0) return m_infos_typed;
    if (m_cindex_typed < 1 || static_cast<std::size_t>(m_cindex_typed) > m_infos_typed.size()) return {};
    return {m_infos_typed[static_cast<std::size_t>(m_cindex_typed - 1)]};
}

void CharaRequirementPanel::select_target(::cw::lib::Integer arg_cindex)
{
    m_proc_typed = true;
    m_cindex_typed = std::clamp<Integer>(arg_cindex, 0, static_cast<Integer>(m_infos_typed.size()));
    const auto infos = _get_infos();
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> common_images;
    bool common_centering = true;
    bool first = true;
    for (const auto& info : infos) if (info) {
        if (first) {
            common_images = info->m_imgpaths;
            common_centering = std::all_of(common_images.begin(), common_images.end(),
                [](const auto& image) { return image && image->m_postype == "Center"; });
            first = false;
        } else if (!same_images(common_images, info->m_imgpaths)) {
            common_images.clear();
            common_centering = true;
        }
    }
    m_imgcentering_typed = common_centering;
    // 複数対象で一致しない文字列項目はnative側が空欄表示し、画像は
    // same_imagesで一致した場合だけプレビューへ反映する。
    _update_images(first ? std::nullopt : std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>(common_images));
    m_proc_typed = false;
}

// ネイティブは素質の選択肢が空なら古い値を残すが、Python版は一覧
// から抽選する。 画像選択は先に消し、顔が見つからなければ空のままにす
// る。 すべてのランダム項目に、同じ空候補時の保持方針を適用しない。
void CharaRequirementPanel::set_random()
{
    std::vector<std::shared_ptr<CharaInfo>> infos = _get_infos();
    for (const auto& info : infos) if (info) {
        // Python版の必須情報パネルは、CharaInfo.set_randomfeatures と異なり
        // 特徴（makings）と能力型(type)を変更しない。ここでは race/sex/age/
        // talent/image/name だけを同じ順序でランダム化する。
        info->m_race = dice_roll_choice(::cw::setting::Setting::s_races);
        if (!info->m_race) info->m_race = default_race();
        info->m_sex = dice_roll_choice(::cw::setting::Setting::s_sexcoupons);
        info->m_age = dice_roll_choice(::cw::setting::Setting::s_periodcoupons);
        std::vector<String> talents;
        for (const auto& nature : ::cw::setting::Setting::s_natures)
            if (nature && (!nature->m_special || m_show_specialtalent_typed)) talents.push_back("＿" + nature->m_name);
        if (!talents.empty()) info->m_talent = dice_roll_choice(talents);
        info->m_imgpaths.clear();
        std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> faces;
        {
            for (const auto& [unused, values] : ::cw::util::get_facepaths(info->m_sex, info->m_age, true)) {
                (void)unused;
                faces.insert(faces.end(), values.begin(), values.end());
            }
        }if (!faces.empty()) for (const auto& image : dice_choice(faces)) {
            auto copy = copy_image(image);
            if (copy) {
                copy->m_postype = m_imgcentering_typed ? "Center" : "Default";
                info->m_imgpaths.push_back(copy);
            }
        }
        info->m_can_loaded_scaledimage = true;
        for (auto& image : info->m_imgpaths)
            if (image) image->m_postype = m_imgcentering_typed ? "Center" : "Default";
        if (info->m_input_name.empty()) {
            for (const auto& sex : ::cw::setting::Setting::s_sexes)
                if (sex && info->m_sex == "＿" + sex->m_name) {
                    const String name = ::cw::dialog::create::get_randomname(sex->m_subname);
                    if (!name.empty()) info->m_name = name;
                    break;
                }
        }
    }
    select_target(m_cindex_typed);
    _update_okbtn();
}

void CharaSelectablePanel::initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> arg_infos, bool arg_create)
{
    (void)arg_parent;
    m_infos_typed = std::move(arg_infos);
    m_create_typed = arg_create;
    m_cindex_typed = 0;
    m_makings_typed = ::cw::setting::Setting::s_makingcoupons;
    m_makinglabels_typed.clear();
    for (std::size_t i = 0; i < m_makings_typed.size(); ++i) {
        String label = i < ::cw::setting::Setting::s_makingnames.size()
            ? ::cw::setting::Setting::s_makingnames[i] : strip_coupon_prefix(m_makings_typed[i]);
        if (label.empty() && i < ::cw::setting::Setting::s_makings.size() &&
            ::cw::setting::Setting::s_makings[i])
            label = ::cw::setting::Setting::s_makings[i]->m_name;
        m_makinglabels_typed.push_back(std::move(label));
    }
    // 共通パネルモデルで、特徴ラベルを定義順に保つ。 全ホストで CharacterEditDialog
    // のPresentationが操作部品を構築する。
    _bind();
    _do_layout();
}

void CharaSelectablePanel::_bind()
{
    if (ui_adapter()->bind) ui_adapter()->bind("chara_selectable");
    // 共通Presentationが、特徴の操作をOnCheck/OnAutoBtn/OnClearBtnへ渡す。
}

void CharaSelectablePanel::_do_layout()
{
    if (ui_adapter()->layout) ui_adapter()->layout("chara_selectable");
    // 共通アダプターへ通知し、配置はCharacterEditDialogのPresentation
    // に任せる。 このパネルのコールバックでOS固有のフォームは作らない。
}

void CharaSelectablePanel::OnCheck(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0 || static_cast<std::size_t>(arg_event->id) >= m_makings_typed.size()) return;
    const String making = m_makings_typed[static_cast<std::size_t>(arg_event->id)];
    const bool checked = arg_event->title.empty() ? arg_event->visible :
        (arg_event->title != "0" && arg_event->title != "false");
    for (const auto& info : _get_infos()) if (info) {
        if (checked) info->m_makings.insert(making);
        else info->m_makings.erase(making);
    }
    if (checked) {
        const auto index = static_cast<std::size_t>(arg_event->id);
        const auto pair_index = index % 2 == 0 ? index + 1 : index - 1;
        if (pair_index < m_makings_typed.size()) for (const auto& info : _get_infos()) if (info) info->m_makings.erase(m_makings_typed[pair_index]);
    }
}

void CharaSelectablePanel::OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    set_random();
}

void CharaSelectablePanel::OnClearBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    for (const auto& info : _get_infos()) if (info) info->m_makings.clear();
    select_target(m_cindex_typed);
}

std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> CharaSelectablePanel::_get_infos()
{
    if (m_cindex_typed == 0) return m_infos_typed;
    if (m_cindex_typed < 1 || static_cast<std::size_t>(m_cindex_typed) > m_infos_typed.size()) return {};
    return {m_infos_typed[static_cast<std::size_t>(m_cindex_typed - 1)]};
}

// Python版の不具合への意図的な修正。cindex==0で infos[cindex-1]
// を繰り返し読むと最後のメンバーだけを見る。 ここでは選択した各
// メンバーを比較し、特徴が異なる場合は未確定のチェック状態にする。
void CharaSelectablePanel::select_target(::cw::lib::Integer arg_cindex)
{
    m_cindex_typed = std::clamp<Integer>(arg_cindex, 0, static_cast<Integer>(m_infos_typed.size()));
    if (ui_adapter()->checkbox) {
        const auto infos = _get_infos();
        for (std::size_t i = 0; i < m_makings_typed.size(); ++i) {
            const String& making = m_makings_typed[i];
            bool first = true;
            bool checked = false;
            bool mixed = false;
            for (const auto& info : infos) if (info) {
                const bool value = info->m_makings.count(making) != 0;
                if (first) { checked = value; first = false; }
                else if (checked != value) { mixed = true; break; }
            }
            const Integer state = first ? 0 : (mixed ? -1 : (checked ? 1 : 0));
            ui_adapter()->checkbox("chara_selectable.making." + std::to_string(i), state);
        }
    }
    // チェックのコールバックには、1=選択、0=未選択、-1=混在を通知済み。
    // 実際のコントロールへの反映はWindows/SDLアダプターが担当する。
}

// このパネルは、CharaInfo・必須項目パネルの広いランダム設定と
// は異なり、特徴だけをランダム化する。 消去は別に特徴をリセットする。
void CharaSelectablePanel::set_random()
{
    for (const auto& info : _get_infos()) if (info) info->m_makings = ::cw::dialog::create::get_randommakings();
    select_target(m_cindex_typed);
}

void CharacterEditDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Integer arg_selected, bool arg_create)
{
    cancel_document_request();
    m_native_parent_handle = arg_parent ? arg_parent->native_handle : 0;
    if (m_native_parent_handle) ui_adapter()->native_owner =
        reinterpret_cast<void*>(m_native_parent_handle);
    m_create_typed = arg_create;
    m_selected_typed = arg_selected;
    m_modal_closed = false;
    m_modal_result_ok = false;
    m_presentation_tab = 0;
    m_presentation_trait_scroll = 0;
    m_common_presentation_active = false;
    m_presentation_operation_pending = false;
    m_presentation_focus = 10;
    m_presentation_popup_kind = 0;
    m_presentation_popup_index = 0;
    m_presentation_popup_values.clear();
    m_presentation_name_draft.clear();
    m_presentation_composition.clear();
    m_presentation_name_select_all = false;
    m_presentation_dirty = true;
    m_image_documents.clear();
    m_infos.clear();
    m_pcards_typed.clear();
    if (arg_create) {
        auto info = std::make_shared<CharaInfo>();
        info->initialize(std::nullopt, true);
        m_infos.push_back(info);
        m_selected_typed = 0;
    } else if (::cw::g_cwpy) {
        m_pcards_typed = ::cw::g_cwpy->m_pcards;
        for (const auto& card : m_pcards_typed) {
            auto info = std::make_shared<CharaInfo>();
            std::shared_ptr<::cw::character::Player> player = std::static_pointer_cast<::cw::character::Player>(card);
            info->initialize(player, true);
            m_infos.push_back(info);
        }
    }
    m_targets.clear();
    m_targets.push_back("全員");
    for (const auto& info : m_infos) m_targets.push_back(info ? info->m_name : String{});
    m_selected_typed = std::clamp<Integer>(m_selected_typed + 1, 0, static_cast<Integer>(m_infos.size()));
    m_pane_req = std::make_shared<CharaRequirementPanel>();
    m_pane_req->initialize(nullptr, m_infos, arg_create);
    m_pane_sel = std::make_shared<CharaSelectablePanel>();
    m_pane_sel->initialize(nullptr, m_infos, arg_create);
    // モデルを完全な共通Presentationとして一度公開する。 ホスト
    // は拡大縮小・合成してよいが、コントロールツリーを作り直してはならない。
    _bind();
    _do_layout();
    select_target();
    m_common_presentation_active = true;
    (void)::cw::dialog::show_common_modal_presentation(shared_from_this());
    m_common_presentation_active = false;
}

void CharacterEditDialog::_bind()
{
    if (ui_adapter()->bind) ui_adapter()->bind("character_edit");
}

void CharacterEditDialog::_do_layout()
{
    if (ui_adapter()->layout) ui_adapter()->layout("character_edit");
}

std::vector<std::shared_ptr<::cw::debug::charaedit::CharaInfo>> CharacterEditDialog::_get_infos()
{
    if (m_selected_typed == 0) return m_infos;
    if (m_selected_typed < 1 || static_cast<std::size_t>(m_selected_typed) > m_infos.size()) return {};
    return {m_infos[static_cast<std::size_t>(m_selected_typed - 1)]};
}

void CharacterEditDialog::OnReCalcMaxLife(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0) return;
    const bool checked = arg_event->id != 0;
    for (const auto& info : _get_infos()) if (info) info->m_recalc_maxlife = checked;
}

void CharacterEditDialog::OnReCalcParameter(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0) return;
    const bool checked = arg_event->id != 0;
    for (const auto& info : _get_infos()) if (info) info->m_recalc_parameter = checked;
    if (ui_adapter()->enabled) ui_adapter()->enabled("character_edit.standard_type", checked);
}

void CharacterEditDialog::OnReCalcCoupons(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0) return;
    const bool checked = arg_event->id != 0;
    for (const auto& info : _get_infos()) if (info) info->m_recalc_coupons = checked;
}

void CharacterEditDialog::OnDebugCoupon(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0) return;
    const bool checked = arg_event->id != 0;
    for (const auto& info : _get_infos()) if (info) info->m_debug_coupon = checked;
}

void CharacterEditDialog::OnLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_selected_typed <= 0) m_selected_typed = static_cast<Integer>(m_infos.size());
    else --m_selected_typed;
    select_target();
}

void CharacterEditDialog::OnRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_selected_typed >= static_cast<Integer>(m_infos.size())) m_selected_typed = 0;
    else ++m_selected_typed;
    select_target();
}

// ネイティブは既知の対象名を解決、またはstd::stoiで解析してから位
// 置を制限する。 解析不能・範囲外の文字列は例外になり得る。 Python
// のwxの選択位置の転送とは異なり、不正入力で何もしないことは保証しない。
void CharacterEditDialog::OnSelectTarget(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event) {
        Integer selected = arg_event->id;
        if (!arg_event->title.empty()) {
            const auto found = std::find(m_targets.begin(), m_targets.end(), arg_event->title);
            if (found != m_targets.end()) selected = static_cast<Integer>(std::distance(m_targets.begin(), found));
            else {
                { selected = std::stoi(arg_event->title); }}
        }
        m_selected_typed = std::clamp<Integer>(selected, 0, static_cast<Integer>(m_infos.size()));
    }
    select_target();
}

void CharacterEditDialog::OnStandardType(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    std::shared_ptr<::cw::features::SampleType> selected;
    bool selection_confirmed = false;
    if (arg_event->id > 0 && static_cast<std::size_t>(arg_event->id - 1) < ::cw::setting::Setting::s_sampletypes.size()) {
        selected = ::cw::setting::Setting::s_sampletypes[static_cast<std::size_t>(arg_event->id - 1)];
        selection_confirmed = true;
    }
    if (!selected && !arg_event->title.empty() && arg_event->title != "カスタム")
        for (const auto& sample : ::cw::setting::Setting::s_sampletypes)
            if (sample && sample->m_name == arg_event->title) { selected = sample; selection_confirmed = true; break; }
    if (!selected && arg_event->title == "カスタム") selection_confirmed = true;
    if (arg_event->id <= 0 && arg_event->title.empty() && ui_adapter()->choice_input) {
        std::vector<String> choices{"カスタム"};
        for (const auto& sample : ::cw::setting::Setting::s_sampletypes)
            if (sample) choices.push_back(sample->m_name);
        Integer initial = 0;
        String common;
        bool first = true;
        for (const auto& info : _get_infos()) if (info) {
            const String current = info->m_type && *info->m_type ? (*info->m_type)->m_name : String("カスタム");
            if (first) { common = current; first = false; }
            else if (common != current) { common.clear(); break; }
        }
        for (std::size_t i = 1; i < choices.size(); ++i)
            if (choices[i] == common) { initial = static_cast<Integer>(i); break; }
        const auto selected_index = ui_adapter()->choice_input("character_edit.standard_type", choices, initial);
        if (!selected_index) return;
        selection_confirmed = true;
        if (*selected_index > 0 && static_cast<std::size_t>(*selected_index) <= ::cw::setting::Setting::s_sampletypes.size())
            selected = ::cw::setting::Setting::s_sampletypes[static_cast<std::size_t>(*selected_index - 1)];
    }
    if (!selection_confirmed) return;
    for (const auto& info : _get_infos()) if (info) { info->m_type = selected; info->_calc_params(); }
    if (m_pane_req) m_pane_req->select_target(m_selected_typed);
}

void CharacterEditDialog::OnAutoBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_pane_req) m_pane_req->set_random();
    if (m_pane_sel) m_pane_sel->set_random();
}

void CharacterEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_modal_closed || m_document_token) return;
    if (m_create_typed) {
        if (m_infos.empty() || !m_infos.front() ||
            !::cw::lib::python_strip_has_text(m_infos.front()->m_name)) return;
        m_fpath = m_infos.front()->create_adventurer();
    } else {
        // Python版はwxの処理内で変更し、更新だけを予約する。 ネイ
        // ティブのPresentationはUIスレッドで動く間もCWPy
        // が実行中で、put_paramsは可視カードを反転し得るため、
        // 確定と更新を一緒にゲームキューへ送る。 ダイアログや編集中のCharaInfo
        // ではなく、受理した値と画像の使用権を保持する。
        std::vector<std::shared_ptr<CharaInfo>> infos;
        infos.reserve(m_infos.size());
        for (const auto& info : m_infos) {
            auto snapshot = info ? std::make_shared<CharaInfo>(*info) : nullptr;
            if (snapshot) {
                for (auto& image : snapshot->m_imgpaths)
                    if (image) image = std::make_shared<::cw::image::ImageInfo>(*image);
                for (auto& image : snapshot->m_imgpaths_base)
                    if (image) image = std::make_shared<::cw::image::ImageInfo>(*image);
            }
            infos.push_back(std::move(snapshot));
        }
        auto commit = [infos = std::move(infos), pcards = m_pcards_typed,
                       documents = std::move(m_image_documents)](
                          const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
            // 取得済みのCharaInfo・Playerの有効な組を枠順に適用
            // してから、変更位置を更新する。 全PCの一括確定や、空の任意項
            // 目がすべて以前の状態を保つ保証はない。 set_imagesが
            // 複製するまで、documentsが一時取り込み画像を所有する。
            (void)documents;
            std::vector<Integer> updates;
            const auto count = (std::min)(infos.size(), pcards.size());
            for (std::size_t i = 0; i < count; ++i) if (infos[i] && pcards[i]) {
                if (infos[i]->put_params(std::static_pointer_cast<::cw::character::Player>(pcards[i])))
                    updates.push_back(static_cast<Integer>(i));
            }
            refresh_edited_cards(updates, pcards);
            return {};
        };
        if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(std::move(commit));
        else commit({}, {});
    }
    // 新規作成は取り込み画像を同期的に複製済み。 編集では、このダ
    // イアログを閉じる前に、受理したゲームコマンドへ使用権を渡す。
    m_image_documents.clear();
    if (ui_adapter()->modal) ui_adapter()->modal("character_edit", true);
    cancel_document_request();
    m_modal_closed = true;
    m_modal_result_ok = true;
}

void CharacterEditDialog::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_modal_closed) return;
    cancel_document_request();
    m_image_documents.clear();
    // Python の wx.ID_CANCEL と同じく編集内容を破棄し、OK結果を返さず閉じる。
    if (ui_adapter()->modal) ui_adapter()->modal("character_edit", false);
    m_modal_closed = true;
    m_modal_result_ok = false;
}

bool CharacterEditDialog::request_image_document(ImageDocumentRequest start)
{
    namespace doc = ::cw::lib::documents;
    if (m_modal_closed || m_document_token || weak_from_this().expired()) return false;
    doc::Request request;
    request.operation = doc::Operation::OpenFiles;
    request.title = "画像ファイルを選択";
    request.multiple = true;
    request.filters = {{"画像ファイル", "bmp;gif;jpg;jpeg;png;tif;tiff;xpm;webp"},
                       {"すべてのファイル", "*"}};
    // 提供側のURIはダイアログの受理時にだけ Character::set_images
    // が複製する。 共通の上限付き一時保存境界を使い、それまでは
    // m_image_documents に使用権を保持する。
    request.transient_read_limit = 64u * 1024u * 1024u;
    const auto token = std::make_shared<int>(0);
    m_document_token = token;
    m_presentation_dirty = true;
    const auto weak = weak_from_this();
    try {
        // 取得自体はプラットフォームの基本機能。 ScenarioSelectと
        // 同様に差し込む場合も、要求・プレビュー・確定・寿命の方針は共通にする。
        if (!start) start = ::cw::dialog::request_presentation_document;
        auto ticket = start(
            request, [weak, token](doc::Result result) {
                if (const auto self = weak.lock())
                    self->finish_image_document(token, std::move(result));
            });
        if (m_document_token == token) {
            m_document_ticket = std::move(ticket);
            if (!m_document_ticket || !m_document_ticket->pending())
                finish_image_document(token, {doc::Status::Failed, {},
                    "ファイル選択を開始できませんでした。"});
        }
    } catch (const std::exception& error) {
        finish_image_document(token, {doc::Status::Failed, {}, error.what()});
    } catch (...) {
        finish_image_document(token, {doc::Status::Failed, {},
            "ファイル選択に失敗しました。"});
    }
    return true;
}

void CharacterEditDialog::finish_image_document(
    const std::shared_ptr<int>& token, ::cw::lib::documents::Result result)
{
    namespace doc = ::cw::lib::documents;
    if (m_document_token != token || m_modal_closed) return;
    cancel_document_request();
    if (result.status == doc::Status::Cancelled) return;
    if (result.status != doc::Status::Success || result.documents.empty()) {
        (void)::cw::dialog::message::show_common_modal_message(
            "画像ファイルを選択", result.error.empty()
                ? "画像ファイルを選択できませんでした。" : result.error, false, true);
        return;
    }
    std::vector<String> paths;
    for (const auto& document : result.documents) {
        if (document.kind != doc::LocationKind::NativePath || document.location.empty()) {
            (void)::cw::dialog::message::show_common_modal_message(
                "画像ファイルを選択",
                "選択した画像を永続的なローカルファイルとして参照できません。",
                false, true);
            return;
        }
        paths.push_back(document.location);
    }
    if (m_pane_req) m_pane_req->_put_image(std::move(paths));
    // プレビューを更新しただけで Document::access
    // を解放しない。 ローカルの一時ファイルは、最後の共通
    // 確定処理の入力となる。 別のキャラクターへ割り当てた画
    // 像や候補内の画像も含め、全一時領域の使用権を保持する。
    m_image_documents.insert(m_image_documents.end(),
        std::make_move_iterator(result.documents.begin()),
        std::make_move_iterator(result.documents.end()));
    m_presentation_focus = 13;
    m_presentation_popup_kind = 0;
    m_presentation_dirty = true;
}

void CharacterEditDialog::cancel_document_request() noexcept
{
    if (m_document_ticket) m_document_ticket->cancel();
    m_document_ticket.reset();
    m_document_token.reset();
    m_presentation_dirty = true;
}

void CharacterEditDialog::select_target()
{
    if (m_pane_req) m_pane_req->select_target(m_selected_typed);
    if (m_pane_sel) m_pane_sel->select_target(m_selected_typed);
    {
        const auto infos=_get_infos();bool first=true;String common;
        for(const auto& info:infos)if(info){if(first){common=info->m_name;first=false;}else if(common!=info->m_name){common.clear();break;}}
        m_presentation_name_draft=common;m_presentation_name_select_all=infos.size()>1;
        m_presentation_composition.clear();m_presentation_popup_kind=0;m_presentation_popup_values.clear();
    }
    if (ui_adapter()->selection) {
        ui_adapter()->selection("character_edit.target", m_selected_typed);
        const auto infos = _get_infos();
        const auto checkbox_state = [&infos](auto selector) {
            bool first = true;
            bool value = false;
            for (const auto& info : infos) if (info) {
                const bool current = selector(*info);
                if (first) { value = current; first = false; }
                else if (value != current) return Integer{-1};
            }
            if (first) return Integer{0};
            return value ? Integer{1} : Integer{0};
        };
        ui_adapter()->checkbox("character_edit.recalc_maxlife", checkbox_state([](const CharaInfo& info) { return info.m_recalc_maxlife; }));
        ui_adapter()->checkbox("character_edit.recalc_parameter", checkbox_state([](const CharaInfo& info) { return info.m_recalc_parameter; }));
        ui_adapter()->checkbox("character_edit.recalc_coupons", checkbox_state([](const CharaInfo& info) { return info.m_recalc_coupons; }));
        ui_adapter()->checkbox("character_edit.debug_coupon", checkbox_state([](const CharaInfo& info) { return info.m_debug_coupon; }));
    }
    // wxの三状態チェックと同様、1=選択、0=未選択、-1=混在をコ
    // ールバックへ渡す。 アダプターが実際のコントロールへ反映する。
    m_presentation_dirty = true;
}

::cw::dialog::DialogPresentation CharacterEditDialog::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    const Integer trait_count=m_pane_sel?static_cast<Integer>(m_pane_sel->m_makings_typed.size()):0;
    const Integer trait_rows=(trait_count+3)/4;
    // 初期サイズの計算には四列換算の行数を使う。タブ
    // のページ切り替えでウィンドウ寸法を変えない。
    const Integer width = m_create_typed ? 600 : 720;
    const Integer height = m_create_typed ? std::max<Integer>(360,84+trait_rows*23) : 520;
    // 出力解像度のフレーム。文字は最終表示サイズでラスタライズする。
    auto surface = retained_ui::scaled_frame(width, height, {245, 245, 245, 255},
                                             retained_ui::presentation_scale(game_window_scale));

    ::cw::dialog::DialogPresentation out;
    out.route = "DEBUG_CHARACTER_EDIT";
    out.title = m_create_typed ? "冒険者の作成" : "キャラクターの編集";

    const auto button = [&](const String& text, const ::cw::lib::Rect& rect,
                            Action action, int index, bool enabled = true) {
        ::cw::dialog::add_presentation_control(out, surface, text, rect, action,
                                               index, enabled, "button", true);
    };
    const auto label = [&](const String& text, Integer x, Integer y, bool bold = false) {
        ::cw::lib::ui::draw_label(surface, text, x, y, 14, bold, {0, 0, 0});
    };
    const auto checkbox = [&](const String& text, const ::cw::lib::Rect& rect,
                              const int state, const Action action, const int index,
                              const bool enabled = true) {
        const ::cw::lib::Rect box{rect.x,rect.y+(rect.height-14)/2,14,14};
        ::cw::lib::ui::draw_checkbox(surface,box,state>0,enabled,state<0);
        label(text,rect.x+20,rect.y+(rect.height-14)/2);
        out.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,false,text,{}});
    };
    const auto selected = _get_infos();
    const auto common_string = [&](const auto member, const String& empty = "未設定",
                                   const String& mixed = "(複数の値)") {
        String value;
        bool first = true;
        for (const auto& info : selected) if (info) {
            const String current = strip_coupon_prefix(info.get()->*member);
            if (first) { value = current; first = false; }
            else if (value != current) return mixed;
        }
        return first || value.empty() ? empty : value;
    };
    const auto common_integer = [&](const auto member, Integer fallback = 0) {
        Integer value = fallback;
        bool first = true;
        for (const auto& info : selected) if (info) {
            const Integer current = info.get()->*member;
            if (first) { value = current; first = false; }
            else if (value != current) return Integer{-1};
        }
        return first ? fallback : value;
    };
    const auto common_bool = [&](const auto member) {
        int value = 0;
        bool first = true;
        for (const auto& info : selected) if (info) {
            const bool current = info.get()->*member;
            if (first) { value = current ? 1 : 0; first = false; }
            else if (value != (current ? 1 : 0)) return -1;
        }
        return first ? 0 : value;
    };
    // 簡易作成は、Python版のタブと右側のボタン列に合わせる。
    // 複数キャラクター用デバッガーの編集・再計算フォームは使わない。
    if (m_create_typed) {
        // Pythonのwx部品と同様、デバッガーのDPIでラスタ
        // ライズする。 ALLはデバッガー配下を二倍で表示するため
        // 、12pxの字を後から広げず、同じ出力倍率で画像を作る。
        const auto dpi=retained_ui::presentation_scale(game_window_scale);
        const auto output_scale=dpi*(::cw::dialog::is_all_mode()?2.0:1.0);
        const auto px=[&](Integer value){return static_cast<Integer>(std::lround(value*output_scale));};
        const auto physical=[&](::cw::lib::Rect rect){
            return ::cw::lib::Rect{px(rect.x),px(rect.y),px(rect.width),px(rect.height)};
        };
        surface->size={px(width),px(height)};
        // このページはすでに px() の機器画素で描くため、通常の画素サーフェスを使う。
        surface->draw_scale=1.0;
        surface->rgba.assign(static_cast<std::size_t>(surface->size.width)*surface->size.height*4,245);
        ::cw::lib::simd_image::force_alpha(surface->rgba.data(),surface->rgba.size()/4,255);
        const auto font=::cw::lib::ui::configured_font("dlgmsg2",12);
        const auto text=[&](const String& value,Integer x,Integer y,Integer size=12,bool bold=false){
            ::cw::lib::ui::draw_label(surface,value,px(x),px(y),px(size),bold,{0,0,0},font.face);
        };
        const auto label=[&](const String& value,Integer x,Integer y,bool bold=false){text(value,x,y,12,bold);};
        const auto button=[&](const String& value,const ::cw::lib::Rect& rect,Action action,int index,bool enabled=true){
            const auto r=physical(rect);
            ::cw::lib::ui::draw_button_face(surface,r,{enabled});
            auto clip=std::make_shared<::cw::lib::Surface>();
            clip->size={r.width,r.height};clip->rgba.assign(static_cast<std::size_t>(r.width)*r.height*4,0);
            const auto glyph=::cw::lib::ui::render_label(value,px(12),false,enabled?
                std::tuple<Integer,Integer,Integer>{0,0,0}:std::tuple<Integer,Integer,Integer>{100,100,100},font.face);
            if(glyph)::cw::lib::ui::blend_surface(clip,glyph,std::max<Integer>(0,(r.width-glyph->size.width)/2),
                std::max<Integer>(0,(r.height-glyph->size.height)/2));
            ::cw::lib::ui::blend_surface(surface,clip,r.x,r.y);
            out.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
                static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,false,value,{}});
        };
        const auto checkbox=[&](const String& value,const ::cw::lib::Rect& rect,int state,Action action,int index){
            ::cw::lib::ui::draw_checkbox(surface,physical({rect.x,rect.y+(rect.height-14)/2,14,14}),state>0);
            text(value,rect.x+20,rect.y+(rect.height-12)/2);
            out.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
                static_cast<float>(rect.width),static_cast<float>(rect.height),true,false,value,{}});
        };
        const auto group = [&](const String& text, const ::cw::lib::Rect& rect) {
            ::cw::lib::ui::draw_sunken_border(surface, physical(rect));
            ::cw::lib::ui::fill_rect(surface,physical({rect.x+5,rect.y-5,static_cast<Integer>(text.size())*5+8,16}),{245,245,245,255});
            label(text,rect.x+8,rect.y-5);
        };
        button("必須情報",{8,8,64,24},Action::Auxiliary,0);
        button("選択情報",{74,8,64,24},Action::Auxiliary,1);
        ::cw::lib::ui::fill_rect(surface,physical({m_presentation_tab==0?8:74,29,64,3}),{0,120,215,255});
        button("能力型",{width-74,8,70,24},Action::Auxiliary,24);
        button("自動",{width-74,38,70,24},Action::Auxiliary,30);
        button("登録",{width-74,height-58,70,24},Action::Accept,40,!m_pane_req||m_pane_req->m_ok_enabled_typed);
        button("中止",{width-74,height-28,70,24},Action::Cancel,41);
        const auto info = selected.empty() ? std::shared_ptr<CharaInfo>{} : selected.front();
        if (m_presentation_tab == 0) {
            group("名前",{18,48,196,44});
            const ::cw::lib::Rect rect{28,62,124,22};
            ::cw::lib::ui::fill_rect(surface,physical(rect),{255,255,255,255});
            ::cw::lib::ui::draw_sunken_border(surface,physical(rect));
            // 置換のIME変換中は、選択された古い名前を未確定文字の前に
            // 描かない。 未確定文字が表示外へ押し出されることを防ぐ。
            String visible_name=(m_presentation_name_select_all&&!m_presentation_composition.empty()
                ?String{}:m_presentation_name_draft)+m_presentation_composition;
            while(!visible_name.empty()&&::cw::lib::ui::label_width(visible_name,px(12),false,font.face)>px(116)){
                std::size_t next=1;
                while(next<visible_name.size()&&(static_cast<unsigned char>(visible_name[next])&0xc0)==0x80)++next;
                visible_name.erase(0,next);
            }
            text(visible_name,32,65);
            if(m_presentation_focus==10)
                ::cw::lib::ui::fill_rect(surface,physical({28,83,124,1}),{0,120,215,255});
            out.controls.push_back({Action::Auxiliary,10,28,62,124,22,true,false,"名前","キャラクター名"});
            button("自動",{156,62,48,22},Action::Auxiliary,11);
            group("レベル",{18,104,196,42});
            button("Lv "+std::to_string(info?info->m_level:1),{28,118,176,22},Action::Auxiliary,12);
            group("能力型",{18,158,196,38});
            label(info&&info->m_type&&*info->m_type?(*info->m_type)->m_name:String("カスタム"),82,173);
            group("イメージ",{222,48,170,168});
            paint_character_portrait(m_pane_req, surface, physical({270,62,74,94}), output_scale);
            String image_name=no_change_text();
            if(m_pane_req&&m_pane_req->m_imgselection_typed>0&&
                static_cast<std::size_t>(m_pane_req->m_imgselection_typed-1)<m_pane_req->m_imgchoice_labels_typed.size())
                image_name=m_pane_req->m_imgchoice_labels_typed[m_pane_req->m_imgselection_typed-1];
            button(image_name,{232,166,128,22},Action::Auxiliary,13);
            button("…",{364,166,22,22},Action::Auxiliary,19);
            checkbox("中央寄せ",{302,190,88,22},m_pane_req&&m_pane_req->m_imgcentering_typed,Action::Auxiliary,14);
            // Python版と同様、未知の種族だけの場合は種族選択欄を隠す。
            if(!::cw::setting::Setting::s_races.empty()&&
                !(::cw::setting::Setting::s_races.size()==1&&
                  std::dynamic_pointer_cast<::cw::header::UnknownRaceHeader>(::cw::setting::Setting::s_races.front()))){
                button(info&&info->m_race?info->m_race->m_name:String("未設定"),{28,198,176,22},Action::Auxiliary,15);
            }
            const auto radios = [&](const String& title,const std::vector<String>& values,
                                    const String& current,Integer x,Integer w,int first) {
                const Integer rows=std::max<Integer>(2,(static_cast<Integer>(values.size())+2)/3);
                group(title,{x,232,w,rows*22+20});
                const Integer columns=(static_cast<Integer>(values.size())+rows-1)/rows;
                const Integer cell=w/std::max<Integer>(1,columns);
                for(std::size_t i=0;i<values.size();++i){
                    const Integer rx=x+5+(static_cast<Integer>(i)/rows)*cell,ry=248+(static_cast<Integer>(i)%rows)*22;
                    ::cw::lib::ui::draw_radio(surface,physical({rx,ry+3,13,13}),strip_coupon_prefix(current)==strip_coupon_prefix(values[i]));
                    text(strip_coupon_prefix(values[i]),rx+17,ry+2);
                    out.controls.push_back({Action::Auxiliary,first+static_cast<int>(i),static_cast<float>(rx),static_cast<float>(ry),
                        static_cast<float>(cell),22,true,false,strip_coupon_prefix(values[i]),{}});
                }
            };
            radios("性別",::cw::setting::Setting::s_sexcoupons,info?info->m_sex:String{},18,48,400);
            radios("年代",::cw::setting::Setting::s_periodcoupons,info?info->m_age:String{},70,102,500);
            std::vector<String> talents;
            for(const auto& nature: ::cw::setting::Setting::s_natures)
                if(nature&&(!nature->m_special||(m_pane_req&&m_pane_req->m_show_specialtalent_typed)))talents.push_back(nature->m_name);
            radios("素質",talents,info?info->m_talent:String{},176,216,600);
            button("自動選択",{width-162,height-28,74,24},Action::Auxiliary,31);
        } else if(m_pane_sel){
            m_presentation_trait_scroll=0;
            const Integer cell=(width-104)/4;
            for(Integer i=0;i<trait_count;++i){
                const auto& making=m_pane_sel->m_makings_typed[i];
                const auto caption=static_cast<std::size_t>(i)<m_pane_sel->m_makinglabels_typed.size()
                    ?m_pane_sel->m_makinglabels_typed[i]:strip_coupon_prefix(making);
                checkbox(caption,{18+(i%4)*cell,44+(i/4)*23,cell-4,22},
                    info&&info->m_makings.count(making),Action::Toggle,static_cast<int>(i));
            }
            button("自動選択",{width-252,height-28,78,24},Action::Auxiliary,25);
            button("クリア",{width-170,height-28,78,24},Action::Auxiliary,26);
        }
        if(m_presentation_popup_kind&&!m_presentation_popup_values.empty()){
            const Integer rows=std::min<Integer>(8,m_presentation_popup_values.size());
            const Integer first=std::clamp<Integer>(m_presentation_popup_index-rows/2,0,
                static_cast<Integer>(m_presentation_popup_values.size())-rows);
            for(Integer row=0;row<rows;++row)
                button(m_presentation_popup_values[first+row],{100,64+row*28,290,28},Action::Create,3000+static_cast<int>(first+row));
        }
        out.wants_text_input=m_presentation_tab==0&&m_presentation_focus==10&&!m_presentation_popup_kind;
        out.input_text=m_presentation_name_draft;out.composition=m_presentation_composition;
        auto& retained=dialog_retained_surface();
        ::cw::lib::ui::ScaledRetainedCanvas canvas(retained,{width,height},output_scale,{245,245,245,255});
        const ::cw::lib::Rect bounds{0,0,width,height};
        const bool changed=canvas.component_changed("character.create",bounds,retained_ui::surface_signature(surface,physical(bounds)));
        canvas.capture_base_and_restore();
        if(changed)canvas.composite(bounds,surface);
        retained_ui::finish(out,canvas,game_window_scale);
        return out;
    }
    if (!m_create_typed) {
        button("◀", {8, 8, 38, 30}, Action::Previous, 0);
        const String target = m_selected_typed >= 0 &&
            static_cast<std::size_t>(m_selected_typed) < m_targets.size()
            ? m_targets[static_cast<std::size_t>(m_selected_typed)] : String("全員");
        button(target, {50, 8, 190, 30}, Action::Select, 0);
        button("▶", {244, 8, 38, 30}, Action::Next, 0);
    } else {
        label("新しい冒険者", 12, 16, true);
    }
    button(m_presentation_tab == 0 ? "● 必須情報" : "必須情報",
           {300, 8, 130, 30}, Action::Auxiliary, 0);
    button(m_presentation_tab == 1 ? "● 特徴" : "特徴",
           {434, 8, 130, 30}, Action::Auxiliary, 1);

    const ::cw::lib::Rect panel{8, 48, 704, 395};
    ::cw::lib::ui::fill_rect(surface, panel, {252, 252, 252, 255});
    ::cw::lib::ui::draw_sunken_border(surface, panel);

    if (m_presentation_tab == 0) {
        const String name = common_string(&CharaInfo::m_name, "(空欄)", "");
        label("名前", 22, 66, true);
        const ::cw::lib::Rect name_rect{120,58,280,31};
        ::cw::lib::ui::fill_rect(surface,name_rect,{255,255,255,255});::cw::lib::ui::draw_sunken_border(surface,name_rect);
        if(m_presentation_focus==10)::cw::lib::ui::fill_rect(surface,{name_rect.x,name_rect.y,name_rect.width,1},{0,120,215,255});
        const String name_display=m_presentation_name_draft.empty()?name:m_presentation_name_draft;
        ::cw::lib::ui::draw_label(surface,name_display+m_presentation_composition,name_rect.x+6,name_rect.y+8,14,false,{0,0,0});
        out.controls.push_back({Action::Auxiliary,10,static_cast<float>(name_rect.x),static_cast<float>(name_rect.y),
            static_cast<float>(name_rect.width),static_cast<float>(name_rect.height),true,false,"名前","キャラクター名"});
        button("自動", {406, 58, 70, 31}, Action::Auxiliary, 11);

        const Integer level = common_integer(&CharaInfo::m_level, 1);
        label("レベル", 22, 108, true);
        button(level < 0 ? "―" : std::to_string(level),
               {120, 100, 120, 31}, Action::Auxiliary, 12);

        String image_label = no_change_text();
        if (m_pane_req && m_pane_req->m_imgselection_typed > 0 &&
            static_cast<std::size_t>(m_pane_req->m_imgselection_typed - 1) <
                m_pane_req->m_imgchoice_labels_typed.size())
            image_label = m_pane_req->m_imgchoice_labels_typed[
                static_cast<std::size_t>(m_pane_req->m_imgselection_typed - 1)];
        label("顔画像", 22, 150, true);
        button(image_label, {120, 142, 356, 31}, Action::Auxiliary, 13);
        checkbox("中央配置",{120,178,150,29},
                 m_pane_req && m_pane_req->m_imgcentering_typed ? 1 : 0,Action::Auxiliary,14);

        String race = "未設定";
        bool first_race = true;
        for (const auto& info : selected) if (info) {
            const String current = info->m_race ? info->m_race->m_name : String{};
            if (first_race) { race = current; first_race = false; }
            else if (race != current) {
                const auto unknown=default_race();race=unknown?unknown->m_name:String{};break;
            }
        }
        if (race.empty()) race = "未設定";
        label("種族", 22, 221, true);
        button(race, {120, 213, 180, 31}, Action::Auxiliary, 15);
        label("性別", 320, 221, true);
        const String default_sex=::cw::setting::Setting::s_sexcoupons.empty()?String("未設定"):
            strip_coupon_prefix(::cw::setting::Setting::s_sexcoupons.front());
        button(common_string(&CharaInfo::m_sex,"未設定",default_sex), {400, 213, 180, 31}, Action::Auxiliary, 16);
        label("年代", 22, 263, true);
        const String default_age=::cw::setting::Setting::s_periodcoupons.empty()?String("未設定"):
            strip_coupon_prefix(::cw::setting::Setting::s_periodcoupons.front());
        button(common_string(&CharaInfo::m_age,"未設定",default_age), {120, 255, 180, 31}, Action::Auxiliary, 17);
        label("素質", 320, 263, true);
        String default_talent="未設定";for(const auto& nature : ::cw::setting::Setting::s_natures)
            if(nature&&(!nature->m_special||m_pane_req&&m_pane_req->m_show_specialtalent_typed)){default_talent=nature->m_name;break;}
        button(common_string(&CharaInfo::m_talent,"未設定",default_talent), {400, 255, 180, 31}, Action::Auxiliary, 18);

        if (!m_create_typed) {
            checkbox("最大生命力を再計算",{22,306,210,29},
                     common_bool(&CharaInfo::m_recalc_maxlife),Action::Auxiliary,20);
            const int recalc_parameter = common_bool(&CharaInfo::m_recalc_parameter);
            checkbox("能力値を再計算",{236,306,180,29},recalc_parameter,Action::Auxiliary,21);
            checkbox("クーポンを再計算",{420,306,190,29},
                     common_bool(&CharaInfo::m_recalc_coupons),Action::Auxiliary,22);
            checkbox("デバッグクーポン",{22,340,190,29},
                     common_bool(&CharaInfo::m_debug_coupon),Action::Auxiliary,23);
            String type = "カスタム";
            bool first_type = true;
            for (const auto& info : selected) if (info) {
                const String current = info->m_type && *info->m_type ? (*info->m_type)->m_name : String("カスタム");
                if (first_type) { type = current; first_type = false; }
                else if (type != current) { type = "―――"; break; }
            }
            button("能力型: " + type, {236, 340, 374, 29}, Action::Auxiliary, 24,
                   recalc_parameter != 0);
        }

        paint_character_portrait(m_pane_req, surface, {594,58,74,94}, 1.0);
        // Python版と同様、必須情報パネルには両モードで独自の自動
        // 選択ボタンを設ける。 ダイアログ全体の自動設定とは分ける。
        button("自動選択", {200, 403, 140, 30}, Action::Auxiliary, 31);
    } else if (m_pane_sel) {
        label("特徴を選択してください。対になる特徴は同時に選択できません。", 22, 66, true);
        constexpr Integer columns = 3;
        constexpr Integer rows = 9;
        const Integer visible = columns * rows;
        const Integer maximum = (std::max<Integer>)(
            0, static_cast<Integer>(m_pane_sel->m_makings_typed.size()) - visible);
        m_presentation_trait_scroll = std::clamp<Integer>(m_presentation_trait_scroll, 0, maximum);
        const auto infos = m_pane_sel->_get_infos();
        for (Integer slot = 0; slot < visible; ++slot) {
            const Integer absolute = m_presentation_trait_scroll + slot;
            if (absolute < 0 || static_cast<std::size_t>(absolute) >= m_pane_sel->m_makings_typed.size()) break;
            const auto& making = m_pane_sel->m_makings_typed[static_cast<std::size_t>(absolute)];
            int state = 0;
            bool first = true;
            for (const auto& info : infos) if (info) {
                const int current = info->m_makings.count(making) ? 1 : 0;
                if (first) { state = current; first = false; }
                else if (state != current) { state = -1; break; }
            }
            const Integer col = slot % columns;
            const Integer row = slot / columns;
            const String text = static_cast<std::size_t>(absolute) < m_pane_sel->m_makinglabels_typed.size()
                ? m_pane_sel->m_makinglabels_typed[static_cast<std::size_t>(absolute)]
                : strip_coupon_prefix(making);
            checkbox(text,{22+col*222,94+row*34,210,29},state,
                     Action::Toggle,static_cast<int>(absolute));
        }
        button("自動選択", {200, 403, 140, 30}, Action::Auxiliary, 25);
        button("クリア", {350, 403, 140, 30}, Action::Auxiliary, 26);
    }

    button("自動設定", {8, 458, 130, 42}, Action::Auxiliary, 30);
    const bool ok_enabled = !m_pane_req || m_pane_req->m_ok_enabled_typed;
    button(m_create_typed ? "登録" : "決定", {392, 458, 150, 42},
           Action::Accept, 40, ok_enabled);
    button("中止", {550, 458, 162, 42}, Action::Cancel, 41);
    if(m_presentation_popup_kind&&!m_presentation_popup_values.empty()){
        const std::array<::cw::lib::Rect,8> anchors={::cw::lib::Rect{},::cw::lib::Rect{50,38,240,30},
            ::cw::lib::Rect{120,173,356,30},::cw::lib::Rect{120,244,180,30},::cw::lib::Rect{400,244,180,30},
            ::cw::lib::Rect{120,286,180,30},::cw::lib::Rect{400,286,180,30},::cw::lib::Rect{236,369,374,30}};
        const auto anchor=anchors[static_cast<std::size_t>(std::clamp<Integer>(m_presentation_popup_kind,1,7))];
        const Integer rows=std::min<Integer>(8,static_cast<Integer>(m_presentation_popup_values.size()));
        const Integer first=std::clamp<Integer>(m_presentation_popup_index-rows/2,0,
            std::max<Integer>(0,static_cast<Integer>(m_presentation_popup_values.size())-rows));
        const ::cw::lib::Rect popup{anchor.x,anchor.y,anchor.width,rows*26+4};
        ::cw::lib::ui::fill_rect(surface,popup,{255,255,255,255});::cw::lib::ui::draw_sunken_border(surface,popup);
        for(Integer row=0;row<rows;++row){const Integer choice=first+row;const ::cw::lib::Rect rect{popup.x+2,popup.y+2+row*26,popup.width-4,26};
            if(choice==m_presentation_popup_index)::cw::lib::ui::fill_rect(surface,rect,{210,225,248,255});
            ::cw::lib::ui::draw_label(surface,m_presentation_popup_values[static_cast<std::size_t>(choice)],rect.x+5,
                retained_ui::centered_y(rect,13),13,false,{0,0,0});
            out.controls.push_back({Action::Create,3000+static_cast<int>(choice),static_cast<float>(rect.x),static_cast<float>(rect.y),
                static_cast<float>(rect.width),static_cast<float>(rect.height),true,false,"",m_presentation_popup_values[static_cast<std::size_t>(choice)]});
        }
    }
    out.wants_text_input=m_presentation_tab==0&&m_presentation_focus==10&&m_presentation_popup_kind==0;
    out.input_text=m_presentation_name_draft;out.composition=m_presentation_composition;
    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::presentation_scale(game_window_scale),{245,245,245,255});
    const ::cw::lib::Rect header_region{0,0,width,48},body_region{0,48,width,402},footer_region{0,450,width,70};
    std::uint64_t header_signature=static_cast<std::uint64_t>(m_presentation_tab+1);
    retained_ui::mix(header_signature,static_cast<std::uint64_t>(m_selected_typed+1));
    retained_ui::mix(header_signature,static_cast<std::uint64_t>(m_create_typed));
    for(const auto& target:m_targets)retained_ui::mix(header_signature,target);
    std::uint64_t body_signature=header_signature;
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_presentation_focus+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_presentation_trait_scroll+1));
    retained_ui::mix(body_signature,m_presentation_name_draft);retained_ui::mix(body_signature,m_presentation_composition);
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_presentation_popup_kind));
    for(const auto& info:selected)if(info){retained_ui::mix(body_signature,info->m_name);
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_level+1));
        retained_ui::mix(body_signature,info->m_sex);retained_ui::mix(body_signature,info->m_age);retained_ui::mix(body_signature,info->m_talent);
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_recalc_maxlife));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_recalc_parameter));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_recalc_coupons));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_debug_coupon));
        if(info->m_race)retained_ui::mix(body_signature,info->m_race->m_name);
        if(info->m_type&&*info->m_type)retained_ui::mix(body_signature,(*info->m_type)->m_name);
        for(const auto& making:info->m_makings)retained_ui::mix(body_signature,making);
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(info->m_can_loaded_scaledimage));
        for(const auto& image:info->m_imgpaths)if(image){retained_ui::mix(body_signature,image->m_path);
            retained_ui::mix(body_signature,image->m_postype);retained_ui::mix(body_signature,static_cast<std::uint64_t>(image->m_pcnumber));}}
    if(m_pane_req){retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_pane_req->m_imgselection_typed+1));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_pane_req->m_imgcentering_typed));}
    const bool footer_ok_enabled=!m_pane_req||m_pane_req->m_ok_enabled_typed;
    std::uint64_t footer_signature=static_cast<std::uint64_t>(footer_ok_enabled);
    retained_ui::mix(footer_signature,static_cast<std::uint64_t>(m_create_typed));
    retained_ui::mix(footer_signature,static_cast<std::uint64_t>(m_presentation_popup_kind));
    const bool paint_header=canvas.component_changed("debug.character.header",header_region,header_signature);
    const bool paint_body=canvas.component_changed("debug.character.body",body_region,body_signature);
    const bool paint_footer=canvas.component_changed("debug.character.footer",footer_region,footer_signature);
    ::cw::lib::Rect popup_region{};bool paint_popup=false;
    if(m_presentation_popup_kind&&!m_presentation_popup_values.empty()){
        const std::array<::cw::lib::Rect,8> anchors={::cw::lib::Rect{},::cw::lib::Rect{50,38,240,30},
            ::cw::lib::Rect{120,173,356,30},::cw::lib::Rect{120,244,180,30},::cw::lib::Rect{400,244,180,30},
            ::cw::lib::Rect{120,286,180,30},::cw::lib::Rect{400,286,180,30},::cw::lib::Rect{236,369,374,30}};
        const auto anchor=anchors[static_cast<std::size_t>(std::clamp<Integer>(m_presentation_popup_kind,1,7))];
        const Integer rows=std::min<Integer>(8,static_cast<Integer>(m_presentation_popup_values.size()));
        popup_region={anchor.x,anchor.y,anchor.width,rows*26+4};
        std::uint64_t popup_signature=static_cast<std::uint64_t>(m_presentation_popup_index+1);
        for(const auto& value:m_presentation_popup_values)retained_ui::mix(popup_signature,value);
        paint_popup=canvas.component_changed("debug.character.popup",popup_region,popup_signature);
    }else (void)retained.remove_component("debug.character.popup");
    canvas.capture_base_and_restore();
    retained_ui::composite_changed(canvas,surface,header_region,paint_header);
    retained_ui::composite_changed(canvas,surface,body_region,paint_body);
    retained_ui::composite_changed(canvas,surface,footer_region,paint_footer);
    retained_ui::composite_changed(canvas,surface,popup_region,paint_popup);
    retained_ui::finish(out,canvas,game_window_scale);
    return out;
}

void CharacterEditDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_closed || m_presentation_operation_pending || m_document_token) return;
    struct PendingScope {
        bool& pending;
        explicit PendingScope(bool& value) : pending(value) { pending = true; }
        ~PendingScope() { pending = false; }
    } operation(m_presentation_operation_pending);
    m_presentation_dirty = true;
    const auto event = [](const Integer id = 0, const String& title = String{}) {
        auto value = std::make_shared<::cw::lib::Window>();
        value->id = id;
        value->title = title;
        return value;
    };
    const auto open_popup = [&](const Integer kind,std::vector<String> values,const Integer initial) {
        m_presentation_popup_kind=kind;m_presentation_popup_values=std::move(values);
        m_presentation_popup_index=m_presentation_popup_values.empty()?0:std::clamp<Integer>(initial,0,static_cast<Integer>(m_presentation_popup_values.size())-1);
    };
    if(action!=Action::Create){m_presentation_popup_kind=0;m_presentation_popup_values.clear();}
    if (action == Action::Accept) { m_presentation_focus=40;OnOkBtn({}); return; }
    if (action == Action::Cancel) { m_presentation_focus=41;OnCancel({}); return; }
    if (action == Action::Previous) { m_presentation_focus=100;OnLeftBtn({}); return; }
    if (action == Action::Next) { m_presentation_focus=101;OnRightBtn({}); return; }
    if (action == Action::Select) {
        m_presentation_focus=2;open_popup(1,m_targets,m_selected_typed);
        return;
    }
    if(action==Action::Create){
        const Integer choice=index-3000;if(choice<0||static_cast<std::size_t>(choice)>=m_presentation_popup_values.size())return;
        const auto kind=m_presentation_popup_kind;const auto value=m_presentation_popup_values[static_cast<std::size_t>(choice)];
        m_presentation_popup_kind=0;m_presentation_popup_values.clear();
        if(kind==1)OnSelectTarget(event(choice));
        else if(kind==2&&m_pane_req){if(value=="外部ファイルを選択...")(void)request_image_document();else m_pane_req->OnSelectImage(event(choice));}
        else if(kind==3&&m_pane_req)m_pane_req->OnRace(event(choice,value));
        else if(kind==4&&m_pane_req)m_pane_req->OnSelectSex(event(choice));
        else if(kind==5&&m_pane_req)m_pane_req->OnSelectAge(event(choice));
        else if(kind==6&&m_pane_req)m_pane_req->OnSelectTalent(event(choice));
        else if(kind==7)OnStandardType(event(choice,value));
        return;
    }
    if (action == Action::Toggle && m_pane_sel) {
        if (index < 0 || static_cast<std::size_t>(index) >= m_pane_sel->m_makings_typed.size()) return;
        const auto infos = m_pane_sel->_get_infos();
        const auto& making = m_pane_sel->m_makings_typed[static_cast<std::size_t>(index)];
        const bool all_checked = !infos.empty() && std::all_of(
            infos.begin(), infos.end(), [&making](const auto& info) {
                return info && info->m_makings.count(making) != 0;
            });
        auto value = event(index, all_checked ? "0" : "1");
        value->visible = !all_checked;
        m_pane_sel->OnCheck(value);
        m_pane_sel->select_target(m_selected_typed);
        m_presentation_focus=1000+index;
        return;
    }
    if (action != Action::Auxiliary) return;
    m_presentation_focus=index;
    if(m_create_typed&&m_pane_req&&index>=400&&index<700){
        if(index<500)m_pane_req->OnSelectSex(event(index-400));
        else if(index<600)m_pane_req->OnSelectAge(event(index-500));
        else m_pane_req->OnSelectTalent(event(index-600));
        return;
    }
    switch (index) {
    case 0: m_presentation_tab = 0;m_presentation_focus=0;m_presentation_popup_kind=0; break;
    case 1: m_presentation_tab = 1;m_presentation_focus=1;m_presentation_popup_kind=0; break;
    case 10:
        m_presentation_focus=10;m_presentation_popup_kind=0;m_presentation_name_select_all=true;
        break;
    case 11:
        if (m_pane_req) m_pane_req->OnAutoName({});
        select_target();
        break;
    case 12: if (m_pane_req) m_pane_req->OnLevelBtn({}); break;
    case 19: (void)request_image_document(); break;
    case 31:
        if(m_pane_req)m_pane_req->OnAutoBtn({});
        select_target();
        break;
    case 13:
        if (m_pane_req) {
            std::vector<String> values{no_change_text()};
            values.insert(values.end(), m_pane_req->m_imgchoice_labels_typed.begin(),
                          m_pane_req->m_imgchoice_labels_typed.end());
            values.push_back("外部ファイルを選択...");
            m_presentation_focus=13;open_popup(2,std::move(values),m_pane_req->m_imgselection_typed);
        }
        break;
    case 14:
        if (m_pane_req) m_pane_req->OnSelectImage(event(
            m_pane_req->m_imgselection_typed,
            m_pane_req->m_imgcentering_typed ? "Default" : "Center"));
        break;
    case 15:
        if (m_pane_req) {
            std::vector<String> values;
            for (const auto& race : ::cw::setting::Setting::s_races)
                if (race) values.push_back(race->m_name);
            String current;bool first=true,mixed=false;for(const auto& info:_get_infos())if(info){
                const String value=info->m_race?info->m_race->m_name:String{};
                if(first){current=value;first=false;}else if(current!=value)mixed=true;}
            if(mixed){const auto unknown=default_race();current=unknown?unknown->m_name:String{};}
            const auto found=std::find(values.begin(),values.end(),current);
            const Integer initial=found==values.end()?0:static_cast<Integer>(std::distance(values.begin(),found));
            m_presentation_focus=15;open_popup(3,std::move(values),initial);
        }
        break;
    case 16:
        if (m_pane_req) {
            std::vector<String> values;
            for (const auto& value : ::cw::setting::Setting::s_sexcoupons)
                values.push_back(strip_coupon_prefix(value));
            String current;bool first=true,mixed=false;for(const auto& info:_get_infos())if(info){const String value=strip_coupon_prefix(info->m_sex);
                if(first){current=value;first=false;}else if(current!=value)mixed=true;}
            const auto found=mixed?values.begin():std::find(values.begin(),values.end(),current);
            const Integer initial=found==values.end()?0:static_cast<Integer>(std::distance(values.begin(),found));
            m_presentation_focus=16;open_popup(4,std::move(values),initial);
        }
        break;
    case 17:
        if (m_pane_req) {
            std::vector<String> values;
            for (const auto& value : ::cw::setting::Setting::s_periodcoupons)
                values.push_back(strip_coupon_prefix(value));
            String current;bool first=true,mixed=false;for(const auto& info:_get_infos())if(info){const String value=strip_coupon_prefix(info->m_age);
                if(first){current=value;first=false;}else if(current!=value)mixed=true;}
            const auto found=mixed?values.begin():std::find(values.begin(),values.end(),current);
            const Integer initial=found==values.end()?0:static_cast<Integer>(std::distance(values.begin(),found));
            m_presentation_focus=17;open_popup(5,std::move(values),initial);
        }
        break;
    case 18:
        if (m_pane_req) {
            std::vector<String> values;
            for (const auto& nature : ::cw::setting::Setting::s_natures)
                if (nature && (!nature->m_special || m_pane_req->m_show_specialtalent_typed))
                    values.push_back(nature->m_name);
            String current;bool first=true,mixed=false;for(const auto& info:_get_infos())if(info){const String value=strip_coupon_prefix(info->m_talent);
                if(first){current=value;first=false;}else if(current!=value)mixed=true;}
            const auto found=mixed?values.begin():std::find(values.begin(),values.end(),current);
            const Integer initial=found==values.end()?0:static_cast<Integer>(std::distance(values.begin(),found));
            m_presentation_focus=18;open_popup(6,std::move(values),initial);
        }
        break;
    case 20: {
        const auto infos = _get_infos();
        const bool all_checked = !infos.empty() && std::all_of(
            infos.begin(), infos.end(), [](const auto& info) {
                return info && info->m_recalc_maxlife;
            });
        OnReCalcMaxLife(event(all_checked ? 0 : 1));
        break;
    }
    case 21: {
        const auto infos = _get_infos();
        const bool all_checked = !infos.empty() && std::all_of(
            infos.begin(), infos.end(), [](const auto& info) {
                return info && info->m_recalc_parameter;
            });
        OnReCalcParameter(event(all_checked ? 0 : 1));
        break;
    }
    case 22: {
        const auto infos = _get_infos();
        const bool all_checked = !infos.empty() && std::all_of(
            infos.begin(), infos.end(), [](const auto& info) {
                return info && info->m_recalc_coupons;
            });
        OnReCalcCoupons(event(all_checked ? 0 : 1));
        break;
    }
    case 23: {
        const auto infos = _get_infos();
        const bool all_checked = !infos.empty() && std::all_of(
            infos.begin(), infos.end(), [](const auto& info) {
                return info && info->m_debug_coupon;
            });
        OnDebugCoupon(event(all_checked ? 0 : 1));
        break;
    }
    case 24: {std::vector<String> values{"カスタム"};for(const auto& sample: ::cw::setting::Setting::s_sampletypes)if(sample)values.push_back(sample->m_name);
        String current="カスタム";bool first=true,mixed=false;for(const auto& info:_get_infos())if(info){const String value=info->m_type&&*info->m_type?(*info->m_type)->m_name:String("カスタム");
            if(first){current=value;first=false;}else if(current!=value)mixed=true;}
        const auto found=mixed?values.begin():std::find(values.begin(),values.end(),current);
        const Integer initial=found==values.end()?0:static_cast<Integer>(std::distance(values.begin(),found));
        m_presentation_focus=24;open_popup(7,std::move(values),initial);break;}
    case 25: if (m_pane_sel) m_pane_sel->OnAutoBtn({}); break;
    case 26: if (m_pane_sel) m_pane_sel->OnClearBtn({}); break;
    case 30: OnAutoBtn({}); select_target(); break;
    default: break;
    }
}

void CharacterEditDialog::dialog_presentation_text(const std::string& text,const bool composition)
{
    if(m_modal_closed||m_presentation_operation_pending||m_presentation_tab!=0||m_presentation_focus!=10||m_presentation_popup_kind)return;
    if(composition){m_presentation_composition=text;m_presentation_dirty=true;return;}
    m_presentation_composition.clear();if(m_presentation_name_select_all)m_presentation_name_draft.clear();m_presentation_name_select_all=false;
    if(text=="\b"){
        if(!m_presentation_name_draft.empty()){auto pos=m_presentation_name_draft.size()-1;
            while(pos>0&&(static_cast<unsigned char>(m_presentation_name_draft[pos])&0xc0)==0x80)--pos;m_presentation_name_draft.erase(pos);}
    }else{
        std::size_t characters=0;for(const unsigned char ch:m_presentation_name_draft)if((ch&0xc0)!=0x80)++characters;
        std::size_t added=0;for(const unsigned char ch:text)if((ch&0xc0)!=0x80)++added;if(characters+added>14)return;
        m_presentation_name_draft+=text;
    }
    if(m_pane_req){auto e=std::make_shared<::cw::lib::Window>();e->title=m_presentation_name_draft;m_pane_req->OnName(e);}
    m_presentation_dirty=true;
}

void CharacterEditDialog::dialog_presentation_drop_files(const std::vector<std::string>& paths)
{
    if(m_modal_closed||m_presentation_operation_pending||!m_pane_req||paths.empty())return;
    std::vector<String> files;
    for(const auto& path:paths) if(!path.empty() && path.find('\0')==String::npos) files.push_back(path);
    if(files.empty()) return;
    if(ui_adapter()->dropped_files) ui_adapter()->dropped_files("chara_requirement.image",files);
    m_pane_req->_put_image(std::move(files));
    m_presentation_focus=13;m_presentation_popup_kind=0;m_presentation_dirty=true;
}

void CharacterEditDialog::dialog_presentation_key(const ::cw::dialog::DialogPresentationKey key,const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;using Action=::cw::dialog::DialogPresentationAction;
    if(!pressed||m_modal_closed||m_presentation_operation_pending)return;
    const bool shift=(modifiers&::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool control=(modifiers&::cw::dialog::DialogPresentationModifierControl)!=0;
    if(key==Key::Escape){if(m_presentation_popup_kind){m_presentation_popup_kind=0;m_presentation_popup_values.clear();m_presentation_dirty=true;return;}
        dialog_presentation_action(Action::Cancel,41);return;}
    if(m_presentation_popup_kind){
        if(m_presentation_popup_values.empty()){m_presentation_popup_kind=0;return;}
        if(key==Key::Up)m_presentation_popup_index--;else if(key==Key::Down)m_presentation_popup_index++;
        else if(key==Key::Home)m_presentation_popup_index=0;else if(key==Key::End)m_presentation_popup_index=static_cast<Integer>(m_presentation_popup_values.size())-1;
        else if(key==Key::PageUp)m_presentation_popup_index-=8;else if(key==Key::PageDown)m_presentation_popup_index+=8;
        else if(key==Key::Enter||key==Key::Space){dialog_presentation_action(Action::Create,3000+m_presentation_popup_index);return;}
        else if(key==Key::Tab){m_presentation_popup_kind=0;m_presentation_popup_values.clear();}else return;
        if(!m_presentation_popup_values.empty())m_presentation_popup_index=std::clamp<Integer>(m_presentation_popup_index,0,static_cast<Integer>(m_presentation_popup_values.size())-1);
        m_presentation_dirty=true;if(key!=Key::Tab)return;
    }
    if(control&&key==Key::A&&m_presentation_focus==10){m_presentation_name_select_all=true;m_presentation_dirty=true;return;}
    if((key==Key::Backspace||key==Key::Delete)&&m_presentation_focus==10){dialog_presentation_text("\b",false);return;}
    if(key==Key::Tab){
        std::vector<Integer> order;if(!m_create_typed)order={100,2,101};order.push_back(0);order.push_back(1);
        if(m_create_typed){
            // 簡易フォームの実際の構成からフォーカス順を作る。 スキン
            // 定義のラジオボタンを含め、非表示の編集専用部品は除く。
            order.clear();
            for(const auto& control:dialog_presentation().controls){
                if(!control.enabled)continue;
                order.push_back(control.action==Action::Toggle?1000+control.index:control.index);
            }
        }
        else if(m_presentation_tab==0){for(Integer id=10;id<=18;++id)order.push_back(id);for(Integer id=20;id<=24;++id)order.push_back(id);}
        else if(m_pane_sel){const Integer count=std::min<Integer>(27,static_cast<Integer>(m_pane_sel->m_makings_typed.size()));for(Integer i=0;i<count;++i)order.push_back(1000+i);order.push_back(25);order.push_back(26);}
        if(!m_create_typed){order.push_back(30);order.push_back(40);order.push_back(41);}auto it=std::find(order.begin(),order.end(),m_presentation_focus);
        Integer pos=it==order.end()?0:static_cast<Integer>(std::distance(order.begin(),it));pos=(pos+(shift?static_cast<Integer>(order.size())-1:1))%static_cast<Integer>(order.size());
        m_presentation_focus=order[static_cast<std::size_t>(pos)];m_presentation_name_select_all=m_presentation_focus==10;m_presentation_dirty=true;return;
    }
    if(key==Key::Enter||key==Key::Space)dialog_presentation_enter(modifiers);
}

void CharacterEditDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_presentation_focus==100)dialog_presentation_action(Action::Previous,0);
    else if(m_presentation_focus==101)dialog_presentation_action(Action::Next,0);
    else if(m_presentation_focus==2)dialog_presentation_action(Action::Select,0);
    else if(m_presentation_focus==40)dialog_presentation_action(Action::Accept,40);
    else if(m_presentation_focus==41)dialog_presentation_action(Action::Cancel,41);
    else if(m_presentation_focus>=1000)dialog_presentation_action(Action::Toggle,m_presentation_focus-1000);
    else if(m_presentation_focus!=10)dialog_presentation_action(Action::Auxiliary,m_presentation_focus);
}

void CharacterEditDialog::dialog_presentation_scroll(const float, const float y, const int lines)
{
    if (m_modal_closed || m_presentation_operation_pending || lines == 0) return;
    if(m_presentation_popup_kind&&!m_presentation_popup_values.empty()){
        m_presentation_popup_index=std::clamp<Integer>(m_presentation_popup_index+lines,0,
            static_cast<Integer>(m_presentation_popup_values.size())-1);m_presentation_dirty=true;return;
    }
    // CharacterEditはエンジンのカード用wins()ではなく、デバッガー
    // のppis()表示を使う。 ポインター座標は finalize_logical_presentation()
    // 後の、最終的なデバッガーDPI画素となる。
    const auto dpi = (std::max<::cw::lib::Real>)(::cw::g_dpi_level, 1.0);
    const auto top = static_cast<float>(std::lround(48.0 * dpi));
    const auto bottom = static_cast<float>(std::lround((m_create_typed?320.0:443.0) * dpi));
    if (m_presentation_tab == 1 && y >= top && y < bottom) {
        m_presentation_trait_scroll = (std::max<Integer>)(0, m_presentation_trait_scroll + lines);
        m_presentation_dirty = true;
    }
}

bool CharacterEditDialog::dialog_presentation_finished() const noexcept
{
    return m_modal_closed;
}

bool CharacterEditDialog::dialog_presentation_needs_refresh() const noexcept
{
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        m_presentation_dirty.load(std::memory_order_acquire);
}

void CharacterEditDialog::dialog_presentation_refreshed() noexcept
{
    ::cw::dialog::DialogPresentationProvider::dialog_presentation_refreshed();
    m_presentation_dirty.store(false, std::memory_order_release);
}

void CharacterEditDialog::dialog_presentation_complete()
{
    if (!m_modal_closed) OnCancel({});
}

void CharacterEditDialog_OnOkBtn_func(std::vector<::cw::lib::Integer> arg_updates, std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_pcards)
{
    auto update = [updates = std::move(arg_updates), pcards = std::move(arg_pcards)](
                      const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
        refresh_edited_cards(updates, pcards);
        return {};
    };
    if (::cw::g_cwpy) ::cw::g_cwpy->exec_func(std::move(update));
    else update({}, {});
}

void CharaInfo_put_params_put_coupons(::cw::lib::Value arg_period)
{
    // Pythonの内部クロージャーによるracecoupons集合の更新は、CharaInfo::put_params
    // の同じループで行う。 この公開の連携口は旧呼び出し元の型を
    // 検証し、不正な年代・種族オブジェクトを黙って無視しない。
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_period.storage());
    if (!object || !*object) throw std::invalid_argument("coupon period must be an object");
    if (!std::dynamic_pointer_cast<::cw::features::Period>(*object) &&
        !std::dynamic_pointer_cast<::cw::header::RaceHeader>(*object))
        throw std::invalid_argument("coupon period must be Period or RaceHeader");
}

std::tuple<::cw::lib::String, ::cw::lib::String> CharaInfo_put_params_create_parentmatcher(::cw::lib::String arg_s)
{
    return parent_matcher(std::move(arg_s));
}

void main()
{
    // モジュールを単体で起動してもwxの主ループは開始せず、UI側からinitializeを呼ぶ。
}

}

