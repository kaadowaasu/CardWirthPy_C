#include "cw/binary/skill.hpp"
#include "cw/binary/card_common.hpp"
#include <stdexcept>

namespace cw::binary::skill {

void SkillCard::initialize(std::optional<::cw::lib::Value> arg_parent,
                           std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                           bool arg_yadodata, bool arg_nameonly,
                           ::cw::lib::String arg_materialdir, bool arg_image_export) {
    (void)::cw::binary::card_common::read_common(*this, arg_parent, arg_f, arg_yadodata,
        arg_nameonly, arg_materialdir, arg_image_export, "SkillCard");
    m_data.reset();
    m_nameonly = arg_nameonly;
    m_level = 0;
    m_limit = 0;
    if (arg_nameonly) return;
    m_level = arg_f->dword(); m_limit = arg_f->dword();
}

std::shared_ptr<::cw::data::CWPyElement> SkillCard::get_data() {
    if (m_nameonly) throw std::logic_error("name-only SkillCard has no XML data");
    if (m_data) return m_data;
    auto [root, property] = ::cw::binary::card_common::common_xml(*this, "SkillCard");
    auto level = ::cw::binary::card_common::make_element("Level", std::to_string(m_level));
    property->append(level);
    property->append(::cw::binary::card_common::make_element("UseLimit", std::to_string(m_limit)));
    property->append(::cw::binary::card_common::make_element("Hold",
        ::cw::binary::card_common::bool_text(m_hold)));
    property->remove(level);
    ::cw::lib::Integer ability_index = static_cast<::cw::lib::Integer>(property->m_children.size());
    for (::cw::lib::Integer i = 0; i < static_cast<::cw::lib::Integer>(property->m_children.size()); ++i) {
        if (property->m_children[static_cast<std::size_t>(i)] &&
            property->m_children[static_cast<std::size_t>(i)]->m_tag == "Ability") {
            ability_index = i;
            break;
        }
    }
    property->insert(ability_index, level);
    root->append(property);
    ::cw::binary::card_common::append_common_children(*this, root);
    m_data = root;
    return m_data;
}

void SkillCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       bool arg_ownerisadventurer) {
    if (!arg_f || !arg_data) throw std::invalid_argument("SkillCard::unconv requires file and data");
    ::cw::lib::Integer level = 0, limit = 0;
    auto value = ::cw::binary::card_common::parse_common_xml(
        arg_f, arg_data, arg_ownerisadventurer, [&](const auto& property) {
        if (property->m_tag == "Level") level = ::cw::binary::card_common::to_pack_integer(property->m_text);
        else if (property->m_tag == "UseLimit") limit = ::cw::binary::card_common::to_pack_integer(property->m_text);
    });
    ::cw::binary::card_common::write_common(arg_f, value, 4);
    arg_f->write_dword(level); arg_f->write_dword(limit);
}

void main() {}
}  // namespace cw::binary::skill
