#include "cw/binary/cast.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/beast.hpp"
#include "cw/binary/card_common.hpp"
#include "cw/binary/coupon.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/skill.hpp"
#include "cw/data.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace cw::binary::cast {
namespace {

using Base = ::cw::binary::base::CWBinaryBase;
using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

Integer strict_integer(const String& value) {
    return ::cw::lib::require_python_integer_for_cardwirth_pack(value);
}

bool strict_bool(const String& value) {
    String normalized = value;
    for (char& ch : normalized)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (normalized == "true" || normalized == "1") return true;
    if (normalized == "false" || normalized == "0") return false;
    throw std::invalid_argument("invalid CastCard boolean: " + value);
}

String bool_text(bool value) { return value ? "True" : "False"; }

ElementPtr make_element(String tag, String text = {}) {
    return ::cw::data::make_element(std::move(tag), std::move(text));
}

String remove_description_marker(String value) {
    const auto position = value.find("TEXT\\n");
    if (position != String::npos) value.erase(position, 6);
    return value;
}

void append_status_duration(const ElementPtr& parent, const String& tag, Integer duration) {
    auto element = make_element(tag);
    element->set("duration", std::to_string(duration));
    parent->append(element);
}

void append_enhance(const ElementPtr& parent, const String& tag,
                    Integer value, Integer duration) {
    auto element = make_element(tag, std::to_string(value));
    element->set("duration", std::to_string(duration));
    parent->append(element);
}

String card_name(const ElementPtr& card) {
    if (!card) return {};
    const auto found = card->find("Property/Name");
    return found && *found ? (*found)->m_text : String{};
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
                file->log_error(owner_name + " の所持する " + card_name(card) +
                    " は対象エンジンで使用できない機能(" + error.m_funcname +
                    ")を使用しているため、変換しません。\n");
            } catch (...) {
                ::cw::util::print_ex(std::nullopt);
                file->seek(position);
                file->log_error(owner_name + " の所持する " + card_name(card) +
                    " は変換できませんでした。\n");
            }
        }
    }
    const auto end_position = file->tell();
    file->seek(length_position);
    file->write_dword(written);
    file->seek(end_position);
}

std::shared_ptr<Base> self(Base& value) {
    try {
        return value.shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return nullptr;
    }
}

}  // namespace

::cw::lib::String CastCard::export_image() {
    if (!m_image || m_image->empty()) return {};
    if (!get_image_export()) return ::cw::binary::image::data_to_code(*m_image);
    return Base::export_image();
}

