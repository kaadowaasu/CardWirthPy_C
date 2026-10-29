#include "cw/binary/effectmotion.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/beast.hpp"
#include "cw/binary/card_common.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"

#include <cctype>
#include <stdexcept>

namespace cw::binary::effectmotion {
namespace {

::cw::lib::String property_text(const ::cw::lib::Value& value)
{
    if (const auto* text = std::get_if<::cw::lib::String>(&value.storage())) {
        return *text;
    }
    if (const auto* number = std::get_if<::cw::lib::Integer>(&value.storage())) {
        return std::to_string(*number);
    }
    throw std::logic_error("EffectMotion property is neither str nor int");
}

::cw::lib::Integer parse_integer(const ::cw::lib::String& text) {
    // Python版と同様、バイナリの固定幅へ詰めるまで任意精度の整数の意味を保
    // つ。 書き込み専用の共通番兵値で、失敗時点と先行する出力バイトを維持する。
    return ::cw::lib::require_python_integer_for_cardwirth_pack(text);
}

}  // namespace

void EffectMotion::initialize(::cw::lib::Value arg_parent,
                              std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                              bool arg_yadodata,
                              ::cw::lib::Integer arg_dataversion)
{
    if (!arg_f) {
        throw std::invalid_argument("EffectMotion requires binary input");
    }

    const auto parent = ::cw::binary::card_common::parent_from_value(
        std::optional<::cw::lib::Value>(arg_parent));
    std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> parent_value;
    if (parent) {
        parent_value = parent;
    }
    ::cw::binary::base::CWBinaryBase::initialize(parent_value, arg_f, arg_yadodata);

    m_xmltype = "EffectMotion";
    m_tabtype = arg_f->byte();
    if (arg_dataversion > 2) {
        // Python版と同じく、既知だが意味不明の5バイト (8,5,0,0,0) は検証せず読み飛ばす。
        for (int index = 0; index < 5; ++index) {
            (void)arg_f->byte();
        }
    }

    m_element = arg_f->byte();
    m_type = m_tabtype == 8 ? 0 : arg_f->byte();
    m_properties.clear();
    m_beasts.reset();
    m_data.reset();

    if (m_tabtype == 0 || m_tabtype == 1) {
        m_properties["damagetype"] = conv_effectmotion_damagetype(arg_f->byte());
        m_properties["value"] = arg_f->dword();
    } else if (m_tabtype == 3 || m_tabtype == 4) {
        m_properties["duration"] = arg_dataversion > 2 ? arg_f->dword() : 10;
    } else if (m_tabtype == 5) {
        m_properties["value"] = arg_f->dword();
        m_properties["duration"] = arg_dataversion > 2 ? arg_f->dword() : 10;
    } else if (m_tabtype == 2 || m_tabtype == 6 || m_tabtype == 7) {
        // この大分類には追加データがない。
    } else if (m_tabtype == 8) {
        const auto beast_count = arg_f->dword();
        m_beasts.emplace();
        if (beast_count > 0) {
            m_beasts->reserve(static_cast<std::size_t>(beast_count));
        }
        // Pythonのrangeと同じく、負数の場合は0件として扱う。
        for (::cw::lib::Integer index = 0; index < beast_count; ++index) {
            auto card = std::make_shared<::cw::binary::beast::BeastCard>();
            card->initialize(
                std::optional<::cw::lib::Value>(::cw::binary::card_common::self_value(*this)),
                arg_f, false, false, "Material", true, true);
            m_beasts->push_back(std::move(card));
        }
    } else {
        throw std::invalid_argument(m_fpath);
    }
}

// EffectMotion はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
std::shared_ptr<::cw::data::CWPyElement> EffectMotion::get_data()
{
    if (m_data && *m_data) {
        return *m_data;
    }

    auto data = ::cw::data::make_element("Motion");
    data->set("type", conv_effectmotion_type(m_tabtype, m_type));
    data->set("element", conv_effectmotion_element(m_element));
    for (const auto& key : m_properties.insertion_keys()) {
        data->set(key, property_text(m_properties.at(key)));
    }

    if (m_beasts && !m_beasts->empty()) {
        auto beasts = ::cw::data::make_element("Beasts");
        for (const auto& card : *m_beasts) {
            if (!card) {
                throw std::invalid_argument("null BeastCard in EffectMotion");
            }
            beasts->append(card->get_data());
        }
        data->append(beasts);
    }

    m_data = data;
    return data;
}

void EffectMotion::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                          std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_f || !arg_data) {
        throw std::invalid_argument("EffectMotion::unconv requires file and data");
    }

    const auto [tabtype, motion_type] =
        ::cw::binary::base::CWBinaryBase::unconv_effectmotion_type(
            arg_data->get("type", ""), arg_f);
    const auto element = ::cw::binary::base::CWBinaryBase::unconv_effectmotion_element(
        arg_data->get("element", ""));

    arg_f->write_byte(tabtype);
    arg_f->write_byte(8);
    arg_f->write_byte(5);
    arg_f->write_byte(0);
    arg_f->write_byte(0);
    arg_f->write_byte(0);
    arg_f->write_byte(element);
    if (tabtype != 8) {
        arg_f->write_byte(motion_type);
    }

    if (tabtype == 0 || tabtype == 1) {
        arg_f->write_byte(::cw::binary::base::CWBinaryBase::unconv_effectmotion_damagetype(
            arg_data->get("damagetype", "")));
        arg_f->write_dword(parse_integer(arg_data->get("value", "0")));
    } else if (tabtype == 3 || tabtype == 4) {
        arg_f->write_dword(parse_integer(arg_data->get("duration", "10")));
    } else if (tabtype == 5) {
        arg_f->write_dword(parse_integer(arg_data->get("value", "0")));
        arg_f->write_dword(parse_integer(arg_data->get("duration", "10")));
    } else if (tabtype == 2) {
        const auto damage_type = arg_data->get("damagetype", "Max");
        if (damage_type != "" && damage_type != "Max") {
            arg_f->check_wsnversion("1", "技能使用回数回復・喪失量の指定");
        }
    } else if (tabtype == 6 || tabtype == 7) {
        // 追加データなし。
    } else if (tabtype == 8) {
        const auto beasts = arg_data->find("Beasts");
        if (!beasts || !*beasts) {
            arg_f->write_dword(0);
        } else {
            arg_f->write_dword(static_cast<::cw::lib::Integer>((*beasts)->m_children.size()));
            for (const auto& card : (*beasts)->m_children) {
                ::cw::binary::beast::BeastCard::unconv(arg_f, card, false);
            }
        }
    } else {
        throw std::invalid_argument("invalid effect motion tabtype: " + std::to_string(tabtype));
    }
}

void main()
{
}

}  // namespace cw::binary::effectmotion
