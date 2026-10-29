#include "cw/binary/adventurer.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/beast.hpp"
#include "cw/binary/coupon.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/skill.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/yadodb.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cw::binary::adventurer {
namespace {

using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

ElementPtr element(String tag, String text = {},
                   std::map<String, String> attributes = {}) {
    return ::cw::data::make_element(std::move(tag), std::move(text),
                                    std::move(attributes));
}

ElementPtr find_path(const ElementPtr& root, const String& path) {
    if (!root) return nullptr;
    const auto found = root->find(path);
    return found ? *found : nullptr;
}

Integer integer(const String& value) {
    return ::cw::lib::require_python_integer(value);
}

// Python版は CWFileWriter が固定幅へ詰めるまで任意精度の整数を
// 保持する。 範囲外の番兵値で、その失敗時点と途中までの出力を維持する。 番兵値は
// CardWirthの符号付き32ビットに収まらないため、直列化されることはない。
Integer export_integer(const String& value) {
    return ::cw::lib::require_python_integer_for_cardwirth_pack(value);
}

Integer export_integer_float(const String& value) {
    const auto parsed = ::cw::data::parse_python_float(value);
    if (std::isnan(parsed)) throw std::invalid_argument("cannot convert float NaN to integer");
    if (!std::isfinite(parsed)) throw std::out_of_range("cannot convert float infinity to integer");
    const auto limit = std::ldexp(1.0, std::numeric_limits<Integer>::digits);
    if (parsed < -limit) return std::numeric_limits<Integer>::min();
    if (parsed >= limit) return std::numeric_limits<Integer>::max();
    return static_cast<Integer>(parsed);
}

bool boolean(const String& value, std::optional<bool> fallback = std::nullopt) {
    String normalized = value;
    for (char& ch : normalized)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (normalized == "true" || normalized == "1") return true;
    if (normalized == "false" || normalized == "0") return false;
    if (fallback) return *fallback;
    throw std::invalid_argument("invalid Adventurer boolean: " + value);
}

String boolean_text(bool value) { return value ? "True" : "False"; }

bool starts_with(const String& value, const String& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

String remove_first(const String& value, const String& marker) {
    // Pythonの str.replace(marker, "", 1) と同様、位置によらず最初の出現を除く。
    const auto position = value.find(marker);
    if (position == String::npos) return value;
    String result = value;
    result.erase(position, marker.size());
    return result;
}

std::shared_ptr<::cw::binary::base::CWBinaryBase> owned_self(
    ::cw::binary::base::CWBinaryBase& value) {
    try { return value.shared_from_this(); }
    catch (const std::bad_weak_ptr&) { return nullptr; }
}

std::optional<::cw::lib::Value> parent_value(
    const std::shared_ptr<::cw::binary::base::CWBinaryBase>& parent) {
    if (!parent) return std::nullopt;
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(parent));
}

void recover_for_f9(const ElementPtr& data) {
    if (!data) throw std::invalid_argument("F9 recovery requires XML data");
    // Python版と同様、同じXMLツリーからCharacter
    // を作り、共通の回復APIを呼ぶ。 スキル上限、称号・状態の効
    // 果、召喚獣の除去は、ここへ複製せずその一つの実装に任せる。
    auto character = std::make_shared<::cw::character::Character>();
    character->initialize(std::make_shared<::cw::data::CWPyElementTree>(data));
    character->set_fullrecovery(false);
}
std::vector<String> split_lines(const String& value) {
    // Pythonの str.splitlines と同じ規則を共有し、Unicode
    // の改行と、末尾区切りの後に空行を増やさない動作を含める。
    return ::cw::lib::encoding::splitlines(value);
}

String encode_text_list(const std::vector<String>& values) {
    String joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) joined += "\\n";
        for (char ch : values[index]) {
            if (ch == '\\') joined += "\\\\";
            else if (ch == '\n') joined += "\\n";
            else if (ch != '\r') joined.push_back(ch);
        }
    }
    return joined;
}

String number_one_coupon() {
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get("number_1_coupon", "＿１");
    return "＿１";
}

template <typename Writer>
void write_card_sequence(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
                         const ElementPtr& cards, const String& owner_name,
                         Writer&& writer) {
    const auto length_position = file->tell();
    file->write_dword(0);
    Integer written = 0;
    if (cards) {
        for (const auto& card : cards->m_children) {
            const auto position = file->tell();
            try {
                writer(card);
                ++written;
            } catch (const ::cw::binary::cwfile::UnsupportedError& error) {
                file->seek(position);
                const auto card_name = find_path(card, "Property/Name");
                file->log_error(owner_name + " の所持する " +
                    (card_name ? card_name->m_text : String{}) +
                    " は対象エンジンで使用できない機能(" + error.m_funcname +
                    ")を使用しているため、変換しません。\n");
            } catch (const std::exception&) {
                // Python版と同様、このカードを戻す前に現在
                // の失敗を報告する。 変換ログだけでなく共通のネ
                // イティブ例外の診断も残し、他のカードは続ける。
                ::cw::util::print_ex(std::nullopt);
                file->seek(position);
                const auto card_name = find_path(card, "Property/Name");
                file->log_error(owner_name + " の所持する " +
                    (card_name ? card_name->m_text : String{}) +
                    " は変換できませんでした。\n");
            }
        }
    }
    const auto end = file->tell();
    file->seek(length_position);
    file->write_dword(written);
    file->seek(end);
}

}  // namespace

::cw::lib::String Adventurer::export_image() {
    if (!m_image || m_image->empty()) return {};
    if (!get_image_export()) return ::cw::binary::image::data_to_code(*m_image);
    return CWBinaryBase::export_image();
}

Adventurer::Adventurer(std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
                       std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                       bool yadodata, bool nameonly, bool album120) {
    initialize(std::move(parent), std::move(file), yadodata, nameonly, album120);
}

void Adventurer::add_128coupons() {
    auto self = owned_self(*this);
    auto ep = std::make_shared<::cw::binary::coupon::Coupon>();
    ep->initialize(self, nullptr);
    ep->m_name = "＠ＥＰ";
    ep->m_value = std::max<Integer>(0, m_level - 1) * 10;
    m_coupons.insert(m_coupons.begin(), ep);

    auto level_origin = std::make_shared<::cw::binary::coupon::Coupon>();
    level_origin->initialize(self, nullptr);
    level_origin->m_name = "＠レベル原点";
    level_origin->m_value = m_level;
    m_coupons.insert(m_coupons.begin(), level_origin);
}

void Adventurer::initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                            std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                            bool arg_yadodata, bool arg_nameonly, bool arg_album120) {
    if (!arg_f) throw std::invalid_argument("Adventurer requires a binary input");
    CWBinaryBase::initialize(arg_parent, arg_f, arg_yadodata);
    m_xmltype = "Adventurer";
    m_items.clear(); m_skills.clear(); m_beasts.clear(); m_coupons.clear();
    m_data.reset(); m_f9data.reset(); m_is_dead = false;
    m_nameonly = false;

    if (arg_album120) {
        m_id = 0;
        (void)arg_f->dword();
        (void)arg_f->dword();
        m_name = arg_f->string();
        m_imgpath.clear();
        m_level = arg_f->dword();
        m_money = arg_f->dword();
        m_avoid = arg_f->dword(); m_resist = arg_f->dword(); m_defense = arg_f->dword();
        m_dex = arg_f->dword(); m_agl = arg_f->dword(); m_int = arg_f->dword();
        m_str = arg_f->dword(); m_vit = arg_f->dword(); m_min = arg_f->dword();
        m_aggressive = arg_f->dword(); m_cheerful = arg_f->dword();
        m_brave = arg_f->dword(); m_cautious = arg_f->dword(); m_trickish = arg_f->dword();
        m_noeffect_weapon = arg_f->boolean(); m_noeffect_magic = arg_f->boolean();
        m_undead = arg_f->boolean(); m_automaton = arg_f->boolean();
        m_unholy = arg_f->boolean(); m_constructure = arg_f->boolean();
        m_resist_fire = arg_f->boolean(); m_resist_ice = arg_f->boolean();
        m_weakness_fire = arg_f->boolean(); m_weakness_ice = arg_f->boolean();
        for (int index = 0; index < 11; ++index) (void)arg_f->dword();
        m_image = arg_f->image();
        m_description = remove_first(arg_f->string(true), "TEXT\\n");
        const auto vitality = std::max<Integer>(1, m_vit);
        const auto mind = std::max<Integer>(1, m_min);
        const auto level = std::max<Integer>(1, m_level);
        m_life = static_cast<Integer>((static_cast<double>(vitality) / 2.0 + 4.0) *
                                      (level + 1) + static_cast<double>(mind) / 2.0);
        m_maxlife = m_life;
        const auto coupon_count = arg_f->dword();
        auto self = owned_self(*this);
        for (Integer index = 0; index < coupon_count; ++index) {
            auto value = std::make_shared<::cw::binary::coupon::Coupon>();
            value->initialize(self, arg_f, arg_yadodata, 4);
            if (value->m_name == "＿死亡") m_is_dead = true;
            m_coupons.push_back(std::move(value));
        }
        add_128coupons();
        m_mentality = m_duration_mentality = m_paralyze = m_poison = 0;
        m_duration_bind = m_duration_silence = m_duration_faceup = m_duration_antimagic = 0;
        m_enhance_action = m_duration_enhance_action = 0;
        m_enhance_avoid = m_duration_enhance_avoid = 0;
        m_enhance_resist = m_duration_enhance_resist = 0;
        m_enhance_defense = m_duration_enhance_defense = 0;
        return;
    }

    m_image.reset();
    m_name = arg_f->string();
    m_nameonly = arg_nameonly;
    if (arg_nameonly) return;

    const auto encoded_id = arg_f->dword();
    Integer data_version = 0;
    if (encoded_id <= 19999) { data_version = 0; m_id = encoded_id; }
    else if (encoded_id <= 39999) { data_version = 2; m_id = encoded_id - 20000; }
    else if (encoded_id <= 49999) { data_version = 4; m_id = encoded_id - 40000; }
    else { data_version = 5; m_id = encoded_id - 50000; }
    m_imgpath.clear();

    m_noeffect_weapon = arg_f->boolean(); m_noeffect_magic = arg_f->boolean();
    m_undead = arg_f->boolean(); m_automaton = arg_f->boolean();
    m_unholy = arg_f->boolean(); m_constructure = arg_f->boolean();
    m_resist_fire = arg_f->boolean(); m_resist_ice = arg_f->boolean();
    m_weakness_fire = arg_f->boolean(); m_weakness_ice = arg_f->boolean();
    m_level = arg_f->dword();
    m_money = data_version <= 4 ? arg_f->dword() : 0;
    m_description = remove_first(arg_f->string(true), "TEXT\\n");
    m_life = arg_f->dword(); m_maxlife = arg_f->dword();
    m_paralyze = arg_f->dword(); m_poison = arg_f->dword();
    m_avoid = arg_f->dword(); m_resist = arg_f->dword(); m_defense = arg_f->dword();
    m_dex = arg_f->dword(); m_agl = arg_f->dword(); m_int = arg_f->dword();
    m_str = arg_f->dword(); m_vit = arg_f->dword(); m_min = arg_f->dword();
    m_aggressive = arg_f->dword(); m_cheerful = arg_f->dword();
    m_brave = arg_f->dword(); m_cautious = arg_f->dword(); m_trickish = arg_f->dword();
    m_mentality = arg_f->byte(); m_duration_mentality = arg_f->dword();
    m_duration_bind = arg_f->dword(); m_duration_silence = arg_f->dword();
    m_duration_faceup = arg_f->dword(); m_duration_antimagic = arg_f->dword();
    m_enhance_action = arg_f->dword(); m_duration_enhance_action = arg_f->dword();
    m_enhance_avoid = arg_f->dword(); m_duration_enhance_avoid = arg_f->dword();
    m_enhance_resist = arg_f->dword(); m_duration_enhance_resist = arg_f->dword();
    m_enhance_defense = arg_f->dword(); m_duration_enhance_defense = arg_f->dword();

    const auto self = owned_self(*this);
    auto read_count = [&arg_f](const char*) { return arg_f->dword(); };
    for (Integer index = 0, count = read_count("item"); index < count; ++index) {
        auto card = std::make_shared<::cw::binary::item::ItemCard>();
        card->initialize(parent_value(self), arg_f, arg_yadodata, false, "Material", true);
        m_items.push_back(std::move(card));
    }
    for (Integer index = 0, count = read_count("skill"); index < count; ++index) {
        auto card = std::make_shared<::cw::binary::skill::SkillCard>();
        card->initialize(parent_value(self), arg_f, arg_yadodata, false, "Material", true);
        m_skills.push_back(std::move(card));
    }
    for (Integer index = 0, count = read_count("beast"); index < count; ++index) {
        auto card = std::make_shared<::cw::binary::beast::BeastCard>();
        card->initialize(parent_value(self), arg_f, arg_yadodata, false, "Material", true, false);
        m_beasts.push_back(std::move(card));
    }
    for (Integer index = 0, count = read_count("coupon"); index < count; ++index) {
        auto value = std::make_shared<::cw::binary::coupon::Coupon>();
        value->initialize(self, arg_f, arg_yadodata, data_version);
        m_coupons.push_back(std::move(value));
    }
    if (data_version <= 4) add_128coupons();
}

