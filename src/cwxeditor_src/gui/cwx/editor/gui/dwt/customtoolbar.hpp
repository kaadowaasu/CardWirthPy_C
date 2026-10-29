#pragma once

// cwx/editor/gui/dwt/customtoolbar.d
// のC++20移植。 バー・グループ・メニュ
// ー項目を編集可能な共通部品のツリーで表す。
// SWTを公開せず、表・メニュー・クリップ
// ボード・逐次検索・Undo・ドラッグを
// ネイティブ・ヘッドレスの両方へ提供する。

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::customtoolbar {

namespace lib = ::cwxeditor::lib;
namespace menu = ::cwxeditor::cwx::menu;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace xml = ::cwxeditor::cwx::xml;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;

using MenuID = types::MenuID;
using Tool = structs::Tool;
using ToolBarSettings = structs::ToolBarSettings;
using XNode = xml::XNode;
using Parent = lib::Container*;

/// ツールバー編集が使う文字列・画像・配置設定・資源の規
/// 則。 すべての通知は任意とし、Editor全体のカタ
/// ログがなくても固定の既定値でヘッドレス構築できる。
struct Presentation {
    std::function<lib::String(MenuID)> menuText;
    std::function<lib::ResourceId(MenuID)> menuImage;
    /// 設定したコマンドからツールバー項目を作る際に使う
    /// 、任意の状態・文字列の提供元。 DではProps/Commons
    /// から読む値をコールバックにし、ネイティ
    /// ブ・ヘッドレスとも同じ遅延更新を行う。
    std::function<lib::String(MenuID)> menuTooltip;
    std::function<lib::String(MenuID)> menuShortcut;
    std::function<bool(MenuID)> menuEnabled;
    std::function<bool(MenuID)> menuChecked;
    std::function<void(MenuID)> menuCommand;
    std::function<std::shared_ptr<dmenu::Menu>(MenuID)> menuDropDown;
    lib::ResourceId toolBarImage{1};
    lib::ResourceId toolGroupImage{1};
    lib::String toolBarName{"ツールバー (%1$s)"};
    lib::String toolGroupName{"グループ (%1$s)"};
    lib::String upSelection{"上へ"};
    lib::String downSelection{"下へ"};
    lib::String addTool{"ツールを追加"};
    lib::String removeTool{"削除"};
    lib::String addToolBar{"バーの追加"};
    lib::String addToolGroup{"グループの追加"};
    lib::String customizeToolBar{"ツールバーの編集"};
    lib::String resetToolBarSettings{"初期設定に戻す"};
    lib::String incSearch{"絞り込み検索"};
    lib::String undo{"元に戻す"};
    lib::String redo{"やり直す"};
    lib::String copy{"コピー"};
    lib::String cut{"切り取り"};
    lib::String paste{"貼り付け"};
    lib::String deleteText{"削除"};
    lib::String xmlMime{"application/x-cwxeditor-xmlbytes"};
    std::size_t undoMax{1024};
    std::function<std::size_t()> undoLimit;
    lib::String ok{"OK"};
    lib::String cancel{"キャンセル(C)"};
    lib::String apply{"適用(A)"};
    std::int32_t sashLeft{1};
    std::int32_t sashRight{1};
    bool xmlCopy{};
};

/// DのCommonsのデリゲート配列に対応する通知群。
struct Callbacks {
    lib::Action refreshToolBar;
    lib::Action modified;
    std::function<void(const ToolBarSettings&)> applied;
    lib::Action closed;
    /// 任意のダイアログ配置の保存先。Dではprop.var.toolBarCustomDlg
    /// に保存する。 再利用可能な編集部品をEditor
    /// 設定へ依存させず、コールバックで受け渡す。
    std::function<std::optional<lib::Rect>()> loadGeometry;
    std::function<void(const lib::Rect&)> saveGeometry;
    std::function<void(std::int32_t, std::int32_t)> saveSashWeights;
    /// Commons.refUndoMaxを購読する。返された処理で
    /// 登録を外し、 編集画面を閉じた後に通知を保持しないようにする。
    std::function<lib::Action(lib::Action)> subscribeUndoMax;
};

/// メインツールバーの設定を編集する、共通のモデルと部品ツリー。
class ToolBarCustomizer final {
public:
    using Services = lib::Services;
    using Panel = lib::Panel;
    using Table = lib::TableView;
    using Tree = lib::TreeView;
    using Button = lib::Button;
    using SplitPane = splitpane::SplitPane;
    using Menu = lib::Menu;
    using MenuItem = lib::MenuItem;
    using TreeItemId = lib::TreeItemId;
    using ModCallback = lib::Action;

    ToolBarCustomizer(Services services, Parent parent,
                      ToolBarSettings tools, const ToolBarSettings& init,
                      Presentation presentation = {},
                      Callbacks callbacks = {});
    ToolBarCustomizer(Parent parent, ToolBarSettings tools,
                      const ToolBarSettings& init, Presentation presentation = {},
                      Services services = {}, Callbacks callbacks = {})
        : ToolBarCustomizer(std::move(services), parent, std::move(tools), init,
                            std::move(presentation), std::move(callbacks)) {}

    ToolBarCustomizer(const ToolBarCustomizer&) = delete;
    ToolBarCustomizer& operator=(const ToolBarCustomizer&) = delete;
    ToolBarCustomizer(ToolBarCustomizer&&) = delete;
    ToolBarCustomizer& operator=(ToolBarCustomizer&&) = delete;
    ~ToolBarCustomizer();

    /// Dの公開デリゲート配列。モデル変更後に操作を呼ぶ。
    std::vector<ModCallback> modEvent;

    [[nodiscard]] const std::shared_ptr<Panel>& widget() const noexcept {
        return root_;
    }
    [[nodiscard]] const std::shared_ptr<Table>& menuList() const noexcept {
        return menu_list_;
    }
    [[nodiscard]] const std::shared_ptr<Tree>& toolTree() const noexcept {
        return tool_tree_;
    }
    [[nodiscard]] const std::shared_ptr<SplitPane>& splitPane() const noexcept {
        return split_;
    }
    [[nodiscard]] const std::shared_ptr<Menu>& menuListMenu() const noexcept {
        return menu_list_menu_;
    }
    [[nodiscard]] const std::shared_ptr<Menu>& toolTreeMenu() const noexcept {
        return tool_tree_menu_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& addButton() const noexcept {
        return add_button_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& removeButton() const noexcept {
        return remove_button_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& addBarButton() const noexcept {
        return add_bar_button_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& addGroupButton() const noexcept {
        return add_group_button_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& upButton() const noexcept {
        return up_button_;
    }
    [[nodiscard]] const std::shared_ptr<Button>& downButton() const noexcept {
        return down_button_;
    }
    [[nodiscard]] const Presentation& presentation() const noexcept {
        return presentation_;
    }
    [[nodiscard]] const Services& services() const noexcept { return services_; }

    [[nodiscard]] ToolBarSettings tools() const;
    void setTools(ToolBarSettings value);
    void disconnectNotifications();
    [[nodiscard]] ToolBarSettings getTools() const { return tools(); }

    /// 現在の設定から実際の共通ツールバー部品を作る。各
    /// バーは一つのdmenu::ToolBarとなり、
    /// 区切り・画像・ツールチップ・有効/チェック状態
    /// ・任意のメニュー・コマンド通知を保持する。 コマ
    /// ンドや状態の変更後はToolBar::refreshItems()
    /// で動的な提供元と同期し直す。
    [[nodiscard]] std::shared_ptr<dmenu::ToolBar> nativeToolBar(
        std::size_t index) const;
    [[nodiscard]] std::vector<std::shared_ptr<dmenu::ToolBar>>
    nativeToolBars() const;

    [[nodiscard]] bool canIncSearch() const;
    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    [[nodiscard]] bool canAddMenu() const noexcept;
    [[nodiscard]] bool canRemoveMenu() const noexcept;
    [[nodiscard]] bool canAppendMenu(MenuID id) const;
    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept { return false; }

    void startIncSearch();
    void up();
    void down();
    void addBar();
    void addGroup();
    void addMenu();
    void removeMenu();
    void resetTools();

    void cut();
    void copy();
    void paste();
    void del();
    void clone() noexcept {}

    [[nodiscard]] bool pasteXml(lib::StringView serialized);
    [[nodiscard]] bool dropXml(lib::StringView serialized, TreeItemId target);
    [[nodiscard]] XNode toNode(TreeItemId item) const;

    [[nodiscard]] const std::vector<TreeItemId>& treeSelection() const noexcept {
        return tree_selection_;
    }
    [[nodiscard]] const std::vector<std::size_t>& menuSelection() const noexcept {
        return menu_selection_;
    }
    void setTreeSelection(std::vector<TreeItemId> values);
    void setMenuSelection(std::vector<std::size_t> values);
    void focusMenuList() noexcept;
    void focusToolTree() noexcept;
    [[nodiscard]] bool treeFocused() const noexcept { return focus_ == Focus::Tree; }

private:
    struct Node {
        TreeItemId id{};
        TreeItemId parent{};
        lib::String text;
        lib::ResourceId image{};
        std::optional<MenuID> menu;
        bool expanded{true};
    };
    struct MenuIdHash {
        std::size_t operator()(MenuID value) const noexcept {
            return static_cast<std::size_t>(static_cast<std::underlying_type_t<MenuID>>(value));
        }
    };
    enum class Focus { None, Menu, Tree };

    class CTUndo;

    Services services_{};
    Parent parent_{};
    Presentation presentation_{};
    Callbacks callbacks_{};
    ToolBarSettings tools_{};
    ToolBarSettings initial_{};
    std::shared_ptr<Panel> root_{};
    std::shared_ptr<SplitPane> split_{};
    std::shared_ptr<Panel> menu_panel_{};
    std::shared_ptr<Panel> tree_panel_{};
    std::shared_ptr<Table> menu_list_{};
    std::shared_ptr<Tree> tool_tree_{};
    std::shared_ptr<Menu> menu_list_menu_{};
    std::shared_ptr<Menu> tool_tree_menu_{};
    std::shared_ptr<Button> add_button_{};
    std::shared_ptr<Button> remove_button_{};
    std::shared_ptr<Button> add_bar_button_{};
    std::shared_ptr<Button> add_group_button_{};
    std::shared_ptr<Button> up_button_{};
    std::shared_ptr<Button> down_button_{};
    std::unique_ptr<incsearch::IncSearch> menu_search_{};
    undo::UndoManager undo_;
    xmlbytestransfer::ClipData clipboard_;
    std::vector<Node> nodes_{};
    std::unordered_set<MenuID, MenuIdHash> added_{};
    std::vector<TreeItemId> tree_selection_{};
    std::vector<std::size_t> menu_selection_{};
    TreeItemId next_id_{1};
    mutable TreeItemId drag_item_{};
    lib::Action undo_max_unsubscribe_{};
    Focus focus_{Focus::None};
    bool building_{};
    std::uint64_t notification_revision_{};
    // DのデリゲートはGC管理だが、保持されたC++部品は
    // 終了した所有者へ触れる前に処理を止める必要がある。
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};

    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        lib::ComponentKind kind) const;
    void construct();
    void constructMenus();
    void constructDragDrop();
    void updateTree();
    void updateBarAndGroupText();
    bool refreshMenu();
    void refreshMenuStates();
    void notifyChanged(bool refresh = true);
    void refreshToolbar() const;
    void store();
    void refUndoMax();

    [[nodiscard]] Node* node(TreeItemId id) noexcept;
    [[nodiscard]] const Node* node(TreeItemId id) const noexcept;
    [[nodiscard]] std::vector<TreeItemId> children(TreeItemId parent) const;
    [[nodiscard]] TreeItemId topItem(TreeItemId id) const;
    [[nodiscard]] std::size_t childIndex(TreeItemId id) const;
    [[nodiscard]] bool descendant(TreeItemId id, TreeItemId ancestor) const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> subtreeRange(TreeItemId id) const;
    [[nodiscard]] TreeItemId selectedTreeItem() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selectedPath() const;
    void restorePath(const std::vector<std::size_t>& path);
    void selectTree(TreeItemId id, bool notify = false);
    void syncTreeComponent();
    void syncMenuComponent();
    [[nodiscard]] TreeItemId addBarImpl(TreeItemId selection);
    [[nodiscard]] TreeItemId addGroupImpl(TreeItemId selection);
    [[nodiscard]] TreeItemId addMenuImpl(TreeItemId selection,
                                         const std::vector<MenuID>& values);
    void removeMenuImpl(TreeItemId selection);
    void removeSubtree(TreeItemId id);
    void moveSelection(int direction);
    [[nodiscard]] lib::String formatIndex(const lib::String& pattern,
                                           std::size_t index) const;
    [[nodiscard]] lib::String menuText(MenuID id) const;
    [[nodiscard]] lib::ResourceId menuImage(MenuID id) const;
    [[nodiscard]] bool sameTools(const ToolBarSettings& left,
                                 const ToolBarSettings& right) const noexcept;
    [[nodiscard]] std::vector<MenuID> menuIdsFromRows() const;
    [[nodiscard]] std::vector<MenuID> menuIdsFromNode(const XNode& node) const;
    [[nodiscard]] bool fromNode(const XNode& node, TreeItemId selection,
                                bool storeBeforePaste);
    [[nodiscard]] bool fromNodeImpl(const XNode& node, TreeItemId selection);
    [[nodiscard]] bool parseDragPayload(const lib::ClipboardData& data,
                                        lib::String& serialized) const;
    [[nodiscard]] lib::ClipboardData menuListPayload() const;
    [[nodiscard]] lib::ClipboardData treePayload() const;
    void copyMenuSelection();
    void removeDraggedItem(lib::DropOperation operation);
    void configureButton(const std::shared_ptr<Button>& button,
                         lib::String text, lib::ResourceId image,
                         lib::Action action, lib::String tooltip);
};

/// MainWindowが使うDのAbsDialog派生型に対応する共通実装。
class ToolBarCustomDialog final : public editdialogsession::EditDialogSession {
public:
    using Services = lib::Services;
    using Window = lib::Window;
    using Dialog = lib::Dialog;
    using Container = lib::Container;

    ToolBarCustomDialog(Services services, std::shared_ptr<Window> owner,
                        lib::String title, lib::ResourceId icon,
                        ToolBarSettings tools, const ToolBarSettings& init,
                        Presentation presentation = {}, Callbacks callbacks = {});
    ToolBarCustomDialog(ToolBarSettings tools, const ToolBarSettings& init,
                        Presentation presentation = {}, Services services = {},
                        Callbacks callbacks = {})
        : ToolBarCustomDialog(std::move(services), {},
                              presentation.customizeToolBar, 0,
                              std::move(tools), init, std::move(presentation),
                              std::move(callbacks)) {}

    ToolBarCustomDialog(const ToolBarCustomDialog&) = delete;
    ToolBarCustomDialog& operator=(const ToolBarCustomDialog&) = delete;
    ToolBarCustomDialog(ToolBarCustomDialog&&) = delete;
    ToolBarCustomDialog& operator=(ToolBarCustomDialog&&) = delete;
    ~ToolBarCustomDialog();

    [[nodiscard]] bool noScenario() const noexcept override { return true; }
    [[nodiscard]] const std::shared_ptr<Dialog>& dialog() const noexcept {
        return dialog_;
    }
    [[nodiscard]] ToolBarSettings tools() const { return tools_; }
    [[nodiscard]] const std::unique_ptr<ToolBarCustomizer>& customizer() const noexcept {
        return customizer_;
    }
    [[nodiscard]] const Presentation& presentation() const noexcept {
        return presentation_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& okButton() const noexcept {
        return ok_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& cancelButton() const noexcept {
        return cancel_button_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Button>& applyButton() const noexcept {
        return apply_button_;
    }

    void setup(Container& area);
    [[nodiscard]] bool setup();
    [[nodiscard]] bool apply();
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return dialog_.get();
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return dirty_ || (apply_button_ && apply_button_->enabled());
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    void cancel();
    void show();
    void close();

private:
    Services services_{};
    std::shared_ptr<Window> owner_{};
    std::shared_ptr<Dialog> dialog_{};
    std::shared_ptr<lib::Panel> content_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};
    std::shared_ptr<lib::Button> apply_button_{};
    std::unique_ptr<ToolBarCustomizer> customizer_{};
    ToolBarSettings tools_{};
    ToolBarSettings initial_{};
    ToolBarSettings opened_{};
    ToolBarSettings committed_{};
    Presentation presentation_{};
    Callbacks callbacks_{};
    bool initialized_{};
    bool visible_{};
    bool dirty_{};
    // 共通の通知でC++の受信先が同期的に破棄されたり、新しい編集が始まった
    // りする場合がある。 古い適用・終了処理で、新しい操作を完了させない。
    std::uint64_t edit_revision_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};

    void finish(lib::DialogResult result);
    void markDirty();
    void saveGeometry();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtoolbar
