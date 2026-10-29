#include "couponview.hpp"
#include "dmenu.hpp"
#include "chooser.hpp"
#include "dutils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <limits>
#include <set>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::couponview {

namespace {

[[nodiscard]] bool isValued(const CVType type) noexcept {
    return type == CVType::Cast || type == CVType::Valued;
}

[[nodiscard]] bool isCast(const CVType type) noexcept {
    return type == CVType::Cast;
}

[[nodiscard]] String replaceFirst(String text, StringView marker,
                                   StringView value) {
    const auto position = text.find(marker);
    if (position != String::npos) text.replace(position, marker.size(), value);
    return text;
}

[[nodiscard]] String toString(const std::int32_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::int32_t clampValue(const std::int32_t value,
                                       const std::int32_t minimum,
                                       const std::int32_t maximum) noexcept {
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] std::vector<std::size_t> normalizeIndices(
    std::vector<std::size_t> values, const std::size_t count) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                [count](const std::size_t value) {
                                    return value >= count;
                                }),
                 values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

[[nodiscard]] bool containsIndex(const std::vector<std::size_t>& values,
                                  const std::size_t index) {
    return std::find(values.begin(), values.end(), index) != values.end();
}

[[nodiscard]] std::size_t insertionIndex(const std::vector<std::size_t>& values,
                                          const std::size_t size) {
    return values.empty() ? size : std::min(values.front(), size);
}

[[nodiscard]] chooser::ChooserProperties couponChooserProperties(
    dprops::Props* prop, commons::Commons* common, const SummaryPtr& summary,
    const CVType type) {
    chooser::ChooserProperties properties;
    const auto skin = common ? common->skin() : nullptr;
    if (prop) {
        properties.logicalSort = prop->var().logicalSort.value;
        properties.usedCouponToCombo = prop->var().usedCouponToCombo.value;
        properties.useNamesAfterStandard = prop->var().useNamesAfterStandard.value;
        dutils::EditorContext context;
        context.prop = prop;
        context.skin = skin.get();
        context.summary = summary.get();
        properties.standardCoupons = dutils::castCoupons(context, false, {}, false, false);
        properties.standardCouponsForValued = dutils::castCoupons(context, false, {}, false, true);
        auto excluded = type == CVType::Valued ? properties.standardCouponsForValued
                                                : properties.standardCoupons;
        chooser::setCouponFilter(properties,
            [prop = prop](const StringView name) {
                return prop->sys().isCouponType(String{name}, CouponType::System);
            }, std::move(excluded));
    }
    return properties;
}

[[nodiscard]] chooser::CouponComboType comboType(const CVType type) {
    return type == CVType::Cast ? chooser::CouponComboType::Cast
         : type == CVType::Valued ? chooser::CouponComboType::Valued
         : chooser::CouponComboType::AllCoupons;
}

}  // namespace

std::atomic<std::uint64_t> CouponViewBase::next_id_{1U};

class CouponViewBase::UndoCoupons final : public undo::Undo {
public:
    explicit UndoCoupons(CouponViewBase& owner) : owner_(&owner) { save(); }

    void save() {
        coupons_ = owner_->snapshotCoupons();
        selected_ = owner_->snapshotSelection();
    }

    void impl() {
        const auto current = owner_->snapshotCoupons();
        const auto current_selection = owner_->snapshotSelection();
        owner_->applySnapshot(coupons_, selected_);
        coupons_ = current;
        selected_ = current_selection;
    }

    void undo() override { impl(); }
    void redo() override { impl(); }
    void dispose() override {}

private:
    CouponViewBase* owner_{};
    CouponArray coupons_;
    std::vector<std::size_t> selected_;
};

CouponViewBase::CouponViewBase(const CVType type, commons::Commons& comm,
                               SummaryPtr summary, UseCounterPtr useCounter,
                               Parent parent, const std::uint32_t style,
                               std::function<bool()> catchMod,
                               FeatureProvider feature)
    : type_(type),
      comm_(&comm),
      commons_lifetime_(comm.lifetimeToken()),
      prop_(comm.prop()),
      summary_(std::move(summary)),
      use_counter_(std::move(useCounter)),
      parent_(std::move(parent)),
      read_only_((style & customtable::STYLE_READ_ONLY) != 0U),
      is_history_view_(static_cast<bool>(feature)),
      catch_mod_(std::move(catchMod)),
      feature_provider_(std::move(feature)) {
    const auto number = next_id_.fetch_add(1U, std::memory_order_relaxed);
    id_ = "couponview-" + std::to_string(number);
    if (prop_) {
        value_maximum_ = static_cast<std::int32_t>(prop_->var().couponValueMax.value);
        value_minimum_ = -value_maximum_;
        const auto max_undo = prop_->var().undoMaxEtc.value;
        undo_manager_ = std::make_shared<undo::UndoManager>(
            max_undo < 0 ? 0U : static_cast<std::size_t>(max_undo));
    } else {
        undo_manager_ = std::make_shared<undo::UndoManager>(1024U);
    }
    createControl(style);
    refreshSkinCoupons();
    refreshNameCandidates();
    dispose_ = std::make_unique<Dispose>(*this);
    key_filter_ = std::make_unique<KeyDownFilter>(*this);
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const std::function<void(const lib::WidgetPtr&)> bind_keys =
        [this, alive, &bind_keys](const lib::WidgetPtr& widget) {
            if (!widget) return;
            const auto container = std::dynamic_pointer_cast<lib::Container>(widget);
            const auto previous = widget->on_key_down;
            const bool coupon_table = widget.get() == coupons_.get();
            // 受動的な配置コンテナーには、合成したキーハンドラーを付けない
            // 。FocusStateMachineがハンドラーをキー入力の
            // 契約とみなすため。既存ハンドラーがあれば保持してフィルターし
            // 、再帰を続けて実際の全コントロールにはフィルターを適用する。
            if (!container || previous) {
                widget->on_key_down = [this, alive, previous, coupon_table](lib::KeyEvent& event) {
                    if (alive.expired()) return;
                    // DのTable.selectAllはSelectionを発生させ
                    // ない。共通の表ハンドラーより前に、両ホストで同じ動作を維持する。
                    if (!event.handled && coupon_table &&
                        event.key == lib::Key::A &&
                        (static_cast<std::uint32_t>(event.modifiers) &
                         static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U) {
                        event.handled = true;
                        selectAll();
                        return;
                    }
                    if (previous) previous(event);
                    if (alive.expired()) return;
                    if (!event.handled && key_filter_)
                        key_filter_->handleEvent(event);
                };
            }
            if (container)
                for (const auto& child : container->children()) bind_keys(child);
        };
    bind_keys(panel_);
    drop_listener_ = std::make_unique<CDropListener>(*this);
    drag_listener_ = std::make_unique<CDragListener>(*this);
    clipboard_actions_ = std::make_unique<CouponTCPD>(*this);
    refUndoMax();
    refMenu(0);
    ref_skin_connection_ = comm.refSkin.add([this, alive] {
        if (alive.expired()) return;
        refreshSkinCoupons();
        if (alive.expired()) return;
        refreshNameCandidates();
        if (alive.expired()) return;
        rebuildTable(selected_);
    });
    ref_coupons_connection_ = comm.refCoupons.add([this, alive] {
        if (alive.expired()) return;
        refreshSkinCoupons();
        if (alive.expired()) return;
        refreshNameCandidates();
        if (alive.expired()) return;
        rebuildTable(selected_);
    });
    ref_undo_connection_ = comm.refUndoMax.add([this, alive] { if (!alive.expired()) refUndoMax(); });
    ref_version_connection_ = comm.refDataVersion.add([this, alive] {
        if (alive.expired()) return;
        refreshNameCandidates();
        if (alive.expired()) return;
        refDataVersion();
        if (alive.expired()) return;
        rebuildTable(selected_);
    });
    ref_menu_connection_ = comm.refMenu.add([this, alive](const types::MenuID id) {
        if (alive.expired()) return;
        refMenu(static_cast<int>(id));
    });
}

CouponViewBase::~CouponViewBase() {
    menu_lifetime_.reset();
    if (toolbar_) toolbar_->on_command = {};
    if (new_coupon_) {
        new_coupon_->on_text_changed = {};
        new_coupon_->on_selection_changed = {};
        new_coupon_->on_key_down = {};
    }
    if (coupon_type_) coupon_type_->on_selection_changed = {};
    if (comm_ && !commons_lifetime_.expired()) {
        comm_->refSkin.remove(ref_skin_connection_);
        comm_->refCoupons.remove(ref_coupons_connection_);
        comm_->refUndoMax.remove(ref_undo_connection_);
        comm_->refDataVersion.remove(ref_version_connection_);
        comm_->refMenu.remove(ref_menu_connection_);
    }
    if (comm_ && coupons_) {
        if (auto backend = coupons_->services().drag_drop) backend->unbind(coupons_);
    }
    if (coupons_) {
        coupons_->on_selection_changed = {};
        coupons_->on_cell_edited = {};
        coupons_->on_inline_editor_created = {};
        coupons_->on_key_down = {};
        coupons_->on_key_up = {};
        coupons_->on_pointer = {};
        coupons_->on_column_resized = {};
        coupons_->drag_data_provider = {};
        coupons_->drag_finished = {};
    }
    if (dispose_) dispose_->widgetDisposed();
}

void CouponViewBase::addModifyListener(ModifyHandler handler) {
    if (handler) modEvent.push_back(std::move(handler));
}

void CouponViewBase::raiseModifyEvent() {
    // DのGCはデリゲート受信先の領域を保持する。ネイティブC++
    // のコールバックは所有元を破棄・置換し得るため、解放済
    // みや置換済みの状態へ進まず、古い通知一式を停止する。
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto revision = render_revision_;
    const auto callbacks = modEvent;
    const auto final_callback = modified;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (alive.expired() || revision != render_revision_) return;
    }
    if (final_callback) final_callback();
}

void CouponViewBase::notifyRefresh() {
    refreshToolbarState();
    if (comm_ && !commons_lifetime_.expired()) comm_->refreshToolBar();
}

void CouponViewBase::refreshToolbarState() {
    if (!toolbar_) return;
    const auto values = normalizedSelection();
    const bool editable = !read_only_ && enabled_;
    const bool selected = editable && !values.empty();
    const bool named = editable && new_coupon_ && !new_coupon_->edit_text.empty();
    const auto inline_name = active_inline_name_editor_.lock();
    const bool deletable = selected && (!inline_name || !inline_name->text.empty());
    for (auto& item : toolbar_->items) {
        if (item.command == "addCoupon") item.enabled = named;
        else if (item.command == "altCoupon") item.enabled = named && selected;
        else if (item.command == "delCoupon") item.enabled = deletable;
        else if (item.command == "upCoupon") item.enabled = editable && canUp();
        else if (item.command == "downCoupon") item.enabled = editable && canDown();
    }
    ++toolbar_->item_update_revision;
}

void CouponViewBase::notifyChanged() {
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto revision = render_revision_;
    raiseModifyEvent();
    if (alive.expired() || revision != render_revision_) return;
    notifyRefresh();
}

bool CouponViewBase::finishInlineEdit(const bool commit) {
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto table = coupons_;
    const auto gui = table ? table->services().gui : nullptr;
    if (gui) (void)gui->finish_programmatic_inline_edit(table, commit);
    return !alive.expired();
}

const std::vector<std::size_t>& CouponViewBase::selection() const noexcept {
    return selected_;
}

std::vector<std::size_t> CouponViewBase::snapshotSelection() const {
    return normalizeIndices(selected_, records_.size());
}

CouponArray CouponViewBase::snapshotCoupons() const {
    // DのUndoCouponsは、未完了の名前編集で作ら
    // れた仮の空行を除く、公開couponsの状態を保存す
    // る。従来どおり、各保存値を使用中の行から切り離す。
    const auto visible = coupons();
    CouponArray result;
    result.reserve(visible.size());
    for (const auto& value : visible)
        if (value) result.push_back(std::make_shared<Coupon>(*value));
    return result;
}

void CouponViewBase::applySnapshot(const CouponArray& values,
                                   const std::vector<std::size_t>& selection_value) {
    const std::weak_ptr<const int> alive = menu_lifetime_;
    records_.clear();
    records_.reserve(values.size());
    for (const auto& value : values) {
        if (value) records_.push_back(std::make_shared<Coupon>(*value));
    }
    rebuildTable(selection_value);
    if (!alive.expired()) notifyRefresh();
}

void CouponViewBase::storeCoupons() {
    if (!undo_manager_) return;
    undo_manager_->add(std::make_shared<UndoCoupons>(*this));
}

bool CouponViewBase::undoCoupons() {
    if (!finishInlineEdit(false)) return false;
    if (read_only_ || !undo_manager_ || !undo_manager_->canUndo()) return false;
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto history = undo_manager_;
    const auto result = history->undo();
    if (!alive.expired()) notifyRefresh();
    return result;
}

bool CouponViewBase::redoCoupons() {
    if (!finishInlineEdit(false)) return false;
    if (read_only_ || !undo_manager_ || !undo_manager_->canRedo()) return false;
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto history = undo_manager_;
    const auto result = history->redo();
    if (!alive.expired()) notifyRefresh();
    return result;
}

bool CouponViewBase::canUndo() const noexcept {
    return !read_only_ && undo_manager_ && undo_manager_->canUndo();
}

bool CouponViewBase::canRedo() const noexcept {
    return !read_only_ && undo_manager_ && undo_manager_->canRedo();
}

void CouponViewBase::setReadOnly(const bool value) {
    if (read_only_ == value) return;
    read_only_ = value;
    if (new_coupon_) new_coupon_->set_enabled(!read_only_ && enabled_);
    if (coupon_type_) coupon_type_->set_enabled(!read_only_ && enabled_);
    if (coupon_value_) coupon_value_->set_enabled(!read_only_ && enabled_);
    if (toolbar_) toolbar_->set_enabled(!read_only_ && enabled_);
    if (coupons_) coupons_->set_enabled(enabled_);
    refreshToolbarState();
}

ResourceId CouponViewBase::couponImage(const StringView name,
                                       const std::int32_t value) const {
    const auto provider = image_provider_;
    if (provider) return provider(name, value);
    if (!prop_) return ResourceId{};
    const auto type = type_ == CVType::Valued ? CouponType::Normal
        : prop_->sys().couponType(String{name});
    return prop_->images().coupon(type, value);
}

CouponPtr CouponViewBase::rowCoupon(const std::size_t row) const {
    if (row >= records_.size()) return {};
    return records_[row];
}

bool CouponViewBase::hasName(const StringView name, const Coupon* except) const {
    for (const auto& value : records_) {
        if (value && value.get() != except && value->name() == name) return true;
    }
    return false;
}

String CouponViewBase::uniqueName(String base, const Coupon* except) const {
    if (!hasName(base, except)) return base;
    return ::cwxeditor::cwx::utils::createNewName(
        std::move(base),
        [this, except](String candidate) { return !hasName(candidate, except); }, true);
}

std::vector<std::size_t> CouponViewBase::normalizedSelection() const {
    if (!coupons_) return normalizeIndices(selected_, records_.size());
    return normalizeIndices(coupons_->selection, records_.size());
}

CouponArray CouponViewBase::selectedCoupons() const {
    CouponArray result;
    for (const auto index : normalizedSelection()) {
        if (const auto value = rowCoupon(index)) result.push_back(value);
    }
    return result;
}

void CouponViewBase::rebuildTable(const std::vector<std::size_t>& selection_value,
                                  const bool revealSelection) {
    if (!coupons_) return;
    const auto target = coupons_;
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto request = ++render_revision_;
    const auto epoch = target->row_order_revision;
    const auto source = records_;
    auto next_selection = normalizeIndices(selection_value, source.size());
    // Couponオブジェクトは共有される。画像プロバイダーのコールバックがプロバ
    // イダーを置換したり表示を再帰的に構築したりしても、表示値を所有して保持する。
    std::vector<std::pair<String, std::int32_t>> values;
    values.reserve(source.size());
    for (const auto& value : source)
        values.emplace_back(value ? value->name() : String{}, value ? value->value() : 0);
    const auto current = [&] {
        if (alive.expired()) return false;
        if (request != render_revision_ || target != coupons_ ||
            epoch != target->row_order_revision) return false;
        return true;
    };
    const auto source_unchanged = [&] {
        if (source != records_) return false;
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (source[index] && (source[index]->name() != values[index].first ||
                                  source[index]->value() != values[index].second)) return false;
        }
        return true;
    };
    std::vector<lib::TableRow> rows;
    std::vector<customtable::TableRowState> states;
    rows.reserve(source.size());
    states.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto& [name, value] = values[index];
        const auto image = source[index] ? couponImage(name, value) : ResourceId{};
        if (!current()) return;
        lib::TableRow row;
        row.cells.push_back({name, name, image, false, true});
        const auto name_editor = nameCreateEditor(index, 0U);
        if (!current() || !source_unchanged()) return;
        row.cells.back().editor = lib::CellEditorKind::EditableChoice;
        for (const auto& choice : name_editor->items)
            row.cells.back().editor_choices.push_back(choice.text);
        row.cells.back().warnings = warningsImpl(name);
        row.warnings = row.cells.back().warnings;
        if (isValued(type_)) {
            const auto text = toString(value);
            row.cells.push_back({text, text, {}, false, true});
            const auto value_editor = valueCreateEditor(index, 1U);
            row.cells.back().editor = lib::CellEditorKind::Number;
            row.cells.back().editor_minimum = value_editor->minimum;
            row.cells.back().editor_maximum = value_editor->maximum;
        }
        rows.push_back(std::move(row));
        customtable::TableRowState state;
        state.data = source[index];
        states.push_back(std::move(state));
    }
    if (!current() || !source_unchanged()) return;
    // 公開行、DWTデータ、表示の選択を公開する間に、コールバックや再
    // 描画を挟まない。外側のsetRedraw(false)にも従う。
    const auto reveal=revealSelection && !next_selection.empty()
        ? std::optional<std::size_t>{next_selection.back()}:std::nullopt;
    if(!target->replaceRows(std::move(rows), next_selection, std::move(states), reveal,
                            [&]{return current() && source_unchanged();})) return;
    selected_.swap(next_selection);
    target->redraw();
    if (alive.expired()) return;
    // SWTのsetSelectionはSelectionを通知しない。インラ
    // イン編集の確定後のモデル再構築で、独立に入力した追加・上書きの名前を置き
    // 換えない。実際の共通行入力の通知は通常どおりselCouponへ渡す。
    notifyRefresh();
}

CouponPtr CouponViewBase::appendCoupon(const Coupon& value, const int index,
                                       const bool select_value) {
    return appendCoupon(std::make_shared<Coupon>(value), index, select_value);
}

CouponPtr CouponViewBase::appendCoupon(const CouponPtr& value, const int index,
                                       const bool select_value) {
    if (!value) return {};
    auto copy = std::make_shared<Coupon>(*value);
    const auto insert_at = index < 0
                               ? records_.size()
                               : std::min(static_cast<std::size_t>(index), records_.size());
    records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(insert_at), copy);
    std::vector<std::size_t> next_selection = normalizedSelection();
    if (select_value) {
        next_selection = {insert_at};
    } else {
        for (auto& selected : next_selection) {
            if (selected >= insert_at) ++selected;
        }
    }
    rebuildTable(next_selection, select_value);
    return copy;
}

void CouponViewBase::addCoupon() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !new_coupon_ || new_coupon_->edit_text.empty()) return;
    if (!finishInlineEdit(true)) return;
    const auto base = new_coupon_->edit_text;
    const auto name = uniqueName(base);
    const auto value = isValued(type_) && coupon_value_
                           ? clampValue(static_cast<std::int32_t>(coupon_value_->value),
                                        value_minimum_, value_maximum_)
                           : 0;
    storeCoupons();
    const auto current = normalizedSelection();
    const auto index = is_history_view_ ? insertionIndex(current, records_.size()) : records_.size();
    static_cast<void>(appendCoupon(Coupon{name, value}, static_cast<int>(index)));
    if (operation_life.expired()) return;
    notifyChanged();
}

void CouponViewBase::altCoupon() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !new_coupon_ || new_coupon_->edit_text.empty()) return;
    if (!finishInlineEdit(true)) return;
    const auto current = normalizedSelection();
    if (current.empty()) return;
    const auto index = current.front();
    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (i != index && records_[i] && records_[i]->name() == new_coupon_->edit_text) {
            // SWTのselect(index)はSelectionなしで選択を追加
            // する。重複を拒否した場合は入力済みの名前・値と既存の選択集合を維持する
            // 。移動用の公開select(index)は、引き続き単独選択とする。
            auto next = current;
            next.push_back(i);
            selected_ = normalizeIndices(next, records_.size());
            if (coupons_) coupons_->setSelection(selected_, false);
            if (operation_life.expired()) return;
            notifyRefresh();
            return;
        }
    }
    const auto value = isValued(type_) && coupon_value_
                           ? clampValue(static_cast<std::int32_t>(coupon_value_->value),
                                        value_minimum_, value_maximum_)
                           : 0;
    storeCoupons();
    records_[index] = std::make_shared<Coupon>(new_coupon_->edit_text, value);
    // 上書きは全選択を保ったまま、最初の選択行を置き換える。
    rebuildTable(current);
    if (operation_life.expired()) return;
    notifyChanged();
}

void CouponViewBase::addCoupon(const Coupon& value) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    addCoupon(std::make_shared<Coupon>(value));
}

void CouponViewBase::addCoupon(const CouponPtr& value) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !value) return;
    if (!finishInlineEdit(true)) return;
    storeCoupons();
    const auto name = uniqueName(value->name());
    static_cast<void>(appendCoupon(Coupon{name, value->value()}));
    if (operation_life.expired()) return;
    notifyChanged();
}

void CouponViewBase::delCoupon() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    const auto values = normalizedSelection();
    if (values.empty()) return;
    delCoupon(values);
}

void CouponViewBase::delCoupon(const std::vector<int>& indices) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    std::vector<std::size_t> converted;
    converted.reserve(indices.size());
    for (const auto value : indices) {
        if (value >= 0) converted.push_back(static_cast<std::size_t>(value));
    }
    delCoupon(converted);
}

void CouponViewBase::delCoupon(const std::vector<std::size_t>& input) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_) return;
    if (!input.empty() && !finishInlineEdit(true)) return;
    auto indices = input;
    indices = normalizeIndices(std::move(indices), records_.size());
    if (indices.empty()) return;
    storeCoupons();
    // Dは行をその場で削除するため、残存する複数選択は各行に追従する。
    std::vector<std::size_t> next;
    for (const auto selected : normalizedSelection()) {
        if (std::binary_search(indices.begin(), indices.end(), selected)) continue;
        const auto removed_before = static_cast<std::size_t>(
            std::lower_bound(indices.begin(), indices.end(), selected) - indices.begin());
        next.push_back(selected - removed_before);
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    rebuildTable(next);
    if (operation_life.expired()) return;
    notifyChanged();
}

void CouponViewBase::selCoupon() {
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto revision = render_revision_;
    if (coupons_) selected_ = normalizeIndices(coupons_->selection, records_.size());
    if (!read_only_ && !selected_.empty() && selected_.front() < records_.size()) {
        const auto value = records_[selected_.front()];
        if (value && new_coupon_) {
            new_coupon_->edit_text = value->name();
            if (const auto menu = dmenu::textMenuOf(new_coupon_)) menu->reset();
            if (alive.expired() || revision != render_revision_) return;
            if (isValued(type_) && coupon_value_) coupon_value_->value = value->value();
            updateCouponType();
        }
    }
    notifyRefresh();
}

void CouponViewBase::updateCouponType() {
    if (!prop_ || !coupon_type_ || !new_coupon_) return;
    const auto type = prop_->sys().couponType(new_coupon_->edit_text);
    const auto found = coupon_type_table_.find(type);
    const auto selected = found == coupon_type_table_.end()
                              ? std::optional<std::size_t>{0U}
                              : std::optional<std::size_t>{found->second};
    (void)lib::select_combo_item(*coupon_type_, selected, false);
    uppdateCouponTypeHint();
}

void CouponViewBase::uppdateCouponTypeHint() {
    if (!coupon_type_ || !prop_ || !coupon_type_->selected_index) return;
    const auto found = coupon_type_table_reverse_.find(*coupon_type_->selected_index);
    if (found == coupon_type_table_reverse_.end()) return;
    coupon_type_->set_tooltip(typeTooltip(found->second));
}

bool CouponViewBase::canUp() const {
    const auto values = normalizedSelection();
    return !read_only_ && !values.empty() && values.front() > 0U;
}

void CouponViewBase::upCoupon() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (!canUp()) return;
    if (!finishInlineEdit(true)) return;
    auto values = normalizedSelection();
    storeCoupons();
    for (auto& index : values) {
        std::swap(records_[index], records_[index - 1U]);
        --index;
    }
    // Dは移動ごとにshowSelectionを呼ぶ。移動した行を表示内に保つ。
    rebuildTable(values, true);
    if (operation_life.expired()) return;
    notifyChanged();
}

bool CouponViewBase::canDown() const {
    const auto values = normalizedSelection();
    return !read_only_ && !values.empty() && values.back() + 1U < records_.size();
}

void CouponViewBase::downCoupon() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (!canDown()) return;
    if (!finishInlineEdit(true)) return;
    auto values = normalizedSelection();
    storeCoupons();
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        const auto index = *it;
        std::swap(records_[index], records_[index + 1U]);
    }
    for (auto& index : values) ++index;
    rebuildTable(values, true);
    if (operation_life.expired()) return;
    notifyChanged();
}

std::vector<CouponPtr> CouponViewBase::couponsFromNode(const xml::XNode& node) const {
    std::vector<CouponPtr> values;
    if (!prop_) return values;
    const system::XMLInfo version(prop_->sys(), versioninfo::LATEST_VERSION);
    if (node.name() == Coupon::XML_NAME) {
        values.push_back(Coupon::fromNode(node, version));
        return values;
    }
    if (node.name() != Coupon::XML_NAME_M) return values;
    for (const auto& child : node.element_children()) {
        if (child.name() == Coupon::XML_NAME) values.push_back(Coupon::fromNode(child, version));
    }
    return values;
}

bool CouponViewBase::canAddInitialCoupons() const {
    if (read_only_ || !isCast(type_) || !feature_provider_) return false;
    const auto feature = feature_provider_();
    std::map<String, bool> names;
    for (const auto& value : records_) {
        if (value) names[value->name()] = true;
    }
    if (feature.race) {
        for (const auto& value : feature.race->coupons()) {
            if (value && !names[value->name()]) return true;
        }
    }
    if (comm_ && comm_->skin() && prop_) {
        for (const auto& value : comm_->skin()->periodInitialCoupons(prop_->sys(), feature.period)) {
            if (value && !names[value->name()]) return true;
        }
    }
    return false;
}

void CouponViewBase::addInitialCoupons() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !isCast(type_) || !feature_provider_) return;
    if (!finishInlineEdit(true)) return;
    const auto provider = feature_provider_;
    const auto feature = provider();
    if (operation_life.expired()) return;
    std::vector<std::shared_ptr<const Coupon>> initial;
    if (feature.race) {
        for (const auto& value : feature.race->coupons()) if (value) initial.push_back(value);
    }
    if (comm_ && comm_->skin() && prop_) {
        for (const auto& value : comm_->skin()->periodInitialCoupons(prop_->sys(), feature.period)) {
            if (value) initial.push_back(value);
        }
    }
    std::map<String, bool> names;
    for (const auto& value : records_) if (value) names[value->name()] = true;
    CouponArray added;
    for (auto it = initial.rbegin(); it != initial.rend(); ++it) {
        if (!*it || names[(*it)->name()]) continue;
        // Dは既存行の名前と照合して除外するが、入力元一覧同士の重複は維持する。
        added.push_back(std::make_shared<Coupon>(**it));
    }
    if (added.empty()) return;
    storeCoupons();
    for (const auto& value : added) records_.insert(records_.begin(), value);
    std::vector<std::size_t> selected_values;
    for (std::size_t i = 0; i < added.size(); ++i) selected_values.push_back(i);
    rebuildTable(selected_values);
    if (operation_life.expired()) return;
    notifyRefresh();
}

void CouponViewBase::reverseCoupons() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !isCast(type_) || records_.size() < 2U) return;
    if (!finishInlineEdit(true)) return;
    storeCoupons();
    std::reverse(records_.begin(), records_.end());
    std::vector<std::size_t> values;
    values = normalizedSelection();
    rebuildTable(values);
    if (operation_life.expired()) return;
    notifyRefresh();
}

bool CouponViewBase::canReverseSignOfValues() const {
    if (read_only_ || type_ != CVType::Valued) return false;
    for (const auto index : normalizedSelection()) {
        if (const auto value = rowCoupon(index); value && value->value() != 0) return true;
    }
    return false;
}

void CouponViewBase::reverseSignOfValues() {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || type_ != CVType::Valued) return;
    if (!finishInlineEdit(true)) return;
    if (!canReverseSignOfValues()) return;
    storeCoupons();
    for (const auto index : normalizedSelection()) {
        if (const auto value = rowCoupon(index); value && value->value() != 0) {
            records_[index] = std::make_shared<Coupon>(value->name(), -value->value());
        }
    }
    rebuildTable(normalizedSelection());
    if (operation_life.expired()) return;
    notifyChanged();
}

void CouponViewBase::CDropListener::dragEnter(lib::DragEvent& event) {
    event.selected = owner_ && !owner_->read_only_ ? lib::DropOperation::Move
                                                   : lib::DropOperation::None;
}

void CouponViewBase::CDropListener::dragOver(lib::DragEvent& event) {
    dragEnter(event);
}

void CouponViewBase::CDropListener::drop(lib::DragEvent& event) {
    if (!owner_ || owner_->read_only_) return;
    String text;
    if (!xmlbytestransfer::extractXML(event, text)) return;
    try {
        auto node = xml::XNode::parse(text);
        // DのDropTargetEventはコントロール内の座標。共通
        // DragEventは配置後の描画面座標なので、その点をローカル
        // のrowAtへ渡さず、共通DWTのドロップ用ヒット判定を使う。
        const auto row = owner_->coupons_
            ? lib::hit_test_table_drop_row(*owner_->coupons_, event.position,
                  owner_->coupons_->rowHeight())
            : std::nullopt;
        const auto index = row ? static_cast<int>(*row)
                               : static_cast<int>(owner_->records_.size());
        const auto same = node.attr("paneId", false) == owner_->id_;
        if (same && index == owner_->drag_index_) return;
        if (!owner_->appendFromNode(node, index, same)) return;
        event.selected = same ? lib::DropOperation::Move : lib::DropOperation::Copy;
        event.handled = true;
    } catch (const std::exception&) {
        event.handled = false;
    }
}

bool CouponViewBase::appendFromNode(xml::XNode& node, const int index, const bool move) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (node.name() != Coupon::XML_NAME_M) return false;
    CouponArray values;
    try {
        values = couponsFromNode(node);
    } catch (const std::exception&) {
        return false;
    }
    if (values.empty() || read_only_) return false;
    // 下記のuniqueNameより先に編集中の値を確定する。DはTableTCEdit.enter()
    // 前に名前集合を作るため、貼り付け名が編集中の名前と衝突し得る。
    if (!finishInlineEdit(true)) return false;
    storeCoupons();
    auto insert_at = index < 0 ? records_.size()
                               : std::min(static_cast<std::size_t>(index), records_.size());
    if (move && node.attr("paneId", false) == id_) {
        // Dはここで取り消し用の状態を一つ保存して挿入行を公
        // 開し、dragFinishedで入力元を削除して、
        // 追加の履歴なしで再公開する。取り消しは一単位に保ち
        // 、監視側へはアトミックな最終一覧だけを公開する。
        for (const auto& dragged : drag_coupons_) {
            const auto found = std::find(records_.begin(), records_.end(), dragged);
            if (found == records_.end()) continue;
            const auto position = static_cast<std::size_t>(std::distance(records_.begin(), found));
            if (position < insert_at) --insert_at;
            records_.erase(found);
        }
        drag_coupons_.clear();
    }
    std::vector<std::size_t> selected_values;
    for (const auto& value : values) {
        if (!value) continue;
        records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(insert_at),
                        std::make_shared<Coupon>(*value));
        selected_values.push_back(insert_at++);
    }
    if (selected_values.empty()) return false;
    rebuildTable(selected_values);
    if (operation_life.expired()) return false;
    notifyChanged();
    return true;
}

bool CouponViewBase::appendFromNode(const xml::XNode& node, const int index,
                                    const bool move) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    auto copy = node.clone();
    return appendFromNode(copy, index, move);
}

void CouponViewBase::CDragListener::dragStart(bool& doit) const {
    doit = owner_ && !owner_->normalizedSelection().empty();
}

void CouponViewBase::CDragListener::dragSetData(lib::ClipboardData& data) {
    if (!owner_) return;
    owner_->drag_rows_ = owner_->normalizedSelection();
    owner_->drag_coupons_.clear();
    for (const auto index : owner_->drag_rows_) {
        if (const auto value = owner_->rowCoupon(index)) owner_->drag_coupons_.push_back(value);
    }
    if (owner_->drag_rows_.empty()) return;
    owner_->drag_index_ = static_cast<std::int32_t>(owner_->drag_rows_.front());
    owner_->buildDragPayload(data);
}

void CouponViewBase::CDragListener::dragFinished(const lib::DropOperation operation) {
    if (!owner_) return;
    auto* const owner = owner_;
    const std::weak_ptr<const int> alive = owner->menu_lifetime_;
    if (!owner_->read_only_ && operation == lib::DropOperation::Move &&
        !owner_->drag_coupons_.empty()) {
        owner_->storeCoupons();
        for (const auto& dragged : owner_->drag_coupons_) {
            const auto found = std::find(owner_->records_.begin(), owner_->records_.end(), dragged);
            if (found != owner_->records_.end()) owner_->records_.erase(found);
        }
        owner->rebuildTable();
        if (alive.expired()) return;
        owner->notifyChanged();
        if (alive.expired()) return;
    }
    owner->drag_index_ = -1;
    owner->drag_rows_.clear();
    owner->drag_coupons_.clear();
}

void CouponViewBase::CouponTCPD::cut() {
    if (!canDoT()) return;
    const std::weak_ptr<const int> alive = owner_->menu_lifetime_;
    copy();
    if (!alive.expired()) del();
}

void CouponViewBase::CouponTCPD::copy() {
    if (!owner_ || !canDoC() || !owner_->comm_) return;
    // 共通ポップアップはネイティブ編集部のフォーカス喪失なしでコピ
    // ーを呼べる。選択モデルの直列化前に、編集中の行を確定する。
    if (!owner_->finishInlineEdit(true)) return;
    const auto text = owner_->makeXML(owner_->selectedCoupons(), false);
    if (owner_->prop_) {
        xmlbytestransfer::XMLtoCB(*owner_->prop_, owner_->comm_->clipboard(), text);
    }
}

void CouponViewBase::CouponTCPD::paste() {
    if (!canDoP() || !owner_ || !owner_->comm_) return;
    const auto text = xmlbytestransfer::CBtoXML(owner_->comm_->clipboard());
    if (!text.empty()) static_cast<void>(owner_->pasteXML(text, false));
}

void CouponViewBase::CouponTCPD::del() {
    if (owner_ && canDoD()) owner_->delCoupon();
}

void CouponViewBase::CouponTCPD::clone() {
    if (!canDoClone() || !owner_ || !owner_->comm_) return;
    auto& clipboard = owner_->comm_->clipboard();
    clipboard.memoryMode(true);
    // コールバックや直列化の失敗時も含め、Dのscope(exit)に合わせる。
    struct MemoryScope {
        xmlbytestransfer::ClipData& clipboard;
        ~MemoryScope() { clipboard.memoryMode(false); }
    } scope{clipboard};
    copy();
    paste();
}

bool CouponViewBase::CouponTCPD::canDoT() const {
    return owner_ && !owner_->read_only_ && canDoC();
}

bool CouponViewBase::CouponTCPD::canDoC() const {
    return owner_ && owner_->prop_ && owner_->comm_ &&
           !owner_->normalizedSelection().empty();
}

bool CouponViewBase::CouponTCPD::canDoP() const {
    if (!owner_ || owner_->read_only_ || !owner_->comm_) return false;
    try {
        const auto xml=xmlbytestransfer::CBtoXML(owner_->comm_->clipboard());
        if (xml.empty()) return false;
        const auto node=xml::XNode::parse(xml);
        return (node.name()==Coupon::XML_NAME || node.name()==Coupon::XML_NAME_M) &&
               !owner_->couponsFromNode(node).empty();
    } catch (...) {
        return false;
    }
}

bool CouponViewBase::CouponTCPD::canDoD() const {
    return canDoT();
}

bool CouponViewBase::CouponTCPD::canDoClone() const {
    return owner_ && !owner_->read_only_ && canDoC();
}

void CouponViewBase::SelCoupon::widgetSelected(const std::vector<std::size_t>&) const {
    if (owner_) owner_->selCoupon();
}

void CouponViewBase::Dispose::widgetDisposed() noexcept {
    if (!owner_) return;
    owner_->drag_rows_.clear();
    owner_->drag_coupons_.clear();
    owner_->comm_ = nullptr;
}

void CouponViewBase::KeyDownFilter::handleEvent(lib::KeyEvent& event) {
    if (!owner_ || owner_->read_only_ || event.handled) return;
    const auto state = static_cast<std::uint32_t>(event.modifiers);
    const bool control = (state & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
    const bool shift = (state & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
    const bool standard_undo = owner_->undo_accelerator_ == 0 && control && !shift &&
                               event.key == lib::Key::Z;
    const bool standard_redo = owner_->redo_accelerator_ == 0 && control &&
        (event.key == lib::Key::Y || (shift && event.key == lib::Key::Z));
    if (standard_undo || dmenu::eqAcc(owner_->undo_accelerator_,
                                     static_cast<int>(event.key), 0U, state)) {
        // Dはローカル履歴のキーが一致すれば、スタックが空で
        // も消費する。履歴がなければ編集取消の事前処理を省き
        // 、そのキーを消費して後続コントロールに渡さない。
        if (owner_->canUndo()) (void)owner_->undoCoupons();
        event.handled = true;
    } else if (standard_redo || dmenu::eqAcc(owner_->redo_accelerator_,
                                             static_cast<int>(event.key), 0U, state)) {
        if (owner_->canRedo()) (void)owner_->redoCoupons();
        event.handled = true;
    }
}

void CouponViewBase::refMenu(const int id) {
    if (!prop_) return;
    if (id != 0 && id != static_cast<int>(types::MenuID::Undo) &&
        id != static_cast<int>(types::MenuID::Redo)) return;
    undo_accelerator_ = static_cast<int>(dmenu::convertAccelerator(
        prop_->buildMenu(types::MenuID::Undo)));
    redo_accelerator_ = static_cast<int>(dmenu::convertAccelerator(
        prop_->buildMenu(types::MenuID::Redo)));
}

std::shared_ptr<lib::Menu> CouponViewBase::contextMenu() {
    using ID = types::MenuID;
    auto menu = std::make_shared<lib::Menu>(true);
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto item = [&](const ID id, lib::Action action, const bool enabled) {
        lib::MenuItem value;
        value.text = prop_ ? prop_->buildMenu(id) : std::to_string(static_cast<int>(id));
        value.command = std::to_string(static_cast<int>(id));
        value.enabled = enabled && enabled_;
        value.action = [alive, action = std::move(action)] {
            if (!alive.expired() && action) action();
        };
        return value;
    };
    lib::MenuItem separator;
    separator.separator = true;
    auto& items = menu->items;
    if (!read_only_) {
        items.push_back(item(ID::Undo, [this] { (void)undoCoupons(); }, canUndo()));
        items.push_back(item(ID::Redo, [this] { (void)redoCoupons(); }, canRedo()));
        items.push_back(separator);
        items.push_back(item(ID::Up, [this] { upCoupon(); }, canUp()));
        items.push_back(item(ID::Down, [this] { downCoupon(); }, canDown()));
        if (type_ == CVType::Cast) {
            if (feature_provider_)
                items.push_back(item(ID::AddInitialCoupons, [this] { addInitialCoupons(); }, canAddInitialCoupons()));
            items.push_back(item(ID::Reverse, [this] { reverseCoupons(); }, records_.size() >= 2U));
        } else if (type_ == CVType::Valued)
            items.push_back(item(ID::ReverseSignOfValues, [this] { reverseSignOfValues(); }, canReverseSignOfValues()));
        items.push_back(separator);
        items.push_back(item(ID::Cut, [this] { clipboard_actions_->cut(); }, clipboard_actions_->canDoT()));
    }
    items.push_back(item(ID::Copy, [this] { clipboard_actions_->copy(); }, clipboard_actions_->canDoC()));
    if (!read_only_) {
        items.push_back(item(ID::Paste, [this] { clipboard_actions_->paste(); }, clipboard_actions_->canDoP()));
        items.push_back(item(ID::Delete, [this] { clipboard_actions_->del(); }, clipboard_actions_->canDoD()));
        items.push_back(item(ID::Clone, [this] { clipboard_actions_->clone(); }, clipboard_actions_->canDoClone()));
    }
    items.push_back(separator);
    items.push_back(item(ID::SelectAll, [this] { selectAll(); }, canSelectAll()));
    static constexpr std::array kinds{CouponType::Normal, CouponType::Hide, CouponType::Dur,
                                      CouponType::DurBattle, CouponType::System};
    static constexpr std::array convert_ids{ID::ConvertCouponTypeNormal, ID::ConvertCouponTypeHide,
        ID::ConvertCouponTypeDur, ID::ConvertCouponTypeDurBattle, ID::ConvertCouponTypeSystem};
    static constexpr std::array copy_ids{ID::CopyTypeConvertedCouponNormal, ID::CopyTypeConvertedCouponHide,
        ID::CopyTypeConvertedCouponDur, ID::CopyTypeConvertedCouponDurBattle, ID::CopyTypeConvertedCouponSystem};
    if (!read_only_) {
        auto convert = item(ID::ConvertCouponType, {}, false);
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const auto kind = kinds[i];
            auto child = item(convert_ids[i], [this, kind] { convType(kind); }, canConvType(kind));
            convert.enabled = convert.enabled || child.enabled;
            convert.children.push_back(std::move(child));
        }
        items.push_back(std::move(convert));
    }
    auto copy = item(ID::CopyTypeConvertedCoupon, {}, canCopyWith());
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const auto kind = kinds[i];
        copy.children.push_back(item(copy_ids[i], [this, kind] { copyWithType(kind); }, canCopyWith()));
    }
    items.push_back(std::move(copy));
    return menu;
}

void CouponViewBase::refUndoMax() {
    if (!undo_manager_ || !prop_) return;
    const auto value = prop_->var().undoMaxEtc.value;
    undo_manager_->max(value < 0 ? 0U : static_cast<std::size_t>(value));
    if (const auto menu = dmenu::textMenuOf(new_coupon_))
        menu->undoManager()->max(value < 0 ? 0U : static_cast<std::size_t>(value));
}

CouponArray CouponViewBase::coupons() const {
    CouponArray result;
    for (const auto& value : records_) {
        if (value && !value->name().empty()) result.push_back(value);
    }
    return result;
}

void CouponViewBase::coupons(const CouponArray& values) {
    // Dは置換値を公開coupons()の結果と比較する。こ
    // の結果は仮の空行を意図的に除くため、未変更のホスト更新
    // で空行を捨てたり一覧の取り消しをリセットしたりしない。
    const auto current = coupons();
    bool same = values.size() == current.size();
    if (same) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!values[i] || !current[i] || !values[i]->opEquals(*current[i])) {
                same = false;
                break;
            }
        }
    }
    if (same) return;
    CouponArray replacement;
    replacement.reserve(values.size());
    for (const auto& value : values) {
        if (value) replacement.push_back(std::make_shared<Coupon>(*value));
    }
    if (undo_manager_) undo_manager_->reset();
    records_.swap(replacement);
    // Dのcoupons()は全項目を消した後、appendCouponで追加す
    // る項目を順に選択・表示する。最終的には選択なしではなく、最後の項目を選ぶ。
    rebuildTable(records_.empty() ? std::vector<std::size_t>{}
                                 : std::vector<std::size_t>{records_.size() - 1U}, true);
}

std::vector<String> CouponViewBase::couponNames() const {
    std::vector<String> result;
    for (const auto& value : coupons()) result.push_back(value->name());
    return result;
}

void CouponViewBase::couponNames(const std::vector<String>& values) {
    CouponArray result;
    result.reserve(values.size());
    for (const auto& value : values) result.push_back(std::make_shared<Coupon>(value, 0));
    coupons(result);
}


void CouponViewBase::toolIconScale(const float scale) {
    if (!toolbar_) return;
    const float value = std::max(1.0F, scale);
    toolbar_->icon_scale = value;
    // 行の高さは、拡大した正方形ボタンと1pxの内側余白に合わせる。
    const float height = std::ceil(24.0F * value + 2.0F);
    auto item = toolbar_->layout_item();
    item.minimum.height = height;
    item.minimum.width = std::max(item.minimum.width,
        std::ceil(2.0F + static_cast<float>(toolbar_->items.size()) *
                  (24.0F * value + 1.0F)));
    toolbar_->set_layout_item(item);
    if (const auto row = toolbar_row_.lock()) {
        auto row_item = row->layout_item();
        row_item.minimum.height = height;
        row_item.preferred.height = height;
        row->set_layout_item(row_item);
    }
}

void CouponViewBase::enabled(const bool value) {
    enabled_ = value;
    if (new_coupon_) new_coupon_->set_enabled(!read_only_ && value);
    if (coupon_type_) coupon_type_->set_enabled(!read_only_ && value);
    if (coupon_value_) coupon_value_->set_enabled(!read_only_ && value);
    if (toolbar_) toolbar_->set_enabled(!read_only_ && value);
    if (coupons_) coupons_->set_enabled(value);
    refreshToolbarState();
}

bool CouponViewBase::enabled() const {
    return coupons_ && coupons_->enabled();
}

void CouponViewBase::toolTip(String value) {
    if (coupons_) coupons_->set_tooltip(std::move(value));
}

String CouponViewBase::toolTip() const {
    return coupons_ ? coupons_->tooltip() : String{};
}

NameEditor CouponViewBase::nameCreateEditor(const std::size_t row,
                                            const std::size_t) {
    const auto value = rowCoupon(row);
    auto editor = createNameEditor(value ? value->name() : String{}, nullptr);
    const auto setup = setup_name_editor_;
    if (setup) setup(editor);
    return editor;
}

std::vector<String> CouponViewBase::nameCandidates() const {
    return chooser::allCoupons({}, summary_, comboType(type_),
        couponChooserProperties(prop_, comm_, summary_, type_));
}

NameEditor CouponViewBase::createNameEditor(String initial, lib::Container* parent) {
    chooser::Commons common;
    common.services = prop_ ? prop_->images().services() : lib::Services{};
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto common_life = comm_ ? comm_->lifetimeToken() : std::weak_ptr<const int>{};
    common.refreshToolBar = [alive, common_life, owner = comm_] {
        if (!alive.expired() && !common_life.expired() && owner) owner->refreshToolBar();
    };
    const auto properties = couponChooserProperties(prop_, comm_, summary_, type_);
    // ChooserComboは共通の文字入力の接続を所有する。通常のComboBoxは、
    // attachTextMenuが戻った後に選択部が作った制御オブジェクトを保持しない。
    auto editor = chooser::createCouponCombo<chooser::ChooserCombo>(common, summary_, use_counter_,
        parent, [alive, catch_mod = catch_mod_] {
            return !alive.expired() && (!catch_mod || catch_mod());
        }, comboType(type_), std::move(initial), false, properties);
    // 共通の選択部・TMM制御を保持する。その履歴はDと同様にコン
    // トロール単位で、クーポン一覧の状態保存の履歴とは独立させる。
    if (const auto menu = dmenu::textMenuOf(editor); menu && prop_)
        menu->undoManager()->max(static_cast<std::size_t>(std::max(0, prop_->var().undoMaxEtc.value)));
    return editor;
}

void CouponViewBase::refreshNameCandidates() {
    if (!new_coupon_) return;
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto editor = new_coupon_;
    const auto text = editor->edit_text;
    const auto setup = setup_name_editor_;
    std::vector<lib::ChoiceItem> items;
    for (const auto& name : nameCandidates()) items.push_back({name, name});
    (void)lib::replace_combo_items(editor, std::move(items), std::nullopt);
    if (alive.expired()) return;
    editor->edit_text = text;
    if (setup) setup(editor);
}

void CouponViewBase::setupNameEditor(std::function<void(const NameEditor&)> handler) {
    setup_name_editor_ = std::move(handler);
    if (setup_name_editor_ && new_coupon_) setup_name_editor_(new_coupon_);
}

void CouponViewBase::setupInlineNameEditor(std::function<void(const std::shared_ptr<lib::TextEdit>&)> handler) {
    setup_inline_name_editor_ = std::move(handler);
}

void CouponViewBase::nameEditEnd(const std::size_t row, const std::size_t,
                                 String text) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || row >= records_.size()) return;
    const auto value = rowCoupon(row);
    if (!value) return;
    auto targets = containsIndex(normalizedSelection(), row) ? normalizedSelection()
                                                             : std::vector<std::size_t>{row};
    if (text.empty()) {
        if (!value->name().empty()) storeCoupons();
        const auto old_selection = normalizedSelection();
        records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(row));
        std::vector<std::size_t> remaining;
        for (auto index : old_selection) {
            if (index == row) continue;
            remaining.push_back(index > row ? index - 1U : index);
        }
        const std::weak_ptr<const int> alive = menu_lifetime_;
        rebuildTable(remaining);
        if (!alive.expired()) notifyChanged();
        return;
    }
    bool edit = false;
    for (const auto index : targets) {
        if (index < records_.size() && records_[index] && records_[index]->name() != text) {
            edit = true;
            break;
        }
    }
    if (!edit) return;
    storeCoupons();
    for (const auto index : targets) {
        if (index >= records_.size() || !records_[index]) continue;
        const auto name = uniqueName(text, records_[index].get());
        records_[index] = std::make_shared<Coupon>(name, records_[index]->value());
    }
    rebuildTable(targets);
    if (operation_life.expired()) return;
    notifyChanged();
}

ValueEditor CouponViewBase::valueCreateEditor(const std::size_t row,
                                              const std::size_t) {
    auto editor = std::make_shared<lib::NumberEdit>();
    editor->minimum = value_minimum_;
    editor->maximum = value_maximum_;
    if (const auto value = rowCoupon(row)) editor->value = value->value();
    return editor;
}

void CouponViewBase::valueEditEnd(const std::size_t row, const std::size_t,
                                  const std::int32_t value) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !isValued(type_) || row >= records_.size()) return;
    const auto targets = containsIndex(normalizedSelection(), row) ? normalizedSelection()
                                                                    : std::vector<std::size_t>{row};
    const auto bounded = clampValue(value, value_minimum_, value_maximum_);
    bool edit = false;
    for (const auto index : targets) {
        if (index < records_.size() && records_[index] && records_[index]->value() != bounded) {
            edit = true;
            break;
        }
    }
    if (!edit) return;
    storeCoupons();
    for (const auto index : targets) {
        if (index < records_.size() && records_[index]) {
            records_[index] = std::make_shared<Coupon>(records_[index]->name(), bounded);
        }
    }
    rebuildTable(targets);
    if (operation_life.expired()) return;
    notifyChanged();
}

std::vector<String> CouponViewBase::getWarnings(const std::size_t row) const {
    const auto value = rowCoupon(row);
    return value ? warningsImpl(value->name()) : std::vector<String>{};
}

void CouponViewBase::updateWarning(const std::size_t row) {
    if (!coupons_ || row >= coupons_->rows.size()) return;
    coupons_->rows[row].warnings = getWarnings(row);
    if (!coupons_->rows[row].cells.empty())
        coupons_->rows[row].cells.front().warnings = coupons_->rows[row].warnings;
    coupons_->redraw(coupons_->rowBounds(row));
}

void CouponViewBase::refDataVersion() {
    if (!coupons_) return;
    for (std::size_t i = 0; i < coupons_->rows.size(); ++i) updateWarning(i);
}

std::vector<String> CouponViewBase::warnings() const {
    std::vector<String> result;
    std::map<String, bool> seen;
    for (const auto& name : couponNames()) {
        for (const auto& value : warningsImpl(name)) {
            if (!seen[value]) {
                seen[value] = true;
                result.push_back(value);
            }
        }
    }
    return result;
}

std::vector<String> CouponViewBase::warningsImpl(const StringView name) const {
    if (!prop_) return {};
    std::vector<String> result;
    const auto messages = prop_->requiredMsgs();
    if (type_ == CVType::Cast) {
        const auto values = warning::sjisWarnings(prop_->parent(), summary_, name,
                                                  messages.history.value);
        result.insert(result.end(), values.begin(), values.end());
        if (sex_coupons_.contains(String{name})) {
            result.push_back(warning::detail::format(messages, "warningSexCoupon", name));
        }
        if (period_coupons_.contains(String{name})) {
            result.push_back(warning::detail::format(messages, "warningPeriodCoupon", name));
        }
        if (nature_coupons_.contains(String{name})) {
            result.push_back(warning::detail::format(messages, "warningNatureCoupon", name));
        }
        if (making_coupons_.contains(String{name})) {
            result.push_back(warning::detail::format(messages, "warningMakingCoupon", name));
        }
        if (name.starts_with(prop_->sys().couponSystem()) &&
            name != prop_->sys().levelLimit() && name != prop_->sys().ep() &&
            !prop_->sys().isGene(name)) {
            result.push_back(messages.warningSystemCouponForHistory.value);
        }
        return result;
    }
    const auto classic = summary_ && summary_->legacy();
    const auto version = summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION;
    const auto get_lose = type_ == CVType::NoValued;
    const auto item_name = type_ == CVType::Valued ? messages.valued.value
                                                   : messages.couponName.value;
    const auto values = warning::couponWarnings(prop_->parent(), classic, version,
                                                prop_->var().targetVersion.value,
                                                name, get_lose, item_name);
    result.insert(result.end(), values.begin(), values.end());
    return result;
}

bool CouponViewBase::canSelectAll() const {
    return coupons_ && coupons_->selection.size() != coupons_->rows.size();
}

void CouponViewBase::selectAll() {
    if (!canSelectAll()) return;
    // DのSWTのselectAllはSelectionを通知しない。両ホストで
    // 入力途中の内容と値・種別の操作部を維持しつつ、ツールバーの状態を更新する。
    std::vector<std::size_t> selection;
    selection.reserve(coupons_->rows.size());
    for (std::size_t i = 0; i < coupons_->rows.size(); ++i) selection.push_back(i);
    const std::weak_ptr<const int> alive = menu_lifetime_;
    const auto revision = render_revision_;
    const auto table = coupons_;
    table->setSelection(selection, false);
    // 選択通知なしの再描画でも、ホストが終了・再構築される場合がある。
    if (alive.expired() || revision != render_revision_ || table != coupons_) return;
    selected_ = normalizeIndices(selection, records_.size());
    notifyRefresh();
}

bool CouponViewBase::select(const int index) {
    if (!coupons_ || index < 0 || static_cast<std::size_t>(index) >= records_.size()) return false;
    coupons_->setSelection({static_cast<std::size_t>(index)});
    // Dのselect()は、入力操作部の同期前にshowSelection
    // を呼ぶ。単体版・ALLとも共通の表示領域の計算を使う。
    (void)lib::reveal_table_row(*coupons_, static_cast<std::size_t>(index));
    selCoupon();
    return true;
}

bool CouponViewBase::canConvType(const CouponType target) const {
    if (read_only_ || !prop_) return false;
    auto names = std::map<String, bool>{};
    for (const auto& value : records_) if (value) names[value->name()] = true;
    for (const auto index : normalizedSelection()) {
        const auto value = rowCoupon(index);
        if (!value) continue;
        const auto converted = prop_->sys().convCoupon(value->name(), target, false);
        if (converted == value->name()) continue;
        names[value->name()] = false;
        if (!names[converted]) return true;
        names[value->name()] = true;
    }
    return false;
}

void CouponViewBase::convType(const CouponType target) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_ || !prop_) return;
    if (!finishInlineEdit(true)) return;
    if (!canConvType(target)) return;
    storeCoupons();
    auto names = std::map<String, bool>{};
    for (const auto& value : records_) if (value) names[value->name()] = true;
    const auto selected_values = normalizedSelection();
    for (const auto index : selected_values) {
        if (index >= records_.size() || !records_[index]) continue;
        const auto old_name = records_[index]->name();
        const auto new_name = prop_->sys().convCoupon(old_name, target, false);
        if (new_name == old_name) continue;
        names[old_name] = false;
        if (names[new_name]) {
            // Dは衝突後にこの予約を捨てるため、後の選択
            // 行が未変更行の名前を生成し得る。予約を維持
            // し、一括変換で重複を作らないようにする。
            names[old_name] = true;
            continue;
        }
        names[new_name] = true;
        records_[index] = std::make_shared<Coupon>(new_name, records_[index]->value());
    }
    rebuildTable(selected_values);
    if (operation_life.expired()) return;
    notifyChanged();
}

bool CouponViewBase::canCopyWith() const {
    return !normalizedSelection().empty();
}

void CouponViewBase::copyWithType(const CouponType target) {
    if (!canCopyWith() || !prop_ || !comm_) return;
    if (!finishInlineEdit(true)) return;
    CouponArray values;
    for (const auto& value : selectedCoupons()) {
        if (!value) continue;
        const auto name = prop_->sys().convCoupon(value->name(), target, false);
        values.push_back(std::make_shared<Coupon>(name, value->value()));
    }
    xmlbytestransfer::XMLtoCB(*prop_, comm_->clipboard(), makeXML(values, false));
    notifyRefresh();
}

void CouponViewBase::setFeatureProvider(FeatureProvider provider) {
    feature_provider_ = std::move(provider);
    is_history_view_ = static_cast<bool>(feature_provider_);
    refreshSkinCoupons();
}

void CouponViewBase::setImageProvider(ImageProvider provider) {
    image_provider_ = std::move(provider);
    rebuildTable(selected_);
}

String CouponViewBase::makeXML(const CouponArray& values, const bool pane_id) const {
    auto node = xml::XNode::create(Coupon::XML_NAME_M);
    if (pane_id) node.newAttr("paneId", id_);
    for (const auto& value : values) if (value) static_cast<void>(value->toNode(node));
    return node.text();
}

void CouponViewBase::buildDragPayload(lib::ClipboardData& data) {
    data = xmlbytestransfer::makeXMLPayload(makeXML(drag_coupons_, true));
}

bool CouponViewBase::pasteXML(const StringView text, const bool) {
    const std::weak_ptr<const int> operation_life = menu_lifetime_;
    if (read_only_) return false;
    try {
        const auto node = xml::XNode::parse(text);
        const auto values = couponsFromNode(node);
        if (values.empty()) return false;
        if (!finishInlineEdit(true)) return false;
        storeCoupons();
        auto index = insertionIndex(normalizedSelection(), records_.size());
        std::vector<std::size_t> selected_values;
        for (const auto& value : values) {
            const auto name = uniqueName(value->name());
            records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(index),
                            std::make_shared<Coupon>(name, value->value()));
            selected_values.push_back(index++);
        }
        rebuildTable(selected_values);
        if (operation_life.expired()) return false;
        notifyChanged();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CouponViewBase::refreshSkinCoupons() {
    sex_coupons_.clear();
    period_coupons_.clear();
    nature_coupons_.clear();
    making_coupons_.clear();
    if (!isCast(type_) || !comm_ || !comm_->skin()) return;
    const auto skin = comm_->skin();
    for (const auto value : skin->allSexes()) sex_coupons_[skin->sexCoupon(value)] = true;
    for (const auto value : skin->allPeriods()) period_coupons_[skin->periodCoupon(value)] = true;
    const auto show_special = prop_ && prop_->var().showSpNature.value;
    const auto natures = show_special ? skin->allNatures() : skin->normalNatures();
    for (const auto value : natures) nature_coupons_[skin->natureCoupon(value)] = true;
    for (const auto value : skin->allMakings()) making_coupons_[skin->makingsCoupon(value)] = true;
    refDataVersion();
}

String CouponViewBase::formatTypeText(const CouponType type, const StringView text) const {
    if (!prop_ || type == CouponType::Normal) return String{text};
    const auto& sys = prop_->sys();
    switch (type) {
    case CouponType::Hide: return replaceFirst(String{text}, "%1$s", sys.couponHide());
    case CouponType::System: return replaceFirst(String{text}, "%1$s", sys.couponSystem());
    case CouponType::Dur: return replaceFirst(String{text}, "%1$s", sys.couponDur());
    case CouponType::DurBattle: return replaceFirst(String{text}, "%1$s", sys.couponDurBattle());
    case CouponType::Normal: break;
    }
    return String{text};
}

String CouponViewBase::typeTooltip(const CouponType type) const {
    if (!prop_) return {};
    const auto text = prop_->requiredMsgs().couponTypeLongDesc(type);
    return formatTypeText(type, text);
}

void CouponViewBase::updateTypeItems() {
    if (!coupon_type_) return;
    std::vector<lib::ChoiceItem> items;
    decltype(coupon_type_table_) type_table;
    decltype(coupon_type_table_reverse_) reverse_table;
    const std::array<CouponType, 5U> types{
        CouponType::Normal, CouponType::Hide, CouponType::Dur,
        CouponType::DurBattle, CouponType::System};
    for (const auto type : types) {
        if (type_ == CVType::Valued && type == CouponType::System) continue;
        const auto index = items.size();
        const auto text = prop_ ? (type_ == CVType::Valued
                                       ? prop_->requiredMsgs().couponTypeName(type)
                                       : formatTypeText(type, prop_->requiredMsgs().couponTypeDesc(type)))
                                : std::to_string(index);
        items.push_back({text, text, {}, true});
        type_table[type] = index;
        reverse_table[index] = type;
    }
    const auto selected = items.empty() ? std::optional<std::size_t>{}
                                        : std::optional<std::size_t>{0U};
    if (!lib::replace_combo_items(coupon_type_, std::move(items), selected)) return;
    coupon_type_table_.swap(type_table);
    coupon_type_table_reverse_.swap(reverse_table);
    uppdateCouponTypeHint();
}

void CouponViewBase::createControl(const std::uint32_t style) {
    const std::weak_ptr<const int> alive = menu_lifetime_;
    panel_ = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = 0.0F;
    layout->padding = {};
    panel_->set_layout(layout);
    if (parent_) parent_->add(panel_);

    std::shared_ptr<lib::Panel> toolbar_row;
    std::shared_ptr<lib::Panel> entry_row;
    if (!read_only_) {
        toolbar_row = std::make_shared<lib::Panel>();
        toolbar_row_ = toolbar_row;
        auto row_layout = std::make_shared<lib::GridLayout>();
        row_layout->columns = 2U;
        row_layout->spacing = 4.0F;
        row_layout->padding = {};
        toolbar_row->set_layout(row_layout);
        toolbar_row->set_layout_item({.horizontal = lib::Align::Stretch,
                                      .minimum = {0.0F, 25.0F},
                                      .preferred = {0.0F, 25.0F}});
        panel_->add(toolbar_row);

        entry_row = std::make_shared<lib::Panel>();
        auto entry_layout = std::make_shared<lib::GridLayout>();
        entry_layout->columns = 2U;
        entry_layout->spacing = 4.0F;
        entry_layout->padding = {};
        entry_row->set_layout(entry_layout);
        entry_row->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .minimum = {0.0F, 24.0F},
                                    .preferred = {0.0F, 24.0F}});
        panel_->add(entry_row);
    }

    if (!read_only_) {
        toolbar_ = std::make_shared<lib::ToolBar>();
        // DのTraverse・KeyDownリスナーは、ネイティブのツールバー
        // 移動を有効のままにする。共通のフォーカス・キーモデルでその動作を扱う。
        toolbar_->keyboard_navigation = true;
        const auto add_icon = prop_ ? prop_->images().load(StringView{"add_coupon.png"})
                                    : lib::ResourceId{};
        const auto alt_icon = prop_ ? prop_->images().load(StringView{"alt_coupon.png"})
                                    : lib::ResourceId{};
        const auto del_icon = prop_ ? prop_->images().load(StringView{"del_res.png"})
                                    : lib::ResourceId{};
        const auto up_icon = prop_ ? prop_->images().load(StringView{"up.png"})
                                   : lib::ResourceId{};
        const auto down_icon = prop_ ? prop_->images().load(StringView{"down.png"})
                                     : lib::ResourceId{};
        // D版の上部ツールバーは追加・上書き・削除・上・下の5項目。
        // Undo/Redoはコンテキストメニューとアクセラレータに置かれる。
        toolbar_->items = {
            {"", "addCoupon", add_icon, true, false, false, false, {}, prop_ ? prop_->requiredMsgs().addCoupon.value : String{}},
            {"", "altCoupon", alt_icon, true, false, false, false, {}, prop_ ? prop_->requiredMsgs().altCoupon.value : String{}},
            {"", "delCoupon", del_icon, true, false, false, false, {}, prop_ ? prop_->requiredMsgs().delCoupon.value : String{}},
            {"", "upCoupon", up_icon, true, false, false, false, {}, prop_ ? prop_->requiredMsgs().menuTextUp.value : String{}},
            {"", "downCoupon", down_icon, true, false, false, false, {}, prop_ ? prop_->requiredMsgs().menuTextDown.value : String{}}};
        toolbar_->on_command = [this, alive](const std::string_view command) {
            if (alive.expired()) return;
            if (command == "addCoupon") addCoupon();
            else if (command == "altCoupon") altCoupon();
            else if (command == "delCoupon") delCoupon();
            else if (command == "upCoupon") upCoupon();
            else if (command == "downCoupon") downCoupon();
            else if (command == "undo") static_cast<void>(undoCoupons());
            else if (command == "redo") static_cast<void>(redoCoupons());
        };
        toolbar_->set_layout_item({.row = 0U, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Center,
                                   .minimum = {160.0F, 25.0F}});
        toolbar_row->add(toolbar_);
    }

    if (!read_only_) {
        new_coupon_ = createNameEditor({}, nullptr);
        // D版と同じく入力欄から直接経歴を確定できるようにする。
        // ツールバーの追加ボタンに加えて Enter でも同じ処理を通す。
        const auto previous_name_key = new_coupon_->on_key_down;
        new_coupon_->on_key_down = [this, alive, previous_name_key](lib::KeyEvent& event) {
            if (alive.expired()) return;
            if (previous_name_key) previous_name_key(event);
            if (alive.expired() || event.handled) return;
            if (event.key != lib::Key::Enter || read_only_) return;
            addCoupon();
            event.handled = true;
        };
        const auto previous_name_selection = new_coupon_->on_selection_changed;
        new_coupon_->on_selection_changed = [this, alive, previous_name_selection](const std::optional<std::size_t> index) {
            if (alive.expired()) return;
            if (previous_name_selection) previous_name_selection(index);
            if (alive.expired()) return;
            updateCouponType();
            refreshToolbarState();
        };
        const auto previous_name_text = new_coupon_->on_text_changed;
        new_coupon_->on_text_changed = [this, alive, previous_name_text](const StringView text) {
            if (alive.expired()) return;
            if (previous_name_text) previous_name_text(text);
            if (alive.expired()) return;
            updateCouponType();
            refreshToolbarState();
        };
        new_coupon_->set_layout_item({.row = 0U, .column = 0U,
                                      .horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Center,
                                      .minimum = {0.0F, 22.0F},
                                      .preferred = {0.0F, 22.0F},
                                      .grow = 1.0F});
        entry_row->add(new_coupon_);
        coupon_type_ = std::make_shared<lib::ComboBox>();
        coupon_type_->editable = false;
        coupon_type_->on_selection_changed = [this, alive](const std::optional<std::size_t> index) {
            if (alive.expired()) return;
            if (!index || !prop_ || !new_coupon_) return;
            const auto found = coupon_type_table_reverse_.find(*index);
            if (found == coupon_type_table_reverse_.end()) return;
            new_coupon_->edit_text = prop_->sys().convCoupon(new_coupon_->edit_text,
                                                             found->second, false);
            uppdateCouponTypeHint();
        };
        coupon_type_->set_layout_item({.row = 0U, .column = 1U,
                                       .horizontal = lib::Align::Stretch,
                                       .vertical = lib::Align::Center,
                                       .minimum = {90.0F, 22.0F},
                                       .preferred = {90.0F, 22.0F}});
        toolbar_row->add(coupon_type_);
        updateTypeItems();
        if (isValued(type_)) {
            coupon_value_ = std::make_shared<lib::NumberEdit>();
            coupon_value_->minimum = value_minimum_;
            coupon_value_->maximum = value_maximum_;
            coupon_value_->value = type_ == CVType::Valued ? 1.0 : 0.0;
            coupon_value_->set_layout_item({.row = 0U, .column = 1U,
                                            .horizontal = lib::Align::Stretch,
                                            .vertical = lib::Align::Center,
                                            .minimum = {56.0F, 22.0F},
                                            .preferred = {56.0F, 22.0F}});
            entry_row->add(coupon_value_);
        }
    }

    const auto table_style = customtable::STYLE_MULTI_SELECT |
                             (read_only_ ? customtable::STYLE_READ_ONLY : customtable::STYLE_NONE);
    const auto services = prop_ ? prop_->images().services() : lib::Services{};
    coupons_ = customtable::rangeSelectableTable(panel_.get(), table_style, services);
    coupons_->on_inline_editor_created = [this, alive](std::size_t, std::size_t column,
        const std::shared_ptr<lib::TextEdit>& editor, const std::shared_ptr<lib::ComboBox>&) {
        if (alive.expired()) return;
        if (column != 0U || !editor) return;
        if (setup_inline_name_editor_) setup_inline_name_editor_(editor);
        if (alive.expired()) return;
        // Dの削除判定は、確定データではなく現在の名前Comboを
        // 読む。弱参照を保持し、共通編集部の終了時に無効にする。
        active_inline_name_editor_ = editor;
        const auto generation = ++inline_name_generation_;
        const std::weak_ptr<lib::TextEdit> weak_editor = editor;
        const auto previous_changed = editor->on_changed;
        editor->on_changed = [this, alive, weak_editor, generation, previous_changed](std::string_view text) {
            if (previous_changed) previous_changed(text);
            if (alive.expired()) return;
            if (inline_name_generation_ == generation &&
                active_inline_name_editor_.lock() == weak_editor.lock()) notifyRefresh();
        };
        const auto previous_finished = editor->on_inline_edit_finished;
        editor->on_inline_edit_finished = [this, alive, weak_editor, generation, previous_finished](bool committed) {
            if (!alive.expired() && inline_name_generation_ == generation &&
                active_inline_name_editor_.lock() == weak_editor.lock())
                active_inline_name_editor_.reset();
            if (previous_finished) previous_finished(committed);
            if (!alive.expired()) notifyRefresh();
        };
        notifyRefresh();
    };
    const auto previous_pointer = coupons_->on_pointer;
    coupons_->on_pointer = [this, alive, previous_pointer, services](lib::PointerEvent& event) {
        if (alive.expired()) return;
        if (previous_pointer) previous_pointer(event);
        if (alive.expired()) return;
        if (event.action == lib::PointerAction::DoubleClick &&
            event.button == lib::PointerButton::Primary && !read_only_ &&
            services.gui && normalizedSelection().empty()) {
            const auto bounds = coupons_->bounds();
            const lib::Point local{event.position.x - bounds.x, event.position.y - bounds.y};
            if (coupons_->rowAt(local)) return;
            if (!finishInlineEdit(true)) return;
            // DはCastの履歴の先頭に、仮の空名を作って編集を始める。
            const auto value = appendCoupon(Coupon{"", 0}, type_ == CVType::Cast ? 0 : -1);
            if (alive.expired() || !value) return;
            const auto table = coupons_;
            const std::weak_ptr<customtable::TableModel> weak_table = table;
            lib::ProgrammaticInlineEditRequest request;
            request.table = table;
            request.focus_scope = panel_;
            request.matches_stable_row = [weak_table, value](std::size_t index) {
                const auto current = weak_table.lock();
                if (!current || index >= current->rows.size()) return false;
                const auto* item = std::any_cast<CouponPtr>(&current->rowState(index).data);
                return item && *item == value;
            };
            event.handled = true;
            (void)services.gui->request_programmatic_inline_edit(request);
            return;
        }
        if (event.handled || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !services.gui) return;
        services.gui->show_context_menu(contextMenu(), coupons_, event.position);
        event.handled = true;
    };
    if (!read_only_) {
        // DのTableTextEditは、選択済みクーポンを押すと名前を編集する。
        coupons_->inline_edit_column = 0U;
        ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedEdit(
            coupons_, services, panel_,
            [this, alive] {
                if (alive.expired()) return false;
                return prop_ && prop_->var().editTriggerType.value ==
                    static_cast<std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
            });
    }
    const float coupon_width = prop_
        ? static_cast<float>(prop_->var().couponWidth.value) : 250.0F;
    const float coupon_height = prop_
        ? static_cast<float>(prop_->var().couponHeight.value) : 170.0F;
    coupons_->set_layout_item({.horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Stretch,
                               .minimum = {coupon_width, coupon_height},
                               .preferred = {coupon_width, coupon_height},
                               .grow = 1.0F});
    // SWT版は列見出しを表示せず、名前列を残り幅一杯にする。
    static_cast<void>(coupons_->addColumn({}, 0.0F, customtable::STYLE_NONE));
    coupons_->columns.front().stretch = 1.0F;
    if (isValued(type_)) {
        const float value_width = prop_
            ? static_cast<float>(prop_->var().couponValueColumn.value) : 40.0F;
        static_cast<void>(coupons_->addColumn(
            {}, value_width, customtable::STYLE_NONE));
        coupons_->on_column_resized = [this, alive](const std::size_t column,
                                              const float width) {
            if (alive.expired()) return;
            if (column != 1U || !prop_ || !std::isfinite(width) || width <= 0.0F)
                return;
            prop_->var().couponValueColumn.value = static_cast<std::int32_t>(
                std::lround(width));
            (void)prop_->apply();
        };
    }
    static_cast<void>(coupons_->addSelectionListener([this, alive](const std::vector<std::size_t>& values) {
        if (alive.expired()) return;
        selected_ = values;
        selCoupon();
    }));
    static_cast<void>(coupons_->addEditListener([this, alive](const std::size_t row, const std::size_t column,
                                     String value) {
        if (alive.expired()) return;
        if (column == 0U) nameEditEnd(row, column, std::move(value));
        else {
            std::int32_t parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
                valueEditEnd(row, column, parsed);
            else rebuildTable(selected_);
        }
    }));
    coupons_->drag_data_provider = [this, alive] {
        lib::ClipboardData data;
        if (alive.expired()) return data;
        if (drag_listener_) drag_listener_->dragSetData(data);
        return data;
    };
    coupons_->drag_finished = [this, alive](const lib::DropOperation operation) {
        if (alive.expired()) return;
        if (drag_listener_) drag_listener_->dragFinished(operation);
    };
    if (services.drag_drop) {
        services.drag_drop->bind_source({coupons_, lib::DropOperation::Copy | lib::DropOperation::Move,
                                         [this, alive] {
                                             lib::ClipboardData data;
                                             if (alive.expired()) return data;
                                             if (drag_listener_) drag_listener_->dragSetData(data);
                                             return data;
                                         },
                                         [this, alive](const lib::DropOperation operation) {
                                             if (alive.expired()) return;
                                             if (drag_listener_) drag_listener_->dragFinished(operation);
                                         }});
        services.drag_drop->bind_target({coupons_, {String{xmlbytestransfer::XML_BYTES_FORMAT}},
                                         [this, alive](lib::DragEvent& event) {
                                             if (alive.expired()) return;
                                             if (drop_listener_) drop_listener_->dragEnter(event);
                                         },
                                         [this, alive](lib::DragEvent& event) {
                                             if (alive.expired()) return;
                                             if (drop_listener_) drop_listener_->dragOver(event);
                                         },
                                         [this, alive](lib::DragEvent& event) {
                                             if (alive.expired()) return;
                                             if (drop_listener_) drop_listener_->drop(event);
                                         }, {}});
    }
    if (!read_only_) {
        // DのCouponView.setTabListは
        // 、配置行とは独立に入力操作部の巡回順を指定する。
        panel_->set_tab_order({toolbar_, new_coupon_, coupon_type_,
                               coupon_value_, coupons_});
    }
    static_cast<void>(style);
    refreshToolbarState();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::couponview
