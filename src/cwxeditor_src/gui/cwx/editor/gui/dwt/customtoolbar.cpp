#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtoolbar.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace cwxeditor::cwx::editor::gui::dwt::customtoolbar {

namespace {

// 共通入力、ドラッグ、購読済みデリゲートに一つの寿命境界を使う。
template<class F>
auto guardOwner(const std::shared_ptr<void>& owner, F action) {
    return [life=std::weak_ptr<void>(owner), action=std::move(action)](auto&&... args)
        -> std::invoke_result_t<F&, decltype(args)...> {
        using Result = std::invoke_result_t<F&, decltype(args)...>;
        if (life.expired()) {
            if constexpr (!std::is_void_v<Result>) return Result{};
            else return;
        }
        return action(std::forward<decltype(args)>(args)...);
    };
}

template <typename T>
std::shared_ptr<T> makeComponent(const lib::Services& services,
                                 const lib::ComponentKind kind) {
    if (services.gui) {
        if (auto widget = services.gui->create_component(kind)) {
            if (auto result = std::dynamic_pointer_cast<T>(std::move(widget))) {
                return result;
            }
        }
    }
    return std::make_shared<T>();
}

[[nodiscard]] std::string replaceIndex(std::string pattern,
                                       const std::size_t index) {
    const std::string value = std::to_string(index);
    const std::array<std::string_view, 4> markers{
        "%1$s", "%s", "%1$d", "%d"};
    for (const auto marker : markers) {
        const auto position = pattern.find(marker);
        if (position != std::string::npos) {
            pattern.replace(position, marker.size(), value);
            return pattern;
        }
    }
    return pattern + " (" + value + ")";
}

[[nodiscard]] bool containsIndex(const std::vector<std::size_t>& values,
                                 const std::size_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] std::string menuValue(const MenuID value) {
    return std::to_string(static_cast<std::underlying_type_t<MenuID>>(value));
}

[[nodiscard]] bool isMenuValue(const MenuID id) {
    for (const auto member : enumutils::enumMembers<MenuID>()) {
        if (member.value == id) return true;
    }
    return false;
}

[[nodiscard]] std::optional<MenuID> menuIdForCommand(
    const std::string_view command) {
    if (command == "incSearch") return MenuID::IncSearch;
    if (command == "undo") return MenuID::Undo;
    if (command == "redo") return MenuID::Redo;
    if (command == "addTool") return MenuID::AddTool;
    if (command == "copy") return MenuID::Copy;
    if (command == "selectAll") return MenuID::SelectAll;
    if (command == "up") return MenuID::Up;
    if (command == "down") return MenuID::Down;
    if (command == "addBar") return MenuID::AddToolBar;
    if (command == "addGroup") return MenuID::AddToolGroup;
    if (command == "cut") return MenuID::Cut;
    if (command == "paste") return MenuID::Paste;
    if (command == "delete") return MenuID::Delete;
    if (command == "reset") return MenuID::ResetToolBarSettings;
    return std::nullopt;
}

}  // namespace

template <typename Component>
std::shared_ptr<Component> ToolBarCustomizer::createComponent(
    const lib::ComponentKind kind) const {
    return makeComponent<Component>(services_, kind);
}

class ToolBarCustomizer::CTUndo final : public undo::Undo {
public:
    explicit CTUndo(ToolBarCustomizer& owner) : owner_(&owner) { save(); }

    void undo() override { impl(); }
    void redo() override { impl(); }
    void dispose() override {}

private:
    ToolBarCustomizer* owner_{};
    std::vector<std::size_t> selected_{};
    ToolBarSettings old_tools_{};

    void save() {
        selected_ = owner_->selectedPath();
        old_tools_ = owner_->tools();
    }

    void impl() {
        const auto selected = selected_;
        const auto old_tools = old_tools_;
        save();
        owner_->tools_ = old_tools;
        owner_->updateTree();
        owner_->restorePath(selected);
        if (!owner_->refreshMenu()) return;
        owner_->refreshMenuStates();
        owner_->notifyChanged();
    }
};

ToolBarCustomizer::ToolBarCustomizer(
    Services services, const Parent parent, ToolBarSettings tools,
    const ToolBarSettings& init, Presentation presentation, Callbacks callbacks)
    : services_(std::move(services)), parent_(parent),
      presentation_(std::move(presentation)), callbacks_(std::move(callbacks)),
      tools_(std::move(tools)), initial_(init),
      undo_(presentation_.undoLimit ? presentation_.undoLimit() : presentation_.undoMax),
      clipboard_(services_.clipboard) {
    if (!presentation_.menuText) {
        presentation_.menuText = [](const MenuID id) {
            try {
                return enumutils::enumToString(id);
            } catch (...) {
                return std::string{};
            }
        };
    }
    if (!presentation_.menuImage) {
        presentation_.menuImage = [](const MenuID) { return lib::ResourceId{1}; };
    }
    construct();
    if (callbacks_.subscribeUndoMax)
        undo_max_unsubscribe_ = callbacks_.subscribeUndoMax(guardOwner(lifetime_, [this] { refUndoMax(); }));
}

ToolBarCustomizer::~ToolBarCustomizer() {
    lifetime_.reset();
    disconnectNotifications();
    if (services_.drag_drop) {
        if (menu_list_) services_.drag_drop->unbind(menu_list_);
        if (tool_tree_) services_.drag_drop->unbind(tool_tree_);
    }
    if (menu_list_) {
        menu_list_->on_focus = {};
        menu_list_->on_selection_changed = {};
        menu_list_->on_pointer = {};
        menu_list_->on_key_down = {};
    }
    if (tool_tree_) {
        tool_tree_->on_focus = {};
        tool_tree_->on_selection_changed = {};
        tool_tree_->on_expansion_changed = {};
        tool_tree_->on_pointer = {};
        tool_tree_->on_key_down = {};
    }
    if (menu_list_menu_) menu_list_menu_->on_command = {};
    if (tool_tree_menu_) tool_tree_menu_->on_command = {};
    for (const auto& button : {add_button_, remove_button_, add_bar_button_,
                               add_group_button_, up_button_, down_button_}) {
        if (button) button->on_click = {};
    }
}

void ToolBarCustomizer::disconnectNotifications() {
    const auto disconnect=std::move(undo_max_unsubscribe_);
    undo_max_unsubscribe_={};
    if (disconnect) disconnect();
}

void ToolBarCustomizer::construct() {
    root_ = createComponent<Panel>(lib::ComponentKind::Panel);
    root_->accessibility_name = "toolbar customizer";
    root_->set_layout(std::make_shared<lib::FillLayout>());
    if (parent_) parent_->add(root_);

    split_ = std::make_shared<SplitPane>(splitpane::Horizontal);
    split_->setWeights({presentation_.sashLeft, presentation_.sashRight});
    split_->addSelectionListener(guardOwner(lifetime_, [this](splitpane::SelectionEvent&) {
        if (!callbacks_.saveSashWeights || !split_) return;
        const auto weights = split_->getWeights();
        if (weights.size() >= 2U)
            callbacks_.saveSashWeights(weights[0], weights[1]);
    }));
    split_->accessibility_name = "toolbar customizer panes";
    root_->add(split_);

    menu_panel_ = createComponent<Panel>(lib::ComponentKind::Panel);
    menu_panel_->set_layout(std::make_shared<lib::GridLayout>());
    auto menu_grid = std::static_pointer_cast<lib::GridLayout>(menu_panel_->layout());
    menu_grid->columns = 2;
    menu_grid->equal_width = false;
    split_->add(menu_panel_);

    menu_list_ = createComponent<Table>(lib::ComponentKind::TableView);
    menu_list_->multi_select = true;
    menu_list_->full_row_selection = true;
    menu_list_->accessibility_name = "available toolbar commands";
    menu_list_->on_focus = guardOwner(lifetime_, [this] { focusMenuList(); });
    // DのMenuListTCPDに合わせ、コマンド一覧はコピーだけを許可し、切り取り・貼り付けは行わない。
    menu_list_->on_key_down = guardOwner(lifetime_, [this](lib::KeyEvent& event) {
        (void)dmenu::handleClipboardShortcut(event, {{},
            [this] { return menu_list_ && !menu_list_->selection.empty(); }, {},
            {}, [this] { copyMenuSelection(); }, {}});
    });
    menu_list_->on_selection_changed = guardOwner(lifetime_, [this](const std::vector<std::size_t>& values) {
        focusMenuList();
        setMenuSelection(values);
        refreshToolbar();
    });
    menu_list_->on_pointer = guardOwner(lifetime_, [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !menu_list_menu_ ||
            !services_.gui)
            return;
        if (event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < menu_list_->rows.size())
            setMenuSelection({static_cast<std::size_t>(event.item_index)});
        refreshMenuStates();
        services_.gui->show_context_menu(menu_list_menu_, menu_list_, event.position);
        event.handled = true;
    });
    auto menu_list_item = lib::LayoutItem{};
    menu_list_item.column = 0;
    menu_list_item.grow = 1.0F;
    menu_list_->set_layout_item(menu_list_item);
    menu_panel_->add(menu_list_);

    auto menu_buttons = createComponent<Panel>(lib::ComponentKind::Panel);
    menu_buttons->set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical));
    auto menu_button_item = lib::LayoutItem{};
    menu_button_item.column = 1;
    menu_button_item.vertical = lib::Align::Center;
    menu_buttons->set_layout_item(menu_button_item);
    menu_panel_->add(menu_buttons);

    add_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(add_button_, presentation_.addTool, menuImage(MenuID::AddTool),
                    guardOwner(lifetime_, [this] { addMenu(); }), presentation_.addTool);
    menu_buttons->add(add_button_);
    remove_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(remove_button_, presentation_.removeTool, menuImage(MenuID::Delete),
                    guardOwner(lifetime_, [this] { removeMenu(); }), presentation_.removeTool);
    auto add_item = lib::LayoutItem{};
    add_item.row = 0;
    add_button_->set_layout_item(add_item);
    auto remove_item = lib::LayoutItem{};
    remove_item.row = 1;
    remove_button_->set_layout_item(remove_item);
    menu_buttons->add(remove_button_);

    tree_panel_ = createComponent<Panel>(lib::ComponentKind::Panel);
    tree_panel_->set_layout(std::make_shared<lib::GridLayout>());
    auto tree_grid = std::static_pointer_cast<lib::GridLayout>(tree_panel_->layout());
    tree_grid->columns = 1;
    split_->add(tree_panel_);

    tool_tree_ = createComponent<Tree>(lib::ComponentKind::TreeView);
    tool_tree_->multi_select = false;
    tool_tree_->accessibility_name = "toolbar layout";
    tool_tree_->on_focus = guardOwner(lifetime_, [this] { focusToolTree(); });
    tool_tree_->on_key_down = guardOwner(lifetime_, [this](lib::KeyEvent& event) {
        (void)dmenu::handleClipboardShortcut(event, {
            [this] { return canDoT(); }, [this] { return canDoC(); },
            [this] { return canDoP(); },
            [this] { cut(); }, [this] { copy(); }, [this] { paste(); }});
    });
    // DのcloneItemはツールバー群の移動時に展開状態を保持す
    // る。単体表示側の状態も、バックエンドのツリー操作と同期する。
    if (!lib::editor_window_host_options().all_mode) {
        tool_tree_->on_expansion_changed = guardOwner(lifetime_,
            [this](const TreeItemId id, const bool expanded) {
                if (auto* value = node(id)) value->expanded = expanded;
            });
    }
    tool_tree_->on_selection_changed = guardOwner(lifetime_, [this](const std::vector<TreeItemId>& values) {
        focusToolTree();
        setTreeSelection(values);
        refreshToolbar();
    });
    tool_tree_->on_pointer = guardOwner(lifetime_, [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !tool_tree_menu_ ||
            !services_.gui)
            return;
        if (event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < tool_tree_->nodes.size())
            selectTree(tool_tree_->nodes[static_cast<std::size_t>(event.item_index)].id, false);
        refreshMenuStates();
        services_.gui->show_context_menu(tool_tree_menu_, tool_tree_, event.position);
        event.handled = true;
    });
    auto tree_item = lib::LayoutItem{};
    tree_item.row = 0;
    tree_item.grow = 1.0F;
    tool_tree_->set_layout_item(tree_item);
    tree_panel_->add(tool_tree_);

    auto tree_buttons = createComponent<Panel>(lib::ComponentKind::Panel);
    tree_buttons->set_layout(std::make_shared<lib::GridLayout>());
    auto tree_button_grid = std::static_pointer_cast<lib::GridLayout>(tree_buttons->layout());
    tree_button_grid->columns = 3;
    auto tree_buttons_item = lib::LayoutItem{};
    tree_buttons_item.horizontal = lib::Align::End;
    tree_buttons_item.row = 1;
    tree_buttons->set_layout_item(tree_buttons_item);
    tree_panel_->add(tree_buttons);

    auto add_buttons = createComponent<Panel>(lib::ComponentKind::Panel);
    add_buttons->set_layout(std::make_shared<lib::GridLayout>());
    auto add_buttons_grid = std::static_pointer_cast<lib::GridLayout>(add_buttons->layout());
    add_buttons_grid->columns = 2;
    auto add_buttons_item = lib::LayoutItem{};
    add_buttons_item.column = 0;
    add_buttons->set_layout_item(add_buttons_item);
    tree_buttons->add(add_buttons);

    add_bar_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(add_bar_button_, presentation_.addToolBar, 0,
                    guardOwner(lifetime_, [this] { addBar(); }), presentation_.addToolBar);
    auto add_bar_item = lib::LayoutItem{};
    add_bar_item.column = 0;
    add_bar_button_->set_layout_item(add_bar_item);
    add_buttons->add(add_bar_button_);
    add_group_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(add_group_button_, presentation_.addToolGroup, 0,
                    guardOwner(lifetime_, [this] { addGroup(); }), presentation_.addToolGroup);
    auto add_group_item = lib::LayoutItem{};
    add_group_item.column = 1;
    add_group_button_->set_layout_item(add_group_item);
    add_buttons->add(add_group_button_);
    up_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(up_button_, presentation_.upSelection,
                    menuImage(MenuID::Up), guardOwner(lifetime_, [this] { up(); }), presentation_.upSelection);
    auto up_item = lib::LayoutItem{};
    up_item.column = 1;
    up_button_->set_layout_item(up_item);
    tree_buttons->add(up_button_);
    down_button_ = createComponent<Button>(lib::ComponentKind::Button);
    configureButton(down_button_, presentation_.downSelection,
                    menuImage(MenuID::Down), guardOwner(lifetime_, [this] { down(); }), presentation_.downSelection);
    auto down_item = lib::LayoutItem{};
    down_item.column = 2;
    down_button_->set_layout_item(down_item);
    tree_buttons->add(down_button_);

    menu_search_ = std::make_unique<incsearch::IncSearch>(
        services_, menu_list_.get(), guardOwner(lifetime_, [this] { return canIncSearch(); }));
    menu_search_->modEvent.push_back(guardOwner(lifetime_, [this] { refreshMenu(); }));

    constructMenus();
    constructDragDrop();
    updateTree();
    if (!refreshMenu()) return;
    refreshMenuStates();
}

void ToolBarCustomizer::configureButton(const std::shared_ptr<Button>& button,
                                        lib::String text,
                                        const lib::ResourceId image,
                                        lib::Action action,
                                        lib::String tooltip) {
    if (!button) return;
    button->text = std::move(text);
    button->icon = image;
    button->set_tooltip(std::move(tooltip));
    button->on_click = std::move(action);
}

