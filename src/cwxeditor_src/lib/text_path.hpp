#pragma once
#include "graphics.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace cwxeditor::lib {
// Dのimages.dで指定されたInlineの描画規則。バックエンドはグリフの輪郭と
// 一つのUnicodeスカラー値の描画寸法を提供し、線・塗り・配置の規則は選ばない。
struct InlineTextStyle {
    FontSpec font;
    Color foreground, border;
    float border_width{1}, line_height{1};
    int decoration_width{1}, underline{}, strike{};
    bool border_antialias{};
    std::function<bool()> current;
    // DのImage/GCは、出力先のalphaDataマスクを維持してRGBを更新する。単体版のInline
    // 文字描画はこれを明示的に選び、ほかの呼び出し側はRGBAのsource-overを使う。
    bool preserve_foreground_alpha{};
};
// nulloptはプロバイダーが輪郭取得に対応せず、共通の代替処理を使うことを表す。空
// のフレームは破棄済みまたは無効な要求を表し、処理を継続してよいという意味ではない。
[[nodiscard]] std::optional<ImageFrame> rasterize_inline_text(
    std::shared_ptr<GraphicsBackend> graphics, const std::vector<std::string>& lines,
    Size extent, InlineTextStyle style);
} // namespace cwxeditor::lib
