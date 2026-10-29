#pragma once

// cwx/editor/gui/dwt/eventwindow.dの移
// 植可能なC++20実装。 イベント編集をモデル部品とコールバックで
// 構成し、ネイティブの表示はlib::Servicesから提供する。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::comment { class CommentDialog; }

namespace cwxeditor::cwx::editor::gui::dwt::eventview {
class EventView;
}

namespace cwxeditor::cwx::editor::gui::dwt::flagspane {
class UndoManager;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventdialog {
class ContentCommentDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs {
class EventTemplateDialog;
class ScriptVarSetDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventtreedialog {
class EventTreeDialog;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventwindow {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace path = ::cwxeditor::cwx::path;
namespace props = ::cwxeditor::cwx::props;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace contentinitializer = ::cwxeditor::cwx::editor::gui::dwt::contentinitializer;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace rich_eventview = ::cwxeditor::cwx::editor::gui::dwt::eventview;

using String = lib::String;
using StringView = lib::StringView;
using Parent = std::shared_ptr<lib::Container>;
using ResourceId = lib::ResourceId;
using Summary = summary::Summary;
using SummaryPtr = summary::Summary::Owner;
using UndoManager = undo::UndoManager;
// Dの複製ビューは履歴位置も含めて同じ操作管理を使う。 共通の詳細Editor
// ではラッパーでなくflagspaneの管理オブジェクトを使う。
class EventView;
struct OperationalHistoryState {
    // Area/Battleの場面・イベント共通履歴の場合だけ存在する。 ペインが
    // 利用権を保持して履歴位置を維持するが、登録表やコマンドからUIを所有しない。
    std::shared_ptr<UndoManager> core;
    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::flagspane::UndoManager> manager;
    std::vector<std::weak_ptr<EventView>> views;
    bool refreshEditCancellationActive{};
    // 他のビューの編集中止と履歴再生の間、同じ所有者のUndo/Redo通知を保護する。
    bool historyReplayCommandActive{};
    // Dはネイティブのフォーカスで履歴の処理先を選ぶ。 接続時に共通のフ
    // ォーカス通知を記録し、選択の取得時はGUIバックエンドを呼ばない。
    std::weak_ptr<lib::Widget> focused_widget;
    std::weak_ptr<lib::GuiBackend> focus_gui;
    std::uint64_t focus_observer{};
    std::uint64_t focus_cache_revision{};
};
using OperationalHistory = std::shared_ptr<OperationalHistoryState>;
using ContentInitializerContext = contentinitializer::ContentInitializerContext;
using SkinPtr = skin::Skin::Ptr;

enum class OwnerKind : std::int32_t {
    Unknown,
    Area,
    Battle,
    Package,
    SkillCard,
    ItemCard,
    BeastCard,
};

/// イベント編集で使う共有の所有者識別。現在のEffectCard
/// は同じイベントAPIを持つが EventTreeOwnerを継
/// 承しないため、カードをコアの所有者インターフェースへ変換する。
class EventOwnerHandle {
public:
    EventOwnerHandle() = default;

    [[nodiscard]] static EventOwnerHandle from(
        const lib::Owner<event::EventTreeOwner>& owner);
    [[nodiscard]] static EventOwnerHandle from(const area::AreaPtr& owner);
    [[nodiscard]] static EventOwnerHandle from(const area::BattlePtr& owner);
    [[nodiscard]] static EventOwnerHandle from(const area::PackagePtr& owner);
    [[nodiscard]] static EventOwnerHandle from(
        const lib::Owner<card::SkillCard>& owner);
    [[nodiscard]] static EventOwnerHandle from(
        const lib::Owner<card::ItemCard>& owner);
    [[nodiscard]] static EventOwnerHandle from(
        const lib::Owner<card::BeastCard>& owner);

    [[nodiscard]] bool empty() const noexcept { return !owner_; }
    [[nodiscard]] event::EventTreeOwner* get() const noexcept { return owner_.get(); }
    [[nodiscard]] const lib::Owner<event::EventTreeOwner>& owner() const noexcept {
        return owner_;
    }
    [[nodiscard]] const lib::Owner<path::CWXPath>& resource() const noexcept {
        return resource_;
    }
    [[nodiscard]] OwnerKind kind() const noexcept { return kind_; }
    [[nodiscard]] const String& id() const noexcept { return id_; }
    [[nodiscard]] const String& name() const noexcept { return name_; }

private:
    friend class EventWindow;
    lib::Owner<event::EventTreeOwner> owner_{};
    lib::Owner<path::CWXPath> resource_{};
    OwnerKind kind_{OwnerKind::Unknown};
    String id_{};
    String name_{};
};

using OwnerHandle = EventOwnerHandle;

struct EventViewCallbacks {
    using Action = lib::Action;
    using Predicate = lib::Callback<bool()>;
    using OpenPath = lib::Callback<bool(StringView, bool)>;

    Action openDuplicate{};
    lib::Callback<void(String)> statusChanged{};
    Action undo{};
    Action redo{};
    Action cut{};
    Action copy{};
    Action paste{};
    Action del{};
    Action clone{};
    Action up{};
    Action down{};
    Action writeComment{};
    Action toScript{};
    Action toScriptAll{};
    Action toScriptOne{};
    Action startToPackage{};
    Action eventToPackage{};
    Action addManyRounds{};
    Action wrapTree{};
    Action findStartUsers{};
    Action edit{};
    Action cutOne{};
    Action copyOne{};
    Action deleteOne{};
    Action pasteInsert{};
    Action swapToParent{};
    Action swapToChild{};
    Action selectCurrentEvent{};
    Action selectConnectedResource{};
    Action movingWindow{};
    Action moveWindow{};
    Action openToolWindow{};
    Action refresh{};
    Action refreshTitle{};

    Predicate canUp{};
    Predicate canDown{};
    Predicate canWriteComment{};
    Predicate canToScript{};
    Predicate canToScriptAll{};
    Predicate canStartToPackage{};
    Predicate canEventToPackage{};
    Predicate canAddManyRounds{};
    Predicate canFindStartUsers{};
    Predicate canEdit{};
    Predicate canCutOne{};
    Predicate canCopyOne{};
    Predicate canDeleteOne{};
    Predicate canPasteInsert{};
    Predicate canSwapToParent{};
    Predicate canSwapToChild{};
    Predicate canSelectCurrentEvent{};
    Predicate canSelectConnectedResource{};
    OpenPath openPath{};
    lib::CommandGestureAction pasteInsertWithGesture{};
};

class EventTreeView {
public:
    EventTreeView(event::EventTreeOwner& owner, Parent parent, bool readOnly,
                  EventViewCallbacks::Action selectConnectedResource = {},
                  EventViewCallbacks::Predicate canSelectConnectedResource = {},
                  String emptyTreeLabel = "イベント");

    [[nodiscard]] const std::shared_ptr<lib::TreeView>& widget() const noexcept {
        return widget_;
    }
    [[nodiscard]] std::shared_ptr<lib::TreeView>& widget() noexcept { return widget_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const String& statusLine() const noexcept { return status_line_; }
    [[nodiscard]] const lib::Array<std::size_t>& selection() const noexcept {
        return selection_;
    }
    [[nodiscard]] bool hasSelection() const noexcept { return !selection_.empty(); }
    [[nodiscard]] std::size_t selectedIndex() const noexcept;

    void refresh();
    void refreshStatusLine();
    void select(std::size_t index);
    void selectCurrentEvent();
    void up();
    void down();
    void removeSelection();
    void cloneSelection();

    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    [[nodiscard]] bool canSelectConnectedResource() const;
    void selectConnectedResource();

private:
    event::EventTreeOwner* owner_{};
    std::shared_ptr<lib::TreeView> widget_{};
    bool read_only_{};
    String empty_tree_label_{"イベント"};
    String status_line_{};
    lib::Array<std::size_t> selection_{};
    EventViewCallbacks::Action select_connected_resource_{};
    EventViewCallbacks::Predicate can_select_connected_resource_{};
};

class EventView {
public:
    EventView(const ContentInitializerContext& context,
              event::EventTreeOwner& owner, SummaryPtr summary, Parent parent,
              UndoManager& undoManager, bool readOnly,
              EventViewCallbacks callbacks = {},
              OperationalHistory operationalHistory = {});
    ~EventView();

    [[nodiscard]] const Parent& widget() const noexcept { return widget_; }
    [[nodiscard]] rich_eventview::EventView* richView() const noexcept { return host_retired_ ? nullptr : rich_view_.get(); }
    [[nodiscard]] OperationalHistory operationalHistory() const noexcept {
        return host_retired_ ? OperationalHistory{} : rich_undo_.lock();
    }
    void retainHistoryOwner(lib::Owner<event::EventTreeOwner> owner) { history_owner_ = std::move(owner); }
    [[nodiscard]] bool hostRetired() const noexcept { return host_retired_; }
    void prepareContentsPalette();
    [[nodiscard]] std::shared_ptr<lib::Panel> allCompactToolbar();
    [[nodiscard]] EventTreeView& eventTreeView() noexcept { return tree_view_; }
    [[nodiscard]] const EventTreeView& eventTreeView() const noexcept {
        return tree_view_;
    }
    [[nodiscard]] event::EventTreeOwner* eventTreeOwner() const noexcept {
        return owner_;
    }
    [[nodiscard]] const SummaryPtr& summary() const noexcept { return summary_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const String& statusLine() const noexcept {
        return tree_view_.statusLine();
    }

    void refresh();
    void refreshTitle();
    void ownerTitle(String value);
    void openToolWindow();
    void openDuplicate();
    void movingWindow();
    void moveWindow();

    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void setUndoLimit(std::size_t value);
    [[nodiscard]] std::size_t undoLimit() const noexcept;
    void up();
    void down();
    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    void writeComment();
    void toScript();
    void toScriptOne();
    void toScriptAll();
    void startToPackage();
    void eventToPackage();
    void addManyRounds();
    void selectConnectedResource();
    void wrapTree();
    void findStartUsers();
    void edit();
    void createEvent();
    [[nodiscard]] bool debugOpenContent(std::size_t visibleIndex);
    [[nodiscard]] bool debugPlaceContent(types::CType type, bool autoOpen,
                                         std::size_t visibleIndex = 0U);
    [[nodiscard]] bool debugPasteScript(StringView script, bool tryInsert = true);
    [[nodiscard]] const contentinitializer::EventDialogPtr& debugActiveDialog() const noexcept {
        return active_dialog_;
    }
    [[nodiscard]] const std::shared_ptr<eventdialog::ContentCommentDialog>&
    debugCommentDialog() const noexcept { return comment_dialog_; }
    [[nodiscard]] const std::shared_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::smalldialogs::ScriptVarSetDialog>&
    debugScriptVarDialog() const noexcept {
        return script_var_dialog_;
    }
    void closeActiveDialog();
    void retireHostBindings();
    void cutOne();
    void copyOne();
    void deleteOne();
    void pasteInsert();
    void pasteInsert(lib::CommandGesture gesture);
    void swapToParent();
    void swapToChild();
    void selectCurrentEvent();
    void cut();
    void copy();
    void paste();
    void del();
    void clone();

    [[nodiscard]] bool canWriteComment() const;
    [[nodiscard]] bool canToScript() const;
    [[nodiscard]] bool canToScriptOne() const;
    [[nodiscard]] bool canToScriptAll() const;
    [[nodiscard]] bool canWrapTree() const;
    [[nodiscard]] bool canStartToPackage() const;
    [[nodiscard]] bool canEventToPackage() const;
    [[nodiscard]] bool canAddManyRounds() const;
    [[nodiscard]] bool canSelectConnectedResource() const;
    [[nodiscard]] bool canFindStartUsers() const;
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] bool canCutOne() const;
    [[nodiscard]] bool canCopyOne() const;
    [[nodiscard]] bool canDeleteOne() const;
    [[nodiscard]] bool canPasteInsert() const;
    [[nodiscard]] bool canSwapToParent() const;
    [[nodiscard]] bool canSwapToChild() const;
    [[nodiscard]] bool canSelectCurrentEvent() const;
    [[nodiscard]] bool canDoTCPD() const;
    [[nodiscard]] bool canDoT() const;
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoD() const;
    [[nodiscard]] bool canDoClone() const;

    void setClipboardAvailable(bool value) noexcept { clipboard_available_ = value; }
    [[nodiscard]] bool clipboardAvailable() const noexcept {
        return clipboard_available_;
    }
    [[nodiscard]] bool openCWXPath(StringView value, bool windowActivate);
    [[nodiscard]] lib::Array<String> openedCWXPath() const;
    [[nodiscard]] bool canOpenDuplicate() const { return !host_retired_ && static_cast<bool>(callbacks_.openDuplicate); }

private:
    [[nodiscard]] bool canSelectConnectedContentResource() const;
    void selectConnectedContentResource();
    [[nodiscard]] bool copyScript(const lib::Array<event::ContentPtr>& contents,
                                  bool shallow = false);
    void closeContentCommentDialogs();
    void runHistoryReplayCommand(lib::Action replay);
    bool openContentDialog(const event::ContentPtr& content,
                           const event::ContentPtr& parent,
                           bool pendingCreate);
    ContentInitializerContext context_{};
    event::EventTreeOwner* owner_{};
    SummaryPtr summary_{};
    Parent widget_{};
    UndoManager* undo_{};
    bool read_only_{};
    bool clipboard_available_{};
    lib::Array<String> opened_paths_{};
    EventViewCallbacks callbacks_{};
    EventTreeView tree_view_;
    // 有効なEventWindowが管理オブジェクトを所有する。 履歴は
    // この操作用ラッパーを保持でき、管理→ビュー→管理の循環は作らない。
    std::weak_ptr<OperationalHistoryState> rich_undo_{};
    OperationalHistory owned_rich_undo_{};
    lib::Owner<event::EventTreeOwner> history_owner_{};
    std::unique_ptr<rich_eventview::EventView> rich_view_{};
    std::unique_ptr<rich_eventview::EventView> retained_history_view_{};
    lib::Action content_tool_disconnect_{};
    lib::Action event_editor_style_disconnect_;
    lib::Action event_slope_disconnect_;
    lib::Action preview_values_disconnect_;
    lib::Action replacement_disconnect_;
    std::vector<lib::Action> scene_notifications_disconnect_;
    void refreshSceneOwners(event::EventTreeOwner* deleting = nullptr);
    contentinitializer::EventDialogPtr active_dialog_{};
    std::unordered_map<const event::Content*, contentinitializer::EventDialogPtr>
        content_dialogs_{};
    // Dと同じく、Content.eventIdごとに一つのモードレスコメント画面を保持する。
    std::unordered_map<String, std::shared_ptr<eventdialog::ContentCommentDialog>>
        content_comment_dialogs_{};
    std::shared_ptr<eventdialog::ContentCommentDialog> comment_dialog_{};
    std::shared_ptr<eventdialog::ContentCommentDialog>
        event_tree_comment_dialog_{};
    std::unordered_map<const event::EventTree*, std::shared_ptr<eventtreedialog::EventTreeDialog>> tree_dialogs_{};
    bool closing_active_dialog_{};
    bool host_retired_{};
    std::shared_ptr<int> command_lifetime_{std::make_shared<int>(0)};
    std::unordered_map<const path::CWXPath*, std::shared_ptr<comment::CommentDialog>>
        left_comment_dialogs_{};
    std::shared_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::smalldialogs::EventTemplateDialog>
        event_template_dialog_{};
    std::shared_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::smalldialogs::ScriptVarSetDialog>
        script_var_dialog_{};
};

struct CommandEvent {
    bool handled{};
};

enum class Command : std::int32_t {
    EditScene,
    EditSceneDuplicate,
    EditEventDuplicate,
    Undo,
    Redo,
    Up,
    Down,
    Comment,
    ToScript,
    ToScriptOne,
    ToScriptAll,
    EventToPackage,
    StartToPackage,
    WrapTree,
    SelectConnectedResource,
    FindId,
    EditProperty,
    CutOne,
    CopyOne,
    DeleteOne,
    PasteInsert,
    SwapToParent,
    SwapToChild,
    SelectCurrentEvent,
};

struct CommandState {
    lib::Action invoke{};
    lib::Callback<bool()> enabled{};
    lib::CommandGestureAction invoke_with_gesture{};

