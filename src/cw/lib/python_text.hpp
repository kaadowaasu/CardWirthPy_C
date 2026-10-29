#pragma once

#include "cw/lib/encoding.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace cw::lib {

// Python の str.split("\n") はリテラルのバッ
// クスラッシュ、CR、空項目を保持する。空入力も一つの空項目になる。
inline std::vector<std::string> python_split_newlines(const std::string& text)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (;;) {
        const auto end = text.find('\n', begin);
        result.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

// Python 3.8/3.11 の str.isspace()/str.strip()
// の空白集合。C0 の情報区切り U+001C～U+001F も意図的に含める。
inline bool is_python_str_whitespace(std::uint32_t codepoint) noexcept
{
    return (9 <= codepoint && codepoint <= 13) || codepoint == 0x20 ||
        (0x1c <= codepoint && codepoint <= 0x1f) ||
        codepoint == 0x85 || codepoint == 0xa0 || codepoint == 0x1680 ||
        (0x2000 <= codepoint && codepoint <= 0x200a) ||
        codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x202f ||
        codepoint == 0x205f || codepoint == 0x3000;
}

inline bool python_strip_has_text(const std::string& text)
{
    try {
        const auto utf16 = ::cw::lib::encoding::utf8_to_utf16(
            text, ::cw::lib::encoding::ByteOrder::Little);
        for (std::size_t i = 0; i < utf16.size(); i += 2) {
            std::uint32_t codepoint = static_cast<unsigned char>(utf16[i]) |
                (static_cast<unsigned char>(utf16[i + 1]) << 8);
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                const auto low = static_cast<unsigned char>(utf16[i + 2]) |
                    (static_cast<unsigned char>(utf16[i + 3]) << 8);
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                    (low - 0xdc00);
                i += 2;
            }
            if (!is_python_str_whitespace(codepoint)) return true;
        }
        return false;
    } catch (const ::cw::lib::encoding::EncodingError&) {
        // Engine の String は UTF-8。不正な外部入力もテキスト
        // として扱い、この検証で渡されたバイトが黙って空の名前になることを防ぐ。
        return !text.empty();
    }
}

inline std::string python_strip(const std::string& text)
{
    try {
        const auto utf16 = ::cw::lib::encoding::utf8_to_utf16(
            text, ::cw::lib::encoding::ByteOrder::Little);
        struct Character { std::uint32_t codepoint; std::size_t first; std::size_t last; };
        std::vector<Character> characters;
        for (std::size_t i = 0; i < utf16.size();) {
            const auto first = i;
            std::uint32_t codepoint = static_cast<unsigned char>(utf16[i]) |
                (static_cast<unsigned char>(utf16[i + 1]) << 8);
            i += 2;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                const auto low = static_cast<unsigned char>(utf16[i]) |
                    (static_cast<unsigned char>(utf16[i + 1]) << 8);
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                    (low - 0xdc00);
                i += 2;
            }
            characters.push_back({codepoint, first, i});
        }
        std::size_t first = 0, last = characters.size();
        while (first < last && is_python_str_whitespace(characters[first].codepoint)) ++first;
        while (first < last && is_python_str_whitespace(characters[last - 1].codepoint)) --last;
        if (first == last) return {};
        const auto byte_first = characters[first].first;
        const auto byte_last = characters[last - 1].last;
        const ::cw::lib::Bytes stripped(utf16.begin() + byte_first,
                                       utf16.begin() + byte_last);
        return ::cw::lib::encoding::utf16_to_utf8(
            stripped,
            ::cw::lib::encoding::ByteOrder::Little,
            ::cw::lib::encoding::ErrorPolicy::Strict, false);
    } catch (const ::cw::lib::encoding::EncodingError&) {
        return text;
    }
}

// 引数なしの Python の str.rstrip() に対応する。strip
// と同じ Unicode 空白判定を使い、先頭側の文字はすべて保持する。
inline std::string python_rstrip(const std::string& text)
{
    try {
        auto utf16 = ::cw::lib::encoding::utf8_to_utf16(
            text, ::cw::lib::encoding::ByteOrder::Little);
        std::size_t last = utf16.size();
        while (last != 0) {
            auto first = last - 2;
            std::uint32_t codepoint = static_cast<unsigned char>(utf16[first]) |
                (static_cast<unsigned char>(utf16[first + 1]) << 8);
            if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                first -= 2; // 厳密な UTF-8 復号によって、有効な組が得られた。
                const auto high = static_cast<unsigned char>(utf16[first]) |
                    (static_cast<unsigned char>(utf16[first + 1]) << 8);
                codepoint = 0x10000 + ((high - 0xd800) << 10) +
                    (codepoint - 0xdc00);
            }
            if (!is_python_str_whitespace(codepoint)) break;
            last = first;
        }
        if (last == utf16.size()) return text;
        if (last == 0) return {};
        utf16.resize(last);
        return ::cw::lib::encoding::utf16_to_utf8(
            utf16, ::cw::lib::encoding::ByteOrder::Little,
            ::cw::lib::encoding::ErrorPolicy::Strict, false);
    } catch (const ::cw::lib::encoding::EncodingError&) {
        // Python の str は不正な UTF-8 バイトを含められない。strip()
        // と同様、外部からの不正なエンジン入力は捨てずに保持する。
        return text;
    }
}

// 区切りなしの Python の str.split() と同様、Unicode
// の連続する空白で分割し、先頭・末尾の空項目を省く。std::istringstream
// はロケールの空白しか認識せず、全角・改行しない空白を含む拡
// 張 XML が一つの未知トークンになってしまうため使わない。
inline std::vector<std::string> python_split_whitespace(const std::string& text)
{
    try {
        const auto utf16 = ::cw::lib::encoding::utf8_to_utf16(
            text, ::cw::lib::encoding::ByteOrder::Little);
        struct Character { std::uint32_t codepoint; std::size_t first; std::size_t last; };
        std::vector<Character> characters;
        for (std::size_t i = 0; i < utf16.size();) {
            const auto first = i;
            std::uint32_t codepoint = static_cast<unsigned char>(utf16[i]) |
                (static_cast<unsigned char>(utf16[i + 1]) << 8);
            i += 2;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                const auto low = static_cast<unsigned char>(utf16[i]) |
                    (static_cast<unsigned char>(utf16[i + 1]) << 8);
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                    (low - 0xdc00);
                i += 2;
            }
            characters.push_back({codepoint, first, i});
        }

        std::vector<std::string> result;
        for (std::size_t i = 0; i < characters.size();) {
            while (i < characters.size() && is_python_str_whitespace(characters[i].codepoint)) ++i;
            if (i == characters.size()) break;
            const auto first = characters[i].first;
            while (i < characters.size() && !is_python_str_whitespace(characters[i].codepoint)) ++i;
            const auto last = characters[i - 1].last;
            const ::cw::lib::Bytes field(utf16.begin() + first, utf16.begin() + last);
            result.push_back(::cw::lib::encoding::utf16_to_utf8(
                field, ::cw::lib::encoding::ByteOrder::Little,
                ::cw::lib::encoding::ErrorPolicy::Strict, false));
        }
        return result;
    } catch (const ::cw::lib::encoding::EncodingError&) {
        // 不正な外部データを黙って捨てず、観測可能な一トークンとして
        // 保つ。python_strip() の代替方針に合わせる。
        return text.empty() ? std::vector<std::string>{} : std::vector<std::string>{text};
    }
}

} // namespace cw::lib