void ToolBarCustomizer::constructMenus() {
    const auto decorate = guardOwner(lifetime_, [this](Menu& menu) {
        for (auto& item : menu.items) {
            if (item.separator) continue;
            const auto id = menuIdForCommand(item.command);
            if (!id) continue;
            item.icon = menuImage(*id);
            if (presentation_.menuShortcut)
                item.shortcut = presentation_.menuShortcut(*id);
            if (presentation_.menuChecked)
                item.checked = presentation_.menuChecked(*id);
        }
    });

    menu_list_menu_ = std::make_shared<Menu>(true);
    menu_list_menu_->accessibility_name = "available toolbar commands menu";
    menu_list_menu_->items = {
        {presentation_.incSearch, "incSearch"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.undo, "undo"},
        {presentation_.redo, "redo"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.addTool, "addTool"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.copy, "copy"},
        {"", {}, {}, 0, true, false, false, true},
        {"すべて選択", "selectAll"},
    };
    decorate(*menu_list_menu_);
    menu_list_menu_->on_command = guardOwner(lifetime_, [this](const std::string_view command) {
        if (command == "incSearch") startIncSearch();
        else if (command == "undo") {
            const auto life=std::weak_ptr<void>(lifetime_);
            (void)undo_.undo();
            if (life.expired()) return;
            refreshMenuStates();
        }
        else if (command == "redo") {
            const auto life=std::weak_ptr<void>(lifetime_);
            (void)undo_.redo();
            if (life.expired()) return;
            refreshMenuStates();
        }
        else if (command == "addTool") addMenu();
        else if (command == "copy") copyMenuSelection();
        else if (command == "selectAll") {
            std::vector<std::size_t> values;
            values.reserve(menu_list_->rows.size());
            for (std::size_t i = 0; i < menu_list_->rows.size(); ++i) values.push_back(i);
            setMenuSelection(std::move(values));
        }
    });

    tool_tree_menu_ = std::make_shared<Menu>(true);
    tool_tree_menu_->accessibility_name = "toolbar layout menu";
    tool_tree_menu_->items = {
        {presentation_.undo, "undo"},
        {presentation_.redo, "redo"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.upSelection, "up"},
        {presentation_.downSelection, "down"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.addToolBar, "addBar"},
        {presentation_.addToolGroup, "addGroup"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.cut, "cut"},
        {presentation_.copy, "copy"},
        {presentation_.paste, "paste"},
        {presentation_.deleteText, "delete"},
        {"", "", {}, 0, true, false, false, true},
        {presentation_.resetToolBarSettings, "reset"},
    };
    decorate(*tool_tree_menu_);
    tool_tree_menu_->on_command = guardOwner(lifetime_, [this](const std::string_view command) {
        if (command == "undo") {
            const auto life=std::weak_ptr<void>(lifetime_);
            (void)undo_.undo();
            if (life.expired()) return;
            refreshMenuStates();
        }
        else if (command == "redo") {
            const auto life=std::weak_ptr<void>(lifetime_);
            (void)undo_.redo();
            if (life.expired()) return;
            refreshMenuStates();
        }
        else if (command == "up") up();
        else if (command == "down") down();
        else if (command == "addBar") addBar();
        else if (command == "addGroup") addGroup();
        else if (command == "cut") cut();
        else if (command == "copy") copy();
        else if (command == "paste") paste();
        else if (command == "delete") del();
        else if (command == "reset") resetTools();
    });
}

void ToolBarCustomizer::constructDragDrop() {
    if (!services_.drag_drop) return;

    lib::DragSourceBinding menu_source;
    menu_source.source = menu_list_;
    menu_source.allowed = lib::DropOperation::Move;
    menu_source.provide_data = guardOwner(lifetime_, [this] { return menuListPayload(); });
    menu_source.completed = [](const lib::DropOperation) {};
    services_.drag_drop->bind_source(std::move(menu_source));

    lib::DropTargetBinding menu_target;
    menu_target.target = menu_list_;
    menu_target.accepted_formats = {xmlbytestransfer::XML_BYTES_FORMAT.data()};
    menu_target.on_enter = guardOwner(lifetime_, [this](lib::DragEvent& event) {
        event.selected = event.target_index && *event.target_index < menu_list_->rows.size()
            ? lib::DropOperation::Move : lib::DropOperation::None;
    });
    menu_target.on_over = menu_target.on_enter;
    menu_target.on_drop = guardOwner(lifetime_, [this](lib::DragEvent& event) {
        // Dは利用可能コマンドの表へのドロップを、有効な行
        // の上だけで受け付ける。表の空白は挿入先ではない。
        if (!event.target_index || *event.target_index >= menu_list_->rows.size()) {
            event.selected = lib::DropOperation::None;
            return;
        }
        lib::String serialized;
        if (!parseDragPayload(event.data, serialized)) return;
        try {
            const auto root = XNode::parse(serialized);
            const auto name = root.name();
            if (name == "toolBar" || name == "toolGroup" || name == "tool") {
                event.selected = lib::DropOperation::Move;
                event.handled = true;
            }
        } catch (...) {
            event.selected = lib::DropOperation::None;
        }
    });
    services_.drag_drop->bind_target(std::move(menu_target));

    lib::DragSourceBinding tree_source;
    tree_source.source = tool_tree_;
    tree_source.allowed = lib::DropOperation::Move;
    tree_source.provide_data = guardOwner(lifetime_, [this] { return treePayload(); });
    tree_source.completed = guardOwner(lifetime_, [this](const lib::DropOperation operation) {
        removeDraggedItem(operation);
    });
    services_.drag_drop->bind_source(std::move(tree_source));

    lib::DropTargetBinding tree_target;
    tree_target.target = tool_tree_;
    tree_target.accepted_formats = {xmlbytestransfer::XML_BYTES_FORMAT.data()};
    tree_target.on_enter = guardOwner(lifetime_, [this](lib::DragEvent& event) {
        event.selected = event.target_index && *event.target_index < tool_tree_->nodes.size()
            ? lib::DropOperation::Move : lib::DropOperation::None;
    });
    tree_target.on_over = tree_target.on_enter;
    tree_target.on_drop = guardOwner(lifetime_, [this](lib::DragEvent& event) {
        lib::String serialized;
        if (!parseDragPayload(event.data, serialized)) return;
        // target_indexは受け渡し方式に依存しないホバー位置。
        // 選択は以前のクリックに属し得るため、挿入位置の決定に使わない。
        const auto target = event.target_index && *event.target_index < tool_tree_->nodes.size()
            ? tool_tree_->nodes[*event.target_index].id : TreeItemId{};
        if (target == 0) return;
        try {
            const auto root = XNode::parse(serialized);
            if (root.name() != "toolBar" && root.name() != "toolGroup" &&
                root.name() != "tool" && root.name() != "tools") return;
            if (dropXml(serialized, target)) {
                event.selected = lib::DropOperation::Move;
                event.handled = true;
            }
        } catch (...) {
            event.selected = lib::DropOperation::None;
        }
    });
    services_.drag_drop->bind_target(std::move(tree_target));
}

void ToolBarCustomizer::removeDraggedItem(const lib::DropOperation operation) {
    if (operation != lib::DropOperation::Move || drag_item_ == 0) {
        drag_item_ = 0;
        return;
    }
    const auto item = drag_item_;
    drag_item_ = 0;
    if (!node(item)) return;
    store();
    removeMenuImpl(item);
    updateBarAndGroupText();
    if (!refreshMenu()) return;
    refreshMenuStates();
    notifyChanged();
}

void ToolBarCustomizer::updateTree() {
    nodes_.clear();
    added_.clear();
    next_id_ = 1;
    for (const auto& bar : tools_.tools) {
        const auto bar_id = next_id_++;
        nodes_.push_back({bar_id, 0, {}, presentation_.toolBarImage,
                          std::nullopt, true});
        auto group_id = next_id_++;
        nodes_.push_back({group_id, bar_id, {}, presentation_.toolGroupImage,
                          std::nullopt, true});
        for (const auto& tool : bar) {
            if (tool.separator || tool.menu == MenuID::None) {
                nodes_.back().expanded = true;
                group_id = next_id_++;
                nodes_.push_back({group_id, bar_id, {}, presentation_.toolGroupImage,
                                  std::nullopt, true});
                continue;
            }
            const auto id = next_id_++;
            nodes_.push_back({id, group_id, menuText(tool.menu),
                              menuImage(tool.menu), tool.menu, true});
            added_.insert(tool.menu);
        }
    }
    updateBarAndGroupText();
    syncTreeComponent();
    if (!nodes_.empty()) {
        std::vector<TreeItemId> value{nodes_.front().id};
        setTreeSelection(std::move(value));
    } else {
        setTreeSelection({});
    }
    refreshToolbar();
}

