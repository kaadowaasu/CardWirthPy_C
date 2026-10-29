#pragma once

// cwx/editor/gui/dwt/eventeditor.d
// の移植可能なC++20実装。 イベントコンテン
// トの配置と選択をウィンドウシステムから分離する
// 。部品にはlib::TreeViewを使い、
// Dでスクロール・当たり判定・コメント・行内編
// 集に使う行の配置をPosInfoへ保持する。

#include "cwxeditor_src/core/cwx/event.hpp"
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
#include <variant>
#include <vector>

namespace cwxeditor::cwx::summary {
class Summary;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventeditor {

namespace event = ::cwxeditor::cwx::event;
namespace lib = ::cwxeditor::lib;
namespace pane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace types = ::cwxeditor::cwx::types;
namespace xml = ::cwxeditor::cwx::xml;

using String = lib::String;
using StringView = lib::StringView;
using Parent = pane::Parent;
using PanelPtr = pane::PanelPtr;
using Commons = pane::Commons;
using Props = pane::Props;
using UndoManager = pane::UndoManager;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using EventTree = event::EventTree;
using EventTreePtr = event::EventTreePtr;
using Summary = ::cwxeditor::cwx::summary::Summary;
using TreeItemId = lib::TreeItemId;
using WidgetPtr = lib::WidgetPtr;

class EventEditor;

/// 表示中のコンテント一行の配置と表示データ。
struct PosInfo {
    int depth1{};
    int depth2{};
    int relY{};
    int height{};
    int index{-1};
    int lineNumber{};
    ContentPtr content{};
    String eventText{};
    int eventTextWidth{};
    int commentLineX{};
    std::optional<lib::Rect> commentRect{};
    bool nameVisible{true};

    PosInfo() = default;
    PosInfo(int depth1Value, int depth2Value, int relYValue, int heightValue,
            int indexValue, int lineNumberValue, ContentPtr contentValue,
            String eventTextValue, int eventTextWidthValue);
};
using PosInfoPtr = std::shared_ptr<PosInfo>;

/// 一つの開始コンテントに属する位置の範囲。
struct StartInfo {
    ContentPtr start{};
    int y{};
    int cHeight{};
    std::uint64_t updateCounter{};
    std::size_t fromIndex{};
    std::size_t toIndex{};
    StartInfo* prev{};
    StartInfo* next{};

    [[nodiscard]] std::size_t count() const noexcept {
        return toIndex >= fromIndex ? toIndex - fromIndex : 0;
    }
};
using StartInfoPtr = std::shared_ptr<StartInfo>;

/// DのEventEditorItemに対応する共通の項目ラッパー。
class EventEditorItem final {
public:
    [[nodiscard]] static std::shared_ptr<EventEditorItem> valueOf(
        EventEditor* parent, const ContentPtr& content);

    [[nodiscard]] EventEditor* getParent() const noexcept { return parent_; }
    [[nodiscard]] std::shared_ptr<EventEditorItem> getParentItem() const;
    [[nodiscard]] std::shared_ptr<EventEditorItem> getItem(int index) const;
    [[nodiscard]] std::vector<std::shared_ptr<EventEditorItem>> getItems() const;
    [[nodiscard]] int getItemCount() const;
    [[nodiscard]] int indexOf(const std::shared_ptr<EventEditorItem>& item) const;

    void setExpanded(bool expanded);
    [[nodiscard]] bool getExpanded() const;
    [[nodiscard]] lib::Rect getBounds() const;
    [[nodiscard]] lib::Rect getImageBounds() const;

    [[nodiscard]] const ContentPtr& content() const noexcept { return content_; }
    [[nodiscard]] String text() const;
    [[nodiscard]] bool isStart() const noexcept {
        return content_ && content_->type() == types::CType::Start;
    }

private:
    friend class EventEditor;
    EventEditorItem(EventEditor* parent, ContentPtr content)
        : parent_(parent), content_(std::move(content)) {}

    EventEditor* parent_{};
    ContentPtr content_{};
};
using EventEditorItemPtr = std::shared_ptr<EventEditorItem>;

/// モデルを基準にするイベントツリーのキャンバス。
class EventEditor final {
public:
    using SelectionListener = std::function<void(const ContentPtr&)>;
    using ExpansionListener = std::function<void(const ContentPtr&, bool)>;

    pane::Signal<void()> selectionChanged;
    pane::Signal<void()> modelChanged;
    pane::Signal<void(const ContentPtr&)> contentChanged;
    pane::Signal<void(const ContentPtr&, bool)> expansionChanged;
    pane::Signal<void()> statusChanged;

    EventEditor(Commons comm, const Parent& parent, int style,
                Summary* summ, EventTreePtr tree);
    EventEditor(Commons comm, EventTreePtr tree = {}, Summary* summ = nullptr,
                bool readOnly = false);
    EventEditor(Commons comm, const Parent& parent, EventTreePtr tree,
                bool readOnly = false);
    ~EventEditor();

    EventEditor(const EventEditor&) = delete;
    EventEditor& operator=(const EventEditor&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent = {});
    [[nodiscard]] PanelPtr control() const noexcept { return panel_; }
    [[nodiscard]] std::shared_ptr<lib::TreeView> treeView() const noexcept {
        return treeView_;
    }
    [[nodiscard]] std::shared_ptr<lib::Canvas> canvas() const noexcept {
        return canvas_;
    }

    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    void setUndoManager(UndoManager* value) noexcept { undo_ = value; }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    void setReadOnly(bool value) noexcept;

    [[nodiscard]] EventTreePtr eventTree() const noexcept { return tree_; }
    void eventTree(EventTreePtr value);
    void removeStoredLine(StringView objectId);

    void clearInfo();
    void expandAll();
    void foldAll();
    void showSelection();
    [[nodiscard]] bool canAllExpand() const noexcept;
    [[nodiscard]] bool canAllFoldStart() const noexcept;
    void expandedOperation(bool enabled) noexcept { expandedOperation_ = enabled; }
    [[nodiscard]] bool expandedOperation() const noexcept {
        return expandedOperation_;
    }

    [[nodiscard]] bool showEventTreeDetail() const noexcept {
        return showEventTreeDetail_;
    }
    void showEventTreeDetail(bool value);
    [[nodiscard]] bool showLineNumber() const noexcept { return showLineNumber_; }
    void showLineNumber(bool value);
    [[nodiscard]] bool showTerminalMark() const noexcept {
        return showTerminalMark_;
    }
    void showTerminalMark(bool value);
    [[nodiscard]] bool forceIndentBranchContent() const noexcept {
        return forceIndentBranchContent_;
    }
    void forceIndentBranchContent(bool value);
    [[nodiscard]] int slope() const noexcept { return slope_; }
    void slope(int value);
    [[nodiscard]] bool drawContentTreeLine() const noexcept {
        return drawContentTreeLine_;
    }
    void drawContentTreeLine(bool value);
    [[nodiscard]] bool drawCountOfUseOfStart() const noexcept {
        return drawCountOfUseOfStart_;
    }
    void drawCountOfUseOfStart(bool value);
    [[nodiscard]] bool drawContentWarnings() const noexcept {
        return drawContentWarnings_;
    }
    void drawContentWarnings(bool value);
    [[nodiscard]] int detailAreaWidth() const noexcept {
        return showEventTreeDetail_ ? detailAreaWidth_ : 0;
    }
    void detailAreaWidth(int value) noexcept;

    void updateEventTree();
    void updateEventText();
    void updateContentInfo(const ContentPtr& content);
    void updatePosOne(const ContentPtr& content);
    void updatePosAll();
    void refresh() { updatePosAll(); }
    void redraw() { updatePosAll(); }
    void updateLightup(const ContentPtr& content = {});
    void clearLightup();

    [[nodiscard]] std::vector<PosInfoPtr> positions() const { return positions_; }
    [[nodiscard]] const PosInfo* position(const ContentPtr& content) const;
    [[nodiscard]] int calcX(const PosInfo& pos) const noexcept;
    [[nodiscard]] int calcRight(const PosInfo& pos) const noexcept;
    [[nodiscard]] String eventText(const ContentPtr& parent,
                                   const ContentPtr& content) const;
    [[nodiscard]] String contentText(const ContentPtr& content) const;

    [[nodiscard]] EventEditorItemPtr getItem(int index) const;
    [[nodiscard]] EventEditorItemPtr getItem(const lib::Point& point) const;
    [[nodiscard]] std::vector<EventEditorItemPtr> getItems() const;
    [[nodiscard]] int getItemCount() const noexcept;
    [[nodiscard]] std::vector<EventEditorItemPtr> getSelection() const;
    void setSelection(const std::vector<EventEditorItemPtr>& items);
    void select(const EventEditorItemPtr& item);
    void select(const ContentPtr& content);
    void clearRestore() noexcept { restoreLine_ = false; }
    [[nodiscard]] int indexOf(const EventEditorItemPtr& item) const;
    [[nodiscard]] int indexOf(const ContentPtr& content) const;
    [[nodiscard]] EventEditorItemPtr getTopItem() const;
    void setTopItem(const EventEditorItemPtr& item);
    [[nodiscard]] ContentPtr getContent(int x, int y) const;
    [[nodiscard]] std::size_t topIndex() const noexcept { return topIndex_; }
    void topIndex(std::size_t value) noexcept { topIndex_ = value; }
    [[nodiscard]] const ContentPtr& selectionContent() const noexcept {
        return selected_;
    }
    [[nodiscard]] String statusLine() const noexcept { return statusLine_; }

    void addSelectionListener(SelectionListener listener);
    void addTreeListener(ExpansionListener listener);
    void callSelectChanged();
    void callExpandedChanged(const EventEditorItemPtr& item);

    [[nodiscard]] bool editEnd(const EventEditorItemPtr& item, String value);
    [[nodiscard]] bool canEdit() const noexcept {
        return !readOnly_ && selected_ != nullptr;
    }
    void nameVisible(int index, bool visible);

    [[nodiscard]] bool canCopy() const noexcept { return selected_ != nullptr; }
    [[nodiscard]] bool canPaste() const noexcept;
    [[nodiscard]] bool canCut() const noexcept;
    [[nodiscard]] lib::ClipboardData clipboardData() const;
    bool copy();
    bool cut();
    bool paste();
    bool pasteData(const lib::ClipboardData& data);
    void beginDrag();
    [[nodiscard]] lib::ClipboardData dragData() const;
    bool drop(lib::DragEvent& event);
    [[nodiscard]] lib::DragSourceBinding dragSourceBinding();
    [[nodiscard]] lib::DropTargetBinding dropTargetBinding();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();

private:
    friend class EventEditorItem;
    friend class EventEdit;

    struct StoredLine {
        int lineNumber{};
        int selected{};
    };
    struct ClipboardPayload {
        String kind;
        xml::XNode node;
    };
    struct Snapshot {
        EventTreePtr tree;
        String selectedId;
    };
    struct TreeBuildState {
        TreeItemId nextId{1};
        std::vector<lib::TreeNode> nodes;
        std::unordered_map<TreeItemId, ContentPtr> nodeContents;
        std::unordered_map<const Content*, TreeItemId> contentIds;
        std::unordered_map<String, EventEditorItemPtr> items;
    };

    void ensureWidgets(const Parent& parent);
    void rebuildPositions();
    void appendNode(const ContentPtr& content, const ContentPtr& parent,
                    TreeItemId parentId, std::size_t index, int depth1,
                    int depth2, int& lineNumber, int& y,
                    std::unordered_set<const Content*>& visiting);
    [[nodiscard]] bool rebuildTreeNodes();
    void selectFallback();
    void refreshStatusLine();
    [[nodiscard]] bool contains(const ContentPtr& root,
                                const ContentPtr& value) const;
    [[nodiscard]] Snapshot snapshot() const;
    void restore(const Snapshot& value);
    bool mutate(const std::function<bool()>& action,
                const ContentPtr& changed = {});
    void notify(const ContentPtr& changed = {});
    [[nodiscard]] bool clipboardAvailable() const noexcept;
    void writeClipboard(const lib::ClipboardData& data);
    [[nodiscard]] lib::ClipboardData readClipboard() const;
    [[nodiscard]] String itemToXML(const ContentPtr& content) const;
    [[nodiscard]] std::optional<ClipboardPayload> parseClipboard(
        StringView value) const;
    bool pastePayload(const ClipboardPayload& payload);
    bool pasteContent(const EventTreePtr& value);
    static void appendClipboardFormats(lib::ClipboardData& data);
    void bindDragDrop();
    void unbindDragDrop();

    Commons comm_;
    Parent parent_{};
    Summary* summary_{};
    EventTreePtr tree_{};
    UndoManager* undo_{};
    bool readOnly_{};
    bool initialized_{};
    bool expandedOperation_{true};
    bool showEventTreeDetail_{true};
    bool showLineNumber_{};
    bool showTerminalMark_{};
    bool forceIndentBranchContent_{};
    bool drawContentTreeLine_{};
    bool drawCountOfUseOfStart_{};
    bool drawContentWarnings_{};
    int slope_{12};
    int detailAreaWidth_{240};
    int lineHeight_{20};
    int imageWidth_{16};
    int selectedIndex_{-1};
    std::size_t topIndex_{};
    bool restoreLine_{};
    ContentPtr selected_{};
    ContentPtr lightup_{};
    String statusLine_{};
    String id_{"eventeditor"};
    std::unordered_map<String, bool> expanded_;
    std::unordered_map<String, StoredLine> storedLine_;
    std::vector<PosInfoPtr> positions_{};
    std::unordered_map<String, PosInfoPtr> positionTable_{};
    std::vector<StartInfoPtr> startInfos_{};
    std::unordered_map<String, StartInfoPtr> startInfoByName_{};
    std::unordered_map<String, EventEditorItemPtr> items_{};
    std::unordered_map<TreeItemId, ContentPtr> nodeContents_{};
    std::unordered_map<const Content*, TreeItemId> contentIds_{};
    TreeItemId nextId_{1};
    TreeItemId dragSourceId_{};
    lib::ClipboardData clipboardMemory_{};
    bool hasClipboardMemory_{};
    PanelPtr panel_{};
    std::shared_ptr<lib::TreeView> treeView_{};
    std::shared_ptr<lib::Canvas> canvas_{};
    std::shared_ptr<lib::StatusBar> status_{};
};

/// 同じコマンド群に対してDのTreeまたはEventEditorを選ぶ場合
/// に、 eventview/eventtreeviewが使うアダプター。
struct TreeViewWrapper {
    using Item = std::variant<std::monostate, EventEditorItemPtr, TreeItemId>;

    std::shared_ptr<lib::TreeView> tree{};
    EventEditor* editor{};

    [[nodiscard]] WidgetPtr control() const;
    [[nodiscard]] Item getTopItem() const;
    void setTopItem(const Item& item) const;
    [[nodiscard]] std::vector<Item> getSelection() const;
    void setSelection(const std::vector<Item>& items) const;
    void select(const Item& item) const;
    [[nodiscard]] Item getItem(int index) const;
    [[nodiscard]] Item getItem(const lib::Point& point) const;
    [[nodiscard]] int getItemCount() const;
    [[nodiscard]] int getItemCount(const Item& item) const;
    [[nodiscard]] int getItemCount(const TreeViewWrapper& view) const;
    void showSelection() const;
    [[nodiscard]] std::vector<Item> getItems() const;
    [[nodiscard]] std::vector<Item> getItems(const Item& item) const;
    [[nodiscard]] Item getItem(const Item& item, int index) const;
    [[nodiscard]] bool getExpanded(const Item& item) const;
    void setExpanded(const Item& item, bool expanded) const;
    [[nodiscard]] Item getParentItem(const Item& item) const;
    [[nodiscard]] Item getItem(const TreeViewWrapper& view, int index) const;
    [[nodiscard]] int indexOf(const Item& item) const;
    [[nodiscard]] int indexOf(const Item& item, const Item& child) const;
    [[nodiscard]] Item topItem(const Item& item) const;
    void treeExpandedAll() const;
    [[nodiscard]] lib::Rect getImageBounds(const Item& item) const;
};

/// EventEditorの名前の行内編集。 独自の
/// 部品生成処理がなければTextEditを使う。
class EventEdit final {
public:
    using ControlPtr = lib::WidgetPtr;
    using EditEnd = std::function<void(const EventEditorItemPtr&,
                                       const ControlPtr&)>;
    using CreateEditor = std::function<ControlPtr(const EventEditorItemPtr&)>;

    EventEdit(Commons comm, EventEditor& list, EditEnd editEnd,
              CreateEditor createEditor = {});
    EventEdit(Commons comm, EventEditor* list, EditEnd editEnd,
              CreateEditor createEditor = {});

    [[nodiscard]] EventEditorItemPtr selectionM(int x, int y) const;
    [[nodiscard]] EventEditorItemPtr selectionK() const;
    void end(const ControlPtr& control);
    void clearEdit(bool cancel);
    void startEdit(const EventEditorItemPtr& item);
    void layout();
    void startEdit();
    void minimumWidth(int value) noexcept { minimumWidth_ = value; }
    [[nodiscard]] int minimumWidth() const noexcept { return minimumWidth_; }
    [[nodiscard]] bool isEditing() const noexcept { return edit_ != nullptr; }
    void cancel();
    void enter();
    [[nodiscard]] EventEditorItemPtr editingItem() const noexcept { return edit_; }
    [[nodiscard]] ControlPtr editor() const noexcept { return editor_; }

private:
    Commons comm_;
    EventEditor* list_{};
    EditEnd editEnd_{};
    CreateEditor createEditor_{};
    ControlPtr editor_{};
    EventEditorItemPtr edit_{};
    String oldName_{};
    int minimumWidth_{120};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventeditor
