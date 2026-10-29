#pragma once

#include "drag_drop.hpp"
#include <memory>
#include <unordered_map>

namespace cwxeditor::lib {

class MainDispatcher;

// 操作の合意形成とコールバックの寿命を、OSの表示側から独立して管理する
// 。元データの削除を許可できるのは、明示的かつ単一の許可された結果だけ。
class DragDropStateMachine final : public DragDropBackend {
public:
    explicit DragDropStateMachine(std::shared_ptr<MainDispatcher> dispatcher = {});

    struct VisualSnapshot {
        Rect bounds{};
        DropOperation operation{DropOperation::None};
        WidgetPtr source;
    };
    void bind_source(DragSourceBinding binding) override;
    void bind_target(DropTargetBinding binding) override;
    void unbind(const WidgetPtr& widget) override;
    [[nodiscard]] bool has_source(const WidgetPtr& widget) const;
    bool begin(const WidgetPtr& widget, Point position, const WidgetPtr& scope = {});
    // ネイティブのファイル・文字列の受け渡しには元データの削除確認がない。
    // 操作の合意形成は共用するが、代わりにMoveを許可してはならない。
    bool begin_external(ClipboardData data, std::function<bool()> live = {},
                        std::function<void(DropOperation)> completed = {});
    [[nodiscard]] WidgetPtr target_for(const WidgetPtr& hit, const WidgetPtr& scope,
                                       const ClipboardData& data) const;
    void update(const WidgetPtr& widget, Point position,
                std::optional<std::size_t> index = std::nullopt,
                const WidgetPtr& scope = {},
                DropOperation preferred = DropOperation::None,
                Modifiers modifiers = Modifiers::None,
                std::optional<bool> physical_control = std::nullopt);
    void finish(const WidgetPtr& widget, Point position,
                std::optional<std::size_t> index = std::nullopt,
                const WidgetPtr& scope = {},
                DropOperation preferred = DropOperation::None,
                Modifiers modifiers = Modifiers::None,
                std::optional<bool> physical_control = std::nullopt);
    void cancel();
    void cancel_for(const WidgetPtr& scope);
    [[nodiscard]] bool active() const noexcept { return bool(session_); }
    [[nodiscard]] std::uint64_t session_id() const noexcept { return session_ ? session_->id : 0; }
    // 実行中のセッションのドロップ可否カーソル。ポインター下の対象が操作を
    // 受け付ける場合はMOVE型、そうでなければドロップ不可を表示する。
    [[nodiscard]] std::optional<CursorKind> feedback_cursor() const noexcept {
        if (!session_ || session_->providing || session_->finishing) return std::nullopt;
        return session_->selected == DropOperation::None ? CursorKind::Forbidden
                                                         : CursorKind::Move;
    }
    [[nodiscard]] std::optional<VisualSnapshot> visual() const noexcept;
    // ネイティブのドラッグセッションなしで呼ばれる型付きの対象でも使用する。
    static DropOperation choose(DropOperation choices, DropOperation allowed, DropOperation preferred);

private:
    struct Session {
        std::shared_ptr<const DragSourceBinding> source;
        std::shared_ptr<const DropTargetBinding> target;
        std::weak_ptr<Widget> source_scope;
        std::weak_ptr<Widget> target_scope;
        bool has_source_scope{}, has_target_scope{}, providing{}, finishing{};
        bool external{};
        std::function<bool()> external_live;
        ClipboardData data;
        DragOrigin origin;
        std::optional<DragVisual> visual;
        Point start_point{}, current_point{};
        DropOperation requested{DropOperation::None};
        Modifiers modifiers{Modifiers::None};
        std::optional<bool> physical_control;
        DropOperation selected{DropOperation::None};
        std::uint64_t revision{};
        std::uint64_t id{};
        DropContinuation deferred_drop;
    };
    struct PendingDrop {
        std::shared_ptr<Session> session;
        DropContinuation continuation;
        // ポインター解放時にデータをSessionへコピーした後は、
        // アプリケーションのトランザクションはドラッグ元に依存しな
        // い。遅延実行先がDの素材確認を開く間に、取り込み元のタブ
        // が閉じる場合がある。対象のトランザクションは維持するが、
        // 元の接続先が解除されていれば完了コールバックは呼ばない。
        bool source_bound{true};
        bool completed{};
    };
    [[nodiscard]] bool source_live(const Session&) const;
    [[nodiscard]] bool target_live(const Session&) const;
    void complete(const std::shared_ptr<Session>&, DropOperation);
    std::shared_ptr<PendingDrop> retire_for_deferred(
        const std::shared_ptr<Session>& session);
    void complete_pending(const std::shared_ptr<PendingDrop>&, DropOperation);
    static bool accepts(const DropTargetBinding&, const ClipboardData&);
    static bool valid_result(DropOperation, DropOperation);
    static DragEvent event_for(const Session&, Point, std::optional<std::size_t>);
    void run_deferred(std::uint64_t id);

    std::unordered_map<Widget*, std::shared_ptr<const DragSourceBinding>> sources_;
    std::unordered_map<Widget*, std::shared_ptr<const DropTargetBinding>> targets_;
    std::unordered_map<std::uint64_t, std::shared_ptr<PendingDrop>> pending_drops_;
    std::shared_ptr<Session> session_;
    std::shared_ptr<MainDispatcher> dispatcher_;
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::uint64_t next_id_{};
    std::uint64_t next_source_identity_{};
};

} // namespace cwxeditor::lib
