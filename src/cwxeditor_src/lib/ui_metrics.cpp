#include "editor_diagnostics.hpp"
#include "cwxeditor_src/lib/ui_edit_preferences.hpp"
#include <chrono>
#include "ui_metrics.hpp"
#include "ui_layout.hpp"
#include "ui_number_edit.hpp"
#include "ui_actions.hpp"
#include "ui_text_layout.hpp"
#include "window_host.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cwxeditor::lib {
namespace {

std::atomic_uint32_t g_ui_text_scale_percent{100U};
std::atomic_uint64_t g_ui_text_scale_generation{1U};
std::atomic_bool g_number_edit_wheel_enabled{true};
std::atomic_bool g_combo_edit_popup_enabled{true};
std::atomic_bool g_tab_wheel_switch_enabled{true};
std::atomic_int g_editor_ime_mode{0};
thread_local UiTextMeasureScope* g_text_measure{};
thread_local std::uint64_t g_text_measure_failure_generation{};
thread_local bool g_measuring_text{};
thread_local bool g_dialog_font_layout{};

[[nodiscard]] float ui_scaled(const float value) noexcept {
    return value * editor_ui_text_scale();
}

struct LineHeightInput {
    float minimum{}, padding{}, fallback{};
};
LineHeightInput prepare_line_height(float minimum, float padding, const FontSpec* font) noexcept {
    const float ratio = font && std::isfinite(font->points)
        ? std::max(1.0F, font->points) / 12.0F : 1.0F;
    minimum = scaled_ui_metric(minimum);
    return {minimum, scaled_ui_metric(padding), std::max(minimum, minimum * ratio)};
}
float resolve_line_height(LineHeightInput input, const std::optional<Size>& glyph) noexcept {
    return glyph ? std::max(input.minimum, glyph->height + input.padding) : input.fallback;
}
ExpandItemMeasurement expand_item_scalar_input(const Widget& item) noexcept {
    ExpandItemMeasurement input;
    const auto* font = item.font() ? &*item.font() : nullptr;
    const auto header = prepare_line_height(kEditorUiMetrics.expand_header_height, 4, font);
    const auto caption = prepare_line_height(kEditorUiMetrics.group_caption_padding, 4, font);
    input.header_minimum = header.minimum; input.header_fallback = header.fallback;
    input.caption_minimum = caption.minimum; input.caption_fallback = caption.fallback;
    input.text_padding = header.padding;
    input.container = dynamic_cast<const Container*>(&item) != nullptr;
    if (const auto* group = dynamic_cast<const GroupBox*>(&item)) {
        input.group = true;
        const auto layout = group->layout();
        input.body_padding = layout ? layout->padding.top : kEditorUiMetrics.default_padding.top;
    }
    return input;
}
ExpandItemMetrics resolve_expand_item(LineHeightInput header_input, LineHeightInput caption_input,
    float body_padding, bool container, bool group, const std::optional<Size>& glyph) noexcept {
    const float header = resolve_line_height(header_input, glyph);
    const float caption = group ? std::max(body_padding, resolve_line_height(caption_input, glyph)) : 0.0F;
    return {header, container ? std::max(0.0F, header-caption) : 0.0F};
}

struct StripLayout {
    Size size{};
    std::vector<Rect> items;
};

StripLayout measure_strip(const std::vector<float>& widths,
                          const std::vector<float>& gaps, float available,
                          float inset, float row_height) {
    StripLayout result;
    const float room = std::max(0.0F, available - inset * 2.0F);
    float x = inset, y = inset;
    float right = inset;
    for (std::size_t index = 0; index < widths.size(); ++index) {
        const float width = std::min(room, widths[index]);
        if (x > inset && x + width > available - inset + .01F) {
            x = inset;
            y += row_height;
        }
        result.items.push_back({x, y, width, std::max(0.0F, row_height - inset * 2.0F)});
        right = std::max(right, x + width);
        x += width + gaps[index];
    }
    result.size = {right + inset, widths.empty() ? 0.0F : y + row_height - inset};
    return result;
}

[[nodiscard]] float scaled_tool_item_width(const ToolItem& item, const FontSpec* font,
                                           const float icon_scale) noexcept {
    const float width = preferred_tool_item_width(item, font);
    return item.icon && icon_scale > 1.0F ? width * icon_scale : width;
}

StripLayout measure_tools(const std::vector<ToolItem>& items,
                          const std::optional<FontSpec>& font, float width,
                          const float icon_scale = 1.0F) {
    std::vector<float> widths, gaps;
    float icon_height{};
    for (const auto& item : items) {
        const float item_width = item.separator ? ui_scaled(7.0F)
            : scaled_tool_item_width(item, font ? &*font : nullptr, icon_scale);
        widths.push_back(item_width);
        gaps.push_back(item.separator ? 0.0F : ui_scaled(1.0F));
        if (item.icon && !item.separator && !item.drop_down)
            icon_height = std::max(icon_height, item_width);
    }
    float row_height = ui_text_line_height(kEditorUiMetrics.toolbar_height, 4, font ? &*font : nullptr);
    // 拡大したアイコンボタンも正方形に保つ。
    if (icon_scale > 1.0F) row_height = std::max(row_height, icon_height);
    return measure_strip(widths, gaps, width, ui_scaled(1.0F), row_height);
}

StripLayout measure_tools(const ToolBar& toolbar, float width) {
    const auto items=toolbar.items;
    const auto font=toolbar.font();
    return measure_tools(items,font,toolbar.single_row ? std::numeric_limits<float>::max() : width,
                         toolbar.icon_scale);
}

StripLayout measure_menu(const Menu& menu, float width) {
    const auto items=menu.items;
    const auto font=menu.font();
    // 文字計測の呼び出し先がメニューを破棄しても、開始時の余白と高さで計測を完了する。
    const float padding = ui_scaled(menu.bar_padding);
    const float extra_height = ui_scaled(menu.bar_extra_height);
    std::vector<float> widths, gaps;
    for (const auto& item : items) {
        widths.push_back(item.separator ? 0.0F : preferred_menu_item_width(item, font ? &*font : nullptr));
        gaps.push_back(0.0F);
    }
    return measure_strip(widths, gaps, width, padding,
                         ui_text_line_height(kEditorUiMetrics.menu_height, 6, font ? &*font : nullptr) +
                             extra_height);
}

[[nodiscard]] Size preferred_ui_size_nested(
    WidgetPtr widget,
    std::unordered_set<const Widget*>& visiting, bool minimum = false,
    bool dialog_content = false);

// 処理単位の計測メモ（MeasurementCacheScope参照）。
struct MeasurementKey {
    const Widget* widget{};
    std::uint32_t flags{};
    float text_scale{};
    bool operator==(const MeasurementKey&) const = default;
};
struct MeasurementKeyHash {
    std::size_t operator()(const MeasurementKey& key) const noexcept {
        std::size_t value = std::hash<const void*>{}(key.widget);
        value ^= static_cast<std::size_t>(key.flags) * 0x9e3779b97f4a7c15ULL;
        value ^= std::hash<float>{}(key.text_scale) + 0x9e3779b9U + (value << 6) + (value >> 2);
        return value;
    }
};
struct MeasurementCache {
    int depth{};
    std::uint64_t generation{};
    std::unordered_map<MeasurementKey, Size, MeasurementKeyHash> values;
    bool profiling{};
    std::uint64_t calls{}, hits{}, misses{}, invalidations{};
    std::chrono::steady_clock::time_point started{};
};
thread_local MeasurementCache g_measurement_cache;

[[nodiscard]] Size preferred_ui_size_nested_uncached(
    WidgetPtr widget,
    std::unordered_set<const Widget*>& visiting, const bool minimum,
    const bool dialog_content) {
    if (!widget) return {};
    const DialogFontLayoutScope dialog_scope(widget->kind() == ComponentKind::Dialog || dialog_content);
    if (!visiting.insert(widget.get()).second) return {};
    if(const auto image=std::dynamic_pointer_cast<ImageView>(widget); image && image->design_size) {
        const auto size=*image->design_size;
        if(std::isfinite(size.width) && std::isfinite(size.height) && size.width>0 && size.height>0) {
            visiting.erase(widget.get());
            const auto frame=image->border?2.0F:0.0F;
            return {size.width+frame,size.height+frame};
        }
    }
    const auto item = widget->layout_item();
    float dialog_tab_header_width{};
    const auto authored_minimum_cap = [&](const float fallback,
                                           const float preferred) noexcept {
        if (!minimum || preferred <= 0.0F) return fallback;
        return std::min(fallback, ui_scaled(preferred));
    };
    const auto owned_font=widget->font();
    const auto* font = owned_font ? &*owned_font : nullptr;
    const auto canvas = std::dynamic_pointer_cast<Canvas>(widget);
    const auto canvas_lines = canvas ? canvas->ui_text_lines : 0;
    const auto scroll_view = std::dynamic_pointer_cast<ScrollView>(widget);
    const bool scroll_horizontal = scroll_view && scroll_view->horizontal_scroll;
    const bool scroll_vertical = scroll_view && scroll_view->vertical_scroll;
    const auto text_edit = std::dynamic_pointer_cast<TextEdit>(widget);
    const auto text_gutters = text_edit ? text_edit_scroll_gutters(*text_edit) : Size{};
    const auto container = std::dynamic_pointer_cast<Container>(widget);
    // ダイアログの範囲では、ALLで見出しが大きくなるネイティ
    // ブ部品を保護する。 全中間コンテナーにも同じ上書きを適用
    // すると、推奨幅が必須の最小幅となり、 400pxのダイア
    // ログに497pxの子を配置してしまう。 コンテナーは子の
    // フォント対応の最小値を再帰的に求めるため、 固定の推奨サ
    // イズへの追加の保護は、末端のコントロールだけに適用する。
    const bool wrapping_caption =
        (widget->kind() == ComponentKind::Label ||
         widget->kind() == ComponentKind::Link) &&
        std::dynamic_pointer_cast<TextWidget>(widget) &&
        std::dynamic_pointer_cast<TextWidget>(widget)->wrap;
    const bool font_sized_dialog = all_dialog_font_layout() &&
        (!container || widget->kind() == ComponentKind::GroupBox) &&
        !wrapping_caption && !(font && font->content_font);
    Size result{};
    switch (widget->kind()) {
    case ComponentKind::ToolBar: {
        const auto tools = std::static_pointer_cast<ToolBar>(widget);
        const auto items=tools->items;
        result = measure_tools(items, owned_font, std::numeric_limits<float>::max(),
                               tools->icon_scale).size;
        if (minimum) {
            // ツール欄は配置時に折り返す。最小幅として一行全体を要求すると
            // 、SplitView内の隣のフォームの領域を奪ってしまう。
            float widest{};
            for (const auto& tool : items)
                widest = std::max(widest, tool.separator ? ui_scaled(7.0F)
                    : scaled_tool_item_width(tool, font, tools->icon_scale));
            result.width = widest + ui_scaled(2.0F);
        }
        break;
    }
    case ComponentKind::MenuBar:
        result = measure_menu(*std::static_pointer_cast<Menu>(widget),
                              std::numeric_limits<float>::max()).size;
        break;
    case ComponentKind::Button:
    case ComponentKind::ToggleButton: {
        const auto control = std::dynamic_pointer_cast<TextWidget>(widget);
        const auto label = display_text_without_mnemonic(
            control ? control->text : std::string{});
        const auto button = std::dynamic_pointer_cast<Button>(widget);
        const bool has_icon = button && button->icon;
        const auto padding = button ? button->text_horizontal_padding : std::optional<float>{};
        result.width = ui_text_width(label, ui_scaled(padding ? std::max(0.0F, *padding) * 2.0F : 22.0F), font);
        if (has_icon)
            result.width += ui_scaled(18.0F);
        if (!padding) result.width = std::max(ui_scaled(30.0F), result.width);
        result.height = ui_scaled(kEditorUiMetrics.control_height);
        break;
    }
    case ComponentKind::CheckBox:
    case ComponentKind::RadioButton: {
        const auto control = std::dynamic_pointer_cast<TextWidget>(widget);
        const auto label = display_text_without_mnemonic(
            control ? control->text : std::string{});
        // ALL のチェックボックスは、箱＋2pxの間隔＋文字＋2pxとし、余分な空白を設けない。
        const bool compact_check = widget->kind() == ComponentKind::CheckBox &&
                                   editor_window_host_options().all_mode;
        result.width = std::max(ui_scaled(24.0F),
                                // ネイティブの印、その間隔、右のフォ
                                // ーカス余白を確保する。 従来の22px
                                // では、グリッドが自然幅を使ったと
                                // きに最後の日本語の字が欠ける。
                                ui_text_width(label, ui_scaled(compact_check ? 23.0F : 34.0F), font));
        result.height = ui_scaled(kEditorUiMetrics.control_height);
        break;
    }
    case ComponentKind::Label:
    case ComponentKind::Link: {
        const auto control = std::dynamic_pointer_cast<TextWidget>(widget);
        const auto text = control ? control->text : std::string{};
        const bool wrap = control && control->wrap;
        const auto alignment = control ? control->alignment : TextAlignment::Start;
        result.width = std::max(ui_scaled(4.0F), ui_text_width(
            text,
            ui_scaled(kEditorUiMetrics.label_horizontal_padding * 2.0F), font));
        result.height = ui_scaled(kEditorUiMetrics.label_height);
        if(wrap || text.find_first_of("\r\n")!=std::string::npos) {
            const float width=item.preferred.width>0 ? ui_scaled(item.preferred.width) : 1.0e9F;
            const auto paragraph=prepare_label_text(text,{0,0,width,0},alignment,
                ui_scaled(kEditorUiMetrics.label_horizontal_padding),wrap,
                effective_editor_widget_font(font));
            result=paragraph.extent;
            if(minimum && wrap) {
                result.width=std::min(result.width,ui_scaled(kEditorUiMetrics.flexible_minimum_width));
                result.height=ui_text_line_height(0,6,font);
            }
        }
        break;
    }
    case ComponentKind::NumberEdit: {
        const auto number = std::static_pointer_cast<NumberEdit>(widget);
        NumberEdit sample;
        sample.decimals = number->decimals;
        sample.value = number->value;
        const double lower = number->minimum, upper = number->maximum;
        float width = ui_text_width(format_number_value(sample), ui_scaled(24), font);
        for (const double value : {lower, upper}) {
            sample.value = value;
            width = std::max(width, ui_text_width(format_number_value(sample), ui_scaled(24), font));
        }
        result = {std::max(ui_scaled(48), width), ui_scaled(kEditorUiMetrics.control_height)};
        break;
    }
    case ComponentKind::ComboBox: {
        const auto& combo = *std::static_pointer_cast<ComboBox>(widget);
        // ポップアップは選択肢を計測するが、閉じた部
        // 品の固定論理幅では、大きい文字倍率で選択文
        // 字列が欠ける。 ネイティブ・SDL表示と同
        // じ共通寸法で矢印・フォーカスの領域を確保
        // し、 編集中の入力があればそれも含める。
        float text_width = combo.editable
            ? ui_text_width(combo.edit_text, ui_scaled(34.0F), font)
            : 0.0F;
        if (!(minimum && combo.minimum_ignores_items))
            for (const auto& choice : combo.items)
                text_width = std::max(text_width,
                    ui_text_width(choice.text, ui_scaled(34.0F), font));
        result = {std::max(ui_scaled(minimum ? 60.0F : 120.0F), text_width),
                  ui_scaled(kEditorUiMetrics.control_height)};
        break;
    }
    case ComponentKind::Separator:
        result = {1.0F, 1.0F};
        break;
    case ComponentKind::TabView: {
        const auto tabs = snapshot_tab_layout(*std::static_pointer_cast<TabView>(widget));
        const auto tab_font=tabs.font ? &*tabs.font : nullptr;
        const float horizontal_inset = ui_scaled(resolved_tab_horizontal_padding(tabs.chrome_inset));
        if ((dialog_content || font_sized_dialog) && tabs.header_visible &&
            !std::static_pointer_cast<TabView>(widget)->dialog_header_scroll) {
            // 設定ダイアログは全タブを表示できる幅を確保する。メインの可変タブ列は対象外。
            dialog_tab_header_width = horizontal_inset * 2.0F;
            for (const auto& page : tabs.pages)
                dialog_tab_header_width += preferred_tab_width(page, tab_font, tabs.header_horizontal_padding);
        }
        const float bottom_inset = ui_scaled(tabs.chrome_inset >= 0.0F ? tabs.chrome_inset : 2.0F);
        const float tab_chrome = tabs.header_visible
            ? ui_text_line_height(kEditorUiMetrics.tab_height, 4, tab_font) + bottom_inset +
                  ui_scaled(std::max(0.0F, tabs.header_vertical_extra)) * 2.0F
            : 0.0F;
        // コンパクトな指定のタブ欄に、汎用の高い文書表示領域を適
        // 用しない。 下の子の計測で、内容の欠けは引き続き防ぐ。
        result = minimum ? Size{ui_scaled(60.0F), tab_chrome}
                         : Size{ui_scaled(160.0F), item.preferred.height > 0.0F
                             ? ui_scaled(item.preferred.height)
                             : default_ui_height(widget->kind())};
        const auto selected = tabs.selected_index.value_or(0U);
        for (std::size_t index = 0; index < tabs.pages.size(); ++index) {
            if ((!dialog_content && !font_sized_dialog && index != selected) || !tabs.pages[index].content) continue;
            const auto page = preferred_ui_size_nested(tabs.pages[index].content, visiting,
                                                       minimum, dialog_content);
            result.width = std::max(result.width, page.width + horizontal_inset * 2.0F);
            result.height = std::max(result.height, page.height + tab_chrome);
        }
        break;
    }
    case ComponentKind::TableView: {
        const auto& table = *std::static_pointer_cast<TableView>(widget);
        // 列の解決は提供処理を呼ばない。 その後で現在のビュー
        // を読み直さず、同じ入力から行・見出しの条件を使う。
        TableView row_metrics;
        row_metrics.set_font(owned_font);
        row_metrics.row_height=table.row_height;
        row_metrics.header_height=table.header_height;
        row_metrics.header_visible=table.header_visible;
        // 前フレームの割り当て幅ではなく、描画と同じ解決処
        // 理で指定列を計測する。 幅を制限した表示領域で広
        // い表をスクロール可能にしつつ、 汎用の最小値40
        // ではなく、隣のツリーに見合う領域を確保する。
        const float cap = authored_minimum_cap(
            ui_scaled(minimum ? 160.0F : 320.0F), item.preferred.width);
        const auto columns = resolved_table_column_widths(table, cap);
        const float total = std::accumulate(columns.begin(), columns.end(), 0.0F);
        const float floor = std::min(
            ui_scaled(minimum ? kEditorUiMetrics.flexible_minimum_width : 160.0F), cap);
        const float row = table_row_height(row_metrics, ui_scaled(kEditorUiMetrics.row_height));
        const float minimum_height = 3.0F * row + table_header_height(row_metrics, row);
        const float one_row_height = row + table_header_height(row_metrics, row);
        const float viewport_height = minimum
            ? std::max(one_row_height,
                       authored_minimum_cap(minimum_height, item.preferred.height))
            : std::max(minimum_height, default_ui_height(widget->kind()));
        result = {std::clamp(total, floor, cap),
                  viewport_height};
        break;
    }
    case ComponentKind::TreeView: {
        const auto& tree = *std::static_pointer_cast<TreeView>(widget);
        auto width_input=prepare_tree_width_measurement(tree);
        const float row_height = item_row_height(tree, ui_scaled(kEditorUiMetrics.row_height));
        // ツリーはスクロールできるため、極端に長いパスでダイアロ
        // グ全体を画面外へ押し出さない。 最小サイズでは読める先
        // 頭部分を確保し、自然サイズは内容に応じてより広くする。
        const float cap = authored_minimum_cap(
            ui_scaled(minimum ? 160.0F : 320.0F), item.preferred.width);
        const float floor = std::min(
            ui_scaled(minimum ? kEditorUiMetrics.flexible_minimum_width : 160.0F), cap);
        const float width = std::clamp(tree_content_width(std::move(width_input), minimum ? ui_scaled(84) : -1), floor, cap);
        const float three_rows = row_height * 3 + ui_scaled(2);
        const float viewport_height = minimum
            ? std::max(row_height + ui_scaled(2),
                       authored_minimum_cap(three_rows, item.preferred.height))
            : std::max(three_rows, default_ui_height(widget->kind()));
        result = {width, viewport_height};
        break;
    }
    case ComponentKind::ListView: {
        const float width=authored_minimum_cap(
            ui_scaled(minimum ? kEditorUiMetrics.flexible_minimum_width : 160.0F),
            item.preferred.width);
        const float natural_height=default_ui_height(widget->kind());
        const float border=ui_scaled(2);
        // 提供処理が呼び出し中にフォントを変える場合があるため、解決は一度だけにする。
        const float three_rows=3.0F*item_row_height(*widget,ui_scaled(kEditorUiMetrics.row_height))+border;
        const float viewport_height = minimum
            ? std::max(three_rows / 3.0F,
                       authored_minimum_cap(three_rows, item.preferred.height))
            : std::max(natural_height,three_rows);
        result={width,viewport_height};
        break;
    }
    default:
        if (!minimum && widget->kind() == ComponentKind::Canvas) {
            if (const auto measured = widget->preferred_size_for_layout(
                    std::nullopt, std::nullopt)) {
                result = *measured;
                break;
            }
        }
        // Canvasやその他の複合部品にも、倍率適用済
        // みの自然な表示領域が必要。 この代替値だけを160
        // 論理画素に固定すると、UI倍率100%超でラ
        // ベル・ボタンに対して相対的に縮んでしまう。
        result = {ui_scaled(160.0F), default_ui_height(widget->kind())};
        if (minimum) {
            result.width = ui_scaled(kEditorUiMetrics.flexible_minimum_width);
            if (widget->kind() == ComponentKind::Canvas ||
                widget->kind() == ComponentKind::ImageView ||
                widget->kind() == ComponentKind::ColorWell) {
                // これらはフォームの文字ではなく、指定された描画領域。
                // 小さな色見本に、仮の160pxの最小値を与えない。
                if (item.preferred.width > 0)
                    result.width = std::min(result.width, ui_scaled(item.preferred.width));
                if (item.preferred.height > 0)
                    result.height = std::min(result.height, ui_scaled(item.preferred.height));
            }
        }
        break;
    }

    if (canvas_lines > 0) {
        result.height = ui_text_line_height(16, 0, font) * canvas_lines;
    }

    float measured_control_height{};
    // 固有フォントの一行部品は、通常の操作部品より高くなる
    // 場合がある。 空の編集欄でも、代表字形を計測する。
    switch (widget->kind()) {
    case ComponentKind::Button:
    case ComponentKind::ToggleButton:
    case ComponentKind::CheckBox:
    case ComponentKind::RadioButton:
    case ComponentKind::Label:
    case ComponentKind::Link:
    case ComponentKind::NumberEdit:
    case ComponentKind::ComboBox:
        measured_control_height = ui_text_line_height(kEditorUiMetrics.minimum_label_height, 4, font);
        result.height = std::max(result.height, measured_control_height);
        if (const auto text = std::dynamic_pointer_cast<TextWidget>(widget)) {
            // 画像だけのボタンは指定されたアイコン領域を保つ。
            // 明示的な改行のあるラベルは、段落全体を確保する。
            if (text->text.empty() && (widget->kind() == ComponentKind::Button ||
                                      widget->kind() == ComponentKind::ToggleButton))
                measured_control_height = 0;
            else if ((widget->kind() == ComponentKind::Label || widget->kind() == ComponentKind::Link) &&
                     text->text.find_first_of("\r\n") != std::string::npos)
                measured_control_height = result.height;
        }
        break;
    default: break;
    }

    // コンテナーの自然サイズ計算は、すべての表示側で一つを共有する。
    // 以前のフレームで配置された境界は、意図的に入力へ含めない。
    if (container) {
        // ホストは、子の寸法に任意の160pxの下限を加えない。
        // 空の作業ドックには、タブ見出しと余白だけあればよい。
        result = {};
        const auto layout = container->layout();
        // フォントの提供処理を呼ぶ前に、構造と指定寸法を取得
        // する。 子の自然サイズの計測中に、コンテナーの消去
        // 、兄弟の解放、配置の置換が起こる場合があるため。
        struct ChildInput {
            WidgetPtr widget;
            LayoutItem item;
            Rect bounds;
            std::optional<FontSpec> font;
            std::string title;
            bool visible{}, explicit_grid{}, expanded{true};
            std::optional<ExpandItemMeasurement> expand_metrics;
        };
        const auto* stack = dynamic_cast<const StackLayout*>(layout.get());
        const auto* expand = dynamic_cast<const ExpandBar*>(container.get());
        const bool is_expand = expand != nullptr;
        const auto active_index = stack ? stack->active_index : 0U;
        const auto* grid = dynamic_cast<const GridLayout*>(layout.get());
        const std::optional<std::size_t> grid_columns = grid
            ? std::optional<std::size_t>{std::max<std::size_t>(1U, grid->columns)} : std::nullopt;
        const bool equal_columns = grid && grid->equal_width;
        const auto split = std::dynamic_pointer_cast<SplitView>(container);
        const bool horizontal_split = !split || split->orientation == Orientation::Horizontal;
        const bool custom_layout = static_cast<bool>(container->on_layout) ||
            static_cast<bool>(container->child_layout_provider());
        const auto group = std::dynamic_pointer_cast<GroupBox>(container);
        const auto group_title = group ? group->title : std::string{};
        std::vector<ChildInput> children;
        children.reserve(container->children().size());
        for (std::size_t index=0; index<container->children().size(); ++index) {
            const auto& child=container->children()[index];
            if (!child) { children.push_back({}); continue; }
            children.push_back({child,child->layout_item(),child->bounds(),child->font(),
                is_expand ? std::string(expand_item_title(*child)) : std::string{},
                child->visible(),child->has_explicit_grid_position(),
                !expand || index>=expand->expanded.size() || expand->expanded[index],
                is_expand ? std::optional{prepare_expand_item_measurement(*child)} : std::nullopt});
        }
        const auto resolved = resolve_ui_container_layout(*container);
        const LayoutKind kind = resolved.kind;
        const float spacing = resolved.spacing;
        const Insets padding = resolved.padding;
        result = {padding.left + padding.right, padding.top + padding.bottom};
        if (group)
            result.width = std::max(result.width,
                ui_text_width(group_title, ui_scaled(12.0F), font));

        std::vector<Size> child_sizes;
        std::vector<const ChildInput*> child_inputs;
        if (is_expand || kind == LayoutKind::Stack) result = {};
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& input = children[index];
            const auto& child = input.widget;
            // 配置の外にあるのはポップアップだけ。 メニューバーは
            // 共通の配置で一行を使うので、初期サイズにも含める。
            if (!child || !input.visible ||
                std::dynamic_pointer_cast<Window>(child) ||
                child->kind() == ComponentKind::ContextMenu)
                continue;
            // Stackのインデックスは、配置と同様に絞り込み前のモデル
            // 順を指す。 非選択のページで、現在のページを大きくしない。
            if (kind == LayoutKind::Stack && index != active_index)
                continue;
            Size size = !is_expand || input.expanded
                ? preferred_ui_size_nested(child, visiting, minimum, dialog_content) : Size{};
            if (is_expand) {
                const auto metrics = measure_expand_item(*input.expand_metrics);
                const auto header = metrics.header_height;
                const bool expanded = input.expanded;
                size.height = expanded ? std::max(header, size.height + metrics.content_offset) : header;
                const auto title_width = ui_text_width(input.title, ui_scaled(34.0F),
                    input.font ? &*input.font : nullptr);
                size.width = expanded ? std::max(size.width, title_width) : title_width;
            }
            const auto& margin = input.item.margin;
            size.width += ui_scaled(margin.left + margin.right);
            size.height += ui_scaled(margin.top + margin.bottom);
            child_sizes.push_back(size);
            child_inputs.push_back(&input);
        }

        float width{};
        float height{};
        if (!child_sizes.empty()) {
            if (minimum && widget->kind() == ComponentKind::CoolBar) {
                // 帯は共通の配置で折り返すため、合計の一
                // 行幅を分割ペインの必須最小幅にしない。
                const auto cool = std::static_pointer_cast<CoolBar>(widget);
                for (const auto& child : child_sizes) {
                    width = std::max(width, child.width);
                    height = std::max(height, child.height);
                }
                if (!cool->locked) width += ui_scaled(UiLayoutParameters{}.cool_bar_gripper);
            } else if (kind == LayoutKind::Row ||
                (kind == LayoutKind::Split && horizontal_split)) {
                for (const auto& child : child_sizes) {
                    width += child.width;
                    height = std::max(height, child.height);
                }
                width += spacing * static_cast<float>(child_sizes.size() - 1U);
            } else if (kind == LayoutKind::Grid || kind == LayoutKind::Form) {
                std::size_t columns = kind == LayoutKind::Form ? 2U : 1U;
                if (grid_columns) columns = *grid_columns;
                std::size_t rows{};
                for (std::size_t index = 0; index < child_inputs.size(); ++index) {
                    const auto& input = *child_inputs[index];
                    const auto& child_item = input.item;
                    const auto row = input.explicit_grid
                        ? child_item.row : index / columns;
                    rows = std::max(rows, row +
                        std::max<std::size_t>(1U, child_item.row_span));
                }
                rows = std::max<std::size_t>(1U, rows);
                std::vector<float> widths(columns), heights(rows);
                for (std::size_t index = 0; index < child_sizes.size(); ++index) {
                    const auto& input = *child_inputs[index];
                    const auto& child_item = input.item;
                    const auto row = input.explicit_grid
                        ? child_item.row : index / columns;
                    const auto column = input.explicit_grid
                        ? child_item.column : index % columns;
                    const auto column_span = std::min(
                        widths.size() - std::min(column, widths.size()),
                        std::max<std::size_t>(1U, child_item.column_span));
                    if (column < widths.size() && column_span > 0U) {
                        float occupied = spacing *
                            static_cast<float>(column_span - 1U);
                        for (std::size_t track = column;
                             track < column + column_span; ++track)
                            occupied += widths[track];
                        const float deficit = std::max(
                            0.0F, child_sizes[index].width - occupied);
                        for (std::size_t track = column;
                             track < column + column_span; ++track)
                            widths[track] += deficit /
                                static_cast<float>(column_span);
                    }
                    const auto row_span = std::min(
                        heights.size() - std::min(row, heights.size()),
                        std::max<std::size_t>(1U, child_item.row_span));
                    if (row < heights.size() && row_span > 0U) {
                        float occupied = spacing *
                            static_cast<float>(row_span - 1U);
                        for (std::size_t track = row;
                             track < row + row_span; ++track)
                            occupied += heights[track];
                        const float deficit = std::max(
                            0.0F, child_sizes[index].height - occupied);
                        for (std::size_t track = row;
                             track < row + row_span; ++track)
                            heights[track] += deficit /
                                static_cast<float>(row_span);
                    }
                }
                // 等幅の列には、配置時に最も広い列と同じ幅
                // を割り当てる。 計測でも全列にその幅を確
                // 保し、コンパクトなボタン群が推奨サイズ
                // でも最長の見出しを切らないようにする。
                if (equal_columns && !widths.empty()) {
                    const float widest = *std::max_element(widths.begin(), widths.end());
                    std::fill(widths.begin(), widths.end(), widest);
                }
                width = std::accumulate(widths.begin(), widths.end(), 0.0F) +
                    spacing * static_cast<float>(columns > 0U ? columns - 1U : 0U);
                height = std::accumulate(heights.begin(), heights.end(), 0.0F) +
                    spacing * static_cast<float>(rows > 0U ? rows - 1U : 0U);
            } else if (kind == LayoutKind::Fill || kind == LayoutKind::Stack ||
                       kind == LayoutKind::Scroll) {
                for (const auto& child : child_sizes) {
                    width = std::max(width, child.width);
                    height = std::max(height, child.height);
                }
            } else if (kind == LayoutKind::Absolute) {
                for (std::size_t index = 0; index < child_inputs.size(); ++index) {
                    const auto& input = *child_inputs[index];
                    // RadarSpinnerなどの独自配置はウィンドウ
                    // 座標を書き込むが、それは出力であり固有寸法ではない
                    // 。 独自形状の下限には、指定された最小寸法を使う。
                    const auto origin = custom_layout ? Point{} :
                        Point{input.bounds.x, input.bounds.y};
                    width = std::max(width, origin.x + child_sizes[index].width);
                    height = std::max(height, origin.y + child_sizes[index].height);
                }
            } else {
                for (const auto& child : child_sizes) {
                    width = std::max(width, child.width);
                    height += child.height;
                }
                height += spacing * static_cast<float>(child_sizes.size() - 1U);
            }
            result.width = std::max(result.width,
                width + padding.left + padding.right);
            result.height = std::max(result.height,
                height + padding.top + padding.bottom);
        }
    }

    if (scroll_view) {
        // ウィンドウサイズと配置の両方で、文書全体ではなく表
        // 示領域を計測する。 縦長のスクロール内容が、隣や下
        // の固定フォーム行を圧迫しないようにする。 下の明
        // 示的な配置指定は、これらの既定寸法より優先する。
        if (scroll_horizontal)
            result.width = std::min(result.width, authored_minimum_cap(
                ui_scaled(minimum ? 160.0F : 320.0F), item.preferred.width));
        if (scroll_vertical) {
            const float document_height = result.height;
            result.height = std::min(result.height, authored_minimum_cap(
                ui_scaled(minimum ? 3.0F * kEditorUiMetrics.row_height : 320.0F),
                item.preferred.height));
            // 縦スクロールだけのビューで内容が高い場
            // 合、幅の内側に縦バーを表示する。 推奨
            // 幅にその分を確保し、設定タブなどが推奨
            // サイズで開いたときに、 バー幅だけ不足
            // して横スクロールが現れないようにする。
            if (!minimum && !scroll_horizontal && document_height > result.height)
                result.width += editor_scrollbar_extent();
        }
    }

    // GroupBoxの指定推奨高さは外枠の要求であ
    // り、見出し・内容の余白を捨てる許可ではない。
    // 実測高さを下限にし、固定高さの名前・効果グル
    // ープでも、 子と下枠の余白が必要なら広げる。
    const Size measured_dialog_content = font_sized_dialog &&
        ((container && !scroll_view && !container->child_layout_provider() && !container->on_layout) ||
         widget->kind()==ComponentKind::TabView) ? result : Size{};
    const Size measured_dialog_control = font_sized_dialog && !container && !canvas &&
        (std::dynamic_pointer_cast<TextWidget>(widget) || widget->kind()==ComponentKind::NumberEdit)
        ? result : Size{};
    const float measured_group_height =
        std::dynamic_pointer_cast<GroupBox>(widget) &&
                !item.constrain_minimum_to_preferred
            ? result.height : 0.0F;

    // LayoutItemの指定寸法は、ダイアログの
    // 論理サイズと同じ基準UI単位。 倍率を掛けない
    // と、二倍の見出しが子を覆ってしまう。拡大するの
    // は表示領域だけで、Canvas描画のFontSpec
    // は変えない。 また、コンテナーの推奨指定が内
    // 容の最小値より小さい場合、自然サイズも最小値
    // を下回らないようにする。 親が自然サイズで配
    // 置しても内容を切らないため。負荷を抑えるため
    // 、この計測は指定のあるコンテナーだけで行う。
    Size hinted_container_floor{};
    if (!minimum && container && !scroll_view && !item.constrain_minimum_to_preferred &&
        (item.preferred.width > 0.0F || item.preferred.height > 0.0F)) {
        visiting.erase(widget.get());
        hinted_container_floor = preferred_ui_size_nested(widget, visiting, true, dialog_content);
        visiting.insert(widget.get());
    }
    if (!minimum && item.preferred.width > 0.0F)
        result.width = std::max(ui_scaled(item.preferred.width), hinted_container_floor.width);
    if (!minimum && item.preferred.height > 0.0F)
        result.height = std::max(ui_scaled(item.preferred.height), hinted_container_floor.height);
    // 幅指定のある折り返しツール欄は、最
    // 小幅、なければ推奨幅で、折り返す全
    // 行の高さを求める。 一行分の計測や
    // 小さい高さ指定では、検索・置換のコ
    // ンテント欄などで後続行が欠ける。
    if (const auto tools = std::dynamic_pointer_cast<ToolBar>(widget)) {
        if (const float authored = item.minimum.width > 0.0F ? item.minimum.width
                                                             : item.preferred.width;
            authored > 0.0F) {
            const auto wrapped = measure_tools(*tools, ui_scaled(authored)).size;
            const auto one_row = measure_tools(tools->items, owned_font,
                std::numeric_limits<float>::max(), tools->icon_scale).size;
            // 一行のツール欄は、指定の高さを維持する。
            if (wrapped.height > one_row.height)
                result.height = std::max(result.height, wrapped.height);
        }
    }
    if (minimum && item.constrain_minimum_to_preferred) {
        if (item.preferred.width > 0.0F)
            result.width = std::min(result.width, ui_scaled(item.preferred.width));
        if (item.preferred.height > 0.0F)
            result.height = std::min(result.height, ui_scaled(item.preferred.height));
    }
    result.width = std::max(ui_scaled(item.minimum.width), result.width);
    result.width = std::max(dialog_tab_header_width, result.width);
    result.height = std::max(ui_scaled(item.minimum.height), result.height);
    if (font_sized_dialog) {
        // ALL仕様: 固定寸法で文字や子部品の必要サイズを切り捨てない。
        // 描画キャンバスとスクロール文書は対象外。モデルの寸法は変更しない。
        result.width = std::max({result.width, measured_dialog_content.width, measured_dialog_control.width});
        result.height = std::max({result.height, measured_dialog_content.height, measured_dialog_control.height});
        if (measured_dialog_control.width > 0 || measured_dialog_content.width > 0) {
            const float ratio = std::max(1.0F, editor_widget_font_points(font) / ui_scaled(12));
            result.width = std::max(result.width, ui_scaled(std::max(item.minimum.width,item.preferred.width)) * ratio);
            result.height = std::max(result.height, ui_scaled(std::max(item.minimum.height,item.preferred.height)) * ratio);
        }
    }
    if (const auto* fixed_combo = dynamic_cast<const ComboBox*>(widget.get());
        fixed_combo && fixed_combo->fixed_width > 0.0F)
        result.width = ui_scaled(fixed_combo->fixed_width);
    if (measured_group_height > 0.0F)
        result.height = std::max(result.height, measured_group_height);
    // 推奨高さは配置の目安であり、フォームの文字を切る許可ではな
    // い。 下の TextEdit と同様、指定適用後にも保護す
    // る。 固定の描画・複合部品の明示的な対象外指定は維持する。
    if (!item.constrain_minimum_to_preferred || font_sized_dialog)
        result.height = std::max(result.height, measured_control_height);
    if (widget->kind() == ComponentKind::TextEdit) {
        const auto glyph = measured_ui_text_size("Mg", font);
        const auto line = text_edit_line_height(glyph ? std::optional<float>{glyph->height} : std::nullopt, font);
        const auto insets = text_edit ? text_edit_insets(*text_edit) : text_edit_insets();
        if (text_edit && text_edit->minimum_visible_columns != 0U) {
            const auto columns = text_edit->minimum_visible_columns;
            // 全角空白一字の幅×桁数ではなく、折り返し処
            // 理と同じように文字列全体を計測する。 漢字
            // が代替書体から描かれると、全角空白と送り幅
            // が違う場合があり、字形ごとの丸めも積み重
            // なる。 メッセージ欄が一字分狭くなり、末
            // 尾が折り返されて縦バーが出ることを防ぐ。
            const auto run_width = [font](const std::string_view glyph,
                                          const std::size_t count) {
                std::string run;
                run.reserve(glyph.size() * count);
                for (std::size_t i = 0; i < count; ++i) run += glyph;
                return ui_text_width(run, 0.0F, font);
            };
            const std::size_t full_count = columns / 2U;
            const float full = full_count == 0U ? 0.0F
                : std::max({run_width("　", full_count), run_width("漢", full_count),
                            run_width("あ", full_count), run_width("（", full_count),
                            run_width("。", full_count), run_width("１", full_count)});
            const float half = ui_text_width(" ", 0.0F, font);
            const float cells = full + ((columns & 1U) ? half : 0.0F) + ui_scaled(1.0F);
            result.width = std::max(ui_scaled(std::max(item.minimum.width, item.preferred.width)),
                cells + insets.left + insets.right + text_gutters.width);
        }
        if (text_edit && text_edit->multiline && !(font && font->content_font)) {
            // 解説・説明・コメント等の複数行欄は、文字だけでなく表示領域も拡大する。
            // authored寸法は12pxの通常UIを基準とする。フレームの倍率は既に適用済み。
            const float baseline = ui_scaled(12);
            const float width_ratio = std::max(1.0F, editor_widget_font_points(font) / baseline);
            const float height_ratio = std::max(width_ratio, line / ui_scaled(18));
            const float authored_width = ui_scaled(std::max(item.minimum.width, item.preferred.width));
            const float authored_height = ui_scaled(std::max(item.minimum.height, item.preferred.height));
            result.width = std::max(result.width, authored_width * width_ratio);
            result.height = std::max(result.height, authored_height * height_ratio);
        }
        // 推奨高さが指定されていても、大きいフォーム用フォントを切らない
        // 。 複数行の編集欄はスクロール可能だが、最低一行は表示する。
        const auto visible_lines = text_edit ? std::max<std::size_t>(1U, text_edit->minimum_visible_lines) : 1U;
        result.height = std::max(result.height, line * static_cast<float>(visible_lines) +
            insets.top + insets.bottom + text_gutters.height);
    }
    if (const auto* combo = dynamic_cast<const ComboBox*>(widget.get()))
        result.height *= combo->row_height_scale;
    if (widget->kind() == ComponentKind::NumberEdit && editor_window_host_options().all_mode) {
        // ALLでは増減ボタンの寸
        // 法をstep_button_extent
        // （未指定なら34指定
        // 画素）から求める。桁
        // 数指定があれば、その
        // 文字幅にボタン二つと
        // ラベルの幅を加える。
        const auto& number = static_cast<const NumberEdit&>(*widget);
        const float label = number.inline_label.empty() ? 0.0F : ui_scaled(14);
        const float button = ui_scaled(number.step_button_extent > 0.0F
                                           ? number.step_button_extent : 34.0F);
        if (number.input_max_length != 0U) {
            // ALL仕様: 入力欄は input_max_length 桁ちょうど(左下 XYWH は3桁)。
            // 余った幅は作らず、その分 +/- を大きくする。
            const auto insets = number.zero_text_padding ? Insets{} : text_edit_insets();
            const float digits = ui_text_width(
                std::string(number.input_max_length, '0'), 0.0F, font);
            result.width = std::max(0.0F, std::ceil(digits + insets.left + insets.right) +
                                          ui_scaled(number.input_width_adjust)) +
                           label + 2.0F * button;
        } else {
            // 余白なしの34pxボタン二つで、従来の文字セルの幅を維持する。
            result.width += ui_scaled(52) + label;
        }
        result.height = std::max(result.height, button);
    }
    visiting.erase(widget.get());
    return result;
}

}  // namespace

std::string display_text_without_mnemonic(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '&') {
            result.push_back(value[index]);
            continue;
        }
        if (index + 1U < value.size() && value[index + 1U] == '&') {
            result.push_back('&');
            ++index;
        }
    }
    return result;
}

std::string menu_item_label(const MenuItem& item) {
    return display_text_without_mnemonic(std::string_view(item.text).substr(0U, item.text.find('\t')));
}

std::string_view menu_item_shortcut(const MenuItem& item) noexcept {
    if (!item.shortcut.empty()) return item.shortcut;
    const auto separator = item.text.find('\t');
    return separator == std::string::npos ? std::string_view{}
                                         : std::string_view(item.text).substr(separator + 1U);
}

UiTextMeasureScope::UiTextMeasureScope(Measure measure, InkOffset ink_offset)
    : measure_(std::move(measure)), ink_offset_(std::move(ink_offset)),
      previous_(g_text_measure) {
    if (previous_) previous_->next_=this;
    g_text_measure = this;
}

UiTextMeasureScope::~UiTextMeasureScope() {
    // ダイアログ・提供側が、内側より先に外側の範囲を解放する場合がある。
    // 無効な提供側を設定したり、現在のものを捨てたりせずに接続を外す。
    if (next_) next_->previous_=previous_;
    else g_text_measure=previous_;
    if (previous_) previous_->next_=next_;
}

float editor_widget_font_points(const FontSpec* font) noexcept {
    const float authored = (font ? font->points : 12.0F) * editor_ui_text_scale();
    return std::max(1.0F, font && font->content_font ? authored : editor_presented_font_points(authored));
}

DialogFontLayoutScope::DialogFontLayoutScope(bool dialog) noexcept
    : previous_(g_dialog_font_layout) { g_dialog_font_layout = previous_ || dialog; }
DialogFontLayoutScope::~DialogFontLayoutScope() { g_dialog_font_layout = previous_; }
bool all_dialog_font_layout() noexcept {
    return g_dialog_font_layout && editor_window_host_options().all_mode;
}

FontSpec effective_editor_widget_font(const FontSpec* font) {
    FontSpec result=font ? *font : FontSpec{};
    result.points=editor_widget_font_points(font);
    return result;
}

std::optional<Size> measured_ui_text_size(
    const std::string_view value, const FontSpec* font) noexcept {
    try {
        return measured_effective_ui_text_size(value, effective_editor_widget_font(font));
    } catch (...) {
        // 所有権の準備も、提供処理と同じ失敗処理の範囲に
        // 含める。 呼び出し元は単一値の代替を計算済み。
        return {};
    }
}

std::optional<Size> measured_effective_ui_text_size(
    const std::string_view value, const FontSpec& effective_font) noexcept {
    if (!g_text_measure || !g_text_measure->measure_ || g_measuring_text) return {};
    if (value.empty()) return Size{};
    struct Guard {
        Guard() { g_measuring_text = true; }
        ~Guard() { g_measuring_text = false; }
    } guard;
    try {
        // 提供側は元の文字列・フォント、コールバック、所有するビ
        // ュー・範囲を置き換える場合がある。 変更後に失敗する場
        // 合も含め、呼び出し全体で借用する全入力を有効に保つ。
        const std::string owned_value(value);
        const FontSpec owned_font=effective_font;
        const auto measure=g_text_measure->measure_;
        const auto size = measure(owned_value, owned_font);
        if (size && std::isfinite(size->width) && std::isfinite(size->height) &&
            size->width >= 0 && size->height > 0) return size;
    } catch (...) {
        // フォント不足・寸法取得失敗で、エディタを開けなくしない。
    }
    ++g_text_measure_failure_generation;
    return {};
}

float measured_effective_ui_text_ink_offset(const FontSpec& effective_font) noexcept {
    if (!g_text_measure || !g_text_measure->ink_offset_ || g_measuring_text) return 0.0F;
    struct Guard {
        Guard() { g_measuring_text = true; }
        ~Guard() { g_measuring_text = false; }
    } guard;
    try {
        const FontSpec owned_font=effective_font;
        const auto offset_of=g_text_measure->ink_offset_;
        const auto offset=offset_of(owned_font);
        // 補正は小さな調整なので、バックエンドの不合理な値は拒否する。
        if (offset && std::isfinite(*offset) &&
            std::abs(*offset) <= std::max(1.0F, owned_font.points)) return *offset;
    } catch (...) {
    }
    return 0.0F;
}

std::uint64_t ui_text_measure_failure_generation() noexcept {
    return g_text_measure_failure_generation;
}

float ui_text_width(const std::string_view value, const float padding,
                    const FontSpec* font) noexcept {
    const float ratio = font && std::isfinite(font->points)
        ? std::max(1.0F, font->points) / 12.0F : 1.0F;
    const auto fallback=estimated_ui_text_width(value, 0) * ratio + padding;
    if (const auto size = measured_ui_text_size(value, font)) return size->width + padding;
    return fallback;
}

float ui_text_line_height(const float logical_minimum, const float logical_padding,
                          const FontSpec* font) noexcept {
    const auto input = prepare_line_height(logical_minimum, logical_padding, font);
    return resolve_line_height(input, measured_ui_text_size("Mg", font));
}

std::string_view expand_item_title(const Widget& item) noexcept {
    if (const auto* group = dynamic_cast<const GroupBox*>(&item)) return group->title;
    if (!item.accessibility_name.empty()) return item.accessibility_name;
    if (const auto* text = dynamic_cast<const TextWidget*>(&item)) return text->text;
    return {};
}

ExpandItemMeasurement prepare_expand_item_measurement(const Widget& item) {
    auto input = expand_item_scalar_input(item);
    input.font = effective_editor_widget_font(item.font() ? &*item.font() : nullptr);
    return input;
}

ExpandItemMetrics measure_expand_item(const ExpandItemMeasurement& input) noexcept {
    // 提供処理中に呼び出し元が準備済み入力を解放してもよ
    // いよう、 外部コードを呼ぶ前に単一値を解決する。
    const LineHeightInput header{input.header_minimum,input.text_padding,input.header_fallback};
    const LineHeightInput caption{input.caption_minimum,input.text_padding,input.caption_fallback};
    const float body_padding = input.body_padding;
    const bool container = input.container, group = input.group;
    const auto glyph = measured_effective_ui_text_size("Mg", input.font);
    return resolve_expand_item(header,caption,body_padding,container,group,glyph);
}

ExpandItemMetrics expand_item_metrics(const Widget& item) noexcept {
    const auto scalar = expand_item_scalar_input(item);
    const auto fallback = resolve_expand_item(
        {scalar.header_minimum,scalar.text_padding,scalar.header_fallback},
        {scalar.caption_minimum,scalar.text_padding,scalar.caption_fallback},
        scalar.body_padding,scalar.container,scalar.group,std::nullopt);
    try {
        const auto input = prepare_expand_item_measurement(item);
        return measure_expand_item(input);
    } catch (...) {
        return fallback;
    }
}

float expand_item_header_height(const Widget& item) noexcept {
    return expand_item_metrics(item).header_height;
}

float expand_item_content_offset(const Widget& item) noexcept {
    return expand_item_metrics(item).content_offset;
}

float menu_popup_item_height(const Menu& menu, const std::size_t index) noexcept {
    if (index >= menu.items.size()) return 0;
    return menu.items[index].separator ? scaled_ui_metric(7)
        : ui_text_line_height(kEditorUiMetrics.row_height, 4,
                              menu.font() ? &*menu.font() : nullptr) *
              (editor_window_host_options().all_mode ? 1.25F : 1.0F) +
              (editor_window_host_options().all_mode ? scaled_ui_metric(6.0F) : 0.0F);
}

float resolved_tab_vertical_extra(const TabView& tabs) noexcept {
    // ALL仕様: ダイアログ内のタブだけ、文字の上下にさらに各2px確保する。
    return tabs.header_vertical_extra +
        (tabs.dialog_header_context && editor_window_host_options().all_mode ? 2.0F : 0.0F);
}

float estimated_ui_text_width(const std::string_view value,
                              const float padding) noexcept {
    float result = padding;
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char lead = static_cast<unsigned char>(value[index]);
        std::size_t length = 1U;
        if ((lead & 0xE0U) == 0xC0U) length = 2U;
        else if ((lead & 0xF0U) == 0xE0U) length = 3U;
        else if ((lead & 0xF8U) == 0xF0U) length = 4U;
        result += (length == 1U ? 7.0F : 12.0F) * editor_ui_text_scale();
        index += std::min(length, value.size() - index);
    }
    return result;
}

float resolved_tab_horizontal_padding(const float requested, const float fallback) noexcept {
    return std::max(kMinimumTabHorizontalPadding, requested >= 0.0F ? requested : fallback);
}

float preferred_tab_width(const TabPage& page, const FontSpec* font,
                          const float horizontal_padding) noexcept {
    const float scale = editor_ui_text_scale();
    const auto label = display_text_without_mnemonic(page.title);
    // コンパクトな上書き指定でも、左右余白に加えて18pxの閉じる操
    // 作領域を確保する。 幅広い既定値は、既存の幅配分を維持する。
    const float decoration=(page.icon ? 18.0F * scale : 0.0F) +
                           (page.closable ? (horizontal_padding >= 0.0F ? 18.0F : 14.0F) * scale : 0.0F);
    if (horizontal_padding >= 0.0F) {
        return ui_text_width(label, resolved_tab_horizontal_padding(horizontal_padding) * 2.0F * scale, font) +
               decoration;
    }
    return std::max(42.0F * scale,
                    ui_text_width(label, 14.0F * scale, font) + decoration);
}

float preferred_menu_item_width(const MenuItem& item, const FontSpec* font) noexcept {
    const float scale = editor_ui_text_scale();
    const auto label = menu_item_label(item);
    const float decoration=item.icon ? 20.0F * scale : 0.0F;
    return std::max(40.0F * scale,
                    ui_text_width(label, 14.0F * scale, font) + decoration);
}

float preferred_tool_item_width(const ToolItem& item, const FontSpec* font) noexcept {
    const float scale = editor_ui_text_scale();
    if (item.icon) return (item.drop_down ? 32.0F : 24.0F) * scale;
    const auto label = display_text_without_mnemonic(item.text);
    const float decoration=item.drop_down ? 12.0F * scale : 0.0F;
    return std::max(24.0F * scale,
                    ui_text_width(label, 12.0F * scale, font) + decoration);
}

std::vector<ToolBarItemGeometry> tool_bar_item_geometry(const ToolBar& toolbar) {
    std::vector<ToolBarItemGeometry> result;
    const auto items=toolbar.items;
    result.reserve(items.size());
    const Rect bar = toolbar.bounds();
    const auto measured = measure_tools(toolbar, bar.width);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        ToolBarItemGeometry geometry;
        geometry.bounds = measured.items[index];
        geometry.bounds.x += bar.x;
        geometry.bounds.y += bar.y;
        if (!item.separator && item.drop_down) {
            const float arrow_width = std::min(geometry.bounds.width, ui_scaled(12.0F));
            geometry.drop_down = {geometry.bounds.x + geometry.bounds.width - arrow_width, geometry.bounds.y,
                                  arrow_width, geometry.bounds.height};
        }
        result.push_back(geometry);
    }
    return result;
}

std::vector<Rect> menu_bar_item_geometry(const Menu& menu) {
    const Rect bar = menu.bounds();
    auto result = measure_menu(menu, bar.width).items;
    for (auto& item : result) {
        item.x += bar.x;
        item.y += bar.y;
    }
    return result;
}

Size preferred_ui_size_for_width(const WidgetPtr& source, float width) noexcept {
    try {
    const auto widget=source;
    if (!widget) return {};
    if(const auto label=std::dynamic_pointer_cast<TextWidget>(widget);
       label && label->wrap && (widget->kind()==ComponentKind::Label || widget->kind()==ComponentKind::Link)) {
        const auto text=label->text;
        const auto alignment=label->alignment;
        const auto font=widget->font();
        const auto natural=preferred_ui_size(widget);
        const auto paragraph=prepare_label_text(text,{0,0,width,0},alignment,
            ui_scaled(kEditorUiMetrics.label_horizontal_padding),true,
            effective_editor_widget_font(font ? &*font : nullptr));
        return {std::min(natural.width,std::max(0.0F,width)),std::max(natural.height,paragraph.extent.height)};
    }
    if (const auto tools = std::dynamic_pointer_cast<ToolBar>(widget))
        return measure_tools(*tools, width).size;
    if (widget->kind() == ComponentKind::MenuBar)
        return measure_menu(*std::static_pointer_cast<Menu>(widget), width).size;
    if (const auto cool = std::dynamic_pointer_cast<CoolBar>(widget))
        return layout_cool_bar(*cool, {0, 0, width, 0}).size;
    if (widget->kind() == ComponentKind::Canvas) {
        if (const auto measured = widget->preferred_size_for_layout(width, std::nullopt))
            return *measured;
    }
    if (const auto container = std::dynamic_pointer_cast<Container>(widget);
        container && container->child_layout_provider()) {
        // 独自の折り返し配置も、共通のCoolBar/ToolBar
        // と同様、割り当て幅での高さを返す。 ここでは論
        // 理単位で準備し、実際の配置側が機器倍率を渡す。
        const auto revision = container->layout_update_revision;
        const auto children = container->children();
        const auto prepare = container->scaled_child_layout_provider();
        const auto prepared = prepare({0,0,std::max(0.0F,width),0},1.0F);
        if (revision != container->layout_update_revision) return {};
        const auto padding = resolve_ui_container_layout(*container).padding;
        Size size{padding.left + padding.right, padding.top + padding.bottom};
        std::unordered_set<const Widget*> seen;
        for (const auto& entry : prepared) {
            if (!entry.widget || std::find(children.begin(),children.end(),entry.widget)==children.end() ||
                !seen.insert(entry.widget.get()).second) return {};
            size.width = std::max(size.width,entry.bounds.x+entry.bounds.width+padding.right);
            size.height = std::max(size.height,entry.bounds.y+entry.bounds.height+padding.bottom);
        }
        return size;
    }
    if (const auto panel=std::dynamic_pointer_cast<Panel>(widget); panel) {
        const auto layout=panel->layout();
        const auto* grid=dynamic_cast<const GridLayout*>(layout.get());
        const auto children=panel->children();
        const bool has_provider_child=std::any_of(children.begin(),children.end(),[](const auto& child) {
            const auto container=std::dynamic_pointer_cast<Container>(child);
            return container && static_cast<bool>(container->child_layout_provider());
        });
        if (!editor_window_host_options().all_mode && grid && grid->columns==1U &&
            has_provider_child) {
            const auto revision=panel->layout_update_revision;
            const auto resolved=resolve_ui_container_layout(*panel);
            const auto padding=resolved.padding;
            const float inner_width=std::max(0.0F,width-padding.left-padding.right);
            std::vector<float> rows;
            for (std::size_t index=0; index<children.size(); ++index) {
                const auto& child=children[index];
                if (!child || !child->visible() || std::dynamic_pointer_cast<Window>(child) ||
                    child->kind()==ComponentKind::ContextMenu) continue;
                const auto item=child->layout_item();
                const std::size_t row=child->has_explicit_grid_position()?item.row:index;
                if ((child->has_explicit_grid_position() && item.column!=0U) ||
                    std::max<std::size_t>(1U,item.column_span)!=1U ||
                    std::max<std::size_t>(1U,item.row_span)!=1U) return preferred_ui_size(widget);
                if (rows.size()<=row) rows.resize(row+1U);
                const float horizontal_margin=ui_scaled(item.margin.left+item.margin.right);
                const auto measured=preferred_ui_size_for_width(child,
                    std::max(0.0F,inner_width-horizontal_margin));
                const float height=std::max({measured.height,ui_scaled(item.minimum.height),
                    ui_scaled(item.preferred.height)})+
                    ui_scaled(item.margin.top+item.margin.bottom);
                rows[row]=std::max(rows[row],height);
                if (revision!=panel->layout_update_revision) return {};
            }
            float height=padding.top+padding.bottom;
            for (const auto row_height:rows) height+=row_height;
            if (rows.size()>1U) height+=resolved.spacing*static_cast<float>(rows.size()-1U);
            const auto item=panel->layout_item();
            const auto natural=preferred_ui_size(widget);
            if (revision!=panel->layout_update_revision) return {};
            return {std::min(natural.width,std::max(0.0F,width)),
                std::max({height,ui_scaled(item.minimum.height),ui_scaled(item.preferred.height)})};
        }
    }
    if (const auto panel=std::dynamic_pointer_cast<Panel>(widget);
        panel && resolve_ui_container_layout(*panel).kind==LayoutKind::Column) {
        // D版と同様、外側の Composite もCoolBarの高
        // さ計算に参加する。 保持された縦パネルを通じて割り当て幅を伝
        // え、折り返した部品が一行分の親からはみ出さないようにする。
        static thread_local std::vector<const Widget*> measuring_columns;
        if (std::find(measuring_columns.begin(),measuring_columns.end(),panel.get())!=
            measuring_columns.end()) return {};
        measuring_columns.push_back(panel.get());
        struct PopColumn {
            std::vector<const Widget*>& stack;
            ~PopColumn() {stack.pop_back();}
        } pop{measuring_columns};
        const auto revision=panel->layout_update_revision;
        const auto children=panel->children();
        const auto resolved=resolve_ui_container_layout(*panel);
        const auto padding=resolved.padding;
        float height=padding.top+padding.bottom;
        std::size_t count{};
        for (const auto& child:children) {
            if (!child || !child->visible() ||
                std::dynamic_pointer_cast<Window>(child) ||
                child->kind()==ComponentKind::ContextMenu) continue;
            const auto item=child->layout_item();
            const float margin=ui_scaled(item.margin.left+item.margin.right);
            const auto size=preferred_ui_size_for_width(child,
                std::max(0.0F,width-padding.left-padding.right-margin));
            height+=std::max({size.height,ui_scaled(item.minimum.height),
                ui_scaled(item.preferred.height)})+
                ui_scaled(item.margin.top+item.margin.bottom);
            ++count;
            if (revision!=panel->layout_update_revision) return {};
        }
        if (count>1U) height+=resolved.spacing*static_cast<float>(count-1U);
        const auto item=panel->layout_item();
        const auto natural=preferred_ui_size(widget);
        if (revision!=panel->layout_update_revision) return {};
        return {std::min(natural.width,std::max(0.0F,width)),
            std::max({height,ui_scaled(item.minimum.height),ui_scaled(item.preferred.height)})};
    }
    return preferred_ui_size(widget);
    } catch (...) { return {}; }
}

float default_ui_height(const ComponentKind kind) noexcept {
    const auto scaled = [](const float value) noexcept {
        return value * editor_ui_text_scale();
    };
    switch (kind) {
    case ComponentKind::Panel: return scaled(kEditorUiMetrics.panel_height);
    case ComponentKind::GroupBox: return scaled(kEditorUiMetrics.group_box_height);
    case ComponentKind::TextEdit:
    case ComponentKind::Button:
    case ComponentKind::ToggleButton:
    case ComponentKind::CheckBox:
    case ComponentKind::RadioButton:
    case ComponentKind::ComboBox:
    case ComponentKind::NumberEdit:
    case ComponentKind::ProgressBar:
    case ComponentKind::Slider: return scaled(kEditorUiMetrics.control_height);
    case ComponentKind::Label:
    case ComponentKind::Link:
    case ComponentKind::StatusBar: return scaled(kEditorUiMetrics.label_height);
    case ComponentKind::MenuBar: return scaled(kEditorUiMetrics.menu_height);
    case ComponentKind::ContextMenu: return 0.0F;
    case ComponentKind::ToolBar:
    case ComponentKind::CoolBar: return scaled(kEditorUiMetrics.toolbar_height);
    case ComponentKind::Separator: return 1.0F;
    case ComponentKind::Spacer: return 0.0F;
    default: return scaled(160.0F);
    }
}

std::uint32_t clamp_editor_ui_text_scale_percent(const std::uint32_t value) noexcept {
    return std::clamp(value, 100U, 200U);
}

float editor_ui_text_scale_from_percent(const std::uint32_t value) noexcept {
    return static_cast<float>(clamp_editor_ui_text_scale_percent(value)) / 100.0F;
}

float editor_ui_text_scale() noexcept {
    return static_cast<float>(g_ui_text_scale_percent.load(std::memory_order_acquire)) /
           100.0F;
}

void set_editor_ui_text_scale(const float value) noexcept {
    // 提供側や壊れた設定からNaN・無限大が来る場合がある。
    // lround・整数変換へ渡さず、現在の倍率も変えない。
    if (!std::isfinite(value)) return;
    const auto percent = clamp_editor_ui_text_scale_percent(
        static_cast<std::uint32_t>(std::lround(std::clamp(value, 1.0F, 2.0F) * 100.0F)));
    const auto previous = g_ui_text_scale_percent.exchange(percent,
                                                            std::memory_order_acq_rel);
    if (previous != percent)
        g_ui_text_scale_generation.fetch_add(1U, std::memory_order_acq_rel);
}

std::uint64_t editor_ui_text_scale_generation() noexcept {
    return g_ui_text_scale_generation.load(std::memory_order_acquire);
}

float scaled_ui_metric(const float logical_value) noexcept {
    return logical_value * editor_ui_text_scale() * editor_presented_ui_metric_scale();
}

float editor_scrollbar_extent() noexcept {
    const float authored = editor_window_host_options().all_mode ? 16.0F : 14.0F;
    return scaled_ui_metric(authored);
}

void set_combo_edit_popup_enabled(const bool value) noexcept {
    g_combo_edit_popup_enabled.store(value, std::memory_order_release);
}

bool combo_edit_popup_enabled() noexcept {
    return g_combo_edit_popup_enabled.load(std::memory_order_acquire);
}

void set_number_edit_wheel_enabled(const bool value) noexcept {
    g_number_edit_wheel_enabled.store(value, std::memory_order_release);
}

bool number_edit_wheel_enabled() noexcept {
    return g_number_edit_wheel_enabled.load(std::memory_order_acquire);
}

void set_tab_wheel_switch_enabled(const bool value) noexcept {
    g_tab_wheel_switch_enabled.store(value,std::memory_order_release);
}

bool tab_wheel_switch_enabled() noexcept {
    return g_tab_wheel_switch_enabled.load(std::memory_order_acquire);
}

void set_editor_ime_mode(const int value) noexcept {
    g_editor_ime_mode.store(value, std::memory_order_release);
}

int editor_ime_mode() noexcept {
    return g_editor_ime_mode.load(std::memory_order_acquire);
}

EditorInteractionMetrics editor_interaction_metrics() noexcept {
    return {scaled_ui_metric(kEditorUiMetrics.row_height),
            scaled_ui_metric(kEditorUiMetrics.tab_height),
            scaled_ui_metric(kEditorUiMetrics.expand_header_height),
            scaled_ui_metric(4.0F), scaled_ui_metric(5.0F), 8.0F, 9U};
}

FontSpec scaled_editor_widget_font(const FontSpec& font) {
    FontSpec result = font;
    result.points = editor_widget_font_points(&font);
    return result;
}

namespace {
Size preferred_ui_size_nested(WidgetPtr widget,
                              std::unordered_set<const Widget*>& visiting,
                              const bool minimum, const bool dialog_content) {
    auto& cache = g_measurement_cache;
    if (cache.profiling) ++cache.calls;
    if (cache.depth <= 0 || !widget || visiting.contains(widget.get()))
        return preferred_ui_size_nested_uncached(std::move(widget), visiting,
                                                 minimum, dialog_content);
    const MeasurementKey key{widget.get(),
        (minimum ? 1U : 0U) | (dialog_content ? 2U : 0U) |
            (all_dialog_font_layout() ? 4U : 0U),
        editor_ui_text_scale()};
    if (const auto found = cache.values.find(key); found != cache.values.end()) {
        if (cache.profiling) ++cache.hits;
        return found->second;
    }
    if (cache.profiling) ++cache.misses;
    const auto generation = cache.generation;
    const auto result = preferred_ui_size_nested_uncached(widget, visiting,
                                                          minimum, dialog_content);
    // 計測中にフォント・文字の提供処理が動く場合があるため、 この部分ツリ
    // ーの計測中にキャッシュが無効化されなかった場合だけ結果を保存する。
    if (cache.depth > 0 && cache.generation == generation)
        cache.values.insert_or_assign(key, result);
    return result;
}
} // namespace

MeasurementCacheScope::MeasurementCacheScope() noexcept
    : owner_(g_measurement_cache.depth == 0) {
    if (owner_) {
        auto& cache = g_measurement_cache;
        cache.profiling = editor_diagnostics_enabled();
        cache.calls = cache.hits = cache.misses = cache.invalidations = 0;
        if (cache.profiling) cache.started = std::chrono::steady_clock::now();
    }
    ++g_measurement_cache.depth;
}
MeasurementCacheScope::~MeasurementCacheScope() {
    if (--g_measurement_cache.depth <= 0) {
        auto& cache = g_measurement_cache;
        if (cache.profiling) {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cache.started).count();
            if (us >= 5000) {
                try { editor_action_log("PERF operation=layout.measure elapsed_us=" + std::to_string(us) +
                    " calls=" + std::to_string(cache.calls) + " hits=" + std::to_string(cache.hits) +
                    " misses=" + std::to_string(cache.misses) + " invalidations=" + std::to_string(cache.invalidations)); }
                catch (...) {}
            }
        }
        cache.profiling = false;
        g_measurement_cache.depth = 0;
        g_measurement_cache.values.clear();
    }
}
void invalidate_measurement_cache() noexcept {
    ++g_measurement_cache.generation;
    if (g_measurement_cache.profiling) ++g_measurement_cache.invalidations;
    g_measurement_cache.values.clear();
}

Size preferred_ui_size(const WidgetPtr& widget) noexcept {
    try {
        std::unordered_set<const Widget*> visiting;
        return preferred_ui_size_nested(widget, visiting);
    } catch (...) { return {}; }
}

Size minimum_ui_size(const WidgetPtr& widget) noexcept {
    try {
        std::unordered_set<const Widget*> visiting;
        return preferred_ui_size_nested(widget, visiting, true);
    } catch (...) { return {}; }
}

namespace {

[[nodiscard]] Size scaled_dialog_size(const Window& window, Size value) noexcept {
    if (window.kind() != ComponentKind::Dialog) return value;
    const float scale = editor_ui_text_scale();
    value.width *= scale;
    value.height *= scale;
    return value;
}

[[nodiscard]] Size dialog_content_minimum(const Window& window) {
    if (window.kind() != ComponentKind::Dialog || window.children().empty()) return {};
    // 読み取り専用・非所有の走査。計測中に配置コールバックやネ
    // イティブの実体化は行わず、入れ子のウィンドウも除外する。
    const WidgetPtr borrowed(const_cast<Window*>(&window), [](Widget*) {});
    std::unordered_set<const Widget*> visiting;
    return preferred_ui_size_nested(borrowed, visiting, true, true);
}

[[nodiscard]] Size dialog_content_preferred(const Window& window) {
    if (window.kind() != ComponentKind::Dialog || window.children().empty()) return {};
    const WidgetPtr borrowed(const_cast<Window*>(&window), [](Widget*) {});
    std::unordered_set<const Widget*> visiting;
    return preferred_ui_size_nested(borrowed, visiting, false, true);
}

std::optional<Size> valid_window_constraint(Size value) noexcept {
    const auto valid=[](float dimension) {
        return std::isfinite(dimension) && dimension>0 ? dimension : 0.0F;
    };
    value.width=valid(value.width); value.height=valid(value.height);
    if(value.width<=0 && value.height<=0) return std::nullopt;
    return value;
}

}  // namespace

Size realized_window_size(const Window& window) noexcept {
    const Rect bounds = window.bounds();
    const float scale = std::max(1.0F, window.bounds_ui_text_scale);
    return realized_window_size(window, {bounds.width / scale, bounds.height / scale});
}

Size realized_window_size(const Window& window, Size value) noexcept {
    // 小さい指定のダイアログは、未初期化のウィンドウではない。
    // D版のイベントダイアログには高さ100px未満のものも
    // あり、 正の寸法をデスクトップの代替値へ変えると、大半が
    // 空の1024x720になってしまう。 代替値は未指定・非
    // 有限の寸法だけに使い、明示的な正のサイズはすべて保つ。
    if (!std::isfinite(value.width) || value.width <= 0.0F)
        value.width = 1024.0F;
    if (!std::isfinite(value.height) || value.height <= 0.0F)
        value.height = 720.0F;
    value = scaled_dialog_size(window, value);
    if (const auto minimum = realized_window_minimum_size(window)) {
        value.width = std::max(value.width, minimum->width);
        value.height = std::max(value.height, minimum->height);
    }
    if (const auto maximum = realized_window_maximum_size(window)) {
        if (maximum->width > 0.0F) value.width = std::min(value.width, maximum->width);
        if (maximum->height > 0.0F) value.height = std::min(value.height, maximum->height);
    }
    return value;
}

Size window_content_minimum_size(const Window& window) noexcept {
    try {
        Size result = dialog_content_minimum(window);
        if (window.minimum_size) {
            const Size authored = scaled_dialog_size(window, *window.minimum_size);
            result.width = std::max(result.width, authored.width);
            result.height = std::max(result.height, authored.height);
        }
        return result;
    } catch (...) {
        // これは noexcept の公開サイズ計算API。
        // 一時的な走査の失敗でUIスレッドを終了させず、明
        // 示的な制限があれば残し、なければ計測値なしを返す。
        return window.minimum_size ? scaled_dialog_size(window, *window.minimum_size) : Size{};
    }
}

Size window_content_preferred_size(const Window& window) noexcept {
    try {
        Size result = dialog_content_preferred(window);
        if (window.minimum_size) {
            const Size authored = scaled_dialog_size(window, *window.minimum_size);
            result.width = std::max(result.width, authored.width);
            result.height = std::max(result.height, authored.height);
        }
        return result;
    } catch (...) {
        return window.minimum_size ? scaled_dialog_size(window, *window.minimum_size)
                                  : Size{};
    }
}

std::optional<Size> realized_window_minimum_size(const Window& window) noexcept {
    Size result = window_content_minimum_size(window);
    if (!window.minimum_size && result.width <= 0 && result.height <= 0) return std::nullopt;
    // サイズ制限付きのツールウィンドウなど、明示的な最大値を最優先にする。
    if (const auto maximum = realized_window_maximum_size(window)) {
        if (maximum->width > 0) result.width = std::min(result.width, maximum->width);
        if (maximum->height > 0) result.height = std::min(result.height, maximum->height);
    }
    return valid_window_constraint(result);
}

std::optional<Size> realized_window_maximum_size(const Window& window) noexcept {
    if (!window.maximum_size) return std::nullopt;
    return valid_window_constraint(scaled_dialog_size(window, *window.maximum_size));
}

Size fit_window_surface(const Window& window, Size requested, Size available) noexcept {
    if (window.kind() != ComponentKind::Dialog) return requested;
    if (std::isfinite(available.width) && available.width > 0)
        requested.width = std::min(requested.width, available.width);
    if (std::isfinite(available.height) && available.height > 0)
        requested.height = std::min(requested.height, available.height);
    return requested;
}

}  // namespace cwxeditor::lib
