#pragma once

#include "ui_actions.hpp"
#include "ui_scroll.hpp"

namespace cwxeditor::lib {

struct ChromeInputContext {
    float tab_height{}, expand_height{}, row_height{};
    bool allow_begin{true};
    std::function<bool()> live;
    ScrollTextProvider text;
};

struct ChromeInputResult {
    bool handled{}, redraw{}, began{};
    CursorKind cursor{CursorKind::Default};
};

// 分割部やCoolBarのつまみ、項目のスクロールバー、表の列境界
// で共有するポインターの所有管理。座標は配置済みの表示画素単位。
class ChromeInputStateMachine {
public:
    ChromeInputResult dispatch(const std::shared_ptr<Window>& scope,
                               const PointerEvent& event,
                               const ChromeInputContext& context);
    [[nodiscard]] WidgetPtr owner() const;
    [[nodiscard]] std::shared_ptr<Window> scope() const;
    [[nodiscard]] bool touch_active() const;
    void cancel_for(const WidgetPtr& root, bool commit_preview = false);
    // フレーム終了コールバックが同じウィンドウで別の操作
    // 部のジェスチャーを開始する前に、同一性を取得する。
    [[nodiscard]] Action prepare_cancel_for(const WidgetPtr& root,
                                            bool commit_preview = false);
    // 同一性を確認して同期的に完了する。無関係なキャプチャや再入で始まったキャプチャは維持する。
    [[nodiscard]] Action prepare_pointer_cancel(const std::shared_ptr<Window>& scope,
                                                const PointerEvent& event);
    void reset() { active_.reset(); }
    void discard_stale();
private:
    enum class Kind { Frame, Scrollbar, Column };
    struct Capture {
        std::weak_ptr<Window> scope;
        WidgetPtr owner;
        PointerType type{};
        std::uint64_t pointer{};
        float scale{};
        Point last{};
        std::function<bool()> live;
        Kind kind{};
        FrameDragStateMachine frame;
        ItemScrollBarStateMachine scrollbar;
        TableColumnResizeStateMachine column;
    };
    static bool valid(const Capture& capture);
    static void arrange(const Capture& capture);
    std::shared_ptr<Capture> active_;
};

} // namespace cwxeditor::lib
