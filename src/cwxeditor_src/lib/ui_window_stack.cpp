#include "ui_window_stack.hpp"
#include "ui_actions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace cwxeditor::lib {
namespace {
std::uint32_t mask_extent(const float value) noexcept {
    if(!std::isfinite(value) || value<=0.0F ||
       value>static_cast<float>(std::numeric_limits<int>::max())) return 0U;
    return static_cast<std::uint32_t>(std::max(1L,std::lround(value)));
}

bool overlay_absorbs_pointer(const WindowInputMask& mask, const Rect bounds,
                             const Point point) noexcept {
    const auto pixels=std::uint64_t(mask.pixel_width)*mask.pixel_height;
    if(!mask.pixel_width || !mask.pixel_height || pixels!=mask.opaque_pixels.size() ||
       bounds.width<=0.0F || bounds.height<=0.0F || !std::isfinite(point.x) ||
       !std::isfinite(point.y)) return true;
    const float x=(point.x-bounds.x)/bounds.width;
    const float y=(point.y-bounds.y)/bounds.height;
    if(x<0.0F || y<0.0F || x>=1.0F || y>=1.0F) return false;
    const auto px=std::min(mask.pixel_width-1U,static_cast<std::uint32_t>(x*mask.pixel_width));
    const auto py=std::min(mask.pixel_height-1U,static_cast<std::uint32_t>(y*mask.pixel_height));
    return sample_window_input_mask(mask,mask.pixel_width,mask.pixel_height,px,py);
}

bool contains(const WidgetPtr& root, const Widget* wanted, const Window* window_boundary) {
    if (!wanted) return false;
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](auto&& self,const WidgetPtr& node)->bool {
        if (!node || !visited.insert(node.get()).second) return false;
        if (window_boundary && node.get()!=window_boundary && std::dynamic_pointer_cast<Window>(node)) return false;
        if (node.get()==wanted) return true;
        if (const auto tabs=std::dynamic_pointer_cast<TabView>(node))
            for (const auto& page:tabs->pages) if (self(self,page.content)) return true;
        if (const auto container=std::dynamic_pointer_cast<Container>(node))
            for (const auto& child:container->children()) if (self(self,child)) return true;
        return false;
    };
    return visit(visit,root);
}
}

bool sample_window_input_mask(const WindowInputMask& source,
    const std::uint32_t target_width, const std::uint32_t target_height,
    const std::uint32_t target_x, const std::uint32_t target_y) noexcept {
    const auto source_pixels=std::uint64_t(source.pixel_width)*source.pixel_height;
    if(!source.pixel_width || !source.pixel_height || !target_width || !target_height ||
       target_x>=target_width || target_y>=target_height ||
       source.pixel_width>std::uint32_t(std::numeric_limits<int>::max()) ||
       source.pixel_height>std::uint32_t(std::numeric_limits<int>::max()) ||
       target_width>std::uint32_t(std::numeric_limits<int>::max()) ||
       target_height>std::uint32_t(std::numeric_limits<int>::max()) ||
       source_pixels!=source.opaque_pixels.size()) return true;
    const auto nearest=[](const std::uint32_t source_extent,
                          const std::uint32_t target_extent,
                          const std::uint32_t target_coordinate) noexcept {
        const auto center=std::uint64_t(target_coordinate)*2U+1U;
        const auto sample=center*source_extent/(std::uint64_t(target_extent)*2U);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(sample,source_extent-1U));
    };
    const auto source_x=nearest(source.pixel_width,target_width,target_x);
    const auto source_y=nearest(source.pixel_height,target_height,target_y);
    return source.opaque_pixels[static_cast<std::size_t>(source_y)*source.pixel_width+source_x]!=0U;
}

std::shared_ptr<const WindowInputMask> resample_window_input_mask(
    const WindowInputMask& source, const std::uint32_t target_width,
    const std::uint32_t target_height) {
    if(!source.pixel_width || !source.pixel_height || !target_width || !target_height ||
       target_width>std::uint32_t(std::numeric_limits<int>::max()) ||
       target_height>std::uint32_t(std::numeric_limits<int>::max())) return {};
    const auto count=std::uint64_t(target_width)*target_height;
    if(count>std::numeric_limits<std::size_t>::max()) return {};
    auto result=std::make_shared<WindowInputMask>();
    result->pixel_width=target_width;
    result->pixel_height=target_height;
    result->opaque_pixels.resize(static_cast<std::size_t>(count));
    for(std::uint32_t y=0;y<target_height;++y)
        for(std::uint32_t x=0;x<target_width;++x)
            result->opaque_pixels[static_cast<std::size_t>(y)*target_width+x]=
                sample_window_input_mask(source,target_width,target_height,x,y) ? 255U : 0U;
    return result;
}

bool contains_widget(const WidgetPtr& root, const Widget* wanted) {
    return contains(root,wanted,nullptr);
}
bool window_contains_widget(const std::shared_ptr<Window>& root, const Widget* wanted) {
    return contains(root,wanted,root.get());
}

void WindowStackStateMachine::attach(const std::shared_ptr<Window>& window,
    SurfaceId surface, Rect bounds, bool embedded) {
    if (!window) return;
    std::erase_if(entries_, [](const Entry& entry) { return entry.window.expired(); });
    for (auto& entry : entries_) if (entry.window.lock() == window) {
        entry.surface=surface; entry.bounds=bounds; entry.embedded=embedded;
        entry.input_mask_enabled=embedded && window->overlay_surface && bool(window->input_mask);
        entry.input_mask=entry.input_mask_enabled ? resample_window_input_mask(
            *window->input_mask,mask_extent(bounds.width),mask_extent(bounds.height)) : nullptr;
        return;
    }
    Entry entry{window,surface,bounds,embedded};
    entry.input_mask_enabled=embedded && window->overlay_surface && bool(window->input_mask);
    entry.input_mask=entry.input_mask_enabled ? resample_window_input_mask(
        *window->input_mask,mask_extent(bounds.width),mask_extent(bounds.height)) : nullptr;
    entries_.push_back(std::move(entry));
}

void WindowStackStateMachine::set_bounds(const std::shared_ptr<Window>& window, Rect bounds) {
    for (auto& entry : entries_) if (entry.window.lock() == window) {
        entry.bounds=bounds;
        entry.input_mask_enabled=entry.embedded && window->overlay_surface && bool(window->input_mask);
        entry.input_mask=entry.input_mask_enabled ? resample_window_input_mask(
            *window->input_mask,mask_extent(bounds.width),mask_extent(bounds.height)) : nullptr;
        return;
    }
}

void WindowStackStateMachine::raise(const std::shared_ptr<Window>& window, const ModalStateMachine* modal) {
    if (!window) return;
    // 所有者を前面へ出しても、既存の子ウィンドウを覆わない。 相対順を
    // 保ち、弱参照・循環する所有関係でも走査が止まらないようにする。
    const auto in_group = [&](const Entry& entry) {
        std::unordered_set<Window*> visited;
        for (auto current=entry.window.lock(); current && visited.insert(current.get()).second;
             current=current->owner.lock()) if (current==window) return true;
        return false;
    };
    std::stable_partition(entries_.begin(),entries_.end(),[&](const Entry& e) { return !in_group(e); });
    reconcile_order(modal);
}

void WindowStackStateMachine::reconcile_order(const ModalStateMachine* modal) {
    std::stable_partition(entries_.begin(),entries_.end(),[](const Entry& e) {
        std::unordered_set<Window*> visited;
        for(auto w=e.window.lock();w && visited.insert(w.get()).second;w=w->owner.lock())
            if(w->always_on_top) return false;
        return true;
    });
    if (modal && !modal->sessions().empty()) {
        const auto active=modal->sessions().back().dialog;
        // 背景の更新・表示で、入力を持つダイアログを覆わない。
        // そのプレビューは上に保つが、入力対象にはしない。
        std::stable_partition(entries_.begin(),entries_.end(),[&](const Entry& e) {
            std::unordered_set<Window*> visited;
            for (auto w=e.window.lock(); w && visited.insert(w.get()).second; w=w->owner.lock())
                if (w==active) return false;
            return true;
        });
    }
}

void WindowStackStateMachine::remove(const std::shared_ptr<Window>& window) {
    std::erase_if(entries_,[&](const Entry& e) { return e.window.expired() || e.window.lock()==window; });
}

std::vector<std::shared_ptr<Window>> WindowStackStateMachine::windows() const {
    std::vector<std::shared_ptr<Window>> result;
    for (const auto& e : entries_) if (auto w=e.window.lock()) result.push_back(std::move(w));
    return result;
}

std::vector<std::shared_ptr<Window>> WindowStackStateMachine::close_order(
    const std::shared_ptr<Window>& root) const {
    const auto snapshot=windows();
    std::vector<std::shared_ptr<Window>> ordered;
    std::unordered_set<const Window*> visited;
    const auto visit=[&](auto&& self,const std::shared_ptr<Window>& current)->void {
        if (!current || !visited.insert(current.get()).second) return;
        for (const auto& child:snapshot)
            if (child->owner.lock()==current) self(self,child);
        ordered.push_back(current);
    };
    visit(visit,root);
    return ordered;
}

std::vector<std::shared_ptr<Window>> WindowStackStateMachine::surface_windows(SurfaceId surface) const {
    std::vector<std::shared_ptr<Window>> result;
    for (const auto& entry : entries_) if (entry.surface==surface)
        if (auto window=entry.window.lock()) result.push_back(std::move(window));
    return result;
}

std::shared_ptr<Window> WindowStackStateMachine::front(SurfaceId surface) const {
    for (auto it=entries_.rbegin(); it!=entries_.rend(); ++it)
        if (auto w=it->window.lock(); w && w->visible() && it->surface==surface) return w;
    return {};
}

std::shared_ptr<Window> WindowStackStateMachine::notification_window(
    SurfaceId surface, const WidgetPtr& focused) const {
    // 画面のフォーカスは、有効な論理ウィンドウに属し、受動的なプレビューに
    // は属さない。 無効化されたモーダルの所有者も通知を受ける場合がある。
    const auto owner=owner_of(focused);
    std::shared_ptr<Window> fallback;
    for (auto it=entries_.rbegin(); it!=entries_.rend(); ++it) {
        const auto w=it->window.lock();
        if (!w || it->surface!=surface || !w->visible() || w->overlay_surface) continue;
        if (w==owner) return w;
        if (!fallback) fallback=w;
    }
    return fallback;
}

std::shared_ptr<Window> WindowStackStateMachine::owner_of(const WidgetPtr& widget) const {
    if (!widget) return {};
    for (auto it=entries_.rbegin(); it!=entries_.rend(); ++it)
        if (auto w=it->window.lock(); w && window_contains_widget(w,widget.get())) return w;
    return {};
}

std::shared_ptr<Window> WindowStackStateMachine::dialog_owner(
    const ModalStateMachine& modal, const WidgetPtr& focused) const {
    if(!modal.sessions().empty() && modal.sessions().back().external) {
        const auto& session=modal.sessions().back();
        if(modal.can_continue(session.id)) return session.owner;
    }
    const auto eligible=[&](const std::shared_ptr<Window>& window) {
        return window && window->visible() && window->enabled() &&
               !window->overlay_surface && modal.allows(window);
    };
    if (const auto owner=owner_of(focused); eligible(owner) &&
        widget_in_input_scope(owner,focused)) return owner;
    for (auto it=entries_.rbegin(); it!=entries_.rend(); ++it)
        if (auto window=it->window.lock(); eligible(window)) return window;
    return {};
}

std::shared_ptr<Window> WindowStackStateMachine::input_window(SurfaceId surface,
    std::optional<Point> position, const ModalStateMachine& modal,
    const std::shared_ptr<Window>& captured) const {
    const auto eligible = [&](const Entry& e, const std::shared_ptr<Window>& w) {
        return w && e.surface==surface && w->visible() && w->enabled() &&
               !w->overlay_surface && modal.allows(w);
    };
    if (captured) {
        for (const auto& e : entries_) if (e.window.lock()==captured)
            return eligible(e,captured) ? captured : nullptr;
        return {}; // 捕捉が解除されても、キュー内の解放の対象を別のものへ変えてはならない。
    }
    for (auto it=entries_.rbegin(); it!=entries_.rend(); ++it) {
        const auto w=it->window.lock();
        if (!w || it->surface!=surface || !w->visible()) continue;
        if (position && it->embedded && !(position->x>=it->bounds.x && position->y>=it->bounds.y &&
            position->x<it->bounds.x+it->bounds.width && position->y<it->bounds.y+it->bounds.height)) continue;
        if(w->overlay_surface) {
            // 明示的なモデルマスクを持つ重ね表示だけが、形状に沿った入力判定を使う。
            // 旧式のPreview以外の重ね表示は、従来の入力を通す動作を維持する。
            if(position && it->embedded && it->input_mask_enabled &&
               (modal.sessions().empty() || modal.allows(w->owner.lock())) &&
               (!it->input_mask || overlay_absorbs_pointer(*it->input_mask,it->bounds,*position)))
                return {};
            continue;
        }
        if (eligible(*it,w)) return w;
        // 無効な前面ウィンドウも、背後を遮る。 この画面のモー
        // ダルは、その外側も含めて背景入力をすべて禁止する。
        if (modal.sessions().empty()) return {};
    }
    return {};
}
} // namespace cwxeditor::lib
