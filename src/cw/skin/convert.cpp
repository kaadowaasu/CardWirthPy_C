#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "cw/skin/convert.hpp"
#include "cw/binary/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/util.hpp"
#include "cw/skin/win32res.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using Integer = cw::lib::Integer;
using String = cw::lib::String;
using Bytes = cw::lib::Bytes;
using Element = std::shared_ptr<cw::data::CWPyElement>;
using Tree = std::shared_ptr<cw::data::CWPyElementTree>;
constexpr std::size_t byte_npos = std::numeric_limits<std::size_t>::max();

String path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return String(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

// Pythonソースの b"..." 断片を、実行時にも欠落なく扱うための16進表現。
Bytes hex_bytes(std::string_view text) {
    Bytes result;
    result.reserve(text.size() / 2);
    auto digit = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::invalid_argument("invalid hexadecimal byte");
    };
    if (text.size() % 2 != 0) throw std::invalid_argument("odd hexadecimal byte count");
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>((digit(text[i]) << 4) | digit(text[i + 1])));
    return result;
}

bool classic_version(const std::tuple<cw::lib::Integer, cw::lib::Integer, cw::lib::Integer, cw::lib::Integer>& v) {
    const auto lo = std::tuple<cw::lib::Integer, cw::lib::Integer, cw::lib::Integer, cw::lib::Integer>{1, 2, 8, 0};
    const auto hi = std::tuple<cw::lib::Integer, cw::lib::Integer, cw::lib::Integer, cw::lib::Integer>{1, 3, 99, 99};
    return lo <= v && v <= hi;
}

cw::lib::String lower_ascii(cw::lib::String value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

String decode_classic(const Bytes& bytes) {
    return cw::lib::encoding::cp932_to_utf8(bytes);
}

String binary_text(const Bytes& binary, std::size_t begin, std::size_t end) {
    if (begin > binary.size()) begin = binary.size();
    end = std::min(end, binary.size());
    if (end < begin) end = begin;
    return decode_classic(Bytes(binary.begin() + static_cast<std::ptrdiff_t>(begin),
                                binary.begin() + static_cast<std::ptrdiff_t>(end)));
}

std::size_t find_bytes(const Bytes& source, const Bytes& needle, std::size_t start = 0) {
    if (start > source.size()) return byte_npos;
    const auto it = std::search(source.begin() + static_cast<std::ptrdiff_t>(start), source.end(),
                                needle.begin(), needle.end());
    return it == source.end() ? byte_npos : static_cast<std::size_t>(it - source.begin());
}

std::ptrdiff_t python_slice_index(std::ptrdiff_t index, std::size_t size) {
    const auto length = static_cast<std::ptrdiff_t>(size);
    if (index < 0) index += length;
    return std::clamp(index, std::ptrdiff_t{0}, length);
}

Bytes python_slice(const Bytes& source, std::ptrdiff_t begin, std::ptrdiff_t end) {
    const auto first = python_slice_index(begin, source.size());
    const auto last = python_slice_index(end, source.size());
    if (last <= first) return {};
    return Bytes(source.begin() + first, source.begin() + last);
}

String python_float(::cw::lib::Real value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0.0 ? "-inf" : "inf";
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::defaultfloat << value;
    auto result = stream.str();
    if (result.find('.') == String::npos &&
        result.find('e') == String::npos &&
        result.find('E') == String::npos)
        result += ".0";
    return result;
}

std::int16_t i16(const Bytes& b, std::size_t p) {
    if (p + 2 > b.size()) throw std::out_of_range("classic integer range");
    const std::uint16_t u = static_cast<std::uint16_t>(b[p]) |
                            static_cast<std::uint16_t>(b[p + 1]) << 8;
    return static_cast<std::int16_t>(u);
}

std::int32_t i32(const Bytes& b, std::size_t p) {
    if (p + 4 > b.size()) throw std::out_of_range("classic integer range");
    const std::uint32_t u = static_cast<std::uint32_t>(b[p]) |
                            static_cast<std::uint32_t>(b[p + 1]) << 8 |
                            static_cast<std::uint32_t>(b[p + 2]) << 16 |
                            static_cast<std::uint32_t>(b[p + 3]) << 24;
    return static_cast<std::int32_t>(u);
}

void set_text(const Tree& tree, const String& path, const String& value) {
    if (tree) tree->find_exists(path)->set_text(value);
}

String get_text(const Tree& tree, const String& path, const String& fallback = {}) {
    if (!tree) return fallback;
    const auto found = tree->find(path);
    return found ? (*found)->get_text() : fallback;
}

void set_attr(const Tree& tree, const String& path, const String& attr, const String& value) {
    if (tree) tree->edit(path, value, attr);
}

String trim_classic(String value) {
    // Pythonのstr.strip(" \u3000")相当（UTF-8の全角空白も含む）。
    constexpr std::string_view full_space = "\xe3\x80\x80";
    std::size_t first = 0;
    while (first < value.size()) {
        if (value[first] == ' ') { ++first; continue; }
        if (first + full_space.size() <= value.size() &&
            value.compare(first, full_space.size(), full_space) == 0) {
            first += full_space.size();
            continue;
        }
        break;
    }
    std::size_t last = value.size();
    while (last > first) {
        if (value[last - 1] == ' ') { --last; continue; }
        if (last >= first + full_space.size() &&
            value.compare(last - full_space.size(), full_space.size(), full_space) == 0) {
            last -= full_space.size();
            continue;
        }
        break;
    }
    return value.substr(first, last - first);
}

String replace_all(String value, const String& from, const String& to) {
    if (from.empty()) return value;
    std::size_t at = 0;
    while ((at = value.find(from, at)) != String::npos) {
        value.replace(at, from.size(), to);
        at += to.size();
    }
    return value;
}

String drop_first_utf8(String value) {
    if (value.empty()) return value;
    std::size_t width = 1;
    const auto lead = static_cast<unsigned char>(value[0]);
    if ((lead & 0xe0u) == 0xc0u) width = 2;
    else if ((lead & 0xf0u) == 0xe0u) width = 3;
    else if ((lead & 0xf8u) == 0xf0u) width = 4;
    return value.substr(std::min(width, value.size()));
}

std::filesystem::path relative_to_skinbase(const String& fpath) {
    std::error_code ec;
    const auto base = std::filesystem::u8path("Data/SkinBase");
    auto result = std::filesystem::relative(std::filesystem::u8path(fpath), base, ec);
    if (ec || result.empty() || result.generic_string().find("..") == 0) {
        return std::filesystem::u8path(fpath).filename();
    }
    return result;
}

void copy_tree(const std::filesystem::path& source, const std::filesystem::path& target) {
    if (!std::filesystem::is_directory(source))
        throw std::runtime_error("source directory is missing: " + path_utf8(source));
    if (std::filesystem::exists(target))
        throw std::runtime_error("destination directory already exists: " + path_utf8(target));
    std::filesystem::create_directories(target);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        const auto rel = std::filesystem::relative(entry.path(), source);
        const auto dst = target / rel;
        if (entry.is_directory()) std::filesystem::create_directories(dst);
        else if (entry.is_regular_file()) {
            std::filesystem::create_directories(dst.parent_path());
            std::filesystem::copy_file(entry.path(), dst);
            std::error_code ec;
            const auto modified = std::filesystem::last_write_time(entry.path(), ec);
            if (!ec) {
                ec.clear();
                std::filesystem::last_write_time(dst, modified, ec);
            }
        }
    }
}

void copy_tree_or_mkdir(const std::filesystem::path& source, const std::filesystem::path& target) {
    if (std::filesystem::is_directory(source)) {
        copy_tree(source, target);
        return;
    }
    if (std::filesystem::exists(target))
        throw std::runtime_error("destination directory already exists: " + path_utf8(target));
    std::filesystem::create_directories(target);
}

void copy_contents(const std::filesystem::path& source, const std::filesystem::path& target) {
    if (!std::filesystem::is_directory(source)) return;
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        const auto dst = target / entry.path().filename();
        if (entry.is_directory()) copy_tree(entry.path(), dst);
        else if (entry.is_regular_file()) {
            std::filesystem::copy_file(entry.path(), dst,
                                       std::filesystem::copy_options::overwrite_existing);
            std::error_code ec;
            const auto modified = std::filesystem::last_write_time(entry.path(), ec);
            if (!ec) {
                ec.clear();
                std::filesystem::last_write_time(dst, modified, ec);
            }
        }
    }
}

void write_bytes(const std::filesystem::path& path, const Bytes& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("resource output could not be opened: " + path_utf8(path));
    if (!data.empty()) output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    output.flush();
    if (!output) throw std::runtime_error("resource output failed: " + path_utf8(path));
}

std::pair<Integer, Integer> image_size(const std::filesystem::path& stem) {
    const auto resource = ::cw::util::find_resource(path_utf8(stem), ::cw::g_M_IMG);
    const auto bitmap = ::cw::util::load_wxbmp(
        resource, false, {}, {}, std::nullopt, true, true, false, std::nullopt);
    if (!bitmap) return {0, 0};
    return {bitmap->size.width, bitmap->size.height};
}

Element menu_card(const Tree& tree, Integer index) {
    if (!tree) return {};
    auto cards = tree->find("MenuCards");
    if (!cards) return {};
    Integer seen = 0;
    for (const auto& child : (*cards)->iter(std::nullopt)) {
        if (child && child->get_tag() == "MenuCard" && ++seen == index) return child;
    }
    return {};
}

void put_message(const Tree& tree, const String& key, const std::map<String, String>& values,
                 const String& fallback = {}) {
    const auto found = values.find(key);
    if (found != values.end()) set_text(tree, "Messages/Message", found->second);
    (void)fallback;
}
}

namespace cw::skin::convert {
// Python版のネスト関数を生成ラッパーからも実行できるよう、現在の変換器を保持する。
// Converterはスレッド単位で使われるため、別変換の状態を混ぜない。
static thread_local Converter* s_active_converter = nullptr;

Converter::~Converter()
{
    if (s_active_converter == this) s_active_converter = nullptr;
    dispose();
}

void Converter::initialize(::cw::lib::String arg_exe)
{
    s_active_converter = this;
    m_maximum = 100;
    m_curnum = 0;
    m_message = "変換を開始しています...";
    m_failure = false;
    m_complete = false;
    m_errormessage.clear();
    m_adventurersinn.reset();
    m_partyinfo_res.reset();
    m_version = {1, 2, 8, 0};
    init(std::move(arg_exe));
}

void Converter::py_del()
{
    dispose();
}

void Converter::dispose()
{
    if (m_res && *m_res) (*m_res)->dispose();
    m_res.reset();
}

void Converter::init(::cw::lib::String arg_exe)
{
    s_active_converter = this;
    dispose();
    m_exe = std::move(arg_exe);
    m_exebinary.clear();
    if (!m_exe.empty()) {
        std::ifstream input(std::filesystem::u8path(m_exe), std::ios::binary);
        if (!input) throw std::runtime_error("classic engine executable could not be opened: " + m_exe);
        m_exebinary.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        m_res = std::make_shared<::cw::skin::win32res::Win32Res>(m_exe);
        const auto rc = (*m_res)->get_rcdata(::cw::skin::win32res::g_RT_VERSION, ::cw::lib::Value(1));
        if (!rc || rc->size() < 56) throw std::runtime_error("classic version resource is missing");
        auto u16 = [&rc](std::size_t p) -> ::cw::lib::Integer {
            return static_cast<::cw::lib::Integer>((*rc)[p] | (static_cast<unsigned int>((*rc)[p + 1]) << 8));
        };
        // Python版の struct.unpack("<HHHH", rc[48:56]) と同じ並び替え。
        m_version = {u16(50), u16(48), u16(54), u16(52)};
    }
    m_datadir = find_datadir();
    m_scenariodir = find_scenariodir();
    m_yadodir = find_yadodir();
    m_skintype = find_type();
    m_initialcash = find_initialcash();
    m_skindirname = find_skinname();

    m_data = ::cw::data::xml2etree("Data/SkinBase/Skin.xml", "", std::nullopt, std::nullopt, false);
    if (!m_data) throw std::runtime_error("Data/SkinBase/Skin.xml could not be loaded");
    set_text(m_data, "Property/Name", m_skindirname);
    set_text(m_data, "Property/Type", m_skintype);
    set_text(m_data, "Property/Author", find_author());
    set_text(m_data, "Property/Description", ::cw::util::encodewrap(find_description()));
    set_text(m_data, "Property/InitialCash", std::to_string(m_initialcash));

    m_actioncard = _get_resources("ActionCard");
    m_gameover = _get_resources("GameOver");
    m_scenario = _get_resources("Scenario");
    m_title = _get_resources("Title");
    m_yado = _get_resources("Yado");
    m_specialcard = _get_resources("SpecialCard");
    _get_features();
    _get_sounds();
    _get_messages();
    _get_cards();
    m_adventurersinn.reset();
    _get_bgs();
    m_partyinfo_res.reset();
    _get_partyinfo();
}

std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> Converter::_get_resources(::cw::lib::String arg_dpath)
{
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> result;
    const std::filesystem::path directory = std::filesystem::u8path("Data/SkinBase/Resource/Xml") /
                                            std::filesystem::u8path(arg_dpath);
    std::vector<std::filesystem::path> paths;
    if (::cardwirth::platform::is_mounted_user_path(directory)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(directory);
        if (!status || !status->is_directory())
            throw std::runtime_error("skin resource directory is missing: " + path_utf8(directory));
        const auto entries = ::cardwirth::platform::list_mounted_user_path(directory);
        if (!entries)
            throw std::runtime_error("skin resource directory cannot be read: " + path_utf8(directory));
        for (const auto& entry : *entries)
            if (entry.stat.is_file()) paths.push_back(entry.path);
    } else {
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("skin resource directory is missing: " + path_utf8(directory));
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file()) paths.push_back(entry.path());
    }
    for (const auto& path : paths) {
        if (lower_ascii(path_utf8(path.extension())) != ".xml") continue;
        auto tree = ::cw::data::xml2etree(path_utf8(path), "", std::nullopt, std::nullopt, false);
        if (!tree) throw std::runtime_error("skin resource XML could not be loaded: " + path_utf8(path));
        result[path_utf8(path.stem())] = std::move(tree);
    }
    return result;
}

void Converter::_write_data(::cw::lib::String arg_dpath, std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElementTree>> arg_table)
{
    for (const auto& [unused_name, tree] : arg_table) {
        (void)unused_name;
        if (!tree) continue;
        const auto rel = relative_to_skinbase(tree->m_fpath);
        const auto destination = std::filesystem::u8path(arg_dpath) / rel;
        std::filesystem::create_directories(destination.parent_path());
        tree->m_fpath = path_utf8(destination);
        tree->write_file(tree->m_fpath);
    }
}

::cw::lib::String Converter::find_skinname()
{
    if (m_exe.empty()) return "Default";
    const auto stem = path_utf8(std::filesystem::u8path(m_exe).stem());
    return stem.empty() ? "Default" : stem;
}

::cw::lib::String Converter::find_description()
{
    if (m_exe.empty()) return {};
    return path_utf8(std::filesystem::u8path(m_exe).filename()) + "をベースに自動生成したスキン。";
}

::cw::lib::String Converter::find_datadir()
{
    if (!m_exe.empty() && classic_version(m_version)) {
        static const ::cw::lib::Bytes key{'\\','M','i','d','i','\\','D','e','f','R','e','s','e','t','.','m','i','d'};
        const auto found = find_bytes(m_exebinary, key);
        const auto index = found == byte_npos ? std::ptrdiff_t{-1} : static_cast<std::ptrdiff_t>(found);
        try {
            return decode_classic(python_slice(m_exebinary, index - 4, index));
        } catch (const std::exception&) {
            // Python と同様、切り出した結果のデコードに失敗した場合だけ代替処理へ進む。
        }
    }
    return "Data";
}

::cw::lib::String Converter::find_scenariodir()
{
    if (!m_exe.empty() && classic_version(m_version)) {
        static const ::cw::lib::Bytes key{0,'\\',0,'\\',0,'\\','S','u','m','m','a','r','y','.','w','s','m',0,'\\',0,'\\',0,'.','w','i','d',0};
        const auto found = find_bytes(m_exebinary, key);
        const auto index = found == byte_npos ? std::ptrdiff_t{-1} : static_cast<std::ptrdiff_t>(found);
        try {
            const auto begin = index + static_cast<std::ptrdiff_t>(key.size());
            return decode_classic(python_slice(m_exebinary, begin, begin + 8));
        } catch (const std::exception&) {
            // Python と同様、切り出した結果のデコードに失敗した場合だけ代替処理へ進む。
        }
    }
    return "Scenario";
}

::cw::lib::String Converter::find_yadodir()
{
    if (!m_exe.empty() && classic_version(m_version)) {
        static const ::cw::lib::Bytes key{'\\',0,'\\','E','n','v','i','r','o','n','m','e','n','t','.','w','y','d',0};
        const auto found = find_bytes(m_exebinary, key);
        const auto index = found == byte_npos ? std::ptrdiff_t{-1} : static_cast<std::ptrdiff_t>(found);
        try {
            const auto end = index + static_cast<std::ptrdiff_t>(key.size());
            return decode_classic(python_slice(m_exebinary, end - static_cast<std::ptrdiff_t>(key.size()) - 4,
                                               end - static_cast<std::ptrdiff_t>(key.size())));
        } catch (const std::exception&) {
            // Python と同様、切り出した結果のデコードに失敗した場合だけ代替処理へ進む。
        }
    }
    return "Yado";
}

::cw::lib::String Converter::find_type()
{
    if (!m_exe.empty()) {
        auto name = lower_ascii(path_utf8(std::filesystem::u8path(m_exe).stem()));
        if (name == "s_c_wirth") return "School";
        if (name == "modernwirth") return "Modern";
        if (name == "darkwirth") return "Monsters";
        if (name == "oedowirth") return "Oedo";
        if (lower_ascii(path_utf8(std::filesystem::u8path(m_exe).parent_path())).find("sfv") != std::string::npos) return "ScienceFiction";
    }
    return "MedievalFantasy";
}

::cw::lib::String Converter::find_author()
{
    return {};
}

::cw::lib::Integer Converter::find_initialcash()
{
    const auto tree = ::cw::data::xml2etree("Data/SkinBase/Skin.xml", "", std::nullopt, std::nullopt, false);
    const auto property = tree ? tree->find("Property/InitialCash") : std::nullopt;
    const auto text = property ? (*property)->get_text() : String("4000");
    const auto cash = static_cast<::cw::lib::Integer>(std::stoll(text));
    if (!m_exe.empty() && m_exebinary.size() >= 0x31d97u + 4u && classic_version(m_version)) {
        const auto p = static_cast<std::size_t>(0x31d97);
        return static_cast<::cw::lib::Integer>(m_exebinary[p] | (static_cast<unsigned int>(m_exebinary[p + 1]) << 8)
            | (static_cast<unsigned int>(m_exebinary[p + 2]) << 16) | (static_cast<unsigned int>(m_exebinary[p + 3]) << 24));
    }
    return cash;
}

void Converter::_get_features()
{
    if (m_exe.empty() || !classic_version(m_version) || !m_data || !m_res || !*m_res) return;
    try {
        const Bytes key{'T','S','t','a','t','u','s','I','t','e','m',0x81,0x89};
        const auto found = find_bytes(m_exebinary, key);
        const auto marker = found == byte_npos ? std::ptrdiff_t{-1} : static_cast<std::ptrdiff_t>(found);
        std::size_t index = static_cast<std::size_t>(marker + static_cast<std::ptrdiff_t>(key.size()) - 2);

        const auto set_params = [&](const Element& data, bool isnature,
                                    const std::array<String, 5>& slist,
                                    ::cw::lib::Real sperb) {
            if (!data) throw std::runtime_error("feature element is missing");
            if (index + 20 + 12 + 10 > m_exebinary.size())
                throw std::out_of_range("feature parameter range");
            const auto name_end = std::find(m_exebinary.begin() + static_cast<std::ptrdiff_t>(index),
                                            m_exebinary.begin() + static_cast<std::ptrdiff_t>(index + 20), 0);
            const auto name_last = name_end;
            const auto name = trim_classic(decode_classic(Bytes(
                m_exebinary.begin() + static_cast<std::ptrdiff_t>(index), name_last)));
            index += 20;
            data->find_exists("./Name")->set_text(name);
            const std::array<std::int16_t, 6> physical{
                i16(m_exebinary, index), i16(m_exebinary, index + 2), i16(m_exebinary, index + 4),
                i16(m_exebinary, index + 6), i16(m_exebinary, index + 8), i16(m_exebinary, index + 10)};
            index += 12;
            auto p = data->find_exists("./Physical");
            const std::array<String, 6> pnames{"dex", "agl", "int", "str", "vit", "min"};
            for (std::size_t n = 0; n < pnames.size(); ++n)
                p->set(pnames[n], std::to_string(static_cast<Integer>(physical[n]) - (isnature ? 6 : 0)));
            const std::array<std::int16_t, 5> mental{
                i16(m_exebinary, index), i16(m_exebinary, index + 2), i16(m_exebinary, index + 4),
                i16(m_exebinary, index + 6), i16(m_exebinary, index + 8)};
            index += 10;
            p = data->find_exists("./Mental");
            const std::array<std::int16_t, 5> reordered{mental[0], mental[3], mental[2], mental[1], mental[4]};
            for (std::size_t n = 0; n < slist.size(); ++n)
                p->set(slist[n], python_float(static_cast<::cw::lib::Real>(reordered[n]) / sperb));
        };

        const Bytes coupon_key{
            0x00,'I','M','A','G','E','_','F','A','T','H','E','R',0x00,'I','M','A','G','E','_','M','O',
            'T','H','E','R',0x00,0x81,0x40,0x81,0x40,0x81,0x40,0x81,0x40,0x81,0x40,0x81,0x40,
            0x81,0x40,0x81,0x40,0x00,0x00,0x81,0x51,0x00,0x81,0x51,0x00,0x81,0x51,0x00,0x81,0x51,
            0x00,0x81,0x40,0x00};
        const auto coupon = find_bytes(m_exebinary, coupon_key);
        if (coupon != byte_npos) {
            auto index2 = coupon + coupon_key.size();
            const auto read = [&](std::size_t& p) {
                const auto result = _get_text(static_cast<::cw::lib::Integer>(p), false);
                p = static_cast<std::size_t>(std::get<1>(result));
                return std::get<0>(result);
            };
            const auto skillful = read(index2);
            if (const auto e = m_data->find("Periods/Period[3]/Coupons/Coupon")) (*e)->set_text(skillful);
            const auto foxy = read(index2);
            if (const auto e = m_data->find("Periods/Period[4]/Coupons/Coupon")) (*e)->set_text(foxy);
        }

        const std::array<String, 5> default_mental{"aggressive", "cautious", "brave", "cheerful", "trickish"};
        const auto load_section = [&](const String& path, bool nature, ::cw::lib::Real sperb = 2.0) {
            const auto section = m_data->find(path);
            if (!section) return;
            // PythonのElementTree直下反復（selfを含まない）に合わせる。
            for (const auto& item : (*section)->m_children) set_params(item, nature, default_mental, sperb);
        };
        load_section("Sexes", false);
        load_section("Periods", false);
        index += 20 + 12 + 10; // 未使用の「古老」
        load_section("Natures", true);
        load_section("Makings", false);
        load_section("SampleTypes", true, 1.0);

        const std::vector<std::pair<String, String>> bases{
            {"Natures/Nature[8]/BaseNatures/BaseNature[1]", "Natures/Nature[1]/Name"},
            {"Natures/Nature[8]/BaseNatures/BaseNature[2]", "Natures/Nature[2]/Name"},
            {"Natures/Nature[9]/BaseNatures/BaseNature[1]", "Natures/Nature[3]/Name"},
            {"Natures/Nature[9]/BaseNatures/BaseNature[2]", "Natures/Nature[4]/Name"},
            {"Natures/Nature[10]/BaseNatures/BaseNature[1]", "Natures/Nature[5]/Name"},
            {"Natures/Nature[10]/BaseNatures/BaseNature[2]", "Natures/Nature[6]/Name"}};
        for (const auto& [dst, src] : bases) {
            set_text(m_data, dst, get_text(m_data, src));
        }

        const auto entrydlg = (*m_res)->get_tpf0form(Bytes{'T','E','N','T','R','Y','D','L','G'});
        if (entrydlg) {
            const auto type_sheet = ::cw::skin::win32res::find_res_7(
                *entrydlg, {Bytes{'E','n','t','r','y','D','l','g'}, Bytes{'P','a','g','e','C','o','n','t','r','o','l'},
                            Bytes{'T','y','p','e','S','h','e','e','t'}}, std::make_shared<::cw::skin::win32res::ResTable>());
            const std::vector<std::pair<String, String>> labels{
                {"Natures/Nature[1]/Description", "Type3Label"}, {"Natures/Nature[2]/Description", "Type2Label"},
                {"Natures/Nature[3]/Description", "Type1Label"}, {"Natures/Nature[4]/Description", "Type0Label"},
                {"Natures/Nature[5]/Description", "Type4Label"}, {"Natures/Nature[6]/Description", "Type5Label"}};
            for (const auto& [dst, label] : labels) {
                auto fallback = get_text(m_data, dst);
                fallback = ::cw::skin::win32res::find_res_4(type_sheet,
                    {Bytes(label.begin(), label.end()), Bytes{'C','a','p','t','i','o','n'}}, fallback);
                set_text(m_data, dst, fallback);
            }
        } else {
            std::cout << "TENTRYDLG" << std::endl;
        }
    } catch (const std::exception&) {
        // Python版と同じく、途中のstruct.unpack失敗以降はこの処理を中断する。
        ::cw::util::print_ex(std::nullopt);
    }
}

