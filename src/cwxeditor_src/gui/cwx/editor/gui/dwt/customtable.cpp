#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::customtable {

namespace {

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(static_cast<double>(value));
}

[[nodiscard]] float nonnegative(const float value) noexcept {
    return finite(value) && value > 0.0F ? value : 0.0F;
}

[[nodiscard]] bool hasModifier(const ::cwxeditor::lib::Modifiers modifiers,
                               const ::cwxeditor::lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(modifiers) &
            static_cast<std::uint32_t>(modifier)) != 0U;
}

[[nodiscard]] SelectionIndices normaliseSelection(SelectionIndices values,
                                                   const std::size_t count,
                                                   const bool multi) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                [count](const std::size_t index) { return index >= count; }),
                 values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (!multi && values.size() > 1U) values.erase(values.begin() + 1, values.end());
    return values;
}

void copyStateFromRow(const ::cwxeditor::lib::TableRow& row, TableRowState& state) {
    state.checked = row.checked;
    state.text.clear();
    state.image.clear();
    state.text.reserve(row.cells.size());
    state.image.reserve(row.cells.size());
    for (const auto& cell : row.cells) {
        state.text.push_back(cell.text);
        state.image.push_back(cell.icon);
    }
}

void applyStateToRow(const TableRowState& state, ::cwxeditor::lib::TableRow& row) {
    row.checked = state.checked;
    row.cells.resize(std::max(state.text.size(), state.image.size()));
    for (std::size_t index = 0; index < row.cells.size(); ++index) {
        row.cells[index].text = index < state.text.size() ? state.text[index] : std::string{};
        row.cells[index].icon = index < state.image.size() ? state.image[index] : ResourceId{};
        row.cells[index].checked = state.checked;
    }
}

[[nodiscard]] TableModelPtr asModel(const TableViewPtr& table) {
    return std::dynamic_pointer_cast<TableModel>(table);
}

[[nodiscard]] std::vector<std::byte> indexPayload(const SelectionIndices& indices) {
    std::string text;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index != 0U) text.push_back(',');
        text += std::to_string(indices[index]);
    }
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

[[nodiscard]] std::string indexPayloadText(const SelectionIndices& indices) {
    std::string text;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index != 0U) text.push_back(',');
        text += std::to_string(indices[index]);
    }
    return text;
}

}  // namespace

TableColumn::TableColumn(std::shared_ptr<TableView> table, const std::size_t index,
                         const Style style)
    : table_(std::move(table)), index_(index), style_(style) {}

TableColumn::~TableColumn() {
    dispose();
    lifetime_.reset();
}

std::string TableColumn::title() const {
    const auto table = table_.lock();
    if (!table || index_ >= table->columns.size()) return std::string{};
    return table->columns[index_].title;
}

void TableColumn::title(std::string value) {
    const auto table = table_.lock();
    if (!table) return;
    if (table->columns.size() <= index_) table->columns.resize(index_ + 1U);
    table->columns[index_].title = std::move(value);
}

float TableColumn::width() const noexcept {
    const auto table = table_.lock();
    if (!table || index_ >= table->columns.size()) return 0.0F;
    return table->columns[index_].width;
}

void TableColumn::width(const float value) noexcept {
    const auto table = table_.lock();
    if (!table) return;
    if (table->columns.size() <= index_) table->columns.resize(index_ + 1U);
    table->columns[index_].width = std::max(0.0F, value);
}

bool TableColumn::resizable() const noexcept {
    const auto table = table_.lock();
    return table && index_ < table->columns.size() ? table->columns[index_].resizable : false;
}

void TableColumn::resizable(const bool value) noexcept {
    const auto table = table_.lock();
    if (!table) return;
    if (table->columns.size() <= index_) table->columns.resize(index_ + 1U);
    table->columns[index_].resizable = value;
}

bool TableColumn::sortable() const noexcept {
    const auto table = table_.lock();
    return table && index_ < table->columns.size() ? table->columns[index_].sortable : false;
}

void TableColumn::sortable(const bool value) noexcept {
    const auto table = table_.lock();
    if (!table) return;
    if (table->columns.size() <= index_) table->columns.resize(index_ + 1U);
    table->columns[index_].sortable = value;
}

void TableColumn::select() {
    if(disposed_)return;
    const std::weak_ptr<void> life=lifetime_;
    const auto callbacks = selection_listeners_;
    for (const auto& entry : callbacks) {
        if(life.expired() || disposed_)return;
        if(std::ranges::any_of(selection_listeners_,[&entry](const auto& peer){return peer.token==entry.token;}) && entry.callback)entry.callback();
    }
}

void TableColumn::dispose() {
    if (disposed_) return;
    disposed_ = true;
    const std::weak_ptr<void> life=lifetime_;
    const auto callbacks = dispose_listeners_;
    // 外部コールバックがこの列を削除したりdisposeへ再入したりする前
    // に、終了状態を公開する。DのGCは配送中のリスナー所有元を保持する。
    selection_listeners_.clear();
    dispose_listeners_.clear();
    for (const auto& entry : callbacks) {
        if(life.expired())return;
        if (entry.callback) entry.callback();
    }
}

std::uint64_t TableColumn::addSelectionListener(Listener listener) {
    if (!listener || disposed_) return 0U;
    const auto token = next_listener_token_++;
    selection_listeners_.push_back({token, std::move(listener)});
    return token;
}

void TableColumn::removeSelectionListener(const std::uint64_t token) noexcept {
    selection_listeners_.erase(
        std::remove_if(selection_listeners_.begin(), selection_listeners_.end(),
                      [token](const ListenerEntry& entry) { return entry.token == token; }),
        selection_listeners_.end());
}

std::uint64_t TableColumn::addDisposeListener(Listener listener) {
    if (!listener || disposed_) return 0U;
    const auto token = next_listener_token_++;
    dispose_listeners_.push_back({token, std::move(listener)});
    return token;
}

void TableColumn::removeDisposeListener(const std::uint64_t token) noexcept {
    dispose_listeners_.erase(
        std::remove_if(dispose_listeners_.begin(), dispose_listeners_.end(),
                      [token](const ListenerEntry& entry) { return entry.token == token; }),
        dispose_listeners_.end());
}

struct TableModel::RangeState {
    TableModel* owner{};
    ::cwxeditor::lib::Modifiers state_mask{::cwxeditor::lib::Modifiers::None};
    ::cwxeditor::lib::Modifiers last_modifiers{::cwxeditor::lib::Modifiers::None};
    Point start{};
    Point end{};
    bool has_start{};
    bool has_end{};
    bool drag_started{};
    bool checkbox_click{};
    bool auto_scroll_pending{};
    SelectionIndices baseline;
    std::optional<std::size_t> start_row;
    std::optional<std::size_t> range_from;
    std::optional<std::size_t> range_to;
    ::cwxeditor::lib::TimerId auto_scroll_timer{};
    std::uint64_t revision{};
    std::chrono::steady_clock::time_point clock_origin{};
    std::int64_t vertical_frame{},horizontal_frame{};

    [[nodiscard]] bool control() const noexcept {
        return hasModifier(state_mask, ::cwxeditor::lib::Modifiers::Control);
    }
    [[nodiscard]] bool shift() const noexcept {
        return hasModifier(state_mask, ::cwxeditor::lib::Modifiers::Shift);
    }
};

TableModel::TableModel(const Style style, Services services)
    : style_(style), services_(std::move(services)) {
    TableView::checkable = (style & STYLE_CHECKABLE) != 0U;
    grid_lines = false;
    multi_select = (style & STYLE_MULTI_SELECT) != 0U;
    virtualized = (style & STYLE_VIRTUAL) != 0U;
    header_visible = (style & STYLE_HEADER) != 0U;
    full_row_selection = true;
    set_enabled(true);
    // 共通の項目入力はTableViewのインデックス付きコールバ
    // ックでヘッダークリックを通知するが、DWTのTableSorter
    // は対応するTableColumnハンドルを監視する。共通モ
    // デルに橋渡しを置き、メインの五つのカードペインも含め、全独
    // 自表で表示ヘッダーのクリックによる並べ替えを有効にする。
    const std::weak_ptr<void> life=lifetime_;
    on_sort_requested = [this,life](const std::size_t index) {
        if(life.expired())return;
        if (index < column_handles_.size() && column_handles_[index])
            column_handles_[index]->select();
    };
}

TableModel::~TableModel() {
    lifetime_.reset();
    cancelRangeSelection();
    if (services_.drag_drop) {
        try {
            services_.drag_drop->unbind(std::static_pointer_cast<TableView>(shared_from_this()));
        } catch (...) {
            // スタック上のTableModelは共有所有
            // 元を持たず、その場合は解除する接続もない。
        }
    }
    on_selection_changed = {};
    on_cell_edited = {};
    on_key_down = {};
    on_key_up = {};
    on_pointer = {};
    on_sort_requested = {};
    on_focus = {};
    on_blur = {};
    for (const auto& column : column_handles_)
        if (column) column->dispose();
}

void TableModel::style(const Style value) noexcept {
    style_ = value;
    multi_select = (value & STYLE_MULTI_SELECT) != 0U;
    TableView::checkable = (value & STYLE_CHECKABLE) != 0U;
    virtualized = (value & STYLE_VIRTUAL) != 0U;
    header_visible = (value & STYLE_HEADER) != 0U;
    setSelection(selection, false);
}

void TableModel::installRangeSelection() {
    if (range_) return;
    range_ = std::make_shared<RangeState>();
    range_->owner = this;

    const auto previous_selection = std::move(on_selection_changed);
    const auto previous_edit = std::move(on_cell_edited);
    const auto previous_key_down = std::move(on_key_down);
    const auto previous_key_up = std::move(on_key_up);
    const auto previous_pointer = std::move(on_pointer);
    const auto previous_blur=std::move(on_blur);
    const std::weak_ptr<void> life=lifetime_;
    // フレーム表示側はWidgetの描画コールバックをローカル矩形で記録する。DのrangeSelectableTable
    // はドラッグ矩形用の描画リスナーを設定する。
    const auto previous_paint_context = on_paint_context;
    on_paint_context = [this, life, previous_paint_context](lib::DrawingContext& context, Rect area) {
        if (life.expired()) return;
        if (previous_paint_context) previous_paint_context(context, area);
        if (!life.expired() && !lib::editor_window_host_options().all_mode)
            render(context, area);
    };
    on_selection_changed = [this, life,previous_selection](const SelectionIndices& indices) {
        if(life.expired())return;
        // 共通入力処理は、このコールバックを呼ぶ前にTableView.selection
        // を公開する。値が同じでもDの選択リスナーへの橋渡しは必要で、
        // setSelectionの変更なしの戻り値で通知を捨てない。
        setSelection(indices,false);
        if(life.expired())return;
        const auto revision=++selection_revision_;
        notifySelection();
        if (!life.expired() && revision==selection_revision_ && previous_selection) previous_selection(indices);
    };
    on_cell_edited = [this, life,previous_edit](const std::size_t row, const std::size_t column,
                                           std::string text) {
        if(life.expired())return;
        if (previous_edit) previous_edit(row, column, text);
        if(life.expired())return;
        editCell(row, column, std::move(text));
    };
    on_key_down = [this, life,previous_key_down](::cwxeditor::lib::KeyEvent& event) {
        if(life.expired())return;
        handleKeyDown(event);
        if (!life.expired() && previous_key_down) previous_key_down(event);
    };
    on_key_up = [this, life,previous_key_up](::cwxeditor::lib::KeyEvent& event) {
        if(life.expired())return;
        handleKeyUp(event);
        if (!life.expired() && previous_key_up) previous_key_up(event);
    };
    on_blur=[this,life,previous_blur]{
        if(life.expired())return;
        cancelRangeSelection();
        if(!life.expired() && previous_blur)previous_blur();
    };
    on_pointer = [this, life,previous_pointer](::cwxeditor::lib::PointerEvent& event) {
        if(life.expired())return;
        handlePointer(event);
        if(life.expired())return;
        if (previous_pointer) previous_pointer(event);
        if(life.expired())return;
        const auto listeners = pointer_listeners_;
        for (const auto& listener : listeners){
            if(life.expired())return;
            if(std::ranges::any_of(pointer_listeners_,[&listener](const auto& peer){return peer.token==listener.token;}) && listener.callback)listener.callback(event);
        }
    };

    if (services_.drag_drop) {
        try {
            ::cwxeditor::lib::DragSourceBinding source;
            source.source = std::static_pointer_cast<TableView>(shared_from_this());
            source.allowed = ::cwxeditor::lib::DropOperation::Copy |
                             ::cwxeditor::lib::DropOperation::Move;
            const auto weak = weak_from_this();
            source.provide_data = [weak] {
                const auto self = weak.lock();
                if (!self) return ::cwxeditor::lib::ClipboardData{};
                if (self->drag_data_provider) return self->drag_data_provider();
                ::cwxeditor::lib::ClipboardData data;
                data.text = indexPayloadText(self->selection);
                data.formats["application/x-cwxeditor-table-indexes"] =
                    indexPayload(self->selection);
                return data;
            };
            source.completed = [weak](const ::cwxeditor::lib::DropOperation operation) {
                if (const auto self = weak.lock()) {
                    self->cancelRangeSelection();
                    if (self->drag_finished) self->drag_finished(operation);
                }
            };
            services_.drag_drop->bind_source(std::move(source));
        } catch (const std::bad_weak_ptr&) {
            // スタック所有のモデルもポインター・キーによる選択は使える。ドラッグ
            // のバックエンドは共有ウィジェットの同一性を必要とするため省略する。
        }
    }
}

std::uint64_t TableModel::addPointerListener(PointerHandler callback) {
    if (!callback) return 0U;
    const auto token = next_listener_token_++;
    pointer_listeners_.push_back({token, std::move(callback)});
    return token;
}

void TableModel::removePointerListener(const std::uint64_t token) noexcept {
    pointer_listeners_.erase(
        std::remove_if(pointer_listeners_.begin(), pointer_listeners_.end(),
                       [token](const PointerListenerEntry& entry) {
                           return entry.token == token;
                       }),
        pointer_listeners_.end());
}

bool TableModel::rangeSelectionActive() const noexcept {
    return range_ && range_->has_start && range_->has_end;
}

std::optional<Rect> TableModel::rangeRectangle() const noexcept {
    if (!rangeSelectionActive()) return std::nullopt;
    const auto left = std::min(range_->start.x, range_->end.x);
    const auto top = std::min(range_->start.y, range_->end.y);
    const auto right = std::max(range_->start.x, range_->end.x);
    const auto bottom = std::max(range_->start.y, range_->end.y);
    return Rect{left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
}

void TableModel::cancelRangeSelection() noexcept {
    if (!range_) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto dispatcher=services_.dispatcher;
    const auto timer=range_->auto_scroll_timer;
    const bool pending=range_->auto_scroll_pending;
    ++range_->revision;
    range_->auto_scroll_pending = false;
    range_->auto_scroll_timer = 0U;
    range_->has_start = false;
    range_->has_end = false;
    range_->drag_started = false;
    range_->checkbox_click = false;
    range_->baseline.clear();
    range_->range_from.reset();
    range_->range_to.reset();
    range_->start_row.reset();
    if(pending && dispatcher && timer)dispatcher->cancel_timer(timer);
    if(!life.expired())redraw();
}

void TableModel::setSelection(SelectionIndices indices, const bool notify) {
    const auto normalised = normaliseSelection(std::move(indices), rows.size(), multi_select);
    if (normalised == selection) return;
    selection = normalised;
    ++selection_revision_;
    const std::weak_ptr<void> life=lifetime_;
    if (notify) notifySelection();
    if(!life.expired())redraw();
}

void TableModel::clearSelection(const bool notify) {
    if (selection.empty()) return;
    selection.clear();
    ++selection_revision_;
    const std::weak_ptr<void> life=lifetime_;
    if (notify) notifySelection();
    if(!life.expired())redraw();
}

void TableModel::select(const std::size_t index, const bool notify) {
    if (index >= rows.size()) return;
    auto values = selection;
    if (!multi_select) values.clear();
    values.push_back(index);
    setSelection(std::move(values), notify);
}

void TableModel::deselect(const std::size_t index, const bool notify) {
    auto values = selection;
    values.erase(std::remove(values.begin(), values.end(), index), values.end());
    setSelection(std::move(values), notify);
}

bool TableModel::isSelected(const std::size_t index) const noexcept {
    return std::find(selection.begin(), selection.end(), index) != selection.end();
}

void TableModel::selectAll(const bool notify) {
    SelectionIndices values;
    values.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) values.push_back(index);
    setSelection(std::move(values), notify);
}

void TableModel::notifySelection() {
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=selection_revision_;
    const auto selected=selection;
    const auto callbacks = selection_listeners_;
    for (const auto& entry : callbacks){
        if(life.expired() || revision!=selection_revision_)return;
        if(std::ranges::any_of(selection_listeners_,[&entry](const auto& peer){return peer.token==entry.token;}) && entry.callback)entry.callback(selected);
    }
}

std::uint64_t TableModel::addSelectionListener(SelectionHandler listener) {
    if (!listener) return 0U;
    const auto token = next_listener_token_++;
    selection_listeners_.push_back({token, std::move(listener)});
    return token;
}

void TableModel::removeSelectionListener(const std::uint64_t token) noexcept {
    selection_listeners_.erase(
        std::remove_if(selection_listeners_.begin(), selection_listeners_.end(),
                      [token](const ListenerEntry& entry) { return entry.token == token; }),
        selection_listeners_.end());
}

