#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::battle {

class EnemyCard;

class Battle : public ::cw::binary::base::CWBinaryBase {
public:
    Battle() = default;
    ~Battle() override = default;
    ::cw::lib::Integer m_type{}, m_spreadtype{};
    ::cw::lib::String m_bgm{};
    bool m_nameonly{};
    std::vector<std::shared_ptr<::cw::binary::event::Event>> m_events{};
    std::vector<std::shared_ptr<EnemyCard>> m_ecards{};
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

class EnemyCard : public ::cw::binary::base::CWBinaryBase {
public:
    EnemyCard() = default;
    ~EnemyCard() override = default;
    ::cw::lib::Integer m_cast_id{}, m_scale{}, m_left{}, m_top{};
    bool m_escape{};
    ::cw::lib::String m_flag{};
    std::vector<std::shared_ptr<::cw::binary::event::Event>> m_events{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    void initialize(std::shared_ptr<Battle> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void main();
}  // namespace cw::binary::battle