std::shared_ptr<Element> Adventurer::get_data() { return _get_data(false); }
std::shared_ptr<Element> Adventurer::get_f9data() { return _get_data(true); }

std::shared_ptr<Element> Adventurer::_get_data(bool arg_f9data) {
    if (m_nameonly) throw std::logic_error("name-only Adventurer has no XML data");
    auto& cache = arg_f9data ? m_f9data : m_data;
    if (cache) return cache;

    if (m_imgpath.empty() && m_image) m_imgpath = export_image();
    auto data = element("Adventurer");
    auto property = element("Property");
    property->append(element("Id", std::to_string(m_id)));
    property->append(element("Name", m_name));
    property->append(element("ImagePath", m_imgpath));
    property->append(element("Description", m_description));
    property->append(element("Level", std::to_string(m_level)));
    auto life = element("Life", std::to_string(m_life));
    life->set("max", std::to_string(m_maxlife));
    property->append(life);

    auto feature = element("Feature");
    auto type = element("Type");
    type->set("undead", boolean_text(m_undead)); type->set("automaton", boolean_text(m_automaton));
    type->set("unholy", boolean_text(m_unholy)); type->set("constructure", boolean_text(m_constructure));
    feature->append(type);
    auto no_effect = element("NoEffect");
    no_effect->set("weapon", boolean_text(m_noeffect_weapon));
    no_effect->set("magic", boolean_text(m_noeffect_magic));
    feature->append(no_effect);
    auto resist_element = element("Resist");
    resist_element->set("fire", boolean_text(m_resist_fire));
    resist_element->set("ice", boolean_text(m_resist_ice));
    feature->append(resist_element);
    auto weakness = element("Weakness");
    weakness->set("fire", boolean_text(m_weakness_fire));
    weakness->set("ice", boolean_text(m_weakness_ice));
    feature->append(weakness);
    property->append(feature);

    auto ability = element("Ability");
    auto physical = element("Physical");
    physical->set("dex", std::to_string(m_dex)); physical->set("agl", std::to_string(m_agl));
    physical->set("int", std::to_string(m_int)); physical->set("str", std::to_string(m_str));
    physical->set("vit", std::to_string(m_vit)); physical->set("min", std::to_string(m_min));
    ability->append(physical);
    auto mental = element("Mental");
    mental->set("aggressive", std::to_string(m_aggressive)); mental->set("cheerful", std::to_string(m_cheerful));
    mental->set("brave", std::to_string(m_brave)); mental->set("cautious", std::to_string(m_cautious));
    mental->set("trickish", std::to_string(m_trickish));
    ability->append(mental);
    auto base_enhance = element("Enhance");
    base_enhance->set("avoid", std::to_string(m_avoid));
    base_enhance->set("resist", std::to_string(m_resist));
    base_enhance->set("defense", std::to_string(m_defense));
    ability->append(base_enhance);
    property->append(ability);

    auto status = element("Status");
    auto mentality = element("Mentality", conv_mentality(m_mentality));
    mentality->set("duration", std::to_string(m_duration_mentality)); status->append(mentality);
    status->append(element("Paralyze", std::to_string(m_paralyze)));
    status->append(element("Poison", std::to_string(m_poison)));
    for (const auto& [name, duration] : std::vector<std::pair<String, Integer>>{
        {"Bind", m_duration_bind}, {"Silence", m_duration_silence},
        {"FaceUp", m_duration_faceup}, {"AntiMagic", m_duration_antimagic}}) {
        auto value = element(name); value->set("duration", std::to_string(duration)); status->append(value);
    }
    property->append(status);

    auto enhance = element("Enhance");
    for (const auto& value : std::vector<std::tuple<String, Integer, Integer>>{
        {"Action", m_enhance_action, m_duration_enhance_action},
        {"Avoid", m_enhance_avoid, m_duration_enhance_avoid},
        {"Resist", m_enhance_resist, m_duration_enhance_resist},
        {"Defense", m_enhance_defense, m_duration_enhance_defense}}) {
        auto child = element(std::get<0>(value), std::to_string(std::get<1>(value)));
        child->set("duration", std::to_string(std::get<2>(value))); enhance->append(child);
    }
    property->append(enhance);

    auto coupons = element("Coupons");
    std::vector<std::shared_ptr<::cw::binary::coupon::Coupon>> selected = m_coupons;
    if (arg_f9data) {
        std::set<String> number_coupons{"＿１", "＿２", "＿３", "＿４", "＿５", "＿６"};
        auto cut = std::find_if(selected.begin(), selected.end(), [&number_coupons](const auto& value) {
            return value && number_coupons.count(value->m_name) != 0;
        });
        if (cut == selected.end()) {
            const auto localized_number_one = number_one_coupon();
            cut = std::find_if(selected.begin(), selected.end(), [&](const auto& value) {
                return value && value->m_name == localized_number_one;
            });
        }
        selected.erase(cut, selected.end());
    }
    for (const auto& value : selected) {
        if (!value) continue;
        if (!arg_f9data && starts_with(value->m_name, "＾")) continue;
        auto coupon_data = value->get_data();
        if (arg_f9data && starts_with(value->m_name, "＾")) {
            coupon_data->m_text = remove_first(value->m_name, "＾");
        }
        coupons->append(coupon_data);
    }
    property->append(coupons);
    data->append(property);

    auto items = element("ItemCards");
    for (const auto& card : m_items) {
        if (!card || (arg_f9data && card->m_premium > 2)) continue;
        card->set_image_export(false, arg_f9data);
        if (auto card_data = card->get_data()) items->append(card_data);
    }
    data->append(items);
    auto skills = element("SkillCards");
    for (const auto& card : m_skills) {
        if (!card || (arg_f9data && card->m_premium > 2)) continue;
        card->set_image_export(false, arg_f9data);
        if (auto card_data = card->get_data()) skills->append(card_data);
    }
    data->append(skills);
    auto beasts = element("BeastCards");
    for (const auto& card : m_beasts) {
        if (!card || (arg_f9data && card->m_premium > 2) || (arg_f9data && card->m_attachment)) continue;
        card->set_image_export(false, arg_f9data);
        if (auto card_data = card->get_data()) beasts->append(card_data);
    }
    data->append(beasts);

    if (arg_f9data) recover_for_f9(data);
    cache = data;
    return data;
}

::cw::lib::String Adventurer::create_xml(::cw::lib::String arg_dpath) {
    const auto path = CWBinaryBase::create_xml(std::move(arg_dpath));
    auto root = get_root();
    if (root && root->m_yadodb && *root->m_yadodb) {
        (*root->m_yadodb)->insert_adventurer(path, false, false, -1);
    }
    return path;
}

void Adventurer::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                        std::shared_ptr<Element> arg_data,
                        std::shared_ptr<Element> arg_logdata) {
    if (!arg_f || !arg_data) throw std::invalid_argument("Adventurer::unconv requires file and data");

    // Python版と同様、解析・書き込みの前に現在・履歴の称号コンテナーを必要とする。
    ElementPtr coupons;
    if (arg_logdata) {
        coupons = element("Coupons");
        std::unordered_map<String, ElementPtr> current_by_name;
        std::unordered_set<const Element*> added;
        if (auto current = arg_data->find_exists("Property/Coupons")) {
            for (const auto& value : current->m_children) {
                current_by_name[value->m_text] = value;
                added.insert(value.get());
            }
        }
        if (auto logged = arg_logdata->find_exists("Property/Coupons")) {
            for (const auto& value : logged->m_children) {
                const auto found = current_by_name.find(value->m_text);
                if (found != current_by_name.end()) {
                    coupons->append(value);
                    added.erase(found->second.get());
                    current_by_name.erase(found);
                } else {
                    coupons->append(element("Coupon", "＾" + value->m_text,
                        {{"value", value->get("value", "0")}}));
                }
            }
        }
        if (auto current = arg_data->find_exists("Property/Coupons")) {
            for (const auto& value : current->m_children) {
                if (added.count(value.get())) coupons->append(value);
            }
        }
    } else {
        coupons = arg_data->find_exists("Property/Coupons");
    }

    String name;
    Integer resource_id = 0, level = 0, life = 0, maxlife = 0;
    String description;
    bool noeffect_weapon = false, noeffect_magic = false, undead = false, automaton = false;
    bool unholy = false, constructure = false, resist_fire = false, resist_ice = false;
    bool weakness_fire = false, weakness_ice = false;
    Integer paralyze = 0, poison = 0, avoid = 0, resist = 0, defense = 0;
    Integer dex = 0, agl = 0, intelligence = 0, strength = 0, vitality = 0, mind = 0;
    Integer aggressive = 0, cheerful = 0, brave = 0, cautious = 0, trickish = 0;
    Integer mentality = 0, duration_mentality = 0;
    Integer duration_bind = 0, duration_silence = 0, duration_faceup = 0, duration_antimagic = 0;
    Integer enhance_action = 0, duration_enhance_action = 0, enhance_avoid = 0, duration_enhance_avoid = 0;
    Integer enhance_resist = 0, duration_enhance_resist = 0, enhance_defense = 0, duration_enhance_defense = 0;
    ElementPtr items, skills, beasts;

    for (const auto& child : arg_data->m_children) {
        if (child->m_tag == "Property") {
            for (const auto& property : child->m_children) {
                if (property->m_tag == "Id") resource_id = export_integer(property->m_text);
                else if (property->m_tag == "Name") name = property->m_text;
                else if (property->m_tag == "Description") description = property->m_text;
                else if (property->m_tag == "Level") level = export_integer(property->m_text);
                else if (property->m_tag == "Life") {
                    life = export_integer(property->m_text); maxlife = export_integer_float(property->get("max", ""));
                } else if (property->m_tag == "Feature") {
                    for (const auto& feature : property->m_children) {
                        if (feature->m_tag == "Type") {
                            undead = boolean(feature->get("undead", "")); automaton = boolean(feature->get("automaton", ""));
                            unholy = boolean(feature->get("unholy", "")); constructure = boolean(feature->get("constructure", ""));
                        } else if (feature->m_tag == "NoEffect") {
                            noeffect_weapon = boolean(feature->get("weapon", "")); noeffect_magic = boolean(feature->get("magic", ""));
                        } else if (feature->m_tag == "Resist") {
                            resist_fire = boolean(feature->get("fire", "")); resist_ice = boolean(feature->get("ice", ""));
                        } else if (feature->m_tag == "Weakness") {
                            weakness_fire = boolean(feature->get("fire", "")); weakness_ice = boolean(feature->get("ice", ""));
                        }
                    }
                } else if (property->m_tag == "Ability") {
                    for (const auto& ability : property->m_children) {
                        if (ability->m_tag == "Physical") {
                            dex = export_integer(ability->get("dex", "")); agl = export_integer(ability->get("agl", ""));
                            intelligence = export_integer(ability->get("int", "")); strength = export_integer(ability->get("str", ""));
                            vitality = export_integer(ability->get("vit", "")); mind = export_integer(ability->get("min", ""));
                        } else if (ability->m_tag == "Mental") {
                            aggressive = export_integer_float(ability->get("aggressive", "")); cheerful = export_integer_float(ability->get("cheerful", ""));
                            brave = export_integer_float(ability->get("brave", "")); cautious = export_integer_float(ability->get("cautious", ""));
                            trickish = export_integer_float(ability->get("trickish", ""));
                        } else if (ability->m_tag == "Enhance") {
                            avoid = export_integer(ability->get("avoid", "")); resist = export_integer(ability->get("resist", ""));
                            defense = export_integer(ability->get("defense", ""));
                        }
                    }
                } else if (property->m_tag == "Status") {
                    for (const auto& status : property->m_children) {
                        if (status->m_tag == "Mentality") { mentality = unconv_mentality(status->m_text); duration_mentality = export_integer(status->get("duration", "")); }
                        else if (status->m_tag == "Paralyze") paralyze = export_integer(status->m_text);
                        else if (status->m_tag == "Poison") poison = export_integer(status->m_text);
                        else if (status->m_tag == "Bind") duration_bind = export_integer(status->get("duration", ""));
                        else if (status->m_tag == "Silence") duration_silence = export_integer(status->get("duration", ""));
                        else if (status->m_tag == "FaceUp") duration_faceup = export_integer(status->get("duration", ""));
                        else if (status->m_tag == "AntiMagic") duration_antimagic = export_integer(status->get("duration", ""));
                    }
                } else if (property->m_tag == "Enhance") {
                    for (const auto& enhance : property->m_children) {
                        if (enhance->m_tag == "Action") { enhance_action = export_integer(enhance->m_text); duration_enhance_action = export_integer(enhance->get("duration", "")); }
                        else if (enhance->m_tag == "Avoid") { enhance_avoid = export_integer(enhance->m_text); duration_enhance_avoid = export_integer(enhance->get("duration", "")); }
                        else if (enhance->m_tag == "Resist") { enhance_resist = export_integer(enhance->m_text); duration_enhance_resist = export_integer(enhance->get("duration", "")); }
                        else if (enhance->m_tag == "Defense") { enhance_defense = export_integer(enhance->m_text); duration_enhance_defense = export_integer(enhance->get("duration", "")); }
                    }
                }
            }
        } else if (child->m_tag == "ItemCards") items = child;
        else if (child->m_tag == "SkillCards") skills = child;
        else if (child->m_tag == "BeastCards") beasts = child;
    }

    arg_f->write_string(name);
    // ネイティブの符号付き加算をあふれさせず、Python版の書き込み時の拒否を維持する。
    const auto encoded_id = ::cw::lib::add_cardwirth_resource_id_bias(resource_id, 50000);
    arg_f->write_dword(encoded_id);
    for (bool value : {noeffect_weapon, noeffect_magic, undead, automaton, unholy, constructure,
                       resist_fire, resist_ice, weakness_fire, weakness_ice}) arg_f->write_bool(value);
    arg_f->write_dword(level); arg_f->write_string("TEXT\n" + description, true);
    arg_f->write_dword(life); arg_f->write_dword(maxlife);
    arg_f->write_dword(paralyze); arg_f->write_dword(poison);
    arg_f->write_dword(avoid); arg_f->write_dword(resist); arg_f->write_dword(defense);
    for (Integer value : {dex, agl, intelligence, strength, vitality, mind, aggressive, cheerful,
                          brave, cautious, trickish}) arg_f->write_dword(value);
    arg_f->write_byte(mentality); arg_f->write_dword(duration_mentality);
    for (Integer value : {duration_bind, duration_silence, duration_faceup, duration_antimagic,
                          enhance_action, duration_enhance_action, enhance_avoid, duration_enhance_avoid,
                          enhance_resist, duration_enhance_resist, enhance_defense,
                          duration_enhance_defense}) arg_f->write_dword(value);

    write_card_sequence(arg_f, items, name, [&arg_f](const ElementPtr& card) {
        ::cw::binary::item::ItemCard::unconv(arg_f, card, true);
    });
    write_card_sequence(arg_f, skills, name, [&arg_f](const ElementPtr& card) {
        ::cw::binary::skill::SkillCard::unconv(arg_f, card, true);
    });
    write_card_sequence(arg_f, beasts, name, [&arg_f](const ElementPtr& card) {
        ::cw::binary::beast::BeastCard::unconv(arg_f, card, true);
    });
    arg_f->write_dword(static_cast<Integer>(coupons->m_children.size()));
    for (const auto& value : coupons->m_children) ::cw::binary::coupon::Coupon::unconv(arg_f, value);
    (void)arg_f->truncate();
}

AdventurerCard::AdventurerCard(std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                               bool yadodata) {
    initialize(nullptr, std::move(file), yadodata);
}

void AdventurerCard::initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                                std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                                bool arg_yadodata) {
    CWBinaryBase::initialize(arg_parent, arg_f, arg_yadodata);
    m_xmltype = "AdventurerCard"; m_type = 1; m_fname = get_fname();
    if (!arg_f) { m_adventurer.reset(); return; }
    for (int index = 0; index < 5; ++index) (void)arg_f->byte();
    m_adventurer = std::make_shared<Adventurer>();
    m_adventurer->initialize(owned_self(*this), arg_f, arg_yadodata);
}

void AdventurerCard::set_image(::cw::lib::Bytes arg_image) {
    if (!m_adventurer) throw std::logic_error("AdventurerCard has no adventurer");
    m_adventurer->m_image = std::move(arg_image);
}

std::shared_ptr<Element> AdventurerCard::get_data() {
    if (!m_adventurer) throw std::logic_error("AdventurerCard has no adventurer");
    return m_adventurer->get_data();
}

::cw::lib::String AdventurerCard::create_xml(::cw::lib::String arg_dpath) {
    if (!m_adventurer) throw std::logic_error("AdventurerCard has no adventurer");
    return m_adventurer->create_xml(std::move(arg_dpath));
}

void AdventurerCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                            std::shared_ptr<Element> arg_data) {
    if (!arg_f || !arg_data) throw std::invalid_argument("AdventurerCard::unconv requires file and data");
    for (int index = 0; index < 5; ++index) arg_f->write_byte(0);
    Adventurer::unconv(std::move(arg_f), std::move(arg_data));
}

AdventurerWithImage::AdventurerWithImage(
    std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
    std::shared_ptr<::cw::binary::cwfile::CWFile> file, bool yadodata) {
    initialize(std::move(parent), std::move(file), yadodata);
}

void AdventurerWithImage::initialize(
    std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, bool arg_yadodata) {
    if (!arg_f) throw std::invalid_argument("AdventurerWithImage requires binary input");
    CWBinaryBase::initialize(arg_parent, arg_f, arg_yadodata);
    m_xmltype = "AdventurerWithImage";
    auto image = arg_f->image();
    m_adventurer = std::make_shared<Adventurer>();
    m_adventurer->initialize(owned_self(*this), arg_f, arg_yadodata);
    m_adventurer->m_image = std::move(image);
}

std::shared_ptr<Element> AdventurerWithImage::get_data() {
    if (!m_adventurer) throw std::logic_error("AdventurerWithImage has no adventurer");
    return m_adventurer->get_data();
}
std::shared_ptr<Element> AdventurerWithImage::get_f9data() {
    if (!m_adventurer) throw std::logic_error("AdventurerWithImage has no adventurer");
    return m_adventurer->get_f9data();
}

::cw::lib::String AdventurerWithImage::create_xml(::cw::lib::String arg_dpath) {
    if (!m_adventurer) throw std::logic_error("AdventurerWithImage has no adventurer");
    const auto path = m_adventurer->create_xml(std::move(arg_dpath));
    m_xmlpath = m_adventurer->m_xmlpath;
    return path;
}

void AdventurerWithImage::unconv(
    std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
    std::shared_ptr<Element> arg_data, std::shared_ptr<Element> arg_logdata) {
    if (!arg_f || !arg_data) throw std::invalid_argument("AdventurerWithImage::unconv requires file and data");
    auto image_paths = find_path(arg_data, "Property/ImagePaths");
    if (!image_paths) image_paths = find_path(arg_data, "Property/ImagePath");
    if (!image_paths) throw std::invalid_argument("AdventurerWithImage requires Property/ImagePath");
    // CWBinaryBase::import_image は、Pythonの
    // wx.Imageによる変換を共通の cw::lib::imagecodec
    // で行う。 埋め込み・ファイル画像を扱い、BMPのバイト列は保持する。
    arg_f->write_image(CWBinaryBase::import_image(arg_f, image_paths, true, false, "Center"));
    if (!arg_logdata) recover_for_f9(arg_data);
    Adventurer::unconv(std::move(arg_f), std::move(arg_data), std::move(arg_logdata));
}

AdventurerHeader::AdventurerHeader(std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                                   bool yadodata, ::cw::lib::Integer dataversion) {
    initialize(nullptr, std::move(file), yadodata, dataversion);
}

void AdventurerHeader::initialize(
    std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
    bool arg_yadodata, ::cw::lib::Integer arg_dataversion) {
    if (!arg_f) throw std::invalid_argument("AdventurerHeader requires binary input");
    CWBinaryBase::initialize(arg_parent, arg_f, arg_yadodata);
    m_xmltype = "AdventurerHeader"; m_type = 0; m_fname = get_fname();
    m_coupons.clear(); m_ep = m_dex = m_agl = m_int = m_str = m_vit = m_min = 0;
    if (arg_dataversion >= 10) {
        (void)arg_f->byte(); (void)arg_f->byte();
        m_name = arg_f->string(); m_image = arg_f->image(); m_level = arg_f->byte();
        (void)arg_f->byte(); m_coupons = split_lines(arg_f->string(true));
        (void)arg_f->word();
        m_ep = arg_f->byte(); (void)arg_f->byte();
        m_dex = arg_f->byte(); (void)arg_f->byte();
        m_agl = arg_f->byte(); (void)arg_f->byte();
        m_int = arg_f->byte(); (void)arg_f->byte();
        m_str = arg_f->byte(); (void)arg_f->byte();
        m_vit = arg_f->byte(); (void)arg_f->byte();
        m_min = arg_f->byte(); (void)arg_f->byte();
    } else {
        (void)arg_f->string(); m_name = arg_f->string(); m_image = arg_f->image();
        m_level = arg_f->dword(); (void)arg_f->dword();
        const auto count = arg_f->dword();
        for (Integer index = 0; index < count; ++index) m_coupons.push_back(arg_f->string());
        m_ep = m_level * 10;
    }
}

void AdventurerHeader::unconv(
    std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
    std::shared_ptr<Element> arg_data, ::cw::lib::String arg_fname) {
    if (!arg_f || !arg_data) throw std::invalid_argument("AdventurerHeader::unconv requires file and data");
    (void)arg_fname;
    String name; std::optional<::cw::lib::Bytes> image;
    Integer level = 0, dex = 0, agl = 0, intelligence = 0, strength = 0, vitality = 0, mind = 0, ep = 0;
    std::vector<String> coupon_names;
    // Python版と同様、全Propertyを文書順に処理し、後の値で前
    // の値を置き換える。 重複したCoupons・Physicalも含む。
    for (const auto& property : arg_data->m_children) {
        if (!property || property->m_tag != "Property") continue;
        for (const auto& value : property->m_children) {
            if (value->m_tag == "Name") name = value->m_text;
            else if (value->m_tag == "ImagePath" || value->m_tag == "ImagePaths") {
                // CWBinaryBase::import_image は共通のimagecodec
                // でRGBA復号・BMP符号化を行い、 既存のBMPはバイト列をそのまま保つ。
                image = CWBinaryBase::import_image(arg_f, value, true, false, "Center");
            } else if (value->m_tag == "Level") {
                // Python版と同様、write_word が拒否する
                // 時点まで任意精度整数の意味を保つ。 名前・画像の書き
                // 込み前に失敗させず、共通の出力用番兵値の境界を使う。
                level = export_integer(value->m_text);
            }
            else if (value->m_tag == "Ability") {
                // Python版と同様、すべてのPhysicalノードを文書順に処理する。
                for (const auto& physical : value->m_children) if (physical && physical->m_tag == "Physical") {
                    dex = export_integer(physical->get("dex", "")); agl = export_integer(physical->get("agl", ""));
                    intelligence = export_integer(physical->get("int", "")); strength = export_integer(physical->get("str", ""));
                    vitality = export_integer(physical->get("vit", "")); mind = export_integer(physical->get("min", ""));
                }
            } else if (value->m_tag == "Coupons") {
                // Python版と同様、コンテナーごとに表示用の称号一覧を
                // リセットする。 別に蓄積したEP値は、ここでは戻さない。
                coupon_names.clear();
                for (const auto& coupon : value->m_children) {
                    coupon_names.push_back(coupon->m_text);
                    if (coupon->m_text == "＠ＥＰ") ep = export_integer(coupon->get("value", "0"));
                }
            }
        }
    }
    arg_f->write_word(0); arg_f->write_string(name); arg_f->write_image(image);
    arg_f->write_word(level); arg_f->write_string(encode_text_list(coupon_names), true);
    arg_f->write_word(0); arg_f->write_word(ep);
    for (Integer value : {dex, agl, intelligence, strength, vitality, mind}) arg_f->write_word(value);
}

void main() {}

}  // namespace cw::binary::adventurer
