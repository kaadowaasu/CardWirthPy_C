#include "cwxeditor_src/core/cwx/binary.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace cwxeditor::cwx::binary {
namespace {

[[nodiscard]] std::out_of_range readOver(std::size_t pointer,
                                         std::size_t amount) {
    return std::out_of_range("read over: 0x" + std::to_string(pointer) +
                             " + " + std::to_string(amount));
}

void checkOffsetAdvance(std::size_t offset, std::size_t amount,
                        std::size_t limit) {
    if (offset > limit || amount > limit - offset) {
        throw readOver(offset, amount);
    }
}

[[nodiscard]] Byte readVarintBufferByte(ByteView bytes, std::size_t& offset) {
    // Dはbytes[offset++]でアクセスするため、範囲検査に失敗しても
    // 位置は進む。std::spanを明示的に検査しつつ、この順序を維持する。
    const auto index = offset++;
    if (index >= bytes.size())
        throw std::out_of_range("extended integer buffer is too small");
    return bytes[index];
}

[[nodiscard]] int fileSeek(std::FILE& file, std::int64_t offset, int origin) noexcept {
    // DのRawFileはfseekの前に公開long位置をintへ狭める。大きなファイ
    // ルで切り詰められる不具合を避けるため、ここでは公開された64ビット位置を保持する。
#ifdef _WIN32
    return ::_fseeki64(&file, offset, origin);
#else
    return ::fseeko(&file, static_cast<off_t>(offset), origin);
#endif
}

#if defined(_WIN32)
[[nodiscard]] std::wstring utf8ToWide(StringView input) {
    std::wstring output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i]);
        char32_t codePoint = 0;
        std::size_t continuationCount = 0;
        if (first <= 0x7F) {
            codePoint = first;
        } else if (first >= 0xC2 && first <= 0xDF) {
            codePoint = static_cast<char32_t>(first & 0x1F);
            continuationCount = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codePoint = static_cast<char32_t>(first & 0x0F);
            continuationCount = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codePoint = static_cast<char32_t>(first & 0x07);
            continuationCount = 3;
        } else {
            throw std::invalid_argument("Invalid UTF-8 path");
        }
        if (i + continuationCount >= input.size()) {
            throw std::invalid_argument("Invalid UTF-8 path");
        }
        for (std::size_t j = 1; j <= continuationCount; ++j) {
            const auto next = static_cast<unsigned char>(input[i + j]);
            if ((next & 0xC0) != 0x80) {
                throw std::invalid_argument("Invalid UTF-8 path");
            }
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            codePoint > 0x10FFFF) {
            throw std::invalid_argument("Invalid UTF-8 path");
        }
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint <= 0xFFFF) {
                output.push_back(static_cast<wchar_t>(codePoint));
            } else {
                const auto value = codePoint - 0x10000;
                output.push_back(
                    static_cast<wchar_t>(0xD800 + (value >> 10)));
                output.push_back(
                    static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
            }
        } else {
            output.push_back(static_cast<wchar_t>(codePoint));
        }
        i += continuationCount + 1;
    }
    return output;
}
#endif

}  // namespace

String repeat(StringView s, std::int32_t count) {
    String result;
    if (count <= 0 || s.empty()) {
        return result;
    }
    result.reserve(s.size() * static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        result.append(s);
    }
    return result;
}

ByteIO::ByteIO(MutableByteView bytes)
    : _bytes(bytes), _constBytes(bytes), _readOnly(false) {}

ByteIO::ByteIO(ByteView bytes)
    : _constBytes(bytes), _readOnly(true) {}

ByteIO::ByteIO(std::span<std::byte> bytes)
    : ByteIO(MutableByteView(reinterpret_cast<Byte*>(bytes.data()),
                             bytes.size())) {}

ByteIO::ByteIO(std::span<const std::byte> bytes)
    : ByteIO(ByteView(reinterpret_cast<const Byte*>(bytes.data()),
                      bytes.size())) {}

ByteIO::ByteIO(std::size_t firstBuffer)
    : _owned(std::make_shared<Bytes>(firstBuffer)), _readOnly(false) {
    refreshOwnedViews();
}

ByteIO::ByteIO(const ByteIO& other)
    : _readOnly(other._readOnly), _pointer(other._pointer) {
    if (other._owned) {
        _owned = std::make_shared<Bytes>(*other._owned);
        refreshOwnedViews();
    } else {
        _bytes = other._bytes;
        _constBytes = other._constBytes;
    }
}

ByteIO& ByteIO::operator=(const ByteIO& other) {
    if (this == &other) return *this;
    ByteIO copy(other);
    *this = std::move(copy);
    return *this;
}

ByteIO::ByteIO(ByteIO&& other) noexcept
    : _owned(std::move(other._owned)), _readOnly(other._readOnly),
      _pointer(other._pointer) {
    if (_owned) {
        refreshOwnedViews();
    } else {
        _bytes = other._bytes;
        _constBytes = other._constBytes;
    }
    other._bytes = {};
    other._constBytes = {};
    other._readOnly = false;
    other._pointer = 0;
}

ByteIO& ByteIO::operator=(ByteIO&& other) noexcept {
    if (this == &other) return *this;
    _owned = std::move(other._owned);
    _readOnly = other._readOnly;
    _pointer = other._pointer;
    if (_owned) {
        refreshOwnedViews();
    } else {
        _bytes = other._bytes;
        _constBytes = other._constBytes;
    }
    other._bytes = {};
    other._constBytes = {};
    other._readOnly = false;
    other._pointer = 0;
    return *this;
}

void ByteIO::dispose() {
    _owned.reset();
    _bytes = {};
    _constBytes = {};
}

MutableByteView ByteIO::bytes() noexcept {
    if (_readOnly) {
        return {};
    }
    if (_pointer == _bytes.size()) {
        return _bytes;
    }
    return _bytes.first(std::min(_pointer, _bytes.size()));
}

ByteView ByteIO::bytes() const noexcept {
    if (_pointer == _constBytes.size()) {
        return _constBytes;
    }
    return _constBytes.first(std::min(_pointer, _constBytes.size()));
}

std::size_t ByteIO::pointer() const noexcept {
    return _pointer;
}

void ByteIO::pointer(std::size_t pos) {
    _pointer = 0;
    // Dはカーソルをリセットした後、posを符号付きのseekへ
    // 渡す。INT64_MAXを超える値は負になって下限検査で失
    // 敗するため、実装定義の符号変換を使わず明示的に拒否する。
    if (pos > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::out_of_range("pointer out of range");
    }
    seek(static_cast<std::int64_t>(pos));
}

std::size_t ByteIO::length() const noexcept {
    return _constBytes.size();
}

bool ByteIO::eob() const noexcept {
    return _pointer >= _constBytes.size();
}

void ByteIO::seek(std::int64_t bytes) {
    if (bytes < 0) {
        // INT64_MINの符号を反転するとC++では未定義の符号付き
        // 演算になるため、反転せずにDの符号なし比較用の大きさを作る。
        const auto amount = static_cast<std::uint64_t>(-(bytes + 1)) + 1;
        if (amount > _pointer) {
            throw std::out_of_range("read over: 0x" +
                                    std::to_string(_pointer) + " - " +
                                    std::to_string(amount));
        }
        _pointer -= static_cast<std::size_t>(amount);
        return;
    }
    if (bytes > 0) {
        const auto amount = static_cast<std::uint64_t>(bytes);
        // 加算が折り返した場合の拒否も含め、Dの厳密な終端の制限を維持する。
        if (amount > std::numeric_limits<std::size_t>::max() - _pointer ||
            _pointer + static_cast<std::size_t>(amount) >= _constBytes.size()) {
            throw std::out_of_range("read over: 0x" +
                                    std::to_string(_pointer) + " + " +
                                    std::to_string(amount));
        }
        _pointer += static_cast<std::size_t>(amount);
    }
}

void ByteIO::seekSet(std::int64_t offset) {
    pointer(static_cast<std::size_t>(offset));
}

MutableByteView ByteIO::rawRead(MutableByteView buffer) {
    if (_pointer > _constBytes.size()) {
        throw readOver(_pointer, buffer.size());
    }
    const auto len =
        std::min(_constBytes.size() - _pointer, buffer.size());
    if (len != 0) {
        std::memmove(buffer.data(), _constBytes.data() + _pointer, len);
    }
    _pointer += len;
    return buffer.first(len);
}

