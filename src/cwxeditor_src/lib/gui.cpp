#include "gui.hpp"

#include <future>

namespace cwxeditor::lib {

namespace {
WindowShownHook& window_shown_hook() {
    static WindowShownHook hook;
    return hook;
}
}  // namespace

void set_window_shown_hook(WindowShownHook hook) {
    window_shown_hook() = std::move(hook);
}

void notify_window_shown(const std::shared_ptr<Window>& window) {
    if (!window) return;
    // フックが実行中に自身を置き換えられるよう、複製してから呼ぶ。
    const auto hook = window_shown_hook();
    if (hook) hook(window);
}

void run_background_io_operation(
    const std::function<bool(ExternalDialogOperation)>& wait, Action work) {
    if (!work) return;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    // 単一スレッドのWeb構成では、ネイティブのワーカーを起動できない。 Asyncify
    // の入口を追加したり、この代替処理を非同期として扱ったりしない。
    (void)wait;
    work();
#else
    std::future<void> result;
    ExternalDialogOperation operation;
    operation.start = [&] { result = std::async(std::launch::async, std::move(work)); };
    operation.completed = [&] {
        return result.valid() &&
            result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };
    operation.timeout = std::chrono::hours(24);
    // モーダル表示が例外・中断で終わっても、Future の破棄時に終了を待つ。 保
    // 存操作を切り離したり、借用する入出力オブジェクトより長く存続させたりしない。
    const bool completed = wait && wait(std::move(operation));
    if (!result.valid()) throw std::runtime_error("Unable to start the file operation's UI wait");
    result.get();
    if (!completed) throw std::runtime_error("The file operation's UI wait was interrupted");
#endif
}

ScopedRedraw::ScopedRedraw(std::shared_ptr<GuiBackend> gui, WidgetPtr widget)
    : gui_(std::move(gui)), widget_(std::move(widget)) {
    if (!gui_ || !widget_) return;
    // スコープを作る前に取得する。取得失敗時には解放せず、 別
    // の外側の再描画抑止の回数を誤って減らさないようにする。
    gui_->set_redraw(widget_, false);
}

ScopedRedraw::~ScopedRedraw() noexcept {
    if (gui_ && widget_) {
        try { gui_->set_redraw(widget_, true); } catch (...) {}
    }
}

std::shared_ptr<Window> GuiBackend::create_window() { return std::make_shared<Window>(); }
std::shared_ptr<Dialog> GuiBackend::create_dialog() { return std::make_shared<Dialog>(); }

WidgetPtr GuiBackend::create_component(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::Window: return std::make_shared<Window>();
    case ComponentKind::Dialog: return std::make_shared<Dialog>();
    case ComponentKind::Panel: return std::make_shared<Panel>();
    case ComponentKind::GroupBox: return std::make_shared<GroupBox>();
    case ComponentKind::Label: return std::make_shared<Label>();
    case ComponentKind::Link: return std::make_shared<Link>();
    case ComponentKind::Button: return std::make_shared<Button>();
    case ComponentKind::ToggleButton: return std::make_shared<ToggleButton>();
    case ComponentKind::CheckBox: return std::make_shared<CheckBox>();
    case ComponentKind::RadioButton: return std::make_shared<RadioButton>();
    case ComponentKind::TextEdit: return std::make_shared<TextEdit>();
    case ComponentKind::NumberEdit: return std::make_shared<NumberEdit>();
    case ComponentKind::ComboBox: return std::make_shared<ComboBox>();
    case ComponentKind::ListView: return std::make_shared<ListView>();
    case ComponentKind::TableView: return std::make_shared<TableView>();
    case ComponentKind::TreeView: return std::make_shared<TreeView>();
    case ComponentKind::TabView: return std::make_shared<TabView>();
    case ComponentKind::ToolBar: return std::make_shared<ToolBar>();
    case ComponentKind::CoolBar: return std::make_shared<CoolBar>();
    case ComponentKind::MenuBar: return std::make_shared<Menu>(false);
    case ComponentKind::ContextMenu: return std::make_shared<Menu>(true);
    case ComponentKind::StatusBar: return std::make_shared<StatusBar>();
    case ComponentKind::Canvas: return std::make_shared<Canvas>();
    case ComponentKind::ImageView: return std::make_shared<ImageView>();
    case ComponentKind::ProgressBar: return std::make_shared<ProgressBar>();
    case ComponentKind::Slider: return std::make_shared<Slider>();
    case ComponentKind::Separator: return std::make_shared<Separator>();
    case ComponentKind::SplitView: return std::make_shared<SplitView>();
    case ComponentKind::ScrollView: return std::make_shared<ScrollView>();
    case ComponentKind::ExpandBar: return std::make_shared<ExpandBar>();
    case ComponentKind::ToolTip: return std::make_shared<ToolTip>();
    case ComponentKind::ColorWell: return std::make_shared<ColorWell>();
    case ComponentKind::Spacer: return std::make_shared<Spacer>();
    }
    return {};
}

} // namespace cwxeditor::lib
