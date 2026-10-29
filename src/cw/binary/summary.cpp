#include "cw/binary/summary.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/image.hpp"
#include "cw/binary/next/next_binary_reader.hpp"
#include "cw/binary/util.hpp"
#include "cw/data.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace cw::binary::summary {
using I = ::cw::lib::Integer;
using S = ::cw::lib::String;
using E = std::shared_ptr<::cw::data::CWPyElement>;

namespace {
I num(const S& s) {
    return ::cw::lib::require_python_integer_for_cardwirth_pack(s);
}

bool bl(const S& s) {
    S value = s;
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    throw std::invalid_argument("invalid boolean: " + s);
}

S bt(bool value) { return value ? "True" : "False"; }

S required_attr(const E& element, const S& key) {
    if (!element) throw std::invalid_argument("missing XML element");
    // Pythonの出力処理と同様、CWPyElement.get を使う。 属性がなけれ
    // ば空文字列になり、XMLのgetattr検索ではなく数値・真偽値の変換で失敗する。
    return element->get(key, "");
}


}

void Step::initialize(std::shared_ptr<Summary> parent,
                      std::shared_ptr<::cw::binary::cwfile::CWFile> file, bool yadodata) {
    if (!file) throw std::invalid_argument("Step file");
    ::cw::binary::base::CWBinaryBase::initialize(parent, file, yadodata);
    m_xmltype = "Step";
    m_data.reset();
    m_name.clear();
    m_py_default = 0;
    m_variable_names.fill({});
    m_next_variable_names.clear();
    m_expand_spchars = false;
    m_name = file->string();
    m_py_default = file->dword();
    for (auto& value : m_variable_names) value = file->string();
}

// Step はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data 代
// 入とは異なり、不完全なツリーを次回用にキャッシュしない。
E Step::get_data() {
    if (m_data && *m_data) return *m_data;
    auto data = ::cw::data::make_element("Step");
    data->set("default", std::to_string(m_py_default));
    if (m_expand_spchars) data->set("spchars", "True");
    data->append(::cw::data::make_element("Name", m_name));
    const auto& next_values = m_next_variable_names;
    if (!next_values.empty()) {
        for (std::size_t i = 0; i < next_values.size(); ++i)
            data->append(::cw::data::make_element("Value" + std::to_string(i), next_values[i]));
    } else for (I i = 0; i < 10; ++i) {
        data->append(::cw::data::make_element("Value" + std::to_string(i),
                                               m_variable_names[static_cast<std::size_t>(i)]));
    }
    m_data = data;
    return data;
}

void Step::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data) {
    if (!file || !data) throw std::invalid_argument("Step null");
    // Python版と同様、バージョン検査・子の走査・出力より前にdefaultを解析する。
    const auto default_value = num(required_attr(data, "default"));
    if (data->getbool(".", ::cw::lib::Value("spchars"), false)) {
        file->check_wsnversion("2", "ステップ値中の特殊文字の展開");
    }
    if (data->getattr(".", "initialize", "Leave") != "Leave") {
        file->check_wsnversion("4", "ステップの初期化タイミング指定");
    }
    std::array<S, 10> variable_names{};
    S name;
    for (const auto& element : data->m_children) {
        if (!element) continue;
        if (element->m_tag == "Name") {
            name = element->m_text;
        } else if (element->m_tag.rfind("Value", 0) == 0) {
            const auto index_text = element->m_tag.substr(5);
            // Python版の上限のない整数変換は、リストの位置参照より先に成
            // 功する。 文法的に有効だがネイティブ範囲を超える整数は、 整数構
            // 文のエラーや書き込み番兵値ではなく、不正なリスト位置として扱う。
            if (!::cw::lib::normalize_python_integer(index_text))
                throw std::invalid_argument("invalid literal for int()");
            const auto parsed_index = ::cw::lib::parse_python_integer(index_text);
            if (!parsed_index) throw std::out_of_range("Step value index");
            auto index = *parsed_index;
            if (index < 0) index += 10;
            if (index < 0 || index >= 10) throw std::out_of_range("Step value index");
            variable_names[static_cast<std::size_t>(index)] = element->m_text;
        }
    }
    file->write_string(name);
    file->write_dword(default_value);
    for (const auto& value : variable_names) file->write_string(value);
}

void Flag::initialize(std::shared_ptr<Summary> parent,
                      std::shared_ptr<::cw::binary::cwfile::CWFile> file, bool yadodata) {
    if (!file) throw std::invalid_argument("Flag file");
    ::cw::binary::base::CWBinaryBase::initialize(parent, file, yadodata);
    m_xmltype = "Flag";
    m_data.reset();
    m_name.clear();
    m_py_default = false;
    m_variable_names.fill({});
    m_name = file->string();
    m_py_default = file->boolean();
    for (auto& value : m_variable_names) value = file->string();
}

// Flag はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data 代
// 入とは異なり、不完全なツリーを次回用にキャッシュしない。
E Flag::get_data() {
    if (m_data && *m_data) return *m_data;
    auto data = ::cw::data::make_element("Flag");
    data->set("default", bt(m_py_default));
    data->append(::cw::data::make_element("Name", m_name));
    data->append(::cw::data::make_element("True", m_variable_names[0]));
    data->append(::cw::data::make_element("False", m_variable_names[1]));
    m_data = data;
    return data;
}

void Flag::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data) {
    if (!file || !data) throw std::invalid_argument("Flag null");
    // Python版と同様、バージョン検査・子の走査・出力より前にdefaultを解析する。
    const bool default_value = bl(required_attr(data, "default"));
    if (data->getbool(".", ::cw::lib::Value("spchars"), false)) {
        file->check_wsnversion("2", "フラグ値中の特殊文字の展開");
    }
    if (data->getattr(".", "initialize", "Leave") != "Leave") {
        file->check_wsnversion("4", "フラグの初期化タイミング指定");
    }
    S name, true_name, false_name;
    for (const auto& element : data->m_children) {
        if (!element) continue;
        if (element->m_tag == "Name") name = element->m_text;
        else if (element->m_tag == "True") true_name = element->m_text;
        else if (element->m_tag == "False") false_name = element->m_text;
    }
    file->write_string(name);
    file->write_bool(default_value);
    file->write_string(true_name);
    file->write_string(false_name);
}

void Summary::initialize(::cw::lib::Value, std::shared_ptr<::cw::binary::cwfile::CWFile> file,
                         bool yadodata, bool nameonly, S material, bool exportimg, bool wpt120) {
    if (!file) throw std::invalid_argument("Summary file");
    ::cw::binary::base::CWBinaryBase::initialize(std::nullopt, file, yadodata, material, exportimg);
    m_xmltype = "Summary";
    m_nameonly = nameonly;
    m_wpt120 = false;
    m_data.reset();
    m_flags.clear();
    m_steps.clear();
    m_next_numeric_variants.clear();
    m_type = -1;
    m_author.clear();
    m_description.clear();
    m_required_coupons.clear();
    m_required_coupons_num = 0;
    m_area_id = 0;
    m_level_min = 0;
    m_level_max = 0;
    m_version = 0;
    m_skintype.clear();
    m_tags.clear();
    m_imgpath.clear();
    m_image = file->image();
    m_name = file->string();
    if (nameonly) return;
    m_description = file->string();
    m_author = file->string();
    m_required_coupons = file->string(true);
    m_required_coupons_num = file->dword();
    const auto area = file->dword();
    if (area <= 19999) {
        m_version = 0;
        m_area_id = area;
    } else if (area <= 39999) {
        m_version = 2;
        m_area_id = area - 20000;
    // Python版のClassic対応を越える、意図的なNext形式対応の追加。 識別
    // 値はD版の読み取りに合わせ、69999未満をClassic、以上をNextとする。
    } else if (area < 69999) {
        m_version = 4;
        m_area_id = area - 40000;
    } else {
        m_version = 7;
        m_area_id = area - 70000;
    }
    const auto self = std::static_pointer_cast<Summary>(shared_from_this());
    if (m_version >= 7) {
        ::cw::binary::next::Reader next(*file);
        for (int i = 0; i < 20; ++i) (void)file->ubyte();
        const auto step_count = next.ex_uint();
        for (std::uint32_t i = 0; i < step_count; ++i) {
            const auto path = next.ex_string();
            auto type = static_cast<std::uint8_t>(file->ubyte());
            const bool expand_spchars = (type & 0x80U) != 0;
            type &= 0x7fU;
            if (expand_spchars) (void)file->ubyte();
            const auto value_count = next.ex_uint();
            if (type == 1U) {
                auto step = std::make_shared<Step>();
                step->m_name = path;
                step->m_expand_spchars = expand_spchars;
                step->m_next_variable_names.reserve(value_count);
                for (std::uint32_t j = 0; j < value_count; ++j)
                    step->m_next_variable_names.push_back(next.ex_string());
                step->m_py_default = next.ex_uint();
                if (step->m_next_variable_names.empty()) step->m_next_variable_names.emplace_back();
                step->m_py_default = std::clamp<I>(
                    step->m_py_default, 0,
                    static_cast<I>(step->m_next_variable_names.size() - 1));
                m_steps.push_back(std::move(step));
            } else if (type == 2U) {
                m_next_numeric_variants.emplace_back(path, static_cast<I>(next.ex_uint()));
            } else {
                throw std::runtime_error("unsupported CardWirthNext Summary step type: " +
                                         std::to_string(type));
            }
        }
        const auto flag_count = next.ex_uint();
        for (std::uint32_t i = 0; i < flag_count; ++i) {
            auto flag = std::make_shared<Flag>();
            flag->m_name = next.ex_string();
            flag->m_py_default = file->boolean();
            flag->m_variable_names[0] = next.ex_string();
            flag->m_variable_names[1] = next.ex_string();
            m_flags.push_back(std::move(flag));
        }
        m_level_min = next.ex_uint();
        m_level_max = next.ex_uint();
        return;
    }
    auto count = file->dword();
    for (I i = 0; i < count; ++i) {
        auto step = std::make_shared<Step>();
        step->initialize(self, file, yadodata);
        m_steps.push_back(step);
    }
    count = file->dword();
    for (I i = 0; i < count; ++i) {
        auto flag = std::make_shared<Flag>();
        flag->initialize(self, file, yadodata);
        m_flags.push_back(flag);
    }
    if (wpt120) {
        m_wpt120 = true;
        return;
    }
    (void)file->dword();
    if (m_version > 0) {
        m_level_min = file->dword();
        m_level_max = file->dword();
    }
}

// Summary はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
E Summary::get_data() {
    if (m_nameonly || m_wpt120) throw std::logic_error("Summary has no complete XML data");
    if (m_data && *m_data) return *m_data;
    m_imgpath = export_image();
    auto data = ::cw::data::make_element("Summary");
    auto property = ::cw::data::make_element("Property");
    property->append(::cw::data::make_element("Name", m_name));
    property->append(::cw::data::make_element("ImagePath", m_imgpath));
    property->append(::cw::data::make_element("Author", m_author));
    property->append(::cw::data::make_element("Description", m_description));
    auto level = ::cw::data::make_element("Level");
    level->set("min", std::to_string(m_level_min));
    level->set("max", std::to_string(m_level_max));
    property->append(level);
    auto coupons = ::cw::data::make_element("RequiredCoupons", m_required_coupons);
    coupons->set("number", std::to_string(m_required_coupons_num));
    property->append(coupons);
    property->append(::cw::data::make_element("StartAreaId", std::to_string(m_area_id)));
    property->append(::cw::data::make_element("Tags", m_tags));
    property->append(::cw::data::make_element("Type", m_skintype));
    data->append(property);
    auto flags = ::cw::data::make_element("Flags");
    for (const auto& flag : m_flags) if (flag) flags->append(flag->get_data());
    data->append(flags);
    auto steps = ::cw::data::make_element("Steps");
    for (const auto& step : m_steps) if (step) steps->append(step->get_data());
    data->append(steps);
    auto variants = ::cw::data::make_element("Variants");
    for (const auto& [name, value] : m_next_numeric_variants) {
        auto variant = ::cw::data::make_element("Variant");
        variant->set("defaulttype", "Number");
        variant->set("defaultvalue", std::to_string(value));
        variant->set("type", "Number");
        variant->set("value", std::to_string(value));
        variant->append(::cw::data::make_element("Name", name));
        variants->append(std::move(variant));
    }
    // Nextの数値変数はClassic XMLへの拡張。 Flags/Steps/Labels
    // だけを持つClassicの概要へ、空のVariants要素を追加しない。
    if (!m_next_numeric_variants.empty()) data->append(variants);
    data->append(::cw::data::make_element("Labels", ""));
    m_data = data;
    return data;
}

void Summary::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> file, E data) {
    if (!file || !data) throw std::invalid_argument("Summary null");
    std::optional<::cw::lib::Bytes> image;
    S name, description, author, required_coupons;
    I required_coupons_num = 0, area_id = 0, level_min = 0, level_max = 0;
    E steps, flags, variants;
    for (const auto& element : data->m_children) {
        if (!element) continue;
        if (element->m_tag == "Property") {
            for (const auto& property : element->m_children) {
                if (!property) continue;
                if (property->m_tag == "Name") name = property->m_text;
                else if (property->m_tag == "ImagePath" || property->m_tag == "ImagePaths") image = ::cw::binary::base::CWBinaryBase::import_image(file, property);
                else if (property->m_tag == "Author") author = property->m_text;
                else if (property->m_tag == "Description") description = property->m_text;
                else if (property->m_tag == "Level") {
                    level_min = num(required_attr(property, "min"));
                    level_max = num(required_attr(property, "max"));
                } else if (property->m_tag == "RequiredCoupons") {
                    required_coupons = property->m_text;
                    required_coupons_num = num(required_attr(property, "number"));
                } else if (property->m_tag == "StartAreaId") {
                    area_id = num(property->m_text);
                }
            }
        } else if (element->m_tag == "Flags") {
            flags = element;
        } else if (element->m_tag == "Steps") {
            steps = element;
        } else if (element->m_tag == "Variants") {
            variants = element;
        }
    }
    file->write_image(image);
    file->write_string(name);
    file->write_string(description);
    file->write_string(author);
    file->write_string(required_coupons, true);
    file->write_dword(required_coupons_num);
    file->write_dword(::cw::lib::add_cardwirth_resource_id_bias(area_id, 40000));
    file->write_dword(steps ? static_cast<I>(steps->m_children.size()) : 0);
    if (steps) for (const auto& step : steps->m_children) Step::unconv(file, step);
    file->write_dword(flags ? static_cast<I>(flags->m_children.size()) : 0);
    if (flags) for (const auto& flag : flags->m_children) Flag::unconv(file, flag);
    if (variants && !variants->m_children.empty()) file->check_wsnversion("4", "コモン");
    file->write_dword(0);
    file->write_dword(level_min);
    file->write_dword(level_max);
}

void main() {}
}
