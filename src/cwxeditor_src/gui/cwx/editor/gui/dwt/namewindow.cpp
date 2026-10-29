#include "cwxeditor_src/gui/cwx/editor/gui/dwt/namewindow.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"

#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_scroll.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::namewindow {

namespace {

[[nodiscard]] String lowerAscii(const StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] int compareNatural(const StringView left, const StringView right,
                                 const bool logical_sort) {
    if (!logical_sort) {
        const auto lhs = lowerAscii(left);
        const auto rhs = lowerAscii(right);
        return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
    }

    std::size_t lhs_pos = 0U;
    std::size_t rhs_pos = 0U;
    while (lhs_pos < left.size() && rhs_pos < right.size()) {
        const bool lhs_digit = std::isdigit(static_cast<unsigned char>(left[lhs_pos])) != 0;
        const bool rhs_digit = std::isdigit(static_cast<unsigned char>(right[rhs_pos])) != 0;
        if (lhs_digit && rhs_digit) {
            const auto lhs_end = left.find_first_not_of("0123456789", lhs_pos);
            const auto rhs_end = right.find_first_not_of("0123456789", rhs_pos);
            const auto lhs_stop = lhs_end == StringView::npos ? left.size() : lhs_end;
            const auto rhs_stop = rhs_end == StringView::npos ? right.size() : rhs_end;
            auto lhs_first = left.substr(lhs_pos, lhs_stop - lhs_pos).find_first_not_of('0');
            auto rhs_first = right.substr(rhs_pos, rhs_stop - rhs_pos).find_first_not_of('0');
            if (lhs_first == StringView::npos) lhs_first = lhs_stop - lhs_pos - 1U;
            if (rhs_first == StringView::npos) rhs_first = rhs_stop - rhs_pos - 1U;
            const auto lhs_digits_count = lhs_stop - lhs_pos - lhs_first;
            const auto rhs_digits_count = rhs_stop - rhs_pos - rhs_first;
            if (lhs_digits_count != rhs_digits_count)
                return lhs_digits_count < rhs_digits_count ? -1 : 1;
            const auto lhs_number = left.substr(lhs_pos + lhs_first, lhs_digits_count);
            const auto rhs_number = right.substr(rhs_pos + rhs_first, rhs_digits_count);
            if (lhs_number != rhs_number) return lhs_number < rhs_number ? -1 : 1;
            lhs_pos = lhs_stop;
            rhs_pos = rhs_stop;
            continue;
        }

        const auto lhs_char = static_cast<char>(std::tolower(
            static_cast<unsigned char>(left[lhs_pos])));
        const auto rhs_char = static_cast<char>(std::tolower(
            static_cast<unsigned char>(right[rhs_pos])));
        if (lhs_char != rhs_char) return lhs_char < rhs_char ? -1 : 1;
        ++lhs_pos;
        ++rhs_pos;
    }
    if (lhs_pos != left.size() || rhs_pos != right.size())
        return lhs_pos == left.size() ? -1 : 1;
    return 0;
}

[[nodiscard]] String replaceToken(String value, const StringView token,
                                   const StringView replacement) {
    std::size_t position = 0U;
    while ((position = value.find(token, position)) != String::npos) {
        value.replace(position, token.size(), replacement);
        position += replacement.size();
    }
    return value;
}

[[nodiscard]] String statusFormat(const StringView format, const StringView type,
                                  const std::size_t count) {
    auto result = replaceToken(String(format), "%1$s", type);
    result = replaceToken(std::move(result), "%2$s", std::to_string(count));
    result = replaceToken(std::move(result), "%s", type);
    return result;
}

[[nodiscard]] String joinLines(const std::vector<String>& values) {
    String result;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) result.push_back('\n');
        result += values[index];
    }
    return result;
}

[[nodiscard]] bool contains(const std::vector<String>& values, const StringView value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

struct Candidate {
    String name;
    std::uint32_t count{};
};

}  // namespace

class NameViewBase::RenameUndo final : public undo::Undo {
public:
    RenameUndo(NameViewBase& owner, String old_name, String new_name,
               std::optional<RenameReplay> replay = {})
        : owner_(owner), old_name_(std::move(old_name)), new_name_(std::move(new_name)),
          replay_(std::move(replay)), lifetime_(owner.callback_lifetime_) {}

    void undo() override { apply(false); }
    void redo() override { apply(true); }
    void dispose() override {}

private:
    NameViewBase& owner_;
    String old_name_;
    String new_name_;
    std::optional<RenameReplay> replay_;
    std::weak_ptr<int> lifetime_;
    void apply(bool forward) {
        if (lifetime_.expired()) return;
        if (!replay_) {
            (void)owner_.applyRename(forward ? old_name_ : new_name_,
                                     forward ? new_name_ : old_name_);
            return;
        }
        // モデルの通知が、ビューと履歴を同期的に破棄する場合がある。
        const auto action = forward ? replay_->redo : replay_->undo;
        const auto life = lifetime_;
        auto* owner = &owner_;
        if (action) action();
        if (!life.expired()) owner->refreshRename();
    }
};

class NameViewBase::RenameBatchUndo final : public undo::Undo {
public:
    RenameBatchUndo(NameViewBase& owner,
                    std::vector<std::pair<String, String>> replacements)
        : owner_(owner), replacements_(std::move(replacements)) {}

    void undo() override {
        std::vector<std::pair<String, String>> reversed;
        reversed.reserve(replacements_.size());
        for (auto it = replacements_.rbegin(); it != replacements_.rend(); ++it)
            reversed.emplace_back(it->second, it->first);
        (void)owner_.renameBatch(reversed, false);
    }

    void redo() override { (void)owner_.renameBatch(replacements_, false); }
    void dispose() override {}

private:
    NameViewBase& owner_;
    std::vector<std::pair<String, String>> replacements_;
};

