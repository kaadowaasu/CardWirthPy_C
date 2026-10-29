#include "ui_window_maximize.hpp"
#include "ui_metrics.hpp"
#include <cmath>
#include <unordered_set>

namespace cwxeditor::lib {
namespace {
bool valid_rect(Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
        std::isfinite(r.height) && r.width>0 && r.height>0;
}
bool same_rect(Rect a,Rect b) {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
void mark_initial_maximized_split_reflows(const std::shared_ptr<Window>& window) {
    if(!window) return;
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](auto&& self,const WidgetPtr& widget)->void {
        if(!widget || !visited.insert(widget.get()).second) return;
        if(widget.get()!=window.get() && std::dynamic_pointer_cast<Window>(widget)) return;
        if(const auto split=std::dynamic_pointer_cast<SplitView>(widget);
           split && split->reflow_on_initial_maximized_geometry)
            split->initial_maximized_reflow_pending=true;
        if(const auto container=std::dynamic_pointer_cast<Container>(widget))
            for(const auto& child:container->children()) self(self,child);
        if(const auto tabs=std::dynamic_pointer_cast<TabView>(widget))
            for(const auto& page:tabs->pages) self(self,page.content);
    };
    for(const auto& child:window->children()) visit(visit,child);
}
}
void WindowMaximizeStateMachine::reset() noexcept {
    ++revision_;owner_.reset();initialized_=embedded_=applied_=last_desired_=false;
    observed_at_=requested_at_=0;pending_.reset();restore_.reset();restore_scale_=1;
    initial_maximize_geometry_pending_=false;initial_maximize_bounds_={};
    issuing_revision_.reset();
    placing_=false;
}
WindowAttributeResult WindowMaximizeStateMachine::update(
    std::shared_ptr<Window> window,WindowMaximizeContext context) {
    using R=WindowAttributeResult;
    if(!window || (context.live && !context.live())) return R::Failed;
    if(placing_) return R::Pending;
    if(owner_.lock()!=window || (initialized_ && embedded_!=context.embedded)) reset();
    owner_=window;
    // ネイティブアダプター内の同一要求で、外側の操作の
    // 失敗・例外時の復旧を無効にしない。 逆の要求には
    // 新しい変更番号を与え、実行中の操作を置き換える。
    if(issuing_revision_ && pending_ && *pending_==window->maximized)
        return R::Pending;
    const auto revision=++revision_;
    const auto live=[&] {return (!context.live || context.live()) && revision==revision_ && owner_.lock()==window;};
    const bool desired=window->maximized;
    if(!initialized_) {
        embedded_=context.embedded;
        applied_=!embedded_ && context.native_maximized.value_or(false);
        last_desired_=desired;initialized_=true;
        initial_maximize_geometry_pending_=desired &&
            (context.embedded || (context.native_maximized && !*context.native_maximized));
        initial_maximize_bounds_=context.bounds;
    }
    if(initial_maximize_geometry_pending_ && !desired)
        initial_maximize_geometry_pending_=false;
    if(desired && !window->resizable) return R::Unsupported;
    if(context.embedded) {
        if(!valid_rect(context.bounds)) return R::Failed;
        if(!desired && !applied_) {
            restore_=context.bounds;restore_scale_=editor_ui_text_scale();
            last_desired_=false;return R::Accepted;
        }
        if(!valid_rect(context.available)) return R::Pending;
        auto restore=restore_;
        auto scale=restore_scale_;
        if(desired && !applied_) {restore=context.bounds;scale=editor_ui_text_scale();}
        auto requested=desired ? context.available : restore.value_or(context.bounds);
        if(!desired && window->kind()==ComponentKind::Dialog) {
            const auto ratio=editor_ui_text_scale()/scale;
            requested.width*=ratio;requested.height*=ratio;
        }
        const auto plan=plan_window_geometry(*window,{requested,context.available,{},true,WindowGeometryIntent::Explicit});
        if(!same_rect(plan.bounds,context.bounds)) {
            if(!context.place) return R::Unsupported;
            const auto old_restore=restore_;const auto old_scale=restore_scale_;
            const auto old_applied=applied_,old_desired=last_desired_;
            restore_=restore;restore_scale_=scale;applied_=last_desired_=desired;placing_=true;
            const auto rollback=[&] {
                restore_=old_restore;restore_scale_=old_scale;applied_=old_applied;last_desired_=old_desired;
            };
            bool placed{};
            try {placed=context.place(plan);}
            catch(...) {placing_=false;if(live()) rollback();throw;}
            placing_=false;
            if(!live()) return R::Pending;
            if(!placed) {rollback();return R::Failed;}
        }
        if(window->maximized!=desired) return R::Pending;
        restore_=restore;restore_scale_=scale;applied_=last_desired_=desired;
        if(!desired) {restore_=plan.bounds;restore_scale_=editor_ui_text_scale();}
        else if(initial_maximize_geometry_pending_) {
            mark_initial_maximized_split_reflows(window);
            initial_maximize_geometry_pending_=false;
        }
        return R::Accepted;
    }
    if(!context.native_maximized) return R::Unsupported;
    // 古いネイティブのフラグがすでに一致していても、要求が変われば
    // 送る。 逆の操作がOS内でまだ待機している場合があるため。
    if((!pending_ || *pending_==desired) && *context.native_maximized==desired) {
        pending_.reset();applied_=last_desired_=desired;
        if(context.timestamp>observed_at_) observed_at_=context.timestamp;
        if(initial_maximize_geometry_pending_ && desired)
            (void)observe_geometry(window,context.bounds,*context.native_maximized);
        return R::Accepted;
    }
    if(pending_ && *pending_==desired) return R::Pending;
    if(!context.request_native) return R::Unsupported;
    // 要求状態はアダプター呼び出し前に反映する。 同期的なネイティブ
    // 通知があれば、observe() が新しい変更番号を管理する。
    const auto old_pending=pending_;
    const auto old_desired=last_desired_;
    const auto old_time=requested_at_;
    pending_=desired;last_desired_=desired;requested_at_=context.timestamp;
    issuing_revision_=revision;
    const auto release=[&] {
        if(issuing_revision_==revision) issuing_revision_.reset();
    };
    bool accepted{};
    try {accepted=context.request_native(desired);}
    catch(...) {
        release();
        if(live()) {pending_=old_pending;last_desired_=old_desired;requested_at_=old_time;}
        throw;
    }
    release();
    if(!live()) return R::Pending;
    if(!accepted) {
        pending_=old_pending;last_desired_=old_desired;requested_at_=old_time;
        if(initial_maximize_geometry_pending_ && desired)
            initial_maximize_geometry_pending_=false;
        return R::Failed;
    }
    return R::Pending; // OSが受け付けたことは、状態の変更を観測したこととは異なる。
}
bool WindowMaximizeStateMachine::observe_geometry(const std::shared_ptr<Window>& window,
    const Rect bounds,const bool maximized) {
    if(!valid_rect(bounds) || !initial_maximize_geometry_pending_ || embedded_ || !window ||
       owner_.lock()!=window || !maximized || !window->maximized) return false;
    if(bounds.width==initial_maximize_bounds_.width &&
       bounds.height==initial_maximize_bounds_.height) return false;
    mark_initial_maximized_split_reflows(window);
    initial_maximize_geometry_pending_=false;
    return true;
}
bool WindowMaximizeStateMachine::observe(const std::shared_ptr<Window>& window,
                                         bool maximized,std::uint64_t timestamp) {
    if(!initialized_ || embedded_ || owner_.lock()!=window || !window ||
        timestamp<observed_at_ || (pending_ && timestamp<requested_at_) ||
        window->maximized!=last_desired_) return false;
    // 古い矛盾する状態を、新しい要求の拒否と見なさない。 ウィンドウ
    // 管理側が拒否する場合も、成功を装わず Pending に保つ。
    if(pending_ && *pending_!=maximized) return false;
    ++revision_;observed_at_=timestamp;pending_.reset();
    const bool changed=window->maximized!=maximized;
    window->maximized=maximized;applied_=last_desired_=maximized;
    return changed;
}
} // namespace cwxeditor::lib