    CommandState() = default;
    template <typename Invoke, typename Enabled>
    CommandState(Invoke&& invokeValue, Enabled&& enabledValue)
        : invoke(std::forward<Invoke>(invokeValue)),
          enabled(std::forward<Enabled>(enabledValue)) {}
    template <typename Invoke, typename GestureInvoke, typename Enabled>
    CommandState(Invoke&& invokeValue, GestureInvoke&& gestureInvokeValue,
                 Enabled&& enabledValue)
        : invoke(std::forward<Invoke>(invokeValue)),
          enabled(std::forward<Enabled>(enabledValue)),
          invoke_with_gesture(std::forward<GestureInvoke>(gestureInvokeValue)) {}
};

class EventWindow {
public:
    using Context = ContentInitializerContext;
    using ParentType = Parent;

    struct EventWindowCallbacks {
        using Action = lib::Action;
        using Predicate = lib::Callback<bool()>;
        using OpenScene = lib::Callback<void(const EventOwnerHandle&,
                                             const SummaryPtr&, bool)>;
        using OwnerAction = lib::Callback<void(const EventOwnerHandle&)>;
        using SummaryAction = lib::Callback<void(const SummaryPtr&)>;
        using ImageResolver = lib::Callback<ResourceId(OwnerKind)>;
        using TitleResolver = lib::Callback<String(const EventOwnerHandle&)>;
        using TitleChanged = lib::Callback<void(StringView)>;

        OpenScene openScene{};
        Action closeWindow{};
        Action titleFocus{};
        OwnerAction ownerClosed{};
        OwnerAction ownerRefreshed{};
        SummaryAction summaryClosed{};
        ImageResolver image{};
        TitleResolver title{};
        TitleChanged titleChanged{};
        EventViewCallbacks view{};
        bool clipboardAvailable{};
    };

    [[nodiscard]] static EventWindowCallbacks defaultCallbacks();

    EventWindow(Context context, SummaryPtr summary, EventOwnerHandle owner,
                Parent parent = {}, UndoManager* undoManager = nullptr,
                bool readOnly = false,
                EventWindowCallbacks callbacks = defaultCallbacks(),
                OperationalHistory operationalHistory = {});
    EventWindow(props::CProps& prop, variables::FlexEtcProps& vars,
                SummaryPtr summary, EventOwnerHandle owner, Parent parent = {},
                UndoManager* undoManager = nullptr, bool readOnly = false,
                EventWindowCallbacks callbacks = defaultCallbacks(),
                OperationalHistory operationalHistory = {});
    EventWindow(Context context, SummaryPtr summary,
                const lib::Owner<event::EventTreeOwner>& owner,
                Parent parent = {}, UndoManager* undoManager = nullptr,
                bool readOnly = false,
                EventWindowCallbacks callbacks = defaultCallbacks(),
                OperationalHistory operationalHistory = {});
    ~EventWindow();
    void refUndoMax();
    void retireHostBindings();

    EventWindow(const EventWindow&) = delete;
    EventWindow& operator=(const EventWindow&) = delete;

    [[nodiscard]] Parent shell() const noexcept { return window_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    [[nodiscard]] OperationalHistory operationalHistory() const noexcept {
        return event_view_ && !event_view_->hostRetired() ? operational_history_ : OperationalHistory{};
    }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const Context& context() const noexcept { return context_; }
    [[nodiscard]] Context& context() noexcept { return context_; }
    [[nodiscard]] const EventOwnerHandle& ownerHandle() const noexcept {
        return owner_;
    }

    [[nodiscard]] bool invoke(Command command);
    [[nodiscard]] bool invoke(Command command, lib::CommandGesture gesture);
    [[nodiscard]] const std::map<Command, CommandState>& commands() const noexcept {
        return commands_;
    }

    [[nodiscard]] ResourceId image() const;
    [[nodiscard]] String title() const;
    [[nodiscard]] SkinPtr summarySkin() const;
    [[nodiscard]] lib::Callback<void(String)> statusText() const { return {}; }
    void refreshTitle();

    [[nodiscard]] event::EventTreeOwner* eventTreeOwner() const noexcept {
        return owner_.get();
    }
    [[nodiscard]] EventView& eventView() noexcept { return *event_view_; }
    [[nodiscard]] const EventView& eventView() const noexcept {
        return *event_view_;
    }
    [[nodiscard]] EventTreeView& eventTreeView() noexcept {
        return event_view_->eventTreeView();
    }
    [[nodiscard]] const EventTreeView& eventTreeView() const noexcept {
        return event_view_->eventTreeView();
    }

    void movingWindow();
    void moveWindow();

    void cut(CommandEvent& event);
    void copy(CommandEvent& event);
    void paste(CommandEvent& event);
    void del(CommandEvent& event);
    void clone(CommandEvent& event);
    void cut();
    void copy();
    void paste();
    void del();
    void clone();

    [[nodiscard]] bool canDoTCPD() const;
    [[nodiscard]] bool canDoT() const;
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoD() const;
    [[nodiscard]] bool canDoClone() const;

    [[nodiscard]] bool openCWXPath(StringView value, bool windowActivate);
    [[nodiscard]] bool debugOpenContent(std::size_t visibleIndex) {
        return event_view_ && event_view_->debugOpenContent(visibleIndex);
    }
    [[nodiscard]] bool debugPlaceContent(types::CType type, bool autoOpen,
                                         std::size_t visibleIndex = 0U) {
        return event_view_ &&
               event_view_->debugPlaceContent(type, autoOpen, visibleIndex);
    }
    [[nodiscard]] contentinitializer::EventDialogPtr debugActiveDialog() const {
        return event_view_ ? event_view_->debugActiveDialog()
                           : contentinitializer::EventDialogPtr{};
    }
    void closeActiveDialog() {
        if (event_view_) event_view_->closeActiveDialog();
    }
    [[nodiscard]] lib::Array<String> openedCWXPath() const;

    void deleteOwner(const event::EventTreeOwner* owner);
    void deleteOwnerArea(const area::Area* owner);
    void deleteOwnerBattle(const area::Battle* owner);
    void deleteOwnerPackage(const area::Package* owner);
    void deleteOwnerSkillCard(const path::CWXPath* owner,
                              const card::SkillCard* card);
    void deleteOwnerItemCard(const path::CWXPath* owner,
                             const card::ItemCard* card);
    void deleteOwnerBeastCard(const path::CWXPath* owner,
                              const card::BeastCard* card);

    void refOwner(const event::EventTreeOwner* owner);
    void refOwnerArea(const area::Area* owner);
    void refOwnerBattle(const area::Battle* owner);
    void refOwnerPackage(const area::Package* owner);
    void refOwnerSkillCard(const card::SkillCard* owner);
    void refOwnerItemCard(const card::ItemCard* owner);
    void refOwnerBeastCard(const card::BeastCard* owner);
    void closeAdds(const Summary* summary);

private:
    [[nodiscard]] bool invokeImpl(
        Command command, const lib::CommandGesture* gesture);

    Context context_{};
    SummaryPtr summary_{};
    EventOwnerHandle owner_{};
    Parent window_{};
    std::unique_ptr<UndoManager> owned_undo_{};
    UndoManager* undo_{};
    OperationalHistory operational_history_{};
    std::shared_ptr<EventView> event_view_{};
    bool read_only_{};
    bool ref_undo_{};
    EventWindowCallbacks callbacks_{};
    String title_{};
    lib::Action replacement_title_disconnect_;
    std::map<Command, CommandState> commands_{};


    [[nodiscard]] bool openScene(bool duplicate);
    void openDuplicate();
    [[nodiscard]] bool matches(const path::CWXPath* resource) const noexcept;
    void ownerClosed(const path::CWXPath* resource);
    void ownerRefreshed(const path::CWXPath* resource);
    void installCommands();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventwindow
