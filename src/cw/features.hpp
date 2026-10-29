#pragma once

// cw/features.py の特性データと能力値補正を公開する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::features {
class Feature : public ::cw::lib::Object {
public:
    Feature() = default;
    explicit Feature(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~Feature() = default;
    ::cw::lib::Real m_aggressive{};
    ::cw::lib::Real m_aglbonus{};
    ::cw::lib::Real m_brave{};
    ::cw::lib::Real m_cautious{};
    ::cw::lib::Real m_cheerful{};
    std::shared_ptr<::cw::data::CWPyElement> m_data{};
    ::cw::lib::Real m_dexbonus{};
    ::cw::lib::Real m_intbonus{};
    ::cw::lib::Real m_minbonus{};
    ::cw::lib::String m_name{};
    ::cw::lib::Real m_strbonus{};
    ::cw::lib::Real m_trickish{};
    ::cw::lib::Real m_vitbonus{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void modulate(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data, bool arg_physical=true, bool arg_mental=true);
    void modulate(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data, bool arg_physical=true, bool arg_mental=true);
    void demodulate(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data, bool arg_physical=true, bool arg_mental=true);
    void demodulate(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data, bool arg_physical=true, bool arg_mental=true);
};

class Making : public ::cw::features::Feature {
public:
    Making() = default;
    explicit Making(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~Making() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class Nature : public ::cw::features::Feature {
public:
    Nature() = default;
    explicit Nature(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~Nature() = default;
    std::vector<::cw::lib::String> m_basenatures{};
    ::cw::lib::String m_description{};
    ::cw::lib::Integer m_genecount{};
    ::cw::lib::String m_genepattern{"0000000000"};
    ::cw::lib::Integer m_levelmax{};
    bool m_special{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class Period : public ::cw::features::Feature {
public:
    Period() = default;
    explicit Period(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~Period() = default;
    ::cw::lib::String m_abbr{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> m_coupons{};
    bool m_firstselect{};
    ::cw::lib::Integer m_spendep{};
    ::cw::lib::String m_subname{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class SampleType : public ::cw::features::Feature {
public:
    SampleType() = default;
    explicit SampleType(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~SampleType() = default;
    ::cw::lib::String m_description{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class Sex : public ::cw::features::Feature {
public:
    Sex() = default;
    explicit Sex(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    virtual ~Sex() = default;
    bool m_father{};
    bool m_mother{};
    ::cw::lib::String m_subname{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

void wrap_ability(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data);
void wrap_ability(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data);
void wrap_ability(::cw::debug::charaedit::CharaInfo& arg_data);
}

