#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::item {
class ItemCard : public ::cw::binary::base::CWBinaryBase {
public:
    ItemCard() = default;
    virtual ~ItemCard() = default;
    ::cw::lib::Integer m_type{};
    ::cw::lib::String m_imgpath{}, m_fname{}, m_description{};
    ::cw::lib::Integer m_p_ability{}, m_m_ability{};
    bool m_silence{}, m_target_all{};
    ::cw::lib::Integer m_target{}, m_effect_type{}, m_resist_type{}, m_success_rate{}, m_visual_effect{};
    std::vector<std::shared_ptr<::cw::binary::effectmotion::EffectMotion>> m_motions{};
    ::cw::lib::Integer m_enhance_avoid{}, m_enhance_resist{}, m_enhance_defense{};
    ::cw::lib::String m_sound_effect{}, m_sound_effect2{};
    std::vector<::cw::lib::String> m_keycodes{};
    ::cw::lib::Integer m_premium{};
    ::cw::lib::String m_scenario_name{}, m_scenario_author{};
    std::vector<std::shared_ptr<::cw::binary::event::SimpleEvent>> m_events{};
    bool m_hold{};
    ::cw::lib::Integer m_limit{}, m_limit_max{}, m_price{};
    ::cw::lib::Integer m_enhance_avoid2{}, m_enhance_resist2{}, m_enhance_defense2{};
    bool m_nameonly{};
    std::shared_ptr<::cw::data::CWPyElement> m_data{};
    void initialize(std::optional<::cw::lib::Value> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false, bool arg_nameonly = false,
                    ::cw::lib::String arg_materialdir = "Material", bool arg_image_export = true);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       bool arg_ownerisadventurer);
};
void main();
}  // namespace cw::binary::item