void ToolBarCustomizer::updateBarAndGroupText() {
    const auto bars = children(0);
    for (std::size_t bar_index = 0; bar_index < bars.size(); ++bar_index) {
        if (auto* bar = node(bars[bar_index])) {
            bar->text = formatIndex(presentation_.toolBarName, bar_index + 1U);
            const auto groups = children(bar->id);
            for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                if (auto* group = node(groups[group_index])) {
                    group->text = formatIndex(presentation_.toolGroupName,
                                              group_index + 1U);
                }
            }
        }
    }
    syncTreeComponent();
}

bool ToolBarCustomizer::refreshMenu() {
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=notification_revision_;
    const auto current = [this, life, revision] {
        return !life.expired() && revision == notification_revision_;
    };
    const auto previous = menu_selection_.empty() ||
                                  menu_selection_.front() >= menu_list_->rows.size()
                              ? MenuID::None
                              : static_cast<MenuID>(std::stoi(
                                    menu_list_->rows[menu_selection_.front()]
                                        .cells.front().value));
    std::vector<lib::TableRow> rows;
    std::vector<std::size_t> selection;
    for (const auto member : enumutils::enumMembers<MenuID>()) {
        const auto id = member.value;
        if (!canAppendMenu(id)) continue;
        const auto image = menuImage(id);
        if (!current()) return false;
        if (image == 0) continue;
        const auto name = menuText(id);
        if (!current()) return false;
        if (menu_search_ && !menu_search_->match(name)) continue;
        lib::TableRow row;
        row.cells.push_back({name, menuValue(id), image, false, true});
        if (id == previous) selection.push_back(rows.size());
        rows.push_back(std::move(row));
    }
    menu_selection_ = std::move(selection);
    if (menu_selection_.empty() && !rows.empty()) {
        menu_selection_.push_back(0);
    }
    const auto list = menu_list_;
    if (!lib::replace_table_rows(list, std::move(rows),
                                 menu_selection_, std::nullopt, current)) return false;
    if (!current()) return false;
    // DのrefreshMenuは、利用可能コマンドの選択を戻し、見える位置へ移す。
    if (!menu_selection_.empty())
        (void)lib::reveal_table_row(*list, menu_selection_.front());
    if (!current()) return false;
    refreshMenuStates();
    if (!current()) return false;
    refreshToolbar();
    return current();
}

void ToolBarCustomizer::refreshMenuStates() {
    const auto can_add = canAddMenu();
    const auto can_remove = canRemoveMenu();
    const auto can_up = canUp();
    const auto can_down = canDown();
    if (add_button_) add_button_->set_enabled(can_add);
    if (remove_button_) remove_button_->set_enabled(can_remove);
    if (up_button_) up_button_->set_enabled(can_up);
    if (down_button_) down_button_->set_enabled(can_down);
    if (menu_list_menu_) {
        auto items = menu_list_menu_->items;
        for (auto& item : items) {
            if (const auto id = menuIdForCommand(item.command)) {
                item.icon = menuImage(*id);
                if (presentation_.menuShortcut)
                    item.shortcut = presentation_.menuShortcut(*id);
                if (presentation_.menuChecked)
                    item.checked = presentation_.menuChecked(*id);
            }
            if (item.command == "undo") item.enabled = undo_.canUndo();
            else if (item.command == "redo") item.enabled = undo_.canRedo();
            else if (item.command == "addTool") item.enabled = can_add;
            else if (item.command == "copy") item.enabled = !menu_selection_.empty();
            else if (item.command == "selectAll")
                item.enabled = menu_list_->rows.size() != menu_selection_.size();
            else if (item.command == "incSearch") item.enabled = canIncSearch();
        }
        (void)lib::replace_menu_items(menu_list_menu_, std::move(items));
    }
    if (tool_tree_menu_) {
        auto items = tool_tree_menu_->items;
        for (auto& item : items) {
            if (const auto id = menuIdForCommand(item.command)) {
                item.icon = menuImage(*id);
                if (presentation_.menuShortcut)
                    item.shortcut = presentation_.menuShortcut(*id);
                if (presentation_.menuChecked)
                    item.checked = presentation_.menuChecked(*id);
            }
            if (item.command == "undo") item.enabled = undo_.canUndo();
            else if (item.command == "redo") item.enabled = undo_.canRedo();
            else if (item.command == "up") item.enabled = can_up;
            else if (item.command == "down") item.enabled = can_down;
            else if (item.command == "addBar" || item.command == "addGroup")
                item.enabled = true;
            else if (item.command == "cut" || item.command == "copy" ||
                     item.command == "delete") item.enabled = canDoC();
            else if (item.command == "paste") item.enabled = canDoP();
            else if (item.command == "reset") item.enabled = !sameTools(tools(), initial_);
        }
        (void)lib::replace_menu_items(tool_tree_menu_, std::move(items));
    }
}

void ToolBarCustomizer::notifyChanged(const bool refresh) {
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=++notification_revision_;
    const auto modified=callbacks_.modified;
    const auto callbacks=modEvent;
    if (refresh) refreshToolbar();
    if (life.expired() || revision!=notification_revision_) return;
    if (modified) modified();
    if (life.expired() || revision!=notification_revision_) return;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired() || revision!=notification_revision_) return;
    }
    refreshMenuStates();
}

void ToolBarCustomizer::refreshToolbar() const {
    const auto refresh=callbacks_.refreshToolBar;
    if (refresh) refresh();
}

void ToolBarCustomizer::store() {
    undo_ += std::make_shared<CTUndo>(*this);
    refreshMenuStates();
}

void ToolBarCustomizer::refUndoMax() {
    const auto get=presentation_.undoLimit;
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto value=get ? get() : presentation_.undoMax;
    if (life.expired()) return;
    presentation_.undoMax=value;
    undo_.max(value);
    if (life.expired()) return;
    refreshMenuStates();
}

ToolBarSettings ToolBarCustomizer::tools() const {
    ToolBarSettings value;
    for (const auto bar_id : children(0)) {
        std::vector<Tool> bar;
        const auto groups = children(bar_id);
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            if (group_index != 0) bar.push_back(Tool::opCall());
            for (const auto menu_id : children(groups[group_index])) {
                const auto* item = node(menu_id);
                if (item && item->menu) bar.emplace_back(*item->menu);
            }
        }
        value.tools.push_back(std::move(bar));
    }
    return value;
}

std::shared_ptr<dmenu::ToolBar> ToolBarCustomizer::nativeToolBar(
    const std::size_t index) const {
    const auto current = tools();
    if (index >= current.tools.size()) return {};

    auto result = std::make_shared<dmenu::ToolBar>();
    const auto text_provider = presentation_.menuText;
    const auto image_provider = presentation_.menuImage;
    const auto tooltip_provider = presentation_.menuTooltip;
    const auto enabled_provider = presentation_.menuEnabled;
    const auto checked_provider = presentation_.menuChecked;
    const auto command_provider = presentation_.menuCommand;
    const auto dropdown_provider = presentation_.menuDropDown;
    std::unordered_map<std::string, MenuID> commands;

    for (const auto& tool : current.tools[index]) {
        if (tool.separator || tool.menu == MenuID::None) {
            (void)result->append({}, {}, 0U, dmenu::MenuSeparator);
            continue;
        }

        const auto id = tool.menu;
        const auto text = text_provider ? text_provider(id) : menuText(id);
        const auto image = image_provider ? image_provider(id) : menuImage(id);
        const auto tooltip = tooltip_provider ? tooltip_provider(id) : text;
        const auto enabled = [enabled_provider, id] {
            return !enabled_provider || enabled_provider(id);
        };
        const auto checked = [checked_provider, id] {
            return checked_provider && checked_provider(id);
        };
        const auto drop_down = dropdown_provider ? dropdown_provider(id) : nullptr;
        const auto style = drop_down ? dmenu::ToolDropDown : dmenu::MenuPush;

        dmenu::MenuData data;
        data.id = id;
        data.enabled = enabled;
        lib::Action command;
        if (command_provider) {
            command = [command_provider, id] { command_provider(id); };
        }
        auto item = result->append(tooltip, text, image, style, std::move(data),
                                  {}, std::move(command));
        if (!item.valid()) continue;

        const auto command_name = std::string("cwx.customtoolbar.") + menuValue(id);
        item.native().command = command_name;
        commands.emplace(command_name, id);
        item.setEnabled(enabled());
        item.native().checked = checked();
        if (auto* entry = result->entry(item.index())) {
            entry->menu = drop_down;
            const auto item_index = item.index();
            // 項目はこのツールバーに属する。弱い所有関係でC++の参照循
            // 環を避け、安定したインデックスで項目の再確保にも対応する。
            entry->refresh = [weak=std::weak_ptr<dmenu::ToolBar>(result), item_index, id, text_provider,
                              image_provider, tooltip_provider,
                              enabled_provider, checked_provider] {
                const auto result=weak.lock();
                if (!result || item_index >= result->items.size()) return;
                // プロバイダーはツールバー項目を追加し得る。拡張可能な二つのベク
                // ターへの参照を取る前に、プロバイダーからの読み取りを完了する。
                auto native = result->items[item_index];
                if (text_provider) native.text = text_provider(id);
                if (image_provider) native.icon = image_provider(id);
                native.enabled = !enabled_provider || enabled_provider(id);
                native.checked = checked_provider && checked_provider(id);
                const auto tooltip = tooltip_provider ? tooltip_provider(id) : lib::String{};
                if (item_index >= result->items.size()) return;
                result->items[item_index] = native;
                if (auto* current = result->entry(item_index)) {
                    if (tooltip_provider) current->tooltip = tooltip;
                    if (image_provider) current->image = native.icon;
                }
            };
        }
    }
    result->on_command = [command_provider, commands = std::move(commands)](
                             const std::string_view command) {
        if (!command_provider) return;
        const auto found = commands.find(std::string(command));
        if (found != commands.end()) command_provider(found->second);
    };
    // コマンドや状態変更後と同じ、実行時に解決する
    // 更新経路を使う。ネイティブバックエンドへ渡す
    // 前にドロップダウンの子も更新し、初期の無効・
    // 選択状態を古い既定値で描かないようにする。
    result->refreshItems();
    return result;
}

std::vector<std::shared_ptr<dmenu::ToolBar>> ToolBarCustomizer::nativeToolBars()
    const {
    const auto current = tools();
    std::vector<std::shared_ptr<dmenu::ToolBar>> result;
    result.reserve(current.tools.size());
    for (std::size_t index = 0; index < current.tools.size(); ++index) {
        if (auto bar = nativeToolBar(index)) result.push_back(std::move(bar));
    }
    return result;
}

void ToolBarCustomizer::setTools(ToolBarSettings value) {
    tools_ = std::move(value);
    updateTree();
    if (!refreshMenu()) return;
    refreshMenuStates();
    notifyChanged();
}

bool ToolBarCustomizer::canIncSearch() const {
    for (const auto member : enumutils::enumMembers<MenuID>()) {
        if (canAppendMenu(member.value) && menuImage(member.value) != 0) return true;
    }
    return false;
}

bool ToolBarCustomizer::canUp() const noexcept {
    const auto id = selectedTreeItem();
    if (id == 0) return false;
    const auto index = childIndex(id);
    return index > 0;
}

bool ToolBarCustomizer::canDown() const noexcept {
    const auto id = selectedTreeItem();
    if (id == 0) return false;
    const auto parent = node(id)->parent;
    return childIndex(id) + 1U < children(parent).size();
}

bool ToolBarCustomizer::canAddMenu() const noexcept {
    return !menu_selection_.empty() && menu_selection_.front() < menu_list_->rows.size();
}

bool ToolBarCustomizer::canRemoveMenu() const noexcept {
    return selectedTreeItem() != 0;
}

bool ToolBarCustomizer::canAppendMenu(const MenuID id) const {
    if (id == MenuID::None || added_.contains(id)) return false;
    return menu::isMainToolBarMenu(id);
}

bool ToolBarCustomizer::canDoTCPD() const noexcept {
    return treeFocused();
}

bool ToolBarCustomizer::canDoT() const noexcept { return canDoC(); }
bool ToolBarCustomizer::canDoC() const noexcept { return !tree_selection_.empty(); }
bool ToolBarCustomizer::canDoP() const {
    // Dは共通のXMLクリップボード判定で貼り付けを有効にする。有効な空グルー
    // プも含め、ノードと対象の検証はpasteXml/fromNodeで行う。
    return xmlbytestransfer::CBisXML(clipboard_);
}
bool ToolBarCustomizer::canDoD() const noexcept { return canDoC(); }

void ToolBarCustomizer::startIncSearch() {
    if (menu_search_) menu_search_->startIncSearch();
}

void ToolBarCustomizer::moveSelection(const int direction) {
    if (selectedTreeItem() == 0) return;
    const auto selected = selectedTreeItem();
    const auto parent = node(selected)->parent;
    const auto siblings = children(parent);
    const auto index = childIndex(selected);
    const auto target_index = direction < 0 ? index - 1U : index + 1U;
    if (direction < 0 && index == 0) return;
    if (direction > 0 && target_index >= siblings.size()) return;
    const auto target = siblings[target_index];
    const auto selected_range = subtreeRange(selected);
    const auto target_range = subtreeRange(target);
    if (direction < 0) {
        std::rotate(nodes_.begin() + static_cast<std::ptrdiff_t>(target_range.first),
                    nodes_.begin() + static_cast<std::ptrdiff_t>(selected_range.first),
                    nodes_.begin() + static_cast<std::ptrdiff_t>(selected_range.second));
    } else {
        std::rotate(nodes_.begin() + static_cast<std::ptrdiff_t>(selected_range.first),
                    nodes_.begin() + static_cast<std::ptrdiff_t>(target_range.first),
                    nodes_.begin() + static_cast<std::ptrdiff_t>(target_range.second));
    }
    updateBarAndGroupText();
    syncTreeComponent();
    selectTree(selected);
    notifyChanged();
}

void ToolBarCustomizer::up() {
    if (!canUp()) return;
    store();
    moveSelection(-1);
}

void ToolBarCustomizer::down() {
    if (!canDown()) return;
    store();
    moveSelection(1);
}

void ToolBarCustomizer::addBar() {
    const auto selection = selectedTreeItem();
    store();
    const auto item = addBarImpl(selection);
    updateBarAndGroupText();
    selectTree(item);
    if (!refreshMenu()) return;
    notifyChanged();
}

ToolBarCustomizer::TreeItemId ToolBarCustomizer::addBarImpl(
    const TreeItemId selection) {
    const auto item = Node{next_id_++, 0, {}, presentation_.toolBarImage,
                           std::nullopt, true};
    std::size_t position = nodes_.size();
    if (selection != 0 && node(selection)) {
        const auto top = topItem(selection);
        const auto range = subtreeRange(top);
        position = range.first;
    }
    nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(position), item);
    return item.id;
}

void ToolBarCustomizer::addGroup() {
    const auto selection = selectedTreeItem();
    if (selection == 0) return;
    store();
    const auto item = addGroupImpl(selection);
    updateBarAndGroupText();
    selectTree(item);
    if (!refreshMenu()) return;
    notifyChanged();
}

ToolBarCustomizer::TreeItemId ToolBarCustomizer::addGroupImpl(
    const TreeItemId selection) {
    const auto* selected = node(selection);
    if (!selected) return 0;
    TreeItemId parent = 0;
    std::size_t position = nodes_.size();
    if (selected->parent == 0) {
        parent = selected->id;
        position = subtreeRange(selected->id).second;
    } else if (node(selected->parent)->parent == 0) {
        parent = selected->parent;
        position = subtreeRange(selected->id).first;
    } else {
        parent = node(selected->parent)->parent;
        position = subtreeRange(parent).second;
    }
    const auto item = Node{next_id_++, parent, {}, presentation_.toolGroupImage,
                           std::nullopt, true};
    nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(position), item);
    if (node(parent)) node(parent)->expanded = true;
    return item.id;
}

void ToolBarCustomizer::addMenu() {
    if (!canAddMenu()) return;
    const auto selection = selectedTreeItem();
    if (selection == 0) return;
    const auto values = menuIdsFromRows();
    if (values.empty()) return;
    store();
    const auto item = addMenuImpl(selection, values);
    if (item != 0) selectTree(item);
    updateBarAndGroupText();
    // Dは新しく挿入したコマンドが表示領域外でも、見える位置へ移す。
    if (item != 0 && tool_tree_) (void)lib::reveal_tree_item(*tool_tree_, item);
    if (!refreshMenu()) return;
    notifyChanged();
}

ToolBarCustomizer::TreeItemId ToolBarCustomizer::addMenuImpl(
    const TreeItemId selection, const std::vector<MenuID>& values) {
    if (!node(selection)) return 0;
    TreeItemId parent = 0;
    std::size_t position = nodes_.size();
    // vectorへの挿入はNodeポインターを無効にし得る。DのTreeItem
    // の同一性は安定しているため、最初のグループ作成をまたいで選択IDを保持する。
    const auto selected = *node(selection);
    if (selected.parent == 0) {
        if (children(selected.id).empty()) (void)addGroupImpl(selected.id);
        const auto groups = children(selected.id);
        if (groups.empty()) return 0;
        parent = groups.front();
        // Dはバー自身を選択した場合、最初のグループの位置
        // 0へ挿入する。子孫はグループの直後へ配置する。
        position = subtreeRange(parent).first + 1U;
    } else if (node(selected.parent)->parent == 0) {
        parent = selected.id;
        position = subtreeRange(parent).second;
    } else {
        parent = selected.parent;
        position = subtreeRange(selected.id).first;
    }
    if (parent == 0 || !node(parent)) return 0;

    TreeItemId last = 0;
    for (const auto id : values) {
        if (!canAppendMenu(id)) continue;
        const auto item = Node{next_id_++, parent, menuText(id), menuImage(id), id, true};
        nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(position), item);
        ++position;
        added_.insert(id);
        last = item.id;
    }
    node(parent)->expanded = true;
    syncTreeComponent();
    return last;
}

void ToolBarCustomizer::removeMenu() {
    if (!canRemoveMenu()) return;
    const auto selection = selectedTreeItem();
    store();
    removeMenuImpl(selection);
    updateBarAndGroupText();
    if (!refreshMenu()) return;
    refreshMenuStates();
    notifyChanged();
}

void ToolBarCustomizer::removeMenuImpl(const TreeItemId selection) {
    if (!node(selection)) return;
    removeSubtree(selection);
    tree_selection_.clear();
    syncTreeComponent();
}

void ToolBarCustomizer::removeSubtree(const TreeItemId id) {
    for (const auto child : children(id)) removeSubtree(child);
    if (const auto* item = node(id); item && item->menu) added_.erase(*item->menu);
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [id](const Node& value) { return value.id == id; }),
                 nodes_.end());
}

void ToolBarCustomizer::resetTools() {
    if (sameTools(tools(), initial_)) return;
    store();
    tools_ = initial_;
    updateTree();
    if (!refreshMenu()) return;
    notifyChanged();
}

void ToolBarCustomizer::cut() {
    if (!canDoC()) return;
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=notification_revision_;
    copy();
    if (life.expired() || revision!=notification_revision_) return;
    removeMenu();
}

void ToolBarCustomizer::copy() {
    const auto selection = selectedTreeItem();
    if (selection == 0) return;
    const auto serialized = toNode(selection).text();
    xmlbytestransfer::XMLtoCB(presentation_.xmlCopy, clipboard_, serialized);
    refreshToolbar();
}

void ToolBarCustomizer::copyMenuSelection() {
    const auto ids = menuIdsFromRows();
    if (ids.empty()) return;
    auto node_value = XNode::create("tools");
    for (const auto id : ids) node_value.newElement("tool", menuValue(id));
    xmlbytestransfer::XMLtoCB(presentation_.xmlCopy, clipboard_, node_value.text());
    refreshToolbar();
}

void ToolBarCustomizer::paste() {
    if (!canDoP()) return;
    const auto serialized = xmlbytestransfer::CBtoXML(clipboard_);
    if (!serialized.empty()) (void)pasteXml(serialized);
}

void ToolBarCustomizer::del() { removeMenu(); }

bool ToolBarCustomizer::pasteXml(const lib::StringView serialized) {
    if (serialized.empty()) return false;
    try {
        const auto root = XNode::parse(serialized);
        return fromNode(root, selectedTreeItem(), true);
    } catch (...) {
        return false;
    }
}

bool ToolBarCustomizer::dropXml(const lib::StringView serialized,
                                 const TreeItemId target) {
    if (serialized.empty() || target == 0) return false;
    try {
        const auto root = XNode::parse(serialized);
        const auto dragged = drag_item_;
        const bool internal = dragged != 0 && node(dragged) != nullptr;
        if (internal && (target == dragged || descendant(target, dragged))) {
            drag_item_ = 0;
            return false;
        }
        if (internal) {
            // DはEditor内の転送元を、転送先で解析
            // する前に削除する。メニューIDを解放し、移
            // 動が重複追加として拒否されないようにする。
            store();
            removeMenuImpl(dragged);
        }
        const auto life=std::weak_ptr<void>(lifetime_);
        const auto result = fromNode(root, target, !internal);
        if (life.expired()) return result;
        if (internal) drag_item_ = 0;
        return result;
    } catch (...) {
        return false;
    }
}

XNode ToolBarCustomizer::toNode(const TreeItemId item) const {
    const auto* selected = node(item);
    if (!selected) return {};
    if (selected->parent == 0) {
        auto result = XNode::create("toolBar");
        for (const auto group : children(item)) {
            auto group_node = result.newElement("toolGroup");
            for (const auto tool : children(group)) {
                if (const auto* value = node(tool); value && value->menu)
                    group_node.newElement("tool", menuValue(*value->menu));
            }
        }
        return result;
    }
    if (node(selected->parent)->parent == 0) {
        auto result = XNode::create("toolGroup");
        for (const auto tool : children(item)) {
            if (const auto* value = node(tool); value && value->menu)
                result.newElement("tool", menuValue(*value->menu));
        }
        return result;
    }
    if (!selected->menu) return {};
    return XNode::create("tool", menuValue(*selected->menu));
}

bool ToolBarCustomizer::fromNode(const XNode& input, const TreeItemId selection,
                                 const bool storeBeforePaste) {
    if (input.name() != "toolBar" && input.name() != "toolGroup" &&
        input.name() != "tool" && input.name() != "tools") return false;
    if (input.name() != "toolBar" && selection == 0) return false;
    if (input.name() == "tool" &&
        (menuIdsFromNode(input).empty() ||
         !canAppendMenu(menuIdsFromNode(input).front()))) return false;
    if (input.name() == "tools" &&
        menuIdsFromNode(input).empty()) return false;
    if (storeBeforePaste) store();
    if (!fromNodeImpl(input, selection)) return false;
    updateBarAndGroupText();
    if (!refreshMenu()) return false;
    refreshMenuStates();
    notifyChanged();
    return true;
}

bool ToolBarCustomizer::fromNodeImpl(const XNode& input,
                                     const TreeItemId selection) {
    const auto name = input.name();
    TreeItemId inserted = 0;
    if (name == "toolBar") {
        inserted = addBarImpl(selection);
        const auto bar = inserted;
        for (const auto& group_node : input.children()) {
            if (group_node.name() != "toolGroup") continue;
            const auto group = addGroupImpl(bar);
            auto values = menuIdsFromNode(group_node);
            inserted = addMenuImpl(group, values);
        }
    } else if (name == "toolGroup") {
        inserted = addGroupImpl(selection);
        inserted = addMenuImpl(inserted, menuIdsFromNode(input));
    } else {
        inserted = addMenuImpl(selection, menuIdsFromNode(input));
    }
    if (inserted == 0 && name != "toolBar") {
        // Dは空のTreeItemを直ちに作るが、ツールが追加され
        // なければ、最後のラベル・選択・変更済みの処理を飛ばす。
        if (name == "toolGroup") syncTreeComponent();
        return false;
    }
    if (inserted != 0) selectTree(inserted);
    return true;
}

