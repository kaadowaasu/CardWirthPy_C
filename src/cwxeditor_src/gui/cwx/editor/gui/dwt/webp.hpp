#pragma once

#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::webp {

// Dの列挙値は、WebPAnimDecoderNewInternalへ渡すABIバージョンを表す。
inline constexpr std::int32_t WEBP_DEMUX_ABI_VERSION = 0x0107;

struct WebPData {
    const std::uint8_t* bytes{};
    std::size_t size{};
};

// Dではこの構造体を不透明に取り込む。C++では設定を
// 準備する呼び出し元にlibwebpの具体的な配置を
// 公開する。 デコーダーは、元のDのnull設定での
// 呼び出しと同じく、常にRGBAフレームを生成する。
struct WebPAnimDecoderOptions {
    std::int32_t color_mode{};
    std::int32_t use_threads{};
    std::array<std::uint32_t, 7> padding{};
};

struct WebPAnimInfo {
    std::uint32_t canvas_width{};
    std::uint32_t canvas_height{};
    std::uint32_t loop_count{};
    std::uint32_t bgcolor{};
    std::uint32_t frame_count{};
    std::array<std::uint32_t, 4> pad{};
};

// デコーダーが復号フレームを所有する。libwebpのWebPAnimDecoder
// と同じく、 返したフレームのポインターは次の
// GetNextまたはデコーダー削除まで有効。
struct WebPAnimDecoder {
    std::uint32_t canvas_width{};
    std::uint32_t canvas_height{};
    std::uint32_t loop_count{};
    std::uint32_t bgcolor{};
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<std::int32_t> timestamps;
    std::size_t next_frame{};
};

[[nodiscard]] int WebPGetInfo(const std::uint8_t* data,
                              std::size_t data_size,
                              std::int32_t* width,
                              std::int32_t* height) noexcept;

[[nodiscard]] std::uint8_t* WebPDecodeRGBA(const std::uint8_t* data,
                                           std::size_t data_size,
                                           std::int32_t* width,
                                           std::int32_t* height) noexcept;

void WebPFree(void* ptr) noexcept;

[[nodiscard]] WebPAnimDecoder* WebPAnimDecoderNewInternal(
    const WebPData* webp_data,
    const WebPAnimDecoderOptions* dec_options,
    std::int32_t abi_version) noexcept;

[[nodiscard]] int WebPAnimDecoderGetInfo(const WebPAnimDecoder* dec,
                                         WebPAnimInfo* info) noexcept;

[[nodiscard]] int WebPAnimDecoderGetNext(WebPAnimDecoder* dec,
                                         std::uint8_t** buf,
                                         std::int32_t* timestamp) noexcept;

void WebPAnimDecoderDelete(WebPAnimDecoder* dec) noexcept;

[[nodiscard]] ::cwxeditor::lib::ImageData loadWebPImageData(
    std::span<const std::int8_t> bytes);

}  // namespace cwxeditor::cwx::editor::gui::dwt::webp
