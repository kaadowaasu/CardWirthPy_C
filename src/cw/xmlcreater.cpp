#include "cw/xmlcreater.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/binary/xmltemplate.hpp"
#include "cw/binary/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/dialog/create.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "platform/storage/storage.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#pragma warning(disable: 4996) // u8pathにより、WindowsでもPythonと同じUTF-8パスの扱いを保つ。
#endif

namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;
using ::cw::lib::Decimal;
using E = std::shared_ptr<::cw::data::CWPyElement>;

String value_string(const ::cw::lib::Value& v, const String& fallback = {}) {
    if (const auto* p = std::get_if<String>(&v.storage())) return *p;
    // XMLのテキストには値を正確に保存する。ここではDecimalの小
    // 数桁数を保持し、精度と末尾の0を失うReal経由の変換は行わない。
    if (const auto* p = std::get_if<Decimal>(&v.storage())) return p->to_string(true);
    if (const auto* p = std::get_if<Integer>(&v.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<Real>(&v.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<bool>(&v.storage())) return *p ? "True" : "False";
    return fallback;
}

std::map<String, String> color_attrs(const ::cw::lib::Value& value) {
    std::map<String, String> attrs;
    if (const auto* bytes = std::get_if<::cw::lib::Bytes>(&value.storage()); bytes && bytes->size() >= 3) {
        attrs["red"] = std::to_string((*bytes)[0]);
        attrs["green"] = std::to_string((*bytes)[1]);
        attrs["blue"] = std::to_string((*bytes)[2]);
        if (bytes->size() >= 4) attrs["alpha"] = std::to_string((*bytes)[3]);
        return attrs;
    }
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    const auto seq = object && *object ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object) : nullptr;
    if (seq && seq->m_values.size() >= 3) {
        attrs["red"] = value_string(seq->m_values[0]);
        attrs["green"] = value_string(seq->m_values[1]);
        attrs["blue"] = value_string(seq->m_values[2]);
        if (seq->m_values.size() >= 4) attrs["alpha"] = value_string(seq->m_values[3]);
    }
    return attrs;
}

bool bytes_color_changed(const ::cw::lib::Value& current, const ::cw::lib::Value& init) {
    const auto* a = std::get_if<::cw::lib::Bytes>(&current.storage());
    const auto* b = std::get_if<::cw::lib::Bytes>(&init.storage());
    if (a && b) return *a != *b;
    return !current.is_none();
}

bool rgba_changed(const std::tuple<Integer, Integer, Integer, Integer>& color, const ::cw::lib::Value& init) {
    const auto* bytes = std::get_if<::cw::lib::Bytes>(&init.storage());
    if (!bytes) return true;
    const auto channel = [&](std::size_t index, Integer fallback) {
        return index < bytes->size() ? static_cast<Integer>((*bytes)[index]) : fallback;
    };
    return std::get<0>(color) != channel(0, 0) || std::get<1>(color) != channel(1, 0) ||
           std::get<2>(color) != channel(2, 0) || std::get<3>(color) != channel(3, 255);
}

bool unicode_whitespace(unsigned code) {
    if (code <= 0x20)
        return code == ' ' || code == '\t' || code == '\n' || code == '\r' || code == '\v' || code == '\f';
    return code == 0x00a0 || code == 0x1680 || (code >= 0x2000 && code <= 0x200a) ||
           code == 0x2028 || code == 0x2029 || code == 0x202f || code == 0x205f || code == 0x3000;
}

unsigned next_utf8(const String& text, std::size_t& offset) {
    if (offset >= text.size()) return 0;
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80U) { ++offset; return first; }
    const unsigned width = (first & 0xe0U) == 0xc0U ? 2U : ((first & 0xf0U) == 0xe0U ? 3U : 4U);
    if (offset + width > text.size()) { ++offset; return 0xfffd; }
    unsigned code = first & ((1U << (8U - width - 1U)) - 1U);
    for (unsigned i = 1; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xc0U) != 0x80U) { ++offset; return 0xfffd; }
        code = (code << 6U) | (byte & 0x3fU);
    }
    offset += width;
    return code;
}

String strip_unicode_whitespace(String value) {
    std::size_t begin = 0;
    while (begin < value.size()) {
        std::size_t next = begin;
        if (!unicode_whitespace(next_utf8(value, next))) break;
        begin = next;
    }
    std::size_t last = value.size();
    while (last > begin) {
        std::size_t start = last;
        do { --start; } while (start > begin && (static_cast<unsigned char>(value[start]) & 0xc0U) == 0x80U);
        std::size_t probe = start;
        if (!unicode_whitespace(next_utf8(value, probe))) break;
        last = start;
    }
    return value.substr(begin, last - begin);
}

String python_float_str(Real value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
    char buffer[64];
    const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::scientific);
    if (conversion.ec != std::errc{})
        throw std::runtime_error("float formatting failed");
    const String shortest(buffer, conversion.ptr);
    const auto exponent_at = shortest.find('e');
    const int exponent = std::stoi(shortest.substr(exponent_at + 1));
    const bool negative = shortest.front() == '-';
    String digits = shortest.substr(negative ? 1 : 0,
                                    exponent_at - (negative ? 1 : 0));
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    String result = negative ? "-" : "";
    // Pythonのreprは元へ戻せる最短の十進数字列を使う。十
    // 進指数が-4～15なら固定小数点、それ以外は指数表記にする。
    if (exponent < -4 || exponent >= 16) {
        result += digits.front();
        if (digits.size() > 1) result += "." + digits.substr(1);
        result += exponent < 0 ? "e-" : "e+";
        const auto magnitude = std::to_string(exponent < 0 ? -exponent : exponent);
        if (magnitude.size() < 2) result += '0';
        return result + magnitude;
    }
    const int decimal_position = exponent + 1;
    if (decimal_position <= 0) {
        result += "0." + String(static_cast<std::size_t>(-decimal_position), '0') + digits;
    } else if (static_cast<std::size_t>(decimal_position) >= digits.size()) {
        result += digits + String(static_cast<std::size_t>(decimal_position) - digits.size(), '0') + ".0";
    } else {
        result += digits.substr(0, static_cast<std::size_t>(decimal_position)) + "." +
                  digits.substr(static_cast<std::size_t>(decimal_position));
    }
    return result;
}

bool value_bool(const ::cw::lib::Value& v, bool fallback = false) {
    if (const auto* p = std::get_if<bool>(&v.storage())) return *p;
    if (const auto* p = std::get_if<Integer>(&v.storage())) return *p != 0;
    if (const auto* p = std::get_if<Decimal>(&v.storage())) return !p->is_zero();
    if (const auto* p = std::get_if<String>(&v.storage())) return *p == "True" || *p == "true" || *p == "1";
    return fallback;
}

template <typename T> bool decimal_scalar_equal(const T& value, const Decimal& init) {
    if constexpr (std::is_same_v<T, bool>) {
        return value == !init.is_zero();
    } else if constexpr (std::is_integral_v<T>) {
        return init.compare(Decimal(static_cast<Integer>(value))) == 0;
    } else if constexpr (std::is_floating_point_v<T>) {
        // 設定は二進浮動小数点のRealフィールドを使う。この比較の文字列
        // 表現だけを変換し、Decimal自体をdouble経由にしない。
        if (!std::isfinite(value)) return false;
        std::ostringstream text;
        text << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
        try {
            return init.compare(Decimal::parse(text.str())) == 0;
        } catch (const ::cw::lib::DecimalError&) {
            return false;
        }
    } else {
        return false;
    }
}

template <typename T> bool changed(const T& value, const ::cw::lib::Value& init) {
    if (const auto* p = std::get_if<T>(&init.storage())) return value != *p;
    if (const auto* p = std::get_if<Decimal>(&init.storage())) {
        if constexpr (std::is_arithmetic_v<T>) return !decimal_scalar_equal(value, *p);
    }
    if constexpr (std::is_same_v<T, bool>) return value != value_bool(init);
    else if constexpr (std::is_same_v<T, String>) return value != value_string(init);
    else return true;
}

String text_bool(bool value) { return value ? "True" : "False"; }
String basename(const String& path) { return ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename()); }

std::pair<String, String> partyrecord_member(const ::cw::lib::Value& item) {
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&item.storage()); object && *object) {
        if (const auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object))
            return {tree->m_fpath, tree->gettext("Property/Name", "")};
        if (const auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object))
            return {element->m_fpath, element->gettext("Property/Name", "")};
    }
    return {value_string(item), {}};
}

E make_element(const String& tag, const String& text = {}, std::map<String, String> attrs = {}) {
    return ::cw::data::make_element(tag, text, std::move(attrs));
}

// 共通のVariant書き出しはDecimalに対応し、Python
// の小数点以下8桁の表示形式を保つ。一方、シナリオログは
// 再開に必要な値を丸めず、各Decimalの保存済みの
// 全精度を意図的に保持する。共通のリスト・構造体の形式
// を保ち、数値の末端要素だけを正確な表現へ置き換える。
void repair_decimal_xml(const ::cw::lib::Value& value, const E& element) {
    if (!element) return;
    if (const auto* decimal = std::get_if<Decimal>(&value.storage())) {
        element->set("type", "Number");
        element->set("value", decimal->to_string(true));
        return;
    }
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return;
    if (const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
        std::size_t index = 0;
        for (const auto& child : element->m_children) {
            if (!child || child->m_tag != "Value" || index >= sequence->m_values.size()) continue;
            repair_decimal_xml(sequence->m_values[index++], child);
        }
        return;
    }
    if (const auto structure = std::dynamic_pointer_cast<::cw::data::StructVal>(*object)) {
        std::size_t fallback_index = 0;
        for (const auto& child : element->m_children) {
            if (!child || child->m_tag != "Member") continue;
            std::size_t index = fallback_index++;
            const auto name = child->get("name", "");
            if (!name.empty()) {
                const auto it = std::find(structure->m_member_names.begin(), structure->m_member_names.end(), name);
                if (it != structure->m_member_names.end()) index = static_cast<std::size_t>(std::distance(structure->m_member_names.begin(), it));
            }
            if (index < structure->m_member_values.size()) repair_decimal_xml(structure->m_member_values[index], child);
        }
    }
}

void value_to_element_decimal_aware(const ::cw::lib::Value& value, const E& element, const String& type_attribute) {
    ::cw::data::Variant::value_to_element(value, element, type_attribute);
    repair_decimal_xml(value, element);
    // ログの正確な書き換えに使うため、元のDecimal値を保
    // つ。型とコンテナ構造は上の共通書き出し処理で対応済み。
}

void write_tree(const E& root, const String& path) {
    if (!root) throw std::invalid_argument("xmlcreater: null XML root");
    auto tree = ::cw::data::xml2etree(String{}, "", std::nullopt, root, true);
    if (!tree) throw std::runtime_error("xmlcreater: unable to create XML tree");
    tree->write_file(path);
}

void write_template(const String& name, const String& path, std::map<String, String> values) {
    values.emplace("indent", "");
    const auto body = ::cw::binary::xmltemplate::get_xmltext(name, std::move(values));
    const auto parent = std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) {
        if (::cardwirth::platform::is_mounted_user_path(parent)) {
            if (!::cardwirth::platform::make_mounted_user_directory(parent, true))
                throw std::runtime_error("xmlcreater: encrypted output directory could not be created");
        } else std::filesystem::create_directories(parent);
    }
    String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + body;
    if (!body.empty() && body.back() != '\n') xml.push_back('\n');
    ::cw::util::write_file(path, ::cw::lib::Value(std::move(xml)), ::cw::g_fsync, "w", String("utf-8"));
    if (::cw::g_fsync) ::cw::g_fsync->sync();
}

String optional_path(const ::cw::lib::Value& value) { return value_string(value); }
String temp_dir() {
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_tempdir : String{};
}
String yado_dir() {
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_yadodir : String{};
}
String join(const std::initializer_list<String>& paths) {
    ::cw::lib::Arguments args;
    for (const auto& p : paths) args.emplace_back(p);
    return ::cw::util::join_paths(args);
}
String relative_to(const String& path, const String& root) {
    // 有効な宿がない場合、素材パスは既に相対パス。
    if (root.empty()) return path;
    // Pyは論理tempdirの接頭辞を取り除くだけで、コピーしたファイルや親ディレク
    // トリを実パスへ正規化しない。同じ共通の論理relpath APIを使い、読めな
    // い親やシンボリックリンクによって絶対ImagePathが混入しないようにする。
    return ::cw::util::relpath(path, root);
}

std::shared_ptr<::cw::setting::MsgDict> runtime_messages() {
    if (::cw::g_cwpy) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage()); object && *object) {
            if (const auto messages = std::dynamic_pointer_cast<::cw::setting::MsgDict>(*object)) return messages;
        }
        if (::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_msgs;
    }
    return {};
}

String strip_path_root(const String& path, const String& root) {
    if (root.empty()) return path;
    const auto normalized_path = std::filesystem::u8path(path).lexically_normal().generic_string();
    auto normalized_root = std::filesystem::u8path(root).lexically_normal().generic_string();
    if (!normalized_root.empty() && normalized_root.back() != '/') normalized_root.push_back('/');
    return normalized_path.rfind(normalized_root, 0) == 0 ? normalized_path.substr(normalized_root.size()) : path;
}

E background_color(const String& name,
                   const std::tuple<Integer, Integer, Integer, Integer>& color) {
    return make_element(name, {}, {{"r", std::to_string(std::get<0>(color))},
                                    {"g", std::to_string(std::get<1>(color))},
                                    {"b", std::to_string(std::get<2>(color))},
                                    {"a", std::to_string(std::get<3>(color))}});
}

std::shared_ptr<::cw::sprite::background::BackgroundCellData> background_cell(const ::cw::lib::Value& value) {
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object)
        return std::dynamic_pointer_cast<::cw::sprite::background::BackgroundCellData>(*object);
    return {};
}

E make_textcell_name_element(const std::shared_ptr<::cw::sprite::message::NameListItem>& item) {
    if (!item) return {};
    const auto* data_object = std::get_if<::cw::lib::ObjectPtr>(&item->m_data.storage());
    const auto variant = data_object && *data_object
        ? std::dynamic_pointer_cast<::cw::data::Variant>(*data_object) : nullptr;
    String text;
    if (!variant) text = value_string(item->m_name);
    auto element = make_element("Name", text);
    if (data_object && *data_object) {
        if (std::dynamic_pointer_cast<::cw::data::YadoData>(*data_object)) {
            element->set("type", "Yado");
        } else if (std::dynamic_pointer_cast<::cw::data::Party>(*data_object)) {
            element->set("type", "Party");
        } else if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(*data_object)) {
            if (::cw::g_cwpy) {
                const auto pcards = ::cw::g_cwpy->get_pcards({});
                for (std::size_t i = 0; i < pcards.size(); ++i) {
                    if (pcards[i] == player) {
                        element->set("type", "Player");
                        element->set("number", std::to_string(i + 1));
                        break;
                    }
                }
            }
        } else if (const auto flag = std::dynamic_pointer_cast<::cw::data::Flag>(*data_object)) {
            element->set("type", "Flag");
            element->set("flag", flag->m_name);
        } else if (const auto step = std::dynamic_pointer_cast<::cw::data::Step>(*data_object)) {
            element->set("type", "Step");
            element->set("step", step->m_name);
        } else if (variant) {
            element->set("type", "Variant");
            element->set("variant", value_string(variant->m_name));
            if (!item->m_name.is_none()) {
                const auto* name_object = std::get_if<::cw::lib::ObjectPtr>(&item->m_name.storage());
                const auto sequence = name_object && *name_object
                    ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*name_object) : nullptr;
                const auto structure = name_object && *name_object
                    ? std::dynamic_pointer_cast<::cw::data::StructVal>(*name_object) : nullptr;
                if (sequence || structure) {
                    value_to_element_decimal_aware(item->m_name, element, "valuetype");
                } else {
                    element->set("valuetype", ::cw::data::Variant::value_to_type(item->m_name));
                    element->set_text(::cw::data::Variant::value_to_str(item->m_name, false));
                }
            }
        }
    } else if (value_string(item->m_data) == "Number") {
        element->set("type", "Number");
    }
    return element;
}
}

namespace cw::xmlcreater {
void _create_xml(::cw::lib::String arg_name, ::cw::lib::String arg_path, std::map<::cw::lib::String, ::cw::lib::String> arg_d)
{
    // Python版と同じxmltemplateを利用する。templateは呼出側で文字列を
    // escape済みとして渡すため、ここでは二重escapeしない。
    write_template(arg_name, arg_path, std::move(arg_d));
}

::cw::lib::String create_party(std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers, ::cw::lib::Integer arg_moneyamount, std::optional<::cw::lib::String> arg_pname, bool arg_is_suspendlevelup)
{
    if (arg_headers.empty()) throw std::invalid_argument("create_party: empty headers");
    String pname = arg_pname.value_or(arg_headers.front() ? arg_headers.front()->m_name : String("新しいパーティ"));
    if (!arg_pname && ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
        const String format = ::cw::g_cwpy->m_setting->m_msgs->get("default_party_name", "%sのパーティ");
        pname = ::cw::lib::python_percent_string(format, arg_headers.front() ? arg_headers.front()->m_name : String{});
    }
    std::map<String, String> d{{"name", ::cw::binary::util::repl_escapechar(pname)},
                               {"money", std::to_string(arg_moneyamount)},
                               {"suspend_levelup", text_bool(arg_is_suspendlevelup)},
                               {"backpack", {}}, {"indent", {}}};
    String members;
    for (const auto& h : arg_headers) {
        if (!h) continue;
        String path = value_string(h->m_fpath);
        auto stem = std::get<0>(::cw::util::splitext(basename(path)));
        members += "\n   <Member>" + ::cw::binary::util::repl_escapechar(stem) + "</Member>";
    }
    d["members"] = members;
    String dirname = ::cw::util::repl_dischar(pname);
    String path = join({yado_dir(), "Party", dirname});
    path = ::cw::util::dupcheck_plus(path, true, true);
    path = ::cw::util::get_yado_writepath(path, yado_dir(), temp_dir());
    _create_xml("Party", join({path, "Party.xml"}), std::move(d));
    return path;
}

::cw::lib::String create_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_party)
{
    return create_partyrecord(std::move(arg_party), yado_dir(), temp_dir());
}

::cw::lib::String create_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_party,
    const String& yadodir, const String& tempdir)
{
    if (!arg_party) throw std::invalid_argument("create_partyrecord: null party");
    const String name = value_string(arg_party->m_name, "noname");
    std::map<String, String> d{{"name", ::cw::binary::util::repl_escapechar(name)},
                               {"money", value_string(arg_party->m_money, "0")},
                               {"suspend_levelup", value_string(arg_party->m_is_suspendlevelup, "False")},
                               {"members", {}}, {"backpack", {}}, {"indent", {}}};
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&arg_party->m_members.storage()); p && *p) {
        if (const auto seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p)) {
            for (const auto& item : seq->m_values) {
                const auto [path, display] = partyrecord_member(item);
                const auto* object = std::get_if<::cw::lib::ObjectPtr>(&item.storage());
                if ((!object || !*object) && path.empty()) continue;
                const auto stem = std::get<0>(::cw::util::splitext(basename(path)));
                d["members"] += "\n   <Member name=\"" + ::cw::binary::util::repl_escapechar(display) + "\">" +
                                 ::cw::binary::util::repl_escapechar(stem) + "</Member>";
            }
        }
    }
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&arg_party->m_backpack.storage()); p && *p) {
        if (const auto seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p)) {
            for (const auto& item : seq->m_values) {
                auto card = std::dynamic_pointer_cast<::cw::header::CardHeader>(std::get_if<::cw::lib::ObjectPtr>(&item.storage()) ? *std::get_if<::cw::lib::ObjectPtr>(&item.storage()) : nullptr);
                if (!card) continue;
                std::map<String, String> cardd{{"name", ::cw::binary::util::repl_escapechar(card->m_name)},
                                               {"desc", ::cw::binary::util::repl_escapechar(card->m_desc)},
                                               {"author", ::cw::binary::util::repl_escapechar(card->m_author)},
                                               {"scenario", ::cw::binary::util::repl_escapechar(card->m_scenario)},
                                               {"uselimit", std::to_string(card->m_uselimit)}, {"indent", {}}};
                d["backpack"] += "\n   " + ::cw::binary::xmltemplate::get_xmltext("CardRecord", std::move(cardd));
            }
        }
    }
    // 現在の宿と一時保存ディレクトリは、絶対パスを含めて一つの名前空
    // 間を成す。固定のYado/Temp接頭辞では未保存の記録を見落
    // とし、連続する保存で同じ一時ファイルを上書きする場合がある。
    const String stem = ::cw::util::repl_dischar(name);
    String filename = stem + ".xml";
    String path;
    for (Integer suffix = 2;; ++suffix) {
        const auto saved = join({yadodir, "PartyRecord", filename});
        path = ::cw::util::get_yado_writepath(saved, yadodir, tempdir);
        if (!::cw::util::path_is_file(saved) && !::cw::util::path_is_directory(saved) &&
            !::cw::util::path_is_file(path) && !::cw::util::path_is_directory(path)) break;
        filename = stem + "(" + std::to_string(suffix) + ").xml";
    }
    const String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" +
        ::cw::binary::xmltemplate::get_xmltext("PartyRecord", std::move(d)) + "\n";
    const auto destination=std::filesystem::u8path(path);
    const auto bytes=std::as_bytes(std::span<const char>(xml.data(),xml.size()));
    if (::cardwirth::platform::is_mounted_user_path(destination)) {
        if (!::cardwirth::platform::write_mounted_user_file(destination,bytes,true))
            throw std::runtime_error("Failed to stage party record: " + path);
    } else {
        const auto absolute=std::filesystem::absolute(destination);
        ::cardwirth::platform::NativeStorage storage(absolute.parent_path(),true);
        if (!storage.write_atomic(absolute.filename(),bytes,false))
            throw std::runtime_error(storage.last_error());
    }
    return path;
}

::cw::lib::String create_environment(::cw::lib::String arg_name, ::cw::lib::String arg_dpath, ::cw::lib::String arg_skindirname, bool arg_is_autoloadparty, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths)
{
    String skintype = "MedievalFantasy";
    Integer cashbox = 4000;
    const auto base = ::cw::data::xml2etree(join({"Data", "SkinBase", "Skin.xml"}), "", std::nullopt, std::nullopt, true);
    if (base) cashbox = base->getint("Property/InitialCash", std::nullopt, cashbox);
    try {
        const auto skin = ::cw::data::xml2etree(join({"Data", "Skin", arg_skindirname, "Skin.xml"}), "", std::nullopt, std::nullopt, true);
        if (skin) {
            skintype = skin->gettext("Property/Type", skintype);
            cashbox = skin->getint("Property/InitialCash", std::nullopt, cashbox);
        }
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
    }
    std::map<String, String> d{{"name", ::cw::binary::util::repl_escapechar(arg_name)},
                               {"skinname", ::cw::binary::util::repl_escapechar(arg_skindirname)},
                               {"skintype", ::cw::binary::util::repl_escapechar(skintype)},
                               {"cashbox", std::to_string(cashbox)},
                               {"selectingparty", {}}, {"nowadventuring", "False"},
                               {"is_autoloadparty", text_bool(arg_is_autoloadparty)},
                               {"completestamps", {}}, {"gossips", {}}, {"imgpaths", {}}, {"indent", {}}};
    copy_yadoimgpaths(arg_dpath, arg_imgpaths);
    for (const auto& info : arg_imgpaths) {
        if (!info) continue;
        d["imgpaths"] += "\n" + ::cw::binary::xmltemplate::get_xmltext("ImagePath", {
            {"path", ::cw::binary::util::repl_escapechar(info->m_path)},
            {"postype", info->m_postype}, {"indent", "   "}});
    }
    const String path = join({arg_dpath, "Environment.xml"});
    _create_xml("Environment", path, std::move(d));
    return path;
}

void create_skinvariables(::cw::lib::String arg_path)
{
    _create_xml("SkinVariables", arg_path, {{"indent", {}}});
}

void copy_yadoimgpaths(::cw::lib::String arg_yadodir, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths)
{
    if(arg_imgpaths.empty()) return;
    const String idpath=join({arg_yadodir,"Material","Signboard"});
    for(const auto& info:arg_imgpaths) {
        if(!info || info->m_path.empty()) continue;
        const String dst=::cw::util::dupcheck_plus(join({idpath,basename(info->m_path)}),false,true);
        const auto directory=std::filesystem::u8path(idpath);
        if(::cardwirth::platform::is_mounted_user_path(directory)) {
            if(!::cardwirth::platform::make_mounted_user_directory(directory,true))
                throw std::runtime_error("copy_yadoimgpaths: Storage directory creation failed");
        } else std::filesystem::create_directories(directory);
        ::cw::util::copy_scaledimagepaths(info->m_path,dst,true);
        // Pyはコピーが完了するたびに即座に反映する。後で失敗しても、以前の
        // コピーとパス変更は残す。倍率違いの関連ファイルはcopy2に従う。
        info->m_path=join({"Material","Signboard",basename(dst)});
    }
}

E settings_element(const ::cw::setting::Setting& s, bool arg_writeplayingdata)
{
    auto root = make_element("Settings", {}, {{"dataVersion", "4"}});
    auto add = [&](const String& tag, const String& value) { root->append(make_element(tag, value)); };
#define ADD_SCALAR(tag, field, init) do { if (changed(s.field, s.init)) add(tag, text_value(s.field)); } while(false)
    auto text_value = [](const auto& value) -> String {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) return text_bool(value);
        else if constexpr (std::is_same_v<T, String>) return value;
        else return std::to_string(value);
    };

    if (s.m_local) create_localsettings(root, s.m_local);
    ADD_SCALAR("ShowAdvancedSettings", m_show_advancedsettings, m_show_advancedsettings_init);
    ADD_SCALAR("ScenarioEditor", m_editor, m_editor_init);
    ADD_SCALAR("StartupScene", m_startupscene, m_startupscene_init);
    if (arg_writeplayingdata) {
        ADD_SCALAR("LastYado", m_lastyado, m_lastyado_init);
        if (std::get<0>(s.m_window_position) || std::get<1>(s.m_window_position)) {
            // 型付きのNoneは座標属性の不在で表す。Python
            // はその側に文字列Noneを書き込むが、XmlReader
            // ではどちらも同じ省略可能な座標として読み込む。
            std::map<String, String> a;
            if (std::get<0>(s.m_window_position)) a["left"] = std::to_string(*std::get<0>(s.m_window_position));
            if (std::get<1>(s.m_window_position)) a["top"] = std::to_string(*std::get<1>(s.m_window_position));
            root->append(make_element("WindowPosition", {}, std::move(a)));
        }
        if (!s.m_yado_order.empty()) {
            auto e = make_element("YadoOrder", s.m_lastyado);
            for (const auto& [name, order] : s.m_yado_order) e->append(make_element("Order", std::to_string(order), {{"name", name}}));
            root->append(e);
        }
    }
    const bool expand_changed = changed(s.m_expanddrawing, s.m_expanddrawing_init) || changed(s.m_expandmode, s.m_expandmode_init) || changed(s.m_smoothexpand, s.m_smoothexpand_init) || (arg_writeplayingdata && changed(s.m_is_expanded, s.m_is_expanded_init));
    if (expand_changed) {
        add("ExpandDrawing", python_float_str(s.m_expanddrawing));
        std::map<String, String> attrs{{"smooth", text_bool(s.m_smoothexpand)}};
        if (arg_writeplayingdata) attrs["expanded"] = text_bool(s.m_is_expanded);
        root->append(make_element("ExpandMode", s.m_expandmode, std::move(attrs)));
    }
    if (arg_writeplayingdata) ADD_SCALAR("DebugMode", m_debug_saved, m_debug_init);
    ADD_SCALAR("ShowDebugLogDialog", m_show_debuglogdialog, m_show_debuglogdialog_init);
    ADD_SCALAR("EnabledTimekeepr", m_enabled_timekeeper, m_enabled_timekeeper_init);
    ADD_SCALAR("NoLevelUpInDebugMode", m_no_levelup_in_debugmode, m_no_levelup_in_debugmode_init);
    if (arg_writeplayingdata) ADD_SCALAR("Skin", m_skindirname, m_skindirname_init);
    ADD_SCALAR("PlayBgm", m_play_bgm, m_play_bgm_init);
    ADD_SCALAR("PlaySound", m_play_sound, m_play_sound_init);
    if (changed(s.m_vol_master, s.m_vol_master_init)) add("MasterVolume", std::to_string(static_cast<Integer>(s.m_vol_master * 100)));
    if (changed(s.m_vol_bgm, s.m_vol_bgm_init) || changed(s.m_vol_bgm_midi, s.m_vol_bgm_midi_init)) root->append(make_element("BgmVolume", std::to_string(static_cast<Integer>(s.m_vol_bgm * 100)), {{"midi", std::to_string(static_cast<Integer>(s.m_vol_bgm_midi * 100))}}));
    if (changed(s.m_vol_sound, s.m_vol_sound_init)) {
        std::map<String, String> attrs;
        const Integer volume = static_cast<Integer>(s.m_vol_sound * 100), midi = static_cast<Integer>(s.m_vol_sound_midi * 100);
        if (volume != midi) attrs["midi"] = std::to_string(midi);
        root->append(make_element("SoundVolume", std::to_string(volume), std::move(attrs)));
    }
    if (s.m_soundfonts_typed != s.m_soundfonts_init_typed) {
        auto e = make_element("SoundFonts");
        for (const auto& [name, enabled, volume] : s.m_soundfonts_typed) e->append(make_element("SoundFont", name, {{"enabled", text_bool(enabled)}, {"volume", std::to_string(volume)}}));
        root->append(e);
    }
    ADD_SCALAR("Bassmidi32bit", m_bassmidi_sample32bit, m_bassmidi_sample32bit_init);
    ADD_SCALAR("SDLMixerIsEnabled", m_sdlmixer_enabled, m_sdlmixer_enabled_init);
    ADD_SCALAR("MessageSpeed", m_messagespeed, m_messagespeed_init);
    ADD_SCALAR("CardDealingSpeed", m_dealspeed, m_dealspeed_init);
    if (changed(s.m_dealspeed_battle, s.m_dealspeed_battle_init) || changed(s.m_use_battlespeed, s.m_use_battlespeed_init)) root->append(make_element("CardDealingSpeedInBattle", std::to_string(s.m_dealspeed_battle), {{"enabled", text_bool(s.m_use_battlespeed)}}));
    ADD_SCALAR("WaitAfterPunctuationMark", m_wait_after_punctuation_mark, m_wait_after_punctuation_mark_init);
    ADD_SCALAR("WaitUseCard", m_wait_usecard, m_wait_usecard_init);
    ADD_SCALAR("ZoomOutFriendCard", m_zoomout_friend, m_zoomout_friend_init);
    ADD_SCALAR("EnlargeBeastCardZoomingRatio", m_enlarge_beastcardzoomingratio, m_enlarge_beastcardzoomingratio_init);
    if (changed(s.m_transition, s.m_transition_init) || changed(s.m_transitionspeed, s.m_transitionspeed_init)) root->append(make_element("Transition", s.m_transition, {{"speed", std::to_string(s.m_transitionspeed)}}));
    {
        std::map<String, String> attrs;
        if (changed(s.m_smoothscale_bg, s.m_smoothscale_bg_init)) attrs["bg"] = text_bool(s.m_smoothscale_bg);
        if (changed(s.m_smoothing_card_up, s.m_smoothing_card_up_init)) attrs["upcard"] = text_bool(s.m_smoothing_card_up);
        if (changed(s.m_smoothing_card_down, s.m_smoothing_card_down_init)) attrs["downcard"] = text_bool(s.m_smoothing_card_down);
        if (!attrs.empty()) root->append(make_element("SmoothScaling", {}, std::move(attrs)));
    }
    ADD_SCALAR("CautionBeforeSaving", m_caution_beforesaving, m_caution_beforesaving_init);
    ADD_SCALAR("RevertCardPocket", m_revert_cardpocket, m_revert_cardpocket_init);
    ADD_SCALAR("QuickDeal", m_quickdeal, m_quickdeal_init);
    ADD_SCALAR("AllQuickDeal", m_all_quickdeal, m_all_quickdeal_init);
    if (arg_writeplayingdata) {
        auto e = make_element("SortKey");
        if (changed(s.m_sort_yado, s.m_sort_yado_init)) e->set("yado", s.m_sort_yado);
        if (changed(s.m_sort_standbys, s.m_sort_standbys_init)) e->set("standbys", s.m_sort_standbys);
        // パーティの並べ替えは、通常のパーティ選択で使うC++側の拡張。
        if (changed(s.m_sort_parties, s.m_sort_parties_init)) e->set("parties", s.m_sort_parties);
        if (changed(s.m_sort_cards, s.m_sort_cards_init)) e->set("cards", s.m_sort_cards);
        if (changed(s.m_sort_cardswithstar, s.m_sort_cardswithstar_init)) e->set("cardswithstar", text_bool(s.m_sort_cardswithstar));
        if (!e->m_attributes.empty()) root->append(e);
        ADD_SCALAR("YadoNarrowType", m_yado_narrowtype, m_yado_narrowtype_init);
        ADD_SCALAR("StandbysNarrowType", m_standbys_narrowtype, m_standbys_narrowtype_init);
        ADD_SCALAR("PartiesNarrowType", m_parties_narrowtype, m_parties_narrowtype_init);
        ADD_SCALAR("CardNarrowType", m_card_narrowtype, m_card_narrowtype_init);
        ADD_SCALAR("InfoViewNarrowType", m_infoview_narrowtype, m_infoview_narrowtype_init);
    }
    ADD_SCALAR("MessageLogMax", m_backlogmax, m_backlogmax_init);
    ADD_SCALAR("MessageLogType", m_messagelog_type, m_messagelog_type_init);
    ADD_SCALAR("DisplayBillInMessageLog", m_display_bill_in_messagelog, m_display_bill_in_messagelog_init);
    ADD_SCALAR("SelectScenarioFromType", m_selectscenariofromtype, m_selectscenariofromtype_init);
    ADD_SCALAR("ShowUnfitnessScenario", m_show_unfitnessscenario, m_show_unfitnessscenario_init);
    ADD_SCALAR("ShowCompletedScenario", m_show_completedscenario, m_show_completedscenario_init);
    ADD_SCALAR("ShowInvisibleScenario", m_show_invisiblescenario, m_show_invisiblescenario_init);
    ADD_SCALAR("WheelUpOperation", m_wheelup_operation, m_wheelup_operation_init);
    ADD_SCALAR("ShowAllSelectedCards", m_show_allselectedcards, m_show_allselectedcards_init);
    ADD_SCALAR("ShowAim", m_show_aim, m_show_aim_init);
    ADD_SCALAR("ConfirmBeforeUsingCard", m_confirm_beforeusingcard, m_confirm_beforeusingcard_init);
    ADD_SCALAR("ConfirmBeforeSaving", m_confirm_beforesaving, m_confirm_beforesaving_init);
    ADD_SCALAR("ShowSavedMessage", m_show_savedmessage, m_show_savedmessage_init);
    ADD_SCALAR("ConfirmBeforeDumpCard", m_confirm_dumpcard, m_confirm_dumpcard_init);
    ADD_SCALAR("ShowBackpackCard", m_show_backpackcard, m_show_backpackcard_init);
    ADD_SCALAR("ShowBackpackCardAtEnd", m_show_backpackcardatend, m_show_backpackcardatend_init);
    ADD_SCALAR("ShowStatusTime", m_show_statustime, m_show_statustime_init);
    ADD_SCALAR("NoticeImpossibleAction", m_noticeimpossibleaction, m_noticeimpossibleaction_init);
    if (changed(s.m_initmoneyamount, s.m_initmoneyamount_init) || changed(s.m_initmoneyisinitialcash, s.m_initmoneyisinitialcash_init)) {
        std::map<String, String> attrs;
        if (changed(s.m_initmoneyisinitialcash, s.m_initmoneyisinitialcash_init)) attrs["sameasbase"] = text_bool(s.m_initmoneyisinitialcash);
        root->append(make_element("InitialMoneyAmount", std::to_string(s.m_initmoneyamount), std::move(attrs)));
    }
    ADD_SCALAR("AutoSavePartyRecord", m_autosave_partyrecord, m_autosave_partyrecord_init);
    ADD_SCALAR("OverwritePartyRecord", m_overwrite_partyrecord, m_overwrite_partyrecord_init);
    if (!s.m_folderoftype.empty()) {
        auto e = make_element("ScenarioFolderOfSkinType");
        for (const auto& [type, folder] : s.m_folderoftype) e->append(make_element("Folder", folder, {{"skintype", type}}));
        root->append(e);
    }
    if (arg_writeplayingdata) {
        ADD_SCALAR("ScenarioNarrowType", m_scenario_narrowtype, m_scenario_narrowtype_init);
        ADD_SCALAR("ScenarioSortType", m_scenario_sorttype, m_scenario_sorttype_init);
    }
    ADD_SCALAR("ScreenShotInformationFormat", m_ssinfoformat, m_ssinfoformat_init);
    if (bytes_color_changed(s.m_ssinfofontcolor, s.m_ssinfofontcolor_init)) {
        auto attrs = color_attrs(s.m_ssinfofontcolor);
        attrs.erase("alpha");
        if (!attrs.empty()) root->append(make_element("ScreenShotInformationFontColor", {}, std::move(attrs)));
    }
    if (bytes_color_changed(s.m_ssinfobackcolor, s.m_ssinfobackcolor_init)) {
        auto attrs = color_attrs(s.m_ssinfobackcolor);
        attrs.erase("alpha");
        if (!attrs.empty()) root->append(make_element("ScreenShotInformationBackgroundColor", {}, std::move(attrs)));
    }
    ADD_SCALAR("ScreenShotInformationBackgroundImage", m_ssinfobackimage, m_ssinfobackimage_init);
    ADD_SCALAR("ScreenShotFileNameFormat", m_ssfnameformat, m_ssfnameformat_init);
    ADD_SCALAR("ScreenShotOfCardsFileNameFormat", m_cardssfnameformat, m_cardssfnameformat_init);
    ADD_SCALAR("ScreenShotWithStatusBar", m_sswithstatusbar, m_sswithstatusbar_init);
    ADD_SCALAR("ScreenShotGameAreaOnly", m_ss_gamearea_only, m_ss_gamearea_only_init);
    ADD_SCALAR("StatusBarMask", m_statusbarmask, m_statusbarmask_init);
    ADD_SCALAR("ShowExperienceBar", m_show_experiencebar, m_show_experiencebar_init);
    ADD_SCALAR("ShowRoundAutoStartButton", m_show_roundautostartbutton, m_show_roundautostartbutton_init);
    ADD_SCALAR("ShowAllStatusDialog", m_show_all_status_dialog, m_show_all_status_dialog_init);
    ADD_SCALAR("AllStatusShortcuts", m_all_status_shortcuts, m_all_status_shortcuts_init);
    ADD_SCALAR("AllStatusShortcutsFolded", m_all_status_shortcuts_folded, m_all_status_shortcuts_folded_init);
    ADD_SCALAR("AllCustomStatus", m_all_custom_status, m_all_custom_status_init);
    ADD_SCALAR("ShowAutoButtonInEntryDialog", m_show_autobuttoninentrydialog, m_show_autobuttoninentrydialog_init);
    ADD_SCALAR("TitleFormat", m_titleformat, m_titleformat_init);
    ADD_SCALAR("PlayLogFormat", m_playlogformat, m_playlogformat_init);
    ADD_SCALAR("StopTheWorldWithIconization", m_stop_the_world_with_iconized, m_stop_the_world_with_iconized_init);
    if (arg_writeplayingdata) ADD_SCALAR("UnconvertTargetFolder", m_unconvert_targetfolder, m_unconvert_targetfolder_init);
    ADD_SCALAR("ShowTiles", m_show_tiles, m_show_tiles_init);
    // Python版の既存比較先（show_tiles_init）も意図的に保持。
    if (changed(s.m_enabled_right_flick, s.m_show_tiles_init)) add("EnabledRightFlick", text_bool(s.m_enabled_right_flick));
    ADD_SCALAR("CanRepeatLClick", m_can_repeatlclick, m_can_repeatlclick_init);
    ADD_SCALAR("CanSkipWait", m_can_skipwait, m_can_skipwait_init);
    ADD_SCALAR("CanSkipAnimation", m_can_skipanimation, m_can_skipanimation_init);
    // C++追加設定も通常保存と設定書出しで同じ共有モデルを使用する。
    ADD_SCALAR("SkipPcClickMotion", m_skip_pc_click_motion, m_skip_pc_click_motion_init);
    ADD_SCALAR("CanSkipWaitWithWheel", m_can_skipwait_with_wheel, m_can_skipwait_with_wheel_init);
    ADD_SCALAR("CanForwardMessageWithWheel", m_can_forwardmessage_with_wheel, m_can_forwardmessage_with_wheel_init);
    ADD_SCALAR("RadiusForNotDetectingCursorMovement", m_radius_notdetectmovement, m_radius_notdetectmovement_init);
    ADD_SCALAR("CursorType", m_cursor_type, m_cursor_type_init);
    ADD_SCALAR("AutoEnterOnSprite", m_autoenter_on_sprite, m_autoenter_on_sprite_init);
    ADD_SCALAR("BlinkStatusButton", m_blink_statusbutton, m_blink_statusbutton_init);
    ADD_SCALAR("BlinkPartyMoney", m_blink_partymoney, m_blink_partymoney_init);
    ADD_SCALAR("ShowButtonDescription", m_show_btndesc, m_show_btndesc_init);
    ADD_SCALAR("ProtectStaredCard", m_protect_staredcard, m_protect_staredcard_init);
    ADD_SCALAR("ProtectPremierCard", m_protect_premiercard, m_protect_premiercard_init);
    ADD_SCALAR("ShowSellAndDumpWithPremierCard", m_show_sell_with_premiercard, m_show_sell_with_premiercard_init);
    ADD_SCALAR("ShowPersonalCards", m_show_personal_cards, m_show_personal_cards_init);
    ADD_SCALAR("LevelAdjustmentAffectPersonalPocket", m_level_adjustment_affect_personal_pocket, m_level_adjustment_affect_personal_pocket_init);
    ADD_SCALAR("ShowCardKind", m_show_cardkind, m_show_cardkind_init);
    ADD_SCALAR("ShowPremiumIcon", m_show_premiumicon, m_show_premiumicon_init);
    ADD_SCALAR("CanClickSidesOfCardControl", m_can_clicksidesofcardcontrol, m_can_clicksidesofcardcontrol_init);
    ADD_SCALAR("ShowPaperAndTree", m_show_paperandtree, m_show_paperandtree_init);
    ADD_SCALAR("FilerDirectory", m_filer_dir, m_filer_dir_init);
    ADD_SCALAR("FilerFile", m_filer_file, m_filer_file_init);
    ADD_SCALAR("RecentHistoryLimit", m_recenthistory_limit, m_recenthistory_limit_init);
    ADD_SCALAR("VolumeIncrement", m_volume_increment, m_volume_increment_init);
    ADD_SCALAR("SpendNoEffectCard", m_spend_noeffectcard, m_spend_noeffectcard_init);
    ADD_SCALAR("WritePlayLog", m_write_playlog, m_write_playlog_init);
    ADD_SCALAR("OpenLastScenario", m_open_lastscenario, m_open_lastscenario_init);
    ADD_SCALAR("OpenFindScenarioResult", m_open_lastfindresult, m_open_lastfindresult_init);
    ADD_SCALAR("CanInstallScenarioFromDrop", m_can_installscenariofromdrop, m_can_installscenariofromdrop_init);
    ADD_SCALAR("DeleteSourceAfterInstalled", m_delete_sourceafterinstalled, m_delete_sourceafterinstalled_init);
    ADD_SCALAR("InstallNotScenarioFiles", m_install_notscenariofiles, m_install_notscenariofiles_init);
    ADD_SCALAR("AutoUpdateFiles", m_auto_update_files, m_auto_update_files_init);
    if (arg_writeplayingdata) {
        auto e = make_element("InstalledPaths");
        for (const auto& [rootdir, stack] : s.m_installed_dir) {
            if (!std::filesystem::is_directory(std::filesystem::u8path(rootdir))) continue;
            auto p = make_element("InstalledPath", {}, {{"root", rootdir}});
            for (const auto& path : stack) p->append(make_element("Path", path));
            e->append(p);
        }
        if (!e->m_children.empty()) root->append(e);
        if (!s.m_bookmarks_for_cardedit.empty()) {
            auto b = make_element("BookmarksForCardEditor");
            for (const auto& [path, name] : s.m_bookmarks_for_cardedit) b->append(make_element("Bookmark", path, {{"name", name}}));
            root->append(b);
        }
        std::map<String, String> attrs;
        if (s.m_show_multiplebases || !s.m_show_multiplebases_init.is_none()) attrs["base"] = text_bool(s.m_show_multiplebases);
        if (s.m_show_multipleparties || !s.m_show_multipleparties_init.is_none()) attrs["party"] = text_bool(s.m_show_multipleparties);
        if (s.m_show_multipleplayers || !s.m_show_multipleplayers_init.is_none()) attrs["player"] = text_bool(s.m_show_multipleplayers);
        if (s.m_show_scenariotree || !s.m_show_scenariotree_init.is_none()) attrs["scenario"] = text_bool(s.m_show_scenariotree);
        if (!attrs.empty()) root->append(make_element("ShowMultipleItems", {}, std::move(attrs)));
        attrs.clear();
        if (s.m_show_additional_yado || !s.m_show_additional_yado_init.is_none()) attrs["yado"] = text_bool(s.m_show_additional_yado);
        if (s.m_show_additional_player || !s.m_show_additional_player_init.is_none()) attrs["player"] = text_bool(s.m_show_additional_player);
        if (s.m_show_additional_party || !s.m_show_additional_party_init.is_none()) attrs["party"] = text_bool(s.m_show_additional_party);
        if (s.m_show_additional_scenario || !s.m_show_additional_scenario_init.is_none()) attrs["scenario"] = text_bool(s.m_show_additional_scenario);
        if (s.m_show_additional_card || !s.m_show_additional_card_init.is_none()) attrs["card"] = text_bool(s.m_show_additional_card);
        if (!attrs.empty() || changed(s.m_show_addctrlbtn, s.m_show_addctrlbtn_init)) root->append(make_element("ShowAdditionalControls", s.m_show_addctrlbtn ? String{} : String("Hidden"), std::move(attrs)));
    }
    ADD_SCALAR("FontExampleFormat", m_fontexampleformat, m_fontexampleformat_init);
    ADD_SCALAR("FontExamplePixelSize", m_fontexamplepixelsize, m_fontexamplepixelsize_init);
#undef ADD_SCALAR
    return root;
}

::cw::lib::Bytes serialize_settings(std::shared_ptr<::cw::setting::Setting> arg_setting, bool arg_writeplayingdata)
{
    if (!arg_setting) throw std::invalid_argument("serialize_settings: null setting");
    ::cw::data::CWPyElementTree tree(settings_element(*arg_setting, arg_writeplayingdata));
    return tree.write_bytes();
}

::cw::lib::String create_settings(std::shared_ptr<::cw::setting::Setting> arg_setting, bool arg_writeplayingdata, ::cw::lib::String arg_fpath)
{
    if (!arg_setting) throw std::invalid_argument("create_settings: null setting");
    write_tree(settings_element(*arg_setting, arg_writeplayingdata), arg_fpath);
    return arg_fpath;
}

void create_localsettings(std::shared_ptr<::cw::data::CWPyElement> arg_element, std::shared_ptr<::cw::setting::LocalSetting> arg_local)
{
    if (!arg_element || !arg_local) return;
    auto& l = *arg_local;
    auto add = [&](const String& tag, const String& value) { arg_element->append(make_element(tag, value)); };
    auto changed_bool = [](bool value, const ::cw::lib::Value& init) { return changed(value, init); };
    if (changed_bool(l.m_important_draw, l.m_important_draw_init)) arg_element->set("importantdrawing", text_bool(l.m_important_draw));
    if (changed_bool(l.m_important_font, l.m_important_font_init)) arg_element->set("importantfont", text_bool(l.m_important_font));
    auto color = [&](const String& tag, const auto& c) {
        arg_element->append(make_element(tag, {}, {{"red", std::to_string(std::get<0>(c))}, {"green", std::to_string(std::get<1>(c))}, {"blue", std::to_string(std::get<2>(c))}, {"alpha", std::to_string(std::get<3>(c))}}));
    };
    if (rgba_changed(l.m_mwincolour, l.m_mwincolour_init)) color("MessageWindowColor", l.m_mwincolour);
    if (rgba_changed(l.m_mwinframecolour, l.m_mwinframecolour_init)) color("MessageWindowFrameColor", l.m_mwinframecolour);
    color("AllEngineFrameColor", l.m_allengineframecolour);
    if (rgba_changed(l.m_blwincolour, l.m_blwincolour_init)) color("MessageLogWindowColor", l.m_blwincolour);
    if (rgba_changed(l.m_blwinframecolour, l.m_blwinframecolour_init)) color("MessageLogWindowFrameColor", l.m_blwinframecolour);
    if (bytes_color_changed(l.m_blcurtaincolour, l.m_blcurtaincolour_init)) {
        const auto attrs = color_attrs(l.m_blcurtaincolour);
        if (!attrs.empty()) arg_element->append(make_element("MessageLogCurtainColor", {}, attrs));
    }
    if (bytes_color_changed(l.m_curtaincolour, l.m_curtaincolour_init)) {
        const auto attrs = color_attrs(l.m_curtaincolour);
        if (!attrs.empty()) arg_element->append(make_element("CurtainColor", {}, attrs));
    }
    if (changed(l.m_fullscreenbackgroundtype, l.m_fullscreenbackgroundtype_init)) add("FullScreenBackgroundType", std::to_string(l.m_fullscreenbackgroundtype));
    if (changed(l.m_fullscreenbackgroundfile, l.m_fullscreenbackgroundfile_init)) add("FullScreenBackgroundFile", l.m_fullscreenbackgroundfile);
    if (changed_bool(l.m_bordering_cardname, l.m_bordering_cardname_init)) add("BorderingCardName", text_bool(l.m_bordering_cardname));
    if (changed_bool(l.m_decorationfont, l.m_decorationfont_init)) add("DecorationFont", text_bool(l.m_decorationfont));
    if (changed_bool(l.m_fontsmoothing_message, l.m_fontsmoothing_message_init)) add("FontSmoothingMessage", text_bool(l.m_fontsmoothing_message));
    if (changed_bool(l.m_fontsmoothing_cardname, l.m_fontsmoothing_cardname_init)) add("FontSmoothingCardName", text_bool(l.m_fontsmoothing_cardname));
    if (changed_bool(l.m_fontsmoothing_statusbar, l.m_fontsmoothing_statusbar_init)) add("FontSmoothingStatusBar", text_bool(l.m_fontsmoothing_statusbar));
    for (const auto& [key, value] : l.m_basefont) {
        // LocalSetting::initialize/loadはPythonのbasefont_init
        // に相当する値を5つの空文字列で初期化する。互換用ValueはABIの印だけに使う。
        if (!value.empty()) {
            const String tag = key == "gothic" ? "FontGothic" : key == "uigothic" ? "FontUIGothic" : key == "mincho" ? "FontMincho" : key == "pmincho" ? "FontPMincho" : "FontPGothic";
            add(tag, value);
        }
    }
    auto append_fonts = [&](const String& tag, const std::map<String, ::cw::setting::FontSettingValue>& table, const std::map<String, ::cw::setting::FontSettingValue>& initial) {
        auto e = make_element(tag, {}, tag == "SyntheticFonts" ? std::map<String, String>{{"key", "message"}} : std::map<String, String>{});
        for (const auto& [key, value] : table) {
            const auto old = initial.find(key);
            if (old != initial.end() && old->second == value) continue;
            const auto& [type, name, pixels, bold, expanded, italic] = value;
            std::map<String, String> attrs{{"key", key}};
            if (!type.empty()) attrs["type"] = type;
            if (pixels > 0) attrs["pixels"] = std::to_string(pixels);
            attrs["bold"] = text_bool(bold); attrs["expandedbold"] = text_bool(expanded); attrs["italic"] = text_bool(italic);
            e->append(make_element("Font", name, std::move(attrs)));
        }
        if (!e->m_children.empty()) arg_element->append(e);
    };
    append_fonts("Fonts", l.m_fonttypes_typed, l.m_fonttypes_init_typed);
    append_fonts("SyntheticFonts", l.m_msg_exfonts, l.m_msg_exfonts_init_typed);
}

::cw::lib::String create_albumpage(::cw::lib::String arg_path, bool arg_lost, bool arg_nocoupon)
{
    const auto source = ::cw::data::yadoxml2etree(arg_path, "", std::nullopt);
    if (!source || !source->getroot()) throw std::runtime_error("create_albumpage: invalid adventurer XML");
    auto album = make_element("Album");
    const bool can_loaded_scaledimage = source->getbool(".", ::cw::lib::Value("scaledimage"), false);
    if (can_loaded_scaledimage) album->set("scaledimage", "True");
    auto prop = make_element("Property");
    const auto source_prop = source->getroot()->find("Property");
    const std::set<String> allowed{"Name", "ImagePath", "ImagePaths", "Description", "Level", "Life", "Feature", "Ability", "Coupons", "BackColor", "Coefficient"};
    if (source_prop && *source_prop) {
        for (const auto& child : (*source_prop)->m_children) if (child && allowed.contains(child->m_tag)) prop->append(child);
    }
    if (const auto life = prop->find("Life"); life && *life) {
        const String max = (*life)->get("max", "");
        if (!max.empty()) (*life)->set_text(max);
    }
    if (!arg_nocoupon) {
        auto coupons = prop->find("Coupons");
        if (!coupons || !*coupons) {
            prop->append(make_element("Coupons"));
            coupons = prop->find("Coupons");
        }
        if (coupons && *coupons) (*coupons)->append(make_element("Coupon", "＿死亡", {{"value", "0"}}));
        String lost = arg_lost ? "＿死亡" : "＿死亡";
        if (const auto messages = runtime_messages()) lost = messages->get(arg_lost ? "lost_coupon_1" : "lost_coupon_2", lost);
        if (coupons && *coupons) (*coupons)->append(make_element("Coupon", lost, {{"value", "0"}}));
    }
    album->append(prop);
    const String name = prop->gettext("Name", "noname").empty() ? "noname" : prop->gettext("Name", "noname");
    const String fname = ::cw::util::repl_dischar(name);
    if (::cw::g_cwpy) {
        const String material_dir = join({yado_dir(), "Material", "Album"});
        ::cw::g_cwpy->copy_materials(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(album)),
                                      material_dir, false, {}, std::nullopt, std::nullopt,
                                      false, std::nullopt, false, can_loaded_scaledimage);
    }
    const String out = ::cw::util::dupcheck_plus(join({temp_dir(), "Album", fname + ".xml"}), true, true);
    write_tree(album, out);
    return out;
}

::cw::lib::String create_adventurer(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data)
{
    if (!arg_data) throw std::invalid_argument("create_adventurer: null data");
    auto d = arg_data->get_d();
    for (auto& [key, value] : d) value = ::cw::binary::util::repl_escapechar(value);
    const String advname = ::cw::util::repl_dischar(d["name"]);
    auto infos = write_castimagepath(advname, arg_data->m_imgpaths, true, false);
    d["imgpaths"].clear();
    for (const auto& info : infos) if (info) d["imgpaths"] += "\n" + ::cw::binary::xmltemplate::get_xmltext("ImagePath", {{"path", ::cw::binary::util::repl_escapechar(info->m_path)}, {"postype", info->m_postype}, {"indent", "   "}});
    d["scaledimage"] = "True";
    d["coupons"].clear();
    for (const auto& [name, value] : arg_data->m_coupons) d["coupons"] += "\n" + create_adventurer_get_coupon(name, value);
    const String path = ::cw::util::dupcheck_plus(join({temp_dir(), "Adventurer", advname + ".xml"}), true, true);
    try {
        _create_xml("Adventurer", path, std::move(d));
    } catch (...) {
        // 画像とキャラクターXMLは一つの作成処理として扱う。最後
        // のXMLを書けない場合に、コピー済みの顔画像を残さない。
        for (const auto& info : infos) {
            if (!info || info->m_path.empty()) continue;
            try {
                ::cw::util::remove_scaledimagepaths(
                    join({temp_dir(), info->m_path}), true, false);
            } catch (...) {}
        }
        throw;
    }
    return path;
}

::cw::lib::String create_adventurer_get_coupon(::cw::lib::String arg_name, ::cw::lib::Integer arg_value)
{
    return ::cw::binary::xmltemplate::get_xmltext("Coupon", {{"name", ::cw::binary::util::repl_escapechar(arg_name)}, {"value", std::to_string(arg_value)}, {"indent", "   "}});
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> write_castimagepath(::cw::lib::String arg_name, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, bool arg_can_loaded_scaledimage, bool arg_checkfilename)
{
    // Pyは空白を除く前に元の名前を調べる。空白だけの名前は空のパス
    // 要素として残し、元から空だった名前だけをnonameにする。
    const String name = arg_name.empty() ? "noname"
        : ::cw::binary::util::check_filename(strip_unicode_whitespace(arg_name));
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> result;
    // Pyより拡張した意図的な失敗時の後始末。後続のコピーに失敗したら、この呼
    // び出しで新しくコピーした画像だけを消し、使われないファイルを残さない。
    std::vector<String> copied_destinations;
    try {
        for (const auto& info : arg_paths) {
            if (!info || info->m_path.empty()) continue;
            if (::cw::g_fsync && ::cw::g_fsync->is_waiting(info->m_path)) ::cw::g_fsync->sync();
            if (!::cw::util::path_is_file(info->m_path)) continue;
            String dir = ::cw::util::dupcheck_plus(join({temp_dir(), "Material", "Adventurer", name}), true, true);
            const auto directory = std::filesystem::u8path(dir);
            if (::cardwirth::platform::is_mounted_user_path(directory)) {
                if (!::cardwirth::platform::make_mounted_user_directory(directory, true))
                    throw std::runtime_error("adventurer image output directory could not be created");
            } else {
                std::filesystem::create_directories(directory);
            }
            String dst = ::cw::util::dupcheck_plus(join({dir, basename(info->m_path)}), false, arg_checkfilename);
            ::cw::util::copy_scaledimagepaths(info->m_path, dst, arg_can_loaded_scaledimage);
            copied_destinations.push_back(dst);
            auto copy = std::make_shared<::cw::image::ImageInfo>();
            // PyはImageInfo(newpath, base=info)で構
            // 築する。元のbaseの連鎖を解決して位置を保持し、pcnumberと
            // basecardtypeはコピーせずコンストラクタの既定値を使う。
            copy->initialize(relative_to(dst, temp_dir()), 0, info, "Default", std::nullopt);
            result.push_back(copy);
        }
    } catch (...) {
        for (const auto& destination : copied_destinations) {
            try { ::cw::util::remove_scaledimagepaths(destination, true, false); }
            catch (...) {}
        }
        throw;
    }
    return result;
}

::cw::lib::String create_scenariolog(std::shared_ptr<::cw::data::ScenarioData> arg_sdata, ::cw::lib::String arg_path, bool arg_recording, ::cw::lib::String arg_logfilepath)
{
    if (!arg_sdata) throw std::invalid_argument("create_scenariolog: null scenario");
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party || !*(*::cw::g_cwpy->m_ydata)->m_party)
        throw std::logic_error("create_scenariolog: an active inn and party are required");
    auto root = make_element("ScenarioLog");
    auto prop = make_element("Property");
    prop->append(make_element("Name", value_string(arg_sdata->m_name)));
    prop->append(make_element("WsnPath", value_string(arg_sdata->m_fpath)));
    prop->append(make_element("RoundAutoStart", text_bool(arg_sdata->m_autostart_round)));
    prop->append(make_element("NoticeInfoView", text_bool(arg_sdata->m_notice_infoview)));
    auto env = make_element("PartyEnvironment");
    if (!arg_sdata->m_party_environment_backpack) env->append(make_element("Backpack", "Disable"));
    if (!arg_sdata->m_party_environment_gameover) env->append(make_element("GameOver", "Disable"));
    if (!arg_sdata->m_party_environment_runaway) env->append(make_element("RunAway", "Disable"));
    if (!env->m_children.empty()) prop->append(env);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_write_playlog) prop->append(make_element("LogFile", arg_logfilepath));
    if (!arg_recording) prop->append(make_element("Debug", text_bool(::cw::g_cwpy->is_debugmode())));
    Integer areaid = 0;
    if (::cw::g_cwpy) {
        areaid = ::cw::g_cwpy->m_areaid;
        if (areaid < 0 && !::cw::g_cwpy->m_pre_areaids.empty()) areaid = std::get<0>(::cw::g_cwpy->m_pre_areaids.front());
        else if (areaid < 0) areaid = 0;
    }
    prop->append(make_element("AreaId", std::to_string(areaid)));
    auto music_paths = make_element("MusicPaths");
    for (const auto& music : ::cw::thread::CWPy::s_music) {
        if (!music) continue;
        const String source_path = !music->m_path_typed.empty() ? music->m_path_typed : music->m_path;
        String path = source_path;
        if (::cw::g_cwpy) path = strip_path_root(path, value_string(::cw::g_cwpy->m_skindir));
        if (path == source_path) path = strip_path_root(path, arg_sdata->m_scedir);
        const String full_path = !music->m_fpath_typed.empty() ? music->m_fpath_typed : music->m_fpath;
        music_paths->append(make_element("MusicPath", path,
            {{"channel", std::to_string(music->m_channel_typed)},
             {"volume", std::to_string(music->m_subvolume_typed)},
             {"loopcount", std::to_string(music->m_loopcount_typed)},
             {"inusecard", text_bool(music->m_inusecard_typed)},
             {"path", full_path}}));
    }
    prop->append(music_paths);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        const auto& yd = *::cw::g_cwpy->m_ydata;
        prop->append(make_element("Yado", yd->m_name));
        if (yd->m_party && *yd->m_party) prop->append(make_element("Party", (*yd->m_party)->m_name));
    }
    root->append(prop);
    auto bgimages = make_element("BgImages");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        for (const auto& [bgtype, value] : ::cw::g_cwpy->m_background->m_bgs) {
            if (bgtype == ::cw::sprite::background::g_BG_SEPARATOR) {
                if (!bgimages->m_children.empty() && bgimages->m_children.back()->m_tag != "Redisplay")
                    bgimages->append(make_element("Redisplay"));
                continue;
            }
            if (!value) continue;
            const auto cell = background_cell(*value);
            if (!cell || cell->layer >= ::cw::g_LAYER_SP_LAYER) continue;
            std::map<String, String> attrs{{"visible", text_bool(cell->visible)}};
            if (cell->battle) attrs["battle"] = "True";
            if (!cell->cellname.empty()) attrs["cellname"] = cell->cellname;
            E bg;
            if (bgtype == ::cw::sprite::background::g_BG_IMAGE) {
                attrs["mask"] = text_bool(cell->mask);
                if (!cell->smoothing.empty() && cell->smoothing != "Default") attrs["smoothing"] = cell->smoothing;
                bg = make_element("BgImage", {}, std::move(attrs));
                if (cell->inusecard)
                    bg->append(make_element("ImagePath", cell->path, {{"inusecard", "True"}, {"scaledimage", text_bool(cell->scaledimage)}}));
                else bg->append(make_element("ImagePath", cell->path));
            } else if (bgtype == ::cw::sprite::background::g_BG_TEXT) {
                attrs["loaded"] = text_bool(cell->loaded);
                bg = make_element("TextCell", {}, std::move(attrs));
                bg->append(make_element("Text", cell->text));
                bg->append(make_element("Font", cell->face,
                    {{"size", std::to_string(cell->text_size)}, {"bold", text_bool(cell->bold)},
                     {"italic", text_bool(cell->italic)}, {"underline", text_bool(cell->underline)},
                     {"strike", text_bool(cell->strike)}}));
                bg->append(make_element("Vertical", text_bool(cell->vertical)));
                bg->append(make_element("Antialias", text_bool(cell->antialias)));
                bg->append(background_color("Color", cell->color1));
                bg->append(make_element("UpdateType", cell->update_type));
                if (!cell->scenario.empty() || !cell->author.empty()) {
                    bg->append(make_element("Scenario", cell->scenario));
                    bg->append(make_element("Author", cell->author));
                }
                if (!cell->border_type.empty() && cell->border_type != "None") {
                    auto bordering = make_element("Bordering", {}, {{"type", cell->border_type}, {"width", std::to_string(cell->border_width)}});
                    bordering->append(background_color("Color", cell->color2));
                    bg->append(bordering);
                }
                if (!cell->names.empty()) {
                    auto names = make_element("Names");
                    for (const auto& item : cell->names) {
                        if (auto name_element = make_textcell_name_element(item)) names->append(name_element);
                    }
                    if (!names->m_children.empty()) bg->append(names);
                }
            } else if (bgtype == ::cw::sprite::background::g_BG_COLOR) {
                bg = make_element("ColorCell", {}, std::move(attrs));
                bg->append(make_element("BlendMode", cell->blend));
                bg->append(background_color("Color", cell->color1));
                if (!cell->gradient.empty() && cell->gradient != "None") {
                    auto gradient = make_element("Gradient", {}, {{"direction", cell->gradient}});
                    gradient->append(background_color("EndColor", cell->color2));
                    bg->append(gradient);
                }
            } else if (bgtype == ::cw::sprite::background::g_BG_PC) {
                attrs["expand"] = text_bool(cell->expand);
                if (!cell->smoothing.empty() && cell->smoothing != "Default") attrs["smoothing"] = cell->smoothing;
                bg = make_element("PCCell", {}, std::move(attrs));
                bg->append(make_element("PCNumber", std::to_string(cell->pcnumber)));
            }
            if (!bg) continue;
            bg->append(make_element("Flag", cell->flag));
            bg->append(make_element("Location", {}, {{"left", std::to_string(cell->pos.x)}, {"top", std::to_string(cell->pos.y)}}));
            bg->append(make_element("Size", {}, {{"width", std::to_string(cell->size.width)}, {"height", std::to_string(cell->size.height)}}));
            if (cell->layer != ::cw::g_LAYER_BACKGROUND) bg->append(make_element("Layer", std::to_string(cell->layer)));
            bgimages->append(bg);
        }
    }
    root->append(bgimages);
    if (!arg_sdata->m_moved_mcards.empty()) {
        auto moved = make_element("MovedCards");
        for (const auto& [key, data] : arg_sdata->m_moved_mcards) {
            const auto& [group, index] = key;
            const auto& [x, y, scale, layer, width, height] = data;
            auto card = make_element("MovedCard", {}, {{"cardgroup", group}, {"index", std::to_string(index)}});
            card->append(make_element("Location", {}, {{"left", std::to_string(x)}, {"top", std::to_string(y)}}));
            if (scale != -1 || width != -1 || height != -1) card->append(make_element("Size", {}, {{"scale", std::to_string(scale)}, {"width", std::to_string(width)}, {"height", std::to_string(height)}}));
            if (layer != -1) card->append(make_element("Layer", std::to_string(layer)));
            moved->append(card);
        }
        root->append(moved);
    }
    auto flags = make_element("Flags");
    // ScenarioData.flags/stepsは、SystemDataの型付きの対応表で表す。
    for (const auto& [name, flag] : arg_sdata->m_flags) if (flag) flags->append(make_element("Flag", name, {{"value", text_bool(flag->m_value)}}));
    root->append(flags);
    auto steps = make_element("Steps");
    for (const auto& [name, step] : arg_sdata->m_steps) if (step) steps->append(make_element("Step", name, {{"value", std::to_string(step->m_value)}}));
    root->append(steps);
    if (!arg_sdata->m_variants.empty()) {
        auto variants = make_element("Variants");
        for (const auto& [name, variant] : arg_sdata->m_variants) {
            if (!variant) continue;
            const String type = value_string(variant->m_type);
            auto e = make_element("Variant", type == "List" || type == "Structure" ? String{} : name);
            if (type == "List" || type == "Structure") e->append(make_element("Name", name));
            value_to_element_decimal_aware(variant->m_value, e, "type");
            variants->append(e);
        }
        root->append(variants);
    }
    if (!arg_recording) {
        auto gossip = make_element("Gossips");
        for (const auto& [name, value] : arg_sdata->m_gossips) gossip->append(make_element("Gossip", name, {{"value", text_bool(value)}}));
        root->append(gossip);
        auto complete = make_element("CompleteStamps");
        for (const auto& [name, value] : arg_sdata->m_compstamps) complete->append(make_element("CompleteStamp", name, {{"value", text_bool(value)}}));
        root->append(complete);
    }
    auto info = make_element("InfoCards");
    for (const auto id : arg_sdata->get_infocards(true)) info->append(make_element("InfoCard", std::to_string(id)));
    root->append(info);
    auto casts = make_element("CastCards");
    for (const auto& card : arg_sdata->m_friendcards)
        if (card && card->m_data_typed && card->m_data_typed->getroot())
            casts->append(card->m_data_typed->getroot());
    root->append(casts);
    if (!arg_recording) {
        auto deleted = make_element("DeletedFiles");
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_sdata->m_deletedpaths.storage());
        const auto seq = object && *object ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object) : nullptr;
        if (seq) for (const auto& path : seq->m_values) deleted->append(make_element("DeletedFile", value_string(path)));
        root->append(deleted);
        auto lost = make_element("LostAdventurers");
        for (const auto& path : arg_sdata->m_lostadventurers) lost->append(make_element("LostAdventurer", path));
        root->append(lost);
    }
    write_tree(root, arg_path);
    return arg_path;
}

std::shared_ptr<::cw::data::CWPyElement> create_scenariolog_make_colorelement(::cw::lib::String arg_name, ::cw::lib::Value arg_color)
{
    auto attrs = color_attrs(arg_color);
    if (!attrs.contains("red")) {
        attrs["r"] = "0"; attrs["g"] = "0"; attrs["b"] = "0";
    } else {
        attrs["r"] = attrs["red"]; attrs["g"] = attrs["green"]; attrs["b"] = attrs["blue"];
        attrs.erase("red"); attrs.erase("green"); attrs.erase("blue");
    }
    attrs["a"] = attrs.contains("alpha") ? attrs["alpha"] : "255";
    attrs.erase("alpha");
    return make_element(arg_name, {}, std::move(attrs));
}

void main()
{
}

}

