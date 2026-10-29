#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/binary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <cstring>

namespace cwxeditor::cwx::imagesize {
namespace {

using Byte = std::uint8_t;
using ByteView = lib::ByteView;

[[nodiscard]] bool range(ByteView bytes, std::size_t offset,
                          std::size_t count) noexcept {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

[[nodiscard]] std::uint16_t readUShortL(ByteView bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1U] << 8U);
}

[[nodiscard]] std::uint16_t readUShortB(ByteView bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset] << 8U) |
           static_cast<std::uint16_t>(bytes[offset + 1U]);
}

[[nodiscard]] std::uint32_t readUIntL(ByteView bytes,
                                      std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint32_t readUIntB(ByteView bytes,
                                      std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::int32_t readIntL(ByteView bytes,
                                    std::size_t offset) noexcept {
    return std::bit_cast<std::int32_t>(readUIntL(bytes, offset));
}

void writeUIntL(lib::MutableByteView bytes, std::size_t offset,
                std::uint32_t value) noexcept {
    bytes[offset] = static_cast<Byte>(value);
    bytes[offset + 1U] = static_cast<Byte>(value >> 8U);
    bytes[offset + 2U] = static_cast<Byte>(value >> 16U);
    bytes[offset + 3U] = static_cast<Byte>(value >> 24U);
}

[[nodiscard]] lib::String lowerAscii(lib::String value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char byte) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(byte)));
    });
    return value;
}

[[nodiscard]] lib::String extensionOf(lib::StringView path) {
    return enumutils::lowerCase(utils::pathExtension(path));
}

[[nodiscard]] lib::Bytes readPrefix(const std::filesystem::path& file,
                                     std::size_t count) {
    (void)std::filesystem::file_size(file);
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::ios_base::failure("Cannot open image file");
    lib::Bytes bytes(count);
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            return {};
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
    }
    return bytes;
}

[[nodiscard]] bool imageSizeByExtension(lib::ByteView bytes,
                                         lib::StringView ext,
                                         std::uint32_t& x,
                                         std::uint32_t& y) noexcept;

// ファイルとメモリで異なるのはバイト取得だけ。JPEG/TIFF
// の検証とサイズの解釈は、下記の一つのパーサーで行う。
struct MemoryImageReader {
    ByteView bytes;
    [[nodiscard]] std::uint64_t size() const noexcept { return bytes.size(); }
    bool read(std::uint64_t offset, Byte* output, std::size_t count) const noexcept {
        if (offset > bytes.size() || count > bytes.size() - offset) return false;
        std::memcpy(output, bytes.data() + static_cast<std::size_t>(offset), count);
        return true;
    }
};
struct FileImageReader {
    std::ifstream input;
    std::uint64_t length{};
    explicit FileImageReader(const std::filesystem::path& path) : input(path, std::ios::binary) {
        length = std::filesystem::file_size(path);
        if (!input) throw std::ios_base::failure("Cannot open image file");
    }
    [[nodiscard]] std::uint64_t size() const noexcept { return length; }
    bool read(std::uint64_t offset, Byte* output, std::size_t count) {
        if (offset > length || count > length - offset ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) return false;
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(count));
        return input.gcount() == static_cast<std::streamsize>(count);
    }
};
template<class Reader>
bool parseJpegSize(Reader& source, std::uint32_t& x, std::uint32_t& y) {
    x = y = 0;
    Byte signature[2]{};
    if (source.size() < 6 || !source.read(0, signature, 2) || signature[0] != 0xff || signature[1] != 0xd8) return false;
    std::uint64_t offset = 2;
    while (offset <= source.size() && source.size() - offset > 2) {
        Byte marker[4]{};
        if (!source.read(offset, marker, 4) || marker[0] != 0xff) return false;
        offset += 4;
        if (marker[1] == 0xc0 || marker[1] == 0xc2) {
            Byte dimensions[5]{};
            if (!source.read(offset, dimensions, 5)) return false;
            y = (static_cast<std::uint32_t>(dimensions[1]) << 8) | dimensions[2];
            x = (static_cast<std::uint32_t>(dimensions[3]) << 8) | dimensions[4];
            return true;
        }
        const auto segment = (static_cast<unsigned>(marker[2]) << 8) | marker[3];
        if (segment < 2 || offset > source.size() || segment - 2 > source.size() - offset) return false;
        offset += segment - 2;
    }
    return false;
}
template<class Reader>
bool parseTiffSize(Reader& source, std::uint32_t& x, std::uint32_t& y, std::uint32_t imageIndex) {
    x = y = 0;
    Byte header[10]{};
    if (!source.read(0, header, sizeof(header))) return false;
    const bool little = header[0] == 'I' && header[1] == 'I';
    if (!little && !(header[0] == 'M' && header[1] == 'M')) return false;
    const auto read16 = [little](const Byte* b) { return little ? readUShortL(ByteView{b,2},0) : readUShortB(ByteView{b,2},0); };
    const auto read32 = [little](const Byte* b) { return little ? readUIntL(ByteView{b,4},0) : readUIntB(ByteView{b,4},0); };
    std::uint64_t ifd = read32(header + 4);
    // 元の最初のIFDの最小境界と、マジック値を無視する動作を維持する。
    if (ifd + 2 + 24 > source.size()) return false;
    std::uint16_t count{};
    for (std::uint32_t image = 0;; ++image) {
        Byte countBytes[2]{};
        if (!source.read(ifd, countBytes, 2)) return false;
        count = read16(countBytes);
        const auto end = ifd + 2 + 12ULL * count;
        // 次のIFDへのポインターは、n=0も含め、すべてのディレクトリの一部。
        if (end + 4 > source.size()) return false;
        if (image == imageIndex) break;
        Byte next[4]{};
        if (!source.read(end, next, 4)) return false;
        ifd = read32(next);
        if (!ifd) return false;
    }
    bool width = false, height = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        Byte entry[12]{};
        if (!source.read(ifd + 2 + 12ULL * i, entry, 12)) return false;
        const auto tag = read16(entry);
        if (tag == 0x100) { x = read32(entry + 8); width = true; }
        else if (tag == 0x101) { y = read32(entry + 8); height = true; }
        if (width && height) return true;
    }
    return false;
}

}  // namespace

bool isImageExt(lib::StringView path) {
    const auto ext = extensionOf(path);
    return ext == ".jpeg" || ext == ".jpg" || ext == ".jpe" ||
           ext == ".jfif" || ext == ".jfi" || ext == ".jif" ||
           ext == ".gif" || ext == ".tiff" || ext == ".tif" ||
           ext == ".bmp" || ext == ".png" || ext == ".apng" ||
           ext == ".ico" || ext == ".webp";
}

lib::String normalizedImageExt(lib::StringView ext) {
    const auto lower = lowerAscii(lib::String(ext));
    if (lower == ".jpeg" || lower == ".jpg" || lower == ".jpe" ||
        lower == ".jfif" || lower == ".jfi" || lower == ".jif") {
        return ".jpg";
    }
    if (lower == ".gif") return ".gif";
    if (lower == ".tiff" || lower == ".tif") {
        return ".tiff";
    }
    if (lower == ".bmp") return ".bmp";
    if (lower == ".png" || lower == ".apng") {
        return ".png";
    }
    if (lower == ".ico") return ".ico";
    if (lower == ".webp") return ".webp";
    // Dの既定分岐は元の拡張子を返し、表記と、呼び
    // 出し側が先頭にドットを付けたかを維持する。
    return lib::String(ext);
}

lib::String imageType(ByteView bytes) noexcept {
    // imagesize.dの最小長を維持する。寸法を調
    // べる前の軽量で範囲を限定した確認に使うため、署名の
    // 検査に必要なバイト数より意図的に大きくしている。
    if (range(bytes, 0, 22) && bytes[0] == 'B' && bytes[1] == 'M') {
        return ".bmp";
    }
    if (range(bytes, 0, 25) && bytes[0] == 0x89U && bytes[1] == 'P' &&
        bytes[2] == 'N' && bytes[3] == 'G') {
        return ".png";
    }
    if (range(bytes, 0, 30) && bytes[0] == 'R' && bytes[1] == 'I' &&
        bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' &&
        bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
        return ".webp";
    }
    if (range(bytes, 0, 10) && bytes[0] == 'G' && bytes[1] == 'I' &&
        bytes[2] == 'F') {
        return ".gif";
    }
    if (range(bytes, 0, 6) && bytes[0] == 0xffU && bytes[1] == 0xd8U) {
        return ".jpg";
    }
    if (range(bytes, 0, 10)) {
        if (bytes[0] == 'M' && bytes[1] == 'M' && bytes[3] == 42U) {
            return ".tiff";
        }
        if (bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42U) {
            return ".tiff";
        }
    }
    return {};
}

lib::String imageType(const lib::Bytes& bytes) noexcept {
    return imageType(ByteView(bytes.data(), bytes.size()));
}

lib::String getNormalizedImageExt(const std::filesystem::path& file) {
    const auto encoded = file.u8string();
    binary::RawFile input(lib::StringView(reinterpret_cast<const char*>(encoded.data()), encoded.size()), "rb");
    return getNormalizedImageExt(input);
}

lib::String getNormalizedImageExt(binary::RawFile& file) {
    const auto position = file.tell();
    lib::String result;
    try {
        file.seek(0);
        lib::Bytes bytes(static_cast<std::size_t>(std::min<std::uint64_t>(25, file.size())));
        // Dはfreadの項目全体の読み取り結果を無視し、部分的に読めたバ
        // イトとゼロ初期化された残りを保持する。共通の生I/O契約を使う。
        (void)file.rawRead(lib::MutableByteView(bytes.data(), bytes.size()));
        result = imageType(bytes);
    } catch (...) {
        file.seek(position);
        throw;
    }
    file.seek(position);
    return result;
}

lib::String getNormalizedImageExt(ByteView bytes) noexcept {
    return imageType(bytes);
}

lib::String getNormalizedImageExt(const lib::Bytes& bytes) noexcept {
    return imageType(bytes);
}

bool imageSize(ByteView bytes, std::uint32_t& x,
               std::uint32_t& y) noexcept {
    x = y = 0;
    return imageSizeByExtension(bytes, imageType(bytes), x, y);
}

bool imageSize(const lib::Bytes& bytes, std::uint32_t& x,
               std::uint32_t& y) noexcept {
    x = y = 0;
    return imageSize(ByteView(bytes.data(), bytes.size()), x, y);
}

bool imageSize(const std::filesystem::path& file, std::uint32_t& x,
               std::uint32_t& y) {
    x = y = 0;
    const auto ext = lowerAscii(file.extension().string());
    if (ext == ".jpeg" || ext == ".jpg" || ext == ".jpe" ||
        ext == ".jfif" || ext == ".jfi" || ext == ".jif") {
        return jpgSize(file, x, y);
    }
    if (ext == ".gif") return gifSize(file, x, y);
    if (ext == ".tiff" || ext == ".tif") return tifSize(file, x, y);
    if (ext == ".bmp") return bmpSize(file, x, y);
    if (ext == ".png" || ext == ".apng") return pngSize(file, x, y);
    if (ext == ".ico") return icoSize(file, x, y);
    if (ext == ".webp") return webpSize(file, x, y);
    return false;
}

bool imageSize(ByteView bytes, ImageSize& result) noexcept {
    return imageSize(bytes, result.width, result.height);
}

bool imageSize(const lib::Bytes& bytes, ImageSize& result) noexcept {
    return imageSize(bytes, result.width, result.height);
}

bool imageSize(const std::filesystem::path& file, ImageSize& result) {
    return imageSize(file, result.width, result.height);
}

namespace {

bool imageSizeByExtension(ByteView bytes, lib::StringView ext,
                          std::uint32_t& x,
                          std::uint32_t& y) noexcept {
    x = y = 0;
    if (ext == ".jpeg" || ext == ".jpg" || ext == ".jpe" ||
        ext == ".jfif" || ext == ".jfi" || ext == ".jif") {
        return jpgSize(bytes, x, y);
    }
    if (ext == ".gif") return gifSize(bytes, x, y);
    if (ext == ".tiff" || ext == ".tif") return tifSize(bytes, x, y);
    if (ext == ".bmp") return bmpSize(bytes, x, y);
    if (ext == ".png" || ext == ".apng") return pngSize(bytes, x, y);
    if (ext == ".ico") return icoSize(bytes, x, y);
    if (ext == ".webp") return webpSize(bytes, x, y);
    return false;
}

}  // namespace

bool webpSize(ByteView b, std::uint32_t& x,
              std::uint32_t& y) noexcept {
    x = y = 0;
    if (!range(b, 0, 30) || b[0] != 'R' || b[1] != 'I' || b[2] != 'F' ||
        b[3] != 'F' || b[8] != 'W' || b[9] != 'E' || b[10] != 'B' ||
        b[11] != 'P' || b[12] != 'V' || b[13] != 'P' || b[14] != '8') {
        return false;
    }
    switch (b[15]) {
    case 'X':
        x = 1U + static_cast<std::uint32_t>(b[24]) +
            (static_cast<std::uint32_t>(b[25]) << 8U) +
            (static_cast<std::uint32_t>(b[26]) << 16U);
        y = 1U + static_cast<std::uint32_t>(b[27]) +
            (static_cast<std::uint32_t>(b[28]) << 8U) +
            (static_cast<std::uint32_t>(b[29]) << 16U);
        return true;
    case 'L':
        if (b[20] != 0x2fU) return false;
        x = 1U + static_cast<std::uint32_t>(b[21]) +
            ((static_cast<std::uint32_t>(b[22]) & 0x3fU) << 8U);
        y = 1U + ((static_cast<std::uint32_t>(b[22]) & 0xc0U) >> 6U) +
            (static_cast<std::uint32_t>(b[23]) << 2U) +
            ((static_cast<std::uint32_t>(b[24]) & 0x0fU) << 10U);
        return true;
    case ' ':
        if (b[23] != 0x9dU || b[24] != 0x01U || b[25] != 0x2aU) {
            return false;
        }
        x = static_cast<std::uint32_t>(b[26]) +
            ((static_cast<std::uint32_t>(b[27]) & 0x3fU) << 8U);
        y = static_cast<std::uint32_t>(b[28]) +
            ((static_cast<std::uint32_t>(b[29]) & 0x3fU) << 8U);
        return x != 0 && y != 0;
    default:
        return false;
    }
}

bool webpSize(const lib::Bytes& file, std::uint32_t& x,
              std::uint32_t& y) noexcept {
    x = y = 0;
    return webpSize(ByteView(file.data(), file.size()), x, y);
}

bool webpSize(const std::filesystem::path& file, std::uint32_t& x,
              std::uint32_t& y) {
    x = y = 0;
    const auto bytes = readPrefix(file, 30);
    return !bytes.empty() && webpSize(bytes, x, y);
}

bool webpSize(ByteView file, ImageSize& result) noexcept {
    return webpSize(file, result.width, result.height);
}

bool webpSize(const lib::Bytes& file, ImageSize& result) noexcept {
    return webpSize(file, result.width, result.height);
}

bool jpgSize(ByteView b, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    MemoryImageReader source{b};
    return parseJpegSize(source, x, y);
}

bool jpgSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    return jpgSize(ByteView(file.data(), file.size()), x, y);
}

bool jpgSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y) {
    x = y = 0;
    FileImageReader source{file};
    return parseJpegSize(source, x, y);
}

bool jpgSize(ByteView file, ImageSize& result) noexcept {
    return jpgSize(file, result.width, result.height);
}

bool jpgSize(const lib::Bytes& file, ImageSize& result) noexcept {
    return jpgSize(file, result.width, result.height);
}

bool tifSize(ByteView b, std::uint32_t& x, std::uint32_t& y,
             std::uint32_t n) noexcept {
    x = y = 0;
    MemoryImageReader source{b};
    return parseTiffSize(source, x, y, n);
}

bool tifSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y, std::uint32_t n) noexcept {
    x = y = 0;
    return tifSize(ByteView(file.data(), file.size()), x, y, n);
}

bool tifSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y, std::uint32_t n) {
    x = y = 0;
    FileImageReader source{file};
    return parseTiffSize(source, x, y, n);
}

bool tifSize(ByteView file, ImageSize& result, std::uint32_t n) noexcept {
    return tifSize(file, result.width, result.height, n);
}

bool tifSize(const lib::Bytes& file, ImageSize& result,
             std::uint32_t n) noexcept {
    return tifSize(file, result.width, result.height, n);
}

bool gifSize(ByteView b, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    if (!range(b, 0, 10) || b[0] != 'G' || b[1] != 'I' || b[2] != 'F') {
        return false;
    }
    x = readUShortL(b, 6U);
    y = readUShortL(b, 8U);
    return true;
}

bool gifSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    return gifSize(ByteView(file.data(), file.size()), x, y);
}

bool gifSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y) {
    x = y = 0;
    const auto bytes = readPrefix(file, 10);
    return !bytes.empty() && gifSize(bytes, x, y);
}

bool gifSize(ByteView file, ImageSize& result) noexcept {
    return gifSize(file, result.width, result.height);
}

bool gifSize(const lib::Bytes& file, ImageSize& result) noexcept {
    return gifSize(file, result.width, result.height);
}

bool bmpSize(ByteView b, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    if (!range(b, 0, 22) || b[0] != 'B' || b[1] != 'M') return false;
    const auto headerSize = readUIntL(b, 14U);
    if (headerSize == 12U) {
        x = readUShortL(b, 18U);
        y = readUShortL(b, 20U);
    } else if (headerSize > 12U) {
        if (!range(b, 0, 26)) return false;
        x = readUIntL(b, 18U);
        y = readUIntL(b, 22U);
    } else {
        return false;
    }
    return true;
}

bool bmpSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    return bmpSize(ByteView(file.data(), file.size()), x, y);
}

bool bmpSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y) {
    x = y = 0;
    const auto bytes = readPrefix(file, 26);
    return !bytes.empty() && bmpSize(bytes, x, y);
}

bool bmpSize(ByteView file, ImageSize& result) noexcept {
    return bmpSize(file, result.width, result.height);
}

bool bmpSize(const lib::Bytes& file, ImageSize& result) noexcept {
    return bmpSize(file, result.width, result.height);
}

bool pngSize(ByteView b, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    if (!range(b, 0, 24) || b[0] != 0x89U || b[1] != 'P' ||
        b[2] != 'N' || b[3] != 'G' || b[4] != 0x0dU || b[5] != 0x0aU ||
        b[6] != 0x1aU || b[7] != 0x0aU || readUIntB(b, 8U) != 13U ||
        b[12] != 'I' || b[13] != 'H' || b[14] != 'D' || b[15] != 'R') {
        return false;
    }
    x = readUIntB(b, 16U);
    y = readUIntB(b, 20U);
    return true;
}

bool pngSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y) noexcept {
    x = y = 0;
    return pngSize(ByteView(file.data(), file.size()), x, y);
}

bool pngSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y) {
    x = y = 0;
    const auto bytes = readPrefix(file, 24);
    return !bytes.empty() && pngSize(bytes, x, y);
}

bool pngSize(ByteView file, ImageSize& result) noexcept {
    return pngSize(file, result.width, result.height);
}

bool pngSize(const lib::Bytes& file, ImageSize& result) noexcept {
    return pngSize(file, result.width, result.height);
}

bool icoSize(ByteView b, std::uint32_t& x,
             std::uint32_t& y, std::uint32_t n) noexcept {
    x = y = 0;
    if (!range(b, 0, 8) || readUShortL(b, 0U) != 0U ||
        readUShortL(b, 2U) != 1U) {
        return false;
    }
    const auto count = readUShortL(b, 4U);
    if (n >= count) return false;
    if (n > 0U) {
        const auto position = 6ULL + static_cast<std::uint64_t>(n) * 16ULL;
        if (position + 2ULL >= b.size() ||
            !range(b, static_cast<std::size_t>(position), 2U)) {
            return false;
        }
        x = b[static_cast<std::size_t>(position)];
        y = b[static_cast<std::size_t>(position) + 1U];
        return true;
    }
    x = b[6U];
    y = b[7U];
    return true;
}

bool icoSize(const lib::Bytes& file, std::uint32_t& x,
             std::uint32_t& y, std::uint32_t n) noexcept {
    x = y = 0;
    return icoSize(ByteView(file.data(), file.size()), x, y, n);
}

bool icoSize(const std::filesystem::path& file, std::uint32_t& x,
             std::uint32_t& y, std::uint32_t n) {
    x = y = 0;
    if (n > (std::numeric_limits<std::size_t>::max() - 22U) / 16U) return false;
    const auto bytes = readPrefix(file, 6U + (static_cast<std::size_t>(n) + 1U) * 16U);
    return !bytes.empty() && icoSize(bytes, x, y, n);
}

bool icoSize(ByteView file, ImageSize& result, std::uint32_t n) noexcept {
    return icoSize(file, result.width, result.height, n);
}

bool icoSize(const lib::Bytes& file, ImageSize& result,
             std::uint32_t n) noexcept {
    return icoSize(file, result.width, result.height, n);
}

namespace {

void fixCWNext16BitBitmapView(lib::MutableByteView bytes) noexcept {
    if (!range(ByteView(bytes.data(), bytes.size()), 0, 14U + 40U)) return;
    const auto view = ByteView(bytes.data(), bytes.size());
    if (view[0] != 'B' || view[1] != 'M') return;
    const auto bfOffBits = readUIntL(view, 10U);
    if (bfOffBits == 0U || readUIntL(view, 14U) != 40U) return;
    const auto biWidth = readUIntL(view, 18U);
    const auto biHeight = readIntL(view, 22U);
    const auto biBitCount = readUShortL(view, 28U);
    const auto biCompression = readUIntL(view, 30U);
    auto biClrUsed = readUIntL(view, 46U);
    const auto lineSize =
        ((biWidth * static_cast<std::uint32_t>(biBitCount) + 31U) / 32U) *
        4U;
    const auto height = biHeight < 0
                            ? static_cast<std::uint32_t>(
                                  -static_cast<std::int64_t>(biHeight))
                            : static_cast<std::uint32_t>(biHeight);
    if (bytes.size() - static_cast<std::size_t>(bfOffBits) ==
        static_cast<std::size_t>(lineSize * height)) {
        return;
    }

    std::uint32_t corrected = 14U + 40U;
    if (biCompression == 3U) corrected += 4U * 3U;
    if (biBitCount == 1U || biBitCount == 4U || biBitCount == 8U) {
        if (biClrUsed == 0U) {
            biClrUsed = static_cast<std::uint32_t>(biBitCount) * biBitCount;
        }
    }
    corrected += biClrUsed * 4U;
    writeUIntL(bytes, 10U, corrected);
}

}  // namespace

void fixCWNext16BitBitmap(lib::Bytes& bytes) noexcept {
    fixCWNext16BitBitmapView(
        lib::MutableByteView(bytes.data(), bytes.size()));
}

void fixCWNext32BitBitmap(lib::Bytes& bytes) {
    if (bytes.size() < 14U + 40U) return;
    auto view = lib::MutableByteView(bytes.data(), bytes.size());
    if (view[0] != 'B' || view[1] != 'M' || readUIntL(ByteView(view.data(), view.size()), 10U) == 0U ||
        readUIntL(ByteView(view.data(), view.size()), 14U) != 40U) {
        return;
    }
    const auto bitCount = readUShortL(ByteView(view.data(), view.size()), 28U);
    if (bitCount != 32U && bitCount != 24U) return;
    const auto clrUsed = readUIntL(ByteView(view.data(), view.size()), 46U);
    if (clrUsed == 0U) return;
    writeUIntL(view, 46U, 0U);
    const auto first = static_cast<std::size_t>(14U + 40U);
    // Dはオーバーフローも含めuint32で末尾部分の開始位置を計算し、そのスラ
    // イスを検査する。この検査に失敗する時点では、色数はすでに0になっている。
    const auto last = static_cast<std::uint32_t>(14U + 40U + clrUsed * 4U);
    if (last > bytes.size()) throw std::out_of_range("Bitmap palette suffix");
    // 二つの独立したスライスを維持する。折り返しで末尾部分がヘッダーと
    // 重なる場合があり、一つのイテレーター範囲の削除では表現できない。
    lib::Bytes replacement;
    replacement.reserve(first + bytes.size() - last);
    replacement.insert(replacement.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(first));
    replacement.insert(replacement.end(), bytes.begin() + static_cast<std::ptrdiff_t>(last), bytes.end());
    bytes = std::move(replacement);
}

bool getBitField(ByteView bytes, std::uint32_t& r,
                 std::uint32_t& g, std::uint32_t& b) noexcept {
    r = g = b = 0;
    if (!range(bytes, 0, 14U + 40U) || bytes[0] != 'B' || bytes[1] != 'M' ||
        readUIntL(bytes, 10U) == 0U || readUIntL(bytes, 14U) != 40U) {
        return false;
    }
    if (readUIntL(bytes, 30U) != 3U || !range(bytes, 54U, 12U)) return false;
    r = readUIntL(bytes, 54U);
    g = readUIntL(bytes, 58U);
    b = readUIntL(bytes, 62U);
    return true;
}

bool getBitField(const lib::Bytes& bytes, std::uint32_t& r,
                 std::uint32_t& g, std::uint32_t& b) noexcept {
    return getBitField(ByteView(bytes.data(), bytes.size()), r, g, b);
}

}  // namespace cwxeditor::cwx::imagesize
