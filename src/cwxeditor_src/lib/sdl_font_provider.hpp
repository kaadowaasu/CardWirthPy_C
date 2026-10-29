#pragma once

#include <optional>

#include "types.hpp"

#include <memory>
#include <string_view>

struct TTF_Font;

namespace cwxeditor::lib {

// GraphicsBackend、Canvas、通常のコントロールで共有するSDLグリフサービス。ここではUI
// 倍率を適用しない。呼び出し側はWidgetのフォントに倍率を適用し、制作された画像などのサイズは変えない。
class SdlFontProvider final {
public:
    SdlFontProvider();
    ~SdlFontProvider();
    SdlFontProvider(const SdlFontProvider&) = delete;
    SdlFontProvider& operator=(const SdlFontProvider&) = delete;

    // 借用した変更不可のフォント。プロバイダーの作成スレッドでだけ
    // 使用する。UI倍率が変わっても、プロバイダーの破棄まで有効。
    [[nodiscard]] TTF_Font* font_for(const FontSpec& font);
    // ワーカーからも安全に使用できる。ワーカーでの計測は、そのスレッドで専用
    // のフォントを開閉し、描画側と同じ解像度・スタイルの規則を使う。wrap_width
    // が0以下ならDrawingContext::draw_text
    // と同じ単一行。正の幅では単語の境界と明示的な改行で折り返す。
    [[nodiscard]] Size measure_text(std::string_view text, const FontSpec& font,
                                    float wrap_width);
    [[nodiscard]] FontMetrics font_metrics(const FontSpec& font);
    [[nodiscard]] std::optional<GlyphPath> glyph_path(char32_t scalar,const FontSpec& font);
    [[nodiscard]] static int wrap_pixels(float width) noexcept;
    // 代表的な日本語グリフ「日」の描画部分の中心が、フォントの行矩
    // 形の中心より何px下にあるかを返す。作成スレッドでだけ使用
    // でき、ほかのスレッドではnullopt（補正なし）を返す。
    [[nodiscard]] std::optional<float> ink_center_offset(const FontSpec& font);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cwxeditor::lib
