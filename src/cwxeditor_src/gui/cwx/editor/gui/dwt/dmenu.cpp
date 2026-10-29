#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace cwxeditor::cwx::editor::gui::dwt::dmenu {

namespace {

using customtext::detail::replaceSelection;

std::atomic_size_t next_menu_command{1U};

[[nodiscard]] std::string lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> selectionBounds(
    const lib::TextEdit& widget) noexcept {
    const auto length = widget.text.size();
    const auto start = std::min(length, widget.selection_start);
    const auto end = std::min(length, start + widget.selection_length);
    return {std::min(start, end), std::max(start, end)};
}

[[nodiscard]] std::pair<std::size_t, std::size_t> comboSelection(
    const std::shared_ptr<lib::ComboBox>& combo) noexcept {
    if (!combo) return {};
    if (const auto ccombo = std::dynamic_pointer_cast<CCombo>(combo)) {
        const auto start = std::min(ccombo->selection_start,
                                    ccombo->edit_text.size());
        const auto end = std::min(ccombo->edit_text.size(),
                                  start + ccombo->selection_length);
        return {std::min(start, end), std::max(start, end)};
    }
    if (combo->edit_selection) {
        const auto [start, end] = *combo->edit_selection;
        return {std::min(start, combo->edit_text.size()), std::min(end, combo->edit_text.size())};
    }
    if (combo->selected_index && !combo->edit_text.empty())
        return {0U, combo->edit_text.size()};
    return {};
}

[[nodiscard]] bool hasModifier(const std::uint32_t value,
                                const std::uint32_t modifier) noexcept {
    return (value & modifier) != 0U;
}

[[nodiscard]] std::uint32_t normalizeModifiers(std::uint32_t value) noexcept {
    // lib::Modifiersの下位ビットとアクセラレーターのビットの両
    // 方を受け付ける。前者はKeyEvent、後者はメニュー文字列が使う。
    std::uint32_t result = value & AcceleratorModifierMask;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U)
        result |= AcceleratorShift;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U)
        result |= AcceleratorControl;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U)
        result |= AcceleratorAlt;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Meta)) != 0U)
        result |= AcceleratorCommand;
    return result;
}

[[nodiscard]] std::uint32_t keyValue(const lib::Key key) noexcept {
    return static_cast<std::uint32_t>(key) & 0x00FFFFFFU;
}

[[nodiscard]] std::optional<lib::Key> keyFromToken(std::string_view value) {
    const auto token = lower(value);
    if (token.size() == 1U) {
        const auto character = token.front();
        if (character >= 'a' && character <= 'z') {
            return static_cast<lib::Key>(static_cast<int>(lib::Key::A) +
                                         character - 'a');
        }
        if (character >= '0' && character <= '9') {
            return static_cast<lib::Key>(static_cast<int>(lib::Key::Digit0) +
                                         character - '0');
        }
        if (character == ' ') return lib::Key::Space;
    }

    if (token == "backspace") return lib::Key::Backspace;
    if (token == "enter" || token == "return")
        return lib::Key::Enter;
    if (token == "delete") return lib::Key::Delete;
    if (token == "escape" || token == "esc") return lib::Key::Escape;
    if (token == "tab") return lib::Key::Tab;
    if (token == "space") return lib::Key::Space;
    if (token == "arrow_up" || token == "arrowup" || token == "up")
        return lib::Key::Up;
    if (token == "arrow_down" || token == "arrowdown" || token == "down")
        return lib::Key::Down;
    if (token == "arrow_left" || token == "arrowleft" || token == "left")
        return lib::Key::Left;
    if (token == "arrow_right" || token == "arrowright" || token == "right")
        return lib::Key::Right;
    if (token == "page_up" || token == "pageup") return lib::Key::PageUp;
    if (token == "page_down" || token == "pagedown") return lib::Key::PageDown;
    if (token == "home") return lib::Key::Home;
    if (token == "end") return lib::Key::End;
    if (token == "insert") return lib::Key::Insert;
    if (token.size() >= 2U && token.front() == 'f') {
        try {
            const auto number = std::stoi(token.substr(1U));
            if (number >= 1 && number <= 15 && token == "f" + std::to_string(number)) {
                return static_cast<lib::Key>(static_cast<int>(lib::Key::F1) +
                                             number - 1);
            }
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string keyName(const lib::Key key) {
    switch (key) {
    case lib::Key::Backspace: return "Backspace";
    case lib::Key::Tab: return "Tab";
    case lib::Key::Enter: return "Enter";
    case lib::Key::Escape: return "Esc";
    case lib::Key::Space: return "Space";
    case lib::Key::Insert: return "Insert";
    case lib::Key::Delete: return "Delete";
    case lib::Key::Home: return "Home";
    case lib::Key::End: return "End";
    case lib::Key::PageUp: return "Page_Up";
    case lib::Key::PageDown: return "Page_Down";
    case lib::Key::Left: return "Arrow_Left";
    case lib::Key::Right: return "Arrow_Right";
    case lib::Key::Up: return "Arrow_Up";
    case lib::Key::Down: return "Arrow_Down";
    case lib::Key::F1: case lib::Key::F2: case lib::Key::F3:
    case lib::Key::F4: case lib::Key::F5: case lib::Key::F6:
    case lib::Key::F7: case lib::Key::F8: case lib::Key::F9:
    case lib::Key::F10: case lib::Key::F11: case lib::Key::F12:
    case lib::Key::F13: case lib::Key::F14: case lib::Key::F15:
        return "F" + std::to_string(static_cast<int>(key) -
                                    static_cast<int>(lib::Key::F1) + 1);
    default: break;
    }
    const auto value = static_cast<int>(key);
    if (value >= static_cast<int>(lib::Key::A) &&
        value <= static_cast<int>(lib::Key::Z)) {
        return std::string(1, static_cast<char>('A' + value -
                                               static_cast<int>(lib::Key::A)));
    }
    if (value >= static_cast<int>(lib::Key::Digit0) &&
        value <= static_cast<int>(lib::Key::Digit9)) {
        return std::string(1, static_cast<char>('0' + value -
                                               static_cast<int>(lib::Key::Digit0)));
    }
    return {};
}

[[nodiscard]] bool itemCanBeUsed(const MenuItemRef item) {
    return item.valid() && menuEnabled(item);
}

void updateTextMenuLabels(const std::shared_ptr<lib::Menu>& menu,
                          const Presentation& prop) {
    if (!menu) return;
    for (auto& item : menu->items) {
        if (item.command == "undo") {
            item.text = prop.buildMenu(MenuID::Undo);
            item.icon = prop.imageMenu(MenuID::Undo);
        } else if (item.command == "redo") {
            item.text = prop.buildMenu(MenuID::Redo);
            item.icon = prop.imageMenu(MenuID::Redo);
        } else if (item.command == "cut") {
            item.text = prop.buildMenu(MenuID::Cut);
            item.icon = prop.imageMenu(MenuID::Cut);
        } else if (item.command == "copy") {
            item.text = prop.buildMenu(MenuID::Copy);
            item.icon = prop.imageMenu(MenuID::Copy);
        } else if (item.command == "paste") {
            item.text = prop.buildMenu(MenuID::Paste);
            item.icon = prop.imageMenu(MenuID::Paste);
        } else if (item.command == "delete") {
            item.text = prop.buildMenu(MenuID::Delete);
            item.icon = prop.imageMenu(MenuID::Delete);
        } else if (item.command == "select_all") {
            item.text = prop.buildMenu(MenuID::SelectAll);
            item.icon = prop.imageMenu(MenuID::SelectAll);
        }
        if (!item.children.empty()) {
            auto nested = std::make_shared<lib::Menu>(true);
            nested->items = item.children;
            updateTextMenuLabels(nested, prop);
            item.children = std::move(nested->items);
        }
    }
}

[[nodiscard]] bool isEditable(const std::shared_ptr<lib::Widget>& widget) {
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget))
        return !text->read_only;
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget))
        return combo->editable;
    return false;
}

void copyText(const std::shared_ptr<lib::TextEdit>& text,
              const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!text || !clipboard) return;
    const auto [start, end] = selectionBounds(*text);
    if (end <= start) return;
    clipboard->write({text->text.substr(start, end - start), {}, {}});
}

void cutText(const std::shared_ptr<lib::TextEdit>& text,
             const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!text || text->read_only) return;
    const auto [start, end] = selectionBounds(*text);
    if (end <= start) return;
    copyText(text, clipboard);
    text->text.erase(start, end - start);
    text->selection_start = start;
    text->selection_length = 0U;
    if (text->on_changed) text->on_changed(text->text);
}

void pasteText(const std::shared_ptr<lib::TextEdit>& text,
               const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!text || text->read_only || !clipboard) return;
    lib::ClipboardData data;
    data.text = clipboard->read_text();
    if (data.text.empty() && !clipboard->contains("text/plain") &&
        !clipboard->contains("text/plain;charset=utf-8"))
        return;
    replaceSelection(*text, data.text);
}

void eraseText(const std::shared_ptr<lib::TextEdit>& text) {
    if (!text || text->read_only) return;
    const auto [start, end] = selectionBounds(*text);
    if (end <= start) return;
    text->text.erase(start, end - start);
    text->selection_start = start;
    text->selection_length = 0U;
    if (text->on_changed) text->on_changed(text->text);
}

[[nodiscard]] std::string selectedComboText(
    const std::shared_ptr<lib::ComboBox>& combo, std::size_t& start,
    std::size_t& end) {
    const auto selection = comboSelection(combo);
    start = selection.first;
    end = selection.second;
    if (!combo || end <= start) return {};
    return combo->edit_text.substr(start, end - start);
}

void copyCombo(const std::shared_ptr<lib::ComboBox>& combo,
               const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!combo || !clipboard) return;
    std::size_t start{};
    std::size_t end{};
    const auto value = selectedComboText(combo, start, end);
    if (end > start) clipboard->write({value, {}, {}});
}

void cutCombo(const std::shared_ptr<lib::ComboBox>& combo,
              const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!combo || !combo->editable || !clipboard) return;
    std::size_t start{};
    std::size_t end{};
    if (selectedComboText(combo, start, end).empty()) return;
    copyCombo(combo, clipboard);
    combo->edit_text.erase(start, end - start);
    combo->edit_selection = std::pair{start, start};
    combo->selection_start = start;
    combo->selection_length = 0U;
    combo->selected_index.reset();
    if (combo->on_selection_changed) combo->on_selection_changed(std::nullopt);
}

void pasteCombo(const std::shared_ptr<lib::ComboBox>& combo,
                const std::shared_ptr<lib::ClipboardBackend>& clipboard) {
    if (!combo || !combo->editable || !clipboard) return;
    lib::ClipboardData data;
    data.text = clipboard->read_text();
    if (data.text.empty() && !clipboard->contains("text/plain") &&
        !clipboard->contains("text/plain;charset=utf-8"))
        return;
    std::size_t start{};
    std::size_t end{};
    (void)selectedComboText(combo, start, end);
    combo->edit_text.replace(start, end - start, data.text);
    combo->edit_selection = std::pair{start + data.text.size(), start + data.text.size()};
    combo->selection_start = start + data.text.size();
    combo->selection_length = 0U;
    combo->selected_index.reset();
    if (combo->on_selection_changed) combo->on_selection_changed(std::nullopt);
}

void eraseCombo(const std::shared_ptr<lib::ComboBox>& combo) {
    if (!combo || !combo->editable) return;
    const auto selection = comboSelection(combo);
    if (selection.second <= selection.first) return;
    combo->edit_text.erase(selection.first, selection.second - selection.first);
    combo->selection_start = selection.first;
    combo->selection_length = 0U;
    combo->edit_selection = std::pair{selection.first, selection.first};
    combo->selected_index.reset();
    if (combo->on_selection_changed) combo->on_selection_changed(std::nullopt);
}

[[nodiscard]] std::size_t itemAt(const ToolBar& bar, const lib::Point point) {
    for (std::size_t index = 0; index < bar.size(); ++index) {
        const auto* entry = bar.entry(index);
        if (!entry) continue;
        const auto& item = entry->bounds;
        if (point.x >= item.x &&
            point.x <= item.x + item.width &&
            point.y >= item.y &&
            point.y <= item.y + item.height)
            return index;
    }
    return bar.size();
}

}  // namespace

lib::String Presentation::buildMenu(const MenuID id) const {
    if (menuText) {
        auto result = menuText(id);
        if (formatMenu) result = formatMenu(id, std::move(result));
        return result;
    }
    try {
        return ::cwxeditor::cwx::enumutils::enumToString(id);
    } catch (const std::exception&) {
        return {};
    }
}

lib::String Presentation::buildTool(const MenuID id) const {
    if (toolText) {
        auto result = toolText(id);
        if (formatTool) result = formatTool(id, std::move(result));
        return result;
    }
    return buildMenu(id);
}

lib::ResourceId Presentation::imageMenu(const MenuID id) const {
    return menuImage ? menuImage(id) : lib::ResourceId{};
}

lib::ResourceId Presentation::imageTool(const MenuID id) const {
    return toolImage ? toolImage(id) : imageMenu(id);
}

TextMenuModify::TextMenuModify(TMM text, CanSaveHistory canSaveHistory,
                               std::shared_ptr<undo::UndoManager> undoManager,
                               TMAppendData appendData,
                               std::shared_ptr<lib::ClipboardBackend> clipboard)
    : text_(std::move(text)), undoManager_(std::move(undoManager)),
      implementation_(std::make_shared<customtext::TextMenuModify>(
          text_, std::move(canSaveHistory), undoManager_.get(),
          std::move(appendData), std::move(clipboard))) {
    implementation_->selectChanged = [this] {
        if (selectChanged) selectChanged();
    };
    implementation_->refreshToolBar = [this] {
        refresh();
        if (refreshToolBar) refreshToolBar();
    };
    implementation_->menu()->on_command = [this](std::string_view command) {
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "cut") cut();
        else if (command == "copy") copy();
        else if (command == "paste") paste();
        else if (command == "delete") erase();
        else if (command == "select_all") selectAll();
    };
}

TextMenuModify::~TextMenuModify() = default;

const TMM& TextMenuModify::text() const noexcept { return text_; }

std::shared_ptr<lib::Menu> TextMenuModify::menu() const noexcept {
    return implementation_->menu();
}

void TextMenuModify::refresh() {
    if (!implementation_) return;
    for (auto& item : implementation_->menu()->items) {
        if (item.command == "undo") item.enabled = canUndo();
        else if (item.command == "redo") item.enabled = canRedo();
        else if (item.command == "cut") item.enabled = canCut();
        else if (item.command == "copy") item.enabled = canCopy();
        else if (item.command == "paste") item.enabled = canPaste();
        else if (item.command == "delete") item.enabled = canDelete();
        else if (item.command == "select_all") item.enabled = canSelectAll();
    }
}

void TextMenuModify::reset() { implementation_->reset(); refresh(); }
void TextMenuModify::modifyText() { implementation_->modifyText(); refresh(); }
bool TextMenuModify::canUndo() const noexcept { return historyCanUndo ? historyCanUndo() : implementation_->canUndo(); }
bool TextMenuModify::canRedo() const noexcept { return historyCanRedo ? historyCanRedo() : implementation_->canRedo(); }
bool TextMenuModify::canCopy() const { return implementation_->canCopy(); }
bool TextMenuModify::canCut() const { return implementation_->canCut(); }
bool TextMenuModify::canPaste() const { return implementation_->canPaste(); }
bool TextMenuModify::canDelete() const { return implementation_->canDelete(); }
bool TextMenuModify::canSelectAll() const { return implementation_->canSelectAll(); }
bool TextMenuModify::undo() { const auto result = historyUndo ? historyUndo() : implementation_->undo(); refresh(); return result; }
bool TextMenuModify::redo() { const auto result = historyRedo ? historyRedo() : implementation_->redo(); refresh(); return result; }
void TextMenuModify::copy() { implementation_->copy(); refresh(); }
void TextMenuModify::cut() { implementation_->cut(); refresh(); }
void TextMenuModify::paste() { implementation_->paste(); refresh(); }
void TextMenuModify::erase() { implementation_->erase(); refresh(); }
void TextMenuModify::selectAll() { implementation_->selectAll(); refresh(); }

namespace {
struct TextMenuEntry {
    std::weak_ptr<lib::Widget> widget;
    std::weak_ptr<TextMenuModify> menu;
};
std::vector<TextMenuEntry>& textMenuRegistry() {
    static std::vector<TextMenuEntry> registry;
    return registry;
}
void registerTextMenu(const lib::WidgetPtr& widget, const TextMenuModifyPtr& menu) {
    auto& registry = textMenuRegistry();
    std::erase_if(registry, [&widget](const TextMenuEntry& entry) {
        const auto owner = entry.widget.lock();
        return !owner || entry.menu.expired() || owner == widget;
    });
    registry.push_back({widget, menu});
}
}  // namespace

TextMenuModifyPtr textMenuOf(const lib::WidgetPtr& widget) {
    if (!widget) return {};
    for (const auto& entry : textMenuRegistry())
        if (entry.widget.lock() == widget) return entry.menu.lock();
    return {};
}

namespace {
// installMissingTextMenusが作ったテキストメニュー。TMM
// とウィジェットのコールバックは弱参照だけを持つため、ここでメニューを所有
// してもウィジェットを生存させない。ウィジェットがなくなったら項目を外す。
struct OwnedTextMenu {
    std::weak_ptr<lib::Widget> widget;
    TextMenuModifyPtr menu;
};
std::vector<OwnedTextMenu>& ownedTextMenus() {
    static std::vector<OwnedTextMenu> menus;
    return menus;
}

void collectEditableTexts(const lib::WidgetPtr& widget,
                          std::vector<lib::WidgetPtr>& result) {
    if (!widget) return;
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget)) {
        if (!text->read_only) result.push_back(widget);
        return;
    }
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget)) {
        if (combo->editable) result.push_back(widget);
        return;
    }
    if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget)) {
        for (const auto& page : tabs->pages) collectEditableTexts(page.content, result);
        return;
    }
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
        for (const auto& child : container->children())
            collectEditableTexts(child, result);
}
}  // namespace

void installMissingTextMenus(const lib::WidgetPtr& root, const Commons& comm) {
    std::vector<lib::WidgetPtr> texts;
    collectEditableTexts(root, texts);
    auto& owned = ownedTextMenus();
    std::erase_if(owned, [](const OwnedTextMenu& entry) { return entry.widget.expired(); });
    for (const auto& widget : texts) {
        if (textMenuOf(widget)) continue;
        TextMenuModifyPtr menu;
        if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget))
            menu = createTextMenu(comm, TMM(text));
        else if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget))
            menu = createTextMenu(comm, TMM(combo));
        if (menu) owned.push_back({widget, std::move(menu)});
    }
}

TextMenuModifyPtr createTextMenu(Commons comm, const Presentation& prop,
                                 TMM text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    // DのウィジェットはネイティブMenuを所有する。共通コントロールは
    // TextMenu接続を保持し、ChooserやNameWindow
    // はネイティブのメニュー項目をコピーせず、既存・独自の行を共通モデ
    // ルへ結合する。読み取り専用でもコピー・全選択メニューは必要で、変
    // 更コマンドはTextMenuModifyの可否検査で無効にする。
    if (text.empty()) return {};
    if (const auto control = text.textWidget()) control->tab_width = prop.textTabs;
    lib::WidgetPtr undoControl = text.textWidget();
    if (!undoControl) undoControl = text.comboWidget();
    if (!undoManager) {
        comm.presentation.undoMaxEtc = prop.undoMaxEtc;
        // 共通の読み取り専用窓口はコピー・全選択を提供するが、Dは変更可能なTextMenuModify
        // を返さない。DがcreateEtcUndoを呼ばないこと
        // に合わせ、動作しない履歴には設定の監視接続を持たせない。
        undoManager = text.readOnly()
            ? std::make_shared<undo::UndoManager>(comm.effectiveUndoMax())
            : createEtcUndo(comm, undoControl);
    }
    auto result = std::make_shared<TextMenuModify>(
        std::move(text), std::move(canSaveHistory), std::move(undoManager),
        std::move(appendData), comm.clipboard());
    result->selectChanged = comm.refreshToolBar;
    result->refreshToolBar = comm.refreshToolBar;
    updateTextMenuLabels(result->menu(), prop);
    result->refresh();
    const auto previousOpen = result->menu()->on_open;
    result->menu()->on_open = [weak = std::weak_ptr<TextMenuModify>(result), previousOpen] {
        if (previousOpen) previousOpen();
        if (const auto owner = weak.lock()) owner->refresh();
    };
    // ALLバックエンドでは、メニューモデルだけでは入力に接続されない。履歴は
    // 呼び出し側が所有し、ウィジェットのコールバックには弱参照だけを保持する。
    lib::WidgetPtr widget = result->text().textWidget();
    if (!widget) widget = result->text().comboWidget();
    if (widget) registerTextMenu(widget, result);
    if (widget) {
        const std::weak_ptr<TextMenuModify> weak = result;
        const std::weak_ptr<lib::Widget> owner = widget;
        const auto previousPointer = widget->on_pointer;
        widget->on_pointer = [weak, owner, gui = comm.services.gui, previousPointer](lib::PointerEvent& event) {
            if (previousPointer) previousPointer(event);
            if (event.handled || !gui) return;
            if (!((event.button == lib::PointerButton::Secondary && event.action == lib::PointerAction::Down) ||
                  event.action == lib::PointerAction::LongPress)) return;
            if (const auto textMenu = weak.lock()) {
                textMenu->refresh();
                event.handled = true;
                gui->show_context_menu(textMenu->menu(), owner.lock(), event.position);
            }
        };
        const auto previousKey = widget->on_key_down;
        // TMM::textWidgetはTextEditとStyledTextの両方を
        // 扱う。編集可能なCombo/CComboは、意図的に既存のキー履歴を維持する。
        const bool native_text_widget =
            static_cast<bool>(result->text().textWidget());
        widget->on_key_down = [weak, previousKey, native_text_widget](lib::KeyEvent& event) {
            if (previousKey) previousKey(event);
            if (event.handled) return;
            const auto textMenu = weak.lock();
            if (!textMenu) return;
            const auto bits = static_cast<std::uint32_t>(event.modifiers);
            const bool command = (bits & static_cast<std::uint32_t>(lib::Modifiers::Control | lib::Modifiers::Meta)) != 0;
            const bool shift = (bits & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0;
            if (!command || (bits & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0) return;
            const bool local_text_keyboard_undo = native_text_widget &&
                textMenu->nativeTextKeyboardUndo;
            switch (event.key) {
            case lib::Key::Z:
                if (local_text_keyboard_undo) {
                    const auto local = textMenu->customText();
                    if (local) { if (shift) (void)local->redo(); else (void)local->undo(); }
                    // DのネイティブText/StyledTextと同様に、
                    // このキーの組は所有ダイアログの共通キー履歴へ流さない。
                    event.handled = true;
                    return;
                }
                if (shift) textMenu->redo(); else textMenu->undo();
                break;
            case lib::Key::Y:
                if (local_text_keyboard_undo) {
                    if (const auto local = textMenu->customText()) (void)local->redo();
                    event.handled = true;
                    return;
                }
                textMenu->redo();
                break;
            case lib::Key::C: textMenu->copy(); break;
            case lib::Key::X: textMenu->cut(); break;
            case lib::Key::V: textMenu->paste(); break;
            case lib::Key::A: textMenu->selectAll(); break;
            default: return;
            }
            event.handled = true;
        };
    }
    return result;
}

TextMenuModifyPtr createTextMenu(Commons comm, TMM text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    const auto prop = comm.presentation;
    return createTextMenu(std::move(comm), prop, std::move(text),
                          std::move(canSaveHistory), std::move(undoManager),
                          std::move(appendData));
}

TextMenuModifyPtr createTextMenu(Commons comm, const Presentation& prop,
                                 TextEditPtr text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    return createTextMenu(std::move(comm), prop, TMM(std::move(text)),
                          std::move(canSaveHistory), std::move(undoManager),
                          std::move(appendData));
}

TextMenuModifyPtr createTextMenu(Commons comm, const Presentation& prop,
                                 StyledTextPtr text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    return createTextMenu(std::move(comm), prop, TMM(std::move(text)),
                          std::move(canSaveHistory), std::move(undoManager),
                          std::move(appendData));
}

TextMenuModifyPtr createTextMenu(Commons comm, const Presentation& prop,
                                 ComboBoxPtr text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    return createTextMenu(std::move(comm), prop, TMM(std::move(text)),
                          std::move(canSaveHistory), std::move(undoManager),
                          std::move(appendData));
}

TextMenuModifyPtr createTextMenu(Commons comm, const Presentation& prop,
                                 CComboPtr text,
                                 TextMenuModify::CanSaveHistory canSaveHistory,
                                 UndoManagerPtr undoManager,
                                 TMAppendData appendData) {
    return createTextMenu(std::move(comm), prop, TMM(std::move(text)),
                          std::move(canSaveHistory), std::move(undoManager),
                          std::move(appendData));
}

UndoManagerPtr createEtcUndo(Commons comm, lib::WidgetPtr control) {
    auto undo = std::make_shared<undo::UndoManager>(comm.effectiveUndoMax());
    if (!control || !comm.subscribeUndoMax) return undo;
    struct Binding {
        lib::Action disconnect;
        ~Binding() { if (disconnect) disconnect(); }
    };
    const auto subscribe = comm.subscribeUndoMax;
    const auto limit = comm.undoLimit;
    const auto fallback = comm.undoMaxEtc.value_or(comm.presentation.undoMaxEtc);
    const std::weak_ptr<lib::Widget> owner = control;
    const std::weak_ptr<undo::UndoManager> history = undo;
    auto binding = std::make_shared<Binding>();
    binding->disconnect = subscribe([owner, history, limit, fallback] {
        if (owner.expired()) return;
        const auto retained = history.lock();
        if (!retained) return;
        const auto max = limit ? limit() : fallback;
        if (owner.expired()) return;
        retained->max(max);
    });
    control->retain_lifetime_binding(std::move(binding));
    return undo;
}

InTCPD::InTCPD(lib::Services services, TCPDPtr target)
    : tcpd(std::move(target)), services_(std::move(services)) {}

void InTCPD::setFocused(lib::WidgetPtr widget) noexcept { focused_ = std::move(widget); }
lib::WidgetPtr InTCPD::focused() const noexcept {
    if (auto widget = focused_.lock()) return widget;
    return services_.gui ? services_.gui->focused_widget() : lib::WidgetPtr{};
}

void InTCPD::cut(SelectionEvent& event) {
    const auto widget = focused();
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget);
        text && isEditable(widget)) {
        cutText(text, services_.clipboard);
        return;
    }
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget);
        combo && isEditable(widget)) {
        cutCombo(combo, services_.clipboard);
        return;
    }
    if (tcpd && tcpd->canDoT()) tcpd->cut(event);
}

void InTCPD::copy(SelectionEvent& event) {
    const auto widget = focused();
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget);
        text) {
        copyText(text, services_.clipboard);
        return;
    }
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget);
        combo) {
        copyCombo(combo, services_.clipboard);
        return;
    }
    if (tcpd && tcpd->canDoC()) tcpd->copy(event);
}

void InTCPD::paste(SelectionEvent& event) {
    const auto widget = focused();
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget);
        text && isEditable(widget)) {
        pasteText(text, services_.clipboard);
        return;
    }
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget);
        combo && isEditable(widget)) {
        pasteCombo(combo, services_.clipboard);
        return;
    }
    if (tcpd && tcpd->canDoP()) tcpd->paste(event);
}

void InTCPD::del(SelectionEvent& event) {
    const auto widget = focused();
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget);
        text && isEditable(widget)) {
        eraseText(text);
        return;
    }
    if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget);
        combo && isEditable(widget)) {
        eraseCombo(combo);
        return;
    }
    if (tcpd && tcpd->canDoD()) tcpd->del(event);
}

void InTCPD::cloneText(lib::Widget& widget) {
    if (auto text = dynamic_cast<lib::TextEdit*>(&widget); text && !text->read_only) {
        const auto [start, end] = selectionBounds(*text);
        if (end <= start) return;
        const auto selected = text->text.substr(start, end - start);
        text->text.insert(end, selected);
        text->selection_start = end;
        text->selection_length = selected.size();
        if (text->on_changed) text->on_changed(text->text);
        return;
    }
    if (auto combo = dynamic_cast<lib::ComboBox*>(&widget); combo && combo->editable) {
        std::size_t start{};
        std::size_t end{};
        if (const auto ccombo = dynamic_cast<CCombo*>(combo)) {
            start = std::min(ccombo->selection_start, combo->edit_text.size());
            end = std::min(combo->edit_text.size(),
                           start + ccombo->selection_length);
        } else if (combo->edit_selection) {
            start = std::min(combo->edit_selection->first, combo->edit_text.size());
            end = std::clamp(combo->edit_selection->second, start, combo->edit_text.size());
        } else if (combo->selected_index && !combo->edit_text.empty()) {
            end = combo->edit_text.size();
        }
        const auto selection = std::pair{std::min(start, end), std::max(start, end)};
        if (selection.second <= selection.first) return;
        const auto selected = combo->edit_text.substr(
            selection.first, selection.second - selection.first);
        combo->edit_text.insert(selection.second, selected);
        combo->selection_start = selection.second;
        combo->selection_length = selected.size();
        combo->edit_selection = std::pair{selection.second, selection.second + selected.size()};
        combo->selected_index.reset();
    if (combo->on_selection_changed) combo->on_selection_changed(std::nullopt);
    }
}

void InTCPD::clone(SelectionEvent& event) {
    if (const auto widget = focused()) {
        if (isEditable(widget)) {
            cloneText(*widget);
            return;
        }
    }
    if (tcpd && tcpd->canDoClone()) tcpd->clone(event);
}

MenuItemRef::MenuItemRef(Menu* owner, const std::size_t index) : owner_(owner) {
    if (owner && index < owner->items.size()) {
        command_ = owner->items[index].command;
        lifetime_ = owner->lifetime_.token;
    }
}
Menu* MenuItemRef::owner() const noexcept { return lifetime_.expired() ? nullptr : owner_; }
std::size_t MenuItemRef::index() const noexcept {
    const auto* menu = owner();
    if (!menu) return std::numeric_limits<std::size_t>::max();
    const auto found = std::find_if(menu->items.begin(), menu->items.end(), [this](const auto& item) { return item.command == command_; });
    return static_cast<std::size_t>(found - menu->items.begin());
}
bool MenuItemRef::valid() const noexcept { return owner() && index() < owner_->size() && owner_->entry(index()); }

lib::MenuItem& MenuItemRef::native() const {
    if (!valid()) throw std::out_of_range("invalid MenuItemRef");
    return owner_->items[index()];
}

MenuData& MenuItemRef::data() const {
    if (!valid()) throw std::out_of_range("invalid MenuItemRef");
    return owner_->entry(index())->data;
}

void MenuItemRef::setText(lib::String text) const {
    native().text = std::move(text);
    owner_->synchronizeToRoot();
}
void MenuItemRef::setImage(const lib::ResourceId image) const {
    native().icon = image;
    if (valid()) owner_->entry(index())->image = image;
    owner_->synchronizeToRoot();
}
void MenuItemRef::setEnabled(const bool value) const {
    native().enabled = value;
    owner_->synchronizeToRoot();
}
bool MenuItemRef::enabled() const { return valid() && native().enabled; }
void MenuItemRef::setChecked(const bool value) const {
    native().checked = value;
    owner_->synchronizeToRoot();
}
int MenuItemRef::style() const {
    if (!valid()) return MenuPush;
    const auto* value = owner_->entry(index());
    return value ? value->style : MenuPush;
}

void MenuItemRef::trigger(SelectionEvent event) const {
    if (!itemCanBeUsed(*this)) return;
    const auto at = index();
    auto* menu = owner();
    const auto revision = menu->revision_;
    const auto current = [this, menu, revision] { return valid() && menu->revision_ == revision; };
    const auto* entryValue = menu->entry(at);
    const auto selection = entryValue->selection;
    const auto action = entryValue->action;
    if ((entryValue->style & MenuRadio) != 0) {
        // SWTは隣接するRADIO項目だけを排他選択する。選択解除された兄弟
        // の変更を、この項目より先に通知する。DのGCは受信先を保持するが、C++
        // では所有元が終了・再構築され得るため、コールバックをコピーする。
        std::vector<MenuItemRef> siblings;
        for (auto i = at; i > 0 && (menu->entries_[i-1U].style & MenuRadio);) siblings.emplace_back(menu, --i);
        for (auto i = at + 1U; i < menu->entries_.size() && (menu->entries_[i].style & MenuRadio); ++i) siblings.emplace_back(menu, i);
        for (const auto& sibling : siblings) {
            if (!sibling.native().checked) continue;
            sibling.setChecked(false);
            const auto selected = menu->entry(sibling.index())->selection;
            const auto invoked = menu->entry(sibling.index())->action;
            auto notification = event; notification.itemIndex = sibling.index();
            if (selected) selected(notification); else if (invoked) invoked();
            if (!current()) return;
        }
        native().checked = true;
    } else if ((entryValue->style & MenuCheck) != 0) {
        native().checked = !native().checked;
    }
    menu->synchronizeToRoot();
    event.itemIndex = index();
    if (selection) selection(event); else if (action) action();
}

Menu::Menu(const bool context) : lib::Menu(context) {
    const std::weak_ptr<const int> life = lifetime_.token;
    on_command = [this, life](std::string_view command) { if (!life.expired()) dispatch(command); };
}
Menu::~Menu() {
    lifetime_.token.reset();
    for (auto& value : entries_) if (value.submenu) value.submenu->parent_ = nullptr;
}

MenuItemRef Menu::append(lib::String text, const lib::ResourceId image,
                         const int style, MenuData data,
                         SelectionHandler selection, lib::Action action,
                         const int index) {
    if (!lifetime_.token) return {};
    const auto insertAt = index < 0
                              ? items.size()
                              : std::min(items.size(), static_cast<std::size_t>(index));
    lib::MenuItem native;
    native.text = std::move(text);
    native.icon = image;
    native.separator = (style & MenuSeparator) != 0;
    native.checked = false;
    native.radio = (style & MenuRadio) != 0;
    native.enabled = true;
    // 共通・ネイティブのメニューツリーは、親へ同期した
    // 時点から実行可能にする。詳細なEntryをdmenu
    // 配送の元として保持し、ネイティブ側もMenuItem
    // から同じ所有操作を呼ぶ。Menu::dispatch
    // は全ツリーをこの文字列で識別する。Menuご
    // との連番では階層項目と最初の子（FileとFile
    // > Newなど）が同じコマンドになり、
    // 親で止まって子を実行できないため避ける。
    native.command = "dmenu.item." + std::to_string(
        next_menu_command.fetch_add(1U, std::memory_order_relaxed));
    Entry entryValue;
    entryValue.data = std::move(data);
    entryValue.image = image;
    entryValue.selection = std::move(selection);
    entryValue.action = std::move(action);
    entryValue.style = style;
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(insertAt),
                 std::move(native));
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(insertAt),
                    std::move(entryValue));
    ++revision_;
    const MenuItemRef item(this, insertAt);
    // ネイティブの項目単位・メニュー全体の実行で、同じ詳細な項目規則を共有する
    // 。コピーした項目も、チェック・ラジオ選択や弱い所有元の確認を迂回しない。
    items[insertAt].action = [item] { const auto retained = item; retained.trigger(); };
    items[insertAt].action_with_gesture = [item](lib::CommandGesture gesture) {
        const auto retained = item;
        SelectionEvent event;
        event.modifiers = gesture.modifiers;
        event.physical_control = gesture.physical_control;
        event.accelerator_modifiers = gesture.accelerator_modifiers;
        event.is_accelerator = gesture.is_accelerator;
        retained.trigger(event);
    };
    synchronizeToRoot();
    return item;
}

MenuItemRef Menu::appendSeparator(const int index) {
    return append({}, {}, MenuSeparator, {}, {}, {}, index);
}

void Menu::clearItems() {
    for (auto& value : entries_)
        if (value.submenu) value.submenu->parent_ = nullptr;
    entries_.clear();
    items.clear();
    ++revision_;
    synchronizeToRoot();
}

void Menu::retireBindings() {
    if (!lifetime_.token) return;
    // SWTの破棄と異なり、共有コントロールはShellより長く生存し得る
    // 。オブジェクトは有効に保つが、借用ホストの規則は永久に無効にする。
    lifetime_.token.reset();
    ++revision_;
    on_command = {};
    on_command_with_gesture = {};
    on_open = {};
    // 破棄は接続の最終境界。すでに終了中のホスト
    // へ、新しい可視性イベントとして通知しない。
    on_visibility_changed = {};
    std::vector<std::shared_ptr<Menu>> children;
    for (const auto& value : entries_) if (value.submenu) children.push_back(value.submenu);
    for (const auto& child : children) child->retireBindings();
    entries_.clear();
    items.clear();
    parent_ = nullptr;
    visible_ = false;
    set_visible(false);
}

Menu::Entry* Menu::entry(const std::size_t index) noexcept {
    return index < entries_.size() ? &entries_[index] : nullptr;
}
const Menu::Entry* Menu::entry(const std::size_t index) const noexcept {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

void Menu::refreshItems() {
    const ::cwxeditor::lib::EditorPerformanceScope performance("menu.capabilities");
    if (!lifetime_.token) return;
    const std::weak_ptr<const int> life = lifetime_.token;
    refreshItemsRecursive();
    if (!life.expired()) synchronizeToRoot();
}

void Menu::refreshItemsRecursive() {
    const std::weak_ptr<const int> life = lifetime_.token;
    const auto revision = revision_;
    const auto current = [this, life, revision] { return !life.expired() && revision_ == revision; };
    const auto count = entries_.size();
    for (std::size_t index = 0; index < count; ++index) {
        const auto submenu = entries_[index].submenu;
        const auto refresh = entries_[index].refresh;
        const auto enabled = entries_[index].data.enabled;
        if (submenu) submenu->refreshItemsRecursive();
        if (!current()) return;
        if (refresh) refresh();
        if (!current()) return;
        if (enabled) {
            const auto value = enabled();
            if (!current()) return;
            items[index].enabled = value;
        }
    }
    synchronizeNative();
}

void Menu::dispatch(const std::string_view command, SelectionEvent event) {
    if (!lifetime_.token) return;
    const std::weak_ptr<const int> life = lifetime_.token;
    const auto revision = revision_;
    const std::string selected(command); // 呼び出し側はネイティブ項目の文字列を渡す場合がある。
    synchronizeNative();
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].command == selected) {
            MenuItemRef(this, index).trigger(std::move(event));
            return;
        }
        if (const auto* value = entry(index); value && value->submenu) {
            const auto submenu = value->submenu;
            submenu->dispatch(selected, event);
            if (life.expired() || revision_ != revision) return;
            if (!event.doit) return;
        }
    }
}

void Menu::setSubmenu(const MenuItemRef item, std::shared_ptr<Menu> submenu) {
    if (!item.valid() || item.owner() != this) return;
    auto* value = entry(item.index());
    if (!value) return;
    if (value->submenu) value->submenu->parent_ = nullptr;
    value->submenu = std::move(submenu);
    if (value->submenu) value->submenu->parent_ = this;
    ++revision_;
    synchronizeToRoot();
}

void Menu::synchronizeNative() {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (index >= items.size()) break;
        auto& native = items[index];
        const auto& value = entries_[index];
        native.radio = (value.style & MenuRadio) != 0;
        native.separator = (value.style & MenuSeparator) != 0;
        if (value.submenu) {
            value.submenu->synchronizeNative();
            native.children = value.submenu->items;
        } else {
            native.children.clear();
        }
    }
}

void Menu::synchronizeToRoot() {
    for (auto* menu = this; menu; menu = menu->parent_) menu->synchronizeNative();
}

void MenuShown::menuShown() {
    if (!item_.valid()) return;
    const auto refresh = item_.owner()->entry(item_.index())->refresh;
    if (refresh) refresh();
    if (!item_.valid()) return;
    if (item_.native().icon == 0 && image_ != 0) item_.setImage(image_);
    (void)menuEnabled(item_);
}

void MenuSel::widgetSelected(SelectionEvent& event) const {
    if (selection_) selection_(event);
    else if (action_) action_();
}

void appendMenuTCPD(Commons comm, TopLevelPanel& panel, TCPDPtr tcpd,
                    const bool t, const bool c, const bool p, const bool d,
                    const bool clone) {
    if (!tcpd) return;
    // DのTopLevelPanelオーバーロードは、ペイン内のコマンドを登録す
    // る。これらのフラグが選ぶのは、下記の別のMenuオーバーロードの項目だけ。
    (void)t; (void)c; (void)p; (void)d; (void)clone;
    auto dispatcher = std::make_shared<InTCPD>(comm.services, tcpd);
    const auto add = [&](const MenuID id, const auto method, const auto can) {
        panel.putMenuAction(
            id,
            [dispatcher, method](const SelectionEvent& event) {
                auto selection = event;
                (dispatcher.get()->*method)(selection);
            },
            [tcpd, can] { return (tcpd.get()->*can)(); });
    };
    add(MenuID::Cut, &InTCPD::cut, &TCPD::canDoT);
    add(MenuID::Copy, &InTCPD::copy, &TCPD::canDoC);
    add(MenuID::Paste, &InTCPD::paste, &TCPD::canDoP);
    add(MenuID::Delete, &InTCPD::del, &TCPD::canDoD);
    add(MenuID::Clone, &InTCPD::clone, &TCPD::canDoClone);
}

void appendMenuTCPD(Commons comm, Menu& menu, TCPDPtr tcpd, const bool t,
                    const bool c, const bool p, const bool d,
                    const bool clone) {
    if (!tcpd) return;
    auto dispatcher = std::make_shared<InTCPD>(comm.services, tcpd);
    const auto add = [&](const MenuID id, const bool include,
                         const auto method, const auto can) {
        if (!include) return;
        createMenuItem(
            comm, menu, id,
            [dispatcher, method](SelectionEvent& event) {
                (dispatcher.get()->*method)(event);
            },
            [tcpd, can] { return (tcpd.get()->*can)(); });
    };
    add(MenuID::Cut, t, &InTCPD::cut, &TCPD::canDoT);
    add(MenuID::Copy, c, &InTCPD::copy, &TCPD::canDoC);
    add(MenuID::Paste, p, &InTCPD::paste, &TCPD::canDoP);
    add(MenuID::Delete, d, &InTCPD::del, &TCPD::canDoD);
    if (clone) {
        (void)menu.appendSeparator();
        (void)createMenuItem(
            comm, menu, MenuID::Clone,
            [dispatcher](SelectionEvent& event) { dispatcher->clone(event); },
            [tcpd] { return tcpd->canDoClone(); });
    }
    // メニューがコールバックのクロージャーを所有する。上記のクロージャ
    // ー内でディスパッチャーを保持し、生成された項目と寿命をそろえる。
}

bool eqAcc(const int acc, const int keyCode, const std::uint32_t character,
           const std::uint32_t stateMask) {
    const auto expected = static_cast<std::uint32_t>(acc);
    const auto modifiers = normalizeModifiers(stateMask) & AcceleratorModifierMask;
    auto key = static_cast<std::uint32_t>(keyCode);
    // 共通Key列挙値の印字キー・修飾キーを正規化し、Dと同じ文字スカラー値の大文字
    // 小文字変換と、修飾キーマスクのShiftビットを反転する比較規則を適用する。
    if (key >= static_cast<std::uint32_t>(lib::Key::A) && key <= static_cast<std::uint32_t>(lib::Key::Z))
        key = 'A' + key - static_cast<std::uint32_t>(lib::Key::A);
    else if (key >= static_cast<std::uint32_t>(lib::Key::Digit0) && key <= static_cast<std::uint32_t>(lib::Key::Digit9))
        key = '0' + key - static_cast<std::uint32_t>(lib::Key::Digit0);
    else if (key == static_cast<std::uint32_t>(lib::Key::Space)) key = ' ';
    else if (key == static_cast<std::uint32_t>(lib::Key::Shift)) key = AcceleratorShift;
    else if (key == static_cast<std::uint32_t>(lib::Key::Control)) key = AcceleratorControl;
    else if (key == static_cast<std::uint32_t>(lib::Key::Alt)) key = AcceleratorAlt;
    else if (key == static_cast<std::uint32_t>(lib::Key::Meta)) key = AcceleratorCommand;
    const auto upper = [](std::uint32_t value) { return static_cast<std::uint32_t>(::cwxeditor::cwx::enumutils::upperScalar(static_cast<char32_t>(value))); };
    const auto lower = [](std::uint32_t value) { return static_cast<std::uint32_t>(::cwxeditor::cwx::enumutils::lowerScalar(static_cast<char32_t>(value))); };
    if (key && (expected & AcceleratorModifierMask) == expected) return (key | modifiers) == expected;
    if (key && upper(key) == upper(character))
        return (upper(key) | modifiers) == expected || (lower(key) | modifiers) == expected;
    return (key && ((upper(key) | modifiers) == expected || (lower(key) | modifiers) == expected)) ||
           (upper(character) | (modifiers ^ AcceleratorShift)) == expected ||
           (lower(character) | (modifiers ^ AcceleratorShift)) == expected ||
           (upper(character) | modifiers) == expected || (lower(character) | modifiers) == expected;
}

lib::String acceleratorText(const int character) {
    if (character == 8) return "Backspace";
    if (character == 13) return "Enter";
    if (character == 127) return "Delete";
    if (character == 27) return "Esc";
    if (character == 9) return "Tab";
    if (character == ' ') return "Space";
    // Dのintオーバーロードは印字可能ASCIIの文字コードを受け付ける。共通Keyの
    // 序数への代替より先に解決する。F14/F15が感嘆符と二重引用符の値に重なるため。
    if (character >= 0x20 && character <= 0x7E)
        return std::string(1, static_cast<char>(
                                 std::toupper(static_cast<unsigned char>(character))));
    const auto value = static_cast<std::uint32_t>(character);
    if (value <= static_cast<std::uint32_t>(lib::Key::F15)) {
        const auto key = static_cast<lib::Key>(value);
        const auto name = keyName(key);
        if (!name.empty()) return name;
    }
    return {};
}

lib::String acceleratorText(const lib::Key key) { return keyName(key); }

std::uint32_t convertAccelerator2(const std::string_view accText) {
    const auto parsed = lib::parse_menu_shortcut(accText);
    std::uint32_t result = normalizeModifiers(static_cast<std::uint32_t>(parsed.modifiers));
    if (parsed.printable.size() == 1U)
        return result | static_cast<std::uint32_t>(static_cast<unsigned char>(parsed.printable.front()));
    if (parsed.key != lib::Key::Unknown)
        return result | (parsed.key == lib::Key::Space ? static_cast<std::uint32_t>(' ') : keyValue(parsed.key));
    return result;
}

std::uint32_t convertAccelerator(const std::string_view text) {
    const auto tab = text.rfind('\t');
    return tab != std::string_view::npos && tab + 1U < text.size()
               ? convertAccelerator2(text.substr(tab + 1U)) : 0U;
}

lib::Modifiers acceleratorModifiers(const std::uint32_t accelerator) noexcept {
    auto result = lib::Modifiers::None;
    if ((accelerator & AcceleratorShift) != 0U)
        result = result | lib::Modifiers::Shift;
    if ((accelerator & AcceleratorControl) != 0U)
        result = result | lib::Modifiers::Control;
    if ((accelerator & AcceleratorAlt) != 0U)
        result = result | lib::Modifiers::Alt;
    return result;
}

std::uint32_t acceleratorModifierMask(const lib::Modifiers modifiers) noexcept {
    const auto bits = static_cast<std::uint32_t>(modifiers);
    std::uint32_t result{};
    if ((bits & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U)
        result |= AcceleratorShift;
    if ((bits & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U)
        result |= AcceleratorControl;
    if ((bits & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U)
        result |= AcceleratorAlt;
    if ((bits & static_cast<std::uint32_t>(lib::Modifiers::Meta)) != 0U)
        result |= AcceleratorCommand;
    return result;
}

bool __unittest_L414_C3() {
    Menu root;
    auto submenu = std::make_shared<Menu>(true);
    const auto cascade = root.append("File", 0U, MenuCascade);
    bool child_selected{};
    const auto child = submenu->append(
        "New", 0U, MenuPush, {}, {}, [&child_selected] { child_selected = true; });
    root.setSubmenu(cascade, submenu);
    root.dispatch(child.native().command);

    return cascade.native().command != child.native().command && child_selected &&
           convertAccelerator("test\tCTRL+ARROW_UP") ==
               (AcceleratorControl | keyValue(lib::Key::Up)) &&
           convertAccelerator("test\tShift+A") ==
               (AcceleratorShift | static_cast<std::uint32_t>('A')) &&
           convertAccelerator("test\tCtrl+Shift+A") ==
               (AcceleratorControl | AcceleratorShift |
                static_cast<std::uint32_t>('A')) &&
           eqAcc(static_cast<int>(convertAccelerator2("Ctrl+Z")),
                 static_cast<int>(lib::Key::Z), 0U,
                 static_cast<std::uint32_t>(lib::Modifiers::Control)) &&
           eqAcc(static_cast<int>(convertAccelerator2("Ctrl+Shift+1")),
                 static_cast<int>(lib::Key::Digit1), 0U,
                 static_cast<std::uint32_t>(lib::Modifiers::Control |
                                            lib::Modifiers::Shift));
}

void addRefMenu(Commons comm, MenuItemRef item) {
    if (!item.valid()) return;
    const auto id = item.data().id;
    item.owner()->entry(item.index())->refresh = [comm, item, id] {
        if (!item.valid()) return;
        const auto format = item.data().format;
        auto text = comm.presentation.buildMenu(id);
        if (!item.valid()) return;
        if (format) text = format(std::move(text));
        if (!item.valid()) return;
        item.setText(std::move(text));
        if (item.native().icon == 0) {
            const auto image = comm.presentation.imageMenu(id);
            if (!item.valid()) return;
            item.setImage(image);
        }
    };
    const auto refresh = item.owner()->entry(item.index())->refresh;
    refresh();
}

void addRefMenu(Commons comm, ToolItemRef item) {
    if (!item.valid()) return;
    const auto id = item.data().id;
    item.owner()->entry(item.index())->refresh = [comm, item, id] {
        if (!item.valid()) return;
        const auto format = item.data().format;
        auto text = comm.presentation.buildTool(id);
        if (!item.valid()) return;
        if (format) text = format(std::move(text));
        if (!item.valid()) return;
        item.setTooltip(std::move(text));
        if (item.native().icon == 0) {
            const auto image = comm.presentation.imageTool(id);
            if (!item.valid()) return;
            item.setImage(image);
        }
    };
    const auto refresh = item.owner()->entry(item.index())->refresh;
    refresh();
}

MenuItemRef createMenuItemImpl(Commons comm, Menu& sub, lib::String text,
                               const lib::ResourceId image,
                               SelectionHandler selection, lib::Action action,
                               const int style, const MenuID id,
                               std::function<bool()> enabled, const int index) {
    MenuData data;
    data.id = id;
    data.enabled = std::move(enabled);
    const auto item = sub.append(std::move(text), image, style,
                                 std::move(data), std::move(selection),
                                 std::move(action), index);
    (void)menuEnabled(item);
    if (!item.valid()) return item;
    if (image != 0) MenuShown(item, image).menuShown();
    if (id != MenuID::None) addRefMenu(std::move(comm), item);
    return item;
}

MenuItemRef createMenuItem2(Commons comm, Menu& sub, lib::String text,
                            const lib::ResourceId image,
                            SelectionHandler selection,
                            std::function<bool()> enabled, const int style,
                            const int index) {
    return createMenuItemImpl(std::move(comm), sub, std::move(text), image,
                              std::move(selection), {}, style, MenuID::None,
                              std::move(enabled), index);
}

MenuItemRef createMenuItem2(Commons comm, Menu& sub, lib::String text,
                            const lib::ResourceId image, lib::Action action,
                            std::function<bool()> enabled, const int style,
                            const int index) {
    return createMenuItemImpl(std::move(comm), sub, std::move(text), image, {},
                              std::move(action), style, MenuID::None,
                              std::move(enabled), index);
}

MenuItemRef createMenuItem(Commons comm, Menu& sub, const MenuID id,
                           SelectionHandler selection,
                           std::function<bool()> enabled, const int style,
                           const int index) {
    return createMenuItemImpl(std::move(comm), sub, {}, {},
                              std::move(selection), {}, style, id,
                              std::move(enabled), index);
}

MenuItemRef createMenuItem(Commons comm, Menu& sub, const MenuID id,
                           lib::Action action, std::function<bool()> enabled,
                           const int style, const int index) {
    return createMenuItemImpl(std::move(comm), sub, {}, {}, {},
                              std::move(action), style, id,
                              std::move(enabled), index);
}

std::shared_ptr<Menu> createMenu2(Commons comm, Menu& bar, lib::String text,
                                  MenuItemRef& item) {
    (void)comm;
    auto submenu = std::make_shared<Menu>();
    item = bar.append(std::move(text), {}, MenuCascade);
    bar.setSubmenu(item, submenu);
    return submenu;
}

std::shared_ptr<Menu> createMenu(Commons comm, Menu& bar, const MenuID id) {
    const auto text = comm.presentation.buildMenu(id);
    MenuItemRef item;
    auto submenu = createMenu2(comm, bar, text, item);
    item.data().id = id;
    // IDの代入後に、プロバイダーとの接続処理を再実行する。
    addRefMenu(std::move(comm), item);
    return submenu;
}

ToolItemRef::ToolItemRef(ToolBar* owner, const std::size_t index) : owner_(owner), index_(index) {
    if (owner) lifetime_ = owner->refresh_lifetime_.token;
}
ToolBar* ToolItemRef::owner() const noexcept { return lifetime_.expired() ? nullptr : owner_; }
bool ToolItemRef::valid() const noexcept { return owner() && index_ < owner_->size() && owner_->entry(index_); }

lib::ToolItem& ToolItemRef::native() const {
    if (!valid()) throw std::out_of_range("invalid ToolItemRef");
    return owner_->items[index_];
}

MenuData& ToolItemRef::data() const {
    if (!valid()) throw std::out_of_range("invalid ToolItemRef");
    return owner_->entry(index_)->data;
}

void ToolItemRef::setText(lib::String text) const { native().text = std::move(text); }
void ToolItemRef::setTooltip(lib::String text) const {
    if (!valid()) return;
    // 共通ToolBarの表示モデルを最新に保つ。フレームのツールチップはlib::ToolItem::tooltip
    // を読み、dmenuは詳細なEntryを元データとして保持する。
    owner_->items[index_].tooltip = text;
    owner_->entry(index_)->tooltip = std::move(text);
}
void ToolItemRef::setImage(const lib::ResourceId image) const {
    native().icon = image;
    if (valid()) owner_->entry(index_)->image = image;
}
void ToolItemRef::setEnabled(const bool value) const { native().enabled = value; }
void ToolItemRef::setChecked(const bool value) const { native().checked = value; }
bool ToolItemRef::enabled() const { return valid() && native().enabled; }
void ToolItemRef::trigger(const bool arrow) const {
    if (!valid() || !enabled()) return;
    owner_->trigger(index_, arrow);
}

ToolBar::ToolBar() {
    // 共通バックエンドはコマンド文字列でlib::ToolItemを実行する
    // 。DWT項目の実際のコールバックはEntryにあるため、公開コマンドか
    // らtrigger()へ橋渡しし、追加項目が無操作にならないようにする。
    const std::weak_ptr<const int> life = refresh_lifetime_.token;
    on_command = [this, life](const std::string_view command) {
        if (life.expired()) return;
        constexpr std::string_view prefix = "cwx.dmenu.toolbar.";
        if (!command.starts_with(prefix) || command.size() == prefix.size()) return;
        std::size_t index{};
        for (const char character : command.substr(prefix.size())) {
            if (character < '0' || character > '9') return;
            index = index * 10U + static_cast<std::size_t>(character - '0');
            if (index >= items.size()) return;
        }
        trigger(index, false);
    };
}

void ToolBar::retireBindings() {
    if (!refresh_lifetime_.token) return;
    refresh_lifetime_.token.reset();
    ++refresh_revision_;
    on_command = {};
    on_command_with_gesture = {};
    on_pointer = {};
    on_key_down = {};
    on_key_up = {};
    on_visibility_changed = {};
    for (const auto& value : entries_) if (value.menu) value.menu->retireBindings();
    entries_.clear();
    items.clear();
    tooltip.visible = false;
    set_visible(false);
}

ToolItemRef ToolBar::append(lib::String tip, lib::String text,
                            const lib::ResourceId image, const int style,
                            MenuData data, SelectionHandler selection,
                            lib::Action action) {
    if (!refresh_lifetime_.token) return {};
    lib::ToolItem native;
    native.text = std::move(text);
    native.tooltip = tip;
    native.icon = image;
    native.enabled = true;
    native.drop_down = (style & ToolDropDown) != 0;
    native.separator = (style & MenuSeparator) != 0;
    native.checked = false;
    Entry entryValue;
    entryValue.data = std::move(data);
    entryValue.tooltip = std::move(tip);
    entryValue.image = image;
    entryValue.selection = std::move(selection);
    entryValue.action = std::move(action);
    entryValue.style = style;
    const auto index = items.size();
    if (!native.separator)
        native.command = "cwx.dmenu.toolbar." + std::to_string(index);
    entryValue.bounds = {static_cast<float>(index * 24U), 0.0F, 24.0F, 24.0F};
    items.push_back(std::move(native));
    entries_.push_back(std::move(entryValue));
    ++refresh_revision_;
    return ToolItemRef(this, index);
}

ToolBar::Entry* ToolBar::entry(const std::size_t index) noexcept {
    return index < entries_.size() ? &entries_[index] : nullptr;
}
const ToolBar::Entry* ToolBar::entry(const std::size_t index) const noexcept {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

void ToolBar::refreshItems(const std::map<MenuID, std::function<bool()>>& overrides,
                           std::function<bool()> continuation) {
    if (!refresh_lifetime_.token) return;
    const std::weak_ptr<const int> life = refresh_lifetime_.token;
    const auto revision = ++refresh_revision_;
    const auto current = [this, life, revision, continuation] {
        return !life.expired() && refresh_revision_ == revision &&
               (!continuation || continuation());
    };
    // C++のプロバイダーは所有元を同期的に終了・再構築し得る。DのGCは受信先
    // を保持するため、C++ではコールバックをコピーし、置換済みの処理を止める。
    for (std::size_t index = 0; index < entries_.size() && index < items.size(); ++index) {
        if (items[index].separator) continue;
        auto refresh = entries_[index].refresh;
        if (refresh) refresh();
        if (!current()) return;
        const auto id = entries_[index].data.id;
        auto enabled = entries_[index].data.enabled;
        try {
            std::optional<bool> value;
            if (const auto found = overrides.find(id); found != overrides.end())
                value = found->second();
            else if (enabled) value = enabled();
            if (!current()) return;
            if (value) items[index].enabled = *value;
        } catch (...) {
            if (!current()) return;
            ::cwxeditor::cwx::utils::debugln("toolbar enabled callback failed");
        }
        if (!current()) return;
        items[index].drop_down_menu = entries_[index].menu;
    }
}

void ToolBar::trigger(const std::size_t index, const bool arrow) {
    if (index >= entries_.size() || !items[index].enabled) return;
    const std::weak_ptr<const int> life = refresh_lifetime_.token;
    const auto revision = refresh_revision_;
    const auto current = [this, life, revision] { return !life.expired() && refresh_revision_ == revision; };
    SelectionEvent event;
    event.toolBar = this;
    event.itemIndex = index;
    event.arrow = arrow;
    const auto value = entries_[index];
    if (!arrow) {
        if ((value.style & MenuRadio) != 0) {
            // SWTのMenuItem/ToolItemと同じ、隣接ラジオ項目と
            // 変更された兄弟の通知契約。区切りや他のスタイルでグループを終える。
            std::vector<std::size_t> siblings;
            for (auto i = index; i > 0 && (entries_[i-1U].style & MenuRadio);) siblings.push_back(--i);
            for (auto i = index + 1U; i < entries_.size() && (entries_[i].style & MenuRadio); ++i) siblings.push_back(i);
            for (const auto sibling : siblings) {
                if (!items[sibling].checked) continue;
                items[sibling].checked = false;
                const auto selected = entries_[sibling].selection;
                const auto invoked = entries_[sibling].action;
                auto notification = event; notification.itemIndex = sibling;
                if (selected) selected(notification); else if (invoked) invoked();
                if (!current()) return;
            }
            items[index].checked = true;
        } else if ((value.style & MenuCheck) != 0) {
            items[index].checked = !items[index].checked;
        }
    }
    const auto opened = value.openArrow && value.openArrow(arrow);
    if (!current()) return;
    if (opened) {
        showDropDownMenu(ToolItemRef(this, index), value.menu);
        return;
    }
    if ((!value.action && !value.selection || arrow) && value.menu &&
        !value.menu->items.empty()) {
        showDropDownMenu(ToolItemRef(this, index), value.menu);
        return;
    }
    if (value.selection) value.selection(event);
    else if (value.action) value.action();
}

void ToolBar::installTooltipHandler(const std::size_t width,
                                     lib::GraphicsBackend* graphics) {
    tooltip_width_provider=[width] { return float(width); };
    on_pointer = [this, width, graphics](lib::PointerEvent& event) {
        if (event.action == lib::PointerAction::Leave ||
            event.action == lib::PointerAction::Cancel) {
            tooltip.visible = false;
            return;
        }
        const auto index = itemAt(*this, event.position);
        if (index >= size()) {
            tooltip.visible = false;
            return;
        }
        const auto& tip = entries_[index].tooltip;
        const auto separator = tip.find('\v');
        if (separator == std::string::npos) {
            tooltip.visible = false;
            return;
        }
        tooltip.visible = true;
        tooltip.itemIndex = index;
        tooltip.image = entries_[index].image;
        tooltip.name = tip.substr(0U, separator);
        tooltip.description = tip.substr(separator + 1U);
        tooltip.width = width;
        if (graphics) {
            const auto measured = graphics->measure_text(
                tooltip.description, {}, static_cast<float>(width));
            if (std::isfinite(measured.width) && measured.width > 0.0F)
                tooltip.width = std::min(width, static_cast<std::size_t>(measured.width));
        }
        const auto bounds = entries_[index].bounds;
        tooltip.bounds = {bounds.x, bounds.y + bounds.height,
                          static_cast<float>(tooltip.width), bounds.height};
    };
}

void ToolSel::widgetSelected(SelectionEvent& event) const {
    if (!handler_) return;
    if (event.toolBar) handler_(ToolItemRef(event.toolBar, event.itemIndex));
}

ToolItemRef createDropDownItem(Commons comm, ToolBar& bar, const MenuID id,
                               lib::Action action, std::shared_ptr<Menu>& menu,
                               std::function<bool()> enabled,
                               std::function<bool(bool)> openArrow) {
    auto item = createDropDownItem2(
        comm, bar, comm.presentation.buildTool(id), comm.presentation.imageTool(id),
        std::move(action), menu, id, std::move(enabled), std::move(openArrow));
    addRefMenu(std::move(comm), item);
    return item;
}

ToolItemRef createDropDownItem2(Commons comm, ToolBar& bar, lib::String text,
                                const lib::ResourceId image, lib::Action action,
                                std::shared_ptr<Menu>& menu, const MenuID id,
                                std::function<bool()> enabled,
                                std::function<bool(bool)> openArrow) {
    menu = std::make_shared<Menu>(true);
    MenuData data;
    data.id = id;
    data.enabled = std::move(enabled);
    const auto tip = text;
    auto item = bar.append(tip, std::move(text), image, ToolDropDown,
                           std::move(data), {}, std::move(action));
    bar.entry(item.index())->menu = menu;
    bar.entry(item.index())->openArrow = std::move(openArrow);
    const auto predicate = item.data().enabled;
    if (predicate) {
        const auto value = predicate();
        if (!item.valid()) return item;
        item.setEnabled(value);
    }
    (void)comm;
    return item;
}

void showDropDownMenu(const ToolItemRef item, const std::shared_ptr<Menu>& menu) {
    if (!item.valid() || !menu || menu->items.empty()) return;
    menu->refreshItems();
    if (!item.valid()) return;
    const auto bounds = item.owner()->entry(item.index())->bounds;
    menu->setLocation({bounds.x, bounds.y + bounds.height});
    menu->setVisible(true);
}

ToolItemRef createToolItemImpl(Commons comm, ToolBar& bar, lib::String tip,
                               lib::String text, const lib::ResourceId image,
                               SelectionHandler selection, lib::Action action,
                               const int style, const MenuID id,
                               std::function<bool()> enabled) {
    MenuData data;
    data.id = id;
    data.enabled = std::move(enabled);
    auto item = bar.append(std::move(tip), std::move(text), image, style,
                           std::move(data), std::move(selection),
                           std::move(action));
    const auto predicate = item.data().enabled;
    if (predicate) {
        const auto value = predicate();
        if (!item.valid()) return item;
        item.setEnabled(value);
    }
    if (id != MenuID::None) addRefMenu(std::move(comm), item);
    return item;
}

ToolItemRef createToolItem2(Commons comm, ToolBar& bar, lib::String tip,
                            lib::String text, const lib::ResourceId image,
                            SelectionHandler selection,
                            std::function<bool()> enabled, const int style) {
    return createToolItemImpl(std::move(comm), bar, std::move(tip),
                              std::move(text), image, std::move(selection), {},
                              style, MenuID::None, std::move(enabled));
}

ToolItemRef createToolItem2(Commons comm, ToolBar& bar, lib::String tip,
                            lib::String text, const lib::ResourceId image,
                            lib::Action action,
                            std::function<bool()> enabled, const int style) {
    return createToolItemImpl(std::move(comm), bar, std::move(tip),
                              std::move(text), image, {}, std::move(action),
                              style, MenuID::None, std::move(enabled));
}

ToolItemRef createToolItem2(Commons comm, ToolBar& bar, lib::String text,
                            const lib::ResourceId image,
                            SelectionHandler selection,
                            std::function<bool()> enabled, const int style) {
    return createToolItem2(std::move(comm), bar, text, {}, image,
                           std::move(selection), std::move(enabled), style);
}

ToolItemRef createToolItem2(Commons comm, ToolBar& bar, lib::String text,
                            const lib::ResourceId image, lib::Action action,
                            std::function<bool()> enabled, const int style) {
    return createToolItem2(std::move(comm), bar, text, {}, image,
                           std::move(action), std::move(enabled), style);
}

ToolItemRef createToolItem2(Commons comm, ToolBar& bar, lib::String tip,
                            lib::String text, const lib::ResourceId image,
                            ToolSel::Handler handler,
                            std::function<bool()> enabled, const int style) {
    return createToolItemImpl(std::move(comm), bar, std::move(tip),
                              std::move(text), image,
                              [tool = ToolSel(std::move(handler))](SelectionEvent& event) mutable {
                                  tool.widgetSelected(event);
                              }, {}, style, MenuID::None, std::move(enabled));
}

ToolItemRef createToolItem(Commons comm, ToolBar& bar, const MenuID id,
                           ToolSel::Handler handler,
                           std::function<bool()> enabled, const int style) {
    return createToolItemImpl(
        comm, bar, comm.presentation.buildTool(id), {},
        comm.presentation.imageTool(id),
        [tool = ToolSel(std::move(handler))](SelectionEvent& event) mutable {
            tool.widgetSelected(event);
        },
        {}, style, id, std::move(enabled));
}

ToolItemRef createToolItem(Commons comm, ToolBar& bar, const MenuID id,
                           SelectionHandler selection,
                           std::function<bool()> enabled, const int style) {
    return createToolItemImpl(
        comm, bar, comm.presentation.buildTool(id), {},
        comm.presentation.imageTool(id), std::move(selection), {}, style, id,
        std::move(enabled));
}

ToolItemRef createToolItem(Commons comm, ToolBar& bar, const MenuID id,
                           lib::Action action,
                           std::function<bool()> enabled, const int style) {
    return createToolItemImpl(
        comm, bar, comm.presentation.buildTool(id), {},
        comm.presentation.imageTool(id), {}, std::move(action), style, id,
        std::move(enabled));
}

PortableCoolBar::PortableCoolBar() {
    set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
}

void PortableCoolBar::applyItemOrder(std::vector<std::size_t> order) {
    if (order.size() != coolItems.size()) throw std::invalid_argument("CoolBar order length");
    std::vector<bool> seen(order.size());
    for (const auto index : order) {
        if (index >= seen.size() || seen[index]) throw std::invalid_argument("CoolBar order permutation");
        seen[index] = true;
    }
    itemOrder = std::move(order);
    clear();
    for (const auto index : itemOrder) if (coolItems[index]->control) add(coolItems[index]->control);
}

void PortableCoolBar::applyWrapIndices(std::vector<std::size_t> indices) {
    std::erase_if(indices, [count = coolItems.size()](auto i) { return i == 0 || i >= count; });
    wrapIndices = std::move(indices);
    wrap_indices = wrapIndices;
    ++layout_update_revision;
}

void PortableCoolBar::itemMoved(std::size_t from, std::size_t to) {
    if (from >= itemOrder.size() || to >= itemOrder.size()) return;
    const auto id = itemOrder[from];
    itemOrder.erase(itemOrder.begin() + static_cast<std::ptrdiff_t>(from));
    itemOrder.insert(itemOrder.begin() + static_cast<std::ptrdiff_t>(to), id);
}

CoolBarHost::CoolBarHost() {
    set_layout(std::make_shared<lib::FillLayout>());
}

CoolBarHost::~CoolBarHost() { retireBindings(); }

void CoolBarHost::retireBindings() {
    if (retired_) return;
    retired_ = true;
    const auto bar = coolBar;
    const auto popup = menu;
    const auto publish = presentation.saveCoolBarState;
    const auto persistOrder = saveOrder;
    CoolBarState state;
    if (bar) {
        state.locked = bar->locked;
        state.wrapIndices = getWrapIndices2(*bar);
        if (persistOrder) {
            state.order = bar->itemOrder;
            bool identity = true;
            for (std::size_t i = 0; i < state.order.size(); ++i) identity &= state.order[i] == i;
            if (identity) state.order.clear();
        }
        presentation.coolBarLocked = state.locked;
        presentation.coolBarWrapIndices = state.wrapIndices;
        if (persistOrder) presentation.coolBarOrder = state.order;
        bar->on_pointer = {};
        bar->on_item_moved = {};
        bar->menu.reset();
    }
    if (popup) popup->retireBindings();
    set_child_layout_provider({});
    on_paint_context = {};
    on_pointer = {};
    listener.reset();
    // 外部への設定公開は最後に行う。ホストを破棄したり再入したりする場合があるため。
    if (bar && publish) publish(state, persistOrder);
}

std::vector<std::size_t> getWrapIndices2(const PortableCoolBar& bar) {
    std::vector<std::size_t> result;
    float previousY = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < bar.children().size(); ++index) {
        const auto& child = bar.children()[index];
        if (!child) continue;
        if (child->bounds().y > previousY) {
            result.push_back(index);
            previousY = child->bounds().y;
        }
    }
    return result;
}

CoolBarListener::CoolBarListener(const Presentation& presentation,
                                 std::shared_ptr<PortableCoolBar> coolBar,
                                 const bool save)
    : prop(presentation), cbar(coolBar), saveOrder(save) {
    wrapIndices = coolBar ? coolBar->wrap_indices : std::vector<std::size_t>{};
}

std::shared_ptr<PortableCoolBar> CoolBarListener::activeBar() const {
    if (disposed_) return {};
    if (hostBound_) {
        const auto owner = host.lock();
        if (!owner || owner->retired()) return {};
    }
    return cbar.lock();
}

void CoolBarListener::widgetDisposed() {
    if (disposed_) return;
    disposed_ = true;
    if (const auto owner = host.lock()) {
        owner->retireBindings();
        return;
    }
    const auto bar = cbar.lock();
    cbar.reset();
    if (!bar || hostBound_) return;
    const auto publish = prop.saveCoolBarState;
    CoolBarState state{bar->locked, {}, getWrapIndices2(*bar)};
    if (saveOrder) {
        state.order = bar->itemOrder;
        bool identity = true;
        for (std::size_t i=0; i<state.order.size(); ++i) identity &= state.order[i] == i;
        if (identity) state.order.clear();
    }
    if (publish) publish(state, saveOrder);
}

void CoolBarListener::itemMoved(std::size_t from, std::size_t to) {
    const auto bar = activeBar();
    if (!bar) return;
    bar->itemMoved(from, to);
    resizeAll();
}

void CoolBarListener::mouseMove() {
    const auto bar = activeBar();
    if (!bar) return;
    const auto current = bar->wrap_indices;
    if (current != wrapIndices) {
        wrapIndices = current;
        bar->wrapIndices = current;
        ++bar->layout_update_revision;
    }
}

void CoolBarListener::mouseMove(const lib::PointerEvent& event) {
    (void)event;
    mouseMove();
}

void CoolBarListener::reset() {
    const auto bar = activeBar();
    if (!bar) return;
    std::vector<std::size_t> order(bar->coolItems.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    bar->applyItemOrder(std::move(order));
    bar->applyWrapIndices(prop.coolBarInitialWrapIndices);
    for (auto i = bar->itemOrder.rbegin(); i != bar->itemOrder.rend(); ++i)
        resetCISize(*bar->coolItems[*i], bar.get());
    resizeAll();
}

void CoolBarListener::resizeAll() {
    const auto bar = activeBar();
    if (!bar) return;
    std::vector<bool> wrap(bar->coolItems.size() + 1U, false);
    for (const auto index : bar->wrap_indices)
        if (index < wrap.size()) wrap[index] = true;
    wrap[bar->coolItems.size()] = true;
    for (std::size_t index = 0; index < bar->itemOrder.size(); ++index) {
        if (!wrap[index + 1U]) {
            const auto item = bar->coolItems[bar->itemOrder[index]];
            // DのsetSize(getMinimumSize())は現在の割り当てサ
            // イズだけを変える。resetCISizeが設定した理想サイズは維持する。
            item->currentSize = item->minimumSize;
        }
    }
    ++bar->layout_update_revision;
}

void CoolBarListener::lock() {
    const auto bar = activeBar();
    if (!bar) return;
    bar->locked = !bar->locked;
    if (lockItem.valid()) lockItem.setChecked(bar->locked);
    for (auto i = bar->itemOrder.rbegin(); i != bar->itemOrder.rend(); ++i)
        resetCISize(*bar->coolItems[*i], bar.get());
    ++bar->layout_update_revision;
}

std::shared_ptr<CoolBarHost> createCoolBar(
    Commons comm, lib::Container& parent, const bool saveOrder,
    std::function<void(lib::Container&)> setupItems,
    std::function<void(Menu&)> putMenu) {
    auto host = std::make_shared<CoolBarHost>();
    host->presentation = comm.presentation;
    host->saveOrder = saveOrder;
    parent.add(host);
    struct ParentBinding {
        std::weak_ptr<CoolBarHost> owner;
        ~ParentBinding() { if (const auto host = owner.lock()) host->retireBindings(); }
    };
    auto parentBinding = std::make_shared<ParentBinding>();
    parentBinding->owner = host;
    parent.retain_lifetime_binding(std::move(parentBinding));
    const auto bindPopup = [gui = comm.services.gui](const std::shared_ptr<CoolBarHost>& owner) {
        const std::weak_ptr<CoolBarHost> hostLife = owner;
        const auto bind = [&](const lib::WidgetPtr& control) {
            if (!control) return;
            const auto previous = control->on_pointer;
            const std::weak_ptr<lib::Widget> widget = control;
            control->on_pointer = [hostLife, widget, gui, previous](lib::PointerEvent& event) {
                const auto host = hostLife.lock();
                if (!host || host->retired()) return;
                if (previous) previous(event);
                if (event.handled || host->retired() || !gui) return;
                if (!((event.button == lib::PointerButton::Secondary && event.action == lib::PointerAction::Down) ||
                      event.action == lib::PointerAction::LongPress)) return;
                const auto popup = host->menu;
                if (!popup) return;
                popup->refreshItems();
                if (host->retired()) return;
                event.handled = true;
                gui->show_context_menu(popup, widget.lock(), event.position);
            };
        };
        bind(owner);
        if (owner->coolBar) {
            bind(owner->coolBar);
            for (const auto& control : owner->coolBar->children()) bind(control);
        } else {
            for (const auto& control : owner->children()) bind(control);
        }
    };
    if (!comm.presentation.useCoolBar) {
        host->nativeCoolBar = false;
        auto row = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        row->padding = {1,1,1,1}; row->spacing = 3;
        host->set_layout(std::move(row));
        if (setupItems) setupItems(*host);
        if (host->retired()) return host;
        const std::weak_ptr<CoolBarHost> weakHost = host;
        host->set_scaled_child_layout_provider([weakHost](lib::Rect available, float scale) {
            std::vector<lib::PreparedChildBounds> result;
            const auto owner = weakHost.lock();
            if (!owner || owner->retired()) return result;
            const auto children = owner->children();
            const float margin = scale, spacing = 3.0F * scale;
            float x = margin, y = margin, rowHeight{};
            const float room = std::max(0.0F, available.width - 2.0F * margin);
            // SWTのRowLayoutに合わせ、折り返し・詰め込み、余白1、間隔3、中央揃えなしとする。
            for (const auto& child : children) {
                if (!child || !child->visible()) continue;
                auto size = lib::preferred_ui_size(child);
                size.width *= scale; size.height *= scale;
                if (owner->retired()) return std::vector<lib::PreparedChildBounds>{};
                if (x > margin && x - margin + size.width > room) {
                    x = margin; y += rowHeight + spacing; rowHeight = 0.0F;
                }
                result.push_back({child, {available.x + x, available.y + y, size.width, size.height}});
                x += size.width + spacing;
                rowHeight = std::max(rowHeight, size.height);
            }
            return result;
        });
        host->on_paint_context = [weakHost](lib::DrawingContext& gc, lib::Rect area) {
            const auto owner = weakHost.lock();
            if (!owner || owner->retired()) return;
            const auto children = owner->children();
            const auto origin = owner->bounds();
            float x = -std::numeric_limits<float>::infinity();
            float y = x;
            // 操作部と共有する、共通のシステムの通常ウィジェット影色。
            const lib::Color shadow{145,145,145,255};
            for (const auto& child : children) {
                if (!child) continue;
                auto b = child->bounds(); b.x -= origin.x; b.y -= origin.y;
                if (std::isfinite(x) && x < b.x) {
                    gc.draw_line({b.x-2,b.y-1},{b.x-2,b.y+b.height+1},shadow,1.0F,lib::StrokeStyle::Solid);
                    if (owner->retired()) return;
                }
                x = b.x;
                if (std::isfinite(y) && y < b.y) {
                    gc.draw_line({0,b.y-2},{area.width,b.y-2},shadow,1.0F,lib::StrokeStyle::Solid);
                    if (owner->retired()) return;
                    x = -std::numeric_limits<float>::infinity();
                }
                y = b.y;
            }
        };
        host->menu = std::make_shared<Menu>(true);
        if (putMenu) putMenu(*host->menu);
        if (host->retired()) return host;
        bindPopup(host);
        return host;
    }

    host->nativeCoolBar = true;
    host->presentation = comm.presentation;
    host->coolBar = std::make_shared<PortableCoolBar>();
    host->add(host->coolBar);
    if (setupItems) setupItems(*host->coolBar);
    if (host->retired()) return host;

    auto& bar = *host->coolBar;
    bar.itemOrder.resize(bar.coolItems.size());
    for (std::size_t index = 0; index < bar.itemOrder.size(); ++index)
        bar.itemOrder[index] = index;
    if (saveOrder && comm.presentation.coolBarOrder.size() == bar.coolItems.size())
        bar.applyItemOrder(comm.presentation.coolBarOrder);
    bar.applyWrapIndices(comm.presentation.coolBarWrapIndices);
    bar.locked = comm.presentation.coolBarLocked;

    auto listener = std::make_shared<CBarListener<void>>(
        host->presentation, host->coolBar, saveOrder);
    listener->bindHost(host);
    listener->wrapIndices = bar.wrapIndices;
    host->listener = listener;
    const std::weak_ptr<CoolBarListener> weakListener = listener;
    bar.on_pointer = [weakListener](lib::PointerEvent& event) {
        if (event.action == lib::PointerAction::Move)
            if (const auto owner = weakListener.lock()) owner->mouseMove(event);
    };
    bar.on_item_moved = [weakListener](std::size_t from, std::size_t to) {
        if (const auto owner = weakListener.lock()) owner->itemMoved(from, to);
    };

    host->menu = std::make_shared<Menu>(true);
    if (putMenu) {
        putMenu(*host->menu);
        if (host->retired()) return host;
        if (!host->menu->items.empty()) (void)host->menu->appendSeparator();
    }
    listener->lockItem = createMenuItem(
        comm, *host->menu, MenuID::LockToolBar,
        [weakListener](SelectionEvent&) { if (const auto owner = weakListener.lock()) owner->lock(); },
        {}, MenuCheck);
    listener->lockItem.setChecked(bar.locked);
    (void)host->menu->appendSeparator();
    (void)createMenuItem(comm, *host->menu, MenuID::ResetToolBar,
                          [weakListener](SelectionEvent&) { if (const auto owner = weakListener.lock()) owner->reset(); });
    bar.menu = host->menu;
    listener->resizeAll();
    bindPopup(host);
    return host;
}

void resetCISize(CoolItem& item, const lib::CoolBar* bar) {
    if (!item.control) return;
    // SWTのcomputeSizeは直前の割り当て矩形ではなく、コントロールを計
    // 測する。共通のネイティブ基本処理で帯の枠分を一度計算する。最小値はコントロー
    // ルサイズのままにし、setPreferredSizeで要求サイズを消さない。
    item.minimumSize = lib::preferred_ui_size(item.control);
    if (bar) item.trimWidth = lib::cool_bar_item_preferred_size(bar, {}).width;
    const auto trim = item.trimWidth.value_or(lib::cool_bar_item_preferred_size(nullptr, {}).width);
    item.preferredSize = {item.minimumSize.width + trim, item.minimumSize.height};
}

std::shared_ptr<CoolItem> createCoolItem(PortableCoolBar& bar,
                                         std::shared_ptr<ToolBar> toolBar,
                                         const int index) {
    if (!toolBar) return {};
    const auto previous_count = bar.coolItems.size();
    if (index >= 0 && static_cast<std::size_t>(index) > previous_count)
        throw std::out_of_range("CoolBar item index");
    const auto insertion_index = index < 0
        ? previous_count : static_cast<std::size_t>(index);

    auto next_order = bar.itemOrder;
    bool valid_order = next_order.size() == previous_count;
    std::vector<bool> seen(previous_count);
    for (const auto previous_index : next_order) {
        if (previous_index >= previous_count || seen[previous_index]) {
            valid_order = false;
            break;
        }
        seen[previous_index] = true;
    }
    if (!valid_order) {
        next_order.resize(previous_count);
        for (std::size_t i = 0; i < previous_count; ++i)
            next_order[i] = i;
    }
    // DWT/SWTは、論理項目一覧と表示順の同じ位
    // 置に新項目を挿入する。以後の既存表示項目は相対
    // 順を維持し、論理インデックスだけが一つ進む。
    for (auto& previous_index : next_order)
        if (previous_index >= insertion_index) ++previous_index;
    next_order.insert(next_order.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                      insertion_index);

    auto next_wraps = bar.wrap_indices;
    for (auto& wrap : next_wraps)
        if (wrap >= insertion_index) ++wrap;

    auto item = std::make_shared<CoolItem>();
    item->control = std::move(toolBar);
    item->owner = item->control;
    bar.coolItems.insert(bar.coolItems.begin() +
                             static_cast<std::ptrdiff_t>(insertion_index),
                         item);
    bar.insert(insertion_index, item->control);
    bar.band_sizes.insert(bar.band_sizes.begin() +
                              static_cast<std::ptrdiff_t>(insertion_index),
                          item);
    for (std::size_t logical_index = 0; logical_index < bar.coolItems.size();
         ++logical_index)
        bar.coolItems[logical_index]->index = logical_index;
    bar.itemOrder = std::move(next_order);
    bar.applyWrapIndices(std::move(next_wraps));
    resetCISize(*item, &bar);
    return item;
}

std::shared_ptr<CoolItem> createCoolItem(
    const std::shared_ptr<PortableCoolBar>& bar, std::shared_ptr<ToolBar> toolBar,
    const int index) {
    return bar ? createCoolItem(*bar, std::move(toolBar), index) : nullptr;
}

Shell::Shell() = default;

std::optional<MenuItemRef> findMenu(const Shell& shell, const int keyCode,
                                    const std::uint32_t character,
                                    const std::uint32_t stateMask) {
    return shell.menuBar ? findMenu(*shell.menuBar, keyCode, character,
                                    stateMask)
                         : std::nullopt;
}

std::optional<MenuItemRef> findMenu(const Menu& menu, const int keyCode,
                                    const std::uint32_t character,
                                    const std::uint32_t stateMask) {
    // DのGCはデリゲート実行中のメニューを生存させる。参照を取得して子を保持し
    // 、C++の判定関数が所有元を終了させても古い対象を巡回しないようにする。
    std::vector<std::pair<MenuItemRef, std::shared_ptr<Menu>>> snapshot;
    for (std::size_t index = 0; index < menu.items.size(); ++index) {
        const auto* entry = menu.entry(index);
        snapshot.emplace_back(MenuItemRef(const_cast<Menu*>(&menu), index),
                              entry ? entry->submenu : nullptr);
    }
    for (const auto& [item, submenu] : snapshot) {
        if (!menuEnabled(item)) continue;
        const bool textMatch = eqAcc(
            static_cast<int>(convertAccelerator(item.native().text)), keyCode,
            character, stateMask);
        const bool shortcutMatch = !item.native().shortcut.empty() && eqAcc(
            static_cast<int>(convertAccelerator2(item.native().shortcut)), keyCode,
            character, stateMask);
        if (textMatch || shortcutMatch)
            return item;
        if (submenu) {
            if (const auto result = findMenu(*submenu, keyCode, character,
                                             stateMask))
                return result;
        }
    }
    return std::nullopt;
}

bool menuEnabled(const MenuItemRef item) {
    if (!item.valid()) return false;
    const auto enabled = item.data().enabled;
    if (enabled) {
        const auto value = enabled();
        if (!item.valid()) return false;
        item.setEnabled(value);
    }
    return item.enabled();
}

void setupToolTips(ToolBar& bar, const Presentation& prop) {
    bar.installTooltipHandler(prop.tooltipWidth, prop.graphics);
}

void doMenu(const MenuItemRef menu, MenuCommandEvent& event) {
    if (!menu.valid()) return;
    SelectionEvent selection;
    selection.time = event.time;
    selection.modifiers = event.modifiers;
    selection.doit = event.doit;
    menu.trigger(std::move(selection));
    event.doit = false;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::dmenu
