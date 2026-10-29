#include "ui_drag_drop.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "ui_actions.hpp"
#include "threading.hpp"

#include <algorithm>
#include <utility>

namespace cwxeditor::lib {
namespace {
bool available(const WidgetPtr& widget, const std::weak_ptr<Widget>& scope,
               bool has_scope) {
    return widget && widget->visible() && widget->enabled() &&
        (!has_scope || widget_in_input_scope(scope.lock(), widget));
}

}

DragDropStateMachine::DragDropStateMachine(std::shared_ptr<MainDispatcher> dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

bool DragDropStateMachine::valid_result(DropOperation selected, DropOperation allowed) {
    if (selected != DropOperation::Copy && selected != DropOperation::Move &&
        selected != DropOperation::Link) return false;
    return (static_cast<unsigned>(selected) & static_cast<unsigned>(allowed)) != 0;
}

DropOperation DragDropStateMachine::choose(DropOperation choices, DropOperation allowed,
                                         DropOperation preferred) {
    const auto intersection = static_cast<DropOperation>(
        static_cast<unsigned>(choices) & static_cast<unsigned>(allowed));
    if (valid_result(preferred, intersection)) return preferred;
    for (const auto value : {DropOperation::Move, DropOperation::Copy, DropOperation::Link})
        if (valid_result(value, intersection)) return value;
    return DropOperation::None;
}

void DragDropStateMachine::bind_source(DragSourceBinding binding) {
    if (!binding.source) return;
    const auto key = binding.source.get();
    // 元の識別情報には、生の Widget アドレスではなく接続の世代を使う。 コ
    // ールバックでビューを破棄すると、同じアドレスが再利用される場合があるため。
    binding.origin.source_identity = ++next_source_identity_;
    const auto old = session_;
    sources_[key] = std::make_shared<DragSourceBinding>(std::move(binding));
    if (old && old->source->source.get() == key) complete(old, DropOperation::None);
}

void DragDropStateMachine::bind_target(DropTargetBinding binding) {
    if (!binding.target) return;
    const auto key = binding.target.get();
    targets_[key] = std::make_shared<DropTargetBinding>(std::move(binding));
    if (session_ && session_->target && session_->target->target.get() == key)
        update({}, {});
}

void DragDropStateMachine::unbind(const WidgetPtr& widget) {
    if (!widget) return;
    sources_.erase(widget.get());
    targets_.erase(widget.get());
    const auto session = session_;
    if (session) {
        if (session->source->source == widget) complete(session, DropOperation::None);
        else if (session->target && session->target->target == widget) {
            ++session->revision;
            const auto target = std::exchange(session->target, {});
            session->selected = DropOperation::None;
            if (target->on_leave) target->on_leave();
        }
    }

    // 保留中のドロップは転送セッションと別に管理す
    // る。転送元が消えた場合は元への通知だけを外し
    // 、転送先が消えた場合は処理をキャンセルする。
    std::vector<std::shared_ptr<PendingDrop>> cancelled;
    for (auto it = pending_drops_.begin(); it != pending_drops_.end();) {
        const auto& pending = it->second;
        const auto& pending_session = pending->session;
        const bool source_match = pending_session && pending_session->source &&
            (pending_session->source->source == widget ||
             (pending_session->has_source_scope &&
              pending_session->source_scope.lock() == widget));
        const bool target_match = pending_session && pending_session->target &&
            (pending_session->target->target == widget ||
             (pending_session->has_target_scope &&
              pending_session->target_scope.lock() == widget));
        if (source_match && !target_match) {
            // D版の DropTarget と同様、ドロップ後のXMLBytes
            // は転送先が所有する。 外部シナリオの転送元を閉じる・隠す操作で
            // 、受け付け済みの転送先処理を捨てない。 古い転送元への通知だ
            // けを切り離し、転送先の除去では引き続き処理をキャンセルする。
            pending->source_bound = false;
            ++it;
        } else if (target_match) {
            cancelled.push_back(pending);
            it = pending_drops_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& pending : cancelled)
        complete_pending(pending, DropOperation::None);
}

void DragDropStateMachine::complete_pending(
    const std::shared_ptr<PendingDrop>& pending, const DropOperation result) {
    if (!pending || pending->completed) return;
    pending->completed = true;
    if (pending->session) {
        const auto found = pending_drops_.find(pending->session->id);
        if (found != pending_drops_.end() && found->second == pending)
            pending_drops_.erase(found);
        const auto& source = pending->session->source;
        if (pending->source_bound && source && source->completed)
            source->completed(result);
    }
}

bool DragDropStateMachine::has_source(const WidgetPtr& widget) const {
    return widget && sources_.contains(widget.get());
}

bool DragDropStateMachine::source_live(const Session& session) const {
    if (session.external) return !session.external_live || session.external_live();
    const auto found = sources_.find(session.source->source.get());
    return found != sources_.end() && found->second == session.source &&
        available(session.source->source, session.source_scope, session.has_source_scope);
}

bool DragDropStateMachine::target_live(const Session& session) const {
    if (!session.target) return false;
    const auto found = targets_.find(session.target->target.get());
    return found != targets_.end() && found->second == session.target &&
        available(session.target->target, session.target_scope, session.has_target_scope);
}

bool DragDropStateMachine::begin(const WidgetPtr& widget, Point point,
                                 const WidgetPtr& scope) {
    if (session_ || !available(widget, scope, bool(scope))) return false;
    const auto found = sources_.find(widget.get());
    if (found == sources_.end() || !found->second->provide_data ||
        choose(found->second->allowed, found->second->allowed, DropOperation::None) ==
            DropOperation::None) return false;
    auto session = std::make_shared<Session>();
    session->id = ++next_id_;
    session->source = found->second;
    session->source_scope = scope;
    session->has_source_scope = bool(scope);
    session->providing = true;
    session->start_point = session->current_point = point;
    session_ = session;
    try {
        auto data = session->source->provide_data();
        if (session_ != session) return false;
        session->origin = session->source->provide_origin ?
            session->source->provide_origin() : session->source->origin;
        session->visual = session->source->provide_visual
            ? session->source->provide_visual() : std::nullopt;
        // 識別情報の寿命・世代を保証するのは、呼び出し
        // 元のスナップショットではなくバックエンド。
        session->origin.source_identity = session->source->origin.source_identity;
        if (session_ != session) return false;
        session->providing = false;
        if (!source_live(*session) ||
            (data.text.empty() && data.files.empty() && data.formats.empty())) {
            complete(session, DropOperation::None);
            return false;
        }
        session->data = std::move(data);
        return true;
    } catch (...) {
        complete(session, DropOperation::None);
        throw;
    }
}

bool DragDropStateMachine::accepts(const DropTargetBinding& target,
                                  const ClipboardData& data) {
    return target.accepted_formats.empty() ||
        std::ranges::any_of(target.accepted_formats, [&](const std::string& format) {
            if ((format == "text/plain" || format == "text/plain;charset=utf-8") &&
                !data.text.empty()) return true;
            if (format == "text/uri-list" && !data.files.empty()) return true;
            return data.formats.contains(format);
        });
}

bool DragDropStateMachine::begin_external(ClipboardData data, std::function<bool()> live,
                                         std::function<void(DropOperation)> completed) {
    if (session_ || (data.text.empty() && data.files.empty() && data.formats.empty()))
        return false;
    auto session = std::make_shared<Session>();
    session->id = ++next_id_;
    auto source = std::make_shared<DragSourceBinding>();
    source->allowed = DropOperation::Copy | DropOperation::Link;
    source->completed = std::move(completed);
    session->source = std::move(source);
    session->external = true;
    session->external_live = std::move(live);
    session->data = std::move(data);
    session_ = session;
    try {
        const bool alive = source_live(*session);
        if (session_ != session) return false;
        if (!alive) { complete(session, DropOperation::None); return false; }
        return true;
    } catch (...) {
        complete(session, DropOperation::None);
        throw;
    }
}

WidgetPtr DragDropStateMachine::target_for(const WidgetPtr& hit, const WidgetPtr& scope,
                                          const ClipboardData& data) const {
    if (!hit || !scope || !hit->enabled()) return {};
    // 当たったコントロールの有効な祖先だけへ通知を伝える。
    // 任意の登録済み対象、非表示タブ、背後の兄弟は選ばない。
    const auto path = input_scope_path(scope, hit);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto found = targets_.find(it->get());
        if (found != targets_.end() && accepts(*found->second, data)) return *it;
    }
    return {};
}

DragEvent DragDropStateMachine::event_for(const Session& session, Point point,
                                         std::optional<std::size_t> index) {
    DragEvent event;
    event.position = point;
    event.target_index = index;
    event.data = session.data;
    event.allowed = session.source->allowed;
    event.requested = session.requested;
    event.modifiers = session.modifiers;
    event.physical_control = session.physical_control;
    event.selected = session.selected;
    event.origin = session.origin;
    if (session.target) event.target_pane_id = session.target->pane_id;
    return event;
}

void DragDropStateMachine::update(const WidgetPtr& widget, Point point,
                                 std::optional<std::size_t> index,
                                 const WidgetPtr& scope, DropOperation preferred, Modifiers modifiers,
                                 std::optional<bool> physical_control) {
    const auto session = session_;
    if (!session || session->providing || session->finishing) return;
    if (!source_live(*session)) { complete(session, DropOperation::None); return; }
    session->requested = preferred;
    session->modifiers = modifiers;
    session->physical_control = physical_control;
    session->current_point = point;
    const auto revision = ++session->revision;
    std::shared_ptr<const DropTargetBinding> target;
    if (available(widget, scope, bool(scope))) {
        const auto found = targets_.find(widget.get());
        if (found != targets_.end() && accepts(*found->second, session->data) &&
            (!found->second->accepts_target_index ||
             found->second->accepts_target_index(index)))
            target = found->second;
    }
    try {
        if (session->target != target) {
            const auto previous = std::exchange(session->target, {});
            session->selected = DropOperation::None;
            if (previous && previous->on_leave) previous->on_leave();
            if (session_ != session || revision != session->revision) return;
            if (!source_live(*session)) { complete(session, DropOperation::None); return; }
            session->target = target;
            session->target_scope = scope;
            session->has_target_scope = bool(scope);
            if (!target_live(*session)) { session->target.reset(); return; }
            session->selected = choose(session->source->allowed, session->source->allowed, preferred);
            auto event = event_for(*session, point, index);
            if (target->on_enter) target->on_enter(event);
            if (session_ != session || revision != session->revision) return;
            session->selected = choose(event.selected, session->source->allowed, preferred);
        }
        if (!source_live(*session)) { complete(session, DropOperation::None); return; }
        if (!target_live(*session)) {
            const auto previous = std::exchange(session->target, {});
            session->selected = DropOperation::None;
            if (previous && previous->on_leave) previous->on_leave();
            return;
        }
        auto event = event_for(*session, point, index);
        // 修飾キーの変更や、拒否された行からの移動も
        // 含め、移動のたびに要求操作を再判定する。
        if (target->on_over) {
            event.selected = choose(session->source->allowed, session->source->allowed, preferred);
            target->on_over(event);
        }
        if (session_ != session || revision != session->revision) return;
        session->selected = choose(event.selected, session->source->allowed, preferred);
    } catch (...) {
        complete(session, DropOperation::None);
        throw;
    }
}

std::optional<DragDropStateMachine::VisualSnapshot>
DragDropStateMachine::visual() const noexcept {
    if (!session_ || !session_->visual) return std::nullopt;
    auto bounds = session_->visual->bounds;
    bounds.x += session_->current_point.x - session_->start_point.x;
    bounds.y += session_->current_point.y - session_->start_point.y;
    return VisualSnapshot{bounds, session_->selected, session_->source->source};
}

void DragDropStateMachine::finish(const WidgetPtr& widget, Point point,
                                 std::optional<std::size_t> index,
                                 const WidgetPtr& scope, DropOperation preferred, Modifiers modifiers,
                                 std::optional<bool> physical_control) {
    const auto session = session_;
    if (!session || session->providing || session->finishing) return;
    const auto revision = session->revision;
    update(widget, point, index, scope, preferred, modifiers, physical_control);
    if (session_ != session) return;
    if (session->revision != revision + 1) {
        // コールバックが別の転送先との調整を始めた場合、 古
        // い解放イベントでその新しい対象へドロップしない。
        complete(session, DropOperation::None);
        return;
    }
    DropOperation result = DropOperation::None;
    try {
        if (source_live(*session) && target_live(*session) &&
            session->selected != DropOperation::None) {
            session->finishing = true;
            const auto target = session->target;
            auto event = event_for(*session, point, index);
            const auto deferred = std::make_shared<bool>(false);
            event.defer = [this, session, deferred](DropContinuation continuation) {
                if (*deferred || session_ != session || !session->finishing ||
                    !dispatcher_ || !continuation)
                    return false;
                *deferred = true;
                session->deferred_drop = std::move(continuation);
                return true;
            };
            if (target->on_drop) target->on_drop(event);
            if (session_ != session) return;
            if (*deferred) {
                // ネイティブの解放コールバックは戻っている。 モーダ
                // ルを開く可能性があるモデル処理より先に、ドラッグ
                // の転送状態を終了する。 転送元への完了通知は、後
                // 続処理が実際の操作の成功を判断するまで保留する。
                const auto pending = retire_for_deferred(session);
                if (!pending || pending->completed) return;
                const auto id = session->id;
                const auto lifetime = std::weak_ptr<int>(lifetime_);
                try {
                    dispatcher_->post([this, id, lifetime] {
                        if (!lifetime.expired()) run_deferred(id);
                    });
                } catch (...) {
                    complete_pending(pending, DropOperation::None);
                    throw;
                }
                return;
            }
            // 拒否・コピー・不正な結果を移動へ変えない。 移動完了は
            // 、DWTの複数の転送元でデータ削除の許可になるため。
            if (source_live(*session) && target_live(*session) && event.handled &&
                valid_result(event.selected, session->source->allowed))
                result = event.selected;
        }
    } catch (...) {
        complete(session, DropOperation::None);
        throw;
    }
    complete(session, result);
}

std::shared_ptr<DragDropStateMachine::PendingDrop>
DragDropStateMachine::retire_for_deferred(const std::shared_ptr<Session>& session) {
    if (session_ != session || !session->deferred_drop) return {};
    auto pending = std::make_shared<PendingDrop>();
    pending->session = session;
    pending->continuation = std::exchange(session->deferred_drop, {});
    pending_drops_.emplace(session->id, pending);

    ::cwxeditor::cwx::utils::debugln(
        "[DROP_TRANSPORT_RETIRED] session=", session->id,
        " source=", session->origin.pane_id,
        " target=", session->target ? session->target->pane_id : std::string{});

    // コールバックが転送元・先の接続を外す前に、保留中の完了状態
    // を反映する。 この時点以降、有効な転送セッションはない。
    session_.reset();
    try {
        if (session->target && session->target->on_leave)
            session->target->on_leave();
    } catch (...) {
        complete_pending(pending, DropOperation::None);
        throw;
    }
    return pending;
}

void DragDropStateMachine::run_deferred(const std::uint64_t id) {
    const auto found = pending_drops_.find(id);
    if (found == pending_drops_.end()) return;
    const auto pending = found->second;
    pending_drops_.erase(found); // 登録解除では実行中の継続処理を取り消せない。
    if (!pending || pending->completed || !pending->session) return;
    const auto& session = pending->session;
    DropOperation result = DropOperation::None;
    try {
        // 解放時のスナップショットがデータを所有する。 D版と同様、外部の取り
        // 込みタブが閉じて転送元のコントロールが破棄されても、 そのバイト列で
        // DropTarget を呼ぶ。有効である必要があるのは転送先だけ。
        if (target_live(*session) && pending->continuation)
            result = pending->continuation();
    } catch (...) {
        complete_pending(pending, DropOperation::None);
        throw;
    }
    if (!valid_result(result, session->source->allowed))
        result = DropOperation::None;
    complete_pending(pending, result);
}

void DragDropStateMachine::complete(const std::shared_ptr<Session>& session,
                                   DropOperation result) {
    if (session_ != session) return;
    session_.reset(); // 通知前に切り離す。コールバックが新しいドラッグを始める場合があるため。
    const auto target = std::exchange(session->target, {});
    try {
        if (target && target->on_leave) target->on_leave();
    } catch (...) {
        if (session->source->completed) session->source->completed(DropOperation::None);
        throw;
    }
    if (session->source->completed) session->source->completed(result);
}

void DragDropStateMachine::cancel() {
    const auto session = session_;
    if (session) complete(session, DropOperation::None);
}

void DragDropStateMachine::cancel_for(const WidgetPtr& scope) {
    if (!scope || !session_) return;
    if (session_->source_scope.lock() == scope) cancel();
    else if (session_->target_scope.lock() == scope) update({}, {});
}

} // namespace cwxeditor::lib
