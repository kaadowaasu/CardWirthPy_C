#include "cw/binary/environment.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/skill.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/beast.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/yadodb.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace cw::binary::environment {
using I = ::cw::lib::Integer;
using S = ::cw::lib::String;
using E = std::shared_ptr<::cw::data::CWPyElement>;

namespace {
E child(const E& element, const S& tag) {
    if (element) for (const auto& value : element->m_children)
        if (value && value->m_tag == tag) return value;
    return {};
}

I strict_int(const S& value) {
    return ::cw::lib::require_python_integer(value);
}

S splitext_stem(const S& filename) {
    // Python版と同様、cw.util.splitext(fname)[0] をそのま
    // ま書く。 相対要素を正規化して除いたり、ドットだけの名前を独自に解釈したりしない。
    return std::get<0>(::cw::util::splitext(filename));
}

I python_round(double value) {
    const auto lower = std::floor(value);
    const auto fraction = value - lower;
    if (fraction < 0.5) return static_cast<I>(lower);
    if (fraction > 0.5) return static_cast<I>(lower + 1.0);
    const auto integral = static_cast<I>(lower);
    return integral % 2 == 0 ? integral : integral + 1;
}
}

void Environment::initialize(::cw::lib::Value, std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                             bool yadodata, bool versiononly) {
    if (!file) throw std::invalid_argument("Environment requires file");
    ::cw::binary::base::CWBinaryBase::initialize(std::nullopt, file, yadodata);
    m_xmltype = "Environment";
    m_versiononly = versiononly;
    std::shared_ptr<Environment> self;
    try {
        self = std::static_pointer_cast<Environment>(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // スタック上に作られた解析オブジェクトは、共有所有の親を提供できない。
    }
    m_data.reset();
    m_unusedcards.clear();
    m_yadocards.clear();
    m_errorcards.clear();
    m_type = -1;
    m_dataversion_int = 0;
    m_yadotype = 0;
    m_drawcard_speed = 0;
    m_drawbg_speed = 0;
    m_message_speed = 0;
    m_play_bgm = false;
    m_play_sound = false;
    m_correct_scaledown = false;
    m_correct_scaleup = false;
    m_autoselect_party = false;
    m_clickcancel = false;
    m_effect_getmoney = false;
    m_clickjump = false;
    m_keep_levelmax = false;
    m_bgeffectatselmode = true;
    m_viewtype_poster = 0;
    m_bgcolor_message = 0;
    m_use_decofont = false;
    m_changetype_bg = 0;
    m_money = 0;
    m_dataversion.clear();
    m_cwpypartyname.clear();
    m_skintype.clear();
    m_skinname.clear();
    m_compstamps.clear();
    m_scenarioname.clear();
    m_gossips.clear();
    m_partyname.clear();

    const auto parent = std::filesystem::u8path(file->m_filename).parent_path();
    m_name = ::cw::lib::encoding::path_to_utf8(parent.filename());
    m_dataversion = file->string();
    static const S prefix = "DATAVERSION_";
    if (m_dataversion.rfind(prefix, 0) == 0) {
        const auto parsed = ::cw::lib::PythonInteger::parse(m_dataversion.substr(prefix.size()));
        if (!parsed) throw std::invalid_argument("invalid literal for int()");
        // Python版と同様、CardWirthNextの検査前に、整数の値全体を保存する。
        m_dataversion_int = *parsed;
    }
    if (m_dataversion_int >= 13) throw std::invalid_argument("CardWirthNext Environment");
    if (versiononly) return;

    m_yadotype = file->byte();
    m_drawcard_speed = file->dword();
    m_drawbg_speed = file->dword();
    m_message_speed = file->dword();
    m_play_bgm = file->boolean();
    m_play_sound = file->boolean();
    if (m_dataversion_int >= 10) {
        m_correct_scaledown = file->boolean();
        m_correct_scaleup = file->boolean();
    } else {
        (void)file->boolean();
        (void)file->boolean();
    }
    m_autoselect_party = file->boolean();
    m_clickcancel = file->boolean();
    if (m_dataversion_int >= 10) {
        m_effect_getmoney = file->boolean();
        m_clickjump = file->boolean();
        m_keep_levelmax = file->boolean();
    }
    m_bgeffectatselmode = m_dataversion_int >= 11 ? file->boolean() : true;
    m_viewtype_poster = file->byte();
    m_bgcolor_message = file->dword();
    m_use_decofont = file->boolean();
    m_changetype_bg = file->byte();
    m_compstamps = file->string(true);
    m_scenarioname = file->string();
    m_gossips = file->string(true);
    if (m_dataversion_int >= 10) {
        auto count = file->dword();
        for (I i = 0; i < count; ++i) {
            auto card = std::make_shared<UnusedCard>();
            card->initialize(self, file, false);
            m_unusedcards.push_back(card);
        }
        count = file->dword();
        for (I i = 0; i < count; ++i) {
            auto card = std::make_shared<YadoCard>();
            card->initialize(self, file, false);
            m_yadocards.push_back(card);
        }
        m_money = file->dword();
    }
    m_partyname = file->string();
    m_cwpypartyname.clear();
    m_skintype.clear();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) m_skinname = ::cw::g_cwpy->m_setting->m_skindirname;
}

E Environment::get_data() {
    if (m_versiononly) throw std::logic_error("version-only Environment has no XML data");
    if (m_data && *m_data) return *m_data;
    auto data = ::cw::data::make_element("Environment");
    auto property = ::cw::data::make_element("Property");
    property->append(::cw::data::make_element("Name", m_name));
    property->append(::cw::data::make_element("Type", m_skintype));
    property->append(::cw::data::make_element("Skin", m_skinname));
    property->append(::cw::data::make_element("Cashbox", std::to_string(m_money)));
    property->append(::cw::data::make_element("NowSelectingParty", m_cwpypartyname));
    data->append(property);
    auto stamps = ::cw::data::make_element("CompleteStamps");
    for (const auto& value : ::cw::util::decodetextlist(m_compstamps))
        if (!value.empty()) stamps->append(::cw::data::make_element("CompleteStamp", value));
    data->append(stamps);
    auto gossips = ::cw::data::make_element("Gossips");
    for (const auto& value : ::cw::util::decodetextlist(m_gossips))
        if (!value.empty()) gossips->append(::cw::data::make_element("Gossip", value));
    data->append(gossips);
    m_data = data;
    m_errorcards.clear();
    for (I i = 0; i < static_cast<I>(m_unusedcards.size()); ++i) {
        const auto& card = m_unusedcards[static_cast<std::size_t>(i)];
        if (!card || !card->m_data) {
            if (card) m_errorcards.push_back(card);
            continue;
        }
        try {
            card->create_xml2(get_dir(), i);
        } catch (...) {
            ::cw::util::print_ex(std::nullopt);
            m_errorcards.push_back(card);
        }
    }
    return data;
}

std::map<S, I> Environment::get_cardtypedict() {
    std::map<S, I> result;
    for (const auto& card : m_yadocards) if (card) result[card->m_fname] = card->m_type;
    return result;
}

I Environment_unconv_roundval(I value) {
    return python_round((static_cast<double>(value) - 5.0) / 10.0 * 8.0) + 4;
}

void Environment::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data,
                          std::map<S, S> party,
                          std::vector<std::tuple<S, E>> unused,
                          std::vector<std::tuple<S, std::tuple<S, E>>> yado) {
    if (!file || !data) throw std::invalid_argument("Environment::unconv null");
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
        throw std::logic_error("Environment conversion requires CWPy setting");
    const auto setting = ::cw::g_cwpy->m_setting;
    // Python版と同様、XMLプロパティの走査より先に設定を評価す
    // る。 設定とプロパティの両方が不正でも、最初の失敗を維持する。
    I changetype_bg = 1;
    if (setting->m_transition == "None") changetype_bg = 0;
    else if (setting->m_transition == "Blinds") changetype_bg = 1;
    else if (setting->m_transition == "Fade") changetype_bg = 2;
    else if (setting->m_transition == "PixelDissolve") changetype_bg = 3;
    const auto drawcard_speed = Environment_unconv_roundval(setting->get_dealspeed(false));
    const auto drawbg_speed = Environment_unconv_roundval(setting->m_transitionspeed);
    const auto message_speed = Environment_unconv_roundval(setting->m_messagespeed);
    I money = 0;
    S party_name, compstamps, gossips;
    for (const auto& element : data->m_children) {
        if (!element) continue;
        if (element->m_tag == "Property") {
            for (const auto& property : element->m_children) {
                if (!property) continue;
                if (property->m_tag == "Cashbox") {
                    // Python版と同様、任意精度整数を範囲内へ制限してから固定幅へ書く
                    // 。 有効な大きいCashboxを、固定幅の解析時点で失敗させない。
                    money = ::cw::lib::require_clamped_python_nonnegative_integer(property->m_text, 999999);
                } else if (property->m_tag == "NowSelectingParty") {
                    const auto found = party.find(property->m_text);
                    // Pythonの party.get(text, "") と同様、後の要
                    // 素が存在しないパーティを指す場合、 以前の有効な選択も空へ置き換える。
                    party_name = found != party.end() ? found->second : S{};
                }
            }
        } else if (element->m_tag == "CompleteStamps") {
            std::vector<S> values;
            for (const auto& child_element : element->m_children)
                if (child_element && !child_element->m_text.empty()) values.push_back(child_element->m_text);
            compstamps = ::cw::util::encodetextlist(values);
        } else if (element->m_tag == "Gossips") {
            std::vector<S> values;
            for (const auto& child_element : element->m_children)
                if (child_element && !child_element->m_text.empty()) values.push_back(child_element->m_text);
            gossips = ::cw::util::encodetextlist(values);
        }
    }
    file->write_string("DATAVERSION_10");
    file->write_byte(1);
    file->write_dword(drawcard_speed);
    file->write_dword(drawbg_speed);
    file->write_dword(message_speed);
    file->write_bool(true);  // play_bgm
    file->write_bool(true);  // play_sound
    file->write_bool(true);  // correct_scaledown
    file->write_bool(true);  // correct_scaleup
    file->write_bool(true);  // autoselect_party
    file->write_bool(true);  // clickcancel
    file->write_bool(true);  // effect_getmoney
    file->write_bool(true);  // clickjump
    file->write_bool(true);  // keep_levelmax
    file->write_byte(1);
    file->write_dword(3);
    file->write_bool(false);
    file->write_byte(changetype_bg);
    file->write_string(compstamps, true);
    file->write_string("");
    file->write_string(gossips, true);
    file->write_dword(static_cast<I>(unused.size()));
    for (const auto& [filename, card] : unused) UnusedCard::unconv(file, card, filename);
    file->write_dword(static_cast<I>(yado.size()));
    for (const auto& [filename, card] : yado)
        YadoCard::unconv(file, std::get<1>(card), std::get<0>(card));
    file->write_dword(money);
    file->write_string(party_name);
}

void UnusedCard::initialize(std::optional<std::shared_ptr<Environment>> parent,
                            std::shared_ptr<::cw::binary::cwfile::CWFile> file, bool yadodata) {
    std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> base_parent;
    if (parent && *parent) base_parent = std::static_pointer_cast<::cw::binary::base::CWBinaryBase>(*parent);
    ::cw::binary::base::CWBinaryBase::initialize(base_parent, file, yadodata);
    m_xmltype = "UnusedCard";
    m_data.reset();
    m_fname.clear();
    m_uselimit = 0;
    if (file) {
        m_fname = file->rawstring();
        m_uselimit = file->dword();
        (void)file->byte();
    }
}

void UnusedCard::set_data(std::shared_ptr<::cw::binary::base::CWBinaryBase> data) {
    m_data = std::move(data);
}

E UnusedCard::get_data() {
    if (!m_data) throw std::logic_error("UnusedCard no data");
    return m_data->get_data();
}

S UnusedCard::create_xml(S path) { return create_xml2(std::move(path), -1); }

S UnusedCard::create_xml2(S path, I order) {
    if (!m_data) throw std::logic_error("UnusedCard no data");
    bool applied = false;
    if (auto skill = std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(m_data)) {
        skill->m_limit = m_uselimit; applied = true;
    } else if (auto item = std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(m_data)) {
        item->m_limit = m_uselimit; applied = true;
    } else if (auto beast = std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(m_data)) {
        beast->m_limit = m_uselimit; applied = true;
    }
    if (!applied) throw std::invalid_argument("UnusedCard data is not a card");
    const auto output = m_data->create_xml(std::move(path));
    const auto root = get_root();
    if (!root) throw std::logic_error("UnusedCard has no root");
    if (root->m_yadodb && *root->m_yadodb) (*root->m_yadodb)->insert_card(output, false, order);
    return output;
}

void UnusedCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data, S filename) {
    if (!file || !data) throw std::invalid_argument("UnusedCard null");
    file->write_rawstring(splitext_stem(filename));
    file->write_dword(data->getint("Property/UseLimit", std::nullopt, I{0}));
    file->write_byte(0);
}

void YadoCard::initialize(std::shared_ptr<Environment> parent,
                          std::shared_ptr<::cw::binary::cwfile::CWFile> file, bool yadodata) {
    if (!file) throw std::invalid_argument("YadoCard file");
    ::cw::binary::base::CWBinaryBase::initialize(parent, file, yadodata);
    m_xmltype = "YadoCard";
    m_description.clear();
    m_fname.clear();
    m_type = 0;
    m_number = 0;
    (void)file->byte();
    (void)file->byte();
    m_name = file->string();
    m_description = file->string();
    m_type = file->byte();
    m_fname = file->rawstring();
    m_number = file->dword();
}

void YadoCard::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data, S filename) {
    if (!file || !data) throw std::invalid_argument("YadoCard null");
    const auto name = data->gettext("Property/Name", "");
    const auto description = data->gettext("Property/Description", "");
    I type = 0;
    if (data->m_tag == "SkillCard") type = 1;
    else if (data->m_tag == "ItemCard") type = 2;
    else if (data->m_tag == "BeastCard") type = 3;
    else throw std::invalid_argument("YadoCard type");
    file->write_byte(0);
    file->write_byte(0);
    file->write_string(name);
    file->write_string(description, true);
    file->write_byte(type);
    file->write_rawstring(splitext_stem(filename));
    file->write_dword(1);
}

void main() {}
}
