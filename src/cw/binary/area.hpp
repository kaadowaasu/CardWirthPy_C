#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::area {

class MenuCard;

class Area : public ::cw::binary::base::CWBinaryBase {
public:
    Area() = default;
    ~Area() override = default;
    ::cw::lib::Integer m_type{};
    ::cw::lib::Integer m_spreadtype{};
    bool m_nameonly{};
    std::vector<std::shared_ptr<::cw::binary::event::Event>> m_events{};
    std::vector<std::shared_ptr<MenuCard>> m_mcards{};
    std::vector<std::shared_ptr<::cw::binary::bgimage::BgImage>> m_bgimgs{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    void initialize(::cw::lib::Value arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false, bool arg_nameonly = false,
                    ::cw::lib::String arg_materialdir = "Material",
                    bool arg_image_export = true);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class MenuCard : public ::cw::binary::base::CWBinaryBase {
public:
    MenuCard() = default;
    ~MenuCard() override = default;
    ::cw::lib::String m_description{}, m_flag{}, m_imgpath{};
    ::cw::lib::Integer m_scale{}, m_left{}, m_top{};
    std::vector<std::shared_ptr<::cw::binary::event::Event>> m_events{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    void initialize(std::shared_ptr<Area> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false,
                    ::cw::lib::Integer arg_dataversion = 4);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();

}  // namespace cw::binary::area
