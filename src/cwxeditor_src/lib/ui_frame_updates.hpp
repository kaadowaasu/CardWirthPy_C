#pragma once
#include "ui_frame_presentation.hpp"
#include "ui_window_stack.hpp"
#include <unordered_set>

namespace cwxeditor::lib {
class MenuPopupStateMachine;
// UIスレッドの状態。別スレッドからの要求はプラットフォーム側でUIスレ
// ッドへ配送する。ネイティブ資源の再生成時には新しい状態を持ち、破棄中の
// 描画面が同じアドレスに作られた後続ウィンドウの処理を完了扱いにしない。
class FrameUpdateState {
public:
    void request() noexcept;
    void retire() noexcept;
    // 入れ子のモデル更新の開始・終了は、レンダラーではなくウィジェットをキーに管理す
    // る。失効したウィジェットが、有効なウィンドウを永久に停止させないようにする。
    bool set_redraw(const WidgetPtr& widget, bool enabled);
    [[nodiscard]] bool suspended() const noexcept;
    [[nodiscard]] bool pending() const noexcept { return pending_ && !retired_ && !suspended(); }
    [[nodiscard]] bool retired() const noexcept { return retired_; }
    [[nodiscard]] std::uint64_t completed_revision() const noexcept { return completed_; }
private:
    friend class SurfaceUpdateTransaction;
    std::uint64_t requested_{1}, completed_{};
    bool pending_{true}, active_{}, retired_{};
    struct Hold { std::weak_ptr<Widget> widget; std::size_t depth{1}; };
    std::vector<Hold> holds_;
};
struct FrameUpdateTarget {
    std::shared_ptr<Window> window;
    std::shared_ptr<FrameUpdateState> updates;
};
// バックエンドは描画面の不透明な識別子だけを提供する。再入と
// 公開の規則は単体版、ALL、モバイルの共有描画面で同じ。
class SurfaceUpdateRegistry {
private:
    friend class SurfaceUpdateTransaction;
    std::unordered_set<std::uintptr_t> active_;
};
class SurfaceUpdateTransaction {
public:
    SurfaceUpdateTransaction(SurfaceUpdateRegistry&, std::uintptr_t surface,
                             std::vector<FrameUpdateTarget> targets);
    ~SurfaceUpdateTransaction();
    SurfaceUpdateTransaction(const SurfaceUpdateTransaction&) = delete;
    SurfaceUpdateTransaction& operator=(const SurfaceUpdateTransaction&) = delete;
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool can_replay_completed() const { return !started_ && !registry_.active_.contains(surface_); }
    [[nodiscard]] const std::vector<std::shared_ptr<Window>>& windows() const { return frame_.windows(); }
    bool append(const std::shared_ptr<Window>& window, FrameStructureSnapshot snapshot);
    // trueなら保留中のラスタ画像を完成した画像と交換してよい。
    // 共有描画面の全ウィンドウが、このトランザクション開始時と同じ
    // 世代でなければならない。準備中に要求があれば一組全体を拒否
    // し、共有画像に新旧のウィンドウ世代が混在しないようにする。
    bool complete(const std::vector<std::shared_ptr<Window>>& current_windows);
private:
    void resolve(bool publish) noexcept;
    SurfaceUpdateRegistry& registry_;
    std::uintptr_t surface_;
    std::vector<FrameUpdateTarget> targets_;
    SurfaceFramePresentation frame_;
    std::vector<std::uint64_t> revisions_;
    bool started_{}, resolved_{};
};
// nullは有効な全ウィンドウの描画を明示的に無効化する。切り離された、または
// 閉じたウィジェットによって無関係なウィンドウを再描画しない。代替先は、呼び出
// し側が論理的な所有元を知っているバックエンド所有のポップアップにだけ使う。
[[nodiscard]] std::vector<std::shared_ptr<Window>> redraw_windows(
    const WindowStackStateMachine& stack, const WidgetPtr& widget,
    const std::shared_ptr<Window>& detached_owner = {});
// ポップアップMenuは、構造上は論理的な所有元の子ではない。通常の
// 構造による解決の前に、共通ポップアップ状態から再描画先を解決する。
// ネイティブアダプターは返されたウィンドウだけに再描画を要求する。
[[nodiscard]] std::vector<std::shared_ptr<Window>> redraw_popup_windows(
    const WindowStackStateMachine& stack, const MenuPopupStateMachine& popup,
    const WidgetPtr& widget);
} // namespace cwxeditor::lib
