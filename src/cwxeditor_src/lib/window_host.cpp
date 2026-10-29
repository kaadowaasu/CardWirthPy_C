#include "window_host.hpp"
#include "components.hpp"
#include "ui_metrics.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <utility>

namespace cwxeditor::lib {

EditorWindowHostGlobals g_editor_window_host{};

namespace {
EditorDataDirectoryOpener& data_directory_opener() {
    static EditorDataDirectoryOpener opener;
    return opener;
}
}  // namespace

namespace {
std::atomic<ResourceId> g_caption_undo_icon{0U};
std::atomic<ResourceId> g_caption_redo_icon{0U};
} // namespace

void set_hosted_caption_history_icons(const ResourceId undo, const ResourceId redo) noexcept {
    g_caption_undo_icon.store(undo, std::memory_order_release);
    g_caption_redo_icon.store(redo, std::memory_order_release);
}
ResourceId hosted_caption_undo_icon() noexcept {
    return g_caption_undo_icon.load(std::memory_order_acquire);
}
ResourceId hosted_caption_redo_icon() noexcept {
    return g_caption_redo_icon.load(std::memory_order_acquire);
}

namespace {
std::function<void(CursorKind)>& cursor_realizer() {
    static std::function<void(CursorKind)> realizer;
    return realizer;
}
int g_wait_cursor_depth = 0;
std::thread::id g_cursor_thread{};
// シナリオローダーなどのワーカーがウィンドウのカーソル値を変える場合がある。 共
// 有の要求とホストに触れるのは、バックエンドを所有するUIスレッドだけにする。
bool cursor_thread() noexcept {
    return g_cursor_thread == std::thread::id{} || g_cursor_thread == std::this_thread::get_id();
}
} // namespace

void set_cursor_realizer(std::function<void(CursorKind)> realizer) {
    g_cursor_thread = realizer ? std::this_thread::get_id() : std::thread::id{};
    cursor_realizer() = std::move(realizer);
}
void begin_wait_cursor() {
    if (!cursor_thread()) return;
    ++g_wait_cursor_depth;
    if (const auto& realizer = cursor_realizer()) realizer(CursorKind::Wait);
}
void end_wait_cursor() {
    end_wait_cursor(CursorKind::Default);
}
void end_wait_cursor(const CursorKind restored_cursor) {
    if (!cursor_thread() || g_wait_cursor_depth <= 0) return;
    if (--g_wait_cursor_depth == 0)
        if (const auto& realizer = cursor_realizer()) realizer(restored_cursor);
}
bool wait_cursor_active() noexcept { return g_wait_cursor_depth > 0; }
bool cursor_ui_thread() noexcept { return cursor_thread(); }
namespace {
ResourceId g_active_custom_cursor{};
} // namespace
void set_active_custom_cursor(const ResourceId image) noexcept { g_active_custom_cursor = image; }
ResourceId active_custom_cursor() noexcept { return g_active_custom_cursor; }

void set_editor_data_directory_opener(EditorDataDirectoryOpener opener) {
    data_directory_opener() = std::move(opener);
}

bool editor_can_open_data_directory() noexcept {
    return static_cast<bool>(data_directory_opener());
}

bool editor_uses_async_dialog_flow(const Platform platform) noexcept {
    return g_editor_window_host.all_mode.load(std::memory_order_acquire) ||
           platform == Platform::Web;
}

bool editor_open_data_directory(const std::filesystem::path& directory) {
    const auto opener = data_directory_opener();
    return opener && !directory.empty() && opener(directory);
}

float editor_presented_font_points(const float authored_points) noexcept {
    return authored_points + (g_editor_window_host.all_mode.load() ? 2.0F : 0.0F);
}

float editor_presented_ui_metric_scale() noexcept {
    // ALL は選択フォントを大きくするが、指定された配置は
    // 共通の論理座標で計測し、cw.s を一度だけ適用する。
    // 一律に14/12を重ねると、ダイアログ・数値欄・スク
    // ロール範囲がホストウィンドウとずれる。 必要な最小サ
    // イズの増加は、各部品が実際のフォント寸法から求める。
    return 1.0F;
}

namespace {
bool valid_work_area(Rect area) noexcept {
    return std::isfinite(area.x) && std::isfinite(area.y) &&
        std::isfinite(area.width) && std::isfinite(area.height) &&
        area.width > 0 && area.height > 0;
}
std::optional<Size> finite_window_limit(std::optional<Size> size) noexcept {
    if(!size) return {};
    const auto valid=[](float value) {return std::isfinite(value) && value>0 ? value : 0.0F;};
    size->width=valid(size->width);size->height=valid(size->height);
    return size->width>0 || size->height>0 ? size : std::nullopt;
}
}

Rect authored_window_bounds(const Window& window) noexcept {
    auto bounds=window.bounds();
    const auto stamp=window.bounds_ui_text_scale;
    const auto scale=std::isfinite(stamp) ? std::max(1.0F,stamp) : 1.0F;
    bounds.width/=scale;bounds.height/=scale;
    return bounds;
}

WindowGeometryPlan plan_window_geometry(const Window& window,const WindowGeometryRequest& request) noexcept {
    if (window.kind() == ComponentKind::Dialog) {
        // ネイティブ・埋め込みのどちらのダイアログも、計測前に共通のタブ方
        // 針を準備する。 入れ子のタブには、それぞれ縦方向の領域が必要。
        std::unordered_set<const Widget*> visited;
        const auto mark = [&](auto&& self, const WidgetPtr& widget) -> void {
            if (!widget || !visited.insert(widget.get()).second ||
                std::dynamic_pointer_cast<Window>(widget)) return;
            if (const auto tabs = std::dynamic_pointer_cast<TabView>(widget)) {
                tabs->dialog_header_context = true;
                for (const auto& page : tabs->pages) self(self, page.content);
            }
            if (const auto container = std::dynamic_pointer_cast<Container>(widget))
                for (const auto& child : container->children()) self(self, child);
        };
        try { for (const auto& child : window.children()) mark(mark, child); }
        catch (...) {} // メモリ確保が難しい場合は、以前の配置を維持する。
    }
    WindowGeometryPlan plan;
    plan.bounds=request.bounds;
    const auto positive=[](float value,float fallback) {
        return std::isfinite(value) && value>0 ? value : fallback;
    };
    if(!std::isfinite(plan.bounds.x)) plan.bounds.x=valid_work_area(request.available)?request.available.x:0;
    if(!std::isfinite(plan.bounds.y)) plan.bounds.y=valid_work_area(request.available)?request.available.y:0;
    Size size{positive(request.bounds.width,1),positive(request.bounds.height,1)};
    // 埋め込みダイアログの境界には ALL の共通見出しを含むが
    // 、Windowの指定・最小・最大サイズはクライアント内容
    // だけを表す。 両座標系を区別し、外側の高さを内容の高さと
    // して使わない。 混ぜると、少し大きいフォームが見出し分ま
    // で使い、余裕のあるホストでも縦横両スクロールバーが出る。
    const bool captioned = request.embedded &&
        window.host_mode == WindowHostMode::EmbeddedDialog &&
        !window.overlay_surface && editor_window_host_options().keep_top_bar;
    const float caption_height = captioned ? scaled_ui_metric(28.0F) : 0.0F;
    if(request.intent==WindowGeometryIntent::Initial && !window.overlay_surface) {
        const auto stamp=window.bounds_ui_text_scale;
        const auto scale=std::isfinite(stamp)?std::max(1.0F,stamp):1.0F;
        auto content = realized_window_size(window,{
            size.width/scale,
            std::max(1.0F,size.height-caption_height)/scale});
        const auto initial_minimum = window.initial_minimum_size
            ? realized_window_minimum_size(window) : std::nullopt;
        if (initial_minimum) {
            // エディタのコンテント設定など、ダイアログの最小サイズで開く。
            if (initial_minimum->width > 0.0F) content.width = initial_minimum->width;
            if (initial_minimum->height > 0.0F) content.height = initial_minimum->height;
        } else if (window.kind() == ComponentKind::Dialog &&
            valid_work_area(request.available)) {
            // 作業領域に余裕があれば、初期ダイアログは内容の通常の表
            // 示領域のサイズで開く。 実測最小値だけでは数行分になる
            // 場合があり、広い画面でも不要なスクロールが生じるため。
            const auto preferred = window_content_preferred_size(window);
            if (window.initial_preferred_width)
                content.width = std::max(content.width, preferred.width);
            content.height = std::max(content.height, preferred.height);
        }
        size = {content.width, content.height + caption_height};
    }
    // 重ね表示は指定画像の画素単位で扱い、通常のウィンドウの代替サイズより
    // 小さいプレビューも含む。 ダイアログのUI文字倍率は引き継がない。
    plan.minimum=finite_window_limit(realized_window_minimum_size(window));
    plan.maximum=finite_window_limit(realized_window_maximum_size(window));
    if (captioned) {
        if (plan.minimum && plan.minimum->height > 0.0F)
            plan.minimum->height += caption_height;
        if (plan.maximum && plan.maximum->height > 0.0F)
            plan.maximum->height += caption_height;
    }
    if(plan.minimum) {
        size.width=std::max(size.width,plan.minimum->width);
        size.height=std::max(size.height,plan.minimum->height);
    }
    if(plan.maximum) {
        if(plan.maximum->width>0) size.width=std::min(size.width,plan.maximum->width);
        if(plan.maximum->height>0) size.height=std::min(size.height,plan.maximum->height);
    }
    plan.bounds.width=positive(size.width,1);plan.bounds.height=positive(size.height,1);
    if(valid_work_area(request.available)) {
        // ネイティブ・埋め込みのウィンドウは同じ画面制限に従う。 画面が小さい
        // 場合も、共通の配置処理は content_extent を保持する。
        if(plan.minimum) {
            plan.minimum->width=std::min(plan.minimum->width,request.available.width);
            plan.minimum->height=std::min(plan.minimum->height,request.available.height);
        }
        const bool center=request.intent==WindowGeometryIntent::Initial &&
            (window.center_on_owner ||
             (request.embedded && !window.overlay_surface &&
              (window.host_mode==WindowHostMode::EmbeddedDialog ||
               editor_window_host_options().all_mode)));
        const auto anchor=request.owner && valid_work_area(*request.owner)?*request.owner:request.available;
        plan.bounds=center ? center_hosted_bounds(plan.bounds,anchor,request.available)
                           : constrain_hosted_bounds(plan.bounds,request.available);
    }
    return plan;
}

Rect place_hosted_window(const Window& window, Rect bounds, Rect host,
                         std::optional<Rect> owner) noexcept {
    if(window.host_mode==WindowHostMode::EmbeddedDialog || window.center_on_owner)
        return center_hosted_bounds(bounds,owner.value_or(host),host);
    return constrain_hosted_bounds(bounds,host);
}

std::optional<WindowTextScalePlan> plan_window_text_scale_change(
    const Window& window, const WindowTextScaleChange& change) noexcept {
    if (window.kind() != ComponentKind::Dialog) return std::nullopt;
    if(window.maximized) {
        const auto requested=change.embedded && valid_work_area(change.available_bounds)
            ? change.available_bounds : change.current_bounds;
        auto plan=plan_window_geometry(window,{requested,change.available_bounds,
            {},change.embedded,WindowGeometryIntent::Explicit});
        if(!change.embedded) {
            // 観測したOSの配置を保持する。 最大化中のサイズ変更は、ウィンドウ管
            // 理側によって復元サイズを変えたり最大化を解除したりする場合がある。
            plan.bounds=change.current_bounds;
            plan.apply_bounds=false;
        }
        return plan;
    }

    const auto size=realized_window_size(window,change.logical_size);
    auto requested=change.current_bounds;
    requested.width=size.width;requested.height=size.height;
    if (change.embedded)
        requested = hosted_window_outer_bounds(
            window, requested, editor_window_host_options().keep_top_bar);
    auto result=plan_window_geometry(window,{requested,change.available_bounds,
        change.owner_bounds,change.embedded,WindowGeometryIntent::Explicit});
    const auto host = change.available_bounds;
    const bool valid_host = valid_work_area(host);
    if (valid_host && (change.embedded || window.center_on_owner)) {
        result.bounds = center_hosted_bounds(result.bounds,
            change.owner_bounds.value_or(change.available_bounds), change.available_bounds);
    } // それ以外は共通の配置計画で、ユーザーが動かした原点を維持し、必要なら範囲内に収める。
    return result;
}

std::vector<std::optional<WindowTextScalePlan>> plan_window_text_scale_changes(
    std::span<const WindowTextScaleSurface> surfaces) {
    std::vector<std::optional<WindowTextScalePlan>> plans(surfaces.size());
    std::unordered_map<const Window*,std::size_t> indices;
    for(std::size_t i=0;i<surfaces.size();++i) {
        if(!surfaces[i].window) continue;
        indices.emplace(surfaces[i].window.get(),i);
        plans[i]=plan_window_text_scale_change(*surfaces[i].window,surfaces[i].change);
    }
    std::vector<std::optional<std::size_t>> dependencies(surfaces.size());
    std::vector<bool> ready(surfaces.size(),true);
    for(std::size_t i=0;i<surfaces.size();++i) {
        if(!plans[i] || surfaces[i].window->maximized || !valid_work_area(surfaces[i].change.available_bounds) ||
                       (!surfaces[i].change.embedded &&
                        !surfaces[i].window->center_on_owner)) continue;
        const auto owner=surfaces[i].window->owner.lock();
        const auto found=indices.find(owner.get());
        if(found==indices.end() || !plans[found->second] ||
           surfaces[found->second].change.embedded!=surfaces[i].change.embedded ||
           surfaces[i].coordinate_space!=surfaces[found->second].coordinate_space) continue;
        dependencies[i]=found->second;
        ready[i]=false;
    }
    // 依存順の反復処理により、ユーザー・モデルの所有者経路を
    // 再帰でたどらない。 進めなくなった循環・依存先は、中
    // 間のネイティブの副作用によらず初期の配置計画を保つ。
    bool progressed=true;
    while(progressed) {
        progressed=false;
        for(std::size_t i=0;i<surfaces.size();++i) {
            if(ready[i] || !ready[*dependencies[i]]) continue;
            // サイズと制限は上で一度だけ計測済み。 所有者
            // の解決は基準位置だけを変え、寸法は変えない。
            plans[i]->bounds=center_hosted_bounds(plans[i]->bounds,
                plans[*dependencies[i]]->bounds,surfaces[i].change.available_bounds);
            ready[i]=true;
            progressed=true;
        }
    }
    return plans;
}

bool window_presentation_hidden(const std::shared_ptr<Window>& window) {
    std::unordered_set<const Window*> visited;
    for (auto current = window; current; current = current->owner.lock()) {
        if (!visited.insert(current.get()).second || current->initially_hidden)
            return true;
    }
    return !window;
}

EditorWindowHostOptions editor_window_host_options() noexcept {
    EditorWindowHostOptions result;
    result.all_mode = g_editor_window_host.all_mode.load(std::memory_order_acquire);
    result.native_parent = g_editor_window_host.native_parent.load(std::memory_order_acquire);
    result.native_sdl_window =
        g_editor_window_host.native_sdl_window.load(std::memory_order_acquire);
    result.native_renderer = g_editor_window_host.native_renderer.load(std::memory_order_acquire);
    result.keep_top_bar = g_editor_window_host.keep_top_bar.load(std::memory_order_acquire);
    result.allow_internal_move =
        g_editor_window_host.allow_internal_move.load(std::memory_order_acquire);
    result.hosted_canvas_size.width =
        g_editor_window_host.hosted_canvas_width.load(std::memory_order_acquire);
    result.hosted_canvas_size.height =
        g_editor_window_host.hosted_canvas_height.load(std::memory_order_acquire);
    return result;
}

Rect editor_hosted_canvas_bounds(const EditorWindowHostOptions& options) noexcept {
    constexpr Size fallback{1590.0F, 910.0F};
    const auto positive = [](const float value, const float fallback_value) noexcept {
        return std::isfinite(value) && value > 0.0F ? value : fallback_value;
    };
    return {0.0F, 0.0F,
            positive(options.hosted_canvas_size.width, fallback.width),
            positive(options.hosted_canvas_size.height, fallback.height)};
}

void set_editor_window_host_options(const EditorWindowHostOptions& options) noexcept {
    g_editor_window_host.native_parent.store(options.native_parent, std::memory_order_release);
    g_editor_window_host.native_sdl_window.store(
        options.native_sdl_window, std::memory_order_release);
    g_editor_window_host.native_renderer.store(options.native_renderer, std::memory_order_release);
    g_editor_window_host.keep_top_bar.store(options.keep_top_bar, std::memory_order_release);
    g_editor_window_host.allow_internal_move.store(
        options.allow_internal_move, std::memory_order_release);
    const auto canvas = editor_hosted_canvas_bounds(options);
    g_editor_window_host.hosted_canvas_width.store(canvas.width, std::memory_order_release);
    g_editor_window_host.hosted_canvas_height.store(canvas.height, std::memory_order_release);
    // 組み込みの合成を有効にする前に、依存する全フィールドを反映する。
    g_editor_window_host.all_mode.store(options.all_mode, std::memory_order_release);
}

Rect constrain_hosted_bounds(Rect bounds, Rect host, const bool fill_host) noexcept {
    const auto finite_or = [](const float value, const float fallback) noexcept {
        return std::isfinite(value) ? value : fallback;
    };
    host.x = finite_or(host.x, 0.0F);
    host.y = finite_or(host.y, 0.0F);
    host.width = std::max(1.0F, finite_or(host.width, 1.0F));
    host.height = std::max(1.0F, finite_or(host.height, 1.0F));
    if (fill_host) return host;

    bounds.width = std::clamp(finite_or(bounds.width, 1.0F), 1.0F, host.width);
    bounds.height = std::clamp(finite_or(bounds.height, 1.0F), 1.0F, host.height);
    bounds.x = std::clamp(finite_or(bounds.x, host.x), host.x,
                          host.x + host.width - bounds.width);
    bounds.y = std::clamp(finite_or(bounds.y, host.y), host.y,
                          host.y + host.height - bounds.height);
    return bounds;
}

Rect center_hosted_bounds(Rect bounds, const Rect anchor, const Rect host) noexcept {
    bounds.x = anchor.x + (anchor.width - bounds.width) * 0.5F;
    bounds.y = anchor.y + (anchor.height - bounds.height) * 0.5F;
    return constrain_hosted_bounds(bounds, host);
}

Rect move_hosted_bounds(Rect bounds, const Point pointer, const Point drag_offset,
                        const Rect host) noexcept {
    bounds.x = pointer.x - drag_offset.x;
    bounds.y = pointer.y - drag_offset.y;
    return constrain_hosted_bounds(bounds, host);
}

HostedCaptionLayout layout_hosted_caption(Rect bounds, const bool visible,
                                          const bool minimize, const bool buttons) noexcept {
    const float height = visible
        ? std::min(std::max(0.0F, bounds.height), scaled_ui_metric(28.0F)) : 0.0F;
    const float close_width = buttons
        ? std::min(std::max(0.0F, bounds.width), scaled_ui_metric(44.0F)) : 0.0F;
    HostedCaptionLayout result{{bounds.x, bounds.y, bounds.width, height},
            {bounds.x + bounds.width - close_width, bounds.y, close_width, height},
            {bounds.x, bounds.y + height, bounds.width, std::max(0.0F, bounds.height - height)},
            {}};
    if (minimize && visible) {
        const float width = std::min(std::max(0.0F, bounds.width - close_width),
                                     scaled_ui_metric(44.0F));
        result.minimize_button = {result.close_button.x - width, bounds.y, width, height};
    }
    if (buttons && visible && height > 0.0F) {
        // 左に取り消し・間隔・やり直しを並べ、その右にダイアログの
        // アイコンとタイトルを置く。 右の最小化・閉じるボタンと
        // は重ねない。 履歴ボタンにはアイコンと文字を表示する。
        const float button = scaled_ui_metric(96.0F);
        const float gap = scaled_ui_metric(24.0F);
        const float total = button * 2.0F + gap;
        const float left = bounds.x + scaled_ui_metric(4.0F);
        const float right_limit = (result.minimize_button.width > 0.0F
            ? result.minimize_button.x : result.close_button.x) - scaled_ui_metric(16.0F);
        if (left + total <= right_limit) {
            result.undo_button = {left, bounds.y, button, height};
            result.redo_button = {left + button + gap, bounds.y, button, height};
        }
    }
    return result;
}

bool hosted_minimizable(const Window& window) noexcept {
    // 固定サイズも含め、ALL のモードレスダイアログにはボタンを付ける。 ネイティブ
    // の最小化可否はSWTのサイズ変更可能な枠に従うが、ALLではその制限を使わない。
    if (window.overlay_surface || !window.caption_buttons ||
        window.host_mode != WindowHostMode::EmbeddedDialog) return false;
    // カード設定から開くレイヤー編集など、別ダイアログの
    // 子にはボタンを付けない。 エディタの主ウィンドウ
    // が直接所有するダイアログだけを最小化可能にする。
    if (const auto owner = window.owner.lock();
        owner && owner->host_mode == WindowHostMode::EmbeddedDialog) return false;
    const auto* dialog = dynamic_cast<const Dialog*>(&window);
    return !dialog || !dialog->modal;
}

Rect hosted_window_outer_bounds(const Window& window, Rect content_bounds,
                                const bool caption_visible) noexcept {
    if (!caption_visible || window.host_mode != WindowHostMode::EmbeddedDialog ||
        window.overlay_surface) return content_bounds;
    content_bounds.height += layout_hosted_caption(content_bounds).caption.height;
    return content_bounds;
}

void HostedCaptionStateMachine::cancel() noexcept { owner_.reset(); }

void HostedCaptionStateMachine::cancel_for(const std::shared_ptr<Window>& window) noexcept {
    if (owner_.lock() == window) cancel();
}

bool HostedCaptionStateMachine::cancel_pointer(const std::shared_ptr<Window>& window,
                                               const PointerEvent& event) noexcept {
    if (!window || owner_.lock() != window || pointer_type_ != event.pointer_type ||
        pointer_id_ != event.pointer_id) return false;
    cancel();
    return true;
}

namespace {
constexpr std::uint8_t resize_left=1,resize_top=2,resize_right=4,resize_bottom=8;
std::uint8_t hosted_resize_edges(Rect bounds,Point point,float band) {
    if(point.x<bounds.x || point.y<bounds.y || point.x>=bounds.x+bounds.width ||
       point.y>=bounds.y+bounds.height) return 0;
    std::uint8_t edges=0;
    const auto x_band=std::min(band,bounds.width/2),y_band=std::min(band,bounds.height/2);
    if(point.x<bounds.x+x_band) edges|=resize_left;
    else if(point.x>=bounds.x+bounds.width-x_band) edges|=resize_right;
    if(point.y<bounds.y+y_band) edges|=resize_top;
    else if(point.y>=bounds.y+bounds.height-y_band) edges|=resize_bottom;
    return edges;
}
CursorKind hosted_resize_cursor(std::uint8_t edges) {
    const bool horizontal=edges&(resize_left|resize_right),vertical=edges&(resize_top|resize_bottom);
    if(horizontal && vertical)
        return ((edges&resize_left)!=0)==((edges&resize_top)!=0)
            ? CursorKind::SizeDiagonalNorthWest : CursorKind::SizeDiagonalNorthEast;
    return horizontal ? CursorKind::SizeHorizontal : CursorKind::SizeVertical;
}
bool same_hosted_limit(const std::optional<Size>& a,const std::optional<Size>& b) {
    const auto same=[](float x,float y) {return x==y || (std::isnan(x) && std::isnan(y));};
    return a.has_value()==b.has_value() &&
        (!a || (same(a->width,b->width) && same(a->height,b->height)));
}
Rect resize_hosted_from_origin(Rect start,Point delta,Rect host,std::uint8_t edges,
                              const WindowGeometryPlan& limits) {
    if(!valid_work_area(start) || !valid_work_area(host) ||
       !std::isfinite(delta.x) || !std::isfinite(delta.y)) return start;
    const auto axis=[](float origin,float size,float motion,float host_origin,float host_size,
                       bool low,bool high,float minimum,float maximum) {
        if(!low && !high) return std::pair{origin,size};
        const float anchor=std::clamp(low ? origin+size : origin,host_origin,host_origin+host_size);
        const float available=low ? anchor-host_origin : host_origin+host_size-anchor;
        if(available<=0) return std::pair{origin,size};
        const float limit=maximum>0 ? std::min(maximum,available) : available;
        const float least=std::min(std::max(1.0F,minimum),limit);
        const float extent=std::clamp(size+(low?-motion:motion),least,limit);
        return std::pair{low?anchor-extent:anchor,extent};
    };
    const auto [x,width]=axis(start.x,start.width,delta.x,host.x,host.width,
        edges&resize_left,edges&resize_right,limits.minimum?limits.minimum->width:0,
        limits.maximum?limits.maximum->width:0);
    const auto [y,height]=axis(start.y,start.height,delta.y,host.y,host.height,
        edges&resize_top,edges&resize_bottom,limits.minimum?limits.minimum->height:0,
        limits.maximum?limits.maximum->height:0);
    return {x,y,width,height};
}
}

HostedCaptionResult HostedCaptionStateMachine::handle(
    const std::shared_ptr<Window>& window, Rect& bounds, const Rect host,
    const bool visible, const bool movable, const PointerEvent& event, const bool resizable) {
    const auto inside = [](const Rect r, const Point p) {
        return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
    };
    const auto same = [](const Rect a, const Rect b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    };
    const auto layout = layout_hosted_caption(bounds, visible,
                                              window && hosted_minimizable(*window),
                                              !window || window->caption_buttons);
    const bool can_move=movable && window && !window->maximized && !window->overlay_surface;
    const bool can_resize=resizable && window && window->resizable &&
        !window->maximized && !window->overlay_surface;
    if (const auto captured = owner_.lock()) {
        // 倍率変更や新しいモーダルなどの外部変更は、古い配置を無効にする
        // 。 新しいフォームへ古いポインターのオフセットを適用しない。
        if (captured != window || !captured->visible() || !captured->enabled() ||
            !visible || !same(bounds, expected_) || caption_height_ != layout.caption.height ||
            can_move != movable_ || can_resize!=resizable_ || !same(host,host_) ||
            (resize_edges_ && (!same_hosted_limit(window->minimum_size,declared_minimum_) ||
                               !same_hosted_limit(window->maximum_size,declared_maximum_)))) {
            cancel();
            return {true,false,false,CursorKind::Arrow};
        }
        if (event.pointer_type != pointer_type_ || event.pointer_id != pointer_id_)
            return {true}; // 別の指・マウスが、この操作を奪わないようにする。
        if (event.action == PointerAction::Cancel ||
            (event.action==PointerAction::Leave && event.pointer_type!=PointerType::Mouse)) {
            cancel(); return {true,false,false,CursorKind::Arrow};
        }
        if (event.action != PointerAction::Move && event.action != PointerAction::Up)
            return {true};
        if (event.action == PointerAction::Up && event.button != PointerButton::Primary)
            return {true};
        if(!std::isfinite(event.position.x) || !std::isfinite(event.position.y) ||
           !std::isfinite(event.position.x-start_pointer_.x) ||
           !std::isfinite(event.position.y-start_pointer_.y)) {
            // 位置が不正な解放でも操作を終了し、
            // ウィンドウを捕捉したままにしない。
            if(event.action==PointerAction::Up) {
                cancel(); return {true,false,false,CursorKind::Arrow};
            }
            return {true};
        }
        HostedCaptionResult result{true};
        if(resize_edges_) {
            result.cursor=hosted_resize_cursor(resize_edges_);
            const auto resized=resize_hosted_from_origin(start_bounds_,
                {event.position.x-start_pointer_.x,event.position.y-start_pointer_.y},host,resize_edges_,resize_limits_);
            result.moved=!same(bounds,resized);
            bounds=expected_=resized;
        } else if (!close_ && !minimize_ && !undo_ && !redo_ && movable_) {
            const auto moved = move_hosted_bounds(bounds, event.position, offset_, host);
            result.moved = !same(bounds, moved);
            bounds = expected_ = moved;
        }
        if (event.action == PointerAction::Up) {
            result.close_requested = close_ && inside(layout.close_button, event.position);
            result.minimize_requested =
                minimize_ && inside(layout.minimize_button, event.position);
            result.undo_requested = undo_ && inside(layout.undo_button, event.position);
            result.redo_requested = redo_ && inside(layout.redo_button, event.position);
            cancel();
        }
        return result;
    }
    if (!window || !window->visible() || !window->enabled() || window->overlay_surface ||
        !visible || event.handled || !valid_work_area(bounds) || !valid_work_area(host) ||
        !std::isfinite(event.position.x) || !std::isfinite(event.position.y)) return {};
    const auto edges=can_resize ? hosted_resize_edges(bounds,event.position,
        scaled_ui_metric(event.pointer_type==PointerType::Touch?8.0F:4.0F)) : 0;
    if(event.action==PointerAction::Move && edges)
        return {true,false,false,hosted_resize_cursor(edges)};
    if(event.action!=PointerAction::Down || event.button!=PointerButton::Primary ||
        (!edges && !inside(layout.caption,event.position))) return {};
    owner_ = window;
    pointer_type_ = event.pointer_type;
    pointer_id_ = event.pointer_id;
    expected_ = bounds;
    caption_height_ = layout.caption.height;
    close_ = !edges && inside(layout.close_button, event.position);
    minimize_ = !edges && !close_ && inside(layout.minimize_button, event.position);
    undo_ = !edges && !close_ && !minimize_ && inside(layout.undo_button, event.position);
    redo_ = !edges && !close_ && !minimize_ && !undo_ &&
            inside(layout.redo_button, event.position);
    movable_ = can_move;
    resizable_ = can_resize;
    resize_edges_=edges;start_bounds_=bounds;start_pointer_=event.position;host_=host;
    declared_minimum_=window->minimum_size;declared_maximum_=window->maximum_size;
    if(edges) resize_limits_=plan_window_geometry(*window,
        {bounds,host,{},true,WindowGeometryIntent::Explicit});
    offset_ = {event.position.x - bounds.x, event.position.y - bounds.y};
    return {true,false,false,edges ? hosted_resize_cursor(edges) : CursorKind::Arrow};
}

}  // namespace cwxeditor::lib
