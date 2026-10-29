#pragma once

// cwx/editor/gui/dwt/editablelistview.d
// の移植可能なC++20実装。 SWTのComposite/Table/DragSource
// を共通の部品・イベントで表す。 値の一
// 覧と全編集規則をこのクラスへ置き、ネイ
// ティブとヘッドレスで同じモデルを使う。

#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::editablelistview {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using XNode = ::cwxeditor::cwx::xml::XNode;
using Services = ::cwxeditor::lib::Services;
using Parent = ::cwxeditor::lib::Container*;
using TableView = ::cwxeditor::lib::TableView;
using ToolBar = ::cwxeditor::lib::ToolBar;
using Menu = ::cwxeditor::lib::Menu;
using TableEditorArray = ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<class AbstractTableEdit>>;

/// この汎用一覧が使うDWT Commonsの共通部分。 Editor
/// 側のモジュールは、自身のプロパティモデルから値を渡せる。
struct Commons {
    Services services{};
    ::cwxeditor::lib::Action refreshToolBar;
    std::size_t undoMax{100U};
    String addText{"追加"};
    String delText{"削除"};
    ::cwxeditor::lib::ResourceId addIcon{};
    ::cwxeditor::lib::ResourceId delIcon{};
    String undoText{"元に戻す"};
    String redoText{"やり直す"};
    String selectAllText{"すべて選択"};
    String copyText{"コピー"};
    String cutText{"切り取り"};
    String pasteText{"貼り付け"};
    String upText{"上へ"};
    String downText{"下へ"};
    dmenu::Presentation presentation{};
    bool xmlCopy{};
    std::function<bool()> xmlCopyProvider;
    std::function<std::size_t()> undoMaxProvider;
    // 解除処理を返し、一つの共有部品が自分の購読を管理できるようにする。
    std::function<::cwxeditor::lib::Action(::cwxeditor::lib::Action)> subscribeUndoMax;
    std::function<::cwxeditor::lib::Action(::cwxeditor::lib::Action)> subscribeMenu;

    Commons() = default;
    explicit Commons(Services value) : services(std::move(value)) {}
    Commons(Services value, ::cwxeditor::lib::Action refresh,
            std::size_t maxUndo = 100U)
        : services(std::move(value)), refreshToolBar(std::move(refresh)),
          undoMax(maxUndo) {}
};

/// DWTのAbstractTableEditに対応する共通型。具体的な
/// GUIは派生してセル編集へ接続できる。 一覧モデルはeditablelistview.d
/// が使う確定・中止・編集開始だけを必要とする。
class AbstractTableEdit {
public:
    virtual ~AbstractTableEdit() = default;

    [[nodiscard]] virtual bool isEditing() const noexcept { return editing_; }
    virtual void startEdit() noexcept { editing_ = true; }
    virtual void enter() noexcept { editing_ = false; }
    virtual void cancel() noexcept { editing_ = false; }

protected:
    void editing(bool value) noexcept { editing_ = value; }

private:
    bool editing_{};
};

using AbstractTableEditPtr = ::cwxeditor::lib::Owner<AbstractTableEdit>;
using ModCallback = ::cwxeditor::lib::Action;
using SelectionCallback =
    ::cwxeditor::lib::Callback<void(const ::cwxeditor::lib::Array<std::size_t>&)>;

inline constexpr int READ_ONLY_STYLE = 1;
inline constexpr StringView XML_MIME_TYPE = "application/x-cwxeditor-xml";

namespace detail {

template <typename Component>
[[nodiscard]] std::shared_ptr<Component> createComponent(
    const Services& services, ::cwxeditor::lib::ComponentKind kind) {
    if (services.gui) {
        auto candidate = services.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<Component>(candidate)) return result;
    }
    return std::make_shared<Component>();
}

[[nodiscard]] inline bool hasModifier(const ::cwxeditor::lib::Modifiers value,
                                      const ::cwxeditor::lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(modifier)) != 0U;
}

[[nodiscard]] inline bool hasOperation(const ::cwxeditor::lib::DropOperation value,
                                       const ::cwxeditor::lib::DropOperation operation) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(operation)) != 0U;
}

[[nodiscard]] inline std::vector<std::byte> bytesFromText(StringView text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const unsigned char value : text)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

[[nodiscard]] inline String textFromBytes(const std::vector<std::byte>& bytes) {
    String result;
    result.reserve(bytes.size());
    for (const auto value : bytes)
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    return result;
}

template <typename T>
[[nodiscard]] bool equalValue(const T& left, const T& right) {
    return left == right;
}

}  // namespace detail

/// DのAbstractEditableListView!(T,
/// Sort)に対応する、モデル付きの編集可能な表。
template <typename T, bool Sort = false>
class AbstractEditableListView {
public:
    using Value = T;
    using Values = ::cwxeditor::lib::Array<T>;
    using IndexArray = ::cwxeditor::lib::Array<std::size_t>;

    AbstractEditableListView(Commons comm, Parent parent, int style,
                             bool canDuplicate)
        : comm_(std::move(comm)),
          parent_(parent),
          read_only_((style & READ_ONLY_STYLE) != 0),
          can_duplicate_(canDuplicate),
          undo_(std::make_shared<undo::UndoManager>(comm_.undoMax)) {
        static std::atomic<std::uint64_t> nextId{1U};
        id_ = std::to_string(nextId.fetch_add(1U, std::memory_order_relaxed));

        root_ = detail::createComponent<::cwxeditor::lib::Panel>(
            comm_.services, ::cwxeditor::lib::ComponentKind::Panel);
        root_->set_layout(std::make_shared<::cwxeditor::lib::GridLayout>());

        auto range_table = std::make_shared<
            ::cwxeditor::cwx::editor::gui::dwt::customtable::TableModel>(
                ::cwxeditor::cwx::editor::gui::dwt::customtable::STYLE_MULTI_SELECT |
                ::cwxeditor::cwx::editor::gui::dwt::customtable::STYLE_VIRTUAL |
                ::cwxeditor::cwx::editor::gui::dwt::customtable::STYLE_HEADER,
                comm_.services);
        range_table->installRangeSelection();
        table_ = std::move(range_table);
        table_->multi_select = true;
        table_->full_row_selection = true;
        table_->virtualized = true;
        table_->set_enabled(true);

        if (!read_only_) {
            toolbar_ = detail::createComponent<ToolBar>(
                comm_.services, ::cwxeditor::lib::ComponentKind::ToolBar);
            toolbar_->set_enabled(true);
        }

        context_menu_ = std::make_shared<Menu>(true);
        root_->add(table_);
        if (toolbar_) root_->insert(0U, toolbar_);
        if (parent_) parent_->add(root_);

        installCallbacks();
        installDragDrop();
        const auto weak=weakLifetime();
        // ポインターの範囲選択は共有TableModelのリスナー経由、 ネイ
        // ティブの行クリックは下のon_selection_changed経由
        // で通知される。 どちらも同じ値の選択とツールバーの状態を更新する。
        if(auto model=std::dynamic_pointer_cast<customtable::TableModel>(table_))
            table_selection_connection_=model->addSelectionListener([this,weak](const auto& indices){
                if(!weak.expired())setSelection(indices);
            });
        if (comm_.undoMaxProvider) undo_->max(comm_.undoMaxProvider());
        if (comm_.subscribeUndoMax) disconnect_undo_=comm_.subscribeUndoMax([this,weak] {
            if(weak.expired())return;
            const auto provider=comm_.undoMaxProvider;
            const auto history=undo_;
            if(provider) {
                const auto maximum=provider();
                if(!weak.expired())history->max(maximum);
            }
        });
        if (comm_.subscribeMenu) disconnect_menu_=comm_.subscribeMenu([this,weak] {
            if (!weak.expired() && initialized_) refreshMenuPresentation();
        });
        root_->on_descendant_key_down=[this,weak](auto& event) {
            if (!weak.expired() && !event.handled) handleHistoryKey(event);
        };
    }

    AbstractEditableListView(const AbstractEditableListView&) = delete;
    AbstractEditableListView& operator=(const AbstractEditableListView&) = delete;
    virtual ~AbstractEditableListView() {
        retireCallbacks();
        if (comm_.services.gui && table_)
            (void)comm_.services.gui->finish_programmatic_inline_edit(table_, false);
        if (disconnect_undo_) disconnect_undo_();
        if (disconnect_menu_) disconnect_menu_();
        if (root_) root_->on_descendant_key_down={};
        if (context_menu_) {context_menu_->on_open={};context_menu_->on_command={};}
        if (comm_.services.drag_drop && table_)
            comm_.services.drag_drop->unbind(table_);
        if (table_) {
            if(auto model=std::dynamic_pointer_cast<customtable::TableModel>(table_))
                model->removeSelectionListener(table_selection_connection_);
            table_->on_selection_changed = {};
            table_->on_cell_activate = {};
            table_->on_cell_edited = {};
            table_->on_inline_edit_cancelled = {};
            table_->on_key_down = {};
            table_->on_pointer = {};
        }
        if (toolbar_) toolbar_->on_command = {};
    }

    /// Dの公開の変更通知デリゲート配列。
    std::vector<ModCallback> modEvent;
    /// 選択だけの変更を通知する、共通APIの追加通知。
    SelectionCallback selectionEvent;

    [[nodiscard]] const String& id() const noexcept { return id_; }
    void id(String value) { id_ = std::move(value); }

    /// 値・履歴の変更後にリスナーへ通知する。
    void raiseModifyEvent() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        const auto callbacks = modEvent;
        for (const auto& callback : callbacks) {
            if(life.expired() || session!=session_revision_)return;
            if(callback)callback();
        }
    }

    [[nodiscard]] bool undo() {
        if (read_only_) return false;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (nativeInlineEditing()) cancelEditors();
        if(life.expired() || session!=session_revision_)return false;
        const auto history=undo_;
        return history->undo();
    }
    [[nodiscard]] bool redo() {
        if (read_only_) return false;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (nativeInlineEditing()) cancelEditors();
        if(life.expired() || session!=session_revision_)return false;
        const auto history=undo_;
        return history->redo();
    }
    [[nodiscard]] bool canUndo() const noexcept { return !read_only_ && undo_->canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return !read_only_ && undo_->canRedo(); }
    [[nodiscard]] std::size_t undoPointer() const noexcept { return undo_->pointer(); }
    void setUndoMax(std::size_t value) { undo_->max(value); }
    [[nodiscard]] undo::UndoManager& undoManager() noexcept { return *undo_; }
    [[nodiscard]] const undo::UndoManager& undoManager() const noexcept { return *undo_; }

    [[nodiscard]] bool mainValueEditEnd(std::size_t index, const T& value) {
        if(read_only_)return false;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (index >= list_.size()) return false;
        const bool isNew = pending_index_.has_value() && *pending_index_ == index;
        if (!isNew && equals(list_[index], value)) return false;

        // Dのscope(exit)と同じく、検証や通知が例外を投げ
        // ても仮の印を外してツールバーを更新する。 再入で始まった新
        // しい編集状態を消したり、終了した所有者へ触れたりしない。
        const auto pending = pending_revision_;
        const auto cleanup = [this, life, session, pending] {
            if (life.expired() || session != session_revision_ || pending != pending_revision_) return;
            pending_index_.reset();
            refreshToolBar();
        };
        struct FinishEdit {
            const decltype(cleanup)& action;
            int exceptions{std::uncaught_exceptions()};
            ~FinishEdit() noexcept(false) {
                if (std::uncaught_exceptions() > exceptions) {
                    try { action(); } catch (...) { /* 元の失敗を維持する。 */ }
                } else action();
            }
        } finish{cleanup};

        const auto duplicate = existsValue(index, value);
        if(life.expired() || session!=session_revision_)return false;
        if (!isValidValue(value) || duplicate.has_value()) {
            if (isNew) {
                eraseRaw(index);
                pending_index_.reset();
                refreshTable();
                if(life.expired() || session!=session_revision_)return false;
                return false;
            }
            if (!isValidValue(value)) {
                storeAll();
                eraseRaw(index);
                refreshTable();
                if(life.expired() || session!=session_revision_)return false;
                raiseModifyEvent();
                if(life.expired() || session!=session_revision_)return false;
            } else {
                // 有効な既存値が別の行との重複だけで拒否さ
                // れた場合も、Dと同じく完了を通知する。
                raiseModifyEvent();
                if(life.expired() || session!=session_revision_)return false;
            }
            if(life.expired() || session!=session_revision_)return false;
            return false;
        }

        pending_index_.reset();
        if (isNew) {
            eraseRaw(index);
            storeAll();
            insertRaw(index, value);
            if constexpr (Sort) {
                sortValues({index});
            } else {
                setSelection({index});
                if(life.expired() || session!=session_revision_)return false;
                refreshTable();
                if(life.expired() || session!=session_revision_)return false;
            }
        } else if constexpr (Sort) {
            storeAll();
            list_[index] = value;
            sortValues({index});
            refreshTable();
            if(life.expired() || session!=session_revision_)return false;
        } else {
            storeSingle(index);
            list_[index] = value;
            refreshTable();
            if(life.expired() || session!=session_revision_)return false;
            setSelection({index});
            if(life.expired() || session!=session_revision_)return false;
        }
        raiseModifyEvent();
        if(life.expired() || session!=session_revision_)return false;
        if(life.expired() || session!=session_revision_)return false;
        return true;
    }

    [[nodiscard]] bool subValueEditEnd(std::size_t index, const T& value) {
        if(read_only_)return false;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (index >= list_.size() || equals(list_[index], value)) return false;
        const auto duplicate = existsValue(index, value);
        if(life.expired() || session!=session_revision_)return false;
        if (duplicate.has_value()) {
            refreshToolBar();
            if(life.expired() || session!=session_revision_)return false;
            return false;
        }
        if constexpr (Sort) {
            storeAll();
            list_[index] = value;
            sortValues({index});
            refreshTable();
            if(life.expired() || session!=session_revision_)return false;
        } else {
            storeSingle(index);
            list_[index] = value;
            refreshTable();
            if(life.expired() || session!=session_revision_)return false;
            setSelection({index});
            if(life.expired() || session!=session_revision_)return false;
        }
        raiseModifyEvent();
        if(life.expired() || session!=session_revision_)return false;
        refreshToolBar();
        if(life.expired() || session!=session_revision_)return false;
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> existsValue(std::size_t index,
                                                          const T& value) {
        if (can_duplicate_) return std::nullopt;
        for (std::size_t i = 0; i < list_.size(); ++i) {
            if (i == index || !equals(list_[i], value)) continue;
            // Dのプログラムによる重複行の選択はSelection
            // を出さない。 ツールバーへの通知は編集完了側が行う。
            setSelection({i}, false);
            // DのexistsValueと同じく、残す行を選択してTable.showSelectionを呼ぶ。
            if (table_) (void)::cwxeditor::lib::reveal_table_row(*table_, i);
            return i;
        }
        return std::nullopt;
    }

    /// 保留中の追加編集を取り消す。確定済みの一覧は変更しない。
    void exitEdit(bool cancel) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return;
        if (cancel && pending_index_) {
            eraseRaw(*pending_index_);
            pending_index_.reset();
            refreshTable();
            if(life.expired() || session!=session_revision_)return;
            refreshToolBar();
            if(life.expired() || session!=session_revision_)return;
        }
        if (!cancel) pending_index_.reset();
    }

    [[nodiscard]] bool isNewItemEditing() const noexcept {
        return pending_index_.has_value();
    }

    /// DのcreateNewValue()の処理を使って値を追加する。
    [[nodiscard]] bool add() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (read_only_) return false;
        enterEditors();
        if(life.expired() || session!=session_revision_)return false;
        // 表示側が拒否・消失した場合も、次の追加で仮
        // の印を置き換える際に、古い仮行を残さない。
        if (nativeInlineEditing() && pending_index_) exitEdit(true);
        if(life.expired() || session!=session_revision_)return false;
        if (valueCount() >= maxCount()) return false;
        std::size_t index = static_cast<std::size_t>(-1);
        bool cancel = false;
        T value = createNewValue(index, cancel);
        if(life.expired() || session!=session_revision_)return false;
        if (cancel) return false;
        if (index == static_cast<std::size_t>(-1) || index > list_.size())
            index = list_.size();
        insertRaw(index, value);
        pending_index_ = index;
        const auto pending = ++pending_revision_;
        setSelection({index});
        if(life.expired() || session!=session_revision_)return false;
        refreshTable();
        if(life.expired() || session!=session_revision_)return false;
        refreshToolBar();
        if(life.expired() || session!=session_revision_)return false;
        if (nativeInlineEditing()) {
            if (pending != pending_revision_ || pending_index_ != index) return false;
            const auto table = table_;
            const auto epoch = table->row_order_revision;
            ::cwxeditor::lib::ProgrammaticInlineEditRequest request;
            request.table = table;
            request.matches_stable_row = [this, life, session, table, epoch, index, pending](std::size_t row) {
                return !life.expired() && session == session_revision_ &&
                    table == table_ && table->row_order_revision == epoch &&
                    pending_revision_ == pending && pending_index_ == index && row == index;
            };
            request.row_height = ::cwxeditor::lib::table_row_height(*table, 0.0F);
            const auto gui = comm_.services.gui;
            const bool started = gui && gui->request_programmatic_inline_edit(request);
            if(life.expired() || session!=session_revision_)return false;
            if (!started) {
                if (table == table_ && pending_revision_ == pending && pending_index_ == index)
                    exitEdit(true);
                return false;
            }
        } else if (const auto editor = mainEditor()) editor->startEdit();
        return true;
    }

    /// 一時的な編集へ入らず、構築済みの値を追加する。
    [[nodiscard]] bool addValue(const T& value, std::size_t index = static_cast<std::size_t>(-1),
                                bool force = false) {
        if (!force && read_only_) return false;
        if (index == static_cast<std::size_t>(-1)) index = list_.size();
        return appendValuesImpl(Values{value}, index, force);
    }

    [[nodiscard]] bool del() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        enterEditors();
        if(life.expired() || session!=session_revision_)return false;
        return delImpl(selection_);
    }

    [[nodiscard]] bool delImpl(IndexArray indices) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (read_only_ || indices.empty()) return false;
        indices.erase(std::remove_if(indices.begin(), indices.end(),
                                     [this](std::size_t index) {
                                         return index >= list_.size();
                                     }),
                      indices.end());
        if (indices.empty()) return false;
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

        Values selectedValues;
        selectedValues.reserve(indices.size());
        for (const auto index : indices) selectedValues.push_back(list_[index]);
        storeAll();
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) eraseRaw(*it);

        pending_index_.reset();
        selection_.clear();
        for (std::size_t index = 0; index < list_.size(); ++index) {
            if (std::find_if(selectedValues.begin(), selectedValues.end(),
                             [this, index](const T& value) {
                                 return equals(list_[index], value);
                             }) != selectedValues.end()) {
                selection_.push_back(index);
            }
        }
        refreshTable();
        if(life.expired() || session!=session_revision_)return false;
        raiseModifyEvent();
        if(life.expired() || session!=session_revision_)return false;
        refreshToolBar();
        if(life.expired() || session!=session_revision_)return false;
        return true;
    }

    [[nodiscard]] bool up() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if constexpr (Sort) {
            return false;
        } else {
            enterEditors();
            if(life.expired() || session!=session_revision_)return false;
            if (selection_.empty() || selection_.front() == 0U || read_only_) return false;
            storeAll();
            for (const auto index : selection_) std::swap(list_[index], list_[index - 1U]);
            for (auto& index : selection_) --index;
            refreshTable({}, false, selection_.front());
            if(life.expired() || session!=session_revision_)return false;
            setSelection(selection_);
            if(life.expired() || session!=session_revision_)return false;
            raiseModifyEvent();
            if(life.expired() || session!=session_revision_)return false;
            refreshToolBar();
            if(life.expired() || session!=session_revision_)return false;
            return true;
        }
    }

    [[nodiscard]] bool down() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if constexpr (Sort) {
            return false;
        } else {
            enterEditors();
            if(life.expired() || session!=session_revision_)return false;
            if (selection_.empty() || selection_.back() + 1U >= list_.size() || read_only_)
                return false;
            storeAll();
            for (auto it = selection_.rbegin(); it != selection_.rend(); ++it)
                std::swap(list_[*it], list_[*it + 1U]);
            for (auto& index : selection_) ++index;
            refreshTable({}, false, selection_.front());
            if(life.expired() || session!=session_revision_)return false;
            setSelection(selection_);
            if(life.expired() || session!=session_revision_)return false;
            raiseModifyEvent();
            if(life.expired() || session!=session_revision_)return false;
            refreshToolBar();
            if(life.expired() || session!=session_revision_)return false;
            return true;
        }
    }

    [[nodiscard]] bool canUp() const noexcept {
        if constexpr (Sort) return false;
        if (read_only_ || selection_.empty()) return false;
        return selection_.front() > 0U;
    }

    [[nodiscard]] bool canDown() const noexcept {
        if constexpr (Sort) return false;
        if (read_only_ || selection_.empty()) return false;
        return selection_.back() + 1U < list_.size();
    }

    [[nodiscard]] bool canSelectAll() const noexcept {
        return table_ && selection_.size() != list_.size();
    }

    void selectAll() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return;
        if (!canSelectAll()) return;
        IndexArray indices;
        indices.reserve(list_.size());
        for (std::size_t index = 0; index < list_.size(); ++index) indices.push_back(index);
        setSelection(std::move(indices));
        if(life.expired() || session!=session_revision_)return;
        refreshToolBar();
        if(life.expired() || session!=session_revision_)return;
    }

    void setSelection(IndexArray indices, bool notify = true) {
        indices.erase(std::remove_if(indices.begin(), indices.end(),
                                     [this](std::size_t index) {
                                         return index >= list_.size();
                                     }),
                      indices.end());
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        selection_ = std::move(indices);
        if (table_ && table_->rows.size() == list_.size()) {
            (void)::cwxeditor::lib::select_table_rows(
                *table_, selection_, false, false);
        }
        if (!notify) return;
        const auto callback = selectionEvent;
        const auto selected = selection_;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (callback) callback(selected);
        if(life.expired() || session!=session_revision_)return;
        refreshToolBar();
    }

    [[nodiscard]] const IndexArray& selectionIndices() const noexcept { return selection_; }

    void clearAll() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return;
        const auto table=table_;
        std::optional<std::size_t> focused;
        if(table) focused=table->selection_cursor;
        const auto generation=render_revision_;
        if(!refreshTable())return;
        if(life.expired() || session!=session_revision_ || table_!=table ||
           render_revision_!=generation+1U)return;
        // DWTのclearAllと同じく、ネイティブのフォーカスは選択集合から独立して保つ。
        if(focused && *focused<table->rows.size())table->selection_cursor=focused;
    }

    [[nodiscard]] Values values() const {
        const auto life=weakLifetime();
        const_cast<AbstractEditableListView*>(this)->ensureInitialized();
        if(life.expired())return {};
        Values result = list_;
        if (pending_index_ && *pending_index_ < result.size())
            result.erase(result.begin() + static_cast<std::ptrdiff_t>(*pending_index_));
        return result;
    }

    void values(Values value) {
        const auto life=weakLifetime();
        auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return;
        session=++session_revision_;
        cancelEditors();
        if(life.expired() || session!=session_revision_)return;
        undo_->reset();
        pending_index_.reset();
        const auto table=table_;
        std::optional<std::size_t> focused;
        if(table) focused=table->selection_cursor;
        const auto new_count=value.size();
        selection_.erase(std::remove_if(selection_.begin(),selection_.end(),
            [new_count](std::size_t index) { return index>=new_count; }),selection_.end());
        list_ = std::move(value);
        if constexpr (Sort) std::sort(list_.begin(), list_.end());
        const auto generation=render_revision_;
        if(!refreshTable({}, true))return;
        if(life.expired() || session!=session_revision_ || table_!=table ||
           render_revision_!=generation+1U)return;
        // DWTが選択なしを返しても、範囲内のネイティブフォーカス行は残す。
        if(focused && *focused<table->rows.size())table->selection_cursor=focused;
        refreshToolBarEnabledState();
    }

    [[nodiscard]] std::size_t valueCount() const noexcept { return list_.size(); }
    [[nodiscard]] const T& value(std::size_t index) const { return list_.at(index); }
    [[nodiscard]] T& value(std::size_t index) { return list_.at(index); }

    void setValue(std::size_t index, const T& value) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return;
        if (index >= list_.size()) return;
        list_[index] = value;
        refreshTable(index);
        if(life.expired() || session!=session_revision_)return;
    }

    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] bool canDuplicate() const noexcept { return can_duplicate_; }

    void enabled(bool value) {
        enabled_ = value;
        if (table_) table_->set_enabled(value);
        if (toolbar_) toolbar_->set_enabled(!read_only_ && value);
    }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    [[nodiscard]] const std::shared_ptr<TableView>& table() const {
        const_cast<AbstractEditableListView*>(this)->ensureInitialized();
        return table_;
    }
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::Panel>& widget() const noexcept {
        return root_;
    }
    [[nodiscard]] const std::shared_ptr<ToolBar>& toolbar() const noexcept { return toolbar_; }
    [[nodiscard]] const std::shared_ptr<Menu>& contextMenu() const noexcept { return context_menu_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Parent parent() const noexcept { return parent_; }

    [[nodiscard]] bool appendValues(const Values& values) {
        return appendValuesImpl(values, static_cast<std::size_t>(-1), false);
    }

    [[nodiscard]] bool appendFromNode(const XNode& node, std::size_t index = static_cast<std::size_t>(-1),
                                      bool force = false) {
        ensureInitialized();
        if (node.name() != String(xmlName())) return false;
        return appendValuesImpl(createFromNode(node), index, force);
    }

    [[nodiscard]] String xmlNameValue() const { return String(xmlName()); }

    [[nodiscard]] bool copy() const {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (selection_.empty()) return false;
        const_cast<AbstractEditableListView*>(this)->enterEditors();
        if(life.expired() || session!=session_revision_)return false;
        auto backend = clipboardBackend();
        if (!backend) return false;
        const auto data=copyData();
        if(life.expired() || session!=session_revision_)return false;
        backend->write(data);
        if(life.expired() || session!=session_revision_)return false;
        refreshToolBar();
        return true;
    }

    [[nodiscard]] bool cut() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (read_only_ || selection_.empty()) return false;
        const auto selected=selection_;
        if (!copy()) return false;
        if(life.expired() || session!=session_revision_)return false;
        return delImpl(selected);
    }

    [[nodiscard]] bool paste() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (read_only_) return false;
        auto backend = clipboardBackend();
        if (!backend) return false;
        const auto data = backend->read();
        if(life.expired() || session!=session_revision_)return false;
        const auto xml = xmlbytestransfer::CBtoXML(data);
        if (xml.empty()) return false;
        // Dと同じく、外部のルートを解析・拒否する前に、実行中の編集を確定する。
        enterEditors();
        if(life.expired() || session!=session_revision_)return false;
        try {
            const auto node = XNode::parse(xml, true);
            return appendFromNode(node, list_.size(), false);
        } catch (const std::exception&) {
            return false;
        }
    }

    [[nodiscard]] bool canDoT() const noexcept { return !read_only_ && canDoC(); }
    [[nodiscard]] bool canDoC() const noexcept {
        return !selection_.empty() && static_cast<bool>(clipboardBackend());
    }
    [[nodiscard]] bool canDoP() const {
        const auto backend = clipboardBackend();
        return !read_only_ && backend && xmlbytestransfer::CBisXML(*backend);
    }
    [[nodiscard]] bool canDoD() const noexcept {
        return !read_only_ && !selection_.empty();
    }
    [[nodiscard]] bool canDoClone() const noexcept { return canDoC() && !read_only_; }
    [[nodiscard]] bool canDoTCPD() const noexcept { return true; }

    /// 現在の選択行から、XML形式のドラッグデータを作る。
    [[nodiscard]] std::optional<::cwxeditor::lib::ClipboardData> beginDrag() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return {};
        enterEditors();
        if(life.expired() || session!=session_revision_)return {};
        if (selection_.empty()) return std::nullopt;
        drag_indices_ = selection_;
        drag_index_ = selection_.front();
        auto node = toNode(std::span<const std::size_t>(drag_indices_));
        if(life.expired() || session!=session_revision_)return {};
        node.newAttr("paneId", id_);
        ::cwxeditor::lib::ClipboardData data;
        data.text = node.text();
        data.formats[String(XML_MIME_TYPE)] = detail::bytesFromText(data.text);
        return data;
    }

    /// 指定の行番号へのドロップを処理する。 バックエンドはポイ
    /// ンター位置から求めた行を渡し、テストでは直接呼び出せる。
    [[nodiscard]] bool handleDrop(::cwxeditor::lib::DragEvent& event,
                                  std::size_t index = static_cast<std::size_t>(-1)) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (read_only_) return false;
        String xml = event.data.text;
        if (xml.empty()) {
            const auto found = event.data.formats.find(String(XML_MIME_TYPE));
            if (found != event.data.formats.end()) xml = detail::textFromBytes(found->second);
        }
        if (xml.empty()) return false;
        try {
            const auto node = XNode::parse(xml, true);
            const auto pane = node.template attr<String>("paneId", false, "");
            const bool samePane = !pane.empty() && pane == id_;
            if constexpr (Sort) {
                if (samePane) return false;
            } else if (samePane && drag_index_ != static_cast<std::size_t>(-1) &&
                       index == drag_index_) {
                return false;
            }
            if (index == static_cast<std::size_t>(-1) || index > list_.size()) index = list_.size();
            const auto old_size=list_.size();
            if (!appendFromNode(node, index, samePane)) return false;
            if(life.expired() || session!=session_revision_)return false;
            if(samePane) {
                // DのdragFinishedは挿入前の添字を削除
                // するため、上方向の移動で無関係な行を消す場合があ
                // る。 この不具合は修正し、挿入後に元の添字をずら
                // してからfinishDragで元の行を削除する。
                const auto added=list_.size()-old_size;
                for(auto& source_index:drag_indices_) if(source_index>=index) source_index+=added;
            }
            const auto preferred = samePane ? ::cwxeditor::lib::DropOperation::Move
                                            : ::cwxeditor::lib::DropOperation::Copy;
            event.selected = preferred;
            event.handled = true;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void finishDrag(::cwxeditor::lib::DropOperation operation) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (!read_only_ && detail::hasOperation(operation, ::cwxeditor::lib::DropOperation::Move) &&
            !drag_indices_.empty()) {
            (void)delImpl(drag_indices_);
            if(life.expired() || session!=session_revision_)return;
        }
        drag_indices_.clear();
        drag_index_ = static_cast<std::size_t>(-1);
    }

    [[nodiscard]] const IndexArray& dragIndices() const noexcept { return drag_indices_; }

protected:
    [[nodiscard]] std::weak_ptr<void> weakLifetime() const noexcept {return lifetime_;}
    void retireCallbacks() noexcept {lifetime_.reset();}
    void storeSingle(std::size_t index) {
        if (index >= list_.size()) return;
        *undo_ += std::make_shared<UndoEdit>(*this, index);
    }

    void storeAll() { *undo_ += std::make_shared<UndoAll>(*this); }

    [[nodiscard]] bool append(const T& value,
                              std::size_t index = static_cast<std::size_t>(-1)) {
        if (read_only_) return false;
        if (index == static_cast<std::size_t>(-1)) index = list_.size();
        insertRaw(index, value);
        refreshTable();
        return true;
    }

    virtual void initColumns(TableView& table) {
        if (table.columns.empty()) {
            table.columns.push_back({});
            table.header_visible = false;
        }
    }
    virtual void initEditors(TableView&) {}
    virtual bool nativeInlineEditing() const noexcept { return false; }
    virtual void initToolBar(ToolBar&) {}
    virtual void initPopupMenu(Menu&) {}

    /// 派生クラスは、表のセル編集をmain/subValueEditEndへ振り分けられる。
    virtual void cellEdit(std::size_t, std::size_t, String) {}

    virtual std::vector<AbstractTableEditPtr> editors() { return {}; }
    virtual AbstractTableEditPtr mainEditor() { return {}; }

    virtual Values createFromNode(const XNode&) { return {}; }
    virtual XNode toNode(std::span<const std::size_t> indices) const {
        auto node = XNode::create(xmlName());
        for (const auto index : indices) {
            if (index < list_.size()) {
                // 汎用基底はTの保存形式を定めない。 XMLを使
                // う前に、派生クラスでこの処理を実装すること。
                (void)index;
            }
        }
        return node;
    }
    virtual String xmlName() const { return {}; }
    virtual ::cwxeditor::lib::ResourceId addIcon() const noexcept { return comm_.addIcon; }
    virtual String addText() const { return comm_.addText; }
    virtual ::cwxeditor::lib::ResourceId delIcon() const noexcept { return comm_.delIcon; }
    virtual String delText() const { return comm_.delText; }
    virtual String upText() const { return comm_.upText; }
    virtual String downText() const { return comm_.downText; }
    // Dは実行中の表の可変添字ではなくTableItemを渡す。
    // 共通実装では、準備済みの行へ書き込んでから一括公開する。
    virtual void updateItem(const T&, ::cwxeditor::lib::TableRow&) = 0;
    virtual bool equals(const T& left, const T& right) const {
        return detail::equalValue(left, right);
    }
    virtual bool isValidValue(const T&) const { return true; }
    virtual std::size_t maxCount() const { return std::numeric_limits<std::size_t>::max(); }
    virtual T createNewValue(std::size_t& index, bool& cancel) const {
        index = static_cast<std::size_t>(-1);
        cancel = false;
        return T{};
    }
    virtual std::vector<String> getWarnings(std::size_t) const { return {}; }

    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::Panel>& rootWidget() const noexcept {
        return root_;
    }

private:
    class UndoEdit final : public undo::Undo {
    public:
        UndoEdit(AbstractEditableListView& owner, std::size_t index)
             : owner_(&owner), life_(owner.weakLifetime()), index_(index), oldValue_(owner.list_.at(index)) {}

        void undo() override { impl(); }
        void redo() override { impl(); }
        void dispose() override {}

    private:
        void impl() {
            if (life_.expired() || !owner_ || index_ >= owner_->list_.size()) return;
            const auto session=owner_->session_revision_;
            owner_->cancelEditors();
            if(life_.expired() || session!=owner_->session_revision_)return;
            std::swap(owner_->list_[index_], oldValue_);
            owner_->refreshTable();
            if(life_.expired() || session!=owner_->session_revision_)return;
            owner_->raiseModifyEvent();
            if(life_.expired() || session!=owner_->session_revision_)return;
            owner_->refreshToolBar();
        }

        AbstractEditableListView* owner_{};
        std::weak_ptr<void> life_;
        std::size_t index_{};
        T oldValue_{};
    };

    class UndoAll final : public undo::Undo {
    public:
        explicit UndoAll(AbstractEditableListView& owner)
             : owner_(&owner), life_(owner.weakLifetime()), oldList_(owner.list_), oldPending_(owner.pending_index_) {}

        void undo() override { impl(); }
        void redo() override { impl(); }
        void dispose() override {}

    private:
        void impl() {
            if(life_.expired() || !owner_)return;
            const auto session=owner_->session_revision_;
            owner_->cancelEditors();
            if(life_.expired() || session!=owner_->session_revision_)return;
            std::swap(owner_->list_, oldList_);
            std::swap(owner_->pending_index_, oldPending_);
            owner_->refreshTable();
            if(life_.expired() || session!=owner_->session_revision_)return;
            owner_->raiseModifyEvent();
            if(life_.expired() || session!=owner_->session_revision_)return;
            owner_->refreshToolBar();
        }

        AbstractEditableListView* owner_{};
        std::weak_ptr<void> life_;
        Values oldList_;
        std::optional<std::size_t> oldPending_;
    };

    void ensureInitialized() {
        if (initialized_) return;
        initialized_ = true;
        const auto life=weakLifetime();
        initColumns(*table_);
        if(life.expired())return;
        if (toolbar_) {
            toolbar_->items = {
                {addText(), "add", addIcon()},
                {delText(), "delete", delIcon()},
            };
            if constexpr (!Sort) {
                toolbar_->items.push_back({upText(), "up", {}});
                toolbar_->items.push_back({downText(), "down", {}});
            }
            toolbar_->on_command = [this,weak=weakLifetime()](StringView command) { if(!weak.expired()) invokeCommand(command); };
            initToolBar(*toolbar_);
        if(life.expired())return;
        }
        context_menu_->items = {
            {comm_.undoText, "undo"},
            {comm_.redoText, "redo"},
            {comm_.copyText, "copy"},
            {comm_.cutText, "cut"},
            {comm_.pasteText, "paste"},
            {delText(), "delete", {}, delIcon()},
            {comm_.selectAllText, "selectAll"},
        };
        if (read_only_) {
            context_menu_->items={{comm_.copyText,"copy"},{comm_.selectAllText,"selectAll"}};
        } else if constexpr (!Sort) {
            context_menu_->items.insert(
                context_menu_->items.begin() + 2,
                {{upText(), "up"}, {downText(), "down"}});
        }
        context_menu_->on_command = [this,weak=weakLifetime()](StringView command) { if(!weak.expired()) invokeCommand(command); };
        context_menu_->on_open = [this,weak=weakLifetime()] {
            if(weak.expired())return;
            for (auto& item : context_menu_->items) {
                if (item.command == "undo") item.enabled = canUndo();
                else if (item.command == "redo") item.enabled = canRedo();
                else if (item.command == "up") item.enabled = canUp();
                else if (item.command == "down") item.enabled = canDown();
                else if (item.command == "copy") item.enabled = canDoC();
                else if (item.command == "cut") item.enabled = canDoT();
                else if (item.command == "paste") item.enabled = canDoP();
                else if (item.command == "delete") item.enabled = canDoD();
                else if (item.command == "selectAll") item.enabled = canSelectAll();
            }
        };
        refreshMenuPresentation();
        if(life.expired())return;
        initPopupMenu(*context_menu_);
        if(life.expired())return;
        initEditors(*table_);
        if(life.expired())return;
        refreshTable();
        if(life.expired())return;
        refreshToolBarEnabledState();
    }

    static ::cwxeditor::cwx::types::MenuID menuID(StringView command) {
        using ID=::cwxeditor::cwx::types::MenuID;
        if(command=="undo")return ID::Undo; if(command=="redo")return ID::Redo;
        if(command=="up")return ID::Up; if(command=="down")return ID::Down;
        if(command=="copy")return ID::Copy; if(command=="cut")return ID::Cut;
        if(command=="delete")return ID::Delete;
        if(command=="paste")return ID::Paste; if(command=="selectAll")return ID::SelectAll;
        return ID::None;
    }
    void refreshMenuPresentation() {
        const auto target=context_menu_;
        if(!target)return;
        const auto life=weakLifetime();
        const auto generation=++menu_revision_;
        const auto source=target->items;
        auto rows=source;
        const auto presentation=comm_.presentation;
        if(presentation.menuText || presentation.formatMenu)
            for(auto& item:rows) {
                const auto id=menuID(item.command);
                if(id!=::cwxeditor::cwx::types::MenuID::None) {
                    item.text=presentation.buildMenu(id);
                    item.icon=presentation.imageMenu(id);
                    if(life.expired() || generation!=menu_revision_)return;
                }
            }
        target->items=std::move(rows);
        ++target->item_update_revision;
    }
    void handleHistoryKey(::cwxeditor::lib::KeyEvent& event) {
        if(event.handled || read_only_)return;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        const auto presentation=comm_.presentation;
        const auto undo_text=presentation.menuText || presentation.formatMenu ?
            presentation.buildMenu(::cwxeditor::cwx::types::MenuID::Undo) : "Undo\tCtrl+Z";
        if(life.expired() || session!=session_revision_)return;
        const auto redo_text=presentation.menuText || presentation.formatMenu ?
            presentation.buildMenu(::cwxeditor::cwx::types::MenuID::Redo) : "Redo\tCtrl+Y";
        if(life.expired() || session!=session_revision_)return;
        const auto match=[&](StringView text) {
            const auto tab=text.find('\t');
            return tab!=StringView::npos && ::cwxeditor::lib::menu_shortcut_matches(text.substr(tab+1),event.key,event.modifiers);
        };
        if(match(undo_text)){(void)undo();event.handled=true;}
        else if(match(redo_text)){(void)redo();event.handled=true;}
    }

    void installCallbacks() {
        table_->on_selection_changed = [this,weak=weakLifetime()](const auto& selection) {
            if(weak.expired())return;
            setSelection(selection);
        };
        table_->on_cell_activate = [this,weak=weakLifetime()](std::size_t row, std::size_t) {
            if(weak.expired())return;
            if (row < list_.size()) setSelection({row});
        };
        table_->on_inline_edit_cancelled = [this,weak=weakLifetime()](std::size_t row, std::size_t) {
            if (!weak.expired() && nativeInlineEditing() && pending_index_ == row)
                exitEdit(true);
        };
        table_->on_cell_edited = [this,weak=weakLifetime()](std::size_t row, std::size_t column, String text) {
            if(weak.expired())return;
            cellEdit(row, column, std::move(text));
        };
        table_->on_key_down = [this,weak=weakLifetime()](::cwxeditor::lib::KeyEvent& event) {
            if(weak.expired())return;
            handleKeyDown(event);
        };
        const auto range_pointer=table_->on_pointer;
        const auto pointer_life=weakLifetime();
        table_->on_pointer = [this,range_pointer,pointer_life](::cwxeditor::lib::PointerEvent& event) {
            if(pointer_life.expired())return;
            if(range_pointer)range_pointer(event);
            if(pointer_life.expired())return;
            if (event.button == ::cwxeditor::lib::PointerButton::Secondary &&
                event.action == ::cwxeditor::lib::PointerAction::Up) {
                if (comm_.services.gui && context_menu_) {
                    comm_.services.gui->show_context_menu(
                        context_menu_, table_, event.position);
                    event.handled = true;
                }
                return;
            }
            if (event.action == ::cwxeditor::lib::PointerAction::DoubleClick &&
                selection_.empty()) {
                event.handled = add();
            }
        };
    }

    void installDragDrop() {
        if (!comm_.services.drag_drop || !table_) return;
        ::cwxeditor::lib::DragSourceBinding source;
        source.source = table_;
        source.allowed = ::cwxeditor::lib::DropOperation::Copy;
        if (!read_only_) source.allowed = source.allowed | ::cwxeditor::lib::DropOperation::Move;
        source.provide_data = [this,weak=weakLifetime()] {
            if(weak.expired())return ::cwxeditor::lib::ClipboardData{};
            const auto data = beginDrag();
            return data.value_or(::cwxeditor::lib::ClipboardData{});
        };
        source.completed = [this,weak=weakLifetime()](::cwxeditor::lib::DropOperation operation) {
            if(weak.expired())return;
            finishDrag(operation);
        };
        comm_.services.drag_drop->bind_source(std::move(source));

        if (read_only_) return;
        ::cwxeditor::lib::DropTargetBinding target;
        target.target = table_;
        target.accepted_formats = {String(XML_MIME_TYPE), "text/xml"};
        target.on_enter = [this,weak=weakLifetime()](::cwxeditor::lib::DragEvent& event) {
            if(weak.expired())return;
            event.selected = read_only_ ? ::cwxeditor::lib::DropOperation::None
                                        : ::cwxeditor::lib::DropOperation::Copy;
        };
        target.on_over = target.on_enter;
        target.on_drop = [this,weak=weakLifetime()](::cwxeditor::lib::DragEvent& event) {
            if(weak.expired())return;
            (void)handleDrop(event, event.target_index.value_or(rowForPoint(event.position)));
        };
        comm_.services.drag_drop->bind_target(std::move(target));
    }

    [[nodiscard]] std::shared_ptr<::cwxeditor::lib::ClipboardBackend> clipboardBackend() const {
        if (comm_.services.clipboard) return comm_.services.clipboard;
        return {};
    }

    [[nodiscard]] ::cwxeditor::lib::ClipboardData copyData() const {
        ::cwxeditor::lib::ClipboardData data;
        if (selection_.empty()) return data;
        auto node = toNode(std::span<const std::size_t>(selection_));
        const bool asText = comm_.xmlCopyProvider ? comm_.xmlCopyProvider() : comm_.xmlCopy;
        return asText ? xmlbytestransfer::makeTextPayload(node.text())
                      : xmlbytestransfer::makeXMLPayload(node.text());
    }

    [[nodiscard]] std::size_t rowForPoint(::cwxeditor::lib::Point point) const {
        const auto model=std::static_pointer_cast<customtable::TableModel>(table_);
        return model ? model->rowAt(point).value_or(list_.size()) : list_.size();
    }

    void invokeCommand(StringView command) {
        if (command == "add") (void)add();
        else if (command == "delete") (void)del();
        else if (command == "up") (void)up();
        else if (command == "down") (void)down();
        else if (command == "undo") (void)undo();
        else if (command == "redo") (void)redo();
        else if (command == "copy") (void)copy();
        else if (command == "cut") (void)cut();
        else if (command == "paste") (void)paste();
        else if (command == "selectAll") selectAll();
    }

    void handleKeyDown(::cwxeditor::lib::KeyEvent& event) {
        using namespace ::cwxeditor::lib;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        const bool control = detail::hasModifier(event.modifiers, Modifiers::Control);
        const bool shift = detail::hasModifier(event.modifiers, Modifiers::Shift);
        handleHistoryKey(event);
        if(life.expired() || session!=session_revision_)return;
        if(event.handled)return;
        if (control && event.key == Key::A) {
            selectAll();
            event.handled = true;
        } else if (control && !shift && event.key == Key::C) {
            // DのLTCPDと同じく、ポップアップだけにある切り取り・コピー・貼り付けにもCtrl+X/C/Vを割り当てる。
            if (canDoC()) event.handled = copy();
        } else if (control && !shift && event.key == Key::X) {
            if (canDoT()) event.handled = cut();
        } else if (control && !shift && event.key == Key::V) {
            if (canDoP()) event.handled = paste();
        } else if (event.key == Key::Delete) event.handled = del();
        else if (control && event.key == Key::Up) event.handled = up();
        else if (control && event.key == Key::Down) event.handled = down();
    }

    void cancelEditors() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (nativeInlineEditing()) {
            const auto gui = comm_.services.gui;
            if (gui) (void)gui->finish_programmatic_inline_edit(table_, false);
            if(life.expired() || session!=session_revision_)return;
            if (pending_index_) exitEdit(true);
            if(life.expired() || session!=session_revision_)return;
        }
        const auto snapshot=editors();
        if(life.expired() || session!=session_revision_)return;
        for(const auto& editor:snapshot) {
            const bool editing=editor && editor->isEditing();
            if(life.expired() || session!=session_revision_)return;
            if(editing)editor->cancel();
            if(life.expired() || session!=session_revision_)return;
        }
    }
    void enterEditors() {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        if (nativeInlineEditing()) {
            const auto gui = comm_.services.gui;
            if (gui) (void)gui->finish_programmatic_inline_edit(table_, true);
            if(life.expired() || session!=session_revision_)return;
        }
        const auto snapshot=editors();
        if(life.expired() || session!=session_revision_)return;
        for(const auto& editor:snapshot) {
            const bool editing=editor && editor->isEditing();
            if(life.expired() || session!=session_revision_)return;
            if(editing)editor->enter();
            if(life.expired() || session!=session_revision_)return;
        }
    }

    void refreshToolBar() const {
        const auto life=weakLifetime();
        refreshToolBarEnabledState();
        if(life.expired())return;
        const auto callback=comm_.refreshToolBar;
        if(callback)callback();
    }

    void refreshToolBarEnabledState() const {
        if(!toolbar_)return;
        const auto life=weakLifetime();
        const auto session=session_revision_;
        const bool can_add=!read_only_ && list_.size()<maxCount();
        if(life.expired() || session!=session_revision_)return;
        const bool can_delete=canDoD();
        if(life.expired() || session!=session_revision_)return;
        const bool can_up=canUp();
        const bool can_down=canDown();
        auto items=toolbar_->items;
        for(auto& item:items) {
            if(item.command=="add") item.enabled=can_add;
            else if(item.command=="delete") item.enabled=can_delete;
            else if(item.command=="up") item.enabled=can_up;
            else if(item.command=="down") item.enabled=can_down;
        }
        if(life.expired() || session!=session_revision_)return;
        const auto toolbar=toolbar_;
        (void)::cwxeditor::lib::replace_tool_bar_items(toolbar,std::move(items),
            [this,life,session] {
                return !life.expired() && session==session_revision_;
            });
    }

    bool refreshTable(std::optional<std::size_t> changed = {}, bool resetTop = false,
                      std::optional<std::size_t> reveal = {}) {
        if (!table_) return false;
        const auto target = std::static_pointer_cast<
            ::cwxeditor::cwx::editor::gui::dwt::customtable::TableModel>(table_);
        const auto life=weakLifetime();
        const auto generation = ++render_revision_;
        const auto row_order = target->row_order_revision;
        const auto source = list_;
        const bool single = changed && *changed < source.size() &&
                            target->rows.size() == source.size();
        auto rows = single ? target->rows : std::vector<::cwxeditor::lib::TableRow>(source.size());
        auto states = single ? target->rowStates() :
            std::vector<::cwxeditor::cwx::editor::gui::dwt::customtable::TableRowState>(source.size());
        const auto first = single ? *changed : 0U;
        const auto end = single ? first+1U : source.size();
        for (auto index = first; index < end; ++index) {
            rows[index] = {};
            states[index] = {};
            updateItem(source[index], rows[index]);
            if(life.expired())return false;
            rows[index].warnings=getWarnings(index);
            if(life.expired())return false;
            if (generation != render_revision_ || row_order != target->row_order_revision) return false;
        }
        if (source != list_) return false;
        return target->replaceRows(std::move(rows), selection_, std::move(states), reveal, [&] {
            return !life.expired() && generation==render_revision_ && source==list_;
        }, resetTop);
    }

    void insertRaw(std::size_t index, const T& value) {
        index = std::min(index, list_.size());
        list_.insert(list_.begin() + static_cast<std::ptrdiff_t>(index), value);
        if (pending_index_ && *pending_index_ >= index) ++*pending_index_;
        for (auto& selected : selection_)
            if (selected >= index) ++selected;
    }

    void eraseRaw(std::size_t index) {
        if (index >= list_.size()) return;
        list_.erase(list_.begin() + static_cast<std::ptrdiff_t>(index));
        if (pending_index_) {
            if (*pending_index_ == index) pending_index_.reset();
            else if (*pending_index_ > index) --*pending_index_;
        }
        // 削除行を外してから残る添字を調整する。 順
        // 序を逆にすると、直後の選択行まで失われる。
        selection_.erase(std::remove(selection_.begin(), selection_.end(), index),
                         selection_.end());
        for (auto& selected : selection_)
            if (selected > index) --selected;
    }

    void sortValues(IndexArray selectedIndices) {
        if constexpr (!Sort) {
            (void)selectedIndices;
        } else {
            Values selectedValues;
            for (const auto index : selectedIndices)
                if (index < list_.size()) selectedValues.push_back(list_[index]);
            std::sort(list_.begin(), list_.end());
            selection_.clear();
            for (std::size_t index = 0; index < list_.size(); ++index) {
                if (std::find_if(selectedValues.begin(), selectedValues.end(),
                                 [this, index](const T& value) {
                                     return equals(list_[index], value);
                                 }) != selectedValues.end()) {
                    selection_.push_back(index);
                }
            }
        }
    }

    [[nodiscard]] bool appendValuesImpl(Values values, std::size_t index, bool force) {
        const auto life=weakLifetime();
        const auto session=session_revision_;
        ensureInitialized();
        if(life.expired() || session!=session_revision_)return false;
        if (read_only_ && !force) return false;
        if (!force && list_.size() >= maxCount()) return false;

        if (!force && !can_duplicate_) {
            Values filtered;
            filtered.reserve(values.size());
            for (const auto& value : values) {
                if (!isValidValue(value)) continue;
                bool duplicate = false;
                for (const auto& existing : list_)
                    if (equals(existing, value)) {
                        duplicate = true;
                        break;
                    }
                if (!duplicate) {
                    for (const auto& accepted : filtered)
                        if (equals(accepted, value)) {
                            duplicate = true;
                            break;
                        }
                }
                if (!duplicate) filtered.push_back(value);
            }
            values = std::move(filtered);
        }

        if (!force) {
            const auto available = maxCount() - list_.size();
            if (values.size() > available) values.resize(available);
        }
        if (values.empty()) return false;

        enterEditors();
        if(life.expired() || session!=session_revision_)return false;
        if (index == static_cast<std::size_t>(-1)) index = list_.size();
        index = std::min(index, list_.size());
        storeAll();
        list_.insert(list_.begin() + static_cast<std::ptrdiff_t>(index),
                     values.begin(), values.end());
        if (pending_index_ && *pending_index_ >= index) *pending_index_ += values.size();
        IndexArray added;
        added.reserve(values.size());
        for (std::size_t offset = 0; offset < values.size(); ++offset)
            added.push_back(index + offset);
        if constexpr (Sort) {
            sortValues(added);
        } else {
            selection_ = added;
        }
        refreshTable({}, false, selection_.front());
        if(life.expired() || session!=session_revision_)return false;
        setSelection(selection_);
        if(life.expired() || session!=session_revision_)return false;
        raiseModifyEvent();
        if(life.expired() || session!=session_revision_)return false;
        refreshToolBar();
        if(life.expired() || session!=session_revision_)return false;
        return true;
    }

    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    ::cwxeditor::lib::Action disconnect_undo_,disconnect_menu_;
    Commons comm_;
    Parent parent_{};
    String id_;
    bool read_only_{};
    bool can_duplicate_{true};
    bool enabled_{true};
    bool initialized_{};
    std::uint64_t render_revision_{};
    std::uint64_t session_revision_{};
    std::uint64_t menu_revision_{};
    std::optional<std::size_t> pending_index_;
    std::uint64_t pending_revision_{};
    Values list_;
    IndexArray selection_;
    IndexArray drag_indices_;
    std::size_t drag_index_{static_cast<std::size_t>(-1)};
    std::shared_ptr<::cwxeditor::lib::Panel> root_;
    std::shared_ptr<TableView> table_;
    std::uint64_t table_selection_connection_{};
    std::shared_ptr<ToolBar> toolbar_;
    std::shared_ptr<Menu> context_menu_;
    std::shared_ptr<undo::UndoManager> undo_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::editablelistview
