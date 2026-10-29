#pragma once

#include "cw/lib/compat.hpp"

#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>

namespace cw::lib::imagecodec {

enum class Format {
    Unknown,
    PNG,
    BMP,
    JPEG,
    GIF,
    WebP,
    TGA,
};

enum class Mode {
    Unknown,
    L,
    LA,
    RGB,
    RGBA,
    Palette,
    CMYK,
};

class Error : public std::runtime_error {
public:
    explicit Error(const ::cw::lib::String& message) : std::runtime_error(message) {}
};

class DataError : public Error {
public:
    explicit DataError(const ::cw::lib::String& message) : Error(message) {}
};

class DecodeError : public Error {
public:
    explicit DecodeError(const ::cw::lib::String& message) : Error(message) {}
};

class EncodeError : public Error {
public:
    explicit EncodeError(const ::cw::lib::String& message) : Error(message) {}
};

class UnsupportedFormatError : public Error {
public:
    explicit UnsupportedFormatError(const ::cw::lib::String& message) : Error(message) {}
};

struct Info {
    Format format{Format::Unknown};
    Mode mode{Mode::Unknown};
    ::cw::lib::Integer width{};
    ::cw::lib::Integer height{};
    std::size_t frame_count{1};
    std::vector<::cw::lib::Integer> durations_ms{};
    ::cw::lib::Integer loop_count{};
    bool loop_defined{};
    // APNG の IDAT 画像は acTL のアニメーションフレームより前にあるが、そのフレーム数には含めない。
    bool separate_default_image{};
    bool has_alpha{};
    bool has_transparency{};
    // コンテナーのメタデータ。EXIF/ICC/XMP などのバイナリーデータは
    // String にバイトを変えずに保持し、テキストのキーは元の文字列を保つ。
    std::map<::cw::lib::String, ::cw::lib::String> metadata{};
};

// 共通 Surface は pygame の RGB 形式でも RGBA で格納する。WebP
// は Py の Pillow から RGBA への経路に従い、通常形式はアルファの有無を保つ。
inline bool surface_has_pixel_alpha(const Info& info) noexcept
{
    return info.has_alpha || info.format == Format::WebP;
}

struct RGBAImage {
    ::cw::lib::Integer width{};
    ::cw::lib::Integer height{};
    std::vector<std::uint8_t> pixels{};
    // 正規の画素だけでは、パレットのカラーキーと画素ごとのアルファを区
    // 別できない。Py の load_image の convert()/mask
    // の方針のため、復号元の形式を保持する。汎用 RGBA
    // の利用側は同じ画素を使い、この情報を解釈する必要はない。
    unsigned source_pixel_bits{32};
    bool source_has_pixel_alpha{true};
    bool source_has_colorkey{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> source_colorkey{};
    // SDL3 は GIF の最初のフレームを描画面に合成するが、Py の通
    // 常の GIF 読み込みはそのインデックス画像自体を返す。その矩形は通
    // 常の画像ローダーだけが使い、共通アニメーションの描画面には使わない。
    std::optional<::cw::lib::Rect> source_frame;
};

struct Frame {
    RGBAImage image{};
    ::cw::lib::Integer duration_ms{100};
};

class Animation {
public:
    Animation() = default;
    Animation(Info info, std::vector<Frame> frames);

    const Info& info() const noexcept { return m_info; }
    std::size_t frame_count() const noexcept { return m_frames.size(); }
    std::size_t frame_index() const noexcept { return m_current; }
    const Frame& frame(std::size_t index) const;
    const Frame& current() const;
    void seek(std::size_t index);
    RGBAImage convert_rgba(std::size_t index) const;
    Animation copy() const;

private:
    Info m_info{};
    std::vector<Frame> m_frames{};
    std::size_t m_current{};
};

Info inspect(std::span<const std::uint8_t> data);
Info inspect_file(const std::filesystem::path& path);

// 共通デコーダーは既定で正規の RGBA を保持する。Py の通常の pygame
// 画像ローダーは、元の Surface としての解釈を明示的に要求する。
enum class DecodeProfile { CanonicalRGBA, PygameSurface };
RGBAImage decode_rgba(std::span<const std::uint8_t> data, std::size_t frame = 0);
RGBAImage decode_rgba(std::span<const std::uint8_t> data, std::size_t frame, DecodeProfile profile);
RGBAImage decode_rgba_file(const std::filesystem::path& path, std::size_t frame = 0);

Animation decode_animation(std::span<const std::uint8_t> data);
Animation decode_animation_file(const std::filesystem::path& path);

std::vector<std::uint8_t> encode_png(const RGBAImage& image);
std::vector<std::uint8_t> encode_bmp(const RGBAImage& image);
// pygame.image.save は未知の拡張子に行単位の RLE TGA を使
// う。RGBA の格納形式でも、元の Surface にアルファがあるとは限らない。
std::vector<std::uint8_t> encode_tga(const RGBAImage& image, bool has_pixel_alpha = true);
// pygame の JPEG 保存は、SRCALPHA を黒背景上の RGB に変換して
// から符号化する。RGB 画像は、共通 RGBA 格納領域のアルファバイトを無視する。
std::vector<std::uint8_t> encode_jpeg(const RGBAImage& image, bool has_pixel_alpha = true);
void encode_png_file(const RGBAImage& image, const std::filesystem::path& path);
void encode_bmp_file(const RGBAImage& image, const std::filesystem::path& path);

}  // namespace cw::lib::imagecodec
