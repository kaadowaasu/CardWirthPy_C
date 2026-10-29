#pragma once

// cw/binary/content.py の型付き移植。
#include "cw/lib/compat.hpp"
#include "cw/lib/xml_attributes.hpp"
#include "cw/fwd.hpp"
#include "cw/binary/base.hpp"

namespace cw::binary::content {
class Content : public ::cw::binary::base::CWBinaryBase {
public:
    Content() = default;
    virtual ~Content() = default;
    std::vector<std::shared_ptr<::cw::binary::bgimage::BgImage>> m_bgimgs{};
    std::set<::cw::lib::String> m_castranges{};
    std::vector<std::shared_ptr<::cw::binary::content::Content>> m_children{};
    std::vector<std::shared_ptr<::cw::binary::coupon::Coupon>> m_coupons{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    std::vector<std::shared_ptr<::cw::binary::dialog::Dialog>> m_dialogs{};
    std::vector<std::shared_ptr<::cw::binary::effectmotion::EffectMotion>> m_motions{};
    ::cw::lib::InsertionOrderedStringMap<::cw::lib::Value> m_properties{};
    ::cw::lib::String m_tag{};
    ::cw::lib::String m_text{};
    ::cw::lib::String m_type{};
    ::cw::lib::Integer m_version{};
    void initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, ::cw::lib::Integer arg_stratum, bool arg_read);
    void _read_properties(std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f, ::cw::lib::String arg_tag, ::cw::lib::String arg_ctype, ::cw::lib::String arg_name, ::cw::lib::Integer arg_version);
    std::shared_ptr<::cw::data::CWPyElement> get_data();
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data);
    static void _unconv_header(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data);
    static void _unconv_properties(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();
}