NameViewBase::NameViewBase(Commons comm, const NameIDKind id_kind,
                           String type_name, const ResourceId image,
                           const Shell parent_shell, const Parent parent,
                           undo::UndoManager* const undo_manager,
                           const bool read_only)
    : comm_(std::move(comm)), id_kind_(id_kind),
      type_name_(std::move(type_name)), image_(image),
      parent_shell_(parent_shell), parent_(parent), read_only_(read_only),
      owned_undo_(undo_manager ? nullptr
                               : std::make_shared<undo::UndoManager>(comm_.undoMaxEvent)),
      undo_(undo_manager ? undo_manager : owned_undo_.get()) {
    installFallbackSource();
    if (comm_.loadSort) {
        const auto [column, direction] = comm_.loadSort(id_kind_);
        sort_column_ = column;
        sort_direction_ = direction;
    }
    if (parent_) reconstruct(parent_);
}

NameViewBase::~NameViewBase() {
    callback_lifetime_.reset();
    if (list_) {
        if (selection_listener_ != 0U) list_->removeSelectionListener(selection_listener_);
        if (edit_listener_ != 0U) list_->removeEditListener(edit_listener_);
        list_->on_cell_activate = {};
        list_->on_cell_edited = {};
        list_->on_column_resized = {};
        if (pointer_listener_ != 0U) list_->removePointerListener(pointer_listener_);
    }
    if (name_column_ && name_column_listener_ != 0U)
        name_column_->removeSelectionListener(name_column_listener_);
    if (use_count_column_ && use_count_column_listener_ != 0U)
        use_count_column_->removeSelectionListener(use_count_column_listener_);
    inc_search_.reset();
    active_editor_.reset();
    if (context_menu_) context_menu_->on_command = {};
    context_menu_.reset();
}

void NameViewBase::installFallbackSource() {
    fallback_source_ = comm_.source;
    if (fallback_source_.keys) local_keys_ = fallback_source_.keys();
    if (!fallback_source_.keys) {
        fallback_source_.keys = [this] { return local_keys_; };
    }
    if (!fallback_source_.count) {
        fallback_source_.count = [this](const StringView value) {
            return contains(local_keys_, value) ? 1U : 0U;
        };
    }
    if (!fallback_source_.rename) {
        fallback_source_.rename = [this](const StringView old_name,
                                         const StringView new_name) {
            if (old_name == new_name || contains(local_keys_, new_name)) return false;
            const auto found = std::find(local_keys_.begin(), local_keys_.end(), old_name);
            if (found == local_keys_.end()) return false;
            *found = String(new_name);
            return true;
        };
    }
    source_ = fallback_source_;
}

void NameViewBase::setSource(NameSource source) {
    if (!source.keys) source.keys = fallback_source_.keys;
    if (!source.count) source.count = fallback_source_.count;
    if (!source.rename) source.rename = fallback_source_.rename;
    if (!source.convertCoupon) source.convertCoupon = fallback_source_.convertCoupon;
    if (!source.convertKeyCode) source.convertKeyCode = fallback_source_.convertKeyCode;
    source_ = std::move(source);
    updateList();
}

void NameViewBase::reconstruct(const Parent parent) {
    if (list_) return;
    parent_ = parent;
    widget_ = std::make_shared<lib::Panel>();
    // D版は一覧にGridData.FILL_BOTHを設定し、NameWindowのペイン全体を
    // 使用する。既定レイアウトの推奨高に任せると一覧が上端の固定高に
    // なり、左上ペインの残りが不要な空白になるため、単一の一覧を常に
    // 全領域へ広げる。
    widget_->set_layout(std::make_shared<lib::FillLayout>());
    if (parent_) parent_->add(widget_);

    list_ = customtable::rangeSelectableTable(widget_.get(),
                                               customtable::STYLE_MULTI_SELECT |
                                                   customtable::STYLE_HEADER,
                                               comm_.services);
    list_->setHeaderVisible(true);
    if (comm_.loadColumnWidths) {
        const auto [name_width, count_width] = comm_.loadColumnWidths();
        comm_.nameColumnWidth = name_width;
        comm_.useCountColumnWidth = count_width;
    }
    name_column_ = list_->addColumn(comm_.messages.idName,
                                    std::max(1.0F, comm_.nameColumnWidth));
    use_count_column_ = list_->addColumn(comm_.messages.idCount,
                                         std::max(1.0F, comm_.useCountColumnWidth));
    installTableListeners();

    const std::weak_ptr<int> lifetime=callback_lifetime_;
    inc_search_ = std::make_shared<incsearch::IncSearch>(
        comm_.services, list_.get(), [this,lifetime] {
            return !lifetime.expired() && canIncSearch();
        });
    inc_search_->modEvent.push_back([this,lifetime] {
        if (!lifetime.expired()) updateList();
    });
    updateList();
}

void NameViewBase::installTableListeners() {
    if (!list_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    if (comm_.storeColumnWidth) {
        list_->on_column_resized = [this, lifetime](std::size_t column, float width) {
            if (lifetime.expired() || !std::isfinite(width) || width < 0.0F) return;
            if (name_column_ && column == name_column_->index())
                comm_.nameColumnWidth = width;
            else if (use_count_column_ && column == use_count_column_->index())
                comm_.useCountColumnWidth = width;
            else return;
            const auto store = comm_.storeColumnWidth;
            store(column, width);
        };
    }
    selection_listener_ = list_->addSelectionListener([this,lifetime](const auto&) {
        if (lifetime.expired()) return;
        if (updating_) return;
        refreshStatusLine();
        if (lifetime.expired()) return;
        const auto notify=comm_.refreshToolBar;
        if (notify) notify();
    });
    edit_listener_ = list_->addEditListener(
        [this,lifetime](const std::size_t row, const std::size_t column, String text) {
            if (lifetime.expired()) return;
            editEnd(row, column, std::move(text));
        });
    list_->on_cell_activate = [this,lifetime](const std::size_t row, const std::size_t column) {
        if (lifetime.expired()) return;
        if (column == 0U && row < list_->rows.size()) {
            const auto editor = createEditor(row, column);
            if (lifetime.expired()) return;
            active_editor_ = editor;
            if (lib::editor_window_host_options().all_mode && active_editor_ && widget_) widget_->add(active_editor_);
        }
    };
    pointer_listener_ = list_->addPointerListener([this,lifetime](lib::PointerEvent& event) {
        if (lifetime.expired() || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up) return;
        // SWT と同様、ポップアップを開く前に指している名前の行を選ぶ。
        // 空白上では元の選択を維持し、コピー・検索を以前の行に対して行う。
        if (event.item_hit_test_available && event.item_index >= 0 &&
            static_cast<std::size_t>(event.item_index) < list_->rows.size()) {
            list_->setSelection({static_cast<std::size_t>(event.item_index)});
        }
        rebuildContextMenu();
        if (context_menu_ && comm_.services.gui)
            comm_.services.gui->show_context_menu(context_menu_, list_, event.position);
        event.handled = true;
    });
    if (!read_only_) {
        // D版の TableTextEdit と同様、選択済みの
        // 名前を押すと、 ダブルクリックと同じ編集欄で編集する。
        ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedAction(
            list_, comm_.services,
            [this, lifetime](const std::size_t row) {
                if (lifetime.expired() || !list_ || row >= list_->rows.size()) return;
                const auto editor = createEditor(row, 0U);
                if (lifetime.expired()) return;
                active_editor_ = editor;
                if (lib::editor_window_host_options().all_mode && active_editor_ && widget_) widget_->add(active_editor_);
            },
            [this, lifetime] {
                return !lifetime.expired() && comm_.quickEditProvider &&
                       comm_.quickEditProvider();
            });
    }
    if (name_column_) {
        name_column_listener_ = name_column_->addSelectionListener(
            [this,lifetime] { if(!lifetime.expired()) toggleSort(SortColumn::Name); });
    }
    if (use_count_column_) {
        use_count_column_listener_ = use_count_column_->addSelectionListener(
            [this,lifetime] { if(!lifetime.expired()) toggleSort(SortColumn::UseCount); });
    }
}

void NameViewBase::rebuildContextMenu() {
    if (context_menu_provider_) {
        context_menu_ = context_menu_provider_();
        return;
    }
    if (!context_menu_) {
        context_menu_ = std::make_shared<lib::Menu>(true);
        context_menu_->on_command = [this](const StringView command) {
            if (command == "name.inc-search") incSearch();
            else if (command == "name.edit") (void)editName();
            else if (command == "name.undo") undo();
            else if (command == "name.redo") redo();
            else if (command == "name.copy") copy();
            else if (command == "name.select-all") selectAll();
            else if (command == "name.find-id") findID();
        };
    }
    const auto item = [this](const MenuID id, StringView fallback, String command, const bool enabled) {
        lib::MenuItem value;
        value.text = comm_.localizedMenuText(id, fallback);
        value.command = std::move(command);
        value.icon = comm_.localizedMenuImage(id);
        value.enabled = enabled;
        return value;
    };
    const auto separator = [] {
        lib::MenuItem value;
        value.separator = true;
        return value;
    };
    std::vector<lib::MenuItem> items;
    items.push_back(item(MenuID::IncSearch, "インクリメンタル検索", "name.inc-search", canIncSearch()));
    if (!read_only_) items.push_back(item(MenuID::EditProp, "名前を編集", "name.edit", canEditName()));
    items.push_back(separator());
    items.push_back(item(MenuID::Undo, "元に戻す", "name.undo", canUndo()));
    items.push_back(item(MenuID::Redo, "やり直す", "name.redo", canRedo()));
    items.push_back(separator());
    items.push_back(item(MenuID::CopyAsText, "テキストとしてコピー", "name.copy", canDoC()));
    items.push_back(item(MenuID::SelectAll, "すべて選択", "name.select-all", canSelectAll()));
    items.push_back(separator());
    items.push_back(item(MenuID::FindID, "IDを検索", "name.find-id", canFindID()));
    context_menu_->items = std::move(items);
    ++context_menu_->item_update_revision;
    ++context_menu_->item_order_revision;
}

void NameViewBase::updateList() {
    if (!list_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=++publication_revision_;
    const auto target=list_;
    const auto source=source_;
    const auto source_owner=summary_owner_;
    const auto search=inc_search_;
    const auto sort_column=sort_column_;
    const auto sort_direction=sort_direction_;
    const auto logical_sort = comm_.logicalSortProvider ? comm_.logicalSortProvider() : comm_.logicalSort;
    const auto image=image_;
    const auto selection=target->selection;
    const auto row_revision=target->row_order_revision;
    const auto update_revision=target->row_update_revision;
    const auto alive=[this,lifetime,revision] {
        return !lifetime.expired() && publication_revision_==revision;
    };
    const auto current=[&] {
        return alive() && list_==target && summary_owner_==source_owner &&
            sort_column_==sort_column && sort_direction_==sort_direction &&
            target->selection==selection && target->row_order_revision==row_revision;
    };
    struct Guard {
        std::weak_ptr<int> lifetime;
        bool* value;
        bool previous;
        ~Guard() { if(!lifetime.expired()) *value=previous; }
    } guard{lifetime,&updating_,updating_};
    updating_=true;

    const auto old_selection = selectedStrings();
    const auto keys = source.keys ? source.keys() : std::vector<String>{};
    if(!current() || target->row_update_revision!=update_revision) return;

    std::vector<Candidate> candidates;
    candidates.reserve(keys.size());
    for (const auto& key : keys) {
        if (search && !search->match(key)) continue;
        const auto count=source.count ? source.count(key) : 0U;
        if(!current() || target->row_update_revision!=update_revision) return;
        candidates.push_back({key,count});
    }

    std::stable_sort(candidates.begin(), candidates.end(), [=](const Candidate& left,
                                                                   const Candidate& right) {
        int result = 0;
        if (sort_column == SortColumn::UseCount) {
            if (left.count != right.count) result = left.count < right.count ? -1 : 1;
            else result = compareNatural(left.name, right.name, logical_sort);
        } else {
            result = compareNatural(left.name, right.name, logical_sort);
        }
        return sort_direction == SortDirection::Down ? result > 0 : result < 0;
    });

    std::vector<String> names;
    std::vector<lib::TableRow> rows;
    names.reserve(candidates.size());
    rows.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        names.push_back(candidate.name);
        lib::TableRow row;
        row.cells.push_back({candidate.name, {}, image});
        row.cells.push_back({std::to_string(candidate.count), {}, 0U});
        rows.push_back(std::move(row));
    }

    std::vector<std::size_t> new_selection;
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (contains(old_selection, names[index])) new_selection.push_back(index);
    }
    // 選択コールバックや、構築途中の名前と行の対応表を外へ見せない。
    if(!target->replaceRows(std::move(rows), std::move(new_selection), {}, {}, current)) return;
    name_list_.swap(names);
    has_name_ = !keys.empty();
    target->highlighted_column = sort_column == SortColumn::Name ? 0U : 1U;
    target->redraw();
    if(!alive()) return;
    refreshStatusLine();
}

void NameViewBase::setSelectionSilently(std::vector<std::size_t> values) {
    if (!list_) return;
    list_->setSelection(std::move(values), false);
}

void NameViewBase::refreshStatusLine() {
    if (!list_) return;
    const auto count = list_->rows.size();
    const auto selected = list_->selection.size();
    status_line_ = statusFormat(comm_.messages.idStatus, type_name_, count);
    if (selected != 0U)
        status_line_ = statusFormat(comm_.messages.idStatusSelected, status_line_, selected);
    const auto notify=comm_.setStatusLine;
    const auto widget=widget_;
    const auto status=status_line_;
    if (notify) notify(widget.get(), status);
}

void NameViewBase::incSearch() {
    const auto table = list_;
    const auto search = inc_search_;
    const auto summary_owner = summary_owner_;
    const auto publication = publication_revision_;
    const std::weak_ptr<int> life = callback_lifetime_;
    if (!table || !search || !canIncSearch()) return;
    const auto services = comm_.services;
    if (services.gui) dutils::forceFocus(table, true, services);
    if (life.expired() || list_ != table || inc_search_ != search ||
        summary_owner_ != summary_owner || publication_revision_ != publication ||
        !canIncSearch()) return;
    search->startIncSearch();
}

bool NameViewBase::canEditName() const noexcept {
    return !read_only_ && list_ && !list_->selection.empty();
}

std::shared_ptr<lib::TextEdit> NameViewBase::createEditor(const std::size_t row,
                                                           const std::size_t column) {
    if (!list_ || row >= list_->rows.size() || column >= list_->rows[row].cells.size())
        return {};
    if (!lib::editor_window_host_options().all_mode && !read_only_ && comm_.services.gui) {
        const auto gui = comm_.services.gui;
        const auto target = list_;
        const std::weak_ptr<int> life = callback_lifetime_;
        const auto revision = target->row_order_revision;
        if (gui->programmatic_inline_edit_active(target))
            (void)gui->finish_programmatic_inline_edit(target, true);
        if (life.expired() || target != list_ || target->row_order_revision != revision)
            return {};
        const std::weak_ptr<customtable::TableModel> weak_table = target;
        auto created = std::make_shared<std::weak_ptr<lib::TextEdit>>();
        lib::ProgrammaticInlineEditRequest request;
        request.generic_owner = target;
        request.generic_text = target->rows[row].cells[column].text;
        request.row_height = lib::table_row_height(*target, 0.0F);
        request.generic_bounds = [weak_table, revision, row, column]() -> std::optional<lib::Rect> {
            const auto table = weak_table.lock();
            if (!table || table->row_order_revision != revision || row >= table->rows.size() ||
                row < table->top_index || column >= table->columns.size()) return {};
            const auto viewport = lib::item_scroll_geometry(*table).viewport;
            auto rect = lib::table_row_geometry(*table, row, 0.0F);
            const auto widths = lib::resolved_table_column_widths(*table, viewport.width);
            float x = table->bounds().x - table->horizontal_offset;
            for (std::size_t i = 0; i < column; ++i) x += widths[i];
            const float right = std::min(viewport.x + viewport.width, x + widths[column]);
            rect.x = std::max(viewport.x, x);
            rect.width = right - rect.x;
            if (rect.width <= 0 || rect.y < viewport.y ||
                rect.y + rect.height > viewport.y + viewport.height) return {};
            return rect;
        };
        request.generic_commit = [this, life, weak_table, revision, row, column, created](String text) {
            const auto table = weak_table.lock();
            if (life.expired()) return false;
            if (!table || table != list_ || table->row_order_revision != revision || read_only_) {
                if (active_editor_ == created->lock()) active_editor_.reset();
                return false;
            }
            editEnd(row, column, std::move(text));
            if (!life.expired() && active_editor_ == created->lock()) active_editor_.reset();
            return true;
        };
        request.generic_cancelled = [this, life, created] {
            if (!life.expired() && active_editor_ == created->lock()) active_editor_.reset();
        };
        request.generic_editor_created = [this, life, created, weak_table,
                                           weak_gui = std::weak_ptr<lib::GuiBackend>(gui)](
            const std::shared_ptr<lib::TextEdit>& editor, const std::shared_ptr<lib::ComboBox>&) {
            if (life.expired()) return;
            *created = editor;
            active_editor_ = editor;
            // コピー操作は、先に表示側が管理する編集処理を完了する。
            editor->on_enter = [life, weak_table, weak_gui] {
                if (life.expired()) return;
                const auto backend = weak_gui.lock();
                const auto table = weak_table.lock();
                if (backend && table) (void)backend->finish_programmatic_inline_edit(table, true);
            };
        };
        const bool started = gui->request_programmatic_inline_edit(request);
        if (life.expired() || target != list_ || target->row_order_revision != revision) return {};
        if (started) {
            const auto editor = created->lock();
            attachEditorTextMenu(editor);
            return editor;
        }
        // 画面なしのバックエンドが表示を拒否する場合がある。 呼び出し元用の切り離された
        // データアダプターは維持するが、ペインの FillLayout には入れない。
    }
    auto editor = std::make_shared<lib::TextEdit>(list_->rows[row].cells[column].text);
    editor->read_only = read_only_;
    editor->selection_start = 0U;
    editor->selection_length = editor->text.size();
    const std::weak_ptr<lib::TextEdit> weak = editor;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto row_revision=list_->row_order_revision;
    const std::weak_ptr<customtable::TableModel> table=list_;
    editor->on_enter = [this, row, column, weak,lifetime,table,row_revision] {
        if(lifetime.expired()) return;
        const auto target=table.lock();
        if(!target || target!=list_ || target->row_order_revision!=row_revision) return;
        if (const auto value = weak.lock()) editEnd(row, column, value->text);
    };
    attachEditorTextMenu(editor);
    return editor;
}

