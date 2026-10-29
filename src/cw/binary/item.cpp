#include "cw/binary/item.hpp"
#include "cw/binary/card_common.hpp"
#include "cw/cw.hpp"
#include "cw/thread.hpp"
#include "cw/setting.hpp"
#include <algorithm>
#include <stdexcept>

namespace cw::binary::item {
void ItemCard::initialize(std::optional<::cw::lib::Value> arg_parent,
                          std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                          bool arg_yadodata, bool arg_nameonly,
                          ::cw::lib::String arg_materialdir, bool arg_image_export) {
    (void)::cw::binary::card_common::read_common(*this, arg_parent, arg_f, arg_yadodata,
        arg_nameonly, arg_materialdir, arg_image_export, "ItemCard");
    m_data.reset();
    m_nameonly = arg_nameonly;
    if (arg_nameonly) return;
    m_limit = arg_f->dword(); m_limit_max = arg_f->dword(); m_price = arg_f->dword();
    m_enhance_avoid2 = arg_f->dword(); m_enhance_resist2 = arg_f->dword();
    m_enhance_defense2 = arg_f->dword();
    // ネイティブの変換は、ゲーム用メッセージなしでも実行できる。 実行
    // 環境・辞書がない、またはリサイクルキーが空なら、Python版の
    // cw.cwpy.msgs 参照をせず利用不可とする。 空でな
    // いキーが設定されていれば、同じキーコードの所属規則を維持する。
    ::cw::lib::String recycle;
    if (::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage())) {
            if (auto messages = std::dynamic_pointer_cast<::cw::setting::MsgDict>(*object))
                recycle = messages->get("recycle_keycode", "");
        }
    }
    if (m_limit == 0 && (recycle.empty() ||
                        std::find(m_keycodes.begin(), m_keycodes.end(), recycle) == m_keycodes.end()))
        m_limit_max = 0;
}

std::shared_ptr<::cw::data::CWPyElement> ItemCard::get_data() {
    if (m_nameonly) throw std::logic_error("name-only ItemCard has no XML data");
    if (m_data) return m_data;
    auto [root, property] = ::cw::binary::card_common::common_xml(*this, "ItemCard");
    auto use_limit = ::cw::binary::card_common::make_element("UseLimit", std::to_string(m_limit));
    use_limit->set("max", std::to_string(m_limit_max)); property->append(use_limit);
    property->append(::cw::binary::card_common::make_element("Price", std::to_string(m_price)));
    auto owner = ::cw::binary::card_common::make_element("EnhanceOwner");
    owner->set("avoid", std::to_string(m_enhance_avoid2));
    owner->set("resist", std::to_string(m_enhance_resist2));
    owner->set("defense", std::to_string(m_enhance_defense2)); property->append(owner);
    property->append(::cw::binary::card_common::make_element("Hold",
        ::cw::binary::card_common::bool_text(m_hold)));
    root->append(property);
    ::cw::binary::card_common::append_common_children(*this, root);
    m_data = root;
    return m_data;
}

void ItemCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                      std::shared_ptr<::cw::data::CWPyElement> arg_data,
                      bool arg_ownerisadventurer) {
    if (!arg_f || !arg_data) throw std::invalid_argument("ItemCard::unconv requires file/data");
    ::cw::lib::Integer limit = 0, limit_max = 0, price = 0;
    ::cw::lib::Integer avoid = 0, resist = 0, defense = 0;
    auto value = ::cw::binary::card_common::parse_common_xml(
        arg_f, arg_data, arg_ownerisadventurer, [&](const auto& property) {
        if (property->m_tag == "UseLimit") {
            limit = ::cw::binary::card_common::to_pack_integer(property->m_text);
            limit_max = ::cw::binary::card_common::to_pack_integer(property->get("max", ""));
        } else if (property->m_tag == "Price") price = ::cw::binary::card_common::to_pack_integer(property->m_text);
        else if (property->m_tag == "EnhanceOwner") {
            avoid = ::cw::binary::card_common::to_pack_integer(property->get("avoid", ""));
            resist = ::cw::binary::card_common::to_pack_integer(property->get("resist", ""));
            defense = ::cw::binary::card_common::to_pack_integer(property->get("defense", ""));
        }
    });
    ::cw::binary::card_common::write_common(arg_f, value, 5);
    arg_f->write_dword(limit); arg_f->write_dword(limit_max); arg_f->write_dword(price);
    arg_f->write_dword(avoid); arg_f->write_dword(resist); arg_f->write_dword(defense);
}

void main() {}
}  // namespace cw::binary::item
