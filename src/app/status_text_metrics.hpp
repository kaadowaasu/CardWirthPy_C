#pragma once

#include <SDL3_ttf/SDL_ttf.h>
#include "cw/sprite/statusbar.hpp"
#include <algorithm>

namespace cardwirth::app {

// すべてのプラットフォームでフォント処理にSDL_ttfを使う。 レイアウトは
// 共通のDesc実装で行い、Pyと同様に文字列サイズと高さを別々に取得する。
inline std::optional<::cw::sprite::statusbar::StatusBarHostServices::TextMetrics>
measure_status_text(TTF_Font* font, const ::cw::lib::String& text,
                    ::cw::lib::Integer points, bool bold) {
    if (!font) return std::nullopt;
    const float previous_size = TTF_GetFontSize(font);
    const auto previous_style = TTF_GetFontStyle(font);
    if (!TTF_SetFontSize(font, static_cast<float>(std::clamp<::cw::lib::Integer>(points, 1, 256))))
        return std::nullopt;
    const auto style = static_cast<TTF_FontStyleFlags>(bold
        ? previous_style | TTF_STYLE_BOLD : previous_style & ~TTF_STYLE_BOLD);
    TTF_SetFontStyle(font, style);
    int width = 0, height = 0;
    const bool measured = TTF_GetStringSize(font, text.data(), text.size(), &width, &height);
    const int font_height = TTF_GetFontHeight(font);
    TTF_SetFontStyle(font, previous_style);
    TTF_SetFontSize(font, previous_size);
    if (!measured) return std::nullopt;
    return ::cw::sprite::statusbar::StatusBarHostServices::TextMetrics{
        {width, height}, font_height};
}

} // namespace cardwirth::app