std::vector<MenuID> ToolBarCustomizer::menuIdsFromNode(const XNode& input) const {
    std::vector<MenuID> values;
    if (input.name() == "tool") {
        try {
            const auto id = input.valueTo<MenuID>();
            if (isMenuValue(id)) values.push_back(id);
        } catch (...) {}
        return values;
    }
    for (const auto& child : input.children()) {
        if (child.name() != "tool") continue;
        try {
            const auto id = child.valueTo<MenuID>();
            if (isMenuValue(id) && canAppendMenu(id)) values.push_back(id);
        } catch (...) {}
    }
    return values;
}

std::vector<MenuID> ToolBarCustomizer::menuIdsFromRows() const {
    std::vector<MenuID> values;
    for (const auto index : menu_selection_) {
        if (index >= menu_list_->rows.size() || menu_list_->rows[index].cells.empty()) continue;
        try {
            values.push_back(static_cast<MenuID>(std::stoi(
                menu_list_->rows[index].cells.front().value)));
        } catch (...) {}
    }
    return values;
}

bool ToolBarCustomizer::parseDragPayload(const lib::ClipboardData& data,
                                         lib::String& serialized) const {
    return xmlbytestransfer::extractXML(data, serialized);
}

lib::ClipboardData ToolBarCustomizer::menuListPayload() const {
    const auto ids = menuIdsFromRows();
    // DのDragMenuListは、コマンド未選択ならdragStartを拒否する。
    if (ids.empty()) return {};
    auto result = XNode::create("tools");
    for (const auto id : ids) result.newElement("tool", menuValue(id));
    return xmlbytestransfer::makeXMLPayload(result.text());
}

lib::ClipboardData ToolBarCustomizer::treePayload() const {
    const auto selected = selectedTreeItem();
    if (selected == 0) return {};
    drag_item_ = selected;
    return xmlbytestransfer::makeXMLPayload(toNode(selected).text());
}

void ToolBarCustomizer::setTreeSelection(std::vector<TreeItemId> values) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                guardOwner(lifetime_, [this](const TreeItemId id) { return node(id) == nullptr; })),
                 values.end());
    if (values.size() > 1) values.resize(1);
    tree_selection_ = std::move(values);
    syncTreeComponent();
    refreshMenuStates();
}

void ToolBarCustomizer::setMenuSelection(std::vector<std::size_t> values) {
    values.erase(std::remove_if(values.begin(), values.end(), guardOwner(lifetime_, [this](const std::size_t id) {
                     return id >= menu_list_->rows.size();
                 })), values.end());
    if (!menu_list_->multi_select && values.size() > 1) values.resize(1);
    menu_selection_ = std::move(values);
    syncMenuComponent();
    refreshMenuStates();
}

void ToolBarCustomizer::focusMenuList() noexcept { focus_ = Focus::Menu; }
void ToolBarCustomizer::focusToolTree() noexcept { focus_ = Focus::Tree; }

ToolBarCustomizer::Node* ToolBarCustomizer::node(const TreeItemId id) noexcept {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
                                    [id](const Node& value) { return value.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
}

const ToolBarCustomizer::Node* ToolBarCustomizer::node(const TreeItemId id) const noexcept {
    const auto found = std::find_if(nodes_.cbegin(), nodes_.cend(),
                                    [id](const Node& value) { return value.id == id; });
    return found == nodes_.cend() ? nullptr : &*found;
}

std::vector<ToolBarCustomizer::TreeItemId> ToolBarCustomizer::children(
    const TreeItemId parent) const {
    std::vector<TreeItemId> values;
    for (const auto& value : nodes_) {
        if (value.parent == parent) values.push_back(value.id);
    }
    return values;
}

ToolBarCustomizer::TreeItemId ToolBarCustomizer::topItem(const TreeItemId id) const {
    auto current = id;
    for (;;) {
        const auto* value = node(current);
        if (!value || value->parent == 0) break;
        current = value->parent;
    }
    return current;
}

std::size_t ToolBarCustomizer::childIndex(const TreeItemId id) const {
    const auto* value = node(id);
    if (!value) return std::numeric_limits<std::size_t>::max();
    const auto siblings = children(value->parent);
    const auto found = std::find(siblings.begin(), siblings.end(), id);
    return found == siblings.end() ? std::numeric_limits<std::size_t>::max()
                                   : static_cast<std::size_t>(found - siblings.begin());
}

bool ToolBarCustomizer::descendant(const TreeItemId id,
                                   const TreeItemId ancestor) const {
    auto current = id;
    while (const auto* value = node(current)) {
        if (value->parent == ancestor) return true;
        if (value->parent == 0) return false;
        current = value->parent;
    }
    return false;
}

std::pair<std::size_t, std::size_t> ToolBarCustomizer::subtreeRange(
    const TreeItemId id) const {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
                                    [id](const Node& value) { return value.id == id; });
    if (found == nodes_.end()) return {nodes_.size(), nodes_.size()};
    const auto begin = static_cast<std::size_t>(found - nodes_.begin());
    std::size_t end = begin + 1U;
    while (end < nodes_.size() && descendant(nodes_[end].id, id)) ++end;
    return {begin, end};
}

ToolBarCustomizer::TreeItemId ToolBarCustomizer::selectedTreeItem() const noexcept {
    return tree_selection_.empty() ? 0 : tree_selection_.front();
}

std::vector<std::size_t> ToolBarCustomizer::selectedPath() const {
    std::vector<std::size_t> value;
    auto current = selectedTreeItem();
    while (current != 0) {
        const auto* item = node(current);
        if (!item) break;
        value.push_back(childIndex(current));
        current = item->parent;
    }
    std::reverse(value.begin(), value.end());
    return value;
}

void ToolBarCustomizer::restorePath(const std::vector<std::size_t>& path) {
    if (path.empty()) return;
    auto siblings = children(0);
    if (path.front() >= siblings.size()) return;
    auto current = siblings[path.front()];
    for (std::size_t index = 1; index < path.size(); ++index) {
        siblings = children(current);
        if (path[index] >= siblings.size()) return;
        current = siblings[path[index]];
    }
    selectTree(current);
}

void ToolBarCustomizer::selectTree(const TreeItemId id, const bool notify) {
    if (id == 0 || !node(id)) {
        tree_selection_.clear();
    } else {
        tree_selection_ = {id};
    }
    syncTreeComponent();
    refreshMenuStates();
    if (notify && tool_tree_->on_selection_changed)
        tool_tree_->on_selection_changed(tree_selection_);
}

void ToolBarCustomizer::syncTreeComponent() {
    if (!tool_tree_) return;
    std::vector<lib::TreeNode> nodes;
    nodes.reserve(nodes_.size());
    for (const auto& value : nodes_) {
        lib::TreeNode item;
        item.id = value.id;
        item.parent = value.parent;
        item.text = value.text;
        item.value = value.menu ? menuValue(*value.menu) :
            (value.parent == 0 ? "toolBar" : "toolGroup");
        item.icon = value.image;
        item.expanded = value.expanded;
        nodes.push_back(std::move(item));
    }
    (void)lib::replace_tree_nodes(tool_tree_, std::move(nodes),
                                  tree_selection_);
}

void ToolBarCustomizer::syncMenuComponent() {
    if (menu_list_)
        (void)lib::select_table_rows(*menu_list_, menu_selection_, false, false);
}

lib::String ToolBarCustomizer::formatIndex(const lib::String& pattern,
                                           const std::size_t index) const {
    // Dのcustomtoolbar.dは、整形をutils.tryFormatへ委ねる。
    if (!lib::editor_window_host_options().all_mode)
        return ::cwxeditor::cwx::utils::tryFormat(pattern, index);
    return replaceIndex(pattern, index);
}

lib::String ToolBarCustomizer::menuText(const MenuID id) const {
    return presentation_.menuText ? presentation_.menuText(id) : std::string{};
}

lib::ResourceId ToolBarCustomizer::menuImage(const MenuID id) const {
    return presentation_.menuImage ? presentation_.menuImage(id) : 0;
}

bool ToolBarCustomizer::sameTools(const ToolBarSettings& left,
                                  const ToolBarSettings& right) const noexcept {
    if (left.tools.size() != right.tools.size()) return false;
    for (std::size_t bar = 0; bar < left.tools.size(); ++bar) {
        if (left.tools[bar].size() != right.tools[bar].size()) return false;
        for (std::size_t item = 0; item < left.tools[bar].size(); ++item) {
            if (left.tools[bar][item].separator != right.tools[bar][item].separator ||
                left.tools[bar][item].menu != right.tools[bar][item].menu) return false;
        }
    }
    return true;
}

ToolBarCustomDialog::ToolBarCustomDialog(
    Services services, std::shared_ptr<Window> owner, lib::String title,
    const lib::ResourceId icon, ToolBarSettings tools,
    const ToolBarSettings& init, Presentation presentation, Callbacks callbacks)
    : services_(std::move(services)), owner_(std::move(owner)),
      tools_(std::move(tools)), initial_(init), opened_(tools_),
      committed_(tools_), presentation_(std::move(presentation)),
      callbacks_(std::move(callbacks)) {
    if (services_.gui) dialog_ = services_.gui->create_dialog();
    if (!dialog_) dialog_ = std::make_shared<Dialog>();
    dialog_->title = std::move(title);
    dialog_->icon = icon;
    if (!dialog_->icon && presentation_.menuImage)
        dialog_->icon = presentation_.menuImage(MenuID::CustomizeToolBar);
    dialog_->resizable = true;
    dialog_->minimizable = false;
    dialog_->maximizable = false;
    dialog_->initially_hidden = true;
    lib::configure_owned_window(dialog_, owner_,
        lib::OwnedWindowKind::Modeless);
    dialog_->accessibility_name = dialog_->title;
    dialog_->minimum_size = lib::Size{600.0F, 360.0F};
    auto bounds = lib::Rect{0.0F, 0.0F, 638.0F, 391.0F};
    if (callbacks_.loadGeometry) {
        if (const auto stored = callbacks_.loadGeometry(); stored &&
            stored->width > 0.0F && stored->height > 0.0F)
            bounds = *stored;
    }
    dialog_->set_bounds(bounds);
    dialog_->on_close_requested = guardOwner(lifetime_, [this] { finish(lib::DialogResult::Cancel); });
}

ToolBarCustomDialog::~ToolBarCustomDialog() {
    lifetime_.reset();
    if (dialog_) dialog_->on_close_requested = {};
    if (ok_button_) ok_button_->on_click = {};
    if (cancel_button_) cancel_button_->on_click = {};
    if (apply_button_) apply_button_->on_click = {};
    if (dialog_ && visible_) saveGeometry();
    if (services_.gui && dialog_ && visible_) services_.gui->close(dialog_);
}

void ToolBarCustomDialog::setup(Container& area) {
    if (!customizer_) {
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1;
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
        layout->spacing = 6.0F;
        area.set_layout(layout);

        content_ = std::make_shared<lib::Panel>();
        content_->set_layout(std::make_shared<lib::FillLayout>());
        content_->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                   lib::Align::Stretch, {580, 300}, {}, {},
                                   1.0F});
        area.add(content_);
        customizer_ = std::make_unique<ToolBarCustomizer>(
            services_, content_.get(), tools_, initial_, presentation_, callbacks_);
        auto item = lib::LayoutItem{};
        item.row = 0;
        item.grow = 1.0F;
        customizer_->widget()->set_layout_item(item);
        customizer_->modEvent.push_back(guardOwner(lifetime_, [this] {
            markDirty();
        }));

        auto separator = std::make_shared<lib::Separator>();
        separator->set_layout_item({1, 0});
        area.add(separator);
        auto buttons = std::make_shared<lib::Panel>();
        auto button_layout = std::make_shared<lib::GridLayout>();
        button_layout->columns = 3;
        button_layout->equal_width = true;
        button_layout->spacing = 5.0F;
        buttons->set_layout(button_layout);
        buttons->set_layout_item({2, 0, 1, 1, lib::Align::End,
                                  lib::Align::Center});
        ok_button_ = std::make_shared<lib::Button>(presentation_.ok);
        ok_button_->default_button = true;
        ok_button_->on_click = guardOwner(lifetime_, [this] { finish(lib::DialogResult::OK); });
        buttons->add(ok_button_);
        cancel_button_ = std::make_shared<lib::Button>(presentation_.cancel);
        cancel_button_->cancel_button = true;
        cancel_button_->on_click = guardOwner(lifetime_, [this] {
            finish(lib::DialogResult::Cancel);
        });
        buttons->add(cancel_button_);
        apply_button_ = std::make_shared<lib::Button>(presentation_.apply);
        apply_button_->set_enabled(false);
        apply_button_->on_click = guardOwner(lifetime_, [this] {
            if (!apply()) return;
            if (dialog_) dialog_->result = lib::DialogResult::Apply;
        });
        buttons->add(apply_button_);
        area.add(buttons);
    }
    initialized_ = true;
}

bool ToolBarCustomDialog::setup() {
    if (!dialog_) return false;
    setup(*dialog_);
    return true;
}

bool ToolBarCustomDialog::apply() {
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=++edit_revision_;
    if (customizer_) tools_ = customizer_->tools();
    committed_ = tools_;
    dirty_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    if (dialog_) dialog_->modified = false;
    const auto applied=callbacks_.applied;
    const auto value=committed_;
    if (applied) applied(value);
    return !life.expired() && revision==edit_revision_;
}

void ToolBarCustomDialog::cancel() {
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=++edit_revision_;
    tools_ = committed_;
    if (customizer_) customizer_->setTools(tools_);
    if (life.expired() || revision!=edit_revision_) return;
    dirty_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    if (dialog_) dialog_->modified = false;
}

void ToolBarCustomDialog::show() {
    if (!initialized_) (void)setup();
    if (!dialog_) return;
    // initially_hiddenはモデルの組み立て中だけに使う
    // 。残すとWindowsバックエンドがshow()ごとにSW_HIDE
    // を実行し、メニューコマンドが何もしないように見える。
    dialog_->initially_hidden = false;
    dialog_->result = lib::DialogResult::None;
    dialog_->set_visible(true);
    visible_ = true;
    if (services_.gui) services_.gui->show(dialog_);
}

void ToolBarCustomDialog::close() {
    const auto life=std::weak_ptr<void>(lifetime_);
    const auto revision=edit_revision_;
    if (!dialog_) return;
    saveGeometry();
    if (life.expired() || revision!=edit_revision_) return;
    if (customizer_) customizer_->disconnectNotifications();
    if (life.expired() || revision!=edit_revision_) return;
    dialog_->set_visible(false);
    visible_ = false;
    if (services_.gui) services_.gui->close(dialog_);
}

void ToolBarCustomDialog::finish(const lib::DialogResult result) {
    const auto life=std::weak_ptr<void>(lifetime_);
    if (result == lib::DialogResult::OK) {
        if (!apply()) return;
    } else if (result == lib::DialogResult::Cancel) {
        cancel();
    }
    if (life.expired()) return;
    const auto revision=edit_revision_;
    if (dialog_) dialog_->result = result;
    const auto closed = callbacks_.closed;
    close();
    if (life.expired() || revision!=edit_revision_) return;
    if (closed) closed();
}

void ToolBarCustomDialog::markDirty() {
    ++edit_revision_;
    if (customizer_) tools_ = customizer_->tools();
    const auto same = [](const ToolBarSettings& left,
                         const ToolBarSettings& right) {
        if (left.tools.size() != right.tools.size()) return false;
        for (std::size_t bar = 0; bar < left.tools.size(); ++bar) {
            if (left.tools[bar].size() != right.tools[bar].size()) return false;
            for (std::size_t item = 0; item < left.tools[bar].size(); ++item) {
                if (left.tools[bar][item].separator !=
                        right.tools[bar][item].separator ||
                    left.tools[bar][item].menu != right.tools[bar][item].menu)
                    return false;
            }
        }
        return true;
    };
    dirty_ = !same(tools_, committed_);
    if (apply_button_) apply_button_->set_enabled(dirty_);
    if (dialog_) dialog_->modified = dirty_;
}

void ToolBarCustomDialog::saveGeometry() {
    const auto save=callbacks_.saveGeometry;
    const auto window=dialog_;
    // 未スケールの寸法を保存(再オープン時の二重スケール防止)。
    if (window && save) save(lib::authored_window_bounds(*window));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtoolbar
