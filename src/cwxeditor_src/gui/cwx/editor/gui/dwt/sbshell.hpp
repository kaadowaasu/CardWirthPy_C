#pragma once

// cwx/editor/gui/dwt/sbshell.d
// の移植可能なC++20実装。 SWTのShell/Composite
// とWindowsのステータスバーハンドルを、共
// 通libの部品へ置き換える。 どのOSでもWindow
// の子として実際のStatusBarを持ち
// 、描画方法だけをバックエンドが決める。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace cwxeditor::cwx::editor::gui::dwt::sbshell {

/// ホストのGUIイベントループが渡すサイ
/// ズ変更情報。 イベント変換はバックエン
/// ドが担当するため、この小さなデータで
/// DWTのリスナー契約を保ち、 HWND
/// やSWTのイベント型を公開しない。
struct ControlResizeEvent {
    ::cwxeditor::lib::Size size{};
    bool minimized{};
    bool maximized{};
};

class SBShell final {
public:
    using Services = ::cwxeditor::lib::Services;
    using Window = ::cwxeditor::lib::Window;
    using Panel = ::cwxeditor::lib::Panel;
    using StatusBar = ::cwxeditor::lib::StatusBar;

    explicit SBShell(std::shared_ptr<Window> parent = {},
                     std::int32_t style = 0,
                     Services services = {});

    SBShell(const SBShell&) = delete;
    SBShell& operator=(const SBShell&) = delete;
    SBShell(SBShell&&) = delete;
    SBShell& operator=(SBShell&&) = delete;
    ~SBShell();

    [[nodiscard]] const std::shared_ptr<Window>& shell() const noexcept {
        return _shl;
    }
    [[nodiscard]] const std::shared_ptr<Panel>& contentPane() const noexcept {
        return _contentPane;
    }
    [[nodiscard]] const std::shared_ptr<StatusBar>& statusBar() const noexcept {
        return _sbar;
    }
    [[nodiscard]] const Services& services() const noexcept { return _services; }
    [[nodiscard]] std::int32_t style() const noexcept { return _style; }
    [[nodiscard]] bool disposed() const noexcept { return _disposed; }

    /// バックエンドのサイズ変更を、共通のシェルと二つの行へ適用する。
    void controlResized(const ControlResizeEvent& event) noexcept;
    /// 論理的なシェルを破棄し、子部品を解放する。
    void widgetDisposed() noexcept;
    /// ステータス行の文字列を設定する。共通StatusBarは通常のUTF-8
    /// を持つため、 OSのアクセラレーター用のエスケープは行わない。
    void statusLine(std::string text);

private:
    class DL final {
    public:
        explicit DL(SBShell& owner) noexcept : _owner(&owner) {}
        void widgetDisposed() noexcept;

    private:
        SBShell* _owner{};
    };

    class CL final {
    public:
        explicit CL(SBShell& owner) noexcept : _owner(&owner) {}
        void controlResized(const ControlResizeEvent& event) noexcept;

    private:
        SBShell* _owner{};
    };

    static constexpr float statusBarHeight = 20.0F;

    Services _services{};
    std::weak_ptr<Window> _parent{};
    std::shared_ptr<Window> _shl{};
    std::shared_ptr<Panel> _contentPane{};
    std::shared_ptr<StatusBar> _sbar{};
    std::unique_ptr<DL> _disposeListener{};
    std::unique_ptr<CL> _controlListener{};
    std::int32_t _style{};
    bool _disposed{};

    void initStatusBar();
    void applyResize(const ControlResizeEvent& event) noexcept;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::sbshell
