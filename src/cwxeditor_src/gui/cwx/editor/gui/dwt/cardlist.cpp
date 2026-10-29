#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardlist.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace cwxeditor::cwx::editor::gui::dwt::cardlist {

namespace {

[[nodiscard]] std::size_t nextUtf8Boundary(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) return text.size();
    ++offset;
    while (offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U)
        ++offset;
    return offset;
}

[[nodiscard]] int measuredWidth(std::string_view text,
                                const TextMeasure& measure_text) {
    return static_cast<int>(measure_text(text).width);
}

}  // namespace

std::string cutText(std::string_view name, TextMeasure measure_text, int max_width,
                    std::string_view dots, bool include_last_char) {
    if (name.empty()) return {};
    if (!measure_text) return std::string(name);
    if (max_width <= 0) return std::string(dots);
    if (measuredWidth(name, measure_text) <= max_width) return std::string(name);

    const auto dots_width = measuredWidth(dots, measure_text);
    if (dots_width >= max_width) return std::string(dots);

    std::size_t low = 0U;
    std::size_t high = name.size();
    while (low < high) {
        const auto middle = low + (high - low + 1U) / 2U;
        const auto boundary = [&] {
            std::size_t current = 0U;
            for (std::size_t count = 0U; count < middle && current < name.size(); ++count)
                current = nextUtf8Boundary(name, current);
            return current;
        }();
        if (measuredWidth(name.substr(0U, boundary), measure_text) + dots_width <= max_width)
            low = middle;
        else
            high = middle - 1U;
    }

    std::size_t boundary = 0U;
    for (std::size_t count = 0U; count < low && boundary < name.size(); ++count)
        boundary = nextUtf8Boundary(name, boundary);
    std::string result{name.substr(0U, boundary)};
    if (include_last_char && boundary < name.size())
        result.append(name.substr(boundary, nextUtf8Boundary(name, boundary) - boundary));
    result.append(dots);
    return result;
}

std::string cutText(std::string_view name, GraphicsContext& context, int max_width,
                    std::string_view dots, bool include_last_char, lib::FontSpec font) {
    return cutText(name, [&context, font](const std::string_view value) {
        return context.measure_text(value, font, 0.0F);
    }, max_width, dots, include_last_char);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardlist
