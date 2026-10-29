// cw/features.py の C++ への段階的な移植。
#include "cw/features.hpp"
#include "cw/data.hpp"
#include "cw/debug/charaedit.hpp"
#include "cw/dialog/create.hpp"

#include <stdexcept>

namespace cw::features {
namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;

template<class T>
void apply_feature(const Feature& feature, T& data, Real sign,
                   bool physical, bool mental) {
    if (physical) {
        data.m_dex += sign * feature.m_dexbonus;
        data.m_agl += sign * feature.m_aglbonus;
        data.m_py_int += sign * feature.m_intbonus;
        data.m_str += sign * feature.m_strbonus;
        data.m_vit += sign * feature.m_vitbonus;
        data.m_min += sign * feature.m_minbonus;
    }
    if (mental) {
        data.m_aggressive += sign * feature.m_aggressive;
        data.m_cheerful += sign * feature.m_cheerful;
        data.m_brave += sign * feature.m_brave;
        data.m_cautious += sign * feature.m_cautious;
        data.m_trickish += sign * feature.m_trickish;
    }
}

Real numwrap(Real value, Real minimum, Real maximum) {
    // Pythonのcw.util.numwrapと同じく、下限判定を上限判定より先に行う。
    if (value < minimum) return minimum;
    if (maximum < value) return maximum;
    return value;
}

template<class T>
void wrap_ability_impl(T& data) {
    data.m_dex = numwrap(data.m_dex, 1, static_cast<Real>(data.m_maxdex));
    data.m_agl = numwrap(data.m_agl, 1, static_cast<Real>(data.m_maxagl));
    data.m_py_int = numwrap(data.m_py_int, 1, static_cast<Real>(data.m_maxint));
    data.m_str = numwrap(data.m_str, 1, static_cast<Real>(data.m_maxstr));
    data.m_vit = numwrap(data.m_vit, 1, static_cast<Real>(data.m_maxvit));
    data.m_min = numwrap(data.m_min, 1, static_cast<Real>(data.m_maxmin));
    // Pythonのint(float)は0方向への切り捨て。
    data.m_aggressive = static_cast<Real>(static_cast<Integer>(numwrap(data.m_aggressive, -4, 4)));
    data.m_cheerful = static_cast<Real>(static_cast<Integer>(numwrap(data.m_cheerful, -4, 4)));
    data.m_brave = static_cast<Real>(static_cast<Integer>(numwrap(data.m_brave, -4, 4)));
    data.m_cautious = static_cast<Real>(static_cast<Integer>(numwrap(data.m_cautious, -4, 4)));
    data.m_trickish = static_cast<Real>(static_cast<Integer>(numwrap(data.m_trickish, -4, 4)));
}

String decodewrap(const String& source) {
    String result;
    result.reserve(source.size());
    bool escaped = false;
    for (char ch : source) {
        if (escaped) {
            if (ch == 'n') result.push_back('\n');
            else result.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            result.push_back(ch);
        }
    }
    // Python実装も末尾の単独バックスラッシュを捨てる。
    return result;
}
}

Feature::Feature(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }
Making::Making(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }
Nature::Nature(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }
Period::Period(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }
SampleType::SampleType(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }
Sex::Sex(std::shared_ptr<::cw::data::CWPyElement> arg_data) { initialize(std::move(arg_data)); }

void Feature::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_data) throw std::invalid_argument("Feature requires XML data");
    m_data = std::move(arg_data);
    m_name = m_data->gettext("Name", String{});
    m_dexbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("dex")), Real{0});
    m_aglbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("agl")), Real{0});
    m_intbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("int")), Real{0});
    m_strbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("str")), Real{0});
    m_vitbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("vit")), Real{0});
    m_minbonus = m_data->getfloat("Physical", ::cw::lib::Value(String("min")), Real{0});
    m_aggressive = m_data->getfloat("Mental", ::cw::lib::Value(String("aggressive")), Real{0});
    m_cheerful = m_data->getfloat("Mental", ::cw::lib::Value(String("cheerful")), Real{0});
    m_brave = m_data->getfloat("Mental", ::cw::lib::Value(String("brave")), Real{0});
    m_cautious = m_data->getfloat("Mental", ::cw::lib::Value(String("cautious")), Real{0});
    m_trickish = m_data->getfloat("Mental", ::cw::lib::Value(String("trickish")), Real{0});
}

void Feature::modulate(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data, bool arg_physical, bool arg_mental)
{
    if (!arg_data) throw std::invalid_argument("modulate requires AdventurerData");
    apply_feature(*this, *arg_data, Real{1}, arg_physical, arg_mental);
}

void Feature::modulate(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data, bool arg_physical, bool arg_mental)
{
    if (!arg_data) throw std::invalid_argument("modulate requires CharaInfo");
    apply_feature(*this, *arg_data, Real{1}, arg_physical, arg_mental);
}

void Feature::demodulate(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data, bool arg_physical, bool arg_mental)
{
    if (!arg_data) throw std::invalid_argument("demodulate requires AdventurerData");
    apply_feature(*this, *arg_data, Real{-1}, arg_physical, arg_mental);
}

void Feature::demodulate(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data, bool arg_physical, bool arg_mental)
{
    if (!arg_data) throw std::invalid_argument("demodulate requires CharaInfo");
    apply_feature(*this, *arg_data, Real{-1}, arg_physical, arg_mental);
}

void Making::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    Feature::initialize(std::move(arg_data));
}

void Nature::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    Feature::initialize(std::move(arg_data));
    m_description = m_data->gettext("Description", String{});
    m_special = m_data->getbool(".", ::cw::lib::Value(String("special")), false);
    m_genecount = m_data->getint(".", ::cw::lib::Value(String("geneCount")), Integer{0});
    m_genepattern = m_data->getattr(".", "genePattern", String("0000000000"));
    m_levelmax = m_data->getint(".", ::cw::lib::Value(String("levelMax")), Integer{10});
    m_basenatures.clear();
    {
        const auto parent = m_data->find_exists("BaseNatures");
        for (const auto& element : parent->m_children) {
            if (element) m_basenatures.push_back(element->gettext(".", String{}));
        }
    }
}

void Period::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    Feature::initialize(std::move(arg_data));
    m_subname = m_data->getattr(".", "subName", m_name);
    m_abbr = m_data->getattr(".", "abbr", m_subname);
    m_spendep = m_data->getint(".", ::cw::lib::Value(String("spendEP")), Integer{10});
    m_coupons.clear();
    {
        const auto parent = m_data->find_exists("Coupons");
        for (const auto& element : parent->m_children) {
            if (!element) continue;
            m_coupons.emplace_back(
                element->gettext(".", String{}),
                element->getint(".", ::cw::lib::Value(String("value")), Integer{0}));
        }
    }
    m_firstselect = m_data->getbool(".", ::cw::lib::Value(String("firstSelect")), false);
}

void SampleType::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    Feature::initialize(std::move(arg_data));
    m_description = decodewrap(m_data->gettext("Description", String{}));
}

void Sex::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    Feature::initialize(std::move(arg_data));
    m_subname = m_data->getattr(".", "subName", m_name);
    m_father = m_data->getbool(".", ::cw::lib::Value(String("father")), true);
    m_mother = m_data->getbool(".", ::cw::lib::Value(String("mother")), true);
}

void wrap_ability(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data)
{
    if (!arg_data) throw std::invalid_argument("wrap_ability requires AdventurerData");
    wrap_ability_impl(*arg_data);
}

void wrap_ability(std::shared_ptr<::cw::debug::charaedit::CharaInfo> arg_data)
{
    if (!arg_data) throw std::invalid_argument("wrap_ability requires CharaInfo");
    wrap_ability(*arg_data);
}

void wrap_ability(::cw::debug::charaedit::CharaInfo& arg_data)
{
    wrap_ability_impl(arg_data);
}

}

