#pragma once

#include "components.hpp"
#include <memory>
#include <vector>

namespace cwxeditor::lib {
class ModalStateMachine;
// 構造上の所属。入力範囲とは異なり、非表示・非アクティブなページも含む。
[[nodiscard]] bool contains_widget(const WidgetPtr& root, const Widget* wanted);
// 描画・入力の所有関係は、別の論理Windowの内部まではたどらない。非ア
// クティブなページも自身のウィンドウに属し、その次のフレームを要求できる。
[[nodiscard]] bool window_contains_widget(const std::shared_ptr<Window>& root, const Widget* wanted);

// 中心基準の最近傍再標本化を共通の規則とし、ネイティブの画素面と組
// み込みの入力格子のどちらでも、ウィンドウごとの最終マスクを作る。
[[nodiscard]] bool sample_window_input_mask(const WindowInputMask& source,
    std::uint32_t target_width, std::uint32_t target_height,
    std::uint32_t target_x, std::uint32_t target_y) noexcept;
[[nodiscard]] std::shared_ptr<const WindowInputMask> resample_window_input_mask(
    const WindowInputMask& source, std::uint32_t target_width, std::uint32_t target_height);

// 描画面はバックエンドの不透明な識別情報であり、ネイティブのウィンドウAPI
// ではない。ALLや単一描画面の表示では、複数の論理ウィンドウで共有できる。
class WindowStackStateMachine {
public:
    using SurfaceId = std::uintptr_t;
    void attach(const std::shared_ptr<Window>& window, SurfaceId surface,
                Rect bounds, bool embedded);
    void set_bounds(const std::shared_ptr<Window>& window, Rect bounds);
    void raise(const std::shared_ptr<Window>& window, const ModalStateMachine* modal = nullptr);
    // 所有元をアクティブ化・前面化せずに、最前面・モーダルの規則
    // を整合させる。所有する子は、所有元の最前面の層を継承する。
    void reconcile_order(const ModalStateMachine* modal = nullptr);
    void remove(const std::shared_ptr<Window>& window);
    [[nodiscard]] std::vector<std::shared_ptr<Window>> windows() const;
    // 一つの物理描画面に属する論理メンバーを、重なり順で取得する。非表示
    // のメンバーも含め、通知・入力側の呼び出し元が対象の可否を決める。
    [[nodiscard]] std::vector<std::shared_ptr<Window>> surface_windows(SurfaceId surface) const;
    [[nodiscard]] std::vector<std::shared_ptr<Window>> close_order(
        const std::shared_ptr<Window>& root) const;
    [[nodiscard]] std::shared_ptr<Window> front(SurfaceId surface) const;
    [[nodiscard]] std::shared_ptr<Window> notification_window(
        SurfaceId surface, const WidgetPtr& focused = {}) const;
    [[nodiscard]] std::shared_ptr<Window> owner_of(const WidgetPtr& widget) const;
    // 選択画面は、アクティブな論理入力ウィンドウに属する。プレビューの重ね
    // 表示や、ALLホストを共有する無関係なネイティブ描画面には属さない。
    [[nodiscard]] std::shared_ptr<Window> dialog_owner(
        const ModalStateMachine& modal, const WidgetPtr& focused = {}) const;
    [[nodiscard]] std::shared_ptr<Window> input_window(
        SurfaceId surface, std::optional<Point> position, const ModalStateMachine& modal,
        const std::shared_ptr<Window>& captured = {}) const;
private:
    struct Entry {
        std::weak_ptr<Window> window;
        SurfaceId surface{};
        Rect bounds;
        bool embedded{};
        std::shared_ptr<const WindowInputMask> input_mask;
        bool input_mask_enabled{};
    };
    std::vector<Entry> entries_;
};
} // namespace cwxeditor::lib
