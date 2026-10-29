#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <filesystem>

namespace cwxeditor::cwx::binary { class RawFile; }

namespace cwxeditor::cwx::imagesize {

struct ImageSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

[[nodiscard]] bool isImageExt(lib::StringView path);
[[nodiscard]] lib::String normalizedImageExt(lib::StringView ext);

[[nodiscard]] lib::String imageType(lib::ByteView bytes) noexcept;
[[nodiscard]] lib::String imageType(const lib::Bytes& bytes) noexcept;

[[nodiscard]] lib::String getNormalizedImageExt(
    const std::filesystem::path& file);
[[nodiscard]] lib::String getNormalizedImageExt(binary::RawFile& file);
[[nodiscard]] lib::String getNormalizedImageExt(lib::ByteView bytes) noexcept;
[[nodiscard]] lib::String getNormalizedImageExt(
    const lib::Bytes& bytes) noexcept;

[[nodiscard]] bool imageSize(lib::ByteView bytes,
                             std::uint32_t& x,
                             std::uint32_t& y) noexcept;
[[nodiscard]] bool imageSize(const lib::Bytes& bytes,
                             std::uint32_t& x,
                             std::uint32_t& y) noexcept;
[[nodiscard]] bool imageSize(const std::filesystem::path& file,
                             std::uint32_t& x,
                             std::uint32_t& y);
[[nodiscard]] bool imageSize(lib::ByteView bytes,
                             ImageSize& result) noexcept;
[[nodiscard]] bool imageSize(const lib::Bytes& bytes,
                             ImageSize& result) noexcept;
[[nodiscard]] bool imageSize(const std::filesystem::path& file,
                             ImageSize& result);

[[nodiscard]] bool webpSize(lib::ByteView file,
                            std::uint32_t& x,
                            std::uint32_t& y) noexcept;
[[nodiscard]] bool webpSize(const lib::Bytes& file,
                            std::uint32_t& x,
                            std::uint32_t& y) noexcept;
[[nodiscard]] bool webpSize(const std::filesystem::path& file,
                            std::uint32_t& x,
                            std::uint32_t& y);
[[nodiscard]] bool webpSize(lib::ByteView file,
                            ImageSize& result) noexcept;
[[nodiscard]] bool webpSize(const lib::Bytes& file,
                            ImageSize& result) noexcept;

[[nodiscard]] bool jpgSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool jpgSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool jpgSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y);
[[nodiscard]] bool jpgSize(lib::ByteView file,
                           ImageSize& result) noexcept;
[[nodiscard]] bool jpgSize(const lib::Bytes& file,
                           ImageSize& result) noexcept;

[[nodiscard]] bool tifSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool tifSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool tifSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0);
[[nodiscard]] bool tifSize(lib::ByteView file,
                           ImageSize& result,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool tifSize(const lib::Bytes& file,
                           ImageSize& result,
                           std::uint32_t n = 0) noexcept;

[[nodiscard]] bool gifSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool gifSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool gifSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y);
[[nodiscard]] bool gifSize(lib::ByteView file,
                           ImageSize& result) noexcept;
[[nodiscard]] bool gifSize(const lib::Bytes& file,
                           ImageSize& result) noexcept;

[[nodiscard]] bool bmpSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool bmpSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool bmpSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y);
[[nodiscard]] bool bmpSize(lib::ByteView file,
                           ImageSize& result) noexcept;
[[nodiscard]] bool bmpSize(const lib::Bytes& file,
                           ImageSize& result) noexcept;

[[nodiscard]] bool pngSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool pngSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y) noexcept;
[[nodiscard]] bool pngSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y);
[[nodiscard]] bool pngSize(lib::ByteView file,
                           ImageSize& result) noexcept;
[[nodiscard]] bool pngSize(const lib::Bytes& file,
                           ImageSize& result) noexcept;

[[nodiscard]] bool icoSize(lib::ByteView file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool icoSize(const lib::Bytes& file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool icoSize(const std::filesystem::path& file,
                           std::uint32_t& x,
                           std::uint32_t& y,
                           std::uint32_t n = 0);
[[nodiscard]] bool icoSize(lib::ByteView file,
                           ImageSize& result,
                           std::uint32_t n = 0) noexcept;
[[nodiscard]] bool icoSize(const lib::Bytes& file,
                           ImageSize& result,
                           std::uint32_t n = 0) noexcept;

void fixCWNext16BitBitmap(lib::Bytes& bytes) noexcept;
void fixCWNext32BitBitmap(lib::Bytes& bytes);

[[nodiscard]] bool getBitField(lib::ByteView bytes,
                               std::uint32_t& r,
                               std::uint32_t& g,
                               std::uint32_t& b) noexcept;
[[nodiscard]] bool getBitField(const lib::Bytes& bytes,
                               std::uint32_t& r,
                               std::uint32_t& g,
                               std::uint32_t& b) noexcept;

}  // namespace cwxeditor::cwx::imagesize
