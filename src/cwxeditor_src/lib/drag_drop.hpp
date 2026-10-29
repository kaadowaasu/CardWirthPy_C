#pragma once

#include "clipboard.hpp"
#include "components.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

enum class DropOperation : std::uint32_t {
    None = 0,
    Copy = 1U << 0U,
    Move = 1U << 1U,
    Link = 1U << 2U,
};

// ドロップ先が、転送側の解放通知から戻った後にモデル処
// 理を続けるための継続処理。 寿命は状態管理側が所有し
// 、継続処理でDragEventの参照を保持しない。
using DropContinuation = std::function<DropOperation()>;

constexpr DropOperation operator|(DropOperation left, DropOperation right) noexcept {
    return static_cast<DropOperation>(static_cast<std::uint32_t>(left) |
                                      static_cast<std::uint32_t>(right));
}

// モデル側のドラッグ元が渡すスナップショット。OSの転送デ
// ータには含めない。 アプリデータを再解析せず、同じペイン
// 内の並べ替えと別ペインへのコピーを区別するために使う。
struct DragOrigin {
    std::string pane_id;
    std::vector<std::string> selected_keys;
    std::optional<std::size_t> cursor_index;
    std::uint64_t source_identity{};

    [[nodiscard]] bool same_pane_as(std::string_view pane) const noexcept {
        return !pane_id.empty() && pane_id == pane;
    }
};

// 表示実装に依存しないネイティブのドラッグ画像要求
// 。 範囲は操作元部品と同じ画面座標で、表示側は半
// 透明の追従画像を描き、独自の挿入印は作らない。
struct DragVisual {
    Rect bounds{};
};

struct DragEvent {
    Point position{};
    std::optional<std::size_t> target_index;
    ClipboardData data;
    DropOperation allowed{DropOperation::None};
    // 転送側の修飾・要求。selectedは実効的な操
    // 作として維持し、既存の受け入れ先との契約を保つ。
    DropOperation requested{DropOperation::None};
    DropOperation selected{DropOperation::None};
    DragOrigin origin;
    std::string target_pane_id;
    bool handled{};
    // メインループの配送機能を持つ実装向けの任意処理。 モデル変更前に制御
    // を返す必要がある場合、受け入れ先はon_dropの実行中だけ呼べる。
    std::function<bool(DropContinuation)> defer;
    // 最新のポインター情報。finishが解放時の修飾キーを渡す。
    Modifiers modifiers{Modifiers::None};
    std::optional<bool> physical_control;

    [[nodiscard]] bool same_pane() const noexcept {
        return origin.same_pane_as(target_pane_id);
    }
};

struct DragSourceBinding {
    WidgetPtr source;
    DropOperation allowed{DropOperation::Copy};
    std::function<ClipboardData()> provide_data;
    std::function<void(DropOperation)> completed;
    DragOrigin origin;
    // 操作元の接続中にも選択が変わり得る場合、begin()でprovide_data
    // と同時に取得し、 受け入れ先へ一つの安定した状態を渡す。
    std::function<DragOrigin()> provide_origin;
    std::function<std::optional<DragVisual>()> provide_visual;
};

struct DropTargetBinding {
    WidgetPtr target;
    std::vector<std::string> accepted_formats;
    std::function<void(DragEvent&)> on_enter;
    std::function<void(DragEvent&)> on_over;
    std::function<void(DragEvent&)> on_drop;
    Action on_leave;
    // 空はモデルのペイン識別なしを表し、同じペイ
    // ンとは判定しない。 検証関数で、通知がモデ
    // ルを変える前に最後の古い添字の検査を行う。
    std::string pane_id;
    std::function<bool(std::optional<std::size_t>)> accepts_target_index;
};

class DragDropBackend {
public:
    virtual ~DragDropBackend() = default;
    virtual void bind_source(DragSourceBinding binding) = 0;
    virtual void bind_target(DropTargetBinding binding) = 0;
    virtual void unbind(const WidgetPtr& widget) = 0;
};

}  // namespace cwxeditor::lib