void ByteIO::rawWrite(ByteView buffer) {
    if (_readOnly) {
        throw std::logic_error("write to read-only ByteIO");
    }
    if (buffer.size() >
        std::numeric_limits<std::size_t>::max() - _pointer) {
        throw std::length_error("ByteIO size overflow");
    }
    const auto end = _pointer + buffer.size();
    Bytes preserved;
    if (end > _bytes.size() && !buffer.empty()) {
        preserved.assign(buffer.begin(), buffer.end());
        buffer = ByteView(preserved);
    }
    ensureWritable(end, Growth::Raw);
    if (!buffer.empty()) {
        std::memmove(_bytes.data() + _pointer, buffer.data(), buffer.size());
    }
    _pointer = end;
    refreshOwnedViews();
}

Byte ByteIO::readUByte() {
    if (_pointer >= _constBytes.size()) {
        throw readOver(_pointer, 1);
    }
    return _constBytes[_pointer++];
}

std::int8_t ByteIO::readByte() {
    return static_cast<std::int8_t>(readUByte());
}

void ByteIO::read(Byte& b) {
    b = readUByte();
}

void ByteIO::write(Byte value) {
    if (_pointer == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("ByteIO size overflow");
    }
    ensureWritable(_pointer + 1, Growth::Single);
    _bytes[_pointer++] = value;
    refreshOwnedViews();
}

void ByteIO::write(std::int8_t value) {
    write(static_cast<Byte>(value));
}

void ByteIO::write(char value) {
    write(static_cast<Byte>(static_cast<unsigned char>(value)));
}

void ByteIO::write(std::byte value) {
    write(static_cast<Byte>(value));
}

void ByteIO::writeB(Byte value) {
    write(value);
}

void ByteIO::writeB(std::int8_t value) {
    write(value);
}

void ByteIO::writeB(char value) {
    write(value);
}

void ByteIO::writeL(Byte value) {
    write(value);
}

void ByteIO::writeL(std::int8_t value) {
    write(value);
}

void ByteIO::writeL(char value) {
    write(value);
}

void ByteIO::checkReadable(std::size_t amount) const {
    checkOffsetAdvance(_pointer, amount, _constBytes.size());
}

void ByteIO::read(MutableByteView buffer) {
    checkReadable(buffer.size());
    if (!buffer.empty()) {
        std::memmove(buffer.data(), _constBytes.data() + _pointer, buffer.size());
    }
    _pointer += buffer.size();
}

void ByteIO::read(std::span<std::int8_t> buffer) {
    checkReadable(buffer.size());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<std::int8_t>(_constBytes[_pointer + i]);
    }
    _pointer += buffer.size();
}

void ByteIO::read(std::span<std::byte> buffer) {
    checkReadable(buffer.size());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<std::byte>(_constBytes[_pointer + i]);
    }
    _pointer += buffer.size();
}

ByteView ByteIO::read(std::size_t len) {
    checkReadable(len);
    const auto result = _constBytes.subspan(_pointer, len);
    _pointer += len;
    return result;
}

void ByteIO::write(ByteView buffer) {
    if (buffer.size() >
        std::numeric_limits<std::size_t>::max() - _pointer) {
        throw std::length_error("ByteIO size overflow");
    }
    const auto end = _pointer + buffer.size();
    Bytes preserved;
    if (end >= _bytes.size() && !buffer.empty()) {
        preserved.assign(buffer.begin(), buffer.end());
        buffer = ByteView(preserved);
    }
    ensureWritable(end, Growth::Array);
    if (!buffer.empty()) {
        std::memmove(_bytes.data() + _pointer, buffer.data(), buffer.size());
    }
    _pointer = end;
    refreshOwnedViews();
}

void ByteIO::write(std::span<const std::int8_t> buffer) {
    write(ByteView(reinterpret_cast<const Byte*>(buffer.data()), buffer.size()));
}

void ByteIO::write(std::span<const std::byte> buffer) {
    write(ByteView(reinterpret_cast<const Byte*>(buffer.data()), buffer.size()));
}

void ByteIO::writeB(ByteView buffer) {
    write(buffer);
}

void ByteIO::writeB(std::span<const std::int8_t> buffer) {
    write(buffer);
}

void ByteIO::writeB(std::span<const std::byte> buffer) {
    write(buffer);
}

void ByteIO::writeL(ByteView buffer) {
    write(buffer);
}

void ByteIO::writeL(std::span<const std::int8_t> buffer) {
    write(buffer);
}

void ByteIO::writeL(std::span<const std::byte> buffer) {
    write(buffer);
}

void ByteIO::ensureWritable(std::size_t end, Growth growth) {
    if (_readOnly) {
        throw std::logic_error("write to read-only ByteIO");
    }
    const bool inclusiveGrowth = growth == Growth::Array ||
                                 growth == Growth::Integer;
    if (inclusiveGrowth ? end < _bytes.size() : end <= _bytes.size()) {
        return;
    }
    if (!_owned) {
        _owned = std::make_shared<Bytes>(_bytes.begin(), _bytes.end());
    }
    const auto oldSize = _owned->size();
    const auto amount = end - _pointer;
    std::size_t newSize = end;
    switch (growth) {
    case Growth::Single:
        if (oldSize <= (std::numeric_limits<std::size_t>::max() - 1) / 2) {
            newSize = oldSize * 2 + 1;
        }
        break;
    case Growth::Array: {
        const auto scaled = static_cast<std::size_t>(
            static_cast<long double>(oldSize) * 1.2L);
        if (scaled <= std::numeric_limits<std::size_t>::max() - amount) {
            newSize = scaled + amount;
        }
        break;
    }
    case Growth::Integer:
        if (oldSize <=
            (std::numeric_limits<std::size_t>::max() - amount) / 2) {
            newSize = oldSize * 2 + amount;
        }
        break;
    case Growth::Raw:
        newSize = end;
        break;
    }
    if (newSize < end) {
        newSize = end;
    }
    _owned->resize(newSize);
    refreshOwnedViews();
}

void ByteIO::refreshOwnedViews() noexcept {
    if (_owned) {
        _bytes = MutableByteView(*_owned);
        _constBytes = ByteView(*_owned);
    }
}

std::int32_t ByteIO::readExInt() {
    return binary::readExInt(true, ByteReader([this] { return readUByte(); }));
}

std::int32_t ByteIO::readExUInt() {
    return binary::readExInt(false, ByteReader([this] { return readUByte(); }));
}

void ByteIO::writeExInt(std::int32_t value) {
    binary::writeExInt(value, true,
                       ByteWriter([this](Byte b) { write(b); }));
}

void ByteIO::writeExUInt(std::int32_t value) {
    binary::writeExInt(value, false,
                       ByteWriter([this](Byte b) { write(b); }));
}

void seekCur(std::FILE& file, std::int64_t offset) {
    const auto result = fileSeek(file, offset, SEEK_CUR);
    if (result != 0) {
        throw std::runtime_error("seek failed");
    }
}

void seekSet(std::FILE& file, std::int64_t offset) {
    const auto result = fileSeek(file, offset, SEEK_SET);
    if (result != 0) {
        throw std::runtime_error("seek failed");
    }
}

void seekEnd(std::FILE& file, std::int64_t offset) {
    const auto result = fileSeek(file, offset, SEEK_END);
    if (result != 0) {
        throw std::runtime_error("seek failed");
    }
}

std::int32_t readExInt(bool sign, const ByteReader& readByte) {
    std::uint32_t result = 0;
    unsigned shift = 0;
    while (true) {
        const Byte byte = readByte();
        const auto part = static_cast<std::uint32_t>(byte & 0x7F);
        if (shift < 32) {
            result |= part << shift;
        }
        if ((byte & 0x80) == 0) {
            break;
        }
        if (shift >= 28) {
            // Dのbinary.dでは次の反復でuintを35ビット以上シフトすることにな
            // るが、そのシフトはDで未定義。書き込み側は最大5バイトを出力するため、その未
            // 定義の復号経路へ入る前に拒否する。 https://dlang.org/spec/expression.html#shift_expressions
            //
            throw std::overflow_error("extended integer is too long");
        }
        shift += 7;
    }
    if (!sign) {
        return std::bit_cast<std::int32_t>(result);
    }
    if ((result & 0x1) != 0) {
        return std::bit_cast<std::int32_t>(~(result >> 1));
    }
    return std::bit_cast<std::int32_t>(result >> 1);
}

void writeExInt(std::int32_t value, bool sign, const ByteWriter& writeByte) {
    std::uint32_t value2 = 0;
    if (sign) {
        if (value < 0) {
            value2 = (static_cast<std::uint32_t>(-(value + 1)) << 1) | 0x1;
        } else {
            value2 = static_cast<std::uint32_t>(value) << 1;
        }
    } else {
        value2 = static_cast<std::uint32_t>(value);
    }
    do {
        Byte byte = static_cast<Byte>(value2 & 0x7F);
        value2 >>= 7;
        if (value2 != 0) {
            byte = static_cast<Byte>(byte | 0x80);
        }
        writeByte(byte);
    } while (value2 != 0);
}

void writeExInt(std::int32_t value, MutableByteView bytes,
                std::size_t& offset) {
    writeExInt(value, true, [&bytes, &offset](Byte byte) {
        if (offset >= bytes.size()) {
            throw std::out_of_range("extended integer buffer is too small");
        }
        bytes[offset++] = byte;
    });
}

void writeExUInt(std::int32_t value, MutableByteView bytes,
                 std::size_t& offset) {
    writeExInt(value, false, [&bytes, &offset](Byte byte) {
        if (offset >= bytes.size()) {
            throw std::out_of_range("extended integer buffer is too small");
        }
        bytes[offset++] = byte;
    });
}

std::int32_t readExInt(ByteView bytes, std::size_t& offset) {
    return readExInt(true, [&bytes, &offset] {
        return readVarintBufferByte(bytes, offset);
    });
}

std::int32_t readExUInt(ByteView bytes, std::size_t& offset) {
    return readExInt(false, [&bytes, &offset] {
        return readVarintBufferByte(bytes, offset);
    });
}

RawFile::RawFile(StringView file, StringView mode) {
#if defined(_WIN32)
    const auto fileName = utf8ToWide(file);
    const auto modeName = utf8ToWide(mode);
    _fp = ::_wfopen(fileName.c_str(), modeName.c_str());
#else
    const auto fileName = String(file);
    const auto modeName = String(mode);
    _fp = std::fopen(fileName.c_str(), modeName.c_str());
#endif
    if (_fp == nullptr) {
        throw std::runtime_error("could not open file: " + String(file));
    }
    std::setbuf(_fp, _buf.data());
}

RawFile::~RawFile() {
    close();
}

void RawFile::close() {
    if (_fp != nullptr) {
        std::fclose(_fp);
        _fp = nullptr;
    }
}

std::uint64_t RawFile::size() {
    const auto position = tell();
    seekEnd(0);
    const auto result = tell();
    seekSet(position);
    return static_cast<std::uint64_t>(result);
}

MutableByteView RawFile::rawRead(MutableByteView buffer) {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    // Dの実装は出力先バッファ全体を一項目のサイズとして
    // freadへ渡す。この全体成功か失敗かの結果を維持
    // し、一項目の部分読み込みを成功として公開しない。
    const auto count = std::fread(buffer.data(), buffer.size(), 1, _fp);
    return buffer.first(count * buffer.size());
}

void RawFile::seek(std::int64_t offset, int origin) {
    switch (origin) {
    case SEEK_SET:
        seekSet(offset);
        return;
    case SEEK_CUR:
        seekCur(offset);
        return;
    case SEEK_END:
        seekEnd(offset);
        return;
    default:
        throw std::invalid_argument("invalid seek origin");
    }
}

void RawFile::seekCur(std::int64_t offset) {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    (void)fileSeek(*_fp, offset, SEEK_CUR);
}

void RawFile::seekSet(std::int64_t offset) {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    (void)fileSeek(*_fp, offset, SEEK_SET);
}

void RawFile::seekEnd(std::int64_t offset) {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    (void)fileSeek(*_fp, offset, SEEK_END);
}

void RawFile::flush() {
    (void)std::fflush(_fp);
}

std::int64_t RawFile::tell() {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    // WindowsでもDのlongの戻り値幅を守る。通常のftellはWindows
    // では32ビットのlongを使うため、大きなファイルの位置を正しく返せない。
#ifdef _WIN32
    const auto result = ::_ftelli64(_fp);
#else
    const auto result = ::ftello(_fp);
#endif
    return static_cast<std::int64_t>(result);
}

void RawFile::writeln(StringView s) {
    if (_fp == nullptr) {
        throw std::runtime_error("file is closed");
    }
    (void)std::fwrite(s.data(), s.size(), 1, _fp);
    (void)std::fputc('\n', _fp);
}

::cwxeditor::lib::Owner<RawFile> rawFile(StringView file, StringView mode) {
    return std::make_shared<RawFile>(file, mode);
}

}  // namespace cwxeditor::cwx::binary