std::uint64_t TableModel::addEditListener(EditHandler listener) {
    if (!listener) return 0U;
    const auto token = next_listener_token_++;
    edit_listeners_.push_back({token, std::move(listener)});
    return token;
}

void TableModel::removeEditListener(const std::uint64_t token) noexcept {
    edit_listeners_.erase(
        std::remove_if(edit_listeners_.begin(), edit_listeners_.end(),
                      [token](const EditEntry& entry) { return entry.token == token; }),
        edit_listeners_.end());
}

std::uint64_t TableModel::addResizeListener(ResizeHandler listener) {
    if (!listener) return 0U;
    const auto token = next_listener_token_++;
    resize_listeners_.push_back({token, std::move(listener)});
    return token;
}

void TableModel::removeResizeListener(const std::uint64_t token) noexcept {
    resize_listeners_.erase(
        std::remove_if(resize_listeners_.begin(), resize_listeners_.end(),
                      [token](const ResizeEntry& entry) { return entry.token == token; }),
        resize_listeners_.end());
}

void TableModel::notifyResize() {
    const std::weak_ptr<void> life=lifetime_;
    const auto callbacks = resize_listeners_;
    for (const auto& entry : callbacks){
        if(life.expired())return;
        if(std::ranges::any_of(resize_listeners_,[&entry](const auto& peer){return peer.token==entry.token;}) && entry.callback)entry.callback();
    }
}

TableColumnPtr TableModel::addColumn(std::string title_value, const float width_value,
                                     const Style style_value) {
    const auto index = columns.size();
    columns.push_back({std::move(title_value), std::max(0.0F, width_value),
                       ::cwxeditor::lib::TextAlignment::Start, true, false});
    auto result = std::make_shared<TableColumn>(
        std::static_pointer_cast<TableView>(shared_from_this()), index, style_value);
    column_handles_.push_back(result);
    return result;
}

TableColumnPtr TableModel::column(const std::size_t index) const {
    if (index >= column_handles_.size()) return TableColumnPtr{};
    return column_handles_[index];
}

void TableModel::ensureRowState() {
    if (row_state_.size() > rows.size()) row_state_.resize(rows.size());
    while (row_state_.size() < rows.size()) row_state_.emplace_back();
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto& state = row_state_[index];
        if (state.text.empty() && state.image.empty()) copyStateFromRow(rows[index], state);
        state.checked = rows[index].checked;
    }
}

const std::vector<TableRowState>& TableModel::rowStates() const {
    auto* self = const_cast<TableModel*>(this);
    self->ensureRowState();
    return row_state_;
}

const TableRowState& TableModel::rowState(const std::size_t index) const {
    return rowStates().at(index);
}

TableRowState& TableModel::rowState(const std::size_t index) {
    ensureRowState();
    return row_state_.at(index);
}

void TableModel::setRowState(const std::size_t index, TableRowState state) {
    if (rows.size() <= index) rows.resize(index + 1U);
    if (row_state_.size() <= index) row_state_.resize(index + 1U);
    applyStateToRow(state, rows[index]);
    row_state_[index] = std::move(state);
}

void TableModel::editCell(const std::size_t row, const std::size_t column_value,
                          std::string text) {
    if (row >= rows.size()) return;
    if (rows[row].cells.size() <= column_value) rows[row].cells.resize(column_value + 1U);
    rows[row].cells[column_value].text = text;
    ensureRowState();
    auto& state = row_state_[row];
    if (state.text.size() <= column_value) state.text.resize(column_value + 1U);
    state.text[column_value] = text;
    const auto callbacks = edit_listeners_;
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=row_order_revision;
    for (const auto& entry : callbacks){
        if(life.expired() || revision!=row_order_revision)return;
        if(std::ranges::any_of(edit_listeners_,[&entry](const auto& peer){return peer.token==entry.token;}) && entry.callback)entry.callback(row,column_value,text);
    }
    if(!life.expired())redraw();
}

bool TableModel::reorderRows(const std::vector<std::size_t>& new_to_old) {
    if (new_to_old.size() != rows.size() ||
        std::any_of(new_to_old.begin(), new_to_old.end(),
            [&](std::size_t row) { return row >= rows.size(); })) return false;
    ensureRowState();
    std::vector<TableRowState> states;
    states.reserve(new_to_old.size());
    std::optional<std::size_t> cursor;
    for (std::size_t next = 0; next < new_to_old.size(); ++next) {
        states.push_back(row_state_[new_to_old[next]]);
        copyStateFromRow(rows[new_to_old[next]], states.back());
        if (cursor_row_ == new_to_old[next]) cursor = next;
    }
    if (!lib::reorder_table_rows(*this, new_to_old)) return false;
    // 付随データの文字・画像項目を公開行へ再適用しない。そのデータ
    // の取得後に、インライン編集がセルを更新している場合がある。
    row_state_.swap(states);
    cursor_row_ = cursor;
    return true;
}

bool TableModel::replaceRows(std::vector<::cwxeditor::lib::TableRow> new_rows,
                              SelectionIndices new_selection,
                              std::vector<TableRowState> new_states,
                              std::optional<std::size_t> reveal,
                              std::function<bool()> current,
                              const bool reset_top) {
    const auto keep_alive=weak_from_this().lock();
    if(current && !current()) return false;
    ++row_update_revision;
    if (!new_states.empty() && new_states.size() != new_rows.size())
        throw std::invalid_argument("TableModel replacement sidecar count");
    if (new_states.empty()) new_states.resize(new_rows.size());
    for (std::size_t index = 0; index < new_rows.size(); ++index) {
        copyStateFromRow(new_rows[index], new_states[index]);
        new_states[index].cursor = false;
    }
    // 公開表や付随データを変える前に、すべての確保を終える。
    if(!lib::replace_table_rows(*this, std::move(new_rows), std::move(new_selection),
                               reveal, std::move(current), reset_top)) return false;
    row_state_.swap(new_states);
    // 従来のセルカーソルでは、新しく渡された論理行を識別できない。
    cursor_row_.reset();
    cursor_column_ = 0U;
    return true;
}

bool TableModel::updateCellDisplays(const std::vector<TableCellDisplayUpdate>& updates,
                                    const std::uint64_t expected_row_revision) {
    if (row_order_revision != expected_row_revision) return false;
    for (const auto& update : updates)
        if (update.row >= rows.size() || update.column >= rows[update.row].cells.size())
            return false;
    if (updates.empty()) return true;
    auto new_rows = rows;
    auto new_states = row_state_;
    new_states.resize(new_rows.size());
    for (const auto& update : updates) {
        auto& cell = new_rows[update.row].cells[update.column];
        if (update.text) cell.text = *update.text;
        if (update.image) cell.icon = *update.image;
    }
    for (std::size_t i = 0; i < new_rows.size(); ++i)
        copyStateFromRow(new_rows[i], new_states[i]);
    if (row_order_revision != expected_row_revision) return false;
    rows.swap(new_rows);
    row_state_.swap(new_states);
    return true;
}

void TableModel::clearAll() {
    const std::weak_ptr<void> life=lifetime_;
    if (!replaceRows({}, {}, {}, {}, {}, true)) return;
    cursor_row_.reset();
    const auto callback = on_selection_changed;
    if (callback) callback(selection);
    if(!life.expired())redraw();
}

void TableModel::setCursor(std::optional<std::size_t> row, const std::size_t column_value) noexcept {
    if (row && *row >= rows.size()) row.reset();
    cursor_row_ = row;
    cursor_column_ = column_value;
}

void TableModel::setRowHeight(const float value) noexcept {
    row_height = finite(value) && value > 0.0F ? value : 1.0F;
}

void TableModel::setHeaderHeight(const float value) noexcept {
    header_height = nonnegative(value);
}

