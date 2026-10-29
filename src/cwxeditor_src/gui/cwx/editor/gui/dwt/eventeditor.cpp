#include "eventeditor.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::eventeditor {
namespace {

constexpr StringView kClipboardRoot = "CWXEditorClipboard";
constexpr StringView kClipboardMime = "application/x-cardwirth-event+xml";

[[nodiscard]] String lowerCopy(const StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

[[nodiscard]] bool hasOperation(const lib::DropOperation value,
                                const lib::DropOperation bit) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(bit)) != 0U;
}

[[nodiscard]] ContentPtr findContent(const EventTreePtr& tree,
                                      const StringView id) {
    if (!tree || id.empty()) return {};
    std::vector<ContentPtr> pending(tree->starts().begin(), tree->starts().end());
    std::unordered_set<const Content*> visited;
    while (!pending.empty()) {
        auto content = pending.back();
        pending.pop_back();
        if (!content || !visited.insert(content.get()).second) continue;
        if (content->eventId() == id) return content;
        for (const auto& child : content->next()) pending.push_back(child);
    }
    return {};
}

[[nodiscard]] String uniqueStartName(const EventTree& tree, String value) {
    if (value.empty()) value = "Start";
    if (!tree.hasStart(value)) return value;
    const auto base = value;
    for (std::size_t index = 2;; ++index) {
        value = base + " (" + std::to_string(index) + ")";
        if (!tree.hasStart(value)) return value;
    }
}

[[nodiscard]] TreeItemId treeItemId(const TreeViewWrapper::Item& item) {
    if (const auto* id = std::get_if<TreeItemId>(&item)) return *id;
    return 0;
}

}  // namespace

PosInfo::PosInfo(const int depth1Value, const int depth2Value,
                 const int relYValue, const int heightValue,
                 const int indexValue, const int lineNumberValue,
                 ContentPtr contentValue, String eventTextValue,
                 const int eventTextWidthValue)
    : depth1(depth1Value), depth2(depth2Value), relY(relYValue),
      height(heightValue), index(indexValue), lineNumber(lineNumberValue),
      content(std::move(contentValue)), eventText(std::move(eventTextValue)),
      eventTextWidth(eventTextWidthValue) {}

std::shared_ptr<EventEditorItem> EventEditorItem::valueOf(
    EventEditor* parent, const ContentPtr& content) {
    if (!parent || !content) return {};
    const auto key = content->eventId();
    if (!key.empty()) {
        if (const auto found = parent->items_.find(key);
            found != parent->items_.end()) {
            return found->second;
        }
    }
    auto item = std::shared_ptr<EventEditorItem>(
        new EventEditorItem(parent, content));
    if (!key.empty()) parent->items_[key] = item;
    return item;
}

std::shared_ptr<EventEditorItem> EventEditorItem::getParentItem() const {
    return parent_ ? valueOf(parent_, content_ ? content_->parent() : ContentPtr{})
                   : EventEditorItemPtr{};
}

std::shared_ptr<EventEditorItem> EventEditorItem::getItem(const int index) const {
    if (!content_ || index < 0 || static_cast<std::size_t>(index) >= content_->next().size())
        return {};
    return valueOf(parent_, content_->next()[static_cast<std::size_t>(index)]);
}

std::vector<std::shared_ptr<EventEditorItem>> EventEditorItem::getItems() const {
    std::vector<std::shared_ptr<EventEditorItem>> result;
    if (!content_) return result;
    result.reserve(content_->next().size());
    for (const auto& child : content_->next()) result.push_back(valueOf(parent_, child));
    return result;
}

int EventEditorItem::getItemCount() const {
    return content_ ? static_cast<int>(content_->next().size()) : 0;
}

int EventEditorItem::indexOf(const std::shared_ptr<EventEditorItem>& item) const {
    if (!content_ || !item || !item->content_) return -1;
    const auto it = std::find(content_->next().begin(), content_->next().end(),
                              item->content_);
    return it == content_->next().end()
               ? -1
               : static_cast<int>(std::distance(content_->next().begin(), it));
}

void EventEditorItem::setExpanded(const bool expanded) {
    if (!parent_ || !content_) return;
    if (getExpanded() == expanded) return;
    parent_->expanded_[content_->eventId()] = expanded;
    parent_->updatePosOne(content_);
    parent_->callExpandedChanged(
        EventEditorItem::valueOf(parent_, content_));
}

bool EventEditorItem::getExpanded() const {
    if (!parent_ || !content_) return true;
    const auto found = parent_->expanded_.find(content_->eventId());
    return found == parent_->expanded_.end() || found->second;
}

lib::Rect EventEditorItem::getBounds() const {
    if (!parent_ || !content_) return {};
    const auto* pos = parent_->position(content_);
    if (!pos) return {};
    return {static_cast<float>(parent_->calcX(*pos)),
            static_cast<float>(pos->relY - static_cast<int>(parent_->topIndex_) *
                               parent_->lineHeight_),
            static_cast<float>(std::max(16, pos->eventTextWidth + 20)),
            static_cast<float>(pos->height)};
}

lib::Rect EventEditorItem::getImageBounds() const {
    if (!parent_ || !content_) return {};
    const auto* pos = parent_->position(content_);
    if (!pos) return {};
    return {static_cast<float>(parent_->calcX(*pos)),
            static_cast<float>(pos->relY - static_cast<int>(parent_->topIndex_) *
                               parent_->lineHeight_), 16.0F,
            static_cast<float>(parent_->lineHeight_)};
}

String EventEditorItem::text() const {
    return parent_ && content_ ? parent_->contentText(content_) : String{};
}

EventEditor::EventEditor(Commons comm, const Parent& parent, const int,
                         Summary* summ, EventTreePtr tree)
    : comm_(std::move(comm)), parent_(parent), summary_(summ),
      tree_(std::move(tree)) {
    if (tree_ && comm_.useCounter) tree_->setUseCounter(comm_.useCounter);
    if (parent_) (void)createControl(parent_);
    updatePosAll();
}

EventEditor::EventEditor(Commons comm, EventTreePtr tree, Summary* summ,
                         const bool readOnly)
    : comm_(std::move(comm)), summary_(summ), tree_(std::move(tree)),
      readOnly_(readOnly) {
    if (tree_ && comm_.useCounter) tree_->setUseCounter(comm_.useCounter);
    updatePosAll();
}

EventEditor::EventEditor(Commons comm, const Parent& parent, EventTreePtr tree,
                         const bool readOnly)
    : EventEditor(std::move(comm), std::move(tree), nullptr, readOnly) {
    parent_ = parent;
    (void)createControl(parent_);
}

EventEditor::~EventEditor() {
    unbindDragDrop();
    if (treeView_) {
        treeView_->on_selection_changed = {};
        treeView_->on_expansion_changed = {};
        treeView_->on_item_edited = {};
        treeView_->on_activate = {};
    }
}

void EventEditor::setReadOnly(const bool value) noexcept {
    readOnly_ = value;
    if (treeView_) treeView_->set_enabled(true);
    if (canvas_) canvas_->set_enabled(true);
}

void EventEditor::ensureWidgets(const Parent& parent) {
    if (!panel_) panel_ = std::make_shared<lib::Panel>();
    if (!treeView_) {
        treeView_ = std::make_shared<lib::TreeView>();
        treeView_->accessibility_name = "Event editor";
        treeView_->on_selection_changed = [this](const std::vector<TreeItemId>& ids) {
            const auto old = selected_;
            selected_.reset();
            if (!ids.empty()) {
                const auto found = nodeContents_.find(ids.front());
                if (found != nodeContents_.end()) selected_ = found->second;
            }
            selectedIndex_ = indexOf(selected_);
            refreshStatusLine();
            if (old != selected_) callSelectChanged();
        };
        treeView_->on_expansion_changed = [this](const TreeItemId id,
                                                  const bool expanded) {
            const auto found = nodeContents_.find(id);
            if (found == nodeContents_.end()) return;
            expanded_[found->second->eventId()] = expanded;
            updatePosOne(found->second);
            callExpandedChanged(EventEditorItem::valueOf(this, found->second));
        };
        treeView_->on_item_edited = [this](const TreeItemId id, String value) {
            const auto found = nodeContents_.find(id);
            if (found != nodeContents_.end())
                (void)editEnd(EventEditorItem::valueOf(this, found->second),
                               std::move(value));
        };
        treeView_->on_activate = [this](const TreeItemId id) {
            const auto found = nodeContents_.find(id);
            if (found != nodeContents_.end()) select(found->second);
        };
        panel_->add(treeView_);
        canvas_ = std::make_shared<lib::Canvas>();
        canvas_->accessibility_name = "Event tree canvas";
        canvas_->double_buffered = true;
        canvas_->set_visible(false);
        panel_->add(canvas_);
        status_ = std::make_shared<lib::StatusBar>();
        panel_->add(status_);
    }
    parent_ = parent;
    if (parent) parent->add(panel_);
}

PanelPtr EventEditor::createControl(const Parent& parent) {
    ensureWidgets(parent);
    if (!initialized_) {
        initialized_ = true;
        bindDragDrop();
    }
    updatePosAll();
    return panel_;
}

void EventEditor::eventTree(EventTreePtr value) {
    if (tree_ == value) return;
    if (tree_ && !tree_->objectId().empty() && selectedIndex_ >= 0)
        storedLine_[tree_->objectId()] = {static_cast<int>(topIndex_), selectedIndex_};
    tree_ = std::move(value);
    if (tree_ && comm_.useCounter) tree_->setUseCounter(comm_.useCounter);
    selected_.reset();
    selectedIndex_ = -1;
    if (tree_ && storedLine_.contains(tree_->objectId())) restoreLine_ = true;
    updateEventTree();
}

void EventEditor::removeStoredLine(const StringView objectId) {
    storedLine_.erase(String(objectId));
}

void EventEditor::clearInfo() {
    if (treeView_) (void)lib::replace_tree_nodes(treeView_, {}, {});
    selected_.reset();
    lightup_.reset();
    selectedIndex_ = -1;
    positions_.clear();
    positionTable_.clear();
    startInfos_.clear();
    startInfoByName_.clear();
    items_.clear();
    nodeContents_.clear();
    contentIds_.clear();
}

void EventEditor::expandAll() {
    if (!tree_) return;
    std::vector<ContentPtr> pending(tree_->starts().begin(), tree_->starts().end());
    std::unordered_set<const Content*> visited;
    while (!pending.empty()) {
        auto content = pending.back();
        pending.pop_back();
        if (!content || !visited.insert(content.get()).second) continue;
        expanded_[content->eventId()] = true;
        for (const auto& child : content->next()) pending.push_back(child);
    }
    updatePosAll();
}

void EventEditor::foldAll() {
    if (!tree_) return;
    for (const auto& start : tree_->starts())
        if (start && !start->next().empty()) expanded_[start->eventId()] = false;
    updatePosAll();
}

void EventEditor::showSelection() {
    if (selectedIndex_ >= 0) topIndex_ = static_cast<std::size_t>(selectedIndex_);
    if (topIndex_ >= positions_.size() && !positions_.empty()) topIndex_ = positions_.size() - 1;
}

bool EventEditor::canAllExpand() const noexcept {
    for (const auto& value : expanded_)
        if (!value.second) return true;
    return false;
}

bool EventEditor::canAllFoldStart() const noexcept {
    if (!tree_) return false;
    for (const auto& start : tree_->starts()) {
        if (!start || start->next().empty()) continue;
        const auto found = expanded_.find(start->eventId());
        if (found == expanded_.end() || found->second) return true;
    }
    return false;
}

void EventEditor::showEventTreeDetail(const bool value) {
    if (showEventTreeDetail_ == value) return;
    showEventTreeDetail_ = value;
    updatePosAll();
}

void EventEditor::showLineNumber(const bool value) {
    if (showLineNumber_ == value) return;
    showLineNumber_ = value;
    updatePosAll();
}

void EventEditor::showTerminalMark(const bool value) {
    if (showTerminalMark_ == value) return;
    showTerminalMark_ = value;
    updatePosAll();
}

void EventEditor::forceIndentBranchContent(const bool value) {
    if (forceIndentBranchContent_ == value) return;
    forceIndentBranchContent_ = value;
    updatePosAll();
}

void EventEditor::slope(const int value) {
    slope_ = std::max(0, value);
    updatePosAll();
}

void EventEditor::drawContentTreeLine(const bool value) {
    drawContentTreeLine_ = value;
    updatePosAll();
}

void EventEditor::drawCountOfUseOfStart(const bool value) {
    drawCountOfUseOfStart_ = value;
    updatePosAll();
}

void EventEditor::drawContentWarnings(const bool value) {
    drawContentWarnings_ = value;
    updatePosAll();
}

void EventEditor::detailAreaWidth(const int value) noexcept {
    detailAreaWidth_ = std::max(imageWidth_, value);
    if (treeView_) treeView_->set_bounds({0.0F, 0.0F, static_cast<float>(detailAreaWidth_), 0.0F});
}

void EventEditor::updateEventTree() { updatePosAll(); }

void EventEditor::updateEventText() {
    for (const auto& pos : positions_) {
        if (!pos || !pos->content) continue;
        pos->eventText = eventText(pos->content->parent(), pos->content);
        pos->eventTextWidth = comm_.services.graphics
            ? static_cast<int>(comm_.services.graphics
                                   ->measure_text(pos->eventText, {}, 0.0F).width)
            : static_cast<int>(pos->eventText.size() * 8U);
    }
    (void)rebuildTreeNodes();
    refreshStatusLine();
    modelChanged.call();
}

void EventEditor::updateContentInfo(const ContentPtr& content) {
    if (!content) return;
    const auto found = positionTable_.find(content->eventId());
    if (found == positionTable_.end()) {
        updatePosAll();
        return;
    }
    auto& position = found->second;
    position->eventText = eventText(content->parent(), content);
    position->eventTextWidth = comm_.services.graphics
            ? static_cast<int>(comm_.services.graphics
                                   ->measure_text(position->eventText, {}, 0.0F).width)
            : static_cast<int>(position->eventText.size() * 8U);
    if (treeView_) (void)rebuildTreeNodes();
    refreshStatusLine();
    modelChanged.call();
    contentChanged.call(content);
}

void EventEditor::updatePosOne(const ContentPtr& content) {
    if (!content) return;
    // 展開・折りたたみは可視部分木と後続の全行位置を
    // 変える。プロパティだけの変更は既存のネイティ
    // ブノードを保持し、文字列をその場で更新する。
    const bool should_show_children =
        content->detail().owner && !content->next().empty() &&
        (!expanded_.contains(content->eventId()) || expanded_.at(content->eventId()));
    bool children_are_visible{};
    if (!content->next().empty())
        children_are_visible = positionTable_.contains(content->next().front()->eventId());
    if (should_show_children != children_are_visible) {
        updatePosAll();
        return;
    }
    updateContentInfo(content);
}

void EventEditor::updatePosAll() {
    const auto oldSelected = selected_;
    rebuildPositions();
    if (selected_ && !findContent(tree_, selected_->eventId())) selected_.reset();
    if (!selected_ && !positions_.empty()) selected_ = positions_.front()->content;
    selectedIndex_ = indexOf(selected_);
    if (restoreLine_ && tree_) {
        if (const auto found = storedLine_.find(tree_->objectId()); found != storedLine_.end()) {
            topIndex_ = found->second.lineNumber >= 0
                            ? static_cast<std::size_t>(found->second.lineNumber)
                            : 0;
            if (found->second.selected >= 0 &&
                found->second.selected < static_cast<int>(positions_.size())) {
                selectedIndex_ = found->second.selected;
                selected_ = positions_[static_cast<std::size_t>(selectedIndex_)]->content;
            }
        }
        restoreLine_ = false;
    }
    if (topIndex_ >= positions_.size() && !positions_.empty()) topIndex_ = positions_.size() - 1;
    (void)rebuildTreeNodes();
    refreshStatusLine();
    if (oldSelected != selected_) callSelectChanged();
    modelChanged.call();
}

String EventEditor::contentText(const ContentPtr& content) const {
    if (!content) return {};
    const auto detail = content->detail();
    String result;
    if (!detail.type.empty()) result = detail.type;
    else if (!detail.names.empty()) result = detail.names.front();
    if (!content->name().empty()) {
        if (!result.empty()) result += ": ";
        result += content->name();
    }
    if (showEventTreeDetail_ && !content->comment().empty()) {
        result += "  — ";
        result += content->comment();
    }
    return result;
}

String EventEditor::eventText(const ContentPtr&, const ContentPtr& content) const {
    return contentText(content);
}

void EventEditor::appendNode(const ContentPtr& content, const ContentPtr& parent,
                             const TreeItemId, const std::size_t,
                             const int depth1, const int depth2, int& lineNumber,
                             int& y, std::unordered_set<const Content*>& visiting) {
    if (!content || !visiting.insert(content.get()).second) return;
    const auto text = eventText(parent, content);
    const int height = showTerminalMark_ && content->type() != types::CType::Start &&
                               content->next().empty()
                           ? lineHeight_ * 2
                           : lineHeight_;
    auto pos = std::make_shared<PosInfo>(
        depth1, depth2, y, height, static_cast<int>(positions_.size()),
        lineNumber++, content, text, static_cast<int>(text.size() * 8U));
    positions_.push_back(pos);
    positionTable_[content->eventId()] = pos;
    y += height;
    if (expanded_.find(content->eventId()) == expanded_.end())
        expanded_[content->eventId()] = true;
    if (expanded_[content->eventId()] && content->detail().owner) {
        for (std::size_t index = 0; index < content->next().size(); ++index) {
            const auto& child = content->next()[index];
            appendNode(child, content, 0, index, depth1 + 1,
                       forceIndentBranchContent_ ? depth2 : depth2 + 1,
                       lineNumber, y, visiting);
        }
    }
    visiting.erase(content.get());
}

void EventEditor::rebuildPositions() {
    positions_.clear();
    positionTable_.clear();
    startInfos_.clear();
    startInfoByName_.clear();
    if (!tree_) {
        selected_.reset();
        return;
    }
    int lineNumber = 1;
    int y = 0;
    for (const auto& start : tree_->starts()) {
        if (!start) continue;
        auto info = std::make_shared<StartInfo>();
        info->start = start;
        info->y = y;
        info->fromIndex = positions_.size();
        const auto before = y;
        std::unordered_set<const Content*> visiting;
        appendNode(start, {}, 0, 0, 0, 0, lineNumber, y, visiting);
        info->toIndex = positions_.size();
        info->cHeight = y - before;
        info->updateCounter = start->updateCounter();
        if (!startInfos_.empty()) {
            info->prev = startInfos_.back().get();
            startInfos_.back()->next = info.get();
        }
        startInfos_.push_back(info);
        startInfoByName_[start->name()] = info;
    }
}

bool EventEditor::rebuildTreeNodes() {
    if (!treeView_) return false;
    TreeBuildState state;
    std::unordered_set<const Content*> visiting;
    const auto append = [&](const auto& self, const ContentPtr& content,
                            const ContentPtr& parent, const TreeItemId parentId,
                            const std::size_t) -> void {
        if (!content || !visiting.insert(content.get()).second) return;
        const auto id = state.nextId++;
        const auto text = eventText(parent, content);
        state.nodes.push_back({id, parentId, text, content->eventId(), 0,
                               expanded_.find(content->eventId()) == expanded_.end() ||
                                   expanded_.at(content->eventId()),
                               false, false});
        state.nodeContents[id] = content;
        state.contentIds[content.get()] = id;
        const auto item = items_.find(content->eventId());
        state.items[content->eventId()] =
            item != items_.end() && item->second
                ? item->second
                : EventEditorItemPtr(new EventEditorItem(this, content));
        for (std::size_t childIndex = 0; childIndex < content->next().size(); ++childIndex)
            self(self, content->next()[childIndex], content, id, childIndex);
        visiting.erase(content.get());
    };
    if (tree_)
        for (std::size_t index = 0; index < tree_->starts().size(); ++index)
            append(append, tree_->starts()[index], {}, 0, index);

    std::vector<TreeItemId> selection;
    if (selected_) {
        const auto found = state.contentIds.find(selected_.get());
        if (found != state.contentIds.end()) selection.push_back(found->second);
    }
    if (!lib::replace_tree_nodes(treeView_, std::move(state.nodes),
                                 std::move(selection)))
        return false;
    nextId_ = state.nextId;
    nodeContents_ = std::move(state.nodeContents);
    contentIds_ = std::move(state.contentIds);
    items_ = std::move(state.items);
    return true;
}

const PosInfo* EventEditor::position(const ContentPtr& content) const {
    if (!content) return nullptr;
    const auto found = positionTable_.find(content->eventId());
    return found == positionTable_.end() ? nullptr : found->second.get();
}

int EventEditor::calcX(const PosInfo& pos) const noexcept {
    const int lineNumberWidth = showLineNumber_ ? 32 : 0;
    return lineNumberWidth + pos.depth1 * imageWidth_ + pos.depth2 * slope_;
}

int EventEditor::calcRight(const PosInfo& pos) const noexcept {
    return calcX(pos) + (pos.eventTextWidth > 0 ? pos.eventTextWidth + 20 : imageWidth_);
}

EventEditorItemPtr EventEditor::getItem(const int index) const {
    if (!tree_ || index < 0 || static_cast<std::size_t>(index) >= tree_->starts().size())
        return {};
    return EventEditorItem::valueOf(const_cast<EventEditor*>(this),
                                    tree_->starts()[static_cast<std::size_t>(index)]);
}

EventEditorItemPtr EventEditor::getItem(const lib::Point& point) const {
    return EventEditorItem::valueOf(const_cast<EventEditor*>(this),
                                    getContent(static_cast<int>(point.x), static_cast<int>(point.y)));
}

std::vector<EventEditorItemPtr> EventEditor::getItems() const {
    std::vector<EventEditorItemPtr> result;
    if (!tree_) return result;
    for (const auto& start : tree_->starts())
        result.push_back(EventEditorItem::valueOf(const_cast<EventEditor*>(this), start));
    return result;
}

int EventEditor::getItemCount() const noexcept {
    return tree_ ? static_cast<int>(tree_->starts().size()) : 0;
}

std::vector<EventEditorItemPtr> EventEditor::getSelection() const {
    if (!selected_) return {};
    return {EventEditorItem::valueOf(const_cast<EventEditor*>(this), selected_)};
}

void EventEditor::setSelection(const std::vector<EventEditorItemPtr>& items) {
    if (!items.empty()) select(items.back());
}

void EventEditor::select(const EventEditorItemPtr& item) {
    select(item ? item->content() : ContentPtr{});
}

void EventEditor::select(const ContentPtr& content) {
    if (content && !findContent(tree_, content->eventId())) return;
    const auto old = selected_;
    selected_ = content;
    selectedIndex_ = indexOf(selected_);
    if (treeView_) {
        std::vector<TreeItemId> selection;
        if (selected_ && contentIds_.contains(selected_.get()))
            selection.push_back(contentIds_.at(selected_.get()));
        (void)lib::select_tree_items(*treeView_, std::move(selection), true, false);
    }
    refreshStatusLine();
    if (old != selected_) callSelectChanged();
}

int EventEditor::indexOf(const EventEditorItemPtr& item) const {
    return indexOf(item ? item->content() : ContentPtr{});
}

int EventEditor::indexOf(const ContentPtr& content) const {
    if (!content) return -1;
    const auto found = positionTable_.find(content->eventId());
    return found == positionTable_.end() ? -1 : found->second->index;
}

EventEditorItemPtr EventEditor::getTopItem() const {
    if (positions_.empty()) return {};
    const auto index = std::min(topIndex_, positions_.size() - 1);
    return EventEditorItem::valueOf(const_cast<EventEditor*>(this), positions_[index]->content);
}

void EventEditor::setTopItem(const EventEditorItemPtr& item) {
    const auto index = indexOf(item);
    if (index >= 0) topIndex_ = static_cast<std::size_t>(index);
}

ContentPtr EventEditor::getContent(const int, const int y) const {
    if (y < 0 || positions_.empty()) return {};
    const auto index = topIndex_ + static_cast<std::size_t>(y / std::max(1, lineHeight_));
    if (index >= positions_.size()) return {};
    return positions_[index]->content;
}

void EventEditor::refreshStatusLine() {
    statusLine_ = selected_ ? contentText(selected_) : String{};
    if (status_) status_->text = statusLine_;
    statusChanged.call();
}

void EventEditor::addSelectionListener(SelectionListener listener) {
    if (listener) selectionChanged.add([this, listener = std::move(listener)] {
        listener(selected_);
    });
}

void EventEditor::addTreeListener(ExpansionListener listener) {
    if (listener) expansionChanged.add(std::move(listener));
}

void EventEditor::callSelectChanged() { selectionChanged.call(); }

void EventEditor::callExpandedChanged(const EventEditorItemPtr& item) {
    if (item && item->content()) expansionChanged.call(item->content(), item->getExpanded());
}

void EventEditor::nameVisible(const int index, const bool visible) {
    if (index < 0 || static_cast<std::size_t>(index) >= positions_.size()) return;
    const auto& pos = positions_[static_cast<std::size_t>(index)];
    pos->nameVisible = visible;
    if (!treeView_ || !pos->content) return;
    const auto foundId = contentIds_.find(pos->content.get());
    if (foundId == contentIds_.end()) return;
    const auto found = std::find_if(
        treeView_->nodes.begin(), treeView_->nodes.end(),
        [id = foundId->second](const lib::TreeNode& node) { return node.id == id; });
    if (found != treeView_->nodes.end()) (void)rebuildTreeNodes();
}

bool EventEditor::editEnd(const EventEditorItemPtr& item, String value) {
    if (readOnly_ || !item || !item->content()) return false;
    const auto content = item->content();
    if (content->name() == value) return false;
    const auto content_id = content->eventId();
    const auto before = content->name();
    const auto after = std::move(value);
    const auto apply = [this, content_id](const String& name) {
        const auto current = findContent(tree_, content_id);
        if (!current) return;
        // DのContent.setName(prop, name)は、親のvalidText()を再実行する。
        current->setName(comm_.modelProps, name);
        updateContentInfo(current);
        comm_.refUseCount.call();
        if (comm_.refreshToolBar) comm_.refreshToolBar();
    };
    apply(after);
    if (undo_) {
        undo_->push([apply, before] { apply(before); },
                    [apply, after] { apply(after); });
    }
    return true;
}

void EventEditor::updateLightup(const ContentPtr& content) {
    lightup_ = content;
}

void EventEditor::clearLightup() { lightup_.reset(); }

EventEditor::Snapshot EventEditor::snapshot() const {
    return {tree_ ? tree_->dup() : EventTreePtr{}, selected_ ? selected_->eventId() : String{}};
}

void EventEditor::restore(const Snapshot& value) {
    if (!tree_ || !value.tree) return;
    tree_->enter(value.tree->fireEnter());
    tree_->escape(value.tree->fireEscape());
    tree_->lose(value.tree->fireLose());
    tree_->everyRound(value.tree->fireEveryRound());
    tree_->roundEnd(value.tree->fireRoundEnd());
    tree_->round0(value.tree->fireRound0());
    tree_->rounds(value.tree->rounds());
    tree_->keyCodes(value.tree->keyCodes());
    tree_->keyCodeMatchingType(value.tree->keyCodeMatchingType());
    tree_->name(value.tree->name());
    tree_->comment(value.tree->comment());
    const auto copy = value.tree->dup();
    tree_->starts(copy->starts());
    selected_ = findContent(tree_, value.selectedId);
    updatePosAll();
}

bool EventEditor::mutate(const std::function<bool()>& action,
                         const ContentPtr& changed) {
    if (readOnly_ || !tree_ || !action) return false;
    const auto before = snapshot();
    if (!action()) return false;
    const auto after = snapshot();
    if (undo_) {
        undo_->push([this, before] { restore(before); },
                    [this, after] { restore(after); });
    }
    notify(changed);
    return true;
}

void EventEditor::notify(const ContentPtr& changed) {
    updatePosAll();
    modelChanged.call();
    if (changed) contentChanged.call(changed);
    comm_.refUseCount.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool EventEditor::clipboardAvailable() const noexcept {
    try {
        return parseClipboard(readClipboard().text).has_value();
    } catch (...) {
        return false;
    }
}

void EventEditor::writeClipboard(const lib::ClipboardData& data) {
    clipboardMemory_ = data;
    hasClipboardMemory_ = !data.text.empty();
    if (const auto clipboard = comm_.clipboard()) clipboard->write(data);
}

lib::ClipboardData EventEditor::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) {
        auto value = clipboard->read();
        if (!value.text.empty() || !value.formats.empty()) return value;
    }
    return clipboardMemory_;
}

String EventEditor::itemToXML(const ContentPtr& content) const {
    if (!content) return {};
    EventTreePtr wrapper;
    if (content->type() == types::CType::Start) {
        wrapper = std::make_shared<EventTree>(content->dup());
    } else {
        auto start = std::make_shared<Content>(types::CType::Start, "Clipboard");
        start->add(content->dup());
        wrapper = std::make_shared<EventTree>(start);
    }
    auto root = xml::XNode::create(kClipboardRoot);
    root.newAttr("kind", content->type() == types::CType::Start ? "start" : "content");
    root.newAttr("paneId", id_);
    root.append(wrapper->toNode());
    return root.text();
}

void EventEditor::appendClipboardFormats(lib::ClipboardData& data) {
    if (data.text.empty()) return;
    std::vector<std::byte> bytes;
    bytes.reserve(data.text.size());
    for (const auto character : data.text)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    data.formats.emplace(String(kClipboardMime), std::move(bytes));
}

lib::ClipboardData EventEditor::clipboardData() const {
    lib::ClipboardData result;
    result.text = itemToXML(selected_);
    appendClipboardFormats(result);
    return result;
}

bool EventEditor::canPaste() const noexcept { return !readOnly_ && clipboardAvailable(); }

bool EventEditor::canCut() const noexcept {
    if (readOnly_ || !selected_ || !tree_) return false;
    return selected_->parent() || tree_->starts().size() > 1;
}

bool EventEditor::copy() {
    auto data = clipboardData();
    if (data.text.empty()) return false;
    writeClipboard(data);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool EventEditor::cut() {
    if (!canCut() || !copy()) return false;
    const auto content = selected_;
    return mutate([this, content] {
        if (const auto parent = content->parent()) {
            parent->remove(content);
            return true;
        }
        const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), content);
        if (it == tree_->starts().end()) return false;
        tree_->remove(static_cast<std::size_t>(std::distance(tree_->starts().begin(), it)));
        return true;
    });
}

std::optional<EventEditor::ClipboardPayload> EventEditor::parseClipboard(
    const StringView value) const {
    if (value.empty()) return std::nullopt;
    try {
        auto root = xml::XNode::parse(value);
        if (root.name() == "Event") return ClipboardPayload{"event", root};
        if (root.name() != kClipboardRoot) return std::nullopt;
        const auto kind = root.attr("kind", false, "content");
        auto node = root.child("Event", false);
        if (!node) return std::nullopt;
        return ClipboardPayload{kind, node};
    } catch (...) {
        return std::nullopt;
    }
}

bool EventEditor::pastePayload(const ClipboardPayload& payload) {
    auto value = EventTree::createFromNode(payload.node);
    return value ? pasteContent(value) : false;
}

bool EventEditor::pasteContent(const EventTreePtr& value) {
    if (readOnly_ || !tree_ || !value || value->starts().empty()) return false;
    const auto source = value->starts().front();
    if (!source) return false;
    auto target = selected_;
    if (!target) return false;
    if (!target->detail().owner) target = target->parent();
    if (!target || !target->detail().owner) return false;
    ContentPtr inserted;
    return mutate([target, source, &inserted, model = comm_.modelProps] {
        for (const auto& child : source->next()) {
            if (!child) continue;
            inserted = child->dup();
            // DのContent.add(prop, ...)は、貼り付けた子にvalidText()を実行する。
            target->add(model, inserted);
            break;
        }
        return inserted != nullptr;
    }, target) && (select(inserted), true);
}

bool EventEditor::paste() {
    if (!canPaste()) return false;
    const auto payload = parseClipboard(readClipboard().text);
    return payload && pastePayload(*payload);
}

bool EventEditor::pasteData(const lib::ClipboardData& data) {
    if (readOnly_) return false;
    const auto payload = parseClipboard(data.text);
    return payload && pastePayload(*payload);
}

void EventEditor::beginDrag() {
    dragSourceId_ = selected_ && contentIds_.contains(selected_.get())
                        ? contentIds_.at(selected_.get())
                        : 0;
}

lib::ClipboardData EventEditor::dragData() const { return clipboardData(); }

bool EventEditor::drop(lib::DragEvent& event) {
    if (readOnly_ || event.data.text.empty()) return false;
    const auto sourceFound = nodeContents_.find(dragSourceId_);
    const auto source = sourceFound == nodeContents_.end() ? ContentPtr{} : sourceFound->second;
    if (source && selected_ && contains(source, selected_)) return false;
    if (!pasteData(event.data)) return false;
    if (source && hasOperation(event.selected, lib::DropOperation::Move)) {
        if (const auto parent = source->parent()) parent->remove(source);
        else if (tree_ && tree_->starts().size() > 1) tree_->remove(source);
        updatePosAll();
    }
    event.handled = true;
    return true;
}

lib::DragSourceBinding EventEditor::dragSourceBinding() {
    lib::DragSourceBinding binding;
    binding.source = treeView_;
    binding.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    binding.provide_data = [this] { beginDrag(); return dragData(); };
    binding.completed = [this](const lib::DropOperation) { dragSourceId_ = 0; };
    return binding;
}

lib::DropTargetBinding EventEditor::dropTargetBinding() {
    lib::DropTargetBinding binding;
    binding.target = treeView_;
    binding.accepted_formats = {String(kClipboardMime), "text/xml"};
    binding.on_enter = [this](lib::DragEvent& event) {
        if (!readOnly_) event.selected = event.allowed;
    };
    binding.on_over = [this](lib::DragEvent& event) {
        if (readOnly_) event.selected = lib::DropOperation::None;
    };
    binding.on_drop = [this](lib::DragEvent& event) { drop(event); };
    return binding;
}

void EventEditor::bindDragDrop() {
    if (const auto backend = comm_.services.drag_drop; backend && treeView_) {
        backend->bind_source(dragSourceBinding());
        backend->bind_target(dropTargetBinding());
    }
}

void EventEditor::unbindDragDrop() {
    if (const auto backend = comm_.services.drag_drop; backend && treeView_)
        backend->unbind(treeView_);
}

bool EventEditor::canUndo() const noexcept { return undo_ && undo_->canUndo(); }
bool EventEditor::canRedo() const noexcept { return undo_ && undo_->canRedo(); }

void EventEditor::undo() {
    if (undo_ && undo_->canUndo()) undo_->undo();
    updatePosAll();
}

void EventEditor::redo() {
    if (undo_ && undo_->canRedo()) undo_->redo();
    updatePosAll();
}

WidgetPtr TreeViewWrapper::control() const {
    if (tree) return tree;
    return editor ? editor->control() : WidgetPtr{};
}

TreeViewWrapper::Item TreeViewWrapper::getTopItem() const {
    if (editor) return editor->getTopItem();
    return tree && !tree->nodes.empty() ? Item{tree->nodes.front().id} : Item{};
}

void TreeViewWrapper::setTopItem(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item)) editor->setTopItem(*value);
    }
}

std::vector<TreeViewWrapper::Item> TreeViewWrapper::getSelection() const {
    if (editor) {
        std::vector<Item> result;
        for (const auto& item : editor->getSelection()) result.emplace_back(item);
        return result;
    }
    std::vector<Item> result;
    if (tree) for (const auto id : tree->selection) result.emplace_back(id);
    return result;
}

void TreeViewWrapper::setSelection(const std::vector<Item>& items) const {
    if (editor) {
        for (const auto& item : items)
            if (const auto* value = std::get_if<EventEditorItemPtr>(&item)) editor->select(*value);
        return;
    }
    if (!tree) return;
    std::vector<TreeItemId> selection;
    for (const auto& item : items) {
        if (const auto* id = std::get_if<TreeItemId>(&item)) selection.push_back(*id);
    }
    (void)lib::select_tree_items(*tree, std::move(selection), true, false);
}

void TreeViewWrapper::select(const Item& item) const {
    setSelection({item});
}

TreeViewWrapper::Item TreeViewWrapper::getItem(const int index) const {
    if (editor) return editor->getItem(index);
    if (!tree || index < 0) return {};
    std::vector<TreeItemId> roots;
    for (const auto& node : tree->nodes) if (!node.parent) roots.push_back(node.id);
    return static_cast<std::size_t>(index) < roots.size() ? Item{roots[index]} : Item{};
}

TreeViewWrapper::Item TreeViewWrapper::getItem(const lib::Point& point) const {
    return editor ? Item{editor->getItem(point)} : getItem(static_cast<int>(point.y));
}

int TreeViewWrapper::getItemCount() const {
    if (editor) return editor->getItemCount();
    if (!tree) return 0;
    return static_cast<int>(std::count_if(tree->nodes.begin(), tree->nodes.end(),
                                          [](const lib::TreeNode& node) { return !node.parent; }));
}

int TreeViewWrapper::getItemCount(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value ? (*value)->getItemCount() : 0;
        return 0;
    }
    if (!tree) return 0;
    const auto id = treeItemId(item);
    return static_cast<int>(std::count_if(tree->nodes.begin(), tree->nodes.end(),
                                          [id](const lib::TreeNode& node) {
                                              return node.parent == id;
                                          }));
}

int TreeViewWrapper::getItemCount(const TreeViewWrapper& view) const {
    return view.getItemCount();
}

void TreeViewWrapper::showSelection() const {
    if (editor) editor->showSelection();
}

std::vector<TreeViewWrapper::Item> TreeViewWrapper::getItems() const {
    std::vector<Item> result;
    for (int index = 0; index < getItemCount(); ++index) result.push_back(getItem(index));
    return result;
}

std::vector<TreeViewWrapper::Item> TreeViewWrapper::getItems(const Item& item) const {
    std::vector<Item> result;
    for (int index = 0; index < getItemCount(item); ++index) result.push_back(getItem(item, index));
    return result;
}

TreeViewWrapper::Item TreeViewWrapper::getItem(const Item& item, const int index) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value ? Item{(*value)->getItem(index)} : Item{};
        return {};
    }
    if (!tree || index < 0) return {};
    const auto id = treeItemId(item);
    std::vector<TreeItemId> children;
    for (const auto& node : tree->nodes) if (node.parent == id) children.push_back(node.id);
    return static_cast<std::size_t>(index) < children.size() ? Item{children[index]} : Item{};
}

bool TreeViewWrapper::getExpanded(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value && (*value)->getExpanded();
        return false;
    }
    if (!tree) return false;
    const auto id = treeItemId(item);
    const auto found = std::find_if(tree->nodes.begin(), tree->nodes.end(),
                                    [id](const lib::TreeNode& node) { return node.id == id; });
    return found != tree->nodes.end() && found->expanded;
}

void TreeViewWrapper::setExpanded(const Item& item, const bool expanded) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            if (*value) (*value)->setExpanded(expanded);
        return;
    }
    if (!tree) return;
    const auto id = treeItemId(item);
    (void)lib::set_tree_expanded(*tree, id, expanded, false, false);
}

TreeViewWrapper::Item TreeViewWrapper::getParentItem(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value ? Item{(*value)->getParentItem()} : Item{};
        return {};
    }
    if (!tree) return {};
    const auto id = treeItemId(item);
    const auto found = std::find_if(tree->nodes.begin(), tree->nodes.end(),
                                    [id](const lib::TreeNode& node) { return node.id == id; });
    return found != tree->nodes.end() && found->parent ? Item{found->parent} : Item{};
}

TreeViewWrapper::Item TreeViewWrapper::getItem(const TreeViewWrapper& view,
                                               const int index) const {
    return view.getItem(index);
}

int TreeViewWrapper::indexOf(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value ? editor->indexOf(*value) : -1;
        return -1;
    }
    if (!tree) return -1;
    const auto id = treeItemId(item);
    std::vector<TreeItemId> roots;
    for (const auto& node : tree->nodes) if (!node.parent) roots.push_back(node.id);
    const auto found = std::find(roots.begin(), roots.end(), id);
    return found == roots.end() ? -1 : static_cast<int>(std::distance(roots.begin(), found));
}

int TreeViewWrapper::indexOf(const Item& item, const Item& child) const {
    const auto parentId = treeItemId(item);
    const auto childId = treeItemId(child);
    if (tree && parentId && childId) {
        std::vector<TreeItemId> children;
        for (const auto& node : tree->nodes) if (node.parent == parentId) children.push_back(node.id);
        const auto found = std::find(children.begin(), children.end(), childId);
        return found == children.end() ? -1 : static_cast<int>(std::distance(children.begin(), found));
    }
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            if (const auto* childValue = std::get_if<EventEditorItemPtr>(&child))
                return *value ? (*value)->indexOf(*childValue) : -1;
    }
    return -1;
}

TreeViewWrapper::Item TreeViewWrapper::topItem(const Item& item) const {
    if (editor) {
        auto current = std::get_if<EventEditorItemPtr>(&item);
        if (!current || !*current) return {};
        auto parent = (*current)->getParentItem();
        while (parent && parent->content() && parent->content()->type() != types::CType::Start)
            parent = parent->getParentItem();
        return parent ? Item{parent} : item;
    }
    if (!tree) return {};
    Item current = item;
    while (true) {
        const auto parent = getParentItem(current);
        if (std::holds_alternative<std::monostate>(parent)) return current;
        current = parent;
    }
}

void TreeViewWrapper::treeExpandedAll() const {
    if (editor) {
        editor->expandAll();
        return;
    }
    if (!tree) return;
    std::vector<lib::TreeItemId> collapsed;
    collapsed.reserve(tree->nodes.size());
    for (const auto& node : tree->nodes)
        if (!node.expanded) collapsed.push_back(node.id);
    for (const auto id : collapsed)
        (void)lib::set_tree_expanded(*tree, id, true, false, false);
}

lib::Rect TreeViewWrapper::getImageBounds(const Item& item) const {
    if (editor) {
        if (const auto* value = std::get_if<EventEditorItemPtr>(&item))
            return *value ? (*value)->getImageBounds() : lib::Rect{};
    }
    return {};
}

EventEdit::EventEdit(Commons comm, EventEditor& list, EditEnd editEnd,
                     CreateEditor createEditor)
    : EventEdit(std::move(comm), &list, std::move(editEnd), std::move(createEditor)) {}

EventEdit::EventEdit(Commons comm, EventEditor* list, EditEnd editEnd,
                     CreateEditor createEditor)
    : comm_(std::move(comm)), list_(list), editEnd_(std::move(editEnd)),
      createEditor_(std::move(createEditor)) {}

EventEditorItemPtr EventEdit::selectionM(const int x, const int y) const {
    if (!list_) return {};
    const auto content = list_->getContent(x, y);
    if (!content) return {};
    const auto* pos = list_->position(content);
    if (!pos || x < list_->calcX(*pos) + 20) return {};
    return EventEditorItem::valueOf(list_, content);
}

EventEditorItemPtr EventEdit::selectionK() const {
    if (!list_) return {};
    const auto selection = list_->getSelection();
    return selection.empty() ? EventEditorItemPtr{} : selection.front();
}

void EventEdit::end(const ControlPtr& control) {
    if (!edit_) return;
    if (editEnd_) {
        editEnd_(edit_, control);
    } else if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(control)) {
        (void)list_->editEnd(edit_, text->text);
    }
    clearEdit(false);
}

void EventEdit::clearEdit(const bool) {
    if (list_) {
        const auto index = list_->indexOf(edit_);
        if (index >= 0) list_->nameVisible(index, true);
    }
    editor_.reset();
    edit_.reset();
    oldName_.clear();
}

void EventEdit::startEdit(const EventEditorItemPtr& item) {
    if (!list_ || !item || !item->content() || list_->readOnly()) return;
    if (isEditing()) enter();
    oldName_ = item->content()->name();
    editor_ = createEditor_ ? createEditor_(item) : std::make_shared<lib::TextEdit>(oldName_);
    if (!editor_) return;
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(editor_)) {
        text->text = oldName_;
        text->read_only = list_->readOnly();
        text->on_enter = [this] { enter(); };
    }
    edit_ = item;
    list_->select(item);
    layout();
    const auto index = list_->indexOf(item);
    if (index >= 0) list_->nameVisible(index, false);
}

void EventEdit::layout() {
    if (!list_ || !edit_ || !editor_) return;
    const auto* pos = list_->position(edit_->content());
    if (!pos) return;
    const auto x = list_->calcX(*pos) + 20;
    const auto width = std::max(minimumWidth_,
                                static_cast<int>(pos->eventText.size() * 8U) + 20);
    editor_->set_bounds({static_cast<float>(x),
                         static_cast<float>(pos->relY) -
                             static_cast<float>(list_->topIndex_ * list_->lineHeight_),
                         static_cast<float>(width),
                         static_cast<float>(list_->lineHeight_)});
}

void EventEdit::startEdit() { startEdit(selectionK()); }

void EventEdit::cancel() { if (isEditing()) clearEdit(true); }

void EventEdit::enter() { if (isEditing()) end(editor_); }

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventeditor
