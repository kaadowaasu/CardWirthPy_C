#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "ui_layout.hpp"

#include "ui_metrics.hpp"
#include "ui_actions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace cwxeditor::lib {
namespace {
bool same_layout_number(double a,double b) noexcept {
    return a==b || (std::isnan(a) && std::isnan(b));
}
bool contains_child_layout_provider(const WidgetPtr& root) {
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](const auto& self,const WidgetPtr& widget)->bool {
        if (!widget || !visited.insert(widget.get()).second) return false;
        const auto container=std::dynamic_pointer_cast<Container>(widget);
        if (!container) return false;
        if (container->child_layout_provider()) return true;
        const auto children=container->children();
        return std::any_of(children.begin(),children.end(),[&](const auto& child) {
            return self(self,child);
        });
    };
    return visit(visit,root);
}
bool same_layout_rect(Rect a,Rect b) noexcept {
    return same_layout_number(a.x,b.x) && same_layout_number(a.y,b.y) &&
        same_layout_number(a.width,b.width) && same_layout_number(a.height,b.height);
}
bool same_layout_insets(Insets a,Insets b) noexcept {
    return same_layout_number(a.left,b.left) && same_layout_number(a.right,b.right) &&
        same_layout_number(a.top,b.top) && same_layout_number(a.bottom,b.bottom);
}
bool same_layout_hint(const LayoutItem& a,const LayoutItem& b) noexcept {
    return a.row==b.row && a.column==b.column && a.row_span==b.row_span && a.column_span==b.column_span &&
        a.horizontal==b.horizontal && a.vertical==b.vertical && same_layout_insets(a.margin,b.margin) &&
        same_layout_number(a.minimum.width,b.minimum.width) && same_layout_number(a.minimum.height,b.minimum.height) &&
        same_layout_number(a.preferred.width,b.preferred.width) && same_layout_number(a.preferred.height,b.preferred.height) &&
        same_layout_number(a.grow,b.grow);
}
bool same_layout_font(const std::optional<FontSpec>& a,const std::optional<FontSpec>& b) noexcept {
    if (a.has_value()!=b.has_value()) return false;
    return !a || (a->family==b->family && same_layout_number(a->points,b->points) &&
        a->weight==b->weight && a->italic==b->italic && a->underline==b->underline && a->strikeout==b->strikeout &&
        a->antialias==b->antialias && a->content_font==b->content_font &&
        a->size_mode==b->size_mode);
}
bool same_layout_size(const std::optional<Size>& a,const std::optional<Size>& b) noexcept {
    return a.has_value()==b.has_value() && (!a ||
        (same_layout_number(a->width,b->width) && same_layout_number(a->height,b->height)));
}
bool same_layout_columns(const std::vector<TableColumn>& a,const std::vector<TableColumn>& b) noexcept {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i)
        if (a[i].title!=b[i].title || !same_layout_number(a[i].width,b[i].width) ||
            !same_layout_number(a[i].stretch,b[i].stretch) || a[i].alignment!=b[i].alignment ||
            a[i].resizable!=b[i].resizable || a[i].sortable!=b[i].sortable) return false;
    return true;
}
// 配置に影響する単一値の設定。これは有効性の記録であり、別の寸法計
// 算ではない。 計測は引き続き ui_metrics だけを使う。
std::array<double,11> layout_options(const Widget& widget) noexcept {
    std::array<double,11> result{};
    if (const auto* combo=dynamic_cast<const ComboBox*>(&widget))
        result[7]=combo->row_height_scale;
    if (const auto* text=dynamic_cast<const TextWidget*>(&widget)) {
        result[0]=text->wrap; result[1]=static_cast<double>(text->alignment);
    }
    if (const auto* edit=dynamic_cast<const TextEdit*>(&widget)) {
        result[2]=edit->multiline; result[3]=edit->horizontal_scroll; result[4]=edit->vertical_scroll;
        result[5]=static_cast<double>(edit->minimum_visible_lines);
        result[6]=static_cast<double>(edit->minimum_visible_columns);
    }
    if (const auto* button=dynamic_cast<const Button*>(&widget)) {
        result[2]=bool(button->icon);
        result[3]=button->text_horizontal_padding.has_value();
        result[4]=button->text_horizontal_padding.value_or(0.0F);
    }
    if (const auto* number=dynamic_cast<const NumberEdit*>(&widget))
        result={number->value,number->minimum,number->maximum,double(number->decimals)};
    if (const auto* canvas=dynamic_cast<const Canvas*>(&widget)) result[0]=canvas->ui_text_lines;
    if (const auto* image=dynamic_cast<const ImageView*>(&widget)) {
        result[0]=image->border; result[1]=image->design_size.has_value();
        if (image->design_size) {result[2]=image->design_size->width;result[3]=image->design_size->height;}
    }
    if (const auto* split=dynamic_cast<const SplitView*>(&widget))
        result={static_cast<double>(split->orientation),
                double(split->constrain_to_child_minimums),
                double(split->initial_first_extent.has_value()),
                split->initial_first_extent.value_or(0.0F),
                double(split->initial_second_extent.has_value()),
                split->initial_second_extent.value_or(0.0F),
                double(split->initial_first_extent_offset.has_value()),
                split->initial_first_extent_offset.value_or(0.0F),
                double(split->use_explicit_edge_minimums),
                double(split->reflow_on_initial_maximized_geometry),
                double(split->initial_maximized_reflow_pending)};
    if (const auto* scroll=dynamic_cast<const ScrollView*>(&widget)) {
        result={double(scroll->horizontal_scroll),double(scroll->vertical_scroll),scroll->scroll_offset.x,
            scroll->scroll_offset.y,double(scroll->content_extent.has_value())};
        if (scroll->content_extent) {result[5]=scroll->content_extent->width;result[6]=scroll->content_extent->height;}
    }
    if (const auto* cool=dynamic_cast<const CoolBar*>(&widget)) {
        result[0] = cool->locked;
        result[1] = cool->single_row;
    }
    if (const auto* table=dynamic_cast<const TableView*>(&widget))
        result={table->row_height,table->header_height,double(table->header_visible)};
    if (const auto* tree=dynamic_cast<const TreeView*>(&widget))
        result={double(tree->checkable),double(tree->show_expanders),double(tree->flush_root_left)};
    if (const auto* window=dynamic_cast<const Window*>(&widget))
        result={window->content_extent.width,window->content_extent.height,window->content_offset.x,
            window->content_offset.y,window->bounds_ui_text_scale};
    return result;
}
class ContainerLayoutGuard {
    struct Node {
        WidgetPtr widget;
        Rect bounds;
        LayoutItem hint;
        std::optional<FontSpec> font;
        bool visible{},enabled{},explicit_grid{},has_callback{};
        std::string text,title;
        std::array<double,11> options{};
        std::vector<WidgetPtr> children;
        LayoutPtr layout;
        LayoutKind kind{};
        Insets padding;
        float spacing{};
        std::size_t columns{},active{};
        bool equal_width{};
        Align cross{};
        std::uint64_t revision{};
        std::vector<float> weights;
        SplitResizeMode split_mode{SplitResizeMode::Ratio};
        float split_min_first{},split_min_second{};
        std::optional<SplitLayoutSnapshot> split_snapshot;
        std::vector<bool> expanded;
        std::vector<std::size_t> wraps;
        std::vector<CoolBarBandSize> bands;
        std::optional<TabLayoutSnapshot> tab;
        std::optional<Size> window_minimum,window_maximum;
        std::weak_ptr<Window> window_owner;
        WindowHostMode window_host_mode{WindowHostMode::Default};
        bool window_center_on_owner{},window_overlay_surface{};
        std::uint64_t child_layout_provider_revision{};
        std::vector<TreeNode> tree_nodes;
        std::vector<TableColumn> table_columns;
        std::uint64_t item_update_revision{},item_order_revision{};
    };
    std::vector<Node> nodes_;
    std::uint64_t scale_generation_{editor_ui_text_scale_generation()};
    bool track_revisions_{true};
public:
    explicit ContainerLayoutGuard(const WidgetPtr& root,bool recursive=true,bool track_revisions=true)
        : track_revisions_(track_revisions) {
        std::unordered_set<const Widget*> visited;
        const auto visit=[&](auto&& self,const WidgetPtr& widget)->void {
            if (!widget || !visited.insert(widget.get()).second) return;
            Node node;
            node.widget=widget; node.bounds=widget->bounds(); node.hint=widget->layout_item(); node.font=widget->font();
            node.visible=widget->visible(); node.enabled=widget->enabled(); node.explicit_grid=widget->has_explicit_grid_position();
            node.options=layout_options(*widget);
            if (auto text=std::dynamic_pointer_cast<TextWidget>(widget)) node.text=text->text;
            if (auto group=std::dynamic_pointer_cast<GroupBox>(widget)) node.title=group->title;
            if (auto container=std::dynamic_pointer_cast<Container>(widget)) {
                node.children=container->children(); node.layout=container->layout();
                node.revision=container->layout_update_revision; node.has_callback=bool(container->on_layout);
                if (node.layout) {
                    node.kind=node.layout->kind(); node.padding=node.layout->padding; node.spacing=node.layout->spacing;
                    if (auto* grid=dynamic_cast<GridLayout*>(node.layout.get())) {node.columns=grid->columns;node.equal_width=grid->equal_width;}
                    if (auto* stack=dynamic_cast<StackLayout*>(node.layout.get())) node.active=stack->active_index;
                    if (auto* linear=dynamic_cast<LinearLayout*>(node.layout.get())) node.cross=linear->cross_axis;
                }
            }
            if (auto split=std::dynamic_pointer_cast<SplitView>(widget)) {
                node.weights=split->weights; node.split_mode=split->resize_mode;
                node.split_snapshot=split->committed_split_layout;
                node.split_min_first=split->drag_minimum_first; node.split_min_second=split->drag_minimum_second;
            }
            if (auto expand=std::dynamic_pointer_cast<ExpandBar>(widget)) {
                node.expanded=expand->expanded; node.item_update_revision=expand->expansion_update_revision;
            }
            if (auto cool=std::dynamic_pointer_cast<CoolBar>(widget)) {
                node.wraps=cool->wrap_indices;
                for (const auto& band:cool->band_sizes)
                    node.bands.push_back(band ? *band : CoolBarBandSize{});
            }
            if (auto tab=std::dynamic_pointer_cast<TabView>(widget)) node.tab=snapshot_tab_layout(*tab);
            if (auto window=std::dynamic_pointer_cast<Window>(widget)) {
                node.window_minimum=window->minimum_size; node.window_maximum=window->maximum_size;
                node.window_owner=window->owner; node.window_host_mode=window->host_mode;
                node.window_center_on_owner=window->center_on_owner;
                node.window_overlay_surface=window->overlay_surface;
            }
            if (auto container=std::dynamic_pointer_cast<Container>(widget))
                node.child_layout_provider_revision=container->child_layout_provider_revision();
            if (auto tree=std::dynamic_pointer_cast<TreeView>(widget)) {
                node.tree_nodes=tree->nodes; node.item_update_revision=tree->node_update_revision;
                node.item_order_revision=tree->node_order_revision;
            }
            if (auto table=std::dynamic_pointer_cast<TableView>(widget)) {
                node.table_columns=table->columns; node.item_update_revision=table->row_update_revision;
                node.item_order_revision=table->row_order_revision;
            }
            const auto children=node.children;
            const auto tabs=node.tab;
            nodes_.push_back(std::move(node));
            if (recursive) {
                for (const auto& child:children)
                    if (!std::dynamic_pointer_cast<Window>(child)) self(self,child);
                if (tabs) for (const auto& page:tabs->pages) self(self,page.content);
            }
        };
        visit(visit,root);
    }
    bool current() const noexcept {
        if (scale_generation_!=editor_ui_text_scale_generation()) return false;
        for (const auto& n:nodes_) {
            const auto& w=n.widget;
            if (!same_layout_rect(n.bounds,w->bounds()) || !same_layout_hint(n.hint,w->layout_item()) ||
                !same_layout_font(n.font,w->font()) || n.visible!=w->visible() || n.enabled!=w->enabled() ||
                n.explicit_grid!=w->has_explicit_grid_position()) return false;
            const auto options=layout_options(*w);
            for (std::size_t i=0;i<options.size();++i) if (!same_layout_number(options[i],n.options[i])) return false;
            if (auto text=std::dynamic_pointer_cast<TextWidget>(w); text && text->text!=n.text) return false;
            if (auto group=std::dynamic_pointer_cast<GroupBox>(w); group && group->title!=n.title) return false;
            if (auto container=std::dynamic_pointer_cast<Container>(w)) {
                if (container->children()!=n.children || container->layout()!=n.layout ||
                    (track_revisions_ && container->layout_update_revision!=n.revision) ||
                    bool(container->on_layout)!=n.has_callback) return false;
                if (n.layout) {
                    if (n.kind!=n.layout->kind() || !same_layout_insets(n.padding,n.layout->padding) ||
                        !same_layout_number(n.spacing,n.layout->spacing)) return false;
                    if (auto* grid=dynamic_cast<GridLayout*>(n.layout.get()); grid && (n.columns!=grid->columns || n.equal_width!=grid->equal_width)) return false;
                    if (auto* stack=dynamic_cast<StackLayout*>(n.layout.get()); stack && n.active!=stack->active_index) return false;
                    if (auto* linear=dynamic_cast<LinearLayout*>(n.layout.get()); linear && n.cross!=linear->cross_axis) return false;
                }
            }
            if (auto split=std::dynamic_pointer_cast<SplitView>(w); split &&
                (n.weights!=split->weights || n.split_mode!=split->resize_mode ||
                 n.split_snapshot!=split->committed_split_layout ||
                 n.split_min_first!=split->drag_minimum_first || n.split_min_second!=split->drag_minimum_second)) return false;
            if (auto expand=std::dynamic_pointer_cast<ExpandBar>(w); expand &&
                (n.expanded!=expand->expanded || n.item_update_revision!=expand->expansion_update_revision)) return false;
            if (auto cool=std::dynamic_pointer_cast<CoolBar>(w)) {
                if (n.wraps!=cool->wrap_indices || n.bands.size()!=cool->band_sizes.size()) return false;
                for (std::size_t i=0; i<n.bands.size(); ++i) {
                    const auto current=cool->band_sizes[i] ? *cool->band_sizes[i] : CoolBarBandSize{};
                    const auto& saved=n.bands[i];
                    if (saved.owner.lock()!=current.owner.lock() ||
                        !same_layout_number(saved.minimumSize.width,current.minimumSize.width) ||
                        !same_layout_number(saved.minimumSize.height,current.minimumSize.height) ||
                        !same_layout_number(saved.preferredSize.width,current.preferredSize.width) ||
                        !same_layout_number(saved.preferredSize.height,current.preferredSize.height) ||
                        !same_layout_size(saved.currentSize,current.currentSize)) return false;
                }
            }
            if (n.tab && !tab_layout_is_current(*n.tab,*std::static_pointer_cast<TabView>(w))) return false;
            if (auto window=std::dynamic_pointer_cast<Window>(w); window &&
                (!same_layout_size(n.window_minimum,window->minimum_size) ||
                 !same_layout_size(n.window_maximum,window->maximum_size) ||
                 n.window_owner.owner_before(window->owner) || window->owner.owner_before(n.window_owner) ||
                 n.window_host_mode!=window->host_mode ||
                 n.window_center_on_owner!=window->center_on_owner ||
                 n.window_overlay_surface!=window->overlay_surface)) return false;
            if (auto container=std::dynamic_pointer_cast<Container>(w); container &&
                n.child_layout_provider_revision!=container->child_layout_provider_revision()) return false;
            if (auto tree=std::dynamic_pointer_cast<TreeView>(w); tree &&
                (n.tree_nodes!=tree->nodes || n.item_update_revision!=tree->node_update_revision ||
                 n.item_order_revision!=tree->node_order_revision)) return false;
            if (auto table=std::dynamic_pointer_cast<TableView>(w); table &&
                (!same_layout_columns(n.table_columns,table->columns) || n.item_update_revision!=table->row_update_revision ||
                 n.item_order_revision!=table->row_order_revision)) return false;
        }
        return true;
    }
};
struct PendingScrollLayout {
    std::shared_ptr<ScrollView> owner;
    Size extent{};
    Point offset{};
};
bool arrange_widget_transaction(const WidgetPtr& widget,Rect bounds);
bool arrange_container_transaction(const std::shared_ptr<Container>& container,Rect bounds,
    std::optional<Point> requested_scroll={});
} // namespace

bool arrange_container(const std::shared_ptr<Container>& source, Rect available) {
    return arrange_container_transaction(source,available);
}

bool scroll_window_content(const std::shared_ptr<Window>& window, Point offset) {
    if (!window || window->kind() != ComponentKind::Dialog ||
        !std::isfinite(offset.x) || !std::isfinite(offset.y)) return false;
    const auto before=window->content_offset;
    try {
        return arrange_container_transaction(window,window->bounds(),offset) &&
            (window->content_offset.x!=before.x || window->content_offset.y!=before.y);
    } catch (...) { return false; }
}

namespace { bool reveal_control_transaction(const WidgetPtr&,const WidgetPtr&); }

void reveal_window_control(const WidgetPtr& scope, const WidgetPtr& control) {
    try { (void)reveal_control_transaction(scope,control); } catch (...) {}
}

bool arrange_widget(const WidgetPtr& widget, Rect bounds) {
    return arrange_widget_transaction(widget,bounds);
}

bool scroll_view_content(const std::shared_ptr<ScrollView>& scroll, Point offset) {
    if (!scroll || !std::isfinite(offset.x) || !std::isfinite(offset.y)) return false;
    const auto before=scroll->scroll_offset;
    try {
        return arrange_container_transaction(scroll,scroll->bounds(),offset) &&
            (scroll->scroll_offset.x!=before.x || scroll->scroll_offset.y!=before.y);
    } catch (...) { return false; }
}

namespace {

[[nodiscard]] Insets scaled_insets(const Insets value, const float scale) noexcept {
    // LayoutItem の余白は、通常のエディタのフォーム・操作部品
    // の単位。 scale は表示側の機器倍率だけなので、先にUI文字倍
    // 率を適用し、 同じフォームの推奨・最小寸法と配置時の余白を揃える。
    return {scaled_ui_metric(value.top) * scale, scaled_ui_metric(value.right) * scale,
            scaled_ui_metric(value.bottom) * scale, scaled_ui_metric(value.left) * scale};
}

[[nodiscard]] Rect apply_ui_margin(Rect value, const Insets& margin,
                                   const float scale) noexcept {
    const auto left=scaled_ui_metric(margin.left)*scale;
    const auto top=scaled_ui_metric(margin.top)*scale;
    const auto right=scaled_ui_metric(margin.right)*scale;
    const auto bottom=scaled_ui_metric(margin.bottom)*scale;
    value.x += left;
    value.y += top;
    value.width = std::max(0.0F,
        value.width - left - right);
    value.height = std::max(0.0F,
        value.height - top - bottom);
    return value;
}

[[nodiscard]] float scaled_preferred_width(const WidgetPtr& widget,
                                           const float scale) noexcept {
    return preferred_ui_size(widget).width * scale;
}

[[nodiscard]] float scaled_preferred_height(const WidgetPtr& widget,
                                            const float scale) noexcept {
    return preferred_ui_size(widget).height * scale;
}

// 配置と仕切りのドラッグで、同じ実現可能な制限を使う。
// 最小寸法すら収まらない場合は比例配分で縮め、 二
// つ目を全幅確保したまま最初のペインをゼロにしない。
[[nodiscard]] std::pair<float, float> split_pair_limits(
    float available, float first_minimum, float second_minimum) noexcept {
    first_minimum = std::max(0.0F, first_minimum);
    second_minimum = std::max(0.0F, second_minimum);
    const float total = first_minimum + second_minimum;
    if (total > available && total > 0.0F) {
        first_minimum *= available / total;
        second_minimum = available - first_minimum;
    }
    return {first_minimum, std::max(first_minimum, available - second_minimum)};
}

// 伸縮して余りを受け取る行・列を、固定サイズの部品より先
// に縮める。 全行を等しく縮めると、隣の結果一覧が余りを
// 譲れる場合でも、フォームのラベル・設定が欠けてしまう。
void shrink_grid_tracks(std::vector<float>& sizes,
                        const std::vector<float>& minimums,
                        const std::vector<float>& weights,
                        const float target,
                        const bool allow_below_minimum) {
    float shortage = std::max(0.0F,
        std::accumulate(sizes.begin(), sizes.end(), 0.0F) - target);
    const auto reduce = [&](const bool grabbing_only) {
        float capacity{};
        for (std::size_t index = 0; index < sizes.size(); ++index)
            if (!grabbing_only || weights[index] > 0.0F)
                capacity += std::max(0.0F, sizes[index] - minimums[index]);
        if (capacity <= 0.0F || shortage <= 0.0F) return;
        const float amount = std::min(shortage, capacity);
        for (std::size_t index = 0; index < sizes.size(); ++index)
            if (!grabbing_only || weights[index] > 0.0F)
                sizes[index] -= amount *
                    std::max(0.0F, sizes[index] - minimums[index]) / capacity;
        shortage -= amount;
    };
    reduce(true);
    reduce(false);
    if (allow_below_minimum && shortage > 0.001F) {
        const float total = std::accumulate(sizes.begin(), sizes.end(), 0.0F);
        const float factor = target / std::max(0.0001F, total);
        for (auto& size : sizes) size = std::max(0.0F, size * factor);
    }
}

}  // namespace

UiContainerLayout resolve_ui_container_layout(
    const Container& container, const UiLayoutParameters parameters) noexcept {
    const float scale = std::max(0.01F, parameters.scale);
    const auto layout = container.layout();
    const bool split = dynamic_cast<const SplitView*>(&container) != nullptr;
    const bool scroll = dynamic_cast<const ScrollView*>(&container) != nullptr;
    UiContainerLayout result;
    result.kind = scroll ? LayoutKind::Scroll
        : layout ? layout->kind() : split ? LayoutKind::Split : LayoutKind::Column;
    result.spacing = scaled_ui_metric(layout ? layout->spacing
        : split ? parameters.split_spacing : kEditorUiMetrics.default_spacing) * scale;
    // ALL はコンパクトな埋め込みエディタ。入れ子の分割ペインが、
    // 一覧・シーン・フラグ領域の間に残る2～7pxの隙間の原因と
    // なるため調整する。 単独版はD版の指定の分割間隔を維持する。
    if (editor_window_host_options().all_mode && split)
        result.spacing = 0.0F;
    // ネイティブの ScrolledComposite・表示領域と同様、
    // クライアント原点から始める。 汎用のコンテナー余白を入れると、明示
    // 的なゼロ配置を設定していない文書キャンバスの上・左に空白ができる。
    result.padding = scaled_insets(layout ? layout->padding
        : (split || scroll) ? Insets{} : kEditorUiMetrics.default_padding, scale);
    const bool group_box = dynamic_cast<const GroupBox*>(&container) != nullptr;
    if (group_box) {
        // GroupBox の見出しは枠の余白の一部に
        // すぎない。 Grid/Linear の余白
        // がゼロのままでも入力欄・ラベルを枠線上に置
        // かないよう、未指定の辺だけに共通の7pxの
        // 枠余白を補う。 明示的な余白はD版の配置を
        // 表すため維持する。 意図した5pxを7px
        // へ置き換えると、コンパクトな並列グループが
        // 広がり、最後のラジオボタンの文字が欠ける。
        const float frame_padding = scaled_ui_metric(
            kEditorUiMetrics.group_content_padding) * scale;
        if (result.padding.left <= 0.0F) result.padding.left = frame_padding;
        if (result.padding.right <= 0.0F) result.padding.right = frame_padding;
        if (result.padding.bottom <= 0.0F) result.padding.bottom = frame_padding;
        result.padding.top = std::max(result.padding.top,
            ui_text_line_height(kEditorUiMetrics.group_caption_padding, 4,
                container.font() ? &*container.font() : nullptr) * scale);
    }
    if (editor_window_host_options().all_mode) {
        // ALL の低い標準キャンバスでは、コンテナーの装飾
        // 領域を少し減らす。 水平余白とGroupBox見出
        // しの間隔は保ち、縦の余白・行間だけを詰める。 ALL
        // ダイアログの外側の縦余白・行間は、通常の ALL
        // 値の半分にする。 compact_all_dialog_metrics
        // は GroupBox やその内側
        // には設定せず、内部余白は変えない。
        const bool compact_dialog = container.compact_all_dialog_metrics;
        const float vertical_scale = compact_dialog ? 0.75F * 0.5F : 0.75F;
        if (!group_box) result.padding.top *= vertical_scale;
        result.padding.bottom *= vertical_scale;
        const bool vertical_split = split &&
            static_cast<const SplitView&>(container).orientation == Orientation::Vertical;
        if (result.kind == LayoutKind::Column || result.kind == LayoutKind::Grid ||
            result.kind == LayoutKind::Form || vertical_split)
            result.spacing *= compact_dialog ? 0.85F * 0.5F : 0.85F;
    }
    return result;
}

TabLayoutSnapshot snapshot_tab_layout(const TabView& tabs) {
    return {tabs.bounds(),tabs.font(),tabs.pages,tabs.selected_index,tabs.header_viewport,
        tabs.header_horizontal_padding,tabs.chrome_inset,tabs.header_visible,
        tabs.enabled(),tabs.visible(),tabs.header_update_revision,
        editor_ui_text_scale_generation(),tabs.operation_lifetime(),
        resolved_tab_vertical_extra(tabs)};
}

bool tab_layout_is_current(const TabLayoutSnapshot& s,const TabView& tabs) noexcept {
    if(s.lifetime.expired()) return false;
    const auto owner=tabs.operation_lifetime();
    if(s.lifetime.owner_before(owner) || owner.owner_before(s.lifetime)) return false;
    const auto number=[](float a,float b){return a==b || (std::isnan(a)&&std::isnan(b));};
    const auto bounds=tabs.bounds();
    if(s.revision!=tabs.header_update_revision || s.text_scale_generation!=editor_ui_text_scale_generation() ||
       s.enabled!=tabs.enabled() || s.visible!=tabs.visible() || s.selected_index!=tabs.selected_index ||
       s.header_visible!=tabs.header_visible ||
       !number(s.header_horizontal_padding,tabs.header_horizontal_padding) ||
       !number(s.header_vertical_extra,resolved_tab_vertical_extra(tabs)) ||
       !number(s.chrome_inset,tabs.chrome_inset) ||
       !number(s.bounds.x,bounds.x) || !number(s.bounds.y,bounds.y) ||
       !number(s.bounds.width,bounds.width) || !number(s.bounds.height,bounds.height) ||
       s.font.has_value()!=tabs.font().has_value() || s.pages.size()!=tabs.pages.size()) return false;
    if(s.font) {
        const auto& f=*s.font; const auto& g=*tabs.font();
        if(f.family!=g.family || !number(f.points,g.points) || f.weight!=g.weight ||
           f.italic!=g.italic || f.underline!=g.underline || f.strikeout!=g.strikeout ||
           f.size_mode!=g.size_mode) return false;
    }
    const auto& a=s.header_viewport; const auto& b=tabs.header_viewport;
    if(a.first!=b.first || a.revealed_selection!=b.revealed_selection ||
       !number(a.available_width,b.available_width) || a.measured_widths.size()!=b.measured_widths.size()) return false;
    for(std::size_t i=0;i<a.measured_widths.size();++i)
        if(!number(a.measured_widths[i],b.measured_widths[i])) return false;
    for(std::size_t i=0;i<s.pages.size();++i) {
        const auto& p=s.pages[i]; const auto& q=tabs.pages[i];
        if(p.title!=q.title || p.icon!=q.icon || p.content!=q.content || p.closable!=q.closable) return false;
    }
    return true;
}

Rect layout_tab_content(const TabLayoutSnapshot& tabs,const UiLayoutParameters parameters) noexcept {
    const float scale = std::max(0.01F, parameters.scale);
    const float horizontal_inset = scaled_ui_metric(
        resolved_tab_horizontal_padding(tabs.chrome_inset)) * scale;
    const float bottom_inset = tabs.header_visible
        ? scaled_ui_metric(tabs.chrome_inset >= 0.0F ? tabs.chrome_inset : 2.0F) * scale
        : 0.0F;
    const float header = tabs.header_visible
        ? (ui_text_line_height(kEditorUiMetrics.tab_height, 4,
               tabs.font ? &*tabs.font : nullptr) +
           scaled_ui_metric(std::max(0.0F, tabs.header_vertical_extra)) * 2.0F) * scale
        : 0.0F;
    return {tabs.bounds.x + horizontal_inset, tabs.bounds.y + header,
        std::max(0.0F, tabs.bounds.width - horizontal_inset * 2.0F),
        std::max(0.0F, tabs.bounds.height - header - bottom_inset)};
}

Rect layout_tab_content(const Rect tab_bounds,
                        const UiLayoutParameters parameters, const FontSpec* font) noexcept {
    const float scale = std::max(0.01F, parameters.scale);
    const float border = scaled_ui_metric(kMinimumTabHorizontalPadding) * scale;
    const float header = ui_text_line_height(kEditorUiMetrics.tab_height, 4, font) * scale;
    return {
        tab_bounds.x + border,
        tab_bounds.y + header,
        std::max(0.0F, tab_bounds.width - border * 2.0F),
        std::max(0.0F, tab_bounds.height - header - border),
    };
}

Rect layout_tab_content(const TabView& tabs, const UiLayoutParameters parameters) {
    return layout_tab_content(snapshot_tab_layout(tabs), parameters);
}

TabHeaderLayout layout_tab_headers(const TabView& tabs, UiLayoutParameters parameters) {
    return layout_tab_headers(snapshot_tab_layout(tabs),parameters);
}

TabHeaderLayout layout_tab_headers(const TabLayoutSnapshot& tabs, UiLayoutParameters parameters) {
    TabHeaderLayout result;
    if (!tabs.header_visible) {
        result.content = layout_tab_content(tabs, parameters);
        return result;
    }
    const float scale = std::max(.01F, parameters.scale);
    const Rect bounds = tabs.bounds;
    const float horizontal_inset = scaled_ui_metric(
        resolved_tab_horizontal_padding(tabs.chrome_inset)) * scale;
    const float vertical_inset = (tabs.chrome_inset >= 0.0F
        ? scaled_ui_metric(tabs.chrome_inset) : scaled_ui_metric(1.0F)) * scale;
    const float height = (ui_text_line_height(kEditorUiMetrics.tab_height, 4, tabs.font ? &*tabs.font : nullptr) +
        scaled_ui_metric(std::max(0.0F, tabs.header_vertical_extra)) * 2.0F) * scale;
    const float room = std::max(0.0F, bounds.width - 2.0F * horizontal_inset);
    float total{};
    result.natural_widths.reserve(tabs.pages.size());
    for (const auto& page : tabs.pages) {
        const auto width = preferred_tab_width(
            page, tabs.font ? &*tabs.font : nullptr,
            tabs.header_horizontal_padding) * scale;
        result.natural_widths.push_back(width);
        total += width;
    }
    const bool overflow = total > room + .01F;
    const float arrow = overflow ? std::min(height, room / 3.0F) : 0.0F;
    const float content_width = std::max(0.0F, room - arrow * 2.0F);
    result.content = {bounds.x + horizontal_inset, bounds.y + vertical_inset, content_width,
                      std::max(0.0F, height - vertical_inset)};
    const std::size_t first = overflow && !tabs.pages.empty()
        ? std::min(tabs.header_viewport.first, tabs.pages.size() - 1U) : 0U;
    float x = bounds.x + horizontal_inset;
    const float right = x + content_width;
    std::size_t index = first;
    for (; index < tabs.pages.size() && x < right; ++index) {
        const auto& page = tabs.pages[index];
        const float natural = result.natural_widths[index];
        const float width = std::min(natural, right - x);
        TabHeaderItem item{index, {x, bounds.y + vertical_inset, width,
            std::max(0.0F, height - vertical_inset)}, {}};
        if (page.closable && width >= scaled_ui_metric(24.0F) * scale) {
            const float close_width = scaled_ui_metric(18.0F) * scale;
            item.close = {x + width - close_width, item.bounds.y, close_width, item.bounds.height};
        }
        result.items.push_back(item);
        x += width;
        if (width + .01F < natural) break;
    }
    if (overflow) {
        result.previous = {right, bounds.y + vertical_inset, arrow, std::max(0.0F, height - vertical_inset)};
        result.next = {right + arrow, bounds.y + vertical_inset, arrow, std::max(0.0F, height - vertical_inset)};
        result.can_previous = first > 0U;
        result.can_next = index < tabs.pages.size() && first + 1U < tabs.pages.size();
    }
    return result;
}

std::optional<SplitSashHit> split_sash_at(
    const SplitView& split, const float x, const float y,
    const float minimum_hit_radius) noexcept {
    std::vector<WidgetPtr> children;
    for (const auto& child : split.children())
        if (child && child->visible()) children.push_back(child);
    if (children.size() < 2U) return std::nullopt;

    const bool horizontal = split.orientation == Orientation::Horizontal;
    const float pointer = horizontal ? x : y;
    for (std::size_t index = 0; index + 1U < children.size(); ++index) {
        const Rect first = children[index]->bounds();
        const Rect second = children[index + 1U]->bounds();
        const float first_end = horizontal
            ? first.x + first.width : first.y + first.height;
        const float second_start = horizontal ? second.x : second.y;
        const float gap = std::max(0.0F, second_start - first_end);
        const float boundary = first_end + gap * 0.5F;
        const float radius = std::max(
            std::max(0.0F, minimum_hit_radius), gap * 0.5F + 2.0F);
        if (std::fabs(pointer - boundary) <= radius)
            return SplitSashHit{index, boundary, gap};
    }
    return std::nullopt;
}

bool resize_split_pair_at(SplitView& split, const std::size_t index,
                          const float coordinate, const float minimum_size) {
    return resize_split_pair_at(split, index, coordinate, minimum_size,
                                SplitResizePhase::Commit);
}

bool resize_split_pair_at(SplitView& split, const std::size_t index,
                           const float coordinate, const float minimum_size,
                           const SplitResizePhase phase) {
    return resize_split_pair_at(split, index, coordinate, minimum_size,
                                minimum_size, phase);
}

bool resize_split_pair_at(SplitView& split, const std::size_t index,
                           const float coordinate, const float minimum_first,
                           const float minimum_second,
                           const SplitResizePhase phase) {
    std::vector<WidgetPtr> children;
    for (const auto& child : split.children())
        if (child && child->visible()) children.push_back(child);
    if (index + 1U >= children.size()) return false;

    const bool horizontal = split.orientation == Orientation::Horizontal;
    const Rect first_bounds = children[index]->bounds();
    const Rect second_bounds = children[index + 1U]->bounds();
    const float first_current = horizontal ? first_bounds.width : first_bounds.height;
    const float second_current = horizontal ? second_bounds.width : second_bounds.height;
    const float combined = std::max(0.0F, first_current + second_current);
    if (combined <= 0.0F) return false;
    // 下の重み付き配置と同じ制限を使う。安定した主ドックの分割では指
    // 定されたペイン最小寸法を使い、 小さいペインを広げようとした
    // ときに、選択ページの自然寸法が仕切りを固定しないようにする。
    const bool use_child_minimums = !split.use_explicit_edge_minimums &&
        (split.constrain_to_child_minimums || all_dialog_font_layout());
    const auto drag_minimum = [use_child_minimums](const WidgetPtr& child) {
        if (use_child_minimums) return minimum_ui_size(child);
        const auto& minimum = child->layout_item().minimum;
        return Size{scaled_ui_metric(minimum.width),
                    scaled_ui_metric(minimum.height)};
    };
    const auto first_minimum = drag_minimum(children[index]);
    const auto second_minimum = drag_minimum(children[index + 1U]);
    // 明示的な端の最小値ゼロは、D版の SplitPane
    // が使う折り畳み許可であり、子の推奨最小値を足す
    // 指定ではない。 足すと、配置経路が保持できる真の
    // 0pxペインを、共通の枠ドラッグで作れなくなる。
    const float first_limit = minimum_first <= 0.0F ? 0.0F :
        (split.use_explicit_edge_minimums
             ? minimum_first
             : std::max(minimum_first,
                        horizontal ? first_minimum.width : first_minimum.height));
    const float second_limit = minimum_second <= 0.0F ? 0.0F :
        (split.use_explicit_edge_minimums
             ? minimum_second
             : std::max(minimum_second,
                        horizontal ? second_minimum.width : second_minimum.height));
    const auto [lower, upper] = split_pair_limits(combined, first_limit,
                                                   second_limit);
    const float origin = horizontal ? first_bounds.x : first_bounds.y;
    const float first_size = std::clamp(coordinate - origin, lower, upper);
    const float second_size = std::max(0.0F, combined - first_size);

    std::vector<float> weights(children.size(), 0.001F);
    for (std::size_t child = 0; child < children.size(); ++child) {
        const Rect bounds = children[child]->bounds();
        weights[child] = std::max(
            0.001F, horizontal ? bounds.width : bounds.height);
    }
    weights[index] = minimum_first <= 0.0F ? std::max(0.0F, first_size)
                                           : std::max(0.001F, first_size);
    weights[index + 1U] = minimum_second <= 0.0F ? std::max(0.0F, second_size)
                                                  : std::max(0.001F, second_size);
    const bool had_initial_extent = split.initial_first_extent.has_value() ||
        split.initial_second_extent.has_value() ||
        split.initial_first_extent_offset.has_value();
    split.initial_first_extent.reset();
    split.initial_second_extent.reset();
    split.initial_first_extent_offset.reset();
    const bool changed = had_initial_extent || split.weights != weights;
    if (changed) split.weights = std::move(weights);
    if (changed && split.on_weights_previewed)
        split.on_weights_previewed(split.weights);
    if (phase == SplitResizePhase::Commit && split.on_weights_changed)
        split.on_weights_changed(split.weights);
    return changed;
}

void commit_split_resize(SplitView& split) {
    if (split.on_drag_committed) split.on_drag_committed();
    if (split.on_weights_changed) split.on_weights_changed(split.weights);
}

Size cool_bar_item_preferred_size(const CoolBar* bar, Size content) noexcept {
    return {content.width + (!bar || !bar->locked ? scaled_ui_metric(kEditorUiMetrics.cool_bar_gripper) : 0.0F), content.height};
}

CoolBarLayout layout_cool_bar(const CoolBar& bar, Rect available,
                              const UiLayoutParameters parameters) {
    CoolBarLayout result;
    struct Band { WidgetPtr widget; Insets margin; bool visible; std::optional<CoolBarBandSize> size; };
    std::vector<Band> bands;
    for (const auto& child:bar.children()) {
        std::optional<CoolBarBandSize> size;
        for (const auto& band:bar.band_sizes)
            if (band && band->owner.lock()==child) {size=*band;break;}
        bands.push_back({child,child ? child->layout_item().margin : Insets{},child && child->visible(),size});
    }
    const auto wraps=bar.wrap_indices;
    const bool locked=bar.locked;
    const float scale = std::max(.01F, parameters.scale);
    const auto resolved = resolve_ui_container_layout(bar, parameters);
    const auto padding = resolved.padding;
    const float room = std::max(0.0F, available.width - padding.left - padding.right);
    const float grip = locked ? 0.0F : scaled_ui_metric(parameters.cool_bar_gripper) * scale;
    float x{}, y{}, row_height{}, right{};
    for (std::size_t index = 0; index < bands.size(); ++index) {
        const auto& child = bands[index].widget;
        if (!child || !bands[index].visible || std::dynamic_pointer_cast<Window>(child)) continue;
        const auto margin = scaled_insets(bands[index].margin, scale);
        const auto& size=bands[index].size;
        const float natural=preferred_ui_size(child).width * scale;
        // setSize は帯全体の幅を要求する。 ネイティブの子の最小
        // サイズと共通の枠の合計を下限とし、理想サイズは独立して扱う。
        const float desired=size ? std::max(size->minimumSize.width * scale + grip,
            size->currentSize.value_or(size->preferredSize).width * scale) - grip : natural;
        const float width = std::min(std::max(0.0F, room - grip),
                                    desired + margin.left + margin.right);
        const float total = width + grip;
        const bool forced = std::find(wraps.begin(), wraps.end(), index) != wraps.end();
        if (x > 0.0F && (forced || (!bar.single_row && x + total > room + .01F))) {
            y += row_height + resolved.spacing;
            row_height = 0.0F;
            x = 0.0F;
        }
        const float content_width = std::max(0.0F, width - margin.left - margin.right);
        const float measured_height=preferred_ui_size_for_width(child, content_width / scale).height * scale;
        const float requested_height=size ? std::max(size->minimumSize.height,
            size->currentSize.value_or(size->preferredSize).height) * scale : 0.0F;
        const float height = std::max(measured_height,requested_height) + margin.top + margin.bottom;
        Rect rect{available.x + padding.left + x + grip,
                  available.y + padding.top + y, width, height};
        result.placements.push_back({child, apply_ui_margin(rect, bands[index].margin, scale), true});
        right = std::max(right, x + total);
        row_height = std::max(row_height, height);
        x += total + resolved.spacing;
    }
    result.size = {right + padding.left + padding.right,
                   y + row_height + padding.top + padding.bottom};
    return result;
}

Rect cool_bar_grip_bounds(const Widget& child, UiLayoutParameters parameters) noexcept {
    const auto bounds = child.bounds();
    const float width = scaled_ui_metric(parameters.cool_bar_gripper) * std::max(.01F, parameters.scale);
    return {bounds.x - width, bounds.y, width, bounds.height};
}

std::optional<std::size_t> cool_bar_item_at(const CoolBar& bar, Point point,
                                           bool grip_only, UiLayoutParameters parameters) noexcept {
    if (bar.locked || !bar.enabled() || !bar.visible()) return std::nullopt;
    for (std::size_t index = 0; index < bar.children().size(); ++index) {
        const auto& child = bar.children()[index];
        if (!child || !child->visible()) continue;
        auto rect = cool_bar_grip_bounds(*child, parameters);
        if (!grip_only) rect.width += child->bounds().width;
        if (point.x >= rect.x && point.x < rect.x + rect.width &&
            point.y >= rect.y && point.y < rect.y + rect.height) return index;
    }
    return std::nullopt;
}

namespace {
std::vector<UiPlacement> compute_ui_children(
    const std::shared_ptr<Container>& container,
    Rect available,
    const UiLayoutParameters parameters, PendingScrollLayout& pending_scroll,
    bool& provider_completed, std::optional<Point> requested_scroll = {}) {
    const float scale = std::max(0.01F, parameters.scale);
    std::vector<UiPlacement> result;
    provider_completed=true;
    if (!container) return result;
    if (const auto cool = std::dynamic_pointer_cast<CoolBar>(container)) {
        auto packed = layout_cool_bar(*cool, available, parameters);
        return packed.placements;
    }
    const LayoutPtr layout = container->layout();
    const auto split_container = std::dynamic_pointer_cast<SplitView>(container);
    const auto resolved = resolve_ui_container_layout(*container, parameters);
    const LayoutKind kind = resolved.kind;
    const float spacing = resolved.spacing;
    const Insets padding = resolved.padding;
    Rect inner{available.x + padding.left, available.y + padding.top,
               std::max(0.0F, available.width - padding.left - padding.right),
               std::max(0.0F, available.height - padding.top - padding.bottom)};
    std::vector<WidgetPtr> children;
    for (const auto& child : container->children()) {
        if (child) children.push_back(child);
    }
    if (children.empty()) {
        if (const auto scroll = std::dynamic_pointer_cast<ScrollView>(container)) {
            pending_scroll={scroll,{}, {}};
        }
        return result;
    }
    result.reserve(children.size());

    const auto add = [&](const WidgetPtr& widget, Rect bounds, bool visible = true) {
        if (!widget) return;
        bounds = apply_ui_margin(bounds, widget->layout_item().margin, scale);
        result.push_back({widget, bounds, visible});
    };
    if (const auto& provider=container->scaled_child_layout_provider(); provider) {
        const auto prepared=provider(available, scale);
        invalidate_measurement_cache(); // アプリケーションコードがツリーを変更している場合がある。
        std::unordered_map<const Widget*,Rect> custom;
        custom.reserve(prepared.size());
        for (const auto& entry:prepared) {
            if (!entry.widget || std::find(children.begin(),children.end(),entry.widget)==children.end() ||
                !custom.emplace(entry.widget.get(),entry.bounds).second) {
                provider_completed=false;
                return {};
            }
        }
        for (const auto& child:children) {
            const auto found=custom.find(child.get());
            add(child,found==custom.end()?child->bounds():found->second,child->visible());
        }
        return result;
    }
    if (!layout && std::dynamic_pointer_cast<Window>(container)) {
        float top = available.y;
        float bottom = available.y + available.height;
        for (const auto& widget : children) {
            if (!widget->visible() || widget->kind() != ComponentKind::StatusBar)
                continue;
            const float height = scaled_preferred_height(widget, scale);
            bottom -= height;
            add(widget, {available.x, bottom, available.width, height});
        }
        for (const auto& widget : children) {
            if (widget->kind() == ComponentKind::StatusBar) continue;
            if (std::dynamic_pointer_cast<Window>(widget)) {
                add(widget, widget->bounds(), widget->visible());
                continue;
            }
            const bool top_chrome = widget->kind() == ComponentKind::ToolBar ||
                widget->kind() == ComponentKind::CoolBar ||
                (parameters.reserve_menu_bar &&
                 widget->kind() == ComponentKind::MenuBar);
            if (top_chrome) {
                const float height = preferred_ui_size_for_width(widget, available.width / scale).height * scale;
                add(widget, {available.x, top, available.width, height}, widget->visible());
                if (widget->visible()) top += height;
            } else if (widget->kind() == ComponentKind::MenuBar) {
                add(widget, widget->bounds(), false);
            } else {
                add(widget, {available.x, top, available.width,
                             std::max(0.0F, bottom - top)}, widget->visible());
            }
        }
        return result;
    }
    if (const auto expand = std::dynamic_pointer_cast<ExpandBar>(container)) {
        struct SectionInput {
            WidgetPtr widget;
            ExpandItemMeasurement metrics;
            Insets margin;
            bool visible{}, open{};
        };
        std::vector<SectionInput> sections;
        sections.reserve(expand->children().size());
        // nullの枠も含め、元のインデックスと expanded の対応を維持する。
        for (std::size_t i=0; i<expand->children().size(); ++i) {
            const auto& widget=expand->children()[i];
            if (!widget) continue;
            sections.push_back({widget,prepare_expand_item_measurement(*widget),
                widget->layout_item().margin,widget->visible(),expand_item_is_open(*expand,i)});
        }
        float cursor = inner.y;
        bool placed{};
        for (const auto& section : sections) {
            const auto& widget = section.widget;
            const bool visible = section.visible;
            if (visible && placed) cursor += spacing;
            const auto metrics = measure_expand_item(section.metrics);
            const float header_height = metrics.header_height * scale;
            const float height = section.open
                ? std::max(header_height, scaled_preferred_height(widget, scale) +
                    metrics.content_offset * scale)
                : header_height;
            result.push_back({widget,apply_ui_margin({inner.x,cursor,inner.width,height},
                section.margin,scale),visible});
            if (visible) {
                cursor += height;
                placed = true;
            }
        }
        return result;
    }
    if (kind == LayoutKind::Absolute) {
        float cursor = inner.y;
        for (const auto& widget : children) {
            if (!widget->visible()) {
                add(widget, widget->bounds(), false);
                continue;
            }
            if (std::dynamic_pointer_cast<Menu>(widget) ||
                std::dynamic_pointer_cast<Window>(widget)) {
                add(widget, widget->bounds());
                continue;
            }
            Rect bounds = widget->bounds();
            const auto& item = widget->layout_item();
            const bool explicitly_sized = bounds.width > 0.0F ||
                                           bounds.height > 0.0F;
            if (item.horizontal == Align::Stretch &&
                item.vertical == Align::Stretch && !explicitly_sized) {
                bounds = inner;
            } else {
                // 上端・左端に置くコントロールは、正当な絶対座
                // 標としてゼロを使う。RadarSpinner
                // は両方を使う。 子が明示的なサイズを指定し
                // たら、そのゼロを位置未指定と読み替えない。
                if (bounds.width <= 0.0F)
                    bounds.width = std::max(1.0F, inner.width);
                if (bounds.height <= 0.0F)
                    bounds.height = scaled_preferred_height(widget, scale);
                if (!explicitly_sized) {
                    if (bounds.x == 0.0F) bounds.x = inner.x;
                    if (bounds.y == 0.0F) bounds.y = cursor;
                }
            }
            cursor = std::max(cursor, bounds.y + bounds.height + spacing);
            add(widget, bounds);
        }
        return result;
    }
    if (kind == LayoutKind::Fill) {
        for (const auto& widget : children) {
            if (std::dynamic_pointer_cast<Menu>(widget) ||
                std::dynamic_pointer_cast<Window>(widget)) {
                add(widget, widget->bounds());
                continue;
            }
            if (widget->kind() == ComponentKind::ComboBox) {
                // 領域を埋めるドロップダウンも、高さは自然な値にして中央に置く。
                const float height = std::min(inner.height,
                    scaled_preferred_height(widget, scale));
                add(widget, {inner.x, inner.y + (inner.height - height) * 0.5F,
                             inner.width, height});
                continue;
            }
            add(widget, inner);
        }
        return result;
    }
    if (kind == LayoutKind::Scroll) {
        const auto scroll = std::dynamic_pointer_cast<ScrollView>(container);
        const bool width_dependent_content=scroll &&
            !editor_window_host_options().all_mode &&
            std::any_of(children.begin(),children.end(),contains_child_layout_provider);
        std::vector<Size> sizes;
        Size content{padding.left + padding.right, padding.top + padding.bottom};
        for (const auto& widget : children) {
            Size size{};
            if (widget->visible()) {
                size = {scaled_preferred_width(widget, scale), scaled_preferred_height(widget, scale)};
                const auto managed_container=std::dynamic_pointer_cast<Container>(widget);
                const bool managed=managed_container && (managed_container->on_layout ||
                    (managed_container->layout() && managed_container->layout()->kind()!=LayoutKind::Absolute));
                if (width_dependent_content && managed)
                    size.height=preferred_ui_size_for_width(widget,available.width/scale).height*scale;
                // 旧式の手動配置の描画文書は、引き続き境界を提供す
                // る。 管理された配置や明示的な推奨サイズでは、
                // 前フレームで引き伸ばした寸法を計測へ戻さない。
                if (!managed && widget->layout_item().preferred.width <= 0)
                    size.width = std::max(size.width, widget->bounds().width);
                if (!managed && widget->layout_item().preferred.height <= 0)
                    size.height = std::max(size.height, widget->bounds().height);
                content.width = std::max(content.width, size.width + padding.left + padding.right);
                content.height = std::max(content.height, size.height + padding.top + padding.bottom);
            }
            sizes.push_back(size);
        }
        if (scroll) {
            const auto source_offset=requested_scroll.value_or(scroll->scroll_offset);
            auto geometry = pixel_scroll_geometry(available, content, source_offset,
                scroll->horizontal_scroll, scroll->vertical_scroll);
            if (width_dependent_content) {
                const float document_width=std::max(0.0F,geometry.viewport.width-
                    padding.left-padding.right)/scale;
                content={padding.left+padding.right,padding.top+padding.bottom};
                for (std::size_t index=0;index<children.size();++index) {
                    const auto& widget=children[index];
                    if (!widget || !widget->visible()) continue;
                    const auto nested=std::dynamic_pointer_cast<Container>(widget);
                    const bool managed=nested && (nested->on_layout ||
                        (nested->layout() && nested->layout()->kind()!=LayoutKind::Absolute));
                    if (managed && contains_child_layout_provider(widget))
                        sizes[index].height=preferred_ui_size_for_width(widget,document_width).height*scale;
                    content.width=std::max(content.width,sizes[index].width+padding.left+padding.right);
                    content.height=std::max(content.height,sizes[index].height+padding.top+padding.bottom);
                }
                geometry=pixel_scroll_geometry(available,content,source_offset,
                    scroll->horizontal_scroll,scroll->vertical_scroll);
            }
            pending_scroll={scroll,content,{
                std::clamp(source_offset.x, 0.0F, geometry.horizontal.maximum),
                std::clamp(source_offset.y, 0.0F, geometry.vertical.maximum)}};
            inner.width = std::max(0.0F, geometry.viewport.width - padding.left - padding.right);
            inner.height = std::max(0.0F, geometry.viewport.height - padding.top - padding.bottom);
        }
        std::size_t index{};
        for (const auto& widget : children) {
            const auto size = sizes[index++];
            if (!widget->visible()) {
                add(widget, widget->bounds(), false);
                continue;
            }
            const auto hint = widget->layout_item();
            float width = hint.horizontal == Align::Stretch
                ? inner.width : size.width;
            float height = hint.vertical == Align::Stretch
                ? inner.height : size.height;
            if (scroll && scroll->horizontal_scroll &&
                hint.horizontal == Align::Stretch)
                width = std::max(width, size.width);
            if (scroll && scroll->vertical_scroll &&
                hint.vertical == Align::Stretch)
                height = std::max(height, size.height);
            float x = inner.x - (scroll ? pending_scroll.offset.x : 0.0F);
            float y = inner.y - (scroll ? pending_scroll.offset.y : 0.0F);
            // スクロール領域を暗黙の FillLayout として扱わな
            // い。 DWTの ScrolledComposite と同様
            // 、Start/Center/End では固定サイズのシーン
            // ・文書を保ち、Stretchだけで広げる。 配置を共通の
            // 形状計算に含め、OSの表示側に別の寸法規則を作らせない。
            if (width < inner.width) {
                if (hint.horizontal == Align::Center)
                    x += (inner.width - width) * 0.5F;
                else if (hint.horizontal == Align::End)
                    x += inner.width - width;
            }
            if (height < inner.height) {
                if (hint.vertical == Align::Center)
                    y += (inner.height - height) * 0.5F;
                else if (hint.vertical == Align::End)
                    y += inner.height - height;
            }
            add(widget, {x, y, width, height});
        }
        return result;
    }
    if (kind == LayoutKind::Stack) {
        const auto* stack = dynamic_cast<const StackLayout*>(layout.get());
        const std::size_t active = stack ? stack->active_index : 0U;
        std::size_t index = 0;
        for (const auto& widget : children) add(widget, inner, index++ == active);
        return result;
    }
    // 非表示のコントロールは、行・列や分割の重みを確保しない。
    // DWTの呼び出し元は表示状態で任意の区画や FillLayout
    // のビューを切り替えるため、 割り当てに残すと大きい空
    // 白行や兄弟の移動が生じる。 StackLayout は
    // 選択位置が絞り込み前の順序を表すので、上で別に扱う。
    children.erase(std::remove_if(children.begin(), children.end(),
                                  [](const WidgetPtr& widget) {
                                      return !widget || !widget->visible();
                                  }),
                   children.end());
    if (children.empty()) return result;
    const bool horizontal = kind == LayoutKind::Row ||
                            (kind == LayoutKind::Split &&
                             (dynamic_cast<const SplitView*>(container.get()) == nullptr ||
                              dynamic_cast<const SplitView*>(container.get())->orientation == Orientation::Horizontal));
    if (kind == LayoutKind::Grid || kind == LayoutKind::Form) {
        std::size_t columns = kind == LayoutKind::Form ? 2U : 1U;
        if (const auto* grid = dynamic_cast<const GridLayout*>(layout.get()))
            columns = std::max<std::size_t>(1U, grid->columns);
        std::size_t max_row = 0U;
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& widget = children[index];
            const auto& item = widget->layout_item();
            // SWTと同様、GridData がなく配置が
            // 未指定の子は、挿入順に次のセルへ置く。 既定
            // の LayoutItem::row=0 を使
            // うと計測が一行分になり、 実際の配置では二行
            // 目以降の高さがゼロとなる。 MenuCardDialog
            // のように明示・自動配置が混在しても、
            // 全グループを表示できるようにする。
            const std::size_t row = widget->has_explicit_grid_position()
                                        ? item.row
                                        : index / columns;
            max_row = std::max(max_row, row +
                                        std::max<std::size_t>(1U, item.row_span));
        }
        if (max_row == 0U) max_row = (children.size() + columns - 1U) / columns;
        std::vector<float> widths(columns, 0.0F), heights(max_row, 0.0F);
        std::vector<float> minimum_widths(columns, 0.0F),
                           minimum_heights(max_row, 0.0F);
        std::vector<float> column_weights(columns, 0.0F), row_weights(max_row, 0.0F);
        // 一列幅のStretchの子（コンボ・入力欄など）を持つ列。
        std::vector<char> stretch_columns(columns, 0);
        const bool constrained_compound =
            container->layout_item().constrain_minimum_to_preferred && !all_dialog_font_layout();
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& item = children[index]->layout_item();
            // GridとSplitで、同じ再帰的・文字倍率対応の
            // 最小寸法を使う。 種類別の別一覧では、200%のラ
            // ベルにも古い16pxの下限を使い、内側のパネル内容
            // も無視してしまう。 制約付き複合部品はSWTの固定
            // Compositeと同様、指定領域内で暗黙の装飾部
            // 品を意図的に切り詰める。 明示的な子の最小値は守り
            // 、通常のフォームは再帰的な自然最小値を維持する。
            const auto intrinsic_minimum = constrained_compound
                ? Size{scaled_ui_metric(item.minimum.width),
                       scaled_ui_metric(item.minimum.height)}
                : minimum_ui_size(children[index]);
            const bool automatic = !children[index]->has_explicit_grid_position();
            const std::size_t row = automatic ? index / columns : item.row;
            const std::size_t column = automatic ? index % columns : item.column;
            const auto column_span = std::min(
                widths.size() - std::min(column, widths.size()),
                std::max<std::size_t>(1U, item.column_span));
            const auto row_span = std::min(
                heights.size() - std::min(row, heights.size()),
                std::max<std::size_t>(1U, item.row_span));
            if (column < widths.size() && column_span > 0U) {
                float minimum_occupied = spacing * static_cast<float>(column_span - 1U);
                for (std::size_t track = column; track < column + column_span; ++track)
                    minimum_occupied += minimum_widths[track];
                const float minimum_deficit =
                    std::max(0.0F, intrinsic_minimum.width * scale - minimum_occupied);
                if (minimum_deficit > 0.0F)
                    for (std::size_t track = column; track < column + column_span; ++track)
                        minimum_widths[track] +=
                            minimum_deficit / static_cast<float>(column_span);
                float occupied = spacing * static_cast<float>(column_span - 1U);
                for (std::size_t track = column; track < column + column_span; ++track)
                    occupied += widths[track];
                const float deficit = std::max(0.0F, scaled_preferred_width(children[index], scale) - occupied);
                if (deficit > 0.0F)
                    for (std::size_t track = column; track < column + column_span; ++track)
                        widths[track] += deficit / static_cast<float>(column_span);
                if (item.horizontal == Align::Stretch && item.grow > 0.0F)
                    for (std::size_t track = column; track < column + column_span; ++track)
                        column_weights[track] = std::max(
                            column_weights[track], item.grow / static_cast<float>(column_span));
                if (item.horizontal == Align::Stretch && column_span == 1U &&
                    column < stretch_columns.size())
                    stretch_columns[column] = 1;
            }
            if (row < heights.size() && row_span > 0U) {
                float minimum_occupied = spacing * static_cast<float>(row_span - 1U);
                for (std::size_t track = row; track < row + row_span; ++track)
                    minimum_occupied += minimum_heights[track];
                const float requested_minimum =
                    intrinsic_minimum.height * scale;
                const float minimum_deficit =
                    std::max(0.0F, requested_minimum - minimum_occupied);
                if (minimum_deficit > 0.0F)
                    for (std::size_t track = row; track < row + row_span; ++track)
                        minimum_heights[track] +=
                            minimum_deficit / static_cast<float>(row_span);
                float occupied = spacing * static_cast<float>(row_span - 1U);
                for (std::size_t track = row; track < row + row_span; ++track)
                    occupied += heights[track];
                const float deficit = std::max(0.0F, scaled_preferred_height(children[index], scale) - occupied);
                if (deficit > 0.0F)
                    for (std::size_t track = row; track < row + row_span; ++track)
                        heights[track] += deficit / static_cast<float>(row_span);
                // SWTの GridData と同様、行・列の
                // 伸縮と子の配置を独立させる。 中央寄せのラジ
                // オボタンは、行に余った高さを受け取りつつ、本
                // 体は自然な高さに保てる。 ここでStretch
                // を必須にすると、自然寸法の部品を使う能力・
                // 属性ダイアログの行が上へつぶれてしまう。
                if (item.grow > 0.0F)
                    for (std::size_t track = row; track < row + row_span; ++track)
                        row_weights[track] = std::max(
                            row_weights[track], item.grow / static_cast<float>(row_span));
            }
        }
        const float total_spacing_x = spacing * static_cast<float>(columns > 0 ? columns - 1U : 0U);
        const float total_spacing_y = spacing * static_cast<float>(max_row > 0 ? max_row - 1U : 0U);
        // 行・列は、内容の最小寸法より狭くしない。初期値は
        // 子の推奨寸法だが、 指定値が実測内容より小さい場
        // 合も、余白の有無にかかわらず最小値を守る。 例え
        // ば43pxの設定行に ALL の大きいフォント
        // の数値欄が収まらない場合も、 ウィンドウの最小
        // 値計算と一致させる。constrained_compound
        // の意図した切り取りは維持する。
        if (!constrained_compound) {
            for (std::size_t track = 0; track < widths.size(); ++track)
                widths[track] = std::max(widths[track], minimum_widths[track]);
            for (std::size_t track = 0; track < heights.size(); ++track)
                heights[track] = std::max(heights[track], minimum_heights[track]);
        }
        const auto* grid = dynamic_cast<const GridLayout*>(layout.get());
        if (grid && grid->equal_width) {
            std::fill(widths.begin(), widths.end(),
                      std::max(0.0F, (inner.width - total_spacing_x) / static_cast<float>(columns)));
        } else {
            const float target = std::max(0.0F, inner.width - total_spacing_x);
            const float natural = std::accumulate(widths.begin(), widths.end(), 0.0F);
            if (natural < target && !widths.empty()) {
                float weight_sum = std::accumulate(column_weights.begin(), column_weights.end(), 0.0F);
                if (weight_sum <= 0.0F) {
                    // 余った幅の要求先がなければ、最後の列へ
                    // 無条件に渡すのではなく、 子が伸縮す
                    // る最後の列（アイコンボタンの前のコン
                    // ボなど）へ渡す。 入力欄と固定サイズ
                    // の末尾ボタンの間に空白帯を作らない。
                    std::size_t target_column = widths.size() - 1U;
                    for (std::size_t track = widths.size(); track-- > 0U;)
                        if (stretch_columns[track]) { target_column = track; break; }
                    column_weights[target_column] = 1.0F;
                    weight_sum = 1.0F;
                }
                for (std::size_t track = 0; track < widths.size(); ++track)
                    widths[track] += (target - natural) * column_weights[track] / weight_sum;
            } else if (natural > target && natural > 0.0F) {
                shrink_grid_tracks(widths, minimum_widths, column_weights, target, constrained_compound);
            }
        }
        // 幅に依存するコントロールの高さは、前フレ
        // ームの境界ではなく最終列幅から求める。
        // 折り返すラベルやCoolBarも含め、狭
        // いときは複数行、広げたら少ない行にする。
        for(std::size_t index=0;index<children.size();++index) {
            const auto& widget=children[index];
            const auto label=std::dynamic_pointer_cast<TextWidget>(widget);
            const bool wrapped_label = label && label->wrap &&
                (widget->kind()==ComponentKind::Label || widget->kind()==ComponentKind::Link);
            // ツール欄も折り返すため、最終幅に応じて高さを決める。
            const auto custom_container=std::dynamic_pointer_cast<Container>(widget);
            const bool provider_layout=custom_container &&
                custom_container->child_layout_provider() &&
                !editor_window_host_options().all_mode;
            const bool width_dependent = wrapped_label ||
                widget->kind()==ComponentKind::CoolBar ||
                widget->kind()==ComponentKind::ToolBar || provider_layout;
            if(!width_dependent) continue;
            const auto& item=widget->layout_item();
            const bool automatic=!widget->has_explicit_grid_position();
            const auto row=automatic ? index/columns : item.row;
            const auto column=automatic ? index%columns : item.column;
            if(row>=heights.size() || column>=widths.size()) continue;
            const auto row_end=row+std::min(heights.size()-row,std::max<std::size_t>(1,item.row_span));
            const auto col_end=column+std::min(widths.size()-column,std::max<std::size_t>(1,item.column_span));
            float width=spacing*static_cast<float>(col_end-column-1);
            for(auto col=column;col<col_end;++col) width+=widths[col];
            const auto margin=scaled_insets(item.margin,scale);
            const float available_width=std::max(1.0F,width-margin.left-margin.right)/scale;
            if(widget->kind()==ComponentKind::ToolBar) {
                // 実際に複数行へ折り返す欄だけを高く
                // し、一行なら指定の高さを維持する。
                const float one_row=preferred_ui_size_for_width(widget,
                    std::numeric_limits<float>::max()).height;
                if(preferred_ui_size_for_width(widget,available_width).height<=one_row) continue;
            }
            const float need=preferred_ui_size_for_width(widget,
                available_width).height*scale+margin.top+margin.bottom;
            float occupied=spacing*static_cast<float>(row_end-row-1);
            for(auto track=row;track<row_end;++track) occupied+=heights[track];
            const float deficit=std::max(0.0F,need-occupied)/static_cast<float>(row_end-row);
            for(auto track=row;track<row_end;++track) {
                heights[track]+=deficit;
                minimum_heights[track]=std::max(minimum_heights[track],heights[track]);
            }
        }
        {
            const float target = std::max(0.0F, inner.height - total_spacing_y);
            const float natural = std::accumulate(heights.begin(), heights.end(), 0.0F);
            if (natural < target && !heights.empty()) {
                const float weight_sum = std::accumulate(row_weights.begin(), row_weights.end(), 0.0F);
                if (weight_sum > 0.0F)
                    for (std::size_t track = 0; track < heights.size(); ++track)
                        heights[track] += (target - natural) * row_weights[track] / weight_sum;
            } else if (natural > target && natural > 0.0F) {
                shrink_grid_tracks(heights, minimum_heights, row_weights, target, constrained_compound);
            }
        }
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& widget = children[index];
            const auto& item = widget->layout_item();
            const bool automatic = !widget->has_explicit_grid_position();
            const std::size_t row = automatic ? index / columns : item.row;
            const std::size_t column = automatic ? index % columns : item.column;
            float x = inner.x;
            for (std::size_t col = 0; col < column && col < widths.size(); ++col) x += widths[col] + spacing;
            float y = inner.y;
            for (std::size_t r = 0; r < row && r < heights.size(); ++r) y += heights[r] + spacing;
            float width = 0.0F;
            for (std::size_t col = column; col < std::min(widths.size(), column + std::max<std::size_t>(1U, item.column_span)); ++col)
                width += widths[col] + (col + 1U < std::min(widths.size(), column + std::max<std::size_t>(1U, item.column_span)) ? spacing : 0.0F);
            float height = 0.0F;
            for (std::size_t r = row; r < std::min(heights.size(), row + std::max<std::size_t>(1U, item.row_span)); ++r)
                height += heights[r] + (r + 1U < std::min(heights.size(), row + std::max<std::size_t>(1U, item.row_span)) ? spacing : 0.0F);

            // SWTの GridData と同様、割り当てた
            // セルの内側でコントロールを配置する。 セル全体
            // をネイティブ部品の矩形にすると、中央寄せのラジ
            // オボタンや小さい数値欄が ダイアログ全幅の枠・
            // 帯に広がってしまう。 行・列の割り当ては変え
            // ず、最終的な子の矩形だけをその内側で揃える。
            const auto text_label=std::dynamic_pointer_cast<TextWidget>(widget);
            const bool wrap_label=text_label && text_label->wrap &&
                (widget->kind()==ComponentKind::Label || widget->kind()==ComponentKind::Link);
            // SWTの GridData と同様、
            // 通常の一行ラベルは既定で縦中央に置く
            // 。 本体を行高へ伸ばすと字形が上に
            // 残り、Text・Comboの隣でず
            // れて見える。 ドロップダウンも一行
            // の部品なので、行・ダイアログの高さ
            // に従わず、自然な高さで中央に置く。
            const Align vertical_alignment =
                item.vertical == Align::Stretch &&
                        ((text_label && !wrap_label &&
                          (widget->kind() == ComponentKind::Label ||
                           widget->kind() == ComponentKind::Link)) ||
                         widget->kind() == ComponentKind::ComboBox)
                    ? Align::Center
                    : item.vertical;
            const Size preferred = wrap_label ? preferred_ui_size_for_width(widget,width/scale) : preferred_ui_size(widget);
            const float preferred_width = std::max(
                scaled_ui_metric(item.minimum.width) * scale, preferred.width * scale);
            float preferred_height = std::max(
                scaled_ui_metric(item.minimum.height) * scale, preferred.height * scale);
            // 伸縮しない配置の子は推奨高さで置くが、
            // 内容の実測最小値よりは小さくしない。
            if (!constrained_compound)
                preferred_height = std::max(preferred_height,
                    minimum_ui_size(widget).height * scale);
            if (item.horizontal != Align::Stretch) {
                const float child_width = std::min(width, preferred_width);
                if (item.horizontal == Align::Center)
                    x += (width - child_width) * 0.5F;
                else if (item.horizontal == Align::End)
                    x += width - child_width;
                width = child_width;
            }
            if (vertical_alignment != Align::Stretch) {
                const float child_height = std::min(height, preferred_height);
                if (vertical_alignment == Align::Center)
                    y += (height - child_height) * 0.5F;
                else if (vertical_alignment == Align::End)
                    y += height - child_height;
                height = child_height;
            }
            add(widget, {x, y, width, height});
        }
        return result;
    }
    // 行・列・分割で、同じ重み付きの割り当て処理を共有する。
    const auto* split = split_container.get();
    std::vector<float> weights;
    if (split) weights = split->weights;
    const float main = horizontal ? inner.width : inner.height;
    const float free = std::max(0.0F, main -
        spacing * static_cast<float>(children.size() - 1U));
    std::vector<float> sizes(children.size(), 0.0F);
    std::vector<float> effective_weights(children.size(), 0.0F);
    std::vector<float> minimums(children.size(), 0.0F);
    float occupied = 0.0F;
    float weight_sum = 0.0F;
    const bool preserve_form_minimums = !split &&
        (!container->layout_item().constrain_minimum_to_preferred || all_dialog_font_layout());
    const bool preserve_split_child_minimums = split &&
        !split->use_explicit_edge_minimums &&
        (split->constrain_to_child_minimums || all_dialog_font_layout());
    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto& item = children[index]->layout_item();
        const auto measured_minimum = preserve_form_minimums || preserve_split_child_minimums
            ? minimum_ui_size(children[index])
            : Size{scaled_ui_metric(item.minimum.width), scaled_ui_metric(item.minimum.height)};
        const float intrinsic_minimum = (horizontal ? measured_minimum.width : measured_minimum.height) * scale;
        if (split && split->use_explicit_edge_minimums &&
            children.size() == 2U && index < 2U) {
            const float edge_minimum = index == 0U ? split->drag_minimum_first
                                                    : split->drag_minimum_second;
            minimums[index] = scaled_ui_metric(edge_minimum) * scale;
        } else {
            minimums[index] = preserve_form_minimums ? intrinsic_minimum : std::max(intrinsic_minimum,
                horizontal ? scaled_ui_metric(kEditorUiMetrics.flexible_minimum_width) * scale
                           : scaled_ui_metric(kEditorUiMetrics.flexible_minimum_height) * scale);
        }
        float weight = split && index < weights.size()
                           ? std::max(0.0F, weights[index])
                           : std::max(0.0F, item.grow);
        // 縦列の中でも、ドロップダウンは自然な高さを維持する。
        if (!split && !horizontal && children[index]->kind() == ComponentKind::ComboBox)
            weight = 0.0F;
        if (split && index < weights.size() && weight <= 0.0F &&
            ((index == 0U && split->drag_minimum_first <= 0.0F) ||
             (index == 1U && split->drag_minimum_second <= 0.0F)))
            minimums[index] = 0.0F;
        // 横並びの行では、編集欄を伸縮部分と
        // する。 ラベル・ボタンは自然なサ
        // イズに保ち、編集欄がクリップ領域
        // の外へ押し出されないようにする。
        if (!split && horizontal && weight <= 0.0F &&
            (children[index]->kind() == ComponentKind::TextEdit ||
             children[index]->kind() == ComponentKind::ComboBox) &&
            item.preferred.width <= 0.0F)
            weight = 1.0F;
        effective_weights[index] = weight;
        if (weight > 0.0F) {
            weight_sum += weight;
            sizes[index] = minimums[index];
        } else if (split && index < weights.size() && weight <= 0.0F &&
                   minimums[index] <= 0.0F) {
            sizes[index] = 0.0F;
        } else {
            sizes[index] = std::max(
                                    preserve_form_minimums ? minimums[index] :
                                        scaled_ui_metric(horizontal ? item.minimum.width : item.minimum.height) * scale,
                                    horizontal ? scaled_preferred_width(children[index], scale)
                                               : preferred_ui_size_for_width(children[index], inner.width / scale).height * scale);
        }
        occupied += sizes[index];
    }
    if (split && children.size() == 2U && weight_sum > 0.0F) {
        // SplitView の重みは、SWTの SplitPane#setWeights
        // と同じ直接の比率で扱う。 汎用の重み
        // 付き行配置は自然サイズを先に足して余り
        // を分けるため、 仕切りがモデルの比率か
        // らずれ、特にMainWindowの縦3:10
        // 分割で当たり判定が合わなくなる。
        const auto [first_minimum, available_first] = split_pair_limits(
            free, minimums[0], minimums[1]);
        // D版の ResizeL と同様、初期の最大化配置後に保存済みの重みを一度適用し直す。 印
        // を付けたその SplitPane の処理だけで PreserveFirst を迂回する。
        const bool initial_maximized_reflow=
            split->reflow_on_initial_maximized_geometry &&
            split->initial_maximized_reflow_pending;
        float requested_first = free * effective_weights[0] / weight_sum;
        if (!initial_maximized_reflow && split->committed_split_layout &&
            split->resize_mode != SplitResizeMode::Ratio) {
            const auto& prior = *split->committed_split_layout;
            if (effective_weights[0] <= 0.0F) requested_first = 0.0F;
            else if (effective_weights[1] <= 0.0F) requested_first = free;
            else requested_first = split->resize_mode == SplitResizeMode::PreserveFirst
                ? prior.first : free - prior.second;
        }
        if (!initial_maximized_reflow &&
            (split->resize_mode == SplitResizeMode::Ratio || !split->committed_split_layout)) {
            if (split->initial_first_extent)
                requested_first = *split->initial_first_extent * scale;
            else if (split->initial_second_extent)
                requested_first = free - *split->initial_second_extent * scale;
            if (split->initial_first_extent_offset &&
                std::isfinite(*split->initial_first_extent_offset))
                requested_first += *split->initial_first_extent_offset * scale;
        }
        sizes[0] = std::clamp(requested_first,
                              first_minimum,
                              available_first);
        sizes[1] = std::max(0.0F, free - sizes[0]);
    } else if (occupied > free && occupied > 0.0F) {
        if (preserve_form_minimums) {
            // Gridと同様、幅や高さが足りない場合は伸縮部分から先に縮
            // める。実測最小値すら収まらない場合は共通ダイアログ・ScrollView
            // 側ではみ出しを扱い、ここで文字をつぶさない。
            shrink_grid_tracks(sizes, minimums, effective_weights, free, false);
        } else {
            const float factor = free / occupied;
            for (auto& size : sizes) size = std::max(1.0F, size * factor);
        }
    } else if (weight_sum > 0.0F) {
        const float remaining = free - occupied;
        for (std::size_t index = 0; index < children.size(); ++index)
            if (effective_weights[index] > 0.0F)
                sizes[index] += remaining * effective_weights[index] / weight_sum;
    }
    float cursor = horizontal ? inner.x : inner.y;
    const Align cross = layout && kind == LayoutKind::Row ? dynamic_cast<const LinearLayout*>(layout.get())->cross_axis : Align::Stretch;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto& widget = children[index];
        const auto& item = widget->layout_item();
        float cross_preferred = horizontal ? scaled_preferred_height(widget, scale) : scaled_preferred_width(widget, scale);
        // Gridと同様、交差方向の寸法も子の実測最小値を
        // 下回らない。推奨指定はそれより小さい場合がある。
        if (preserve_form_minimums) {
            const auto cross_minimum = minimum_ui_size(widget);
            cross_preferred = std::max(cross_preferred,
                (horizontal ? cross_minimum.height : cross_minimum.width) * scale);
        }
        Align align = horizontal ? item.vertical : item.horizontal;
        // 上のGridと同様、ドロップダウンの高さは行に合わせて伸ばさない。
        if (horizontal && align == Align::Stretch && widget->kind() == ComponentKind::ComboBox)
            align = Align::Center;
        float cross_size = (align == Align::Stretch && cross == Align::Stretch) ?
                           (horizontal ? inner.height : inner.width) : cross_preferred;
        float cross_origin = horizontal ? inner.y : inner.x;
        if (align == Align::Center) cross_origin += ((horizontal ? inner.height : inner.width) - cross_size) / 2.0F;
        else if (align == Align::End) cross_origin += (horizontal ? inner.height : inner.width) - cross_size;
        Rect bounds = horizontal ? Rect{cursor, cross_origin, sizes[index], cross_size} :
                                   Rect{cross_origin, cursor, cross_size, sizes[index]};
        add(widget, bounds);
        cursor += sizes[index] + spacing;
    }
    return result;
}

} // namespace

Size measure_center_layout(const std::span<const Size> children, const float margin,
                           float width_hint, float height_hint) noexcept {
    if (width_hint == -1.0F) width_hint = 0.0F;
    if (height_hint == -1.0F) height_hint = 0.0F;
    for (const auto child : children) {
        width_hint = std::max(width_hint, child.width);
        height_hint = std::max(height_hint, child.height);
    }
    return {width_hint + margin * 2.0F, height_hint + margin * 2.0F};
}

Size resolve_center_layout_preferred_size(const Size preferred, const Size minimum,
                                          const Size fallback) noexcept {
    return {preferred.width > 0.0F ? preferred.width
            : minimum.width > 0.0F ? minimum.width : fallback.width,
            preferred.height > 0.0F ? preferred.height
            : minimum.height > 0.0F ? minimum.height : fallback.height};
}

Rect plan_center_layout_item(const CenterLayoutItemRequest& request) noexcept {
    std::optional<float> x, y, width, height;
    if (request.anchors) {
        const auto& edge = *request.anchors;
        if (edge.right && edge.left) width = *edge.right - *edge.left;
        else if (edge.right || edge.left) width = request.preferred.width;
        if (edge.top && edge.bottom) height = *edge.bottom - *edge.top;
        else if (edge.top || edge.bottom) height = request.preferred.height;
        if (width) x = edge.left ? *edge.left
            : request.client.x + request.client.width - *width - *edge.right;
        if (height) y = edge.top ? *edge.top
            : request.client.y + request.client.height - *height - *edge.bottom;
    }
    if (request.explicit_bounds) {
        x = request.explicit_bounds->x; y = request.explicit_bounds->y;
        width = request.explicit_bounds->width; height = request.explicit_bounds->height;
    }
    if (request.fill_horizontal) {
        if (!x) x = request.client.x + request.margin;
        if (!width) width = request.client.width - request.margin * 2.0F;
    } else {
        if (!x) x = request.client.x + request.margin +
            (request.center_horizontal ?
                (request.client.width - request.margin * 2.0F - request.preferred.width) / 2.0F :
                0.0F);
        if (!width) width = request.preferred.width;
    }
    if (request.fill_vertical) {
        if (!y) y = request.client.y + request.margin;
        if (!height) height = request.client.height - request.margin * 2.0F;
    } else {
        if (!y) y = request.client.y + request.margin +
            (request.center_vertical ?
                (request.client.height - request.margin * 2.0F - request.preferred.height) / 2.0F :
                0.0F);
        if (!height) height = request.preferred.height;
    }
    return {*x, *y, *width, *height};
}

std::vector<UiPlacement> layout_ui_children(const std::shared_ptr<Container>& source,
    Rect available, UiLayoutParameters parameters, bool* completed) {
    if (completed) *completed=false;
    const auto container=source;
    if (!container) { if (completed) *completed=true; return {}; }
    const auto revision=++container->layout_update_revision;
    // 一括処理の準備と同様、追加の再帰スナップショットが必要なのは実際のコールバ
    // ックがある場合だけ。 計算自体は、下の提供処理用ガードが引き続き保護する。
    std::optional<ContainerLayoutGuard> callback_guard;
    if (container->on_layout) callback_guard.emplace(container);
    const bool visible=container->visible();
    const auto layout_callback=container->on_layout;
    if (layout_callback) layout_callback(available);
    const bool source_released=!source;
    if (container->layout_update_revision!=revision || (visible && !container->visible()) ||
        (!source_released && callback_guard && !callback_guard->current())) return {};
    const ContainerLayoutGuard guard(container);
    PendingScrollLayout pending_scroll;
    bool provider_completed=true;
    auto result=compute_ui_children(container,available,parameters,pending_scroll,provider_completed);
    if (!provider_completed || !guard.current()) return {};
    // 確定区間では、コールバック・メモリ確保・計測を行わない。
    if (pending_scroll.owner) {
        pending_scroll.owner->content_extent=pending_scroll.extent;
        pending_scroll.owner->scroll_offset=pending_scroll.offset;
    }
    for (const auto& placement:result) placement.widget->set_bounds(placement.bounds);
    if (completed) *completed=true;
    return result;
}

namespace {
struct PreparedWindowLayout {
    std::shared_ptr<Window> owner;
    Size extent{};
    Point offset{};
};
struct PreparedTabLayout {
    std::shared_ptr<TabView> owner;
    TabLayoutSnapshot source;
    TabHeaderViewport viewport;
};
struct PreparedSplitLayout {
    std::shared_ptr<SplitView> owner;
    SplitLayoutSnapshot snapshot;
    std::vector<float> weights;
};
struct PreparedLayoutTree {
    std::vector<UiPlacement> placements;
    std::vector<PendingScrollLayout> scrolls;
    std::vector<PreparedWindowLayout> windows;
    std::vector<PreparedTabLayout> tabs;
    std::vector<PreparedSplitLayout> splits;
    std::vector<std::unique_ptr<ContainerLayoutGuard>> guards;
    std::unordered_set<const Widget*> visited;
    std::unordered_map<Container*,Point> requested_scrolls;
    bool current() const noexcept {
        for (const auto& guard:guards) if (!guard || !guard->current()) return false;
        for (const auto& tab:tabs)
            if (!tab.owner || !tab_layout_is_current(tab.source,*tab.owner)) return false;
        return true;
    }
    void commit() noexcept {
        for (const auto& window:windows) {
            window.owner->content_extent=window.extent;
            window.owner->content_offset=window.offset;
        }
        for (const auto& scroll:scrolls) {
            scroll.owner->content_extent=scroll.extent;
            scroll.owner->scroll_offset=scroll.offset;
        }
        for (auto& tab:tabs) {
            ++tab.owner->header_update_revision;
            tab.owner->header_viewport=std::move(tab.viewport);
        }
        for (const auto& placement:placements)
            if (placement.widget) placement.widget->set_bounds(placement.bounds);
        // 反映時にはアプリのコールバックもメモリ確保も行わない
        // 。 派生状態より先に、この処理の全配置を確定する。
        for (auto& split:splits) {
            split.owner->weights.swap(split.weights);
            split.owner->committed_split_layout=split.snapshot;
            split.owner->initial_first_extent.reset();
            split.owner->initial_second_extent.reset();
            split.owner->initial_first_extent_offset.reset();
            if(split.owner->reflow_on_initial_maximized_geometry)
                split.owner->initial_maximized_reflow_pending=false;
        }
    }
};

bool prepare_layout_widget(const WidgetPtr&,Rect,PreparedLayoutTree&);

bool prepare_layout_container(const std::shared_ptr<Container>& container,Rect available,
    PreparedLayoutTree& plan,std::optional<Point> requested_scroll={}) {
    if (!container) return true;
    if (!plan.visited.insert(container.get()).second) return false;
    if (!requested_scroll) {
        if (const auto found=plan.requested_scrolls.find(container.get());
            found!=plan.requested_scrolls.end()) requested_scroll=found->second;
    }
    if (const auto window=std::dynamic_pointer_cast<Window>(container);
        window && window->kind()==ComponentKind::Dialog) {
        const ContainerLayoutGuard minimum_guard(container);
        const auto minimum=window_content_minimum_size(*window);
        if (!minimum_guard.current()) return false;
        const auto source_offset=requested_scroll.value_or(window->content_offset);
        const auto geometry=pixel_scroll_geometry(available,minimum,source_offset);
        const Point offset{std::clamp(source_offset.x,0.0F,geometry.horizontal.maximum),
                           std::clamp(source_offset.y,0.0F,geometry.vertical.maximum)};
        plan.windows.push_back({window,minimum,offset});
        available={geometry.viewport.x-offset.x,geometry.viewport.y-offset.y,
            std::max(geometry.viewport.width,minimum.width),
            std::max(geometry.viewport.height,minimum.height)};
        requested_scroll.reset();
    }
    const auto revision=++container->layout_update_revision;
    // 検証用ガードが必要なのはアプリのコールバックをまたぐ場合
    // 。 コールバックがなければ再入による変更境界もなく、各
    // 階層で全子孫を複製すると同じ作業を繰り返す。 処理元の
    // ガードと、コンテナーごとの提供処理用ガードは維持する。
    std::optional<ContainerLayoutGuard> callback_guard;
    if (container->on_layout) callback_guard.emplace(container);
    const bool visible=container->visible();
    const auto callback=container->on_layout;
    if (callback) {
        callback(available);
        // A layout callback may change the tree or its measurements.
        invalidate_measurement_cache();
    }
    if (container->layout_update_revision!=revision || (visible && !container->visible()) ||
        (callback_guard && !callback_guard->current())) return false;
    // 子孫は下で準備し、それぞれ自身のガードを持つ。 親の
    // 再帰ガードでは、その正当な子の変更番号の更新を外部変
    // 更と誤認し、 入れ子の処理をすべて拒否してしまう。
    auto guard=std::make_unique<ContainerLayoutGuard>(container,false);
    PendingScrollLayout pending;
    const UiLayoutParameters parameters{1.0F,kEditorUiMetrics.split_spacing,
        kEditorUiMetrics.cool_bar_gripper,true};
    bool provider_completed=true;
    auto placements=compute_ui_children(container,available,parameters,pending,
        provider_completed,requested_scroll);
    if (!provider_completed || !guard->current()) return false;
    const auto* parent_guard=guard.get();
    plan.guards.push_back(std::move(guard));
    if (pending.owner) plan.scrolls.push_back(pending);
    if (auto split=std::dynamic_pointer_cast<SplitView>(container);
        split && split->resize_mode!=SplitResizeMode::Ratio && placements.size()==2U &&
        placements[0].widget && placements[1].widget) {
        const bool horizontal=split->orientation==Orientation::Horizontal;
        const auto& first=placements[0].bounds;
        const auto& second=placements[1].bounds;
        const float first_extent=horizontal ? first.width : first.height;
        const float second_extent=horizontal ? second.width : second.height;
        SplitLayoutSnapshot snapshot{first_extent,second_extent,
            horizontal ? available.width : available.height};
        snapshot.sash_x=horizontal ? first.x+first.width-available.x : first.x-available.x;
        snapshot.sash_y=horizontal ? first.y-available.y : first.y+first.height-available.y;
        snapshot.sash_width=horizontal ? std::max(0.0F,second.x-first.x-first.width) : first.width;
        snapshot.sash_height=horizontal ? first.height : std::max(0.0F,second.y-first.y-first.height);
        plan.splits.push_back({split,snapshot,{first_extent,second_extent}});
    }
    const auto expand=std::dynamic_pointer_cast<ExpandBar>(container);
    for (const auto& placement:placements) {
        if (!parent_guard->current()) return false;
        if (!placement.widget) continue;
        plan.placements.push_back(placement);
        if (!placement.visible || !placement.widget->visible()) continue;
        if (expand) {
            if (!expand_child_is_open(*expand,placement.widget.get())) continue;
            if (const auto body=std::dynamic_pointer_cast<Container>(placement.widget)) {
                const ContainerLayoutGuard body_guard(body,false);
                auto bounds=placement.bounds;
                const auto offset=expand_item_content_offset(*body);
                if (!body_guard.current()) return false;
                bounds.y+=offset;
                bounds.height=std::max(0.0F,bounds.height-offset);
                if (!prepare_layout_container(body,bounds,plan)) return false;
            } else if (!prepare_layout_widget(placement.widget,placement.bounds,plan)) return false;
        } else if (!prepare_layout_widget(placement.widget,placement.bounds,plan)) return false;
    }
    return true;
}

bool prepare_layout_widget(const WidgetPtr& widget,Rect bounds,PreparedLayoutTree& plan) {
    if (!widget) return true;
    if (std::dynamic_pointer_cast<Window>(widget)) return true;
    if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
        if (!plan.visited.insert(tabs.get()).second) return false;
        const auto source=snapshot_tab_layout(*tabs);
        auto prepared=source;
        prepared.bounds=bounds;
        prepared.header_viewport=resolved_tab_header_viewport(prepared);
        if (!tab_layout_is_current(source,*tabs)) return false;
        const auto content=layout_tab_content(prepared);
        plan.tabs.push_back({tabs,source,prepared.header_viewport});
        if (prepared.selected_index && *prepared.selected_index<prepared.pages.size()) {
            const auto& page=prepared.pages[*prepared.selected_index];
            if (page.content) {
                plan.placements.push_back({page.content,content,page.content->visible()});
                if (page.content->visible() && !prepare_layout_widget(page.content,content,plan)) return false;
            }
        }
        return true;
    }
    if (const auto child=std::dynamic_pointer_cast<Container>(widget))
        return prepare_layout_container(child,bounds,plan);
    return true;
}

bool arrange_container_transaction(const std::shared_ptr<Container>& container,Rect bounds,
    std::optional<Point> requested_scroll) {
    const ::cwxeditor::lib::EditorPerformanceScope performance("layout.transaction");
    if (!container) return true;
    const DialogFontLayoutScope font_scope(container->kind()==ComponentKind::Dialog);
    // 処理全体で一つの計測メモを使う（MeasurementCacheScope参照）。
    const MeasurementCacheScope measurement_cache;
    // 再帰的な配置準備は、正当に各 Container の
    // 処理変更番号を進める。 それ以外の計測・構造に影響す
    // る状態は、下の一回の確定まで同じでなければならない。
    const ContainerLayoutGuard source_guard(container,true,false);
    PreparedLayoutTree plan;
    if (requested_scroll) {
        ++container->scroll_update_revision;
        plan.requested_scrolls.emplace(container.get(),*requested_scroll);
    }
    if (!prepare_layout_container(container,bounds,plan) || !plan.current() || !source_guard.current()) return false;
    plan.commit();
    return true;
}

Rect prepared_bounds(const PreparedLayoutTree& plan,const WidgetPtr& widget) {
    for (auto it=plan.placements.rbegin();it!=plan.placements.rend();++it)
        if (it->widget==widget) return it->bounds;
    return widget ? widget->bounds() : Rect{};
}

bool reveal_control_transaction(const WidgetPtr& scope,const WidgetPtr& control) {
    const auto root=std::dynamic_pointer_cast<Container>(scope);
    if (!root || !control) return false;
    const auto path=input_scope_path(scope,control);
    if (path.empty()) return false;
    const MeasurementCacheScope measurement_cache;
    PreparedLayoutTree measured;
    if (!prepare_layout_container(root,root->bounds(),measured) || !measured.current() ||
        input_scope_path(scope,control)!=path) return false;
    Rect target=prepared_bounds(measured,control);
    std::unordered_map<Container*,Point> requests;
    for (auto it=path.rbegin();it!=path.rend();++it) {
        if (*it==control) continue;
        const auto container=std::dynamic_pointer_cast<Container>(*it);
        const auto scroll=std::dynamic_pointer_cast<ScrollView>(*it);
        const auto window=std::dynamic_pointer_cast<Window>(*it);
        if (!container || (!scroll && (!window || window->kind()!=ComponentKind::Dialog))) continue;
        const auto owner_bounds=prepared_bounds(measured,*it);
        Size extent{}; Point old{}; bool horizontal=true,vertical=true;
        if (scroll) {
            horizontal=scroll->horizontal_scroll; vertical=scroll->vertical_scroll;
            old=scroll->scroll_offset;
            for (const auto& value:measured.scrolls) if (value.owner==scroll) {
                extent=value.extent; old=value.offset; break;
            }
        } else {
            old=window->content_offset;
            for (const auto& value:measured.windows) if (value.owner==window) {
                extent=value.extent; old=value.offset; break;
            }
        }
        const auto geometry=pixel_scroll_geometry(owner_bounds,extent,old,horizontal,vertical);
        Point delta{};
        if (target.x<geometry.viewport.x) delta.x=target.x-geometry.viewport.x;
        else if (target.x+target.width>geometry.viewport.x+geometry.viewport.width)
            delta.x=std::min(target.x-geometry.viewport.x,
                target.x+target.width-geometry.viewport.x-geometry.viewport.width);
        if (target.y<geometry.viewport.y) delta.y=target.y-geometry.viewport.y;
        else if (target.y+target.height>geometry.viewport.y+geometry.viewport.height)
            delta.y=std::min(target.y-geometry.viewport.y,
                target.y+target.height-geometry.viewport.y-geometry.viewport.height);
        const Point next{std::clamp(old.x+delta.x,0.0F,geometry.horizontal.maximum),
                         std::clamp(old.y+delta.y,0.0F,geometry.vertical.maximum)};
        if (next.x!=old.x || next.y!=old.y) {
            requests[container.get()]=next;
            target.x-=next.x-old.x; target.y-=next.y-old.y;
        }
    }
    if (requests.empty()) return false;
    for (const auto& [owner,offset]:requests) ++owner->scroll_update_revision;
    PreparedLayoutTree committed;
    committed.requested_scrolls=std::move(requests);
    if (!prepare_layout_container(root,root->bounds(),committed) || !committed.current() ||
        input_scope_path(scope,control)!=path) return false;
    committed.commit();
    return true;
}

bool arrange_widget_transaction(const WidgetPtr& widget,Rect bounds) {
    if (!widget) return true;
    const DialogFontLayoutScope font_scope(widget->kind()==ComponentKind::Dialog);
    const MeasurementCacheScope measurement_cache;
    if (!std::dynamic_pointer_cast<Container>(widget) && !std::dynamic_pointer_cast<TabView>(widget)) {
        widget->set_bounds(bounds);
        return true;
    }
    PreparedLayoutTree plan;
    plan.placements.push_back({widget,bounds,widget->visible()});
    // prepare_layout_widget は、別の表示ルートの下で見つ
    // かった Window を意図的に無視する。 所有・組み込みウィンドウは、
    // 独立したネイティブの配置処理を持つため。 ただし arrange_widget
    // に渡すものは処理のルートなので、 Dialog/Window も
    // arrange_container と同様に自身の内容を配置する。
    const auto root_window=std::dynamic_pointer_cast<Window>(widget);
    const bool prepared=root_window ? prepare_layout_container(root_window,bounds,plan)
                                    : prepare_layout_widget(widget,bounds,plan);
    if (!prepared || !plan.current()) return false;
    plan.commit();
    return true;
}
} // namespace

Rect ComboPopupLayout::row_bounds(const std::size_t row) const noexcept {
    if (row >= visible_rows) return {};
    const float offset = static_cast<float>(row) * row_height;
    return {content.x, content.y + offset, content.width,
            std::max(0.0F, std::min(row_height, content.height - offset))};
}

Rect ComboPopupLayout::action_bounds(std::size_t visible_row, std::size_t action) const noexcept {
    if (action >= action_widths.size()) return {};
    const auto row = row_bounds(visible_row);
    const float gap = scaled_ui_metric(2.0F);
    float left = row.x + row.width - gap;
    for (std::size_t i = action; i < action_widths.size(); ++i) left -= action_widths[i];
    const float right = std::min(row.x + row.width, left + action_widths[action] - gap);
    left = std::max(row.x, left);
    return {left, row.y + gap, std::max(0.0F, right - left), std::max(0.0F, row.height - gap * 2)};
}

std::optional<std::size_t> ComboPopupLayout::item_at(
    const Point point, const std::size_t top, const std::size_t count) const noexcept {
    if (row_height <= 0.0F || point.x < content.x || point.y < content.y ||
        point.x >= content.x + content.width || point.y >= content.y + content.height)
        return std::nullopt;
    const auto row = static_cast<std::size_t>((point.y - content.y) / row_height);
    if (row >= visible_rows || top >= count || row >= count - top)
        return std::nullopt;
    return top + row;
}

std::optional<Rect> combo_popup_scroll_thumb(const ComboPopupLayout& layout,
                                             const std::size_t item_count,
                                             const std::size_t top) noexcept {
    if (!layout.scroll_track || layout.visible_rows == 0U ||
        item_count <= layout.visible_rows) return std::nullopt;
    const auto& track = *layout.scroll_track;
    const auto maximum = item_count - layout.visible_rows;
    const float thumb_height = std::min(track.height, std::max(
        scaled_ui_metric(12.0F), track.height *
        static_cast<float>(layout.visible_rows) / static_cast<float>(item_count)));
    const float travel = std::max(0.0F, track.height - thumb_height);
    const float ratio = static_cast<float>(std::min(top, maximum)) /
                        static_cast<float>(maximum);
    return Rect{track.x, track.y + travel * ratio, track.width, thumb_height};
}

ComboPopupLayout layout_combo_popup(const ComboBox& combo, const Rect viewport,
                                    const std::size_t maximum_rows,
                                    const UiLayoutParameters parameters) noexcept {
    ComboPopupLayout result;
    if (combo.items.empty() || maximum_rows == 0U ||
        viewport.width <= 0.0F || viewport.height <= 0.0F) return result;
    const float scale = std::max(0.01F, parameters.scale);
    const float edge = std::min(scale, std::min(viewport.width, viewport.height) * 0.25F);
    result.row_height = ui_text_line_height(kEditorUiMetrics.row_height, 4, combo.font() ? &*combo.font() : nullptr) * scale * combo.row_height_scale;
    float label_width = 0.0F;
    for (const auto& item : combo.items) {
        const float icon_width = item.icon ? scaled_ui_metric(21.0F) : 0.0F;
        label_width = std::max(label_width, (ui_text_width(item.text, scaled_ui_metric(16.0F),
            combo.font() ? &*combo.font() : nullptr) + icon_width) * scale);
    }
    const auto wanted = std::min(maximum_rows, combo.items.size());
    for (const auto& action : combo.popup_actions) {
        const float width = ui_text_width(action, scaled_ui_metric(16.0F),
            combo.font() ? &*combo.font() : nullptr) * scale;
        result.action_widths.push_back(width);
        label_width += width;
    }
    const float below_y = std::clamp(combo.bounds().y + combo.bounds().height,
                                     viewport.y, viewport.y + viewport.height);
    const float above_y = std::clamp(combo.bounds().y, viewport.y, viewport.y + viewport.height);
    const float below = viewport.y + viewport.height - below_y;
    const float above = above_y - viewport.y;
    const float wanted_height = result.row_height * static_cast<float>(wanted) + edge * 2.0F;
    const bool upward = wanted_height > below && above > below;
    const float available = upward ? above : below;
    // 可能なら完全な行を表示する。上下どちらにも一行すら収まらなければ
    // 、 ホストの外へ描画・当たり判定を広げず、表示領域内に留める。
    const float capacity = std::max(0.0F, available - edge * 2.0F);
    result.visible_rows = std::min(wanted, std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::floor(capacity / result.row_height))));
    // 行ラベルは両側の枠とスクロールバー領域の内側に描く。 アイ
    // コン・間隔・文字余白に加えてその領域を確保し、 最大行数で
    // はなく短い表示領域が理由でスクロールする場合も考慮する。
    const float scrollbar_width = combo.items.size() > result.visible_rows
        ? editor_scrollbar_extent() * scale : 0.0F;
    const float width = std::min(viewport.width, std::max({
        scaled_ui_metric(80.0F) * scale, combo.bounds().width,
        std::ceil(label_width + edge * 2.0F + scrollbar_width)}));
    const float height = std::min(viewport.height,
        result.row_height * static_cast<float>(result.visible_rows) + edge * 2.0F);
    const float x = std::clamp(combo.bounds().x, viewport.x, viewport.x + viewport.width - width);
    const float y = std::clamp(upward ? above_y - height : below_y,
                               viewport.y, viewport.y + viewport.height - height);
    result.bounds = {x, y, width, height};
    result.content = {x + edge, y + edge, std::max(0.0F, width - edge * 2.0F),
                      std::max(0.0F, height - edge * 2.0F)};
    if (combo.items.size() > result.visible_rows) {
        const float gutter = std::min(scrollbar_width, result.content.width);
        result.content.width = std::max(0.0F, result.content.width - gutter);
        result.scroll_track = Rect{result.content.x + result.content.width,
                                   result.content.y, gutter,
                                   result.content.height};
    }
    return result;
}

std::optional<std::size_t> MenuPopupLayout::item_at(const Point point) const noexcept {
    for (const auto& row : rows)
        if (point.x >= row.bounds.x && point.x < row.bounds.x + row.bounds.width &&
            point.y >= row.bounds.y && point.y < row.bounds.y + row.bounds.height)
            return row.index;
    return std::nullopt;
}

MenuPopupLayout layout_menu_popup(const Menu& menu, const Point anchor, const Rect viewport,
                                  const std::size_t top, const std::optional<Rect> parent_row,
                                  const UiLayoutParameters parameters) {
    MenuPopupLayout result;
    if (menu.items.empty() || top >= menu.items.size() || viewport.width <= 0.0F ||
        viewport.height <= 0.0F) return result;
    const float scale = std::max(0.01F, parameters.scale);
    const auto metric = [scale](float value) { return scaled_ui_metric(value) * scale; };
    const float edge = std::min(metric(2.0F), std::min(viewport.width, viewport.height) * 0.25F);
    float label_width{};
    float shortcut_width{};
    bool has_icons{};
    bool has_shortcuts{};
    bool has_submenus{};
    float height = edge * 2.0F;
    for (const auto& item : menu.items) {
        if (item.separator) continue;
        label_width = std::max(label_width,
            ui_text_width(menu_item_label(item),
                                    0.0F, menu.font() ? &*menu.font() : nullptr) * scale);
        shortcut_width = std::max(shortcut_width,
            ui_text_width(menu_item_shortcut(item), 0.0F, menu.font() ? &*menu.font() : nullptr) * scale);
        has_icons = has_icons || item.icon != 0U;
        has_shortcuts = has_shortcuts || !menu_item_shortcut(item).empty();
        has_submenus = has_submenus || item.has_submenu();
    }
    const float check_width = metric(20.0F);
    const float icon_width = has_icons ? metric(20.0F) : 0.0F;
    const float shortcut_gap = has_shortcuts ? metric(16.0F) : 0.0F;
    const float arrow_width = has_submenus ? metric(18.0F) : 0.0F;
    const float padding = metric(4.0F);
    const float width = std::min(viewport.width, std::max(metric(120.0F),
        edge * 2.0F + padding * 2.0F + check_width + icon_width + label_width +
        shortcut_gap + shortcut_width + arrow_width));
    for (std::size_t index = top; index < menu.items.size(); ++index) {
        const float row_height = menu_popup_item_height(menu, index) * scale;
        const float remaining = viewport.height - height;
        if (remaining <= 0.0F || (!result.rows.empty() && row_height > remaining)) break;
        const float used = std::min(row_height, remaining);
        result.rows.push_back({index, {edge, height - edge, width - 2.0F * edge, used}});
        height += used;
    }
    float x = anchor.x;
    if (parent_row && x + width > viewport.x + viewport.width)
        x = parent_row->x - width;
    x = std::clamp(x, viewport.x, viewport.x + viewport.width - width);
    const float y = std::clamp(anchor.y, viewport.y, viewport.y + viewport.height - height);
    result.bounds = {x, y, width, height};
    for (auto& row : result.rows) {
        row.bounds.x += x;
        row.bounds.y += y;
        if (menu.items[row.index].separator) continue;
        float left = row.bounds.x + std::min(padding, row.bounds.width);
        float right = std::max(left, row.bounds.x + row.bounds.width - padding);
        const auto leading = [&](float wanted) {
            const float used = std::min(wanted, right - left);
            const Rect rect{left, row.bounds.y, used, row.bounds.height};
            left += used;
            return rect;
        };
        const auto trailing = [&](float wanted) {
            const float used = std::min(wanted, right - left);
            right -= used;
            return Rect{right, row.bounds.y, used, row.bounds.height};
        };
        row.check = leading(check_width);
        const Rect icon_slot = leading(icon_width);
        const float icon_size = std::min({metric(16.0F), icon_slot.width, icon_slot.height});
        row.icon = {icon_slot.x, icon_slot.y + (icon_slot.height - icon_size) * 0.5F,
                    icon_size, icon_size};
        row.arrow = trailing(arrow_width);
        row.shortcut = trailing(shortcut_width);
        (void)trailing(shortcut_gap);
        row.label = {left, row.bounds.y, right - left, row.bounds.height};
    }
    return result;
}

}  // namespace cwxeditor::lib
