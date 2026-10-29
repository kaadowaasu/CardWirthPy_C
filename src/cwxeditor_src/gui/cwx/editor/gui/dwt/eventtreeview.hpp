#pragma once

// cwx/editor/gui/dwt/eventtreeview.d
// の移植可能なC++20実装。 元のSWT
// ツリーとコンテントのツールボックスのモデ
// ル・操作を保ち、実際の部品はlib::TreeView
// で表す。 Win32/SWTや
// 生成済みの互換型は公開しない。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::eventtreeview {

namespace event = ::cwxeditor::cwx::event;
namespace flagspane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace lib = ::cwxeditor::lib;
namespace path = ::cwxeditor::cwx::path;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace xml = ::cwxeditor::cwx::xml;

using String = lib::String;
using StringView = lib::StringView;
using Parent = flagspane::Parent;
using PanelPtr = flagspane::PanelPtr;
using Commons = flagspane::Commons;
using Props = flagspane::Props;
using UndoManager = flagspane::UndoManager;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using EventTree = event::EventTree;
using EventTreePtr = event::EventTreePtr;
using EventTreeOwner = event::EventTreeOwner;
using TreeItemId = lib::TreeItemId;

/// 表示中の各ツリー項目に保持する情報。
struct NodeInfo {
    enum class Kind { Content };

    Kind kind{Kind::Content};
    TreeItemId id{};
    TreeItemId parentId{};
    ContentPtr content{};
    ContentPtr parentContent{};
    std::size_t index{};
    bool start{};
};

enum class PutMode { Quick, Select, Continue };

class ContentsToolBox;

/// モデルを基準にする、イベントコンテントのツリーエディター。
class EventTreeView final {
public:
    flagspane::Signal<void()> selectionChanged;
    flagspane::Signal<void()> modelChanged;
    flagspane::Signal<void(const EventTreePtr&)> eventTreeChanged;
    flagspane::Signal<void(const ContentPtr&)> contentChanged;
    flagspane::Signal<void()> statusChanged;
    flagspane::Signal<void(bool)> toolWindowChanged;

    EventTreeView(Commons comm, Props prop, EventTreePtr tree,
                  UndoManager& undo, bool readOnly = false);
    EventTreeView(Commons comm, Props prop, EventTree& tree,
                  UndoManager& undo, bool readOnly = false);
    EventTreeView(Commons comm, Props prop, EventTreeOwner& owner,
                  UndoManager& undo, bool readOnly = false);
    EventTreeView(Commons comm, Props prop, EventTreeOwner* owner,
                  UndoManager& undo, bool readOnly = false);
    ~EventTreeView();

    EventTreeView(const EventTreeView&) = delete;
    EventTreeView& operator=(const EventTreeView&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent = {});
    [[nodiscard]] PanelPtr control() const noexcept { return panel_; }
    [[nodiscard]] std::shared_ptr<lib::TreeView> tree() const noexcept {
        return treeView_;
    }
    [[nodiscard]] std::shared_ptr<lib::TreeView> widget() const noexcept {
        return treeView_;
    }

    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Props& props() noexcept { return prop_; }
    [[nodiscard]] const Props& props() const noexcept { return prop_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    [[nodiscard]] EventTreeOwner* owner() const noexcept { return owner_; }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    void setReadOnly(bool value) noexcept { readOnly_ = value; }

    void id(String value) { id_ = std::move(value); }
    [[nodiscard]] const String& id() const noexcept { return id_; }

    [[nodiscard]] EventTreePtr eventTree() const noexcept { return tree_; }
    void setEventTree(EventTreePtr tree);
    void refresh(const EventTreePtr& tree = {});
    void refreshStatusLine();
    void refreshTreeName();
    void redraw() { refresh(); }

    [[nodiscard]] TreeItemId selection() const noexcept { return selectedId_; }
    [[nodiscard]] ContentPtr selectionContent() const noexcept {
        return selectedContent_;
    }
    [[nodiscard]] std::vector<std::size_t> selectionPath() const;
    [[nodiscard]] TreeItemId fromPath(const std::vector<std::size_t>& value) const;
    [[nodiscard]] TreeItemId fromPath(StringView value) const;
    [[nodiscard]] const NodeInfo* nodeInfo(TreeItemId id) const noexcept;
    bool select(TreeItemId id);
    bool select(const ContentPtr& content);
    [[nodiscard]] bool hasSelection() const noexcept {
        return selectedContent_ != nullptr;
    }

    [[nodiscard]] std::vector<TreeItemId> search(StringView value);
    [[nodiscard]] const std::vector<TreeItemId>& searchResults() const noexcept {
        return searchResults_;
    }
    bool searchNext();
    void clearSearch() noexcept;

    [[nodiscard]] String statusLine() const noexcept { return statusLine_; }
    void statusLine(String value);
    [[nodiscard]] bool isFocusControl() const noexcept { return focused_; }
    void setFocus(bool value) noexcept { focused_ = value; }

    [[nodiscard]] bool canEdit() const noexcept;
    [[nodiscard]] bool canEditSelection() const noexcept { return canEdit(); }
    void edit() noexcept { editing_ = canEdit(); }
    void editM() noexcept { edit(); }
    void editCancel() noexcept { editing_ = false; }
    void editEnter() noexcept { editing_ = true; }
    [[nodiscard]] bool editing() const noexcept { return editing_; }
    bool editEnd(TreeItemId id, String value);

    [[nodiscard]] bool canCreate(types::CType type,
                                  ContentPtr parent = {}) const noexcept;
    ContentPtr create(types::CType type, ContentPtr parent = {},
                      String name = {});
    bool addContent(const ContentPtr& content, const ContentPtr& parent = {});
    bool insertContent(std::size_t index, const ContentPtr& content,
                       const ContentPtr& parent = {});
    bool deleteContent(const ContentPtr& content = {});
    bool del1Content();
    bool moveContent(const ContentPtr& content, int delta);

    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    bool up();
    bool down();
    [[nodiscard]] bool canSwapToParent() const noexcept;
    [[nodiscard]] bool canSwapToChild() const noexcept;
    bool swapToParent();
    bool swapToChild();

    [[nodiscard]] bool canConvert(types::CType type,
                                  const ContentPtr& content = {}) const;
    bool convert(types::CType type, const ContentPtr& content = {});
    [[nodiscard]] bool canConvTerminal() const noexcept;
    [[nodiscard]] std::vector<types::CType> conversionTypes() const;

    [[nodiscard]] bool canDoTCPD() const noexcept { return !readOnly_ && tree_; }
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept { return canCopy1Content(); }
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept { return canDoT(); }
    [[nodiscard]] bool canDoClone() const noexcept { return !readOnly_ && canDoC(); }
    [[nodiscard]] bool canCut1Content() const noexcept;
    [[nodiscard]] bool canDel1Content() const noexcept { return canCut1Content(); }
    [[nodiscard]] bool canCopy1Content() const noexcept;
    [[nodiscard]] bool canPasteInsert() const noexcept;

    [[nodiscard]] String itemToXML(TreeItemId id = 0, bool shallow = false,
                                   bool includeClipboardMetadata = true) const;
    [[nodiscard]] lib::ClipboardData clipboardData() const;
    bool copy();
    bool cut();
    bool cut1Content();
    bool paste();
    bool clone();
    bool pasteData(const lib::ClipboardData& data);
    bool pasteScript(StringView script, bool tryInsert = true);
    bool copy1Content();
    bool pasteInsert();
    void copySelectionXML(bool shallow);

    void beginDrag();
    [[nodiscard]] lib::ClipboardData dragData() const;
    bool drop(lib::DragEvent& event);
    [[nodiscard]] lib::DragSourceBinding dragSourceBinding();
    [[nodiscard]] lib::DropTargetBinding dropTargetBinding();

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();

    void expand();
    void collapse();
    void treeOpen();
    void treeClose();
    [[nodiscard]] bool canExpand() const noexcept;
    [[nodiscard]] bool canCollapse() const noexcept;
    [[nodiscard]] bool canExpandTree() const noexcept;
    [[nodiscard]] bool canFoldTree() const noexcept;

    [[nodiscard]] bool showEventTreeDetail() const noexcept {
        return showEventTreeDetail_;
    }
    void showEventTreeDetail(bool value) noexcept { showEventTreeDetail_ = value; }
    void reverseShowEventTreeDetail() noexcept {
        showEventTreeDetail(!showEventTreeDetail_);
    }
    [[nodiscard]] bool showLineNumber() const noexcept { return showLineNumber_; }
    void showLineNumber(bool value) noexcept { showLineNumber_ = value; }
    void reverseShowLineNumber() noexcept { showLineNumber(!showLineNumber_); }
    [[nodiscard]] int eventTreeSlope() const noexcept { return eventTreeSlope_; }
    void eventTreeSlope(int value) noexcept { eventTreeSlope_ = value; }
    [[nodiscard]] bool straightEventTreeView() const noexcept {
        return straightEventTreeView_;
    }
    void straightEventTreeView(bool value) noexcept { straightEventTreeView_ = value; }

    [[nodiscard]] bool openToolWindow(bool focusInEventView = false) noexcept;
    void closeToolWindow() noexcept;
    [[nodiscard]] bool toolWindowOpen() const noexcept { return toolWindowOpen_; }
    void movingShell() noexcept {}
    void getContentsBox();
    [[nodiscard]] ContentsToolBox* contentsToolBox() const noexcept {
        return box_.get();
    }

    [[nodiscard]] String eventText(const ContentPtr& parent,
                                   const ContentPtr& content) const;
    [[nodiscard]] bool canCreateEditor(const ContentPtr& content) const noexcept;

private:
    struct ClipboardPayload {
        String kind;
        xml::XNode node;
    };

    struct Snapshot {
        EventTreePtr tree;
        std::vector<std::size_t> selectedPath;
    };
    struct TreeBuildState {
        TreeItemId nextId{1};
        std::unordered_map<TreeItemId, NodeInfo> nodes;
        std::unordered_map<const Content*, TreeItemId> contentIds;
        std::vector<lib::TreeNode> viewNodes;
    };

    void ensureWidgets(const Parent& parent);
    void rebuildTree();
    TreeItemId appendNode(const ContentPtr& content, const ContentPtr& parent,
                          TreeItemId parentId, std::size_t index,
                          std::unordered_set<const Content*>& visiting,
                          TreeBuildState& build);
    void selectFallback();
    [[nodiscard]] ContentPtr selectedContentOr(const ContentPtr& value) const noexcept;
    [[nodiscard]] ContentPtr insertionOwner(const ContentPtr& value,
                                             bool tryInsert) const;
    [[nodiscard]] bool contains(const ContentPtr& root,
                                const ContentPtr& value) const;
    [[nodiscard]] String contentText(const ContentPtr& content) const;
    [[nodiscard]] Snapshot snapshot() const;
    void restore(const Snapshot& value);
    bool mutate(const std::function<bool()>& action,
                ContentPtr changed = {}, bool useCount = true);
    void notify(EventTreePtr tree = {}, ContentPtr content = {},
                bool useCount = true);
    [[nodiscard]] bool clipboardAvailable() const noexcept;
    void writeClipboard(const lib::ClipboardData& data);
    [[nodiscard]] lib::ClipboardData readClipboard() const;
    [[nodiscard]] std::optional<ClipboardPayload> parseClipboard(
        StringView value) const;
    [[nodiscard]] String makeClipboard(StringView kind,
                                       const xml::XNode& node) const;
    bool pastePayload(const ClipboardPayload& payload, bool tryInsert);
    bool pasteEvent(const EventTreePtr& value);
    bool pasteContent(const EventTreePtr& value, bool tryInsert);
    bool pasteStart(const EventTreePtr& value);
    void bindDragDrop();
    void unbindDragDrop();
    void restoreSelection(const ContentPtr& preferred);

    Commons comm_;
    Props prop_;
    EventTreeOwner* owner_{};
    EventTreePtr tree_{};
    UndoManager* undo_{};
    bool readOnly_{};
    bool initialized_{};
    bool editing_{};
    bool focused_{};
    bool toolWindowOpen_{};
    bool showEventTreeDetail_{};
    bool showLineNumber_{};
    bool straightEventTreeView_{};
    int eventTreeSlope_{};
    String id_;
    String statusLine_;
    PanelPtr panel_;
    std::shared_ptr<lib::TreeView> treeView_;
    std::shared_ptr<lib::StatusBar> status_;
    std::unique_ptr<ContentsToolBox> box_;
    TreeItemId nextId_{1};
    TreeItemId selectedId_{};
    TreeItemId dragSourceId_{};
    ContentPtr selectedContent_{};
    std::unordered_map<TreeItemId, NodeInfo> nodes_;
    std::unordered_map<const Content*, TreeItemId> contentIds_;
    // TreeItemIdは再構成ごとに作り直されるため、一時的な行ID
    // ではなく、 モデルの固定イベントIDでDと同じ展開状態を保つ。
    std::unordered_map<String, bool> expanded_by_event_id_;
    std::vector<TreeItemId> searchResults_;
    std::size_t searchCursor_{};
    String searchText_;
    lib::ClipboardData clipboardMemory_;
    bool hasClipboardMemory_{};
};

/// EventTreeViewが使う、共通のツールバー・ツールウィンドウの状態。
class ContentsToolBox final {
public:
    explicit ContentsToolBox(EventTreeView* parent = nullptr);
    ContentsToolBox(const ContentsToolBox&) = delete;
    ContentsToolBox& operator=(const ContentsToolBox&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent = {});
    [[nodiscard]] PanelPtr control() const noexcept { return panel_; }
    [[nodiscard]] EventTreeView* owner() const noexcept { return parent_; }
    void owner(EventTreeView* value) noexcept { parent_ = value; }
    void dispose() noexcept;

