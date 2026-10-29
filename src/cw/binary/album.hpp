#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::album {

class Album : public ::cw::binary::base::CWBinaryBase {
public:
    Album() = default;
    ~Album() override = default;

    ::cw::lib::Integer m_type{4};
    ::cw::lib::String m_fname{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_dex{}, m_agl{}, m_int{}, m_str{}, m_vit{}, m_min{};
    ::cw::lib::Integer m_aggressive{}, m_cheerful{}, m_brave{}, m_cautious{}, m_trickish{};
    ::cw::lib::Integer m_avoid{}, m_resist{}, m_defense{};
    ::cw::lib::String m_description{};
    ::cw::lib::String m_imgpath{};
    std::vector<std::shared_ptr<::cw::binary::coupon::Coupon>> m_coupons{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};

    void initialize(::cw::lib::Value arg_parent,
                    std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> arg_f,
                    bool arg_yadodata = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    ::cw::lib::String create_xml(::cw::lib::String arg_dpath);
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();

}  // namespace cw::binary::album
