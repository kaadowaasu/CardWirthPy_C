#pragma once

#include "ui_window_attributes.hpp"

namespace cwxeditor::lib {
struct WindowMaximizeContext {
    bool embedded{};
    Rect bounds,available;
    std::optional<bool> native_maximized;
    std::uint64_t timestamp{}; // observe()と同じ単位の、単調増加する入力元の時計。
    std::function<bool(bool)> request_native;
    std::function<bool(const WindowGeometryPlan&)> place;
    std::function<bool()> live;
};
// ネイティブの最大化・復元時の座標はOSが管理する。借用したALL描画面
// では同じ論理状態を使うが、ホストを操作せずに矩形を反映する。一つの状態
// は一つのネイティブ世代に属し、OS全体の単一インスタンスにはしない。
class WindowMaximizeStateMachine {
public:
    WindowAttributeResult update(std::shared_ptr<Window> window,WindowMaximizeContext context);
    // ネイティブアダプターはキュー内のイベント内容を信用せず、現在のフラグを問い合わ
    // せる。古いイベントや、未送信のより新しいモデル要求によってUIを巻き戻さない。
    bool observe(const std::shared_ptr<Window>& window,bool maximized,std::uint64_t timestamp);
    // ネイティブの最大化状態は、対応するクライアントサイズのイベ
    // ントより先に届く場合があるため、座標の反映確認は別に行う。
    bool observe_geometry(const std::shared_ptr<Window>& window,Rect bounds,bool maximized);
    void reset() noexcept;
private:
    std::weak_ptr<Window> owner_;
    std::uint64_t revision_{},observed_at_{},requested_at_{};
    bool initialized_{},embedded_{},applied_{},last_desired_{};
    bool initial_maximize_geometry_pending_{};
    Rect initial_maximize_bounds_{};
    std::optional<bool> pending_;
    std::optional<std::uint64_t> issuing_revision_;
    std::optional<Rect> restore_;
    float restore_scale_{1};
    bool placing_{};
};
} // namespace cwxeditor::lib
