#include "cw/lib/imagecodec.hpp"
#include "cw/lib/simd_image.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <csetjmp>
#include <cstdio>
extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}

namespace cw::lib::imagecodec {
namespace {

using Byte = std::uint8_t;

RGBAImage decode_jpeg_bytes(std::span<const Byte> data, DecodeProfile profile);

String sdl_message(const String& prefix)
{
    const char* detail = SDL_GetError();
    if (detail && *detail) return prefix + ": " + detail;
    return prefix;
}

void checked_product(Integer width, Integer height, std::size_t channels)
{
    if (width <= 0 || height <= 0) throw DataError("image dimensions must be positive");
    const auto w = static_cast<std::uint64_t>(width);
    const auto h = static_cast<std::uint64_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / channels ||
        h > std::numeric_limits<std::size_t>::max() / (w * channels))
        throw DataError("image dimensions overflow");
}

class Cursor {
public:
    explicit Cursor(std::span<const Byte> data) : m_data(data) {}

    std::size_t position() const noexcept { return m_pos; }
    std::size_t remaining() const noexcept { return m_data.size() - m_pos; }

    void require(std::size_t count) const
    {
        if (count > remaining()) throw DataError("short image data");
    }

    Byte u8()
    {
        require(1);
        return m_data[m_pos++];
    }

    std::uint16_t le16()
    {
        require(2);
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(m_data[m_pos]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(m_data[m_pos + 1]) << 8));
        m_pos += 2;
        return value;
    }

    std::uint16_t be16()
    {
        require(2);
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(m_data[m_pos]) << 8) |
            static_cast<std::uint16_t>(m_data[m_pos + 1]));
        m_pos += 2;
        return value;
    }

    std::uint32_t le32()
    {
        require(4);
        const auto value = static_cast<std::uint32_t>(m_data[m_pos]) |
            (static_cast<std::uint32_t>(m_data[m_pos + 1]) << 8) |
            (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 16) |
            (static_cast<std::uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return value;
    }

    std::uint32_t be32()
    {
        require(4);
        const auto value = (static_cast<std::uint32_t>(m_data[m_pos]) << 24) |
            (static_cast<std::uint32_t>(m_data[m_pos + 1]) << 16) |
            (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 8) |
            static_cast<std::uint32_t>(m_data[m_pos + 3]);
        m_pos += 4;
        return value;
    }

    void skip(std::size_t count)
    {
        require(count);
        m_pos += count;
    }

    std::span<const Byte> bytes(std::size_t count)
    {
        require(count);
        const auto result = m_data.subspan(m_pos, count);
        m_pos += count;
        return result;
    }

private:
    std::span<const Byte> m_data;
    std::size_t m_pos{};
};

bool starts_with(std::span<const Byte> data, std::initializer_list<Byte> prefix)
{
    if (data.size() < prefix.size()) return false;
    std::size_t index = 0;
    for (const auto byte : prefix) if (data[index++] != byte) return false;
    return true;
}

bool type_is(std::span<const Byte> data, std::size_t offset, const char* type)
{
    return offset + 4 <= data.size() &&
        data[offset] == static_cast<Byte>(type[0]) &&
        data[offset + 1] == static_cast<Byte>(type[1]) &&
        data[offset + 2] == static_cast<Byte>(type[2]) &&
        data[offset + 3] == static_cast<Byte>(type[3]);
}

Integer le24(const Byte* value)
{
    return static_cast<Integer>(value[0]) |
        (static_cast<Integer>(value[1]) << 8) |
        (static_cast<Integer>(value[2]) << 16);
}

std::uint16_t le16_at(std::span<const Byte> data, std::size_t offset)
{
    if (offset > data.size() || data.size() - offset < 2) throw DataError("short image integer");
    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t le32_at(std::span<const Byte> data, std::size_t offset)
{
    if (offset > data.size() || data.size() - offset < 4) throw DataError("short image integer");
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::int32_t le_i32_at(std::span<const Byte> data, std::size_t offset)
{
    return static_cast<std::int32_t>(le32_at(data, offset));
}

String bytes_string(std::span<const Byte> bytes)
{
    return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

Integer duration_from_fraction(std::uint16_t numerator, std::uint16_t denominator)
{
    if (denominator == 0) denominator = 100;
    const auto milliseconds = (static_cast<std::uint64_t>(numerator) * 1000u +
        denominator / 2u) / denominator;
    return static_cast<Integer>(std::min<std::uint64_t>(
        milliseconds, static_cast<std::uint64_t>(std::numeric_limits<Integer>::max())));
}

std::uint32_t crc32(std::span<const Byte> data);

Info parse_png(std::span<const Byte> data)
{
    if (data.size() < 8) throw DataError("short PNG signature");
    Info info;
    info.format = Format::PNG;
    std::size_t offset = 8;
    bool have_header = false;
    bool have_image_data = false;
    std::size_t animated_frames = 0;
    while (offset < data.size()) {
        if (data.size() - offset < 12) throw DataError("short PNG chunk");
        const auto length = static_cast<std::size_t>(
            (static_cast<std::uint32_t>(data[offset]) << 24) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
            static_cast<std::uint32_t>(data[offset + 3]));
        if (length > data.size() - offset - 12) throw DataError("short PNG chunk payload");
        const auto payload = data.subspan(offset + 8, length);
        const auto stored_crc = (static_cast<std::uint32_t>(data[offset + 8 + length]) << 24) |
            (static_cast<std::uint32_t>(data[offset + 9 + length]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 10 + length]) << 8) | data[offset + 11 + length];
        if (crc32(data.subspan(offset + 4, length + 4)) != stored_crc) throw DataError("PNG chunk CRC mismatch");
        if (type_is(data, offset + 4, "IHDR")) {
            if (length != 13 || have_header || offset != 8) throw DataError("invalid PNG IHDR");
            const auto width = static_cast<Integer>(
                (static_cast<std::uint32_t>(payload[0]) << 24) |
                (static_cast<std::uint32_t>(payload[1]) << 16) |
                (static_cast<std::uint32_t>(payload[2]) << 8) |
                static_cast<std::uint32_t>(payload[3]));
            const auto height = static_cast<Integer>(
                (static_cast<std::uint32_t>(payload[4]) << 24) |
                (static_cast<std::uint32_t>(payload[5]) << 16) |
                (static_cast<std::uint32_t>(payload[6]) << 8) |
                static_cast<std::uint32_t>(payload[7]));
            checked_product(width, height, 4);
            info.width = width;
            info.height = height;
            const auto depth = payload[8];
            switch (payload[9]) {
            case 0: info.mode = Mode::L; break;
            case 2: info.mode = Mode::RGB; break;
            case 3: info.mode = Mode::Palette; break;
            case 4: info.mode = Mode::LA; info.has_alpha = true; break;
            case 6: info.mode = Mode::RGBA; info.has_alpha = true; break;
            default: throw UnsupportedFormatError("unsupported PNG color type");
            }
            const bool valid_depth = (payload[9] == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                (payload[9] == 2 && (depth == 8 || depth == 16)) ||
                (payload[9] == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                ((payload[9] == 4 || payload[9] == 6) && (depth == 8 || depth == 16));
            if (!valid_depth || payload[10] != 0 || payload[11] != 0 || payload[12] > 1) throw DataError("invalid PNG IHDR fields");
            have_header = true;
        } else if (type_is(data, offset + 4, "tRNS")) {
            info.has_transparency = true;
            info.has_alpha = true;
        } else if (type_is(data, offset + 4, "acTL")) {
            if (length < 8) throw DataError("short PNG acTL");
            const auto count = (static_cast<std::uint32_t>(payload[0]) << 24) |
                (static_cast<std::uint32_t>(payload[1]) << 16) |
                (static_cast<std::uint32_t>(payload[2]) << 8) |
                static_cast<std::uint32_t>(payload[3]);
            if (count == 0) throw DataError("PNG animation has no frames");
            info.frame_count = count;
            info.loop_count = (static_cast<Integer>(payload[4]) << 24) |
                (static_cast<Integer>(payload[5]) << 16) |
                (static_cast<Integer>(payload[6]) << 8) |
                static_cast<Integer>(payload[7]);
            info.loop_defined = true;
        } else if (type_is(data, offset + 4, "IDAT")) {
            have_image_data = true;
        } else if (type_is(data, offset + 4, "fcTL")) {
            if (length < 26) throw DataError("short PNG fcTL");
            if (animated_frames == 0 && have_image_data) info.separate_default_image = true;
            const auto numerator = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(payload[20]) << 8) | payload[21]);
            const auto denominator = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(payload[22]) << 8) | payload[23]);
            info.durations_ms.push_back(duration_from_fraction(numerator, denominator));
            ++animated_frames;
        } else if (type_is(data, offset + 4, "eXIf")) {
            info.metadata["exif"] = bytes_string(payload);
        } else if (type_is(data, offset + 4, "iCCP")) {
            info.metadata["icc"] = bytes_string(payload);
        } else if (type_is(data, offset + 4, "iTXt") || type_is(data, offset + 4, "tEXt") ||
                   type_is(data, offset + 4, "zTXt")) {
            info.metadata["text." + std::to_string(info.metadata.size())] = bytes_string(payload);
        } else if (type_is(data, offset + 4, "pHYs") && payload.size() == 9) {
            const auto x = (static_cast<std::uint32_t>(payload[0]) << 24) | (static_cast<std::uint32_t>(payload[1]) << 16) |
                           (static_cast<std::uint32_t>(payload[2]) << 8) | payload[3];
            const auto y = (static_cast<std::uint32_t>(payload[4]) << 24) | (static_cast<std::uint32_t>(payload[5]) << 16) |
                           (static_cast<std::uint32_t>(payload[6]) << 8) | payload[7];
            info.metadata["pixels_per_unit_x"] = std::to_string(x);
            info.metadata["pixels_per_unit_y"] = std::to_string(y);
            info.metadata["pixel_unit"] = std::to_string(payload[8]);
        } else if (type_is(data, offset + 4, "IEND")) {
            if (length != 0) throw DataError("invalid PNG IEND");
            offset += 12 + length;
            break;
        }
        offset += 12 + length;
    }
    if (!have_header) throw DataError("PNG has no IHDR");
    if (info.frame_count == 1 && animated_frames > 1) info.frame_count = animated_frames;
    if (info.frame_count == 0) info.frame_count = 1;
    if (info.loop_defined && info.separate_default_image) {
        // Pillow と同様、seek(0) で既定画像を公開し、既定の待機時間を 100ms とする。
        if (info.frame_count == (std::numeric_limits<std::size_t>::max)())
            throw DataError("PNG frame count overflow");
        ++info.frame_count;
        info.durations_ms.insert(info.durations_ms.begin(), 100);
    } else {
        info.separate_default_image = false;
    }
    info.durations_ms.resize(info.frame_count, 100);
    return info;
}

std::vector<Byte> gif_subblocks(Cursor& cursor)
{
    std::vector<Byte> result;
    for (;;) {
        const auto length = cursor.u8();
        if (length == 0) break;
        if (length > cursor.remaining()) throw DataError("short GIF sub-block");
        const auto block = cursor.bytes(length);
        if (block.size() > std::numeric_limits<std::size_t>::max() - result.size())
            throw DataError("GIF metadata overflow");
        result.insert(result.end(), block.begin(), block.end());
    }
    return result;
}

Info parse_gif(std::span<const Byte> data)
{
    if (data.size() < 13) throw DataError("short GIF header");
    Info info;
    info.format = Format::GIF;
    info.mode = Mode::Palette;
    info.frame_count = 0;
    info.width = static_cast<Integer>(data[6] | (data[7] << 8));
    info.height = static_cast<Integer>(data[8] | (data[9] << 8));
    checked_product(info.width, info.height, 4);
    Cursor cursor(data.subspan(13));
    const auto packed = data[10];
    info.metadata["background_index"] = std::to_string(data[11]);
    info.metadata["pixel_aspect"] = std::to_string(data[12]);
    info.metadata["global_palette_entries"] = (packed & 0x80) ? std::to_string(2u << (packed & 7u)) : "0";
    if (packed & 0x80) cursor.skip(static_cast<std::size_t>(3u << ((packed & 7u) + 1u)));
    Integer pending_duration = 100;
    for (;;) {
        const auto block = cursor.u8();
        if (block == 0x3b) break;
        if (block == 0x21) {
            const auto label = cursor.u8();
            if (label == 0xf9) {
                if (cursor.u8() != 4) throw DataError("invalid GIF graphic control extension");
                const auto flags = cursor.u8();
                const auto delay = cursor.le16();
                cursor.skip(1);
                if (cursor.u8() != 0) throw DataError("invalid GIF graphic control terminator");
                pending_duration = static_cast<Integer>(delay) * 10;
                info.metadata["frame." + std::to_string(info.frame_count) + ".disposal"] = std::to_string((flags >> 2) & 7);
                if (flags & 1) {
                    info.has_transparency = true;
                    info.has_alpha = true;
                }
            } else if (label == 0xff) {
                const auto length = cursor.u8();
                const auto application = cursor.bytes(length);
                const auto payload = gif_subblocks(cursor);
                if (application.size() >= 11 &&
                    (std::memcmp(application.data(), "NETSCAPE2.0", 11) == 0 ||
                     std::memcmp(application.data(), "ANIMEXTS1.0", 11) == 0) &&
                    payload.size() >= 3 && payload[0] == 1) {
                    info.loop_count = payload[1] | (static_cast<Integer>(payload[2]) << 8);
                    info.loop_defined = true;
                }
            } else {
                (void)gif_subblocks(cursor);
            }
        } else if (block == 0x2c) {
            cursor.require(9);
            const auto local_packed = data.subspan(13)[cursor.position() + 8];
            cursor.skip(9);
            if (local_packed & 0x80)
                cursor.skip(static_cast<std::size_t>(3u << ((local_packed & 7u) + 1u)));
            (void)cursor.u8();
            (void)gif_subblocks(cursor);
            ++info.frame_count;
            info.durations_ms.push_back(pending_duration);
            pending_duration = 100;
        } else {
            throw DataError("invalid GIF block");
        }
    }
    if (info.frame_count == 0) throw DataError("GIF has no image frames");
    info.durations_ms.resize(info.frame_count, 100);
    return info;
}

bool is_jpeg_sof(Byte marker)
{
    return marker == 0xc0 || marker == 0xc1 || marker == 0xc2 || marker == 0xc3 ||
        marker == 0xc5 || marker == 0xc6 || marker == 0xc7 || marker == 0xc9 ||
        marker == 0xca || marker == 0xcb || marker == 0xcd || marker == 0xce ||
        marker == 0xcf;
}

Info parse_jpeg(std::span<const Byte> data)
{
    if (data.size() < 2) throw DataError("short JPEG header");
    Info info;
    info.format = Format::JPEG;
    info.mode = Mode::RGB;
    info.frame_count = 1;
    std::size_t offset = 2;
    while (offset < data.size()) {
        if (data[offset++] != 0xff) throw DataError("invalid JPEG marker");
        while (offset < data.size() && data[offset] == 0xff) ++offset;
        if (offset >= data.size()) throw DataError("short JPEG marker");
        const auto marker = data[offset++];
        if (marker == 0xd9) break;
        if (marker == 0xda) break;
        if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (offset + 2 > data.size()) throw DataError("short JPEG segment");
        const auto length = static_cast<std::size_t>((data[offset] << 8) | data[offset + 1]);
        if (length < 2 || length > data.size() - offset) throw DataError("short JPEG segment payload");
        const auto segment = data.subspan(offset + 2, length - 2);
        if (marker == 0xe0 && segment.size() >= 5 && std::memcmp(segment.data(), "JFIF\0", 5) == 0)
            info.metadata["jfif"] = bytes_string(segment);
        else if (marker == 0xe1 && segment.size() >= 6 && std::memcmp(segment.data(), "Exif\0\0", 6) == 0)
            info.metadata["exif"] = bytes_string(segment.subspan(6));
        else if (marker == 0xe2 && segment.size() >= 12 && std::memcmp(segment.data(), "ICC_PROFILE\0", 12) == 0)
            info.metadata["icc." + std::to_string(segment.size() >= 13 ? segment[12] : 0)] = bytes_string(segment);
        if (is_jpeg_sof(marker)) {
            if (length < 8) throw DataError("short JPEG frame header");
            info.height = static_cast<Integer>((data[offset + 3] << 8) | data[offset + 4]);
            info.width = static_cast<Integer>((data[offset + 5] << 8) | data[offset + 6]);
            const auto components = data[offset + 7];
            if (components == 1) info.mode = Mode::L;
            else if (components == 3) info.mode = Mode::RGB;
            else if (components == 4) info.mode = Mode::CMYK;
            else info.mode = Mode::Unknown;
            checked_product(info.width, info.height, 4);
            info.durations_ms = {100};
            return info;
        }
        offset += length;
    }
    throw DataError("JPEG has no frame header");
}

struct BmpHeader {
    Integer width{};
    Integer height{};
    bool top_down{};
    std::uint16_t bits{};
    std::uint32_t compression{};
    std::size_t pixel_offset{};
    std::size_t dib_size{};
    std::uint32_t colors_used{};
    std::array<std::uint32_t, 4> masks{};
    // BITMAPCOREHEADER（OS/2 1.x）のパレット項目は三バイトとする。
    bool core{};
};

// BMP の許容範囲は、仕様より寛容な Python の pygame
// から SDL_LoadBMP への読み込みに合わせる。 厳密な読み
// 取りとの差異は、いずれも SDL の動作に従う。 ・biPlanes
// は無視する。 ・12 バイトの BITMAPCOREHEADER、
// 2 ビット、15 ビット画像を受け付ける。 ・bfOffBits は
// シーク位置として扱い、ヘッダー・パレット内でも許す。 ・パレットは
// bfOffBits と独立して、ヘッダー直後から読む。 ・BI_BITFIELDS
// の三マスクがゼロなら、16 ビットは 565、32 ビットは 0x00FF0000/0x0000FF00/0x000000FF
// を使う。 ・RLE は負の高さを無視し、8
// ビット以下の画像で RLE4/RLE8 を
// 許し、画像外のピクセルは失敗させず捨てる。
BmpHeader parse_bmp_header(std::span<const Byte> data)
{
    if (data.size() < 26 || !starts_with(data, {'B', 'M'})) throw DataError("short BMP header");
    BmpHeader header;
    header.pixel_offset = le32_at(data, 10);
    header.dib_size = le32_at(data, 14);
    std::int32_t width = 0, signed_height = 0;
    if (header.dib_size == 12) {
        header.core = true;
        // SDL と同様、CORE ヘッダーの幅・高さは符号なし 16 ビットとして読む。
        width = le16_at(data, 18);
        signed_height = le16_at(data, 20);
        header.bits = le16_at(data, 24);
        header.compression = 0;
        header.colors_used = 0;
    } else {
        if (header.dib_size < 40 || data.size() < 54 || header.dib_size > data.size() - 14)
            throw UnsupportedFormatError("unsupported BMP DIB header");
        width = le_i32_at(data, 18);
        signed_height = le_i32_at(data, 22);
        header.bits = le16_at(data, 28);
        header.compression = le32_at(data, 30);
        header.colors_used = le32_at(data, 46);
    }
    if (width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min()) throw DataError("invalid BMP dimensions");
    header.width = width;
    header.height = signed_height < 0 ? -static_cast<Integer>(signed_height) : signed_height;
    header.top_down = signed_height < 0;
    if (!(header.bits == 1 || header.bits == 2 || header.bits == 4 || header.bits == 8 ||
          header.bits == 15 || header.bits == 16 || header.bits == 24 || header.bits == 32))
        throw UnsupportedFormatError("unsupported BMP bit depth");
    if (!(header.compression == 0 || header.compression == 1 || header.compression == 2 || header.compression == 3 || header.compression == 6))
        throw UnsupportedFormatError("unsupported BMP compression");
    if (((header.compression == 1 || header.compression == 2) && header.bits > 8) ||
        ((header.compression == 3 || header.compression == 6) && !(header.bits == 15 || header.bits == 16 || header.bits == 32)))
        throw DataError("BMP compression and bit depth disagree");
    // SDL と同様、RLE は上から下への指定を無視し、下から上へ行を書き込む。
    if (header.compression == 1 || header.compression == 2) header.top_down = false;
    if (header.pixel_offset > data.size()) throw DataError("invalid BMP pixel offset");
    checked_product(header.width, header.height, 4);
    const std::uint32_t low_mask16 = 0xffffU;
    if (header.compression == 3 || header.compression == 6) {
        const std::size_t mask_offset = header.dib_size >= 52 ? 54 : 14 + header.dib_size;
        const auto mask_at = [&](std::size_t offset) {
            if (offset > data.size() || data.size() - offset < 4) throw DataError("short BMP bitfield masks");
            return le32_at(data, offset);
        };
        header.masks[0] = mask_at(mask_offset);
        header.masks[1] = mask_at(mask_offset + 4);
        header.masks[2] = mask_at(mask_offset + 8);
        if (header.dib_size >= 56 || header.compression == 6) header.masks[3] = mask_at(mask_offset + 12);
        if (!header.masks[0] && !header.masks[1] && !header.masks[2]) {
            // マスクがゼロの場合は、SDL_MasksToPixelFormatEnum の既定値を使う。
            if (header.bits == 32) header.masks = {0x00ff0000U, 0x0000ff00U, 0x000000ffU, header.masks[3]};
            else header.masks = {0xf800U, 0x07e0U, 0x001fU, header.masks[3]};
        }
        if (!header.masks[0] || !header.masks[1] || !header.masks[2]) throw DataError("BMP bitfield masks are incomplete");
    } else if (header.bits == 15 || header.bits == 16) {
        header.masks = {0x7c00U, 0x03e0U, 0x001fU, 0};
    } else if (header.bits == 32) {
        header.masks = {0x00ff0000U, 0x0000ff00U, 0x000000ffU, 0};
        // CardWirth/Python と同様、BI_RGB の予約バイトは、ど
        // れかのピクセルがゼロでなければアルファとして使う。 全体がゼロなら不透明と
        // する。全ホストで、メタデータ検査とピクセルのデコードに同じマスクを使う。
        const auto stride = static_cast<std::size_t>(header.width) * 4U;
        const auto rows = static_cast<std::size_t>(header.height);
        if (rows > (data.size() - header.pixel_offset) / stride)
            throw DataError("short BMP pixel array");
        for (std::size_t offset = header.pixel_offset + 3; offset < header.pixel_offset + rows * stride; offset += 4)
            if (data[offset] != 0) { header.masks[3] = 0xff000000U; break; }
    }
    const std::uint32_t valid_mask = header.bits == 32 ? 0xffffffffU :
        (header.bits == 15 || header.bits == 16) ? low_mask16 :
        ((std::uint32_t{1} << static_cast<unsigned>(header.bits)) - 1U);
    for (const auto mask : header.masks) if (mask & ~valid_mask) throw DataError("BMP channel mask exceeds bit depth");
    for (std::size_t i = 0; i < header.masks.size(); ++i) {
        for (std::size_t j = i + 1; j < header.masks.size(); ++j)
            if (header.masks[i] & header.masks[j]) throw DataError("BMP channel masks overlap");
    }
    return header;
}

Info parse_bmp(std::span<const Byte> data)
{
    const auto header = parse_bmp_header(data);
    Info info;
    info.format = Format::BMP;
    info.width = header.width;
    info.height = header.height;
    info.mode = header.bits <= 8 ? Mode::Palette : header.masks[3] ? Mode::RGBA : Mode::RGB;
    info.has_alpha = header.masks[3] != 0;
    info.has_transparency = info.has_alpha;
    info.durations_ms = {100};
    info.metadata["bits_per_pixel"] = std::to_string(header.bits);
    info.metadata["compression"] = std::to_string(header.compression);
    info.metadata["top_down"] = header.top_down ? "1" : "0";
    info.metadata["colors_used"] = std::to_string(header.colors_used);
    if (!header.core) {
        info.metadata["x_pixels_per_meter"] = std::to_string(le_i32_at(data, 38));
        info.metadata["y_pixels_per_meter"] = std::to_string(le_i32_at(data, 42));
    }
    for (std::size_t i = 0; i < header.masks.size(); ++i) info.metadata["mask." + std::to_string(i)] = std::to_string(header.masks[i]);
    return info;
}

Byte masked_channel(std::uint32_t pixel, std::uint32_t mask, Byte absent)
{
    if (!mask) return absent;
    unsigned shift = 0;
    while (shift < 32U && ((mask >> shift) & 1U) == 0U) ++shift;
    const auto field = mask >> shift;
    if ((field & (field + 1U)) != 0U) throw DataError("BMP channel mask is not contiguous");
    const auto value = (pixel & mask) >> shift;
    return static_cast<Byte>((static_cast<std::uint64_t>(value) * 255U + field / 2U) / field);
}

RGBAImage decode_bmp_rgba(std::span<const Byte> data)
{
    const auto header = parse_bmp_header(data);
    RGBAImage result{header.width, header.height, {}};
    result.source_pixel_bits = header.bits <= 8 ? 8 : header.bits == 15 ? 16 : header.bits;
    result.source_has_pixel_alpha = header.masks[3] != 0;
    const auto width = static_cast<std::size_t>(header.width);
    const auto height = static_cast<std::size_t>(header.height);
    result.pixels.assign(width * height * 4U, 0);
    std::vector<std::array<Byte, 4>> palette;
    if (header.bits <= 8) {
        const auto maximum = std::size_t{1} << static_cast<unsigned>(header.bits);
        // Python の pygame/SDL と同様、4 ビット BMP の 17
        // 色など過大な biClrUsed は、ファイルを拒否せずビット深度の最
        // 大値に収める。 余分なパレット項目は無視し、ピクセルは bfOffBits
        // から読む。 同じ画像を読み込めるよう、意図的にこの許容動作を使う。
        const auto count = header.colors_used
            ? std::min(static_cast<std::size_t>(header.colors_used), maximum) : maximum;
        if (count == 0) throw DataError("invalid BMP palette size");
        // SDL と同様、パレットは bfOffBits と無関係にヘッダー直後
        // から読む。 bfOffBits がパレット内を指す場合も同じとする。
        const auto entry = header.core ? 3U : 4U;
        const auto palette_offset = 14U + header.dib_size;
        if (palette_offset > data.size() || count > (data.size() - palette_offset) / entry) throw DataError("short BMP palette");
        palette.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto at = palette_offset + i * entry;
            palette.push_back({data[at + 2], data[at + 1], data[at], 255});
        }
    }
    const auto target_offset = [&](std::size_t x, std::size_t storage_y) {
        if (x >= width || storage_y >= height) throw DataError("BMP pixel coordinate out of range");
        const auto y = header.top_down ? storage_y : height - 1U - storage_y;
        return (y * width + x) * 4U;
    };
    const auto put_rgba = [&](std::size_t x, std::size_t y, const std::array<Byte, 4>& rgba) {
        const auto out = target_offset(x, y);
        std::copy(rgba.begin(), rgba.end(), result.pixels.begin() + static_cast<std::ptrdiff_t>(out));
    };
    const auto put_index = [&](std::size_t x, std::size_t y, std::size_t index) {
        if (index >= palette.size()) throw DataError("BMP palette index out of range");
        put_rgba(x, y, palette[index]);
    };
    if (!palette.empty()) {
        for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) put_index(x, y, 0);
    }

    if (header.compression == 1 || header.compression == 2) {
        // SDL の readRlePixels と同様、4 バイト
        // 境界の行間隔を持つ 8 ビットのインデックス画像へ下から
        // 書く。 行末を超える連続データは次のメモリー行へ続き、画
        // 像外への書き込みは捨てる。 差分・改行による移動位置も制
        // 限せず、画像終端記号なしで入力が尽きた場合だけ失敗する。
        const bool rle8 = header.compression == 1;
        const auto pitch = (width + 3U) & ~std::size_t{3};
        std::vector<Byte> indices(pitch * height, 0);
        const auto total = static_cast<std::int64_t>(indices.size());
        std::int64_t row = static_cast<std::int64_t>(height) - 1, ofs = 0;
        const auto copy_pixel = [&](Byte value) {
            const auto spot = row * static_cast<std::int64_t>(pitch) + ofs++;
            if (spot >= 0 && spot < total) indices[static_cast<std::size_t>(spot)] = value;
        };
        Cursor cursor(data.subspan(header.pixel_offset));
        bool ended = false;
        while (!ended) {
            if (!cursor.remaining()) throw DataError("BMP RLE stream has no end marker");
            auto count = cursor.u8();
            if (count) {
                const auto value = cursor.u8();
                if (rle8) { do copy_pixel(value); while (--count); }
                else {
                    for (;;) {
                        copy_pixel(value >> 4U); if (!--count) break;
                        copy_pixel(value & 0x0fU); if (!--count) break;
                    }
                }
                continue;
            }
            count = cursor.u8();
            if (count == 0) { ofs = 0; --row; }
            else if (count == 1) ended = true;
            else if (count == 2) { ofs += cursor.u8(); row -= cursor.u8(); }
            else {
                const bool pad = rle8 ? (count & 1U) != 0 : (((count + 1U) >> 1U) & 1U) != 0;
                if (rle8) { do copy_pixel(cursor.u8()); while (--count); }
                else {
                    for (;;) {
                        const auto value = cursor.u8();
                        copy_pixel(value >> 4U); if (!--count) break;
                        copy_pixel(value & 0x0fU); if (!--count) break;
                    }
                }
                if (pad) cursor.skip(1);
            }
        }
        // SDL と同様、ここではパレットの検証をせず、未使用のパレット枠を白として扱う。
        const std::array<Byte, 4> white{255, 255, 255, 255};
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const auto index = indices[y * pitch + x];
                const auto out = (y * width + x) * 4U;
                const auto& rgba = index < palette.size() ? palette[index] : white;
                std::copy(rgba.begin(), rgba.end(), result.pixels.begin() + static_cast<std::ptrdiff_t>(out));
            }
        }
        return result;
    }

    const auto storage_bits = header.bits == 15 ? 16U : header.bits;
    const auto row_bits = static_cast<std::uint64_t>(width) * storage_bits;
    const auto row_stride64 = ((row_bits + 31U) / 32U) * 4U;
    if (row_stride64 > std::numeric_limits<std::size_t>::max()) throw DataError("BMP row size overflow");
    const auto row_stride = static_cast<std::size_t>(row_stride64);
    if (height > (data.size() - header.pixel_offset) / row_stride) throw DataError("short BMP pixel array");
    for (std::size_t y = 0; y < height; ++y) {
        const auto row = data.subspan(header.pixel_offset + y * row_stride, row_stride);
        for (std::size_t x = 0; x < width; ++x) {
            if (header.bits <= 8) {
                const auto bit = x * header.bits;
                const auto byte = row[bit / 8U];
                const auto shift = 8U - header.bits - (bit % 8U);
                put_index(x, y, (byte >> shift) & ((1U << header.bits) - 1U));
            } else if (header.bits == 24) {
                const auto at = x * 3U;
                put_rgba(x, y, {row[at + 2], row[at + 1], row[at], 255});
            } else {
                const auto bytes = storage_bits / 8U;
                const auto at = x * bytes;
                std::uint32_t pixel = row[at] | (static_cast<std::uint32_t>(row[at + 1]) << 8);
                if (bytes == 4) pixel |= static_cast<std::uint32_t>(row[at + 2]) << 16 | static_cast<std::uint32_t>(row[at + 3]) << 24;
                put_rgba(x, y, {masked_channel(pixel, header.masks[0], 0), masked_channel(pixel, header.masks[1], 0),
                                masked_channel(pixel, header.masks[2], 0), masked_channel(pixel, header.masks[3], 255)});
            }
        }
    }
    return result;
}

Info parse_webp(std::span<const Byte> data)
{
    if (data.size() < 20) throw DataError("short WebP header");
    Info info;
    info.format = Format::WebP;
    info.mode = Mode::RGB;
    const auto riff_size = static_cast<std::size_t>(
        data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
    if (riff_size < 4 || riff_size > data.size() - 8) throw DataError("invalid WebP RIFF size");
    const auto end = 8 + riff_size;
    std::size_t offset = 12;
    bool animation = false;
    while (offset < end) {
        if (end - offset < 8) throw DataError("short WebP chunk");
        const auto length = static_cast<std::size_t>(
            data[offset + 4] | (data[offset + 5] << 8) |
            (data[offset + 6] << 16) | (data[offset + 7] << 24));
        if (length > std::numeric_limits<std::size_t>::max() - (length & 1u))
            throw DataError("WebP chunk size overflow");
        const auto padded = length + (length & 1u);
        if (padded > end - offset - 8) throw DataError("short WebP chunk payload");
        const auto payload = data.subspan(offset + 8, length);
        if (type_is(data, offset, "VP8X")) {
            if (length < 10) throw DataError("short WebP VP8X");
            info.has_alpha = (payload[0] & 0x10) != 0;
            animation = (payload[0] & 0x02) != 0;
            info.width = 1 + le24(payload.data() + 4);
            info.height = 1 + le24(payload.data() + 7);
        } else if (type_is(data, offset, "ANIM")) {
            if (length < 6) throw DataError("short WebP ANIM");
            info.loop_count = payload[4] | (static_cast<Integer>(payload[5]) << 8);
            info.loop_defined = true;
            animation = true;
            info.metadata["animation_background_bgra"] = bytes_string(payload.first(4));
        } else if (type_is(data, offset, "ANMF")) {
            if (length < 16) throw DataError("short WebP ANMF");
            animation = true;
            info.durations_ms.push_back(
                static_cast<Integer>(payload[12] | (payload[13] << 8) | (payload[14] << 16)));
            info.metadata["frame." + std::to_string(info.durations_ms.size() - 1) + ".flags"] = std::to_string(payload[15] & 3U);
        } else if (type_is(data, offset, "VP8L")) {
            if (length < 5 || payload[0] != 0x2f) throw DataError("invalid WebP lossless header");
            if (info.width == 0) {
                info.width = 1 + (payload[1] | ((payload[2] & 0x3f) << 8));
                info.height = 1 + ((payload[2] >> 6) | (payload[3] << 2) | ((payload[4] & 0xf) << 10));
            }
            info.has_alpha = info.has_alpha || (payload[4] & 0x10U) != 0;
        } else if (type_is(data, offset, "VP8 ")) {
            if (length < 10) throw DataError("short WebP lossy header");
            if (info.width == 0 && payload[3] == 0x9d && payload[4] == 0x01 && payload[5] == 0x2a) {
                info.width = static_cast<Integer>(payload[6] | (payload[7] << 8)) & 0x3fff;
                info.height = static_cast<Integer>(payload[8] | (payload[9] << 8)) & 0x3fff;
            }
        } else if (type_is(data, offset, "EXIF")) info.metadata["exif"] = bytes_string(payload);
        else if (type_is(data, offset, "ICCP")) info.metadata["icc"] = bytes_string(payload);
        else if (type_is(data, offset, "XMP ")) info.metadata["xmp"] = bytes_string(payload);
        offset += 8 + padded;
    }
    if (info.width <= 0 || info.height <= 0) throw DataError("WebP has no dimensions");
    checked_product(info.width, info.height, 4);
    info.mode = info.has_alpha ? Mode::RGBA : Mode::RGB;
    info.has_transparency = info.has_alpha;
    info.frame_count = animation ? info.durations_ms.size() : 1;
    if (animation && info.frame_count == 0) throw DataError("WebP animation has no frames");
    info.durations_ms.resize(info.frame_count, 100);
    return info;
}

std::vector<Byte> read_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw DataError("cannot stat image file: " + path.generic_string());
    if (size > std::numeric_limits<std::size_t>::max()) throw DataError("image file is too large");
    std::vector<Byte> data(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw DataError("cannot open image file: " + path.generic_string());
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw DataError("image file is too large for stream");
    if (!data.empty()) stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream && !stream.eof()) throw DataError("cannot read image file: " + path.generic_string());
    return data;
}

class SurfaceHandle {
public:
    explicit SurfaceHandle(SDL_Surface* surface = nullptr) : m_surface(surface) {}
    ~SurfaceHandle() { if (m_surface) SDL_DestroySurface(m_surface); }
    SurfaceHandle(const SurfaceHandle&) = delete;
    SurfaceHandle& operator=(const SurfaceHandle&) = delete;
    SurfaceHandle(SurfaceHandle&& other) noexcept : m_surface(other.m_surface) { other.m_surface = nullptr; }
    SDL_Surface* get() const noexcept { return m_surface; }
private:
    SDL_Surface* m_surface;
};

class AnimationHandle {
public:
    explicit AnimationHandle(IMG_Animation* animation = nullptr) : m_animation(animation) {}
    ~AnimationHandle() { if (m_animation) IMG_FreeAnimation(m_animation); }
    AnimationHandle(const AnimationHandle&) = delete;
    AnimationHandle& operator=(const AnimationHandle&) = delete;
    IMG_Animation* get() const noexcept { return m_animation; }
private:
    IMG_Animation* m_animation;
};

class IOHandle {
public:
    explicit IOHandle(SDL_IOStream* stream = nullptr) : m_stream(stream) {}
    ~IOHandle() { if (m_stream) SDL_CloseIO(m_stream); }
    IOHandle(const IOHandle&) = delete;
    IOHandle& operator=(const IOHandle&) = delete;
    SDL_IOStream* get() const noexcept { return m_stream; }
    SDL_IOStream* release() noexcept { auto result = m_stream; m_stream = nullptr; return result; }
private:
    SDL_IOStream* m_stream;
};

SurfaceHandle load_surface(std::span<const Byte> data, Format format = Format::Unknown)
{
    if (data.empty()) throw DataError("cannot decode empty image data");
    IOHandle stream(SDL_IOFromConstMem(data.data(), data.size()));
    if (!stream.get()) throw DecodeError(sdl_message("cannot create image stream"));
    // TGA には識別子がない。 pygame が未知の拡張子や拡張子な
    // しで保存した場合も、調査した形式を SDL_image へ渡す。
    SurfaceHandle surface(format == Format::TGA
        ? IMG_LoadTyped_IO(stream.get(), true, "TGA")
        : IMG_Load_IO(stream.get(), true));
    stream.release();
    if (!surface.get()) throw DecodeError(sdl_message("SDL_image decode failed"));
    // SDL_image の低ビット灰色パレットは ncolors で割るため、白の端点
    // に届かない。 PNG の灰色値は Python のデコーダーと同じく 0～(2^depth-1)
    // に広がるため、 RGBA 変換前に共通デコーダーの境界で補正する。
    if (data.size() >= 26 && type_is(data, 12, "IHDR") && data[25] == 0 && data[24] <= 8) {
        if (auto* palette = SDL_GetSurfacePalette(surface.get())) {
            const unsigned count = 1U << data[24];
            if (static_cast<unsigned>(palette->ncolors) == count) {
                std::vector<SDL_Color> colors(palette->colors, palette->colors + palette->ncolors);
                for (unsigned i = 0; i < count; ++i) {
                    const auto gray = static_cast<Byte>(count > 1 ? i * 255U / (count - 1U) : 0);
                    colors[i].r = colors[i].g = colors[i].b = gray;
                }
                if (!SDL_SetPaletteColors(palette, colors.data(), 0, palette->ncolors))
                    throw DecodeError(sdl_message("cannot normalize PNG gray palette"));
            }
        }
    }
    return surface;
}

RGBAImage surface_to_rgba(SDL_Surface* source)
{
    if (!source || source->w <= 0 || source->h <= 0)
        throw DecodeError("decoded surface has invalid dimensions");
    checked_product(source->w, source->h, 4);
    // デコーダーは RGB24・インデックス・他のパック形式を返す場合があり、有
    // 効な行間隔が幅×4 より小さくてもよい。 SDL_ConvertSurface
    // が要求した RGBA32 へ変換した後にだけ、行間隔を検証する。
    SurfaceHandle converted(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32));
    if (!converted.get()) throw DecodeError(sdl_message("cannot convert surface to RGBA"));
    if (!SDL_LockSurface(converted.get()))
        throw DecodeError(sdl_message("cannot lock decoded surface"));
    RGBAImage result;
    result.width = converted.get()->w;
    result.height = converted.get()->h;
    const auto* format = SDL_GetPixelFormatDetails(source->format);
    if (!format) throw DecodeError(sdl_message("cannot inspect decoded pixel format"));
    result.source_pixel_bits = format->bits_per_pixel;
    result.source_has_pixel_alpha = format->Amask != 0;
    Uint32 key = 0;
    if (SDL_GetSurfaceColorKey(source, &key)) {
        Uint8 r = 0, g = 0, b = 0;
        SDL_GetRGB(key, format, SDL_GetSurfacePalette(source), &r, &g, &b);
        result.source_has_colorkey = true;
        result.source_colorkey = {r, g, b};
    }
    checked_product(result.width, result.height, 4);
    const auto output_pitch = static_cast<std::size_t>(result.width) * 4;
    if (converted.get()->pitch < 0 ||
        static_cast<std::size_t>(converted.get()->pitch) < output_pitch)
    {
        SDL_UnlockSurface(converted.get());
        throw DecodeError("converted surface has invalid pitch");
    }
    result.pixels.resize(output_pitch * static_cast<std::size_t>(result.height));
    for (Integer y = 0; y < result.height; ++y) {
        const auto* row = static_cast<const Byte*>(converted.get()->pixels) +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(converted.get()->pitch);
        std::memcpy(result.pixels.data() + static_cast<std::size_t>(y) * output_pitch, row, output_pitch);
    }
    SDL_UnlockSurface(converted.get());
    return result;
}

Animation decode_animation_backend(std::span<const Byte> data, Info info)
{
    if (info.frame_count <= 1) {
        const auto duration = info.durations_ms.empty() ? 100 : info.durations_ms.front();
        if (info.format == Format::BMP)
            return Animation(std::move(info), {Frame{decode_bmp_rgba(data), duration}});
        if (info.format == Format::JPEG)
            return Animation(std::move(info), {Frame{decode_jpeg_bytes(data, DecodeProfile::CanonicalRGBA), duration}});
        auto surface = load_surface(data, info.format);
        return Animation(std::move(info), {Frame{surface_to_rgba(surface.get()), duration}});
    }
    IOHandle stream(SDL_IOFromConstMem(data.data(), data.size()));
    if (!stream.get()) throw DecodeError(sdl_message("cannot create animation stream"));
    AnimationHandle animation(IMG_LoadAnimation_IO(stream.get(), true));
    stream.release();
    if (!animation.get()) throw DecodeError(sdl_message("SDL_image animation decode failed"));
    if (animation.get()->count <= 0 || !animation.get()->frames)
        throw DecodeError("SDL_image returned an empty animation");
    const auto count = static_cast<std::size_t>(animation.get()->count);
    std::vector<Frame> frames;
    const std::size_t default_frames = info.separate_default_image ? 1 : 0;
    frames.reserve(count + default_frames);
    info.frame_count = count + default_frames;
    info.durations_ms.resize(info.frame_count, 100);
    if (info.separate_default_image) {
        // SDL_image 3.4.4 は fcTL のフレームだけを出力する。 Pillow
        // の追加の IDAT 位置は別に保持し、アニメーションのキャンバス初期値には使わない。
        frames.push_back(Frame{surface_to_rgba(load_surface(data).get()), 100});
        info.durations_ms[0] = 100;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!animation.get()->frames[index]) throw DecodeError("SDL_image returned a null animation frame");
        const auto duration = animation.get()->delays
            ? static_cast<Integer>(animation.get()->delays[index]) : info.durations_ms[index + default_frames];
        info.durations_ms[index + default_frames] = duration;
        frames.push_back(Frame{surface_to_rgba(animation.get()->frames[index]), std::max<Integer>(duration, 16)});
    }
    return Animation(std::move(info), std::move(frames));
}

void append_u32_be(std::vector<Byte>& output, std::uint32_t value)
{
    output.push_back(static_cast<Byte>(value >> 24));
    output.push_back(static_cast<Byte>(value >> 16));
    output.push_back(static_cast<Byte>(value >> 8));
    output.push_back(static_cast<Byte>(value));
}

std::uint32_t crc32(std::span<const Byte> data)
{
    std::uint32_t crc = 0xffffffffu;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & static_cast<std::uint32_t>(-
                static_cast<std::int32_t>(crc & 1u)));
    }
    return ~crc;
}

void append_chunk(std::vector<Byte>& output, const char (&type)[5], std::span<const Byte> payload)
{
    if (payload.size() > std::numeric_limits<std::uint32_t>::max())
        throw EncodeError("image chunk is too large");
    append_u32_be(output, static_cast<std::uint32_t>(payload.size()));
    const auto type_offset = output.size();
    for (std::size_t index = 0; index < 4; ++index)
        output.push_back(static_cast<Byte>(type[index]));
    output.insert(output.end(), payload.begin(), payload.end());
    const auto crc_size = output.size() - type_offset;
    append_u32_be(output, crc32(std::span<const Byte>(output.data() + type_offset, crc_size)));
}

struct EncodeLayout {
    std::size_t row_bytes;
    std::size_t pixel_bytes;
};

EncodeLayout validate_encode_image(const RGBAImage& image)
{
    if (image.width <= 0 || image.height <= 0)
        throw EncodeError("RGBA image dimensions must be positive");
    const auto width = static_cast<std::uint64_t>(image.width);
    const auto height = static_cast<std::uint64_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / 4u)
        throw EncodeError("RGBA image row size overflow");
    const auto row_bytes = static_cast<std::size_t>(width * 4u);
    if (height > std::numeric_limits<std::size_t>::max() / row_bytes)
        throw EncodeError("RGBA image size overflow");
    const auto pixel_bytes = row_bytes * static_cast<std::size_t>(height);
    if (image.pixels.size() != pixel_bytes)
        throw EncodeError("RGBA image buffer size does not match dimensions");
    return {row_bytes, pixel_bytes};
}

std::vector<Byte> encode_png_bytes(const RGBAImage& image, EncodeLayout layout)
{
    if (image.width > std::numeric_limits<std::uint32_t>::max() ||
        image.height > std::numeric_limits<std::uint32_t>::max())
        throw EncodeError("PNG dimensions exceed the format limit");
    const auto height = static_cast<std::size_t>(image.height);
    if (height > std::numeric_limits<std::size_t>::max() / (layout.row_bytes + 1u))
        throw EncodeError("PNG scanline size overflow");
    const auto raw_size = height * (layout.row_bytes + 1u);
    std::vector<Byte> raw;
    raw.reserve(raw_size);
    for (std::size_t row = 0; row < height; ++row) {
        raw.push_back(0);  // PNG のフィルターは「なし」とする。
        const auto offset = row * layout.row_bytes;
        raw.insert(raw.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(offset),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(offset + layout.row_bytes));
    }
    const auto blocks = raw.size() / 65535u + (raw.size() % 65535u != 0 ? 1u : 0u);
    if (raw.size() > std::numeric_limits<std::size_t>::max() - 6u ||
        blocks > (std::numeric_limits<std::size_t>::max() - raw.size() - 6u) / 5u)
        throw EncodeError("PNG compressed size overflow");
    std::vector<Byte> compressed;
    compressed.reserve(6u + raw.size() + blocks * 5u);
    compressed.push_back(0x78);  // zlib は無圧縮・最速の方式を使う。
    compressed.push_back(0x01);
    for (std::size_t offset = 0; offset < raw.size();) {
        const auto length = std::min<std::size_t>(65535u, raw.size() - offset);
        const auto final = offset + length == raw.size();
        compressed.push_back(static_cast<Byte>(final ? 1 : 0));
        compressed.push_back(static_cast<Byte>(length));
        compressed.push_back(static_cast<Byte>(length >> 8));
        const auto inverse = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(length));
        compressed.push_back(static_cast<Byte>(inverse));
        compressed.push_back(static_cast<Byte>(inverse >> 8));
        compressed.insert(compressed.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
            raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }
    std::uint32_t adler_a = 1;
    std::uint32_t adler_b = 0;
    for (const auto byte : raw) {
        adler_a = (adler_a + byte) % 65521u;
        adler_b = (adler_b + adler_a) % 65521u;
    }
    append_u32_be(compressed, (adler_b << 16) | adler_a);

    if (compressed.size() > std::numeric_limits<std::size_t>::max() - 45u)
        throw EncodeError("PNG output size overflow");
    std::vector<Byte> result;
    result.reserve(8u + 25u + compressed.size() + 12u);
    result.insert(result.end(), {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a});
    std::vector<Byte> header;
    header.reserve(13);
    append_u32_be(header, static_cast<std::uint32_t>(image.width));
    append_u32_be(header, static_cast<std::uint32_t>(image.height));
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(result, "IHDR", header);
    for (std::size_t offset = 0; offset < compressed.size();) {
        const auto length = std::min<std::size_t>(std::numeric_limits<std::uint32_t>::max(),
            compressed.size() - offset);
        append_chunk(result, "IDAT", std::span<const Byte>(compressed.data() + offset, length));
        offset += length;
    }
    append_chunk(result, "IEND", {});
    return result;
}

std::vector<Byte> encode_bmp_bytes(const RGBAImage& image, EncodeLayout layout)
{
    if (image.width > std::numeric_limits<std::int32_t>::max() ||
        image.height > std::numeric_limits<std::int32_t>::max())
        throw EncodeError("BMP dimensions exceed the format limit");
    constexpr std::size_t dib_size = 108u;  // BITMAPV4HEADER にアルファマスクを格納する。
    constexpr std::size_t header_size = 14u + dib_size;
    if (layout.pixel_bytes > std::numeric_limits<std::uint32_t>::max() - header_size)
        throw EncodeError("BMP image size exceeds the format limit");
    const auto file_size = header_size + layout.pixel_bytes;
    std::vector<Byte> result(file_size, 0);
    result[0] = 'B';
    result[1] = 'M';
    result[2] = static_cast<Byte>(file_size);
    result[3] = static_cast<Byte>(file_size >> 8);
    result[4] = static_cast<Byte>(file_size >> 16);
    result[5] = static_cast<Byte>(file_size >> 24);
    result[10] = static_cast<Byte>(header_size);
    result[11] = static_cast<Byte>(header_size >> 8);
    result[12] = static_cast<Byte>(header_size >> 16);
    result[13] = static_cast<Byte>(header_size >> 24);
    result[14] = static_cast<Byte>(dib_size);
    const auto write_le32 = [&result](std::size_t offset, std::uint32_t value) {
        result[offset] = static_cast<Byte>(value);
        result[offset + 1] = static_cast<Byte>(value >> 8);
        result[offset + 2] = static_cast<Byte>(value >> 16);
        result[offset + 3] = static_cast<Byte>(value >> 24);
    };
    write_le32(18, static_cast<std::uint32_t>(image.width));
    write_le32(22, static_cast<std::uint32_t>(-image.height));
    write_le32(26, 1u | (32u << 16));
    write_le32(30, 3u);  // BI_BITFIELDS を使い、RGBA のチャンネルマスクを維持する。
    write_le32(34, static_cast<std::uint32_t>(layout.pixel_bytes));
    write_le32(54, 0x00ff0000u);
    write_le32(58, 0x0000ff00u);
    write_le32(62, 0x000000ffu);
    write_le32(66, 0xff000000u);
    write_le32(70, 0x73524742u);  // LCS_sRGB
    for (std::size_t row = 0; row < static_cast<std::size_t>(image.height); ++row) {
        const auto source_offset = row * layout.row_bytes;
        const auto target_offset = header_size + source_offset;
        for (std::size_t x = 0; x < static_cast<std::size_t>(image.width); ++x) {
            const auto source = source_offset + x * 4u;
            const auto target = target_offset + x * 4u;
            result[target] = image.pixels[source + 2];
            result[target + 1] = image.pixels[source + 1];
            result[target + 2] = image.pixels[source];
            result[target + 3] = image.pixels[source + 3];
        }
    }
    return result;
}

std::vector<Byte> encode(const RGBAImage& image, bool png)
{
    const auto layout = validate_encode_image(image);
    return png ? encode_png_bytes(image, layout) : encode_bmp_bytes(image, layout);
}

Info parse_tga(std::span<const Byte> data)
{
    if (data.size() < 18) throw DataError("short TGA header");
    const auto type = data[2];
    const bool indexed = type == 1 || type == 9;
    const bool rgb = type == 2 || type == 10;
    const bool gray = type == 3 || type == 11;
    const auto depth = data[16];
    const auto colors = le16_at(data, 5);
    if (data[1] > 1 || (!indexed && !rgb && !gray) || (data[17] & 0xd0) ||
        (indexed && (data[1] != 1 || depth != 8 || colors == 0 || colors > 256)) ||
        (gray && depth != 8) ||
        (rgb && depth != 15 && depth != 16 && depth != 24 && depth != 32) ||
        (data[1] && data[7] != 15 && data[7] != 16 && data[7] != 24 && data[7] != 32))
        throw UnsupportedFormatError("unsupported TGA layout");
    Info info;
    info.format = Format::TGA;
    info.width = le16_at(data, 12);
    info.height = le16_at(data, 14);
    checked_product(info.width, info.height, 4);
    info.mode = indexed ? Mode::Palette : gray ? Mode::L : depth == 32 ? Mode::RGBA : Mode::RGB;
    info.has_alpha = rgb && depth == 32;
    info.has_transparency = info.has_alpha || (indexed && data[7] == 32);
    const auto prefix = 18u + static_cast<std::size_t>(data[0]) +
        (data[1] ? static_cast<std::size_t>(colors) * ((data[7] + 7u) / 8u) : 0u);
    if (prefix >= data.size()) throw DataError("short TGA image data");
    return info;
}

std::vector<Byte> encode_tga_bytes(const RGBAImage& image, bool has_pixel_alpha)
{
    const auto layout = validate_encode_image(image);
    // Py の C エンコーダーは寸法を uint16 へ切り詰めるが、
    // ここでは表現不能なヘッダーを書かず、形式の範囲外の値を拒否する。
    if (image.width > 65535 || image.height > 65535)
        throw EncodeError("TGA dimensions exceed the format limit");
    const auto width = static_cast<std::size_t>(image.width);
    const auto bpp = has_pixel_alpha ? 4u : 3u;
    std::vector<Byte> output(18, 0);
    output[2] = 10;  // pygame 1.9.6 の SaveTGA(..., 1) と同じ、RLE のフルカラー形式を使う。
    output[12] = static_cast<Byte>(image.width);
    output[13] = static_cast<Byte>(image.width >> 8);
    output[14] = static_cast<Byte>(image.height);
    output[15] = static_cast<Byte>(image.height >> 8);
    output[16] = static_cast<Byte>(bpp * 8u);
    output[17] = static_cast<Byte>(0x20u | (has_pixel_alpha ? 8u : 0u));
    std::vector<Byte> row(width * bpp);
    for (std::size_t y = 0; y < static_cast<std::size_t>(image.height); ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto source = y * layout.row_bytes + x * 4u;
            const auto target = x * bpp;
            row[target] = image.pixels[source + 2];
            row[target + 1] = image.pixels[source + 1];
            row[target + 2] = image.pixels[source];
            if (has_pixel_alpha) row[target + 3] = image.pixels[source + 3];
        }
        // pygame のパケット境界を維持し、最後の一ピクセルの
        // 反復パケットと、各行内の 128 ピクセル上限も含める。
        std::size_t x = 0, raw = 0;
        while (x < width) {
            const auto first = x++;
            while (x < width && x - first < 128u &&
                   std::memcmp(row.data() + first * bpp, row.data() + x * bpp, bpp) == 0)
                ++x;
            if ((x - first - 1u) * bpp >= 2u || x == width) {
                while (raw < first) {
                    const auto count = std::min<std::size_t>(128u, first - raw);
                    output.push_back(static_cast<Byte>(count - 1u));
                    output.insert(output.end(), row.begin() + raw * bpp, row.begin() + (raw + count) * bpp);
                    raw += count;
                }
                output.push_back(static_cast<Byte>(0x7fu + x - first));
                output.insert(output.end(), row.begin() + first * bpp, row.begin() + (first + 1u) * bpp);
                raw = x;
            }
        }
    }
    return output;
}

struct JpegEncodeError {
    jpeg_error_mgr manager{};
    std::jmp_buf jump{};
    char message[JMSG_LENGTH_MAX]{};
};

void jpeg_encode_error(j_common_ptr codec)
{
    auto* error = reinterpret_cast<JpegEncodeError*>(codec->err);
    codec->err->format_message(codec, error->message);
    std::longjmp(error->jump, 1);
}


struct JpegDecodeState {
    jpeg_decompress_struct codec{};
    JpegEncodeError error{};
    std::vector<Byte> samples;
    RGBAImage image;
    ~JpegDecodeState() { if (codec.mem) jpeg_destroy_decompress(&codec); }
};

RGBAImage decode_jpeg_bytes(std::span<const Byte> data, DecodeProfile profile)
{
    if (data.size() > (std::numeric_limits<unsigned long>::max)())
        throw DataError("JPEG input exceeds the codec address range");
    // Py の SDL_image と Pillow は libjpeg-turbo
    // の整数低速 DCT と高品質の色差拡大を使う。 STB の独立したデコーダー
    // では RGB が変わるため、全プラットフォーム・製品でこの一つの経路を使う。
    // 状態はヒープに所有し、libjpeg の致命的エラーの longjmp でもロ
    // ーカル C++ の破棄を飛ばしたり、出力バッファーの所有者を失ったりしない。
    auto state = std::make_unique<JpegDecodeState>();
    state->codec.err = jpeg_std_error(&state->error.manager);
    state->error.manager.error_exit = jpeg_encode_error;
    if (setjmp(state->error.jump))
        throw DataError(String("JPEG decode failed: ") + state->error.message);
    jpeg_create_decompress(&state->codec);
    jpeg_mem_src(&state->codec, data.data(), static_cast<unsigned long>(data.size()));
    if (jpeg_read_header(&state->codec, TRUE) != JPEG_HEADER_OK)
        throw DataError("JPEG has no complete image header");
    const bool cmyk = state->codec.jpeg_color_space == JCS_CMYK ||
                      state->codec.jpeg_color_space == JCS_YCCK;
    state->codec.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    state->codec.dct_method = JDCT_ISLOW;
    state->codec.do_fancy_upsampling = TRUE;
    state->codec.do_block_smoothing = TRUE;
    jpeg_start_decompress(&state->codec);
    auto& image = state->image;
    image.width = state->codec.output_width;
    image.height = state->codec.output_height;
    checked_product(image.width, image.height, 4);
    const auto channels = cmyk ? 4u : 3u;
    if (state->codec.output_components != channels)
        throw DataError("JPEG output has an unexpected channel count");
    const auto count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    state->samples.resize(count * channels);
    image.pixels.resize(count * 4u);
    const auto stride = static_cast<std::size_t>(image.width) * channels;
    while (state->codec.output_scanline < state->codec.output_height) {
        JSAMPROW row = state->samples.data() + state->codec.output_scanline * stride;
        if (jpeg_read_scanlines(&state->codec, &row, 1) != 1)
            throw DataError("JPEG scanline decoding suspended unexpectedly");
    }
    jpeg_finish_decompress(&state->codec);
    const bool pygame_cmyk = cmyk && profile == DecodeProfile::PygameSurface;
    image.source_pixel_bits = pygame_cmyk ? 32 : 24;
    image.source_has_pixel_alpha = pygame_cmyk;
    // Py の通常の JPEG 読み込みは、SDL1 の CMYK Surface の解
    // 釈、つまり Y,M,C,K で K がアルファ位置に入る形式を維持する。 Pillow/wx
    // の呼び出し側には実際の RGB 変換が必要となる。 同じデコーダーの呼び出し
    // 用途の違いであり、OS 専用ローダーや標準 RGBA 全体の変更にはしない。
    for (std::size_t i = 0; i < count; ++i) {
        const auto* src = state->samples.data() + i * channels;
        auto* dst = image.pixels.data() + i * 4u;
        if (!cmyk) {
            std::copy_n(src, 3, dst); dst[3] = 255;
        } else if (pygame_cmyk) {
            dst[0]=src[2]; dst[1]=src[1]; dst[2]=src[0]; dst[3]=src[3];
        } else {
            const bool inverted = state->codec.saw_Adobe_marker != 0;
            const unsigned black = inverted ? src[3] : 255u-src[3];
            for (unsigned c=0; c<3; ++c) {
                const unsigned colour = inverted ? src[c] : 255u-src[c];
                dst[c] = static_cast<Byte>((colour * black + 127u) / 255u);
            }
            dst[3]=255;
        }
    }
    return std::move(image);
}

struct JpegEncodeState {
    jpeg_compress_struct codec{};
    JpegEncodeError error{};
    jpeg_destination_mgr destination{};
    std::vector<Byte> output = std::vector<Byte>(4096);
    std::size_t size{};
    ~JpegEncodeState()
    {
        jpeg_destroy_compress(&codec);
    }
};

void jpeg_destination_init(j_compress_ptr codec)
{
    auto* state = static_cast<JpegEncodeState*>(codec->client_data);
    state->destination.next_output_byte = state->output.data();
    state->destination.free_in_buffer = state->output.size();
}

boolean jpeg_destination_grow(j_compress_ptr codec)
{
    auto* state = static_cast<JpegEncodeState*>(codec->client_data);
    const auto previous = state->output.size();
    bool failed = previous > state->output.max_size() / 2u;
    if (!failed) {
        try { state->output.resize(previous * 2u); }
        catch (const std::exception&) { failed = true; }
    }
    if (failed) {
        // libjpeg の longjmp より前に C++ の catch を抜ける。 C の呼
        // び出し階層を C++ 例外が越えず、ヒープ所有の vector を後処理できる状態に保つ。
        codec->err->msg_code = JERR_OUT_OF_MEMORY;
        codec->err->msg_parm.i[0] = 11;
        codec->err->error_exit(reinterpret_cast<j_common_ptr>(codec));
        return FALSE;
    }
    state->destination.next_output_byte = state->output.data() + previous;
    state->destination.free_in_buffer = previous;
    return TRUE;
}

void jpeg_destination_finish(j_compress_ptr codec)
{
    auto* state = static_cast<JpegEncodeState*>(codec->client_data);
    state->size = state->output.size() - state->destination.free_in_buffer;
}

std::vector<Byte> encode_jpeg_bytes(const RGBAImage& image, bool has_pixel_alpha)
{
    const auto layout = validate_encode_image(image);
    if (image.width > JPEG_MAX_DIMENSION || image.height > JPEG_MAX_DIMENSION)
        throw EncodeError("JPEG dimensions exceed the format limit");
    std::vector<Byte> rgb(layout.pixel_bytes / 4u * 3u);
    // imageext.c と同様、SDL1 の黒い RGB24 描画面へ転送する。 SDL1 の汎用 ALPHA_BLEND
    // は +255 の後に 256 で割り、pygame の RGBA 転送とは異なる。
    simd_image::rgba_to_rgb_black(image.pixels.data(), rgb.data(),
                                 layout.pixel_bytes / 4u, has_pixel_alpha);
    // libjpeg はエラーで longjmp するため、変更可能な状態をすべ
    // てヒープに置き、C++ の所有者は setjmp より前に構築する。 C
    // の呼び出しからのジャンプでデストラクターを飛ばさない。 既定のプロセス終了
    // ではなく共通 Storage の例外規則を使い、エラー後は書き込まない。
    auto state = std::make_unique<JpegEncodeState>();
    state->codec.err = jpeg_std_error(&state->error.manager);
    state->error.manager.error_exit = jpeg_encode_error;
    if (setjmp(state->error.jump))
        throw EncodeError(String("JPEG encode failed: ") + state->error.message);
    jpeg_create_compress(&state->codec);
    // IJG の jpeg_mem_dest は、拡張したバッファーを正常完了時
    // にだけ公開する。 致命的エラー後は呼び出し側の古いポインターが既に解放済み
    // の場合があるため、 出力先を継続して所有し、エラー後処理で二重解放しない。
    state->codec.client_data = state.get();
    state->destination.init_destination = jpeg_destination_init;
    state->destination.empty_output_buffer = jpeg_destination_grow;
    state->destination.term_destination = jpeg_destination_finish;
    state->codec.dest = &state->destination;
    state->codec.image_width = static_cast<JDIMENSION>(image.width);
    state->codec.image_height = static_cast<JDIMENSION>(image.height);
    state->codec.input_components = 3;
    state->codec.in_color_space = JCS_RGB;
    jpeg_set_defaults(&state->codec);
    // 参照元の固定版 Py の圧縮処理は、既定で通常の 2×2 の色差縮小を使う。 新しい IJG
    // 専用の do_fancy_downsampling 拡張は、その ABI には含まれない。
    jpeg_set_quality(&state->codec, 85, TRUE);
    jpeg_start_compress(&state->codec, TRUE);
    while (state->codec.next_scanline < state->codec.image_height) {
        auto* row = rgb.data() + static_cast<std::size_t>(state->codec.next_scanline) *
            static_cast<std::size_t>(image.width) * 3u;
        jpeg_write_scanlines(&state->codec, &row, 1);
    }
    jpeg_finish_compress(&state->codec);
    state->output.resize(state->size);
    return std::move(state->output);
}

}  // namespace

Info inspect(std::span<const std::uint8_t> data)
{
    if (data.empty()) throw DataError("image data is empty");
    if (starts_with(data, {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a}))
        return parse_png(data);
    if (starts_with(data, {'G', 'I', 'F', '8', '7', 'a'}) ||
        starts_with(data, {'G', 'I', 'F', '8', '9', 'a'}))
        return parse_gif(data);
    if (starts_with(data, {'R', 'I', 'F', 'F'})) {
        if (data.size() < 12) throw DataError("short RIFF header");
        if (!starts_with(data.subspan(8), {'W', 'E', 'B', 'P'}))
            throw UnsupportedFormatError("unsupported RIFF image format");
        return parse_webp(data);
    }
    if (starts_with(data, {'B', 'M'})) return parse_bmp(data);
    if (starts_with(data, {0xff, 0xd8})) return parse_jpeg(data);
    if (data.size() >= 18 && data[1] <= 1 &&
        (data[2] == 1 || data[2] == 2 || data[2] == 3 || data[2] == 9 || data[2] == 10 || data[2] == 11))
        return parse_tga(data);
    if (data.size() < 12) throw DataError("short image header");
    throw UnsupportedFormatError("unsupported image format");
}

Info inspect_file(const std::filesystem::path& path)
{
    const auto data = read_file(path);
    return inspect(data);
}

namespace {
void preserve_pygame_source_format(RGBAImage& image, const Info& info, std::span<const Byte> data)
{
    // SDL3 は PNG にパレットのアルファを使い、GIF のフレーム
    // を RGBA に合成する。 Py/SDL1 は convert()
    // の前に二値の透過色とピクセルのアルファを区別するため、 Pillow
    // 型・アニメーション用の正規のピクセルを変えず、その違いを維持する。
    if (info.format == Format::PNG) {
        std::span<const Byte> palette, transparency;
        for (std::size_t offset = 8; offset + 12 <= data.size();) {
            const auto length = Cursor(data.subspan(offset, 4)).be32();
            if (length > data.size() - offset - 12) break;
            const auto payload = data.subspan(offset + 8, length);
            if (type_is(data, offset + 4, "PLTE")) palette = payload;
            if (type_is(data, offset + 4, "tRNS")) transparency = payload;
            offset += length + 12;
        }
        image.source_has_colorkey = false;
        image.source_has_pixel_alpha = info.mode == Mode::RGBA || info.mode == Mode::LA;
        image.source_pixel_bits = info.mode == Mode::Palette || info.mode == Mode::L ? 8 : image.source_has_pixel_alpha ? 32 : 24;
        if (!transparency.empty()) {
            if (info.mode == Mode::Palette) {
                std::optional<std::size_t> index;
                bool partial = false;
                for (std::size_t i = 0; i < transparency.size(); ++i) {
                    if (transparency[i] == 0) {
                        if (index) partial = true;
                        index = i;
                    } else if (transparency[i] != 255) partial = true;
                }
                if (index && !partial && *index * 3 + 2 < palette.size()) {
                    image.source_has_colorkey = true;
                    image.source_colorkey = {palette[*index * 3], palette[*index * 3 + 1], palette[*index * 3 + 2]};
                } else if (partial || index) {
                    image.source_has_pixel_alpha = true;
                    image.source_pixel_bits = 32;
                }
            } else if (info.mode == Mode::RGB && transparency.size() >= 6) {
                image.source_has_colorkey = true;
                const unsigned byte = data[24] == 16 ? 0 : 1;
                image.source_colorkey = {transparency[byte], transparency[byte + 2], transparency[byte + 4]};
            } else if (info.mode == Mode::L && transparency.size() >= 2) {
                image.source_has_colorkey = true;
                const auto depth = data[24];
                const auto gray = depth == 16 ? static_cast<Integer>(transparency[0]) :
                    static_cast<Integer>(transparency[1]) * 255 / ((1U << depth) - 1);
                image.source_colorkey = {gray, gray, gray};
            }
        }
    } else if (info.format == Format::GIF) {
        image.source_pixel_bits = 8;
        image.source_has_pixel_alpha = false;
        image.source_has_colorkey = false;
        std::span<const Byte> palette;
        Cursor cursor(data.subspan(13));
        if (data[10] & 0x80) palette = cursor.bytes(static_cast<std::size_t>(3U << ((data[10] & 7U) + 1U)));
        std::optional<Byte> transparent;
        for (;;) {
            const auto block = cursor.u8();
            if (block == 0x21) {
                const auto label = cursor.u8();
                if (label == 0xf9) {
                    const auto length = cursor.u8();
                    const auto control = cursor.bytes(length);
                    transparent = control.size() >= 4 && (control[0] & 1)
                        ? std::optional<Byte>{control[3]} : std::nullopt;
                    cursor.skip(1);
                } else gif_subblocks(cursor);
            } else if (block == 0x2c) {
                const auto x = cursor.le16(), y = cursor.le16();
                const auto w = cursor.le16(), h = cursor.le16();
                image.source_frame = Rect{x, y, w, h};
                const auto packed = cursor.u8();
                if (packed & 0x80) palette = cursor.bytes(static_cast<std::size_t>(3U << ((packed & 7U) + 1U)));
                if (transparent && static_cast<std::size_t>(*transparent) * 3 + 2 < palette.size()) {
                    image.source_has_colorkey = true;
                    const auto i = static_cast<std::size_t>(*transparent) * 3;
                    image.source_colorkey = {palette[i], palette[i + 1], palette[i + 2]};
                }
                break;
            } else break;
        }
    }
}
}

RGBAImage decode_rgba(std::span<const std::uint8_t> data, std::size_t frame)
{
    return decode_rgba(data, frame, DecodeProfile::CanonicalRGBA);
}

RGBAImage decode_rgba(std::span<const std::uint8_t> data, std::size_t frame, DecodeProfile profile)
{
    const auto info = inspect(data);
    if (frame >= info.frame_count) throw DataError("image frame index out of range");
    if (info.format == Format::JPEG) return decode_jpeg_bytes(data, profile);
    if (info.format == Format::BMP) return decode_bmp_rgba(data);
    auto result = info.frame_count > 1 ? decode_animation_backend(data, info).frame(frame).image
                                       : surface_to_rgba(load_surface(data, info.format).get());
    if (frame == 0) preserve_pygame_source_format(result, info, data);
    return result;
}

RGBAImage decode_rgba_file(const std::filesystem::path& path, std::size_t frame)
{
    const auto data = read_file(path);
    return decode_rgba(data, frame);
}

Animation decode_animation(std::span<const std::uint8_t> data)
{
    return decode_animation_backend(data, inspect(data));
}

Animation decode_animation_file(const std::filesystem::path& path)
{
    const auto data = read_file(path);
    return decode_animation(data);
}

std::vector<std::uint8_t> encode_png(const RGBAImage& image)
{
    return encode(image, true);
}

std::vector<std::uint8_t> encode_bmp(const RGBAImage& image)
{
    return encode(image, false);
}

std::vector<std::uint8_t> encode_tga(const RGBAImage& image, bool has_pixel_alpha)
{
    return encode_tga_bytes(image, has_pixel_alpha);
}

std::vector<std::uint8_t> encode_jpeg(const RGBAImage& image, bool has_pixel_alpha)
{
    return encode_jpeg_bytes(image, has_pixel_alpha);
}

void encode_png_file(const RGBAImage& image, const std::filesystem::path& path)
{
    const auto data = encode_png(image);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw EncodeError("cannot open PNG output: " + path.generic_string());
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw EncodeError("PNG output is too large for stream");
    if (!data.empty()) stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw EncodeError("cannot write PNG output: " + path.generic_string());
}

void encode_bmp_file(const RGBAImage& image, const std::filesystem::path& path)
{
    const auto data = encode_bmp(image);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw EncodeError("cannot open BMP output: " + path.generic_string());
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw EncodeError("BMP output is too large for stream");
    if (!data.empty()) stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) throw EncodeError("cannot write BMP output: " + path.generic_string());
}

Animation::Animation(Info info, std::vector<Frame> frames)
    : m_info(std::move(info)), m_frames(std::move(frames))
{
    if (m_frames.empty()) throw DataError("animation has no frames");
    m_info.frame_count = m_frames.size();
    m_info.durations_ms.clear();
    for (const auto& frame : m_frames) m_info.durations_ms.push_back(frame.duration_ms);
}

const Frame& Animation::frame(std::size_t index) const
{
    if (index >= m_frames.size()) throw DataError("image frame index out of range");
    return m_frames[index];
}

const Frame& Animation::current() const
{
    return frame(m_current);
}

void Animation::seek(std::size_t index)
{
    if (index >= m_frames.size()) throw DataError("image frame index out of range");
    m_current = index;
}

RGBAImage Animation::convert_rgba(std::size_t index) const
{
    return frame(index).image;
}

Animation Animation::copy() const
{
    Animation result(m_info, m_frames);
    result.m_current = m_current;
    return result;
}

}  // namespace cw::lib::imagecodec
