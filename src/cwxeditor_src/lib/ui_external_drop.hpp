#pragma once

#include "ui_drag_drop.hpp"
#include "ui_window_stack.hpp"

namespace cwxeditor::lib {

enum class ExternalDropPhase { Begin, Data, Position, Complete, Cancel };
struct ExternalDropEvent {
    ExternalDropPhase phase{};
    WindowStackStateMachine::SurfaceId surface{};
    // ポインター入力と同じく、配置済みのUI座標に変換済み。
    std::optional<Point> position;
    ClipboardData data;
};
struct ExternalDropContext {
    DragDropStateMachine& drag;
    const WindowStackStateMachine& stack;
    const ModalStateMachine& modal;
    // ネイティブ側の世代確認だけを行う。所有関係、モーダル、ヒット判定は共通処理で扱う。
    std::function<bool(const std::shared_ptr<Window>&)> live;
    std::function<void(const std::shared_ptr<Window>&)> redraw;
};
struct ExternalDropResult {
    bool consumed{};
    std::shared_ptr<Window> scope;
    DropOperation operation{DropOperation::None};
};

// モデルに通知する前に、物理的な描画面ごとに一つのネイティブ入力のまとまりを組み立て
// る。ネイティブハンドルは所有せず、入力元ファイルの読み取り・移動・削除も行わない。
class ExternalDropStateMachine {
public:
    [[nodiscard]] ExternalDropResult receive(ExternalDropEvent event, ExternalDropContext context);
    void retire(WindowStackStateMachine::SurfaceId surface) noexcept;
    [[nodiscard]] bool pending(WindowStackStateMachine::SurfaceId surface) const noexcept;
private:
    struct Batch { ClipboardData data; std::optional<Point> position; };
    std::unordered_map<WindowStackStateMachine::SurfaceId, Batch> batches_;
};

} // namespace cwxeditor::lib
