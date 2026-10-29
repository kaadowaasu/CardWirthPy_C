#include "ui_frame_updates.hpp"
#include "ui_actions.hpp"
#include <algorithm>

namespace cwxeditor::lib {
namespace {
std::vector<std::shared_ptr<Window>> models(const std::vector<FrameUpdateTarget>& targets) {
    std::vector<std::shared_ptr<Window>> result;
    result.reserve(targets.size());
    for (const auto& target : targets) result.push_back(target.window);
    return result;
}
}
void FrameUpdateState::request() noexcept {
    if (retired_) return;
    // 描画中の連続変更も含め、一つの予約済み世代にまとめる。
    if (!pending_) ++requested_;
    pending_=true;
}
void FrameUpdateState::retire() noexcept { retired_=true; pending_=false; holds_.clear(); }
bool FrameUpdateState::suspended() const noexcept {
    return std::any_of(holds_.begin(),holds_.end(),[](const auto& hold) { return !hold.widget.expired(); });
}
bool FrameUpdateState::set_redraw(const WidgetPtr& widget, bool enabled) {
    if (!widget || retired_) return false;
    std::erase_if(holds_,[](const auto& hold) { return hold.widget.expired(); });
    const auto found=std::find_if(holds_.begin(),holds_.end(),[&](const auto& hold) { return hold.widget.lock()==widget; });
    if (enabled) {
        if (found==holds_.end()) return false;
        if (--found->depth==0) holds_.erase(found);
    } else {
        if (found!=holds_.end()) ++found->depth;
        else holds_.push_back({widget,1});
    }
    request();
    return true;
}
SurfaceUpdateTransaction::SurfaceUpdateTransaction(SurfaceUpdateRegistry& registry,
    std::uintptr_t surface, std::vector<FrameUpdateTarget> targets)
    : registry_(registry), surface_(surface), targets_(std::move(targets)),
      frame_(models(targets_)), revisions_(targets_.size()) {
    if (targets_.empty()) return;
    // 共有の描画面は一つの対象群として準備するが、変更がなけ
    // ればホストの表示要求だけで再描画しない。 少なくとも一
    // つの論理ウィンドウが新しい世代を要求するまで、前の完成
    // ラスターを再利用する。 アニメーション時刻・露出・サ
    // イズ変更の経路は、この処理の構築前にその要求を出す。
    const bool requested=std::any_of(targets_.begin(),targets_.end(),[](const auto& target) {
        return target.updates && target.updates->pending();
    });
    if (!requested) return;
    const auto eligible=std::all_of(targets_.begin(),targets_.end(),[](const auto& target) {
        return target.window && target.window->visible() && target.updates &&
               !target.updates->retired_ && !target.updates->active_ && !target.updates->suspended();
    });
    // 対象群の半分だけを取得したり、コールバックが可視ウィンドウを置き換え
    // た後で共有レンダラーへ再入したりしない。 次の処理周期で再試行する。
    if (!eligible || !registry_.active_.insert(surface_).second) {
        for (const auto& target : targets_) if (target.updates) target.updates->request();
        return;
    }
    started_=true;
    for (std::size_t i=0;i<targets_.size();++i) {
        auto& state=*targets_[i].updates;
        revisions_[i]=state.requested_;
        state.pending_=false;
        state.active_=true;
    }
}
SurfaceUpdateTransaction::~SurfaceUpdateTransaction() {
    if (!started_) return;
    if (!resolved_) resolve(false);
    registry_.active_.erase(surface_);
}
bool SurfaceUpdateTransaction::append(const std::shared_ptr<Window>& window, FrameStructureSnapshot snapshot) {
    return started_ && !resolved_ && frame_.append(window,std::move(snapshot));
}
bool SurfaceUpdateTransaction::complete(const std::vector<std::shared_ptr<Window>>& current_windows) {
    if (!started_ || resolved_) return false;
    const bool same_generation=std::all_of(targets_.begin(),targets_.end(),[&](const auto& target) {
        const auto index=static_cast<std::size_t>(&target-targets_.data());
        const auto& state=*target.updates;
        return !state.retired_ && !state.suspended() && !state.pending_ &&
               state.requested_==revisions_[index];
    });
    const bool publish=same_generation && frame_.current(current_windows);
    resolve(publish);
    return publish;
}
void SurfaceUpdateTransaction::resolve(bool publish) noexcept {
    for (std::size_t i=0;i<targets_.size();++i) {
        auto& state=*targets_[i].updates;
        state.active_=false;
        if (state.retired_) continue;
        if (publish) state.completed_=revisions_[i];
        else state.request();
    }
    resolved_=true;
}
std::vector<std::shared_ptr<Window>> redraw_windows(const WindowStackStateMachine& stack,
    const WidgetPtr& widget, const std::shared_ptr<Window>& detached_owner) {
    auto live=stack.windows();
    if (!widget) return live;
    std::vector<std::shared_ptr<Window>> result;
    for (const auto& window : live)
        if (window_contains_widget(window,widget.get())) result.push_back(window);
    if (!result.empty()) return result;
    if (detached_owner && std::find(live.begin(),live.end(),detached_owner)!=live.end()) return {detached_owner};
    return {};
}
std::vector<std::shared_ptr<Window>> redraw_popup_windows(
    const WindowStackStateMachine& stack, const MenuPopupStateMachine& popup,
    const WidgetPtr& widget) {
    std::shared_ptr<Window> owner;
    if (widget && std::any_of(popup.levels().begin(), popup.levels().end(),
        [&](const auto& level) { return level.menu.get()==widget.get(); }))
        owner=stack.owner_of(popup.owner());
    return redraw_windows(stack,widget,owner);
}
} // namespace cwxeditor::lib
