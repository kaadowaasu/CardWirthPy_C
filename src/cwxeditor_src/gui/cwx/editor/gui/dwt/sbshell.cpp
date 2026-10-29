#include "cwxeditor_src/gui/cwx/editor/gui/dwt/sbshell.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::sbshell {

SBShell::SBShell(std::shared_ptr<Window> parent,
                 std::int32_t style,
                 Services services)
    : _services(std::move(services)), _parent(parent), _style(style) {
    try {
        if (_services.gui) _shl = _services.gui->create_window();
        if (!_shl) _shl = std::make_shared<Window>();

        // 新規のSWTウィンドウと同様、所有者が表示するまで非表
        // 示とする。 共通モデルでもこの寿命の扱いを維持する。
        _shl->set_visible(false);
        _disposeListener = std::make_unique<DL>(*this);
        _controlListener = std::make_unique<CL>(*this);
        if (parent) parent->add(_shl);

        initStatusBar();
        _shl->on_close_requested = [this] {
            if (_disposeListener) _disposeListener->widgetDisposed();
        };
    } catch (...) {
        if (_shl) {
            _shl->on_close_requested = {};
            _shl->clear();
        }
        if (parent && _shl) parent->remove(_shl);
        _contentPane.reset();
        _sbar.reset();
        _shl.reset();
        throw;
    }
}

SBShell::~SBShell() {
    widgetDisposed();
}

void SBShell::initStatusBar() {
    if (!_shl) throw std::runtime_error("SBShell has no Window");
    if (_contentPane && _sbar) return;

    _shl->set_layout(
        std::make_shared<::cwxeditor::lib::LinearLayout>(
            ::cwxeditor::lib::Orientation::Vertical));
    if (const auto layout = _shl->layout()) {
        layout->padding = {};
        layout->spacing = 0.0F;
    }

    if (_services.gui) {
        auto widget = _services.gui->create_component(
            ::cwxeditor::lib::ComponentKind::Panel);
        _contentPane = std::dynamic_pointer_cast<Panel>(std::move(widget));
    }
    if (!_contentPane) _contentPane = std::make_shared<Panel>();
    _contentPane->set_layout(std::make_shared<::cwxeditor::lib::FillLayout>());

    if (_services.gui) {
        auto widget = _services.gui->create_component(
            ::cwxeditor::lib::ComponentKind::StatusBar);
        _sbar = std::dynamic_pointer_cast<StatusBar>(std::move(widget));
    }
    if (!_sbar) _sbar = std::make_shared<StatusBar>();

    ::cwxeditor::lib::LayoutItem contentItem;
    contentItem.grow = 1.0F;
    contentItem.horizontal = ::cwxeditor::lib::Align::Stretch;
    contentItem.vertical = ::cwxeditor::lib::Align::Stretch;
    _contentPane->set_layout_item(contentItem);

    ::cwxeditor::lib::LayoutItem statusItem;
    statusItem.preferred.height = statusBarHeight;
    statusItem.minimum.height = statusBarHeight;
    statusItem.horizontal = ::cwxeditor::lib::Align::Stretch;
    statusItem.vertical = ::cwxeditor::lib::Align::Stretch;
    _sbar->set_layout_item(statusItem);

    _shl->add(_contentPane);
    _shl->add(_sbar);
}

void SBShell::applyResize(const ControlResizeEvent& event) noexcept {
    if (_disposed || !_shl || !_contentPane || !_sbar || event.minimized) return;

    auto shellBounds = _shl->bounds();
    shellBounds.width = std::max(0.0F, event.size.width);
    shellBounds.height = std::max(0.0F, event.size.height);
    _shl->set_bounds(shellBounds);

    // DWTの FormLayout と同様、下部にネイティブのステータ
    // スバーの行を確保し、 残りの領域はすべて内容ペインに割り当てる。
    const auto visibleStatusHeight =
        std::min(statusBarHeight, shellBounds.height);
    const auto contentHeight = shellBounds.height - visibleStatusHeight;

    ::cwxeditor::lib::Rect contentBounds;
    contentBounds.width = shellBounds.width;
    contentBounds.height = contentHeight;
    _contentPane->set_bounds(contentBounds);

    ::cwxeditor::lib::Rect statusBounds;
    statusBounds.y = contentHeight;
    statusBounds.width = shellBounds.width;
    statusBounds.height = visibleStatusHeight;
    _sbar->set_bounds(statusBounds);
}

void SBShell::controlResized(const ControlResizeEvent& event) noexcept {
    if (_controlListener) {
        _controlListener->controlResized(event);
    } else {
        applyResize(event);
    }
}

void SBShell::widgetDisposed() noexcept {
    if (_disposed) return;
    _disposed = true;

    const auto shell = _shl;
    if (shell) {
        shell->on_close_requested = {};
        shell->set_visible(false);
        shell->clear();
    }
    if (const auto parent = _parent.lock(); parent && shell) {
        parent->remove(shell);
    }
    _parent.reset();
    _contentPane.reset();
    _sbar.reset();
}

void SBShell::statusLine(std::string text) {
    if (_disposed || !_sbar) return;
    _sbar->text = std::move(text);
    if (_services.gui) _services.gui->request_redraw(_sbar);
}

void SBShell::DL::widgetDisposed() noexcept {
    if (_owner) _owner->widgetDisposed();
}

void SBShell::CL::controlResized(const ControlResizeEvent& event) noexcept {
    if (_owner) _owner->applyResize(event);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::sbshell
