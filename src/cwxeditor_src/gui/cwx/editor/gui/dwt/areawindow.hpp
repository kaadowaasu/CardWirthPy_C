#pragma once
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/area_context.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaview.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::areawindow {

namespace detail {

template <typename T>
[[nodiscard]] std::shared_ptr<T> createComponent(const lib::Services& services,
                                                   lib::ComponentKind kind) {
    if (services.gui) {
        auto candidate = services.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<T>(candidate)) return result;
    }
    return std::make_shared<T>();
}

template <typename T>
inline constexpr bool always_false = false;

template <typename A>
[[nodiscard]] inline std::string objectName(const Props& prop) {
    if constexpr (std::is_same_v<A, area::Area>) {
        return prop.requiredMsgs().area.value;
    } else if constexpr (std::is_same_v<A, area::Battle>) {
        return prop.requiredMsgs().battle.value;
    } else if constexpr (std::is_same_v<A, area::Package>) {
        return prop.requiredMsgs().cwPackage.value;
    } else {
        return {};
    }
}

template <typename A>
[[nodiscard]] inline ResourceId sceneImage(const AreaWindowContext& context) {
    if constexpr (std::is_same_v<A, area::Area>) {
        return context.area_image;
    } else if constexpr (std::is_same_v<A, area::Battle>) {
        return context.battle_image;
    } else {
        return {};
    }
}

}  // namespace detail

// 共通化のため、以前のBasicAreaViewの空の別UIは使わな
// い。 公開の補助処理とMainWindowで、すべての実装に共通
// の製品用の場面部品・動作を使う。 コンテキストの注入箇所は残す。
template<typename A,typename C>
using BasicAreaView=areaview::AbstractAreaView<A,C,true,std::is_same_v<A,area::Area>>;
using AreaView=areaview::AreaView;
using BattleView=areaview::BattleView;

/// DのTAreaWindow!(V, A, C)に対応する共通
/// の場面ウィンドウ。 部品ツリーだけを所有し、所有ハンドルを渡
/// す版以外ではSummary・Props・エリアを借用する。
template <typename V, typename A, typename C>
class TAreaWindow {
public:
    using View = V;
    using Area = A;
    using Card = C;
    using Context = AreaWindowContext;
    using Parent = std::shared_ptr<Container>;
    using AreaOwner = std::shared_ptr<A>;
    using ViewOwner = std::shared_ptr<V>;
    using Selection = SelectionEvent;

    TAreaWindow(Context& context, Props& prop, Summary* summ, Parent parent,
                std::shared_ptr<Window> area_window, A* area,
                UndoManager* undo = nullptr, bool read_only = false,
                ViewOwner view = {})
        : context_(&context), prop_(&prop), summ_(summ), area_(area),
          area_window_(std::move(area_window)), read_only_(read_only),
          view_(std::move(view)) {
        initialize(parent, undo);
    }

    TAreaWindow(Context& context, Props& prop, Summary& summ, Parent parent,
                std::shared_ptr<Window> area_window, A* area,
                UndoManager* undo = nullptr, bool read_only = false,
                ViewOwner view = {})
        : TAreaWindow(context, prop, &summ, std::move(parent),
                      std::move(area_window), area, undo, read_only,
                      std::move(view)) {}

    TAreaWindow(Context& context, Props& prop, Summary* summ, Parent parent,
                std::shared_ptr<Window> area_window, AreaOwner area,
                std::shared_ptr<UndoManager> undo = {},
                bool read_only = false, ViewOwner view = {})
        : context_(&context), prop_(&prop), summ_(summ),
          area_owner_(std::move(area)), area_(area_owner_.get()),
          undo_owner_(std::move(undo)), area_window_(std::move(area_window)),
          read_only_(read_only), view_(std::move(view)) {
        initialize(parent, undo_owner_.get());
    }

    TAreaWindow(Context& context, Props& prop, Summary& summ, Parent parent,
                std::shared_ptr<Window> area_window, AreaOwner area,
                std::shared_ptr<UndoManager> undo = {},
                bool read_only = false, ViewOwner view = {})
        : TAreaWindow(context, prop, &summ, std::move(parent),
                      std::move(area_window), std::move(area),
                      std::move(undo), read_only, std::move(view)) {}

    TAreaWindow(const TAreaWindow&) = delete;
    TAreaWindow& operator=(const TAreaWindow&) = delete;
    TAreaWindow(TAreaWindow&&) = delete;
    TAreaWindow& operator=(TAreaWindow&&) = delete;

    ~TAreaWindow() { dispose(); }

    [[nodiscard]] const std::shared_ptr<Panel>& shell() const noexcept {
        return window_;
    }
    [[nodiscard]] std::shared_ptr<Panel>& shell() noexcept { return window_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& menu() const noexcept {
        return menu_;
    }
    [[nodiscard]] Props& props() const noexcept { return *prop_; }
    [[nodiscard]] Summary* summary() const noexcept { return summ_; }
    [[nodiscard]] A* eventTreeOwner() const noexcept { return area_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    [[nodiscard]] V* areaView() const noexcept { return view_.get(); }
    [[nodiscard]] ResourceId image() const noexcept {
        return detail::sceneImage<A>(*context_);
    }
    [[nodiscard]] std::string title() const;
    [[nodiscard]] std::function<void(std::string)> statusText() const noexcept {
        return {};
    }

    void refresh();
    void refreshFromMenu();
    [[nodiscard]] bool canUp() const;
    [[nodiscard]] bool canDown() const;
    void up();
    void down();
    void deleteArea(const A* area);
    void refArea(const A* area);
    void refreshTitle();
    void refUndoMax();

    void undo();
    void redo();
    void openEvent(bool can_duplicate = false);
    void openDup();
    void edit();
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] bool canSelectConnectedResource() const;
    void selectConnectedResource();

    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);
    [[nodiscard]] bool canDoTCPD() const;
    [[nodiscard]] bool canDoT() const;
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoD() const;
    [[nodiscard]] bool canDoClone() const;

    [[nodiscard]] V& tcpd() noexcept { return *view_; }
    [[nodiscard]] const V& tcpd() const noexcept { return *view_; }
    [[nodiscard]] bool openCWXPath(std::string_view value,
                                   bool shell_activate);
    [[nodiscard]] std::vector<std::string> openedCWXPath() const;

    void setFocused(bool value) noexcept;
    [[nodiscard]] bool focused() const noexcept { return focused_; }
    [[nodiscard]] bool disposed() const noexcept { return disposed_; }
    void dispose() noexcept;

private:
    class Dispose {
    public:
        explicit Dispose(TAreaWindow* owner) : owner_(owner) {}
        void widgetDisposed() noexcept {
            if (owner_) owner_->dispose();
        }

    private:
        TAreaWindow* owner_{};
    };

    Context* context_{};
    Props* prop_{};
    Summary* summ_{};
    AreaOwner area_owner_;
    A* area_{};
    std::shared_ptr<UndoManager> undo_owner_;
    std::unique_ptr<UndoManager> owned_undo_;
    UndoManager* undo_{};
    std::shared_ptr<Window> area_window_;
    std::shared_ptr<Panel> window_;
    std::shared_ptr<lib::Menu> menu_;
    ViewOwner view_;
    bool read_only_{};
    bool ref_undo_{};
    bool focused_{};
    bool disposed_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t menu_revision_{};
    std::uint64_t focus_observer_{};
    std::map<types::MenuID,std::uint64_t> registry_connections_;
    std::vector<std::pair<types::MenuID, std::string>> menu_ids_;
    std::map<std::string, std::size_t, std::less<>> menu_indices_;
    std::uint64_t del_connection_{};
    std::uint64_t ref_connection_{};
    std::uint64_t close_adds_connection_{};
    std::uint64_t repl_text_connection_{};
    std::uint64_t preview_connection_{};
    std::uint64_t card_state_connection_{};
    std::uint64_t ref_undo_connection_{};

    void initialize(Parent parent, UndoManager* supplied_undo);
    void connectSignals();
    void installMenus();
    void refreshMenuState();
    void unregisterMenus() noexcept;
    void closePanel();
    void closeAdds(const Summary* summary);
    void attachFocusHandlers();

    template<class Function>auto guarded(Function function)const{
        const std::weak_ptr<void> life=lifetime_;
        return [life,function=std::move(function)](auto&&... args)mutable -> decltype(auto){
            using Result=std::invoke_result_t<Function&,decltype(args)...>;
            if(life.expired()){if constexpr(std::is_void_v<Result>)return;else return Result{};}
            return function(std::forward<decltype(args)>(args)...);
        };
    }

    template <typename Action, typename Enabled = std::function<bool()>>
    void registerMenu(types::MenuID id, std::string command,
                      Action action, Enabled enabled = {}) {
        const auto index = menu_->items.size();
        menu_->items.push_back({command, command, {}, {}, true, false, false,
                                false, {}});
        menu_indices_.emplace(command, index);
        menu_ids_.emplace_back(id, command);
        if (context_->menu_registry) {
            std::function<bool()> eligibility=std::move(enabled);
            if(eligibility)eligibility=guarded(std::move(eligibility));
            registry_connections_[id]=context_->menu_registry->put(id,guarded(std::move(action)),std::move(eligibility));
        }
    }

    [[nodiscard]] static ViewOwner createView(Context& context, Props& prop,
                                               Summary* summ, Parent parent,
                                               A* area, UndoManager* undo,
                                               bool read_only, ViewOwner view,
                                               std::shared_ptr<UndoManager> history = {}) {
        if (view) return view;
        if constexpr (std::is_constructible_v<
                          V, Context&, Props&, Summary*, Parent, A*,
                          std::shared_ptr<UndoManager>, bool, bool>) {
            if (history)
                return std::make_shared<V>(context, prop, summ, std::move(parent),
                                           area, std::move(history),
                                           context.show_inherit_background, read_only);
        }
        if constexpr (std::is_constructible_v<
                          V, Context&, Props&, Summary*, Parent, A*,
                          UndoManager*, bool, bool>) {
            return std::make_shared<V>(context, prop, summ, std::move(parent),
                                       area, undo, context.show_inherit_background,
                                       read_only);
        } else if constexpr (std::is_default_constructible_v<V>) {
            return std::make_shared<V>();
        } else {
            static_assert(detail::always_false<V>,
                          "TAreaWindow view requires a portable constructor or owner");
        }
    }
};

using AreaSceneWindow = TAreaWindow<AreaView, area::Area, area::MenuCard>;
using BattleSceneWindow = TAreaWindow<BattleView, area::Battle, area::EnemyCard>;

template <typename V, typename A, typename C>
inline std::string TAreaWindow<V, A, C>::title() const {
    if (!prop_ || !area_) return {};
    const auto format = prop_->requiredMsgs().viewNameSceneTab.value;
    return utils::tryFormat(format, detail::objectName<A>(*prop_), area_->id(),
                            area_->name());
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::initialize(Parent parent,
                                              UndoManager* supplied_undo) {
    if (!context_ || !prop_ || !area_)
        throw std::invalid_argument("TAreaWindow requires context, props and area");
    window_ = detail::createComponent<Panel>(context_->services,
                                             lib::ComponentKind::Panel);
    // 場面タブはAreaViewを一つだけ持ち、D/SWT
    // と同じくクライアント領域全体を割り当てる。 伸
    // 長指定のない一セルのGridでは、子の推奨高さ
    // が使われて大きな灰色の余白が残る場合がある。
    // FillLayoutで全体を使うことを明示し、
    // 境界のドラッグも子の推奨サイズから独立させる。
    window_->set_layout(std::make_shared<lib::FillLayout>());
    auto window_item = window_->layout_item();
    window_item.grow = 1.0F;
    window_item.horizontal = lib::Align::Stretch;
    window_item.vertical = lib::Align::Stretch;
    window_->set_layout_item(window_item);
    if (parent) parent->add(window_);
    attachFocusHandlers();

    if (supplied_undo) {
        undo_ = supplied_undo;
    } else {
        const auto configured = prop_->var().undoMaxEvent.value;
        const auto maximum = configured > 0
                                 ? static_cast<std::size_t>(configured)
                                 : std::size_t{0U};
        owned_undo_ = std::make_unique<UndoManager>(maximum);
        undo_ = owned_undo_.get();
        ref_undo_ = true;
    }

    view_ = createView(*context_, *prop_, summ_, window_, area_, undo_,
                       read_only_, std::move(view_), undo_owner_);
    if (!view_) throw std::runtime_error("TAreaWindow could not create its view");
    menu_ = detail::createComponent<lib::Menu>(context_->services,
                                               lib::ComponentKind::MenuBar);
    installMenus();
    connectSignals();
    refreshTitle();
    refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::attachFocusHandlers() {
    window_->on_focus = guarded([this] { setFocused(true); });
    window_->on_blur = guarded([this] { setFocused(false); });
    const std::weak_ptr<void> life=lifetime_;
    const std::weak_ptr<Panel> root=window_;
    // DのhasFocusは子孫も含む。Displayフィルターを共通のフォ
    // ーカス監視へ置き換え、 ネイティブ・ALL専用の祖先判定を設けない。
    focus_observer_=lib::add_focus_observer([this,life,root](const lib::WidgetPtr& target){
        if(life.expired())return;
        const auto panel=root.lock();
        setFocused(panel && target && lib::focus_target_in_scope(panel,target,false)==target);
    });
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::connectSignals() {
    if constexpr (std::is_same_v<A, area::Area>) {
        del_connection_ = context_->signals.del_area.connect(
            guarded([this](const area::Area* value) { deleteArea(value); }));
        ref_connection_ = context_->signals.ref_area.connect(
            guarded([this](const area::Area* value) { refArea(value); }));
    } else if constexpr (std::is_same_v<A, area::Battle>) {
        del_connection_ = context_->signals.del_battle.connect(
            guarded([this](const area::Battle* value) { deleteArea(value); }));
        ref_connection_ = context_->signals.ref_battle.connect(
            guarded([this](const area::Battle* value) { refArea(value); }));
    } else if constexpr (std::is_same_v<A, area::Package>) {
        del_connection_ = context_->signals.del_package.connect(
            guarded([this](const area::Package* value) { deleteArea(value); }));
        ref_connection_ = context_->signals.ref_package.connect(
            guarded([this](const area::Package* value) { refArea(value); }));
    }
    close_adds_connection_ = context_->signals.close_adds.connect(
        guarded([this](const Summary* value) { closeAdds(value); }));
    repl_text_connection_ = context_->signals.repl_text.connect(
        guarded([this] {
            const auto view = view_;
            const std::weak_ptr<void> life = lifetime_;
            refreshTitle();
            if (!life.expired() && view) view->replText();
        }));
    preview_connection_ = context_->signals.ref_preview_values.connect(
        guarded([this] { const auto view = view_; if (view) view->refreshPresentation(); }));
    card_state_connection_ = context_->signals.ref_card_state.connect(
        guarded([this] { const auto view = view_; if (view) view->refreshCardState(); }));
    ref_undo_connection_ = context_->signals.ref_undo_max.connect(
        guarded([this] { refUndoMax(); }));
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::installMenus() {
    const std::weak_ptr<void> life=lifetime_;
    menu_->on_command = [this,life](std::string_view command) {
        if(life.expired())return;
        const auto found = menu_indices_.find(command);
        if (found == menu_indices_.end()) return;
        auto& item = menu_->items[found->second];
        if (!item.enabled) return;
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "up") up();
        else if (command == "down") down();
        else if (command == "edit-event") openEvent(false);
        else if (command == "edit-scene-duplicate") openDup();
        else if (command == "edit-event-duplicate") openEvent(true);
        else if (command == "refresh") refreshFromMenu();
        else if (command == "edit-prop") edit();
        else if (command == "select-connected-resource") selectConnectedResource();
        else if (command == "comment") {
            const auto view=view_;if(view)view->writeComment();
        }
        else {
            SelectionEvent event{std::string(command), false};
            if (command == "cut") cut(event);
            else if (command == "copy") copy(event);
            else if (command == "paste") paste(event);
            else if (command == "delete") del(event);
            else if (command == "clone") clone(event);
        }
        if(!life.expired())refreshMenuState();
    };
    registerMenu(types::MenuID::Undo, "undo", [this] { undo(); },
                 [this] { return undo_ && undo_->canUndo(); });
    registerMenu(types::MenuID::Redo, "redo", [this] { redo(); },
                 [this] { return undo_ && undo_->canRedo(); });
    registerMenu(types::MenuID::Up, "up", [this] { up(); },
                 [this] { return canUp(); });
    registerMenu(types::MenuID::Down, "down", [this] { down(); },
                 [this] { return canDown(); });
    registerMenu(types::MenuID::Cut, "cut", [this] { SelectionEvent e{"cut"}; cut(e); },
                 [this] { return canDoT(); });
    registerMenu(types::MenuID::Copy, "copy", [this] { SelectionEvent e{"copy"}; copy(e); },
                 [this] { return canDoC(); });
    registerMenu(types::MenuID::Paste, "paste", [this] { SelectionEvent e{"paste"}; paste(e); },
                 [this] { return canDoP(); });
    registerMenu(types::MenuID::Delete, "delete", [this] { SelectionEvent e{"delete"}; del(e); },
                 [this] { return canDoD(); });
    registerMenu(types::MenuID::Clone, "clone", [this] { SelectionEvent e{"clone"}; clone(e); },
                 [this] { return canDoClone(); });
    registerMenu(types::MenuID::EditEvent, "edit-event", [this] { openEvent(false); });
    registerMenu(types::MenuID::EditSceneDup, "edit-scene-duplicate", [this] { openDup(); });
    registerMenu(types::MenuID::EditEventDup, "edit-event-duplicate", [this] { openEvent(true); });
    registerMenu(types::MenuID::Refresh, "refresh", [this] { refreshFromMenu(); });
    registerMenu(types::MenuID::EditProp, "edit-prop", [this] { edit(); },
                 [this] { return canEdit(); });
    registerMenu(types::MenuID::SelectConnectedResource, "select-connected-resource",
                 [this] { selectConnectedResource(); },
                 [this] { return canSelectConnectedResource(); });
    registerMenu(types::MenuID::Comment, "comment",
                 [this] { const auto view=view_;if(view)view->writeComment(); },
                 [this] { const auto view=view_;return view && view->canWriteComment(); });
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refreshMenuState() {
    if (!menu_) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=++menu_revision_;
    const auto popup=menu_;
    const auto item_revision=popup->item_update_revision;
    const auto commands=menu_ids_;
    const auto indices=menu_indices_;
    auto items=popup->items;
    for (const auto& [id, command] : commands) {
        static_cast<void>(id);
        const auto found = indices.find(command);
        if (found == indices.end() || found->second>=items.size()) continue;
        bool enabled = true;
        if (command == "undo") enabled = undo_ && undo_->canUndo();
        else if (command == "redo") enabled = undo_ && undo_->canRedo();
        else if (command == "up") enabled = canUp();
        else if (command == "down") enabled = canDown();
        else if (command == "cut") enabled = canDoT();
        else if (command == "copy") enabled = canDoC();
        else if (command == "paste") enabled = canDoP();
        else if (command == "delete") enabled = canDoD();
        else if (command == "clone") enabled = canDoClone();
        else if (command == "edit-prop") enabled = canEdit();
        else if (command == "select-connected-resource") enabled = canSelectConnectedResource();
        else if (command == "comment") {const auto view=view_;enabled=view && view->canWriteComment();}
        if(life.expired() || revision!=menu_revision_ || popup->item_update_revision!=item_revision)return;
        items[found->second].enabled = enabled;
    }
    popup->items=std::move(items);
    ++popup->item_update_revision;
    const auto notify=context_->refresh_toolbar;
    if(notify)notify();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::unregisterMenus() noexcept {
    if (!context_ || !context_->menu_registry) return;
    for (const auto& [id, command] : menu_ids_) {
        static_cast<void>(command);
        const auto connection=registry_connections_.find(id);
        if(connection!=registry_connections_.end())context_->menu_registry->remove(id,connection->second);
    }
    menu_ids_.clear();
    registry_connections_.clear();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::closePanel() {
    if (disposed_) return;
    if (context_->close_panel) {
        const auto close=context_->close_panel;
        const auto panel=window_;
        close(panel);
    } else if (window_) {
        // 場面エディターはドックの子なので、Dと同じくこの子だけを閉じ
        // る。 ホストがドック用の通知を渡さなかった場合にarea_window_
        // を閉じると、Editor本体まで閉じてしまう。
        window_->set_visible(false);
    }
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::closeAdds(const Summary* summary) {
    if (summ_ && summary == summ_) closePanel();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refresh() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view)view->refresh();
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refreshFromMenu() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view)view->refreshFromMenu();
    if(!life.expired())refreshMenuState();
}
template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canUp() const {
    const auto view=view_;
    return view && view->canUp();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDown() const {
    const auto view=view_;
    return view && view->canDown();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::up() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view && view->canUp()){if(life.expired())return;view->up();}
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::down() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view && view->canDown()){if(life.expired())return;view->down();}
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::deleteArea(const A* value) {
    if (value == area_) closePanel();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refArea(const A* value) {
    if (value == area_) refreshTitle();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refreshTitle() {
    if (!window_) return;
    const auto value = title();
    if (context_->set_title) {
        const auto set_title=context_->set_title;
        const auto panel=window_;
        set_title(panel,value);
    } else if (area_window_) {
        // 単独・ヘッドレスのホストでは従来のタイトル通知先を使う。 ドックの場
        // 合はメインウィンドウでなく、set_title経由でタブを更新する。
        area_window_->title = value;
    }
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::refUndoMax() {
    const auto history_owner = undo_owner_;
    if ((!ref_undo_ && !undo_owner_) || !undo_ || !prop_) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto configured = prop_->var().undoMaxEvent.value;
    undo_->max(configured > 0 ? static_cast<std::size_t>(configured)
                              : std::size_t{0U});
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::undo() {
    const auto history_owner = undo_owner_;
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if (undo_ && undo_->canUndo()) {
        static_cast<void>(undo_->undo());
        if(life.expired())return;
        if(view)view->refresh();
    }
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::redo() {
    const auto history_owner = undo_owner_;
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if (undo_ && undo_->canRedo()) {
        static_cast<void>(undo_->redo());
        if(life.expired())return;
        if(view)view->refresh();
    }
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::openEvent(bool duplicate) {
    const auto view=view_;
    if(view)view->openEvent(duplicate);
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::openDup() {
    const auto view=view_;
    if(view)view->openDup();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::edit() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view && view->canEdit() && !life.expired())view->edit();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canEdit() const {
    const auto view=view_;
    return view && view->canEdit();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canSelectConnectedResource() const {
    const auto view=view_;
    return view && view->canSelectConnectedResource();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::selectConnectedResource() {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(view && view->canSelectConnectedResource() && !life.expired())view->selectConnectedResource();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::cut(SelectionEvent& event) {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(canDoT() && !life.expired() && view)view->cut(event);
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::copy(SelectionEvent& event) {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(canDoC() && !life.expired() && view)view->copy(event);
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::paste(SelectionEvent& event) {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(canDoP() && !life.expired() && view)view->paste(event);
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::del(SelectionEvent& event) {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(canDoD() && !life.expired() && view)view->del(event);
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::clone(SelectionEvent& event) {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    if(canDoClone() && !life.expired() && view)view->clone(event);
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoTCPD() const {
    if (disposed_ || !window_ || !view_) return false;
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    const auto panel=window_;
    const auto has_focus=context_->has_focus;
    if(has_focus){const bool focused=has_focus(panel);return !life.expired() && focused;}
    return focused_ && view->canDoTCPD();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoT() const {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    return canDoTCPD() && !life.expired() && view && view->canDoT();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoC() const {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    return canDoTCPD() && !life.expired() && view && view->canDoC();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoP() const {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    return canDoTCPD() && !life.expired() && view && view->canDoP();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoD() const {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    return canDoTCPD() && !life.expired() && view && view->canDoD();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::canDoClone() const {
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    return canDoTCPD() && !life.expired() && view && view->canDoClone();
}

template <typename V, typename A, typename C>
inline bool TAreaWindow<V, A, C>::openCWXPath(std::string_view value,
                                               bool shell_activate) {
    const auto category = path::cpcategory(value);
    if (path::cpempty(value) ||
        (((category == "menucard") || (category == "enemycard")) &&
         path::cpempty(path::cpbottom(value))) ||
        (category == "menucard" &&
         path::cpcategory(path::cpbottom(value)) == "name") ||
        category == "background") {
        const auto view=view_;
        return view && view->openCWXPath(value, shell_activate);
    }
    return false;
}

template <typename V, typename A, typename C>
inline std::vector<std::string> TAreaWindow<V, A, C>::openedCWXPath() const {
    const auto view=view_;
    return view ? view->openedCWXPath() : std::vector<std::string>{};
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::setFocused(bool value) noexcept {
    if(disposed_)return;
    const std::weak_ptr<void> life=lifetime_;
    const auto view=view_;
    focused_ = value;
    if constexpr (requires(V& target) { target.setFocused(value); }) {
        if(view)view->setFocused(value);
    }
    if(!life.expired())refreshMenuState();
}

template <typename V, typename A, typename C>
inline void TAreaWindow<V, A, C>::dispose() noexcept {
    if (disposed_) return;
    disposed_ = true;
    lifetime_.reset();
    ++menu_revision_;
    lib::remove_focus_observer(focus_observer_);
    focus_observer_=0;
    unregisterMenus();
    if (context_) {
        if constexpr (std::is_same_v<A, area::Area>) {
            context_->signals.del_area.disconnect(del_connection_);
            context_->signals.ref_area.disconnect(ref_connection_);
        } else if constexpr (std::is_same_v<A, area::Battle>) {
            context_->signals.del_battle.disconnect(del_connection_);
            context_->signals.ref_battle.disconnect(ref_connection_);
        } else if constexpr (std::is_same_v<A, area::Package>) {
            context_->signals.del_package.disconnect(del_connection_);
            context_->signals.ref_package.disconnect(ref_connection_);
        }
        context_->signals.close_adds.disconnect(close_adds_connection_);
        context_->signals.repl_text.disconnect(repl_text_connection_);
        context_->signals.ref_preview_values.disconnect(preview_connection_);
        context_->signals.ref_card_state.disconnect(card_state_connection_);
        context_->signals.ref_undo_max.disconnect(ref_undo_connection_);
    }
    if (menu_) menu_->on_command = {};
    if (window_) {
        window_->on_focus = {};
        window_->on_blur = {};
    }
    view_.reset();
    menu_.reset();
    window_.reset();
    // ホストが停止済みのラッパーをシナリオ終了まで保持しても、場面を
    // 閉じたら共通履歴の利用権を手放す。 他の場面・イベントペイン
    // が編集を続けている場合があるため、共有コアはリセットしない。
    undo_ = nullptr;
    undo_owner_.reset();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::areawindow
