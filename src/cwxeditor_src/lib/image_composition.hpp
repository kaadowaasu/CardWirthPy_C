#pragma once
#include "types.hpp"
#include "drawing_clip.hpp"

namespace cwxeditor::lib {
enum class ImageLayerScale { Cut, Scale, Center };
struct ImageLayerInsets {
    std::int32_t top{},right{},bottom{},left{};
};
struct ImageLayerRequest {
    std::uint32_t canvas_width{},canvas_height{},source_width{},source_height{};
    std::uint32_t target_scale{1U},source_scale{1U};
    ImageLayerInsets insets{};
    ImageLayerScale mode{ImageLayerScale::Cut};
};
// 画像全体を基準とする中央配置と、入力倍率を正規化した合成位置を共有する。
[[nodiscard]] RasterClip plan_image_layer_destination(const ImageLayerRequest& request) noexcept;
// 非乗算アルファのRGBA/BGRA画素をsource-overで合成す
// る。拡大縮小、マスク、フォント、ウィンドウ制御は扱わない。原点は出力先
// の画素格子に切り下げ、クリッピングは表示側と同じ画素矩形の規則を使う。
[[nodiscard]] bool composite_image_pixels(ImageFrame& destination,
    const ImageFrame& source, Point origin, Rect clip);
// 矩形の境界を中心とする実線を描き、対象画素ごとにRGBを一度だけXORする。出力先のア
// ルファは維持する。バックエンドは読み出し可能な画素を提供し、XORの規則は持たない。
[[nodiscard]] bool xor_rectangle_pixels(ImageFrame& destination,Rect rectangle,
    Color foreground,float width);
// 境界を中心とするXOR線の範囲を正確に覆う、重なりのない整数座標の帯。ネイティブアダ
// プターは、クリッピングやゼロサイズの辺も含め、この範囲の画素だけを読み戻せばよい。
[[nodiscard]] std::vector<RasterClip> xor_rectangle_regions(Rect rectangle,float width,RasterClip clip);
} // namespace cwxeditor::lib
