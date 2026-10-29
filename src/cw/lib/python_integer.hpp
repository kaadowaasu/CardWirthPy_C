#pragma once
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_decimal_digit.hpp"
#include "cw/lib/python_text.hpp"
#include <optional>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <limits>

namespace cw::lib {
inline bool is_python_integer_whitespace(std::uint32_t c) noexcept
{
    // Python の int(str) は str.strip() と異なり、U+001C～U+001F を拒否する。
    return (9<=c && c<=13) || c==0x20 || c==0x85 || c==0xa0 ||
        c==0x1680 || (0x2000<=c && c<=0x200a) || c==0x2028 ||
        c==0x2029 || c==0x202f || c==0x205f || c==0x3000;
}

inline std::optional<std::string> normalize_python_integer(const std::string& text, unsigned base = 10)
{
    // Python の int(str) は Unicode の十進数字、外側の空白、ASCII
    // の符号、数字間の下線を受け入れる。"3abc" の先頭部分だけを受け入れてはならない。
    try {
        if (base != 10 && base != 16) return std::nullopt;
        const auto utf16=::cw::lib::encoding::utf8_to_utf16(text,::cw::lib::encoding::ByteOrder::Little);
        std::vector<std::uint32_t> chars;
        for(std::size_t i=0;i<utf16.size();i+=2) {
            std::uint32_t c=static_cast<unsigned char>(utf16[i]) |
                (static_cast<unsigned char>(utf16[i+1])<<8);
            if(c>=0xd800 && c<=0xdbff) {
                const auto low=static_cast<unsigned char>(utf16[i+2]) |
                    (static_cast<unsigned char>(utf16[i+3])<<8);
                c=0x10000+((c-0xd800)<<10)+(low-0xdc00); i+=2;
            }
            chars.push_back(c);
        }
        std::size_t first=0,last=chars.size();
        while(first<last && is_python_integer_whitespace(chars[first])) ++first;
        while(first<last && is_python_integer_whitespace(chars[last-1])) --last;
        std::string normalized;
        if(first<last && (chars[first]=='+' || chars[first]=='-'))
            normalized.push_back(static_cast<char>(chars[first++]));
        // int(str, 16) は 0x と、その直後の下線一つも許可する。Python
        // は接頭辞の解析前に Unicode の十進数字ゼロを正規化する。
        if (base == 16 && first + 1 < last &&
            python_decimal_digit(chars[first]) == std::optional<unsigned>{0} &&
            (chars[first + 1] == 'x' || chars[first + 1] == 'X')) {
            first += 2;
            if (first < last && chars[first] == '_') ++first;
        }
        bool digit=false;
        for(;first<last;++first) {
            if(chars[first]=='_' && digit) {digit=false;continue;}
            if (base == 16 && (('a' <= chars[first] && chars[first] <= 'f') ||
                               ('A' <= chars[first] && chars[first] <= 'F'))) {
                normalized.push_back(static_cast<char>(chars[first])); digit=true;continue;
            }
            const auto value=python_decimal_digit(chars[first]);
            if(!value) return std::nullopt;
            normalized.push_back(static_cast<char>('0'+*value)); digit=true;
        }
        if(!digit) return std::nullopt;
        return normalized;
    } catch(const ::cw::lib::encoding::EncodingError&) {
        return std::nullopt;
    }
}

// Python の int(bytes) は int(str) と異なり、ASCII の数
// 字・空白だけを受け入れる。UTF-8 の十進数字や非 ASCII の空白は不正とする。
inline std::optional<std::string> normalize_python_integer_bytes(const std::string& bytes)
{
    if (std::any_of(bytes.begin(), bytes.end(), [](unsigned char c) { return c >= 128; }))
        return std::nullopt;
    return normalize_python_integer(bytes);
}

// Python の int(str) の構文を、エンジンの固定幅 Integer
// に解析する。Python 自体は任意精度なので、エンジンの範囲外は縮小
// 変換やトラップ、特に Wasm のトラップにせず、変換失敗として扱う。
inline std::optional<Integer> parse_python_integer(const std::string& text, unsigned base = 10) noexcept
{
    try {
        const auto normalized = normalize_python_integer(text, base);
        if (!normalized) return std::nullopt;
        std::size_t consumed = 0;
        const auto value = std::stoll(*normalized, &consumed, base);
        if (consumed != normalized->size()) return std::nullopt;
        return static_cast<Integer>(value);
    } catch (...) {
        return std::nullopt;
    }
}

inline Integer require_python_integer(const std::string& text, unsigned base = 10)
{
    if (const auto value = parse_python_integer(text, base)) return *value;
    throw std::invalid_argument("invalid or unrepresentable Python integer: " + text);
}

// Py のバイナリー出力は word/dword に詰めるまで任意
// 精度整数を保つ。ネイティブ Integer に収まらない値は、書
// き込めない符号付きの目印で失敗位置と部分出力を保つ。CardWirth
// の 16/32 ビット出力専用とし、通常の getint に
// は使わない。値を変換する側は、目印の演算でオーバーフローせず、
// リソース ID の加算などの後も書き込めないことを保証する。
inline Integer require_python_integer_for_cardwirth_pack(const std::string& text)
{
    const auto normalized = normalize_python_integer(text);
    if (!normalized) throw std::invalid_argument("invalid literal for int()");
    if (const auto parsed = parse_python_integer(*normalized)) return *parsed;
    return normalized->front() == '-' ? std::numeric_limits<Integer>::min()
                                      : std::numeric_limits<Integer>::max();
}

// 旧形式の出力は ID や背景幅に形式バージョン用の小さな補正値を加える。Py
// は無制限に加算してから dword への格納時に拒否する。ネイティブの
// 符号付きオーバーフローによる未定義動作を避けつつ、同じ失敗位置を保つ。
inline Integer add_cardwirth_resource_id_bias(Integer value, Integer bias)
{
    if (bias > 0 && value > std::numeric_limits<Integer>::max() - bias)
        return std::numeric_limits<Integer>::max();
    if (bias < 0 && value < std::numeric_limits<Integer>::min() - bias)
        return std::numeric_limits<Integer>::min();
    return value + bias;
}

// Python は RGB の取り出し時に任意精度の int(str,
// 16) をマスクする。stoll で範囲を狭めず全桁を検証し、要求
// された下位ビットだけを保持する。非常に長い色値や負数も正確に扱う。
inline std::optional<std::uint64_t> python_hex_integer_low_bits(const std::string& text, unsigned bits)
{
    if (bits == 0 || bits > 64) return std::nullopt;
    const auto normalized = normalize_python_integer(text, 16);
    if (!normalized) return std::nullopt;
    const auto mask = bits == 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
    std::uint64_t value = 0;
    for (const auto c : *normalized) {
        if (c == '+' || c == '-') continue;
        const auto digit = c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
        value = (value * 16 + static_cast<unsigned>(digit)) & mask;
    }
    return normalized->front() == '-' ? (std::uint64_t{0} - value) & mask : value;
}

// 大きな Python 整数を先に縮小変換せず、max(0, min(upper, int(text)))
// と同じ結果を求める。上限・下限に達しても文字列全体を検証する。
inline std::optional<Integer> clamp_python_nonnegative_integer(const std::string& text, Integer upper) {
    if (upper < 0) throw std::invalid_argument("negative integer clamp bound");
    const auto normalized = normalize_python_integer(text);
    if (!normalized) return std::nullopt;
    if (normalized->front() == '-') return 0;
    Integer value = 0;
    for (const auto c : *normalized) {
        if (c == '+') continue;
        const Integer digit = c - '0';
        if (value > upper / 10 || (value == upper / 10 && digit > upper % 10)) value = upper;
        else value = value * 10 + digit;
    }
    return value;
}

inline Integer require_clamped_python_nonnegative_integer(const std::string& text, Integer upper) {
    if (const auto value = clamp_python_nonnegative_integer(text, upper)) return *value;
    throw std::invalid_argument("invalid literal for int()");
}
}
