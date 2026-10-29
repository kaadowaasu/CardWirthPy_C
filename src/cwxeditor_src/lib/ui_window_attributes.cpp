#include "ui_window_attributes.hpp"
#include "ui_metrics.hpp"
#include <cmath>

namespace cwxeditor::lib {
namespace {
bool same_size(const std::optional<Size>& a,const std::optional<Size>& b) {
    const auto equal=[](float x,float y) {return x==y || (std::isnan(x) && std::isnan(y));};
    return a.has_value()==b.has_value() && (!a || (equal(a->width,b->width) && equal(a->height,b->height)));
}
bool same_attribute(WindowAttribute attribute,const WindowAttributes& a,const WindowAttributes& b) {
    switch(attribute) {
    case WindowAttribute::Title: return a.title==b.title;
    case WindowAttribute::Icon: return a.icon==b.icon && a.icon_image==b.icon_image;
    case WindowAttribute::Resizable: return a.resizable==b.resizable;
    case WindowAttribute::SizeLimits: return same_size(a.minimum,b.minimum) && same_size(a.maximum,b.maximum);
    case WindowAttribute::AlwaysOnTop: return a.always_on_top==b.always_on_top;
    case WindowAttribute::Maximized: return a.maximized==b.maximized && a.resizable==b.resizable;
    case WindowAttribute::Modified: return a.modified==b.modified;
    case WindowAttribute::MinimizeButton: return a.minimizable==b.minimizable;
    case WindowAttribute::MaximizeButton: return a.maximizable==b.maximizable;
    default: return false;
    }
}
}
WindowAttributeResult WindowAttributeReport::get(WindowAttribute attribute) const noexcept {
    const auto i=static_cast<std::size_t>(attribute);
    return i<values.size() ? values[i] : WindowAttributeResult::Unsupported;
}
WindowAttributes prepare_window_attributes(const Window& window,const WindowGeometryPlan& geometry) {
    return {window.title,window.icon,window.resizable,window.always_on_top,window.maximized,
        window.modified,window.minimizable,window.maximizable,geometry.minimum,geometry.maximum};
}
void WindowAttributeStateMachine::synchronize(WindowAttributes requested,Apply apply,
                                             std::function<bool()> live) {
    const auto revision=++revision_;
    const auto current=[&] { return (!live || live()) && revision==revision_; };
    for(std::size_t i=0;i<count;++i) {
        if(!current()) return;
        const auto attribute=static_cast<WindowAttribute>(i);
        if(acknowledged_[i] && same_attribute(attribute,*acknowledged_[i],requested)) continue;
        // 同期的なアダプターのコールバックが、同じ操作を再
        // 要求する場合がある。 再帰呼び出しはせず、外側
        // の変更番号を古くし、次の通常の同期で反映する。
        if(applying_[i] && same_attribute(attribute,*applying_[i],requested)) continue;
        report_.values[i]=WindowAttributeResult::Pending;
        acknowledged_[i].reset();
        applying_[i]=requested;
        applying_revision_[i]=revision;
        const auto release=[&] {
            if(applying_revision_[i]==revision) applying_[i].reset();
        };
        WindowAttributeResult result;
        try { result=apply ? apply(attribute,requested) : WindowAttributeResult::Unsupported; }
        catch(...) {
            release();
            if(current()) report_.values[i]=WindowAttributeResult::Failed;
            throw;
        }
        release();
        if(!current()) return;
        report_.values[i]=result;
        if(result==WindowAttributeResult::Accepted || result==WindowAttributeResult::Unsupported)
            acknowledged_[i]=requested;
    }
}
WindowAttributes WindowAttributeStateMachine::prepare(const Window& window,const WindowGeometryRequest& geometry) {
    const ConstraintKey key{window.minimum_size,window.maximum_size,geometry.available,
        editor_ui_text_scale(),window.kind(),geometry.embedded};
    const auto same_rect=[](Rect a,Rect b) {
        const auto equal=[](float x,float y) {return x==y || (std::isnan(x) && std::isnan(y));};
        return equal(a.x,b.x) && equal(a.y,b.y) && equal(a.width,b.width) && equal(a.height,b.height);
    };
    if(!constraint_key_ || !same_size(key.minimum,constraint_key_->minimum) ||
        !same_size(key.maximum,constraint_key_->maximum) || !same_rect(key.available,constraint_key_->available) ||
        key.scale!=constraint_key_->scale || key.kind!=constraint_key_->kind || key.embedded!=constraint_key_->embedded) {
        constraints_=plan_window_geometry(window,geometry);
        constraint_key_=key;
        invalidate(WindowAttribute::Maximized); // ALL は新しいホスト・倍率の制限に合わせて領域を埋める。
    }
    return prepare_window_attributes(window,constraints_);
}
void WindowAttributeStateMachine::reset() noexcept {
    ++revision_;
    acknowledged_={};
    applying_={};
    applying_revision_={};
    report_={};
    invalidate_constraints();
}
void WindowAttributeStateMachine::invalidate(WindowAttribute attribute) noexcept {
    const auto index=static_cast<std::size_t>(attribute);
    if(index>=count) return;
    ++revision_;acknowledged_[index].reset();report_.values[index]=WindowAttributeResult::Pending;
}
} // namespace cwxeditor::lib
