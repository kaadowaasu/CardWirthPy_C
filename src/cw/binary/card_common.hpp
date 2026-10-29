#pragma once

#include "cw/binary/base.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/effectmotion.hpp"
#include "cw/binary/event.hpp"
#include "cw/data.hpp"
#include "cw/lib/python_integer.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace cw::binary::card_common {

using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

inline ElementPtr make_element(String tag, String text = {}) {
    return ::cw::data::make_element(std::move(tag), std::move(text));
}

inline bool to_bool(const String& value) {
    String normalized = value;
    for (char& ch : normalized)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (normalized == "true" || normalized == "1") return true;
    if (normalized == "false" || normalized == "0") return false;
    throw std::invalid_argument("invalid boolean: " + value);
}

inline bool to_bool(const String& value, bool fallback) {
    return value.empty() ? fallback : to_bool(value);
}

inline String bool_text(bool value) { return value ? "True" : "False"; }

inline Integer to_integer(const String& value) {
    return ::cw::lib::require_python_integer(value);
}

inline Integer to_pack_integer(const String& value) {
    // これらの出力フィールドは word/dword の書き込みだけで使う。部分
    // 出力時も含め、その書き込みまで Py の任意精度の有効な整数を保持する。
    return ::cw::lib::require_python_integer_for_cardwirth_pack(value);
}

inline Integer to_integer(const String& value, Integer fallback) {
    return value.empty() ? fallback : to_integer(value);
}

inline std::vector<String> decode_text_list(const String& encoded) {
    String decoded;
    bool escaped = false;
    for (char ch : encoded) {
        if (escaped) {
            decoded.push_back(ch == 'n' ? '\n' : ch);
            escaped = false;
        } else if (ch == '\\') escaped = true;
        else decoded.push_back(ch);
    }
    std::vector<String> result;
    std::size_t begin = 0;
    while (begin <= decoded.size()) {
        const auto end = decoded.find('\n', begin);
        result.push_back(decoded.substr(begin, end == String::npos ? end : end - begin));
        if (end == String::npos) break;
        begin = end + 1;
    }
    if (result.size() == 1 && result[0].empty()) result.clear();
    return result;
}

inline String encode_text_list(const std::vector<String>& values) {
    String result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) result += "\\n";
        for (char ch : values[index]) {
            if (ch == '\\') result += "\\\\";
            else if (ch == '\n') result += "\\n";
            else if (ch != '\r') result.push_back(ch);
        }
    }
    return result;
}

inline std::shared_ptr<::cw::binary::base::CWBinaryBase> parent_from_value(
    const std::optional<::cw::lib::Value>& value) {
    if (!value) return nullptr;
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value->storage())) {
        return std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object);
    }
    return nullptr;
}

inline ::cw::lib::Value self_value(::cw::binary::base::CWBinaryBase& value) {
    try {
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(value.shared_from_this()));
    } catch (const std::bad_weak_ptr&) {
        return {};
    }
}