void NameViewBase::attachEditorTextMenu(const std::shared_ptr<lib::TextEdit>& editor) {
    if (!editor) return;
    dmenu::Commons text_comm;
    text_comm.services = comm_.services;
    text_comm.refreshToolBar = comm_.refreshToolBar;
    auto binding = dmenu::createTextMenu(text_comm, dmenu::TMM(editor));
    if (!binding) return;
    auto menu = binding->menu();
    if (!menu) return;
    const auto item = [this](const MenuID id, const StringView fallback,
                             const String command) {
        lib::MenuItem value;
        value.text = comm_.localizedMenuText(id, fallback);
        value.command = command;
        value.icon = comm_.localizedMenuImage(id);
        return value;
    };
    const auto separator = [] {
        lib::MenuItem value;
        value.separator = true;
        return value;
    };
    if (id_kind_ == NameIDKind::Coupon) {
        menu->items.push_back(separator());
        auto convert = item(MenuID::ConvertCouponType, "クーポン種別を変換",
                            "name.inline.coupon.convert");
        convert.children = {
            item(MenuID::ConvertCouponTypeNormal, "通常クーポンへ変換", "name.inline.coupon.convert.normal"),
            item(MenuID::ConvertCouponTypeHide, "隠蔽クーポンへ変換", "name.inline.coupon.convert.hide"),
            item(MenuID::ConvertCouponTypeDur, "時限クーポンへ変換", "name.inline.coupon.convert.duration"),
            item(MenuID::ConvertCouponTypeDurBattle, "戦闘時限クーポンへ変換", "name.inline.coupon.convert.battle"),
            item(MenuID::ConvertCouponTypeSystem, "システムクーポンへ変換", "name.inline.coupon.convert.system")};
        menu->items.push_back(std::move(convert));
        auto copy = item(MenuID::CopyTypeConvertedCoupon, "変換してコピー",
                         "name.inline.coupon.copy");
        copy.children = {
            item(MenuID::CopyTypeConvertedCouponNormal, "通常クーポンとしてコピー", "name.inline.coupon.copy.normal"),
            item(MenuID::CopyTypeConvertedCouponHide, "隠蔽クーポンとしてコピー", "name.inline.coupon.copy.hide"),
            item(MenuID::CopyTypeConvertedCouponDur, "時限クーポンとしてコピー", "name.inline.coupon.copy.duration"),
            item(MenuID::CopyTypeConvertedCouponDurBattle, "戦闘時限クーポンとしてコピー", "name.inline.coupon.copy.battle"),
            item(MenuID::CopyTypeConvertedCouponSystem, "システムクーポンとしてコピー", "name.inline.coupon.copy.system")};
        menu->items.push_back(std::move(copy));
    } else if (id_kind_ == NameIDKind::KeyCode) {
        menu->items.push_back(separator());
        auto copy = item(MenuID::CopyTimingConvertedKeyCode, "時期を変換してコピー",
                         "name.inline.keycode.copy");
        copy.children = {
            item(MenuID::CopyTimingConvertedKeyCodeUse, "使用時キーコードとしてコピー", "name.inline.keycode.copy.use"),
            item(MenuID::CopyTimingConvertedKeyCodeSuccess, "成功時キーコードとしてコピー", "name.inline.keycode.copy.success"),
            item(MenuID::CopyTimingConvertedKeyCodeFailure, "失敗時キーコードとしてコピー", "name.inline.keycode.copy.failure"),
            item(MenuID::CopyTimingConvertedKeyCodeHasNot, "所持無しキーコードとしてコピー", "name.inline.keycode.copy.has-not")};
        menu->items.push_back(std::move(copy));
    }
    const auto previous = menu->on_command;
    const auto lifetime = std::weak_ptr<int>(callback_lifetime_);
    const std::weak_ptr<lib::TextEdit> weak_editor = editor;
    menu->on_command = [this, lifetime, weak_editor, previous](const StringView command) {
        if (previous) previous(command);
        if (lifetime.expired()) return;
        const auto editor = weak_editor.lock();
        if (!editor || editor->text.empty()) return;
        std::optional<CouponType> coupon_type;
        if (command == "name.inline.coupon.convert.normal" || command == "name.inline.coupon.copy.normal") coupon_type = CouponType::Normal;
        else if (command == "name.inline.coupon.convert.hide" || command == "name.inline.coupon.copy.hide") coupon_type = CouponType::Hide;
        else if (command == "name.inline.coupon.convert.duration" || command == "name.inline.coupon.copy.duration") coupon_type = CouponType::Dur;
        else if (command == "name.inline.coupon.convert.battle" || command == "name.inline.coupon.copy.battle") coupon_type = CouponType::DurBattle;
        else if (command == "name.inline.coupon.convert.system" || command == "name.inline.coupon.copy.system") coupon_type = CouponType::System;
        const auto copy_value = [this, lifetime](String value, const StringView kind) {
            if (lifetime.expired() || value.empty()) return;
            const std::vector<String> values{std::move(value)};
            const auto custom_copy = comm_.copyConverted;
            const auto copied = custom_copy && custom_copy(values, kind);
            if (lifetime.expired() || copied) return;
            if (const auto clipboard = comm_.clipboardBackend())
                clipboard->write({values.front(), {}, {}});
        };
        if (coupon_type && (command.starts_with("name.inline.coupon.convert.") ||
                            command.starts_with("name.inline.coupon.copy."))) {
            const auto type = *coupon_type;
            const auto converter = comm_.convertCoupon ? comm_.convertCoupon
                                                       : source_.convertCoupon;
            const auto converted = converter ? converter(editor->text, type, false)
                                             : editor->text;
            if (command.starts_with("name.inline.coupon.convert.")) {
                if (converted == editor->text) return;
                dmenu::TMM(editor).setText(converted);
                editor->selection_start = converted.size();
                editor->selection_length = 0U;
            } else {
                copy_value(converted, "coupon");
            }
            return;
        }
        std::optional<FKCKind> timing;
        if (command == "name.inline.keycode.copy.use") timing = FKCKind::Use;
        else if (command == "name.inline.keycode.copy.success") timing = FKCKind::Success;
        else if (command == "name.inline.keycode.copy.failure") timing = FKCKind::Failure;
        else if (command == "name.inline.keycode.copy.has-not") timing = FKCKind::HasNot;
        if (timing) {
            const auto converter = comm_.convertKeyCode ? comm_.convertKeyCode
                                                        : source_.convertKeyCode;
            copy_value(converter ? converter(editor->text, *timing) : editor->text,
                       "keycode");
        }
    };
    active_editor_menu_ = std::move(binding);
}

std::shared_ptr<lib::TextEdit> NameViewBase::editName() {
    // D版と同様、行を指定しない TableTextEdit.startEdit は、選択行が一つだけの場合に限る。
    if (!canEditName() || !list_ || list_->selection.size() != 1U) return {};
    const std::weak_ptr<int> life = callback_lifetime_;
    const auto editor = createEditor(list_->selection.front(), 0U);
    if (life.expired()) return {};
    active_editor_ = editor;
    if (lib::editor_window_host_options().all_mode && active_editor_ && widget_) widget_->add(active_editor_);
    return active_editor_;
}

bool NameViewBase::canUndo() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_canUndo(__FILE__, __LINE__);
    return list_ && !read_only_ && undo_ && undo_->canUndo();
}

void NameViewBase::undo() {
    ::cwxeditor::cwx::perf::TraceScope trace_scope(__FILE__, __LINE__);
    if (!canUndo()) return;
    const std::weak_ptr<int> life = callback_lifetime_;
    // コマンド実行中にビューが同期的に無効化されても、自身の履歴を保持する。
    const auto keep_history = owned_undo_;
    auto* history = undo_;
    const auto refresh = comm_.refreshToolBar;
    in_proc_ = true;
    struct Reset { bool* flag; std::weak_ptr<int> life;
        ~Reset() { if (!life.expired()) *flag = false; }
    } reset{&in_proc_, life};
    history->undo();
    if (!life.expired() && refresh) refresh();
}

bool NameViewBase::canRedo() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_canRedo(__FILE__, __LINE__);
    return list_ && !read_only_ && undo_ && undo_->canRedo();
}

void NameViewBase::redo() {
    ::cwxeditor::cwx::perf::TraceScope trace_scope(__FILE__, __LINE__);
    if (!canRedo()) return;
    const std::weak_ptr<int> life = callback_lifetime_;
    // コマンド実行中にビューが同期的に無効化されても、自身の履歴を保持する。
    const auto keep_history = owned_undo_;
    auto* history = undo_;
    const auto refresh = comm_.refreshToolBar;
    in_proc_ = true;
    struct Reset { bool* flag; std::weak_ptr<int> life;
        ~Reset() { if (!life.expired()) *flag = false; }
    } reset{&in_proc_, life};
    history->redo();
    if (!life.expired() && refresh) refresh();
}

bool NameViewBase::canSelectAll() const noexcept {
    return list_ && list_->selection.size() != list_->rows.size();
}

void NameViewBase::selectAll() {
    if (!canSelectAll()) return;
    list_->selectAll();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool NameViewBase::canFindID() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_canFindID(__FILE__, __LINE__);
    return list_ && !list_->selection.empty();
}

void NameViewBase::findID() {
    ::cwxeditor::cwx::perf::TraceScope trace_findID(__FILE__, __LINE__);
    if (!canFindID()) return;
    const auto selected = selectedStrings();
    if (selected.empty()) return;
    last_find_id_ = selected.front();
    if (comm_.replaceID) comm_.replaceID(id_kind_, last_find_id_, true);
}

void NameViewBase::selectStrings(const std::span<const String> ids) {
    if (!list_) return;
    const auto table = list_;
    const auto gui = comm_.services.gui;
    const auto toolbar = comm_.refreshToolBar;
    const std::weak_ptr<int> life = callback_lifetime_;
    if (inc_search_) inc_search_->close();
    if (life.expired()) return;
    std::vector<std::size_t> indices;
    for (std::size_t index = 0U; index < name_list_.size(); ++index) {
        if (std::find(ids.begin(), ids.end(), name_list_[index]) != ids.end())
            indices.push_back(index);
    }
    const auto reveal = indices.empty() ? std::optional<std::size_t>{}
                                       : std::optional<std::size_t>{indices.front()};
    table->setSelection(std::move(indices));
    if (life.expired()) return;
    if (reveal) (void)lib::reveal_table_row(*table, *reveal);
    if (gui) {
        gui->request_redraw(table);
        if (life.expired()) return;
        // 関連リソースの選択は非アクティブなペインを対象にできる。 D版の NameView::select
        // と同様、キーボードフォーカスを奪わずに表を更新する。
    }
    if (!life.expired() && toolbar) toolbar();
}

std::vector<String> NameViewBase::selectedStrings() const {
    std::vector<String> result;
    if (!list_) return result;
    result.reserve(list_->selection.size());
    for (const auto index : list_->selection) {
        // ネイティブのインライン編集が表示文字列を仮に置き換えて
        // いても、 選択は公開済みの元の名前との対応に基づく。
        if (index < list_->rows.size() && index < name_list_.size())
            result.push_back(name_list_[index]);
    }
    return result;
}

void NameViewBase::changed() {
    if (!in_proc_ && undo_) undo_->reset();
}

void NameViewBase::refID() {
    if (!in_proc_ && undo_) undo_->reset();
    updateList();
}

void NameViewBase::refIDFS(const std::span<const String> flags,
                             const std::span<const String> steps,
                             const std::span<const String> variants) {
    if (!list_ || !summary_owner_ ||
        (id_kind_ != NameIDKind::Coupon && id_kind_ != NameIDKind::Gossip)) return;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = publication_revision_;
    const auto source_owner = summary_owner_;
    const auto references = source_.referencesVariables;
    if (references && references(flags, steps, variants) && !lifetime.expired() &&
        publication_revision_ == revision && summary_owner_ == source_owner) refID();
}

void NameViewBase::refUseCount() {
    if (!list_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision = ++publication_revision_;
    const auto target = list_;
    const auto source = source_;
    const auto owner = summary_owner_;
    const auto names = name_list_;
    const auto row_revision = target->row_order_revision;
    const auto update_revision = target->row_update_revision;
    const auto current = [&] {
        return !lifetime.expired() && publication_revision_ == revision &&
            list_ == target && summary_owner_ == owner &&
            target->row_order_revision == row_revision &&
            target->row_update_revision == update_revision;
    };
    const auto keys = source.keys ? source.keys() : std::vector<String>{};
    if (!current()) return;
    if (names.size() != keys.size()) {
        updateList();
    } else {
        // SWT の clearAll と同様、表示キャッシュを無効化するだけで、論理的な行順は変えない。
        std::vector<customtable::TableCellDisplayUpdate> updates;
        updates.reserve(names.size());
        for (std::size_t row = 0; row < names.size(); ++row) {
            const auto count = source.count ? source.count(names[row]) : 0U;
            if (!current()) return;
            updates.push_back({row, 1U, std::to_string(count), {}});
        }
        if (!target->updateCellDisplays(updates, row_revision)) return;
        target->redraw();
    }
    if(lifetime.expired()) return;
    const auto notify=comm_.refreshToolBar;
    if (notify) notify();
}

void NameViewBase::refUndoMax() {
    if (!owned_undo_) return;
    const std::weak_ptr<int> life = callback_lifetime_;
    const auto provider = comm_.undoMaxReplaceProvider;
    const auto maximum = provider ? provider() : comm_.undoMaxReplace;
    if (!life.expired()) owned_undo_->max(maximum);
}

void NameViewBase::sort() { updateList(); }

bool NameViewBase::applyRename(const StringView old_name, const StringView new_name,
                               std::optional<RenameReplay>* replay) {
    if (old_name.empty() || new_name.empty() || old_name == new_name) return false;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto revision = publication_revision_;
    const auto source_owner = summary_owner_;
    const auto prepare = source_.prepareRename;
    const auto rename = source_.rename;
    if (prepare) {
        auto plan = prepare(old_name, new_name);
        if (lifetime.expired() || publication_revision_ != revision || summary_owner_ != source_owner)
            return false;
        if (!plan || !plan->redo) return false;
        const auto action = plan->redo;
        if (replay) *replay = std::move(plan);
        action();
    } else {
        if (!rename || !rename(old_name, new_name)) return false;
    }
    if (lifetime.expired()) return true;
    if (publication_revision_ != revision || summary_owner_ != source_owner) return false;
    refreshRename();
    return true;
}

void NameViewBase::refreshRename() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    updateList();
    if (lifetime.expired()) return;
    const auto ref_name_id = comm_.refNameID;
    const auto replaced = comm_.replText;
    const auto refresh = comm_.refreshToolBar;
    if (ref_name_id) ref_name_id(id_kind_);
    if (lifetime.expired()) return;
    if (replaced) replaced();
    if (!lifetime.expired() && refresh) refresh();
}

bool NameViewBase::renameBatch(
    const std::vector<std::pair<String, String>>& replacements,
    const bool record_undo, const bool skip_occupied) {
    if (read_only_ || replacements.empty() || !source_.rename) return false;

    struct Guard final {
        bool& value;
        bool previous;
        ~Guard() { value = previous; }
    } guard{in_proc_, in_proc_};
    in_proc_ = true;

    std::vector<std::pair<String, String>> applied;
    applied.reserve(replacements.size());
    for (const auto& [old_name, new_name] : replacements) {
        if (old_name.empty() || new_name.empty() || old_name == new_name) continue;
        if (skip_occupied && source_.count && source_.count(new_name) != 0U) continue;
        if (source_.rename(old_name, new_name)) applied.emplace_back(old_name, new_name);
    }
    if (applied.empty()) return false;

    updateList();
    std::vector<String> selected;
    selected.reserve(applied.size());
    for (const auto& [old_name, new_name] : applied) {
        static_cast<void>(old_name);
        selected.push_back(new_name);
    }
    std::vector<std::size_t> selected_rows;
    for (std::size_t index = 0U; index < name_list_.size(); ++index) {
        if (contains(selected, name_list_[index])) selected_rows.push_back(index);
    }
    setSelectionSilently(std::move(selected_rows));

    if (record_undo && undo_)
        *undo_ += std::make_shared<RenameBatchUndo>(*this, applied);

    const auto ref_name_id = comm_.refNameID;
    const auto replaced = comm_.replText;
    const auto refresh = comm_.refreshToolBar;
    if (ref_name_id) ref_name_id(id_kind_);
    if (replaced) replaced();
    if (refresh) refresh();
    return true;
}

void NameViewBase::editEnd(const std::size_t row, const std::size_t column,
                           String new_text) {
    if (!list_ || read_only_ || column != 0U || row >= list_->rows.size() || row >= name_list_.size())
        return;
    if (list_->rows[row].cells.empty()) return;
    // ネイティブのインライン編集は、このコールバック前に表示セルを更新する。 D版の
    // TableTextEdit が editEnd で元の TableItem
    // を使うのと同様、 並行して持つ名前一覧を、編集前の元の識別情報として使う。
    const auto old_text = name_list_[row];
    if (new_text.empty()) { updateList(); return; }
    if (old_text == new_text) return;

    in_proc_ = true;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    // D版の scope(exit) と同様、提供処理・通知が例外を出してもガード
    // を戻す。 コールバックがビューを破棄する場合もあるため、その後は参照しない。
    struct ResetInProc {
        bool* flag;
        std::weak_ptr<int> lifetime;
        ~ResetInProc() { if (!lifetime.expired()) *flag = false; }
    } reset{&in_proc_, lifetime};
    std::optional<RenameReplay> replay;
    const auto changed = applyRename(old_text, new_text, &replay);
    if(lifetime.expired()) return;
    if (!changed) { updateList(); return; }
    if (undo_) *undo_ += std::make_shared<RenameUndo>(*this, old_text, new_text, std::move(replay));
    if (lifetime.expired()) return;
    active_editor_.reset();
    // D版と同様、編集確定後は、改名・統合した先を選択状態に保つ。
    std::vector<std::size_t> destination;
    for (std::size_t index = 0; index < name_list_.size(); ++index)
        if (name_list_[index] == new_text) destination.push_back(index);
    setSelectionSilently(std::move(destination));
    refreshStatusLine();
}

void NameViewBase::copy() {
    if (!canDoC()) return;
    const std::weak_ptr<int> life = callback_lifetime_;
    const auto editor = active_editor_;
    if (editor && editor->on_enter) { const auto enter = editor->on_enter; enter(); }
    if (life.expired()) return;
    const auto values = selectedStrings();
    const auto clipboard = comm_.clipboardBackend();
    const auto refresh = comm_.refreshToolBar;
    if (!clipboard) return;
    clipboard->write({joinLines(values), {}, {}});
    if (!life.expired() && refresh) refresh();
}

bool NameViewBase::canDoTCPD() const noexcept {
    return list_ && !list_->selection.empty();
}

bool NameViewBase::canDoC() const noexcept { return canDoTCPD(); }

bool NameViewBase::canCopyWith() const noexcept { return canDoC(); }

bool writeConvertedNames(const std::shared_ptr<ClipboardBackend>& clipboard,
                         const bool xml_copy, const std::vector<String>& values,
                         const StringView kind) {
    if (!clipboard || values.empty()) return false;
    String text;
    if (kind == "coupon") {
        auto node = xml::XNode::create(coupon::Coupon::XML_NAME_M);
        for (const auto& value : values) (void)coupon::Coupon(value, 0).toNode(node);
        text = node.text();
    } else if (kind == "keycode") {
        text = card::keyCodesToXML(values);
    } else return false;
    xmlbytestransfer::ClipData clip(clipboard);
    xmlbytestransfer::XMLtoCB(xml_copy, clip, text);
    return true;
}

void NameViewBase::copyWith(std::function<String(StringView)> convert,
                            const StringView kind) {
    if (!canCopyWith()) return;
    const std::weak_ptr<int> life = callback_lifetime_;
    const auto owner = summary_owner_;
    const auto editor = active_editor_;
    if (editor && editor->on_enter) { const auto enter = editor->on_enter; enter(); }
    if (life.expired() || summary_owner_ != owner) return;
    // インライン改名の確定では、正常な動作として表示情報が再構築される。
    // 同じ所有者での確定が終わってから、この操作の変更番号を設定する。
    const auto current = [this, life, revision = publication_revision_, owner] {
        return !life.expired() && publication_revision_ == revision && summary_owner_ == owner;
    };
    const auto values = selectedStrings();
    const auto clipboard = comm_.clipboardBackend();
    const auto custom_copy = comm_.copyConverted;
    const auto refresh = comm_.refreshToolBar;
    const String owned_kind(kind);
    std::vector<String> converted;
    converted.reserve(values.size());
    for (const auto& value : values) {
        converted.push_back(convert ? convert(value) : value);
        if (!current()) return;
    }
    if (converted.empty()) return;
    const bool copied = custom_copy && custom_copy(converted, owned_kind);
    if (!current()) return;
    if (copied) { if (refresh) refresh(); return; }
    if (!clipboard) return;
    // 製品のXML書き込み機能を持たない組み込みホストでは、テキストによる代替経路を維持する。
    clipboard->write({joinLines(converted), {}, {}});
    if (current() && refresh) refresh();
}

void NameViewBase::toggleSort(const SortColumn column) {
    if (sort_column_ == column) {
        sort_direction_ = sort_direction_ == SortDirection::Up ? SortDirection::Down
                                                                 : SortDirection::Up;
    } else {
        sort_column_ = column;
        sort_direction_ = SortDirection::Up;
    }
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto store_typed=comm_.storeTypedSort;
    const auto store=comm_.storeSort;
    if (store_typed) store_typed(id_kind_, sort_column_, sort_direction_);
    if (store) store(sort_column_, sort_direction_);
    if(lifetime.expired()) return;
    updateList();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::namewindow
