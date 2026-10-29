#include "cw/binary/next/next_binary_reader.hpp"

#include "cw/binary/cwfile.hpp"
#include "cw/lib/encoding.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace cw::binary::next {
namespace {

template <typename ReadByte>
std::uint32_t read_stream_encoded(ReadByte&& read_byte) {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        const auto byte = static_cast<std::uint32_t>(read_byte());
        const auto payload = byte & 0x7fU;
        if (shift == 28 && payload > 0x0fU)
            throw std::overflow_error("CardWirthNext extended integer overflow");
        result |= payload << shift;
        if ((byte & 0x80U) == 0) return result;
    }
    throw std::overflow_error("CardWirthNext extended integer is too long");
}

}  // namespace

std::uint32_t Reader::read_encoded() {
    return read_stream_encoded([this]() {
        return static_cast<std::uint8_t>(m_file.ubyte());
    });
}

std::int32_t Reader::ex_int() {
    const auto encoded = read_encoded();
    if ((encoded & 1U) != 0)
        return -static_cast<std::int32_t>(encoded >> 1U) - 1;
    return static_cast<std::int32_t>(encoded >> 1U);
}

std::uint32_t Reader::ex_uint() { return read_encoded(); }

::cw::lib::String Reader::ex_string(const bool multiline) {
    const auto length = ex_uint();
    if (length == 0) return {};
    constexpr std::uint32_t kPreflightAllocation = 1024U * 1024U;
    if (length > kPreflightAllocation && m_file.seekable()) {
        const auto position = m_file.tell();
        const auto end = m_file.seek(0, 2);
        m_file.seek(position, 0);
        if (end < position || static_cast<std::uint64_t>(end - position) < length)
            throw std::runtime_error("unexpected end of CardWirthNext string");
    }
    auto bytes = m_file.read(static_cast<::cw::lib::Integer>(length));
    if (bytes.size() != length)
        throw std::runtime_error("unexpected end of CardWirthNext string");
    if (!multiline && !bytes.empty() && bytes.back() == 0) bytes.pop_back();

    // Nextは、NULで終わる追加の文字列情報を前に付ける場合がある。
    // D版のcwl読み取りと同様、その接頭部分を文字列として扱わず捨てる。
    const auto zero = std::find(bytes.begin(), bytes.end(), std::uint8_t{0});
    if (zero != bytes.end()) bytes.erase(bytes.begin(), std::next(zero));

    auto value = ::cw::lib::encoding::cp932_to_utf8(
        bytes, ::cw::lib::encoding::ErrorPolicy::Replace);
    std::size_t position = 0;
    while ((position = value.find("\r\n", position)) != ::cw::lib::String::npos)
        value.replace(position, 2, "\n");
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    return value;
}

}  // namespace cw::binary::next