void CastCard::initialize(::cw::lib::Value arg_parent,
                          std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                          bool arg_yadodata, bool arg_nameonly,
                          String arg_materialdir, bool arg_image_export) {
    if (!arg_f) throw std::invalid_argument("CastCard requires input");
    const auto parent = ::cw::binary::card_common::parent_from_value(
        std::optional<::cw::lib::Value>(arg_parent));
    std::optional<std::shared_ptr<Base>> base_parent;
    if (parent) base_parent = parent;
    Base::initialize(base_parent, arg_f, arg_yadodata, std::move(arg_materialdir), arg_image_export);
    m_xmltype = "CastCard";
    m_type = arg_f->byte();
    m_image = arg_f->image();
    m_imgpath.clear();
    m_name = arg_f->string();
    const auto encoded_id = arg_f->dword();
    Integer dataversion = 0;
    if (encoded_id <= 19999) {
        dataversion = 0;
        m_id = encoded_id;
    } else if (encoded_id <= 39999) {
        dataversion = 2;
        m_id = encoded_id - 20000;
    } else {
        dataversion = 4;
        m_id = encoded_id - 40000;
    }

    m_items.clear();
    m_skills.clear();
    m_beasts.clear();
    m_coupons.clear();
    m_data.reset();
    m_nameonly = arg_nameonly;
    if (arg_nameonly) return;

    m_noeffect_weapon = arg_f->boolean();
    m_noeffect_magic = arg_f->boolean();
    m_undead = arg_f->boolean();
    m_automaton = arg_f->boolean();
    m_unholy = arg_f->boolean();
    m_constructure = arg_f->boolean();
    m_resist_fire = arg_f->boolean();
    m_resist_ice = arg_f->boolean();
    m_weakness_fire = arg_f->boolean();
    m_weakness_ice = arg_f->boolean();

    m_level = arg_f->dword();
    m_money = arg_f->dword();
    m_description = remove_description_marker(arg_f->string(true));
    m_life = arg_f->dword();
    m_maxlife = arg_f->dword();
    m_paralyze = arg_f->dword();
    m_poison = arg_f->dword();
    m_avoid = arg_f->dword();
    m_resist = arg_f->dword();
    m_defense = arg_f->dword();

    m_dex = arg_f->dword();
    m_agl = arg_f->dword();
    m_int = arg_f->dword();
    m_str = arg_f->dword();
    m_vit = arg_f->dword();
    m_min = arg_f->dword();
    m_aggressive = arg_f->dword();
    m_cheerful = arg_f->dword();
    m_brave = arg_f->dword();
    m_cautious = arg_f->dword();
    m_trickish = arg_f->dword();

    m_mentality = arg_f->byte();
    m_duration_mentality = arg_f->dword();
    m_duration_bind = arg_f->dword();
    m_duration_silence = arg_f->dword();
    m_duration_faceup = arg_f->dword();
    m_duration_antimagic = arg_f->dword();
    m_enhance_action = arg_f->dword();
    m_duration_enhance_action = arg_f->dword();
    m_enhance_avoid = arg_f->dword();
    m_duration_enhance_avoid = arg_f->dword();
    m_enhance_resist = arg_f->dword();
    m_duration_enhance_resist = arg_f->dword();
    m_enhance_defense = arg_f->dword();
    m_duration_enhance_defense = arg_f->dword();

    const auto parent_as_value = ::cw::binary::card_common::self_value(*this);
    auto count = arg_f->dword();
    for (Integer index = 0; index < count; ++index) {
        auto card = std::make_shared<::cw::binary::item::ItemCard>();
        card->initialize(parent_as_value, arg_f, arg_yadodata, false, "Material", true);
        m_items.push_back(std::move(card));
    }
    count = arg_f->dword();
    for (Integer index = 0; index < count; ++index) {
        auto card = std::make_shared<::cw::binary::skill::SkillCard>();
        card->initialize(parent_as_value, arg_f, arg_yadodata, false, "Material", true);
        m_skills.push_back(std::move(card));
    }
    count = arg_f->dword();
    for (Integer index = 0; index < count; ++index) {
        auto card = std::make_shared<::cw::binary::beast::BeastCard>();
        card->initialize(parent_as_value, arg_f, arg_yadodata, false, "Material", true, false);
        m_beasts.push_back(std::move(card));
    }

    if (dataversion > 0) {
        count = arg_f->dword();
        const auto parent_object = self(*this);
        for (Integer index = 0; index < count; ++index) {
            auto coupon = std::make_shared<::cw::binary::coupon::Coupon>();
            // cast.py の Coupon(self, f) は dataversion
            // の既定値5を使う。 CastCardのバイナリ版によって、Coupon
            // の旧冒険者・アルバム用の遺伝子復号を有効にしない。
            coupon->initialize(parent_object, arg_f, arg_yadodata, 5);
            m_coupons.push_back(std::move(coupon));
        }
    }
}

