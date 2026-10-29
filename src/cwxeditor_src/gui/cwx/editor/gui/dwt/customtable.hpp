#pragma once

// cwx/editor/gui/dwt/customtable.d
// の移植可能なC++20実装。
// 範囲選択、並べ替えの補助、余っ
// た幅を使う列をTableModel
// で保持し、 共通libの部品
// ・イベント・描画・ドラッグ・
// レイアウトだけを公開する。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <any>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::customtable {

using Component = ::cwxeditor::lib::Widget;
using Container = ::cwxeditor::lib::Container;
using Parent = std::shared_ptr<Container>;
using Services = ::cwxeditor::lib::Services;
using Point = ::cwxeditor::lib::Point;
using Rect = ::cwxeditor::lib::Rect;
using Size = ::cwxeditor::lib::Size;
using Color = ::cwxeditor::lib::Color;
using ResourceId = ::cwxeditor::lib::ResourceId;
using TableView = ::cwxeditor::lib::TableView;
using TableViewPtr = std::shared_ptr<TableView>;
using SelectionIndices = std::vector<std::size_t>;
using SelectionHandler = std::function<void(const SelectionIndices&)>;
using EditHandler = std::function<void(std::size_t, std::size_t, std::string)>;
using Action = ::cwxeditor::lib::Action;
using Style = std::uint32_t;

// 表のスタイルビットはこの共通モジュールで定義する。 ウ
// ィンドウツールキットの数値ではなく、customtable.d
// が使う三つの機能を指定するための値とする。
inline constexpr Style STYLE_NONE = 0U;
inline constexpr Style STYLE_MULTI_SELECT = 1U << 0U;
inline constexpr Style STYLE_CHECKABLE = 1U << 1U;
inline constexpr Style STYLE_VIRTUAL = 1U << 2U;
inline constexpr Style STYLE_HEADER = 1U << 3U;
inline constexpr Style STYLE_READ_ONLY = 1U << 4U;

// 従来スタイル式を渡していた箇所を読みやすくする短
// い別名。 OS固有のスタイル型は持ち込まない。
inline constexpr Style MULTI_SELECT = STYLE_MULTI_SELECT;
inline constexpr Style CHECKABLE = STYLE_CHECKABLE;
inline constexpr Style VIRTUAL = STYLE_VIRTUAL;
inline constexpr Style HEADER = STYLE_HEADER;

inline constexpr Color lineColor{64U, 160U, 222U, 255U};
inline constexpr Color fillColor{64U, 160U, 222U, 96U};

enum class SortDirection : std::int32_t { None = 0, Up = 1, Down = 2 };

inline constexpr std::int32_t SORT_NONE = static_cast<std::int32_t>(SortDirection::None);
inline constexpr std::int32_t SORT_UP = static_cast<std::int32_t>(SortDirection::Up);
inline constexpr std::int32_t SORT_DOWN = static_cast<std::int32_t>(SortDirection::Down);

// 表示だけの更新では、論理行の同一性、セルの編集部品・値、選択
// 、表示位置を保つ。 省略された項目は現在のセルのままとする。
struct TableCellDisplayUpdate {
    std::size_t row{};
    std::size_t column{};
    std::optional<std::string> text;
    std::optional<ResourceId> image;
};

class TableModel;

/// 共通の表の列ハンドル。行データは所有しない。 親の表が列
/// の記述子を所有し、列一覧の中でこのハンドルを維持する。
class TableColumn final : public std::enable_shared_from_this<TableColumn> {
public:
    using Listener = Action;

    TableColumn(std::shared_ptr<TableView> table, std::size_t index,
                Style style = STYLE_NONE);
    ~TableColumn();

    TableColumn(const TableColumn&) = delete;
    TableColumn& operator=(const TableColumn&) = delete;

    [[nodiscard]] std::shared_ptr<TableView> table() const noexcept { return table_.lock(); }
    [[nodiscard]] std::shared_ptr<TableView> parent() const noexcept { return table_.lock(); }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] Style style() const noexcept { return style_; }

    [[nodiscard]] std::string title() const;
    void title(std::string value);
    [[nodiscard]] float width() const noexcept;
    void width(float value) noexcept;
    [[nodiscard]] bool resizable() const noexcept;
    void resizable(bool value) noexcept;
    [[nodiscard]] bool sortable() const noexcept;
    void sortable(bool value) noexcept;

    /// ネイティブ側から届く、列見出しの選択を再現する。
    void select();
    void dispose();

    [[nodiscard]] std::uint64_t addSelectionListener(Listener listener);
    void removeSelectionListener(std::uint64_t token) noexcept;
    [[nodiscard]] std::uint64_t addDisposeListener(Listener listener);
    void removeDisposeListener(std::uint64_t token) noexcept;

private:
    struct ListenerEntry {
        std::uint64_t token{};
        Listener callback;
    };

    std::weak_ptr<TableView> table_;
    std::size_t index_{};
    Style style_{};
    std::vector<ListenerEntry> selection_listeners_;
    std::vector<ListenerEntry> dispose_listeners_;
    std::uint64_t next_listener_token_{1U};
    bool disposed_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
};

using TableColumnPtr = std::shared_ptr<TableColumn>;

/// lib::TableRowで表せない行項目をここへ保持し、
/// 並べ替え・上下移動でもDの表と同じようにデータを保つ。
struct TableRowState {
    std::any data;
    std::vector<std::string> text;
    std::vector<ResourceId> image;
    Style style{};
    bool checked{};
    bool grayed{};
    bool cursor{};
};

/// customtable.dに必要な最小限の状態を追加したTableView
/// 。 各バックエンドからは通常のlib::TableViewとして扱える。
class TableModel final : public TableView,
                         public std::enable_shared_from_this<TableModel> {
public:
    using PaintHandler = std::function<void(::cwxeditor::lib::DrawingContext&, Rect)>;
    using ResizeHandler = Action;

    explicit TableModel(Style style = STYLE_NONE, Services services = {});
    ~TableModel() override;

    TableModel(const TableModel&) = delete;
    TableModel& operator=(const TableModel&) = delete;

    [[nodiscard]] Style style() const noexcept { return style_; }
    void style(Style value) noexcept;
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    void services(Services value) noexcept { services_ = std::move(value); }

    [[nodiscard]] bool multiSelect() const noexcept { return multi_select; }
    [[nodiscard]] bool checkable() const noexcept { return TableView::checkable; }
    [[nodiscard]] bool isVirtualized() const noexcept { return virtualized; }

    /// 範囲選択のイベント処理を接続する。rangeSelectableTable()
    /// は自動で呼ぶ。 TableModel
    /// を直接作る呼び出し元向けに公開する。
    void installRangeSelection();
    // Windows版DのCardPaneでは、空白での押下時は未選
    // 択行を選ばず、 最初の移動でクリックと範囲ドラッグを区別する。
    void deferUnselectedWhitespaceDownSelection(bool value) noexcept {
        defer_unselected_whitespace_down_selection_ = value;
    }
    using PointerHandler = std::function<void(::cwxeditor::lib::PointerEvent&)>;
    /// 範囲処理後のポインター通知を追加する。 登録後にon_pointer
    /// を置き換えると共通の選択経路を迂回するため、置き換えないこと。
    [[nodiscard]] std::uint64_t addPointerListener(PointerHandler callback);
    void removePointerListener(std::uint64_t token) noexcept;
    [[nodiscard]] bool rangeSelectionActive() const noexcept;
    [[nodiscard]] std::optional<Rect> rangeRectangle() const noexcept;
    void cancelRangeSelection() noexcept;

    void setSelection(SelectionIndices indices, bool notify = true);
    void clearSelection(bool notify = true);
    void select(std::size_t index, bool notify = true);
    void deselect(std::size_t index, bool notify = true);
    [[nodiscard]] bool isSelected(std::size_t index) const noexcept;
    [[nodiscard]] const SelectionIndices& selectionIndices() const noexcept { return selection; }
    void selectAll(bool notify = true);
    void notifySelection();

    [[nodiscard]] std::uint64_t addSelectionListener(SelectionHandler listener);
    void removeSelectionListener(std::uint64_t token) noexcept;
    [[nodiscard]] std::uint64_t addEditListener(EditHandler listener);
    void removeEditListener(std::uint64_t token) noexcept;
    [[nodiscard]] std::uint64_t addResizeListener(ResizeHandler listener);
    void removeResizeListener(std::uint64_t token) noexcept;
    void notifyResize();

    [[nodiscard]] TableColumnPtr addColumn(std::string title = {}, float width = 0.0F,
                                           Style style = STYLE_NONE);
    [[nodiscard]] TableColumnPtr column(std::size_t index) const;
    [[nodiscard]] const std::vector<TableColumnPtr>& columnHandles() const noexcept {
        return column_handles_;
    }

    void ensureRowState();
    // 二乗時間になる行ごとの同期を避け、一括読み込みの前に一度だけ同期する。
    [[nodiscard]] const std::vector<TableRowState>& rowStates() const;
    [[nodiscard]] const TableRowState& rowState(std::size_t index) const;
    [[nodiscard]] TableRowState& rowState(std::size_t index);
    void setRowState(std::size_t index, TableRowState state);
    void editCell(std::size_t row, std::size_t column, std::string text);
    // DWTの補助データも含め、所有vectorで行を並べ替える。
    // 呼び出し元が状態を確定するまで、通知・再描画は行わない。
    [[nodiscard]] bool reorderRows(const std::vector<std::size_t>& new_to_old);
    // 表示キャッシュだけでなく論理行を置き換える。任意の補助データは新し
    // い件数に合わせ、 省略時は古いデータ・スタイルを使わず空で始める。
    // 公開セルから文字・画像・チェック状態を取り、通知・再描画はしない。
    [[nodiscard]] bool replaceRows(std::vector<::cwxeditor::lib::TableRow> new_rows,
                     SelectionIndices new_selection,
                     std::vector<TableRowState> new_states = {},
                     std::optional<std::size_t> reveal = {},
                     std::function<bool()> current = {},
                     bool reset_top = false);
    // 公開前に全セルと補助データを準備する。アドレスが不正、または論理
    // 行の版が古ければ一括更新を拒否する。 通知・再描画は行わず、replaceRows
    // と異なり表示更新では行内編集を無効にしない。
    [[nodiscard]] bool updateCellDisplays(
        const std::vector<TableCellDisplayUpdate>& updates,
        std::uint64_t expected_row_revision);
    // 一覧を再構成する側が使う明示的な削除。SWTのキャッシュ無効化とは別に扱う。
    void clearAll();

    [[nodiscard]] SortDirection sortDirection() const noexcept { return sort_direction_; }
    void sortDirection(SortDirection value) noexcept { sort_direction_ = value; }
    [[nodiscard]] const TableColumnPtr& sortColumn() const noexcept { return sort_column_; }
    void sortColumn(TableColumnPtr value) noexcept { sort_column_ = std::move(value); }

    void setCursor(std::optional<std::size_t> row, std::size_t column = 0U) noexcept;
    [[nodiscard]] std::optional<std::size_t> cursorRow() const noexcept { return cursor_row_; }
    [[nodiscard]] std::size_t cursorColumn() const noexcept { return cursor_column_; }

    void setRowHeight(float value) noexcept;
    [[nodiscard]] float rowHeight() const noexcept;
    void setHeaderHeight(float value) noexcept;
    [[nodiscard]] float headerHeight() const noexcept;
    void setHeaderVisible(bool value) noexcept { header_visible = value; }
    [[nodiscard]] bool headerVisible() const noexcept { return header_visible; }
    void setHorizontalOffset(float value) noexcept;
    [[nodiscard]] float horizontalOffset() const noexcept { return horizontal_offset; }
    void setVerticalOffset(float value) noexcept;
    [[nodiscard]] float verticalOffset() const noexcept { return static_cast<float>(top_index) * rowHeight(); }
    [[nodiscard]] std::optional<std::size_t> rowAt(Point position) const noexcept;
    [[nodiscard]] Rect rowBounds(std::size_t row) const noexcept;
    [[nodiscard]] Rect cellBounds(std::size_t row, std::size_t column) const noexcept;
    [[nodiscard]] bool pointInCellContent(Point position, std::size_t row,
                                           std::size_t column) const noexcept;

    /// 範囲の強調表示を描いてから、任意のホストの表描画を呼ぶ。
    /// バックエンドはTableViewの描画処理から呼び出せる。
    void render(::cwxeditor::lib::DrawingContext& context, Rect area);
    PaintHandler on_paint;
    Action on_drag_detected;
    std::function<::cwxeditor::lib::ClipboardData()> drag_data_provider;
    std::function<void(::cwxeditor::lib::DropOperation)> drag_finished;

    void redraw(std::optional<Rect> dirty = std::nullopt);
    void setRedraw(bool value) noexcept { redraw_enabled_ = value; }
    [[nodiscard]] bool redrawEnabled() const noexcept { return redraw_enabled_; }

private:
    struct RangeState;
    struct ListenerEntry {
        std::uint64_t token{};
        SelectionHandler callback;
    };
    struct ResizeEntry {
        std::uint64_t token{};
        ResizeHandler callback;
    };
    struct EditEntry {
        std::uint64_t token{};
        EditHandler callback;
    };
    struct PointerListenerEntry {
        std::uint64_t token{};
        PointerHandler callback;
    };

    Style style_{};
    Services services_{};
    std::vector<TableRowState> row_state_;
    std::vector<TableColumnPtr> column_handles_;
    std::vector<ListenerEntry> selection_listeners_;
    std::vector<EditEntry> edit_listeners_;
    std::vector<ResizeEntry> resize_listeners_;
    std::vector<PointerListenerEntry> pointer_listeners_;
    std::uint64_t next_listener_token_{1U};
    SortDirection sort_direction_{SortDirection::None};
    TableColumnPtr sort_column_{};
    std::optional<std::size_t> cursor_row_{};
    std::size_t cursor_column_{};
    bool redraw_enabled_{true};
    bool defer_unselected_whitespace_down_selection_{};
    std::shared_ptr<RangeState> range_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t selection_revision_{};

    void handlePointer(::cwxeditor::lib::PointerEvent& event);
    void handleKeyDown(::cwxeditor::lib::KeyEvent& event);
    void handleKeyUp(::cwxeditor::lib::KeyEvent& event);
    void updateRangeSelection();
    void autoScrollOnce();
    void scheduleAutoScroll();
    void drawRange(::cwxeditor::lib::DrawingContext& context, Rect area);
};

using TableModelPtr = std::shared_ptr<TableModel>;

/// 範囲選択可能な表を作る。戻り値は通常のlib::TableView
/// なので、 親へ接続したりバックエンドへ渡したりできる。
[[nodiscard]] TableModelPtr rangeSelectableTable(Parent parent, Style style,
                                                  Services services = {});
[[nodiscard]] TableModelPtr rangeSelectableTable(Container* parent, Style style,
                                                  Services services = {});
[[nodiscard]] inline TableModelPtr rangeSelectableTable(std::nullptr_t, Style style,
                                                         Services services = {}) {
    return rangeSelectableTable(static_cast<Container*>(nullptr), style, std::move(services));
}

/// TableRowState::dataで行を並べ替え、
/// 文字・画像・チェック状態・選択・カーソル位置は保持する。
template <typename DataT>
class TableSorter final {
public:
    using Compare = std::function<bool(const DataT&, const DataT&)>;

    explicit TableSorter(TableColumnPtr column, Compare compare,
                         Compare reverse_compare = {})
        : column_(std::move(column)), compare_(std::move(compare)),
          reverse_compare_(std::move(reverse_compare)) {
        if (column_) selection_listener_token_ = column_->addSelectionListener(
            [this] { doSort(); });
    }

    ~TableSorter() {
        if (column_ && selection_listener_token_ != 0U)
            column_->removeSelectionListener(selection_listener_token_);
    }

    TableSorter(const TableSorter&) = delete;
    TableSorter& operator=(const TableSorter&) = delete;
    TableSorter(TableSorter&&) = delete;
    TableSorter& operator=(TableSorter&&) = delete;

    std::vector<Action> sortedEvent;

    [[nodiscard]] const TableColumnPtr& column() const noexcept { return column_; }
    [[nodiscard]] bool compC(const TableRowState& left, const TableRowState& right) const;
    void doSort(std::int32_t direction);
    void doSort(SortDirection direction) { doSort(static_cast<std::int32_t>(direction)); }
    void doSortR();
    void doSort();

private:
    TableColumnPtr column_;
    Compare compare_;
    Compare reverse_compare_;
    std::uint64_t selection_listener_token_{};
    SortDirection last_direction_{SortDirection::None};

    [[nodiscard]] static const DataT* data(const TableRowState& row) noexcept;
    [[nodiscard]] bool compareRows(const TableRowState& left,
                                   const TableRowState& right) const;
};

/// 他の列が使わない幅を、すべて使う列。
class FullTableColumn final {
public:
    explicit FullTableColumn(TableViewPtr table, Style style = STYLE_NONE);
    explicit FullTableColumn(TableModelPtr table, Style style = STYLE_NONE)
        : FullTableColumn(std::static_pointer_cast<TableView>(std::move(table)), style) {}
    ~FullTableColumn();

    FullTableColumn(const FullTableColumn&) = delete;
    FullTableColumn& operator=(const FullTableColumn&) = delete;

    [[nodiscard]] const TableColumnPtr& column() const noexcept { return column_; }
    void resize();
    void setPackWidth(float value) noexcept { pack_width_ = value; }
    [[nodiscard]] float packWidth() const noexcept { return pack_width_; }

private:
    TableViewPtr table_{};
    TableColumnPtr column_{};
    float pack_width_{50.0F};
    std::uint64_t resize_listener_token_{};
};

/// customtable.dと同じ項目の置き換え順で、index
/// の直前の行をindexへ移す。 対の関数は直後の行をindex
/// へ移す。どちらも共通の全行項目と選択添字を保つ。
void upItem(TableViewPtr table, std::size_t index);
void downItem(TableViewPtr table, std::size_t index);

template <typename DataT>
const DataT* TableSorter<DataT>::data(const TableRowState& row) noexcept {
    return std::any_cast<DataT>(&row.data);
}

template <typename DataT>
bool TableSorter<DataT>::compareRows(const TableRowState& left,
                                     const TableRowState& right) const {
    const auto* left_data = data(left);
    const auto* right_data = data(right);
    if (left_data == nullptr || right_data == nullptr || !compare_) return false;
    return compare_(*left_data, *right_data);
}

template <typename DataT>
bool TableSorter<DataT>::compC(const TableRowState& left,
                               const TableRowState& right) const {
    const auto table = column_ ? column_->parent() : TableViewPtr{};
    const auto model = std::dynamic_pointer_cast<TableModel>(table);
    const auto direction = model ? model->sortDirection() : last_direction_;
    if (direction == SortDirection::Up) return compareRows(left, right);
    if (reverse_compare_) {
        const auto* left_data = data(left);
        const auto* right_data = data(right);
        if (left_data == nullptr || right_data == nullptr) return false;
        return reverse_compare_(*left_data, *right_data);
    }
    const auto* left_data = data(left);
    const auto* right_data = data(right);
    if (left_data == nullptr || right_data == nullptr || !compare_) return false;
    return compare_(*right_data, *left_data);
}

template <typename DataT>
void TableSorter<DataT>::doSort(const std::int32_t direction) {
    const auto table = column_ ? column_->parent() : TableViewPtr{};
    if (!table) return;

    SortDirection sort_direction = SortDirection::None;
    if (direction == SORT_UP) sort_direction = SortDirection::Up;
    else if (direction == SORT_DOWN) sort_direction = SortDirection::Down;
    const auto model = std::dynamic_pointer_cast<TableModel>(table);
    if (sort_direction == SortDirection::None) {
        last_direction_ = sort_direction;
        if (model) model->sortDirection(sort_direction);
        return;
    }

    // DのSWT.VIRTUALは背後のコレクションでなくSetData
    // のキャッシュを無効にする。 ここでは両モードが行を所有するため、
    // データを保つ同じ経路を使う。 比較関数は読み取り専用とし、行や並
    // べ替え情報を変更する前に順列を準備して、比較失敗時に旧表を残す。
    const auto compare = compare_, reverse = reverse_compare_;
    const auto column = column_;
    const auto callbacks = sortedEvent;
    const auto states = model ? model->rowStates() : std::vector<TableRowState>(table->rows.size());
    std::vector<std::size_t> order;
    order.reserve(table->rows.size());
    for (std::size_t index = 0; index < table->rows.size(); ++index) {
        order.push_back(index);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto* a = data(states[left]);
        const auto* b = data(states[right]);
        // 補助データがない行は、定義された安定した区分へ分ける。
        // すべての行と等しい扱いにして、厳密な弱順序を破らない。
        if (!a || !b) return a && !b;
        if (sort_direction == SortDirection::Up) return compare && compare(*a, *b);
        return reverse ? reverse(*a, *b) : compare && compare(*b, *a);
    });
    if (model ? !model->reorderRows(order) : !lib::reorder_table_rows(*table, order)) return;
    last_direction_ = sort_direction;
    if (model) {
        model->sortDirection(sort_direction);
        model->sortColumn(column);
        model->redraw();
    }
    // 完了通知で行が置き換わったり並べ替え器が破棄されたりする場
    // 合がある。 以後は古い行・選択状態をモデルへ書き戻さない。
    for (const auto& callback : callbacks)
        if (callback) callback();
}

template <typename DataT>
void TableSorter<DataT>::doSortR() {
    const auto table = column_ ? column_->parent() : TableViewPtr{};
    if (!table) return;
    const auto model = std::dynamic_pointer_cast<TableModel>(table);
    if (model && model->sortColumn() == column_ &&
        model->sortDirection() != SortDirection::None) {
        doSort(model->sortDirection());
    } else if (!model && last_direction_ != SortDirection::None) {
        doSort(last_direction_);
    }
}

template <typename DataT>
void TableSorter<DataT>::doSort() {
    const auto table = column_ ? column_->parent() : TableViewPtr{};
    if (!table) return;
    const auto model = std::dynamic_pointer_cast<TableModel>(table);
    const auto current_column = model ? model->sortColumn() : TableColumnPtr{};
    const auto current_direction = model ? model->sortDirection() : last_direction_;
    if (current_column != column_ || current_direction == SortDirection::None ||
        current_direction == SortDirection::Down) {
        doSort(SortDirection::Up);
    } else {
        doSort(SortDirection::Down);
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtable
