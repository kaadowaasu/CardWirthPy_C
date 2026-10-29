#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_value_dialogs.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "3rdparty/srell/srell.hpp"
#include "replace_text_targets.hpp"
#include "replace_regex.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {
namespace {

namespace event = ::cwxeditor::cwx::event;
namespace msgs = ::cwxeditor::cwx::msgs;
namespace structs = ::cwxeditor::cwx::structs;
namespace utils = ::cwxeditor::cwx::utils;
namespace versioninfo = ::cwxeditor::cwx::versioninfo;
// D版と同様、蓄積した結果を表示中の仮想表とは別に保持する。 保留
// 中の行を反映するのは、完了時かリアルタイム表示が有効な場合だけ。
void publishPendingResults(const std::shared_ptr<lib::TableView>& table,
                           const std::vector<ResultRow>& results, const Tab tab) {
    if (!table || table->rows.size() >= results.size()) return;
    std::vector<lib::TableRow> rows;
    rows.reserve(results.size() - table->rows.size());
    for (std::size_t i = table->rows.size(); i < results.size(); ++i) {
        const auto& row = results[i];
        lib::TableRow value;
        value.cells.push_back({row.main, row.cwxPath, row.image, false, true});
        if (tab == Tab::Coupon)
            value.cells.push_back({std::to_string(row.count), {}, 0U, false, true});
        else {
            value.cells.push_back({row.parent, {}, row.parentImage, false, true});
            if (tab == Tab::Error || tab == Tab::Grep)
                value.cells.push_back({row.description, {}, row.descriptionImage, false, true});
        }
        rows.push_back(std::move(value));
    }
    auto publication = lib::prepare_table_append(table, std::move(rows), false);
    (void)publication.commit();
}

// D版のワーカーはUI外で書き込むが、この共通の走査はUI上で書き込
// み、イベントも処理する。 自身が実行するその設定関数の同期通知だけ
// を抑止し、 対象間・イベント処理間に届いた外部の編集は抑止しない。
struct OwnModelWriteScope {
    bool* value;
    bool before;
    std::shared_ptr<std::atomic_bool> alive;
    OwnModelWriteScope(bool& state, std::shared_ptr<std::atomic_bool> token)
        : value(&state), before(state), alive(std::move(token)) { state = true; }
    ~OwnModelWriteScope() {
        if (alive->load(std::memory_order_acquire)) *value = before;
    }
};


// 称号配列の参照元は、IDの列挙が終わるまで接続したままにする。 各所有
// 者の最初の単一値の編集前に保存し、後で一度だけ絞り込み・再構築する。
class DeferredCouponArraysUndo final : public undo::Undo {
public:
    explicit DeferredCouponArraysUndo(CWXPathPtr owner) : owner_(std::move(owner)) {}
    const CWXPath* owner() const { return owner_.get(); }
    bool empty() const { return slots_.empty(); }
    template<class Getter, class Setter, class Copier>
    void add(Getter get, Setter set, Copier copy) {
        // 称号の取得処理は、現在の共有スロットの記述情報を返す場
        // 合がある。 取り消し用には、その情報を保持せず、複製
        // 処理のネイティブな値のスナップショット型を保存する。
        using Values = decltype(copy(get()));
        auto old_value = std::make_shared<Values>(copy(get()));
        auto new_value = std::make_shared<Values>(*old_value);
        slots_.push_back({[get,copy,new_value] { *new_value = copy(get()); },
            [old_value,set,copy] { set(copy(*old_value)); },
            [new_value,set,copy] { set(copy(*new_value)); }});
    }
    void finish() {
        // 設定関数が UseCounter の参照元を組み直す前に、全フィールドを取得する。
        for (const auto& slot : slots_) slot.capture();
        redo();
    }
    void undo() override { for (auto it=slots_.rbegin(); it!=slots_.rend(); ++it) it->undo(); }
    void redo() override { for (const auto& slot : slots_) slot.redo(); }
    void dispose() override {}
private:
    struct Slot { std::function<void()> capture, undo, redo; };
    CWXPathPtr owner_;
    std::vector<Slot> slots_;
};

std::shared_ptr<DeferredCouponArraysUndo> couponArrayRecovery(const CWXPathPtr& owner) {
    auto result = std::make_shared<DeferredCouponArraysUndo>(owner);
    const auto strings = [](const auto& values) { return values; };
    const auto records = [](const auto& values) {
        lib::Array<lib::Owner<coupon::Coupon>> snapshot;
        snapshot.reserve(values.size());
        for (const auto& value : values)
            snapshot.push_back(value ? value->dup() : value);
        return snapshot;
    };
    const auto filter_strings = [](auto values) {
        std::erase_if(values, [](const auto& value) { return value.empty(); });
        return values;
    };
    const auto filter_records = [](auto values) {
        std::erase_if(values, [](const auto& value) { return value && value->name().empty(); });
        return values;
    };
    if (const auto value = std::dynamic_pointer_cast<summary::Summary>(owner)) {
        result->add([value] { return value->rCoupons(); },
            [value,filter_strings](auto values) { value->rCoupons(filter_strings(std::move(values))); }, strings);
    } else if (const auto value = std::dynamic_pointer_cast<card::CastCard>(owner)) {
        result->add([value] { return value->coupons(); },
            [value,filter_records](auto values) { value->coupons(filter_records(std::move(values))); }, records);
    } else if (const auto value = std::dynamic_pointer_cast<event::SDialog>(owner)) {
        result->add([value] { return value->rCoupons(); },
            [value,filter_strings](auto values) { value->rCoupons(filter_strings(std::move(values))); }, strings);
    } else if (const auto value = std::dynamic_pointer_cast<event::Content>(owner)) {
        if (value->detail().use(types::CArg::Coupons))
            result->add([value] { return value->coupons(); },
                [value,filter_records](auto values) { value->coupons(filter_records(std::move(values))); }, records);
        if (value->detail().use(types::CArg::CouponNames))
            result->add([value] { return value->couponNames(); },
                [value,filter_strings](auto values) {
                    value->couponNames(lib::SharedSlice<String>(filter_strings(std::move(values))));
                }, strings);
    }
    return result->empty() ? nullptr : result;
}

[[nodiscard]] std::shared_ptr<lib::GridLayout> grid(
    const std::size_t columns, const float spacing = 6.0F,
    const lib::Insets padding = {}) {
    auto result = std::make_shared<lib::GridLayout>();
    result->columns = columns;
    result->spacing = spacing;
    result->padding = padding;
    return result;
}

[[nodiscard]] lib::LayoutItem item(
    const std::size_t row, const std::size_t column,
    const std::size_t row_span = 1U, const std::size_t column_span = 1U,
    const float grow = 0.0F, const lib::Size preferred = {},
    const lib::Align horizontal = lib::Align::Stretch,
    const lib::Align vertical = lib::Align::Stretch) {
    return lib::LayoutItem{row, column, row_span, column_span, horizontal,
                           vertical, {}, preferred, {}, grow};
}

[[nodiscard]] String selectedValue(const std::shared_ptr<lib::ComboBox>& combo) {
    if (!combo) return {};
    if (combo->selected_index && *combo->selected_index < combo->items.size())
        return combo->items[*combo->selected_index].value;
    return combo->edit_text;
}

[[nodiscard]] String lower(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

[[nodiscard]] String singleLine(StringView value) {
    String result;
    result.reserve(value.size());
    for (const char c : value) {
        if (c != '\r' && c != '\n') result.push_back(c);
    }
    return result;
}

[[nodiscard]] String replaceToken(String format, StringView first,
                                   StringView second = {}) {
    const auto put = [&](StringView token, StringView value) {
        std::size_t position = 0U;
        while ((position = format.find(token, position)) != String::npos) {
            format.replace(position, token.size(), value);
            position += value.size();
        }
    };
    put("%1$s", first);
    put("%1", first);
    put("%2$s", second);
    put("%2", second);
    return format;
}

[[nodiscard]] String kindText(IDKind kind) {
    switch (kind) {
    case IDKind::Area: return "Area";
    case IDKind::Battle: return "Battle";
    case IDKind::Package: return "Package";
    case IDKind::Cast: return "CastCard";
    case IDKind::Skill: return "SkillCard";
    case IDKind::Item: return "ItemCard";
    case IDKind::Beast: return "BeastCard";
    case IDKind::Info: return "InfoCard";
    case IDKind::Flag: return "Flag";
    case IDKind::Step: return "Step";
    case IDKind::Variant: return "Variant";
    case IDKind::Coupon: return "Coupon";
    case IDKind::Gossip: return "Gossip";
    case IDKind::CompleteStamp: return "CompleteStamp";
    case IDKind::KeyCode: return "KeyCode";
    case IDKind::CellName: return "CellName";
    case IDKind::CardGroup: return "CardGroup";
    }
    return {};
}

[[nodiscard]] String tabText(Tab tab, const ReplaceMessages& messages) {
    switch (tab) {
    case Tab::Text: return messages.tabText;
    case Tab::Id: return messages.tabID;
    case Tab::Path: return messages.tabPath;
    case Tab::Contents: return messages.tabContents;
    case Tab::Coupon: return messages.tabCoupon;
    case Tab::Unused: return messages.tabUnused;
    case Tab::Error: return messages.tabError;
    case Tab::Grep: return messages.tabGrep;
    }
    return {};
}

[[nodiscard]] bool stringKind(IDKind kind) noexcept {
    switch (kind) {
    case IDKind::Flag:
    case IDKind::Step:
    case IDKind::Variant:
    case IDKind::Coupon:
    case IDKind::Gossip:
    case IDKind::CompleteStamp:
    case IDKind::KeyCode:
    case IDKind::CellName:
    case IDKind::CardGroup:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<namewindow::NameIDKind> nameIdKind(const IDKind kind) noexcept {
    using NameIDKind = namewindow::NameIDKind;
    switch (kind) {
    case IDKind::Coupon: return NameIDKind::Coupon;
    case IDKind::Gossip: return NameIDKind::Gossip;
    case IDKind::CompleteStamp: return NameIDKind::CompleteStamp;
    case IDKind::KeyCode: return NameIDKind::KeyCode;
    case IDKind::CellName: return NameIDKind::CellName;
    case IDKind::CardGroup: return NameIDKind::CardGroup;
    default: return std::nullopt;
    }
}

[[nodiscard]] CWXPathPtr summaryPath(const SummaryPtr& value) {
    if (!value) return {};
    // Summary は、各々が CWXPath を継承する複数の
    // 独立した所有者インターフェースを実装する。 共通のパス処理で
    // は、エリアの所有者としてのビューを標準のルートとして使う。
    return CWXPathPtr(value,
                      static_cast<::cwxeditor::cwx::area::AreaOwner*>(value.get()));
}

template <typename ID>
[[nodiscard]] std::optional<ID> parseId(StringView value) {
    if constexpr (std::is_same_v<ID, FlagId> ||
                  std::is_same_v<ID, StepId> ||
                  std::is_same_v<ID, VariantId> ||
                  std::is_same_v<ID, CouponId> ||
                  std::is_same_v<ID, GossipId> ||
                  std::is_same_v<ID, CompleteStampId> ||
                  std::is_same_v<ID, KeyCodeId> ||
                  std::is_same_v<ID, CellNameId> ||
                  std::is_same_v<ID, CardGroupId>) {
        return ID(String(value));
    } else {
        try {
            std::size_t used = 0U;
            const auto number = std::stoull(String(value), &used, 10);
            if (used != value.size()) return std::nullopt;
            return ID(number);
        } catch (...) {
            return std::nullopt;
        }
    }
}

template <typename ID>
ID referenceId(const lib::Owner<usecounter::IDToUser_t<ID>>& user) {
    return replace_text::referenceId<ID>(user);
}

template <typename ID>
bool appendIdTargets(const lib::Owner<usecounter::UseCounter>& counter,
                     const IDKind requested, const IDKind actual,
                     const std::function<bool(IdTarget)>& emit) {
    if (requested != actual || !counter) return true;
    using User = usecounter::IDToUser_t<ID>;
    std::vector<lib::Owner<User>> users;
    // 候補にするのは、ルートの現在の所属だけ。 定義の改名は sub()
    // も使うが、IDタブの置換では保持中の履歴の複製を変更しない。
    for (const auto& id : counter->keys<ID>())
        for (const auto& user : counter->values(id)) if (user) users.push_back(user);
    std::stable_sort(users.begin(), users.end(), [](const auto& a, const auto& b) {
        const auto ao = a->owner(), bo = b->owner();
        const auto left = path::cpsplit(ao ? ao->cwxPath(true) : String{});
        const auto right = path::cpsplit(bo ? bo->cwxPath(true) : String{});
        return path::cpcmp(left, right) < 0;
    });
    // 意図的な安全上の拡張として、件数にかかわらず異なる参照をすべて保持する。
    // D版の512件以上で使う所有者マップは、重複する所有者を不定な順でま
    // とめてしまう。 その別のAPI表現の方針を、ここでは暗黙に再現しない。
    for (const auto& user : users) {
        const auto owner = user->owner();
        if (!owner) continue;
        IdTarget target;
        target.kind = actual;
        target.owner = owner;
        target.path = owner->cwxPath(true);
        target.parentPath = path::cpparent(target.path);
        target.description = kindText(actual);
        target.read = [counter, owner, user] { return referenceId<ID>(user).toString(); };
        target.write = [counter, owner, user](String value) {
            const auto next = parseId<ID>(value);
            return next && replace_text::replaceReference<ID>(user, *next);
        };
        if (emit && !emit(std::move(target))) return false;
    }
    return true;
}

[[nodiscard]] String targetKind(const TextTarget& target) {
    return lower(target.kind);
}

}  // namespace

FKeyCodesUndo::FKeyCodesUndo(std::vector<FKeyCode> old_value,
                             std::vector<FKeyCode> new_value, Setter setter)
    : TUndo<std::vector<FKeyCode>>(std::move(old_value), std::move(new_value),
                                   std::move(setter)) {}

CouponsUndo::CouponsUndo(std::vector<CouponPtr> old_value,
                         std::vector<CouponPtr> new_value, Setter setter)
    : TUndo<std::vector<CouponPtr>>(std::move(old_value), std::move(new_value),
                                    std::move(setter)) {}

CardImagesUndo::CardImagesUndo(std::vector<CardImagePtr> old_value,
                               std::vector<CardImagePtr> new_value,
                               Setter setter)
    : TUndo<std::vector<CardImagePtr>>(std::move(old_value), std::move(new_value),
                                       std::move(setter)) {}

UndoRepl::UndoRepl(undo::UndoArray operations, bool reverse,
                   lib::Action ref_call, ReplaceDialog* owner,
                   const bool id_changed,
                   const std::optional<namewindow::NameIDKind> name_id_kind)
    : operations_(std::move(operations)),
      reverse_(reverse),
      ref_call_(std::move(ref_call)),
      owner_(owner),
      id_changed_(id_changed),
      name_id_kind_(name_id_kind) {}

void UndoRepl::undo() {
    if (reverse_) {
        for (auto it = operations_.rbegin(); it != operations_.rend(); ++it)
            if (*it) (*it)->undo();
    } else {
        for (const auto& operation : operations_)
            if (operation) operation->undo();
    }
    if (ref_call_) ref_call_();
    if (owner_) owner_->completeUndoRedo(false, id_changed_, name_id_kind_);
}

void UndoRepl::redo() {
    for (const auto& operation : operations_)
        if (operation) operation->redo();
    if (ref_call_) ref_call_();
    if (owner_) owner_->completeUndoRedo(true, id_changed_, name_id_kind_);
}

void UndoRepl::dispose() {
    for (auto& operation : operations_)
        if (operation) operation->dispose();
    operations_.clear();
}

RUndo::RUndo(CWXPathPtr parent, CWXPathPtr path_value, String cwx_path,
             undo::UndoArray operations)
    : parent_(std::move(parent)),
      path_(std::move(path_value)),
      cwx_path_(std::move(cwx_path)),
      operations_(std::move(operations)) {}

RUndo::RUndo(String file_path, undo::UndoArray operations)
    : file_path_(std::move(file_path)), operations_(std::move(operations)) {}

void RUndo::undo() {
    for (auto it = operations_.rbegin(); it != operations_.rend(); ++it)
        if (*it) (*it)->undo();
}

void RUndo::redo() {
    // RUndo はD版の対象ごとの順序を維持し、両方向とも対象内の配
    // 列を逆順に再適用する。 項目は入れ子のフィールドを走査しながら
    // 集められ、後の設定処理が外側のスナップショットに依存するため。
    for (auto it = operations_.rbegin(); it != operations_.rend(); ++it)
        if (*it) (*it)->redo();
}

void RUndo::dispose() {
    for (auto& operation : operations_)
        if (operation) operation->dispose();
    operations_.clear();
}

class ReplaceDialog::ValueUndo final : public undo::Undo {
public:
    ValueUndo(String old_value, String new_value,
              std::function<void(String)> setter)
        : old_(std::move(old_value)),
          new_(std::move(new_value)),
          setter_(std::move(setter)) {}

    void undo() override {
        if (setter_) setter_(old_);
    }
    void redo() override {
        if (setter_) setter_(new_);
    }
    void dispose() override {}
    void retainedValue(String value) noexcept { new_.swap(value); }

private:
    String old_;
    String new_;
    std::function<void(String)> setter_;
};

ReplaceDialog::ReplaceDialog(Commons comm, ReplaceProperties properties,
                             Shell parent_shell, SummaryPtr summ,
                             ReplaceAction send_reload_props)
    : comm_(std::move(comm)),
      properties_(std::move(properties)),
      source_(properties_.source),
      summary_(std::move(summ)),
      send_reload_props_(std::move(send_reload_props)),
      undo_(properties_.options.undoMaxReplace) {
    buildUi(parent_shell);
    refreshSource();
    refreshRangeTree(true);
}

ReplaceDialog::~ReplaceDialog() {
    lifetime_alive_->store(false, std::memory_order_release);
    unbindExternalNotifications();
    if (inc_search_) inc_search_->close();
    undo_.dispose();
}

void ReplaceDialog::bindExternalNotifications() {
    if (notification_commons_ || !properties_.editor_commons) return;
    notification_commons_ = properties_.editor_commons;
    // パスの置換は、JPY1/JPDC など外部の提供処
    // 理の入力も変える場合がある。 表示文書を指さなくな
    // った行を残さず、派生した一覧をすべて無効化する。
    repl_path_connection_ = notification_commons_->replPath.add(
        [this](String, String) { synchronizeExternalInputs(false); });
    ref_path_connection_ = notification_commons_->refPath.add(
        [this](String, String, bool) { synchronizeExternalInputs(false); });
    ref_paths_connection_ = notification_commons_->refPaths.add(
        [this](String) { synchronizeExternalInputs(false); });
    del_paths_connection_ = notification_commons_->delPaths.add(
        [this] { synchronizeExternalInputs(false); });
    changed_connection_ = notification_commons_->changed.add(
        [this, weak = std::weak_ptr<std::atomic_bool>(lifetime_alive_)] {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            // 自身の該当する書き込み・通知では履歴を維持する。 ignoreMod
            // はイベント処理をまたぐため使わない。 その間の外部編集は
            // 、このUIスレッド上の置換走査をキャンセルする必要がある。
            if (own_model_write_ || publishing_changes_ || in_undo_) return;
            // 既存の changed() が、自身のワーカー・取り消しの
            // 通知と外部編集を区別し、 D版のキャンセル動作を維持する。
            changed();
        });
    ref_scenario_connection_ = notification_commons_->refScenario.add(
        [this](SummaryPtr value) {
            // 自身の置換後に MainWindow の refreshAll が同じ文書を再
            // 通知する。 その場合は結果と履歴を維持し、新しい文書への切り替えは受け付ける。
            if (publishing_changes_ && value == summary_) return;
            if (in_proc_) {
                cancel_ = true;
                return;
            }
            summary(std::move(value));
        });
    ref_scenario_path_connection_ = notification_commons_->refScenarioPath.add(
        [this] { synchronizeExternalInputs(true); });
    ref_data_version_connection_ = notification_commons_->refDataVersion.add(
        [this] { synchronizeExternalInputs(true); });
    ref_target_version_connection_ = notification_commons_->refTargetVersion.add(
        [this] { synchronizeExternalInputs(true); });
    ref_skin_connection_ = notification_commons_->refSkin.add(
        [this] { synchronizeExternalInputs(false); });
    repl_text_connection_ = notification_commons_->replText.add(
        [this] {
            // 自身の完了では他のビューを更新し、自身の結果・履歴は更新し直さない。
            if (!publishing_changes_) synchronizeExternalInputs(false);
        });
    const auto bindScope = [this](auto& signal) {
        const auto token = signal.add([this, weak = std::weak_ptr<std::atomic_bool>(lifetime_alive_)](const auto&...) {
            const auto alive = weak.lock();
            if (alive && alive->load(std::memory_order_acquire)) requestScopeRefresh();
        });
        live_scope_disconnects_.push_back([borrowed = &signal, token, life = notification_commons_->lifetimeToken()] {
            if (!life.expired()) borrowed->remove(token);
        });
    };
    bindScope(notification_commons_->refArea); bindScope(notification_commons_->delArea);
    bindScope(notification_commons_->refBattle); bindScope(notification_commons_->delBattle);
    bindScope(notification_commons_->refPackage); bindScope(notification_commons_->delPackage);
    bindScope(notification_commons_->refCast); bindScope(notification_commons_->delCast);
    bindScope(notification_commons_->refSkill); bindScope(notification_commons_->delSkill);
    bindScope(notification_commons_->refItem); bindScope(notification_commons_->delItem);
    bindScope(notification_commons_->refBeast); bindScope(notification_commons_->delBeast);
    bindScope(notification_commons_->refInfo); bindScope(notification_commons_->delInfo);
    const auto bindSettings = [this](auto& signal) {
        const auto token = signal.add([this, weak = std::weak_ptr<std::atomic_bool>(lifetime_alive_)] {
            const auto alive = weak.lock();
            if (alive && alive->load(std::memory_order_acquire)) applyLiveSettings();
        });
        live_scope_disconnects_.push_back([borrowed = &signal, token, life = notification_commons_->lifetimeToken()] {
            if (!life.expired()) borrowed->remove(token);
        });
    };
    bindSettings(notification_commons_->refUndoMax);
    bindSettings(notification_commons_->refTableViewStyle);
    auto& histories = notification_commons_->refSearchHistories;
    const auto history_token = histories.add(
        [this, weak = std::weak_ptr<std::atomic_bool>(lifetime_alive_)] {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire) ||
                !notification_commons_ || !notification_commons_->prop()) return;
            const auto& current = notification_commons_->prop()->var();
            properties_.options.searchHistories = current.searchHistories.value;
            properties_.options.replaceHistories = current.replaceHistories.value;
            properties_.options.grepDirectoryHistories = current.grepDirHistories.value;
            refreshHistoryCombos();
        });
    live_scope_disconnects_.push_back(
        [borrowed = &histories, history_token, life = notification_commons_->lifetimeToken()] {
            if (!life.expired()) borrowed->remove(history_token);
        });
}

void ReplaceDialog::unbindExternalNotifications() noexcept {
    if (!notification_commons_) return;
    auto disconnects = std::move(live_scope_disconnects_);
    live_scope_disconnects_.clear();
    for (const auto& disconnect : disconnects) if (disconnect) disconnect();
    notification_commons_->replPath.remove(repl_path_connection_);
    notification_commons_->refPath.remove(ref_path_connection_);
    notification_commons_->refPaths.remove(ref_paths_connection_);
    notification_commons_->delPaths.remove(del_paths_connection_);
    notification_commons_->changed.remove(changed_connection_);
    notification_commons_->refScenario.remove(ref_scenario_connection_);
    notification_commons_->refScenarioPath.remove(ref_scenario_path_connection_);
    notification_commons_->refDataVersion.remove(ref_data_version_connection_);
    notification_commons_->refTargetVersion.remove(ref_target_version_connection_);
    notification_commons_->refSkin.remove(ref_skin_connection_);
    notification_commons_->replText.remove(repl_text_connection_);
    notification_commons_ = nullptr;
    repl_path_connection_ = ref_path_connection_ = ref_paths_connection_ = 0U;
    del_paths_connection_ = ref_scenario_connection_ = ref_scenario_path_connection_ = 0U;
    ref_data_version_connection_ = ref_target_version_connection_ = ref_skin_connection_ = 0U;
    repl_text_connection_ = 0U;
}

void ReplaceDialog::requestScopeRefresh() {
    // D版の改名・削除通知をまたいで、結果・履歴と各リソースのチェック状態を保持する。
    if (in_proc_) { cancel_ = true; scope_refresh_pending_ = true; return; }
    scope_refresh_pending_ = false;
    refreshRangeTree(false);
}

void ReplaceDialog::applyLiveSettings() {
    if (in_proc_) { live_settings_pending_ = true; return; }
    live_settings_pending_ = false;
    if (!notification_commons_ || !notification_commons_->prop()) return;
    const auto& vars = notification_commons_->prop()->var();
    properties_.options.undoMaxReplace = static_cast<std::size_t>(
        std::max<std::int32_t>(0, vars.undoMaxReplace.value));
    properties_.options.showAreaDirectoryTree = vars.showAreaDirTree.value;
    properties_.options.logicalSort = vars.logicalSort.value;
    undo_.max(properties_.options.undoMaxReplace);
    requestScopeRefresh();
}

void ReplaceDialog::synchronizeExternalInputs(const bool refresh_error_versions) {
    // キャンセル可能なまとまりの走査中に、提供側が通知する場合がある。
    // 実行中の結果とモデルの対応を途中で置き換えない。 チェックポイン
    // トで cancel_ を確認し、完了したまとまりだけを保持する。
    if (in_proc_) {
        cancel_ = true;
        return;
    }
    reset();
    refreshSource();
    if (refresh_error_versions) setupErrorVersions();
    refreshRangeTree(false);
    setupPaths();
    setupIDs(true, true, true);
    refreshToolbar();
}

void ReplaceDialog::buildUi(Shell parent_shell) {
    const auto configured_combo_visible_item_count = [&]() -> std::size_t {
        const auto* prop = properties_.editor_commons
            ? properties_.editor_commons->prop() : nullptr;
        if (lib::editor_window_host_options().all_mode || !prop) return 0U; // ALL と所有者なしの場合の既定値を維持する。
        return static_cast<std::size_t>(std::max<std::int32_t>(
            1, prop->var().comboVisibleItemCount.value));
    }();
    const auto applyComboVisibleItemCount = [configured_combo_visible_item_count](
        const std::shared_ptr<lib::ComboBox>& combo) {
        if (configured_combo_visible_item_count != 0U)
            combo->visible_item_count = configured_combo_visible_item_count;
    };
    parent_ = dynamic_cast<Parent>(parent_shell);
    if (comm_.services.gui) window_ = comm_.services.gui->create_dialog();
    if (!window_) window_ = std::make_shared<lib::Dialog>();
    if (auto* owner = dynamic_cast<lib::Window*>(parent_shell))
        owner_window_ = WindowPtr(owner, [](lib::Window*) {});
    lib::configure_owned_window(window_, owner_window_,
        lib::OwnedWindowKind::Modeless, properties_.geometry.valid()
            ? lib::OwnedWindowCenter::Never
            : lib::OwnedWindowCenter::OwnerOrHost);
    window_->title = properties_.messages.dialogTitle;
    window_->icon = properties_.dialogImage;
    window_->minimum_size = properties_.webAllScreenFit
        ? lib::Size{0.0F, 0.0F} : lib::Size{652.0F, 520.0F};
    // ALL仕様: 上バー(28px)を含めて縦595px以内。初回配置でコンテンツの推奨
    // 高さまで伸びないよう、クライアント部の最大高さを 595 - 28 にする。
    // (幅は制限しない。maximum_size はクライアント寸法で、上バーは含まない)
    if (properties_.webAllScreenFit)
        window_->maximum_size = lib::Size{0.0F, 595.0F - 28.0F};
    window_->resizable = true;
    window_->minimizable = true;
    window_->maximizable = true;
    applyGeometry();
    window_->set_layout(std::make_shared<lib::FillLayout>());
    root_ = std::make_shared<lib::Panel>();
    root_->set_layout(grid(1U, 6.0F, {8.0F, 8.0F, 8.0F, 8.0F}));
    window_->add(root_);
    window_->on_close_requested = [this] { close(); };

    split_ = std::make_shared<lib::SplitView>();
    split_->orientation = lib::Orientation::Horizontal;
    split_->weights = {properties_.options.replaceRangeSashLeft,
                       properties_.options.replaceRangeSashRight};
    if (split_->weights[0] <= 0.0F || split_->weights[1] <= 0.0F)
        split_->weights = {3.0F, 1.0F};
    split_->on_weights_changed = [this](const std::vector<float>& weights) {
        if (weights.size() < 2U) return;
        properties_.options.replaceRangeSashLeft = weights[0];
        properties_.options.replaceRangeSashRight = weights[1];
        if (send_reload_props_) send_reload_props_();
    };
    split_->set_layout_item(item(0U, 0U, 1U, 1U, 1.0F));
    root_->add(split_);

    left_panel_ = std::make_shared<lib::Panel>();
    left_panel_->set_layout(grid(1U, 6.0F));
    split_->add(left_panel_);

    tabs_ = std::make_shared<lib::TabView>();
    tabs_->set_layout_item(item(0U, 0U));
    left_panel_->add(tabs_);

    const auto makePage = [&](String title, bool horizontal_scroll = true) {
        auto page = std::make_shared<lib::Panel>();
        page->set_layout(grid(1U, 6.0F, {6.0F, 6.0F, 6.0F, 6.0F}));
        auto viewport = std::make_shared<lib::ScrollView>();
        viewport->horizontal_scroll = horizontal_scroll;
        viewport->vertical_scroll = true;
        viewport->set_layout(std::make_shared<lib::FillLayout>());
        viewport->add(page);
        tabs_->pages.push_back(lib::TabPage{std::move(title), 0U, viewport, false});
        return page;
    };

    text_page_ = makePage(properties_.messages.tabText, lib::editor_window_host_options().all_mode);
    auto text_group = std::make_shared<lib::GroupBox>();
    text_group->title = properties_.messages.textGroup;
    text_group->set_layout(grid(2U));
    text_group->set_layout_item(item(0U, 0U));
    text_page_->add(text_group);

    auto from_label = std::make_shared<lib::Label>(properties_.messages.from);
    from_label->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F, {},
                                     lib::Align::Start, lib::Align::Center));
    text_group->add(from_label);
    text_from_host_ = std::make_shared<lib::Panel>();
    text_from_host_->set_layout(std::make_shared<lib::FillLayout>());
    text_from_host_->set_layout_item(item(0U, 1U, 1U, 1U, 1.0F,
                                          {500.0F, 28.0F}));
    text_group->add(text_from_host_);
    from_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(from_edit_);
    from_edit_->editable = true;
    from_edit_->accessibility_name = properties_.messages.from;
    text_from_host_->add(from_edit_);

    auto to_label = std::make_shared<lib::Label>(properties_.messages.to);
    to_label->set_layout_item(item(1U, 0U, 1U, 1U, 0.0F, {},
                                   lib::Align::Start, lib::Align::Center));
    text_group->add(to_label);
    to_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(to_edit_);
    to_edit_->editable = true;
    to_edit_->accessibility_name = properties_.messages.to;
    to_edit_->set_layout_item(item(1U, 1U, 1U, 1U, 1.0F, {500.0F, 28.0F}));
    text_group->add(to_edit_);

    text_conditions_group_ = std::make_shared<lib::GroupBox>();
    text_conditions_group_->title = properties_.messages.conditionGroup;
    text_conditions_group_->set_layout(grid(3U));
    text_conditions_group_->set_layout_item(item(
        1U, 0U, 1U, 1U, 0.0F,
        {std::max(0.0F, properties_.resultTableSize.width), 0.0F}));
    text_page_->add(text_conditions_group_);
    bindOptions(text_conditions_group_);

    const std::array<bool*, 22> text_states{
        &properties_.options.summary, &properties_.options.scenario,
        &properties_.options.author, &properties_.options.message,
        &properties_.options.cardName, &properties_.options.cardDescription,
        &properties_.options.eventText, &properties_.options.start,
        &properties_.options.variableName, &properties_.options.variableValue,
        &properties_.options.expression, &properties_.options.coupon,
        &properties_.options.gossip, &properties_.options.endScenario,
        &properties_.options.areaName, &properties_.options.keyCode,
        &properties_.options.cellName, &properties_.options.cardGroup,
        &properties_.options.file, &properties_.options.comment,
        &properties_.options.jptx, &properties_.options.textFile};
    std::vector<std::pair<String, bool*>> text_entries;
    for (std::size_t index = 0U; index < text_states.size(); ++index)
        text_entries.emplace_back(properties_.messages.textTargets[index],
                                  text_states[index]);
    text_targets_group_ = addCheckGroup(
        text_page_, properties_.messages.textTargetGroup, std::move(text_entries),
        3U, 2U, properties_.messages.allCheck);
    if (text_targets_group_) {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        if (all_mode) {
            // ALL の設計上の固定幅と三列のグリッドを維持する。
            auto target_item = text_targets_group_->layout_item();
            target_item.minimum.width =
                std::max(0.0F, properties_.resultTableSize.width);
            target_item.preferred.width = target_item.minimum.width;
            text_targets_group_->set_layout_item(target_item);
        } else {
            const auto group_index = check_groups_.size() - 1U;
            const std::weak_ptr<std::atomic_bool> weak_lifetime = lifetime_alive_;
            text_targets_group_->set_layout(std::make_shared<lib::AbsoluteLayout>());
            text_targets_group_->set_scaled_child_layout_provider(
                [this, group_index, weak_lifetime](const lib::Rect bounds, const float scale) {
                    const auto lifetime = weak_lifetime.lock();
                    std::vector<lib::PreparedChildBounds> placements;
                    if (!lifetime || !lifetime->load(std::memory_order_acquire))
                        return placements;
                    if (group_index >= check_groups_.size()) return placements;
                    // 文字・フォントの提供処理がホストへ再入してモードレスダイアログを
                    // 閉じる場合があるため、 計測前に保持中のウィジェットを複製する。
                    const auto members = check_groups_[group_index].members;
                    const auto separator = check_groups_[group_index].separator;
                    const auto all = check_groups_[group_index].all;
                    const float gap = 5.0F * scale;
                    float x = 0.0F, y = 0.0F, row_height = 0.0F;
                    for (const auto& member : members) {
                        const auto& box = member.first;
                        if (!box) continue;
                        if (!lifetime->load(std::memory_order_acquire)) return std::vector<lib::PreparedChildBounds>{};
                        const auto preferred = lib::preferred_ui_size(box);
                        if (!lifetime->load(std::memory_order_acquire)) return std::vector<lib::PreparedChildBounds>{};
                        const float width = std::max(1.0F * scale, preferred.width * scale);
                        const float height = std::max(22.0F * scale, preferred.height * scale);
                        if (x > 0.0F && x + width > bounds.width) {
                            x = 0.0F;
                            y += row_height + gap;
                            row_height = 0.0F;
                        }
                        placements.push_back({box, {bounds.x + x, bounds.y + y,
                                                     std::min(width, bounds.width), height}});
                        x += width + gap;
                        row_height = std::max(row_height, height);
                    }
                    y += row_height + gap;
                    if (separator)
                        placements.push_back({separator,
                            {bounds.x, bounds.y + y, bounds.width, std::max(1.0F, scale)}});
                    y += gap + std::max(1.0F, scale);
                    if (all) {
                        if (!lifetime->load(std::memory_order_acquire)) return std::vector<lib::PreparedChildBounds>{};
                        const auto preferred = lib::preferred_ui_size(all);
                        if (!lifetime->load(std::memory_order_acquire)) return std::vector<lib::PreparedChildBounds>{};
                        placements.push_back({all,
                            {bounds.x, bounds.y + y, std::min(bounds.width, preferred.width * scale),
                             std::max(22.0F * scale, preferred.height * scale)}});
                    }
                    return placements;
                });
        }
    }

    const auto id_page = makePage(properties_.messages.tabID);
    auto id_group = std::make_shared<lib::GroupBox>();
    id_group->title = properties_.messages.idGroup;
    id_group->set_layout(grid(3U));
    id_group->set_layout_item(item(0U, 0U));
    id_page->add(id_group);
    auto id_kind_label = std::make_shared<lib::Label>(properties_.messages.idKind);
    id_kind_label->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F, {},
                                        lib::Align::Start, lib::Align::Center));
    id_group->add(id_kind_label);
    id_kind_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(id_kind_edit_);
    id_kind_edit_->editable = false;
    id_kind_edit_->set_layout_item(item(0U, 1U, 1U, 2U, 1.0F,
                                        {500.0F, 28.0F}));
    id_group->add(id_kind_edit_);
    auto id_separator = std::make_shared<lib::Separator>();
    id_separator->orientation = lib::Orientation::Horizontal;
    id_separator->set_layout_item(item(1U, 0U, 1U, 3U, 0.0F, {0.0F, 1.0F}));
    id_group->add(id_separator);
    auto id_from_label = std::make_shared<lib::Label>(properties_.messages.from);
    id_from_label->set_layout_item(item(2U, 0U, 1U, 1U, 0.0F, {},
                                        lib::Align::Start, lib::Align::Center));
    id_group->add(id_from_label);
    from_id_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(from_id_edit_);
    from_id_edit_->set_layout_item(item(2U, 1U, 1U, 1U, 1.0F,
                                        {400.0F, 28.0F}));
    id_group->add(from_id_edit_);
    from_id_value_edit_ = std::make_shared<lib::NumberEdit>();
    from_id_value_edit_->minimum = 1.0;
    from_id_value_edit_->maximum = static_cast<double>(properties_.editor_commons && properties_.editor_commons->prop()
        ? properties_.editor_commons->prop()->looks().idMax() : props::Looks{}.idMax());
    from_id_value_edit_->step = 1.0;
    from_id_value_edit_->value = 1.0;
    from_id_value_edit_->set_layout_item(item(2U, 2U, 1U, 1U, 0.0F,
                                              {88.0F, 28.0F}));
    id_group->add(from_id_value_edit_);
    auto id_to_label = std::make_shared<lib::Label>(properties_.messages.to);
    id_to_label->set_layout_item(item(3U, 0U, 1U, 1U, 0.0F, {},
                                      lib::Align::Start, lib::Align::Center));
    id_group->add(id_to_label);
    to_id_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(to_id_edit_);
    to_id_edit_->set_layout_item(item(3U, 1U, 1U, 1U, 1.0F,
                                      {400.0F, 28.0F}));
    id_group->add(to_id_edit_);
    to_id_value_edit_ = std::make_shared<lib::NumberEdit>();
    to_id_value_edit_->minimum = 1.0;
    to_id_value_edit_->maximum = from_id_value_edit_->maximum;
    to_id_value_edit_->step = 1.0;
    to_id_value_edit_->value = 1.0;
    to_id_value_edit_->set_layout_item(item(3U, 2U, 1U, 1U, 0.0F,
                                            {88.0F, 28.0F}));
    id_group->add(to_id_value_edit_);

    const auto path_page = makePage(properties_.messages.tabPath, false);
    auto path_group = std::make_shared<lib::GroupBox>();
    path_group->title = properties_.messages.pathGroup;
    path_group->set_layout(grid(2U));
    path_group->set_layout_item(item(0U, 0U));
    path_page->add(path_group);
    auto path_from_label = std::make_shared<lib::Label>(properties_.messages.from);
    path_from_label->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F, {},
                                          lib::Align::Start, lib::Align::Center));
    path_group->add(path_from_label);
    from_path_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(from_path_edit_);
    from_path_edit_->editable = true;
    from_path_edit_->accessibility_name = properties_.messages.from;
    from_path_edit_->set_layout_item(item(0U, 1U, 1U, 1U, 1.0F,
                                          {500.0F, 28.0F}));
    path_group->add(from_path_edit_);
    auto path_to_label = std::make_shared<lib::Label>(properties_.messages.to);
    path_to_label->set_layout_item(item(1U, 0U, 1U, 1U, 0.0F, {},
                                        lib::Align::Start, lib::Align::Center));
    path_group->add(path_to_label);
    to_path_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(to_path_edit_);
    to_path_edit_->editable = true;
    to_path_edit_->accessibility_name = properties_.messages.to;
    to_path_edit_->set_layout_item(item(1U, 1U, 1U, 1U, 1.0F,
                                        {500.0F, 28.0F}));
    path_group->add(to_path_edit_);
    auto path_description =
        std::make_shared<lib::Label>(properties_.messages.wildcardDescription);
    // 説明文はダイアログ内で折り返し、文章の長さでダイアログ幅を決めない。
    path_description->wrap = true;
    path_description->set_layout_item(item(2U, 1U, 1U, 1U, 0.0F, {},
                                            lib::Align::Stretch,
                                            lib::Align::Center));
    path_group->add(path_description);

    const auto contents_page = makePage(properties_.messages.tabContents);
    auto contents_group = std::make_shared<lib::GroupBox>();
    contents_group->title = properties_.messages.searchRange;
    contents_group->set_layout(grid(1U, 4.0F));
    contents_group->set_layout_item(item(0U, 0U));
    contents_page->add(contents_group);
    contents_toolbar_ = std::make_shared<lib::ToolBar>();
    contents_toolbar_->set_layout_item(item(0U, 0U, 1U, 1U, 1.0F,
                                            {600.0F, 104.0F}));
    static const msgs::Msgs default_messages;
    const auto& content_groups = event::CTYPE_GROUP();
    for (std::int32_t group_index =
             static_cast<std::int32_t>(types::CTypeGroup::Terminal);
         group_index <= static_cast<std::int32_t>(types::CTypeGroup::Variant);
         ++group_index) {
        const auto found =
            content_groups.find(static_cast<types::CTypeGroup>(group_index));
        if (found == content_groups.end()) continue;
        if (!contents_toolbar_->items.empty())
            contents_toolbar_->items.push_back({{}, {}, 0U, true, false, true});
        for (const auto content_type : found->second) {
            const auto index = static_cast<std::size_t>(content_type);
            // D版と同様、コンテントの絞り込みはアイコンのツールバーとし、名前
            // はツールチップにする。 アイコンがない場合だけ文字で表示する。
            lib::ToolItem tool;
            const auto name = default_messages.contentName(content_type);
            tool.icon = properties_.contentImages[index];
            tool.text = tool.icon ? String{} : name;
            tool.tooltip = name;
            tool.command = "content-" + std::to_string(index);
            tool.checked = properties_.options.contents[index];
            contents_toolbar_->items.push_back(std::move(tool));
        }
    }
    // ALL仕様: コンテント指定アイコンを1.25倍にする。
    if (lib::editor_window_host_options().all_mode)
        contents_toolbar_->icon_scale = 1.25F;
    contents_toolbar_->on_command = [this](StringView command) {
        constexpr StringView prefix{"content-"};
        if (!command.starts_with(prefix)) return;
        try {
            const auto index = static_cast<std::size_t>(
                std::stoul(String(command.substr(prefix.size()))));
            if (index >= properties_.options.contents.size()) return;
            properties_.options.contents[index] =
                !properties_.options.contents[index];
            auto items = contents_toolbar_->items;
            for (auto& tool : items)
                if (tool.command == command)
                    tool.checked = properties_.options.contents[index];
            (void)lib::replace_tool_bar_items(contents_toolbar_, std::move(items));
            refreshContentsAll();
            refreshToolbar();
        } catch (...) {
        }
    };
    contents_group->add(contents_toolbar_);
    auto contents_separator = std::make_shared<lib::Separator>();
    contents_separator->set_layout_item(item(1U, 0U, 1U, 1U, 0.0F,
                                              {0.0F, 1.0F}));
    contents_group->add(contents_separator);
    contents_all_check_ =
        std::make_shared<lib::CheckBox>(properties_.messages.allSelect);
    contents_all_check_->set_layout_item(item(2U, 0U, 1U, 1U, 0.0F, {},
                                              lib::Align::Start,
                                              lib::Align::Center));
    contents_all_check_->on_changed =
        [this](const bool checked) { setContentsAll(checked); };
    contents_group->add(contents_all_check_);

    const auto coupon_page = makePage(properties_.messages.tabCoupon);
    const std::array<bool*, 6> name_states{
        &properties_.options.replaceNameCoupon,
        &properties_.options.replaceNameGossip,
        &properties_.options.replaceNameEndScenario,
        &properties_.options.replaceNameKeyCode,
        &properties_.options.replaceNameCellName,
        &properties_.options.replaceNameCardGroup};
    std::vector<std::pair<String, bool*>> name_entries;
    for (std::size_t index = 0U; index < name_states.size(); ++index)
        name_entries.emplace_back(properties_.messages.nameTargets[index],
                                  name_states[index]);
    (void)addCheckGroup(coupon_page, properties_.messages.searchRange,
                        std::move(name_entries), 6U, 0U,
                        properties_.messages.allCheck);

    const auto unused_page = makePage(properties_.messages.tabUnused, false);
    const std::array<bool*, 13> unused_states{
        &properties_.options.unusedFlag, &properties_.options.unusedStep,
        &properties_.options.unusedVariant, &properties_.options.unusedArea,
        &properties_.options.unusedBattle, &properties_.options.unusedPackage,
        &properties_.options.unusedCast, &properties_.options.unusedSkill,
        &properties_.options.unusedItem, &properties_.options.unusedBeast,
        &properties_.options.unusedInfo, &properties_.options.unusedStart,
        &properties_.options.unusedPath};
    std::vector<std::pair<String, bool*>> unused_entries;
    for (std::size_t index = 0U; index < unused_states.size(); ++index)
        unused_entries.emplace_back(properties_.messages.unusedTargets[index],
                                    unused_states[index]);
    auto unused_group = addCheckGroup(
        unused_page, properties_.messages.searchRange, std::move(unused_entries),
        5U, 0U, properties_.messages.allCheck);
    auto unused_separator = std::make_shared<lib::Separator>();
    unused_separator->set_layout_item(item(5U, 0U, 1U, 5U, 0.0F,
                                            {0.0F, 1.0F}));
    unused_group->add(unused_separator);
    auto unused_hint = std::make_shared<lib::Label>(properties_.messages.unusedHint);
    unused_hint->wrap = true;
    unused_hint->set_layout_item(item(6U, 0U, 1U, 5U, 0.0F, {},
                                      lib::Align::End, lib::Align::Center));
    unused_group->add(unused_hint);

    // このページは幅広い文書ではなく、折り返す説明文。 表示
    // 領域に幅を制限し、折り返しラベルに有限の列幅を渡す。
    const auto error_page = makePage(properties_.messages.tabError, false);
    auto error_description =
        std::make_shared<lib::Label>(properties_.messages.errorDescription);
    error_description->wrap = true;
    error_description->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F,
                                             {0.0F, 48.0F},
                                             lib::Align::Stretch,
                                             lib::Align::Center));
    error_page->add(error_description);
    auto error_group = std::make_shared<lib::GroupBox>();
    error_group->title = properties_.messages.errorConditionGroup;
    // DWT と同様、対象バージョンのコンボを自然な幅に
    // する。 共通のグリッドは最後の列に残りの幅を渡すた
    // め、コンボを最後にせず、専用の伸縮する空白を置く。
    error_group->set_layout(grid(3U));
    error_group->set_layout_item(item(1U, 0U));
    error_page->add(error_group);
    auto error_version_label =
        std::make_shared<lib::Label>(properties_.messages.errorTargetVersion);
    error_version_label->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F, {},
                                              lib::Align::Start,
                                              lib::Align::Center));
    error_group->add(error_version_label);
    error_version_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(error_version_edit_);
    error_version_edit_->editable = false;
    setupErrorVersions();
    error_version_edit_->set_layout_item(item(
        0U, 1U, 1U, 1U, 0.0F, {188.0F, 28.0F}, lib::Align::Start,
        lib::Align::Center));
    error_version_edit_->on_selection_changed = [this](const auto index) {
        if (!index || *index >= error_version_edit_->items.size()) return;
        properties_.options.searchErrorTargetVersion =
            error_version_edit_->items[*index].value;
        if (send_reload_props_) send_reload_props_();
    };
    error_group->add(error_version_edit_);
    auto error_version_spacer = std::make_shared<lib::Panel>();
    error_version_spacer->set_layout_item(item(0U, 2U, 1U, 1U, 1.0F));
    error_group->add(error_version_spacer);
    auto error_hint =
        std::make_shared<lib::Label>(properties_.messages.errorTargetVersionHint);
    error_hint->wrap = true;
    error_hint->set_layout_item(item(1U, 0U, 1U, 3U, 0.0F, {},
                                     lib::Align::Stretch, lib::Align::Center));
    error_group->add(error_hint);

    grep_page_ = makePage(properties_.messages.tabGrep);
    auto grep_text_group = std::make_shared<lib::GroupBox>();
    grep_text_group->title = properties_.messages.grepTextGroup;
    grep_text_group->set_layout(grid(2U));
    grep_text_group->set_layout_item(item(0U, 0U));
    grep_page_->add(grep_text_group);
    auto grep_from_label = std::make_shared<lib::Label>(properties_.messages.grepFrom);
    grep_from_label->set_layout_item(item(0U, 0U, 1U, 1U, 0.0F, {},
                                         lib::Align::Start, lib::Align::Center));
    grep_text_group->add(grep_from_label);
    grep_from_host_ = std::make_shared<lib::Panel>();
    grep_from_host_->set_layout(std::make_shared<lib::FillLayout>());
    grep_from_host_->set_layout_item(item(0U, 1U, 1U, 1U, 1.0F,
                                          {500.0F, 28.0F}));
    grep_text_group->add(grep_from_host_);

    auto grep_target_group = std::make_shared<lib::GroupBox>();
    grep_target_group->title = properties_.messages.grepTargetGroup;
    grep_target_group->set_layout(grid(4U));
    grep_target_group->set_layout_item(item(1U, 0U));
    grep_page_->add(grep_target_group);
    grep_directory_edit_ = std::make_shared<lib::ComboBox>();
    applyComboVisibleItemCount(grep_directory_edit_);
    grep_directory_edit_->editable = true;
    grep_directory_edit_->accessibility_name = properties_.messages.grepTargetGroup;
    grep_directory_edit_->set_layout_item(item(0U, 0U, 1U, 1U, 1.0F,
                                               {360.0F, 28.0F}));
    grep_target_group->add(grep_directory_edit_);
    auto grep_browse = std::make_shared<lib::Button>(properties_.messages.grepBrowse);
    grep_browse->accessibility_name = properties_.messages.grepDirectoryTitle;
    grep_browse->set_layout_item(item(0U, 1U, 1U, 1U, 0.0F,
                                      {38.0F, 28.0F}));
    grep_browse->on_click = [this] { browseGrepDirectory(); };
    grep_target_group->add(grep_browse);
    grep_open_button_ = std::make_shared<lib::Button>();
    grep_open_button_->icon = properties_.openDirectoryImage;
    grep_open_button_->accessibility_name = properties_.messages.grepOpen;
    grep_open_button_->set_tooltip(
        properties_.editor_commons && properties_.editor_commons->prop()
            ? properties_.editor_commons->prop()->requiredMsgs().menuText(
                  types::MenuID::OpenDir)
            : properties_.messages.grepOpen);
    grep_open_button_->set_layout_item(item(0U, 2U, 1U, 1U, 0.0F,
                                    {32.0F, 28.0F}));
    grep_open_button_->on_click = [this] { openGrepDirectory(); };
    // ALL にはOSのファイル管理ソフトがないため、ホストが提供する場合は
    // Data Explorer を開く。 提供しない場合はボタンを隠す。
    if (lib::editor_window_host_options().all_mode &&
        !lib::editor_can_open_data_directory())
        grep_open_button_->set_visible(false);
    grep_target_group->add(grep_open_button_);
    auto grep_current =
        std::make_shared<lib::Button>(properties_.messages.grepCurrent);
    grep_current->set_layout_item(item(0U, 3U, 1U, 1U, 0.0F,
                                       {92.0F, 28.0F}));
    grep_current->on_click = [this] { setGrepCurrentDirectory(); };
    grep_target_group->add(grep_current);
    grep_subdirectory_check_ =
        std::make_shared<lib::CheckBox>(properties_.messages.grepSubDirectory);
    grep_subdirectory_check_->checked = properties_.options.grepSubDirectory;
    grep_subdirectory_check_->set_layout_item(item(1U, 0U, 1U, 4U, 0.0F, {},
                                                   lib::Align::End,
                                                   lib::Align::Center));
    grep_target_group->add(grep_subdirectory_check_);

    tab_ = properties_.options.searchPlan <= static_cast<std::size_t>(Tab::Grep)
               ? static_cast<Tab>(properties_.options.searchPlan)
               : Tab::Text;
    id_kind_ = properties_.options.searchIDKind <=
                       static_cast<std::size_t>(IDKind::CardGroup)
                   ? static_cast<IDKind>(properties_.options.searchIDKind)
                   : IDKind::Area;
    (void)lib::select_tab_page(*tabs_, static_cast<std::size_t>(tab_), false);
    tabs_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index && *index <= static_cast<std::size_t>(Tab::Grep))
            setTab(static_cast<Tab>(*index));
    };

    result_table_ = customtable::rangeSelectableTable(
        left_panel_, customtable::STYLE_MULTI_SELECT | customtable::STYLE_HEADER |
            customtable::STYLE_VIRTUAL,
        comm_.services);
    // ALL の検索結果欄（マッチ箇所）
    // は通常の1.32倍の高さにする。
    // グリッドは高さ不足時に伸縮行を先に
    // 縮めるため、推奨高さだけでなく最小
    // 高さも拡大する。 上の条件タブが領
    // 域を譲り、スクロールで吸収する。
    {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        const float result_height = std::max(0.0F, properties_.resultTableSize.height) *
                                    (all_mode ? 2.2F * 0.6F : 1.0F);
        auto result_item = item(
            1U, 0U, 1U, 1U, 1.0F,
            {std::max(0.0F, properties_.resultTableSize.width), result_height});
        if (all_mode) result_item.minimum.height = result_height;
        result_table_->set_layout_item(result_item);
    }
    result_table_->on_column_resized = [this](const std::size_t column,
                                               const float width) {
        const float saved = std::max(0.0F, width);
        if (tab_ == Tab::Coupon) {
            if (column == 0U) properties_.resultColumnMain = saved;
            else if (column == 1U) properties_.resultColumnCouponCount = saved;
        } else if (tab_ == Tab::Error) {
            if (column == 0U) properties_.resultColumnMain = saved;
            else if (column == 1U) properties_.resultColumnParent = saved;
            else if (column == 2U)
                properties_.resultColumnErrorDescription = saved;
        } else if (tab_ == Tab::Grep) {
            if (column == 0U) properties_.resultColumnMain = saved;
            else if (column == 1U) properties_.resultColumnParent = saved;
            else if (column == 2U) properties_.resultColumnScenario = saved;
        } else {
            if (column == 0U) properties_.resultColumnMain = saved;
            else if (column == 1U) properties_.resultColumnParent = saved;
        }
        storeProperties();
    };
    result_table_->setHeaderVisible(false);
    result_table_->inline_edit_column = 0U;
    // D版と同様、称号の行は名前の記録として一列目で編集する
    // 。 他の結果行は、有効化で対象を開く動作を維持する。
    result_table_->edit_on_double_click = false;
    (void)result_table_->addSelectionListener([this](const auto&) { refreshToolbar(); });
    result_table_->on_cell_activate = [this](const std::size_t row, std::size_t) {
        if (tab_ == Tab::Coupon && row < results_.size() &&
            results_[row].textTarget && comm_.services.gui) {
            const auto target = results_[row].textTarget;
            lib::ProgrammaticInlineEditRequest request;
            request.table = result_table_;
            request.focus_scope = left_panel_;
            request.matches_stable_row = [this, target](const std::size_t index) {
                return index < results_.size() && results_[index].textTarget == target;
            };
            (void)comm_.services.gui->request_programmatic_inline_edit(request);
            return;
        }
        (void)openPath(comm_.services.gui &&
            (static_cast<std::uint32_t>(comm_.services.gui->current_modifiers()) &
             static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U);
    };
    // TableView の従来の直接編集フックを、正式なコールバックとして維持す
    // る。 TableModel のリスナーは editCell() を補うものだ
    // が、 D版互換の称号経路などは on_cell_edited を直接呼ぶ。
    result_table_->on_cell_edited = [this](const std::size_t row,
                                           const std::size_t column, String value) {
        if (tab_ == Tab::Coupon && column == 0U) renameCouponResult(row, std::move(value));
    };
    result_table_->on_key_down = [this](lib::KeyEvent& event) {
        if (event.key != lib::Key::Enter) return;
        const bool shift = (static_cast<std::uint32_t>(event.modifiers) &
                            static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        event.handled = openPath(shift);
    };

    auto result_options = std::make_shared<lib::Panel>();
    auto result_options_layout =
        std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    result_options_layout->spacing = 8.0F;
    result_options_layout->cross_axis = lib::Align::Center;
    result_options->set_layout(result_options_layout);
    result_options->set_layout_item(item(2U, 0U, 1U, 1U, 0.0F,
                                         {0.0F, 26.0F}, lib::Align::Stretch,
                                         lib::Align::Center));
    left_panel_->add(result_options);
    auto result_options_spacer = std::make_shared<lib::Spacer>();
    result_options_spacer->set_layout_item(
        item(0U, 0U, 1U, 1U, 1.0F, {}, lib::Align::Stretch,
             lib::Align::Center));
    result_options->add(result_options_spacer);
    realtime_check_ =
        std::make_shared<lib::CheckBox>(properties_.messages.searchResultRealtime);
    realtime_check_->checked = properties_.options.searchResultRealtime;
    realtime_check_->on_changed = [this](const bool value) {
        properties_.options.searchResultRealtime = value;
        if (value && result_table_) {
            publishPendingResults(result_table_, results_, external_find_results_ ? Tab::Text : tab_);
            result_table_->ensureRowState();
            result_table_->redraw();
        }
        if (send_reload_props_) send_reload_props_();
    };
    result_options->add(realtime_check_);
    open_dialog_check_ =
        std::make_shared<lib::CheckBox>(properties_.messages.searchOpenDialog);
    open_dialog_check_->checked = properties_.options.searchOpenDialog;
    open_dialog_check_->set_tooltip(properties_.messages.searchOpenDialogHint);
    open_dialog_check_->on_changed = [this](const bool value) {
        properties_.options.searchOpenDialog = value;
        if (send_reload_props_) send_reload_props_();
    };
    result_options->add(open_dialog_check_);

    auto range_panel = std::make_shared<lib::GroupBox>();
    range_panel->title = properties_.messages.searchRange;
    range_panel->set_layout(grid(1U, 6.0F, {4.0F, 4.0F, 4.0F, 4.0F}));
    split_->add(range_panel);
    range_view_ = std::make_shared<lib::TreeView>();
    range_view_->multi_select = true;
    range_view_->checkable = true;
    range_view_->set_layout_item(item(0U, 0U, 1U, 1U, 1.0F,
                                      {200.0F, 500.0F}));
    range_view_->on_check_changed = [this](lib::TreeItemId id, bool checked) {
        std::function<void(lib::TreeItemId)> apply = [&](lib::TreeItemId target) {
            const auto found = range_paths_.find(target);
            if (found != range_paths_.end()) {
                range_table_[found->second.get()] = checked;
                for (auto& node : range_view_->nodes)
                    if (node.id == target) node.checked = checked;
                // キャストと、その所持カードは独立して選択できる。
                return;
            }
            for (auto& node : range_view_->nodes) {
                if (node.id == target) node.checked = checked;
                if (node.parent == target) apply(node.id);
            }
        };
        std::vector<lib::TreeItemId> targets{id};
        if (std::find(range_view_->selection.begin(), range_view_->selection.end(), id)
                != range_view_->selection.end()) targets = range_view_->selection;
        for (const auto target : targets) apply(target);
        // D版と同様、子孫全体がクリック後の状態と一致するコンテナーだけ
        // を通じて上へ伝える。 状態が混在する親は、元の状態を維持する。
        const auto find_node = [&](lib::TreeItemId target) {
            return std::find_if(range_view_->nodes.begin(), range_view_->nodes.end(),
                [target](const auto& node) { return node.id == target; });
        };
        std::function<bool(lib::TreeItemId)> same = [&](lib::TreeItemId target) {
            for (const auto& child : range_view_->nodes) {
                if (child.parent != target) continue;
                if (child.checked != checked || !same(child.id)) return false;
            }
            return true;
        };
        for (const auto target : targets) {
            auto node = find_node(target);
            while (node != range_view_->nodes.end()) {
                auto parent = find_node(node->parent);
                if (parent == range_view_->nodes.end() || range_paths_.contains(parent->id) ||
                    !same(parent->id)) break;
                parent->checked = checked;
                node = parent;
            }
        }
        refreshRangeAllCheck();
        refreshToolbar();
    };
    range_view_->on_activate = [this](const lib::TreeItemId id) {
        if (!source_.openCWXPath) return;
        const auto found = std::find_if(
            range_view_->nodes.begin(), range_view_->nodes.end(),
            [id](const lib::TreeNode& node) { return node.id == id; });
        if (found != range_view_->nodes.end() && range_paths_.contains(id)) {
            const auto target = path::cpaddattr(path::cpaddattr(found->value, "shallow"), "only");
            const auto alive = lifetime_alive_;
            const auto open = source_.openCWXPath;
            const bool opened = open(target, false);
            if (!opened && alive->load(std::memory_order_acquire)) warnOpenFailure(target);
        }
    };
    range_panel->add(range_view_);
    // 範囲ツリーのメニューはD版と同じ操作にする。 表示範
    // 囲内の検索、表示中の全行選択、選択したシナリオノード
    // を開く操作を、 共通のツリーモデルに対して行い、すべ
    // てのバックエンドで同じメニューを使えるようにする。
    range_menu_ = std::make_shared<lib::Menu>(true);
    range_menu_->items = {
        {properties_.messages.incrementalSearch, "incremental-search"},
        {"Select all", "select-all"},
        {"Open", "open"},
    };
    range_menu_->on_command = [this](const StringView command) {
        if (!range_view_) return;
        if (command == "incremental-search") {
            incSearch();
        } else if (command == "select-all") {
            std::vector<lib::TreeItemId> all;
            all.reserve(range_view_->nodes.size());
            for (const auto& node : range_view_->nodes) all.push_back(node.id);
            (void)lib::select_tree_items(*range_view_, std::move(all), true, false);
        } else if (command == "open" && source_.openCWXPath &&
                   !range_view_->selection.empty()) {
            const auto selected = range_view_->selection.front();
            const auto found = std::find_if(
                range_view_->nodes.begin(), range_view_->nodes.end(),
                [selected](const lib::TreeNode& node) { return node.id == selected; });
            if (found != range_view_->nodes.end() && range_paths_.contains(selected))
                (void)source_.openCWXPath(
                    path::cpaddattr(path::cpaddattr(found->value, "shallow"), "only"),
                    false);
        }
    };
    range_menu_->on_open = [this] {
        if (!range_menu_ || range_menu_->items.size() < 3U) return;
        auto items = range_menu_->items;
        items[0].enabled = inc_search_ != nullptr;
        items[1].enabled = range_view_ && !range_view_->nodes.empty() &&
                           range_view_->selection.size() != range_view_->nodes.size();
        items[2].enabled = source_.openCWXPath && range_view_ &&
                           !range_view_->selection.empty() &&
                           range_paths_.contains(range_view_->selection.front());
        (void)lib::replace_menu_items(range_menu_, std::move(items));
    };
    range_view_->on_pointer = [this](lib::PointerEvent& event) {
        if (!lib::editor_window_host_options().all_mode && range_view_ &&
            event.button == lib::PointerButton::Primary &&
            event.action == lib::PointerAction::DoubleClick &&
            !range_view_->selection.empty() && range_view_->on_activate) {
            // D版と同様、このツリーではチェックボックス領域も含め、どこをダブル
            // クリックしても選択範囲のパスを開く。 この例外は局所的に適用し、汎
            // 用の TreeView のチェックボックスの有効化動作は変えない。
            const auto activate = range_view_->on_activate;
            const auto selected = range_view_->selection.front();
            event.handled = true;
            activate(selected);
            return;
        }
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !comm_.services.gui ||
            !range_menu_)
            return;
        if (range_menu_->on_open) range_menu_->on_open();
        comm_.services.gui->show_context_menu(range_menu_, range_view_, event.position);
        event.handled = true;
    };
    range_all_check_ = std::make_shared<lib::CheckBox>(properties_.messages.allCheckRange);
    range_all_check_->set_layout_item(item(1U, 0U, 1U, 1U, 0.0F, {},
                                           lib::Align::Start,
                                           lib::Align::Center));
    range_all_check_->on_changed = [this](bool value) {
        setVisibleRangeChecked(value);
    };
    range_panel->add(range_all_check_);

    auto root_separator = std::make_shared<lib::Separator>();
    root_separator->orientation = lib::Orientation::Horizontal;
    root_separator->set_layout_item(item(1U, 0U, 1U, 1U, 0.0F,
                                         {0.0F, 1.0F}));
    root_->add(root_separator);

    auto footer = std::make_shared<lib::Panel>();
    footer->set_layout(grid(2U, 8.0F));
    footer->set_layout_item(item(2U, 0U));
    root_->add(footer);
    status_label_ = std::make_shared<lib::Label>(properties_.messages.searchResultEmpty);
    status_label_->set_layout_item(item(0U, 0U, 1U, 1U, 1.0F, {},
                                        lib::Align::Stretch,
                                        lib::Align::Center));
    footer->add(status_label_);
    auto buttons = std::make_shared<lib::Panel>();
    auto buttons_layout = grid(4U, 6.0F);
    buttons_layout->equal_width = true;
    buttons->set_layout(buttons_layout);
    // D版と同様、操作行はコントロールの実測サイズに合わせる。 パネルの幅を
    // 固定すると、倍率や翻訳でラベルが長くなったときに最後の操作が欠ける。
    buttons->set_layout_item(item(0U, 1U, 1U, 1U, 0.0F,
                                  {0.0F, 30.0F}, lib::Align::End,
                                  lib::Align::Center));
    footer->add(buttons);
    find_button_ = std::make_shared<lib::Button>(properties_.messages.search);
    replace_button_ = std::make_shared<lib::Button>(properties_.messages.replace);
    close_button_ = std::make_shared<lib::Button>(properties_.messages.close);
    cancel_button_ = std::make_shared<lib::Button>(properties_.messages.cancel);
    find_button_->default_button = true;
    cancel_button_->cancel_button = true;
    find_button_->on_click = [this] { search(); };
    replace_button_->on_click = [this] { replace(); };
    close_button_->on_click = [this] { close(); };
    cancel_button_->on_click = [this] { cancel(); };
    std::size_t action_column = 0U;
    for (const auto& button : {find_button_, replace_button_, close_button_, cancel_button_}) {
        auto action_item = item(0U, action_column++, 1U, 1U, 0.0F, {0.0F, 28.0F});
        action_item.minimum.width = 84.0F;
        button->set_layout_item(action_item);
    }
    buttons->add(find_button_);
    buttons->add(replace_button_);
    buttons->add(close_button_);
    buttons->add(cancel_button_);

    menu_ = std::make_shared<lib::Menu>(true);
    menu_->items = {
        {"Undo", "undo", "", 0U, true, false, false, false, {}},
        {"Redo", "redo", "", 0U, true, false, false, false, {}},
        {"Copy", "copy", "", 0U, true, false, false, false, {}},
        {"Select all", "select-all", "", 0U, true, false, false, false, {}},
        {"Find ID", "find-id", "", 0U, true, false, false, false, {}},
        {"Open", "open", "", 0U, true, false, false, false, {}},
    };
    menu_->on_command = [this](StringView command) {
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "copy") copyResult();
        else if (command == "select-all" && result_table_) result_table_->selectAll();
        else if (command == "find-id") findSelectedID();
        else if (command == "open") (void)openPath(comm_.services.gui &&
            (static_cast<std::uint32_t>(comm_.services.gui->current_modifiers()) &
             static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U);
        if (menu_ && menu_->on_open) menu_->on_open();
    };
    menu_->on_open = [this] {
        if (menu_->items.size() < 6U) return;
        auto items = menu_->items;
        items[0].enabled = canUndo();
        items[1].enabled = canRedo();
        items[2].enabled = !selectedResultIndices().empty();
        items[3].enabled = result_table_ && !result_table_->rows.empty();
        items[4].enabled = canFindSelectedID();
        items[5].enabled = canOpenPath();
        (void)lib::replace_menu_items(menu_, std::move(items));
    };
    result_table_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !comm_.services.gui)
            return;
        if (menu_->on_open) menu_->on_open();
        comm_.services.gui->show_context_menu(menu_, result_table_, event.position);
        event.handled = true;
    };

    from_edit_->on_text_changed = [this](StringView value) {
        from_text_ = String(value);
        refreshToolbar();
    };
    from_edit_->on_selection_changed = [this](const auto) {
        from_text_ = selectedValue(from_edit_);
        from_edit_->edit_text = from_text_;
        refreshToolbar();
    };
    to_edit_->on_text_changed = [this](StringView value) {
        to_text_ = String(value);
        refreshToolbar();
    };
    to_edit_->on_selection_changed = [this](const auto) {
        to_text_ = selectedValue(to_edit_);
        to_edit_->edit_text = to_text_;
        refreshToolbar();
    };
    from_path_edit_->on_text_changed = [this](StringView value) {
        from_path_ = String(value);
        refreshToolbar();
    };
    from_path_edit_->on_selection_changed = [this](const auto) {
        from_path_ = selectedValue(from_path_edit_);
        from_path_edit_->edit_text = from_path_;
        refreshToolbar();
    };
    to_path_edit_->on_text_changed = [this](StringView value) {
        to_path_ = String(value);
        refreshToolbar();
    };
    to_path_edit_->on_selection_changed = [this](const auto) {
        to_path_ = selectedValue(to_path_edit_);
        to_path_edit_->edit_text = to_path_;
        refreshToolbar();
    };
    grep_directory_edit_->on_text_changed = [this](StringView value) {
        properties_.options.grepDirectory = String(value);
        refreshToolbar();
    };
    grep_directory_edit_->on_selection_changed = [this](const auto) {
        properties_.options.grepDirectory = selectedValue(grep_directory_edit_);
        grep_directory_edit_->edit_text = properties_.options.grepDirectory;
        refreshToolbar();
    };
    grep_open_enabled_refresh_ = dutils::bindOpenButtonEnabledToTextGetter(
        grep_open_button_, grep_directory_edit_,
        [this] { return grepDirectoryOpenPath(); });
    grep_subdirectory_check_->on_changed = [this](bool value) {
        properties_.options.grepSubDirectory = value;
        refreshToolbar();
    };
    from_id_edit_->on_text_changed = [this](StringView) { syncIDControl(true); };
    to_id_edit_->on_text_changed = [this](StringView) { syncIDControl(false); };
    from_id_edit_->on_selection_changed =
        [this](const auto) { syncIDControl(true); };
    to_id_edit_->on_selection_changed =
        [this](const auto) { syncIDControl(false); };
    from_id_value_edit_->on_changed = [this](double) { syncIDControl(true); };
    to_id_value_edit_->on_changed = [this](double) { syncIDControl(false); };
    id_kind_edit_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index && *index <= static_cast<std::size_t>(IDKind::CardGroup)) {
            id_kind_ = static_cast<IDKind>(*index);
            properties_.options.searchIDKind = *index;
            setupIDs(true, true, true);
        }
        refreshToolbar();
    };

    attachIncrementalMenu(from_edit_,
                          [this] { return !properties_.options.searchHistories.empty(); },
                          [this] { updateFromHistory(); });
    attachIncrementalMenu(to_edit_,
                          [this] { return !properties_.options.replaceHistories.empty(); },
                          [this] { updateToHistory(); });
    attachIncrementalMenu(from_path_edit_,
                          [this] { return summary_ || source_.pathTargets ||
                                            source_.pathBatchTargets; },
                          [this] { setupPaths(); });
    attachIncrementalMenu(to_path_edit_,
                          [this] { return summary_ || source_.pathTargets ||
                                            source_.pathBatchTargets; },
                          [this] { setupPaths(); });
    attachIncrementalMenu(from_id_edit_,
                          [this] { return summary_ || source_.idTargets ||
                                            source_.idBatchTargets; },
                          [this] { setupIDs(false, true, false); });
    attachIncrementalMenu(to_id_edit_,
                          [this] { return summary_ || source_.idTargets ||
                                            source_.idBatchTargets; },
                          [this] { setupIDs(false, false, true); });

    inc_search_ = std::make_unique<incsearch::IncSearch>(
        comm_.services, range_view_.get(),
        [this] { return summary_ != nullptr || static_cast<bool>(source_.rangeTree); });
    inc_search_->modEvent.push_back([this] { refreshRangeTree(false); });
    refreshHistoryCombos();
    setupPaths();
    setupIDs(true, true, true);
    refreshContentsAll();
    if (properties_.options.grepDirectory.empty()) setGrepCurrentDirectory();
    tabChanged();
    refreshToolbar();
}

void ReplaceDialog::bindOptions(const std::shared_ptr<lib::Container>& panel) {
    if (!panel) return;
    auto add = [&](String label, bool& state, const std::size_t row,
                   const std::size_t column, const std::size_t span = 1U) {
        auto box = std::make_shared<lib::CheckBox>(std::move(label));
        box->checked = state;
        box->set_layout_item(item(row, column, 1U, span, 0.0F, {},
                                  lib::Align::Start, lib::Align::Center));
        auto* state_ptr = &state;
        box->on_changed = [this, state_ptr](bool value) {
            *state_ptr = value;
            refreshToolbar();
        };
        panel->add(box);
        option_controls_.push_back({box, &state});
        return box;
    };
    (void)add(properties_.messages.notIgnoreCase,
              properties_.options.notIgnoreCase, 0U, 0U);
    (void)add(properties_.messages.exact, properties_.options.exact, 0U, 1U);
    ignore_return_check_ =
        add(properties_.messages.ignoreReturnCode,
            properties_.options.ignoreReturnCode, 0U, 2U);
    wildcard_check_ = add(properties_.messages.wildcard,
                          properties_.options.wildcard, 1U, 0U, 3U);
    regex_check_ = add(properties_.messages.regex, properties_.options.regex,
                       2U, 0U, 3U);
    wildcard_check_->on_changed = [this](const bool value) {
        properties_.options.wildcard = value;
        if (value) {
            properties_.options.regex = false;
            if (regex_check_) regex_check_->checked = false;
        }
        refreshToolbar();
    };
    regex_check_->on_changed = [this](const bool value) {
        properties_.options.regex = value;
        if (value) {
            properties_.options.wildcard = false;
            if (wildcard_check_) wildcard_check_->checked = false;
        }
        refreshToolbar();
    };
}

std::shared_ptr<lib::GroupBox> ReplaceDialog::addCheckGroup(
    const std::shared_ptr<lib::Panel>& parent_value, String title,
    std::vector<std::pair<String, bool*>> entries, const std::size_t columns,
    const std::size_t row, String all_text) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = std::move(title);
    group->set_layout(grid(std::max<std::size_t>(1U, columns), 5.0F));
    group->set_layout_item(item(row, 0U));
    if (parent_value) parent_value->add(group);

    const auto group_index = check_groups_.size();
    check_groups_.push_back({});
    auto& check_group = check_groups_.back();
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        auto box = std::make_shared<lib::CheckBox>(std::move(entries[index].first));
        box->checked = entries[index].second && *entries[index].second;
        box->set_layout_item(item(index / columns, index % columns, 1U, 1U,
                                  0.0F, {}, lib::Align::Start,
                                  lib::Align::Center));
        auto* state = entries[index].second;
        box->on_changed = [this, group_index, state](const bool value) {
            if (state) *state = value;
            refreshCheckGroup(group_index);
            refreshToolbar();
        };
        group->add(box);
        check_group.members.push_back({box, state});
        option_controls_.push_back({box, state});
    }
    const auto next_row = (entries.size() + columns - 1U) / columns;
    auto separator = std::make_shared<lib::Separator>();
    separator->set_layout_item(item(next_row, 0U, 1U, columns, 0.0F,
                                    {0.0F, 1.0F}));
    group->add(separator);
    check_group.separator = separator;
    check_group.all = std::make_shared<lib::CheckBox>(std::move(all_text));
    check_group.all->set_layout_item(item(next_row + 1U, 0U, 1U, columns,
                                          0.0F, {}, lib::Align::Start,
                                          lib::Align::Center));
    check_group.all->on_changed = [this, group_index](const bool value) {
        setCheckGroup(group_index, value);
    };
    group->add(check_group.all);
    refreshCheckGroup(group_index);
    return group;
}

void ReplaceDialog::refreshCheckGroup(const std::size_t index) {
    if (index >= check_groups_.size() || !check_groups_[index].all) return;
    const auto& members = check_groups_[index].members;
    check_groups_[index].all->checked =
        std::all_of(members.begin(), members.end(), [](const auto& member) {
            return member.second && *member.second;
        });
    check_groups_[index].all->indeterminate = false;
}

void ReplaceDialog::setCheckGroup(const std::size_t index, const bool checked) {
    if (index >= check_groups_.size()) return;
    for (auto& [control, state] : check_groups_[index].members) {
        if (state) *state = checked;
        if (control) control->checked = checked;
    }
    refreshCheckGroup(index);
    refreshToolbar();
}

void ReplaceDialog::refreshContentsAll() {
    if (!contents_all_check_) return;
    contents_all_check_->checked = std::all_of(
        properties_.options.contents.begin(), properties_.options.contents.end(),
        [](const bool value) { return value; });
    contents_all_check_->indeterminate = false;
}

void ReplaceDialog::setContentsAll(const bool checked) {
    properties_.options.contents.fill(checked);
    if (contents_toolbar_) {
        auto items = contents_toolbar_->items;
        for (auto& tool : items)
            if (!tool.separator) tool.checked = checked;
        (void)lib::replace_tool_bar_items(contents_toolbar_, std::move(items));
    }
    refreshContentsAll();
    refreshToolbar();
}

void ReplaceDialog::attachIncrementalMenu(
    const std::shared_ptr<lib::ComboBox>& combo,
    incsearch::IncSearch::Enabled enabled,
    incsearch::IncSearch::ModCallback modified) {
    if (!combo) return;
    auto search = std::make_unique<incsearch::IncSearch>(
        comm_.services, combo.get(), enabled);
    if (modified) search->modEvent.push_back(std::move(modified));
    auto* search_ptr = search.get();
    const auto weak_life = std::weak_ptr<std::atomic_bool>(lifetime_alive_);
    dmenu::Commons text_comm;
    text_comm.services = comm_.services;
    text_comm.services.clipboard = comm_.clipboardBackend();
    text_comm.presentation.menuText = comm_.menuText;
    text_comm.presentation.menuImage = comm_.menuImage;
    text_comm.refreshToolBar = comm_.refreshToolBar;
    text_comm.undoLimit = properties_.textUndoLimit;
    if (auto* publisher = properties_.editor_commons) {
        const auto publisher_life = publisher->lifetimeToken();
        text_comm.subscribeUndoMax = [publisher, publisher_life](lib::Action handler) {
            if (publisher_life.expired()) return lib::Action{};
            const auto token = publisher->refUndoMax.add(std::move(handler));
            return lib::Action([publisher, publisher_life, token] {
                if (!publisher_life.expired()) publisher->refUndoMax.remove(token);
            });
        };
    }

    auto binding = dmenu::createTextMenu(text_comm, dmenu::TMM(combo),
        [this, weak_life] {
            const auto life = weak_life.lock();
            return life && life->load(std::memory_order_acquire) && catchMod();
        });
    auto menu = binding->menu();
    const bool id_input = combo == from_id_edit_ || combo == to_id_edit_;
    if (id_input) {
        menu->items.push_back({.command = "id-helper-separator", .separator = true});
        chooser::ChooserProperties chooser_props;
        chooser_props.editor_commons = properties_.editor_commons;
        chooser_props.messages.convertCouponType = comm_.localizedMenuText(
            types::MenuID::ConvertCouponType, chooser_props.messages.convertCouponType);
        chooser_props.messages.copyConvertedCoupon = comm_.localizedMenuText(
            types::MenuID::CopyTypeConvertedCoupon, chooser_props.messages.copyConvertedCoupon);
        chooser_props.messages.copyConvertedKeyCode = comm_.localizedMenuText(
            types::MenuID::CopyTimingConvertedKeyCode, chooser_props.messages.copyConvertedKeyCode);
        chooser::createCouponTypeMenu(comm_, combo, menu, true, true, chooser_props);
        chooser::createKeyCodeTimingMenu(comm_, combo, menu, false, true, chooser_props);
    }
    menu->items.insert(menu->items.begin(), {
        {properties_.messages.incrementalSearch, "incremental-search", {}, comm_.localizedMenuImage(types::MenuID::IncSearch)},
        {.separator = true}});
    const auto all_items = menu->items;
    const auto previous_open = menu->on_open;
    const auto previous_command = menu->on_command;
    const std::weak_ptr<lib::Menu> weak_menu = menu;
    menu->on_open = [this, weak_life, weak_menu, all_items, previous_open, enabled, id_input] {
        const auto life = weak_life.lock();
        const auto menu = weak_menu.lock();
        if (!life || !life->load(std::memory_order_acquire) || !menu) return;
        auto items = all_items;
        std::erase_if(items, [this, id_input](const auto& item) {
            return id_input && ((item.command == "id-helper-separator" && id_kind_ != IDKind::Coupon && id_kind_ != IDKind::KeyCode) ||
                (item.command.starts_with("coupon.") && id_kind_ != IDKind::Coupon) ||
                (item.command.starts_with("keycode.") && id_kind_ != IDKind::KeyCode));
        });
        for (auto& item : items)
            if (item.command == "incremental-search") item.enabled = !enabled || enabled();
        (void)lib::replace_menu_items(menu, std::move(items));
        if (previous_open) previous_open();
    };
    menu->on_command = [this, weak_life, search_ptr, previous_command, id_input](StringView command) {
        const auto life = weak_life.lock();
        if (!life || !life->load(std::memory_order_acquire)) return;
        if (command == "incremental-search") { search_ptr->startIncSearch(); return; }
        if (id_input && ((command.starts_with("coupon.") && id_kind_ != IDKind::Coupon) ||
            (command.starts_with("keycode.") && id_kind_ != IDKind::KeyCode))) return;
        if (previous_command) previous_command(command);
    };
    // 表示後にIDコンボが数値・読み取り専用から文字列・編集可能に
    // 変わっても、 一つの共通テキストメニューの接続を維持する。
    input_text_bindings_.push_back(std::move(binding));
    input_menus_.push_back(std::move(menu));
    input_searches_.push_back(std::move(search));
    // D版と同様、インクリメンタル検索は編集文字列だけ
    // でなく、履歴コンボ自体を絞り込む。 所有する IncSearch
    // は破棄時まで外されないため、これらの
    // ポインターはダイアログの存続中は有効。
    if (combo == from_edit_) from_history_search_ = search_ptr;
    else if (combo == to_edit_) to_history_search_ = search_ptr;
    else if (combo == from_path_edit_) from_path_search_ = search_ptr;
    else if (combo == to_path_edit_) to_path_search_ = search_ptr;
    else if (combo == from_id_edit_) from_id_search_ = search_ptr;
    else if (combo == to_id_edit_) to_id_search_ = search_ptr;
}

void ReplaceDialog::refreshHistoryCombos() {
    const auto set_items = [](const std::shared_ptr<lib::ComboBox>& combo,
                              const std::vector<String>& values,
                              const String& edit_text,
                              const incsearch::IncSearch* filter) {
        if (!combo) return;
        std::vector<lib::ChoiceItem> items;
        items.reserve(values.size());
        for (const auto& value : values)
            if (!filter || filter->match(value))
                items.push_back({value, value, 0U, true});
        (void)lib::replace_combo_items(combo, std::move(items), {}, edit_text);
    };
    set_items(from_edit_, properties_.options.searchHistories, from_text_,
              from_history_search_);
    set_items(to_edit_, properties_.options.replaceHistories, to_text_,
              to_history_search_);
    set_items(grep_directory_edit_, properties_.options.grepDirectoryHistories,
              properties_.options.grepDirectory, nullptr);
}

bool ReplaceDialog::addHistory(std::vector<String>& histories,
                               const StringView value) {
    if (value.empty()) return false;
    const String stored(value);
    std::erase(histories, stored);
    histories.insert(histories.begin(), stored);
    if (histories.size() > properties_.options.searchHistoryMax)
        histories.pop_back();
    return true;
}

void ReplaceDialog::setupPaths() {
    std::vector<String> values{String{}};
    std::unordered_set<String> seen;
    seen.insert({});
    const auto* shared = properties_.editor_commons;
    const auto skin = shared ? shared->skin() : nullptr;
    const auto prop = shared ? shared->prop() : nullptr;
    if (!source_.pathTargets && summary_ && skin && prop) {
        // D版と同様、現在の置換対象だけでなく、未使用の実
        // ファイルやスキン素材も含む共通の素材一覧を使う。
        for (const auto& value : summary_->allMaterials(*skin,
                 prop->var().ignorePaths.value, properties_.options.logicalSort, false))
            if (seen.insert(value).second) values.push_back(value);
    } else {
        const auto targets = source_.pathTargets ? source_.pathTargets() : summaryPathTargets();
        for (const auto& target : targets) {
            const auto value = target.value();
            if (seen.insert(value).second) values.push_back(value);
        }
    }
    const auto set_items = [&values](const std::shared_ptr<lib::ComboBox>& combo,
                                     const String& edit_text,
                                     const incsearch::IncSearch* filter) {
        if (!combo) return;
        std::vector<lib::ChoiceItem> items;
        items.reserve(values.size());
        for (const auto& value : values)
            if (!filter || filter->match(value))
                items.push_back({value, value, 0U, true});
        (void)lib::replace_combo_items(combo, std::move(items), {}, edit_text);
    };
    set_items(from_path_edit_, from_path_, from_path_search_);
    set_items(to_path_edit_, to_path_, to_path_search_);
}

void ReplaceDialog::setupErrorVersions() {
    if (!error_version_edit_) return;

    const auto requested = properties_.options.searchErrorTargetVersion;
    auto choices = properties_.errorVersions;
    if (choices.empty() && properties_.errorVersionProvider)
        choices = properties_.errorVersionProvider(summary_.get());

    std::vector<lib::ChoiceItem> items;
    items.push_back(
        {properties_.messages.errorTargetVersionNotSet, {}, 0U, true});
    std::size_t selected = 0U;

    std::unordered_set<String> values;
    values.insert({});
    const auto append = [&](lib::ChoiceItem choice,
                            const StringView alternate = {}) {
        if (choice.value.empty() || !values.insert(choice.value).second) return;
        items.push_back(std::move(choice));
        const auto index = items.size() - 1U;
        const auto& value = items[index].value;
        if (value == requested || (!alternate.empty() && alternate == requested))
            selected = index;
    };

    if (!choices.empty()) {
        for (auto& choice : choices) append(std::move(choice));
    } else {
        static const msgs::Msgs default_messages;
        for (std::size_t index = 0U; index < versioninfo::VERSIONS.size();
             ++index) {
            append({replaceToken(default_messages.searchErrorTargetVersionName,
                                 versioninfo::VERSION_NAMES[index],
                                 versioninfo::ENGINES[index]),
                    versioninfo::VERSION_NAMES[index], 0U, true},
                   versioninfo::VERSIONS[index]);
        }
        for (std::size_t index = 0U;
             index < versioninfo::CLASSIC_VERSIONS.size(); ++index) {
            append({versioninfo::CLASSIC_ENGINES[index],
                    versioninfo::CLASSIC_ENGINES[index], 0U, true},
                   versioninfo::CLASSIC_VERSIONS[index]);
        }
    }

    const auto edit_text = selected < items.size() ? items[selected].text : String{};
    (void)lib::replace_combo_items(error_version_edit_, std::move(items), selected,
                                   edit_text);
}

void ReplaceDialog::applyGeometry() {
    if (!window_) return;

    if (properties_.webAllScreenFit) {
        auto viewport = lib::Rect{};
        const auto host = lib::editor_window_host_options();
        if (host.hosted_canvas_size.width > 0.0F &&
            host.hosted_canvas_size.height > 0.0F) {
            viewport = {0.0F, 0.0F, host.hosted_canvas_size.width,
                        host.hosted_canvas_size.height};
        } else if (comm_.services.gui) {
            const auto screens = comm_.services.gui->screens();
            const auto primary = std::find_if(
                screens.begin(), screens.end(),
                [](const auto& screen) { return screen.primary; });
            const auto selected = primary != screens.end() ? primary
                : screens.empty() ? screens.end() : screens.begin();
            if (selected != screens.end()) viewport = selected->bounds;
        }
        if (viewport.width > 0.0F && viewport.height > 0.0F) {
            const float width = viewport.width * 0.95F;
            const float max_height = viewport.height * 0.95F;
            const float requested_height = properties_.geometry.valid()
                ? properties_.geometry.bounds.height
                : properties_.defaultSize.height;
            // ALL では、上の見出しを含めて高さ595px以内に収
            // める。 埋め込みウィンドウの境界には見出しも含まれる。
            constexpr float kAllMaximumHeight = 595.0F;
            const float height = std::min({
                max_height, kAllMaximumHeight,
                requested_height > 0.0F ? requested_height : 680.0F});
            window_->center_on_owner = false;
            window_->maximized = false;
            const lib::Rect bounds{
                viewport.x + (viewport.width - width) * 0.5F,
                viewport.y + (viewport.height - height) * 0.5F,
                width, height};
            if (comm_.services.gui)
                comm_.services.gui->set_window_bounds(window_, bounds);
            else
                window_->set_bounds(bounds);
            return;
        }
    }

    if (properties_.geometry.valid()) {
        window_->center_on_owner = false;
        if (comm_.services.gui)
            comm_.services.gui->set_window_bounds(window_,
                                                   properties_.geometry.bounds);
        else
            window_->set_bounds(properties_.geometry.bounds);
        window_->maximized = properties_.geometry.maximized;
        return;
    }

    window_->center_on_owner = true;
    window_->maximized = false;
    auto initial = properties_.defaultSize;
    if (initial.width <= 0.0F) {
        // D版の小さい従来の表サイズ指定とは異なり、初期ウ
        // ィンドウでは設定された結果列を横スクロールなしで
        // 表示する。 明示・保存済みの配置と列幅は保ち、小
        // さい画面では引き続きホスト側で画面内に収める。
        const float main = std::max(0.0F, properties_.resultColumnMain);
        const float parent = std::max(0.0F, properties_.resultColumnParent);
        const float columns = main + std::max({
            std::max(0.0F, properties_.resultColumnCouponCount), parent,
            parent + std::max(0.0F, properties_.resultColumnErrorDescription),
            parent + std::max(0.0F, properties_.resultColumnScenario)});
        const float left = properties_.options.replaceRangeSashLeft;
        const float right = properties_.options.replaceRangeSashRight;
        const float fraction = left > 0.0F && right > 0.0F &&
                               std::isfinite(left + right)
            ? left / (left + right) : 0.75F;
        initial.width = std::max(652.0F,
            (std::max(columns + 24.0F, properties_.resultTableSize.width) /
             std::max(0.1F, fraction)) + 24.0F);
    }
    if (initial.height <= 0.0F) initial.height = 680.0F;
    const lib::Rect bounds{0.0F, 0.0F, initial.width, initial.height};
    if (comm_.services.gui)
        comm_.services.gui->set_window_bounds(window_, bounds);
    else
        window_->set_bounds(bounds);
}

void ReplaceDialog::syncIDControl(const bool from) {
    const auto& combo = from ? from_id_edit_ : to_id_edit_;
    const auto& number = from ? from_id_value_edit_ : to_id_value_edit_;
    auto& value = from ? from_id_ : to_id_;
    if (!combo || !number) return;
    if (idKindIsString(id_kind_)) {
        value = selectedValue(combo);
        number->set_enabled(false);
    } else {
        const bool direct = !combo->selected_index || *combo->selected_index == 0U;
        number->set_enabled(combo->enabled() && direct);
        value = direct ? std::to_string(static_cast<std::int64_t>(
                             std::llround(number->value)))
                       : selectedValue(combo);
    }
    refreshToolbar();
}

void ReplaceDialog::configureResultColumns() {
    if (!result_table_) return;
    result_table_->columns.clear();
    const auto add = [this](String title, const float width) {
        result_table_->columns.push_back(
            {std::move(title), width, lib::TextAlignment::Start, true, false});
    };
    const auto result_tab = external_find_results_ ? Tab::Text : tab_;
    if (result_tab == Tab::Coupon) {
        add(properties_.messages.searchResultColumnCoupon,
            properties_.resultColumnMain);
        add(properties_.messages.searchResultColumnCouponCount,
            properties_.resultColumnCouponCount);
    } else if (result_tab == Tab::Error) {
        add(properties_.messages.searchResultColumnError,
            properties_.resultColumnMain);
        add(properties_.messages.searchResultColumnParent,
            properties_.resultColumnParent);
        add(properties_.messages.searchResultColumnDescription,
            properties_.resultColumnErrorDescription);
    } else if (result_tab == Tab::Grep) {
        add(properties_.messages.searchResultColumnMain,
            properties_.resultColumnMain);
        add(properties_.messages.searchResultColumnParent,
            properties_.resultColumnParent);
        add(properties_.messages.searchResultColumnScenario,
            properties_.resultColumnScenario);
    } else {
        add(properties_.messages.searchResultColumnMain,
            properties_.resultColumnMain);
        add(properties_.messages.searchResultColumnParent,
            properties_.resultColumnParent);
    }
    result_table_->setHeaderVisible(true);
}

String ReplaceDialog::currentScenarioDirectory() const {
    if (!summary_) return {};
    if (!summary_->readOnlyPath().empty()) return summary_->readOnlyPath();
    const auto value = summary_->useTemp() ? summary_->origZipName() : summary_->scenarioPath();
    return value.empty() ? String{} : structs::pathToUtf8(structs::pathFromUtf8(value).parent_path());
}

void ReplaceDialog::setGrepCurrentDirectory() {
    const auto directory = currentScenarioDirectory();
    if (directory.empty()) return;
    properties_.options.grepDirectory = directory;
    if (grep_directory_edit_) grep_directory_edit_->edit_text = directory;
    refreshToolbar();
}

void ReplaceDialog::browseGrepDirectory() {
    auto request = dutils::makeDirectoryPickerRequest(
        properties_.messages.grepDirectoryTitle,
        properties_.messages.grepDirectoryDescription,
        std::filesystem::path(properties_.options.grepDirectory));
    const auto gui = comm_.services.gui;
    if (gui && lib::editor_uses_async_dialog_flow(gui->platform())) {
        const auto picker = comm_.services.file_picker;
        if (!picker) return;
        const std::weak_ptr<std::atomic_bool> lifetime = lifetime_alive_;
        const std::weak_ptr<lib::ComboBox> target = grep_directory_edit_;
        (void)picker->select_directory_async(
            std::move(request), [this, lifetime, target](
                lib::FilePickerResult selected) {
                if (!selected.succeeded() || selected.selections.empty()) return;
                const auto alive = lifetime.lock();
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                if (!window_ || !window_->visible()) return;
                const auto path = selected.selections.front().native_path();
                if (!path) return;
                properties_.options.grepDirectory = path->string();
                if (const auto control = target.lock())
                    control->edit_text = properties_.options.grepDirectory;
                refreshToolbar();
            });
        return;
    }
    if (!comm_.services.dialogs) return;
    lib::FileDialogRequest native_request;
    native_request.title = request.title;
    native_request.description = request.description;
    native_request.initial_directory = request.initial_directory;
    const auto selected = comm_.services.dialogs->select_directory(native_request);
    if (!selected) return;
    properties_.options.grepDirectory = selected->string();
    if (grep_directory_edit_)
        grep_directory_edit_->edit_text = properties_.options.grepDirectory;
    refreshToolbar();
}

String ReplaceDialog::grepDirectoryOpenPath() const {
    const String value = grep_directory_edit_
        ? grep_directory_edit_->edit_text : properties_.options.grepDirectory;
    return dutils::openButtonText(
        properties_.editor_commons ? properties_.editor_commons->prop() : nullptr,
        value);
}

void ReplaceDialog::openGrepDirectory() {
    const auto resolved = dutils::resolveOpenDirectory(grepDirectoryOpenPath());
    if (!resolved) return;
    const auto operation_alive = lifetime_alive_;
    const auto shell = comm_.services.shell;
    const auto dialogs = comm_.services.dialogs;
    const auto title = properties_.messages.dialogTitle;
    String error;
    try {
        const auto native_path = structs::pathFromUtf8(*resolved);
        if (lib::editor_window_host_options().all_mode) {
            // ブラウザー内のエディタは、OSのシェルの代わりに Data Explorer を使う。
            if (lib::editor_can_open_data_directory())
                (void)lib::editor_open_data_directory(native_path);
            return;
        }
        if (!shell) return;
        if (shell->open_path(native_path, error)) return;
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    if (!operation_alive->load(std::memory_order_acquire)) return;
    if (!error.empty() && dialogs) {
        lib::MessageDialogRequest request;
        request.title = title;
        request.message = std::move(error);
        request.severity = lib::MessageSeverity::Warning;
        (void)dialogs->message(request);
    }
}

void ReplaceDialog::open() {
    ++operation_generation_;
    if (!window_) return;
    if (tabs_) {
        tabs_->on_descendant_key_down =
            [this, weak = std::weak_ptr<std::atomic_bool>(lifetime_alive_)]
            (lib::KeyEvent& event) {
                const auto alive = weak.lock();
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                if (!window_ || !window_->visible()) return;
                if (event.key != lib::Key::Enter) return;
                event.handled = true;
                search();
            };
    }
    bindExternalNotifications();
    cancel_ = false;
    // D版の open() と同様、古い結果行だけでなく結果列も消去する。
    reset(true);
    refreshSource();
    applyLiveSettings();
    refreshRangeTree(false);
    setupPaths();
    setupIDs(false, true, true);
    if (comm_.services.gui) comm_.services.gui->show(window_);
    tabChanged();
    if (comm_.services.gui) {
        if (tab_ == Tab::Text && from_edit_) comm_.services.gui->focus(from_edit_);
        else if (tab_ == Tab::Id && id_kind_edit_) comm_.services.gui->focus(id_kind_edit_);
        else if (tab_ == Tab::Path && from_path_edit_) comm_.services.gui->focus(from_path_edit_);
        else if (tab_ == Tab::Grep && from_edit_) comm_.services.gui->focus(from_edit_);
    }
}

void ReplaceDialog::close() {
    ++operation_generation_;
    if (tabs_) tabs_->on_descendant_key_down = {};
    if (!window_) return;
    cancel_ = true;
    unbindExternalNotifications();
    if (inc_search_) inc_search_->close();
    for (auto& search : input_searches_)
        if (search) search->close();
    storeProperties();
    if (comm_.services.gui) comm_.services.gui->close(window_);
    window_->set_visible(false);
}

void ReplaceDialog::summary(SummaryPtr value) {
    if (!value) {
        close();
        summary_.reset();
        undo_.reset();
        refreshToolbar();
        return;
    }
    summary_ = std::move(value);
    setupErrorVersions();
    reset();
    refreshSource();
    refreshRangeTree(true);
    setupPaths();
    setupIDs(true, true, true);
    if (properties_.options.grepDirectory.empty()) setGrepCurrentDirectory();
    undo_.reset();
    refreshToolbar();
}

void ReplaceDialog::replaceText(String from, bool start) {
    reset();
    setFromText(std::move(from));
    setToText({});
    setTab(Tab::Text);
    // 公開の対象変更用の補助処理は、D版と同じフォーカス動作
    // にする。 replaceText() は任意の検索を始
    // める前に、必ず置換前の欄へキーボードフォーカスを移す。
    if (comm_.services.gui && from_edit_)
        comm_.services.gui->focus(from_edit_);
    if (start) search();
}

void ReplaceDialog::replacePath(String from, bool start) {
    reset(true);
    setFromPath(std::move(from));
    setToPath({});
    setTab(Tab::Path);
    if (comm_.services.gui && from_path_edit_)
        comm_.services.gui->focus(from_path_edit_);
    if (start) search();
}

void ReplaceDialog::replaceID(String from, IDKind kind, bool start) {
    reset(true);
    id_kind_ = kind;
    properties_.options.searchIDKind = static_cast<std::size_t>(kind);
    const bool strings = idKindIsString(kind);
    from_id_ = strings ? from : String{};
    to_id_.clear();
    setupIDs(true, true, true);
    if (strings) {
        if (from_id_edit_) from_id_edit_->edit_text = from_id_;
    } else {
        if (!from_id_edit_) return;
        const auto numeric_from = getIDValue(from);
        const auto found = std::find_if(
            from_id_edit_->items.begin(), from_id_edit_->items.end(),
            [&](const auto& choice) {
                if (choice.value == from) return true;
                const auto choice_id = getIDValue(choice.value);
                return numeric_from && choice_id &&
                       *numeric_from == *choice_id;
            });
        // D版と同様、型付きの数値IDは値で選択する。 ゼロ
        // 埋めした表記も同じシナリオの表で解決するが、任意
        // の直接入力数値には選択できる一覧リソースがない。
        if (found == from_id_edit_->items.end()) return;
        const auto index = static_cast<std::size_t>(found-from_id_edit_->items.begin());
        (void)lib::select_combo_item(*from_id_edit_,index,false);
        from_id_ = std::move(from);
        if (from_id_value_edit_) from_id_value_edit_->set_enabled(false);
    }
    if (to_id_edit_) to_id_edit_->edit_text.clear();
    setTab(Tab::Id);
    if (comm_.services.gui && from_id_edit_)
        comm_.services.gui->focus(from_id_edit_);
    if (start) search();
}

namespace {
std::optional<std::pair<IDKind, String>> resourceFindID(
    const CWXPathPtr& object, const SummaryPtr& summary) {
    if (!object) return {};
    if (const auto p = std::dynamic_pointer_cast<area::Area>(object)) return {{IDKind::Area, std::to_string(p->id())}};
    if (const auto p = std::dynamic_pointer_cast<area::Battle>(object)) return {{IDKind::Battle, std::to_string(p->id())}};
    if (const auto p = std::dynamic_pointer_cast<area::Package>(object)) return {{IDKind::Package, std::to_string(p->id())}};
    if (const auto p = std::dynamic_pointer_cast<card::CastCard>(object)) return {{IDKind::Cast, std::to_string(p->id())}};
    if (const auto p = std::dynamic_pointer_cast<card::InfoCard>(object)) return {{IDKind::Info, std::to_string(p->id())}};
    if (object->cwxParent().lock() == summary) {
        if (const auto p = std::dynamic_pointer_cast<card::SkillCard>(object)) return {{IDKind::Skill, std::to_string(p->id())}};
        if (const auto p = std::dynamic_pointer_cast<card::ItemCard>(object)) return {{IDKind::Item, std::to_string(p->id())}};
        if (const auto p = std::dynamic_pointer_cast<card::BeastCard>(object)) return {{IDKind::Beast, std::to_string(p->id())}};
    }
    return {};
}
bool allowedNameFindID(const IDKind kind) {
    // D版と同様、実行されない操作分岐があっても、有効化条件には CardGroup を含めない。
    return kind == IDKind::Coupon || kind == IDKind::Gossip ||
        kind == IDKind::CompleteStamp || kind == IDKind::KeyCode || kind == IDKind::CellName;
}
}

std::vector<CWXPathPtr> startUserPaths(const lib::Owner<event::Content>& start) {
    std::vector<CWXPathPtr> matches, sorted;
    if (!start || start->type() != types::CType::Start || !start->tree()) return sorted;
    const auto tree = start->tree();
    std::vector<event::ContentPtr> pending(tree->starts().begin(), tree->starts().end());
    std::unordered_set<const event::Content*> visited;
    while (!pending.empty()) {
        const auto content = pending.back(); pending.pop_back();
        if (!content || !visited.insert(content.get()).second) continue;
        if ((content->type() == types::CType::LinkStart || content->type() == types::CType::CallStart) &&
            content->start() == start->name()) matches.push_back(content);
        pending.insert(pending.end(), content->next().begin(), content->next().end());
    }
    SortableCWXPath<CWXPathPtr>::sortedPaths(matches, [&](const auto& p) { sorted.push_back(p.obj); });
    return sorted;
}

CWXPathPtr ReplaceDialog::selectedFindObject() const {
    const auto selection = selectedResultIndices();
    if (selection.empty() || selection.front() >= results_.size()) return {};
    const auto& row = results_[selection.front()];
    auto object = row.referenceObject;
    // 名前一覧の行は、範囲の管理にだけ owner を使う。D版ではオブジェクトを保存しない。
    if (!object && row.textTarget && row.textTarget->nameIdKind) return {};
    if (!object && row.textTarget) object = row.textTarget->owner;
    if (!object && row.idTarget) object = row.idTarget->owner;
    if (!object && summary_ && !row.cwxPath.empty()) object = summary_->findCWXPath(row.cwxPath);
    if (const auto holder = std::dynamic_pointer_cast<textholder::SimpleTextHolder>(object))
        object = holder->owner();
    return object;
}

bool ReplaceDialog::canFindSelectedID() const {
    if (result_tab_ == Tab::Grep) return false;
    const auto selection = selectedResultIndices();
    if (selection.empty() || selection.front() >= results_.size()) return false;
    const auto object = selectedFindObject();
    if (resourceFindID(object, summary_)) return true;
    if (std::dynamic_pointer_cast<flag::Flag>(object) ||
        std::dynamic_pointer_cast<flag::Step>(object) ||
        std::dynamic_pointer_cast<flag::Variant>(object)) return true;
    if (const auto content = std::dynamic_pointer_cast<event::Content>(object))
        return content->type() == types::CType::Start && bool(content->tree());
    const auto& row = results_[selection.front()];
    return !object && row.textTarget && row.textTarget->nameIdKind &&
        allowedNameFindID(*row.textTarget->nameIdKind);
}

void ReplaceDialog::findSelectedID() {
    if (!canFindSelectedID()) return;
    const auto selection = selectedResultIndices();
    const auto row = results_[selection.front()]; // リセット・検索で results_ が置き換わる場合がある。
    const auto object = selectedFindObject();
    if (const auto resource = resourceFindID(object, summary_)) {
        replaceID(resource->second, resource->first, true); return;
    }
    const auto variable = [this]<typename T, typename ID>(const std::shared_ptr<T>& value,
                                                         const IDKind kind, const ID& id) {
        if (!value) return false;
        const auto counter = value->useCounter();
        if (counter && std::dynamic_pointer_cast<flag::LocalVariableOwner>(counter->owner())) {
            std::vector<CWXPathPtr> paths, sorted;
            for (const auto& user : counter->values(id))
                if (user && user->owner()) paths.push_back(user->owner());
            SortableCWXPath<CWXPathPtr>::sortedPaths(paths, [&](const auto& p) { sorted.push_back(p.obj); });
            setFindResult(summary::cwxPlace(counter->owner()), sorted, properties_.messages.localVariableUsers);
        } else replaceID(value->path(), kind, true);
        return true;
    };
    if (const auto p = std::dynamic_pointer_cast<flag::Flag>(object)) {
        (void)variable(p, IDKind::Flag, usecounter::FlagId(p->path())); return;
    }
    if (const auto p = std::dynamic_pointer_cast<flag::Step>(object)) {
        (void)variable(p, IDKind::Step, usecounter::StepId(p->path())); return;
    }
    if (const auto p = std::dynamic_pointer_cast<flag::Variant>(object)) {
        (void)variable(p, IDKind::Variant, usecounter::VariantId(p->path())); return;
    }
    if (const auto start = std::dynamic_pointer_cast<event::Content>(object)) {
        auto paths = startUserPaths(start);
        const auto place = paths.empty() ? CWXPathPtr{} : summary::cwxPlace(paths.front());
        setFindResult(place, paths, properties_.messages.startUsers); return;
    }
    if (row.textTarget && row.textTarget->nameIdKind)
        replaceID(row.textTarget->value(), *row.textTarget->nameIdKind, true);
}

void ReplaceDialog::setFindResult(CWXPathPtr parent_path,
                                  std::span<const CWXPathPtr> paths,
                                  StringView kind) {
    replace_mode_ = false;
    reset(true);
    external_find_results_ = true;
    result_tab_ = Tab::Text;
    configureResultColumns();
    for (const auto& value : paths) {
        if (!value) continue;
        ResultRow row;
        row.referenceObject = value;
        row.main = value->cwxPath(true);
        row.parent = parent_path ? parent_path->cwxPath(true) : String{};
        row.description = String(kind);
        row.cwxPath = row.main;
        row.count = 1U;
        addResult(std::move(row));
    }
    status_text_ = results_.empty() ? properties_.messages.searchResultEmpty
        : replaceToken(properties_.messages.searchResult,
                       std::to_string(results_.size()), kind);
    if (status_label_) status_label_->text = status_text_;
}

void ReplaceDialog::setSource(ReplaceSource source) {
    source_ = std::move(source);
    properties_.source = source_;
    refreshSource();
    refreshRangeTree(false);
    setupPaths();
    setupIDs(true, true, true);
    refreshToolbar();
}

void ReplaceDialog::setTab(Tab value) {
    tab_ = value;
    properties_.options.searchPlan = static_cast<std::size_t>(value);
    if (tabs_)
        (void)lib::select_tab_page(*tabs_, static_cast<std::size_t>(value), false);
    tabChanged();
}

void ReplaceDialog::setFromText(String value) {
    from_text_ = std::move(value);
    if (from_edit_) from_edit_->edit_text = from_text_;
    refreshToolbar();
}

void ReplaceDialog::setToText(String value) {
    to_text_ = std::move(value);
    if (to_edit_) to_edit_->edit_text = to_text_;
    refreshToolbar();
}

void ReplaceDialog::setFromPath(String value) {
    from_path_ = std::move(value);
    if (from_path_edit_) from_path_edit_->edit_text = from_path_;
    refreshToolbar();
}

void ReplaceDialog::setToPath(String value) {
    to_path_ = std::move(value);
    if (to_path_edit_) to_path_edit_->edit_text = to_path_;
    refreshToolbar();
}

void ReplaceDialog::setOptions(ReplaceOptions value) {
    const bool range_layout_changed =
        properties_.options.showAreaDirectoryTree != value.showAreaDirectoryTree ||
        properties_.options.logicalSort != value.logicalSort;
    properties_.options = std::move(value);
    undo_.max(properties_.options.undoMaxReplace);
    for (auto& [control, state] : option_controls_) {
        if (control && state) control->checked = *state;
    }
    for (std::size_t index = 0U; index < check_groups_.size(); ++index)
        refreshCheckGroup(index);
    if (contents_toolbar_) {
        auto items = contents_toolbar_->items;
        for (auto& tool : items) {
            if (tool.separator || !tool.command.starts_with("content-")) continue;
            try {
                const auto index = static_cast<std::size_t>(
                    std::stoul(tool.command.substr(8U)));
                if (index < properties_.options.contents.size())
                    tool.checked = properties_.options.contents[index];
            } catch (...) {
            }
        }
        (void)lib::replace_tool_bar_items(contents_toolbar_, std::move(items));
    }
    refreshContentsAll();
    if (grep_subdirectory_check_)
        grep_subdirectory_check_->checked = properties_.options.grepSubDirectory;
    if (grep_directory_edit_)
        grep_directory_edit_->edit_text = properties_.options.grepDirectory;
    if (realtime_check_)
        realtime_check_->checked = properties_.options.searchResultRealtime;
    if (open_dialog_check_)
        open_dialog_check_->checked = properties_.options.searchOpenDialog;
    tab_ = properties_.options.searchPlan <= static_cast<std::size_t>(Tab::Grep)
               ? static_cast<Tab>(properties_.options.searchPlan)
               : Tab::Text;
    if (tabs_)
        (void)lib::select_tab_page(*tabs_, static_cast<std::size_t>(tab_), false);
    if (split_) {
        split_->weights = {properties_.options.replaceRangeSashLeft,
                           properties_.options.replaceRangeSashRight};
        if (split_->weights[0] <= 0.0F || split_->weights[1] <= 0.0F)
            split_->weights = {3.0F, 1.0F};
    }
    refreshHistoryCombos();
    setupPaths();
    id_kind_ = properties_.options.searchIDKind <=
                       static_cast<std::size_t>(IDKind::CardGroup)
                   ? static_cast<IDKind>(properties_.options.searchIDKind)
                   : IDKind::Area;
    setupIDs(true, true, true);
    setupErrorVersions();
    tabChanged();
    if (range_layout_changed) refreshRangeTree(false);
}

void ReplaceDialog::loadProperties(ReplaceProperties value) {
    properties_ = std::move(value);
    source_ = properties_.source;
    if (window_) {
        window_->title = properties_.messages.dialogTitle;
        window_->icon = properties_.dialogImage;
        applyGeometry();
    }
    if (contents_toolbar_) {
        auto items = contents_toolbar_->items;
        for (auto& tool : items) {
            if (tool.separator || !tool.command.starts_with("content-")) continue;
            try {
                const auto index = static_cast<std::size_t>(
                    std::stoul(tool.command.substr(8U)));
                if (index < properties_.contentImages.size())
                    tool.icon = properties_.contentImages[index];
            } catch (...) {
            }
        }
        (void)lib::replace_tool_bar_items(contents_toolbar_, std::move(items));
    }
    auto options = properties_.options;
    setOptions(std::move(options));
    refreshSource();
    refreshRangeTree(false);
    refreshToolbar();
}

void ReplaceDialog::storeProperties() {
    for (const auto& [control, state] : option_controls_)
        if (control && state) *state = control->checked;
    for (const auto& group : check_groups_)
        for (const auto& [control, state] : group.members)
            if (control && state) *state = control->checked;

    if (contents_toolbar_)
        for (const auto& tool : contents_toolbar_->items) {
            if (tool.separator || !tool.command.starts_with("content-")) continue;
            try {
                const auto index = static_cast<std::size_t>(
                    std::stoul(tool.command.substr(8U)));
                if (index < properties_.options.contents.size())
                    properties_.options.contents[index] = tool.checked;
            } catch (...) {
            }
        }
    if (grep_directory_edit_)
        properties_.options.grepDirectory = grep_directory_edit_->edit_text;
    if (grep_subdirectory_check_)
        properties_.options.grepSubDirectory = grep_subdirectory_check_->checked;
    if (realtime_check_)
        properties_.options.searchResultRealtime = realtime_check_->checked;
    if (open_dialog_check_)
        properties_.options.searchOpenDialog = open_dialog_check_->checked;
    properties_.options.searchPlan = static_cast<std::size_t>(tab_);
    properties_.options.searchIDKind = static_cast<std::size_t>(id_kind_);
    if (split_ && split_->weights.size() >= 2U) {
        properties_.options.replaceRangeSashLeft = split_->weights[0];
        properties_.options.replaceRangeSashRight = split_->weights[1];
    }
    if (error_version_edit_)
        properties_.options.searchErrorTargetVersion =
            selectedValue(error_version_edit_);

    if (window_) {
        properties_.geometry.maximized = window_->maximized;
        const auto bounds = window_->bounds();
        if (!window_->maximized && bounds.width > 0.0F && bounds.height > 0.0F)
            properties_.geometry.bounds = bounds;
    }

    const auto store = properties_.storeProperties;
    if (store) store(properties_);
    if (send_reload_props_) send_reload_props_();
}

bool ReplaceDialog::canFind() const {
    if (in_proc_) return false;
    // D版と異なり、この移植版はすべてのモデル走査を内部に持つわ
    // けではない。 必須のホストアダプターがない場合、その一覧
    // タブを利用可能にしない。 検索ボタンが有効なのに必ず0件
    // では、本当に一致がない場合とユーザーが区別できないため。
    if (tab_ == Tab::Grep)
        return static_cast<bool>(source_.grepBatchTargets) ||
               static_cast<bool>(source_.grepTargets);
    if (tab_ == Tab::Contents)
        return summary_ != nullptr || static_cast<bool>(source_.contentsTargets);
    // コンテントと称号には、Summary を使う実際の代替処理がある。 下の残り
    // のD版のタブは、完全なモデル走査のアダプターが揃うまで提供処理を必要とする。
    if (tab_ == Tab::Coupon) return summary_ || static_cast<bool>(source_.couponTargets);
    if (tab_ == Tab::Unused)
        return summary_ && (static_cast<bool>(source_.unusedBatchTargets) ||
                            static_cast<bool>(source_.unusedTargets));
    if (tab_ == Tab::Error)
        return static_cast<bool>(source_.errorBatchTargets) ||
               static_cast<bool>(source_.errorTargets);
    if (tab_ == Tab::Text)
        return !from_text_.empty() &&
               (summary_ || source_.textTargets || source_.textBatchTargets);
    if (tab_ == Tab::Path)
        return !from_path_.empty() &&
               (summary_ || source_.pathTargets || source_.pathBatchTargets);
    return !from_id_.empty() &&
           (summary_ || source_.idTargets || source_.idBatchTargets);
}

bool ReplaceDialog::canReplace() const {
    if (!canFind()) return false;
    if (tab_ == Tab::Text && properties_.options.ignoreReturnCode) return false;
    return tab_ == Tab::Text || tab_ == Tab::Id || tab_ == Tab::Path;
}

void ReplaceDialog::search() {
    const auto operation_alive = lifetime_alive_;
    const std::weak_ptr<lib::Widget> prior =
        comm_.services.gui ? comm_.services.gui->focused_widget()
                           : lib::WidgetPtr{};
    const auto focus_revision =
        comm_.services.gui ? comm_.services.gui->focus_revision() : 0U;
    replaceImpl(false);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    restoreOperationFocus(prior, focus_revision, false);
}

void ReplaceDialog::replace() {
    const auto operation_alive = lifetime_alive_;
    if (!canReplace()) return;
    const std::weak_ptr<lib::Widget> prior =
        comm_.services.gui ? comm_.services.gui->focused_widget()
                           : lib::WidgetPtr{};
    const auto focus_revision =
        comm_.services.gui ? comm_.services.gui->focus_revision() : 0U;
    replaceImpl(true);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    restoreOperationFocus(prior, focus_revision, true);
}

void ReplaceDialog::undo() {
    if (!undo_.canUndo() || in_proc_) return;
    const auto operation_alive = lifetime_alive_;
    auto* const editor_commons = properties_.editor_commons;
    const auto commons_life = editor_commons ? editor_commons->lifetimeToken() : std::weak_ptr<const int>{};
    const auto refresh_jpy1 = [operation_alive, editor_commons, commons_life] {
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (!editor_commons || commons_life.expired()) return true;
        const bool changed = editor_commons->updateJpy1Files(true);
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (!changed || commons_life.expired()) return true;
        auto* const sync = editor_commons->sync();
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (sync) sync->sync();
        return operation_alive->load(std::memory_order_acquire);
    };
    in_proc_ = true;
    in_undo_ = true;
    resultRedraw(false);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    try {
        (void)undo_.undo();
        if (!operation_alive->load(std::memory_order_acquire)) return;
        // D版と同様、パスの取り消し後は毎回JPY1/JPDCの解析済み
        // 情報を更新する。 ALL のウィンドウも含めて共通の履歴経路
        // を同期し、次の保存で古いファイル名を直列化しないようにする。
        if (!refresh_jpy1()) return;
        // D版と同様、通知済みの結果は保持し、表示キャッシュだけを消す。
        // 共通の表には、ここで無効化する独立した仮想キャッシュはない。
    } catch (...) {
        const auto failure = std::current_exception();
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        // 逆操作の適用中に、監視先・提供側が失敗する場合があ
        // る。 ダイアログを処理中・再描画禁止のままにせず、
        // 通知済みの結果の識別情報はD版と同様に維持する。
        in_undo_ = false;
        in_proc_ = false;
        try { resultRedraw(true); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { refreshToolbar(); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        // D版の scope(exit) と同様、例外を外へ伝える前に
        // 状態を戻す。 ここでの更新フックの追加失敗は許容し、元のコ
        // マンドの失敗を置き換えたり、処理中の状態へ戻したりしない。
        try { refreshSource(); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { after(true, false, std::nullopt); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { if (comm_.refIds) comm_.refIds(); } catch (...) {}
        std::rethrow_exception(failure);
    }
    if (!operation_alive->load(std::memory_order_acquire)) return;
    resultRedraw(true);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    in_undo_ = false;
    in_proc_ = false;
    refreshToolbar();
}

void ReplaceDialog::redo() {
    if (!undo_.canRedo() || in_proc_) return;
    const auto operation_alive = lifetime_alive_;
    auto* const editor_commons = properties_.editor_commons;
    const auto commons_life = editor_commons ? editor_commons->lifetimeToken() : std::weak_ptr<const int>{};
    const auto refresh_jpy1 = [operation_alive, editor_commons, commons_life] {
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (!editor_commons || commons_life.expired()) return true;
        const bool changed = editor_commons->updateJpy1Files(true);
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (!changed || commons_life.expired()) return true;
        auto* const sync = editor_commons->sync();
        if (!operation_alive->load(std::memory_order_acquire)) return false;
        if (sync) sync->sync();
        return operation_alive->load(std::memory_order_acquire);
    };
    in_proc_ = true;
    in_undo_ = true;
    resultRedraw(false);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    try {
        (void)undo_.redo();
        if (!operation_alive->load(std::memory_order_acquire)) return;
        // D版と同様、パスのやり直し後は毎回JPY1/JPDCの
        // 解析済み情報を更新する。 ALL も含めて共通の履歴経
        // 路を同期し、次の保存で古いファイル名を直列化しない。
        if (!refresh_jpy1()) return;
    } catch (...) {
        const auto failure = std::current_exception();
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        in_undo_ = false;
        in_proc_ = false;
        try { resultRedraw(true); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { refreshToolbar(); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { refreshSource(); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { after(true, false, std::nullopt); } catch (...) {}
        if (!operation_alive->load(std::memory_order_acquire))
            std::rethrow_exception(failure);
        try { if (comm_.refIds) comm_.refIds(); } catch (...) {}
        std::rethrow_exception(failure);
    }
    if (!operation_alive->load(std::memory_order_acquire)) return;
    resultRedraw(true);
    if (!operation_alive->load(std::memory_order_acquire)) return;
    in_undo_ = false;
    in_proc_ = false;
    refreshToolbar();
}

void ReplaceDialog::changed() {
    if (!in_undo_ && !in_proc_) undo_.reset();
    if (in_proc_) cancel_ = true;
    else refreshToolbar();
}

void ReplaceDialog::reset(bool remove_columns) {
    grep_progress_.reset();
    result_tab_ = tab_;
    external_find_results_ = false;
    if (!in_undo_ && !in_proc_) undo_.reset();
    results_.clear();
    if (result_table_) {
        result_table_->clearAll();
        if (remove_columns) {
            result_table_->columns.clear();
            result_table_->setHeaderVisible(false);
        }
    }
    status_text_ = replace_mode_ ? properties_.messages.replaceResultEmpty
                                 : properties_.messages.searchResultEmpty;
    if (status_label_) status_label_->text = status_text_;
    refreshToolbar();
}

std::vector<std::size_t> ReplaceDialog::selectedResultIndices() const {
    return result_table_ ? result_table_->selectionIndices()
                         : std::vector<std::size_t>{};
}

void ReplaceDialog::resultRedraw(bool value) {
    result_redraw_ = value;
    if (result_table_) result_table_->setRedraw(value);
}

void ReplaceDialog::setRangeChecked(CWXPathPtr value, bool checked) {
    if (!value) return;
    range_table_[value.get()] = checked;
    refreshRangeTree(false);
}

std::vector<CWXPathPtr> ReplaceDialog::searchRange() const {
    std::vector<CWXPathPtr> result;
    std::function<void(const std::shared_ptr<RangeNode>&)> visit =
        [&](const std::shared_ptr<RangeNode>& node) {
            if (!node) return;
            if (node->path) {
                const auto found = range_table_.find(node->path.get());
                if (found != range_table_.end() && found->second)
                    result.push_back(node->path);
            }
            for (const auto& child : node->children) visit(child);
        };
    for (const auto& node : defaultRangeNodes()) visit(node);
    return result;
}

std::vector<CWXPathPtr> ReplaceDialog::rangeTree(SummaryPtr summ) {
    std::vector<CWXPathPtr> result;
    (void)enumerateRangeTree(std::move(summ), [&result](CWXPathPtr value) {
        result.push_back(std::move(value));
        return true;
    });
    return result;
}

bool ReplaceDialog::enumerateRangeTree(
    SummaryPtr summ, const std::function<bool(CWXPathPtr)>& emit) {
    if (!summ || !emit) return true;
    if (!emit(summaryPath(summ))) return false;
    if (summ->flagDirRoot() && !emit(summ->flagDirRoot())) return false;
    const auto each = [&](const auto& values) {
        for (const auto& value : values) {
            if (!value) continue;
            if constexpr (std::is_base_of_v<area::AbstractArea, typename std::remove_reference_t<decltype(value)>::element_type>) {
                if (!emit(std::static_pointer_cast<area::AbstractArea>(value))) return false;
            } else if (!emit(value)) return false;
        }
        return true;
    };
    if (!each(summ->areas()) || !each(summ->battles()) || !each(summ->packages())) return false;
    for (const auto& cast : summ->casts()) {
        if (!cast) continue;
        if (!emit(cast)) return false;
        const auto carried = [&](const auto& values) {
            for (const auto& value : values)
                if (value && !value->linkId() && !emit(value)) return false;
            return true;
        };
        if (!carried(cast->skills()) || !carried(cast->items()) || !carried(cast->beasts())) return false;
    }
    return each(summ->skills()) && each(summ->items()) && each(summ->beasts()) && each(summ->infos());
}

bool ReplaceDialog::dec(const CWXPathPtr& value, const RangeMap& range) {
    if (range.empty()) return true;
    if (!value) return true;
    if (const auto found = range.find(value.get()); found != range.end())
        return found->second;
    return dec(value->cwxParent().lock(), range);
}

void ReplaceDialog::refreshRangeTree(bool clear_checked) {
    if (!range_view_) return;
    const RangeMap source_range = clear_checked ? RangeMap{} : range_table_;
    const auto roots = defaultRangeNodes();
    std::unordered_set<const CWXPath*> selected_paths;
    std::unordered_set<String> selected_directories;
    for (const auto id : range_view_->selection) {
        if (const auto found = range_paths_.find(id); found != range_paths_.end())
            selected_paths.insert(found->second.get());
        else
            for (const auto& node : range_view_->nodes)
                if (node.id == id) selected_directories.insert(node.value);
    }
    RangeMap next_range;
    std::unordered_map<const RangeNode*, bool> visible;
    std::function<bool(const std::shared_ptr<RangeNode>&)> prepare = [&](const auto& node) {
        if (!node) return false;
        if (node->path) {
            const auto old = source_range.find(node->path.get());
            next_range[node->path.get()] = old == source_range.end() ? node->checked : old->second;
        }
        bool show = node->path && (node->alwaysVisible || !inc_search_ ||
            inc_search_->match(node->matchName.empty() ? node->label : node->matchName));
        for (const auto& child : node->children) show = prepare(child) || show;
        visible[node.get()] = show;
        return show;
    };
    for (const auto& node : roots) prepare(node);
    std::vector<lib::TreeNode> nodes;
    std::vector<lib::TreeItemId> selection;
    std::unordered_map<lib::TreeItemId, CWXPathPtr> paths;
    std::function<void(const std::shared_ptr<RangeNode>&, lib::TreeItemId)> add =
        [&](const std::shared_ptr<RangeNode>& node, lib::TreeItemId parent_id) {
            if (!node || !visible[node.get()]) return;
            const auto id = static_cast<lib::TreeItemId>(nodes.size() + 1U);
            const auto cwx_path = node->cwxPath.empty() && node->path
                                      ? node->path->cwxPath(true)
                                      : node->cwxPath;
            nodes.push_back({id, parent_id,
                             node->label.empty() ? cwx_path : node->label,
                             cwx_path, node->image, true,
                             node->path ? next_range.at(node->path.get()) : true, false});
            if (node->path) paths[id] = node->path;
            if ((node->path && selected_paths.contains(node->path.get())) ||
                (!node->path && selected_directories.contains(cwx_path)))
                selection.push_back(id);
            for (const auto& child : node->children) add(child, id);
            if (!node->path)
                nodes[id - 1U].checked = std::all_of(
                    nodes.begin(), nodes.end(),
                    [id](const lib::TreeNode& child) { return child.parent != id || child.checked; });
        };
    for (const auto& node : roots) add(node, 0U);
    auto publication = lib::prepare_tree_nodes(range_view_, std::move(nodes),
                                                selection);
    if (!publication.commit()) return;
    range_table_ = std::move(next_range);
    range_paths_.swap(paths);
    refreshRangeAllCheck();
}

void ReplaceDialog::setRangeSearchText(String text) {
    if (inc_search_) inc_search_->setSearchText(std::move(text));
}

void ReplaceDialog::setVisibleRangeChecked(const bool checked) {
    for (const auto& [item, owner] : range_paths_)
        if (owner) range_table_[owner.get()] = checked;
    refreshRangeTree(false);
}

void ReplaceDialog::refreshRangeAllCheck() {
    if (!range_all_check_) return;
    range_all_check_->checked = range_table_.empty() ||
                                std::all_of(range_table_.begin(), range_table_.end(),
                                            [](const auto& entry) { return entry.second; });
}

void ReplaceDialog::incSearch() {
    if (!inc_search_) return;
    inc_search_->startIncSearch();
}

void ReplaceDialog::searchAll(
    CWXPathPtr parent_path, CWXPathPtr value, std::size_t& count,
    const std::function<void(CWXPathPtr, CWXPathPtr, std::size_t&, StringView)>& callback,
    String cwx_path) const {
    if (cancel_ || !value) return;
    if (cwx_path.empty()) {
        cwx_path = value->cwxPath(true);
        if (summary_ && value == summary_->flagDirRoot()) cwx_path = "variable";
    }
    if (callback) callback(parent_path, value, count, cwx_path);
    // 一致数はコールバックが管理する。子孫は同じリソースの親を維持する。
    const auto child = [&](const CWXPathPtr& next, String path) {
        if (next) searchAll(parent_path, next, count, callback, std::move(path));
    };
    const auto indexed = [&](const auto& values, StringView category) {
        for (std::size_t i = 0; i < values.size(); ++i)
            child(values[i], path::cpjoin(cwx_path, category, i));
    };
    if (auto dir = std::dynamic_pointer_cast<flag::FlagDir>(value)) {
        indexed(dir->flags(), "flag"); indexed(dir->steps(), "step");
        indexed(dir->variants(), "variant"); indexed(dir->subDirs(), "dir");
    }
    if (auto local = std::dynamic_pointer_cast<flag::LocalVariableOwner>(value))
        child(local->flagDirRoot(), path::cpjoin(cwx_path, "variable"));
    // EffectCardのイベントツリーも、次のEventTreeOwner共通処理で走査する。
    if (auto owner = std::dynamic_pointer_cast<event::EventTreeOwner>(value)) indexed(owner->trees(), "event");
    if (auto tree = std::dynamic_pointer_cast<event::EventTree>(value)) indexed(tree->starts(), "");
    if (auto area = std::dynamic_pointer_cast<area::Area>(value)) {
        if (area->playerEvents()) child(area->playerEvents()->ownerHandle(), path::cpjoin(cwx_path, "playercard"));
        indexed(area->cards(), "menucard"); indexed(area->backs(), "background");
    }
    if (auto battle = std::dynamic_pointer_cast<area::Battle>(value)) {
        if (battle->playerEvents()) child(battle->playerEvents()->ownerHandle(), path::cpjoin(cwx_path, "playercard"));
        indexed(battle->cards(), "enemycard");
    }
    if (auto owner = std::dynamic_pointer_cast<motion::MotionOwner>(value)) {
        const auto motions = owner->motions();
        for (std::size_t i = 0; i < motions.size(); ++i)
            if (motions[i] && motions[i]->beast() && !motions[i]->beast()->linkId())
                child(motions[i]->beast(), path::cpjoin(path::cpjoin(cwx_path, "motion", i), "beastcard", 0));
    }
    if (auto content = std::dynamic_pointer_cast<event::Content>(value)) {
        indexed(content->dialogs(), "dialog"); indexed(content->backgroundImages(), "background");
        indexed(content->next(), "");
    }
}

std::pair<String, bool> ReplaceDialog::initText(String from, String to) {
    from_text_ = std::move(from);
    to_text_ = std::move(to);
    text_initialised_ = true;
    const auto mode = properties_.options.regex
                          ? SearchMode::Regex
                          : (properties_.options.wildcard ? SearchMode::Wildcard
                                                          : SearchMode::Plain);
    if (mode == SearchMode::Regex) {
        try {
            (void)srell::u8cregex(from_text_, replaceRegexOptions(properties_.options));
        } catch (const srell::regex_error& failure) {
            text_initialised_ = false;
            const auto result = std::make_pair(to_text_, false);
            lib::MessageDialogRequest request;
            request.title = "警告";
            String prefix = "正規表現が正しくありません。";
            if (properties_.editor_commons && properties_.editor_commons->prop()) {
                const auto& messages = properties_.editor_commons->prop()->requiredMsgs();
                request.title = messages.dlgTitWarning.value;
                prefix = messages.regexError.value;
            }
            request.message = prefix + "\n" + failure.what();
            request.severity = lib::MessageSeverity::Warning;
            const auto gui = comm_.services.gui;
            const auto dialogs = comm_.services.dialogs;
            const auto owner = window_;
            // D版と同様、この警告は置換ウィンドウを所有者にする。 モーダルのコールバックが
            // このオブジェクトを無効化する場合があるため、戻り値はすべてローカルで保持する。
            if (gui) {
                auto presentation = lib::make_message_dialog(request, owner);
                (void)gui->show_modal(presentation.dialog, owner);
            } else if (dialogs) {
                (void)dialogs->message(request);
            }
            return result;
        }
    }
    return {to_text_, text_initialised_};
}

void ReplaceDialog::exitText() {
    text_initialised_ = false;
}

String ReplaceDialog::fTextRepl(StringView value) const {
    // D版の改行コード無視は一致数を数えるときに適用し
    // 、置換自体は元のテキストに対して行う。 ここで検
    // 索パターンだけから改行を除くと、複数行パターンが
    // 予期せず一致し、プレビューの件数とも食い違う。
    const String& from = from_text_;
    String source = String(value);
    if (from.empty() && (!properties_.options.exact || properties_.options.regex ||
                         properties_.options.wildcard)) return source;
    if (properties_.options.regex) {
        try {
            const auto flags = replaceRegexOptions(properties_.options);
            const srell::u8cregex expression(from, flags);
            return srell::regex_replace(source, expression, to_text_);
        } catch (const srell::regex_error&) {
            return source;
        }
    }
    if (properties_.options.wildcard) {
        return utils::Wildcard(from, !properties_.options.notIgnoreCase)
            .replace(source, to_text_);
    }
    if (properties_.options.exact) {
        const bool same = properties_.options.notIgnoreCase
                              ? source == from
                              : ::cwxeditor::cwx::enumutils::simpleLowerCase(source) ==
                                ::cwxeditor::cwx::enumutils::simpleLowerCase(from);
        // D版の公開補助関数は、完全一致しない場合に元の検索パターンを
        // 返す。 通常の置換側は、適用前に一致数を数える必要がある。
        return same ? to_text_ : from;
    }
    return properties_.options.notIgnoreCase
               ? [&] {
                     String result;
                     std::size_t start = 0U;
                     while (true) {
                         const auto found = source.find(from, start);
                         if (found == String::npos) {
                             result.append(source, start, String::npos);
                             break;
                         }
                         result.append(source, start, found - start);
                         result.append(to_text_);
                         start = found + from.size();
                     }
                     return result;
                 }()
               : utils::ireplace(source, from, to_text_);
}

std::size_t ReplaceDialog::fTextCount(StringView value) const {
    if (from_text_.empty()) return 0U;
    String source = properties_.options.ignoreReturnCode ? singleLine(value) : String(value);
    const String& from = from_text_;
    if (properties_.options.regex) {
        try {
            const auto flags = replaceRegexOptions(properties_.options);
            const srell::u8cregex expression(from, flags);
            std::size_t result = 0U;
            for (srell::u8csregex_iterator it(source.begin(), source.end(), expression), end;
                 it != end; ++it)
                ++result;
            return result;
        } catch (const srell::regex_error&) {
            return 0U;
        }
    }
    if (properties_.options.wildcard)
        return utils::Wildcard(from, !properties_.options.notIgnoreCase).count(source);
    if (properties_.options.exact) {
        return (properties_.options.notIgnoreCase ? source == from
                                                   : ::cwxeditor::cwx::enumutils::simpleLowerCase(source) ==
                                ::cwxeditor::cwx::enumutils::simpleLowerCase(from))
                   ? 1U
                   : 0U;
    }
    if (properties_.options.notIgnoreCase) {
        std::size_t count = 0U;
        std::size_t position = 0U;
        while ((position = source.find(from, position)) != String::npos) {
            ++count;
            position += std::max<std::size_t>(1U, from.size());
        }
        return count;
    }
    return utils::icount(source, from);
}

bool ReplaceDialog::cautionReplace(StringView name1, StringView name2) const {
    if (!replace_mode_ || !properties_.options.cautionBeforeReplace) return true;
    if (properties_.messages.cautionOfReplace.empty()) return true;
    if (caution_confirmed_) return true;
    if (!comm_.services.dialogs) return false;
    lib::MessageDialogRequest request;
    request.title = "確認";
    request.message = replaceToken(properties_.messages.cautionOfReplace,
                                    name1, name2);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNo;
    const auto result = comm_.services.dialogs->message(request);
    return result == lib::MessageResult::Yes || result == lib::MessageResult::OK;
}

void ReplaceDialog::warnOpenFailure(StringView path, bool file) const {
    if (!comm_.services.dialogs) return;
    lib::MessageDialogRequest request;
    request.title = properties_.messages.dialogTitle;
    request.severity = lib::MessageSeverity::Warning;
    if (properties_.editor_commons && properties_.editor_commons->prop()) {
        const auto& messages = properties_.editor_commons->prop()->requiredMsgs();
        request.message = utils::tryFormat(file ? messages.filePathOpenError.value : messages.cwxPathOpenError.value, String(path));
    } else request.message = "Unable to open: " + String(path);
    (void)comm_.services.dialogs->message(request);
}

bool ReplaceDialog::canOpenPath() const {
    if (!result_table_) return false;
    for (const auto index : result_table_->selectionIndices()) {
        if (index >= results_.size()) continue;
        const auto& row = results_[index];
        if ((source_.openExternal && row.externalOpenTarget) ||
            (source_.openCWXPath && !row.cwxPath.empty())) return true;
    }
    return false;
}

bool ReplaceDialog::openPath(bool shift) {
    if (!result_table_ || result_table_->selectionIndices().empty()) return false;
    const auto alive = lifetime_alive_;
    const auto selected = result_table_->selectionIndices().front();
    std::vector<std::size_t> order{selected};
    for (std::size_t i = 0; i < results_.size(); ++i) if (i != selected) order.push_back(i);
    const bool open_dialog = properties_.options.searchOpenDialog != shift;
    for (const auto index : order) {
        if (index >= results_.size()) continue;
        const auto row = results_[index];
        if (row.externalOpenTarget && source_.openExternal) {
            bool opened = false;
            const auto open = source_.openExternal;
            try { opened = open(*row.externalOpenTarget, open_dialog); }
            catch (const std::exception&) {}
            if (!alive->load(std::memory_order_acquire)) return opened;
            if (!opened) {
                const bool file = row.externalOpenTarget->kind != ExternalOpenKind::ScenarioCWXPath;
                const auto& target = file ? row.externalOpenTarget->relativePath : row.externalOpenTarget->cwxPath;
                warnOpenFailure(target.empty() ? row.main : target, file);
            }
            return opened;
        }
        if (row.cwxPath.empty() || !source_.openCWXPath) continue;
        String target = row.cwxPath;
        if (open_dialog) target = path::cpaddattr(std::move(target), "opendialog");
        target = path::cpaddattr(std::move(target), "only");
        bool opened = false;
        const auto open = source_.openCWXPath;
        try { opened = open(target, false); }
        catch (const std::exception&) {}
        if (!alive->load(std::memory_order_acquire)) return opened;
        if (!opened) warnOpenFailure(target);
        return opened;
    }
    return false;
}

ResourceId ReplaceDialog::fimage(StringView file, const Skin* skin) const {
    if (!comm_.services.image_codecs || !comm_.services.graphics || file.empty()) return 0U;
    std::vector<std::filesystem::path> candidates;
    const auto relative = structs::pathFromUtf8(file);
    if (relative.is_absolute()) candidates.push_back(relative);
    else {
        if (summary_ && !summary_->scenarioPath().empty())
            candidates.emplace_back(structs::pathFromUtf8(summary_->scenarioPath()) / relative);
        if (skin && !skin->path().empty())
            candidates.emplace_back(structs::pathFromUtf8(skin->path()) / relative);
        candidates.push_back(relative);
    }
    for (const auto& candidate : candidates) {
        lib::DecodeImageRequest request;
        request.path = candidate;
        String error;
        const auto data = comm_.services.image_codecs->decode(request, error);
        if (error.empty() && !data.frames.empty())
            return comm_.services.graphics->create_image(data);
    }
    return 0U;
}

void ReplaceDialog::copyResult() {
    if (!result_table_) return;
    const auto selection = result_table_->selectionIndices();
    if (selection.empty()) return;
    std::vector<String> values;
    values.reserve(selection.size());
    String text;
    for (const auto index : selection) {
        if (index >= results_.size()) continue;
        const auto& table_row = result_table_->rows[index];
        String line;
        for (std::size_t column = 0U; column < table_row.cells.size(); ++column) {
            if (column != 0U) line.push_back('\t');
            // ClipboardData はLFに正規化し、ネイティブのバックエ
            // ンドが改行を一度だけ変換する。 ここではセル内の改行を保持する。
            line.append(table_row.cells[column].text);
        }
        values.push_back(line);
        if (!text.empty()) text.push_back('\n');
        text.append(line);
    }
    if (values.empty()) return;
    if (comm_.copyConverted && comm_.copyConverted(values, "replace-result")) return;
    const auto clipboard = comm_.clipboardBackend();
    if (!clipboard) return;
    clipboard->clear();
    clipboard->write(lib::ClipboardData{std::move(text), {}, {}});
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

undo::UndoOwner ReplaceDialog::store(String file_path, undo::UndoArray operations) {
    if (operations.empty()) return {};
    return std::make_shared<RUndo>(std::move(file_path), std::move(operations));
}

undo::UndoOwner ReplaceDialog::store(CWXPathPtr parent_path,
                                     CWXPathPtr path_value, String cwx_path,
                                     undo::UndoArray operations) {
    if (operations.empty()) return {};
    return std::make_shared<RUndo>(std::move(parent_path), std::move(path_value),
                                   std::move(cwx_path), std::move(operations));
}

bool ReplaceDialog::repl(CWXPathPtr parent_path, CWXPathPtr path_value,
                         String cwx_path, String text,
                         std::function<void(String)> set,
                         std::size_t& count, undo::UndoArray& operations,
                         bool store_to_array, bool variable_path) {
    const auto original = text;
    String prefix;
    const system::System fallback_system;
    const auto* system = &fallback_system;
    if (properties_.editor_commons && properties_.editor_commons->prop())
        system = &properties_.editor_commons->prop()->parent().sys();
    if (variable_path && system->isLocalVariable(text)) {
        prefix = String(flag::FlagDir::SEPARATOR) + system->localVariablePrefix() +
                 String(flag::FlagDir::SEPARATOR);
        text.erase(0U, prefix.size());
    }
    const auto matches = fTextCount(text);
    if (matches == 0U) return false;
    // 検索はモデルを変更しない。D版と同様、設定処理を呼ぶのは _replMode
    // が true の場合だけにする。 検索から変更すると、モデ
    // ルを壊し、表示結果を説明できない取り消し履歴を作ってしまう。
    if (!replace_mode_) {
        count += matches;
        return true;
    }
    const auto replacement = prefix + fTextRepl(text);
    if (replacement == original || !set) {
        count += matches;
        return true;
    }
    if (!cautionReplace(original, replacement)) return false;
    const auto operation = std::make_shared<ValueUndo>(original, replacement, set);
    // D版と同様、切り離し・配列保存用の取り消し処理は、コールバック
    // を呼ぶ前に登録する。 一部を書いた後で例外を出しても、復旧操
    // 作を残す。 パスに結び付いた配列以外の保存は、その後に行う。
    const bool store_before_set = !path_value || store_to_array;
    if (store_before_set) operations.push_back(operation);
    count += matches;
    set(replacement);
    // この共通APIは、D版のパスに結び付いた RUndo を呼び出し元
    // の配列へ平坦化するが、 設定処理の前後での登録境界は同じに保つ。
    if (!store_before_set) operations.push_back(operation);
    (void)parent_path;
    (void)cwx_path;
    return true;
}

bool ReplaceDialog::replFilePath(String text, std::function<void(String)> set,
                                 std::size_t& count,
                                 undo::UndoArray& operations) {
    const auto matches = fTextCount(utils::encodePath(text));
    if (matches == 0U) return false;
    if (!replace_mode_ || !set) {
        count += matches;
        return true;
    }
    const auto original = utils::decodePath(text);
    const auto replacement = fTextRepl(original);
    if (replacement == original) {
        count += matches;
        return true;
    }
    if (!cautionReplace(original, replacement)) return false;
    operations.push_back(std::make_shared<ValueUndo>(original, replacement, set));
    count += matches;
    set(replacement);
    return true;
}

bool ReplaceDialog::replImagePaths(
    std::vector<CardImagePtr> images,
    std::function<void(std::vector<CardImagePtr>)> set,
    std::size_t& count, undo::UndoArray& operations) {
    if (!set) return false;
    const auto operation_alive = lifetime_alive_;
    // パスの操作欄と、テキスト・Grepの検索語は独立している。 APIの
    // コールバックが画像配列を一部代入した後で例外を出しても、復元する。
    struct QueryRestore {
        std::shared_ptr<std::atomic_bool> alive;
        String& from;
        String& to;
        bool& initialised;
        String old_from;
        String old_to;
        bool old_initialised;
        bool active;
        ~QueryRestore() {
            if (!active || !alive->load(std::memory_order_acquire)) return;
            from.swap(old_from);
            to.swap(old_to);
            initialised = old_initialised;
        }
    } restore{operation_alive, from_text_, to_text_, text_initialised_, from_text_, to_text_,
              text_initialised_, tab_ == Tab::Path};
    if (restore.active) (void)initText(from_path_, to_path_);
    if (!operation_alive->load(std::memory_order_acquire)) return false;
    auto replacement = images;
    bool matched = false;
    bool changed_value = false;
    for (std::size_t index = 0U; index < images.size(); ++index) {
        if (!images[index] || images[index]->type != types::CardImageType::File ||
            utils::isBinImg(images[index]->path())) continue;
        const auto source = images[index]->path();
        const auto matches = fTextCount(utils::encodePath(source));
        if (matches == 0U) continue;
        count += matches;
        matched = true;
        const auto before = utils::decodePath(source);
        const auto after = fTextRepl(before);
        if (after == before) continue;
        replacement[index] = std::make_shared<CardImage>(
            after, images[index]->positionType());
        changed_value = true;
    }
    // 検索では観測だけを行う意図的な方針を維持する。 D版の
    // 非公開の画像処理は検索一致でも同じ配列を代入して履歴を
    // 作るが、 この公開APIでは通常の検索で変更しない。
    if (!replace_mode_ || !changed_value) return matched;
    if (!cautionReplace("image", "image")) return false;
    operations.push_back(std::make_shared<CardImagesUndo>(images, replacement, set));
    set(replacement);
    return true;
}

bool ReplaceDialog::replFontsInText(CWXPathPtr parent_path, TextTarget target,
                                    std::size_t& count,
                                    undo::UndoArray& operations) {
    if (!properties_.options.file || !target.enabled) return false;
    const auto original = target.value();
    // 共通APIはD版の ITextHolder の代わりに TextTarget
    // を受け取る。 製品の保持クラスで本文全体を解析し、提供されたコ
    // ールバックを通して、所有するテキスト全体を確定・再適用する。 通
    // 常の本文の文字や無関係な変数記号は、フォント置換の候補にしない。
    textholder::TextHolder holder({});
    holder.text(original);
    const auto fonts = holder.fontsInText();
    bool matched = false;
    for (std::size_t index = 0U; index < fonts.size(); ++index) {
        const auto codepoint = msgutils::decodeFontPath(fonts[index]);
        String character;
        if (codepoint < 0x80) character += char(codepoint);
        else if (codepoint < 0x800) {
            character += char(0xc0 | (codepoint >> 6));
            character += char(0x80 | (codepoint & 63));
        } else if (codepoint < 0x10000) {
            character += char(0xe0 | (codepoint >> 12));
            character += char(0x80 | ((codepoint >> 6) & 63));
            character += char(0x80 | (codepoint & 63));
        } else {
            character += char(0xf0 | (codepoint >> 18));
            character += char(0x80 | ((codepoint >> 12) & 63));
            character += char(0x80 | ((codepoint >> 6) & 63));
            character += char(0x80 | (codepoint & 63));
        }
        const auto matches = fTextCount(character);
        if (matches == 0U) continue;
        count += matches;
        matched = true;
        if (!replace_mode_ || !target.replaceable || !target.write) continue;
        const auto replacement = msgutils::toGraphemeArray(fTextRepl(character));
        if (replacement.empty() || replacement.front().empty()) continue;
        const auto extension = structs::pathToUtf8(
            structs::pathFromUtf8(fonts[index]).extension());
        const auto path = msgutils::encodeFontPath(replacement.front().front(), extension);
        if (!path.empty()) holder.changePathInText(index, path);
    }
    const auto replacement = holder.text();
    if (!replace_mode_ || replacement == original || !target.write ||
        !target.replaceable) return matched;
    if (!cautionReplace(original, replacement)) return false;
    auto setter = std::move(target.write);
    const auto operation = std::make_shared<ValueUndo>(original, replacement,
        [setter](String value) { (void)setter(std::move(value)); });
    operations.push_back(operation);
    if (!setter(replacement)) operations.pop_back();
    (void)parent_path;
    return matched;
}

bool ReplaceDialog::replFlagsInText(CWXPathPtr parent_path, TextTarget target,
                                    std::size_t& count,
                                    undo::UndoArray& operations) {
    if (!target.enabled || !properties_.options.variableName ||
        properties_.options.message) return false;
    textholder::TextHolder holder({});
    holder.text(target.value());
    auto references = holder.flagsInText();
    const auto steps = holder.stepsInText();
    const auto variants = holder.variantsInText();
    references.insert(references.end(), steps.begin(), steps.end());
    references.insert(references.end(), variants.begin(), variants.end());
    bool variable_root_selected = false;
    // D版と同様、チェックされた最上位の FlagDir の範囲項目を調べ
    // る。 定義が範囲に含まれる場合、検索の記号照合にはローカルの接頭辞
    // を含める。 参照だけの分岐でのみ、名前空間の接頭辞として除外する。
    for (const auto& node : defaultRangeNodes()) {
        if (!node || !std::dynamic_pointer_cast<flag::FlagDir>(node->path)) continue;
        const auto selected = range_table_.find(node->path.get());
        if (selected == range_table_.end() ? node->checked : selected->second) {
            variable_root_selected = true;
            break;
        }
    }
    bool matched = false;
    // D版の補助関数と同様、通常の本文ではなく埋め込まれた各変数記号
    // を報告する。 設定処理は元の記号を書き、局所的な取り消し配列は
    // 返さない。 通常の走査と同様、この変更しない動作を維持する。
    undo::UndoArray unused;
    for (const auto& reference : references)
        matched |= repl(parent_path, {}, {}, reference, {}, count, unused,
                        false, !variable_root_selected);
    (void)operations;
    return matched;
}

bool ReplaceDialog::replContent(CWXPathPtr parent_path, TextTarget target,
                                String cwx_path, std::size_t& count) {
    undo::UndoArray operations;
    const bool result = repl(std::move(parent_path), target.owner,
                             std::move(cwx_path), target.value(),
                             std::move(target.write), count, operations);
    if (result && !operations.empty()) undo_.add(std::make_shared<UndoRepl>(
        std::move(operations), true, [this] { refreshSource(); }, this));
    return result;
}

bool ReplaceDialog::replaceTextTarget(TextTarget target, std::size_t& count,
                                      undo::UndoArray& operations) {
    if (!target.enabled || !targetSelected(target)) return false;
    const auto before = target.filePath ? utils::decodePath(target.value()) : target.value();
    const bool listing = tab_ == Tab::Contents || tab_ == Tab::Coupon ||
                         tab_ == Tab::Unused || tab_ == Tab::Error ||
                         (tab_ == Tab::Grep && target.wholeScenarioListing);
    // D版の素材タブと同様、テキスト側の正規表現・大小文字・単語単位の設定とは
    // 独立して、 パス全体をワイルドカードで照合し、新しいパス全体を代入する。
    const bool material = tab_ == Tab::Path;
    String normalized_material_from;
    StringView material_from = from_path_;
    if (material && !lib::editor_window_host_options().all_mode && summary_ &&
        summary_->loadScaledImage()) {
        // D版と同様、PathId の一致検査と素材の PathUser のワイルド
        // カード走査の両方より前に検索語を正規化する。 ここでも同じ論理パスを使う。
        const auto unscaled = utils::noScaledPath(dutils::filesystemPath(material_from));
        if (!unscaled.empty()) {
            normalized_material_from = dutils::pathString(unscaled);
            material_from = normalized_material_from;
        }
    }
    const auto parts = target.readParts ? target.readParts() : std::vector<String>{before};
    auto replacements = parts;
    std::size_t matches = 0U;
    const system::System fallback_system;
    const auto* system = &fallback_system;
    if (properties_.editor_commons && properties_.editor_commons->prop())
        system = &properties_.editor_commons->prop()->parent().sys();
    for (std::size_t i = 0U; i < parts.size(); ++i) {
        String prefix;
        String value = target.filePath ? utils::decodePath(parts[i]) : parts[i];
        if (target.variablePath && system->isLocalVariable(value)) {
            prefix = String(flag::FlagDir::SEPARATOR) + system->localVariablePrefix() + String(flag::FlagDir::SEPARATOR);
            value.erase(0U, prefix.size());
        }
        const auto part_matches = material ? static_cast<std::size_t>(
            utils::Wildcard(usecounter::toPathId(material_from).id, utils::fncmp("A", "a") == 0)
                .match(usecounter::toPathId(value).id))
                : fTextCount(target.filePath ? utils::encodePath(value) : value);
        matches += part_matches;
        // 公開の完全一致補助関数は、不一致で from を返す。D版は
        // 個別フィールドの一致を数えてから呼ぶため、 共通アダプター
        // が複数フィールドをまとめる場合も、不一致のものは維持する。
        if (part_matches != 0U)
            replacements[i] = prefix + (material ? to_path_ : fTextRepl(value));
    }
    std::size_t listing_display_count = matches;
    if (listing) {
        listing_display_count = std::max<std::size_t>(1U, target.resultCount);
        // D版と同様、称号の行は参照回数を表示するが、ステータスの結果数は名前ごとに一つ加算する。
        matches = tab_ == Tab::Coupon ? 1U : listing_display_count;
        replacements = parts;
    }
    if (matches == 0U) return false;
    const auto display = [](const std::vector<String>& values) {
        String text;
        for (const auto& value : values) { if (!text.empty()) text += '\n'; text += value; }
        return text;
    };
    const auto after = display(replacements);
    auto target_ptr = std::make_shared<TextTarget>(target);
    if (!listing && replace_mode_ && replacements != parts && target_ptr->replaceable &&
        cautionReplace(before, after)) {
        // フォント文字は、完全なメッセージ本文から派生した表示。 こ
        // の変更境界で実際の本文を保存する。復号後の文字では記号の
        // 表記が失われ、 他のフォントとの衝突を元に戻せないため。
        const auto font_body = target_ptr->kind == "embedded-font"
            ? std::dynamic_pointer_cast<textholder::ISimpleTextHolder>(target_ptr->owner)
            : std::shared_ptr<textholder::ISimpleTextHolder>{};
        const auto body_before = font_body ? font_body->text() : String{};
        bool written;
        {
            OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
            written = target_ptr->writeParts ? target_ptr->writeParts(replacements)
                                            : target_ptr->assign(after);
        }
        if (!written && material) return false;
        const bool definition_name = target_ptr->kind == "variable-name" &&
            (std::dynamic_pointer_cast<flag::FlagDir>(target_ptr->owner) ||
             std::dynamic_pointer_cast<flag::Flag>(target_ptr->owner) ||
             std::dynamic_pointer_cast<flag::Step>(target_ptr->owner) ||
             std::dynamic_pointer_cast<flag::Variant>(target_ptr->owner));
        if (written) {
            if (font_body) {
                operations.push_back(std::make_shared<ValueUndo>(body_before, font_body->text(),
                    [font_body](String value) { font_body->text(value); }));
            } else if (target_ptr->writeParts) {
                operations.push_back(std::make_shared<undo::StrArrUndo>(parts, replacements,
                    [target_ptr](std::vector<String> values) { (void)target_ptr->writeParts(values); }));
            } else {
                operations.push_back(std::make_shared<ValueUndo>(before, after,
                    [target_ptr](String value) { (void)target_ptr->assign(std::move(value)); }));
            }
            if (target_ptr->finishWrite) deferred_text_writes_.push_back(target_ptr->finishWrite);
        } else if (!lib::editor_window_host_options().all_mode && definition_name) {
            // D版と同様、validName が最初の書き込みを拒
            // 否しても StrUndo を残す。 後の取り消し・
            // やり直しでも設定処理を再び呼び、 再試行で実際に定
            // 義パスが変わった場合だけ、参照の移行を再適用する。
            operations.push_back(std::make_shared<ValueUndo>(before, after,
                [target_ptr](String value) {
                    if (target_ptr->assign(std::move(value)) && target_ptr->finishWrite)
                        (void)target_ptr->finishWrite();
                }));
        }
    }
    ResultRow row;
    row.main = target.path.empty() ? before : target.path;
    row.parent = resultParent(target);
    row.description = target.description.empty() ? target.kind : target.description;
    row.before = before;
    row.after = after;
    row.cwxPath = target.path;
    row.count = tab_ == Tab::Coupon && listing ? listing_display_count : matches;
    row.image = target.image;
    row.textTarget = std::move(target_ptr);
    row.externalOpenTarget = target.externalOpenTarget;
    if (target.presentation) {
        row.main = target.presentation->main;
        row.parent = target.presentation->parent;
        row.description = target.presentation->description;
        row.image = target.presentation->image;
        row.parentImage = target.presentation->parentImage;
        row.descriptionImage = target.presentation->descriptionImage;
    }
    if (target.kind == "comment") {
        // D版の AddResultComment と同様、置換後の実
        // 際の本文を取得し、 所有者の説明用の記号の代わりに表示する。
        const auto life = lifetime_alive_;
        auto comment = row.textTarget->value();
        if (!life->load(std::memory_order_acquire)) return false;
        std::erase(comment, '\n');
        static const msgs::Msgs defaults;
        const auto* props = properties_.editor_commons
            ? properties_.editor_commons->prop() : nullptr;
        const auto& format = props ? props->requiredMsgs().commentText.value : defaults.commentText.value;
        row.main = utils::tryFormat(format, comment);
        const auto image = comm_.menuImage;
        row.image = image ? image(types::MenuID::Comment) : 0U;
        if (!life->load(std::memory_order_acquire)) return false;
    }
    if (target.nameIdKind) {
        auto id_target = std::make_shared<IdTarget>();
        id_target->kind = *target.nameIdKind;
        id_target->value = before;
        id_target->read = [text_target = row.textTarget] {
            return text_target ? text_target->value() : String{};
        };
        row.idTarget = std::move(id_target);
    }
    if (tab_ == Tab::Grep && grep_progress_) {
        const auto alive = lifetime_alive_;
        const auto generation = operation_generation_;
        count += matches;
        // D版の repl は、一致対象ごとに新しい dmy=0 を AddResult
        // へ渡し、その行の一時的な状態は1となる。 シナリオ終了時の
        // 状態には count を使う。D版のコメント通知はシナリオ
        // 全体の一覧と同じ共有 count を渡すが、 ここではコメ
        // ント行を余分に加算せず、既存の出現回数の数え方を維持する。
        refResultStatus((listing || target.kind == "comment") ? count : 1U, false);
        if (!alive->load(std::memory_order_acquire) ||
            operation_generation_ != generation) return false;
        // D版と同様、状態通知のコールバックでキャンセルされても、訪問済みのこの行は追加する。
        addResult(std::move(row));
    } else {
        addResult(std::move(row));
        count += matches;
    }
    return true;
}

bool ReplaceDialog::replaceIdTarget(IdTarget target, StringView from,
                                    StringView to, std::size_t& count,
                                    undo::UndoArray& operations) {
    if (!target.enabled || !targetSelected(target)) return false;
    const auto before = target.currentValue();
    // D版と同様、数値IDは型付きの値として渡し、文字列IDは完
    // 全一致で扱う。 そのため、数値の07と7は同じキーを表す。
    bool matched = before == from;
    if (!idKindIsString(target.kind)) {
        const auto current_id = getIDValue(before);
        const auto query_id = getIDValue(from);
        matched = current_id && query_id && *current_id == *query_id;
    }
    if (!matched) return false;
    const auto after = String(to);
    auto target_ptr = std::make_shared<IdTarget>(target);
    if (replace_mode_ && before != after && target_ptr->replaceable &&
        cautionReplace(before, after)) {
        std::shared_ptr<DeferredCouponArraysUndo> coupon_arrays;
        if (target.kind == IDKind::Coupon && after.empty()) {
            for (const auto& operation : operations) {
                const auto existing = std::dynamic_pointer_cast<DeferredCouponArraysUndo>(operation);
                if (existing && existing->owner() == target.owner.get()) { coupon_arrays = existing; break; }
            }
            if (!coupon_arrays) {
                coupon_arrays = couponArrayRecovery(target.owner);
                if (coupon_arrays) {
                    operations.push_back(coupon_arrays);
                    deferred_text_writes_.push_back([coupon_arrays] {
                        coupon_arrays->finish();
                        return undo::UndoArray{};
                    });
                }
            }
        }
        if (coupon_arrays) {
            OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
            (void)target_ptr->assign(after);
        } else {
        const auto fire_owner = target.kind == IDKind::KeyCode && after.empty()
            ? std::dynamic_pointer_cast<event::EventTree>(target.owner) : event::EventTreePtr{};
        if (fire_owner) {
            // D版と同様、FKeyCodes 配列全体を一度保存してから、一致して
            // 空になった発火条件をすべて除く。 配列の再構築で単一値の参照元は無効
            // になるため、個別の ValueUndo では識別情報を失ってしまう。
            auto old_codes = fire_owner->keyCodes();
            auto next_codes = old_codes;
            std::erase_if(next_codes, [&](const FKeyCode& code) {
                return code.keyCode.empty() || code.keyCode == from;
            });
            if (next_codes.size() != old_codes.size()) {
                auto operation = std::make_shared<FKeyCodesUndo>(old_codes, next_codes,
                    [fire_owner](const std::vector<FKeyCode>& codes) {
                        auto filtered = codes;
                        std::erase_if(filtered, [](const FKeyCode& code) { return code.keyCode.empty(); });
                        fire_owner->keyCodes(std::move(filtered));
                    });
                operations.push_back(operation);
                OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
                fire_owner->keyCodes(std::move(next_codes));
            }
        } else {
        // 設定処理が書き込み後に例外を出す場合があるため、呼び出し
        // 前に復旧用の領域を確保・保持する。 正常に戻った回数だけ
        // では、ID置換が取り消し可能に残すべき編集を表せない。
        auto operation = std::make_shared<ValueUndo>(
            before, after, [target_ptr](String value) {
                (void)target_ptr->assign(std::move(value));
            });
        operations.push_back(operation);
        const auto reconcile = [&] {
            auto retained = target_ptr->currentValue();
            if (retained == before) operations.pop_back();
            else operation->retainedValue(std::move(retained));
        };
        bool assigned = false;
        try {
            OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
            assigned = target_ptr->assign(after);
        } catch (...) {
            // 通常の Summary のID対象は、現在値を読み戻せる。 ホスト
            // の読み戻しも失敗した場合は元の例外を維持する。 内部の分からない書
            // き込み専用の提供処理には、before へ戻す保守的な復旧を残す。
            if (target_ptr->read || !target_ptr->write) {
                try { reconcile(); } catch (...) {}
            }
            throw;
        }
        if (target_ptr->read || !target_ptr->write) reconcile();
        else if (!assigned) operations.pop_back();
        }
    }
    }
    ResultRow row;
    row.main = target.path.empty() ? before : target.path;
    row.parent = target.parentPath;
    row.description = target.description.empty() ? kindText(target.kind)
                                                  : target.description;
    row.before = before;
    row.after = after;
    row.cwxPath = target.path;
    row.count = 1U;
    row.image = target.image;
    row.idResult = true;
    row.idTarget = std::move(target_ptr);
    addResult(std::move(row));
    ++count;
    return true;
}

void ReplaceDialog::applyResultSymbols(ResultRow& row) const {
    const auto symbols = source_.symbols;
    const auto owner = summary_;
    const auto alive = lifetime_alive_;
    const bool show_route = properties_.options.showRouteOfSearchResult;
    if (!symbols || row.externalOpenTarget) return;
    CWXPathPtr object;
    if (!row.cwxPath.empty() && owner) {
        try {
            object = owner->findCWXPath(row.cwxPath);
        } catch (...) {
            object.reset();
        }
    }
    if (!object && row.textTarget) object = row.textTarget->owner;
    if (!object && row.idTarget) object = row.idTarget->owner;
    if (!object) return;
    String text;
    ResourceId image{};
    try {
        if (!symbols(*object, true, text, image) || text.empty()) return;
    } catch (...) {
        return;
    }
    if (!alive->load(std::memory_order_acquire)) return;
    // D版のマッチ箇所は、getSymbols(path, desc) の結果とそのアイコンで表示する。
    row.main = std::move(text);
    if (image) row.image = image;

    // D版の所属は、最上位リソースからの経路、またはそのリソースだけを表示する。
    const auto parent = object->cwxParent().lock();
    if (!parent || dynamic_cast<const Summary*>(parent.get())) {
        row.parent.clear();
        row.parentImage = 0U;
        return;
    }
    std::vector<CWXPathPtr> route;
    const bool content = dynamic_cast<const event::Content*>(object.get()) != nullptr;
    if (!content && dynamic_cast<const event::Content*>(parent.get()))
        route.push_back(parent);
    for (auto current = parent; current && !dynamic_cast<const Summary*>(current.get());) {
        auto next = current->cwxParent().lock();
        if (const auto* c = dynamic_cast<const event::Content*>(current.get())) {
            if (c->type() != types::CType::Start) { current = std::move(next); continue; }
        } else if (dynamic_cast<const ::cwxeditor::cwx::flag::FlagDir*>(current.get()) &&
                   next && dynamic_cast<const ::cwxeditor::cwx::flag::FlagDir*>(next.get())) {
            current = std::move(next);
            continue;
        }
        route.push_back(current);
        current = std::move(next);
    }
    if (route.empty()) return;
    if (!show_route) route.erase(route.begin(), route.end() - 1);
    String joined;
    ResourceId first_image{};
    for (auto it = route.rbegin(); it != route.rend(); ++it) {
        String part;
        ResourceId part_image{};
        try {
            if (!symbols(**it, false, part, part_image)) {
                if (!alive->load(std::memory_order_acquire)) return;
                continue;
            }
        } catch (...) {
            if (!alive->load(std::memory_order_acquire)) return;
            continue;
        }
        if (!alive->load(std::memory_order_acquire)) return;
        if (joined.empty()) first_image = part_image;
        else joined += " > ";
        joined += part;
    }
    if (joined.empty()) return;
    row.parent = std::move(joined);
    row.parentImage = first_image;
}

String ReplaceDialog::resultParent(const TextTarget& target) const {
    if (!properties_.options.showRouteOfSearchResult || !target.owner)
        return target.parentPath;
    const auto parent = target.owner->cwxParent().lock();
    if (!parent) return target.parentPath;
    auto route = parent->cwxPath(true);
    for (std::size_t pos = 0; (pos = route.find('/', pos)) != String::npos;) {
        route.replace(pos, 1U, " > ");
        pos += 3U;
    }
    return route.empty() ? target.parentPath : route;
}

String ReplaceDialog::getIDName(IDKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    return index < properties_.messages.idKinds.size()
               ? properties_.messages.idKinds[index]
               : String{};
}

std::optional<std::uint64_t> ReplaceDialog::getIDValue(StringView value) const {
    // 数値IDは十進数字だけを許可する。stoull は符号や空白も受け付けるため、別に検査する。
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](const char ch) {
            return ch >= '0' && ch <= '9';
        }))
        return std::nullopt;
    try {
        std::size_t used = 0U;
        const auto result = std::stoull(String(value), &used, 10);
        return used == value.size() ? std::optional<std::uint64_t>(result)
                                    : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool ReplaceDialog::idKindIsString(IDKind kind) const noexcept {
    return stringKind(kind);
}

void ReplaceDialog::setupIDs(bool clear, bool from, bool to) {
    if (clear && id_kind_edit_) {
        std::vector<lib::ChoiceItem> items;
        items.reserve(properties_.messages.idKinds.size());
        for (std::size_t index = 0U; index < properties_.messages.idKinds.size();
             ++index)
            items.push_back(
                {properties_.messages.idKinds[index], std::to_string(index), 0U,
                 true});
        (void)lib::replace_combo_items(id_kind_edit_, std::move(items),
                                       static_cast<std::size_t>(id_kind_));
    }
    const bool available = summary_ != nullptr ||
                           static_cast<bool>(source_.idTargets) ||
                           static_cast<bool>(source_.idBatchTargets);
    if (!available) {
        if (from_id_edit_) {
            (void)lib::replace_combo_items(from_id_edit_, {}, {}, String{});
            from_id_edit_->set_enabled(false);
        }
        if (to_id_edit_) {
            (void)lib::replace_combo_items(to_id_edit_, {}, {}, String{});
            to_id_edit_->set_enabled(false);
        }
        if (from_id_value_edit_) from_id_value_edit_->set_enabled(false);
        if (to_id_value_edit_) to_id_value_edit_->set_enabled(false);
        from_id_.clear();
        to_id_.clear();
        return;
    }

    const bool strings = idKindIsString(id_kind_);
    const auto targets = [&] {
        if (!summary_) return idTargets();
        if (strings) {
            auto references = idTargets();
            // D版と同様、候補には未使用の標準名も含む allKeyCodes
            // を使う。 実際の検索・置換の参照対象は変えない。
            if (id_kind_ == IDKind::KeyCode &&
                !lib::editor_window_host_options().all_mode && properties_.editor_commons) {
                const auto prop = properties_.editor_commons->prop();
                if (prop) {
                    chooser::ChooserProperties options;
                    options.logicalSort = properties_.options.logicalSort;
                    options.useNamesAfterStandard = prop->var().useNamesAfterStandard.value;
                    const auto skin = properties_.editor_commons->skin();
                    options.standardKeyCodes = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                        prop->standardKeyCodes(
                            skin ? skin->type() : summary_->type()));
                    std::vector<IdTarget> choices;
                    for (const auto& key : chooser::allKeyCodes(comm_, summary_, options)) {
                        if (key.empty()) continue;
                        IdTarget choice;
                        choice.value = key;
                        choices.push_back(std::move(choice));
                    }
                    return choices;
                }
            }
            const auto root = summary_->flagDirRoot();
            const bool variables = id_kind_ == IDKind::Flag || id_kind_ == IDKind::Step || id_kind_ == IDKind::Variant;
            if (!variables || !root) return references;
            std::vector<IdTarget> choices;
            const auto append = [&](const auto& values) {
                ::cwxeditor::cwx::flag::sortedWithPath(values, properties_.options.logicalSort,
                    [&](const auto& variable) {
                        if (!variable || variable->path().empty()) return;
                        IdTarget choice;
                        choice.value = variable->path();
                        choices.push_back(std::move(choice));
                    });
            };
            if (id_kind_ == IDKind::Flag) append(root->allFlags());
            else if (id_kind_ == IDKind::Step) append(root->allSteps());
            else append(root->allVariants());
            // D版と同様、宣言済みのパスを先に並べ、その後に未宣言の参照キーを
            // 並べる。 予約された動的な値は、置換可能な変数の選択肢にしない。
            const auto prop = properties_.editor_commons ? properties_.editor_commons->prop() : nullptr;
            if (prop && id_kind_ != IDKind::Variant) {
                std::erase_if(references, [&](const auto& ref) {
                    const auto value = utils::caseFold(ref.currentValue());
                    return value == utils::caseFold(prop->sys().randomValue()) ||
                        (id_kind_ == IDKind::Step && value == utils::caseFold(prop->sys().selectedPlayerCardNumber()));
                });
            }
            std::stable_sort(references.begin(), references.end(), [&](const auto& a, const auto& b) {
                const auto av = a.currentValue(), bv = b.currentValue();
                return properties_.options.logicalSort ? utils::incmp(av, bv) < 0
                    : utils::caseFold(av) < utils::caseFold(bv);
            });
            choices.insert(choices.end(), references.begin(), references.end());
            return choices;
        }
        // D版と同様、数値の選択表には実際のシナリオリソースを
        // 並べる。 検索・置換に使う参照対象とは独立して扱う。
        std::vector<IdTarget> choices;
        const auto append = [&](const auto& resources) {
            for (const auto& resource : resources) {
                if (!resource) continue;
                IdTarget choice;
                choice.value = std::to_string(resource->id());
                choice.description = resource->name();
                choices.push_back(std::move(choice));
            }
        };
        switch (id_kind_) {
        case IDKind::Area: append(summary_->areas()); break;
        case IDKind::Battle: append(summary_->battles()); break;
        case IDKind::Package: append(summary_->packages()); break;
        case IDKind::Cast: append(summary_->casts()); break;
        case IDKind::Skill: append(summary_->skills()); break;
        case IDKind::Item: append(summary_->items()); break;
        case IDKind::Beast: append(summary_->beasts()); break;
        case IDKind::Info: append(summary_->infos()); break;
        default: break;
        }
        return choices;
    }();
    const auto setup = [&](const std::shared_ptr<lib::ComboBox>& combo,
                           const std::shared_ptr<lib::NumberEdit>& number,
                           String& current, const bool update,
                           const incsearch::IncSearch* filter) {
        if (!update || !combo || !number) return;
        const auto old = current;
        std::vector<lib::ChoiceItem> items;
        std::unordered_set<String> seen;
        if (!strings)
            items.push_back(
                {"[" + properties_.messages.directID + "]", {}, 0U, true});
        for (const auto& target : targets) {
            const auto value = target.currentValue();
            if (value.empty() || !seen.insert(value).second ||
                (filter && !filter->match(strings ? value : target.description)))
                continue;
            String label = value;
            if (!strings && !target.description.empty())
                label += "." + target.description;
            items.push_back({std::move(label), value, target.image, true});
        }

        if (strings) {
            std::optional<std::size_t> selected;
            for (std::size_t index = 0U; index < items.size(); ++index)
                if (items[index].value == old) {
                    selected = index;
                    break;
                }
            String edit_text = old;
            if (edit_text.empty() && !items.empty()) {
                selected = 0U;
                edit_text = items.front().value;
            }
            if (!lib::replace_combo_items(combo, std::move(items), selected,
                                          edit_text)) return;
            combo->set_enabled(true);
            combo->editable = true;
            current = std::move(edit_text);
            number->set_enabled(false);
            return;
        }

        std::size_t selected = 0U;
        if (!old.empty()) {
            for (std::size_t index = 1U; index < items.size(); ++index)
                if (items[index].value == old) {
                    selected = index;
                    break;
                }
        } else if (items.size() > 1U) {
            selected = 1U;
        }
        const auto edit_text = selected < items.size() ? items[selected].text : String{};
        const auto next_current = selected == 0U
                                      ? std::to_string(static_cast<std::int64_t>(
                                            std::llround(number->value)))
                                      : items[selected].value;
        if (!lib::replace_combo_items(combo, std::move(items), selected,
                                      edit_text)) return;
        combo->set_enabled(true);
        combo->editable = false;
        number->set_enabled(selected == 0U);
        current = next_current;
    };
    setup(from_id_edit_, from_id_value_edit_, from_id_, from, from_id_search_);
    setup(to_id_edit_, to_id_value_edit_, to_id_, to, to_id_search_);
}

void ReplaceDialog::tabChanged() {
    if (tabs_)
        (void)lib::select_tab_page(*tabs_, static_cast<std::size_t>(tab_), false);
    properties_.options.searchPlan = static_cast<std::size_t>(tab_);
    if (from_edit_ && text_from_host_ && grep_from_host_) {
        text_from_host_->remove(from_edit_);
        grep_from_host_->remove(from_edit_);
        (tab_ == Tab::Grep ? grep_from_host_ : text_from_host_)->add(from_edit_);
    }
    if (text_conditions_group_ && text_targets_group_ && text_page_ && grep_page_) {
        text_page_->remove(text_conditions_group_);
        text_page_->remove(text_targets_group_);
        grep_page_->remove(text_conditions_group_);
        grep_page_->remove(text_targets_group_);
        auto& destination = tab_ == Tab::Grep ? grep_page_ : text_page_;
        const auto first_row = tab_ == Tab::Grep ? 2U : 1U;
        const lib::Size persisted_width{
            std::max(0.0F, properties_.resultTableSize.width), 0.0F};
        text_conditions_group_->set_layout_item(
            item(first_row, 0U, 1U, 1U, 0.0F, persisted_width));
        text_targets_group_->set_layout_item(
            item(first_row + 1U, 0U, 1U, 1U, 0.0F, persisted_width));
        destination->add(text_conditions_group_);
        destination->add(text_targets_group_);
    }
    if (from_edit_) from_edit_->edit_text = from_text_;
    if (to_edit_) to_edit_->edit_text = to_text_;
    if (from_path_edit_) from_path_edit_->edit_text = from_path_;
    if (to_path_edit_) to_path_edit_->edit_text = to_path_;
    if (grep_directory_edit_)
        grep_directory_edit_->edit_text = properties_.options.grepDirectory;
    if (grep_subdirectory_check_)
        grep_subdirectory_check_->checked = properties_.options.grepSubDirectory;
    const bool range_enabled = tab_ != Tab::Unused && tab_ != Tab::Grep;
    if (range_view_) range_view_->set_enabled(range_enabled);
    const bool range_all_enabled =
        !(tab_ == Tab::Text && properties_.options.ignoreReturnCode) &&
        tab_ != Tab::Contents && tab_ != Tab::Coupon && tab_ != Tab::Unused &&
        tab_ != Tab::Error;
    if (range_all_check_) range_all_check_->set_enabled(range_all_enabled);
    if (!range_enabled && inc_search_) inc_search_->close();
    refreshToolbar();
}

void ReplaceDialog::updateFromHistory() {
    // D版の removeAll/add/setText と同様、IncSearch
    // は呼び出し元の入力値を保ったまま、表示する選択肢を変える。
    refreshHistoryCombos();
}

void ReplaceDialog::updateToHistory() {
    refreshHistoryCombos();
}

bool ReplaceDialog::renameCoupon(StringView old_value, StringView new_value,
                                 std::vector<String>& names,
                                 const std::function<void()>& notify) {
    bool changed_value = false;
    for (auto& name : names) {
        if (name == old_value) {
            name = String(new_value);
            changed_value = true;
        }
    }
    if (changed_value && notify) notify();
    return changed_value;
}

void ReplaceDialog::renameCouponResult(const std::size_t row, String value) {
    if (in_proc_ || tab_ != Tab::Coupon || row >= results_.size()) return;
    const auto target = results_[row].textTarget;
    if (!target || !target->nameIdKind) return;
    const String before = target->value();
    if (value == before) return;
    // D版と同様、設定処理、履歴通知、行の統合を通じて _inProc
    // を保持する。 インライン改名の反映中に、再入した再読み
    // 込み・モデル監視から新しい検索や履歴再適用を始めない。
    struct RenameScope {
        bool* processing;
        std::shared_ptr<std::atomic_bool> alive;
        std::function<void()> refresh;
        ~RenameScope() {
            if (!alive->load(std::memory_order_acquire)) return;
            *processing = false;
            try { refresh(); } catch (...) {}
        }
    } scope{&in_proc_, lifetime_alive_, [this] { refreshToolbar(); }};
    in_proc_ = true;

    // D版の couponEditEnd と同様、空の名前を拒否し、現在の
    // 選択範囲内の参照元だけを変更する。 その範囲は対象の書き込み処理が管
    // 理するため、ここでカウンター全体の改名に広げない。 D版の CouponUndo
    // と同様、改名・再適用の前に表示状態を保存する。 書き込み処
    // 理はモデルを戻せるが、統合された表示行までは分離できない。
    const auto priorResults = results_;
    const auto source_display_count = priorResults[row].count;
    bool assigned = false;
    if (!value.empty()) {
        OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
        assigned = target->assign(value);
    }
    if (!assigned) {
        if (result_table_ && row < result_table_->rows.size() &&
            !result_table_->rows[row].cells.empty()) {
            result_table_->rows[row].cells[0].text = before;
            result_table_->redraw();
        }
        return;
    }

    undo::UndoArray operations;
    operations.push_back(std::make_shared<ValueUndo>(
        before, value, [target](String next) { (void)target->assign(std::move(next)); }));
    undo_.add(std::make_shared<UndoRepl>(std::move(operations), true,
                                         [this, saved = priorResults]() mutable {
                                             refreshSource();
                                             std::vector<lib::TableRow> rows;
                                             rows.reserve(saved.size());
                                             for (const auto& item : saved) {
                                                 lib::TableRow display;
                                                 display.cells.push_back({item.main, item.cwxPath,
                                                     item.image, false, true});
                                                 display.cells.push_back({std::to_string(item.count),
                                                     {}, 0U, false, true});
                                                 rows.push_back(std::move(display));
                                             }
                                             if (result_table_ &&
                                                 !result_table_->replaceRows(std::move(rows), {}))
                                                 throw std::runtime_error("Coupon history row publication rejected");
                                             // 取り消し・やり直しの両方でスナップショットを交換し、
                                             // 範囲内の対象と、統合前後の正確な件数を保持する。
                                             results_.swap(saved);
                                         }, this, true,
                                         nameIdKind(*target->nameIdKind)));
    // 単独版はD版と同様、統合後も改名前の元行の表示件数を保持する。
    // ALL は再計算した統合後の件数を表示する。 インライン改
    // 名後は UseCounter から再構築し、重複行の統合と範
    // 囲内の参照回数を正確に反映する。 これは同じ編集の表示更新で
    // あり、新しい検索ではない。 D版の couponEditEnd
    // と同様、結果行を統合しても改名の取り消し履歴は保持する。
    replaceImpl(false, false);
    // D版と同様、編集した項目、または同じ種類の統合先を選択
    // する。 共通の一覧更新後に、その論理的な行を復元する。
    for (std::size_t index = 0; index < results_.size(); ++index) {
        const auto& refreshed = results_[index].textTarget;
        if (refreshed && refreshed->nameIdKind == target->nameIdKind &&
            refreshed->value() == value) {
            if (!lib::editor_window_host_options().all_mode) {
                results_[index].count = source_display_count;
                if (result_table_ && index < result_table_->rows.size() &&
                    result_table_->rows[index].cells.size() > 1U) {
                    result_table_->rows[index].cells[1].text =
                        std::to_string(source_display_count);
                    result_table_->redraw();
                }
            }
            if (result_table_) (void)lib::update_table_selection(*result_table_, index);
            break;
        }
    }
    // 外部のモデル監視先より先に、改名後の整合した表示を反
    // 映する。 監視先が例外を出しても、履歴には統合前後の
    // 両状態が残り、その後の取り消し・やり直しに使える。
    after(true, true, nameIdKind(*target->nameIdKind));
}

void ReplaceDialog::addResult(ResultRow row) {
    if ((tab_ == Tab::Text || tab_ == Tab::Grep) && row.textTarget &&
        !row.textTarget->resultGroup.empty()) {
        const auto found = std::find_if(results_.begin(), results_.end(), [&](const ResultRow& existing) {
            return existing.textTarget && existing.textTarget->resultGroup == row.textTarget->resultGroup;
        });
        if (found != results_.end()) {
            found->count += row.count;
            // 所有者の補助グループは一度だけ表示するが、出現回数はすべて数える。 コ
            // メント、イベントコメント、キーコード、ダイアログのグループは分ける。
            return;
        }
    }
    results_.reserve(results_.size() + 1U);
    if (!result_table_) {
        results_.push_back(std::move(row));
        return;
    }
    if (result_table_->columns.empty()) configureResultColumns();
    const auto result_tab = external_find_results_ ? Tab::Text : tab_;
    if (result_tab != Tab::Coupon) {
        const bool comment = row.textTarget && row.textTarget->kind == "comment";
        const auto comment_text = comment ? row.main : String{};
        const auto comment_image = row.image;
        applyResultSymbols(row);
        if (comment) {
            // 通常の所有者への経路は保ちつつ、D版と同様、 所有者の名前・アイ
            // コンではなく、コメント専用の一致テキストとアイコンを保持する。
            row.main = comment_text;
            row.image = comment_image;
        }
    }
    if (in_proc_ && !properties_.options.searchResultRealtime) {
        results_.push_back(std::move(row));
        return;
    }
    // リアルタイム表示の切り替えにより、この追加前に蓄積済みの行が表示される場合がある。
    publishPendingResults(result_table_, results_, result_tab);
    lib::TableRow table_row;
    table_row.cells.push_back(
        {row.main, row.cwxPath, row.image, false, true});
    if (result_tab == Tab::Coupon) {
        table_row.cells.push_back(
            {std::to_string(row.count), {}, 0U, false, true});
    } else {
        table_row.cells.push_back({row.parent, {}, row.parentImage, false, true});
        if (result_tab == Tab::Error || result_tab == Tab::Grep)
            table_row.cells.push_back(
                {row.description, {}, row.descriptionImage, false, true});
    }
    auto publication = lib::prepare_table_append(result_table_,
                                                  {std::move(table_row)}, false);
    if (!publication.commit()) return;
    results_.push_back(std::move(row));
    if (properties_.options.searchResultRealtime) {
        result_table_->ensureRowState();
        if (result_redraw_) result_table_->redraw();
    }
}

void ReplaceDialog::setResultStatus(std::size_t count) {
    const auto tab = tabText(tab_, properties_.messages);
    if (!replace_mode_ && grep_progress_) {
        const auto& progress = *grep_progress_;
        const auto& messages = properties_.messages;
        const auto& format = progress.phase == GrepPhase::Loading ? messages.searchResultGrep1
            : progress.phase == GrepPhase::Loaded ? messages.searchResultGrep2
                                                  : messages.searchResultGrep3;
        status_text_ = utils::tryFormat(format, utils::formatNum(progress.successfulScenarios),
            utils::formatNum(count), progress.phase == GrepPhase::Finished ? tab : progress.path);
    } else {
        status_text_ = replace_mode_
                           ? replaceToken(properties_.messages.replaceResult, std::to_string(count), tab)
                           : replaceToken(properties_.messages.searchResult, std::to_string(count), tab);
        if (count == 0U)
            status_text_ = replace_mode_ ? properties_.messages.replaceResultEmpty
                                         : properties_.messages.searchResultEmpty;
    }
    if (status_label_) status_label_->text = status_text_;
    // 外部の結果受け取り処理が、ダイアログとコールバックの保存領域を破棄する場合がある。
    const auto sink = comm_.setStatusLine;
    const auto window = window_;
    const auto text = status_text_;
    if (sink) sink(window.get(), text);
}

void ReplaceDialog::refResultStatus(std::size_t count, bool force) {
    if (!force && !properties_.options.searchResultRealtime &&
        properties_.options.searchResultRefreshCount != 0U &&
        count % properties_.options.searchResultRefreshCount != 0U)
        return;
    setResultStatus(count);
}

void ReplaceDialog::after(const bool changed_value, const bool id_changed,
                          const std::optional<namewindow::NameIDKind> name_id_kind) {
    struct PublicationScope {
        bool& value;
        bool previous;
        explicit PublicationScope(bool& state) : value(state), previous(state) { value = true; }
        ~PublicationScope() { value = previous; }
    } publication(publishing_changes_);
    if (changed_value) {
        if (summary_) summary_->changed();
        if (name_id_kind) {
            // 文字列IDの改名には、D版に対応する種類別のシ
            // グナルを使う。 新しい型付きコールバックを登録
            // していないホストだけに、従来の広い通知を残す。
            if (comm_.refNameID) comm_.refNameID(*name_id_kind);
            else {
                if (comm_.refUseCount) comm_.refUseCount();
                if (comm_.refIds) comm_.refIds();
            }
            if (comm_.replText) comm_.replText();
        } else {
            if (comm_.refUseCount) comm_.refUseCount();
            if (comm_.replText && !id_changed) comm_.replText();
            if (comm_.refIds && id_changed) comm_.refIds();
        }
        if (send_reload_props_) send_reload_props_();
    }
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void ReplaceDialog::completeUndoRedo(
    const bool redo, const bool id_changed,
    const std::optional<namewindow::NameIDKind> name_id_kind) {
    if (properties_.editor_commons && properties_.editor_commons->sync())
        properties_.editor_commons->sync()->sync();
    // D版の UndoRepl と同様、モデルの履歴適用後に現在のパス名・アイコン
    // を更新する。 結果行は検索データ、選択、スクロール位置を保持する。 記号の提
    // 供処理が新しい検索を通知する場合があるため、スナップショット上で準備する。
    const auto alive = lifetime_alive_;
    const auto owner = summary_;
    const auto generation = operation_generation_;
    const auto table = result_table_;
    const auto revision = table ? table->row_update_revision : 0U;
    const auto order = table ? table->row_order_revision : 0U;
    auto refreshed = results_;
    const auto row_count = refreshed.size();
    const auto current = [&] {
        return alive->load(std::memory_order_acquire) &&
            summary_ == owner && operation_generation_ == generation &&
            result_table_ == table && results_.size() == row_count &&
            (!table || (table->row_update_revision == revision &&
                        table->row_order_revision == order));
    };
    const auto result_tab = result_tab_;
    if (result_tab != Tab::Coupon) {
        for (auto& row : refreshed) {
            const bool comment = row.textTarget && row.textTarget->kind == "comment";
            const auto comment_text = comment ? row.main : String{};
            const auto comment_image = row.image;
            applyResultSymbols(row);
            if (!current()) return;
            if (comment) {
                row.main = comment_text;
                row.image = comment_image;
            }
        }
        if (!current()) return;
        if (table) {
            std::vector<customtable::TableCellDisplayUpdate> updates;
            for (std::size_t i = 0; i < refreshed.size() && i < table->rows.size(); ++i) {
                const auto& cells = table->rows[i].cells;
                if (!cells.empty()) updates.push_back({i, 0U, refreshed[i].main, refreshed[i].image});
                if (cells.size() > 1U) updates.push_back({i, 1U, refreshed[i].parent, refreshed[i].parentImage});
            }
            if (!table->updateCellDisplays(updates, order)) return;
        }
        results_ = std::move(refreshed);
        if (table) table->redraw();
        if (!current()) return;
    }
    // UndoRepl は undo()/redo() が in_undo_
    // を保持した状態でここへ来る。 逆変更後の通知を新しい外
    // 部編集として扱わず、残りのやり直し履歴を保持する。 D版
    // と同様、個々の一致数ではなく現在の結果行数を表示する。
    String format = redo ? "%1$s件をやり直しました" : "%1$s件を元に戻しました";
    if (properties_.editor_commons && properties_.editor_commons->prop()) {
        const auto& messages = properties_.editor_commons->prop()->requiredMsgs();
        format = redo ? messages.replaceRedo.value : messages.replaceUndo.value;
    }
    status_text_ = replaceToken(std::move(format), utils::formatNum(results_.size()));
    if (status_label_) status_label_->text = status_text_;
    if (comm_.setStatusLine) comm_.setStatusLine(window_.get(), status_text_);
    after(true, id_changed, name_id_kind);
}

void ReplaceDialog::restoreOperationFocus(const std::weak_ptr<lib::Widget>& prior,
                                          const std::uint64_t focus_revision,
                                          const bool replace_operation) {
    const auto gui = comm_.services.gui;
    if (!gui || !window_ || !window_->visible()) return;
    const auto before = prior.lock();
    // D版と同様、操作後にフォーカスを復元する。ただしコールバックがダイアログを
    // 閉じたり、 別の有効なコントロールへ意図的に移した場合は、それを優先する
    // 。 失効・切り離された対象を復活させず、新しいフォーカスを上書きしない。
    if (!before || gui->focus_revision() != focus_revision) return;
    const auto target = replace_operation
        ? std::static_pointer_cast<lib::Widget>(replace_button_)
        : before;
    if (!target || !target->visible() || !target->enabled()) return;
    if (const auto owner = gui->window_owner(target);
        owner && (!owner->visible() || !lib::widget_in_input_scope(owner, target)))
        return;
    gui->focus(target);
}

void ReplaceDialog::replaceImpl(bool replace_mode, bool retire_history) {
    const auto operation_alive = lifetime_alive_;
    const auto operation_generation = operation_generation_;
    auto* const editor_commons = properties_.editor_commons;
    const auto commons_life = editor_commons ? editor_commons->lifetimeToken() : std::weak_ptr<const int>{};
    const bool previousIgnoreMod = ignoreMod;
    ignoreMod = true;
    struct RestoreIgnoreMod {
        bool* flag; bool old; std::shared_ptr<std::atomic_bool> alive;
        ~RestoreIgnoreMod() { if (alive->load(std::memory_order_acquire)) *flag = old; }
    } restoreIgnoreMod{&ignoreMod, previousIgnoreMod, lifetime_alive_};
    const bool caller_processing = in_proc_;
    if (retire_history) {
        if (in_proc_ || (replace_mode ? !canReplace() : !canFind())) return;
    } else {
        // 称号の表示専用の非公開更新。 新しい検索時の無効化を回避
        // したり、変更を伴う置換に入ったりする公開経路にはしない。
        if (!in_proc_ || tab_ != Tab::Coupon || replace_mode) return;
    }
    // D版と同様、検索範囲のスナップショットを取る前に、範囲の
    // インクリメンタル絞り込みを閉じる。 開いたままだと、以前
    // の表示用フィルターが次の検索・置換範囲を黙って変えたり、
    // 行の再構築中に範囲ツリーの一部を隠したりしてしまう。
    if (inc_search_) inc_search_->close();
    // fTextCount/fTextRepl はコンパイル済みのテ
    // キストの組を使う。 パス置換も一時的に独自の組を作るが、D版
    // と同様、操作欄はテキスト・Grepから独立している。 一時状
    // 態で、タブを戻した後に表示するテキストの履歴を上書きしない。
    const bool path_tab = tab_ == Tab::Path;
    const String saved_from_text = from_text_;
    const String saved_to_text = to_text_;
    const auto restore_text_pair = [this, path_tab, &saved_from_text,
                                    &saved_to_text] {
        if (!path_tab) return;
        from_text_ = saved_from_text;
        to_text_ = saved_to_text;
    };
    replace_mode_ = replace_mode;
    caution_confirmed_ = false;
    if (replace_mode_) {
        String from;
        String to;
        if (tab_ == Tab::Text) {
            from = from_text_;
            to = to_text_;
        } else if (tab_ == Tab::Path) {
            from = from_path_;
            to = to_path_;
        } else if (tab_ == Tab::Id) {
            // D版の getIDName と同様、名前のあるコンボ選択と任
            // 意の数値IDを区別する。 説明のない生の数値を表示しない。
            String valueFormat = "「%1$s」";
            String idFormat = "ID:%1$sの%2$s";
            String emptyText = "空文字列";
            if (properties_.editor_commons && properties_.editor_commons->prop()) {
                const auto& messages = properties_.editor_commons->prop()->requiredMsgs();
                valueFormat = messages.replaceValue.value;
                idFormat = messages.idValue.value;
                emptyText = messages.emptyText.value;
            }
            const auto name = [&](const auto& combo, const String& value) -> String {
                if (idKindIsString(id_kind_))
                    return value.empty() ? emptyText : replaceToken(valueFormat, value);
                if (combo && combo->selected_index && *combo->selected_index > 0U &&
                    *combo->selected_index < combo->items.size())
                    return replaceToken(valueFormat, combo->items[*combo->selected_index].text);
                return replaceToken(idFormat, value, getIDName(id_kind_));
            };
            from = name(from_id_edit_, from_id_);
            to = name(to_id_edit_, to_id_);
        }
        if (!cautionReplace(from, to)) {
            replace_mode_ = false;
            return;
        }
        caution_confirmed_ = true;
    }
    // パス正規化より先に、この互換性の判定を単独版だけに限
    // 定する。 ALL の既存の動作と処理負荷は維持する。
    const bool standalone_notifications =
        !lib::editor_window_host_options().all_mode;
    // D版の replaceIDImpl と同様、同じIDへの置換は検索操作に変える。
    if (replace_mode_ && tab_ == Tab::Id) {
        bool same_id = false;
        if (idKindIsString(id_kind_)) {
            same_id = from_id_ == to_id_;
        } else {
            const auto from = getIDValue(from_id_);
            const auto to = getIDValue(to_id_);
            same_id = from && to && *from == *to;
        }
        if (same_id) replace_mode_ = false;
    }
    bool d_replace_mode_for_noop = false;
    bool d_scaled_path_alias_disables_replace = false;
    if (standalone_notifications && replace_mode_) {
        if (tab_ == Tab::Text) {
            d_replace_mode_for_noop = true;
        } else if (tab_ == Tab::Id) {
            // D版と同様、数値IDは整数で、文字列IDは表記どおりに比較する。
            if (idKindIsString(id_kind_)) {
                d_replace_mode_for_noop = from_id_ != to_id_;
            } else {
                const auto from = getIDValue(from_id_);
                const auto to = getIDValue(to_id_);
                d_replace_mode_for_noop = from && to && *from != *to;
            }
        } else if (tab_ == Tab::Path) {
            auto from = from_path_;
            if (summary_ && summary_->loadScaledImage()) {
                const auto unscaled = utils::noScaledPath(dutils::filesystemPath(from));
                if (!unscaled.empty()) from = dutils::pathString(unscaled);
            }
            const auto from_id = usecounter::toPathId(from);
            const auto to_id = usecounter::toPathId(to_path_);
            d_replace_mode_for_noop = from_id != to_id;
            d_scaled_path_alias_disables_replace = from_id == to_id;
        }
    }
    if (d_scaled_path_alias_disables_replace) replace_mode_ = false;
    in_proc_ = true;
    // 新しい検索・置換は、新しい履歴を持つ。 古い履歴の消去と操作
    // 欄の更新中も再入防止を維持する。 reset() は処理中に
    // も使われ、その場合は復旧用の取り消し履歴を残す必要がある。
    if (retire_history) undo_.reset();
    cancel_ = false;
    bool history_changed = false;
    if (tab_ == Tab::Text || tab_ == Tab::Grep) {
        history_changed |=
            addHistory(properties_.options.searchHistories, from_text_);
        if (replace_mode_ && tab_ == Tab::Text)
            history_changed |=
                addHistory(properties_.options.replaceHistories, to_text_);
    }
    if (tab_ == Tab::Grep)
        history_changed |= addHistory(
            properties_.options.grepDirectoryHistories,
            properties_.options.grepDirectory);
    if (history_changed) {
        try {
            refreshHistoryCombos();
            if (send_reload_props_) send_reload_props_();
        } catch (...) {
            // 受け付けた検索は以前の履歴を無効化したが、まだワー
            // カーは開始していない。 履歴の監視先が失敗しても、
            // 早期の再入防止が処理中のまま残らないようにする。
            in_proc_ = caller_processing;
            caution_confirmed_ = false;
            restore_text_pair();
            throw;
        }
    }
    // D版の ReplaceDialog と同様、検索処理の終了まで setWaitCursors(_win) を適用する。
    dutils::WaitCursorScope wait_cursors(window_);
    refreshToolbar();
    reset(true);
    if (tab_ == Tab::Grep) grep_progress_ = GrepProgress{};
    configureResultColumns();
    if (tab_ == Tab::Text || tab_ == Tab::Grep)
        (void)initText(from_text_, to_text_);
    else if (tab_ == Tab::Path) text_initialised_ = !from_path_.empty();
    else if (tab_ == Tab::Id) exitText();
    if (!operation_alive->load(std::memory_order_acquire)) return;
    if ((tab_ == Tab::Text || tab_ == Tab::Path || tab_ == Tab::Grep) &&
        !text_initialised_) {
        in_proc_ = caller_processing;
        caution_confirmed_ = false;
        restore_text_pair();
        setResultStatus(0U);
        if (!operation_alive->load(std::memory_order_acquire)) return;
        refreshToolbar();
        return;
    }
    refreshSource();
    undo::UndoArray operations;
    deferred_text_writes_.clear();
    std::size_t count = 0U;
    std::size_t processed = 0U;
    bool changed_value = false;
    bool id_changed = false;
    const auto name_id_kind = tab_ == Tab::Id ? nameIdKind(id_kind_) : std::nullopt;
    const auto pump_cancel = [this, &processed] {
        // 共通実装は、この走査をUIスレッド上で行う。一致数と
        // 独立してイベントを処理し、 一致がなくてもキャンセル
        // を受け付ける。 最初に一度、その後は16対象ごとに処
        // 理し、対象ごとの再描画を避けつつ操作への応答を保つ。
        ++processed;
        if (comm_.services.gui && (processed == 1U || processed % 16U == 0U))
            (void)comm_.services.gui->pump_events(false);
    };
    try {
        const auto batch_provider = batchProviderForTab();
        if (batch_provider) {
            (void)runBatchProvider(batch_provider, count, operations);
            if (!operation_alive->load(std::memory_order_acquire)) return;
            if (operation_generation_ != operation_generation) {
                in_proc_ = caller_processing;
                caution_confirmed_ = false;
                restore_text_pair();
                refreshToolbar();
                return;
            }
            changed_value = changed_value || (replace_mode_ && !operations.empty());
        } else if (tab_ == Tab::Id &&
                   (source_.idBatchTargets || (summary_ && !source_.idTargets))) {
            const auto provider = source_.idBatchTargets
                ? source_.idBatchTargets
                : ReplaceSource::BatchProvider{[this](ReplaceQueryContext& context) {
                    ReplaceProviderResult result;
                    const bool completed=enumerateSummaryIds(id_kind_, [&](IdTarget target) {
                        if (context.checkpoint && !context.checkpoint()) return false;
                        ++result.scanned;
                        return !context.publishIdBatch ||
                            context.publishIdBatch({std::move(target)});
                    });
                    result.cancelled=!completed;
                    result.completed=completed;
                    return result;
                }};
            (void)runIdBatchProvider(provider, count, operations, id_changed);
            changed_value = changed_value || !operations.empty();
        } else if (tab_ == Tab::Id) {
            const auto targets = idTargets();
            for (auto target : targets) {
                if (cancel_) break;
                const auto operation_count = operations.size();
                if (replaceIdTarget(std::move(target), from_id_, to_id_, count,
                                    operations)) {
                    const bool replaced = replace_mode_ && operations.size() != operation_count;
                    id_changed = id_changed || replaced;
                    changed_value = changed_value || replaced;
                }
                if (count != 0U) refResultStatus(count, false);
                pump_cancel();
            }
        } else {
            const auto targets = targetsForTab();
            for (auto target : targets) {
                if (cancel_) break;
                const auto operation_count = operations.size();
                if (replaceTextTarget(std::move(target), count, operations))
                    changed_value = changed_value ||
                                     (replace_mode_ && operations.size() != operation_count);
                if (count != 0U) refResultStatus(count, false);
                pump_cancel();
            }
        }
    } catch (...) {
        if (!operation_alive->load(std::memory_order_acquire)) return;
        if (operation_generation_ != operation_generation) {
            in_proc_ = caller_processing;
            caution_confirmed_ = false;
            restore_text_pair();
            refreshToolbar();
            return;
        }
        // D版のIDワーカーと同様、記録を出して終了し、完了済みの編集は戻さ
        // ない。 下の最終反映で、その取り消し履歴と通知を引き続き記録する。
        if (tab_ == Tab::Id) {
            try {
                (void)utils::printStackTrace();
                try { throw; }
                catch (const std::exception& error) {
                    utils::debugln("ID replacement stopped: ", error.what());
                } catch (...) {
                    utils::debugln("ID replacement stopped: unknown exception");
                }
            } catch (...) {} // 診断処理のために復旧用の履歴を捨てない。
            changed_value = replace_mode_ && !operations.empty();
            id_changed = changed_value;
        } else if (replace_mode_ && tab_ == Tab::Text && !source_.textTargets && !source_.textBatchTargets) {
            // D版と同様、後の通常ファイルの読み込みが失敗しても、完了済みのテキ
            // スト編集を記録する。 行と正確な取り消し履歴を維持し、補助ファイル
            // の列挙が止まっただけで以前のモデル・ファイルの書き込みを戻さない。
            changed_value = !operations.empty();
        } else if (replace_mode_) {
            deferred_text_writes_.clear();
            bool rollback_failed = false;
            for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
                if (!*it) continue;
                try {
                    (*it)->undo();
                } catch (...) {
                    // 一つの逆操作の失敗で、残りの
                    // 適用済み置換の復元を妨げない
                    // 。 一つでも失敗したら、元の
                    // 状態に戻ったとは言えないため
                    // 、下でモデル変更を通知する。
                    rollback_failed = true;
                }
            }
            operations.clear();
            changed_value = rollback_failed;
            if (!rollback_failed) id_changed = false;
            // 置換結果の行には、書き込み後の仮の値が入る
            // 。 D版と同様、置換処理を破棄した後に選
            // 択可能なまま残さず、 提供処理が例外前に
            // 複数のまとまりを通知していても消去する。
            reset(true);
            count = 0U;
            refreshSource();
        }
    }
    try {
        for (auto& finish : deferred_text_writes_) {
            OwnModelWriteScope own_write(own_model_write_, lifetime_alive_);
            auto writes = finish();
            operations.insert(operations.end(), writes.begin(), writes.end());
        }
    } catch (...) {
        // テキスト処理の境界と同様、記録済みの編集は保持する。
        // 処理中の状態を解除し、その取り消しを利用可能にする。
        changed_value = !operations.empty();
    }
    deferred_text_writes_.clear();
    if (replace_mode_ && !operations.empty()) {
        undo_.add(std::make_shared<UndoRepl>(std::move(operations), true,
                                              [this] { refreshSource(); }, this,
                                              id_changed, name_id_kind));
    }
    in_proc_ = caller_processing;
    caution_confirmed_ = false;
    if (result_table_) {
        publishPendingResults(result_table_, results_, external_find_results_ ? Tab::Text : tab_);
        result_table_->ensureRowState();
        if (result_redraw_) result_table_->redraw();
    }
    if (grep_progress_) { grep_progress_->phase = GrepPhase::Finished; grep_progress_->path.clear(); }
    setResultStatus(count);
    if (!operation_alive->load(std::memory_order_acquire) ||
        operation_generation_ != operation_generation) return;
    if (replace_mode_ && path_tab && editor_commons && !commons_life.expired()) {
        try {
            // D版と同様、パス置換後は必ずJPY1/JPDCの解析済みファイル
            // の更新を通知する。 ALL もモデルと保存経路は共通なので、コ
            // ンパクトな表示を理由に更新や FileSync を抑止しない。
            const bool jpy1_changed = editor_commons->updateJpy1Files(true);
            if (!operation_alive->load(std::memory_order_acquire) ||
                operation_generation_ != operation_generation) return;
            if (jpy1_changed && !commons_life.expired()) {
                auto* const sync = editor_commons->sync();
                if (!operation_alive->load(std::memory_order_acquire) ||
                    operation_generation_ != operation_generation) return;
                if (sync) sync->sync();
                if (!operation_alive->load(std::memory_order_acquire) ||
                    operation_generation_ != operation_generation) return;
            }
        } catch (...) {
            // ファイル同期が失敗しても、独立したテキスト・Grep
            // の検索語に、 一時的なパスの検索語を残さない。
            if (operation_alive->load(std::memory_order_acquire) &&
                operation_generation_ == operation_generation) restore_text_pair();
            throw;
        }
    }
    if (standalone_notifications && d_replace_mode_for_noop && count > 0U &&
        !changed_value && summary_) {
        const auto notification_summary = summary_;
        const auto notification_alive = operation_alive;
        const auto notification_generation = operation_generation;
        try {
            const auto ref_use_count = comm_.refUseCount;
            const bool previous_publishing = publishing_changes_;
            publishing_changes_ = true;
            struct PublishingScope {
                bool* value;
                std::shared_ptr<std::atomic_bool> alive;
                bool previous;
                ~PublishingScope() {
                    if (alive->load(std::memory_order_acquire)) *value = previous;
                }
            } publication{&publishing_changes_, notification_alive, previous_publishing};
            notification_summary->changed();
            if (!notification_alive->load(std::memory_order_acquire) ||
                operation_generation_ != notification_generation) return;
            if (summary_ != notification_summary) {
                restore_text_pair();
                return;
            }
            if (ref_use_count) ref_use_count();
            if (!notification_alive->load(std::memory_order_acquire) ||
                operation_generation_ != notification_generation) return;
            if (summary_ != notification_summary) {
                restore_text_pair();
                return;
            }
        } catch (...) {
            // 通知がホストへ再入する場合がある。 この操作
            // がまだ有効なダイアログを所有しているなら、
            // パス置換の一時的なテキストの組を残さない。
            if (notification_alive->load(std::memory_order_acquire) &&
                operation_generation_ == notification_generation)
                restore_text_pair();
            throw;
        }
    }
    // D版のID処理と同様、書き込みをしない同値置換でも、一致が
    // あれば replID と選択中の文字列IDの種類別シグナル
    // を通知する。 上のテキストの無書き込み時の Summary.changed/refUseCount
    // 通知とは意図的に分ける。 D版は同値IDで
    // は結果の状態通知前に _replMode を
    // 解除するため、汎用のモデル通知は出さない。
    if (standalone_notifications && tab_ == Tab::Id && count > 0U &&
        !changed_value && summary_) {
        const auto notification_summary = summary_;
        const auto notification_alive = operation_alive;
        const auto notification_generation = operation_generation;
        const auto ref_ids = comm_.refIds;
        const auto ref_name_id = comm_.refNameID;
        const bool previous_publishing = publishing_changes_;
        publishing_changes_ = true;
        struct PublishingScope {
            bool* value;
            std::shared_ptr<std::atomic_bool> alive;
            bool previous;
            ~PublishingScope() {
                if (alive->load(std::memory_order_acquire)) *value = previous;
            }
        } publication{&publishing_changes_, notification_alive, previous_publishing};
        // MainWindow は refIds をD版の汎用 replID 更新
        // に対応させる。 D版と同様、文字列IDの種類別シグナルより先に通知する。
        if (ref_ids) ref_ids();
        if (!notification_alive->load(std::memory_order_acquire) ||
            operation_generation_ != notification_generation) return;
        if (summary_ != notification_summary) return;
        if (name_id_kind) {
            if (ref_name_id) ref_name_id(*name_id_kind);
            if (!notification_alive->load(std::memory_order_acquire) ||
                operation_generation_ != notification_generation) return;
            if (summary_ != notification_summary) return;
        }
    }
    after(changed_value, id_changed, name_id_kind);
    restore_text_pair();
    refreshToolbar();
}

std::vector<TextTarget> ReplaceDialog::targetsForTab() const {
    switch (tab_) {
    case Tab::Text:
        if (source_.textTargets) return source_.textTargets();
        return summaryTextTargets();
    case Tab::Path:
        if (source_.pathTargets) return source_.pathTargets();
        return summaryPathTargets();
    case Tab::Contents: {
        if (source_.contentsTargets) return source_.contentsTargets();
        // D版のコンテント検索と同様、テキストの検索語とは独立して選択した CType
        // の項目を一覧にする。 移動に使えるよう、結果には実際のパスを保持する。
        std::vector<TextTarget> targets;
        static const msgs::Msgs messages;
        std::size_t ignored = 0;
        for (const auto& root : rangeTree(summary_)) {
            searchAll(root, root, ignored, [&](CWXPathPtr, CWXPathPtr owner, std::size_t&, StringView cwxPath) {
            const auto content = std::dynamic_pointer_cast<event::Content>(owner);
            if (!content) return;
            TextTarget target;
            target.owner = owner;
            target.path = String(cwxPath);
            target.parentPath = path::cpparent(target.path);
            target.kind = "content";
            target.contentType = content->type();
            target.description = messages.contentName(content->type());
            target.read = [content] { return content->name(); };
            target.replaceable = false;
            const auto index = static_cast<std::size_t>(content->type());
            if (index < properties_.contentImages.size())
                target.image = properties_.contentImages[index];
            targets.push_back(std::move(target));
            });
        }
        return targets;
    }
    case Tab::Coupon:
        if (source_.couponTargets) return source_.couponTargets();
        return summaryCouponTargets();
    case Tab::Unused:
        if (source_.unusedTargets) return source_.unusedTargets();
        return {};
    case Tab::Error:
        if (source_.errorTargets) return source_.errorTargets();
        return {};
    case Tab::Grep:
        if (source_.grepTargets)
            return source_.grepTargets(properties_.options.grepDirectory,
                                       properties_.options.grepSubDirectory);
        // ディレクトリのGrepには、ホストのシナリオローダーが必要。 現在開いて
        // いる Summary に代えると、誤ったシナリオを黙って検索してしまう。
        return {};
    case Tab::Id:
        return {};
    }
    return {};
}

ReplaceSource::BatchProvider ReplaceDialog::batchProviderForTab() const {
    switch (tab_) {
    case Tab::Text:
        if (source_.textBatchTargets) return source_.textBatchTargets;
        // 明示的に渡されたベクターの提供処理を正とする。 ここで
        // Summary の一括経路へ切り替えると、ホストのテ
        // キストアダプターと、 それが行うフォーカス・モデルの
        // 遷移を無視し、下のパスタブの同等の規則とも食い違う。
        if (source_.textTargets || !summary_) return {};
        return [this](ReplaceQueryContext& context) {
            ReplaceProviderResult result;
            const auto publish = [&](TextTarget target) {
                if (context.checkpoint && !context.checkpoint()) return false;
                ++result.scanned;
                return !context.publishBatch || context.publishBatch({std::move(target)});
            };
            bool completed = enumerateSummaryText(publish);
            if (completed && (properties_.options.jptx || properties_.options.textFile)) {
                const system::System fallback_system;
                const props::CProps fallback("", fallback_system);
                const auto* host = properties_.editor_commons ? properties_.editor_commons->prop() : nullptr;
                auto* borrowed = properties_.editor_commons ? properties_.editor_commons->sync() : nullptr;
                // Commons は、モードレスの置換ダイアログとその履歴より長く存続する。 このア
                // ダプターはホストの FileSync を借用するだけで、ホスト自体は所有しない。
                lib::Owner<filesync::FileSync> sync;
                if (borrowed) sync = {borrowed, [](filesync::FileSync*) {}};
                completed = replace_text::enumerateFiles(summary_, properties_.options,
                    properties_.plainTextFileExtensions, host ? host->parent() : fallback,
                    sync, publish);
                if (replace_mode_ && sync) sync->sync();
            }
            result.cancelled = !completed;
            result.completed = completed;
            return result;
        };
    case Tab::Path:
        if (source_.pathBatchTargets) return source_.pathBatchTargets;
        // 従来のベクターの提供処理も、指定されていれば引き続き正として使う。
        if (source_.pathTargets || !summary_) return {};
        return [this](ReplaceQueryContext& context) {
            ReplaceProviderResult result;
            const bool completed = enumerateSummaryPath([&](TextTarget target) {
                if (context.checkpoint && !context.checkpoint()) return false;
                ++result.scanned;
                return !context.publishBatch || context.publishBatch({std::move(target)});
            });
            result.cancelled=!completed;
            result.completed=completed;
            return result;
        };
    case Tab::Unused:
        return source_.unusedBatchTargets;
    case Tab::Error:
        return source_.errorBatchTargets;
    case Tab::Grep:
        return source_.grepBatchTargets;
    default:
        return {};
    }
}

ReplaceProviderResult ReplaceDialog::runBatchProvider(
    const ReplaceSource::BatchProvider& provider, std::size_t& count,
    undo::UndoArray& operations) {
    ReplaceProviderResult result;
    if (!provider) return result;

    ReplaceQueryContext context;
    context.summary = summary_;
    context.query = tab_ == Tab::Path ? from_path_
                  : tab_ == Tab::Id ? from_id_
                                     : from_text_;
    context.options = properties_.options;
    context.selectedRange = searchRange();
    context.errorVersion = properties_.options.searchErrorTargetVersion;
    context.grepDirectory = properties_.options.grepDirectory;
    // D版と同様、入力されたディレクトリをアプリの基準パスに対して解決する。
    if (tab_ == Tab::Grep && properties_.editor_commons &&
        properties_.editor_commons->prop()) {
        context.grepDirectory =
            properties_.editor_commons->prop()->toAppAbs(context.grepDirectory);
    }
    context.grepSubDirectory = properties_.options.grepSubDirectory;
    context.plainTextFileExtensions = properties_.plainTextFileExtensions;
    const auto alive = lifetime_alive_;
    const auto active = std::make_shared<bool>(true);
    struct RetireCallbacks {
        std::shared_ptr<bool> active;
        ~RetireCallbacks() { *active = false; }
    } retire{active};
    const auto generation = operation_generation_;
    const auto valid = [this, alive, active, generation] {
        return *active && alive->load(std::memory_order_acquire) &&
               operation_generation_ == generation;
    };
    context.checkpoint = [this, valid] {
        if (!valid()) return false;
        const auto gui = comm_.services.gui;
        if (gui) (void)gui->pump_events(false);
        return valid() && !cancel_;
    };
    if (tab_ == Tab::Grep) {
        context.grepProgress = [this, valid, &count](const GrepProgress& progress) {
            if (!valid()) return false;
            grep_progress_ = progress;
            if (progress.refresh) setResultStatus(count);
            return valid() && !cancel_;
        };
    }
    context.publishBatch = [this, valid, &count, &operations](std::vector<TextTarget> batch) {
        if (!valid() || cancel_) return false;
        for (auto& target : batch) {
            if (!valid() || cancel_) return false;
            (void)replaceTextTarget(std::move(target), count, operations);
            if (!valid()) return false;
        }
        if (tab_ != Tab::Grep && count != 0U) refResultStatus(count, false);
        return valid() && !cancel_;
    };
    result = provider(context);
    if (!valid()) { result.cancelled = true; result.completed = false; return result; }
    result.cancelled = result.cancelled || cancel_;
    if (result.cancelled) result.completed = false;
    return result;
}

ReplaceProviderResult ReplaceDialog::runIdBatchProvider(
    const ReplaceSource::BatchProvider& provider, std::size_t& count,
    undo::UndoArray& operations, bool& id_changed) {
    ReplaceProviderResult result;
    if (!provider) return result;

    ReplaceQueryContext context;
    context.summary = summary_;
    context.query = from_id_;
    context.options = properties_.options;
    context.selectedRange = searchRange();
    context.checkpoint = [this] {
        if (comm_.services.gui) (void)comm_.services.gui->pump_events(false);
        return !cancel_;
    };
    context.publishIdBatch = [this, &count, &operations, &id_changed](
                                 std::vector<IdTarget> batch) {
        if (cancel_) return false;
        for (auto& target : batch) {
            if (cancel_) return false;
            const auto operation_count = operations.size();
            if (replaceIdTarget(std::move(target), from_id_, to_id_, count, operations)) {
                const bool replaced = replace_mode_ && operations.size() != operation_count;
                id_changed = id_changed || replaced;
            }
        }
        if (count != 0U) refResultStatus(count, false);
        return !cancel_;
    };
    result = provider(context);
    result.cancelled = result.cancelled || cancel_;
    if (result.cancelled) result.completed = false;
    return result;
}

std::vector<IdTarget> ReplaceDialog::idTargets() const {
    if (source_.idTargets) return source_.idTargets(id_kind_);
    return summaryIdTargets(id_kind_);
}

bool ReplaceDialog::enumerateSummaryText(
    const std::function<bool(TextTarget)>& emit) const {
    if (!summary_) return true;
    const auto root = summary_->flagDirRoot();
    const bool definitions_selected = root && dec(root, range_table_);
    const auto* props = properties_.editor_commons ? properties_.editor_commons->prop() : nullptr;
    return replace_text::enumerate(summary_, properties_.options,
                                  definitions_selected, emit,
                                  props ? &props->parent().sys() : nullptr);
}

std::vector<TextTarget> ReplaceDialog::summaryTextTargets() const {
    std::vector<TextTarget> result;
    (void)enumerateSummaryText([&result](TextTarget target) {
        result.push_back(std::move(target));
        return true;
    });
    return result;
}

std::vector<TextTarget> ReplaceDialog::summaryPathTargets() const {
    std::vector<TextTarget> result;
    (void)enumerateSummaryPath([&result](TextTarget target) {
        result.push_back(std::move(target));
        return true;
    });
    return result;
}

bool ReplaceDialog::enumerateSummaryPath(const std::function<bool(TextTarget)>& emit) const {
    if (!emit) return true;
    const auto summ = summary_;
    if (!summ) return true;
    const auto counter = summ->useCounter();
    if (!counter) return true;
    // D版の replacePathImpl と同様、シナリオの表紙だけでなく登
    // 録済みの PathUser を列挙する。 設定処理がカウンターを変える前
    // に複製し、リソース所有者内の参照元のために Summary を保持する。
    std::vector<lib::Owner<usecounter::PathUser>> users;
    std::unordered_set<const usecounter::PathUser*> seen;
    for (const auto& key : counter->keys<usecounter::PathId>()) {
        for (const auto& user : counter->values(key)) {
            if (user && seen.insert(user.get()).second) users.push_back(user);
        }
    }
    std::stable_sort(users.begin(), users.end(), [](const auto& left, const auto& right) {
        const auto left_owner = left->owner();
        const auto right_owner = right->owner();
        const auto left_path = left_owner ? left_owner->cwxPath(true) : String{};
        const auto right_path = right_owner ? right_owner->cwxPath(true) : String{};
        if (left_path != right_path) return left_path < right_path;
        return left->path() < right->path();
    });
    for (const auto& user : users) {
        const auto owner = user->owner();
        if (!owner) continue;
        TextTarget target;
        target.path = owner->cwxPath(true);
        target.kind = "file";
        target.description = "Material path";
        target.owner = owner;
        if (properties_.rangeImage) target.image = properties_.rangeImage(owner.get());
        target.read = [summ, user] { return user->path(); };
        // C++版のJPY1/JPDCパスは PathUser のラッパーを経
        // 由する。PathUser::id() はラッパーを再登録するが、
        // 所有者の付加情報のコールバックを通らない。D版の仮想パス設定と同様
        // 、 解析済みファイル名と UseCounter の所属を両方更新す
        // るため、 両ホスト・両種類のコールバック付きJPYリソースで所有者
        // のパス設定を呼ぶ。通常の PathUser は id() を使う。
        target.write = [summ, user, owner](String value) {
            const auto next = usecounter::toPathId(value);
            if (const auto jpy1 = std::dynamic_pointer_cast<jpy::Jpy1Sec>(owner)) {
                jpy1->path(value);
                return true;
            }
            if (const auto jpdc = std::dynamic_pointer_cast<jpy::Jpdc>(owner)) {
                jpdc->path(value);
                return true;
            }
            return user->id(next);
        };
        if (!emit(std::move(target))) return false;
    }
    return true;
}

std::vector<TextTarget> ReplaceDialog::summaryCouponTargets() const {
    std::vector<TextTarget> result;
    const auto counter = summary_ ? summary_->useCounter()
                                  : lib::Owner<usecounter::UseCounter>{};
    if (!counter) return result;

    // D版の searchCouponImpl に相当する処理。整列
    // 済みのキーごとに一行を作り、 範囲ツリーで所有者が選ばれた参照
    // 元の数を表示する。 全参照元を数える UseCounter::get()
    // は、狭い範囲の件数を誤って表示するため使わない。
    const auto append = [this, &result, counter]<typename ID>(
                            const IDKind id_kind, StringView kind,
                            const bool selected, const ResourceId image) {
        if (!selected) return;
        std::vector<ID> keys;
        for (const auto& key : counter->keys<ID>()) keys.push_back(key);
        std::sort(keys.begin(), keys.end(), [](const ID& left, const ID& right) {
            return left.toString() < right.toString();
        });
        for (const auto& key : keys) {
            if (cancel_) break;
            const auto all_users = counter->values(key);
            std::vector<lib::Owner<usecounter::IDToUser_t<ID>>> users;
            std::vector<CWXPathPtr> owners;
            users.reserve(all_users.size());
            owners.reserve(all_users.size());
            for (const auto& user : all_users) {
                if (!user) continue;
                const auto owner = user->owner();
                if (!dec(owner, range_table_)) continue;
                users.push_back(user);
                owners.push_back(owner);
            }
            if (users.empty()) continue;

            // D版の renameCoupon と同様、従属カウ
            // ンターの記録も ucOwner で範囲を判定して更
            // 新する。 一覧の行にはしないが、残すと範囲を限定
            // したインライン改名で一つの名前が分裂してしまう。
            std::vector<lib::Owner<usecounter::IDToUser_t<ID>>> sub_users;
            if (const auto sub = counter->sub()) {
                const auto all_sub_users = sub->values(key);
                sub_users.reserve(all_sub_users.size());
                for (const auto& user : all_sub_users) {
                    if (!user) continue;
                    const auto owner = user->ucOwner();
                    if (!owner || dec(owner, range_table_)) sub_users.push_back(user);
                }
            }

            auto current = std::make_shared<ID>(key);
            TextTarget target;
            target.kind = String(kind);
            target.description = kindText(id_kind);
            target.resultCount = users.size();
            target.image = image;
            target.replaceable = false;
            target.rangeOwners = std::move(owners);
            target.nameIdKind = id_kind;
            target.read = [current] { return current->toString(); };
            target.write = [current, users = std::move(users),
                            sub_users = std::move(sub_users)](String value) {
                if (value.empty()) return false;
                const auto next = parseId<ID>(value);
                if (!next) return false;
                if (*current == *next) return true;
                bool changed = false;
                // D版の UseCounter.replaceID(newVal,
                // u) と同様、参照元ごとの設定処理
                // を使う。 カウンター全体を変えると、
                // 選択範囲外の参照まで改名してしまう。
                for (const auto& user : users) {
                    if (!user) continue;
                    usecounter::UseCounter::replaceID(*next, user);
                    changed = true;
                }
                for (const auto& user : sub_users) {
                    if (!user) continue;
                    usecounter::UseCounter::replaceID(*next, user);
                    changed = true;
                }
                if (!changed) return false;
                *current = *next;
                return true;
            };
            result.push_back(std::move(target));
        }
    };

    const auto& option = properties_.options;
    append.template operator()<CouponId>(IDKind::Coupon, "coupon-name",
                                         option.replaceNameCoupon,
                                         comm_.images.couponNormal);
    append.template operator()<GossipId>(IDKind::Gossip, "gossip-name",
                                         option.replaceNameGossip,
                                         comm_.images.gossip);
    append.template operator()<CompleteStampId>(IDKind::CompleteStamp, "end-name",
                                                option.replaceNameEndScenario,
                                                comm_.images.completeStamp);
    append.template operator()<KeyCodeId>(IDKind::KeyCode, "keycode-name",
                                          option.replaceNameKeyCode,
                                          comm_.images.keyCode);
    append.template operator()<CellNameId>(IDKind::CellName, "cellname-name",
                                           option.replaceNameCellName,
                                           comm_.images.backs);
    append.template operator()<CardGroupId>(IDKind::CardGroup, "cardgroup-name",
                                            option.replaceNameCardGroup,
                                            comm_.images.cards);
    return result;
}

std::vector<IdTarget> ReplaceDialog::summaryIdTargets(IDKind kind) const {
    std::vector<IdTarget> result;
    (void)enumerateSummaryIds(kind, [&result](IdTarget target) {
        result.push_back(std::move(target));
        return true;
    });
    return result;
}

bool ReplaceDialog::enumerateSummaryIds(
    const IDKind kind, const std::function<bool(IdTarget)>& emit) const {
    if (!emit) return true;
    const auto counter = summary_ ? summary_->useCounter() : lib::Owner<usecounter::UseCounter>{};
    if (!counter) return true;
    return appendIdTargets<AreaId>(counter, kind, IDKind::Area, emit) &&
           appendIdTargets<BattleId>(counter, kind, IDKind::Battle, emit) &&
           appendIdTargets<PackageId>(counter, kind, IDKind::Package, emit) &&
           appendIdTargets<CastId>(counter, kind, IDKind::Cast, emit) &&
           appendIdTargets<SkillId>(counter, kind, IDKind::Skill, emit) &&
           appendIdTargets<ItemId>(counter, kind, IDKind::Item, emit) &&
           appendIdTargets<BeastId>(counter, kind, IDKind::Beast, emit) &&
           appendIdTargets<InfoId>(counter, kind, IDKind::Info, emit) &&
           appendIdTargets<FlagId>(counter, kind, IDKind::Flag, emit) &&
           appendIdTargets<StepId>(counter, kind, IDKind::Step, emit) &&
           appendIdTargets<VariantId>(counter, kind, IDKind::Variant, emit) &&
           appendIdTargets<CouponId>(counter, kind, IDKind::Coupon, emit) &&
           appendIdTargets<GossipId>(counter, kind, IDKind::Gossip, emit) &&
           appendIdTargets<CompleteStampId>(counter, kind, IDKind::CompleteStamp, emit) &&
           appendIdTargets<KeyCodeId>(counter, kind, IDKind::KeyCode, emit) &&
           appendIdTargets<CellNameId>(counter, kind, IDKind::CellName, emit) &&
           appendIdTargets<CardGroupId>(counter, kind, IDKind::CardGroup, emit);
}

std::vector<std::shared_ptr<RangeNode>> ReplaceDialog::defaultRangeNodes() const {
    if (source_.rangeTree) return source_.rangeTree();
    std::vector<std::shared_ptr<RangeNode>> roots;
    if (!summary_) return roots;
    const auto make = [&](CWXPathPtr path, String label, String match = {}, bool always = false) {
        auto node = std::make_shared<RangeNode>();
        node->path = std::move(path);
        node->label = std::move(label);
        node->matchName = std::move(match);
        node->alwaysVisible = always;
        const auto& value = node->path;
        node->cwxPath = value->cwxPath(true);
        if (properties_.rangeImage) node->image = properties_.rangeImage(value.get());
        if (const auto found = range_table_.find(value.get()); found != range_table_.end())
            node->checked = found->second;
        return node;
    };
    roots.push_back(make(summaryPath(summary_), properties_.messages.rangeSummary, {}, true));
    if (const auto flags = summary_->flagDirRoot())
        roots.push_back(make(flags, properties_.messages.rangeVariables, {}, true));

    // D版と同様、イベント・コンテント・テキストの全ノードではなく、リソース単位で
    // 検索する。 子孫は dec() で、最も近いリソースの範囲設定を引き継ぐ。
    std::unordered_map<String, std::shared_ptr<RangeNode>> directories;
    std::vector<String> directory_names;
    const auto collect_dirs = [&](const auto& values) {
        for (const auto& value : values) {
            if (!value) continue;
            auto name = value->dirName();
            std::replace(name.begin(), name.end(), '/', '\\');
            if (!name.empty()) directory_names.push_back(std::move(name));
        }
    };
    if (properties_.options.showAreaDirectoryTree) {
        collect_dirs(summary_->areas());
        collect_dirs(summary_->battles());
        collect_dirs(summary_->packages());
        std::stable_sort(directory_names.begin(), directory_names.end(), [&](const auto& a, const auto& b) {
            return utils::NCompare(a, true, properties_.options.logicalSort) <
                   utils::NCompare(b, true, properties_.options.logicalSort);
        });
        for (const auto& name : directory_names) {
            std::shared_ptr<RangeNode> parent;
            String full;
            std::size_t start{};
            while (start < name.size()) {
                const auto end = name.find('\\', start);
                const auto part = name.substr(start, end == String::npos ? String::npos : end - start);
                if (!part.empty()) {
                    if (!full.empty()) full += '\\';
                    full += part;
                    const auto key = lower(full);
                    auto& folder = directories[key];
                    if (!folder) {
                        folder = std::make_shared<RangeNode>();
                        folder->label = part;
                        folder->cwxPath = key;
                        folder->image = properties_.rangeDirectoryImage;
                        (parent ? parent->children : roots).push_back(folder);
                    }
                    parent = folder;
                }
                if (end == String::npos) break;
                start = end + 1U;
            }
        }
    }
    const auto add_areas = [&](const auto& values) {
        for (const auto& value : values) {
            if (!value) continue;
            const auto name = properties_.options.showAreaDirectoryTree ? value->baseName() : value->name();
            auto node = make(std::static_pointer_cast<area::AbstractArea>(value),
                             std::to_string(value->id()) + "." + name, name);
            auto dir = value->dirName();
            std::replace(dir.begin(), dir.end(), '/', '\\');
            const auto parent = directories.find(lower(dir));
            (parent == directories.end() ? roots : parent->second->children).push_back(std::move(node));
        }
    };
    add_areas(summary_->areas());
    add_areas(summary_->battles());
    add_areas(summary_->packages());
    const auto add_cards = [&](const auto& values, auto& destination, bool carried) {
        for (const auto& value : values) {
            if (!value) continue;
            if constexpr (requires { value->linkId(); })
                if (carried && value->linkId() != 0U) continue;
            destination.push_back(make(value, std::to_string(value->id()) + "." + value->name(), value->name()));
        }
    };
    for (const auto& cast : summary_->casts()) {
        if (!cast) continue;
        auto node = make(cast, std::to_string(cast->id()) + "." + cast->name(), cast->name());
        add_cards(cast->skills(), node->children, true);
        add_cards(cast->items(), node->children, true);
        add_cards(cast->beasts(), node->children, true);
        roots.push_back(std::move(node));
    }
    add_cards(summary_->skills(), roots, false);
    add_cards(summary_->items(), roots, false);
    add_cards(summary_->beasts(), roots, false);
    add_cards(summary_->infos(), roots, false);
    return roots;
}

bool ReplaceDialog::targetSelected(const TextTarget& target) const {
    const auto kind = targetKind(target);
    const auto& option = properties_.options;
    const auto in_range = [&] {
        if (!target.rangeOwners.empty()) {
            return std::any_of(target.rangeOwners.begin(), target.rangeOwners.end(),
                               [this](const CWXPathPtr& owner) {
                                   return dec(owner, range_table_);
                               });
        }
        return !target.owner || dec(target.owner, range_table_);
    };
    if (tab_ == Tab::Grep && target.wholeScenarioListing) return true;
    if (tab_ == Tab::Path && (std::dynamic_pointer_cast<jpy::Jpy1Sec>(target.owner) ||
        std::dynamic_pointer_cast<jpy::Jpdc>(target.owner))) return true;
    if (tab_ == Tab::Path || tab_ == Tab::Error) return in_range();
    if (tab_ == Tab::Contents) {
        std::optional<std::size_t> content_index;
        if (target.contentType)
            content_index = static_cast<std::size_t>(*target.contentType);
        else {
            constexpr std::array<StringView, 2> prefixes{"content-", "content:"};
            for (const auto prefix : prefixes) {
                if (!kind.starts_with(prefix)) continue;
                try {
                    content_index = static_cast<std::size_t>(
                        std::stoul(kind.substr(prefix.size())));
                } catch (...) {
                }
                break;
            }
        }
        if (content_index &&
            (*content_index >= option.contents.size() ||
             !option.contents[*content_index]))
            return false;
        return in_range();
    }
    if (tab_ == Tab::Coupon) {
        bool enabled = true;
        if (kind == "coupon" || kind == "coupon-name")
            enabled = option.replaceNameCoupon;
        else if (kind == "gossip" || kind == "gossip-name")
            enabled = option.replaceNameGossip;
        else if (kind == "end" || kind == "end-name" ||
                 kind == "endscenario" || kind == "endscenario-name")
            enabled = option.replaceNameEndScenario;
        else if (kind == "keycode" || kind == "key-code" ||
                 kind == "keycode-name" || kind == "key-code-name")
            enabled = option.replaceNameKeyCode;
        else if (kind == "cellname" || kind == "cell-name" ||
                 kind == "cellname-name" || kind == "cell-name-name")
            enabled = option.replaceNameCellName;
        else if (kind == "cardgroup" || kind == "card-group" ||
                 kind == "cardgroup-name" || kind == "card-group-name")
            enabled = option.replaceNameCardGroup;
        return enabled && in_range();
    }
    if (tab_ == Tab::Unused) {
        if (kind == "flag" || kind == "unusedflag" || kind == "unused-flag")
            return option.unusedFlag;
        if (kind == "step" || kind == "unusedstep" || kind == "unused-step")
            return option.unusedStep;
        if (kind == "variant" || kind == "common" || kind == "unusedvariant" ||
            kind == "unused-variant")
            return option.unusedVariant;
        if (kind == "area" || kind == "unusedarea" || kind == "unused-area")
            return option.unusedArea;
        if (kind == "battle" || kind == "unusedbattle" ||
            kind == "unused-battle")
            return option.unusedBattle;
        if (kind == "package" || kind == "unusedpackage" ||
            kind == "unused-package")
            return option.unusedPackage;
        if (kind == "cast" || kind == "unusedcast" || kind == "unused-cast")
            return option.unusedCast;
        if (kind == "skill" || kind == "unusedskill" || kind == "unused-skill")
            return option.unusedSkill;
        if (kind == "item" || kind == "unuseditem" || kind == "unused-item")
            return option.unusedItem;
        if (kind == "beast" || kind == "unusedbeast" || kind == "unused-beast")
            return option.unusedBeast;
        if (kind == "info" || kind == "unusedinfo" || kind == "unused-info")
            return option.unusedInfo;
        if (kind == "start" || kind == "unusedstart" || kind == "unused-start")
            return option.unusedStart;
        if (kind == "material" || kind == "path" || kind == "unusedpath" ||
            kind == "unused-path")
            return option.unusedPath;
        return true;
    }
    std::optional<bool> enabled;
    if (kind == "summary") enabled = option.summary;
    else if (kind == "scenario") enabled = option.scenario;
    else if (kind == "author") enabled = option.author;
    else if (kind == "message" || kind == "text" || kind == "msg") enabled = option.message;
    else if (kind == "cardname" || kind == "card-name") enabled = option.cardName;
    if (kind == "carddesc" || kind == "carddescription" || kind == "card-description")
        enabled = option.cardDescription;
    else if (kind == "event" || kind == "eventtext" || kind == "event-text")
        enabled = option.eventText;
    else if (kind == "start") enabled = option.start;
    if (kind == "varname" || kind == "variable" || kind == "variable-name")
        enabled = option.variableName;
    else if (kind == "varvalue" || kind == "variable-value") enabled = option.variableValue;
    else if (kind == "expression") enabled = option.expression;
    else if (kind == "coupon") enabled = option.coupon;
    else if (kind == "coupon-name") enabled = option.replaceNameCoupon;
    else if (kind == "gossip") enabled = option.gossip;
    else if (kind == "gossip-name") enabled = option.replaceNameGossip;
    if (kind == "end" || kind == "endscenario" || kind == "end-scenario")
        enabled = option.endScenario;
    else if (kind == "end-name" || kind == "endscenario-name")
        enabled = option.replaceNameEndScenario;
    else if (kind == "area" || kind == "area-name") enabled = option.areaName;
    else if (kind == "keycode" || kind == "key-code") enabled = option.keyCode;
    if (kind == "keycode-name" || kind == "key-code-name")
        enabled = option.replaceNameKeyCode;
    else if (kind == "cellname" || kind == "cell-name") enabled = option.cellName;
    if (kind == "cellname-name" || kind == "cell-name-name")
        enabled = option.replaceNameCellName;
    else if (kind == "cardgroup" || kind == "card-group") enabled = option.cardGroup;
    if (kind == "cardgroup-name" || kind == "card-group-name")
        enabled = option.replaceNameCardGroup;
    else if (kind == "file" || kind == "image" || kind == "path" || kind == "embedded-font") enabled = option.file;
    else if (kind == "comment") enabled = option.comment;
    else if (kind == "jptx") enabled = option.jptx;
    else if (kind == "textfile" || kind == "text-file") enabled = option.textFile;
    else if (kind == "unusedflag" || kind == "unused-flag") enabled = option.unusedFlag;
    else if (kind == "unusedstep" || kind == "unused-step") enabled = option.unusedStep;
    else if (kind == "unusedvariant" || kind == "unused-variant") enabled = option.unusedVariant;
    else if (kind == "unusedarea" || kind == "unused-area") enabled = option.unusedArea;
    else if (kind == "unusedbattle" || kind == "unused-battle") enabled = option.unusedBattle;
    else if (kind == "unusedpackage" || kind == "unused-package") enabled = option.unusedPackage;
    else if (kind == "unusedcast" || kind == "unused-cast") enabled = option.unusedCast;
    else if (kind == "unusedskill" || kind == "unused-skill") enabled = option.unusedSkill;
    else if (kind == "unuseditem" || kind == "unused-item") enabled = option.unusedItem;
    else if (kind == "unusedbeast" || kind == "unused-beast") enabled = option.unusedBeast;
    else if (kind == "unusedinfo" || kind == "unused-info") enabled = option.unusedInfo;
    else if (kind == "unusedstart" || kind == "unused-start") enabled = option.unusedStart;
    else if (kind == "unusedpath" || kind == "unused-path") enabled = option.unusedPath;
    if (enabled && !*enabled) return false;
    if (tab_ == Tab::Unused || tab_ == Tab::Grep || !target.owner) return true;
    return dec(target.owner, range_table_);
}

bool ReplaceDialog::targetSelected(const IdTarget& target) const {
    if (!target.owner) return true;
    return dec(target.owner, range_table_);
}

void ReplaceDialog::refreshToolbar() {
    if (!in_proc_) {
        if (live_settings_pending_) applyLiveSettings();
        if (scope_refresh_pending_) requestScopeRefresh();
    }
    // 意図的な追加として、空の検索語では操作を無効にする。 D版のよう
    // に、ボタンを有効にしたまま実行時に入力検証する方式にはしない。
    if (find_button_) find_button_->set_enabled(canFind());
    if (replace_button_) replace_button_->set_enabled(canReplace());
    if (cancel_button_) cancel_button_->set_enabled(canCancel());
    if (close_button_) close_button_->set_enabled(true);
    if (grep_open_enabled_refresh_) grep_open_enabled_refresh_();
    const bool range_enabled = tab_ != Tab::Unused && tab_ != Tab::Grep;
    if (range_view_) range_view_->set_enabled(range_enabled && !in_proc_);
    const bool range_all_enabled =
        !(tab_ == Tab::Text && properties_.options.ignoreReturnCode) &&
        tab_ != Tab::Contents && tab_ != Tab::Coupon && tab_ != Tab::Unused &&
        tab_ != Tab::Error;
    if (range_all_check_)
        range_all_check_->set_enabled(range_all_enabled && !in_proc_);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void ReplaceDialog::refreshSource() {
    if (source_.refreshTargets) source_.refreshTargets();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::replacedialog
