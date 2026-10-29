#pragma once
#include "graphics.hpp"
#include <memory>
#include <functional>
namespace cwxeditor::lib {
// 場面画像とカード画像の補助処理で、見出しのラスタライズ規則を共有する。バックエンド
// はフォントの計測とグリフ画素を提供し、縮小・縁取り・サイズ変更は共通処理で行う。
struct ImageTextStyle {
    FontSpec font;
    Color foreground{0,0,0,255};
    Color outline{0,0,0,0};
    float shrink_width{};
    bool smoothing{};
    // TextCellのAAでは倍のフォントを選び、両マスクを元のフォン
    // トの範囲へ縮小する。通常のカード見出しは倍率1で幅だけを調整する。
    float glyph_scale{1};
    std::optional<Size> target_size;
    std::function<bool()> current;
    // Dのカード名描画は、滑らかなサイズ変更が指定されてもAAなしのフォ
    // ントを選ぶ。状態・個数表示や明示的なGCアダプターは、この既定規
    // 則を変えずに、渡されたネイティブフォントのAA設定を維持できる。
    bool preserve_font_antialias{};
};
[[nodiscard]] ImageFrame rasterize_image_text(std::shared_ptr<GraphicsBackend> graphics,
    std::string text, ImageTextStyle style);
} // namespace cwxeditor::lib
