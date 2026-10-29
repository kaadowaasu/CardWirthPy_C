#pragma once

#include "cw/binary/base.hpp"
#include "cw/lib/xml_attributes.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::effectmotion {

class EffectMotion : public ::cw::binary::base::CWBinaryBase {
public:
    EffectMotion() = default;
    ~EffectMotion() override = default;

    ::cw::lib::Integer m_tabtype{};
    ::cw::lib::Integer m_element{};
    ::cw::lib::Integer m_type{};
    ::cw::lib::InsertionOrderedStringMap<::cw::lib::Value> m_properties{};
    std::optional<std::vector<std::shared_ptr<::cw::binary::beast::BeastCard>>> m_beasts{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};

    void initialize(::cw::lib::Value arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false,
                    ::cw::lib::Integer arg_dataversion = 4);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();

}  // namespace cw::binary::effectmotion