template <typename Card>
Integer read_common(Card& card, const std::optional<::cw::lib::Value>& parent,
                    const std::shared_ptr<::cw::binary::cwfile::CWFile>& file,
                    bool yadodata, bool nameonly, const String& materialdir,
                    bool image_export, const String& xmltype) {
    if (!file) throw std::invalid_argument(xmltype + " requires binary input");
    static_cast<::cw::binary::base::CWBinaryBase&>(card).initialize(
        parent_from_value(parent), file, yadodata, materialdir, image_export);
    card.m_xmltype = xmltype;
    card.m_type = file->byte();
    card.m_image = file->image();
    card.m_imgpath.clear();
    card.m_name = file->string();
    const auto encoded_id = file->dword();
    Integer version = 0;
    if (encoded_id <= 19999) { version = 0; card.m_id = encoded_id; }
    else if (encoded_id <= 39999) { version = 2; card.m_id = encoded_id - 20000; }
    else if (encoded_id <= 49999) { version = 4; card.m_id = encoded_id - 40000; }
    else { version = 5; card.m_id = encoded_id - 50000; }
    if (nameonly) return version;
    if (version >= 5) card.m_fname = card.get_fname();
    card.m_description = file->string(true);
    card.m_p_ability = file->dword(); card.m_m_ability = file->dword();
    card.m_silence = file->boolean(); card.m_target_all = file->boolean();
    card.m_target = file->byte(); card.m_effect_type = file->byte();
    card.m_resist_type = file->byte(); card.m_success_rate = file->dword();
    card.m_visual_effect = file->byte();
    const auto motion_count = file->dword();
    card.m_motions.clear();
    const auto parent_object = self_value(card);
    for (Integer index = 0; index < motion_count; ++index) {
        auto motion = std::make_shared<::cw::binary::effectmotion::EffectMotion>();
        motion->initialize(parent_object, file, yadodata, version);
        card.m_motions.push_back(std::move(motion));
    }
    card.m_enhance_avoid = file->dword(); card.m_enhance_resist = file->dword();
    card.m_enhance_defense = file->dword();
    card.m_sound_effect = file->string(); card.m_sound_effect2 = file->string();
    card.m_keycodes.clear();
    for (int index = 0; index < 5; ++index) card.m_keycodes.push_back(file->string());
    card.m_events.clear();
    if (version > 2) {
        card.m_premium = file->byte();
        card.m_scenario_name = file->string(); card.m_scenario_author = file->string();
        const auto event_count = file->dword();
        for (Integer index = 0; index < event_count; ++index) {
            auto event = std::make_shared<::cw::binary::event::SimpleEvent>();
            event->initialize(parent_object, file, yadodata);
            card.m_events.push_back(std::move(event));
        }
        card.m_hold = file->boolean();
    } else {
        card.m_scenario_name.clear(); card.m_scenario_author.clear(); card.m_hold = false;
        card.m_premium = version > 0 ? file->byte() : 0;
    }
    if (version >= 5) (void)file->dword();
    return version;
}

template <typename Card>
std::pair<ElementPtr, ElementPtr> common_xml(Card& card, const String& tag) {
    if (card.m_premium > 2) card.set_image_export(false, true);
    if (card.m_imgpath.empty() && card.m_image) card.m_imgpath = card.export_image();
    auto root = make_element(tag);
    auto property = make_element("Property");
    property->append(make_element("Id", std::to_string(card.m_id)));
    property->append(make_element("Name", card.m_name));
    property->append(make_element("ImagePath", card.m_imgpath));
    property->append(make_element("Description", card.m_description));
    property->append(make_element("Scenario", card.m_scenario_name));
    property->append(make_element("Author", card.m_scenario_author));
    auto ability = make_element("Ability");
    ability->set("physical", card.conv_card_physicalability(card.m_p_ability));
    ability->set("mental", card.conv_card_mentalability(card.m_m_ability));
    property->append(ability);
    auto target = make_element("Target", card.conv_card_target(card.m_target));
    target->set("allrange", bool_text(card.m_target_all)); property->append(target);
    auto effect = make_element("EffectType", card.conv_card_effecttype(card.m_effect_type));
    effect->set("spell", bool_text(card.m_silence)); property->append(effect);
    property->append(make_element("ResistType", card.conv_card_resisttype(card.m_resist_type)));
    property->append(make_element("SuccessRate", std::to_string(card.m_success_rate)));
    property->append(make_element("VisualEffect", card.conv_card_visualeffect(card.m_visual_effect)));
    auto enhance = make_element("Enhance");
    enhance->set("avoid", std::to_string(card.m_enhance_avoid));
    enhance->set("resist", std::to_string(card.m_enhance_resist));
    enhance->set("defense", std::to_string(card.m_enhance_defense)); property->append(enhance);
    property->append(make_element("SoundPath", card.get_materialpath(card.m_sound_effect)));
    property->append(make_element("SoundPath2", card.get_materialpath(card.m_sound_effect2)));
    property->append(make_element("KeyCodes", encode_text_list(card.m_keycodes)));
    if (card.m_premium > 2) {
        root->set("scenariocard", "True");
        property->append(make_element("Premium", card.conv_card_premium(card.m_premium - 3)));
    } else {
        property->append(make_element("Premium", card.conv_card_premium(card.m_premium)));
    }
    return {root, property};
}

template <typename Card>
void append_common_children(Card& card, const ElementPtr& root) {
    auto motions = make_element("Motions");
    for (const auto& motion : card.m_motions) if (motion && motion->get_data()) motions->append(motion->get_data());
    root->append(motions);
    auto events = make_element("Events");
    for (const auto& event : card.m_events) if (event && event->get_data()) events->append(event->get_data());
    root->append(events);
}

struct WriteData {
    std::optional<::cw::lib::Bytes> image{};
    String name{}, description{}, scenario_name{}, scenario_author{};
    Integer id{}, p_ability{}, m_ability{}, target{}, effect_type{}, resist_type{};
    Integer success_rate{}, visual_effect{}, enhance_avoid{}, enhance_resist{}, enhance_defense{};
    bool silence{}, target_all{}, hold{}, scenario_card{};
    String sound_effect{}, sound_effect2{};
    std::vector<String> keycodes{};
    Integer premium{};
    ElementPtr property{}, motions{}, events{};
};

inline void normalize_keycodes(
    const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
    std::vector<String>& keycodes) {
    // Python版はKeyCodes要素を読んだ時だけ、空要素を捨てて非空キーコード
    // を最大5件に絞る。6件目の非空値に到達した時点でWSN判定を行う。
    if (keycodes.size() > 5) {
        std::vector<String> limited;
        limited.reserve(5);
        for (const auto& keycode : keycodes) {
            if (keycode.empty()) continue;
            if (limited.size() >= 5) {
                file->check_wsnversion("", "5件を超えるキーコード指定");
                break;
            }
            limited.push_back(keycode);
        }
        keycodes = std::move(limited);
    }
    if (keycodes.size() < 5) keycodes.resize(5);
}

inline WriteData parse_common_xml(
    const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
    const ElementPtr& data, bool owner_is_adventurer,
    const std::function<void(const ElementPtr&)>& parse_specific = {}) {
    WriteData result;
    result.scenario_card = to_bool(data->get("scenariocard", "False"));
    for (const auto& child : data->m_children) {
        if (child->m_tag == "Property") {
            result.property = child;
            for (const auto& property : child->m_children) {
                if (property->m_tag == "Id") result.id = to_pack_integer(property->m_text);
                else if (property->m_tag == "Name") result.name = property->m_text;
                else if (property->m_tag == "ImagePath" || property->m_tag == "ImagePaths") result.image = ::cw::binary::base::CWBinaryBase::import_image(file, property);
                else if (property->m_tag == "Description") result.description = property->m_text;
                else if (property->m_tag == "Scenario") result.scenario_name = property->m_text;
                else if (property->m_tag == "Author") result.scenario_author = property->m_text;
                else if (property->m_tag == "Ability") {
                    result.p_ability = ::cw::binary::base::CWBinaryBase::unconv_card_physicalability(property->get("physical", ""));
                    result.m_ability = ::cw::binary::base::CWBinaryBase::unconv_card_mentalability(property->get("mental", ""));
                } else if (property->m_tag == "Target") {
                    result.target = ::cw::binary::base::CWBinaryBase::unconv_card_target(property->m_text);
                    result.target_all = to_bool(property->get("allrange", ""));
                } else if (property->m_tag == "EffectType") {
                    result.effect_type = ::cw::binary::base::CWBinaryBase::unconv_card_effecttype(property->m_text);
                    result.silence = to_bool(property->get("spell", ""));
                } else if (property->m_tag == "ResistType") result.resist_type = ::cw::binary::base::CWBinaryBase::unconv_card_resisttype(property->m_text);
                else if (property->m_tag == "SuccessRate") result.success_rate = to_pack_integer(property->m_text);
                else if (property->m_tag == "VisualEffect") result.visual_effect = ::cw::binary::base::CWBinaryBase::unconv_card_visualeffect(property->m_text);
                else if (property->m_tag == "Enhance") {
                    result.enhance_avoid = to_pack_integer(property->get("avoid", ""));
                    result.enhance_resist = to_pack_integer(property->get("resist", ""));
                    result.enhance_defense = to_pack_integer(property->get("defense", ""));
                } else if (property->m_tag == "SoundPath") {
                    result.sound_effect = ::cw::binary::base::CWBinaryBase::materialpath(property->m_text); file->check_soundoptions(property);
                } else if (property->m_tag == "SoundPath2") {
                    result.sound_effect2 = ::cw::binary::base::CWBinaryBase::materialpath(property->m_text); file->check_soundoptions(property);
                } else if (property->m_tag == "KeyCodes") {
                    result.keycodes = decode_text_list(property->m_text);
                    normalize_keycodes(file, result.keycodes);
                }
                else if (property->m_tag == "Premium") {
                    result.premium = ::cw::binary::base::CWBinaryBase::unconv_card_premium(property->m_text);
                    if (owner_is_adventurer && result.scenario_card) result.premium += 3;
                } else if (property->m_tag == "Hold") result.hold = to_bool(property->m_text);
                else if (property->m_tag == "LinkId" && !property->m_text.empty() && property->m_text != "0") file->check_wsnversion("1", "カード参照");
                // カード固有の変換は Py の元の XML 順に行う。二回に分
                // けると、どの不正フィールド・検査が最初に失敗するかが変わる。
                else if (parse_specific) parse_specific(property);
            }
        } else if (child->m_tag == "Motions") result.motions = child;
        else if (child->m_tag == "Events") result.events = child;
        else if ((child->m_tag == "Flags" || child->m_tag == "Steps" || child->m_tag == "Variants") && !child->m_children.empty()) file->check_wsnversion("4", "ローカル変数");
    }
    return result;
}

inline void write_common(
    const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
    const WriteData& value, Integer unknown_yado_value) {
    file->write_byte(0); file->write_image(value.image); file->write_string(value.name);
    file->write_dword(::cw::lib::add_cardwirth_resource_id_bias(value.id, 50000)); file->write_string(value.description, true);
    file->write_dword(value.p_ability); file->write_dword(value.m_ability);
    file->write_bool(value.silence); file->write_bool(value.target_all);
    file->write_byte(value.target); file->write_byte(value.effect_type); file->write_byte(value.resist_type);
    file->write_dword(value.success_rate); file->write_byte(value.visual_effect);
    file->write_dword(value.motions ? static_cast<Integer>(value.motions->m_children.size()) : 0);
    if (value.motions) for (const auto& motion : value.motions->m_children) ::cw::binary::effectmotion::EffectMotion::unconv(file, motion);
    file->write_dword(value.enhance_avoid); file->write_dword(value.enhance_resist); file->write_dword(value.enhance_defense);
    file->write_string(value.sound_effect); file->write_string(value.sound_effect2);
    for (const auto& keycode : value.keycodes) file->write_string(keycode);
    file->write_byte(value.premium); file->write_string(value.scenario_name); file->write_string(value.scenario_author);
    file->write_dword(value.events ? static_cast<Integer>(value.events->m_children.size()) : 0);
    if (value.events) for (const auto& event : value.events->m_children) ::cw::binary::event::SimpleEvent::unconv(file, event);
    file->write_bool(value.hold); file->write_dword(unknown_yado_value);
}

}  // namespace cw::binary::card_common
