#include "cw/binary/util.hpp"

#include "cw/cw.hpp"
#include "cw/util.hpp"
#include "platform/storage/storage_mount.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cw::binary::util {

namespace {

using S = ::cw::lib::String;

void replace_all(S& value, const S& old_value, const S& new_value)
{
    if (old_value.empty()) return;
    std::size_t position = 0;
    while ((position = value.find(old_value, position)) != S::npos) {
        value.replace(position, old_value.size(), new_value);
        position += new_value.size();
    }
}

// Pythonのre/strはUnicodeの空白を扱う。std::isspace
// だけではロケール依存で、 CWの名前に多いU+3000も認識しないため、必
// 要な範囲だけUTF-8を復号して、安定したPythonの空白集合を使う。
bool decode_utf8(const S& value, std::size_t position, std::uint32_t& codepoint, std::size_t& width)
{
    if (position >= value.size()) return false;
    const auto first = static_cast<unsigned char>(value[position]);
    if (first < 0x80) {
        codepoint = first;
        width = 1;
        return true;
    }
    auto continuation = [&](std::size_t offset) {
        return position + offset < value.size() &&
               (static_cast<unsigned char>(value[position + offset]) & 0xC0) == 0x80;
    };
    if (first >= 0xC2 && first <= 0xDF && continuation(1)) {
        codepoint = (static_cast<std::uint32_t>(first & 0x1F) << 6) |
                    (static_cast<unsigned char>(value[position + 1]) & 0x3F);
        width = 2;
        return true;
    }
    if (first >= 0xE0 && first <= 0xEF && continuation(1) && continuation(2)) {
        const auto second = static_cast<unsigned char>(value[position + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0)) return false;
        codepoint = (static_cast<std::uint32_t>(first & 0x0F) << 12) |
                    (static_cast<std::uint32_t>(second & 0x3F) << 6) |
                    (static_cast<unsigned char>(value[position + 2]) & 0x3F);
        width = 3;
        return true;
    }
    if (first >= 0xF0 && first <= 0xF4 && continuation(1) && continuation(2) && continuation(3)) {
        const auto second = static_cast<unsigned char>(value[position + 1]);
        if ((first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) return false;
        codepoint = (static_cast<std::uint32_t>(first & 0x07) << 18) |
                    (static_cast<std::uint32_t>(second & 0x3F) << 12) |
                    (static_cast<std::uint32_t>(static_cast<unsigned char>(value[position + 2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(value[position + 3]) & 0x3F);
        width = 4;
        return true;
    }
    // 不正なUTF-8は空白ではないバイトとして扱う。 復号不能なバイト
    // 列をUnicode空白文字列と認めないPython版に合わせる。
    codepoint = first;
    width = 1;
    return false;
}

bool is_python_space(std::uint32_t codepoint)
{
    if (codepoint <= 0x7F && std::isspace(static_cast<unsigned char>(codepoint)) != 0) return true;
    switch (codepoint) {
    case 0x0085: // 次行（NEXT LINE）。
    case 0x00A0: // 改行しない空白（NO-BREAK SPACE）。
    case 0x1680: // オガム文字の空白（OGHAM SPACE MARK）。
    case 0x2028: // 行区切り（LINE SEPARATOR）。
    case 0x2029: // 段落区切り（PARAGRAPH SEPARATOR）。
    case 0x202F: // 幅の狭い改行しない空白（NARROW NO-BREAK SPACE）。
    case 0x205F: // 中程度の幅の数式用空白（MEDIUM MATHEMATICAL SPACE）。
    case 0x3000: // 表意文字用の空白（IDEOGRAPHIC SPACE）。
        return true;
    default:
        return (codepoint >= 0x2000 && codepoint <= 0x200A) ||
               (codepoint >= 0x001C && codepoint <= 0x001F);
    }
}

bool only_python_spaces(const S& value)
{
    if (value.empty()) return false; // Pythonの + 量指定子と同様、空には一致しない。
    std::size_t position = 0;
    while (position < value.size()) {
        std::uint32_t codepoint{};
        std::size_t width{};
        const bool valid = decode_utf8(value, position, codepoint, width);
        if (!valid || !is_python_space(codepoint)) return false;
        position += width;
    }
    return true;
}

S strip_python_spaces_impl(const S& value)
{
    std::size_t first = 0;
    while (first < value.size()) {
        std::uint32_t codepoint{};
        std::size_t width{};
        if (!decode_utf8(value, first, codepoint, width) || !is_python_space(codepoint)) break;
        first += width;
    }
    std::size_t end = first;
    std::size_t position = first;
    while (position < value.size()) {
        std::uint32_t codepoint{};
        std::size_t width{};
        if (!decode_utf8(value, position, codepoint, width)) {
            end = position + 1;
            position = end;
            continue;
        }
        if (!is_python_space(codepoint)) end = position + width;
        position += width;
    }
    return value.substr(first, end - first);
}

std::filesystem::path path_from_utf8(const S& value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const auto byte : value)
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    return std::filesystem::path(encoded);
}

bool path_exists(const S& value)
{
    const auto path = path_from_utf8(value);
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        return status && status->exists();
    }
    std::error_code error;
    return std::filesystem::exists(path, error);
}

S join(const std::vector<S>& paths)
{
    ::cw::lib::Arguments values;
    values.reserve(paths.size());
    for (const auto& path : paths) {
        values.emplace_back(path);
    }
    // cw.binary.util.join_paths は、cw.util.join_paths と異なり両端を取り除く。
    auto result = ::cw::util::join_paths(values);
    const auto begin = result.find_first_not_of('/');
    return begin == S::npos ? S{} : result.substr(begin);
}

std::pair<S, S> split_native_path(const S& path)
{
    // os.path.split は実行OSの処理。元の表記とドットを保持する。 std::filesystem
    // の汎用表記では、連続する区切りがまとめられる場合がある。
    constexpr bool windows = std::filesystem::path::preferred_separator == '\\';
    const auto is_separator = [](char ch) { return ch == '/' || (windows && ch == '\\'); };
    std::size_t drive = 0;
    if constexpr (windows) {
        if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]) &&
            (path.size() == 2 || !is_separator(path[2]))) {
            const auto server = path.find_first_of("/\\", 2);
            if (server == S::npos) drive = path.size();
            else {
                const auto share = path.find_first_of("/\\", server + 1);
                drive = share == S::npos ? path.size() : share;
            }
        } else if (path.size() >= 2 && path[1] == ':') drive = 2;
    }
    std::size_t split = path.size();
    while (split > drive && !is_separator(path[split - 1])) --split;
    const S tail = path.substr(split);
    std::size_t end = split;
    while (end > drive && is_separator(path[end - 1])) --end;
    // 先頭部分が区切りだけなら、そのまま保つ（/、//、C:\など）。
    if (end == drive) end = split;
    return {path.substr(0, end), tail};
}

} // namespace

S strip_python_spaces(const S& value)
{
    return strip_python_spaces_impl(value);
}

S join_paths(const ::cw::lib::Arguments& arg_paths)
{
    std::vector<S> paths;
    paths.reserve(arg_paths.size());
    for (const auto& value : arg_paths) {
        if (const auto* string = std::get_if<S>(&value.storage())) {
            if (!string->empty()) paths.push_back(*string);
        } else if (!std::holds_alternative<std::monostate>(value.storage())) {
            throw std::invalid_argument("join_paths expects strings");
        }
    }
    return join(paths);
}

S check_filename(S name)
{
    if (only_python_spaces(name)) return "noname";

    // ファイル名の禁止文字を置き換える前に、XMLのエスケープを戻す。
    replace_all(name, "&amp;", "&");
    replace_all(name, "&lt;", "<");
    replace_all(name, "&gt;", ">");
    replace_all(name, "&quot;", "\"");
    replace_all(name, "&apos;", "'");
    for (const auto& [old_value, new_value] : std::vector<std::pair<S, S>>{
             {"\\", "￥"}, {"/", "／"}, {":", "："}, {",", "，"}, {".", "．"}, {";", "；"},
             {"*", "＊"}, {"?", "？"}, {"\"", "”"}, {"<", "＜"}, {">", "＞"}, {"|", "｜"}})
        replace_all(name, old_value, new_value);

    const auto [stem, extension] = ::cw::util::splitext(std::move(name));
    return strip_python_spaces(stem) + strip_python_spaces(extension);
}

S check_duplicate(S path)
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();

    const auto [directory, original_basename] = split_native_path(path);
    S basename = original_basename;
    const auto [stem, extension] = ::cw::util::splitext(basename);
    const S original_stem = stem;
    const S original_extension = extension;
    ::cw::lib::Integer count = 2;
    while (path_exists(path)) {
        basename = original_stem + "(" + std::to_string(count) + ")" + original_extension;
        ::cw::lib::Arguments parts{::cw::lib::Value(directory), ::cw::lib::Value(basename)};
        path = join_paths(parts);
        ++count;
    }
    return path;
}

S repl_escapechar(S value)
{
    replace_all(value, "&", "&amp;");
    replace_all(value, "<", "&lt;");
    replace_all(value, ">", "&gt;");
    replace_all(value, "\"", "&quot;");
    replace_all(value, "'", "&apos;");
    return value;
}

S repl_specialchar(S value)
{
    S result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto next_character = index + 1 < value.size()
            ? static_cast<unsigned char>(value[index + 1]) : 0;
        const bool next_ascii_alnum = (next_character >= 'a' && next_character <= 'z') ||
                                      (next_character >= 'A' && next_character <= 'Z') ||
                                      (next_character >= '0' && next_character <= '9');
        if (value[index] == '\\' && index + 1 < value.size() && next_ascii_alnum) {
            result += "￥";
            result += value[++index];
        } else {
            result += value[index];
        }
    }
    return result;
}

S repl_specialchar_repl_metachar(::cw::lib::Value arg_match)
{
    const auto* match = std::get_if<S>(&arg_match.storage());
    if (!match) throw std::invalid_argument("match must be string");
    S result = *match;
    replace_all(result, "\\", "￥");
    return result;
}

void main()
{
    // Pythonのモジュールの入口は意図的に何もしない。 アプリの起動
    // は、この補助モジュールではなく cw/cw.cpp が担当する。
}

} // namespace cw::binary::util
