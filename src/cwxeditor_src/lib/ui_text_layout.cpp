#include "ui_text_layout.hpp"
#include "ui_metrics.hpp"
#include "graphics.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {

GcTextLayout layout_gc_text(DrawingContext& drawing, const std::string_view text,
                            const FontSpec& font, const GcTextOptions options) {
    GcTextLayout layout;
    layout.line_height = std::max(1.0F, drawing.measure_text("Ag", font, 0.0F).height);
    const bool has_tabs = options.expand_tabs &&
        text.find('\t') != std::string_view::npos;
    const float tab_stop = has_tabs
        ? std::max(1.0F, drawing.measure_text(" ", font, 0.0F).width * 8.0F)
        : 0.0F;
    const auto add_line = [&](const std::string_view line) {
        GcTextLine measured;
        const auto first_tab = options.expand_tabs
            ? line.find('\t') : std::string_view::npos;
        if (first_tab == std::string_view::npos) {
            if (!line.empty()) {
                measured.width = drawing.measure_text(line, font, 0.0F).width;
                measured.runs.push_back({std::string(line), 0.0F});
            }
        } else {
            float cursor = 0.0F;
            std::size_t begin = 0U;
            while (true) {
                const auto end = line.find('\t', begin);
                const auto segment = line.substr(begin,
                    end == std::string_view::npos ? std::string_view::npos : end - begin);
                const float width = segment.empty()
                    ? 0.0F
                    : std::ceil(drawing.measure_text(segment, font, 0.0F).width);
                if (!segment.empty()) measured.runs.push_back({std::string(segment), cursor});
                cursor += width;
                if (end == std::string_view::npos) break;
                cursor = (std::floor(cursor / tab_stop) + 1.0F) * tab_stop;
                begin = end + 1U;
            }
            measured.width = cursor;
        }
        layout.extent.width = std::max(layout.extent.width, measured.width);
        layout.lines.push_back(std::move(measured));
    };

    if (!options.delimiters) {
        add_line(text);
    } else {
        std::size_t begin = 0U;
        while (begin <= text.size()) {
            const auto end = text.find_first_of("\r\n", begin);
            add_line(text.substr(begin,
                end == std::string_view::npos ? std::string_view::npos : end - begin));
            if (end == std::string_view::npos) break;
            begin = end + 1U;
            if (text[end] == '\r' && begin < text.size() && text[begin] == '\n')
                ++begin;
        }
    }
    layout.extent.height = layout.line_height * static_cast<float>(layout.lines.size());
    return layout;
}

void draw_gc_text(DrawingContext& drawing, const GcTextLayout& layout,
                  const Point origin, const FontSpec& font, const Color color,
                  const std::optional<float> line_advance) {
    float y = origin.y;
    for (const auto& line : layout.lines) {
        for (const auto& run : line.runs)
            drawing.draw_text(run.text, {origin.x + run.x, y}, font, color);
        y += line_advance.value_or(layout.line_height);
    }
}

std::string ellipsize_gc_text(DrawingContext& drawing, const std::string_view text,
                              const FontSpec& font, const float width,
                              const std::string_view suffix) {
    if (text.empty() || width <= 0.0F) return {};
    if (layout_gc_text(drawing, text, font).extent.width <= width)
        return std::string(text);
    if (layout_gc_text(drawing, suffix, font).extent.width > width) return {};

    std::vector<std::size_t> boundaries{0U};
    auto remaining = text;
    std::size_t offset = 0U;
    while (!remaining.empty()) {
        const auto before = remaining.size();
        (void)utf8::consume_scalar(remaining);
        offset += before - remaining.size();
        boundaries.push_back(offset);
    }
    for (auto boundary = boundaries.size() - 1U; boundary > 0U; --boundary) {
        std::string candidate(text.substr(0U, boundaries[boundary - 1U]));
        candidate.append(suffix);
        if (layout_gc_text(drawing, candidate, font).extent.width <= width)
            return candidate;
    }
    return std::string(suffix);
}

std::optional<AlignedTextLayout> layout_aligned_text(Rect bounds,TextAlignment alignment,
    float padding,std::optional<Size> measured) noexcept {
    if(!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
       !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
       !std::isfinite(padding) || bounds.width<=0 || bounds.height<=0) return {};
    float x=bounds.x+padding,height=0;
    if(measured && std::isfinite(measured->width) && std::isfinite(measured->height) &&
       measured->width>=0 && measured->height>=0) {
        height=measured->height;
        if(alignment==TextAlignment::Center) x=bounds.x+std::max(0.0F,(bounds.width-measured->width)*.5F);
        else if(alignment==TextAlignment::End) x=bounds.x+std::max(padding,bounds.width-measured->width-padding);
    }
    // アイコンと文字の行では、文字の中央を上下の中央に合わせる。
    // 最低2pxの余白を強制すると、行高に近い文字がアイコンの
    // 中心より2～3px下へずれるため、通常は設けない。 本当
    // に大きすぎるラベルだけは、従来の上端＋2pxで切り取る。
    const float slack=bounds.height-std::max(1.0F,height);
    const float y=slack>=-bounds.height*.25F ? bounds.y+slack*.5F : bounds.y+2.0F;
    if(!std::isfinite(x) || !std::isfinite(y)) return {};
    return AlignedTextLayout{{x,y},bounds};
}

std::optional<AlignedTextLayout> prepare_aligned_text(std::string_view text,Rect bounds,
    TextAlignment alignment,float padding,const FontSpec& effective_font) noexcept {
    const auto measured=measured_effective_ui_text_size(text,effective_font);
    auto result=layout_aligned_text(bounds,alignment,padding,measured);
    // アイコン横の文字は、字面の中心をアイコンの中心に合
    // わせる。 日本語フォントの字面はアセント・ディセン
    // トの中点より下に来るため、 中央配置した行をバック
    // エンドの字形中心の補正量だけずらす。 大きすぎる
    // ラベルは、従来の上寄せの切り取り位置を維持する。
    if(result && measured && bounds.height-measured->height>=-bounds.height*.25F)
        result->origin.y-=measured_effective_ui_text_ink_offset(effective_font);
    return result;
}

float text_edit_line_height(std::optional<float> glyph_height,const FontSpec* font) noexcept {
    const float height = glyph_height && std::isfinite(*glyph_height) && *glyph_height>0
        ? *glyph_height : editor_widget_font_points(font) * (4.0F/3.0F);
    // D版の FixedWidthText と同様、行送りはフォント寸法その
    // ものを使う。 フォーム用の追加行間で、メッセージの七行目を消費しない。
    return height + (font && font->content_font ? 0.0F : scaled_ui_metric(2));
}

LabelTextPresentation prepare_label_text(std::string_view text,Rect bounds,
    TextAlignment alignment,float padding,bool wrap,const FontSpec& source_font) {
    LabelTextPresentation result;
    if(!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
       !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
       !std::isfinite(padding)) return result;
    padding=std::max(0.0F,padding);
    // 字形の提供処理が、呼び出し元のラベルと文字列・FontSpecを破
    // 棄する場合がある。 最初の呼び出し前に、両入力を所有して保持する。
    const FontSpec font=source_font;
    TextEdit document;
    document.text=std::string(text); document.multiline=true; document.wrap=wrap;
    const auto glyph=measured_effective_ui_text_size("Mg",font);
    const float glyph_height=glyph ? glyph->height : font.points*(4.0F/3.0F);
    const float line_height=std::max(1.0F,glyph_height)+scaled_ui_metric(2);
    const float font_ratio=font.points/editor_widget_font_points();
    const TextWidthMeasure measure=[&](std::string_view value) {
        const auto size=measured_effective_ui_text_size(value,font);
        return size ? size->width : estimated_ui_text_width(value,0)*font_ratio;
    };
    const float inner_width=std::max(1.0F,bounds.width-padding*2);
    const auto lines=layout_text_lines(document,inner_width,measure);
    const float block_height=line_height*static_cast<float>(lines.size());
    const float top=bounds.y+std::max(scaled_ui_metric(2),(bounds.height-block_height)*.5F);
    result.extent.height=block_height+scaled_ui_metric(4);
    for(std::size_t i=0;i<lines.size();++i) {
        const auto& line=lines[i];
        result.extent.width=std::max(result.extent.width,line.width+padding*2);
        float x=bounds.x+padding;
        if(alignment==TextAlignment::Center) x+=std::max(0.0F,(inner_width-line.width)*.5F);
        else if(alignment==TextAlignment::End) x+=std::max(0.0F,inner_width-line.width);
        result.lines.push_back({text_display_range(document,line.start,line.end),
            {{x,top+line_height*static_cast<float>(i)},bounds}});
    }
    return result;
}

Insets text_edit_insets() noexcept {
    Insets insets;
    insets.left = insets.right = scaled_ui_metric(5);
    insets.top = insets.bottom = scaled_ui_metric(3);
    return insets;
}

Size text_edit_scroll_gutters(const TextEdit& edit) noexcept {
    if (!edit.multiline) return {};
    return {edit.vertical_scroll ? editor_scrollbar_extent() : 0,
            edit.horizontal_scroll && !edit.wrap ? editor_scrollbar_extent() : 0};
}

Insets text_edit_insets(const TextEdit& edit) noexcept {
    return edit.zero_text_padding ? Insets{} : text_edit_insets();
}

Size text_edit_outer_size(const TextEdit& edit, const Size client_text_extent) noexcept {
    const auto extent=[](const float value) {
        return std::isfinite(value) ? std::max(0.0F,value) : 0.0F;
    };
    const auto insets=text_edit_insets(edit);
    const auto gutters=text_edit_scroll_gutters(edit);
    return {extent(client_text_extent.width)+insets.left+insets.right+gutters.width,
            extent(client_text_extent.height)+insets.top+insets.bottom+gutters.height};
}

Rect text_edit_inner_bounds(const TextEdit& edit) noexcept {
    const auto bounds = edit.bounds();
    const auto insets = text_edit_insets(edit);
    // 文字が収まっても指定されたバー領域を確保し、 一行増え
    // ただけで前の全段落の折り返しが変わらないようにする。
    const auto gutters = text_edit_scroll_gutters(edit);
    return {bounds.x + insets.left, bounds.y + insets.top,
            std::max(1.0F, bounds.width - insets.left - insets.right - gutters.width),
            std::max(1.0F, bounds.height - insets.top - insets.bottom - gutters.height)};
}

ItemScrollGeometry text_edit_scroll_geometry(const TextEdit& edit,
    std::span<const TextVisualLine> lines, float line_height,
    const TextEditInteractionState& state) {
    const auto inner = text_edit_inner_bounds(edit);
    ItemScrollGeometry result; result.viewport = inner;
    const bool horizontal=edit.horizontal_scroll && !edit.wrap;
    if (!edit.multiline || (!edit.vertical_scroll && !horizontal) || !std::isfinite(line_height) || line_height<=0)
        return result;
    const auto gutters = text_edit_scroll_gutters(edit);
    const auto padding=text_edit_insets(edit);
    if (edit.bounds().width<=padding.left+padding.right+gutters.width ||
        edit.bounds().height<=padding.top+padding.bottom+gutters.height) return result;
    const float extent = line_height * static_cast<float>(lines.size());
    float width=0;
    for (const auto& line:lines) width=std::max(width,line.width);
    // 行末のカーソル用の領域も含め、カーソルを見える位置へ移す処理・範囲制限と揃える。
    width+=scaled_ui_metric(2);
    result = pixel_scroll_geometry({inner.x,inner.y,inner.width+gutters.width,
        inner.height+gutters.height}, {width,extent},{state.scroll_x,state.scroll_y},
        horizontal,edit.vertical_scroll,true);
    return result;
}

TextComposition composition_from_characters(std::string text, int start, int length) {
    const auto advance = [&](std::size_t at, int count) {
        for (int n=0; n<count && at<text.size(); ++n) at=next_utf8_boundary(text,at);
        return at;
    };
    const auto first=start<0 ? text.size() : advance(0,start);
    const auto last=advance(first,std::max(0,length));
    return {std::move(text),{first,last-first},first};
}

void update_text_composition(TextEdit& edit, TextEditInteractionState& state,
    TextComposition composition, std::uint64_t revision) {
    synchronize_text_edit_state(edit,state);
    if (edit.read_only || !edit.enabled() || composition.text.empty()) {
        // 空の未確定文字は、キャンセル・確定をOSが通知したも
        // の。 ネイティブの変換状態を再び消す要求ではない。
        state.composition.reset(); return;
    }
    composition.selection.start=clamp_utf8_boundary(composition.text,composition.selection.start);
    const auto end=clamp_utf8_boundary(composition.text,composition.selection.start+
        std::min(composition.selection.length,composition.text.size()-composition.selection.start));
    composition.selection.length=end-composition.selection.start;
    composition.cursor=clamp_utf8_boundary(composition.text,composition.cursor);
    if (!state.composition || state.composition->focus_revision!=revision)
        state.composition=TextEditComposition{{},edit.text,{edit.selection_start,edit.selection_length},revision};
    state.composition->value=std::move(composition);
}

std::optional<TextCompositionView> project_text_composition(
    const TextEdit& edit, const TextEditInteractionState& state) {
    if (!state.composition || state.composition->document!=edit.text || edit.read_only || !edit.enabled()) return {};
    const auto& active=*state.composition;
    TextCompositionView view;
    view.edit.text=edit.text;
    view.edit.text.replace(active.replaced.start,active.replaced.length,active.value.text);
    view.edit.set_bounds(edit.bounds());
    view.edit.multiline=edit.multiline; view.edit.wrap=edit.wrap;
    view.edit.wrap_columns=edit.wrap_columns;
    view.edit.single_logical_line=edit.single_logical_line;
    view.edit.vertical_scroll=edit.vertical_scroll;
    view.edit.horizontal_scroll=edit.horizontal_scroll;
    view.edit.password=edit.password; view.edit.tab_width=edit.tab_width;
    view.edit.alignment=edit.alignment;
    view.state=state; view.state.composition.reset();
    view.state.caret=active.replaced.start+active.value.cursor;
    view.state.anchor=view.state.caret;
    if (!active.value.selection.empty()) {
        view.state.anchor=active.replaced.start+active.value.selection.start;
        view.state.caret=view.state.anchor+active.value.selection.length;
    }
    view.range={active.replaced.start,active.value.text.size()};
    return view;
}

std::string text_display_range(const TextEdit& edit, std::size_t start, std::size_t end) {
    start = clamp_utf8_boundary(edit.text, start);
    end = clamp_utf8_boundary(edit.text, std::max(start, end));
    if (edit.password)
        return std::string(utf8_codepoint_count(
            std::string_view(edit.text).substr(start, end - start)), '*');
    if (edit.tab_width == 0U || edit.text.find('\t', start) >= end)
        return edit.text.substr(start, end - start);

    std::size_t column{};
    const auto newline = start == 0U ? std::string::npos
                                    : edit.text.find_last_of("\r\n", start - 1U);
    const std::size_t line_start = newline == std::string::npos ? 0U : newline + 1U;
    for (std::size_t at = line_start; at < start;) {
        if (edit.text[at] == '\t') {
            column += edit.tab_width - column % edit.tab_width;
            ++at;
        } else {
            at = next_utf8_boundary(edit.text, at);
            ++column;
        }
    }

    std::string result;
    result.reserve(end - start);
    for (std::size_t at = start; at < end;) {
        if (edit.text[at] == '\t') {
            const auto spaces = edit.tab_width - column % edit.tab_width;
            result.append(spaces, ' ');
            column += spaces;
            ++at;
            continue;
        }
        const auto next = std::min(end, next_utf8_boundary(edit.text, at));
        result.append(edit.text, at, next - at);
        column = edit.text[at] == '\r' || edit.text[at] == '\n' ? 0U : column + 1U;
        at = next;
    }
    return result;
}

float measure_text_range(const TextEdit& edit, std::size_t start, std::size_t end,
                         const TextWidthMeasure& measure) {
    return measure ? std::max(0.0F, measure(text_display_range(edit, start, end))) : 0.0F;
}

namespace {
// at にあるUTF-8文字の、CardWirth（Shift_JIS）での桁幅。
std::size_t cw_text_columns(std::string_view text, std::size_t at) noexcept {
    const auto lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80U) return 1U;
    char32_t cp{};
    std::size_t length{};
    if ((lead & 0xE0U) == 0xC0U) { cp = lead & 0x1FU; length = 2U; }
    else if ((lead & 0xF0U) == 0xE0U) { cp = lead & 0x0FU; length = 3U; }
    else if ((lead & 0xF8U) == 0xF0U) { cp = lead & 0x07U; length = 4U; }
    else return 1U;
    for (std::size_t i = 1U; i < length && at + i < text.size(); ++i)
        cp = (cp << 6U) | (static_cast<unsigned char>(text[at + i]) & 0x3FU);
    // 半角カタカナはShift_JISでは一バイト。
    if (cp >= 0xFF61U && cp <= 0xFF9FU) return 1U;
    return 2U;
}
}  // namespace

std::vector<TextVisualLine> layout_text_lines(const TextEdit& edit, float wrap_width,
                                             const TextWidthMeasure& measure) {
    if (!edit.multiline)
        return {{0U, edit.text.size(), edit.text.size(),
                 measure_text_range(edit, 0U, edit.text.size(), measure)}};
    wrap_width = std::max(1.0F, wrap_width);
    std::vector<TextVisualLine> lines;
    std::size_t paragraph{};
    for (;;) {
        const auto newline = edit.text.find_first_of("\r\n", paragraph);
        const auto end = newline == std::string::npos ? edit.text.size() : newline;
        auto next = end;
        if (newline != std::string::npos) {
            next = newline + 1U;
            if (edit.text[newline] == '\r' && next < edit.text.size() && edit.text[next] == '\n')
                ++next;
        }
        if (!edit.wrap || paragraph == end) {
            lines.push_back({paragraph, end, next,
                measure_text_range(edit, paragraph, end, measure)});
        } else {
            std::size_t start = paragraph;
            while (start < end && edit.wrap_columns != 0U) {
                // 桁数による折り返し（CardWirthのメッセージの規則）。
                std::size_t stop = start, columns{};
                while (stop < end) {
                    const auto candidate = next_utf8_boundary(edit.text, stop);
                    const auto add = cw_text_columns(edit.text, stop);
                    if (stop != start && columns + add > edit.wrap_columns) break;
                    columns += add;
                    stop = candidate;
                }
                lines.push_back({start, stop, stop,
                                 measure_text_range(edit, start, stop, measure)});
                start = stop;
            }
            while (start < end) {
                std::size_t stop = start;
                float width{};
                while (stop < end) {
                    const auto candidate = next_utf8_boundary(edit.text, stop);
                    const float candidate_width = measure_text_range(edit, start, candidate, measure);
                    if (candidate_width > wrap_width && stop != start) break;
                    stop = candidate;
                    width = candidate_width;
                    if (candidate_width > wrap_width) break;
                }
                lines.push_back({start, stop, stop, width});
                start = stop;
            }
            lines.back().next_start = next;
        }
        if (newline == std::string::npos) break;
        paragraph = next;
        // 次の走査で、改行後の空の表示行を保持する。
    }
    return lines;
}

std::size_t text_visual_line_for_offset(std::span<const TextVisualLine> lines,
                                        std::size_t offset) noexcept {
    std::size_t selected{};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (offset < lines[i].start) break;
        selected = i;
        if (offset < lines[i].end) break;
        if (offset == lines[i].end && (i + 1U == lines.size() || lines[i + 1U].start != offset))
            break;
    }
    return selected;
}

Point text_visual_line_origin(const TextEdit& edit, const TextVisualLine& line,
    std::size_t index, Rect inner, float line_height,
    const TextEditInteractionState& state) noexcept {
    float x = inner.x - state.scroll_x;
    if (state.scroll_x <= 0.0F && line.width < inner.width) {
        if (edit.alignment == TextAlignment::Center) x += (inner.width - line.width) * 0.5F;
        else if (edit.alignment == TextAlignment::End) x += inner.width - line.width;
    }
    const float y = edit.multiline
        ? inner.y + line_height * static_cast<float>(index) - state.scroll_y
        : inner.y + std::max(0.0F, (inner.height - line_height) * 0.5F);
    return {x, y};
}

std::size_t text_visual_line_hit(const TextEdit& edit, const TextVisualLine& line,
                                 float x, const TextWidthMeasure& measure) {
    if (x <= 0.0F) return line.start;
    std::size_t previous = line.start;
    float previous_width{};
    for (auto position = line.start; position < line.end;) {
        const auto next = next_utf8_boundary(edit.text, position);
        const float width = measure_text_range(edit, line.start, next, measure);
        if (x < (previous_width + width) * 0.5F) return previous;
        previous = position = next;
        previous_width = width;
    }
    return line.end;
}

std::size_t text_edit_hit(const TextEdit& edit, std::span<const TextVisualLine> lines,
    Rect inner, float line_height, const TextEditInteractionState& state, Point point,
    const TextWidthMeasure& measure) {
    if (lines.empty()) return 0U;
    const auto index = edit.multiline ? static_cast<std::size_t>(std::clamp(
        std::floor((point.y - inner.y + state.scroll_y) / std::max(1.0F, line_height)),
        0.0F, static_cast<float>(lines.size() - 1U))) : 0U;
    const auto origin = text_visual_line_origin(edit, lines[index], index, inner, line_height, state);
    return text_visual_line_hit(edit, lines[index], point.x - origin.x, measure);
}

