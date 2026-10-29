#pragma once
#include "types.hpp"
#include <functional>
#include <memory>

namespace cwxeditor::lib {
// 指定順の画像レイヤー。単位はUI文字の倍率に追従しない。
struct ImageLayerSpec {
    ResourceId image{};
    std::size_t frame{};
    Size logical_size;
    Point offset;
    bool centered{true};
    bool shrink_to_fit{};
    Interpolation interpolation{Interpolation::Linear};
    // 通常のUI文字の倍率とは独立した、明示的な画像の設計倍率
    // 。保持した資源の本来の論理サイズを解決した後に適用する。
    float scale{1.0F};
};
struct ImageViewportPresentation {
    Rect outer;
    Rect content;
    bool recessed_border{};
};

// 論理的な座標情報だけを扱う。ImageViewの枠は固定
// の1画素の内側余白とし、UI文字の倍率には追従させない。
[[nodiscard]] ImageViewportPresentation prepare_image_viewport(
    Rect bounds, bool border) noexcept;

struct ImageBlitPresentation {
    Rect destination;
    std::optional<Rect> source;
};
// 描画バックエンドから独立した、変更不可の画像世代と座標情報
// 。タイルの座標は逐次生成し、画素ごとに命令を確保しない。
struct ImagePresentation {
    std::shared_ptr<const ImageData> image;
    std::size_t frame{};
    Interpolation interpolation{Interpolation::Linear};
    std::optional<ImageBlitPresentation> single;
    std::optional<Rect> tiled_bounds;
    Size tile_size;
    void for_each_blit(const std::function<void(const ImageBlitPresentation&)>& draw) const;
};
[[nodiscard]] ImagePresentation prepare_image_presentation(
    std::shared_ptr<const ImageData> image, std::size_t requested_frame,
    Rect bounds, ImageScaleMode mode, Interpolation interpolation);
[[nodiscard]] ImagePresentation prepare_image_layer_presentation(
    std::shared_ptr<const ImageData> image, const ImageLayerSpec& layer, Rect bounds);
} // namespace cwxeditor::lib
