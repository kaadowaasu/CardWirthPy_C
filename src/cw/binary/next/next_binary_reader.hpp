#pragma once

#include "cw/lib/compat.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <span>

namespace cw::binary::cwfile {
class CWFile;
}

namespace cw::binary::next {

// CardWirthNext 1.60 は、新しく追加された整数の多く
// をリトルエンディアンの 128 進数で保存する。符号付き値はジグザグ
// 符号化してから七ビットずつに分ける。CWFile の Classic
// 用基本処理とは分離し、整数の形式を取り違えて、I/O エラーなし
// に .wsm/.wid ストリームの後続位置がずれることを防ぐ。
namespace detail {
[[nodiscard]] constexpr std::uint32_t decode_ex_encoded(
    const std::span<const std::uint8_t> bytes, std::size_t& offset) {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (offset >= bytes.size())
            throw std::out_of_range("CardWirthNext extended integer is truncated");
        const auto byte = static_cast<std::uint32_t>(bytes[offset++]);
        const auto payload = byte & 0x7fU;
        if (shift == 28 && payload > 0x0fU)
            throw std::overflow_error("CardWirthNext extended integer overflow");
        result |= payload << shift;
        if ((byte & 0x80U) == 0) return result;
    }
    throw std::overflow_error("CardWirthNext extended integer is too long");
}
}  // namespace detail

[[nodiscard]] constexpr std::int32_t decode_ex_int(
    const std::span<const std::uint8_t> bytes, std::size_t& offset) {
    const auto encoded = detail::decode_ex_encoded(bytes, offset);
    if ((encoded & 1U) != 0)
        return -static_cast<std::int32_t>(encoded >> 1U) - 1;
    return static_cast<std::int32_t>(encoded >> 1U);
}

[[nodiscard]] constexpr std::uint32_t decode_ex_uint(
    const std::span<const std::uint8_t> bytes, std::size_t& offset) {
    return detail::decode_ex_encoded(bytes, offset);
}

class Reader final {
public:
    explicit Reader(::cw::binary::cwfile::CWFile& file) noexcept : m_file(file) {}

    [[nodiscard]] std::int32_t ex_int();
    [[nodiscard]] std::uint32_t ex_uint();
    [[nodiscard]] ::cw::lib::String ex_string(bool multiline = false);

private:
    [[nodiscard]] std::uint32_t read_encoded();

    ::cw::binary::cwfile::CWFile& m_file;
};

}  // namespace cw::binary::next