// CastCard はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
std::shared_ptr<Element> CastCard::get_data() {
    if (m_nameonly) throw std::logic_error("name-only CastCard has no XML data");
    if (m_data && *m_data) return *m_data;
    m_imgpath = m_image && !m_image->empty() ? export_image() : String{};

    auto data = make_element("CastCard");
    auto property = make_element("Property");
    property->append(make_element("Id", std::to_string(m_id)));
    property->append(make_element("Name", m_name));
    property->append(make_element("ImagePath", m_imgpath));
    property->append(make_element("Description", m_description));
    property->append(make_element("Level", std::to_string(m_level)));
    property->append(make_element("Money", std::to_string(m_money)));
    auto life = make_element("Life", std::to_string(m_life));
    life->set("max", std::to_string(m_maxlife));
    property->append(life);

    auto feature = make_element("Feature");
    auto type = make_element("Type");
    type->set("undead", bool_text(m_undead));
    type->set("automaton", bool_text(m_automaton));
    type->set("unholy", bool_text(m_unholy));
    type->set("constructure", bool_text(m_constructure));
    feature->append(type);
    auto no_effect = make_element("NoEffect");
    no_effect->set("weapon", bool_text(m_noeffect_weapon));
    no_effect->set("magic", bool_text(m_noeffect_magic));
    feature->append(no_effect);
    auto resist = make_element("Resist");
    resist->set("fire", bool_text(m_resist_fire));
    resist->set("ice", bool_text(m_resist_ice));
    feature->append(resist);
    auto weakness = make_element("Weakness");
    weakness->set("fire", bool_text(m_weakness_fire));
    weakness->set("ice", bool_text(m_weakness_ice));
    feature->append(weakness);
    property->append(feature);

    auto ability = make_element("Ability");
    auto physical = make_element("Physical");
    physical->set("dex", std::to_string(m_dex));
    physical->set("agl", std::to_string(m_agl));
    physical->set("int", std::to_string(m_int));
    physical->set("str", std::to_string(m_str));
    physical->set("vit", std::to_string(m_vit));
    physical->set("min", std::to_string(m_min));
    ability->append(physical);
    auto mental = make_element("Mental");
    mental->set("aggressive", std::to_string(m_aggressive));
    mental->set("cheerful", std::to_string(m_cheerful));
    mental->set("brave", std::to_string(m_brave));
    mental->set("cautious", std::to_string(m_cautious));
    mental->set("trickish", std::to_string(m_trickish));
    ability->append(mental);
    auto ability_enhance = make_element("Enhance");
    ability_enhance->set("avoid", std::to_string(m_avoid));
    ability_enhance->set("resist", std::to_string(m_resist));
    ability_enhance->set("defense", std::to_string(m_defense));
    ability->append(ability_enhance);
    property->append(ability);

    auto status = make_element("Status");
    auto mentality = make_element("Mentality", conv_mentality(m_mentality));
    mentality->set("duration", std::to_string(m_duration_mentality));
    status->append(mentality);
    status->append(make_element("Paralyze", std::to_string(m_paralyze)));
    status->append(make_element("Poison", std::to_string(m_poison)));
    append_status_duration(status, "Bind", m_duration_bind);
    append_status_duration(status, "Silence", m_duration_silence);
    append_status_duration(status, "FaceUp", m_duration_faceup);
    append_status_duration(status, "AntiMagic", m_duration_antimagic);
    property->append(status);

    auto enhance_group = make_element("Enhance");
    append_enhance(enhance_group, "Action", m_enhance_action, m_duration_enhance_action);
    append_enhance(enhance_group, "Avoid", m_enhance_avoid, m_duration_enhance_avoid);
    append_enhance(enhance_group, "Resist", m_enhance_resist, m_duration_enhance_resist);
    append_enhance(enhance_group, "Defense", m_enhance_defense, m_duration_enhance_defense);
    property->append(enhance_group);

    auto coupons = make_element("Coupons");
    for (const auto& coupon : m_coupons)
        if (coupon) coupons->append(coupon->get_data());
    property->append(coupons);
    data->append(property);

    auto items = make_element("ItemCards");
    for (const auto& item : m_items)
        if (item) items->append(item->get_data());
    data->append(items);
    auto skills = make_element("SkillCards");
    for (const auto& skill : m_skills)
        if (skill) skills->append(skill->get_data());
    data->append(skills);
    auto beasts = make_element("BeastCards");
    for (const auto& beast : m_beasts)
        if (beast) beasts->append(beast->get_data());
    data->append(beasts);
    m_data = data;
    return data;
}

void CastCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,
                      ElementPtr data) {
    if (!file || !data) throw std::invalid_argument("CastCard::unconv requires file and data");

    Integer id = 0, level = 0, money = 0, life = 0, max_life = 0;
    Integer paralyze = 0, poison = 0, avoid = 0, resist = 0, defense = 0;
    Integer dex = 0, agl = 0, integer_ability = 0, strength = 0, vit = 0, mind = 0;
    Integer aggressive = 0, cheerful = 0, brave = 0, cautious = 0, trickish = 0;
    Integer mentality = 0, duration_mentality = 0, duration_bind = 0;
    Integer duration_silence = 0, duration_faceup = 0, duration_antimagic = 0;
    Integer enhance_action = 0, duration_enhance_action = 0;
    Integer enhance_avoid = 0, duration_enhance_avoid = 0;
    Integer enhance_resist = 0, duration_enhance_resist = 0;
    Integer enhance_defense = 0, duration_enhance_defense = 0;
    String name, description;
    std::optional<::cw::lib::Bytes> image;
    bool no_effect_weapon = false, no_effect_magic = false;
    bool undead = false, automaton = false, unholy = false, constructure = false;
    bool resist_fire = false, resist_ice = false, weakness_fire = false, weakness_ice = false;
    ElementPtr items, skills, beasts, coupons;

    for (const auto& child : data->m_children) {
        if (!child) continue;
        if (child->m_tag == "Property") {
            for (const auto& property : child->m_children) {
                if (!property) continue;
                const auto& tag = property->m_tag;
                if (tag == "Id") id = strict_integer(property->m_text);
                else if (tag == "Name") name = property->m_text;
                else if (tag == "ImagePath" || tag == "ImagePaths")
                    // CWBinaryBase::import_image は、共通のimagecodec
                    // でRGBA復号・BMP符号化を行い、BMPのバイト列は保持する。
                    image = Base::import_image(file, property, true, false, "Center");
                else if (tag == "Description") description = property->m_text;
                else if (tag == "Level") level = strict_integer(property->m_text);
                else if (tag == "Money")
                    money = std::clamp(strict_integer(property->m_text), Integer{0}, Integer{100000});
                else if (tag == "Life") {
                    life = strict_integer(property->m_text);
                    max_life = strict_integer(property->get("max", ""));
                } else if (tag == "Feature") {
                    for (const auto& feature : property->m_children) {
                        if (!feature) continue;
                        if (feature->m_tag == "Type") {
                            undead = strict_bool(feature->get("undead", ""));
                            automaton = strict_bool(feature->get("automaton", ""));
                            unholy = strict_bool(feature->get("unholy", ""));
                            constructure = strict_bool(feature->get("constructure", ""));
                        } else if (feature->m_tag == "NoEffect") {
                            no_effect_weapon = strict_bool(feature->get("weapon", ""));
                            no_effect_magic = strict_bool(feature->get("magic", ""));
                        } else if (feature->m_tag == "Resist") {
                            resist_fire = strict_bool(feature->get("fire", ""));
                            resist_ice = strict_bool(feature->get("ice", ""));
                        } else if (feature->m_tag == "Weakness") {
                            weakness_fire = strict_bool(feature->get("fire", ""));
                            weakness_ice = strict_bool(feature->get("ice", ""));
                        }
                    }
                } else if (tag == "Ability") {
                    for (const auto& ability : property->m_children) {
                        if (!ability) continue;
                        if (ability->m_tag == "Physical") {
                            dex = strict_integer(ability->get("dex", ""));
                            agl = strict_integer(ability->get("agl", ""));
                            integer_ability = strict_integer(ability->get("int", ""));
                            strength = strict_integer(ability->get("str", ""));
                            vit = strict_integer(ability->get("vit", ""));
                            mind = strict_integer(ability->get("min", ""));
                        } else if (ability->m_tag == "Mental") {
                            aggressive = strict_integer(ability->get("aggressive", ""));
                            cheerful = strict_integer(ability->get("cheerful", ""));
                            brave = strict_integer(ability->get("brave", ""));
                            cautious = strict_integer(ability->get("cautious", ""));
                            trickish = strict_integer(ability->get("trickish", ""));
                        } else if (ability->m_tag == "Enhance") {
                            avoid = strict_integer(ability->get("avoid", ""));
                            resist = strict_integer(ability->get("resist", ""));
                            defense = strict_integer(ability->get("defense", ""));
                        }
                    }
                } else if (tag == "Status") {
                    for (const auto& status : property->m_children) {
                        if (!status) continue;
                        if (status->m_tag == "Mentality") {
                            mentality = Base::unconv_mentality(status->m_text);
                            duration_mentality = strict_integer(status->get("duration", ""));
                        } else if (status->m_tag == "Paralyze") {
                            paralyze = strict_integer(status->m_text);
                        } else if (status->m_tag == "Poison") {
                            poison = strict_integer(status->m_text);
                        } else if (status->m_tag == "Bind") {
                            duration_bind = strict_integer(status->get("duration", ""));
                        } else if (status->m_tag == "Silence") {
                            duration_silence = strict_integer(status->get("duration", ""));
                        } else if (status->m_tag == "FaceUp") {
                            duration_faceup = strict_integer(status->get("duration", ""));
                        } else if (status->m_tag == "AntiMagic") {
                            duration_antimagic = strict_integer(status->get("duration", ""));
                        }
                    }
                } else if (tag == "Enhance") {
                    for (const auto& enhance : property->m_children) {
                        if (!enhance) continue;
                        if (enhance->m_tag == "Action") {
                            enhance_action = strict_integer(enhance->m_text);
                            duration_enhance_action = strict_integer(enhance->get("duration", ""));
                        } else if (enhance->m_tag == "Avoid") {
                            enhance_avoid = strict_integer(enhance->m_text);
                            duration_enhance_avoid = strict_integer(enhance->get("duration", ""));
                        } else if (enhance->m_tag == "Resist") {
                            enhance_resist = strict_integer(enhance->m_text);
                            duration_enhance_resist = strict_integer(enhance->get("duration", ""));
                        } else if (enhance->m_tag == "Defense") {
                            enhance_defense = strict_integer(enhance->m_text);
                            duration_enhance_defense = strict_integer(enhance->get("duration", ""));
                        }
                    }
                } else if (tag == "Coupons") {
                    coupons = property;
                }
            }
        } else if (child->m_tag == "ItemCards") {
            items = child;
        } else if (child->m_tag == "SkillCards") {
            skills = child;
        } else if (child->m_tag == "BeastCards") {
            beasts = child;
        }
    }

    file->write_byte(2);
    file->write_image(image);
    file->write_string(name);
    file->write_dword(::cw::lib::add_cardwirth_resource_id_bias(id, 40000));
    for (const bool value : {no_effect_weapon, no_effect_magic, undead, automaton,
                             unholy, constructure, resist_fire, resist_ice,
                             weakness_fire, weakness_ice}) file->write_bool(value);
    file->write_dword(level);
    file->write_dword(money);
    file->write_string("TEXT\n" + description, true);
    file->write_dword(life);
    file->write_dword(max_life);
    for (const Integer value : {paralyze, poison, avoid, resist, defense, dex, agl,
                                integer_ability, strength, vit, mind, aggressive,
                                cheerful, brave, cautious, trickish}) file->write_dword(value);
    file->write_byte(mentality);
    for (const Integer value : {duration_mentality, duration_bind, duration_silence,
                                duration_faceup, duration_antimagic, enhance_action,
                                duration_enhance_action, enhance_avoid,
                                duration_enhance_avoid, enhance_resist,
                                duration_enhance_resist, enhance_defense,
                                duration_enhance_defense}) file->write_dword(value);

    write_card_sequence(file, items, name,
        [&](const ElementPtr& card) { ::cw::binary::item::ItemCard::unconv(file, card, false); });
    write_card_sequence(file, skills, name,
        [&](const ElementPtr& card) { ::cw::binary::skill::SkillCard::unconv(file, card, false); });
    write_card_sequence(file, beasts, name,
        [&](const ElementPtr& card) { ::cw::binary::beast::BeastCard::unconv(file, card, false); });

    if (!coupons) {
        file->write_dword(0);
    } else {
        file->write_dword(static_cast<Integer>(coupons->m_children.size()));
        for (const auto& coupon : coupons->m_children)
            ::cw::binary::coupon::Coupon::unconv(file, coupon);
    }
    file->truncate();
}

void main() {}

}  // namespace cw::binary::cast
