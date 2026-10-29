#pragma once

#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
// SDL_imageでは同梱の静的WebPを明示的に有効化している。 そ
// のインクルード先は非公開なので、既存のソースルートのパスから読み込む。
#include "3rdparty/SDL_image/external/libwebp/src/webp/demux.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <span>

namespace cwxeditor::cwx::editor::gui::dwt::standalone_webp {

// Dと同じく、透明なSDL画面へ合成するのではなくネイティブのMODE_RGBA
// で再構成する。 WebP本来のblend/disposeとNO_BLENDを
// 保持する。 空の結果なら、未対応・不正データの処理を既存の変換器へ委譲する。
inline lib::ImageData decode(std::span<const std::byte> encoded) {
    if (lib::editor_window_host_options().all_mode || encoded.size() < 12 ||
        std::memcmp(encoded.data(), "RIFF", 4) != 0 ||
        std::memcmp(encoded.data() + 8, "WEBP", 4) != 0) return {};
    try {
        ::WebPData input{reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size()};
        std::unique_ptr<::WebPAnimDecoder, decltype(&::WebPAnimDecoderDelete)>
            decoder(::WebPAnimDecoderNew(&input, nullptr), &::WebPAnimDecoderDelete);
        ::WebPAnimInfo info{};
        if (!decoder || !::WebPAnimDecoderGetInfo(decoder.get(), &info) ||
            !info.canvas_width || !info.canvas_height ||
            info.canvas_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            info.canvas_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) return {};
        const auto width = static_cast<std::size_t>(info.canvas_width);
        const auto height = static_cast<std::size_t>(info.canvas_height);
        if (width > std::numeric_limits<std::size_t>::max() / 4 ||
            height > std::numeric_limits<std::size_t>::max() / (width * 4)) return {};
        const auto size = width * height * 4;
        lib::ImageData result;
        result.format = lib::ImageFormat::WebP;
        result.loop_count = info.loop_count;
        std::int64_t previous{};
        while (::WebPAnimDecoderHasMoreFrames(decoder.get())) {
            std::uint8_t* rgba{};
            int timestamp{};
            if (!::WebPAnimDecoderGetNext(decoder.get(), &rgba, &timestamp) || !rgba) return {};
            lib::ImageFrame frame;
            frame.pixel_width = info.canvas_width;
            frame.pixel_height = info.canvas_height;
            frame.logical_size = {static_cast<float>(info.canvas_width), static_cast<float>(info.canvas_height)};
            frame.pixel_format = lib::PixelFormat::RGBA32;
            frame.duration = std::chrono::milliseconds(std::max<std::int64_t>(0, static_cast<std::int64_t>(timestamp) - previous));
            previous = timestamp;
            frame.pixels.resize(size);
            std::memcpy(frame.pixels.data(), rgba, size);
            result.frames.push_back(std::move(frame));
        }
        return result;
    } catch (...) {
        return {};
    }
}
} // namespace cwxeditor::cwx::editor::gui::dwt::standalone_webp