    void arrow() noexcept { arrowMode_ = true; }
    void insertMode(types::CType type) noexcept {
        arrowMode_ = false;
        contentType_ = type;
    }
    [[nodiscard]] bool isInsert() const noexcept { return !arrowMode_; }
    [[nodiscard]] bool isInsertFirst() const noexcept { return insertFirst_; }
    [[nodiscard]] bool isAutoOpen() const noexcept { return autoOpen_; }
    void isInsertFirst(bool value) noexcept { insertFirst_ = value; }
    void isAutoOpen(bool value) noexcept { autoOpen_ = value; }
    [[nodiscard]] bool arrowMode() const noexcept { return arrowMode_; }
    [[nodiscard]] types::CType contentType() const noexcept { return contentType_; }
    [[nodiscard]] PutMode putMode() const noexcept { return putMode_; }
    void putMode(PutMode value) noexcept { putMode_ = value; }
    [[nodiscard]] bool isSingleton() const noexcept { return singleton_; }
    void singleton(bool value) noexcept { singleton_ = value; }
    void openToolWindow() noexcept { open_ = true; }
    void closeToolWindow() noexcept { open_ = false; }
    [[nodiscard]] bool open() const noexcept { return open_; }
    bool create();
    void refreshTemplates();

private:
    EventTreeView* parent_{};
    PanelPtr panel_;
    std::shared_ptr<lib::ToolBar> toolbar_{};
    std::shared_ptr<lib::ComboBox> type_selector_{};
    std::shared_ptr<lib::Menu> template_menu_{};
    std::vector<types::CType> selectable_types_{};
    bool arrowMode_{true};
    bool insertFirst_{};
    bool autoOpen_{true};
    bool singleton_{};
    bool open_{};
    PutMode putMode_{PutMode::Select};
    types::CType contentType_{types::CType::TalkMessage};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventtreeview
