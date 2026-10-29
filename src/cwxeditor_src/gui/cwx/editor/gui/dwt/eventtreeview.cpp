#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventtreeview.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace cwxeditor::cwx::editor::gui::dwt::eventtreeview {
namespace {

constexpr StringView kClipboardRoot = "CWXEditorClipboard";
constexpr StringView kClipboardMime = "application/x-cardwirth-event+xml";

EventTreePtr borrowTree(EventTree& value) {
    try {
        return value.shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return EventTreePtr(&value, [](EventTree*) {});
    }
}

EventTreePtr firstTree(EventTreeOwner* owner) {
    if (!owner || owner->trees().empty()) return {};
    return owner->trees().front();
}

bool hasOperation(const lib::DropOperation value, const lib::DropOperation bit) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(bit)) != 0;
}

std::string lowerCopy(StringView value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::vector<std::size_t> numericPath(StringView value) {
    std::vector<std::size_t> result;
    std::string part;
    std::stringstream stream{std::string(value)};
    while (std::getline(stream, part, '/')) {
        if (part.empty()) continue;
        const auto colon = part.rfind(':');
        if (colon != std::string::npos) part = part.substr(colon + 1);
        const auto attribute = part.find(';');
        if (attribute != std::string::npos) part = part.substr(0, attribute);
        try {
            result.push_back(static_cast<std::size_t>(std::stoull(part)));
        } catch (...) {
            return {};
        }
    }
    return result;
}

String uniqueStartName(const EventTree& tree, String value) {
    if (value.empty()) value = "Start";
    if (!tree.hasStart(value)) return value;
    const auto base = value;
    for (std::size_t index = 2;; ++index) {
        value = base + " (" + std::to_string(index) + ")";
        if (!tree.hasStart(value)) return value;
    }
}

void appendClipboardFormats(lib::ClipboardData& result) {
    if (result.text.empty()) return;
    std::vector<std::byte> bytes;
    bytes.reserve(result.text.size());
    for (const auto character : result.text)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    result.formats.emplace(String(kClipboardMime), std::move(bytes));
}

void adjustText(const flagspane::Props& prop, const ContentPtr& owner,
                const ContentPtr& content,
                const types::CNextType lastNextType) {
    if (!prop.etc.adjustContentName || !owner || !content) return;
    const auto textLike = [](const types::CNextType value) {
        return value == types::CNextType::Text ||
               value == types::CNextType::Coupon;
    };
    const auto ownerType = owner->detail().nextType;
    if (ownerType != lastNextType &&
        !(textLike(ownerType) && textLike(lastNextType))) {
        content->setName({});
        return;
    }
    if (textLike(ownerType)) return;
    for (const auto& sibling : owner->next()) {
        if (sibling && sibling != content && sibling->name() == content->name()) {
            content->setName({});
            return;
        }
    }
}

[[nodiscard]] ContentPtr findContentById(const EventTreePtr& tree,
                                         const StringView id) {
    if (!tree || id.empty()) return {};
    std::vector<ContentPtr> pending(tree->starts().begin(), tree->starts().end());
    while (!pending.empty()) {
        auto value = pending.back();
        pending.pop_back();
        if (!value) continue;
        if (value->eventId() == id) return value;
        for (const auto& child : value->next()) pending.push_back(child);
    }
    return {};
}

}  // namespace

EventTreeView::EventTreeView(Commons comm, Props prop, EventTreePtr tree,
                             UndoManager& undo, const bool readOnly)
    : comm_(std::move(comm)), prop_(std::move(prop)), tree_(std::move(tree)),
      undo_(&undo), readOnly_(readOnly), id_("eventtreeview") {
    if (!tree_) tree_ = std::make_shared<EventTree>();
    if (comm_.useCounter) tree_->setUseCounter(comm_.useCounter);
}

EventTreeView::EventTreeView(Commons comm, Props prop, EventTree& tree,
                             UndoManager& undo, const bool readOnly)
    : EventTreeView(std::move(comm), std::move(prop), borrowTree(tree), undo, readOnly) {}

EventTreeView::EventTreeView(Commons comm, Props prop, EventTreeOwner& owner,
                             UndoManager& undo, const bool readOnly)
    : EventTreeView(std::move(comm), std::move(prop), firstTree(&owner), undo, readOnly) {
    owner_ = &owner;
    if (!tree_) {
        tree_ = std::make_shared<EventTree>();
        owner_->add(tree_);
    }
}

EventTreeView::EventTreeView(Commons comm, Props prop, EventTreeOwner* owner,
                             UndoManager& undo, const bool readOnly)
    : EventTreeView(std::move(comm), std::move(prop), firstTree(owner), undo, readOnly) {
    owner_ = owner;
    if (owner_ && !tree_) {
        tree_ = std::make_shared<EventTree>();
        owner_->add(tree_);
    }
}

EventTreeView::~EventTreeView() {
    unbindDragDrop();
    if (box_) box_->dispose();
    if (treeView_) {
        treeView_->on_selection_changed = {};
        treeView_->on_activate = {};
        treeView_->on_item_edited = {};
        treeView_->on_expansion_changed = {};
    }
}

void EventTreeView::ensureWidgets(const Parent& parent) {
    if (!panel_) panel_ = std::make_shared<lib::Panel>();
    if (!treeView_) {
        treeView_ = std::make_shared<lib::TreeView>();
        treeView_->accessibility_name = "Event content tree";
        treeView_->on_selection_changed = [this](const std::vector<TreeItemId>& ids) {
            const auto old = selectedId_;
            selectedId_ = ids.empty() ? 0 : ids.front();
            selectedContent_.reset();
            if (const auto* info = nodeInfo(selectedId_)) selectedContent_ = info->content;
            refreshStatusLine();
            if (old != selectedId_) selectionChanged.call();
            if (comm_.refreshToolBar) comm_.refreshToolBar();
        };
        treeView_->on_activate = [this](const TreeItemId id) {
            if (canEdit()) editEnd(id, nodeInfo(id) && nodeInfo(id)->content
                                         ? nodeInfo(id)->content->name()
                                         : String{});
        };
        treeView_->on_item_edited = [this](const TreeItemId id, String value) {
            editEnd(id, std::move(value));
        };
        treeView_->on_expansion_changed = [this](const TreeItemId id, const bool expanded) {
            if (auto node = std::find_if(treeView_->nodes.begin(), treeView_->nodes.end(),
                                         [id](const lib::TreeNode& value) { return value.id == id; });
                node != treeView_->nodes.end()) node->expanded = expanded;
        };
        panel_->add(treeView_);
        status_ = std::make_shared<lib::StatusBar>();
        panel_->add(status_);
    }
    if (parent) parent->add(panel_);
}

PanelPtr EventTreeView::createControl(const Parent& parent) {
    ensureWidgets(parent);
    if (!initialized_) {
        initialized_ = true;
        bindDragDrop();
        getContentsBox();
    }
    refresh();
    return panel_;
}

void EventTreeView::setEventTree(EventTreePtr tree) {
    if (tree == tree_) return;
    expanded_by_event_id_.clear();
    tree_ = std::move(tree);
    if (!tree_) tree_ = std::make_shared<EventTree>();
    if (owner_) {
        const auto& trees = owner_->trees();
        if (std::find(trees.begin(), trees.end(), tree_) == trees.end()) owner_->add(tree_);
    }
    if (comm_.useCounter) tree_->setUseCounter(comm_.useCounter);
    refresh();
    eventTreeChanged.call(tree_);
}

String EventTreeView::contentText(const ContentPtr& content) const {
    if (!content) return {};
    const auto detail = content->detail();
    String result = detail.type;
    if (result.empty() && !detail.names.empty()) result = detail.names.front();
    if (!content->name().empty()) {
        result += ": ";
        result += content->name();
    }
    if (showEventTreeDetail_ && !content->comment().empty()) {
        result += "  — ";
        result += content->comment();
    }
    return result;
}

String EventTreeView::eventText(const ContentPtr& parent,
                                const ContentPtr& content) const {
    if (!content) return {};
    if (!parent) return contentText(content);
    return contentText(content);
}

TreeItemId EventTreeView::appendNode(
    const ContentPtr& content, const ContentPtr& parent, const TreeItemId parentId,
    const std::size_t index, std::unordered_set<const Content*>& visiting,
    TreeBuildState& build) {
    if (!content || visiting.contains(content.get())) return 0;
    const auto id = build.nextId++;
    build.nodes.emplace(id, NodeInfo{NodeInfo::Kind::Content, id, parentId, content,
                                     parent, index, content->type() == types::CType::Start});
    build.contentIds[content.get()] = id;
    bool expanded = true;
    if (const auto found = expanded_by_event_id_.find(content->eventId());
        found != expanded_by_event_id_.end())
        expanded = found->second;
    lib::TreeNode node{id, parentId, eventText(parent, content),
                       content->eventId(), 0, expanded, false, false};
    if (comm_.warnings) node.warnings = comm_.warnings(*content);
    if (!node.warnings.empty()) node.warning_icon = comm_.warningImage;
    build.viewNodes.push_back(std::move(node));
    visiting.insert(content.get());
    if (content->detail().owner) {
        for (std::size_t i = 0; i < content->next().size(); ++i)
            appendNode(content->next()[i], content, id, i, visiting, build);
    }
    visiting.erase(content.get());
    return id;
}

void EventTreeView::rebuildTree() {
    const auto preferred = selectedContent_;
    const auto source_tree = tree_;
    if (treeView_) {
        for (const auto& node : treeView_->nodes) {
            const auto found = nodes_.find(node.id);
            if (found != nodes_.end() && found->second.content &&
                !found->second.content->eventId().empty())
                expanded_by_event_id_[found->second.content->eventId()] =
                    node.expanded;
        }
    }
    TreeBuildState build;
    if (tree_) {
        std::unordered_set<const Content*> visiting;
        for (std::size_t i = 0; i < tree_->starts().size(); ++i)
            appendNode(tree_->starts()[i], {}, 0, i, visiting, build);
    }
    TreeItemId selected_id{};
    ContentPtr selected_content;
    if (preferred) {
        if (const auto found = build.contentIds.find(preferred.get());
            found != build.contentIds.end()) {
            selected_id = found->second;
            selected_content = preferred;
        }
    }
    if (!selected_id && !build.viewNodes.empty()) {
        selected_id = build.viewNodes.front().id;
        selected_content = build.nodes.at(selected_id).content;
    }
    const auto selection = selected_id ? std::vector<TreeItemId>{selected_id}
                                       : std::vector<TreeItemId>{};
    if (treeView_) {
        const auto order_revision = treeView_->node_order_revision;
        const std::weak_ptr<lib::TreeView> weak = treeView_;
        if (!lib::replace_tree_nodes(treeView_, std::move(build.viewNodes), selection, {},
                [this, weak, source_tree, order_revision] {
                    const auto target = weak.lock();
                    return target && tree_ == source_tree &&
                           target->node_order_revision == order_revision;
                })) return;
    }
    nodes_ = std::move(build.nodes);
    contentIds_ = std::move(build.contentIds);
    nextId_ = build.nextId;
    selectedId_ = selected_id;
    selectedContent_ = std::move(selected_content);
    std::unordered_set<String> live_event_ids;
    for (const auto& [id, info] : nodes_) {
        static_cast<void>(id);
        if (info.content && !info.content->eventId().empty())
            live_event_ids.insert(info.content->eventId());
    }
    std::erase_if(expanded_by_event_id_, [&live_event_ids](const auto& value) {
        return !live_event_ids.contains(value.first);
    });
}

void EventTreeView::restoreSelection(const ContentPtr& preferred) {
    selectedContent_.reset();
    selectedId_ = 0;
    if (preferred) {
        if (const auto found = contentIds_.find(preferred.get()); found != contentIds_.end()) {
            selectedId_ = found->second;
            selectedContent_ = preferred;
        }
    }
    if (!selectedId_ && treeView_ && !treeView_->nodes.empty()) {
        selectedId_ = treeView_->nodes.front().id;
        selectedContent_ = nodes_.at(selectedId_).content;
    }
    if (treeView_)
        (void)lib::select_tree_items(*treeView_,
            selectedId_ ? std::vector<TreeItemId>{selectedId_}
                        : std::vector<TreeItemId>{}, true, false);
}

void EventTreeView::refresh(const EventTreePtr& value) {
    if (value) tree_ = value;
    rebuildTree();
    refreshStatusLine();
}

void EventTreeView::refreshTreeName() { refresh(); }

const NodeInfo* EventTreeView::nodeInfo(const TreeItemId id) const noexcept {
    const auto found = nodes_.find(id);
    return found == nodes_.end() ? nullptr : &found->second;
}

bool EventTreeView::select(const TreeItemId id) {
    const auto* info = nodeInfo(id);
    if (!info) return false;
    const auto old = selectedId_;
    selectedId_ = id;
    selectedContent_ = info->content;
    if (treeView_) (void)lib::select_tree_items(*treeView_, {id}, true, false);
    refreshStatusLine();
    if (old != id) selectionChanged.call();
    return true;
}

bool EventTreeView::select(const ContentPtr& content) {
    if (!content) return false;
    const auto found = contentIds_.find(content.get());
    return found != contentIds_.end() && select(found->second);
}

std::vector<std::size_t> EventTreeView::selectionPath() const {
    // ctPathは値を返す。二回の呼び出しのイテレーターは、別々の配列に属する。
    return selectedContent_ ? selectedContent_->ctPath() : std::vector<std::size_t>{};
}

TreeItemId EventTreeView::fromPath(const std::vector<std::size_t>& value) const {
    if (!tree_ || value.empty()) return 0;
    const auto content = tree_->fromPath(lib::Array<std::size_t>(value.begin(), value.end()));
    if (!content) return 0;
    const auto found = contentIds_.find(content.get());
    return found == contentIds_.end() ? 0 : found->second;
}

TreeItemId EventTreeView::fromPath(const StringView value) const {
    return fromPath(numericPath(value));
}

std::vector<TreeItemId> EventTreeView::search(const StringView value) {
    searchText_ = String(value);
    searchResults_.clear();
    searchCursor_ = 0;
    const auto fold = [this](const StringView text) {
        return comm_.services.text ? comm_.services.text->case_fold(text)
                                   : lowerCopy(text);
    };
    const auto query = fold(value);
    for (const auto& [id, info] : nodes_) {
        if (fold(contentText(info.content)).find(query) != std::string::npos)
            searchResults_.push_back(id);
    }
    std::sort(searchResults_.begin(), searchResults_.end());
    if (!searchResults_.empty()) select(searchResults_.front());
    return searchResults_;
}

bool EventTreeView::searchNext() {
    if (searchResults_.empty()) return false;
    searchCursor_ = (searchCursor_ + 1) % searchResults_.size();
    return select(searchResults_[searchCursor_]);
}

void EventTreeView::clearSearch() noexcept {
    searchText_.clear();
    searchResults_.clear();
    searchCursor_ = 0;
}

void EventTreeView::statusLine(String value) {
    statusLine_ = std::move(value);
    if (status_) status_->text = statusLine_;
    statusChanged.call();
}

void EventTreeView::refreshStatusLine() {
    if (selectedContent_) {
        auto value = contentText(selectedContent_);
        if (!selectedContent_->comment().empty()) {
            value += " — ";
            value += selectedContent_->comment();
        }
        statusLine(std::move(value));
    } else {
        statusLine({});
    }
}

ContentPtr EventTreeView::selectedContentOr(const ContentPtr& value) const noexcept {
    return value ? value : selectedContent_;
}

bool EventTreeView::contains(const ContentPtr& root, const ContentPtr& value) const {
    if (!root || !value) return false;
    if (root == value) return true;
    for (const auto& child : root->next()) if (contains(child, value)) return true;
    return false;
}

bool EventTreeView::canEdit() const noexcept {
    return !readOnly_ && tree_ && selectedContent_;
}

bool EventTreeView::editEnd(const TreeItemId id, String value) {
    if (readOnly_) return false;
    const auto* info = nodeInfo(id);
    if (!info || !info->content) return false;
    const auto content = info->content;
    if (content->name() == value) return false;
    const auto content_id = content->eventId();
    const auto before = content->name();
    const auto after = std::move(value);
    const auto apply = [this, content_id](const String& name) {
        const auto current = findContentById(tree_, content_id);
        if (!current) return;
        // DのContent.setName(prop, name)は、親のvalidText()を再実行する。
        current->setName(comm_.modelProps, name);
        notify(tree_, current, true);
    };
    apply(after);
    if (undo_) {
        undo_->push([apply, before] { apply(before); },
                    [apply, after] { apply(after); });
    }
    return true;
}

bool EventTreeView::canCreate(const types::CType type,
                              ContentPtr parent) const noexcept {
    if (readOnly_ || !tree_) return false;
    if (type == types::CType::Start) return !parent;
    const auto target = parent ? std::move(parent) : selectedContent_;
    return target && target->detail().owner;
}

ContentPtr EventTreeView::create(const types::CType type, ContentPtr parent, String name) {
    if (!canCreate(type, parent)) return {};
    if (name.empty()) {
        if (type == types::CType::Start) name = "Start";
        else name = contentText(std::make_shared<Content>(type));
    }
    auto value = std::make_shared<Content>(type, std::move(name));
    if (type == types::CType::Start) {
        value->setName(uniqueStartName(*tree_, value->name()));
        const auto index = selectedContent_ && selectedContent_->type() == types::CType::Start
                               ? std::distance(tree_->starts().begin(),
                                               std::find(tree_->starts().begin(), tree_->starts().end(),
                                                         selectedContent_)) + 1
                               : static_cast<std::ptrdiff_t>(tree_->starts().size());
        if (!mutate([this, value, index] {
                tree_->insert(index < 0 ? tree_->starts().size() : static_cast<std::size_t>(index), value);
                return true;
            }, value)) return {};
    } else if (!addContent(value, parent)) {
        return {};
    }
    select(value);
    return value;
}

ContentPtr EventTreeView::insertionOwner(const ContentPtr& value,
                                         const bool tryInsert) const {
    auto owner = value ? value : selectedContent_;
    if (!owner) return {};
    if (tryInsert && owner->detail().owner) return owner;
    if (owner->detail().owner) return owner;
    return owner->parent();
}

bool EventTreeView::insertContent(const std::size_t index, const ContentPtr& content,
                                  const ContentPtr& parent) {
    if (readOnly_ || !tree_ || !content) return false;
    if (content->type() == types::CType::Start) {
        if (parent || content->isDescendant(content)) return false;
        const auto actual = std::min(index, tree_->starts().size());
        return mutate([this, content, actual] { tree_->insert(actual, content); return true; }, content);
    }
    const auto target = insertionOwner(parent, true);
    if (!target || !target->detail().owner || target == content || contains(content, target)) return false;
    const auto actual = std::min(index, target->next().size());
    return mutate([this, target, content, actual] {
        if (actual >= target->next().size()) target->add(comm_.modelProps, content);
        else target->insert(comm_.modelProps, actual, content);
        return true;
    }, content);
}

bool EventTreeView::addContent(const ContentPtr& content, const ContentPtr& parent) {
    return insertContent(std::numeric_limits<std::size_t>::max(), content,
                         parent ? parent : selectedContent_);
}

bool EventTreeView::deleteContent(const ContentPtr& value) {
    if (readOnly_ || !tree_) return false;
    const auto content = selectedContentOr(value);
    if (!content) return false;
    if (const auto parent = content->parent()) {
        return mutate([parent, content] { parent->remove(content); return true; }, parent);
    }
    const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), content);
    if (it == tree_->starts().end() || tree_->starts().size() <= 1) return false;
    const auto index = static_cast<std::size_t>(std::distance(tree_->starts().begin(), it));
    return mutate([this, index] { tree_->remove(index); return true; });
}

bool EventTreeView::moveContent(const ContentPtr& value, const int delta) {
    if (readOnly_ || !tree_ || !value || delta == 0) return false;
    if (const auto parent = value->parent()) {
        const auto it = std::find(parent->next().begin(), parent->next().end(), value);
        if (it == parent->next().end()) return false;
        const auto from = static_cast<std::ptrdiff_t>(std::distance(parent->next().begin(), it));
        const auto to = from + delta;
        if (to < 0 || static_cast<std::size_t>(to) >= parent->next().size()) return false;
        return mutate([parent, from, to] {
            parent->swapContent(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
            return true;
        }, value);
    }
    const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), value);
    if (it == tree_->starts().end()) return false;
    const auto from = static_cast<std::ptrdiff_t>(std::distance(tree_->starts().begin(), it));
    const auto to = from + delta;
    if (to < 0 || static_cast<std::size_t>(to) >= tree_->starts().size()) return false;
    return mutate([this, from, to] {
        tree_->swapStart(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
        return true;
    }, value);
}

bool EventTreeView::canUp() const noexcept {
    if (readOnly_ || !selectedContent_) return false;
    if (const auto parent = selectedContent_->parent()) {
        const auto it = std::find(parent->next().begin(), parent->next().end(), selectedContent_);
        return it != parent->next().begin() && it != parent->next().end();
    }
    if (!tree_) return false;
    const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), selectedContent_);
    return it != tree_->starts().begin() && it != tree_->starts().end();
}

bool EventTreeView::canDown() const noexcept {
    if (readOnly_ || !selectedContent_) return false;
    if (const auto parent = selectedContent_->parent()) {
        const auto it = std::find(parent->next().begin(), parent->next().end(), selectedContent_);
        return it != parent->next().end() && std::next(it) != parent->next().end();
    }
    if (!tree_) return false;
    const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), selectedContent_);
    return it != tree_->starts().end() && std::next(it) != tree_->starts().end();
}

bool EventTreeView::up() {
    if (canUp()) return moveContent(selectedContent_, -1);
    return canSwapToParent() && swapToParent();
}

bool EventTreeView::down() {
    if (canDown()) return moveContent(selectedContent_, 1);
    return canSwapToChild() && swapToChild();
}

bool EventTreeView::canSwapToParent() const noexcept {
    if (readOnly_ || !selectedContent_) return false;
    const auto parent = selectedContent_->parent();
    return parent && parent->type() != types::CType::Start && parent->parent() &&
           parent->next().size() == 1 && selectedContent_->detail().owner;
}

bool EventTreeView::canSwapToChild() const noexcept {
    if (readOnly_ || !selectedContent_ || selectedContent_->type() == types::CType::Start)
        return false;
    return selectedContent_->next().size() == 1 &&
           selectedContent_->next().front()->detail().owner &&
           selectedContent_->parent() != nullptr;
}

bool EventTreeView::swapToParent() {
    if (!canSwapToParent()) return false;
    const auto child = selectedContent_;
    const auto parent = child->parent();
    const auto grand = parent->parent();
    const auto it = std::find(grand->next().begin(), grand->next().end(), parent);
    if (it == grand->next().end()) return false;
    const auto index = static_cast<std::size_t>(std::distance(grand->next().begin(), it));
    return mutate([this, grand, parent, child, index] {
        parent->remove(child);
        grand->remove(parent);
        grand->insert(comm_.modelProps, index, child);
        child->add(comm_.modelProps, parent);
        return true;
    }, child);
}

bool EventTreeView::swapToChild() {
    if (!canSwapToChild()) return false;
    const auto parent = selectedContent_;
    const auto child = parent->next().front();
    const auto grand = parent->parent();
    const auto it = std::find(grand->next().begin(), grand->next().end(), parent);
    if (it == grand->next().end()) return false;
    const auto index = static_cast<std::size_t>(std::distance(grand->next().begin(), it));
    return mutate([this, grand, parent, child, index] {
        parent->remove(child);
        grand->remove(parent);
        grand->insert(comm_.modelProps, index, child);
        child->add(comm_.modelProps, parent);
        return true;
    }, child);
}

bool EventTreeView::canConvert(const types::CType type,
                               const ContentPtr& value) const {
    const auto content = selectedContentOr(value);
    return !readOnly_ && content && content->canConvert(type);
}

bool EventTreeView::convert(const types::CType type, const ContentPtr& value) {
    const auto content = selectedContentOr(value);
    if (!canConvert(type, content)) return false;
    return mutate([this, content, type] {
        // DのconvertTypeは子にvalidText()を実行する。
        content->convertType(type, {}, comm_.modelProps);
        return true;
    }, content);
}

bool EventTreeView::canConvTerminal() const noexcept {
    return selectedContent_ && selectedContent_->next().empty();
}

std::vector<types::CType> EventTreeView::conversionTypes() const {
    std::vector<types::CType> result;
    if (!selectedContent_) return result;
    for (const auto& [type, detail] : event::CONTENT_DETAILS())
        if (selectedContent_->canConvert(type)) result.push_back(type);
    return result;
}

EventTreeView::Snapshot EventTreeView::snapshot() const {
    Snapshot result;
    result.tree = tree_ ? tree_->dup() : EventTreePtr{};
    result.selectedPath = selectionPath();
    return result;
}

void EventTreeView::restore(const Snapshot& value) {
    if (!tree_ || !value.tree) return;
    // 保持したEventTreeの保存状態はルートを所有する。そのルートを使用中ツリ
    // ーへ移すと、履歴を消費し、最後のStartを削除しようとするため、移動しない。
    std::vector<ContentPtr> starts;
    for (const auto& start : value.tree->starts())
        if (start) starts.push_back(start->dup());
    tree_->enter(value.tree->fireEnter());
    tree_->escape(value.tree->fireEscape());
    tree_->lose(value.tree->fireLose());
    tree_->everyRound(value.tree->fireEveryRound());
    tree_->roundEnd(value.tree->fireRoundEnd());
    tree_->round0(value.tree->fireRound0());
    tree_->rounds(value.tree->rounds());
    tree_->keyCodes(value.tree->keyCodes());
    tree_->keyCodeMatchingType(value.tree->keyCodeMatchingType());
    tree_->comment(value.tree->comment());
    tree_->starts(std::move(starts));
    rebuildTree();
    if (!value.selectedPath.empty()) select(fromPath(value.selectedPath));
    else selectFallback();
}

bool EventTreeView::mutate(const std::function<bool()>& action,
                           ContentPtr changed, const bool useCount) {
    if (readOnly_ || !tree_ || !action) return false;
    const auto before = snapshot();
    if (!action()) return false;
    // 削除では、操作後の状態を取得する前に選択Contentが切り離され得
    // る。ここで再構築して有効なツリーノードへ選択を移す。省くとsnapshot()
    // が切り離したContentにイベントパスを問い合わせる。
    rebuildTree();
    const auto after = snapshot();
    if (undo_) {
        undo_->push(
            [this, before] { restore(before); notify({}, {}, true); },
            [this, after] { restore(after); notify({}, {}, true); });
    }
    notify(tree_, std::move(changed), useCount);
    return true;
}

void EventTreeView::notify(EventTreePtr tree, ContentPtr content,
                           const bool useCount) {
    refresh();
    modelChanged.call();
    if (!tree) tree = tree_;
    if (!content) content = selectedContent_;
    eventTreeChanged.call(tree);
    if (content) contentChanged.call(content);
    if (useCount) comm_.refUseCount.call();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool EventTreeView::clipboardAvailable() const noexcept {
    try {
        return parseClipboard(readClipboard().text).has_value();
    } catch (...) {
        return false;
    }
}

void EventTreeView::writeClipboard(const lib::ClipboardData& data) {
    clipboardMemory_ = data;
    hasClipboardMemory_ = !data.text.empty();
    if (const auto clipboard = comm_.clipboard()) clipboard->write(data);
}

lib::ClipboardData EventTreeView::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) {
        const std::string_view requested[]{kClipboardMime, "text/xml", "text/plain;charset=utf-8", "text/plain"};
        const auto value = clipboard->read_formats(requested);
        if (!value.text.empty() || !value.formats.empty()) return value;
    }
    return clipboardMemory_;
}

String EventTreeView::makeClipboard(const StringView kind,
                                    const xml::XNode& node) const {
    auto root = xml::XNode::create(kClipboardRoot);
    root.newAttr("kind", kind);
    root.newAttr("paneId", id_);
    root.append(node.clone());
    return root.text();
}

String EventTreeView::itemToXML(const TreeItemId id, const bool shallow,
                                const bool includeClipboardMetadata) const {
    const auto itemId = id ? id : selectedId_;
    const auto* info = nodeInfo(itemId);
    if (!info || !info->content) return {};
    auto content = info->content;
    EventTreePtr wrapper;
    if (content->type() == types::CType::Start) {
        auto copy = content->dup();
        if (shallow) copy->next().clear();
        wrapper = std::make_shared<EventTree>(copy);
    } else {
        wrapper = std::make_shared<EventTree>("Clipboard");
        auto copy = content->dup();
        if (shallow) copy->next().clear();
        const auto content_name = content->name();
        wrapper->starts().front()->add(comm_.modelProps, copy);
        // DはこのContentを直接直列化する。XML出力用の
        // 一時的なネイティブStartは自身の子名を正規化する
        // ため、コピーを接続した後に選択ノードの指定名を戻す。
        copy->setName(content_name);
    }
    const auto node = wrapper->toNode();
    const auto kind = content->type() == types::CType::Start ? "start" : "content";
    if (includeClipboardMetadata) return makeClipboard(kind, node);
    return node.text();
}

lib::ClipboardData EventTreeView::clipboardData() const {
    lib::ClipboardData result;
    result.text = itemToXML();
    appendClipboardFormats(result);
    return result;
}

bool EventTreeView::canCopy1Content() const noexcept {
    return selectedContent_ != nullptr;
}

bool EventTreeView::canDoT() const noexcept {
    if (readOnly_ || !tree_ || !selectedContent_ || tree_->starts().empty())
        return false;
    // Dの通常の切り取りコマンドは、最初のEventTree項目だけを除
    // 外する。後のStartはContentの親がなくても切り取れる。
    return tree_->starts().front() != selectedContent_;
}

bool EventTreeView::canCut1Content() const noexcept {
    return canDoT() && static_cast<bool>(selectedContent_->parent());
}

bool EventTreeView::del1Content() {
    if (!canDel1Content()) return false;
    editEnter();
    const auto content = selectedContent_;
    const auto owner = content->parent();
    if (!owner) return false;
    const auto position = std::find(owner->next().begin(), owner->next().end(), content);
    if (position == owner->next().end()) return false;

    const auto insertIndex = static_cast<std::size_t>(
        std::distance(owner->next().begin(), position));
    const auto children = content->next();
    const auto contentName = content->name();
    const auto lastNextType = content->detail().nextType;
    // Dは選択Contentを切り離す前に終了通知し、元の親・ツリー
    // のリンクが有効な間に開いている編集部を閉じられるようにする。
    if (comm_.deletingContent) comm_.deletingContent(content);

    return mutate([this, owner, content, children, insertIndex, contentName,
                   lastNextType] {
        if (std::find(owner->next().begin(), owner->next().end(), content) ==
            owner->next().end())
            return false;
        // Dの一つだけ削除は、全子を元の所有項目へつなぎ替える。通
        // 常削除では、部分木をそのままクリップボードへ保持する。
        owner->remove(content);
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& child = children[index];
            content->remove(child);
            if (prop_.etc.adjustContentName && index == 0 &&
                lastNextType != owner->detail().nextType) {
                // Dは二つの継続種別が異なる場合、取り除いたコンテ
                // ナーのラベルを、繰り上げた最初の子へ引き継ぐ。
                child->setName(contentName);
            } else {
                adjustText(prop_, owner, child, lastNextType);
            }
            owner->insert(comm_.modelProps, insertIndex + index, child);
        }
        return true;
    }, owner);
}

bool EventTreeView::canPasteInsert() const noexcept {
    return !readOnly_ && tree_ && clipboardAvailable();
}

bool EventTreeView::canDoP() const noexcept { return canPasteInsert(); }

bool EventTreeView::copy() {
    const auto data = clipboardData();
    if (data.text.empty()) return false;
    writeClipboard(data);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool EventTreeView::cut() {
    if (!canDoT() || !copy()) return false;
    return deleteContent();
}

bool EventTreeView::cut1Content() {
    if (!canCut1Content() || !copy1Content()) return false;
    return del1Content();
}

bool EventTreeView::copy1Content() {
    if (!canCopy1Content()) return false;
    editEnter();
    auto data = lib::ClipboardData{itemToXML(0, true), {}, {}};
    if (data.text.empty()) return false;
    appendClipboardFormats(data);
    writeClipboard(data);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool EventTreeView::clone() {
    if (readOnly_ || !copy()) return false;
    return pasteInsert();
}

void EventTreeView::copySelectionXML(const bool shallow) {
    auto data = lib::ClipboardData{itemToXML(0, shallow, false), {}, {}};
    appendClipboardFormats(data);
    writeClipboard(data);
}

std::optional<EventTreeView::ClipboardPayload> EventTreeView::parseClipboard(
    const StringView value) const {
    if (value.empty()) return std::nullopt;
    try {
        auto root = xml::XNode::parse(value);
        if (root.name() == "Event") return ClipboardPayload{"event", root};
        if (root.name() != kClipboardRoot) return std::nullopt;
        const auto kind = root.attr("kind", false, "event");
        auto node = root.child("Event", false);
        if (!node) return std::nullopt;
        return ClipboardPayload{kind, node};
    } catch (...) {
        return std::nullopt;
    }
}

bool EventTreeView::paste() {
    if (readOnly_ || !clipboardAvailable()) return false;
    const auto data = readClipboard();
    const auto payload = parseClipboard(data.text);
    return payload && pastePayload(*payload, false);
}

bool EventTreeView::pasteData(const lib::ClipboardData& data) {
    if (readOnly_) return false;
    const auto payload = parseClipboard(data.text);
    return payload && pastePayload(*payload, false);
}

bool EventTreeView::pasteScript(const StringView script, const bool tryInsert) {
    if (readOnly_ || !tree_ || !comm_.compileEventTemplate || script.empty())
        return false;
    const auto contents = comm_.compileEventTemplate(script);
    if (contents.empty()) return false;
    auto target = insertionOwner(selectedContent_, tryInsert);
    if (!target || !target->detail().owner) return false;
    ContentPtr last;
    const auto accepted = mutate([this, target, &contents, &last] {
        for (const auto& content : contents) {
            if (!content) continue;
            last = content->dup();
            target->add(comm_.modelProps, last);
        }
        return last != nullptr;
    }, target);
    if (accepted && last) select(last);
    return accepted;
}

bool EventTreeView::pasteInsert() {
    if (readOnly_ || !clipboardAvailable()) return false;
    const auto data = readClipboard();
    const auto payload = parseClipboard(data.text);
    return payload && pastePayload(*payload, true);
}

bool EventTreeView::pastePayload(const ClipboardPayload& payload,
                                 const bool tryInsert) {
    if (payload.kind == "event") {
        const auto value = EventTree::createFromNode(payload.node);
        return value && pasteEvent(value);
    }
    if (payload.kind == "start") {
        const auto value = EventTree::createFromNode(payload.node);
        return value && pasteStart(value);
    }
    if (payload.kind == "content") {
        const auto value = EventTree::createFromNode(payload.node);
        return value && pasteContent(value, tryInsert);
    }
    return false;
}

bool EventTreeView::pasteEvent(const EventTreePtr& value) {
    if (readOnly_ || !tree_ || !value) return false;
    const auto starts = value->starts();
    if (starts.empty()) return false;
    auto index = tree_->starts().size();
    if (selectedContent_ && selectedContent_->type() == types::CType::Start) {
        const auto it = std::find(tree_->starts().begin(), tree_->starts().end(), selectedContent_);
        if (it != tree_->starts().end()) index = static_cast<std::size_t>(std::distance(tree_->starts().begin(), it)) + 1;
    }
    return mutate([this, starts, index] {
        std::size_t offset = 0;
        for (const auto& start : starts) {
            auto copy = start->dup();
            copy->setName(uniqueStartName(*tree_, copy->name()));
            tree_->insert(index + offset++, copy);
        }
        return true;
    });
}

bool EventTreeView::pasteStart(const EventTreePtr& value) {
    return pasteEvent(value);
}

bool EventTreeView::pasteContent(const EventTreePtr& value, const bool tryInsert) {
    if (readOnly_ || !tree_ || !value || value->starts().empty()) return false;
    auto target = insertionOwner(selectedContent_, tryInsert);
    if (!target || !target->detail().owner) return false;
    const auto source = value->starts().front();
    if (!source || source->next().empty()) return false;
    const auto children = source->next();
    ContentPtr last;
    if (!mutate([this, target, children, &last] {
            for (const auto& child : children) {
                if (!child) continue;
                last = child->dup();
                target->add(comm_.modelProps, last);
            }
            return last != nullptr;
        }, target)) return false;
    if (last) select(last);
    return true;
}

void EventTreeView::beginDrag() { dragSourceId_ = selectedId_; }

lib::ClipboardData EventTreeView::dragData() const { return clipboardData(); }

bool EventTreeView::drop(lib::DragEvent& event) {
    if (readOnly_ || event.data.text.empty()) return false;
    const auto source = nodeInfo(dragSourceId_);
    const auto sourceContent = source ? source->content : ContentPtr{};
    if (sourceContent && selectedContent_ && contains(sourceContent, selectedContent_)) return false;
    // この表示内の移動は、一つのモデルトランザクションにする。貼
    // り付け・削除の二操作で取り消し記録が二つになるのを避ける。
    if (sourceContent && sourceContent->parent() &&
        hasOperation(event.selected, lib::DropOperation::Move)) {
        const auto target = insertionOwner(selectedContent_, true);
        const auto oldParent = sourceContent->parent();
        if (!target || !target->detail().owner || target == oldParent ||
            contains(sourceContent, target))
            return false;
        const auto accepted = mutate([this, oldParent, target, sourceContent] {
            oldParent->remove(sourceContent);
            target->add(comm_.modelProps, sourceContent);
            return true;
        }, sourceContent);
        if (!accepted) return false;
        select(sourceContent);
        event.handled = true;
        return true;
    }
    const auto accepted = pasteData(event.data);
    if (!accepted) return false;
    if (sourceContent && hasOperation(event.selected, lib::DropOperation::Move))
        deleteContent(sourceContent);
    event.handled = true;
    return true;
}

lib::DragSourceBinding EventTreeView::dragSourceBinding() {
    lib::DragSourceBinding binding;
    binding.source = treeView_;
    binding.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    binding.provide_data = [this] { beginDrag(); return dragData(); };
    binding.completed = [this](const lib::DropOperation) { dragSourceId_ = 0; };
    return binding;
}

lib::DropTargetBinding EventTreeView::dropTargetBinding() {
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

void EventTreeView::bindDragDrop() {
    if (const auto backend = comm_.services.drag_drop; backend && treeView_) {
        backend->bind_source(dragSourceBinding());
        backend->bind_target(dropTargetBinding());
    }
}

void EventTreeView::unbindDragDrop() {
    if (const auto backend = comm_.services.drag_drop; backend && treeView_)
        backend->unbind(treeView_);
}

bool EventTreeView::canUndo() const noexcept { return undo_ && undo_->canUndo(); }
bool EventTreeView::canRedo() const noexcept { return undo_ && undo_->canRedo(); }

void EventTreeView::undo() {
    if (!readOnly_ && undo_ && undo_->canUndo()) undo_->undo();
    refresh();
}

void EventTreeView::redo() {
    if (!readOnly_ && undo_ && undo_->canRedo()) undo_->redo();
    refresh();
}

void EventTreeView::selectFallback() {
    if (treeView_ && !treeView_->nodes.empty()) select(treeView_->nodes.front().id);
    else {
        selectedId_ = 0;
        selectedContent_.reset();
    }
}

bool EventTreeView::canExpand() const noexcept {
    if (!selectedId_ || !treeView_) return false;
    const auto it = std::find_if(treeView_->nodes.begin(), treeView_->nodes.end(),
                                 [this](const lib::TreeNode& node) { return node.id == selectedId_; });
    return it != treeView_->nodes.end() && !it->expanded && selectedContent_ &&
           !selectedContent_->next().empty();
}

bool EventTreeView::canCollapse() const noexcept {
    if (!selectedId_ || !treeView_) return false;
    const auto it = std::find_if(treeView_->nodes.begin(), treeView_->nodes.end(),
                                 [this](const lib::TreeNode& node) { return node.id == selectedId_; });
    return it != treeView_->nodes.end() && it->expanded && selectedContent_ &&
           !selectedContent_->next().empty();
}

bool EventTreeView::canExpandTree() const noexcept {
    if (!treeView_) return false;
    return std::any_of(treeView_->nodes.begin(), treeView_->nodes.end(),
                       [](const lib::TreeNode& node) { return !node.expanded; });
}

bool EventTreeView::canFoldTree() const noexcept {
    if (!treeView_) return false;
    return std::any_of(treeView_->nodes.begin(), treeView_->nodes.end(),
                       [](const lib::TreeNode& node) { return node.expanded; });
}

void EventTreeView::expand() {
    if (!canExpand()) return;
    editEnter();
    for (auto& node : treeView_->nodes) if (node.id == selectedId_) node.expanded = true;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void EventTreeView::collapse() {
    if (!canCollapse()) return;
    editEnter();
    for (auto& node : treeView_->nodes) if (node.id == selectedId_) node.expanded = false;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void EventTreeView::treeOpen() {
    if (!treeView_) return;
    for (auto& node : treeView_->nodes) node.expanded = true;
}

void EventTreeView::treeClose() {
    if (!treeView_) return;
    for (auto& node : treeView_->nodes) node.expanded = false;
}

bool EventTreeView::openToolWindow(const bool focusInEventView) noexcept {
    toolWindowOpen_ = true;
    if (box_) box_->openToolWindow();
    if (focusInEventView) focused_ = true;
    toolWindowChanged.call(true);
    return true;
}

void EventTreeView::closeToolWindow() noexcept {
    toolWindowOpen_ = false;
    if (box_) box_->closeToolWindow();
    toolWindowChanged.call(false);
}

void EventTreeView::getContentsBox() {
    if (!box_) {
        box_ = std::make_unique<ContentsToolBox>(this);
        if (panel_) (void)box_->createControl(panel_);
    }
}

ContentsToolBox::ContentsToolBox(EventTreeView* parent) : parent_(parent) {}

PanelPtr ContentsToolBox::createControl(const Parent& parent) {
    if (!panel_) {
        panel_ = std::make_shared<lib::Panel>();
        panel_->set_layout(std::make_shared<lib::LinearLayout>(
            lib::Orientation::Horizontal));
        toolbar_ = std::make_shared<lib::ToolBar>();
        toolbar_->items.push_back({"Arrow", "arrow", 0, true, true, false, false});
        toolbar_->items.push_back({"Create", "create", 0, true, false, false, false});
        template_menu_ = std::make_shared<lib::Menu>(true);
        template_menu_->on_open = [this] { refreshTemplates(); };
        lib::ToolItem templates{
            parent_ ? parent_->commons().eventTemplatesText : String{"イベントテンプレート"},
            "templates", 0, true, false, false, true};
        templates.drop_down_menu = template_menu_;
        toolbar_->items.push_back(std::move(templates));
        toolbar_->on_command = [this](const StringView command) {
            if (command == "arrow") arrow();
            else if (command == "create") create();
        };
        panel_->add(toolbar_);

        type_selector_ = std::make_shared<lib::ComboBox>();
        type_selector_->editable = false;
        for (auto raw = static_cast<std::int32_t>(types::CType::StartBattle);
             raw <= static_cast<std::int32_t>(types::CType::CustomXmlContent);
             ++raw) {
            const auto type = static_cast<types::CType>(raw);
            const auto detail = event::contentDetail(type);
            String label = detail.type;
            if (label.empty() && !detail.names.empty()) label = detail.names.front();
            if (label.empty()) continue;
            selectable_types_.push_back(type);
            type_selector_->items.push_back({std::move(label), std::to_string(raw)});
        }
        const auto selected = std::find(selectable_types_.begin(),
                                        selectable_types_.end(), contentType_);
        if (selected != selectable_types_.end())
            type_selector_->selected_index = static_cast<std::size_t>(
                std::distance(selectable_types_.begin(), selected));
        type_selector_->on_selection_changed =
            [this](const std::optional<std::size_t> index) {
                if (!index || *index >= selectable_types_.size()) return;
                insertMode(selectable_types_[*index]);
            };
        panel_->add(type_selector_);
    }
    if (parent) parent->add(panel_);
    return panel_;
}

void ContentsToolBox::dispose() noexcept {
    if (toolbar_) toolbar_->on_command = {};
    if (template_menu_) template_menu_->on_open = {};
    if (type_selector_) type_selector_->on_selection_changed = {};
    toolbar_.reset();
    type_selector_.reset();
    template_menu_.reset();
    selectable_types_.clear();
    panel_.reset();
}

bool ContentsToolBox::create() {
    if (!parent_ || arrowMode_) return false;
    return static_cast<bool>(parent_->create(contentType_, parent_->selectionContent()));
}

void ContentsToolBox::refreshTemplates() {
    if (!template_menu_ || !parent_) return;
    std::vector<lib::MenuItem> items;
    const auto append = [this, &items](const lib::Array<structs::EvTemplate>& values) {
        for (const auto& value : values) {
            lib::MenuItem item;
            item.text = ::cwxeditor::cwx::menu::MenuProps::buildMenu(
                value.name.empty() ? String{"（無題）"} : value.name,
                value.mnemonic, value.hotkey, false);
            item.shortcut = value.hotkey;
            if (parent_->commons().compileEventTemplate &&
                parent_->commons().contentImage) {
                const auto contents =
                    parent_->commons().compileEventTemplate(value.script);
                if (!contents.empty() && contents.front())
                    item.icon = parent_->commons().contentImage(
                        contents.front()->type());
            }
            item.action = [this, script = value.script] {
                if (!parent_) return;
                const auto gui = parent_->commons().services.gui;
                const auto modifiers = gui ? gui->current_modifiers()
                                           : lib::Modifiers::None;
                const bool tryInsert =
                    (static_cast<std::uint32_t>(modifiers) &
                     static_cast<std::uint32_t>(lib::Modifiers::Shift)) == 0U;
                (void)parent_->pasteScript(script, tryInsert);
            };
            items.push_back(std::move(item));
        }
    };
    const auto scenario = parent_->commons().scenarioEventTemplates
                              ? parent_->commons().scenarioEventTemplates()
                              : lib::Array<structs::EvTemplate>{};
    const auto global = parent_->commons().globalEventTemplates
                            ? parent_->commons().globalEventTemplates()
                            : lib::Array<structs::EvTemplate>{};
    append(scenario);
    if (!scenario.empty() && !global.empty()) {
        lib::MenuItem separator;
        separator.separator = true;
        items.push_back(std::move(separator));
    }
    append(global);
    if (!scenario.empty() || !global.empty()) {
        lib::MenuItem separator;
        separator.separator = true;
        items.push_back(std::move(separator));
    }
    lib::MenuItem edit;
    edit.text = parent_->commons().editScenarioEventTemplatesText;
    edit.enabled = static_cast<bool>(parent_->commons().editScenarioEventTemplates);
    edit.action = parent_->commons().editScenarioEventTemplates;
    items.push_back(std::move(edit));
    (void)lib::replace_menu_items(template_menu_, std::move(items));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventtreeview
