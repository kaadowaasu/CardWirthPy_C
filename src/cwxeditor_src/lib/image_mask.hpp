#pragma once

#include "types.hpp"
#include <span>

namespace cwxeditor::lib {

// PNGのヘッダーとチャンクのメタデータだけを調べる。画素は展開せず、妥当性の最終判断
// はデコーダーに任せる。メタデータが不正または途中で切れている場合はUnknown。
ImageSourceTransparency png_source_transparency(std::span<const std::byte> encoded);
bool apply_image_color_key(ImageFrame& frame, Color key);
void apply_image_color_key(ImageData& image, Color key);

// 座標は復号した入力元の画素を指し、表示用に拡大縮小した画素ではない。拡大縮小
// 、フィルター、アップロードの前に実行する。透過色以外のアルファを保持し、RGB/Gray
// にはアルファチャンネルを追加する。不正なフレームは変更しない。
bool apply_image_color_key(ImageFrame& frame, std::int64_t x = 0,
                           std::int64_t y = 0,
                           bool preserve_explicit_alpha = false);
void apply_image_color_key(ImageData& image, std::int64_t x = 0,
                           std::int64_t y = 0,
                           bool preserve_explicit_alpha = false);
// Canvasバックエンドは画像ごとのGCアルファ設定を持たず、RGBAのアップロー
// ドを受け取る。この不透明度を、不変の入力画素からアップロード前に一度だけ適用する。
void apply_image_opacity(ImageData& image, std::uint8_t opacity);
void set_image_alpha(ImageData& image, std::uint8_t alpha);

}  // namespace cwxeditor::lib
