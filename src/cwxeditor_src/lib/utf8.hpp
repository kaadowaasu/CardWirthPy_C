#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cwxeditor::lib::utf8 {

[[nodiscard]] inline bool is_scalar(const std::uint32_t value) noexcept {
    return value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
}

// この補助処理はUnicodeスカラー値だけを検証する。より狭いXML 1.0
// の文字範囲は、XMLパーサーがこのエンコーダーを呼ぶ前に確認する。
inline void append_scalar(std::string& output, const std::uint32_t value) {
    if (!is_scalar(value)) {
        throw std::invalid_argument("Invalid Unicode scalar value");
    }
    if (value <= 0x7FU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

[[nodiscard]] inline std::string encode_scalar(const std::uint32_t value) {
    std::string result;
    result.reserve(4U);
    append_scalar(result, value);
    return result;
}

// スカラー値を一つ読み進め、単一文字と文字列全体の変換で、不
// 完全・過長・サロゲート・範囲外の入力に同じ検証を適用する。
[[nodiscard]] inline char32_t consume_scalar(std::string_view& text) {
    if (text.empty()) {
        throw std::invalid_argument("Expected one UTF-8 scalar");
    }

    const auto first = static_cast<unsigned char>(text.front());
    std::size_t length = 0U;
    std::uint32_t value = 0U;
    if (first <= 0x7FU) {
        length = 1U;
        value = first;
    } else if (first >= 0xC2U && first <= 0xDFU) {
        length = 2U;
        value = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        length = 3U;
        value = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        length = 4U;
        value = first & 0x07U;
    } else {
        throw std::invalid_argument("Invalid UTF-8 leading byte");
    }

    if (text.size() < length) {
        throw std::invalid_argument("Incomplete UTF-8 scalar");
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0U) != 0x80U) {
            throw std::invalid_argument("Invalid UTF-8 continuation byte");
        }
        value = (value << 6U) | (byte & 0x3FU);
    }

    // 上記の先頭バイトの範囲とこれらの制限で、過長形式、サロゲートのUTF-8
    // 符号化、Unicode上限を超えるコードポイントを拒否する。
    if ((length == 2U && value < 0x80U) ||
        (length == 3U && value < 0x800U) ||
        (length == 4U && value < 0x10000U) || !is_scalar(value)) {
        throw std::invalid_argument("Invalid UTF-8 scalar");
    }
    text.remove_prefix(length);
    return static_cast<char32_t>(value);
}

// UTF-8スカラー値をちょうど一つ復号し、後続のスカラー値があれば拒否する。
[[nodiscard]] inline char32_t decode_one_scalar(std::string_view text) {
    const auto value = consume_scalar(text);
    if (!text.empty()) {
        throw std::invalid_argument("Expected exactly one UTF-8 scalar");
    }
    return value;
}

}  // namespace cwxeditor::lib::utf8