void clamp_text_edit_scroll(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height) noexcept {
    float widest{};
    for (const auto& line : lines) widest = std::max(widest, line.width);
    const float caret_space = scaled_ui_metric(2.0F);
    state.scroll_x = edit.multiline && edit.wrap ? 0.0F : std::clamp(state.scroll_x,
        0.0F, std::max(0.0F, widest + caret_space - inner.width));
    state.scroll_y = edit.multiline ? std::clamp(state.scroll_y, 0.0F,
        std::max(0.0F, line_height * static_cast<float>(lines.size()) - inner.height)) : 0.0F;
}

bool scroll_text_edit_by(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height, Point delta) noexcept {
    const Point before{state.scroll_x, state.scroll_y};
    state.scroll_x += delta.x;
    state.scroll_y += delta.y;
    clamp_text_edit_scroll(edit, state, lines, inner, line_height);
    return before.x != state.scroll_x || before.y != state.scroll_y;
}

void reveal_text_edit_caret(const TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    const TextWidthMeasure& measure) {
    if (lines.empty()) return;
    const auto index = text_visual_line_for_offset(lines, state.caret);
    const float x = measure_text_range(edit, lines[index].start,
        std::clamp(state.caret, lines[index].start, lines[index].end), measure);
    if (!edit.multiline || !edit.wrap) {
        if (x < state.scroll_x) state.scroll_x = x;
        else if (x > state.scroll_x + inner.width - scaled_ui_metric(2.0F))
            state.scroll_x = x - inner.width + scaled_ui_metric(2.0F);
    }
    if (edit.multiline) {
        const float y = line_height * static_cast<float>(index);
        if (y < state.scroll_y) state.scroll_y = y;
        else if (y + line_height > state.scroll_y + inner.height)
            state.scroll_y = y + line_height - inner.height;
    }
    clamp_text_edit_scroll(edit, state, lines, inner, line_height);
}

bool move_text_caret_by_lines(TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, int delta, bool extend, const TextWidthMeasure& measure) {
    if (lines.empty()) return false;
    synchronize_text_edit_state(edit, state);
    const auto index = text_visual_line_for_offset(lines, state.caret);
    const auto valid_preferred_range = [&] {
        return state.preferred_line_start <= state.preferred_line_end &&
               state.preferred_line_end <= edit.text.size() &&
               clamp_utf8_boundary(edit.text, state.preferred_line_start) ==
                   state.preferred_line_start &&
               clamp_utf8_boundary(edit.text, state.preferred_line_end) ==
                   state.preferred_line_end;
    };
    if (state.preferred_x < 0.0F || !valid_preferred_range()) {
        state.preferred_line_start = lines[index].start;
        state.preferred_line_end = std::clamp(state.caret, lines[index].start, lines[index].end);
    }
    // 元の行の先頭からの幅を計測し続ける。短い行へ移っても置き換え
    // ず、 フォント・倍率が変わったらキャッシュしたxを更新する。
    state.preferred_x = measure_text_range(edit, state.preferred_line_start,
        state.preferred_line_end, measure);
    const auto target = static_cast<std::size_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(index) + delta, 0, static_cast<std::int64_t>(lines.size() - 1U)));
    return set_text_caret(edit, state,
        text_visual_line_hit(edit, lines[target], state.preferred_x, measure), extend);
}

void drag_text_selection_to(TextEdit& edit, TextEditInteractionState& state,
    std::span<const TextVisualLine> lines, Rect inner, float line_height,
    Point point, const TextWidthMeasure& measure) {
    synchronize_text_edit_state(edit, state);
    if (edit.multiline) {
        if (point.y < inner.y) state.scroll_y -= line_height;
        else if (point.y > inner.y + inner.height) state.scroll_y += line_height;
    }
    if (!edit.multiline || !edit.wrap) {
        const float step = scaled_ui_metric(12.0F);
        if (point.x < inner.x) state.scroll_x -= step;
        else if (point.x > inner.x + inner.width) state.scroll_x += step;
    }
    clamp_text_edit_scroll(edit, state, lines, inner, line_height);
    state.caret = text_edit_hit(edit, lines, inner, line_height, state, point, measure);
    state.preferred_x = -1.0F;
    commit_text_edit_state(edit, state);
    reveal_text_edit_caret(edit, state, lines, inner, line_height, measure);
}

Rect text_edit_caret_bounds(const TextEdit& edit, std::span<const TextVisualLine> lines,
    Rect inner, float line_height, const TextEditInteractionState& state,
    const TextWidthMeasure& measure) {
    if (lines.empty()) return {inner.x, inner.y, scaled_ui_metric(1.0F), line_height};
    const auto index = text_visual_line_for_offset(lines, state.caret);
    const auto& line = lines[index];
    const auto origin = text_visual_line_origin(edit, line, index, inner, line_height, state);
    return {origin.x + measure_text_range(edit, line.start,
                std::clamp(state.caret, line.start, line.end), measure),
            origin.y, scaled_ui_metric(1.0F), line_height};
}

namespace {
bool prepare_text_edit_key(TextEdit& edit, TextEditInteractionState& state,
    Key key, Modifiers modifiers, Rect inner, float line_height,
    const TextWidthMeasure& measure, const TextClipboardAccess& clipboard) {
    if (!edit.enabled()) return false;
    synchronize_text_edit_state(edit, state);
    const auto bits = static_cast<std::uint32_t>(modifiers);
    const bool command = (bits & static_cast<std::uint32_t>(Modifiers::Control | Modifiers::Meta)) != 0U;
    const bool extend = (bits & static_cast<std::uint32_t>(Modifiers::Shift)) != 0U;
    const auto reveal = [&] {
        const auto lines = layout_text_lines(edit, inner.width, measure);
        reveal_text_edit_caret(edit, state, lines, inner, line_height, measure);
    };
    if (command && key == Key::A) {
        (void)select_all_text(edit, state);
    } else if (command && (key == Key::C || key == Key::X)) {
        // クリップボードへの書き込み失敗で、選択テキストを失わないようにする。
        if (!edit.password && (key == Key::C || !edit.read_only) &&
            edit.selection_length != 0U && clipboard.write &&
            clipboard.write(std::string_view(edit.text).substr(
                edit.selection_start, edit.selection_length)) && key == Key::X)
            (void)replace_text_selection(edit, state, {});
    } else if (command && key == Key::V) {
        if (!edit.read_only && clipboard.read) {
            if (auto value = clipboard.read()) {
                // 複数行の編集欄は改行を保持する。 一行欄では、CRLF
                // も含め、各改行区切りを一つの空白として受け付ける。
                if (!edit.multiline || edit.single_logical_line) {
                    std::string single_line;
                    for (std::size_t i = 0; i < value->size(); ++i) {
                        const char ch = (*value)[i];
                        if (ch == '\r' && i + 1U < value->size() && (*value)[i + 1U] == '\n') ++i;
                        single_line += ch == '\r' || ch == '\n' ? ' ' : ch;
                    }
                    *value = std::move(single_line);
                }
                (void)replace_text_selection(edit, state, *value);
            }
        }
    } else if (key == Key::Backspace || key == Key::Delete) {
        if (!edit.read_only) {
            if (key == Key::Backspace) (void)erase_text_backward(edit, state);
            else (void)erase_text_forward(edit, state);
        }
    } else if (key == Key::Home || key == Key::End || key == Key::Left || key == Key::Right ||
               key == Key::Up || key == Key::Down || key == Key::PageUp || key == Key::PageDown) {
        const auto lines = layout_text_lines(edit, inner.width, measure);
        const auto& current = lines[text_visual_line_for_offset(lines, state.caret)];
        if (key == Key::Home || key == Key::End || key == Key::Left || key == Key::Right) {
            const auto movement = key == Key::Left ? TextCaretMove::CharacterBackward
                : key == Key::Right ? TextCaretMove::CharacterForward
                : key == Key::Home && command ? TextCaretMove::DocumentStart
                : key == Key::End && command ? TextCaretMove::DocumentEnd
                : key == Key::Home ? TextCaretMove::LineStart : TextCaretMove::LineEnd;
            (void)move_text_caret(edit, state, movement, current.start, current.end, extend);
        } else {
            const int delta = key == Key::Up ? -1 : key == Key::Down ? 1 :
                std::max(1, static_cast<int>(std::floor(inner.height / std::max(1.0F, line_height)))) *
                (key == Key::PageUp ? -1 : 1);
            (void)move_text_caret_by_lines(edit, state, lines, delta, extend, measure);
        }
    } else if (key == Key::Enter) {
        if (!edit.multiline || edit.single_logical_line) return activate_text_enter(edit);
        if (!edit.read_only) (void)replace_text_selection(edit, state, "\n");
        // 読み取り専用・最大長で挿入できなくても、Enterは消費する。
        // 複数行の編集中に、予期せずダイアログが確定されることを防ぐ。
    } else {
        return false;
    }
    reveal();
    return true;
}
bool same_text_state(const TextEditInteractionState& a, const TextEditInteractionState& b) {
    if (a.scroll_x != b.scroll_x || a.scroll_y != b.scroll_y || a.anchor != b.anchor ||
        a.caret != b.caret || a.preferred_x != b.preferred_x || a.initialized != b.initialized ||
        a.preferred_line_start != b.preferred_line_start || a.preferred_line_end != b.preferred_line_end ||
        a.composition_reset_serial != b.composition_reset_serial ||
        a.composition.has_value() != b.composition.has_value()) return false;
    if (!a.composition) return true;
    const auto& x = *a.composition; const auto& y = *b.composition;
    return x.document == y.document && x.focus_revision == y.focus_revision &&
        x.replaced.start == y.replaced.start && x.replaced.length == y.replaced.length &&
        x.value.text == y.value.text && x.value.cursor == y.value.cursor &&
        x.value.selection.start == y.value.selection.start && x.value.selection.length == y.value.selection.length;
}
bool same_text_input(const TextEdit& a, const TextEdit& b) {
    const auto x = a.bounds(), y = b.bounds();
    const auto& f = a.font(); const auto& g = b.font();
    if (f.has_value() != g.has_value()) return false;
    const bool same_font = !f || (f->family == g->family && f->points == g->points &&
        f->weight == g->weight && f->italic == g->italic && f->underline == g->underline &&
        f->strikeout == g->strikeout && f->size_mode==g->size_mode);
    return a.text == b.text && a.selection_start == b.selection_start && a.selection_length == b.selection_length &&
        a.selection_reversed == b.selection_reversed &&
        a.enabled() == b.enabled() && a.visible() == b.visible() && a.read_only == b.read_only &&
        a.multiline == b.multiline && a.password == b.password && a.tab_width == b.tab_width &&
        a.max_length == b.max_length &&
        a.wrap == b.wrap && a.wrap_columns == b.wrap_columns &&
        a.alignment == b.alignment && a.vertical_scroll == b.vertical_scroll &&
        a.horizontal_scroll == b.horizontal_scroll && x.x == y.x && x.y == y.y &&
        x.width == y.width && x.height == y.height && same_font;
}
} // namespace

TextEditSnapshot::TextEditSnapshot(const TextEdit& source, const TextEditInteractionState& interaction)
    : edit(source), state(interaction) {}

bool TextEditSnapshot::matches(const TextEdit& source, const TextEditInteractionState& interaction) const {
    return same_text_input(source, edit) && same_text_state(interaction, state);
}

bool dispatch_text_edit_key(TextEdit& edit, TextEditInteractionState& state,
    Key key, Modifiers modifiers, Rect inner, float line_height,
    const TextWidthMeasure& measure, const TextClipboardAccess& clipboard, const TextEditKeyGuard& guard) {
    if (guard.committed) *guard.committed = false;
    if (guard.live && !guard.live()) return true;
    if (!edit.enabled()) return false;
    synchronize_text_edit_state(edit, state);
    const TextEditSnapshot snapshot(edit, state);
    const auto& before = snapshot.edit;
    TextEdit prepared = before;
    auto prepared_state = state;
    bool changed{},entered{};
    prepared.on_changed = [&](std::string_view) {changed = true;};
    prepared.on_enter = before.on_enter ? Action{[&] {entered = true;}} : Action{};
    // クリップボードの文字列と計測は、この所有する表示用データだ
    // けを参照する。 例外時は、元の未確定入力・選択を変えない。
    const bool handled = prepare_text_edit_key(prepared, prepared_state, key, modifiers,
        inner, line_height, measure, clipboard);
    const auto current = [&] {
        return (!guard.live || guard.live()) && snapshot.matches(edit, state);
    };
    if (!current()) return true;
    if (!handled) return false;
    if (guard.accepts && !guard.accepts(prepared.text)) return true;
    if (!current()) return true;
    const auto notify = before.on_changed;
    const auto enter = before.on_enter;
    // コールバック前に文字列とカーソルを両方確定する。 通知が例外
    // を出す・別の編集を始める場合も含め、その後は状態を書かない。
    edit.text = std::move(prepared.text);
    state = std::move(prepared_state);
    commit_text_edit_state(edit, state);
    if (guard.committed) *guard.committed = true;
    const auto published_text = edit.text;
    if (changed && notify) notify(published_text);
    if (entered && enter) enter();
    return true;
}

}  // namespace cwxeditor::lib
