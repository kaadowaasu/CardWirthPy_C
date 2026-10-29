#include "cw/binary/beast.hpp"
#include "cw/binary/adventurer.hpp"
#include "cw/binary/card_common.hpp"
#include <stdexcept>

namespace cw::binary::beast {

void BeastCard::initialize(std::optional<::cw::lib::Value> arg_parent,
                           std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                           bool arg_yadodata, bool arg_nameonly,
                           ::cw::lib::String arg_materialdir, bool arg_image_export,
                           bool arg_summoneffect) {
    m_summoneffect = arg_summoneffect;
    const auto version = ::cw::binary::card_common::read_common(*this, arg_parent, arg_f,
        arg_yadodata, arg_nameonly, arg_materialdir, arg_image_export, "BeastCard");
    m_data.reset();
    m_nameonly = arg_nameonly;
    m_limit = 0;
    m_attachment = false;
    m_has_attachment = false;
    if (arg_nameonly) return;
    m_limit = arg_f->dword();
    if (version >= 5) { m_attachment = arg_f->boolean(); m_has_attachment = true; }
    else if (get_root()->is_yadodata()) {
        m_has_attachment = true;
        // Python旧形式: 冒険者所持なら残り回数、それ以外の親なら付帯扱い。
        const auto parent = ::cw::binary::card_common::parent_from_value(arg_parent);
        if (std::dynamic_pointer_cast<::cw::binary::adventurer::Adventurer>(parent))
            m_attachment = m_limit != 0;
        else m_attachment = parent != nullptr;
    } else m_attachment = false;
}

std::shared_ptr<::cw::data::CWPyElement> BeastCard::get_data() {
    if (m_nameonly) throw std::logic_error("name-only BeastCard has no XML data");
    if (m_data) return m_data;
    auto [root, property] = ::cw::binary::card_common::common_xml(*this, "BeastCard");
    property->append(::cw::binary::card_common::make_element("UseLimit", std::to_string(m_limit)));
    if (m_has_attachment) {
        property->append(::cw::binary::card_common::make_element("Attachment",
            ::cw::binary::card_common::bool_text(!m_attachment)));
    }
    root->append(property);
    ::cw::binary::card_common::append_common_children(*this, root);
    m_data = root;
    return m_data;
}

void BeastCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       bool arg_ownerisadventurer) {
    if (!arg_f || !arg_data) throw std::invalid_argument("BeastCard::unconv requires file and data");
    ::cw::lib::Integer limit = 0;
    bool attachment = false;
    auto value = ::cw::binary::card_common::parse_common_xml(
        arg_f, arg_data, arg_ownerisadventurer, [&](const auto& property) {
        if (property->m_tag == "UseLimit") limit = ::cw::binary::card_common::to_pack_integer(property->m_text);
        else if (property->m_tag == "Attachment") attachment = ::cw::binary::card_common::to_bool(property->m_text);
        else if (property->m_tag == "InvocationCondition") {
            if (property->m_children.size() != 1) arg_f->check_wsnversion("3", "発動条件");
            const auto status = property->find("Status");
            if (!status || (*status)->m_text != "Alive") arg_f->check_wsnversion("3", "発動条件");
        } else if (property->m_tag == "RemovalCondition") {
            if (property->m_children.size() != 1) arg_f->check_wsnversion("3", "消滅条件");
            const auto status = property->find("Status");
            if (!status || (*status)->m_text != "Unconscious") arg_f->check_wsnversion("3", "消滅条件");
        } else if (property->m_tag == "ShowStyle" && property->m_text != "Center") {
            arg_f->check_wsnversion("4", "発動時の視覚効果");
        }
    });
    ::cw::binary::card_common::write_common(arg_f, value, 4);
    arg_f->write_dword(limit);
    // CardWirthのバイナリは、Attachmentを反転した真偽値で保存する。
    arg_f->write_bool(!attachment);
}

void main() {}
}  // namespace cw::binary::beast
