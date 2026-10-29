#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "dockingfolder.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_stack.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/portable_runtime.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder {
namespace {

[[nodiscard]] bool startsWith(StringView value, StringView prefixValue) {
    return value.size() >= prefixValue.size() &&
           value.compare(0, prefixValue.size(), prefixValue) == 0;
}

[[nodiscard]] bool pointIn(const lib::Rect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
           y < rect.y + rect.height;
}

[[nodiscard]] int readInt(const XNode& node, StringView name, int fallback) {
    try {
        return node.attr<int>(name, false, fallback);
    } catch (const std::exception&) {
        return fallback;
    }
}

[[nodiscard]] bool excluded(StringView key,
                            const std::vector<String>& exclusions) {
    return std::any_of(exclusions.begin(), exclusions.end(),
                       [&](const String& value) { return startsWith(key, value); });
}

std::unordered_map<const lib::TabView*, DockingFolder*> tabOwners;

[[nodiscard]] Pane asPane(const Composite& value) {
    return std::dynamic_pointer_cast<lib::Panel>(value);
}

[[nodiscard]] lib::Rect restoreSubWindowBounds(
    const int x, const int y, const int width, const int height,
    const lib::Rect& localRootBounds, const lib::Rect& hostBounds,
    const bool useHostOrigin, lib::Rect displayClientArea) {
    // PaneMemoryは単体版ではホスト相対、ALLでは
    // ルート相対の座標を保存する。その原点は一度だけ加える。
    const float originX = useHostOrigin ? hostBounds.x : localRootBounds.x;
    const float originY = useHostOrigin ? hostBounds.y : localRootBounds.y;
    float restoredX = originX + static_cast<float>(x);
    float restoredY = originY + static_cast<float>(y);
    if (!useHostOrigin || displayClientArea.width <= 0.0F ||
        displayClientArea.height <= 0.0F) {
        displayClientArea = useHostOrigin
            ? lib::Rect{hostBounds.x + localRootBounds.x,
                        hostBounds.y + localRootBounds.y,
                        localRootBounds.width, localRootBounds.height}
            : localRootBounds;
    }
    if (displayClientArea.width > 0.0F &&
        displayClientArea.height > 0.0F) {
        // DのintoDisplayに合わせ、サイズを維持してウィンドウを操作可能な範囲に置く。
        if (displayClientArea.x + displayClientArea.width < restoredX + width)
            restoredX = displayClientArea.x + displayClientArea.width - width;
        if (displayClientArea.y + displayClientArea.height < restoredY + height)
            restoredY = displayClientArea.y + displayClientArea.height - height;
        restoredX = std::max(restoredX, displayClientArea.x);
        restoredY = std::max(restoredY, displayClientArea.y);
    }
    return {restoredX, restoredY, static_cast<float>(width),
            static_cast<float>(height)};
}

// ネイティブの選択通知に、移動途中のページを見せない。
// Dは二つの所有関係の変更をまたいで_onNewTab
// を保留し、完了した移動先の選択を通常どおり公開する。
class ScopedTabMove final {
public:
    ScopedTabMove(bool& value, std::weak_ptr<void> life) noexcept
        : value_(value), saved_(value), life_(std::move(life)) {
        value_ = true;
    }
    void restore() noexcept { if (!life_.expired()) value_ = saved_; }
    ~ScopedTabMove() { restore(); }

private:
    bool& value_;
    bool saved_{};
    std::weak_ptr<void> life_;
};

// DのnewSashは、一つの子を置換した後に、その親の分割構造を復元する。
class ScopedParentSplitWeights final {
public:
    explicit ScopedParentSplitWeights(const SplitPane& split)
        : split_(split), weights_(split ? split->weights : std::vector<float>{}) {}
    ~ScopedParentSplitWeights() noexcept {
        if (split_) split_->weights = std::move(weights_);
    }

private:
    SplitPane split_{};
    std::vector<float> weights_{};
};


// D形式のドック復元に失敗した場合、親に接続したフォルダー全体を破棄する。
class ScopedRestoreAttachment final {
public:
    ScopedRestoreAttachment(Composite parent, Control component)
        : parent_(std::move(parent)), component_(std::move(component)) {}
    ~ScopedRestoreAttachment() noexcept {
        if (parent_ && component_) parent_->remove(component_);
    }
    void release() noexcept {
        parent_.reset();
        component_.reset();
    }

private:
    Composite parent_;
    Control component_;
};
}  // namespace

String dirToString(const Dir dir) {
    switch (dir) {
    case Dir::N: return "north";
    case Dir::E: return "east";
    case Dir::S: return "south";
    case Dir::W: return "west";
    }
    return "north";
}

Dir stringToDir(const StringView value) {
    String lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    if (lower == "east") return Dir::E;
    if (lower == "south") return Dir::S;
    if (lower == "west") return Dir::W;
    return Dir::N;
}

DockingFolder::DockingFolder(const Composite parent, const int style,
                             const bool createTabf, String firstPaneKey)
    : _style(style) {
    auto component = std::make_shared<lib::Panel>();
    // ドッキング面はペイン領域にドラッグCanvasを重ねる。埋
    // める配置がないと汎用バックエンドが領域を幅160pxの列項
    // 目として扱い、分割ツリーがクライアント領域を使わず重なる。
    component->set_layout(std::make_shared<lib::FillLayout>());
    _comp = component;
    if (parent) {
        parent->add(component);
        registerParent(parent, component);
    }

    _area = createArea(_comp);

    _canvas = createCanvas(_comp);
    _canvas->set_visible(false);
    if (createTabf) {
        if (firstPaneKey.empty()) firstPaneKey = newTabfKey("t");
        (void)newTabf(_area, std::move(firstPaneKey));
    }
}

DockingFolder::DockingFolder(const Composite parent, const int style,
                             String firstPaneKey)
    : DockingFolder(parent, style, true, std::move(firstPaneKey)) {}

DockingFolder::~DockingFolder() {
    _lifetime.reset();
    _canSave = false;
    resetDockDrag();
    // ドロップCanvasは親コンテナーが所有し、このDockingFolder
    // より長く生存し得る。フォルダー破棄前にthis
    // をキャプチャするコールバックを無効にする。
    const auto retireCanvas = [](const Canvas& canvas) {
        if (!canvas) return;
        canvas->on_pointer = {};
        canvas->on_paint = {};
        canvas->on_paint_context = {};
        canvas->set_visible(false);
    };
    retireCanvas(_canvas);
    for (const auto& state : _subWindows) retireCanvas(state.canvas);
    for (const auto& [keyValue, state] : _panes) {
        if (!state.tabs) continue;
        state.tabs->on_selection_changed = {};
        state.tabs->on_selected_page_activated = {};
        state.tabs->on_close_page = {};
        tabOwners.erase(state.tabs.get());
    }
    for (const auto& state : _subWindows)
        if (state.shell) state.shell->on_close_requested = {};
}

Canvas DockingFolder::createCanvas(const Composite& parent) {
    auto canvas = std::make_shared<lib::Canvas>();
    canvas->double_buffered = true;
    canvas->border = false;
    canvas->set_bounds(parent ? parent->bounds() : lib::Rect{});
    if (parent) {
        parent->add(canvas);
        registerParent(parent, canvas);
    }
    // 半透明のドックの印は最前面の子に置くため、表示後の
    // 移動・解放を、ドラッグ開始元のTabViewの代わ
    // りに受け取る。その入力を同じドッキングセッションへ
    // 転送する。そうしないとMouseUpで終了できず、
    // 黒い印がEditorを覆って後続入力を奪い続ける。
    canvas->on_pointer = [this, weak = std::weak_ptr<lib::Canvas>(canvas)](
                             lib::PointerEvent& event) {
        if (!_dragItm) return;
        auto point = event.screen_position;
        if ((lib::editor_window_host_options().all_mode ||
             !event.screen_position_available) &&
            point.x == 0.0F && point.y == 0.0F &&
            (event.position.x != 0.0F || event.position.y != 0.0F)) {
            if (const auto current = weak.lock()) {
                const auto bounds = boundsOnDisplay(current);
                point = {bounds.x + event.position.x,
                         bounds.y + event.position.y};
            }
        }
        if (event.action == lib::PointerAction::Move) {
            updateDockDrag(point);
            event.handled = true;
        } else if (event.action == lib::PointerAction::Up &&
                   event.button == lib::PointerButton::Primary) {
            finishDockDrag(point, true);
            event.handled = true;
        } else if (event.action == lib::PointerAction::Cancel) {
            finishDockDrag(point, false);
            event.handled = true;
        }
    };
    return canvas;
}

Canvas DockingFolder::dropCanvas(const TabF& tabf) const {
    if (!tabf) return {};
    const auto pane = std::find_if(
        _panes.begin(), _panes.end(),
        [&](const auto& entry) { return entry.second.tabs == tabf; });
    if (pane == _panes.end() || !pane->second.shell) return _canvas;
    const auto sub = std::find_if(
        _subWindows.begin(), _subWindows.end(),
        [&](const SubWindowState& state) {
            return state.shell == pane->second.shell;
        });
    return sub == _subWindows.end() ? Canvas{} : sub->canvas;
}

void DockingFolder::hideDropCanvases() {
    const auto hide = [](const Canvas& canvas) {
        if (!canvas) return;
        canvas->set_visible(false);
        if (canvas->invalidate) canvas->invalidate();
    };
    hide(_canvas);
    for (const auto& sub : _subWindows) hide(sub.canvas);
}

void DockingFolder::resetDockDrag() {
    _dragItm = nullptr;
    _drawTabf.reset();
    _dragMoved = false;
    _drawPos = DPos::NONE;
    _dropPos = DPos::NONE;
    hideDropCanvases();
}

void DockingFolder::updateDockDrag(const lib::Point screenPoint) {
    if (!_dragItm || !_dragMoved) return;
    DropEvent drag{screenPoint.x, screenPoint.y, _dragItm->key, false};
    DTL{this, _canvas}.dragOver(drag);
}

void DockingFolder::finishDockDrag(const lib::Point screenPoint,
                                   const bool commit) {
    const std::weak_ptr<void> life = _lifetime;
    if (commit && _dragItm && _dragMoved) {
        // ドロップ完了中のコールバックがタブのベクターを
        // 再構築しても、転送元のキーを独立して保持する。
        const String keyValue = _dragItm->key;
        DropEvent drop{screenPoint.x, screenPoint.y, keyValue, false};
        DTL handler{this, _canvas};
        handler.dragOver(drop);
        if (life.expired()) return;
        if (drop.accepted) handler.drop(drop);
        // Dの単体版では、既知のペインがcanMoveを拒否したら移動を取り消し
        // 、本当に外側へ落とした場合だけ切り離す。ALLの既存規則は維持する。
        else if (_tabs.contains(keyValue) &&
                 (lib::editor_window_host_options().all_mode || !_drawTabf))
            createSubWindow(keyValue);
    }
    if (!life.expired()) resetDockDrag();
}

void DockingFolder::updateDropIndicator(const TabF& tabf) {
    const Canvas active = dropCanvas(tabf);
    for (const auto& sub : _subWindows)
        if (sub.canvas && sub.canvas != active) sub.canvas->set_visible(false);
    if (_canvas && _canvas != active) _canvas->set_visible(false);
    if (!active || !tabf || _drawPos == DPos::NONE || !canDrop(tabf)) {
        if (active) active->set_visible(false);
        return;
    }
    const auto target = boundsOnCanvas(active, tabf);
    drawDropMark(active, target.x, target.y, target.width, target.height);
}

Pane DockingFolder::createArea(const Composite& parent) {
    auto area = std::make_shared<lib::Panel>();
    area->set_layout(std::make_shared<lib::FillLayout>());
    if (parent) {
        parent->add(area);
        registerParent(parent, area);
    }
    return area;
}

String DockingFolder::prefix(String key) {
    while (!key.empty()) {
        const char last = key.back();
        if (last == '_') {
            key.pop_back();
            break;
        }
        if (last < '0' || last > '9') break;
        key.pop_back();
    }
    return key;
}

String DockingFolder::newPaneKey(const String prefixValue) const {
    String keyValue = prefixValue;
    std::size_t index = 0;
    while (_panes.contains(keyValue) || _sashes.contains(keyValue)) {
        keyValue = prefixValue + "_" + std::to_string(++index);
    }
    return keyValue;
}

String DockingFolder::newCtrlKey(const String prefixValue) const {
    String keyValue = prefixValue;
    std::size_t index = 0;
    while (_controls.contains(keyValue)) {
        keyValue = prefixValue + "_" + std::to_string(++index);
    }
    return keyValue;
}

void DockingFolder::updateCloseButtons() {
    for (const String& keyValue : _controlOrder) {
        auto found = _tabs.find(keyValue);
        if (found == _tabs.end()) continue;
        const bool closable = !hasCloseButton || hasCloseButton(keyValue);
        const auto paneIt = _panes.find(found->second->paneKey);
        if (paneIt != _panes.end()) {
            const std::size_t index = pageIndex(paneIt->second, keyValue);
            if (index < paneIt->second.tabs->pages.size()) {
                auto pages = paneIt->second.tabs->pages;
                pages[index].closable = closable;
                if (!lib::replace_tab_pages(paneIt->second.tabs, std::move(pages),
                                            paneIt->second.tabs->selected_index))
                    continue;
            }
        }
        found->second->closable = closable;
    }
}

String DockingFolder::newTabfKey(const String prefixValue) const {
    std::size_t index = _paneOrder.size();
    String keyValue;
    do {
        keyValue = prefixValue + "_" + std::to_string(index++);
    } while (_panes.contains(keyValue) || _sashes.contains(keyValue));
    return keyValue;
}

std::vector<Pane> DockingFolder::panes() const {
    std::vector<Pane> result;
    result.reserve(_paneOrder.size());
    for (const String& keyValue : _paneOrder) {
        const auto found = _panes.find(keyValue);
        if (found != _panes.end()) result.push_back(found->second.pane);
    }
    return result;
}

std::vector<String> DockingFolder::paneKeys() const {
    return _paneOrder;
}

Pane DockingFolder::pane(const StringView keyValue,
                         const Shell priorityShell) const {
    // Dは要求されたShell内で、キーの系統全体を優先し、その後に完全一致のキーを探す。
    const auto exact = _panes.find(String(keyValue));
    const auto belongsToPriorityShell = [&](const PaneState& state) {
        // メインShellのペインには明示的なShell項目がない。ウィジェット
        // ツリーから、DがgetShell()で得るのと同じ所有関係を解決する。
        return state.shell ? state.shell == priorityShell
                           : lib::window_contains_widget(priorityShell, state.pane.get());
    };
    if (exact != _panes.end()) {
        if (!priorityShell || belongsToPriorityShell(exact->second))
            return exact->second.pane;
    }
    if (priorityShell) {
        const String requestedFamily = prefix(String(keyValue));
        for (const String& candidate : _paneOrder) {
            const auto found = _panes.find(candidate);
            if (found == _panes.end() || prefix(candidate) != requestedFamily)
                continue;
            if (belongsToPriorityShell(found->second))
                return found->second.pane;
        }
    }
    return exact == _panes.end() ? Pane{} : exact->second.pane;
}

String DockingFolder::key(const Composite& paneValue) const {
    if (!paneValue) return {};
    for (const auto& [keyValue, state] : _panes) {
        if (state.pane == paneValue) return keyValue;
    }
    return {};
}

std::size_t DockingFolder::pageIndex(const PaneState& state,
                                     const StringView keyValue) const {
    const auto found = std::find(state.tabKeys.begin(), state.tabKeys.end(),
                                 keyValue);
    return found == state.tabKeys.end()
               ? state.tabKeys.size()
               : static_cast<std::size_t>(found - state.tabKeys.begin());
}

String DockingFolder::selectedCtrl(const StringView paneKey) const {
    const Pane paneValue = pane(paneKey);
    const auto found = _panes.find(key(paneValue));
    if (found == _panes.end() || !found->second.tabs ||
        !found->second.tabs->selected_index)
        return {};
    const std::size_t index = *found->second.tabs->selected_index;
    if (index >= found->second.tabKeys.size()) return {};
    return found->second.tabKeys[index];
}

String DockingFolder::control(const StringView paneKey, const float x,
                              const float y) const {
    const auto found = _panes.find(key(pane(paneKey)));
    if (found == _panes.end() || !found->second.tabs) return {};
    const auto& view = found->second.tabs;
    if (!pointIn(view->bounds(), x, y)) return {};
    if (view->pages.empty()) return {};
    const float width = view->bounds().width;
    const float localX = x - view->bounds().x;
    std::size_t index = 0;
    if (width > 0.0F)
        index = std::min(view->pages.size() - 1U,
                         static_cast<std::size_t>(localX / width *
                                                  view->pages.size()));
    return index < found->second.tabKeys.size() ? found->second.tabKeys[index]
                                                 : String{};
}

Control DockingFolder::control(const StringView keyValue) const {
    const auto found = _controls.find(String(keyValue));
    return found == _controls.end() ? Control{} : found->second.control;
}

String DockingFolder::keyFromCtrl(const Control& controlValue) const {
    if (!controlValue) return {};
    const auto found = _controlKeys.find(controlValue.get());
    return found == _controlKeys.end() ? String{} : found->second;
}

std::vector<Control> DockingFolder::allControls() const {
    std::vector<Control> result;
    result.reserve(_controlOrder.size());
    for (const String& keyValue : _controlOrder) {
        if (const auto found = _controls.find(keyValue);
            found != _controls.end())
            result.push_back(found->second.control);
    }
    return result;
}

std::vector<String> DockingFolder::controlKeys() const {
    return _controlOrder;
}

std::vector<Control> DockingFolder::controls(const StringView paneKey) const {
    std::vector<Control> result;
    const auto found = _panes.find(String(paneKey));
    if (found == _panes.end()) return result;
    for (const String& keyValue : found->second.tabKeys) {
        if (const auto controlValue = control(keyValue)) result.push_back(controlValue);
    }
    return result;
}

std::vector<String> DockingFolder::controlKeys(const StringView paneKey) const {
    const auto found = _panes.find(String(paneKey));
    return found == _panes.end() ? std::vector<String>{} : found->second.tabKeys;
}

int DockingFolder::controlCount(const StringView paneKey) const {
    const auto found = _panes.find(String(paneKey));
    return found == _panes.end()
               ? 0
               : static_cast<int>(found->second.tabKeys.size());
}

std::vector<Control> DockingFolder::showingControls() const {
    std::vector<Control> result;
    for (const String& paneKey : _paneOrder) {
        const String selectedKey = selectedCtrl(paneKey);
        if (const auto selectedControl = control(selectedKey))
            result.push_back(selectedControl);
    }
    return result;
}

DockingFolder::Tab* DockingFolder::tab(const StringView keyValue) {
    const auto found = _tabs.find(String(keyValue));
    return found == _tabs.end() ? nullptr : found->second.get();
}

const DockingFolder::Tab* DockingFolder::tab(const StringView keyValue) const {
    const auto found = _tabs.find(String(keyValue));
    return found == _tabs.end() ? nullptr : found->second.get();
}

TabF DockingFolder::tabView(const StringView keyValue) const {
    const auto tabIt = _tabs.find(String(keyValue));
    if (tabIt == _tabs.end() || !tabIt->second) return {};
    const auto paneIt = _panes.find(tabIt->second->paneKey);
    return paneIt == _panes.end() ? TabF{} : paneIt->second.tabs;
}

bool DockingFolder::setTabText(const StringView keyValue, String text) {
    Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto paneIt = _panes.find(tabValue->paneKey);
    if (paneIt == _panes.end()) return false;
    const std::size_t index = pageIndex(paneIt->second, keyValue);
    if (index >= paneIt->second.tabs->pages.size()) return false;
    auto pages = paneIt->second.tabs->pages;
    pages[index].title = text;
    if (!lib::replace_tab_pages(paneIt->second.tabs, std::move(pages),
                                paneIt->second.tabs->selected_index))
        return false;
    tabValue->text = std::move(text);
    if (tabValue->paneKey == paneKeyFromCtrlKey(keyValue) &&
        paneIt->second.shell) {
        (void)updateSubShellTitle(paneIt->second.shell);
    }
    return true;
}

String DockingFolder::getTabText(const StringView keyValue) const {
    const Tab* tabValue = tab(keyValue);
    return tabValue ? tabValue->text : String{};
}

bool DockingFolder::tabImage(const StringView keyValue, const Image image) {
    Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto paneIt = _panes.find(tabValue->paneKey);
    if (paneIt == _panes.end()) return false;
    const std::size_t index = pageIndex(paneIt->second, keyValue);
    if (index >= paneIt->second.tabs->pages.size()) return false;
    auto pages = paneIt->second.tabs->pages;
    pages[index].icon = image;
    if (!lib::replace_tab_pages(paneIt->second.tabs, std::move(pages),
                                paneIt->second.tabs->selected_index))
        return false;
    tabValue->image = image;
    if (paneIt->second.shell) (void)updateSubShellTitle(paneIt->second.shell);
    return true;
}

Image DockingFolder::tabText(const StringView keyValue) const {
    const Tab* tabValue = tab(keyValue);
    return tabValue ? tabValue->image : Image{};
}

Pane DockingFolder::first() const {
    return _paneOrder.empty() ? Pane{} : pane(_paneOrder.front());
}

String DockingFolder::firstKey() const {
    return _paneOrder.empty() ? String{} : _paneOrder.front();
}

std::vector<Shell> DockingFolder::subWindows() const {
    std::vector<Shell> result;
    result.reserve(_subWindows.size());
    for (const auto& state : _subWindows)
        if (state.shell) result.push_back(state.shell);
    return result;
}

bool DockingFolder::isVanishable(const StringView keyValue) const {
    const auto found = _panes.find(String(keyValue));
    if (found == _panes.end()) return false;
    if (found->second.pane == _area && _area->children().size() == 1)
        return false;
    if (canVanish) return canVanish(*this, keyValue);
    return true;
}

Composite DockingFolder::parentOf(const Control& child) const {
    if (!child) return {};
    const auto found = _parents.find(child.get());
    return found == _parents.end() ? Composite{} : found->second;
}

Shell DockingFolder::hostWindow() const {
    Composite current = _comp;
    while (current) {
        if (auto shell = std::dynamic_pointer_cast<lib::Window>(current))
            return shell;
        current = parentOf(current);
    }
    return {};
}

Pane DockingFolder::paneForControl(const Control& controlValue) const {
    const String keyValue = keyFromCtrl(controlValue);
    if (!keyValue.empty()) {
        const String paneKey = paneKeyFromCtrlKey(keyValue);
        if (const auto found = _panes.find(paneKey); found != _panes.end())
            return found->second.pane;
    }
    for (const auto& [paneKey, state] : _panes)
        if (std::find(state.pane->children().begin(), state.pane->children().end(),
                      controlValue) != state.pane->children().end())
            return state.pane;
    return {};
}

Composite DockingFolder::getArea(const Control& controlValue) const {
    const Pane paneValue = paneForControl(controlValue);
    if (!paneValue) return _area;
    const auto found = _panes.find(key(paneValue));
    if (found == _panes.end() || !found->second.shell) return _area;
    for (const auto& sub : _subWindows)
        if (sub.shell == found->second.shell) return sub.area;
    return _area;
}

void DockingFolder::registerParent(const Composite& parent,
                                   const Control& child) {
    if (parent && child) _parents[child.get()] = parent;
}

std::size_t DockingFolder::childIndex(const Composite& parent,
                                      const Control& child) const {
    if (!parent || !child) return 0;
    const auto& children = parent->children();
    const auto found = std::find_if(
        children.begin(), children.end(),
        [&](const Control& value) { return value == child; });
    return found == children.end()
               ? children.size()
               : static_cast<std::size_t>(found - children.begin());
}

void DockingFolder::replaceChild(const Composite& parent, const Control& oldChild,
                                 const Control& newChild) {
    if (!parent || !oldChild || !newChild) return;
    const std::size_t index = childIndex(parent, oldChild);
    parent->remove(oldChild);
    parent->insert(index, newChild);
    registerParent(parent, newChild);
}

Pane DockingFolder::createPane(const Composite& parent, String keyValue,
                               const bool notifyCreated) {
    if (keyValue.empty()) keyValue = newTabfKey("t");
    if (_panes.contains(keyValue) || _sashes.contains(keyValue))
        throw std::invalid_argument("docking pane key already exists: " + keyValue);

    auto paneValue = std::make_shared<lib::Panel>();
    auto tabs = std::make_shared<lib::TabView>();
    // ALLはドックのタブ見出しではなく、メインのコンパクトな選択部
    // を使う。後のMainWindowの更新だけでなくペイン公開前に
    // 適用し、初回配置でツール上に見出し領域を確保しないようにする。
    tabs->header_visible = !lib::editor_window_host_options().all_mode;
    // ドックページの上下をそろえる。共通のタブ配置は、
    // 見出し非表示でも左右最小2画素の余白を確保する。
    tabs->chrome_inset = 0.0F;
    tabOwners[tabs.get()] = this;
    tabs->set_layout_item(lib::LayoutItem{});
    paneValue->set_layout(std::make_shared<lib::FillLayout>());
    paneValue->add(tabs);
    // タブページはContainer::children()ではなくTabView
    // が保持する。論理的な親の連鎖を明示し、ページ内容でもエリア・ペイン
    // 検索、切り離したウィンドウの検出、分割の折りたたみを機能させる。
    registerParent(paneValue, tabs);
    _panes.emplace(keyValue,
                   PaneState{keyValue, paneValue, tabs, {}, {}, {}});
    _paneOrder.push_back(keyValue);
    registerParent(parent, paneValue);
    if (parent) parent->add(paneValue);
    // DのnewSash/newTabfは、全分割ペインに同じ入力規則を設定する
    // 。ここで接続し、通常・復元・新規分割のタブで一つのアダプターを共有する。
    bindTabInputs(tabs);
    if (notifyCreated && !notifyPaneCreated(paneValue)) return {};
    return paneValue;
}

bool DockingFolder::notifyPaneCreated(const Pane& paneValue) {
    if (!paneValue) return false;
    const String paneKey = key(paneValue);
    const std::weak_ptr<void> life = _lifetime;
    const auto handlers = createPaneEvent;
    for (const auto& handler : handlers) {
        if (handler) handler(paneValue, paneKey);
        if (life.expired()) return false;
    }
    return true;
}

TabF DockingFolder::newTabf(const Composite& parent, String keyValue) {
    const std::weak_ptr<void> life = _lifetime;
    const Pane paneValue = createPane(parent, std::move(keyValue));
    if (life.expired() || !paneValue) return {};
    const auto found = _panes.find(key(paneValue));
    return found == _panes.end() ? TabF{} : found->second.tabs;
}

void DockingFolder::bindTabInputs(const TabF& tabs) {
    // 空の分割先も含め、タブを移す前に各ペインへ同じ選択
    // ・再アクティブ化・終了のコールバックを付ける。TabView
    // が所有し、逆参照は弱くして、切り離したページが
    // 操作部やシナリオを保持し続けないようにする。
    const std::weak_ptr<lib::TabView> weak_tabs = tabs;
    const std::weak_ptr<void> weak_lifetime = _lifetime;
    tabs->on_selection_changed = [this, weak_tabs, weak_lifetime](
                                           std::optional<std::size_t> selectedIndex) {
        if (weak_lifetime.expired()) return;
        if (const auto tabs = weak_tabs.lock(); tabs && selectedIndex) selectTab(tabs);
    };
    tabs->on_selected_page_activated = [this, weak_tabs, weak_lifetime](
                                                  const std::size_t selectedIndex) {
        if (weak_lifetime.expired()) return;
        const auto tabs = weak_tabs.lock();
        if (!tabs || tabs->selected_index != selectedIndex) return;
        const auto pane = std::find_if(_panes.begin(), _panes.end(),
            [&](const auto& entry) { return entry.second.tabs == tabs; });
        if (pane == _panes.end() || selectedIndex >= pane->second.tabKeys.size()) return;
        const auto tab = _tabs.find(pane->second.tabKeys[selectedIndex]);
        if (tab == _tabs.end() || tab->second->paneKey != pane->first) return;
        // 通常の選択経路を再利用する。有効なページを再検証し、選択
        // リスナーへ通知せずに選択ホストへフォーカスを要求する。
        selectTab(tabs, true);
    };
    tabs->on_close_page = [this, weak_tabs, weak_lifetime](
                                    const std::size_t index) {
        if (weak_lifetime.expired()) return;
        const auto tabs = weak_tabs.lock();
        if (!tabs) return;
        for (const auto& [paneKey, paneState] : _panes) {
            if (paneState.tabs != tabs || index >= paneState.tabKeys.size())
                continue;
            close(paneState.tabKeys[index]);
            break;
        }
    };

    // ドラッグと補助ボタンの動作はコントロールのコールバックに置く。主ボ
    // タンの選択・フォーカスは、共通TabViewの実行経路で解決する。
    const std::weak_ptr<void> life = _lifetime;
    tabs->on_pointer = [this, life, weak_tabs](lib::PointerEvent& event) {
        if (life.expired()) return;
        const auto tabs = weak_tabs.lock();
        if (!tabs) return;
        if (event.action == lib::PointerAction::Cancel) {
            resetDockDrag();
            event.handled = true;
            return;
        }
        const auto paneIt = std::find_if(
            _panes.begin(), _panes.end(),
            [&](const auto& entry) { return entry.second.tabs == tabs; });
        if (paneIt == _panes.end() || paneIt->second.tabKeys.empty()) {
            if (event.action == lib::PointerAction::Up) resetDockDrag();
            return;
        }
        const std::size_t count = paneIt->second.tabKeys.size();
        std::size_t index = tabs->selected_index.value_or(0U);
        const float width = tabs->bounds().width;
        const auto displayPoint = [&] {
            auto point = event.screen_position;
            // 画面座標のない合成イベントは、ローカル座標から表示座標への代替
            // 変換を維持する。ネイティブの(0,0)は実際の座標として扱う。
            if ((lib::editor_window_host_options().all_mode ||
                 !event.screen_position_available) &&
                point.x == 0.0F && point.y == 0.0F &&
                (event.position.x != 0.0F || event.position.y != 0.0F)) {
                const auto bounds = boundsOnDisplay(tabs);
                point = {bounds.x + event.position.x,
                         bounds.y + event.position.y};
            }
            return point;
        };
        // ネイティブの主ボタン選択は、正確なタブイン
        // デックスを提供済み。比率による代替は中央ク
        // リック用だけにし、主クリックに使わない。
        // 幅広ペインの短いタブが選べなくなるため。
        if (event.item_hit_test_available && event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < count) {
            index=static_cast<std::size_t>(event.item_index);
        } else if ((event.button == lib::PointerButton::Middle ||
                    event.button == lib::PointerButton::Secondary) && width > 0.0F) {
            const float x = std::clamp(event.position.x, 0.0F, width);
            index = std::min(count - 1U,
                            static_cast<std::size_t>(x / width * count));
        }
        if (event.action == lib::PointerAction::Down &&
            event.button == lib::PointerButton::Primary) {
            // ドッキングのドラッグリスナーは汎用TabViewの実行処理よ
            // り先に動く。閉じる記号をタブ選択・ドラッグ開始として消費し
            // ない。DのCTabFolderはここでそのページを閉じる。
            if (const auto close =
                    lib::hit_test_tab_close_page(*tabs, event.position)) {
                (void)lib::request_close_tab_page(*tabs, *close);
                if (!life.expired()) resetDockDrag();
                event.handled = true;
                return;
            }
            // DはCTabFolder::getItem(point)が実
            // 際のタブ見出しを返す場合だけドックのドラッグを始める。selected_index
            // へ代替すると、ページ本体の押下で選択中の場面・イ
            // ベントタブがドラッグされ、配置を崩してしまう。
            std::optional<std::size_t> hit;
            if (event.item_hit_test_available && event.item_index >= 0 &&
                static_cast<std::size_t>(event.item_index) < count)
                hit = static_cast<std::size_t>(event.item_index);
            else
                hit = lib::hit_test_tab_page(*tabs, event.position);
            if (!hit || *hit >= count) return;
            // 以前の中断したドラッグでMouseUpが来なくても、そ
            // の重ね表示や転送元の同一性を新しい操作へ持ち越さない。
            const String source_key = paneIt->second.tabKeys[*hit];
            const String source_pane = paneIt->first;
            resetDockDrag();
            if (life.expired()) return;
            _dragItm = tab(source_key);
            if (!_dragItm || _dragItm->paneKey != source_pane) {
                resetDockDrag();
                return;
            }
            _drawTabf = tabs;
            _dragMoved = false;
            _dragStartX = event.position.x;
            _dragStartY = event.position.y;
            // 主ボタン押下を未消費のままにし、activate_controlが共通TabView
            // の見出し選択とフォーカスのコールバックを実行できるようにする。
            return;
        }
        if (event.action == lib::PointerAction::Move && _dragItm) {
            const float dx = event.position.x - _dragStartX;
            const float dy = event.position.y - _dragStartY;
            if (!_dragMoved && dx * dx + dy * dy < 16.0F) return;
            _dragMoved = true;
            updateDockDrag(displayPoint());
            event.handled = true;
            return;
        }
        if (event.action == lib::PointerAction::Up &&
            event.button == lib::PointerButton::Primary) {
            const bool wasDragging = _dragItm && _dragMoved;
            finishDockDrag(displayPoint(), true);
            if (wasDragging) event.handled = true;
            return;
        }
        if (event.action == lib::PointerAction::Up &&
            event.button == lib::PointerButton::Secondary) {
            if (!event.item_hit_test_available) {
                if (const auto hit=lib::hit_test_tab_page(*tabs,event.position)) index=*hit;
                else return;
            }
            if (index>=count) return;
            const String selected_key = paneIt->second.tabKeys[index];
            const String selected_pane = paneIt->first;
            (void)lib::select_tab_page(*tabs,index,false);
            selectTab(tabs);
            if (life.expired()) return;
            const auto current = _panes.find(selected_pane);
            const auto selected = tab(selected_key);
            if (current == _panes.end() || current->second.tabs != tabs ||
                !selected || selected->paneKey != selected_pane) return;
            const auto menu = current->second.menu;
            const auto show_menu = showContextMenu;
            if (menu && show_menu) {
                show_menu(menu,tabs,event.screen_position);
                event.handled=true;
            }
            return;
        }
        if (event.action != lib::PointerAction::Up ||
            event.button != lib::PointerButton::Middle)
            return;
        const String tabKey = paneIt->second.tabKeys[index];
        const auto accepts = closeTabWithMiddleClick;
        if (accepts && !accepts(tabKey)) return;
        if (life.expired()) return;
        if (close(tabKey)) event.handled = true;
    };
}

Pane DockingFolder::addPane(const StringView base, const Dir dir,
                            const int lWeight, const int rWeight,
                            String paneKey) {
    return addPane(findPane2(base), dir, lWeight, rWeight, std::move(paneKey));
}

Pane DockingFolder::addPane(const Composite& base, const Dir dir,
                            const int lWeight, const int rWeight,
                            String paneKey) {
    if (!base) return {};
    if (paneKey.empty()) paneKey = newTabfKey("t");
    if (_panes.contains(paneKey) || _sashes.contains(paneKey))
        throw std::invalid_argument("docking pane key already exists: " + paneKey);

    // DのaddPane/newSashは、葉ペインだけでなく分割部も包める。
    // 渡されたCompositeを維持し、分割部の分割では部分木全体を包む。
    Composite parent = parentOf(base);
    if (!parent) parent = _area;
    if (!parent) return {};
    const std::size_t targetIndex = childIndex(parent, base);
    if (targetIndex >= parent->children().size()) return {};

    Shell sourceShell;
    for (const auto& entry : _panes) {
        const auto& state = entry.second;
        Composite current = state.pane;
        while (current && current != base) current = parentOf(current);
        if (current == base) {
            sourceShell = state.shell;
            break;
        }
    }
    ScopedParentSplitWeights restoreParentWeights(
        std::dynamic_pointer_cast<lib::SplitView>(parent));

    auto split = std::make_shared<lib::SplitView>();
    split->orientation =
        dir == Dir::N || dir == Dir::S ? lib::Orientation::Vertical
                                      : lib::Orientation::Horizontal;
    split->weights = {static_cast<float>(std::max(1, lWeight)),
                      static_cast<float>(std::max(1, rWeight))};
    const String sashKey = newSashKey();
    putSashTable(split, sashKey);
    parent->remove(base);
    parent->insert(targetIndex, split);
    registerParent(parent, split);

    const Pane created = createPane(split, paneKey, false);
    split->remove(created);
    if (dir == Dir::N || dir == Dir::W) {
        split->insert(0, created);
        split->insert(1, base);
    } else {
        split->insert(0, base);
        split->insert(1, created);
    }
    registerParent(split, base);
    registerParent(split, created);
    if (!lib::editor_window_host_options().all_mode) {
        // Dはウィジェットの親関係からShellを継承する。切り離した
        // ウィンドウの分割で作るペインにも、同じ所有関係を登録する。
        const auto added = _panes.find(paneKey);
        if (added != _panes.end() && sourceShell) {
            added->second.shell = sourceShell;
            for (auto& sub : _subWindows) {
                if (sub.shell != sourceShell) continue;
                if (std::find(sub.paneKeys.begin(), sub.paneKeys.end(), paneKey) == sub.paneKeys.end())
                    sub.paneKeys.push_back(paneKey);
                break;
            }
        }
    }
    setFirstResize(split);
    // DはnewTabfから通知する。C++のMainWindowハン
    // ドラーはコントロールの組み込みも行いaddPaneへ再入し得る
    // ため、この分割の子と親リンクが完成ツリーを表してから公開する。
    if (!notifyPaneCreated(created)) return {};
    return created;
}

bool DockingFolder::setPaneWidthToMainRatio(
    const StringView paneKey, const float paneToMainRatio) {
    if (!std::isfinite(paneToMainRatio) || paneToMainRatio <= 0.0F) return false;
    const auto target = pane(paneKey);
    if (!target) return false;
    const auto parent = parentOf(target);
    const auto split = std::dynamic_pointer_cast<lib::SplitView>(parent);
    if (!split || split->orientation != lib::Orientation::Horizontal ||
        split->children().size() != 2U)
        return false;

    const auto& children = split->children();
    const auto found = std::find(children.begin(), children.end(), target);
    if (found == children.end()) return false;
    const float mainWeight = 1.0F / paneToMainRatio;
    if (!std::isfinite(mainWeight)) return false;
    const float paneWeight = 1.0F;
    const auto index = static_cast<std::size_t>(found - children.begin());
    split->weights = index == 0U
        ? std::vector<float>{paneWeight, mainWeight}
        : std::vector<float>{mainWeight, paneWeight};
    return true;
}

Pane DockingFolder::addPaneAtRoot(const Dir dir, const int lWeight,
                                  const int rWeight, String paneKey) {
    if (!_area) return {};
    if (paneKey.empty()) paneKey = newTabfKey("t");
    if (_panes.contains(paneKey) || _sashes.contains(paneKey))
        throw std::invalid_argument("docking pane key already exists: " + paneKey);

    const auto roots = _area->children();
    if (roots.empty()) return createPane(_area, std::move(paneKey));

    // DockingFolderの領域は一つの論理ルートを持つ。不正な複数
    // ルートのツリーは、呼び出し側所有の部品を黙って失わず、そのまま残す。
    if (roots.size() != 1U || !roots.front()) return {};
    const Control target = roots.front();
    auto split = std::make_shared<lib::SplitView>();
    split->orientation = dir == Dir::N || dir == Dir::S
                             ? lib::Orientation::Vertical
                             : lib::Orientation::Horizontal;
    split->weights = {static_cast<float>(std::max(1, lWeight)),
                      static_cast<float>(std::max(1, rWeight))};
    putSashTable(split, newSashKey());

    _area->remove(target);
    _area->add(split);
    registerParent(_area, split);
    const Pane created = createPane(split, std::move(paneKey), false);
    split->remove(created);
    if (dir == Dir::N || dir == Dir::W) {
        split->insert(0, created);
        split->insert(1, target);
    } else {
        split->insert(0, target);
        split->insert(1, created);
    }
    registerParent(split, target);
    registerParent(split, created);
    setFirstResize(split);
    // ルート分割のコールバックをaddPaneと一致させる。ドッキン
    // グへ再入しても、完成した分割内に元のルートが見える必要がある。
    if (!notifyPaneCreated(created)) return {};
    return created;
}

Composite DockingFolder::addPaneFromMemory(const StringView base1,
                                      const StringView base2, Dir dir,
                                      int lWeight, int rWeight, String paneKey,
                                      const bool useDefaultWeights) {
    const std::weak_ptr<void> weak_lifetime = _lifetime;
    if (!paneKey.empty())
        if (const Composite existing = findPane2(paneKey)) return existing;
    const String memoryKey = prefix(paneKey);
    String preferredBase(base1);
    bool restoringPaneMemory = false;
    if (const auto memory = _pMemories.find(memoryKey);
        memory != _pMemories.end()) {
        const PaneMemory saved = memory->second;
        _pMemories.erase(memory);
        if (saved.isSubWindow) {
            Shell shell;
            Pane paneValue;
            createSubWindow(saved.subPaneKey.empty() ? paneKey : saved.subPaneKey,
                            shell, paneValue);
            // TabFolderの作成はcreatePaneEventを呼ぶ。コールバックがこ
            // のDockingFolderを破棄し得るため、メンバーを読む前に停止判定する。
            if (weak_lifetime.expired() || !shell || !paneValue) return {};
            if (saved.width >= 0 && saved.height >= 0) {
                const lib::Rect localRootBounds = _comp ? _comp->bounds() : lib::Rect{};
                const Shell parentShell = hostWindow();
                const bool useHostOrigin = parentShell &&
                    !lib::editor_window_host_options().all_mode;
                const lib::Rect hostBounds = useHostOrigin
                    ? parentShell->bounds() : lib::Rect{};
                // 実行中にフォルダーを終了させてもプロバイダーを生
                // 存させ、ローカルShellに触る前に再検証する。
                const auto clientAreaProvider = displayClientArea;
                const lib::Rect clientArea = clientAreaProvider
                    ? clientAreaProvider() : lib::Rect{};
                if (weak_lifetime.expired()) return {};
                shell->set_bounds(restoreSubWindowBounds(
                    saved.x, saved.y, saved.width, saved.height,
                    localRootBounds, hostBounds, useHostOrigin, clientArea));
            }
            return paneValue;
        }
        dir = saved.dir;
        lWeight = saved.lWeight;
        rWeight = saved.rWeight;
        preferredBase = saved.pairPane;
        restoringPaneMemory = true;
    }
    // Dは記憶した隣接先、要求された有効な基準先、最初のペイン
    // の順に試す。記憶した隣接先自体が閉じている場合もある。
    Composite target = findPane2(preferredBase);
    if (!target) target = findPane2(base1);
    if (!target) target = findPane2(base2);
    if (!target) target = first();
    if (!target) return {};
    // SWTの既定のドッキング比率は、新規ペインを広めにする
    // （上1:3、左1:4、下3:1、右4:1）。呼び出し
    // 側が既定の1:1を渡した場合も、その動作を維持する。
    if (!restoringPaneMemory && useDefaultWeights && lWeight == 1 && rWeight == 1) {
        switch (dir) {
        case Dir::N: lWeight = 1; rWeight = 3; break;
        case Dir::W: lWeight = 1; rWeight = 4; break;
        case Dir::S: lWeight = 3; rWeight = 1; break;
        case Dir::E: lWeight = 4; rWeight = 1; break;
        }
    }
    const String createdKey = newPaneKey(paneKey);
    const Pane created = addPane(target, dir, lWeight, rWeight, createdKey);
    // ALL固有の配置として、既存の側面ペインの幅240pxを維持す
    // る。上記の保存済み配置を優先し、単体版の位置補正は加えない。
    if (created && lib::editor_window_host_options().all_mode &&
        !restoringPaneMemory && useDefaultWeights &&
        dir == Dir::E && prefix(paneKey) == "side") {
        if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(
                parentOf(created));
            split && split->orientation == lib::Orientation::Horizontal)
            split->initial_second_extent = 240.0F;
    }
    return created;
}

Composite DockingFolder::addPaneFromCtrlMemory(const StringView base, Dir dir,
                                          int lWeight, int rWeight,
                                          String paneKey, String ctrlKey) {
    if (!paneKey.empty())
        if (const Composite existing = findPane2(paneKey)) return existing;
    if (const auto existing = findCtrl(ctrlKey, true); !existing.empty())
        return pane(paneKeyFromCtrlKey(existing.front()));
    const String memoryKey = prefix(ctrlKey);
    if (const auto memory = _cMemories.find(memoryKey);
        memory != _cMemories.end()) {
        const CtrlMemory saved = memory->second;
        _cMemories.erase(memory);
        // DはaddPaneFromMemoryを通じてコントロールの記憶状態を復元し、
        // 対応するPaneMemoryも消費する。記録したコントロールの比率を優先する。
        return addPaneFromMemory(saved.pairPane, saved.pairPane, saved.dir,
                                 saved.lWeight, saved.rWeight, saved.pane, false);
    }
    return addPaneFromMemory(base, base, dir, lWeight, rWeight,
                             std::move(paneKey));
}

void DockingFolder::add(Control controlValue, String tabTextValue, String keyValue,
                        const bool selectValue, const NewCtrlLocation location) {
    addImpl(std::move(controlValue), std::move(tabTextValue), 0,
            std::move(keyValue), selectValue, location, false);
}

void DockingFolder::add(Control controlValue, String tabTextValue,
                        const Image tabImageValue, String keyValue,
                        const bool selectValue, const NewCtrlLocation location) {
    addImpl(std::move(controlValue), std::move(tabTextValue), tabImageValue,
            std::move(keyValue), selectValue, location, false);
}

void DockingFolder::addImpl(Control controlValue, String tabTextValue,
                            const Image tabImageValue, String keyValue,
                            const bool selectValue,
                            const NewCtrlLocation location, const bool loading) {
    if (!controlValue || keyValue.empty() || _controls.contains(keyValue))
        throw std::invalid_argument("invalid docking control key: " + keyValue);

    Pane target;
    if (const auto parentIt = _parents.find(controlValue.get());
        parentIt != _parents.end()) {
        Composite current = parentIt->second;
        while (current && !target) {
            target = asPane(current);
            if (!target) current = parentOf(current);
        }
    }
    for (const auto& [paneKey, state] : _panes) {
        if (target) break;
        if (std::find_if(state.pane->children().begin(), state.pane->children().end(),
                         [&](const Control& child) { return child == controlValue; }) !=
            state.pane->children().end()) {
            target = state.pane;
            (void)paneKey;
            break;
        }
    }
    if (!target) target = first();
    if (!target) target = createPane(_area, newTabfKey("t"));
    const String paneKey = key(target);
    auto paneIt = _panes.find(paneKey);
    if (paneIt == _panes.end()) throw std::logic_error("docking pane disappeared");

    auto tabValue = std::make_unique<TabRecord>();
    tabValue->key = keyValue;
    tabValue->text = std::move(tabTextValue);
    tabValue->image = tabImageValue;
    tabValue->control = controlValue;
    tabValue->paneKey = paneKey;
    tabValue->closable = !hasCloseButton || hasCloseButton(keyValue);
    TabRecord* tabPtr = tabValue.get();
    _tabs.emplace(keyValue, std::move(tabValue));
    _controls.emplace(keyValue, ControlState{keyValue, controlValue, paneKey});
    _controlKeys[controlValue.get()] = keyValue;
    _controlOrder.push_back(keyValue);

    auto& state = paneIt->second;
    std::size_t insertAt = state.tabKeys.size();
    if (location == NewCtrlLocation::Right && state.tabs->selected_index)
        insertAt = *state.tabs->selected_index + 1U;
    insertAt = std::min(insertAt, state.tabKeys.size());

    // コントロールはTabPageに属し、TabViewの隣のペイン直
    // 下には置かない。直接の子にもすると、ネイティブ側でタブページと兄
    // 弟として二重作成される。葉ウィジェットには軽量なページの入れ物を
    // 作り、SWTのCompositeと同様にGuiBackend::create_children
    // で全ページを巡回できるようにする。
    std::shared_ptr<lib::Container> pageContent =
        std::dynamic_pointer_cast<lib::Container>(controlValue);
    if (!pageContent) {
        pageContent = std::make_shared<lib::Panel>();
        pageContent->set_layout(std::make_shared<lib::FillLayout>());
        pageContent->add(controlValue);
    }
    auto pages = state.tabs->pages;
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(insertAt),
                 lib::TabPage{tabPtr->text, tabPtr->image, pageContent,
                              tabPtr->closable});
    const auto selected = !state.tabs->selected_index || selectValue
                              ? std::optional<std::size_t>{insertAt}
                              : state.tabs->selected_index;
    if (!lib::replace_tab_pages(state.tabs, std::move(pages), selected)) {
        _controlOrder.erase(
            std::remove(_controlOrder.begin(), _controlOrder.end(), keyValue),
            _controlOrder.end());
        _controlKeys.erase(controlValue.get());
        _controls.erase(keyValue);
        _tabs.erase(keyValue);
        throw std::logic_error("docking tab publication failed");
    }
    state.tabKeys.insert(state.tabKeys.begin() +
                             static_cast<std::ptrdiff_t>(insertAt),
                         keyValue);
    // 共通モデルのTabViewは葉Widgetなので、ページ内容の最寄
    // りの論理ContainerはTabViewを所有するペインとなる。
    registerParent(state.pane, pageContent);
    if (pageContent != controlValue) registerParent(pageContent, controlValue);

    const std::weak_ptr<void> weak_lifetime = _lifetime;
    if (selectValue) selectTab(state.tabs);
    if (weak_lifetime.expired()) return;
    if (!loading && state.shell) {
        state.shell->set_visible(true);
        (void)updateSubShellTitle(state.shell);
    }
}

void DockingFolder::addFromMemory(const CreateControl& createControl,
                                  String tabTextValue, const Image tabImageValue,
                                  String keyValue, String paneKey,
                                  String pairPane, Dir dir,
                                  const bool selectValue,
                                  const NewCtrlLocation location) {
    if (_controls.contains(keyValue)) throw std::invalid_argument("duplicate docking control");
    const String requestedPairPane = pairPane;
    const std::weak_ptr<void> weak_lifetime = _lifetime;
    String chosenPane = paneKey;
    int leftWeight = 1, rightWeight = 1;
    bool hasSavedWeights = false;
    if (const auto memory = _cMemories.find(keyValue);
        memory != _cMemories.end()) {
        const CtrlMemory saved = memory->second;
        _cMemories.erase(memory);
        chosenPane = saved.pane;
        pairPane = saved.pairPane;
        dir = saved.dir;
        hasSavedWeights = true;
        leftWeight = saved.lWeight;
        rightWeight = saved.rWeight;
    }
    Composite target = findPane2(chosenPane);
    if (!target) {
        target = addPaneFromMemory(pairPane, requestedPairPane, dir,
                                  leftWeight, rightWeight, chosenPane,
                                  !hasSavedWeights);
        // createPaneEventはこのフォルダーを終了・置換し得る。古
        // いホストで代替ペインを探したりファクトリーを呼んだりする前に戻る。
        if (weak_lifetime.expired()) return;
    }
    if (!target) target = first();
    if (weak_lifetime.expired()) return;
    if (!target) throw std::runtime_error("no docking pane for control");
    Control created = createControl ? createControl(target, keyValue) : Control{};
    if (weak_lifetime.expired()) return;
    if (!created) throw std::runtime_error("control factory returned no widget");
    _parents[created.get()] = target;
    addImpl(std::move(created), std::move(tabTextValue), tabImageValue,
            std::move(keyValue), selectValue, location, false);
}

void DockingFolder::setMenu(const StringView paneKey, Menu menuValue) {
    const String resolvedKey = key(pane(paneKey));
    if (auto found = _panes.find(resolvedKey); found != _panes.end())
        found->second.menu = std::move(menuValue);
}

Menu DockingFolder::getMenu(const StringView paneKey) const {
    const auto found = _panes.find(key(pane(paneKey)));
    return found == _panes.end() ? Menu{} : found->second.menu;
}

String DockingFolder::paneKeyFromCtrlKey(const StringView keyValue) const {
    const auto found = _controls.find(String(keyValue));
    return found == _controls.end() ? String{} : found->second.paneKey;
}

std::vector<String> DockingFolder::findPane(const StringView prefixValue,
                                            const bool includeSubShells) const {
    std::vector<String> result;
    for (const String& keyValue : _paneOrder) {
        const auto found = _panes.find(keyValue);
        if (found == _panes.end() || !startsWith(keyValue, prefixValue)) continue;
        if (!includeSubShells && found->second.shell) continue;
        result.push_back(keyValue);
    }
    return result;
}

Composite DockingFolder::findPane2(const StringView prefixValue) const {
    if (const auto sash = _sashes.find(String(prefixValue)); sash != _sashes.end())
        return sash->second;
    if (const auto direct = _panes.find(String(prefixValue)); direct != _panes.end())
        return direct->second.pane;
    const auto found = findPane(prefixValue, true);
    return found.empty() ? Composite{} : pane(found.front());
}

std::vector<String> DockingFolder::findCtrl(const StringView prefixValue,
                                            const bool includeSubShells) const {
    std::vector<String> result;
    for (const String& keyValue : _controlOrder) {
        const auto found = _controls.find(keyValue);
        if (found == _controls.end() || !startsWith(keyValue, prefixValue)) continue;
        if (!includeSubShells && isSubWindowCtrl(keyValue)) continue;
        result.push_back(keyValue);
    }
    return result;
}

std::vector<DockingFolder::Tab*> DockingFolder::getTabs(const Composite& areaValue) {
    std::vector<Tab*> result;
    for (const String& paneKey : _paneOrder) {
        const auto found = _panes.find(paneKey);
        if (found == _panes.end()) continue;
        // 主領域に、切り離したウィンドウへ移したペインを誤
        // って含めない。DはSWTのウィジェットツリーを
        // 巡回するため、これらは自然に主領域の外になる。
        if (areaValue == _area && found->second.shell) continue;
        bool inArea = found->second.pane == areaValue;
        Composite parent = parentOf(found->second.pane);
        while (!inArea && parent) {
            if (parent == areaValue) inArea = true;
            parent = parentOf(parent);
        }
        if (!inArea && areaValue != _area) continue;
        for (const String& keyValue : found->second.tabKeys)
            if (auto tabValue = tab(keyValue)) result.push_back(tabValue);
    }
    return result;
}

void DockingFolder::createSubWindow(Shell& shell, Composite& areaValue) {
    shell = std::make_shared<lib::Window>();
    shell->always_on_top = true;
    shell->set_layout(std::make_shared<lib::FillLayout>());
    areaValue = std::make_shared<lib::Panel>();
    areaValue->set_layout(std::make_shared<lib::FillLayout>());
    shell->add(areaValue);
    registerParent(shell, areaValue);
    const Canvas canvas = createCanvas(shell);
    canvas->set_visible(false);
    // 切り離したウィンドウは管理一覧だけでなく、部
    // 品ツリーにも保持する。GuiBackendは
    // ルートからの巡回で入れ子のWindowモデル
    // を検出するため、この接続がないとXMLでは正
    // しく表せてもネイティブHWNDが作られない。
    if (_comp) {
        _comp->add(shell);
        registerParent(_comp, shell);
    }
    _subWindows.push_back(SubWindowState{shell, areaValue, canvas, {}});
    const std::weak_ptr<lib::Window> weakShell = shell;
    const std::weak_ptr<void> weakLife = _lifetime;
    const auto closing = std::make_shared<bool>(false);
    shell->on_close_requested = [this, weakShell, weakLife, closing] {
        if (weakLife.expired() || *closing) return;
        *closing = true;
        struct ClosingScope {
            std::shared_ptr<bool> value;
            ~ClosingScope() { *value = false; }
        } scope{closing};
        const Shell shell = weakShell.lock();
        if (!shell) return;
        const auto found = std::find_if(
            _subWindows.begin(), _subWindows.end(),
            [&](const SubWindowState& state) { return state.shell == shell; });
        if (found == _subWindows.end()) return;
        const auto paneKeysValue = found->paneKeys;
        for (const String& paneKey : paneKeysValue) {
            const auto paneIt = _panes.find(paneKey);
            if (paneIt == _panes.end()) continue;
            // DのgetTabsは取得時の状態を返す。終了でこのベクター
            // やペインが変わり得るため、独立して所有するキーを保持する。
            const auto tabKeysValue = paneIt->second.tabKeys;
            for (const String& keyValue : tabKeysValue) {
                (void)close(keyValue);
                if (weakLife.expired()) return;
            }
        }
    };
}

void DockingFolder::createSubWindow(const StringView tabfKey, Shell& shell,
                                    Pane& paneValue) {
    const std::weak_ptr<void> weak_lifetime = _lifetime;
    Composite areaValue;
    createSubWindow(shell, areaValue);
    const TabF tabs = newTabf(areaValue, String(tabfKey));
    // newTabfはcreatePaneEventを通知し、そのハンドラーが所有元を破棄する場合がある。
    if (weak_lifetime.expired() || !tabs) return;
    paneValue = pane(tabfKey);
    if (paneValue) {
        auto found = _panes.find(String(tabfKey));
        if (found != _panes.end()) found->second.shell = shell;
        _subWindows.back().paneKeys.push_back(String(tabfKey));
        (void)tabs;
    }
}

void DockingFolder::createSubWindow(const StringView keyValue) {
    auto tabValue = tab(keyValue);
    const auto ctrlIt = _controls.find(String(keyValue));
    if (!tabValue || ctrlIt == _controls.end()) return;
    auto sourceIt = _panes.find(tabValue->paneKey);
    if (sourceIt == _panes.end() || isSingleWindow(keyValue)) return;
    const Shell oldShell = sourceIt->second.shell;
    for (const auto& handler : movingShellEvent)
        if (handler) handler(keyValue);

    const String newKey = newTabfKey(prefix(sourceIt->first));
    const String oldPaneKey = sourceIt->first;
    // タブを切り離す前の位置・寸法。メインウィンドウから外
    // へドラッグしたタブには旧サブウィンドウがなく、新しい
    // タブ列も未配置で0×0。これを保持しないと、新ウィン
    // ドウのサイズがなくなり、タブが消えたように見える。
    const lib::Rect sourceTabsBounds = sourceIt->second.tabs
        ? boundsOnDisplay(sourceIt->second.tabs) : lib::Rect{};
    Shell shell;
    Pane target;
    createSubWindow(newKey, shell, target);
    if (!target) return;
    newTab(_panes[newKey].tabs, tabValue, -1);
    if (const auto oldPane = _panes.find(oldPaneKey);
        oldPane != _panes.end() && oldPane->second.tabKeys.empty() &&
        isVanishable(oldPaneKey))
        removeTabf(oldPane->second.tabs, true);
    shell->title = tabValue->text;
    const lib::Rect sourceBounds = oldShell ? oldShell->bounds() : lib::Rect{};
    const lib::Rect tabBounds = _panes[newKey].tabs->bounds();
    if (tabBounds.width > 0.0F && tabBounds.height > 0.0F)
        shell->set_bounds(tabBounds);
    else if (sourceBounds.width > 0.0F && sourceBounds.height > 0.0F)
        shell->set_bounds(sourceBounds);
    else if (sourceTabsBounds.width > 0.0F && sourceTabsBounds.height > 0.0F)
        shell->set_bounds(sourceTabsBounds);
    else {
        // 最後の代替として、メインウィンドウ付近の見える既定サイズを使う。
        const lib::Rect root = _comp ? _comp->bounds() : lib::Rect{};
        shell->set_bounds({root.x + 40.0F, root.y + 40.0F, 400.0F, 300.0F});
    }
    shell->set_visible(true);
    _panes[newKey].shell = shell;
    (void)updateSubShellTitle(shell);
    for (const auto& handler : moveShellEvent)
        if (handler) handler(oldShell, keyValue);
}

bool DockingFolder::isSubWindowPane(const StringView paneKey) const {
    const auto found = _panes.find(String(paneKey));
    return found != _panes.end() && static_cast<bool>(found->second.shell);
}

bool DockingFolder::isSubWindowCtrl(const StringView keyValue) const {
    const String paneKey = paneKeyFromCtrlKey(keyValue);
    return !paneKey.empty() && isSubWindowPane(paneKey);
}

bool DockingFolder::isSingleWindow(const StringView keyValue) const {
    if (!isSubWindowCtrl(keyValue)) return false;
    const String paneKey = paneKeyFromCtrlKey(keyValue);
    const auto found = _panes.find(paneKey);
    if (found == _panes.end() || found->second.tabKeys.size() != 1) return false;
    if (lib::editor_window_host_options().all_mode) return true;
    // 一つのタブだけのペインでも、複数ペインの浮動Shell全体で唯一のタブとは限らない。
    return std::count_if(_panes.begin(), _panes.end(), [&](const auto& entry) {
        return entry.second.shell == found->second.shell;
    }) == 1;
}

bool DockingFolder::isSinglePane(const StringView paneKey) const {
    if (!isSubWindowPane(paneKey)) return false;
    const auto found = _panes.find(String(paneKey));
    if (found == _panes.end() || found->second.tabKeys.size() != 1) return false;
    if (lib::editor_window_host_options().all_mode) return true;
    // 一つのタブだけのペインでも、複数ペインの浮動Shell全体で唯一のタブとは限らない。
    return std::count_if(_panes.begin(), _panes.end(), [&](const auto& entry) {
        return entry.second.shell == found->second.shell;
    }) == 1;
}

bool DockingFolder::select(const StringView keyValue, const bool requestFocus) {
    const lib::EditorPerformanceScope performance("dock.select", 0);
    Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    auto paneIt = _panes.find(tabValue->paneKey);
    if (paneIt == _panes.end()) return false;
    const std::size_t index = pageIndex(paneIt->second, keyValue);
    if (index >= paneIt->second.tabs->pages.size()) return false;
    (void)lib::select_tab_page(*paneIt->second.tabs, index, false);
    selectTab(paneIt->second.tabs, requestFocus);
    return true;
}

bool DockingFolder::walkLeft(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto paneIt = _panes.find(tabValue->paneKey);
    if (paneIt == _panes.end() || paneIt->second.tabKeys.size() < 2) return false;
    const std::size_t index = pageIndex(paneIt->second, keyValue);
    const std::size_t next = index == 0 ? paneIt->second.tabKeys.size() - 1 : index - 1;
    return select(paneIt->second.tabKeys[next]);
}

bool DockingFolder::walkRight(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto paneIt = _panes.find(tabValue->paneKey);
    if (paneIt == _panes.end() || paneIt->second.tabKeys.size() < 2) return false;
    const std::size_t index = pageIndex(paneIt->second, keyValue);
    const std::size_t next = (index + 1) % paneIt->second.tabKeys.size();
    return select(paneIt->second.tabKeys[next]);
}

bool DockingFolder::close(const StringView keyValue) {
    if (!tab(keyValue)) return false;
    removeControl(keyValue, true);
    return true;
}

bool DockingFolder::setRestoreKey(const StringView keyValue, String restoreKey) {
    auto found = _tabs.find(String(keyValue));
    if (found == _tabs.end() || !found->second) return false;
    found->second->restoreKey = std::move(restoreKey);
    return true;
}

bool DockingFolder::discard(const StringView keyValue) {
    if (!tab(keyValue)) return false;
    removeControl(keyValue, false);
    return true;
}

bool DockingFolder::hasEtc(const StringView keyValue) const {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto found = _panes.find(tabValue->paneKey);
    return found != _panes.end() && found->second.tabKeys.size() > 1;
}

void DockingFolder::closeEtc(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return;
    const auto keys = found->second.tabKeys;
    for (const String& candidate : keys)
        if (candidate != keyValue) removeControl(candidate, true);
}

bool DockingFolder::hasLeft(const StringView keyValue) const {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return false;
    return pageIndex(found->second, keyValue) > 0;
}

void DockingFolder::closeLeft(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return;
    const std::size_t index = pageIndex(found->second, keyValue);
    const auto keys = std::vector<String>(found->second.tabKeys.begin(),
                                          found->second.tabKeys.begin() +
                                              static_cast<std::ptrdiff_t>(
                                                  std::min(index, found->second.tabKeys.size())));
    for (const String& candidate : keys) removeControl(candidate, true);
}

bool DockingFolder::hasRight(const StringView keyValue) const {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return false;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return false;
    const std::size_t index = pageIndex(found->second, keyValue);
    return index + 1 < found->second.tabKeys.size();
}

void DockingFolder::closeRight(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return;
    const std::size_t index = pageIndex(found->second, keyValue);
    if (index + 1 >= found->second.tabKeys.size()) return;
    const auto begin = found->second.tabKeys.begin() +
                       static_cast<std::ptrdiff_t>(index + 1);
    const auto keys = std::vector<String>(begin, found->second.tabKeys.end());
    for (const String& candidate : keys) removeControl(candidate, true);
}

void DockingFolder::closeAll(const StringView keyValue) {
    const Tab* tabValue = tab(keyValue);
    if (!tabValue) return;
    const auto found = _panes.find(tabValue->paneKey);
    if (found == _panes.end()) return;
    const auto keys = found->second.tabKeys;
    for (const String& candidate : keys) removeControl(candidate, true);
}

void DockingFolder::addCreatePaneEvent(PaneHandler handler) {
    if (!handler) return;
    createPaneEvent.push_back(handler);
    for (const String& keyValue : _paneOrder) {
        const auto found = _panes.find(keyValue);
        if (found != _panes.end()) handler(found->second.pane, keyValue);
    }
}

void DockingFolder::removeTabf(const TabF& tabf, const bool memory) {
    if (!tabf) return;
    String keyValue;
    for (const auto& [candidate, state] : _panes)
        if (state.tabs == tabf) {
            keyValue = candidate;
            break;
        }
    if (keyValue.empty()) return;
    auto paneIt = _panes.find(keyValue);
    if (paneIt == _panes.end()) return;
    const Pane removedPane = paneIt->second.pane;
    const Shell removedShell = paneIt->second.shell;
    Dir direction = Dir::N;
    int leftWeight = 1;
    int rightWeight = 1;
    const String pair = pairKey(tabf, direction, leftWeight, rightWeight);
    if (memory && memoryPane && memoryPane(keyValue)) {
        PaneMemory saved;
        saved.pairPane = pair;
        saved.dir = direction;
        saved.lWeight = leftWeight;
        saved.rWeight = rightWeight;
        saved.isSubWindow = pair.empty() && static_cast<bool>(paneIt->second.shell);
        if (saved.isSubWindow) {
            saved.subPaneKey = keyValue;
            const auto bounds = paneIt->second.shell->bounds();
            const lib::Rect rootBounds = _comp ? _comp->bounds() : lib::Rect{};
            const auto shell = hostWindow();
            const bool useScreenOrigin = shell &&
                !lib::editor_window_host_options().all_mode;
            const auto origin = useScreenOrigin ? shell->bounds() : rootBounds;
            saved.x = static_cast<int>(bounds.x - origin.x);
            saved.y = static_cast<int>(bounds.y - origin.y);
            saved.width = static_cast<int>(bounds.width);
            saved.height = static_cast<int>(bounds.height);
        }
        _pMemories[prefix(keyValue)] = saved;
    }

    Composite parent = parentOf(paneIt->second.pane);
    if (parent) parent->remove(paneIt->second.pane);
    if (parent) {
        if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(parent))
            collapseSplit(split);
    }
    tabOwners.erase(tabf.get());
    _parents.erase(tabf.get());
    if (removedPane) _parents.erase(removedPane.get());
    _panes.erase(paneIt);
    _paneOrder.erase(std::remove(_paneOrder.begin(), _paneOrder.end(), keyValue),
                     _paneOrder.end());
    for (auto& state : _subWindows)
        state.paneKeys.erase(std::remove(state.paneKeys.begin(),
                                         state.paneKeys.end(), keyValue),
                             state.paneKeys.end());
    for (std::size_t i = 0; i < _subWindows.size();) {
        if (!_subWindows[i].paneKeys.empty()) {
            ++i;
            continue;
        }
        const Shell closedShell = _subWindows[i].shell;
        _subWindows.erase(_subWindows.begin() + static_cast<std::ptrdiff_t>(i));
        if (closedShell && _comp) {
            _comp->remove(closedShell);
            _parents.erase(closedShell.get());
            closedShell->set_visible(false);
        }
        for (const auto& handler : shellClosedEvent)
            if (handler) handler();
        if (closedShell) closedShell->on_close_requested = {};
    }
    std::vector<String> removedTabs;
    for (const auto& [tabKey, record] : _tabs)
        if (record->paneKey == keyValue) removedTabs.push_back(tabKey);
    for (const String& tabKey : removedTabs) {
        if (const auto controlIt = _controls.find(tabKey);
            controlIt != _controls.end()) {
            _controlKeys.erase(controlIt->second.control.get());
            _parents.erase(controlIt->second.control.get());
            _controls.erase(controlIt);
        }
        _controlOrder.erase(
            std::remove(_controlOrder.begin(), _controlOrder.end(), tabKey),
            _controlOrder.end());
        _tabs.erase(tabKey);
    }
    if (removedShell && _comp &&
        std::find_if(_subWindows.begin(), _subWindows.end(),
                     [&](const SubWindowState& state) {
                         return state.shell == removedShell;
                     }) == _subWindows.end()) {
        _comp->remove(removedShell);
        _parents.erase(removedShell.get());
        removedShell->set_visible(false);
    }
}

String DockingFolder::pairKey(const TabF& tabf, Dir& dir, int& lWeight,
                              int& rWeight) const {
    if (!tabf) return {};
    String paneKey;
    for (const auto& [candidate, state] : _panes)
        if (state.tabs == tabf) {
            paneKey = candidate;
            break;
        }
    const auto paneIt = _panes.find(paneKey);
    if (paneIt == _panes.end()) return {};
    const auto parent = std::dynamic_pointer_cast<lib::SplitView>(
        parentOf(paneIt->second.pane));
    if (!parent || parent->children().size() != 2) return {};
    const auto& children = parent->children();
    const bool firstChild = children.front() == paneIt->second.pane;
    if (parent->orientation == lib::Orientation::Vertical)
        dir = firstChild ? Dir::N : Dir::S;
    else
        dir = firstChild ? Dir::W : Dir::E;
    if (parent->weights.size() >= 2) {
        lWeight = static_cast<int>(parent->weights[0]);
        rWeight = static_cast<int>(parent->weights[1]);
    }
    const auto other = firstChild ? children[1] : children[0];
    if (const auto otherPane = std::dynamic_pointer_cast<lib::Panel>(other))
        return key(otherPane);
    for (const auto& [candidate, sash] : _sashes)
        if (sash == other) return candidate;
    return {};
}

void DockingFolder::close(Tab* tabValue) {
    if (tabValue) removeControl(tabValue->key, true);
}

bool DockingFolder::updateSubShellTitle(const Shell& shell) {
    if (!shell) return false;
    for (const auto& [keyValue, state] : _panes) {
        if (state.shell != shell || !state.tabs || !state.tabs->selected_index)
            continue;
        const std::size_t index = *state.tabs->selected_index;
        if (index >= state.tabKeys.size()) continue;
        const auto tabIt = _tabs.find(state.tabKeys[index]);
        if (tabIt == _tabs.end()) continue;
        shell->title = tabIt->second->text;
        return true;
    }
    return false;
}

String DockingFolder::newSashKey() const {
    std::size_t index = 0;
    String keyValue;
    do {
        keyValue = "sash_" + std::to_string(index++);
    } while (_sashes.contains(keyValue) || _panes.contains(keyValue));
    return keyValue;
}

void DockingFolder::putSashTable(const SplitPane& sash, String keyValue) {
    if (!sash) return;
    _sashes[keyValue] = sash;
    _sashOrder.push_back(keyValue);
    registerParent({}, sash);
}

void DockingFolder::collapseSplit(const SplitPane& split) {
    if (!split) return;
    Composite parent = parentOf(split);
    const auto children = split->children();
    if (children.size() == 1 && parent) {
        replaceChild(parent, split, children.front());
        registerParent(parent, children.front());
    } else if (children.empty() && parent) {
        parent->remove(split);
    }
    String sashKey;
    for (const auto& [candidate, value] : _sashes)
        if (value == split) {
            sashKey = candidate;
            break;
        }
    if (!sashKey.empty()) {
        _sashes.erase(sashKey);
        _sashOrder.erase(
            std::remove(_sashOrder.begin(), _sashOrder.end(), sashKey),
            _sashOrder.end());
    }
    _parents.erase(split.get());
}

void DockingFolder::reconstruct(const Control& areaValue) {
    if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(areaValue))
        collapseSplit(split);
}

void DockingFolder::removeControl(const StringView inputKey,
                                   const bool invokeEvents) {
    const String stableKey(inputKey);
    const StringView keyValue = stableKey;
    const StringView ownedKey = stableKey;
    const std::weak_ptr<void> weakLife = _lifetime;
    if (_closingControls.contains(stableKey)) return;
    _closingControls.emplace(stableKey, true);
    struct ClosingScope {
        DockingFolder* owner;
        std::weak_ptr<void> life;
        String key;
        ~ClosingScope() { if (!life.expired()) owner->_closingControls.erase(key); }
    } scope{this, weakLife, stableKey};
    auto tabIt = _tabs.find(String(keyValue));
    auto controlIt = _controls.find(String(keyValue));
    if (tabIt == _tabs.end() || controlIt == _controls.end()) return;
    if (invokeEvents) {
        const auto handlers = closeCtrlEvent;
        for (const auto& handler : handlers) {
            const bool accepted = !handler || handler(ownedKey);
            if (weakLife.expired() || !accepted) return;
        }
        // 終了の監視通知が、別のタブを同期的に閉じたりこのタブを削除したり
        // し得る。その通知をまたいで保持したイテレーターを再利用しない。
        tabIt = _tabs.find(stableKey);
        controlIt = _controls.find(stableKey);
        if (tabIt == _tabs.end() || controlIt == _controls.end()) return;
    }
    TabRecord& record = *tabIt->second;
    const String removedPaneKey = record.paneKey;
    auto paneIt = _panes.find(record.paneKey);
    if (paneIt == _panes.end()) return;
    const TabF removedPaneTabs = paneIt->second.tabs;
    const bool mayVanish = isVanishable(record.paneKey);
    if (mayVanish && memoryControl && memoryControl(keyValue)) {
        Dir direction = Dir::N;
        int leftWeight = 1;
        int rightWeight = 1;
        const String pair = pairKey(paneIt->second.tabs, direction, leftWeight,
                                    rightWeight);
        if (!pair.empty())
            _cMemories[String(keyValue)] =
                CtrlMemory{record.paneKey, pair, direction, leftWeight,
                            rightWeight};
    }

    const std::size_t index = pageIndex(paneIt->second, keyValue);
    const std::size_t oldSelection =
        paneIt->second.tabs->selected_index.value_or(0U);
    Composite pageContent;
    if (index < paneIt->second.tabs->pages.size())
        pageContent = std::dynamic_pointer_cast<lib::Container>(
            paneIt->second.tabs->pages[index].content);
    if (index >= paneIt->second.tabKeys.size() ||
        index >= paneIt->second.tabs->pages.size())
        return;
    auto pages = paneIt->second.tabs->pages;
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(index));
    std::optional<std::size_t> nextSelection;
    if (!pages.empty()) {
        const std::size_t adjustedSelection =
            oldSelection > index ? oldSelection - 1U : oldSelection;
        nextSelection = std::min(adjustedSelection, pages.size() - 1U);
    }
    if (!lib::replace_tab_pages(paneIt->second.tabs, std::move(pages),
                                nextSelection))
        return;
    paneIt->second.tabKeys.erase(
        paneIt->second.tabKeys.begin() + static_cast<std::ptrdiff_t>(index));
    paneIt->second.pane->remove(controlIt->second.control);
    if (pageContent) {
        _parents.erase(pageContent.get());
        if (pageContent != controlIt->second.control)
            pageContent->remove(controlIt->second.control);
    }
    _controlKeys.erase(controlIt->second.control.get());
    _parents.erase(controlIt->second.control.get());
    _controls.erase(controlIt);
    _controlOrder.erase(
        std::remove(_controlOrder.begin(), _controlOrder.end(), keyValue),
        _controlOrder.end());
    // 共通タブのアクティブ化は、ドラッグを開始した直後のタブを同期的
    // に閉じる場合がある。記録の同一性を照合して削除し、後続のポイン
    // ターイベントが古いアドレスを参照する前にドラッグを終了する。
    const bool retiringDragSource = _dragItm == tabIt->second.get();
    _tabs.erase(tabIt);
    if (retiringDragSource) {
        resetDockDrag();
        if (weakLife.expired()) return;
        paneIt = _panes.find(removedPaneKey);
        if (paneIt == _panes.end() || paneIt->second.tabs != removedPaneTabs)
            return;
    }

    if (paneIt->second.tabs->pages.empty()) {
        if (mayVanish && !(paneIt->second.pane == _area &&
                           _area->children().size() == 1))
            removeTabf(paneIt->second.tabs, true);
    } else {
        selectTab(paneIt->second.tabs);
    }
}

void DockingFolder::selectTab(const TabF& tabf, const bool requestFocus) {
    const lib::EditorPerformanceScope performance("dock.selectTab", 0);
    const std::weak_ptr<void> life = _lifetime;
    if (_onNewTab || !tabf || !tabf->selected_index) return;
    String paneKey;
    for (const auto& [candidate, state] : _panes)
        if (state.tabs == tabf) {
            paneKey = candidate;
            if (state.shell) (void)updateSubShellTitle(state.shell);
            break;
        }
    auto paneIt = _panes.find(paneKey);
    if (paneIt == _panes.end() || !paneIt->second.tabs->selected_index) return;
    const PaneState& selectedPane = paneIt->second;
    const std::size_t index = *selectedPane.tabs->selected_index;
    if (index >= paneIt->second.tabKeys.size() ||
        index >= paneIt->second.tabs->pages.size())
        return;
    const String keyValue = selectedPane.tabKeys[index];
    const bool selection_changed = keyValue != _oldSel;
    if (selection_changed) _oldSel = keyValue;

    // コールバックがペインを閉じたり移動・再構築したり
    // し得る。通知をまたいでベクターやページへの参照を
    // 借用せず、不変の識別情報を保持する。下記で表示側
    // へフォーカスを要求する前に、すべて再解決する。
    const auto selectedPage = selectedPane.tabs->pages[index].content;
    const auto selectedControl = control(keyValue);
    const auto tabLifetime = selectedPane.tabs->operation_lifetime();
    const auto pageUpdateRevision = selectedPane.tabs->page_update_revision;
    const auto pageOrderRevision = selectedPane.tabs->page_order_revision;
    const auto handlers = selectEvent;
    if (selection_changed) {
        for (const auto& handler : handlers) {
            if (handler) handler(keyValue);
            if (life.expired()) return;
        }
    }

    // モデルの通知後も、その正確なタブ・ページ・コントロールが
    // 選択中の有効な対象である場合だけ、フォーカスを公開する。
    if (!requestFocus || !selectFocusEvent || !selectedPage || !selectedControl ||
        tabLifetime.expired())
        return;
    paneIt = _panes.find(paneKey);
    if (paneIt == _panes.end() || paneIt->second.tabs != tabf ||
        paneIt->second.tabs->page_update_revision != pageUpdateRevision ||
        paneIt->second.tabs->page_order_revision != pageOrderRevision ||
        paneIt->second.tabs->selected_index != index ||
        index >= paneIt->second.tabKeys.size() ||
        paneIt->second.tabKeys[index] != keyValue ||
        index >= paneIt->second.tabs->pages.size() ||
        paneIt->second.tabs->pages[index].content != selectedPage)
        return;
    const auto controlIt = _controls.find(keyValue);
    const auto tabIt = _tabs.find(keyValue);
    if (controlIt == _controls.end() || tabIt == _tabs.end() ||
        controlIt->second.control != selectedControl ||
        controlIt->second.paneKey != paneKey || tabIt->second->control != selectedControl ||
        tabIt->second->paneKey != paneKey)
        return;
    // この共通経路は選択コントロール、そのアクティブでフォーカス可能な子
    // の順に優先し、その後で外側のTabViewやページ内の代替を使う。
    if (const auto target = lib::focus_target_in_scope(paneIt->second.pane,
                                                       selectedControl, true, true)) {
        const auto focus = selectFocusEvent;
        if (focus) focus(target);
    }
}

void DockingFolder::setFirstResize(const SplitPane& sash) {
    if (!sash || sash->children().empty()) return;
    const Pane firstPane =
        std::dynamic_pointer_cast<lib::Panel>(sash->children().front());
    const bool resizeFirst = firstPane && firstResize && firstResize(key(firstPane));
    setFirstResize(sash, resizeFirst);
}

void DockingFolder::setFirstResize(const SplitPane& sash,
                                   const bool resizeFirst) {
    if (!sash || sash->children().empty()) return;
    sash->weights.resize(2, 1.0F);
    const Pane firstPane =
        std::dynamic_pointer_cast<lib::Panel>(sash->children().front());
    if (firstPane && firstResize) sash->on_weights_changed = {};
    if (lib::editor_window_host_options().all_mode) {
        // ALL固有の配置として、既存の部品位置を維持する
        // ため、従来の比率交換とサイズ変更の動作を残す。
        if (resizeFirst) std::swap(sash->weights[0], sash->weights[1]);
    } else {
        // DのresizeControl1は、ウィンドウのサイズ変更を吸収する
        // 子を切り替える。指定済み・復元済みの分割比率を反転するものではない。
        sash->resize_mode = resizeFirst ? lib::SplitResizeMode::PreserveSecond
                                       : lib::SplitResizeMode::PreserveFirst;
    }
}

void DockingFolder::newTab(const TabF& targetTabs, Tab* tabValue,
                           const std::ptrdiff_t index, const bool updateCloseStyle) {
    if (!targetTabs || !tabValue) return;
    const auto sourceIt = _panes.find(tabValue->paneKey);
    String targetKey;
    for (const auto& [candidate, state] : _panes)
        if (state.tabs == targetTabs) {
            targetKey = candidate;
            break;
        }
    auto targetIt = _panes.find(targetKey);
    if (sourceIt == _panes.end() || targetIt == _panes.end()) return;
    if (sourceIt->first == targetIt->first && index >= 0) {
        const std::size_t current = pageIndex(sourceIt->second, tabValue->key);
        std::size_t destination = static_cast<std::size_t>(index);
        if (current < destination) --destination;
        if (current == destination) return;
    }
    const std::size_t oldIndex = pageIndex(sourceIt->second, tabValue->key);
    if (oldIndex >= sourceIt->second.tabKeys.size()) return;
    if (oldIndex >= sourceIt->second.tabs->pages.size()) return;
    const std::weak_ptr<void> life = _lifetime;
    ScopedTabMove tabMove(_onNewTab, life);
    lib::TabPage page = sourceIt->second.tabs->pages[oldIndex];
    auto sourceKeys = sourceIt->second.tabKeys;
    auto sourcePages = sourceIt->second.tabs->pages;
    sourceKeys.erase(sourceKeys.begin() + static_cast<std::ptrdiff_t>(oldIndex));
    sourcePages.erase(sourcePages.begin() + static_cast<std::ptrdiff_t>(oldIndex));
    // 旧呼び出し側のファクトリーは、コントロールを直接の子とし
    // て接続している場合がある。それを外してもページの入れ物を
    // 使うモデルには影響せず、旧呼び出し側の古い重複を防げる。
    std::size_t destination = index < 0
                                  ? (sourceIt == targetIt ? sourceKeys.size()
                                                          : targetIt->second.tabKeys.size())
                                  : static_cast<std::size_t>(index);
    if (sourceIt == targetIt) {
        // Dは転送先の元インデックスの前へ挿入してから転送元を削除する。この所
        // 有ページの移動は転送元を先に外すため、右方向だけ位置を正規化する。
        if (index >= 0 && oldIndex < destination) --destination;
        destination = std::min(destination, sourceKeys.size());
        sourceKeys.insert(sourceKeys.begin() + static_cast<std::ptrdiff_t>(destination),
                          tabValue->key);
        sourcePages.insert(sourcePages.begin() + static_cast<std::ptrdiff_t>(destination),
                           std::move(page));
        if (!lib::replace_tab_pages(sourceIt->second.tabs, std::move(sourcePages),
                                    destination))
            return;
        if (life.expired()) return;
        sourceIt->second.tabKeys = std::move(sourceKeys);
    } else {
        auto targetKeys = targetIt->second.tabKeys;
        auto targetPages = targetIt->second.tabs->pages;
        destination = std::min(destination, targetKeys.size());
        targetKeys.insert(targetKeys.begin() + static_cast<std::ptrdiff_t>(destination),
                          tabValue->key);
        targetPages.insert(targetPages.begin() + static_cast<std::ptrdiff_t>(destination),
                           std::move(page));
        const auto sourceSelection = sourcePages.empty()
            ? std::optional<std::size_t>{}
            : std::optional<std::size_t>{std::min(
                  sourceIt->second.tabs->selected_index.value_or(0U),
                  sourcePages.size() - 1U)};
        auto sourceUpdate = lib::prepare_tab_pages(
            sourceIt->second.tabs, std::move(sourcePages), sourceSelection);
        auto targetUpdate = lib::prepare_tab_pages(
            targetIt->second.tabs, std::move(targetPages), destination);
        if (!sourceUpdate.current() || !targetUpdate.current()) return;
        if (!sourceUpdate.commit() || !targetUpdate.commit()) return;
        sourceIt->second.tabKeys = std::move(sourceKeys);
        targetIt->second.tabKeys = std::move(targetKeys);
        sourceIt->second.pane->remove(tabValue->control);
        if (const auto content = std::dynamic_pointer_cast<lib::Container>(
                targetIt->second.tabs->pages[destination].content))
            registerParent(targetIt->second.pane, content);
    }
    tabValue->paneKey = targetKey;
    if (const auto controlIt = _controls.find(tabValue->key);
        controlIt != _controls.end())
        controlIt->second.paneKey = targetKey;
    const auto movedKey = tabValue->key;
    const auto movedControl = tabValue->control;
    const auto targetPresentation = targetIt->second.tabs;
    if (updateCloseStyle) {
        const auto closePolicy = hasCloseButton;
        const bool closable = !closePolicy || closePolicy(movedKey);
        // ネイティブの規則を問い合わせるコールバックが、タブやフォルダーを閉じ
        // たり移したりし得る。公開・フォーカス前に、正確な移動先を再取得する。
        if (life.expired()) return;
        const auto current = _tabs.find(movedKey);
        const auto destinationPane = _panes.find(targetKey);
        if (current == _tabs.end() || destinationPane == _panes.end() ||
            current->second->control != movedControl || current->second->paneKey != targetKey ||
            destinationPane->second.tabs != targetPresentation) return;
        current->second->closable = closable;
    }
    tabMove.restore();
    selectTab(targetPresentation);
}

void DockingFolder::drawDropMark(const Canvas& canvas, const float x, const float y,
                                 const float width, const float height) {
    if (!canvas) return;
    const DPos position = _drawPos;
    const bool linuxOutline = lib::native_platform() == lib::Platform::Linux &&
                              !lib::editor_window_host_options().all_mode;
    canvas->set_visible(true);
    canvas->on_paint = {};
    canvas->on_paint_context = [position, x, y, width, height, linuxOutline](
        lib::DrawingContext& drawing, const lib::Rect&) {
        lib::Rect mark{x, y, width, height};
        switch (position) {
        case DPos::C: break;
        case DPos::N: mark.height /= 2.0F; break;
        case DPos::E:
            mark.x += mark.width / 2.0F;
            mark.width /= 2.0F;
            break;
        case DPos::S:
            mark.y += mark.height / 2.0F;
            mark.height /= 2.0F;
            break;
        case DPos::W: mark.width /= 2.0F; break;
        case DPos::NONE: return;
        }
        constexpr lib::Color color{0U, 0U, 0U, 0x7FU};
        if (!linuxOutline) { drawing.fill_rect(mark, color); return; }
        if (mark.width <= 0.0F || mark.height <= 0.0F) return;
        // DのLinux版は輪郭に別の形状付きShellを使う。このクリップ
        // 済みCanvasは枠を印の少し内側に置き、端の対象を見えるように
        // する。小さい内向きの補正で、二つ目のネイティブ重ね表示を避ける。
        const float border = std::min({lib::scaled_ui_metric(5.0F),
                                       mark.width / 2.0F, mark.height / 2.0F});
        drawing.fill_rect({mark.x, mark.y, mark.width, border}, color);
        drawing.fill_rect({mark.x, mark.y + mark.height - border, mark.width, border}, color);
        const float middle = std::max(0.0F, mark.height - 2.0F * border);
        if (middle > 0.0F) {
            drawing.fill_rect({mark.x, mark.y + border, border, middle}, color);
            drawing.fill_rect({mark.x + mark.width - border, mark.y + border, border, middle}, color);
        }
    };
    if (canvas->invalidate) canvas->invalidate();
}

bool DockingFolder::canDrop(const TabF& tabf) const {
    if (!_dragItm || !tabf || _dropPos == DPos::NONE) return false;
    if (!canMove) return true;
    String dropKey;
    for (const auto& [keyValue, state] : _panes)
        if (state.tabs == tabf) {
            dropKey = keyValue;
            break;
        }
    return canMove(_dragItm->key, _dropPos == DPos::C ? dropKey : String{});
}

std::vector<Control> DockingFolder::afters(const Control& target) {
    (void)target;
    return std::vector<Control>{};
}

void DockingFolder::addAfters(const std::vector<Control>& controlsValue) {
    for (const auto& controlValue : controlsValue)
        if (controlValue) controlValue->set_visible(controlValue->visible());
}

void DockingFolder::setInsertMark(const TabF& tabf, Tab* tabValue,
                                  const bool after) {
    if (!tabf) return;
    const auto owner = tabOwners.find(tabf.get());
    if (owner == tabOwners.end()) return;
    for (const auto& [paneKey, state] : owner->second->_panes) {
        if (state.tabs != tabf) continue;
        std::size_t index = state.tabKeys.size();
        if (tabValue) {
            const String tabKey = tabValue->key;
            index = owner->second->pageIndex(state, tabKey);
            if (after && index < state.tabKeys.size()) ++index;
        }
        if (!state.tabKeys.empty())
            (void)lib::select_tab_page(
                *tabf,
                std::min(index, state.tabKeys.size() - static_cast<std::size_t>(1)),
                false);
        return;
    }
}

DockingFolder::Tab* DockingFolder::selected(const TabF& tabf) {
    if (!tabf || !tabf->selected_index) return nullptr;
    const auto owner = tabOwners.find(tabf.get());
    if (owner == tabOwners.end()) return nullptr;
    for (const auto& [paneKey, state] : owner->second->_panes) {
        if (state.tabs != tabf || *tabf->selected_index >= state.tabKeys.size())
            continue;
        return owner->second->tab(state.tabKeys[*tabf->selected_index]);
    }
    return nullptr;
}

lib::Rect DockingFolder::boundsOnDisplay(const Control& controlValue) const {
    if (!controlValue) return {};
    lib::Rect result = controlValue->bounds();
    Composite current = parentOf(controlValue);
    if (!lib::editor_window_host_options().all_mode) {
        // Widgetの配置矩形は、すでにウィンドウ内の絶
        // 対座標。祖先の位置を再加算するとドロップ先が右ペ
        // インから外れ、分割できても結合し直せなくなる。
        while (current) {
            if (std::dynamic_pointer_cast<lib::Window>(current)) {
                const lib::Rect bounds = current->bounds();
                result.x += bounds.x;
                result.y += bounds.y;
                break;
            }
            current = parentOf(current);
        }
        return result;
    }
    while (current) {
        const lib::Rect bounds = current->bounds();
        result.x += bounds.x;
        result.y += bounds.y;
        current = parentOf(current);
    }
    return result;
}

lib::Rect DockingFolder::boundsOnCanvas(const Canvas& canvas,
                                        const Control& controlValue) const {
    if (!canvas || !controlValue) return {};
    const lib::Rect canvasBounds = boundsOnDisplay(canvas);
    const lib::Rect controlBounds = boundsOnDisplay(controlValue);
    return {controlBounds.x - canvasBounds.x, controlBounds.y - canvasBounds.y,
            controlBounds.width, controlBounds.height};
}

void DockingFolder::DListener::handleEvent() {
    if (!owner || !owner->_canSave) return;
    owner->saveTree();
}

void DockingFolder::CTFL::close(const StringView keyValue) {
    if (owner) owner->close(keyValue);
}

void DockingFolder::PL::paintControl(const PaintEvent& event) {
    if (owner && owner->_drawTabf) {
        owner->drawDropMark(owner->_canvas, event.bounds.x, event.bounds.y,
                            event.bounds.width, event.bounds.height);
    }
}

void DockingFolder::DSL::dragStart(const StringView keyValue) {
    if (!owner) return;
    owner->_dragItm = owner->tab(keyValue);
    owner->_drawTabf = tabf;
    owner->hideDropCanvases();
}

void DockingFolder::DSL::dragSetData(const StringView keyValue) {
    if (!owner || !owner->_dragItm) return;
    (void)keyValue;
}

void DockingFolder::DSL::dragFinished(const bool moved) {
    if (!owner) return;
    (void)moved;
    owner->resetDockDrag();
}

void DockingFolder::DTL::dragEnter(DropEvent& event) {
    dragOver(event);
}

void DockingFolder::DTL::dragLeave(DropEvent& event) {
    event.accepted = false;
    if (owner) {
        owner->_drawPos = DPos::NONE;
        owner->_dropPos = DPos::NONE;
        owner->_drawTabf.reset();
        owner->hideDropCanvases();
    }
}

TabF DockingFolder::DTL::getTabf(const float x, const float y) const {
    if (!owner) return {};
    for (const String& keyValue : owner->_paneOrder) {
        const auto found = owner->_panes.find(keyValue);
        if (found != owner->_panes.end() &&
            pointIn(owner->boundsOnDisplay(found->second.tabs), x, y))
            return found->second.tabs;
    }
    return {};
}

void DockingFolder::DTL::dragOver(DropEvent& event) {
    event.accepted = false;
    if (!owner) return;
    const TabF previousTarget = owner->_drawTabf;
    const DPos previousPosition = owner->_drawPos;
    const TabF target = getTabf(event.x, event.y);
    owner->_drawTabf = target;
    if (!target) {
        owner->_dropPos = DPos::NONE;
        owner->_drawPos = DPos::NONE;
        if (previousTarget || previousPosition != DPos::NONE)
            owner->hideDropCanvases();
        return;
    }
    const auto bounds = owner->boundsOnDisplay(target);
    const float relX = event.x - bounds.x;
    const float relY = event.y - bounds.y;
    owner->_dropPos = DPos::NONE;
    if (bounds.width > 0.0F && bounds.height > 0.0F) {
        const float fromRight = bounds.width - relX;
        const float fromBottom = bounds.height - relY;
        bool sourceOnlyTab = false;
        if (owner->_dragItm) {
            const auto source = owner->_panes.find(owner->_dragItm->paneKey);
            sourceOnlyTab = source != owner->_panes.end() &&
                source->second.tabs == target && source->second.tabKeys.size() == 1U;
        }
        // Dではタブ見出し上のポインターは、常に挿入・並べ
        // 替えを意味する。ペイン端の座標だけで判定すると、
        // 見出しの小さいYが上分割になり、通常の横ドラッグ
        // で場面・イベントペインを分割・切り離してしまう。
        const auto hitPoint = lib::editor_window_host_options().all_mode
            ? lib::Point{relX, relY}
            : lib::Point{relX + target->bounds().x, relY + target->bounds().y};
        const auto header = lib::hit_test_tab_page(*target, hitPoint);
        const auto content = lib::layout_tab_content(*target);
        const float headerHeight = content.y - target->bounds().y;
        const bool headerBand = !lib::editor_window_host_options().all_mode &&
            std::isfinite(headerHeight) && relX >= 0.0F && relX < bounds.width &&
            relY >= 0.0F && relY < headerHeight;
        if (header || headerBand || relY < 0.0F || sourceOnlyTab) owner->_dropPos = DPos::C;
        else if (relY <= relX && relY <= fromRight &&
                 relY < bounds.height / 3.0F) owner->_dropPos = DPos::N;
        else if (fromRight <= relY && fromRight <= fromBottom &&
                 relX > bounds.width * 2.0F / 3.0F) owner->_dropPos = DPos::E;
        else if (fromBottom <= relX && fromBottom <= fromRight &&
                 relY > bounds.height * 2.0F / 3.0F) owner->_dropPos = DPos::S;
        else if (relX <= relY && relX <= fromBottom &&
                 relX < bounds.width / 3.0F) owner->_dropPos = DPos::W;
        else if (relX >= 0.0F && relY >= 0.0F && relX < bounds.width &&
                 relY < bounds.height) owner->_dropPos = DPos::C;
    }
    event.accepted = owner->canDrop(target);
    if (!event.accepted) owner->_dropPos = DPos::NONE;
    owner->_drawPos = owner->_dropPos;
    if (previousTarget != target || previousPosition != owner->_drawPos)
        owner->updateDropIndicator(target);
}

void DockingFolder::DTL::drop(DropEvent& event) {
    if (!owner || !event.accepted || !owner->_dragItm || !owner->_drawTabf) return;
    const std::weak_ptr<void> life = owner->_lifetime;
    Tab* dragged = owner->_dragItm;
    const String sourcePaneKey = dragged->paneKey;
    const String movedKey = dragged->key;
    const auto source = owner->_panes.find(sourcePaneKey);
    const auto destinationPane = std::find_if(owner->_panes.begin(), owner->_panes.end(),
        [&](const auto& entry) { return entry.second.tabs == owner->_drawTabf; });
    const Shell oldShell = source == owner->_panes.end() ? Shell{} : source->second.shell;
    const Shell newShell = destinationPane == owner->_panes.end() ? Shell{} : destinationPane->second.shell;
    const bool changesShell = source != owner->_panes.end() &&
        destinationPane != owner->_panes.end() && oldShell != newShell;
    const auto plannedTargetTabs = owner->_drawTabf;
    const auto plannedDropPosition = owner->_dropPos;
    const String destinationPaneKey = destinationPane == owner->_panes.end()
        ? String{} : destinationPane->first;
    const auto validPlan = [&] {
        if (life.expired() || owner->tab(movedKey) != dragged ||
            dragged->paneKey != sourcePaneKey || owner->_drawTabf != plannedTargetTabs ||
            owner->_dropPos != plannedDropPosition) return false;
        const auto currentSource = owner->_panes.find(sourcePaneKey);
        const auto currentTarget = owner->_panes.find(destinationPaneKey);
        return currentSource != owner->_panes.end() && currentTarget != owner->_panes.end() &&
            currentSource->second.shell == oldShell && currentTarget->second.shell == newShell &&
            currentTarget->second.tabs == plannedTargetTabs;
    };
    const auto notifyMoved = [&] {
        if (!changesShell || life.expired()) return;
        const auto handlers = owner->moveShellEvent;
        for (const auto& handler : handlers) {
            if (handler) handler(oldShell, movedKey);
            if (life.expired()) return;
        }
    };
    const auto abortDrop = [&] {
        event.accepted = false;
        if (life.expired()) return;
        owner->_drawPos = DPos::NONE;
        owner->_dropPos = DPos::NONE;
        owner->hideDropCanvases();
        // 後処理の境界は実際に有効なホストを解決し、コールバックが移動を取り
        // 消した場合も、MainWindowで停止中の取得処理を解除する。
        notifyMoved();
    };
    // Dのイベントパレットは、ウィンドウ間のドロップ前に旧Shellから離す。createSubWindow
    // には自身の境界があり、newTab自体は通知しない。
    if (changesShell) {
        const auto handlers = owner->movingShellEvent;
        for (const auto& handler : handlers) {
            if (handler) handler(movedKey);
            if (life.expired()) return;
            if (!validPlan()) { abortDrop(); return; }
        }
    }
    if (owner->_dropPos == DPos::C) {
        std::ptrdiff_t destination = -1;
        const auto paneIt = std::find_if(
            owner->_panes.begin(), owner->_panes.end(),
            [&](const auto& entry) { return entry.second.tabs == owner->_drawTabf; });
        if (paneIt != owner->_panes.end() && !paneIt->second.tabKeys.empty()) {
            const lib::Rect bounds = owner->boundsOnDisplay(owner->_drawTabf);
            const auto widgetBounds = owner->_drawTabf->bounds();
            const lib::Point local{
                event.x - bounds.x + (lib::editor_window_host_options().all_mode ? 0.0F : widgetBounds.x),
                event.y - bounds.y + (lib::editor_window_host_options().all_mode ? 0.0F : widgetBounds.y)};
            if (const auto hit = lib::hit_test_tab_page(*owner->_drawTabf, local))
                destination = static_cast<std::ptrdiff_t>(*hit);
            else
                destination = static_cast<std::ptrdiff_t>(
                    paneIt->second.tabKeys.size());
        }
        owner->newTab(owner->_drawTabf, dragged, destination);
    } else {
        Dir dir = owner->_dropPos == DPos::N ? Dir::N
                 : owner->_dropPos == DPos::E ? Dir::E
                 : owner->_dropPos == DPos::S ? Dir::S : Dir::W;
        Composite target;
        for (const auto& [keyValue, state] : owner->_panes)
            if (state.tabs == owner->_drawTabf) {
                target = state.pane;
                break;
            }
        String createdKey;
        const String dragged_key = dragged->key;
        const String target_key = owner->key(target);
        const auto name_pane = owner->newPaneName;
        if (!lib::editor_window_host_options().all_mode) {
            // Dの分割経路は、転送元ペインの系統から新ペインのキーを作る。
            // newPaneNameコールバックは上で準備するが呼ばない。
            createdKey = owner->newPaneKey(DockingFolder::prefix(sourcePaneKey));
        } else if (name_pane) {
            // ALLでは既存のC++のコールバックによる命名規則を維持する。
            createdKey = name_pane(dragged_key, target_key, dir);
        }
        if (life.expired()) return;
        if (!validPlan() || owner->tab(dragged_key) != dragged || owner->pane(target_key) != target) {
            abortDrop();
            return;
        }
        const Pane created = owner->addPane(target, dir, 1, 1,
                                            std::move(createdKey));
        if (life.expired()) return;
        if (created) {
            if (owner->tab(movedKey) != dragged || dragged->paneKey != sourcePaneKey) {
                abortDrop();
                return;
            }
            const String newPaneKeyValue = owner->key(created);
            if (newPaneKeyValue.empty() || owner->pane(newPaneKeyValue) != created) {
                abortDrop();
                return;
            }
            owner->newTab(owner->_panes[newPaneKeyValue].tabs, dragged, -1);
        }
    }
    if (life.expired()) return;
    if (const auto source = owner->_panes.find(sourcePaneKey);
        source != owner->_panes.end() && source->second.tabKeys.empty() &&
        owner->isVanishable(sourcePaneKey))
        owner->removeTabf(source->second.tabs, true);
    if (life.expired()) return;
    event.accepted = true;
    owner->_drawPos = DPos::NONE;
    owner->_dropPos = DPos::NONE;
    owner->hideDropCanvases();
    notifyMoved();
}

void DockingFolder::SelTab::widgetSelected(const TabF& tabf) {
    if (owner) owner->selectTab(tabf);
}

void DockingFolder::ClickTab::mouseDown(const TabF& tabf) {
    if (owner && tabf) owner->selectTab(tabf);
}

void DockingFolder::ClickTab::mouseUp(const TabF& tabf, const float x, const float y,
                                      const lib::PointerButton button) {
    if (!owner || button != lib::PointerButton::Middle || !tabf) return;
    const String keyValue = owner->control(owner->key(
        [&]() -> Composite {
            for (const auto& [candidate, state] : owner->_panes)
                if (state.tabs == tabf) return state.pane;
            return Composite{};
        }()), x, y);
    if (!keyValue.empty() &&
        (!owner->closeTabWithMiddleClick ||
         owner->closeTabWithMiddleClick(keyValue)))
        owner->close(keyValue);
}

void DockingFolder::FocusL::focusIn(const lib::WidgetPtr& focused) {
    if (!owner || !focused) return;
    for (const auto& [paneKey, state] : owner->_panes) {
        (void)paneKey;
        if (!state.tabs || !state.tabs->selected_index) continue;
        const auto index = *state.tabs->selected_index;
        if (index >= state.tabs->pages.size()) continue;
        // タブフォルダー内で、フォーカス中のコントロールを所有できるのは表示中のページだけ。
        if (lib::contains_widget(state.tabs->pages[index].content, focused.get())) {
            owner->selectTab(state.tabs, false);
            return;
        }
    }
}

void DockingFolder::FocusL::handleEvent() {
    if (!owner) return;
    const std::weak_ptr<void> life = owner->_lifetime;
    for (const String& keyValue : owner->_paneOrder) {
        const auto found = owner->_panes.find(keyValue);
        if (found != owner->_panes.end()) owner->selectTab(found->second.tabs);
        if (life.expired()) return;
    }
}

TabF DockingFolder::newSash(const Composite& target,
                            const lib::Orientation orientation, const bool before,
                            const int lWeight, const int rWeight, String keyValue) {
    const Pane created = addPane(target, orientation == lib::Orientation::Vertical
                                             ? (before ? Dir::N : Dir::S)
                                             : (before ? Dir::W : Dir::E),
                                 lWeight, rWeight, std::move(keyValue));
    if (!created) return {};
    return _panes.at(key(created)).tabs;
}

void DockingFolder::Proc::createSub() {
    if (owner && !parent) owner->createSubWindow(shell, parent);
}

void DockingFolder::Proc::sash(const XNode& node) {
    if (owner) owner->restoreNode(node, parent, {});
}

void DockingFolder::Proc::tabs(const XNode& node) {
    if (!owner) return;
    owner->restoreNode(node, parent,
                       [this](const XNode& item, const TabF& tabs) {
                           if (addRemoveList)
                               addRemoveList(item.attr("key", false, String{}),
                                             tabs);
                       });
}

void DockingFolder::saveTree(const Control& controlValue,
                             std::shared_ptr<Sashf>& sash,
                             std::shared_ptr<Tabf>& tabf) {
    sash.reset();
    tabf.reset();
    if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(controlValue)) {
        sash = std::make_shared<Sashf>();
        sash->key = [&]() {
            for (const auto& [keyValue, value] : _sashes)
                if (value == split) return keyValue;
            return newSashKey();
        }();
        sash->vertical = split->orientation == lib::Orientation::Vertical;
        if (split->weights.size() >= 2) {
            sash->lWeight = static_cast<int>(split->weights[0]);
            sash->rWeight = static_cast<int>(split->weights[1]);
        }
        const auto& children = split->children();
        if (!children.empty()) saveTree(children.front(), sash->lSash, sash->lTabf);
        if (children.size() > 1) saveTree(children[1], sash->rSash, sash->rTabf);
        return;
    }
    const auto paneValue = std::dynamic_pointer_cast<lib::Panel>(controlValue);
    if (!paneValue) return;
    const String keyValue = key(paneValue);
    const auto found = _panes.find(keyValue);
    if (found == _panes.end()) return;
    tabf = std::make_shared<Tabf>();
    tabf->key = keyValue;
    tabf->select = found->second.tabs->selected_index
                       ? static_cast<int>(*found->second.tabs->selected_index)
                       : -1;
    for (const String& tabKey : found->second.tabKeys) {
        const auto tabIt = _tabs.find(tabKey);
        if (tabIt != _tabs.end())
            tabf->tabs.push_back(
                Tabi{tabKey, tabIt->second->text, tabIt->second->restoreKey});
    }
}

void DockingFolder::saveTree() {
    _tree = std::make_shared<Area>();
    if (_area && !_area->children().empty())
        saveTree(_area->children().front(), _tree->sash, _tree->tabf);
    for (const auto& sub : _subWindows) {
        SubWindow item;
        // Dは切り離したShellの位置を、ドックのローカル原点ではなく包含す
        // るShellからの相対位置で保存する。ALLの既存経路は維持する。
        const auto rootBounds = _comp ? _comp->bounds() : lib::Rect{};
        const auto shell = hostWindow();
        const bool useScreenOrigin = shell &&
            !lib::editor_window_host_options().all_mode;
        const auto origin = useScreenOrigin ? shell->bounds() : rootBounds;
        item.x = static_cast<int>(sub.shell->bounds().x - origin.x);
        item.y = static_cast<int>(sub.shell->bounds().y - origin.y);
        item.width = static_cast<int>(sub.shell->bounds().width);
        item.height = static_cast<int>(sub.shell->bounds().height);
        if (sub.area && !sub.area->children().empty())
            saveTree(sub.area->children().front(), item.area.sash, item.area.tabf);
        _tree->subShells.push_back(std::move(item));
    }
}

XNode DockingFolder::toNode(const std::vector<String>& exclude) const {
    XNode result = XNode::create("dockingFolder");
    const_cast<DockingFolder*>(this)->toNodeImpl(result, exclude);
    return result;
}

XNode DockingFolder::toNode(XNode& parent,
                            const std::vector<String>& exclude) const {
    XNode result = parent.newElement("dockingFolder");
    const_cast<DockingFolder*>(this)->toNodeImpl(result, exclude);
    return result;
}

void DockingFolder::toNodeImpl(XNode& node,
                               const std::vector<String>& exclude) const {
    const_cast<DockingFolder*>(this)->saveTree();
    if (!_cMemories.empty()) {
        XNode memories = node.newElement("controlMemories");
        for (const auto& [keyValue, memory] : _cMemories) {
            XNode item = memories.newElement("controlMemory", keyValue);
            item.newAttr("pane", memory.pane);
            item.newAttr("pairPane", memory.pairPane);
            item.newAttr("dir", dirToString(memory.dir));
            item.newAttr("lWeight", memory.lWeight);
            item.newAttr("rWeight", memory.rWeight);
        }
    }
    if (!_pMemories.empty()) {
        XNode memories = node.newElement("paneMemories");
        for (const auto& [keyValue, memory] : _pMemories) {
            XNode item = memories.newElement("paneMemory", keyValue);
            if (memory.isSubWindow) {
                item.newAttr("subPaneKey", memory.subPaneKey);
                item.newAttr("x", memory.x);
                item.newAttr("y", memory.y);
                item.newAttr("width", memory.width);
                item.newAttr("height", memory.height);
            } else {
                item.newAttr("pairPane", memory.pairPane);
                item.newAttr("dir", dirToString(memory.dir));
                item.newAttr("lWeight", memory.lWeight);
                item.newAttr("rWeight", memory.rWeight);
            }
        }
    }
    if (!_tree) return;
    if (_tree->sash) (void)toNodeImpl(node, _tree->sash, exclude);
    else if (_tree->tabf) (void)toNodeImpl(node, _tree->tabf, exclude);
    for (const auto& sub : _tree->subShells) {
        XNode item = node.newElement("subWindow");
        item.newAttr("x", sub.x);
        item.newAttr("y", sub.y);
        item.newAttr("width", sub.width);
        item.newAttr("height", sub.height);
        if (sub.area.sash) (void)toNodeImpl(item, sub.area.sash, exclude);
        else if (sub.area.tabf) (void)toNodeImpl(item, sub.area.tabf, exclude);
    }
}

XNode DockingFolder::toNodeImpl(XNode& parent,
                                const std::shared_ptr<Sashf>& sash,
                                const std::vector<String>& exclude) const {
    XNode item = parent.newElement("sash");
    item.newAttr("key", sash->key);
    item.newAttr("type", sash->vertical ? "vertical" : "horizontal");
    item.newAttr("lWeight", sash->lWeight);
    item.newAttr("rWeight", sash->rWeight);
    if (sash->lSash) (void)toNodeImpl(item, sash->lSash, exclude);
    else if (sash->lTabf) (void)toNodeImpl(item, sash->lTabf, exclude);
    if (sash->rSash) (void)toNodeImpl(item, sash->rSash, exclude);
    else if (sash->rTabf) (void)toNodeImpl(item, sash->rTabf, exclude);
    return item;
}

XNode DockingFolder::toNodeImpl(XNode& parent,
                                const std::shared_ptr<Tabf>& tabf,
                                const std::vector<String>& exclude) const {
    XNode item = parent.newElement("tabs");
    item.newAttr("key", tabf->key);
    if (tabf->select >= 0) item.newAttr("select", tabf->select);
    for (const auto& tabValue : tabf->tabs) {
        if (excluded(tabValue.key, exclude)) continue;
        XNode child = item.newElement("tab");
        child.newAttr("key", tabValue.key);
        child.newAttr("name", tabValue.name);
        if (!tabValue.restoreKey.empty() && tabValue.restoreKey != tabValue.key)
            child.newAttr("restoreKey", tabValue.restoreKey);
    }
    return item;
}

void DockingFolder::readMemories(const XNode& node) {
    for (const XNode& child : node.element_children()) {
        if (child.name() == "controlMemories") {
            for (const XNode& item : child.element_children()) {
                if (item.name() != "controlMemory") continue;
                CtrlMemory memory;
                const String keyValue = item.value();
                memory.pane = item.attr("pane", false, String{});
                memory.pairPane = item.attr("pairPane", false, String{});
                memory.dir = stringToDir(item.attr("dir", false, String{}));
                memory.lWeight = readInt(item, "lWeight", 1);
                memory.rWeight = readInt(item, "rWeight", 1);
                _cMemories[keyValue] = memory;
            }
        } else if (child.name() == "paneMemories") {
            for (const XNode& item : child.element_children()) {
                if (item.name() != "paneMemory") continue;
                PaneMemory memory;
                const String keyValue = item.value();
                memory.pairPane = item.attr("pairPane", false, String{});
                memory.isSubWindow = memory.pairPane.empty();
                if (memory.isSubWindow) {
                    memory.subPaneKey = item.attr("subPaneKey", false, keyValue);
                    memory.x = readInt(item, "x", -1);
                    memory.y = readInt(item, "y", -1);
                    memory.width = readInt(item, "width", -1);
                    memory.height = readInt(item, "height", -1);
                } else {
                    memory.dir = stringToDir(item.attr("dir", false, String{}));
                    memory.lWeight = readInt(item, "lWeight", 1);
                    memory.rWeight = readInt(item, "rWeight", 1);
                }
                _pMemories[keyValue] = memory;
            }
        }
    }
}

void DockingFolder::restoreNode(
    const XNode& node, const Composite& parent,
    const std::function<void(const XNode&, const TabF&)>& emptyPane) {
    if (node.name() == "tabs") {
        const String keyValue = node.attr("key", true);
        const bool resizeFirst = firstResize && firstResize(keyValue);
        const auto splitParent = std::dynamic_pointer_cast<lib::SplitView>(parent);
        const bool firstSplitChild = splitParent && splitParent->children().empty();
        const TabF tabs = newTabf(parent, keyValue);
        auto paneValue = pane(keyValue);
        // Dはペイン作成前にfirstResizeを問い合わせ、true
        // の結果は分割部がまだ子を持たない間だけ適用する。
        if (firstSplitChild && resizeFirst)
            setFirstResize(splitParent, resizeFirst);
        for (const XNode& item : node.element_children()) {
            if (item.name() != "tab") continue;
            if (emptyPane) emptyPane(item, tabs);
        }
        const int selectedIndex = readInt(node, "select", -1);
        if (selectedIndex >= 0 && paneValue &&
            !_panes[keyValue].tabKeys.empty())
            (void)lib::select_tab_page(
                *_panes[keyValue].tabs,
                static_cast<std::size_t>(std::min(
                    selectedIndex,
                    static_cast<int>(_panes[keyValue].tabKeys.size()) - 1)),
                false);
        return;
    }
    if (node.name() == "sash") {
        auto split = std::make_shared<lib::SplitView>();
        split->orientation = node.attr("type", false, String{}) == "vertical"
                                  ? lib::Orientation::Vertical
                                  : lib::Orientation::Horizontal;
        split->weights = {static_cast<float>(readInt(node, "lWeight", 1)),
                          static_cast<float>(readInt(node, "rWeight", 1))};
        const String sashKey = node.attr("key", false, newSashKey());
        putSashTable(split, sashKey);
        if (parent) {
            parent->add(split);
            registerParent(parent, split);
        }
        const auto children = node.element_children();
        for (const XNode& child : children)
            restoreNode(child, split, emptyPane);
        return;
    }
    if (node.name() == "subWindow") {
        Shell shell;
        Composite subArea;
        createSubWindow(shell, subArea);
        const auto children = node.element_children();
        if (!children.empty()) restoreNode(children.front(), subArea, emptyPane);
        if (!_subWindows.empty()) {
            auto& subWindow = _subWindows.back();
            for (const String& paneKey : _paneOrder) {
                const auto paneIt = _panes.find(paneKey);
                if (paneIt == _panes.end()) continue;
                Composite current = paneIt->second.pane;
                while (current && current != subArea) current = parentOf(current);
                if (current == subArea) {
                    paneIt->second.shell = shell;
                    subWindow.paneKeys.push_back(paneKey);
                }
            }
        }
        const int x = readInt(node, "x", -1);
        const int y = readInt(node, "y", -1);
        const int width = readInt(node, "width", -1);
        const int height = readInt(node, "height", -1);
        if (width >= 0 && height >= 0) {
            const lib::Rect localRootBounds = _comp ? _comp->bounds() : lib::Rect{};
            const auto parentShell = hostWindow();
            const bool useScreenOrigin = parentShell &&
                !lib::editor_window_host_options().all_mode;
            const auto shellBounds = useScreenOrigin ? parentShell->bounds()
                                                    : lib::Rect{};
            // 保存位置はShell相対。操作可能範囲への制限は、ド
            // ックのクライアント表示領域を画面座標へ移して使う。
            const lib::Rect rootBounds = useScreenOrigin
                ? lib::Rect{shellBounds.x + localRootBounds.x,
                            shellBounds.y + localRootBounds.y,
                            localRootBounds.width, localRootBounds.height}
                : localRootBounds;
            const float originX = useScreenOrigin ? shellBounds.x : rootBounds.x;
            const float originY = useScreenOrigin ? shellBounds.y : rootBounds.y;
            float restoredWidth = static_cast<float>(width);
            float restoredHeight = static_cast<float>(height);
            float restoredX = originX + static_cast<float>(std::max(0, x));
            float restoredY = originY + static_cast<float>(std::max(0, y));
            // 保存したペインは大きなモニター、別のDPI、大きなALL
            // 表示領域から来る場合がある。共通ホストの矩形で制限し
            // 、全OSでタイトル部と操作部へ到達できるようにする。
            if (rootBounds.width > 0.0F && rootBounds.height > 0.0F) {
                restoredWidth = std::clamp(restoredWidth, 1.0F, rootBounds.width);
                restoredHeight = std::clamp(restoredHeight, 1.0F, rootBounds.height);
                restoredX = std::clamp(
                    restoredX, rootBounds.x,
                    rootBounds.x + rootBounds.width - restoredWidth);
                restoredY = std::clamp(
                    restoredY, rootBounds.y,
                    rootBounds.y + rootBounds.height - restoredHeight);
            }
            shell->set_bounds(lib::Rect{restoredX, restoredY,
                                        restoredWidth, restoredHeight});
        }
        return;
    }
    for (const XNode& child : node.element_children())
        if (child.name() != "controlMemories" && child.name() != "paneMemories")
            restoreNode(child, parent, emptyPane);
}

std::shared_ptr<DockingFolder> DockingFolder::fromNode(
    const XNode& node, Composite parent, const int style,
    std::function<bool(StringView)> closeButton,
    CanVanish vanishHandler, CreateControl createControl,
    std::function<bool(StringView)> resizeHandler) {
    XNode root = node;
    if (root.name() != "dockingFolder") root = node.child("dockingFolder", false);
    if (!root.valid()) return {};
    auto result = std::make_shared<DockingFolder>(parent, style, false);
    // 復元コールバックやXMLハンドラーが例外を出した場合は、公開した途中の部分木を外す。
    ScopedRestoreAttachment restoreAttachment(parent, result->component());
    result->hasCloseButton = std::move(closeButton);
    result->canVanish = std::move(vanishHandler);
    result->firstResize = std::move(resizeHandler);
    result->readMemories(root);

    auto factory = [result, createControl](const XNode& item, const TabF& tabs) {
        if (!createControl) return;
        const String keyValue = item.attr("key", true);
        const String restoreKey = item.attr("restoreKey", false, keyValue);
        const String paneKey = [&]() {
            for (const auto& [candidate, state] : result->_panes)
                if (state.tabs == tabs) return candidate;
            return String{};
        }();
        const Pane paneValue = result->pane(paneKey);
        Control controlValue = createControl(paneValue, restoreKey);
        if (!controlValue) return;
        result->_parents[controlValue.get()] = paneValue;
        const String tabName = item.attr("name", false, keyValue);
        result->addImpl(std::move(controlValue), tabName,
                        0, String(keyValue), false, NewCtrlLocation::Last, true);
        (void)result->setRestoreKey(keyValue, restoreKey);
    };
    for (const XNode& child : root.element_children()) {
        if (child.name() != "controlMemories" && child.name() != "paneMemories")
            result->restoreNode(child, result->_area, factory);
    }
    if (result->_paneOrder.empty())
        (void)result->newTabf(result->_area, result->newTabfKey("t"));
    std::vector<String> emptyPanes;
    for (const String& paneKey : result->_paneOrder) {
        const auto paneIt = result->_panes.find(paneKey);
        if (paneIt != result->_panes.end() && paneIt->second.tabKeys.empty() &&
            paneIt->second.pane != result->_area && result->isVanishable(paneKey))
            emptyPanes.push_back(paneKey);
    }
    for (const String& paneKey : emptyPanes) {
        const auto paneIt = result->_panes.find(paneKey);
        if (paneIt != result->_panes.end())
            result->removeTabf(paneIt->second.tabs, true);
    }
    for (const auto& [keyValue, state] : result->_panes)
        if (state.tabs && state.tabs->selected_index) {
            result->selectTab(state.tabs);
            if (state.shell && !state.tabKeys.empty()) {
                state.shell->set_visible(true);
                (void)result->updateSubShellTitle(state.shell);
            }
        }
    restoreAttachment.release();
    return result;
}

DockingFolder::TabRecord* DockingFolder::tabOwner(
    const StringView keyValue) const {
    const auto found = _tabs.find(String(keyValue));
    return found == _tabs.end() ? nullptr : found->second.get();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::dockingfolder
