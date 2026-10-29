#pragma once

// cwx/editor/gui/dwt/dmenu.d
// の移植可能なC++20実装。 メニュ
// ー・ツールバーの全動作を維持し、OS
// の部品ABIでなく 共通libの安定
// した部品モデルとイベントを公開する。

#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class TopLevelPanel;
}

namespace cwxeditor::cwx::editor::gui::dwt::dmenu {

namespace lib = ::cwxeditor::lib;
namespace customtext = ::cwxeditor::cwx::editor::gui::dwt::customtext;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace types = ::cwxeditor::cwx::types;

using MenuID = types::MenuID;
using TMM = customtext::TMM;
using TMAppendData = customtext::TMAppendData;
using TextEdit = customtext::TextEdit;
using StyledText = customtext::StyledText;
using ComboBox = customtext::ComboBox;
using CCombo = customtext::CCombo;
using TextEditPtr = customtext::TextEditPtr;
using StyledTextPtr = customtext::StyledTextPtr;
using ComboBoxPtr = customtext::ComboBoxPtr;
using CComboPtr = customtext::CComboPtr;

class Menu;
class ToolBar;

/// 共通メニューモデルでSWTのスタイル定数の代わりに使う値。
enum MenuStyle : std::int32_t {
    MenuPush = 1 << 0,
    MenuCheck = 1 << 1,
    MenuRadio = 1 << 2,
    MenuSeparator = 1 << 3,
    MenuCascade = 1 << 4,
    ToolDropDown = 1 << 5,
};

/// dmenuが使うDのPropsの一部。 ネイティブアプリが自分の
/// 資源一覧を管理できるよう、文字・画像はコールバックで取得する。
struct CoolBarState {
    bool locked{};
    std::vector<std::size_t> order;
    std::vector<std::size_t> wrapIndices;
};

struct Presentation {
    std::function<lib::String(MenuID)> menuText;
    std::function<lib::String(MenuID)> toolText;
    std::function<lib::ResourceId(MenuID)> menuImage;
    std::function<lib::ResourceId(MenuID)> toolImage;
    std::function<lib::String(MenuID, lib::String)> formatMenu;
    std::function<lib::String(MenuID, lib::String)> formatTool;
    std::size_t textTabs{};
    std::size_t undoMaxEtc{100};
    std::size_t tooltipWidth{320};
    lib::GraphicsBackend* graphics{};
    bool useCoolBar{true};
    bool coolBarLocked{};
    std::vector<std::size_t> coolBarOrder;
    std::vector<std::size_t> coolBarWrapIndices;
    std::vector<std::size_t> coolBarInitialWrapIndices;
    // DはPropsオブジェクトを保持する。共通ホストはスタック上のPropsを借
    // りず、 所有者の寿命を保護した設定アダプターへ値のスナップショットを渡す。
    std::function<void(const CoolBarState&, bool)> saveCoolBarState;

    [[nodiscard]] lib::String buildMenu(MenuID id) const;
    [[nodiscard]] lib::String buildTool(MenuID id) const;
    [[nodiscard]] lib::ResourceId imageMenu(MenuID id) const;
    [[nodiscard]] lib::ResourceId imageTool(MenuID id) const;
};

/// dmenuが使うCommonsの共通部分。SWTの状態は
/// 所有せず、 外部依存はサービスのバックエンドだけにする。
struct Commons {
    lib::Services services{};
    lib::Action refreshToolBar{};
    // 上書きなしと、100や0を明示指定した状態は区別する。
    std::optional<std::size_t> undoMaxEtc;
    Presentation presentation{};
    std::function<std::size_t()> undoLimit;
    std::function<lib::Action(lib::Action)> subscribeUndoMax;

    Commons() = default;
    Commons(const customtext::Commons& value)
        : services(value.services), refreshToolBar(value.refreshToolBar),
          undoMaxEtc(value.undoMaxEtc) {}

    [[nodiscard]] std::size_t effectiveUndoMax() const {
        return undoLimit ? undoLimit() : undoMaxEtc.value_or(presentation.undoMaxEtc);
    }

    [[nodiscard]] std::shared_ptr<lib::ClipboardBackend> clipboard() const
        noexcept {
        return services.clipboard;
    }
    [[nodiscard]] lib::GraphicsBackend* graphics() const noexcept {
        return services.graphics ? services.graphics.get()
                                 : presentation.graphics;
    }
};

/// 具体的な文字列メニューの窓口。UndoManager
/// が渡されなければ自動生成して所有し、 SWTを直接置
/// き換えた際に生ポインターの管理先が失われるのを防ぐ。
class TextMenuModify final {
public:
    using CanSaveHistory = customtext::TextMenuModify::CanSaveHistory;

    TextMenuModify(TMM text, CanSaveHistory canSaveHistory = {},
                   std::shared_ptr<undo::UndoManager> undoManager = {},
                   TMAppendData appendData = {},
                   std::shared_ptr<lib::ClipboardBackend> clipboard = {});
    ~TextMenuModify();

    TextMenuModify(const TextMenuModify&) = delete;
    TextMenuModify& operator=(const TextMenuModify&) = delete;
    TextMenuModify(TextMenuModify&&) = delete;
    TextMenuModify& operator=(TextMenuModify&&) = delete;

    lib::Action selectChanged;
    lib::Action refreshToolBar;
    // 複合編集部品は、文字と他の項目をまとめた履歴を持てる。
    std::function<bool()> historyUndo;
    std::function<bool()> historyRedo;
    std::function<bool()> historyCanUndo;
    std::function<bool()> historyCanRedo;
    // このネイティブText/StyledText自身がキーボードのUndo履歴
    // を持つ場合だけ有効にする。 一般の複合編集部品は共通の窓口の経路を使う。
    bool nativeTextKeyboardUndo{};

    [[nodiscard]] const TMM& text() const noexcept;
    [[nodiscard]] std::shared_ptr<lib::Menu> menu() const noexcept;
    [[nodiscard]] const std::shared_ptr<customtext::TextMenuModify>&
    customText() const noexcept {
        return implementation_;
    }
    [[nodiscard]] std::shared_ptr<undo::UndoManager> undoManager() const
        noexcept {
        return undoManager_;
    }

    void refresh();
    void reset();
    void modifyText();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool canCopy() const;
    [[nodiscard]] bool canCut() const;
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] bool canDelete() const;
    [[nodiscard]] bool canSelectAll() const;
    bool undo();
    bool redo();
    void copy();
    void cut();
    void paste();
    void erase();
    void selectAll();

private:
    TMM text_{};
    std::shared_ptr<undo::UndoManager> undoManager_{};
    std::shared_ptr<customtext::TextMenuModify> implementation_{};
};

using TextMenuModifyPtr = std::shared_ptr<TextMenuModify>;
using UndoManagerPtr = std::shared_ptr<undo::UndoManager>;

/// 文字入力部品に対するDのControl.getMenu()相当。 createTextMenu
/// が作った有効なメニュー、なければnullを返す。 MainWindowが
/// Commons.lastFocusEditorと同じ振り分けに使い、 編
/// 集メニューのUndo/Redoなどをまずフォーカス中の文字入力へ送る。
[[nodiscard]] TextMenuModifyPtr textMenuOf(const lib::WidgetPtr& widget);

/// Dと同じく、ダイアログの編集可能なText/Comboごとに文字
/// 列メニューとUndo履歴を持たせる。 root配下のTextEdit
/// ・編集可能ComboBoxのうち未設定のものへ標準メニューを接
/// 続し、部品の寿命中は保持する。 Editorはlib::set_window_shown_hook
/// で表示する全ウィンドウへ適用する。 既に独
/// 自の文字列メニューがある部品は変更しない。
void installMissingTextMenus(const lib::WidgetPtr& root, const Commons& comm);

[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, const Presentation& prop, TMM text,
    TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});
[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, TMM text, TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});
[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, const Presentation& prop, TextEditPtr text,
    TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});
[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, const Presentation& prop, StyledTextPtr text,
    TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});
[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, const Presentation& prop, ComboBoxPtr text,
    TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});
[[nodiscard]] TextMenuModifyPtr createTextMenu(
    Commons comm, const Presentation& prop, CComboPtr text,
    TextMenuModify::CanSaveHistory canSaveHistory = {},
    UndoManagerPtr undoManager = {}, TMAppendData appendData = {});

[[nodiscard]] UndoManagerPtr createEtcUndo(Commons comm,
                                            lib::WidgetPtr control = {});

struct SelectionEvent {
    lib::WidgetPtr widget{};
    ToolBar* toolBar{};
    std::size_t itemIndex{};
    bool arrow{};
    lib::Modifiers modifiers{lib::Modifiers::None};
    std::uint64_t time{};
    bool doit{true};
    bool handled{};
    std::optional<bool> physical_control{};
    lib::Modifiers accelerator_modifiers{lib::Modifiers::None};
    bool is_accelerator{};
};

struct MenuCommandEvent {
    std::uint64_t time{};
    lib::Modifiers modifiers{lib::Modifiers::None};
    bool doit{true};
};

/// アプリ固有の切り取り・コピー・貼り付け・削除・複製の対象。
class TCPD {
public:
    virtual ~TCPD() = default;
    virtual void cut(SelectionEvent&) = 0;
    virtual void copy(SelectionEvent&) = 0;
    virtual void paste(SelectionEvent&) = 0;
    virtual void del(SelectionEvent&) = 0;
    virtual void clone(SelectionEvent&) = 0;
    [[nodiscard]] virtual bool canDoTCPD() const = 0;
    [[nodiscard]] virtual bool canDoT() const = 0;
    [[nodiscard]] virtual bool canDoC() const = 0;
    [[nodiscard]] virtual bool canDoP() const = 0;
    [[nodiscard]] virtual bool canDoD() const = 0;
    [[nodiscard]] virtual bool canDoClone() const = 0;
};

using TCPDPtr = std::shared_ptr<TCPD>;

/// 切り取り・コピー・貼り付けがポップアップだけにある一
/// 覧向けのCtrl（Cmd）+X/C/V。 ポップアッ
/// プは部品ツリー外なので、共通のメニュー検索ではショー
/// トカットを拾えない。 DのSWT一覧と同じくTCPD
/// のキーリスナーで扱う。 一覧・表・キャンバスにだけ接
/// 続し、文字入力には自身のクリップボード操作を残す。
struct ClipboardShortcut {
    std::function<bool()> canCut, canCopy, canPaste;
    std::function<void()> cut, copy, paste;
};
inline bool handleClipboardShortcut(lib::KeyEvent& event, const ClipboardShortcut& shortcut) {
    if (event.handled) return false;
    const auto mods = static_cast<std::uint32_t>(event.modifiers);
    const auto command = static_cast<std::uint32_t>(lib::Modifiers::Control | lib::Modifiers::Meta);
    const auto other = static_cast<std::uint32_t>(lib::Modifiers::Shift | lib::Modifiers::Alt);
    if ((mods & command) == 0U || (mods & other) != 0U) return false;
    const auto run = [&event](const std::function<bool()>& can, const std::function<void()>& action) {
        if (!action || (can && !can())) return false;
        action();
        event.handled = true;
        return true;
    };
    if (event.key == lib::Key::X) return run(shortcut.canCut, shortcut.cut);
    if (event.key == lib::Key::C) return run(shortcut.canCopy, shortcut.copy);
    if (event.key == lib::Key::V) return run(shortcut.canPaste, shortcut.paste);
    return false;
}
/// TCPD実装（canDoT/C/Pとcut/copy/paste）向けの同等の処理。
template <typename T>
bool handleTCPDShortcut(lib::KeyEvent& event, T& tcpd) {
    return handleClipboardShortcut(event, {
        [&tcpd] { return tcpd.canDoT(); }, [&tcpd] { return tcpd.canDoC(); },
        [&tcpd] { return tcpd.canDoP(); },
        [&tcpd] { SelectionEvent e; tcpd.cut(e); },
        [&tcpd] { SelectionEvent e; tcpd.copy(e); },
        [&tcpd] { SelectionEvent e; tcpd.paste(e); }});
}

/// 現在フォーカス中の共通Text/Comboへ編集操作を
/// 送り、 アプリの選択対象にはTCPDを代替として使う。
class InTCPD final {
public:
    TCPDPtr tcpd;

    explicit InTCPD(lib::Services services = {}, TCPDPtr target = {});
    void setFocused(lib::WidgetPtr widget) noexcept;
    [[nodiscard]] lib::WidgetPtr focused() const noexcept;

    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);

private:
    lib::Services services_{};
    std::weak_ptr<lib::Widget> focused_{};
    void cloneText(lib::Widget& widget);
};

struct MenuData {
    MenuID id{MenuID::None};
    std::function<lib::String(lib::String)> format{};
    std::function<bool()> enabled{};
};

/// Menuが所有する項目への、値として扱える安定した参照。
/// native()でlibの項目を取得でき、コールバッ
/// クとMenuDataはdmenuのモデル側に保持する。
class MenuItemRef final {
public:
    MenuItemRef() = default;
    MenuItemRef(Menu* owner, std::size_t index);
    [[nodiscard]] bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] std::size_t index() const noexcept;
    [[nodiscard]] Menu* owner() const noexcept;
    [[nodiscard]] lib::MenuItem& native() const;
    [[nodiscard]] MenuData& data() const;
    void setText(lib::String text) const;
    void setImage(lib::ResourceId image) const;
    void setEnabled(bool value) const;
    [[nodiscard]] bool enabled() const;
    void setChecked(bool value) const;
    [[nodiscard]] int style() const;
    void trigger(SelectionEvent event = {}) const;

private:
    friend class Menu;
    friend class MenuShown;
    Menu* owner_{};
    std::string command_;
    std::weak_ptr<const int> lifetime_;
};

using SelectionHandler = std::function<void(SelectionEvent&)>;

class Menu : public lib::Menu {
public:
    struct Entry {
        MenuData data{};
        lib::ResourceId image{};
        SelectionHandler selection{};
        lib::Action action{};
        std::function<void()> refresh{};
        std::shared_ptr<Menu> submenu{};
        int style{MenuPush};
    };

    explicit Menu(bool context = false);
    ~Menu() override;
    [[nodiscard]] MenuItemRef append(lib::String text, lib::ResourceId image,
                                     int style, MenuData data = {},
                                     SelectionHandler selection = {},
                                     lib::Action action = {},
                                     int index = -1);
    [[nodiscard]] MenuItemRef appendSeparator(int index = -1);
    /// ネイティブの項目とモデル側の操作をまとめて削除する。 lib::Menu::items
    /// だけを消すと、旧添字のEntryコールバックが残る。
    void clearItems();
    /// SWTはシェルとともに所有部品を破棄するが、C++では部品だけが保持
    /// される場合がある。 ホストが有効な間に、一度だけ接続を無効化する。
    void retireBindings();
    [[nodiscard]] Entry* entry(std::size_t index) noexcept;
    [[nodiscard]] const Entry* entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return items.size(); }
    void refreshItems();
    void dispatch(std::string_view command, SelectionEvent event = {});
    /// サブメニューを接続し、共通のMenuItemツリーも同期する。
    /// SWTは入れ子のMenuItemを直接公開するが、共通モデルは情
    /// 報の多いdmenu Entryを正式な状態とし、 ネイティブ向
    /// けにlib::MenuItem::childrenへ反映する。
    void setSubmenu(MenuItemRef item, std::shared_ptr<Menu> submenu);
    void setVisible(bool value) noexcept { visible_ = value; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setLocation(lib::Point value) noexcept { location_ = value; }
    [[nodiscard]] lib::Point location() const noexcept { return location_; }

private:
    friend class MenuItemRef;
    std::vector<Entry> entries_;
    bool visible_{};
    lib::Point location_{};
    Menu* parent_{};
    std::uint64_t revision_{};
    lib::WidgetOperationLifetime lifetime_;

    void synchronizeNative();
    void synchronizeToRoot();
    void refreshItemsRecursive();
};

class MenuShown final {
public:
    MenuShown(MenuItemRef item, lib::ResourceId image)
        : item_(item), image_(image) {}
    void menuShown();

private:
    MenuItemRef item_{};
    lib::ResourceId image_{};
};

class MenuSel final {
public:
    explicit MenuSel(SelectionHandler handler) : selection_(std::move(handler)) {}
    explicit MenuSel(lib::Action action) : action_(std::move(action)) {}
    void widgetSelected(SelectionEvent& event) const;

private:
    SelectionHandler selection_{};
    lib::Action action_{};
};

// Dのこのオーバーロードは、共有の最上位ペインへアプリのショートカッ
// トを登録する。 包含フラグを使うポップアップMenu版とは分ける。
using TopLevelPanel = ::cwxeditor::cwx::editor::gui::dwt::commons::TopLevelPanel;

void appendMenuTCPD(Commons comm, TopLevelPanel& panel, TCPDPtr tcpd, bool t,
                    bool c, bool p, bool d, bool clone);
void appendMenuTCPD(Commons comm, Menu& menu, TCPDPtr tcpd, bool t, bool c,
                    bool p, bool d, bool clone);
void addRefMenu(Commons comm, MenuItemRef item);

struct IgnoreHotkey {
    virtual ~IgnoreHotkey() = default;
};
struct CIgnoreHotkey final : IgnoreHotkey {};

/// 共通のアクセラレータービットは、下位24ビットにlib::Key
/// 、 上位にはcustomtextと同じ位置の修飾ビットを使う。
inline constexpr std::uint32_t AcceleratorShift = 1U << 24U;
inline constexpr std::uint32_t AcceleratorControl = 1U << 25U;
inline constexpr std::uint32_t AcceleratorAlt = 1U << 26U;
inline constexpr std::uint32_t AcceleratorCommand = 1U << 27U;
inline constexpr std::uint32_t AcceleratorModifierMask = 0x0F000000U;

[[nodiscard]] bool eqAcc(int acc, int keyCode, std::uint32_t character,
                         std::uint32_t stateMask);
[[nodiscard]] lib::String acceleratorText(int character);
[[nodiscard]] lib::String acceleratorText(lib::Key key);
[[nodiscard]] std::uint32_t convertAccelerator2(std::string_view accText);
[[nodiscard]] std::uint32_t convertAccelerator(std::string_view text);
[[nodiscard]] lib::Modifiers acceleratorModifiers(std::uint32_t accelerator) noexcept;
[[nodiscard]] std::uint32_t acceleratorModifierMask(lib::Modifiers modifiers) noexcept;
[[nodiscard]] bool __unittest_L414_C3();

MenuItemRef createMenuItemImpl(
    Commons comm, Menu& sub, lib::String text, lib::ResourceId image,
    SelectionHandler selection, lib::Action action, int style, MenuID id,
    std::function<bool()> enabled = {}, int index = -1);
MenuItemRef createMenuItem2(Commons comm, Menu& sub, lib::String text,
                            lib::ResourceId image, SelectionHandler selection,
                            std::function<bool()> enabled = {},
                            int style = MenuPush, int index = -1);
MenuItemRef createMenuItem2(Commons comm, Menu& sub, lib::String text,
                            lib::ResourceId image, lib::Action action,
                            std::function<bool()> enabled = {},
                            int style = MenuPush, int index = -1);
MenuItemRef createMenuItem(Commons comm, Menu& sub, MenuID id,
                           SelectionHandler selection,
                           std::function<bool()> enabled = {},
                           int style = MenuPush, int index = -1);
MenuItemRef createMenuItem(Commons comm, Menu& sub, MenuID id,
                           lib::Action action,
                           std::function<bool()> enabled = {},
                           int style = MenuPush, int index = -1);
std::shared_ptr<Menu> createMenu2(Commons comm, Menu& bar, lib::String text,
                                  MenuItemRef& item);
std::shared_ptr<Menu> createMenu(Commons comm, Menu& bar, MenuID id);

class ToolItemRef final {
public:
    ToolItemRef() = default;
    ToolItemRef(ToolBar* owner, std::size_t index);
    [[nodiscard]] bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] ToolBar* owner() const noexcept;
    [[nodiscard]] lib::ToolItem& native() const;
    [[nodiscard]] MenuData& data() const;
    void setText(lib::String text) const;
    void setTooltip(lib::String text) const;
    void setImage(lib::ResourceId image) const;
    void setEnabled(bool value) const;
    void setChecked(bool value) const;
    [[nodiscard]] bool enabled() const;
    void trigger(bool arrow = false) const;

private:
    friend class ToolBar;
    friend class ToolSel;
    ToolBar* owner_{};
    std::size_t index_{};
    std::weak_ptr<const int> lifetime_;
};

class ToolBar : public lib::ToolBar {
public:
    struct Entry {
        MenuData data{};
        lib::String tooltip;
        lib::ResourceId image{};
        SelectionHandler selection{};
        lib::Action action{};
        std::function<void()> refresh{};
        std::shared_ptr<Menu> menu{};
        std::function<bool(bool)> openArrow{};
        lib::Rect bounds{};
        int style{MenuPush};
    };

    ToolBar();
    void retireBindings();
    [[nodiscard]] ToolItemRef append(lib::String tip, lib::String text,
                                     lib::ResourceId image, int style,
                                     MenuData data = {},
                                     SelectionHandler selection = {},
                                     lib::Action action = {});
    [[nodiscard]] Entry* entry(std::size_t index) noexcept;
    [[nodiscard]] const Entry* entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return items.size(); }
    void refreshItems(const std::map<MenuID, std::function<bool()>>& overrides = {},
                      std::function<bool()> continuation = {});
    void trigger(std::size_t index, bool arrow = false);

    struct TooltipState {
        bool visible{};
        std::size_t itemIndex{};
        lib::ResourceId image{};
        lib::String name;
        lib::String description;
        lib::Rect bounds{};
        std::size_t width{};
    } tooltip;

    void installTooltipHandler(std::size_t width, lib::GraphicsBackend* graphics);

private:
    friend class ToolItemRef;
    std::vector<Entry> entries_;
    lib::WidgetOperationLifetime refresh_lifetime_;
    std::uint64_t refresh_revision_{};
};

class ToolSel final {
public:
    using Handler = std::function<void(ToolItemRef)>;
    explicit ToolSel(Handler handler) : handler_(std::move(handler)) {}
    void widgetSelected(SelectionEvent& event) const;

private:
    Handler handler_{};
};

ToolItemRef createDropDownItem(Commons comm, ToolBar& bar, MenuID id,
                               lib::Action action, std::shared_ptr<Menu>& menu,
                               std::function<bool()> enabled = {},
                               std::function<bool(bool)> openArrow = {});
ToolItemRef createDropDownItem2(
    Commons comm, ToolBar& bar, lib::String text, lib::ResourceId image,
    lib::Action action, std::shared_ptr<Menu>& menu, MenuID id,
    std::function<bool()> enabled = {},
    std::function<bool(bool)> openArrow = {});
void showDropDownMenu(ToolItemRef item, const std::shared_ptr<Menu>& menu);
void addRefMenu(Commons comm, ToolItemRef item);

ToolItemRef createToolItemImpl(
    Commons comm, ToolBar& bar, lib::String tip, lib::String text,
    lib::ResourceId image, SelectionHandler selection, lib::Action action,
    int style, MenuID id, std::function<bool()> enabled = {});
ToolItemRef createToolItem2(
    Commons comm, ToolBar& bar, lib::String tip, lib::String text,
    lib::ResourceId image, SelectionHandler selection,
    std::function<bool()> enabled = {}, int style = MenuPush);
ToolItemRef createToolItem2(
    Commons comm, ToolBar& bar, lib::String tip, lib::String text,
    lib::ResourceId image, lib::Action action,
    std::function<bool()> enabled = {}, int style = MenuPush);
ToolItemRef createToolItem2(
    Commons comm, ToolBar& bar, lib::String text, lib::ResourceId image,
    SelectionHandler selection, std::function<bool()> enabled = {},
    int style = MenuPush);
ToolItemRef createToolItem2(
    Commons comm, ToolBar& bar, lib::String text, lib::ResourceId image,
    lib::Action action, std::function<bool()> enabled = {},
    int style = MenuPush);
ToolItemRef createToolItem2(
    Commons comm, ToolBar& bar, lib::String tip, lib::String text,
    lib::ResourceId image, ToolSel::Handler handler,
    std::function<bool()> enabled = {}, int style = MenuPush);
ToolItemRef createToolItem(Commons comm, ToolBar& bar, MenuID id,
                           ToolSel::Handler handler,
                           std::function<bool()> enabled = {},
                           int style = MenuPush);
ToolItemRef createToolItem(Commons comm, ToolBar& bar, MenuID id,
                           SelectionHandler selection,
                           std::function<bool()> enabled = {},
                           int style = MenuPush);
ToolItemRef createToolItem(Commons comm, ToolBar& bar, MenuID id,
                           lib::Action action,
                           std::function<bool()> enabled = {},
                           int style = MenuPush);

class CoolItem final : public lib::CoolBarBandSize {
public:
    std::shared_ptr<ToolBar> control;
    // 親の外枠寸法は値として保持し、親自身を寿命を超えて借用しない。
    std::optional<float> trimWidth;
    std::size_t index{};
};

class PortableCoolBar : public lib::CoolBar {
public:
    PortableCoolBar();
    std::vector<std::shared_ptr<CoolItem>> coolItems;
    std::vector<std::size_t> itemOrder;
    std::vector<std::size_t> wrapIndices;
    std::shared_ptr<Menu> menu;
    void applyItemOrder(std::vector<std::size_t> order);
    void applyWrapIndices(std::vector<std::size_t> indices);
    void itemMoved(std::size_t from, std::size_t to);
};

class CoolBarHost : public lib::Container {
public:
    CoolBarHost();
    ~CoolBarHost() override;
    [[nodiscard]] lib::ComponentKind kind() const noexcept override {
        return lib::ComponentKind::Panel;
    }
    std::shared_ptr<PortableCoolBar> coolBar;
    std::shared_ptr<Menu> menu;
    Presentation presentation{};
    bool nativeCoolBar{};
    std::shared_ptr<void> listener;
    bool saveOrder{};
    void retireBindings();
    [[nodiscard]] bool retired() const noexcept { return retired_; }
private:
    bool retired_{};
};

[[nodiscard]] std::vector<std::size_t> getWrapIndices2(
    const PortableCoolBar& bar);

class CoolBarListener final {
public:
    Presentation prop;
    std::weak_ptr<PortableCoolBar> cbar;
    std::weak_ptr<CoolBarHost> host;
    MenuItemRef lockItem{};
    std::vector<std::size_t> wrapIndices;
    bool saveOrder{};

    CoolBarListener(const Presentation& presentation,
                 std::shared_ptr<PortableCoolBar> coolBar, bool save);
    void widgetDisposed();
    void mouseMove();
    void mouseMove(const lib::PointerEvent& event);
    void itemMoved(std::size_t from, std::size_t to);
    void reset();
    void resizeAll();
    void lock();
private:
    [[nodiscard]] std::shared_ptr<PortableCoolBar> activeBar() const;
    bool hostBound_{};
    bool disposed_{};
public:
    void bindHost(const std::shared_ptr<CoolBarHost>& owner) { host = owner; hostBound_ = true; }
};

// Dは文字列Nameからmixinで設定項目を選ぶ。 C++はPresentation
// で設定アダプターを渡し、すべてのNameで同じ接続済みの規則を使う。
template <typename Name = void>
using CBarListener = CoolBarListener;

[[nodiscard]] std::shared_ptr<CoolBarHost> createCoolBar(
    Commons comm, lib::Container& parent, bool saveOrder,
    std::function<void(lib::Container&)> setupItems,
    std::function<void(Menu&)> putMenu = {});
inline std::shared_ptr<CoolBarHost> createCoolBar(
    Commons comm, const std::shared_ptr<lib::Container>& parent,
    bool saveOrder, std::function<void(lib::Container&)> setupItems,
    std::function<void(Menu&)> putMenu = {}) {
    auto result = createCoolBar(std::move(comm), *parent, saveOrder,
                                std::move(setupItems), std::move(putMenu));
    return result;
}

void resetCISize(CoolItem& item, const lib::CoolBar* bar = nullptr);
[[nodiscard]] std::shared_ptr<CoolItem> createCoolItem(
    PortableCoolBar& bar, std::shared_ptr<ToolBar> toolBar, int index = -1);
[[nodiscard]] std::shared_ptr<CoolItem> createCoolItem(
    const std::shared_ptr<PortableCoolBar>& bar,
    std::shared_ptr<ToolBar> toolBar, int index = -1);

class Shell : public lib::Window {
public:
    Shell();
    std::shared_ptr<Menu> menuBar;
};

[[nodiscard]] std::optional<MenuItemRef> findMenu(
    const Shell& shell, int keyCode, std::uint32_t character,
    std::uint32_t stateMask);
[[nodiscard]] std::optional<MenuItemRef> findMenu(
    const Menu& menu, int keyCode, std::uint32_t character,
    std::uint32_t stateMask);
[[nodiscard]] bool menuEnabled(MenuItemRef item);

void setupToolTips(ToolBar& bar, const Presentation& prop);

void doMenu(MenuItemRef menu, MenuCommandEvent& event);

}  // namespace cwxeditor::cwx::editor::gui::dwt::dmenu