void Converter::_get_sounds()
{
    if (m_exe.empty() || !classic_version(m_version) || !m_data) return;
    try {
        const auto sounds = m_data->find_exists("Sounds");
        const auto key_before = [&](const Element& e, const Bytes& key, Integer length, Integer less = 0) {
            const auto at = find_bytes(m_exebinary, key);
            if (!e || at == byte_npos || less < 0 || static_cast<std::size_t>(less) > at ||
                static_cast<std::size_t>(length) > at - static_cast<std::size_t>(less)) return;
            const auto end = at - static_cast<std::size_t>(less);
            e->set_text(decode_classic(Bytes(m_exebinary.begin() + static_cast<std::ptrdiff_t>(end - length),
                                               m_exebinary.begin() + static_cast<std::ptrdiff_t>(end))));
        };
        const auto key_after = [&](const Element& e, const Bytes& key, Integer length, Integer than = 0) {
            const auto at = find_bytes(m_exebinary, key);
            if (!e || at == byte_npos || than < 0) return;
            const auto begin = at + key.size() + static_cast<std::size_t>(than);
            if (begin > m_exebinary.size() || static_cast<std::size_t>(length) > m_exebinary.size() - begin) return;
            e->set_text(decode_classic(Bytes(m_exebinary.begin() + static_cast<std::ptrdiff_t>(begin),
                                               m_exebinary.begin() + static_cast<std::ptrdiff_t>(begin + length))));
        };
        const auto sound = [&](Integer n) -> Element {
            const auto found = sounds->find("Sound[" + std::to_string(n + 1) + "]");
            return found ? *found : Element{};
        };
        const Bytes dead{'.','w','a','v',0,0x82,0xcd,0x81,0x41,0x8d,0x73,0x93,0xae,0x95,0x73,0x94,0x5c,0x82,0xc5,0x82,0xb7,0x81,0x42,0};
        key_before(sound(0), dead, 16);
        key_before(sound(1), dead, 18, 16 + 5);
        const Bytes signal{'.','w','a','v',0,0x96,0x7b,0x83,0x41,0x83,0x76,0x83,0x8a,0x83,0x50,0x81,0x5b,0x83,0x56,0x83,0x87,0x83,0x93,0x82,0xcd,0x81,0x77,0x8f,0xac,0x82,0xb3,0x82,0xa2,0x83,0x74,0x83,0x48,0x83,0x93,0x83,0x67,0x81,0x78,0x82,0xc9,0x91,0xce,0x89,0x9e,0x82,0xb5,0x82,0xc4,0x82,0xa2,0x82,0xdc,0x82,0xb7,0x81,0x42};
        key_before(sound(2), signal, 18);
        key_before(sound(6), signal, 16, 18 + 8);
        const Bytes resist{0x8e,0x80,0x8e,0xd2,0x97,0x4c,0x8c,0xf8,0,0x92,0xef,0x8d,0x52,0x97,0x4c,0x8c,0xf8,0};
        key_after(sound(3), resist, 14);
        key_after(sound(11), resist, 14, 14 + 5);
        const Bytes page{'.','w','a','v',0,'C','H','E','C','K','_','F','I','X','E','D',0,'C','H','E','C','K','_','T','A','R','G','E','T',0};
        key_before(sound(4), page, 18);
        const Bytes harvest{'.','w','a','v',0,'T','M','a','i','n','W','i','n','d','o','w',0,'T','B','o','o','k','D','l','g',0,0x8f,0xf3,0x91,0xd4,0};
        key_before(sound(5), harvest, 14);
        const Bytes battle{'.','w','a','v',0,'E','n','c','o','u','n','t','e','r',0,'0',0,0,'R','o','u','n','d',' ',0};
        key_before(sound(7), battle, 14);
        const Bytes equip{0,0x81,0x51,0x82,0x51,0,0x81,0x51,0x82,0x52,0,0x81,0x51,0x82,0x53,0,0x81,0x51,0x82,0x54,0,0x81,0x51,0x82,0x55,0,0x88,0xd9,0x8f,0xed,0x94,0xad,0x90,0xb6,0};
        key_after(sound(8), equip, 14);
        key_after(sound(12), equip, 12, 41);
        const Bytes effect{'S','I','G','N','_','P','E','N','A','L','T','Y',0,'S','I','G','N','_','R','A','R','E',0,'S','I','G','N','_','P','R','E','M','I','E','R',0,0};
        key_after(sound(13), effect, 12, 75);
        const Bytes runaway{'.','w','a','v',0,'T','I','T','L','E','_','C','A','R','D','1',0,'T','I','T','L','E','_','C','A','R','D','1',0,'T','I','T','L','E','_','C','A','R','D','2',0};
        key_before(sound(9), runaway, 14, 16 + 5);
        const Bytes discard{0,0x82,0xf0,0x8e,0xcc,0x82,0xc4,0x82,0xdc,0x82,0xb7,0x81,0x42,0x82,0xe6,0x82,0xeb,0x82,0xb5,0x82,0xa2,0x82,0xc5,0x82,0xb7,0x82,0xa9,0x81,0x48,0};
        key_after(sound(10), discard, 14);
    } catch (const std::exception&) {
        // 一部派生版ではSound要素の数が異なるため、取得できた値だけを採用する。
        ::cw::util::print_ex(std::nullopt);
    }
}

void Converter::_get_partyinfo()
{
    if (m_exe.empty() || !classic_version(m_version)) return;
    try {
        const Bytes key{0,'I','M','A','G','E','_','C','O','M','M','A','N','D','3',0};
        const auto at = find_bytes(m_exebinary, key);
        if (at == byte_npos) return;
        const auto value = binary_text(m_exebinary, at + key.size() + 98,
                                       std::min(m_exebinary.size(), at + key.size() + 98 + 12));
        if (!value.empty() && value != "IMAGE_FATHER") m_partyinfo_res = value;
    } catch (const std::exception&) {
        ::cw::util::print_ex(std::nullopt);
    }
}

void Converter::_get_cards()
{
    if (m_exe.empty() || !classic_version(m_version)) return;
    try {
        const Bytes action_key{0,'C','A','R','D','_','S','K','I','L','L',0,'C','A','R','D','_','A','C','T','I','O','N',0,'I','M','A','G','E','_','A','C','T','I','O','N',0};
        auto index = find_bytes(m_exebinary, action_key);
        const auto get_text_at = [&](std::size_t& p, bool cutzero = false) {
            const auto result = _get_text(static_cast<::cw::lib::Integer>(p), cutzero);
            p = static_cast<std::size_t>(std::get<1>(result));
            return std::get<0>(result);
        };
        if (index != byte_npos) index += action_key.size();
        const auto action = [&](const String& cardkey, Integer keycount) {
            if (index == byte_npos) return;
            const auto name = get_text_at(index, true), desc = get_text_at(index, true);
            const auto sound1 = get_text_at(index), sound2 = get_text_at(index);
            std::vector<String> keycodes;
            for (Integer n = 0; n < keycount; ++n) {
                keycodes.push_back(get_text_at(index));
            }
            const auto tree = m_actioncard.find(cardkey);
            if (tree == m_actioncard.end() || !tree->second)
                throw std::out_of_range("action card resource is missing: " + cardkey);
            String joined;
            for (std::size_t n = 0; n < keycodes.size(); ++n) {
                if (n != 0) joined += "\n";
                joined += keycodes[n];
            }
            set_text(tree->second, "Property/Name", name);
            set_text(tree->second, "Property/Description", desc);
            set_text(tree->second, "Property/SoundPath", sound1);
            set_text(tree->second, "Property/SoundPath2", sound2);
            set_text(tree->second, "Property/KeyCodes", ::cw::util::encodewrap(joined));
        };
        if (index != byte_npos) {
            action("00_Exchange", 1); action("01_Attack", 1); action("02_PowerfulAttack", 1);
            action("03_CriticalAttack", 1); action("04_Feint", 2); action("05_Defense", 1);
            action("06_Distance", 1); action("-1_Confuse", 1); action("07_Runaway", 1);
        }

        const Bytes menu_key{'.','w','a','v',0,'E','n','c','o','u','n','t','e','r',0,'0',0,0,'R','o','u','n','d',' ',0};
        index = find_bytes(m_exebinary, menu_key);
        if (index == byte_npos) return;
        index += menu_key.size();
        const auto menu = [&](const std::vector<std::tuple<Tree, Integer>>& areas) {
            for (const auto& [tree, number] : areas) {
                if (!tree || !menu_card(tree, number))
                    throw std::out_of_range("menu card resource is missing");
            }
            const auto name = get_text_at(index, true), desc = get_text_at(index, true);
            (void)get_text_at(index);
            for (const auto& [tree, number] : areas) {
                const auto card = menu_card(tree, number);
                card->find_exists("Property/Name")->set_text(name);
                card->find_exists("Property/Description")->set_text(desc);
            }
        };
        menu({{m_title["01_Title"],1}}); menu({{m_title["01_Title"],2}});
        menu({{m_yado["01_Yado"],1},{m_yado["02_Yado2"],1},{m_yado["03_YadoInitial"],1}});
        menu({{m_yado["01_Yado"],3},{m_yado["02_Yado2"],3}}); menu({{m_yado["02_Yado2"],5}});
        menu({{m_yado["01_Yado"],4},{m_yado["02_Yado2"],6}});
        menu({{m_scenario["-4_Camp"],1},{m_yado["02_Yado2"],7}}); menu({{m_scenario["-4_Camp"],2}});
        menu({{m_scenario["-4_Camp"],5},{m_yado["02_Yado2"],10}});
        menu({{m_yado["01_Yado"],6},{m_yado["03_YadoInitial"],2}}); menu({{m_yado["02_Yado2"],4}});
        menu({{m_scenario["-4_Camp"],4},{m_yado["01_Yado"],5},{m_yado["02_Yado2"],9}});
        menu({{m_yado["01_Yado"],2},{m_yado["02_Yado2"],2}});
        menu({{m_scenario["-4_Camp"],3},{m_yado["02_Yado2"],8}});
        menu({{m_scenario["-5_TradeArea"],1},{m_yado["-2_TradeArea2"],2}});
        menu({{m_yado["-1_TradeArea"],1},{m_yado["-2_TradeArea2"],1}});
        menu({{m_scenario["-5_TradeArea"],2},{m_yado["-1_TradeArea"],3},{m_yado["-2_TradeArea2"],4}});
        menu({{m_yado["-1_TradeArea"],2},{m_yado["-2_TradeArea2"],3}}); menu({{m_yado["-3_PartyBreakup"],1}});
    } catch (const std::exception&) {
        // Python版と同じく、欠落したカードでこの抽出段階を中断する。
        ::cw::util::print_ex(std::nullopt);
    }
}

void Converter::_get_bgs()
{
    if (m_exe.empty() || !classic_version(m_version)) return;
    try {
        const Bytes key{0,0,0,'M','a','p','O','f','W','i','r','t','h','.','b','m','p',0,0,'.','b','m','p',0,'.','B','M','P',0,'M','a','p','O','f','W','i','r','t','h','.','b','m','p',0};
        const auto at = find_bytes(m_exebinary, key);
        if (at == byte_npos) return;
        auto begin = at + key.size();
        const auto result = _get_text(static_cast<::cw::lib::Integer>(begin), true);
        m_adventurersinn = std::get<0>(result);
    } catch (const std::exception&) {
        ::cw::util::print_ex(std::nullopt);
    }
}

void Converter::_get_messages()
{
    if (m_exe.empty() || !classic_version(m_version) || !m_data || !m_res || !*m_res) return;
    try {
        std::map<String, String> msgtable;
        auto read_text = [&](std::size_t& p, bool cutzero = false) {
            const auto result = _get_text(static_cast<::cw::lib::Integer>(p), cutzero);
            p = static_cast<std::size_t>(std::get<1>(result));
            return std::get<0>(result);
        };
        auto read_after = [&](const Bytes& key, Integer count) {
            std::vector<String> result;
            const auto at = find_bytes(m_exebinary, key);
            if (at == byte_npos) return result;
            std::size_t p = at + key.size();
            for (Integer n = 0; n < count; ++n)
                result.push_back(read_text(p, true));
            return result;
        };
        auto required = [](const std::vector<String>& list, std::size_t n) -> const String& {
            if (n >= list.size()) throw std::out_of_range("classic message list is incomplete");
            return list[n];
        };

        // ゲームオーバー本文と3つの遷移ボタン名を読み取る。
        const auto over = find_bytes(m_exebinary, hex_bytes("00494d4147455f4f56455200"));
        if (over != byte_npos) {
            std::size_t p = over + 12;
            const auto msg = read_text(p), goyado = read_text(p), load = read_text(p), end = read_text(p);
            const auto it = m_gameover.find("01_GameOver");
            if (it != m_gameover.end() && it->second) {
                const auto talk = it->second->find_exists("Events/Event//Talk");
                talk->find_exists("Text")->set_text(::cw::util::encodewrap("\n\n\n" + msg));
                talk->find_exists("Contents/Post[1]")->set("name", goyado);
                talk->find_exists("Contents/Post[2]")->set("name", load);
                talk->find_exists("Contents/Post[4]")->set("name", end);
            }
        }

        struct ResourceMessage { const char* key; const char* path; const char* from; const char* to; };
        // Resource/TPF0のメッセージ一覧（Pythonのrsrcmsgsを省略しない）。
        const std::vector<ResourceMessage> resources{
            {"message", "TCAUTIONDLG/CautionDlg/Caption", nullptr, nullptr},
            {"decide", "TBOOKDLG/BookDlg/PartyPanel/Party_OpenBtn/Caption", nullptr, nullptr},
            {"yes", "TCAUTIONDLG/CautionDlg/YesBtn/Caption", nullptr, nullptr},
            {"no", "TCAUTIONDLG/CautionDlg/NoBtn/Caption", nullptr, nullptr},
            {"close", "TCAUTIONDLG/CautionDlg/OkBtn/Caption", nullptr, nullptr},
            {"cancel", "TACTIONDLG/ActionDlg/CancelBtn/Caption", nullptr, nullptr},
            {"coution", "TREPAIRDLG/RepairDlg/RepairBTitle/Caption", nullptr, nullptr},
            {"sex", "TREPAIRDLG/RepairDlg/SexGroup/Caption", nullptr, nullptr},
            {"age", "TREPAIRDLG/RepairDlg/AgeGroup/Caption", nullptr, nullptr},
            {"description", "TBILLDLG/BillDlg/WorkPanel/Work_MemoBtn/Caption", nullptr, nullptr},
            {"extension", "TBILLDLG/BillDlg/yadoPanel/yado_EditBtn/Caption", nullptr, nullptr},
            {"history", "TSTATUSDLG/StatusDlg/ChannelPanel/CareerBtn/Caption", nullptr, nullptr},
            {"status", "TSTATUSDLG/StatusDlg/ChannelPanel/MultiBtn/Caption", nullptr, nullptr},
            {"skills", "TSTATUSDLG/StatusDlg/ChannelPanel/SkillBtn/Caption", nullptr, nullptr},
            {"items", "TSTATUSDLG/StatusDlg/ChannelPanel/ItemBtn/Caption", nullptr, nullptr},
            {"beasts", "TSTATUSDLG/StatusDlg/ChannelPanel/BeastBtn/Caption", nullptr, nullptr},
            {"delete", "TBOOKDLG/BookDlg/MemberPanel/Member_DeleteBtn/Caption", nullptr, nullptr},
            {"information", "TBOOKDLG/BookDlg/MemberPanel/Member_InfoBtn/Caption", nullptr, nullptr},
            {"new", "TBOOKDLG/BookDlg/MemberPanel/Member_EntryBtn/Caption", nullptr, nullptr},
            {"add_member", "TBOOKDLG/BookDlg/MemberPanel/Member_JoinBtn/Caption", nullptr, nullptr},
            {"members", "TBOOKDLG/BookDlg/PartyPanel/Party_MemberBtn/Caption", nullptr, nullptr},
            {"create_base_title", "TSTARTDLG/StartDlg/Caption", nullptr, nullptr},
            {"create_base_message_1", "TSTARTDLG/StartDlg/TitleLabel/Caption", nullptr, nullptr},
            {"create_base_message_2", "TSTARTDLG/StartDlg/NameLabel/Caption", nullptr, nullptr},
            {"card_control", "TCARDDLG/CardDlg/Caption", nullptr, nullptr},
            {"send_to", "TCARDDLG/CardDlg/TargetPanel/TargetLabel/Caption", nullptr, nullptr},
            {"card_information", "TCAPTIONDLG/CaptionDlg/Caption", nullptr, nullptr},
            {"entry_title", "TENTRYDLG/EntryDlg/Caption", nullptr, nullptr},
            {"entry_message", "TENTRYDLG/EntryDlg/PageControl/DefaultSheet/DefaultTitle/Caption", nullptr, nullptr},
            {"design_title", "TDESIGNDLG/DesignDlg/Caption", nullptr, nullptr},
            {"edit_character_message", "TDESIGNDLG/DesignDlg/TitleLabel/Caption", nullptr, nullptr},
            {"entry_name", "TDESIGNDLG/DesignDlg/NameLabel/Caption", nullptr, nullptr},
            {"entry_image", "TDESIGNDLG/DesignDlg/ImageLabel/Caption", nullptr, nullptr},
            {"entry_comment", "TDESIGNDLG/DesignDlg/CommentLabel/Caption", nullptr, nullptr},
            {"entry_sex", "TENTRYDLG/EntryDlg/PageControl/DefaultSheet/SexTitle/Caption", nullptr, nullptr},
            {"entry_age", "TENTRYDLG/EntryDlg/PageControl/DefaultSheet/AgeTitle/Caption", nullptr, nullptr},
            {"entry_cancel", "TENTRYDLG/EntryDlg/ButtonPanel/CancelBtn/Caption", nullptr, nullptr},
            {"entry_decide", "TENTRYDLG/EntryDlg/ButtonPanel/SaveBtn/Caption", nullptr, nullptr},
            {"entry_next", "TENTRYDLG/EntryDlg/ButtonPanel/ForeBtn/Caption", nullptr, nullptr},
            {"entry_previous", "TENTRYDLG/EntryDlg/ButtonPanel/BackBtn/Caption", nullptr, nullptr},
            {"relation_title", "TENTRYDLG/EntryDlg/PageControl/ParentSheet/ParentTitle/Caption", nullptr, nullptr},
            {"relation_message", "TENTRYDLG/EntryDlg/PageControl/ParentSheet/ParentLabel/Caption", nullptr, nullptr},
            {"father", "TENTRYDLG/EntryDlg/PageControl/ParentSheet/FatherTitle/Caption", nullptr, nullptr},
            {"mother", "TENTRYDLG/EntryDlg/PageControl/ParentSheet/MotherTitle/Caption", nullptr, nullptr},
            {"consumption_ep", "TENTRYDLG/EntryDlg/PageControl/ParentSheet/FatherExtra/Caption", "60", "%s"},
            {"nature_title", "TENTRYDLG/EntryDlg/PageControl/TypeSheet/TypeTitle/Caption", nullptr, nullptr},
            {"nature_message", "TENTRYDLG/EntryDlg/PageControl/TypeSheet/TypeComment/Caption", nullptr, nullptr},
            {"making_title", "TENTRYDLG/EntryDlg/PageControl/MarkSheet/MarkTitle/Caption", nullptr, nullptr},
            {"making_message", "TENTRYDLG/EntryDlg/PageControl/MarkSheet/MarkLabel/Caption", nullptr, nullptr},
            {"character_information", "TSTATUSDLG/StatusDlg/Caption", nullptr, nullptr},
            {"insufficiency_title", "TREPAIRDLG/RepairDlg/Caption", nullptr, nullptr},
            {"insufficiency_message", "TREPAIRDLG/RepairDlg/CommentLabel/Caption", nullptr, nullptr},
            {"instructions", "TMEMODLG/MemoDlg/TargetPanel/Caption", nullptr, nullptr},
            {"referencing_file", "TMEMODLG/MemoDlg/TargetPanel/TargetLabel/Caption", nullptr, nullptr},
            {"party_information", "TPARTYDLG/PartyDlg/Caption", nullptr, nullptr},
            {"party_money", "TPARTYDLG/PartyDlg/MoneyLabel/Caption", nullptr, nullptr},
            {"party_name", "TPARTYDLG/PartyDlg/NameLabel/Caption", nullptr, nullptr},
            {"table", "TMAINWINDOW/MainWindow/ButtonControl/NormalSheet/TableBtn/Caption", nullptr, nullptr},
            {"camp", "TMAINWINDOW/MainWindow/ButtonControl/NormalSheet/CampBtn/Caption", nullptr, nullptr},
            {"start_action", "TMAINWINDOW/MainWindow/ButtonControl/BattleSheet/BattleBtn/Caption", nullptr, nullptr},
            {"runaway", "TMAINWINDOW/MainWindow/ButtonControl/BattleSheet/EscapeBtn/Caption", nullptr, nullptr},
            {"round", "TMAINWINDOW/MainWindow/ButtonControl/BattleSheet/RoundPanel/Caption", "00", "%s"},
            {"skillcard", "TCARDDLG/CardDlg/TablePanel/SpeedPanel/SkillBtn/Hint", nullptr, nullptr},
            {"itemcard", "TCARDDLG/CardDlg/TablePanel/SpeedPanel/ItemBtn/Hint", nullptr, nullptr},
            {"beastcard", "TCARDDLG/CardDlg/TablePanel/SpeedPanel/BeastBtn/Hint", nullptr, nullptr}};
        std::map<String, std::shared_ptr<::cw::skin::win32res::ResTable>> forms;
        for (const auto& spec : resources) {
            const String path = spec.path;
            std::vector<Bytes> parts;
            std::size_t begin = 0;
            while (begin < path.size()) {
                const auto slash = path.find('/', begin);
                const auto part = path.substr(begin, slash == String::npos ? slash : slash - begin);
                parts.emplace_back(part.begin(), part.end());
                if (slash == String::npos) break;
                begin = slash + 1;
            }
            if (parts.empty()) continue;
            const String form_name(parts[0].begin(), parts[0].end());
            auto form = forms.find(form_name);
            if (form == forms.end()) form = forms.emplace(form_name, (*m_res)->get_tpf0form(parts[0]).value_or(nullptr)).first;
            if (!form->second) continue;
            const String missing("\x1f__missing__");
            auto value = ::cw::skin::win32res::find_res_4(form->second,
                std::vector<Bytes>(parts.begin() + 1, parts.end()), missing);
            if (value == missing) continue;
            if (spec.from) {
                String replaced;
                std::size_t at = 0;
                while (true) {
                    const auto found = value.find(spec.from, at);
                    if (found == String::npos) { replaced += value.substr(at); break; }
                    replaced += value.substr(at, found - at) + spec.to;
                    at = found + std::char_traits<char>::length(spec.from);
                }
                value = std::move(replaced);
                if (std::string_view(spec.key) == "consumption_ep") value = replace_all(value, "120", "%s");
            }
            msgtable[spec.key] = trim_classic(value);
        }
        // 1.3系ではフォームの貨幣ヒントの位置が異なる。
        const auto money_specs = classic_version(m_version)
            ? std::array<ResourceMessage, 2>{{{"desc_base_money", "TMAINWINDOW/MainWindow/ButtonControl/NormalSheet/VaultPanel/Hint", nullptr, nullptr},
                                               {"desc_party_money", "TMAINWINDOW/MainWindow/ButtonControl/NormalSheet/PursePanel/Hint", nullptr, nullptr}}}
            : std::array<ResourceMessage, 2>{{{"desc_base_money", "TMAINWINDOW/MainWindow/BottomBar/ButtonControl/NormalSheet/VaultPanel/Hint", nullptr, nullptr},
                                               {"desc_party_money", "TMAINWINDOW/MainWindow/BottomBar/ButtonControl/NormalSheet/PursePanel/Hint", nullptr, nullptr}}};
        for (const auto& spec : money_specs) {
            const String path = spec.path;
            std::vector<Bytes> parts;
            std::size_t begin = 0;
            while (begin < path.size()) {
                const auto slash = path.find('/', begin);
                const auto part = path.substr(begin, slash == String::npos ? slash : slash - begin);
                parts.emplace_back(part.begin(), part.end());
                if (slash == String::npos) break;
                begin = slash + 1;
            }
            const auto form = (*m_res)->get_tpf0form(parts[0]);
            if (!form) continue;
            const String missing("\x1f__missing__");
            const auto value = ::cw::skin::win32res::find_res_4(*form,
                std::vector<Bytes>(parts.begin() + 1, parts.end()), missing);
            if (value != missing) msgtable[spec.key] = trim_classic(value);
        }

        struct Crib { Bytes key; bool after; Integer offset; String prefix; };
        const Bytes table_pad = hex_bytes("005441424c455f504144000043617020002f00494d4147455f434f4d4d414e443700494d4147455f434f4d4d414e443500494d4147455f434f4d4d414e4438002f00");
        const Bytes mode = hex_bytes("0092868e7e0095c282b682e90095c282b682e90095c282b682e90095c282b682e90095c282b682e900");
        const Bytes buttons = hex_bytes("00425554544f4e5f524d4f564500425554544f4e5f4c4d4f564500425554544f4e5f555000425554544f4e5f444f574e00425554544f4e5f52534d414c4c00425554544f4e5f4c534d414c4c00");
        const std::vector<std::pair<String, Crib>> cribs{
            {"select_base_title", {hex_bytes("00494d4147455f434f4d4d414e443000494d4147455f444542554700"), false, -8, {}}},
            {"cards_hand", {table_pad, false, -104, "%s"}},
            {"cards_backpack", {table_pad, false, -91, {}}},
            {"cards_storehouse", {table_pad, false, -59, {}}},
            {"info_card", {table_pad, false, -23, {}}},
            {"mode_show", {mode, false, -57, {}}}, {"mode_move", {mode, false, -44, {}}},
            {"mode_use", {mode, false, -31, {}}}, {"mode_battle", {mode, false, -18, {}}},
            {"send_to_manual", {buttons, true, 0, {}}}, {"send_to_storehouse", {buttons, true, 9, {}}},
            {"send_to_backpack", {buttons, true, 20, {}}}, {"send_to_shelf", {buttons, true, 27, {}}},
            {"send_to_trush", {buttons, true, 32, {}}},
            {"select_battle_action", {hex_bytes("008d7393ae8a4a8e6e00494d4147455f424154544c450093a682b082e900"), false, -12, {}}},
            {"lost_coupon_1", {hex_bytes("8146838c8378838b95e290b392860081518fc196c5975c96f10081518e80965300"), true, 0, {}}},
            {"currency", {hex_bytes("967b83418376838a8350815b83568387839382cd81778fac82b382a28374834883938367817882c991ce899e82b582c482a282dc82b7814289e696ca82cc8376838d83708365834282f08a4a82ab8141837483488393836783548343835982f081778fac82b382a28374834883938367817882c98e7792e882b582c48351815b838082f08dc48a4a82b582c482ad82be82b382a2814200"), true, 187, "%s"}}
        };
        for (const auto& [name, crib] : cribs) {
            const auto at = find_bytes(m_exebinary, crib.key);
            if (at == byte_npos) continue;
            const auto raw = static_cast<std::ptrdiff_t>(at) + (crib.after ? static_cast<std::ptrdiff_t>(crib.key.size()) : 0) + crib.offset;
            const auto value = _get_text(static_cast<::cw::lib::Integer>(raw), false);
            msgtable[name] = trim_classic(crib.prefix + std::get<0>(value));
        }
        for (const auto& name : {String("cards_backpack"), String("cards_storehouse"), String("info_card")}) {
            const auto it = msgtable.find(name);
            if (it != msgtable.end()) {
                const auto split = it->second.find(" - ");
                if (split != String::npos) it->second = it->second.substr(split + 3);
            }
        }

        const auto list1 = read_after(hex_bytes("5c4d6964695c44656652657365742e6d69640000"), 71);
        const auto list2 = read_after(hex_bytes("004d616c650046656d616c65004368696c6400596f756e67004164756c74004f6c6400425554544f4e5f4c4d4f564500425554544f4e5f524d4f564500425554544f4e5f4c4d4f564500425554544f4e5f524d4f564500425554544f4e5f4c4d4f564500425554544f4e5f524d4f564500"), 50);
        const auto list3 = read_after(hex_bytes("00494d4147455f434f4d4d414e443000494d4147455f444542554700"), 62);
        const auto list4 = read_after(hex_bytes("00816900816a008d7393ae97cd0089f194f097cd0092ef8d5297cd0096688ce497cd008fac0092860091e5008dc591e500837b815b8369835800"), 30);
        const auto list5 = read_after(hex_bytes("5c5461626c655c426f6f6b2e626d7000202f2000"), 100);
        const auto list6 = read_after(hex_bytes("00434152445f5245564552534500814f00"), 86);
        const auto list7 = read_after(hex_bytes("00494d4147455f444542554700494d4147455f434f4d4d414e4430005061727479002e777074002e777074002e77706c002e777074002e777074002e77706c"), 8);
        const auto list8 = read_after(hex_bytes("008146827100000000000044656661756c7400"), 8);
        const auto list9 = read_after(hex_bytes("d8ffffff000005000000000084334f00"), 19);

        // cribs2は抽出したリストの固定位置をそのままPython順に組み立てる。
        const auto R = [&](const std::vector<String>& list, std::size_t n) { return required(list, n); };
        std::map<String, String> cribs2{
            {"new_base", R(list3,3)}, {"adventurers", R(list3,1)},
            {"confirm_sell", "%s" + R(list1,62) + "%s" + R(list1,63)}, {"confirm_dump", "%s" + R(list1,67)},
            {"error_hand_be_full", "%s" + R(list1,59)}, {"error_sell_premier_card", R(list1,61)},
            {"error_dump_premier_card", "%s" + R(list1,66)}, {"confirm_use_card", "%s" + R(list1,57)},
            {"inactive", "%s" + R(list1,28)}, {"selected_penalty", R(list1,30)},
            {"general_father", R(list2,21)}, {"general_mother", R(list2,30)},
            {"entry_cancel_message", R(list2,7)}, {"entry_decide_message", "%s" + R(list2,5)},
            {"edit_design", R(list4,21)}, {"regulate_level", R(list4,22)}, {"regulate_level_title", R(list4,29)},
            {"card_number", R(list4,6) + " %s / %s"}, {"select_member_title", R(list5,0)}, {"album", R(list5,72)},
            {"confirm_delete_character", R(list5,50) + "%s" + R(list5,51)}, {"confirm_delete_character_in_album", "%s" + R(list5,98)},
            {"character_level", R(list5,2)}, {"character_class", R(list5,3)}, {"character_age", R(list5,9) + ":%s"},
            {"character_sex", R(list5,14) + ":%s"}, {"character_ep", R(list5,16) + ":%s"},
            {"character_history", "【" + R(list5,21) + "】"}, {"history_etc", R(list5,20)},
            {"confirm_grow", R(list5,32) + "%s" + R(list5,33) + replace_all(replace_all(R(list5,34), drop_first_utf8(R(list6,2)), "%s"), drop_first_utf8(R(list6,3)), "%s") + R(list5,38)},
            {"confirm_die", R(list5,32) + "%s" + R(list5,33) + R(list5,37) + R(list5,38)},
            {"die_message", "%s" + R(list5,44)}, {"default_party_name", "%s" + R(list5,24)}, {"resume_adventure", R(list5,64)},
            {"select_scenario_title", R(list3,20)}, {"target_level_1", R(list3,49) + "%s"}, {"target_level_2", R(list3,49) + "%s ～ %s"},
            {"contents", R(list3,61)}, {"confirm_save", R(list1,52)}, {"saved", R(list1,54)}, {"confirm_go_title", R(list1,51)},
            {"confirm_quit", R(list1,50)}, {"load_scenario_failure", R(list1,45)}, {"f9_message", R(list1,40)},
            {"confirm_runaway", R(list1,13)}, {"ok", R(list1,70)}, {"adventurers_team", R(list5,66)},
            {"adventurers_money", R(list5,68) + " %s " + R(list5,69)}, {"select_member_message", R(list8,0)}, {"select_message", R(list8,3)},
            {"number_1_coupon", R(list6,17)}, {"father_coupon", R(list7,2) + "%s"}, {"mother_coupon", R(list7,3) + "%s"},
            {"lost_coupon_2", R(list5,41)}, {"level_up", "\n\n\n#I" + R(list1,69)}, {"extension_title", R(list9,2) + "%s" + R(list9,3)}};
        for (auto& [key, value] : cribs2) msgtable[key] = trim_classic(std::move(value));
        if (const auto messages = m_data->find("Messages")) {
            // Pythonのfor e in Element（直下のMessage列）と同じ範囲。
            for (const auto& message : (*messages)->m_children) {
                if (!message) continue;
                const auto value = msgtable.find(message->get("key", ""));
                if (value != msgtable.end()) message->set_text(value->second);
            }
        }
    } catch (const std::exception&) {
        // Python版のprint_ex相当。派生版の構造差で当該段階を中断しても変換全体は継続する。
        ::cw::util::print_ex(std::nullopt);
    }
}

std::tuple<::cw::lib::String, ::cw::lib::Integer> Converter::_get_text(::cw::lib::Integer arg_index, bool arg_cutzero)
{
    const auto length = static_cast<std::ptrdiff_t>(m_exebinary.size());
    auto search_begin = static_cast<std::ptrdiff_t>(arg_index);
    if (search_begin < 0) search_begin += length;
    search_begin = std::clamp(search_begin, std::ptrdiff_t{0}, length);
    const auto end_it = std::find(
        m_exebinary.begin() + search_begin, m_exebinary.end(), static_cast<std::uint8_t>(0));
    const auto end = end_it == m_exebinary.end()
        ? std::ptrdiff_t{-1}
        : static_cast<std::ptrdiff_t>(end_it - m_exebinary.begin());
    const auto text = decode_classic(python_slice(m_exebinary, arg_index, end));
    auto next = end + 1;
    if (arg_cutzero) {
        while (next >= 0 && next < length && m_exebinary[static_cast<std::size_t>(next)] == 0) ++next;
    }
    return {text, static_cast<::cw::lib::Integer>(next)};
}

void Converter::run()
{
    run({});
}

void Converter::run(const ProgressReport& report)
{
    const auto publish=[&](Integer value, String message) {
        m_curnum=value; m_message=std::move(message);
        if(report) report(value,m_message);
    };
    publish(0,"スキンのベースをコピー中...");
    if (!m_data) throw std::runtime_error("converter data is not initialized");
    std::filesystem::path dpath;
    const auto name = ::cw::binary::util::check_filename(get_text(m_data, "Property/Name", "Default"));
    const auto candidate = std::filesystem::u8path("Data/Skin") / std::filesystem::u8path(name);
    std::filesystem::create_directories(candidate.parent_path());
    dpath = std::filesystem::u8path(
        ::cw::binary::util::check_duplicate(path_utf8(candidate)));
    m_skindirname = path_utf8(dpath.filename());
    // Python の SkinBase の copytree 呼び出しは try の外にある。
    // ここで失敗した場合は failure/complete を変更せず、後処理も行わない。
    copy_tree(std::filesystem::u8path("Data/SkinBase"), dpath);
    try {
        const auto oldshot = dpath / std::filesystem::u8path("Sound/System_ScreenShot.wav");
        const auto newshot = dpath / std::filesystem::u8path("Sound/システム・スクリーンショット.wav");
        std::filesystem::rename(oldshot, newshot);

        publish(10,"リソースを抽出中...");
        const auto base = ::cw::data::xml2etree("Data/SkinBase/Skin.xml", "", std::nullopt, std::nullopt, false);
        if (!base) throw std::runtime_error("Data/SkinBase/Skin.xml could not be loaded");
        const auto base_messages = base->find("Messages");
        const auto messages = m_data->find("Messages");
        if (!base_messages || !messages)
            throw std::runtime_error("Skin.xml Messages element is missing");
        if ((*base_messages)->m_children.size() != (*messages)->m_children.size())
            throw std::runtime_error("Skin.xml Messages count differs from SkinBase");
        std::map<String, String> unchanged;
        for (const auto& e : (*base_messages)->m_children) {
            if (e) unchanged[e->get("key", "")] = e->get_text();
        }
        std::vector<Element> remove;
        for (const auto& e : (*messages)->m_children) {
            if (!e) continue;
            const auto it = unchanged.find(e->get("key", ""));
            if (it != unchanged.end() && it->second == e->get_text()) remove.push_back(e);
        }
        for (const auto& e : remove) {
            (*messages)->remove(e);
        }
        auto property = m_data->find_exists("Property");
        auto source_flag = property->find("SourceOfMaterialsIsClassicEngine");
        if (!source_flag) property->append(::cw::data::make_element("SourceOfMaterialsIsClassicEngine"));
        m_data->edit("Property/SourceOfMaterialsIsClassicEngine", "True", std::nullopt);
        m_data->m_fpath = path_utf8(dpath / "Skin.xml");
        m_data->write_file(m_data->m_fpath);
        _write_data(path_utf8(dpath), m_actioncard);
        _write_data(path_utf8(dpath), m_gameover);
        _write_data(path_utf8(dpath), m_scenario);
        _write_data(path_utf8(dpath), m_title);
        _write_data(path_utf8(dpath), m_yado);
        _write_data(path_utf8(dpath), m_specialcard);

        std::map<String, std::vector<String>> images;
        for (const auto& [resource, target] : g_IMGTBL) images[resource] = {target};
        if (m_partyinfo_res) {
            const auto& resource = *m_partyinfo_res;
            images[resource] = {"Card/" + (resource.rfind("IMAGE_", 0) == 0 ? resource.substr(6) : resource)};
        }
        std::map<String, String> curtbl = g_CURTBL;
        std::map<Bytes, String> glyphtbl;
        const auto old_version = m_version <= std::tuple<Integer, Integer, Integer, Integer>{1, 2, 0, 99};
        if (old_version) {
            const std::vector<std::pair<String, String>> replacements = {
                {"BUTTON_ARROW", ""}, {"BUTTON_CAST", ""}, {"BUTTON_DECK", ""}, {"BUTTON_DOWN", ""},
                {"BUTTON_LSMALL", ""}, {"BUTTON_RSMALL", ""}, {"BUTTON_SACK", ""}, {"BUTTON_SHELF", ""},
                {"BUTTON_TRUSH", ""}, {"BUTTON_UP", ""}, {"CARD_REVERSE", ""}, {"CHECK_FOLDER", "IMAGE_FOLDER"},
                {"CHECK_LINK", ""}, {"CHECK_UTILITY", ""}, {"IMAGE_COMMAND5", ""}, {"IMAGE_SHELF", "IMAGE_COMMAND5"},
                {"IMAGE_ALARM", ""}, {"MARK_STATUS7", "MARK_STATUS4"}, {"MARK_STATUS8", "DBG_CARD0"},
                {"MARK_STATUS9", "DBG_CARD1"}, {"MARK_STATUS10", "MARK_STATUS6"}, {"MARK_STATUS11", "MARK_STATUS5"},
                {"MARK_STATUS12", ""}, {"MARK_STATUS13", ""}, {"SIGN_HOLD", ""}, {"SIGN_PENALTY", ""},
                {"SIGN_PREMIER", ""}, {"SIGN_RARE", ""}, {"STATUS_BODY0", "STATUS_POISON"},
                {"STATUS_BODY1", "STATUS_PARALY"}, {"STATUS_MIND0", ""}, {"STONE_HAND0", "MARK_HAND0"},
                {"STONE_HAND1", "MARK_HAND1"}, {"STONE_HAND2", "MARK_HAND2"}, {"STONE_HAND3", "MARK_HAND3"},
                {"STONE_HAND4", ""}, {"STONE_HAND5", "MARK_HAND4"}, {"STONE_HAND6", "MARK_HAND5"},
                {"STONE_HAND7", "MARK_HAND6"}, {"STONE_HAND8", "MARK_HAND7"}, {"STONE_HAND9", "MARK_HAND8"},
                {"TITLE_SHADOW", "TITLE_CARDWIRTH"}};
            for (const auto& [from, to] : replacements) {
                const auto it = images.find(from);
                if (it == images.end()) continue;
                auto value = it->second;
                images.erase(it);
                if (!to.empty()) images[to] = std::move(value);
            }
            images["BUTTON_SKILL"] = {"Button/SKILL"}; images["BUTTON_ITEM"] = {"Button/ITEM"}; images["BUTTON_BEAST"] = {"Button/BEAST"};
            images["MARK_STATUS0"] = {"Dialog/STATUS1", "Dialog/STATUS2", "Dialog/STATUS3"};
            images["MARK_STATUS1"] = {"Dialog/STATUS0"}; images["MARK_STATUS2"] = {"Dialog/STATUS5"}; images["MARK_STATUS3"] = {"Dialog/STATUS6"};
            images["MARK_HAND9"] = {"Dialog/RARE_ICON"}; images["MARK_HAND10"] = {"Dialog/PREMIER_ICON"};
            glyphtbl[Bytes{'T','M','A','I','N','W','I','N','D','O','W','/','M','a','i','n','W','i','n','d','o','w','/','D','e','b','u','g','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Dialog/STATUS12";
            glyphtbl[Bytes{'T','M','A','I','N','W','I','N','D','O','W','/','M','a','i','n','W','i','n','d','o','w','/','S','y','s','t','e','m','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Dialog/SETTINGS";
            curtbl.clear();
        } else if (classic_version(m_version)) {
            glyphtbl[Bytes{'T','C','A','R','D','D','L','G','/','C','a','r','d','D','l','g','/','T','a','b','l','e','P','a','n','e','l','/','S','p','e','e','d','P','a','n','e','l','/','B','e','a','s','t','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Button/BEAST";
            glyphtbl[Bytes{'T','C','A','R','D','D','L','G','/','C','a','r','d','D','l','g','/','T','a','b','l','e','P','a','n','e','l','/','S','p','e','e','d','P','a','n','e','l','/','I','t','e','m','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Button/ITEM";
            glyphtbl[Bytes{'T','C','A','R','D','D','L','G','/','C','a','r','d','D','l','g','/','T','a','b','l','e','P','a','n','e','l','/','S','p','e','e','d','P','a','n','e','l','/','S','k','i','l','l','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Button/SKILL";
            glyphtbl[Bytes{'T','M','A','I','N','W','I','N','D','O','W','/','M','a','i','n','W','i','n','d','o','w','/','S','y','s','t','e','m','B','t','n','/','G','l','y','p','h','.','D','a','t','a'}] = "Dialog/SETTINGS";
            const auto middle = m_version <= std::tuple<Integer, Integer, Integer, Integer>{1, 3, 99, 99};
            if (middle) {
                glyphtbl[Bytes{'T','M','A','I','N','W','I','N','D','O','W','/','M','a','i','n','W','i','n','d','o','w','/','B','u','t','t','o','n','C','o','n','t','r','o','l','/','N','o','r','m','a','l','S','h','e','e','t','/','P','u','r','s','e','P','a','n','e','l','/','P','u','r','s','e','I','m','a','g','e','/','P','i','c','t','u','r','e','.','D','a','t','a'}] = "Dialog/MONEYP";
                glyphtbl[Bytes{'T','M','A','I','N','W','I','N','D','O','W','/','M','a','i','n','W','i','n','d','o','w','/','B','u','t','t','o','n','C','o','n','t','r','o','l','/','N','o','r','m','a','l','S','h','e','e','t','/','V','a','u','l','t','P','a','n','e','l','/','V','a','u','l','t','I','m','a','g','e','/','P','i','c','t','u','r','e','.','D','a','t','a'}] = "Dialog/MONEYY";
            }
        } else {
            const auto add_glyph = [&](const String& resource, const String& target) {
                glyphtbl[Bytes(resource.begin(), resource.end())] = target;
            };
            add_glyph("TCARDDLG/CardDlg/TablePanel/SpeedPanel/BeastBtn/Glyph.Data", "Button/BEAST");
            add_glyph("TCARDDLG/CardDlg/TablePanel/SpeedPanel/ItemBtn/Glyph.Data", "Button/ITEM");
            add_glyph("TCARDDLG/CardDlg/TablePanel/SpeedPanel/SkillBtn/Glyph.Data", "Button/SKILL");
            add_glyph("TMAINWINDOW/MainWindow/BottomBar/ButtonControl/NormalSheet/PursePanel/PurseImage/Picture.Data", "Dialog/MONEYP");
            add_glyph("TMAINWINDOW/MainWindow/BottomBar/ButtonControl/NormalSheet/VaultPanel/VaultImage/Picture.Data", "Dialog/MONEYY");
            add_glyph("TMAINWINDOW/MainWindow/BottomBar/SystemBtn/Glyph.Data", "Dialog/SETTINGS");
        }
        if (m_res && *m_res) {
            for (const auto& [resource, targets] : images) {
                auto bitmap = (*m_res)->get_bitmap(resource);
                if (!bitmap) {
                    std::cout << "Resource not found: " << resource << std::endl;
                    continue;
                }
                for (const auto& target : targets) write_bytes(dpath / std::filesystem::u8path("Resource/Image") / std::filesystem::u8path(target + ".bmp"), *bitmap);
            }
            for (const auto& [resource, target] : curtbl) {
                auto cursor = (*m_res)->get_cursor(resource);
                if (!cursor) {
                    std::cout << "Cursor not found: " << resource << std::endl;
                    continue;
                }
                write_bytes(dpath / std::filesystem::u8path("Resource/Image") / std::filesystem::u8path(target + ".cur"), *cursor);
            }
            for (const auto& [respath, target] : glyphtbl) {
                const String glyph_name(respath.begin(), respath.end());
                const auto slash = std::find(respath.begin(), respath.end(), static_cast<std::uint8_t>('/'));
                if (slash == respath.end()) continue;
                const Bytes form_name(respath.begin(), slash);
                const auto form = (*m_res)->get_tpf0form(form_name);
                if (!form) {
                    std::cout << "Glyph not found: " << glyph_name << std::endl;
                    continue;
                }
                std::vector<Bytes> parts;
                for (auto it = slash + 1; it != respath.end();) {
                    auto end = std::find(it, respath.end(), static_cast<std::uint8_t>('/'));
                    parts.emplace_back(it, end); if (end == respath.end()) break; it = end + 1;
                }
                auto glyph = ::cw::skin::win32res::find_res_5(*form, std::move(parts), {});
                if (glyph.empty()) {
                    std::cout << "Glyph not found: " << glyph_name << std::endl;
                    continue;
                }
                const std::size_t skip = glyph.size() >= 8 && Bytes(glyph.begin() + 1, glyph.begin() + 8) == Bytes{'T','B','i','t','m','a','p'} ? 12 : 4;
                const auto begin = std::min(skip, glyph.size());
                write_bytes(dpath / std::filesystem::u8path("Resource/Image") / std::filesystem::u8path(target + ".bmp"),
                            Bytes(glyph.begin() + static_cast<std::ptrdiff_t>(begin), glyph.end()));
            }
        }

        const auto exe_dir = m_exe.empty() ? std::filesystem::path{} : std::filesystem::u8path(m_exe).parent_path();
        const auto datadir = std::filesystem::u8path(m_datadir).is_absolute()
            ? std::filesystem::u8path(m_datadir) : exe_dir / std::filesystem::u8path(m_datadir);
        publish(20,"BGMフォルダをコピー中...");
        copy_tree_or_mkdir(datadir / "Midi", dpath / "Bgm");
        publish(30,"カード画像フォルダをコピー中...");
        copy_tree_or_mkdir(datadir / "Face", dpath / "Face");
        publish(40,"効果音フォルダをコピー中...");
        // Python版と同じくWave配下をバイト列のまま再帰コピーする。変換時に
        // codecへ依存させず、再生時の音声backendがWAV等の形式を判定する。
        copy_contents(datadir / "Wave", dpath / "Sound");
        publish(50,"背景画像フォルダをコピー中...");
        copy_contents(datadir / "Table", dpath / "Table");
        publish(60,"エフェクトブースターファイルをコピー中...");
        copy_contents(exe_dir / "Scheme", dpath / "EffectBooster");
        publish(70,"名前のリストをコピー中...");
        std::filesystem::create_directories(dpath / "Name");
        for (const auto& file : {"MaleNames.txt", "FemaleNames.txt", "CommonNames.txt"}) {
            const auto name_source = datadir / file;
            if (std::filesystem::is_regular_file(name_source) && std::filesystem::file_size(name_source) != 0)
                std::filesystem::copy_file(name_source, dpath / "Name" / file, std::filesystem::copy_options::overwrite_existing);
        }
        publish(75,m_message);
        std::filesystem::remove_all(dpath / "Name" / "Example");

        publish(80,"オーバーライドされたリソースをコピー中...");
        const auto resdir = datadir / "Resource";
        for (const auto& [key, targets] : images) {
            const auto source_bmp = resdir / std::filesystem::u8path(key + ".bmp");
            if (std::filesystem::is_regular_file(source_bmp)) {
                if (targets.size() != 1)
                    throw std::runtime_error("resource override has multiple targets: " + key);
                const auto output_base = dpath / std::filesystem::u8path("Resource/Image") /
                                         std::filesystem::u8path(targets.front());
                auto output_bmp = output_base; output_bmp += ".bmp";
                std::filesystem::copy_file(source_bmp, output_bmp, std::filesystem::copy_options::overwrite_existing);
            }
            const auto source_png = resdir / std::filesystem::u8path(key + ".png");
            if (std::filesystem::is_regular_file(source_png)) {
                if (targets.size() != 1)
                    throw std::runtime_error("resource override has multiple targets: " + key);
                const auto output_base = dpath / std::filesystem::u8path("Resource/Image") /
                                         std::filesystem::u8path(targets.front());
                auto output_bmp = output_base; output_bmp += ".bmp";
                auto output_png = output_base; output_png += ".png";
                std::filesystem::copy_file(source_png, output_png, std::filesystem::copy_options::overwrite_existing);
                std::filesystem::remove(output_bmp);
            }
        }
        for (const auto& [key, target] : curtbl) {
            const auto cursor_source = resdir / std::filesystem::u8path(key + ".cur");
            if (std::filesystem::is_regular_file(cursor_source))
                std::filesystem::copy_file(cursor_source, dpath / std::filesystem::u8path("Resource/Image") / std::filesystem::u8path(target + ".cur"), std::filesystem::copy_options::overwrite_existing);
        }

        publish(90,m_message);
        // Python版と同じく、find_resource()とload_wxbmp()でタイトル画像を
        // 読み込む。JPEGとscaled-imageもこの経路で寸法を取得できる。
        const auto title_xml = dpath / std::filesystem::u8path("Resource/Xml/Title/01_Title.xml");

        if (std::filesystem::is_regular_file(title_xml)) {
            auto tree = ::cw::data::xml2etree(path_utf8(title_xml), "", std::nullopt, std::nullopt, true);
            const auto tsize = image_size(dpath / std::filesystem::u8path("Resource/Image/Other/TITLE_CELL3"));
            const auto vsize = image_size(dpath / std::filesystem::u8path("Resource/Image/Other/TITLE_VERSION"));
            if (tree) {
                const auto half_floor = [](Integer value) { return value / 2 - (value < 0 && value % 2 != 0 ? 1 : 0); };
                const auto tleft = half_floor(std::get<0>(::cw::g_SIZE_AREA) - tsize.first);
                const auto ttop = half_floor(std::get<1>(::cw::g_SIZE_AREA) - tsize.second);
                if (tsize.first != 0) {
                    tree->edit("BgImages/BgImage[2]/Location", std::to_string(tleft), "left");
                    tree->edit("BgImages/BgImage[2]/Location", std::to_string(ttop), "top");
                    tree->edit("BgImages/BgImage[2]/Size", std::to_string(tsize.first), "width");
                    tree->edit("BgImages/BgImage[2]/Size", std::to_string(tsize.second), "height");
                    tree->edit("BgImages/BgImage[3]/Location", std::to_string(tleft), "left");
                    tree->edit("BgImages/BgImage[3]/Location", std::to_string(ttop), "top");
                    tree->edit("BgImages/BgImage[3]/Size", std::to_string(tsize.first), "width");
                    tree->edit("BgImages/BgImage[3]/Size", std::to_string(tsize.second), "height");
                }
                if (vsize.first != 0) {
                    const auto vleft = half_floor(std::get<0>(::cw::g_SIZE_AREA) - vsize.first);
                    const auto vtop = ttop + tsize.second + 12;
                    tree->edit("BgImages/BgImage[4]/Location", std::to_string(vleft), "left");
                    tree->edit("BgImages/BgImage[4]/Location", std::to_string(vtop), "top");
                    tree->edit("BgImages/BgImage[4]/Size", std::to_string(vsize.first), "width");
                    tree->edit("BgImages/BgImage[4]/Size", std::to_string(vsize.second), "height");
                }
                tree->write_file(path_utf8(title_xml));
            }
        }
        if (m_partyinfo_res) {
            const auto resource = *m_partyinfo_res;
            const auto party = "Resource/Image/Card/" + (resource.rfind("IMAGE_", 0) == 0 ? resource.substr(6) : resource);
            for (const auto& item : {std::pair<String, String>{"Yado/02_Yado2.xml", "MenuCards/MenuCard[8]/Property/ImagePath"},
                                      {"Scenario/-4_Camp.xml", "MenuCards/MenuCard[3]/Property/ImagePath"}}) {
                const auto path = dpath / std::filesystem::u8path("Resource/Xml") / std::filesystem::u8path(item.first);
                if (!std::filesystem::is_regular_file(path)) continue;
                auto tree = ::cw::data::xml2etree(path_utf8(path), "", std::nullopt, std::nullopt, true);
                if (tree) { tree->edit(item.second, party, std::nullopt); tree->write_file(path_utf8(path)); }
            }
        }
        if (m_adventurersinn) {
            for (const auto& file : {"Yado/01_Yado.xml", "Yado/02_Yado2.xml", "Yado/03_YadoInitial.xml"}) {
                const auto path = dpath / std::filesystem::u8path("Resource/Xml") / file;
                if (!std::filesystem::is_regular_file(path)) continue;
                auto tree = ::cw::data::xml2etree(path_utf8(path), "", std::nullopt, std::nullopt, true);
                if (tree) { tree->edit("BgImages/BgImage[2]/ImagePath", "Table/" + *m_adventurersinn, std::nullopt); tree->write_file(path_utf8(path)); }
            }
        }
        if (::cw::g_fsync) ::cw::g_fsync->sync();
        publish(100,"スキンの生成が完了しました。");
        m_complete = true;
    } catch (...) {
        ::cw::util::print_ex(std::nullopt);
        m_failure = true;
        m_complete = true;
        m_errormessage = "スキンの自動生成に失敗しました。";
        std::filesystem::remove_all(dpath);
        throw;
    }
}

std::map<::cw::lib::String, ::cw::lib::String> g_CURTBL = {
    {"CURSOR_BACK", "Cursor/CURSOR_BACK"}, {"CURSOR_DRIVER", "Cursor/CURSOR_DRIVER"},
    {"CURSOR_FINGER", "Cursor/CURSOR_FINGER"}, {"CURSOR_FORE", "Cursor/CURSOR_FORE"}};

std::map<::cw::lib::String, ::cw::lib::String> g_IMGTBL = [] {
    std::map<::cw::lib::String, ::cw::lib::String> t;
    const auto add = [&t](const String& key_prefix, const String& prefix, std::initializer_list<const char*> names) {
        for (const auto* name : names) t.emplace(key_prefix + name, prefix + "/" + name);
    };
    add("BUTTON_", "Button", {"ARROW","CAST","DECK","DOWN","LJUMP","LMOVE","LSMALL","RJUMP","RMOVE","RSMALL","SACK","SHELF","TRUSH","UP"});
    for (Integer n : {0,1,2,3,4,5,6,7,9}) t["IMAGE_ACTION" + std::to_string(n)] = "Card/ACTION" + std::to_string(n);
    add("IMAGE_", "Card", {"ALARM","BATTLE","DEBUG","FATHER","MOTHER","OVER","SHELF","TRUSH"});
    for (Integer n = 0; n <= 11; ++n) t["IMAGE_COMMAND" + std::to_string(n)] = "Card/COMMAND" + std::to_string(n);
    add("CARD_", "CardBg", {"ACTION","BEAST","BIND","DANGER","FAINT","INFO","INJURY","ITEM","LARGE","NORMAL","OPTION","PARALY","PETRIF","REVERSE","SKILL","SLEEP"});
    t["SIGN_HOLD"] = "CardBg/HOLD"; t["SIGN_PENALTY"] = "CardBg/PENALTY";
    t["SIGN_PREMIER"] = "CardBg/PREMIER"; t["SIGN_RARE"] = "CardBg/RARE";
    add("TABLE_", "Dialog", {"CAUTION","PAD","STATUS"});
    add("CHECK_", "Dialog", {"COMPLETE","FIXED","FOLDER","INVISIBLE","LINK","PLAYING","SELECT","UTILITY"});
    for (Integer n = 0; n <= 13; ++n) t["MARK_STATUS" + std::to_string(n)] = "Dialog/STATUS" + std::to_string(n);
    add("FONT_", "Font", {"ANGRY","CLUB","DIAMOND","EASY","FLY","GRIEVE","HEART","JACK","KISS","LAUGH","NIKO","ONSEN","PUZZLE","QUICK","SPADE","WORRY","X","ZAP"});
    add("", "Other", {"TITLE_CARD1","TITLE_CARD2","TITLE_CELL1","TITLE_CELL2","TITLE_CELL3","TITLE_SHADOW","TITLE_VERSION","SCREENSHOT_HEADER"});
    add("STATUS_", "Status", {"BODY0","BODY1","DOWN0","DOWN1","DOWN2","DOWN3","LIFE","LIFEBAR","LIFEGUAGE","MAGIC0","MAGIC1","MAGIC2","MAGIC3","MIND0","MIND1","MIND2","MIND3","MIND4","MIND5","SUMMON","UP0","UP1","UP2","UP3"});
    t["CHECK_TARGET"] = "Status/TARGET";
    for (Integer n = 0; n <= 9; ++n) t["STONE_HAND" + std::to_string(n)] = "Stone/HAND" + std::to_string(n);
    t["SYMBOL_SKILL"] = "Button/SKILL"; t["SYMBOL_ITEM"] = "Button/ITEM";
    t["SYMBOL_BEAST"] = "Button/BEAST"; t["SYMBOL_BACKPACK"] = "Button/PERSONAL";
    return t;
}();

std::map<::cw::lib::String, ::cw::lib::String> g_IMGTBL_C = [] {
    std::map<String, String> t;
    for (Integer n : {1,2,3,4,5,6,7,9}) t["IMAGE_ACTION" + std::to_string(n)] = "Card/ACTION" + std::to_string(n);
    for (Integer n = 0; n <= 11; ++n) t["IMAGE_COMMAND" + std::to_string(n)] = "Card/COMMAND" + std::to_string(n);
    for (const auto* n : {"ALARM","BATTLE","DEBUG","FATHER","MOTHER","OVER","SHELF","TRUSH"}) t["IMAGE_" + String(n)] = String("Card/") + n;
    t["IMAGE_ACTION0"] = "Card/ACTION0";
    return t;
}();

::cw::lib::Integer Converter__get_features_set_params(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_index, bool arg_isnature, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> arg_slist, ::cw::lib::Real arg_sperb)
{
    if (!s_active_converter || !arg_data) return arg_index;
    if (arg_sperb == 0.0 || arg_index < 0) throw std::invalid_argument("feature parameter divisor/index");
    auto& binary = s_active_converter->m_exebinary;
    std::size_t p = static_cast<std::size_t>(arg_index);
    if (p + 42 > binary.size()) throw std::out_of_range("feature parameter range");
    const auto name_end_it = std::find(binary.begin() + static_cast<std::ptrdiff_t>(p),
                                       binary.begin() + static_cast<std::ptrdiff_t>(p + 20), 0);
    const auto name_last = name_end_it;
    arg_data->find_exists("./Name")->set_text(trim_classic(decode_classic(Bytes(
        binary.begin() + static_cast<std::ptrdiff_t>(p), name_last))));
    p += 20;
    auto physical = arg_data->find_exists("./Physical");
    const std::array<const char*, 6> pnames{"dex", "agl", "int", "str", "vit", "min"};
    for (Integer n = 0; n < 6; ++n)
        physical->set(pnames[static_cast<std::size_t>(n)], std::to_string(static_cast<Integer>(i16(binary, p + n * 2)) - (arg_isnature ? 6 : 0)));
    p += 12;
    auto mental = arg_data->find_exists("./Mental");
    const std::array<String, 5> slist{std::get<0>(arg_slist), std::get<1>(arg_slist), std::get<2>(arg_slist), std::get<3>(arg_slist), std::get<4>(arg_slist)};
    const std::array<Integer, 5> order{0, 3, 2, 1, 4};
    for (std::size_t n = 0; n < order.size(); ++n)
        mental->set(slist[n], python_float(static_cast<::cw::lib::Real>(i16(binary, p + order[n] * 2)) / arg_sperb));
    return static_cast<Integer>(p + 10);
}

void Converter__get_sounds_get_keybefore(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Bytes arg_key, ::cw::lib::Integer arg_length, ::cw::lib::Integer arg_less)
{
    if (!s_active_converter || !arg_e || arg_length < 0 || arg_less < 0) return;
    const auto at = find_bytes(s_active_converter->m_exebinary, arg_key);
    if (at == byte_npos || static_cast<std::size_t>(arg_less) > at ||
        static_cast<std::size_t>(arg_length) > at - static_cast<std::size_t>(arg_less)) return;
    const auto end = at - static_cast<std::size_t>(arg_less);
    arg_e->set_text(decode_classic(Bytes(s_active_converter->m_exebinary.begin() + static_cast<std::ptrdiff_t>(end - arg_length),
                                         s_active_converter->m_exebinary.begin() + static_cast<std::ptrdiff_t>(end))));
}

void Converter__get_sounds_get_keyafter(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Bytes arg_key, ::cw::lib::Integer arg_length, ::cw::lib::Integer arg_than)
{
    if (!s_active_converter || !arg_e || arg_length < 0 || arg_than < 0) return;
    const auto at = find_bytes(s_active_converter->m_exebinary, arg_key);
    if (at == byte_npos) return;
    const auto begin = at + arg_key.size() + static_cast<std::size_t>(arg_than);
    if (begin > s_active_converter->m_exebinary.size() || static_cast<std::size_t>(arg_length) > s_active_converter->m_exebinary.size() - begin) return;
    arg_e->set_text(decode_classic(Bytes(s_active_converter->m_exebinary.begin() + static_cast<std::ptrdiff_t>(begin),
                                         s_active_converter->m_exebinary.begin() + static_cast<std::ptrdiff_t>(begin + arg_length))));
}

::cw::lib::Integer Converter__get_cards_get_actioncard(::cw::lib::String arg_cardkey, ::cw::lib::Integer arg_index, ::cw::lib::Integer arg_keycodenum)
{
    if (!s_active_converter || arg_index < 0 || arg_keycodenum < 0) return arg_index;
    auto index = arg_index;
    const auto name = s_active_converter->_get_text(index, true); index = std::get<1>(name);
    const auto desc = s_active_converter->_get_text(index, true); index = std::get<1>(desc);
    const auto sound1 = s_active_converter->_get_text(index, false); index = std::get<1>(sound1);
    const auto sound2 = s_active_converter->_get_text(index, false); index = std::get<1>(sound2);
    std::vector<String> keycodes;
    for (Integer n = 0; n < arg_keycodenum; ++n) {
        const auto keycode = s_active_converter->_get_text(index, false);
        index = std::get<1>(keycode);
        keycodes.push_back(std::get<0>(keycode));
    }
    const auto it = s_active_converter->m_actioncard.find(arg_cardkey);
    if (it == s_active_converter->m_actioncard.end() || !it->second)
        throw std::out_of_range("action card resource is missing: " + arg_cardkey);
    String joined;
    for (std::size_t n = 0; n < keycodes.size(); ++n) {
        if (n != 0) joined += "\n";
        joined += keycodes[n];
    }
    set_text(it->second, "Property/Name", std::get<0>(name));
    set_text(it->second, "Property/Description", std::get<0>(desc));
    set_text(it->second, "Property/SoundPath", std::get<0>(sound1));
    set_text(it->second, "Property/SoundPath2", std::get<0>(sound2));
    set_text(it->second, "Property/KeyCodes", ::cw::util::encodewrap(joined));
    return index;
}

::cw::lib::Integer Converter__get_cards_get_menucard(std::vector<std::tuple<std::shared_ptr<::cw::data::CWPyElementTree>, ::cw::lib::Integer>> arg_area, ::cw::lib::Integer arg_index)
{
    if (!s_active_converter || arg_index < 0) return arg_index;
    auto index = arg_index;
    for (const auto& [tree, number] : arg_area) {
        if (!tree || !menu_card(tree, number))
            throw std::out_of_range("menu card resource is missing");
    }
    const auto name = s_active_converter->_get_text(index, true); index = std::get<1>(name);
    const auto desc = s_active_converter->_get_text(index, true); index = std::get<1>(desc);
    const auto image = s_active_converter->_get_text(index, false); (void)image; index = std::get<1>(image);
    for (const auto& [tree, number] : arg_area) {
        const auto card = menu_card(tree, number);
        card->find_exists("Property/Name")->set_text(std::get<0>(name));
        card->find_exists("Property/Description")->set_text(std::get<0>(desc));
    }
    return index;
}

void main()
{
    // Python版mainはGUI起動側から呼ばれる薄いエントリで、ここでは副作用を持たない。
}

}

