#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::binary {

using Byte = ::cwxeditor::lib::Byte;
using Bytes = ::cwxeditor::lib::Bytes;
using ByteView = ::cwxeditor::lib::ByteView;
using MutableByteView = ::cwxeditor::lib::MutableByteView;
using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using ByteReader = std::function<Byte()>;
using ByteWriter = std::function<void(Byte)>;

[[nodiscard]] std::int32_t readExInt(bool sign, const ByteReader& readByte);
void writeExInt(std::int32_t value, bool sign, const ByteWriter& writeByte);

[[nodiscard]] String repeat(StringView s, std::int32_t count);

/// 読み書き位置を明示し、エンディアンを扱う補助機能を持つバイトストリーム。
class ByteIO {
public:
    ByteIO() = default;
    explicit ByteIO(MutableByteView bytes);
    explicit ByteIO(ByteView bytes);
    explicit ByteIO(std::span<std::byte> bytes);
    explicit ByteIO(std::span<const std::byte> bytes);
    explicit ByteIO(std::size_t firstBuffer);
    ByteIO(const ByteIO& other);
    ByteIO& operator=(const ByteIO& other);
    ByteIO(ByteIO&& other) noexcept;
    ByteIO& operator=(ByteIO&& other) noexcept;

    void dispose();

    [[nodiscard]] MutableByteView bytes() noexcept;
    [[nodiscard]] ByteView bytes() const noexcept;

    [[nodiscard]] std::size_t pointer() const noexcept;
    void pointer(std::size_t pos);

    [[nodiscard]] std::size_t length() const noexcept;
    [[nodiscard]] bool eob() const noexcept;

    void seek(std::int64_t bytes);
    void seekCur(std::int64_t bytes) { seek(bytes); }
    void seekSet(std::int64_t offset);

    [[nodiscard]] MutableByteView rawRead(MutableByteView buffer);
    void rawWrite(ByteView buffer);

    [[nodiscard]] Byte readUByte();
    [[nodiscard]] std::int8_t readByte();
    void read(Byte& b);
    [[nodiscard]] Byte readUByteB() { return readUByte(); }
    [[nodiscard]] std::int8_t readByteB() { return readByte(); }
    [[nodiscard]] Byte readUByteL() { return readUByte(); }
    [[nodiscard]] std::int8_t readByteL() { return readByte(); }

    void write(Byte value);
    void write(std::int8_t value);
    void write(char value);
    void write(std::byte value);

    void writeB(Byte value);
    void writeB(std::int8_t value);
    void writeB(char value);
    void writeL(Byte value);
    void writeL(std::int8_t value);
    void writeL(char value);

    void read(MutableByteView buffer);
    void read(std::span<std::int8_t> buffer);
    void read(std::span<std::byte> buffer);
    [[nodiscard]] ByteView read(std::size_t len);
    void readB(Byte& b) { read(b); }
    void readB(MutableByteView buffer) { read(buffer); }
    void readB(std::span<std::int8_t> buffer) { read(buffer); }
    void readB(std::span<std::byte> buffer) { read(buffer); }
    [[nodiscard]] ByteView readB(std::size_t len) { return read(len); }
    void readL(Byte& b) { read(b); }
    void readL(MutableByteView buffer) { read(buffer); }
    void readL(std::span<std::int8_t> buffer) { read(buffer); }
    void readL(std::span<std::byte> buffer) { read(buffer); }
    [[nodiscard]] ByteView readL(std::size_t len) { return read(len); }

    void write(ByteView buffer);
    void write(std::span<const std::int8_t> buffer);
    void write(std::span<const std::byte> buffer);

    void writeB(ByteView buffer);
    void writeB(std::span<const std::int8_t> buffer);
    void writeB(std::span<const std::byte> buffer);
    void writeL(ByteView buffer);
    void writeL(std::span<const std::int8_t> buffer);
    void writeL(std::span<const std::byte> buffer);

    template <typename T>
    [[nodiscard]] T readBytesBImpl() {
        return readInteger<T>(true);
    }

    template <typename T>
    [[nodiscard]] T readBytesLImpl() {
        return readInteger<T>(false);
    }

    template <typename T>
    void writeBytesBImpl(T value) {
        writeInteger(value, true);
    }

    template <typename T>
    void writeBytesLImpl(T value) {
        writeInteger(value, false);
    }

    template <typename T>
    [[nodiscard]] T readBytesB() {
        return readBytesBImpl<T>();
    }

    template <typename T>
    [[nodiscard]] T readBytesL() {
        return readBytesLImpl<T>();
    }

    [[nodiscard]] std::int64_t readLongB() { return readBytesB<std::int64_t>(); }
    [[nodiscard]] std::uint64_t readULongB() { return readBytesB<std::uint64_t>(); }
    [[nodiscard]] std::int32_t readIntB() { return readBytesB<std::int32_t>(); }
    [[nodiscard]] std::uint32_t readUIntB() { return readBytesB<std::uint32_t>(); }
    [[nodiscard]] std::int16_t readShortB() { return readBytesB<std::int16_t>(); }
    [[nodiscard]] std::uint16_t readUShortB() { return readBytesB<std::uint16_t>(); }
    [[nodiscard]] std::int64_t readLongL() { return readBytesL<std::int64_t>(); }
    [[nodiscard]] std::uint64_t readULongL() { return readBytesL<std::uint64_t>(); }
    [[nodiscard]] std::int32_t readIntL() { return readBytesL<std::int32_t>(); }
    [[nodiscard]] std::uint32_t readUIntL() { return readBytesL<std::uint32_t>(); }
    [[nodiscard]] std::int16_t readShortL() { return readBytesL<std::int16_t>(); }
    [[nodiscard]] std::uint16_t readUShortL() { return readBytesL<std::uint16_t>(); }

    void writeB(std::int64_t value) { writeBytesBImpl(value); }
    void writeB(std::uint64_t value) { writeBytesBImpl(value); }
    void writeB(std::int32_t value) { writeBytesBImpl(value); }
    void writeB(std::uint32_t value) { writeBytesBImpl(value); }
    void writeB(std::int16_t value) { writeBytesBImpl(value); }
    void writeB(std::uint16_t value) { writeBytesBImpl(value); }
    void writeL(std::int64_t value) { writeBytesLImpl(value); }
    void writeL(std::uint64_t value) { writeBytesLImpl(value); }
    void writeL(std::int32_t value) { writeBytesLImpl(value); }
    void writeL(std::uint32_t value) { writeBytesLImpl(value); }
    void writeL(std::int16_t value) { writeBytesLImpl(value); }
    void writeL(std::uint16_t value) { writeBytesLImpl(value); }

    [[nodiscard]] std::int32_t readExInt();
    [[nodiscard]] std::int32_t readExUInt();
    void writeExInt(std::int32_t value);
    void writeExUInt(std::int32_t value);
    [[nodiscard]] std::size_t tell() const noexcept { return _pointer; }
    void tell(std::size_t position) { pointer(position); }

private:
    enum class Growth {
        Single,
        Raw,
        Array,
        Integer,
    };

    template <typename T>
    [[nodiscard]] T readInteger(bool bigEndian) {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        checkReadable(sizeof(T));
        using U = std::make_unsigned_t<T>;
        U value = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            const Byte byte = readUByte();
            if (bigEndian) {
                value = static_cast<U>((value << 8) | byte);
            } else {
                value |= static_cast<U>(byte) << (index * 8);
            }
        }
        return std::bit_cast<T>(value);
    }

    template <typename T>
    void writeInteger(T value, bool bigEndian) {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        if (sizeof(T) > std::numeric_limits<std::size_t>::max() - _pointer) {
            throw std::length_error("ByteIO size overflow");
        }
        using U = std::make_unsigned_t<T>;
        const U bits = std::bit_cast<U>(value);
        ensureWritable(_pointer + sizeof(T), Growth::Integer);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            const auto shift = bigEndian ? (sizeof(T) - index - 1) * 8
                                         : index * 8;
            _bytes[_pointer++] =
                static_cast<Byte>((bits >> shift) & static_cast<U>(0xFF));
        }
        refreshOwnedViews();
    }

    void ensureWritable(std::size_t end, Growth growth);
    void refreshOwnedViews() noexcept;
    void checkReadable(std::size_t amount) const;

    std::shared_ptr<Bytes> _owned;
    MutableByteView _bytes{};
    ByteView _constBytes{};
    bool _readOnly = false;
    std::size_t _pointer = 0;
};

template <typename InputStream>
void read(InputStream& stream, Byte& b) {
    std::array<Byte, 1> buffer{};
    const auto got = stream.rawRead(MutableByteView(buffer));
    if (got.size() != 1) {
        throw std::out_of_range("read over");
    }
    b = buffer[0];
}

template <typename OutputStream, typename T>
void write(OutputStream& stream, T value) {
    static_assert((std::is_integral_v<T> || std::is_enum_v<T>) &&
                      sizeof(T) == sizeof(Byte),
                  "binary::write accepts only one-byte values");
    const Byte b = static_cast<Byte>(value);
    stream.rawWrite(ByteView(&b, 1));
}

namespace detail {

template <typename InputStream>
[[nodiscard]] Byte readStreamByte(InputStream& input) {
    Byte value{};
    // Dのinp.read(b)はメンバーreadがあれば使い、なければUFCS経
    // 由でrawReadを使う自由関数を呼ぶ。 どちらの型の受け渡しにも対応する。
    if constexpr (requires { input.read(value); }) input.read(value);
    else binary::read(input, value);
    return value;
}

template <typename InputStream, typename T>
[[nodiscard]] T readEndian(InputStream& input, bool bigEndian) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(sizeof(T) == 2 || sizeof(T) == 4);
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const auto byte = readStreamByte(input);
        if (bigEndian) {
            value = static_cast<U>((value << 8) | byte);
        } else {
            value |= static_cast<U>(byte) << (index * 8);
        }
    }
    return std::bit_cast<T>(value);
}

template <typename OutputStream, typename T>
void writeEndian(OutputStream& output, T value, bool bigEndian) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(sizeof(T) == 2 || sizeof(T) == 4);
    using U = std::make_unsigned_t<T>;
    const U bits = std::bit_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const auto shift = bigEndian ? (sizeof(T) - index - 1) * 8
                                     : index * 8;
        binary::write(output, static_cast<Byte>((bits >> shift) & 0xFF));
    }
}

}  // namespace detail

template <typename InputStream>
[[nodiscard]] std::int32_t readIntB(InputStream& input) {
    return detail::readEndian<InputStream, std::int32_t>(input, true);
}

template <typename InputStream>
[[nodiscard]] std::uint32_t readUIntB(InputStream& input) {
    return detail::readEndian<InputStream, std::uint32_t>(input, true);
}

template <typename InputStream>
[[nodiscard]] std::int32_t readIntL(InputStream& input) {
    return detail::readEndian<InputStream, std::int32_t>(input, false);
}

template <typename InputStream>
[[nodiscard]] std::uint32_t readUIntL(InputStream& input) {
    return detail::readEndian<InputStream, std::uint32_t>(input, false);
}

template <typename InputStream>
[[nodiscard]] std::int16_t readShortB(InputStream& input) {
    return detail::readEndian<InputStream, std::int16_t>(input, true);
}

template <typename InputStream>
[[nodiscard]] std::int16_t readShortL(InputStream& input) {
    return detail::readEndian<InputStream, std::int16_t>(input, false);
}

template <typename InputStream>
[[nodiscard]] std::uint16_t readUShortB(InputStream& input) {
    return detail::readEndian<InputStream, std::uint16_t>(input, true);
}

template <typename InputStream>
[[nodiscard]] std::uint16_t readUShortL(InputStream& input) {
    return detail::readEndian<InputStream, std::uint16_t>(input, false);
}

template <typename OutputStream>
void writeShortB(OutputStream& output, std::int16_t value) {
    detail::writeEndian(output, value, true);
}

template <typename OutputStream>
void writeUShortB(OutputStream& output, std::uint16_t value) {
    detail::writeEndian(output, value, true);
}

template <typename OutputStream>
void writeShortL(OutputStream& output, std::int16_t value) {
    detail::writeEndian(output, value, false);
}

template <typename OutputStream>
void writeUShortL(OutputStream& output, std::uint16_t value) {
    detail::writeEndian(output, value, false);
}

template <typename OutputStream>
void writeIntB(OutputStream& output, std::int32_t value) {
    detail::writeEndian(output, value, true);
}

template <typename OutputStream>
void writeUIntB(OutputStream& output, std::uint32_t value) {
    detail::writeEndian(output, value, true);
}

template <typename OutputStream>
void writeIntL(OutputStream& output, std::int32_t value) {
    detail::writeEndian(output, value, false);
}

template <typename OutputStream>
void writeUIntL(OutputStream& output, std::uint32_t value) {
    detail::writeEndian(output, value, false);
}

template <typename InputStream>
[[nodiscard]] std::int32_t readExInt(InputStream& input) {
    return readExInt(true, ByteReader([&input] {
        return detail::readStreamByte(input);
    }));
}

template <typename InputStream>
[[nodiscard]] std::int32_t readExUInt(InputStream& input) {
    return readExInt(false, ByteReader([&input] {
        return detail::readStreamByte(input);
    }));
}

template <typename OutputStream>
void writeExInt(OutputStream& output, std::int32_t value) {
    writeExInt(value, true, ByteWriter([&output](Byte b) {
        binary::write(output, b);
    }));
}

template <typename OutputStream>
void writeExUInt(OutputStream& output, std::int32_t value) {
    writeExInt(value, false, ByteWriter([&output](Byte b) {
        binary::write(output, b);
    }));
}

void seekCur(std::FILE& file, std::int64_t offset);
void seekSet(std::FILE& file, std::int64_t offset);
void seekEnd(std::FILE& file, std::int64_t offset);

void writeExInt(std::int32_t value, MutableByteView bytes, std::size_t& offset);
void writeExUInt(std::int32_t value, MutableByteView bytes, std::size_t& offset);
[[nodiscard]] std::int32_t readExInt(ByteView bytes, std::size_t& offset);
[[nodiscard]] std::int32_t readExUInt(ByteView bytes, std::size_t& offset);

class RawFile {
public:
    // 開いたファイルではDのstdioの失敗時の扱いに合わせ、seek/flush/writeln
    // はCの戻り値を無視し、 tellは番兵値を返す。fflush(nullptr)
    // はCの全出力フラッシュを維持する。 その他の閉じたハンドルへの操作
    // は、nullのFILE*で未定義動作を起こさず明示的に例外とする。
    RawFile(StringView file, StringView mode);
    ~RawFile();

    RawFile(const RawFile&) = delete;
    RawFile& operator=(const RawFile&) = delete;

    void close();
    [[nodiscard]] std::uint64_t size();
    [[nodiscard]] MutableByteView rawRead(MutableByteView buffer);
    void seek(std::int64_t offset, int origin = SEEK_SET);
    void seekCur(std::int64_t offset);
    void seekSet(std::int64_t offset);
    void seekEnd(std::int64_t offset);
    void flush();
    [[nodiscard]] std::int64_t tell();
    void writeln(StringView s);

private:
    std::FILE* _fp = nullptr;
    std::array<char, BUFSIZ> _buf{};
};

[[nodiscard]] ::cwxeditor::lib::Owner<RawFile> rawFile(StringView file,
                                                        StringView mode);

}  // namespace cwxeditor::cwx::binary
