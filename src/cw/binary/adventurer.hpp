#pragma once

#include "cw/binary/base.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::adventurer {

class Adventurer : public ::cw::binary::base::CWBinaryBase {
public:
    Adventurer() = default;
    Adventurer(std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
               std::shared_ptr<::cw::binary::cwfile::CWFile> file,
               bool yadodata = false, bool nameonly = false, bool album120 = false);
    virtual ~Adventurer() = default;

    ::cw::lib::String m_imgpath{};
    ::cw::lib::String m_description{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_money{};
    ::cw::lib::Integer m_life{};
    ::cw::lib::Integer m_maxlife{};

    bool m_noeffect_weapon{};
    bool m_noeffect_magic{};
    bool m_undead{};
    bool m_automaton{};
    bool m_unholy{};
    bool m_constructure{};
    bool m_resist_fire{};
    bool m_resist_ice{};
    bool m_weakness_fire{};
    bool m_weakness_ice{};

    ::cw::lib::Integer m_avoid{};
    ::cw::lib::Integer m_resist{};
    ::cw::lib::Integer m_defense{};
    ::cw::lib::Integer m_dex{};
    ::cw::lib::Integer m_agl{};
    ::cw::lib::Integer m_int{};
    ::cw::lib::Integer m_str{};
    ::cw::lib::Integer m_vit{};
    ::cw::lib::Integer m_min{};
    ::cw::lib::Integer m_aggressive{};
    ::cw::lib::Integer m_cheerful{};
    ::cw::lib::Integer m_brave{};
    ::cw::lib::Integer m_cautious{};
    ::cw::lib::Integer m_trickish{};

    ::cw::lib::Integer m_mentality{};
    ::cw::lib::Integer m_duration_mentality{};
    ::cw::lib::Integer m_paralyze{};
    ::cw::lib::Integer m_poison{};
    ::cw::lib::Integer m_duration_bind{};
    ::cw::lib::Integer m_duration_silence{};
    ::cw::lib::Integer m_duration_faceup{};
    ::cw::lib::Integer m_duration_antimagic{};
    ::cw::lib::Integer m_enhance_action{};
    ::cw::lib::Integer m_duration_enhance_action{};
    ::cw::lib::Integer m_enhance_avoid{};
    ::cw::lib::Integer m_duration_enhance_avoid{};
    ::cw::lib::Integer m_enhance_resist{};
    ::cw::lib::Integer m_duration_enhance_resist{};
    ::cw::lib::Integer m_enhance_defense{};
    ::cw::lib::Integer m_duration_enhance_defense{};

    bool m_is_dead{};
    std::vector<std::shared_ptr<::cw::binary::item::ItemCard>> m_items{};
    std::vector<std::shared_ptr<::cw::binary::skill::SkillCard>> m_skills{};
    std::vector<std::shared_ptr<::cw::binary::beast::BeastCard>> m_beasts{};
    std::vector<std::shared_ptr<::cw::binary::coupon::Coupon>> m_coupons{};
    std::shared_ptr<::cw::data::CWPyElement> m_data{};
    std::shared_ptr<::cw::data::CWPyElement> m_f9data{};
    bool m_nameonly{};

    void initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false, bool arg_nameonly = false,
                    bool arg_album120 = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    std::shared_ptr<::cw::data::CWPyElement> get_f9data();
    std::shared_ptr<::cw::data::CWPyElement> _get_data(bool arg_f9data);
    ::cw::lib::String export_image();
    ::cw::lib::String create_xml(::cw::lib::String arg_dpath);
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       std::shared_ptr<::cw::data::CWPyElement> arg_logdata = nullptr);

private:
    void add_128coupons();
};

class AdventurerCard : public ::cw::binary::base::CWBinaryBase {
public:
    AdventurerCard() = default;
    AdventurerCard(std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                   bool yadodata = false);
    virtual ~AdventurerCard() = default;
    std::shared_ptr<Adventurer> m_adventurer{};
    ::cw::lib::String m_fname{};
    ::cw::lib::Integer m_type{1};

    void initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false);
    void set_image(::cw::lib::Bytes arg_image);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    ::cw::lib::String create_xml(::cw::lib::String arg_dpath);
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class AdventurerWithImage : public ::cw::binary::base::CWBinaryBase {
public:
    AdventurerWithImage() = default;
    AdventurerWithImage(std::shared_ptr<::cw::binary::base::CWBinaryBase> parent,
                        std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                        bool yadodata = false);
    virtual ~AdventurerWithImage() = default;
    std::shared_ptr<Adventurer> m_adventurer{};

    void initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false);
    std::shared_ptr<::cw::data::CWPyElement> get_data() override;
    std::shared_ptr<::cw::data::CWPyElement> get_f9data();
    ::cw::lib::String create_xml(::cw::lib::String arg_dpath);
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       std::shared_ptr<::cw::data::CWPyElement> arg_logdata = nullptr);
};

class AdventurerHeader : public ::cw::binary::base::CWBinaryBase {
public:
    AdventurerHeader() = default;
    AdventurerHeader(std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                     bool yadodata = false, ::cw::lib::Integer dataversion = 10);
    virtual ~AdventurerHeader() = default;
    ::cw::lib::Integer m_type{};
    ::cw::lib::String m_fname{};
    ::cw::lib::Integer m_level{};
    std::vector<::cw::lib::String> m_coupons{};
    ::cw::lib::Integer m_ep{};
    ::cw::lib::Integer m_dex{};
    ::cw::lib::Integer m_agl{};
    ::cw::lib::Integer m_int{};
    ::cw::lib::Integer m_str{};
    ::cw::lib::Integer m_vit{};
    ::cw::lib::Integer m_min{};

    void initialize(std::shared_ptr<::cw::binary::base::CWBinaryBase> arg_parent,
                    std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                    bool arg_yadodata = false,
                    ::cw::lib::Integer arg_dataversion = 10);
    static void unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                       std::shared_ptr<::cw::data::CWPyElement> arg_data,
                       ::cw::lib::String arg_fname);
};

void main();

}  // namespace cw::binary::adventurer