float TableModel::rowHeight() const noexcept {
    return lib::table_row_height(*this, lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
}

float TableModel::headerHeight() const noexcept {
    return lib::table_header_height(*this, rowHeight());
}

void TableModel::setHorizontalOffset(const float value) noexcept {
    (void)lib::set_table_horizontal_offset(*this, value);
}

void TableModel::setVerticalOffset(const float value) noexcept {
    const auto visible = lib::table_visible_row_count(*this, rowHeight());
    const auto maximum = rows.size() > visible ? rows.size() - visible : 0U;
    const float requested = std::min(nonnegative(value) / rowHeight(), static_cast<float>(maximum));
    (void)lib::set_item_view_top_index(*this, static_cast<std::size_t>(requested), visible);
}

std::optional<std::size_t> TableModel::rowAt(const Point position) const noexcept {
    return lib::hit_test_table_row(*this, {bounds().x + position.x, bounds().y + position.y}, rowHeight());
}

Rect TableModel::rowBounds(const std::size_t row) const noexcept {
    auto result = lib::table_row_geometry(*this, row, rowHeight());
    result.x -= bounds().x;
    result.y -= bounds().y;
    return result;
}

Rect TableModel::cellBounds(const std::size_t row, const std::size_t column_value) const noexcept {
    const auto row_rect = rowBounds(row);
    float x = -horizontal_offset;
    const auto widths = lib::resolved_table_column_widths(*this, bounds().width);
    for (std::size_t index = 0; index < column_value && index < columns.size(); ++index)
        x += widths[index];
    const auto width = column_value < widths.size() ? widths[column_value]
                                                     : std::max(0.0F, row_rect.width - x);
    return Rect{x, row_rect.y, width, row_rect.height};
}

bool TableModel::pointInCellContent(const Point position, const std::size_t row,
                                    const std::size_t column_value) const noexcept {
    if (row >= rows.size()) return false;
    const auto cell = cellBounds(row, column_value);
    if (position.x < cell.x || position.x > cell.x + cell.width ||
        position.y < cell.y || position.y > cell.y + cell.height) return false;
    const auto& cells = rows[row].cells;
    const auto text = column_value < cells.size() ? cells[column_value].text : std::string{};
    const auto image = column_value < cells.size() ? cells[column_value].icon : ResourceId{};
    const auto check_space = checkable() && column_value == 0U ? lib::scaled_ui_metric(18.0F) : 0.0F;
    const auto image_space = image != 0U ? lib::scaled_ui_metric(18.0F) : 0.0F;
    const auto text_width = lib::ui_text_width(text, lib::scaled_ui_metric(2.0F),
                                               font() ? &*font() : nullptr);
    return position.x <= cell.x + check_space + image_space + text_width;
}

void TableModel::render(::cwxeditor::lib::DrawingContext& context, const Rect area) {
    const std::weak_ptr<void> life=lifetime_;
    const auto paint=on_paint;
    if(paint)paint(context,area);
    if(!life.expired())drawRange(context,area);
}

void TableModel::redraw(const std::optional<Rect> dirty) {
    if (!redraw_enabled_) return;
    if (!services_.gui) return;
    try {
        services_.gui->request_redraw(std::static_pointer_cast<TableView>(shared_from_this()), dirty);
    } catch (const std::bad_weak_ptr&) {
        // スタック所有のモデルはバックエンドの所有ハンドルを持たない。
    }
}

void TableModel::drawRange(::cwxeditor::lib::DrawingContext& context, const Rect area) {
    const auto range = rangeRectangle();
    if (!range) return;
    const auto left = std::max(area.x, range->x);
    const auto top = std::max(area.y, range->y);
    const auto right = std::min(area.x + area.width, range->x + range->width);
    const auto bottom = std::min(area.y + area.height, range->y + range->height);
    if (right <= left || bottom <= top) return;
    const Rect clipped{left, top, right - left, bottom - top};
    context.fill_rect(clipped, fillColor);
    context.draw_rect(clipped, lineColor, 1.0F, ::cwxeditor::lib::StrokeStyle::Solid);
}

void TableModel::updateRangeSelection() {
    if (!range_ || !range_->has_start || !range_->has_end) return;
    const bool ctrl = hasModifier(range_->state_mask, lib::Modifiers::Control);
    if (!ctrl) range_->baseline = selection;
    const auto old_from = range_->range_from, old_to = range_->range_to;
    const auto row_number = [this](const Point point) {
        // Dの整数除算は、行0のすぐ上の帯も含め、0へ向かって
        // 切り捨てる。この境界をfloor()に置き換えない。
        return static_cast<std::ptrdiff_t>((verticalOffset() + point.y - headerHeight()) /
                                          std::max(1.0F, rowHeight()));
    };
    // 自動スクロール中も、内容行に固定された既存のアンカーを維持する。
    const auto first = range_->start_row ? static_cast<std::ptrdiff_t>(*range_->start_row)
                                         : row_number(range_->start);
    const auto last = row_number(range_->end);
    const auto from = std::min(first, last), to = std::max(first, last);
    float table_width = 0.0F;
    for (const auto width : lib::resolved_table_column_widths(*this, bounds().width)) table_width += width;
    if (table_width <= 0.0F) table_width = std::max(0.0F, bounds().width);
    const auto left = std::min(range_->start.x, range_->end.x);
    const auto right = std::max(range_->start.x, range_->end.x);
    if (rows.empty() || to < 0 || from >= static_cast<std::ptrdiff_t>(rows.size()) ||
        right < -horizontal_offset || left >= table_width - horizontal_offset) {
        range_->range_from.reset();
        range_->range_to.reset();
    } else {
        range_->range_from = static_cast<std::size_t>(std::max<std::ptrdiff_t>(from, 0));
        range_->range_to = static_cast<std::size_t>(std::min<std::ptrdiff_t>(to, rows.size() - 1));
    }
    auto values = selection;
    const auto update = [&](std::size_t index) {
        const bool inside = range_->range_from && *range_->range_from <= index && index <= *range_->range_to;
        const bool selected_at_start = std::find(range_->baseline.begin(), range_->baseline.end(), index) != range_->baseline.end();
        // DはCtrlの選択範囲から外れた場合も、元は未選択だった行を選
        // 択済みのまま残す。基準選択と現在範囲の排他的論理和ではない。
        const bool selected = ctrl ? (!selected_at_start || !inside) : inside;
        const auto existing = std::find(values.begin(), values.end(), index);
        if (selected && existing == values.end()) values.push_back(index);
        else if (!selected && existing != values.end()) values.erase(existing);
    };
    if (range_->range_from && old_from) {
        for (auto index = std::min(*old_from, *range_->range_from);
             index <= std::max(*old_to, *range_->range_to); ++index)
            if (index < std::max(*old_from, *range_->range_from) ||
                index > std::min(*old_to, *range_->range_to)) update(index);
    } else if (range_->range_from) {
        for (auto index = *range_->range_from; index <= *range_->range_to; ++index) update(index);
    } else if (old_from) {
        for (auto index = *old_from; index <= *old_to; ++index) update(index);
    }
    setSelection(std::move(values));
}

void TableModel::autoScrollOnce() {
    if (!range_ || !range_->has_end || !services_.dispatcher || rows.empty()) return;
    const auto now=services_.dispatcher->now();
    const auto frame=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(now-range_->clock_origin).count()/16);
    const auto vertical_frames=std::max<std::int64_t>(0,frame-range_->vertical_frame);
    const auto horizontal_frames=std::max<std::int64_t>(0,frame-range_->horizontal_frame);
    const auto area = bounds();
    const auto top = headerHeight();
    const auto bottom = std::max(top, lib::table_horizontal_scroll_geometry(*this).viewport.height);
    const auto visible=lib::table_visible_row_count(*this,rowHeight());
    const bool up=range_->end.y<top;
    const bool down=range_->end.y>=bottom;
    const bool at_vertical_end=up?top_index==0:down?top_index+visible>=rows.size():true;
    if(at_vertical_end)range_->vertical_frame=frame;
    else if(vertical_frames>0){
        // Dは経過した16msのフレーム数と、距離の2倍を60で割った行数を使う。
        const double distance=up?top-range_->end.y:range_->end.y-bottom+1.0F;
        const auto step=static_cast<std::size_t>(std::min<double>(rows.size(),std::floor(vertical_frames/60.0*distance*2.0)));
        if(step){
            setVerticalOffset(verticalOffset()+(up?-1.0F:1.0F)*static_cast<float>(step)*rowHeight());
            range_->vertical_frame=frame;
        }
    }
    const auto width=std::max(0.0F,area.width);
    const bool left=range_->end.x<0;
    const bool right=range_->end.x>=width;
    if((!left&&!right)||(left&&horizontal_offset<=0))range_->horizontal_frame=frame;
    else if(horizontal_frames>0){
        // Dの横方向の距離規則がWindows限定なのは、LVM_SCROLLがネイテ
        // ィブ処理だったため。ここでは全バックエンドで同じ共通スクロール処理を使う。
        const double distance=left?range_->end.x:range_->end.x-width+1.0F;
        setHorizontalOffset(horizontal_offset+static_cast<float>(horizontal_frames*distance));
        range_->horizontal_frame=frame;
    }
    updateRangeSelection();
}

void TableModel::scheduleAutoScroll() {
    if (!range_ || !range_->has_end || !services_.dispatcher || range_->auto_scroll_pending)
        return;
    range_->auto_scroll_pending = true;
    const auto revision=range_->revision;
    const auto row_revision=row_order_revision;
    const auto dispatcher=services_.dispatcher;
    const std::weak_ptr<void> life=lifetime_;
    try {
        const auto weak = weak_from_this();
        const auto timer=dispatcher->post_delayed(
            std::chrono::milliseconds{16}, [weak,revision,row_revision] {
                const auto self = weak.lock();
                if (!self || !self->range_ || self->range_->revision!=revision) return;
                if(self->row_order_revision!=row_revision){self->cancelRangeSelection();return;}
                self->range_->auto_scroll_pending = false;
                self->range_->auto_scroll_timer = 0U;
                if (!self->range_->has_end) return;
                self->autoScrollOnce();
                if(self->row_order_revision!=row_revision){self->cancelRangeSelection();return;}
                if(self->range_->revision!=revision || !self->range_->has_end)return;
                self->redraw();
                if(self->range_->revision!=revision || !self->range_->has_end)return;
                self->scheduleAutoScroll();
            });
        if(life.expired() || range_->revision!=revision){dispatcher->cancel_timer(timer);return;}
        range_->auto_scroll_timer=timer;
    } catch (...) {
        if(life.expired() || range_->revision!=revision)return;
        range_->auto_scroll_pending = false;
        range_->auto_scroll_timer = 0U;
    }
}

void TableModel::handlePointer(::cwxeditor::lib::PointerEvent& event) {
    if (!range_ || !enabled() || event.pointer_type != ::cwxeditor::lib::PointerType::Mouse)
        return;
    const auto primary = event.button == ::cwxeditor::lib::PointerButton::Primary;
    // 表示側のポインターイベントはウィンドウ座標、Dの表の補助
    // 処理はクライアント座標。原点を混ぜず、一度だけ変換する。
    const Point position{event.position.x - bounds().x, event.position.y - bounds().y};
    if(!finite(position.x)||!finite(position.y))return;
    const std::weak_ptr<void> life=lifetime_;
    switch (event.action) {
    case ::cwxeditor::lib::PointerAction::Down: {
        if (!primary) return;
        if(range_->has_start || range_->auto_scroll_pending){
            cancelRangeSelection();
            if(life.expired())return;
        }
        ++range_->revision;
        const auto revision=range_->revision;
        range_->state_mask = event.modifiers;
        range_->last_modifiers = event.modifiers;
        range_->start = position;
        range_->has_start = true;
        range_->has_end = false;
        range_->drag_started = false;
        range_->checkbox_click = false;
        const auto row = rowAt(position);
        if (row) {
            range_->start_row = row;
        } else if (!lib::editor_window_host_options().all_mode &&
                   position.y >= headerHeight() && position.y < bounds().height) {
            // 空白から始めた場合も、自動スクロールで表示領域が移る間、Dの
            // 内容座標のアンカーを維持する。更新ごとに基準を取り直さない。
            const double anchor = (double(verticalOffset()) + position.y - headerHeight()) /
                                  std::max(1.0F, rowHeight());
            if (std::isfinite(anchor) && anchor >= double(rows.size()) &&
                anchor >= 0.0 && anchor < double(std::numeric_limits<std::ptrdiff_t>::max()))
                range_->start_row = static_cast<std::size_t>(anchor);
        }
        if (!row) return;
        const auto first_cell = cellBounds(*row, 0U);
        range_->checkbox_click = checkable() && position.x < first_cell.x + lib::scaled_ui_metric(18.0F);
        bool content{};
        for(std::size_t column=0;column<columns.size();++column)
            if(pointInCellContent(position,*row,column)){content=true;break;}
        if (isSelected(*row) || content) return;
        const bool defer_blank_down_selection =
            defer_unselected_whitespace_down_selection_;
        if (services_.gui) {
            try {
                const auto self = std::static_pointer_cast<TableView>(shared_from_this());
                services_.gui->focus(self);
                if(life.expired() || range_->revision!=revision)return;
                services_.gui->capture_pointer(self, true);
                if(life.expired() || range_->revision!=revision)return;
            } catch (const std::bad_weak_ptr&) {
                // スタック所有のモデルには、フォーカスやキャプチャに使うウィジェットの同一性がない。
            }
        }
        if (!defer_blank_down_selection &&
            !hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Control) &&
            !hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Shift)) {
            clearSelection(false);
        }
        range_->baseline = selection;
        // 選択と範囲の状態はここで初期化するが、共通マウス状態
        // 機械にも主ボタンの押下を渡す。連続クリック判定と登録
        // 済みの表のドラッグ元の準備を担当するため。DのWindows
        // のCardPaneの空白経路は、押下時に共通の行
        // 選択を行わず、移動または解放まで選択を遅らせる。
        if (defer_blank_down_selection)
            event.suppress_default_item_view_selection = true;
        event.handled = false;
        break;
    }
    case ::cwxeditor::lib::PointerAction::Move: {
        if (!range_->has_start || !enabled()) return;
        if (!range_->has_end) {
            const auto row = rowAt(range_->start);
            if (row && isSelected(*row) && (on_drag_detected || services_.drag_drop)) {
                range_->drag_started = true;
                cancelRangeSelection();
                const auto drag=on_drag_detected;
                if(drag)drag();
                if(life.expired())return;
                // 実際の操作の合意形成は共有DragDropService
                // が行う。登録済みの転送元セッションを開始す
                // る前に、しきい値を越えた移動を消費しない。
                event.handled = !services_.drag_drop;
                return;
            }
            // Dは最初の範囲移動が始まる時点で基準状態を取得する
            // 。空白ヒットだけでなく、全行の外側での押下も含む。
            const auto revision = range_->revision;
            if (!hasModifier(event.modifiers, lib::Modifiers::Control) &&
                !hasModifier(event.modifiers, lib::Modifiers::Shift)) clearSelection(false);
            if (life.expired() || range_->revision != revision) return;
            range_->baseline = selection;
            range_->has_end = true;
            if(services_.dispatcher)range_->clock_origin=services_.dispatcher->now();
            range_->vertical_frame=range_->horizontal_frame=0;
        }
        range_->end = position;
        range_->state_mask = event.modifiers;
        range_->last_modifiers = event.modifiers;
        const auto revision=range_->revision;
        const auto row_revision=row_order_revision;
        updateRangeSelection();
        if(life.expired() || range_->revision!=revision)return;
        if(row_order_revision!=row_revision){cancelRangeSelection();return;}
        scheduleAutoScroll();
        if(life.expired() || range_->revision!=revision)return;
        event.handled = true;
        redraw(rangeRectangle());
        break;
    }
    case ::cwxeditor::lib::PointerAction::Up:
    case ::cwxeditor::lib::PointerAction::Cancel: {
        if (!range_->has_start) return;
        const auto had_range = range_->has_end;
        const auto row = rowAt(position);
        const auto modifiers = event.modifiers;
        const auto checkbox_click = range_->checkbox_click;
        const auto prior_revision=range_->revision;
        cancelRangeSelection();
        if(life.expired() || range_->revision!=prior_revision+1)return;
        if (!had_range && !checkbox_click && row &&
            !hasModifier(modifiers, ::cwxeditor::lib::Modifiers::Control) &&
            !hasModifier(modifiers, ::cwxeditor::lib::Modifiers::Shift)) {
            setSelection({*row});
            if(life.expired() || range_->revision!=prior_revision+1)return;
        }
        // DのmouseUpは、修飾なしのクリックが空のクライアント領域で終わる
        // と、フォーカス中の表の選択を消す。取消や表の外での解放は何もしない。
        if (!row && !had_range && !checkbox_click && primary &&
            event.action == lib::PointerAction::Up &&
            !hasModifier(modifiers, ::cwxeditor::lib::Modifiers::Control) &&
            !hasModifier(modifiers, ::cwxeditor::lib::Modifiers::Shift) &&
            !lib::editor_window_host_options().all_mode && services_.gui &&
            services_.gui->focused_widget().get() == this &&
            position.x >= 0.0F && position.x < bounds().width &&
            position.y >= headerHeight() && position.y < bounds().height) {
            setSelection({});
            if (life.expired() || range_->revision != prior_revision + 1) return;
        }
        if (services_.gui) {
            try {
                services_.gui->capture_pointer(
                    std::static_pointer_cast<TableView>(shared_from_this()), false);
            } catch (const std::bad_weak_ptr&) {
                // 押下時の対応するフォーカス・キャプチャ経路を参照。
            }
        }
        // 認識された二回目のクリックにも、別の共通DoubleClick
        // イベントが必要。この表の既定の解放時処理だけを譲り、後
        // 続の所有元リスナーが必要なら解放を明示的に消費する。
        event.handled = !event.allow_deferred_double_click;
        break;
    }
    case ::cwxeditor::lib::PointerAction::Enter:
    case ::cwxeditor::lib::PointerAction::Leave:
    case ::cwxeditor::lib::PointerAction::DoubleClick:
    case ::cwxeditor::lib::PointerAction::LongPress:
    default:
        break;
    }
}

void TableModel::handleKeyDown(::cwxeditor::lib::KeyEvent& event) {
    if (!range_) return;
    range_->state_mask = event.modifiers;
    range_->last_modifiers = event.modifiers;
    if (event.key == ::cwxeditor::lib::Key::A &&
        hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Control)) {
        selectAll();
        event.handled = true;
    }
}

void TableModel::handleKeyUp(::cwxeditor::lib::KeyEvent& event) {
    if (!range_) return;
    range_->state_mask = event.modifiers;
    range_->last_modifiers = event.modifiers;
}

TableModelPtr rangeSelectableTable(const Parent parent, const Style style,
                                   Services services) {
    auto table = std::make_shared<TableModel>(style, std::move(services));
    if ((style & STYLE_MULTI_SELECT) != 0U) table->installRangeSelection();
    if (parent) parent->add(table);
    return table;
}

TableModelPtr rangeSelectableTable(Container* const parent, const Style style,
                                   Services services) {
    auto table = std::make_shared<TableModel>(style, std::move(services));
    if ((style & STYLE_MULTI_SELECT) != 0U) table->installRangeSelection();
    if (parent) parent->add(table);
    return table;
}

FullTableColumn::FullTableColumn(const TableViewPtr table, const Style style)
    : table_(table) {
    if (!table_) return;
    if (const auto model = asModel(table_)) {
        column_ = model->addColumn({}, 0.0F, style);
        column_->resizable(false);
        resize_listener_token_ = model->addResizeListener([this] { resize(); });
        resize();
    } else {
        const auto index = table_->columns.size();
        table_->columns.push_back({{}, 0.0F, ::cwxeditor::lib::TextAlignment::Start, false, false});
        column_ = std::make_shared<TableColumn>(table_, index, style);
        column_->resizable(false);
        resize();
    }
}

FullTableColumn::~FullTableColumn() {
    if (resize_listener_token_ != 0U) {
        if (const auto model = asModel(table_))
            model->removeResizeListener(resize_listener_token_);
    }
}

void FullTableColumn::resize() {
    if (!table_ || !column_) return;
    // 配置規則は共通の記述子に保つ。サイズ変更ごとに
    // 、前フレームの画素幅を指定幅へ焼き込まない。
    table_->columns[column_->index()].stretch = 1.0F;
    column_->width(pack_width_);
    if (const auto model = asModel(table_)) model->redraw();
}

namespace {
void exchangeRows(const TableViewPtr& table, std::size_t first, std::size_t second) {
    std::vector<std::size_t> order;
    order.reserve(table->rows.size());
    for (std::size_t row = 0; row < table->rows.size(); ++row) order.push_back(row);
    std::swap(order[first], order[second]);
    if (const auto model = asModel(table)) {
        if (model->reorderRows(order)) model->redraw();
    } else {
        (void)lib::reorder_table_rows(*table, order);
    }
}
}

void upItem(const TableViewPtr table, const std::size_t index) {
    if (!table || index == 0U || index >= table->rows.size()) return;
    exchangeRows(table, index - 1U, index);
}

void downItem(const TableViewPtr table, const std::size_t index) {
    if (!table || index >= table->rows.size() || table->rows.size() - index <= 1U) return;
    exchangeRows(table, index + 1U, index);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtable
