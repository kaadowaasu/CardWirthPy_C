#include "cwxeditor_src/gui/cwx/editor/gui/dwt/webp.hpp"

#include "cw/lib/imagecodec.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::webp {
namespace {

using Byte = std::uint8_t;
using ByteView = std::span<const Byte>;

[[nodiscard]] ByteView asUnsigned(std::span<const std::int8_t> bytes) noexcept {
    return {reinterpret_cast<const Byte*>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool validWebP(ByteView bytes,
                             std::int32_t* width = nullptr,
                             std::int32_t* height = nullptr) noexcept {
    if (bytes.empty()) return false;
    try {
        const auto info = ::cw::lib::imagecodec::inspect(bytes);
        if (info.format != ::cw::lib::imagecodec::Format::WebP ||
            info.width <= 0 || info.height <= 0 ||
            info.width > std::numeric_limits<std::int32_t>::max() ||
            info.height > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        if (width) *width = static_cast<std::int32_t>(info.width);
        if (height) *height = static_cast<std::int32_t>(info.height);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::size_t checkedRgbaSize(std::int32_t width,
                                          std::int32_t height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid WebP size");
    }
    const auto widthValue = static_cast<std::size_t>(width);
    const auto heightValue = static_cast<std::size_t>(height);
    if (widthValue > std::numeric_limits<std::size_t>::max() / 4U ||
        heightValue > std::numeric_limits<std::size_t>::max() /
                           (widthValue * 4U)) {
        throw std::runtime_error("WebP image is too large");
    }
    return widthValue * heightValue * 4U;
}

[[nodiscard]] ::cwxeditor::lib::ImageData rgbaToImageData(
    const Byte* rgba,
    std::int32_t width,
    std::int32_t height) {
    if (!rgba) throw std::runtime_error("WebP decode failure");
    const auto rgbaSize = checkedRgbaSize(width, height);

    ::cwxeditor::lib::ImageData result;
    result.format = ::cwxeditor::lib::ImageFormat::WebP;
    ::cwxeditor::lib::ImageFrame frame;
    frame.logical_size = {static_cast<float>(width), static_cast<float>(height)};
    frame.pixel_width = static_cast<std::uint32_t>(width);
    frame.pixel_height = static_cast<std::uint32_t>(height);

    bool hasAlpha = false;
    for (std::size_t offset = 3; offset < rgbaSize; offset += 4U) {
        if (rgba[offset] != 0xffU) {
            hasAlpha = true;
            break;
        }
    }
    if (hasAlpha) {
        frame.pixel_format = ::cwxeditor::lib::PixelFormat::RGBA32;
        frame.pixels.resize(rgbaSize);
        std::memcpy(frame.pixels.data(), rgba, rgbaSize);
    } else {
        frame.pixel_format = ::cwxeditor::lib::PixelFormat::RGB24;
        const auto pixelCount = rgbaSize / 4U;
        frame.pixels.resize(pixelCount * 3U);
        for (std::size_t source = 0, target = 0; source < rgbaSize;
             source += 4U, target += 3U) {
            frame.pixels[target] = static_cast<std::byte>(rgba[source]);
            frame.pixels[target + 1U] =
                static_cast<std::byte>(rgba[source + 1U]);
            frame.pixels[target + 2U] =
                static_cast<std::byte>(rgba[source + 2U]);
        }
    }
    result.frames.push_back(std::move(frame));
    return result;
}

[[nodiscard]] std::uint32_t animationBackground(
    const ::cw::lib::imagecodec::Info& info) noexcept {
    const auto found = info.metadata.find("animation_background_bgra");
    if (found == info.metadata.end() || found->second.size() < 4U) return 0;
    const auto* bytes = reinterpret_cast<const Byte*>(found->second.data());
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::int32_t timestampFor(
    const std::vector<std::int64_t>& timestamps,
    std::size_t index) noexcept {
    if (index >= timestamps.size()) return 0;
    const auto value = std::clamp<std::int64_t>(
        timestamps[index], 0, std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(value);
}

}  // namespace

int WebPGetInfo(const std::uint8_t* data,
                std::size_t data_size,
                std::int32_t* width,
                std::int32_t* height) noexcept {
    if (!data && data_size != 0) return 0;
    return validWebP(ByteView(data, data_size), width, height) ? 1 : 0;
}

std::uint8_t* WebPDecodeRGBA(const std::uint8_t* data,
                             std::size_t data_size,
                             std::int32_t* width,
                             std::int32_t* height) noexcept {
    if (!data && data_size != 0) return nullptr;
    try {
        const auto bytes = ByteView(data, data_size);
        std::int32_t decodedWidth{};
        std::int32_t decodedHeight{};
        if (!validWebP(bytes, &decodedWidth, &decodedHeight)) return nullptr;
        const auto rgba = ::cw::lib::imagecodec::decode_rgba(bytes);
        const auto size = checkedRgbaSize(decodedWidth, decodedHeight);
        if (rgba.width != decodedWidth || rgba.height != decodedHeight ||
            rgba.pixels.size() != size) {
            return nullptr;
        }
        auto* result = static_cast<Byte*>(std::malloc(size));
        if (!result) return nullptr;
        std::memcpy(result, rgba.pixels.data(), size);
        if (width) *width = decodedWidth;
        if (height) *height = decodedHeight;
        return result;
    } catch (...) {
        return nullptr;
    }
}

void WebPFree(void* ptr) noexcept {
    std::free(ptr);
}

WebPAnimDecoder* WebPAnimDecoderNewInternal(
    const WebPData* webp_data,
    const WebPAnimDecoderOptions* dec_options,
    std::int32_t abi_version) noexcept {
    if (!webp_data || (!webp_data->bytes && webp_data->size != 0) ||
        abi_version != WEBP_DEMUX_ABI_VERSION) {
        return nullptr;
    }
    // D版の呼び出し元はオプションに null を渡す。 共通コー
    // デックは意図的にRGBAフレームだけを公開するため、 オプ
    // ションオブジェクトは受け付けてもフレームの表現は変えない。
    static_cast<void>(dec_options);
    try {
        const ByteView bytes(webp_data->bytes, webp_data->size);
        const auto info = ::cw::lib::imagecodec::inspect(bytes);
        if (info.format != ::cw::lib::imagecodec::Format::WebP) return nullptr;
        auto animation = ::cw::lib::imagecodec::decode_animation(bytes);
        if (animation.frame_count() == 0 || info.width <= 0 || info.height <= 0 ||
            info.width > std::numeric_limits<std::int32_t>::max() ||
            info.height > std::numeric_limits<std::int32_t>::max()) {
            return nullptr;
        }

        auto result = std::make_unique<WebPAnimDecoder>();
        result->canvas_width = static_cast<std::uint32_t>(info.width);
        result->canvas_height = static_cast<std::uint32_t>(info.height);
        result->loop_count = info.loop_defined && info.loop_count >= 0
                                 ? static_cast<std::uint32_t>(info.loop_count)
                                 : 0U;
        result->bgcolor = animationBackground(info);
        result->frames.reserve(animation.frame_count());
        result->timestamps.reserve(animation.frame_count());

        std::vector<std::int64_t> timestamps;
        timestamps.reserve(animation.frame_count());
        std::int64_t timestamp{};
        for (std::size_t index = 0; index < animation.frame_count(); ++index) {
            const auto& frame = animation.frame(index);
            if (frame.image.width != info.width || frame.image.height != info.height ||
                frame.image.pixels.empty()) {
                return nullptr;
            }
            result->frames.push_back(frame.image.pixels);
            timestamp += std::max<::cw::lib::Integer>(frame.duration_ms, 0);
            timestamps.push_back(timestamp);
        }
        for (std::size_t index = 0; index < timestamps.size(); ++index) {
            result->timestamps.push_back(timestampFor(timestamps, index));
        }
        return result.release();
    } catch (...) {
        return nullptr;
    }
}

int WebPAnimDecoderGetInfo(const WebPAnimDecoder* dec,
                           WebPAnimInfo* info) noexcept {
    if (!dec || !info) return 0;
    if (dec->frames.size() > std::numeric_limits<std::uint32_t>::max()) return 0;
    info->canvas_width = dec->canvas_width;
    info->canvas_height = dec->canvas_height;
    info->loop_count = dec->loop_count;
    info->bgcolor = dec->bgcolor;
    info->frame_count = static_cast<std::uint32_t>(dec->frames.size());
    info->pad.fill(0U);
    return 1;
}

int WebPAnimDecoderGetNext(WebPAnimDecoder* dec,
                           std::uint8_t** buf,
                           std::int32_t* timestamp) noexcept {
    if (!dec || !buf || !timestamp || dec->next_frame >= dec->frames.size()) {
        return 0;
    }
    try {
        auto& frame = dec->frames[dec->next_frame];
        if (frame.empty()) return 0;
        *buf = frame.data();
        *timestamp = dec->timestamps[dec->next_frame];
        ++dec->next_frame;
        return 1;
    } catch (...) {
        *buf = nullptr;
        return 0;
    }
}

void WebPAnimDecoderDelete(WebPAnimDecoder* dec) noexcept {
    delete dec;
}

::cwxeditor::lib::ImageData loadWebPImageData(
    std::span<const std::int8_t> bytes) {
    const auto encoded = asUnsigned(bytes);
    WebPData data{encoded.data(), encoded.size()};
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
        WebPAnimDecoderNewInternal(&data, nullptr, WEBP_DEMUX_ABI_VERSION),
        &WebPAnimDecoderDelete);

    if (decoder) {
        WebPAnimInfo info;
        Byte* rgba = nullptr;
        std::int32_t timestamp{};
        if (WebPAnimDecoderGetInfo(decoder.get(), &info)) {
            ::cwxeditor::lib::ImageData result;
            result.format = ::cwxeditor::lib::ImageFormat::WebP;
            result.loop_count = info.loop_count;
            std::int32_t previous{};
            while (WebPAnimDecoderGetNext(decoder.get(), &rgba, &timestamp) && rgba) {
                auto decoded = rgbaToImageData(
                    rgba, static_cast<std::int32_t>(info.canvas_width),
                    static_cast<std::int32_t>(info.canvas_height));
                if (decoded.frames.empty()) break;
                decoded.frames.front().duration = std::chrono::milliseconds(
                    std::max<std::int32_t>(0, timestamp - previous));
                previous = timestamp;
                result.frames.push_back(std::move(decoded.frames.front()));
            }
            if (!result.frames.empty()) return result;
        }
    }

    std::int32_t width{};
    std::int32_t height{};
    std::unique_ptr<Byte, decltype(&WebPFree)> rgba(
        WebPDecodeRGBA(encoded.data(), encoded.size(), &width, &height),
        &WebPFree);
    if (!rgba) throw std::runtime_error("WebP decode failure");
    return rgbaToImageData(rgba.get(), width, height);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::webp
