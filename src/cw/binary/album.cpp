#include "cw/binary/album.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/coupon.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"
#include "cw/yadodb.hpp"

#include <cctype>
#include <stdexcept>

namespace cw::binary::album {
namespace {

using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

ElementPtr element(String tag, String text = {}) {
    return ::cw::data::make_element(std::move(tag), std::move(text));
}

Integer integer(const String& text, const String& field) {
    try {
        // Python版と同様、全プロパティを解析してから書く。 ネイティブのInteger
        // の範囲外でも、書き込み時の失敗と、それ以前の出力バイトを維持する。
        return ::cw::lib::require_python_integer_for_cardwirth_pack(text);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid integer for " + field + ": " + text);
    }
}

String remove_first_marker(String text) {
    const auto position = text.find("TEXT\\n");
    if (position != String::npos) text.erase(position, 6);
    return text;
}

std::shared_ptr<::cw::binary::base::CWBinaryBase> parent_from_value(const ::cw::lib::Value& value) {
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object);
    }
    return nullptr;
}

std::shared_ptr<::cw::binary::base::CWBinaryBase> self_reference(Album& value) {
    try { return value.shared_from_this(); }
    catch (const std::bad_weak_ptr&) { return nullptr; }
}

}  // namespace

void Album::initialize(::cw::lib::Value arg_parent,
                       std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> arg_f,
                       bool arg_yadodata) {
    std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> parent;
    if (auto value = parent_from_value(arg_parent)) parent = value;
    CWBinaryBase::initialize(parent, arg_f, arg_yadodata);
    m_xmltype = "Album";
    m_type = 4;
    m_fname = get_fname();
    m_level = m_dex = m_agl = m_int = m_str = m_vit = m_min = 0;
    m_aggressive = m_cheerful = m_brave = m_cautious = m_trickish = 0;
    m_avoid = m_resist = m_defense = 0;
    m_description.clear(); m_imgpath.clear(); m_coupons.clear(); m_data.reset();

    if (!arg_f || !*arg_f) {
        m_name.clear(); m_image.reset();
        return;
    }
    const auto& file = *arg_f;
    (void)file->byte(); (void)file->byte();
    m_name = file->string(); m_image = file->image();
    m_level = file->word();
    (void)file->word(); (void)file->word(); (void)file->word();
    m_dex = file->byte(); (void)file->byte();
    m_agl = file->byte(); (void)file->byte();
    m_int = file->byte(); (void)file->byte();
    m_str = file->byte(); (void)file->byte();
    m_vit = file->byte(); (void)file->byte();
    m_min = file->byte(); (void)file->byte();
    m_aggressive = file->byte(); (void)file->byte();
    m_cheerful = file->byte(); (void)file->byte();
    m_brave = file->byte(); (void)file->byte();
    m_cautious = file->byte(); (void)file->byte();
    m_trickish = file->byte(); (void)file->byte();
    m_avoid = file->byte(); (void)file->byte();
    m_resist = file->byte(); (void)file->byte();
    m_defense = file->byte(); (void)file->byte();
    (void)file->dword();
    m_description = remove_first_marker(file->string(true));
    const auto coupon_count = file->dword();
    const auto self = self_reference(*this);
    for (Integer index = 0; index < coupon_count; ++index) {
        auto coupon = std::make_shared<::cw::binary::coupon::Coupon>();
        coupon->initialize(self, file, false, 5);
        m_coupons.push_back(std::move(coupon));
    }
}

// Album はローカルの直列化が成功してから m_data を公開する。 Python
// 版の早い self.data 代入とは異なり、後の変換失敗で不完全なツ
// リーをキャッシュし、 次の get_data が再利用することを防ぐ。
std::shared_ptr<Element> Album::get_data() {
    if (m_data && *m_data) return *m_data;
    m_imgpath = m_image && !m_image->empty() ? export_image() : "";
    auto data = element("Album");
    auto property = element("Property");
    property->append(element("Name", m_name));
    property->append(element("ImagePath", m_imgpath));
    property->append(element("Description", m_description));
    property->append(element("Level", std::to_string(m_level)));
    auto ability = element("Ability");
    auto physical = element("Physical");
    physical->set("dex", std::to_string(m_dex)); physical->set("agl", std::to_string(m_agl));
    physical->set("int", std::to_string(m_int)); physical->set("str", std::to_string(m_str));
    physical->set("vit", std::to_string(m_vit)); physical->set("min", std::to_string(m_min));
    ability->append(physical);
    auto mental = element("Mental");
    mental->set("aggressive", std::to_string(m_aggressive)); mental->set("cheerful", std::to_string(m_cheerful));
    mental->set("brave", std::to_string(m_brave)); mental->set("cautious", std::to_string(m_cautious));
    mental->set("trickish", std::to_string(m_trickish)); ability->append(mental);
    auto enhance = element("Enhance");
    enhance->set("avoid", std::to_string(m_avoid)); enhance->set("resist", std::to_string(m_resist));
    enhance->set("defense", std::to_string(m_defense)); ability->append(enhance);
    property->append(ability);
    auto coupons = element("Coupons");
    for (const auto& coupon : m_coupons) if (coupon) coupons->append(coupon->get_data());
    property->append(coupons); data->append(property); m_data = data; return data;
}

::cw::lib::String Album::create_xml(::cw::lib::String arg_dpath) {
    const auto path = CWBinaryBase::create_xml(std::move(arg_dpath));
    const auto root = get_root();
    if (root && root->m_yadodb && *root->m_yadodb) {
        (*root->m_yadodb)->insert_adventurer(path, true, false, -1);
    }
    return path;
}

void Album::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file,
                   std::shared_ptr<Element> data) {
    if (!file || !data) throw std::invalid_argument("Album::unconv requires file and data");
    String name, description; std::optional<::cw::lib::Bytes> image; Integer level = 0;
    Integer dex=0, agl=0, inte=0, stre=0, vit=0, mind=0;
    Integer aggressive=0, cheerful=0, brave=0, cautious=0, trickish=0;
    Integer avoid=0, resist=0, defense=0; ElementPtr coupons;
    for (const auto& node : data->m_children) if (node && node->m_tag == "Property") {
        for (const auto& property : node->m_children) if (property) {
            if (property->m_tag == "Name") name = property->m_text;
            else if (property->m_tag == "ImagePath" || property->m_tag == "ImagePaths")
                image = CWBinaryBase::import_image(file, property, true, false, "Center");
            else if (property->m_tag == "Description") description = property->m_text;
            else if (property->m_tag == "Level") level = integer(property->m_text, "Level");
            else if (property->m_tag == "Ability") {
                for (const auto& ability : property->m_children) if (ability) {
                    if (ability->m_tag == "Physical") {
                        dex=integer(ability->get("dex", ""), "dex"); agl=integer(ability->get("agl", ""), "agl");
                        inte=integer(ability->get("int", ""), "int"); stre=integer(ability->get("str", ""), "str");
                        vit=integer(ability->get("vit", ""), "vit"); mind=integer(ability->get("min", ""), "min");
                    } else if (ability->m_tag == "Mental") {
                        aggressive=integer(ability->get("aggressive", ""), "aggressive");
                        cheerful=integer(ability->get("cheerful", ""), "cheerful"); brave=integer(ability->get("brave", ""), "brave");
                        cautious=integer(ability->get("cautious", ""), "cautious"); trickish=integer(ability->get("trickish", ""), "trickish");
                    } else if (ability->m_tag == "Enhance") {
                        avoid=integer(ability->get("avoid", ""), "avoid"); resist=integer(ability->get("resist", ""), "resist");
                        defense=integer(ability->get("defense", ""), "defense");
                    }
                }
            } else if (property->m_tag == "Coupons") coupons = property;
        }
    }
    file->write_byte(0); file->write_byte(0); file->write_string(name); file->write_image(image);
    file->write_word(level); file->write_word(0); file->write_word(0); file->write_word(0);
    for (const auto value : {dex,agl,inte,stre,vit,mind,aggressive,cheerful,brave,cautious,trickish,avoid,resist,defense})
        file->write_word(value);
    file->write_dword(0); file->write_string("TEXT\n" + description, true);
    if (!coupons) file->write_dword(0);
    else {
        file->write_dword(static_cast<Integer>(coupons->m_children.size()));
        for (const auto& coupon : coupons->m_children) ::cw::binary::coupon::Coupon::unconv(file, coupon);
    }
}

void main() {}

}  // namespace cw::binary::album
