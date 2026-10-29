#include "event_content_dialog_eligibility.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventwindow.hpp"

#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventtreedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/roundview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_stack.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::eventwindow {

namespace {

[[nodiscard]] std::int32_t configuredEventTreeSlopeMaximum(
    const variables::FlexEtcProps* vars) noexcept {
    if (lib::editor_window_host_options().all_mode) return 32;
    constexpr std::int64_t standaloneDefault = 16;
    const std::int64_t dpiMultiplier = std::max<std::int64_t>(1, dutils::dpiMuls());
    const std::int64_t loadedValue = vars
        ? std::max<std::int64_t>(0, vars->eventTreeSlope.INIT)
        : standaloneDefault;
    // D版の Spinner と同様、eventTreeSlope.INIT.ppis を使う。保
    // 存された INIT が不正でも 整数の上限を超えないよう、倍率適用前に広い整数型へ変換する。
    const std::int64_t maximum = loadedValue * dpiMultiplier;
    return static_cast<std::int32_t>(std::clamp(maximum,
        std::int64_t{0}, static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

String connectedMaterialFile(const event::ContentPtr& content) {
    if (!content) return {};
    if (lib::editor_window_host_options().all_mode ||
        !event::contentDetail(content->type()).use(event::CArg::TalkerC))
        return content->connectedFile();
    // D版と同様、最初の関連素材を選ぶ前に埋め込み画像URIを
    // 除外する。 この処理は単独エディタのコマンド経路で行う。
    for (const auto& image : content->cardImages()) {
        if (image && image->type == types::CardImageType::File &&
            !image->path().empty() && !utils::isBinImg(image->path()))
            return image->path();
    }
    for (const auto& path : content->cardPaths())
        if (!path.empty() && !utils::isBinImg(path)) return path;
    return {};
}

event::EventTreeOwner* selectedConnectedOwner(
        const rich_eventview::EventView* view) noexcept {
    if (!view) return nullptr;
    const auto cards = view->tree();
    if (!cards || cards->selection.empty()) return nullptr;
    const auto* info = view->nodeInfo(cards->selection.front());
    if (!info || info->kind != rich_eventview::NodeInfo::Kind::Owner || !info->owner ||
        dynamic_cast<area::PlayerCardEvents*>(info->owner)) return nullptr;
    return info->owner;
}

String ownerCommentName(const event::EventTreeOwner* owner, const String& displayed,
                        const std::shared_ptr<summary::Summary>& summary) {
    if (const auto* value = dynamic_cast<const area::AbstractArea*>(owner)) return value->name();
    if (const auto* value = dynamic_cast<const area::MenuCard*>(owner)) return value->name();
    if (const auto* value = dynamic_cast<const area::EnemyCard*>(owner)) {
        // D版と同様、明示的な空の上書き値を尊重し、Cast がない場合は空文字列を使う。
        if (value->isOverrideName()) return value->overrideName();
        const auto cast = summary ? summary->cwCast(value->id()) : nullptr;
        return cast ? cast->name() : String{};
    }
    return displayed;
}

void clearOperationalHistoryFocus(OperationalHistoryState& history) noexcept {
    if (history.focus_observer != 0U) {
        lib::remove_focus_observer(history.focus_observer);
        history.focus_observer = 0U;
    }
    history.focused_widget.reset();
    history.focus_gui.reset();
}

void observeOperationalHistoryFocus(
        OperationalHistory history, std::shared_ptr<lib::GuiBackend> gui,
        std::shared_ptr<EventView> origin,
        const rich_eventview::EventView* rich) {
    if (!history || !gui || !origin || !rich || origin->hostRetired()) return;
    // 一つの OperationalHistory は、アプリ内の一つのフォーカス推移を
    // 表す。 関連ビューが誤って別のバックエンドに接続されていた場合は、最初の購読を維持
    // する。 スナップショットの解決は、最初の有効な関連ビューへ安全に切り替えられる。
    if (history->focus_observer != 0U) return;

    const auto weak = std::weak_ptr<OperationalHistoryState>(history);
    const auto observer = lib::add_focus_observer(
        [weak](const lib::WidgetPtr& focused) {
            if (const auto live = weak.lock()) {
                live->focused_widget = focused;
                ++live->focus_cache_revision;
            }
        });
    history->focus_observer = observer;
    history->focus_gui = gui;

    // 再入によるフォーカス遷移を取りこぼさないよう、取得前に購読する。
    // 取得中に遷移した場合は、今回の取得値より通知による更新を優先する。
    const auto cacheRevision = history->focus_cache_revision;
    const auto revision = gui->focus_revision();
    if (history->focus_observer != observer || history->focus_gui.lock() != gui ||
        history->focus_cache_revision != cacheRevision ||
        origin->hostRetired() || origin->richView() != rich) return;
    lib::WidgetPtr focused;
    try { focused = gui->focused_widget(); }
    catch (...) { return; }
    if (history->focus_observer != observer || history->focus_gui.lock() != gui ||
        origin->hostRetired() || origin->richView() != rich) return;
    const auto afterRevision = gui->focus_revision();
    if (history->focus_observer != observer || history->focus_gui.lock() != gui ||
        origin->hostRetired() || origin->richView() != rich) return;
    if (afterRevision == revision && history->focus_cache_revision == cacheRevision)
        history->focused_widget = std::move(focused);
}

// 改名・履歴再適用・表示更新を経ても、タイトルと元のモデルの対応を維持する。
template<class DialogMap>
void refreshCommentTitles(const rich_eventview::EventView* view,
                          const DialogMap& dialogs, const props::CProps* properties,
                          const std::shared_ptr<summary::Summary>& summary) {
    if (!view || !properties) return;
    const auto rows = view->tree();
    if (!rows) return;
    for (const auto& row : rows->nodes) {
        const auto* info = view->nodeInfo(row.id);
        if (!info) continue;
        const path::CWXPath* key = nullptr;
        String name;
        if (info->kind == rich_eventview::NodeInfo::Kind::Owner) {
            key = info->owner; name = ownerCommentName(info->owner, row.text, summary);
        } else if (info->tree) {
            key = info->tree.get();
            // 空の EventTree にはスタート名がないため、安全に取得済
            // みの表示ラベルを使う。 EventTree::name() は
            // D版と同様に最初のスタートを参照し、空のツリーでは例外になる。
            name = info->tree->starts().empty() || !info->tree->starts().front()
                ? row.text : info->tree->name();
        }
        const auto found = dialogs.find(key);
        if (found != dialogs.end() && found->second)
            found->second->title = utils::tryFormat(properties->requiredMsgs().dlgTitCommentWith.value, name);
    }
}

// D版の EventTreeView と同様、スクリプト操作では、左側のイベント一覧にフォーカス
// があっても 保持中の右ペインの選択を使う。それ以外の編集操作はフォーカスのある側を対象とする。
[[nodiscard]] event::ContentPtr scriptSelection(const rich_eventview::EventView& view) {
    const auto active = view.selectionContent();
    return active ? active : view.displayedContent();
}

// D版の EventTreeView.views() と同様、同じ所有者と選択 EventTree
// に対応するビューを Commons から取得する。 コールバック中も
// 関連ビューの寿命を保持し、呼び出し元のホストが無効になったら中止する。
[[nodiscard]] bool finishEventTreeEdits(EventView& current,
                                     const OperationalHistory& history,
                                     commons::Commons* common,
                                     const commons::CWXPathPtr& owner,
                                     const event::EventTreePtr& selectedTree,
                                     const std::weak_ptr<int>& life) {
    auto* const currentRich = current.richView();
    if (!currentRich || life.expired()) return false;
    const auto tree = selectedTree ? selectedTree : currentRich->displayedEventTree();
    const auto control = currentRich->eventTreeControl();
    // 最初のビュー取得コールバックや editEnter の前に、ローカルの関連ビューを保
    // 持する。 どちらも同期的にビューを無効化し、この弱参照一覧を変更する可能性がある。
    std::vector<std::shared_ptr<EventView>> historyViews;
    if (history) {
        historyViews.reserve(history->views.size());
        for (const auto& weak : history->views)
            if (auto view = weak.lock(); view && !view->hostRetired())
                historyViews.push_back(std::move(view));
    }
    std::vector<decltype(currentRich->eventTreeControl())> finishedControls;
    if (common && tree) {
        const auto commonLife = common->lifetimeToken();
        const auto peerControls = common->eventTreeViewFrom(owner, tree, false);
        if (commonLife.expired() || life.expired() || current.richView() != currentRich)
            return false;
        for (const auto& peerControl : peerControls) {
            if (!peerControl || peerControl == control) continue;
            if (life.expired() || commonLife.expired() ||
                current.richView() != currentRich) return false;
            const auto panel = commons::getTopLevelPanel(peerControl);
            const auto finish = panel
                ? panel->viewProjection().finishEventTreeEdit
                : std::function<bool(const event::EventTreePtr&)>{};
            if (!finish || !finish(tree)) return false;
            if (life.expired() || commonLife.expired() ||
                current.richView() != currentRich ||
                !commons::Commons::sameEventTreeIdentity(
                    currentRich->displayedEventTree(), tree)) return false;
            finishedControls.push_back(peerControl);
        }
        if (!commons::Commons::sameEventTreeIdentity(
                currentRich->displayedEventTree(), tree)) return false;
    }

    // MainWindow から取得するビューには、独立したパッケージの
    // 履歴も含まれる。 直接使用するホストは未登録でも Commons
    // ポインタを持てるため、 取得した関連ビューとローカル履歴の関連ビ
    // ューを合わせ、EventTree コントロール単位で重複を除く。
    auto* const currentOwner = tree ? tree->owner() : current.eventTreeOwner();
    const auto canonicalOwner = commons::Commons::canonicalEventOwner(currentOwner);
    for (const auto& view : historyViews) {
        if (!view || view.get() == &current || view->hostRetired()) continue;
        auto* const rich = view->richView();
        if (!rich) continue;
        const auto peerTree = rich->displayedEventTree();
        auto* const peerOwner = peerTree ? peerTree->owner() : view->eventTreeOwner();
        const auto peerCanonicalOwner =
            commons::Commons::canonicalEventOwner(peerOwner);
        if (!commons::Commons::sameOwnerIdentity(canonicalOwner, peerCanonicalOwner) ||
            !commons::Commons::sameEventTreeIdentity(peerTree, tree)) continue;
        const auto peerControl = rich->eventTreeControl();
        if (peerControl == control ||
            std::find(finishedControls.begin(), finishedControls.end(), peerControl) !=
                finishedControls.end()) continue;
        if (life.expired() || current.richView() != currentRich ||
            !commons::Commons::sameEventTreeIdentity(
                currentRich->displayedEventTree(), tree)) return false;
        rich->editEnter();
        if (peerControl) finishedControls.push_back(peerControl);
        if (life.expired() || current.richView() != currentRich ||
            !commons::Commons::sameEventTreeIdentity(
                currentRich->displayedEventTree(), tree)) return false;
    }
    currentRich->editEnter();
    return !life.expired() && current.richView() == currentRich;
}

// スクリプト出力と発火操作では、D版と同じ所有者・選択ツリーのビュー集合を使う。
[[nodiscard]] bool finishScriptEdits(EventView& current,
                                     const OperationalHistory& history,
                                     commons::Commons* common,
                                     const commons::CWXPathPtr& owner,
                                     const std::weak_ptr<int>& life) {
    auto* const rich = current.richView();
    if (!rich) return false;
    const auto tree = rich->displayedEventTree();
    return finishEventTreeEdits(current, history, common, owner, tree, life);
}

// D版の EventView.refresh と同様、同じ所有者の全ビューで未確定の編集をキャンセ
// ルする。 コールバック中も関連ビューの寿命を保持し、呼び出し元のホストが無効になったら中止する。
[[nodiscard]] bool cancelScriptEdits(EventView& current,
                                     const OperationalHistory& history,
                                     const std::weak_ptr<int>& life) {
    if (history && history->refreshEditCancellationActive) return true;
    struct RefreshEditCancellationScope {
        OperationalHistory history;
        explicit RefreshEditCancellationScope(OperationalHistory value)
            : history(std::move(value)) {
            if (history) history->refreshEditCancellationActive = true;
        }
        ~RefreshEditCancellationScope() {
            if (history) history->refreshEditCancellationActive = false;
        }
    } cancellationScope(history);
    std::vector<std::shared_ptr<EventView>> peers;
    bool includesCurrent = false;
    if (history) {
        for (const auto& weak : history->views) {
            if (auto view = weak.lock(); view && !view->hostRetired()) {
                includesCurrent = includesCurrent || view.get() == &current;
                peers.push_back(std::move(view));
            }
        }
    }
    if (!includesCurrent) {
        if (auto* rich = current.richView()) rich->editCancel();
        if (life.expired()) return false;
    }
    for (const auto& view : peers) {
        if (life.expired()) return false;
        if (view->hostRetired()) continue;
        auto* rich = view->richView();
        if (!rich || view->hostRetired() || view->richView() != rich) continue;
        rich->editCancel();
        if (life.expired()) return false;
    }
    return true;
}

// 同じ所有者のインライン編集をキャンセルすると、MainWindow に処理が戻る場合が
// ある。 共有履歴の寿命を保持し、履歴の再適用が終わるまで入れ子の履歴操作を拒否する。
class HistoryReplayCommandScope {
public:
    explicit HistoryReplayCommandScope(OperationalHistory history)
        : history_(std::move(history)) {
        if (history_ && (history_->historyReplayCommandActive ||
                         history_->refreshEditCancellationActive)) return;
        if (history_) history_->historyReplayCommandActive = true;
        active_ = true;
    }

    ~HistoryReplayCommandScope() {
        if (active_ && history_) history_->historyReplayCommandActive = false;
    }

    HistoryReplayCommandScope(const HistoryReplayCommandScope&) = delete;
    HistoryReplayCommandScope& operator=(const HistoryReplayCommandScope&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return active_; }

private:
    OperationalHistory history_;
    bool active_{};
};

void copyContentsPaletteProperties(flagspane::EtcProperties& properties,
                                  const ::cwxeditor::cwx::variables::FlexEtcProps& settings) {
        properties.contentsAutoOpen = settings.contentsAutoOpen.value;
        properties.contentsLock = settings.contentsLock.value;
        properties.contentsWrapIndices.assign(
            settings.contentsWrapIndices.value.begin(),
            settings.contentsWrapIndices.value.end());
        properties.contentsPutMode = settings.contentsPutMode.value;
        properties.contentsInsertFirst = settings.contentsInsertFirst.value;
        properties.contentsOrder.assign(settings.contentsOrder.value.begin(),
                                            settings.contentsOrder.value.end());
        properties.showContentsGroupName =
            settings.showContentsGroupName.value;
        properties.contentsFloat = settings.contentsFloat.value;
        properties.contentsAutoHide = settings.contentsAutoHide.value;
        properties.showContentsBoxHeightWhenNoToolBar =
            settings.showContentsBoxHeightWhenNoToolBar.value;
}


class CardEventOwner final : public event::EventTreeOwner,
                             public ::cwxeditor::cwx::flag::LocalVariableOwner,
                             public std::enable_shared_from_this<CardEventOwner> {
public:
    explicit CardEventOwner(lib::Owner<card::EffectCard> card)
        : card_(std::move(card)) {
        // D版と同様、空のカードを開くだけでは変更しない。明示的な NewEvent 操作でのみツリーを追加する。
    }

    [[nodiscard]] lib::Owner<path::CWXPath> retainForCallback() override {
        return std::static_pointer_cast<path::CWXPath>(weak_from_this().lock());
    }

    [[nodiscard]] lib::SharedSlice<event::EventTreePtr> trees() override {
        return card_->trees();
    }
    [[nodiscard]] lib::ConstOwnerSlice<event::EventTree> trees() const override {
        return static_cast<const card::EffectCard&>(*card_).trees();
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override {
        return card_->eventTreeLifetimeToken();
    }
    [[nodiscard]] bool canHasFireEnter() const noexcept override {
        return card_->canHasFireEnter();
    }
    [[nodiscard]] bool canHasFireLose() const noexcept override {
        return card_->canHasFireLose();
    }
    [[nodiscard]] bool canHasFireEscape() const noexcept override {
        return card_->canHasFireEscape();
    }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override {
        return card_->canHasFireEveryRound();
    }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override {
        return card_->canHasFireRoundEnd();
    }
    [[nodiscard]] bool canHasFireRound0() const noexcept override {
        return card_->canHasFireRound0();
    }
    [[nodiscard]] bool canHasFireRound() const noexcept override {
        return card_->canHasFireRound();
    }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override {
        return card_->canHasFireKeyCode();
    }
    void add(const event::EventTreePtr& value) override { card_->add(value); }
    void insert(std::size_t index, const event::EventTreePtr& value) override {
        card_->insert(index, value);
    }
    void removeEvent(std::size_t index) override { card_->removeEvent(index); }
    void remove(const event::EventTreePtr& value) override { card_->remove(value); }
    void swapEventTree(std::size_t first, std::size_t second) override {
        card_->swapEventTree(first, second);
    }
    void clearEvents() override { card_->clearEvents(); }
    [[nodiscard]] lib::Array<std::size_t> areaPath() const override {
        return card_->areaPath();
    }
    [[nodiscard]] bool isEmpty() const override { return card_->isEmpty(); }
    [[nodiscard]] String commentForEvents() const override {
        return card_->commentForEvents();
    }
    void commentForEvents(String value) override {
        card_->commentForEvents(std::move(value));
    }

    [[nodiscard]] std::uint64_t id() const override { return card_->id(); }
    [[nodiscard]] String name() const override { return card_->name(); }
    [[nodiscard]] ::cwxeditor::cwx::flag::FlagDirPtr flagDirRoot() const override {
        return card_->flagDirRoot();
    }
    [[nodiscard]] ::cwxeditor::cwx::flag::UseCounterPtr useCounter() const override {
        return card_->useCounter();
    }

    [[nodiscard]] String cwxPath(bool id) override { return card_->cwxPath(id); }
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(
        StringView value) override {
        return card_->findCWXPath(value);
    }
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds() override {
        return card_->cwxChilds();
    }
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override {
        return card_->cwxParent();
    }
    void changed() override { card_->changed(); }

protected:
    [[nodiscard]] event::EventTreeOwner* con() noexcept override { return card_.get(); }
    [[nodiscard]] lib::SharedSlice<event::EventTreePtr>& eventTreesStorage() override {
        return eventTreesStorageOf(*card_);
    }

private:
    lib::Owner<card::EffectCard> card_;
};

[[nodiscard]] String numberString(std::uint64_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::size_t configuredUndoMax(
    const ContentInitializerContext& context) {
    if (!context.vars) return 1024U;
    const auto value = context.vars->undoMaxEvent.value;
    return value > 0 ? static_cast<std::size_t>(value) : 0U;
}

[[nodiscard]] String ownerLabel(const props::CProps* prop, OwnerKind kind) {
    if (!prop) {
        switch (kind) {
        case OwnerKind::Area: return "Area";
        case OwnerKind::Battle: return "Battle";
        case OwnerKind::Package: return "Package";
        case OwnerKind::SkillCard: return "Skill";
        case OwnerKind::ItemCard: return "Item";
        case OwnerKind::BeastCard: return "Beast";
        case OwnerKind::Unknown: break;
        }
        return "Event";
    }

    const auto& messages = prop->requiredMsgs();
    switch (kind) {
    case OwnerKind::Area: return messages.area.value;
    case OwnerKind::Battle: return messages.battle.value;
    case OwnerKind::Package: return messages.cwPackage.value;
    case OwnerKind::SkillCard: return messages.skill.value;
    case OwnerKind::ItemCard: return messages.item.value;
    case OwnerKind::BeastCard: return messages.beast.value;
    case OwnerKind::Unknown: break;
    }
    return "Event";
}

[[nodiscard]] String connectedResourcePath(
    const event::ContentPtr& content, const SummaryPtr& summary) {
    if (!content || !summary) return {};
    const auto resourcePath = [](const auto& resource) -> String {
        return resource ? resource->cwxPath(true) : String{};
    };
    const auto detail = content->detail();
    if (detail.use(types::CArg::Area))
        return resourcePath(summary->area(content->area()));
    if (detail.use(types::CArg::Battle))
        return resourcePath(summary->battle(content->battle()));
    if (detail.use(types::CArg::Package))
        return resourcePath(summary->cwPackage(content->packages()));
    if (detail.use(types::CArg::Flag))
        return resourcePath(flag::findVar<flag::Flag>(
            summary->flagDirRoot(), content->useCounter(), content->flag()));
    if (detail.use(types::CArg::Step))
        return resourcePath(flag::findVar<flag::Step>(
            summary->flagDirRoot(), content->useCounter(), content->step()));
    if (detail.use(types::CArg::Variant))
        return resourcePath(flag::findVar<flag::Variant>(
            summary->flagDirRoot(), content->useCounter(), content->variant()));
    if (detail.use(types::CArg::Cast))
        return resourcePath(summary->cwCast(content->casts()));
    if (detail.use(types::CArg::Item))
        return resourcePath(summary->item(content->item()));
    if (detail.use(types::CArg::Skill))
        return resourcePath(summary->skill(content->skill()));
    if (detail.use(types::CArg::Beast))
        return resourcePath(summary->beast(content->beast()));
    if (detail.use(types::CArg::Info))
        return resourcePath(summary->info(content->info()));
    if (detail.use(types::CArg::Start)) {
        if (const auto tree = content->tree())
            return resourcePath(tree->start(content->start()));
    }
    if (detail.use(types::CArg::Flag2))
        return resourcePath(flag::findVar<flag::Flag>(
            summary->flagDirRoot(), content->useCounter(), content->flag2()));
    if (detail.use(types::CArg::Step2))
        return resourcePath(flag::findVar<flag::Step>(
            summary->flagDirRoot(), content->useCounter(), content->step2()));
    return {};
}

}  // namespace

EventOwnerHandle EventOwnerHandle::from(
    const lib::Owner<event::EventTreeOwner>& owner) {
    EventOwnerHandle result;
    if (!owner) return result;

    result.owner_ = owner;
    result.resource_ = std::static_pointer_cast<path::CWXPath>(owner);
    if (const auto areaOwner = std::dynamic_pointer_cast<area::Area>(owner)) {
        result.kind_ = OwnerKind::Area;
        result.id_ = numberString(areaOwner->id());
        result.name_ = areaOwner->name();
    } else if (const auto battleOwner =
                   std::dynamic_pointer_cast<area::Battle>(owner)) {
        result.kind_ = OwnerKind::Battle;
        result.id_ = numberString(battleOwner->id());
        result.name_ = battleOwner->name();
    } else if (const auto packageOwner =
                   std::dynamic_pointer_cast<area::Package>(owner)) {
        result.kind_ = OwnerKind::Package;
        result.id_ = numberString(packageOwner->id());
        result.name_ = packageOwner->name();
    } else {
        result.id_ = owner->cwxPath(true);
    }
    return result;
}

EventOwnerHandle EventOwnerHandle::from(const area::AreaPtr& owner) {
    if (!owner) return {};
    auto result = from(std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(
        std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.kind_ = OwnerKind::Area;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventOwnerHandle EventOwnerHandle::from(const area::BattlePtr& owner) {
    if (!owner) return {};
    auto result = from(std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(
        std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.kind_ = OwnerKind::Battle;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventOwnerHandle EventOwnerHandle::from(const area::PackagePtr& owner) {
    if (!owner) return {};
    auto result = from(std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(
        std::static_pointer_cast<event::EventTreeOwner>(owner));
    result.kind_ = OwnerKind::Package;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventOwnerHandle EventOwnerHandle::from(
    const lib::Owner<card::SkillCard>& owner) {
    if (!owner) return {};
    EventOwnerHandle result;
    result.owner_ = std::make_shared<CardEventOwner>(
        std::static_pointer_cast<card::EffectCard>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(owner);
    result.kind_ = OwnerKind::SkillCard;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventOwnerHandle EventOwnerHandle::from(
    const lib::Owner<card::ItemCard>& owner) {
    if (!owner) return {};
    EventOwnerHandle result;
    result.owner_ = std::make_shared<CardEventOwner>(
        std::static_pointer_cast<card::EffectCard>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(owner);
    result.kind_ = OwnerKind::ItemCard;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventOwnerHandle EventOwnerHandle::from(
    const lib::Owner<card::BeastCard>& owner) {
    if (!owner) return {};
    EventOwnerHandle result;
    result.owner_ = std::make_shared<CardEventOwner>(
        std::static_pointer_cast<card::EffectCard>(owner));
    result.resource_ = std::static_pointer_cast<path::CWXPath>(owner);
    result.kind_ = OwnerKind::BeastCard;
    result.id_ = numberString(owner->id());
    result.name_ = owner->name();
    return result;
}

EventTreeView::EventTreeView(
    event::EventTreeOwner& owner, Parent parent, bool readOnly,
    EventViewCallbacks::Action selectConnectedResource,
    EventViewCallbacks::Predicate canSelectConnectedResource,
    String emptyTreeLabel)
    : owner_(&owner),
      widget_(std::make_shared<lib::TreeView>()),
      read_only_(readOnly),
      empty_tree_label_(std::move(emptyTreeLabel)),
      select_connected_resource_(std::move(selectConnectedResource)),
      can_select_connected_resource_(std::move(canSelectConnectedResource)) {
    if (parent) parent->add(widget_);
    refresh();
}

std::size_t EventTreeView::selectedIndex() const noexcept {
    return selection_.empty() ? std::numeric_limits<std::size_t>::max()
                              : selection_.front();
}

void EventTreeView::refresh() {
    auto nextSelection = selection_;
    nextSelection.erase(
        std::remove_if(nextSelection.begin(), nextSelection.end(),
                       [this](std::size_t index) {
                           return !owner_ || index >= owner_->trees().size();
                       }),
        nextSelection.end());
    if (owner_ && nextSelection.empty() && !owner_->trees().empty()) {
        nextSelection.push_back(0U);
    }

    std::vector<lib::TreeNode> nodes;
    if (owner_) {
        const auto& trees = owner_->trees();
        nodes.reserve(trees.size());
        for (std::size_t index = 0; index < trees.size(); ++index) {
            lib::TreeNode node;
            node.id = static_cast<lib::TreeItemId>(index + 1U);
            if (trees[index]) {
                // C++版ではスタートのない EventTree も接続できる。EventTree::name()
                // を呼ばず、 詳細な EventTreeView と同じ
                // 翻訳済みの代替ラベルで、その行を表示できるようにする。
                const auto& starts = trees[index]->starts();
                node.text = starts.empty() || !starts.front()
                    ? empty_tree_label_ : trees[index]->name();
            }
            node.value = trees[index] ? trees[index]->objectId() : String{};
            node.expanded = true;
            nodes.push_back(std::move(node));
        }
    }
    std::vector<lib::TreeItemId> viewSelection;
    viewSelection.reserve(nextSelection.size());
    for (const auto index : nextSelection)
        viewSelection.push_back(static_cast<lib::TreeItemId>(index + 1U));
    if (!lib::replace_tree_nodes(widget_, std::move(nodes),
                                 std::move(viewSelection)))
        return;
    selection_ = std::move(nextSelection);
    refreshStatusLine();
}

void EventTreeView::refreshStatusLine() {
    const auto count = owner_ ? owner_->trees().size() : 0U;
    status_line_ = std::to_string(count);
}

void EventTreeView::select(std::size_t index) {
    if (!owner_ || index >= owner_->trees().size()) {
        selection_.clear();
        (void)lib::select_tree_items(*widget_, {}, true, false);
        return;
    }
    selection_.assign(1U, index);
    (void)lib::select_tree_items(
        *widget_, {static_cast<lib::TreeItemId>(index + 1U)}, true, false);
}

void EventTreeView::selectCurrentEvent() {
    if (owner_ && !owner_->trees().empty()) select(0U);
}

void EventTreeView::up() {
    if (read_only_ || !canUp()) return;
    const auto index = selectedIndex();
    owner_->swapEventTree(index - 1U, index);
    select(index - 1U);
    refresh();
}

void EventTreeView::down() {
    if (read_only_ || !canDown()) return;
    const auto index = selectedIndex();
    owner_->swapEventTree(index, index + 1U);
    select(index + 1U);
    refresh();
}

void EventTreeView::removeSelection() {
    if (read_only_ || !owner_ || !hasSelection()) return;
    const auto index = selectedIndex();
    if (index >= owner_->trees().size()) return;
    owner_->removeEvent(index);
    if (index >= owner_->trees().size() && index != 0U) select(index - 1U);
    else if (!owner_->trees().empty()) select(index);
    else selection_.clear();
    refresh();
}

void EventTreeView::cloneSelection() {
    if (read_only_ || !owner_ || !hasSelection()) return;
    const auto index = selectedIndex();
    if (index >= owner_->trees().size() || !owner_->trees()[index]) return;
    auto duplicate = owner_->trees()[index]->dup();
    owner_->insert(index + 1U, duplicate);
    select(index + 1U);
    refresh();
}

bool EventTreeView::canUp() const noexcept {
    return !read_only_ && owner_ && hasSelection() && selectedIndex() > 0U &&
           selectedIndex() < owner_->trees().size();
}

bool EventTreeView::canDown() const noexcept {
    return !read_only_ && owner_ && hasSelection() &&
           selectedIndex() + 1U < owner_->trees().size();
}

bool EventTreeView::canSelectConnectedResource() const {
    return can_select_connected_resource_ ? can_select_connected_resource_()
                                           : hasSelection();
}

void EventTreeView::selectConnectedResource() {
    if (canSelectConnectedResource() && select_connected_resource_) {
        select_connected_resource_();
    }
}

EventView::EventView(const ContentInitializerContext& context,
                     event::EventTreeOwner& owner, SummaryPtr summary,
                     Parent parent, UndoManager& undoManager, bool readOnly,
                     EventViewCallbacks callbacks,
                     OperationalHistory operationalHistory)
    : context_(context),
      owner_(&owner),
      summary_(std::move(summary)),
      widget_(std::make_shared<lib::Panel>()),
      undo_(&undoManager),
      read_only_(readOnly),
      callbacks_(std::move(callbacks)),
      tree_view_(owner, widget_, readOnly, callbacks_.selectConnectedResource,
                  callbacks_.canSelectConnectedResource,
                  context.prop
                      ? context.prop->requiredMsgs().eventView.value
                      : String{"イベント"}) {
    widget_->set_layout(std::make_shared<lib::FillLayout>());
    lib::LayoutItem widgetItem;
    widgetItem.minimum = {240.0F, 180.0F};
    widgetItem.grow = 1.0F;
    widgetItem.horizontal = lib::Align::Stretch;
    widgetItem.vertical = lib::Align::Stretch;
    widget_->set_layout_item(widgetItem);
    if (parent) parent->add(widget_);
    widget_->clear();
    rich_eventview::Commons commons;
    commons.services = context.services;
    commons.dialogOwner = context.dialog_owner;
    commons.xmlCopyProvider = [vars = context.vars, life = std::weak_ptr<int>(command_lifetime_)] {
        return !life.expired() && vars && vars->xmlCopy.value;
    };
    commons.summary = summary_;
    commons.refreshToolBar = context.refreshToolBar;
    commons.setStatusLine = [this, life = std::weak_ptr<int>(command_lifetime_)](
        const lib::WidgetPtr&, StringView text) {
        if (life.expired()) return;
        const auto publish = callbacks_.statusChanged;
        if (publish) publish(String(text));
    };
    commons.initialContent = [context = context_, summary = summary_,
        life = std::weak_ptr<int>(command_lifetime_)](const types::CType type) {
        if (life.expired()) return event::ContentPtr{};
        const auto skin = context.findSkin(summary.get());
        if (life.expired()) return event::ContentPtr{};
        const bool legacy = summary && summary->legacy();
        const String version = summary ? summary->dataVersion() : String{};
        const auto defaults = context.editor_props && skin
            ? ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                  context.editor_props->bgImagesDefault(skin->type()))
            : lib::Array<::cwxeditor::cwx::structs::BgImageS>{};
        auto initial = std::make_shared<event::Content>(type);
        bool configured = false;
        if (context.vars) {
            for (const auto& key : {legacy ? String("Classic") : version, String("Basic")}) {
                for (const auto& setting : context.vars->contentInitializers.value) {
                    if (setting.dataVersion != key) continue;
                    const auto found = setting.initializer.find(type);
                    if (found == setting.initializer.end() || !found->second) continue;
                    initial->shallowCopy(*found->second);
                    configured = true;
                    break;
                }
                if (configured) break;
            }
        }
        if (!configured)
            initial = contentinitializer::initial(context, skin.get(), legacy, version, defaults, initial);
        // D版の EventTreeView.initial と同様、独立したカウンターを
        // 使い、 設定された複製と組み込みの既定値の両方でスキン素材の拡張子を補正する。
        if (initial && context.editor_commons && skin) {
            const auto previous_counter = initial->useCounter();
            const auto previous_owner = initial->ucOwner();
            const auto restore_counter = [&] {
                if (previous_counter) initial->setUseCounter(previous_counter, previous_owner);
                else initial->removeUseCounter();
            };
            initial->setUseCounter(std::make_shared<usecounter::UseCounter>(), {});
            try {
                context.editor_commons->updateSkinMaterialsExtension(initial->useCounter(), {}, skin);
            } catch (...) {
                restore_counter();
                throw;
            }
            restore_counter();
        }
        return initial;
    };
    const auto notifications = context_.editor_commons;
    const auto notifications_life = notifications
        ? notifications->lifetimeToken() : std::weak_ptr<const int>{};
    commons.refUseCount.add([notifications, notifications_life,
                              life = std::weak_ptr<int>(command_lifetime_)] {
        if (!life.expired() && notifications && !notifications_life.expired())
            notifications->refUseCount.call();
    });
    commons.convertContent = [context = context_, summary = summary_,
        factory = commons.initialContent,
        life = std::weak_ptr<int>(command_lifetime_), notifications,
        notifications_life](const event::ContentPtr& content,
                            const types::CType type) {
        if (life.expired() || !content) return;
        const auto skin = context.findSkin(summary.get());
        if (life.expired()) return;
        const String version = summary ? summary->dataVersion() : String{};
        const auto defaults = context.editor_props && skin
            ? ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                  context.editor_props->bgImagesDefault(skin->type()))
            : lib::Array<::cwxeditor::cwx::structs::BgImageS>{};
        const auto initial = factory(type);
        if (life.expired() || !initial) return;
        const auto status = ::cwxeditor::cwx::structs::toDialogStatus(
            context.vars ? context.vars->dialogStatus.value : 0);
        const bool had_background = content->detail().use(types::CArg::BgImages);
        const auto tree = content->tree();
        const auto parent = content->parent();
        if (notifications && !notifications_life.expired())
            notifications->delContent.call(content);
        // コメント・プロパティの Close 通知先は、元の親子関係を通知中で
        // も、 ホストを無効化したり選択モデルを置き換えたりする場合がある。
        if (life.expired() || (notifications && notifications_life.expired()) ||
            content->tree() != tree || content->parent() != parent) return;
        content->convertType(type, initial, context.prop, &status, skin.get(),
                             summary ? summary->scenarioPath() : String{}, version);
        if (life.expired()) return;
        if (skin && !had_background && content->detail().use(types::CArg::BgImages))
            content->backgroundImages(::cwxeditor::cwx::skin::createBgImages(*skin, defaults));
        if (content->detail().use(types::CArg::Dialogs) && content->dialogs().empty())
            content->dialogs({std::make_shared<event::SDialog>()});
        if (notifications && !notifications_life.expired())
            notifications->refUseCount.call();
    };
    commons.modelProps = context.prop;
    commons.editorProps = [properties = context.editor_props,
                           life = std::weak_ptr<int>(command_lifetime_)] {
        return life.expired() ? nullptr : properties;
    };
    commons.branchRangeText = [prop=context.editor_props, life=std::weak_ptr<int>(command_lifetime_)](const types::Range range, const String& coupon) {
        return dutils::rangeName(life.expired() ? nullptr : prop,range,coupon);
    };
    commons.branchCastRangesText = [prop=context.editor_props, life=std::weak_ptr<int>(command_lifetime_)](const std::vector<types::CastRange>& ranges) {
        return dutils::castRangesName(life.expired() ? nullptr : prop, ranges);
    };
    commons.branchPreviewText = [context, summary = summary_, life = std::weak_ptr<int>(command_lifetime_)](
        const event::ContentPtr& content, const String& text) {
        if (life.expired() || !context.editor_props || !context.vars ||
            !context.vars->showVariableValuesInEventText.value) return text;
        auto& prop = *context.editor_props;
        messageutils::NameMap names;
        ::cwxeditor::cwx::msgutils::VarGetter flags, steps, variants, sysSteps;
        const auto counter = content ? content->useCounter() : nullptr;
        messageutils::getPreviewValues(prop, summary.get(), counter.get(),
            messageutils::SPCHAR_TEXT, names, flags, steps, variants, sysSteps);
        return ::cwxeditor::cwx::msgutils::simpleFormatMsg(text, flags, steps, variants, sysSteps,
            names, [&prop, &summary](const StringView version) { return prop.isTargetVersion(summary.get(), version); },
            prop.sys().prefixSystemVarName());
    };

    commons.deletingContent = [notifications, notifications_life](
        const event::ContentPtr& content) {
        if (notifications && !notifications_life.expired())
            notifications->delContent.call(content);
    };
    commons.scenarioEventTemplates = [summary = summary_]() {
        // FlagPaneCommons は要素を所有するベクターを返す。
        // メニューは項目を組み立てるときだけテンプレートのフィールドを読む。
        lib::Array<::cwxeditor::cwx::structs::EvTemplate> values;
        if (!summary) return values;
        const auto templates = summary->eventTemplates();
        values.reserve(templates.size());
        for (const auto& value : templates) values.push_back(value);
        return values;
    };
    commons.globalEventTemplates = [vars = context.vars]() {
        return vars ? vars->eventTemplates.value
                    : lib::Array<::cwxeditor::cwx::structs::EvTemplate>{};
    };
    commons.compileEventTemplate =
        [prop = context.prop, summary = summary_, vars = context.vars,
         report = context.errorReporter,
         script_report = context.scriptErrorReporter](StringView source) {
            lib::Array<event::ContentPtr> result;
            script::CompileOption option;
            // 変数のない同期処理でもD版のリンクカードの扱いを維持する。 プレースホルダー付
            // きテンプレートを扱う ScriptVarSetDialog と同じ方針とする。
            option.linkId = (vars && vars->linkCard.value) ||
                            (summary && !summary->legacy());
            try {
                for (const auto& compiled :
                     ::cwxeditor::cwx::script::compile(prop, summary.get(), source, option)) {
                    if (auto value = contentinitializer::ContentInitialValueEditor::fromScript(
                            compiled, prop))
                        result.push_back(std::move(value));
                }
            } catch (const script::CWXScriptException& error) {
                result.clear();
                if (script_report) script_report(error, source, option);
                else if (report) report(scripterrordialog::ScriptErrorDialog::formatError(
                    error, prop, source, option));
            } catch (const std::exception& error) {
                result.clear();
                if (report) report(error.what());
            }
            return result;
        };
    commons.contentName = context.contentName;
    if (!commons.contentName && context.prop) {
        // D版と同様、名前の取得時に親から Props.msgs を解決する。 EventWindow
        // の存続中に CProps::msgs が共有カタログを置き換えること
        // があるため、 以前の Msgs への参照を保持すると無効な参照になる。
        commons.contentName = [prop = context.prop](const types::CType type) {
            return prop->requiredMsgs().contentName(type);
        };
    }
    if (context.prop) {
        // D版と同様、説明も現在有効な Props のカタログから取得する。
        commons.contentDesc = [prop = context.prop](const types::CType type) {
            return prop->requiredMsgs().contentDesc(type);
        };
    }
    commons.contentImage = context.contentImage;
    commons.couponChoicesProvider = [context = context_, summary = summary_,
        life = std::weak_ptr<int>(command_lifetime_)](const event::ContentPtr& content) {
        if (life.expired() || !summary) return std::vector<String>{};
        ::cwxeditor::cwx::editor::gui::dwt::chooser::Commons chooser_common;
        chooser_common.services = context.services;
        ::cwxeditor::cwx::editor::gui::dwt::chooser::ChooserProperties properties;
        properties.editor_commons = context.editor_commons;
        properties.editor_props = context.editor_props;
        if (context.vars) {
            properties.logicalSort = context.vars->logicalSort.value;
            properties.usedCouponToCombo = context.vars->usedCouponToCombo.value;
            properties.useNamesAfterStandard = context.vars->useNamesAfterStandard.value;
            properties.showCurrentValueOnTopAlways =
                context.vars->showCurrentValueOnTopAlways.value;
        }
        properties.couponStandards = [context, summary](
            const ::cwxeditor::cwx::editor::gui::dwt::chooser::CouponComboType type) {
            const auto skin = context.findSkin(summary.get());
            const String legacyName = skin ? skin->legacyName() : String{};
            dutils::EditorContext coupon_context;
            coupon_context.prop = context.editor_props;
            coupon_context.skin = skin.get();
            coupon_context.summary = summary.get();
            return dutils::castCoupons(coupon_context, false, legacyName,
                type == ::cwxeditor::cwx::editor::gui::dwt::chooser::CouponComboType::GetLose,
                false);
        };
        using CouponComboType =
            ::cwxeditor::cwx::editor::gui::dwt::chooser::CouponComboType;
        ::cwxeditor::cwx::editor::gui::dwt::chooser::setCouponFilter(properties,
            [prop = context.editor_props](StringView value) {
                return prop && prop->sys().isCouponType(
                    String(value), features::CouponType::System);
            });
        auto values = ::cwxeditor::cwx::editor::gui::dwt::chooser::allCoupons(
            chooser_common, summary, CouponComboType::AllCoupons, properties);
        return ::cwxeditor::cwx::editor::gui::dwt::chooser::addInitValue(
            chooser_common, std::move(values), content ? content->name() : String{},
            properties);
    };
    commons.couponChangesBinding = [publisher = context.editor_commons](lib::Action refresh) {
        return ::cwxeditor::cwx::editor::gui::dwt::chooser::watchCouponChanges(
            publisher, std::move(refresh));
    };
    // このビュー内のペイン（ローカル変数の表など）に使う、 ポップアップの
    // ラベルとアイコン（D版の createMenuItem に相当）。
    if (context.editor_props) {
        commons.menuText = [prop = context.editor_props](const types::MenuID id) {
            return prop->buildMenu(id);
        };
        commons.menuImage = [prop = context.editor_props](const types::MenuID id) {
            return prop->images().menu(id);
        };
    }
    commons.eventViewImage = context.eventViewImage;
    commons.returnToScene = context.returnToScene;
    commons.findStartUsers = context.findStartUsers;
    commons.findLocalVariableUsers = context.findLocalVariableUsers;
    if (context.vars && context.vars->showDuplicateViewInToolBar.value && callbacks_.openDuplicate) {
        commons.openDuplicate = [this, life = std::weak_ptr<int>(command_lifetime_)] {
            if (!life.expired()) openDuplicate();
        };
        if (context.editor_props)
            commons.duplicateViewText = context.editor_props->buildTool(types::MenuID::EditEventDup);
    }
    if (context.editor_props) {
        commons.setupSelectionEditor = [this](
            const std::shared_ptr<lib::TextEdit>& editor,
            const std::shared_ptr<lib::ComboBox>& choice) -> std::shared_ptr<void> {
            if (!editor) return {};
            dmenu::Commons menu_comm;
            menu_comm.services = context_.services;
            menu_comm.undoMaxEtc = static_cast<std::size_t>(
                std::max<std::int32_t>(0, context_.editor_props->var().undoMaxEtc));
            menu_comm.presentation.menuText = [prop = context_.editor_props](auto id) {
                return prop->buildMenu(id);
            };
            menu_comm.presentation.menuImage = [prop = context_.editor_props](auto id) {
                return prop->images().menu(id);
            };
            auto binding = dmenu::createTextMenu(
                menu_comm, choice ? dmenu::TMM(choice) : dmenu::TMM(editor));
            if (!binding) return {};

            ::cwxeditor::cwx::usecounter::CounterOwner counter;
            if (rich_view_) {
                if (const auto selected = rich_view_->selectionContent())
                    counter = selected->useCounter();
                if (!counter)
                    if (const auto tree = rich_view_->selectionEventTree())
                        counter = tree->useCounter();
            }
            messageutils::MessageContext message;
            message.services = context_.services;
            message.props = context_.editor_props;
            message.summary = summary_.get();
            message.summary_owner = summary_;
            message.editor_commons = context_.editor_commons;
            message.use_counter = counter.get();
            message.use_counter_owner = counter;
            (void)messageutils::setupSPCharsMenu(
                message, summary_.get(), [this] { return context_.findSkin(summary_.get()); },
                counter.get(), binding->menu(), false, true, [] { return true; },
                [weak = std::weak_ptr<dmenu::TextMenuModify>(binding)](
                    const StringView value) {
                    if (const auto owner = weak.lock()) {
                        auto text = owner->text();
                        auto next = text.getText();
                        const auto selection = text.getSelection();
                        const auto start = std::min(
                            next.size(), static_cast<std::size_t>(
                                std::max(0.0F, selection.x)));
                        const auto end = std::clamp(
                            static_cast<std::size_t>(std::max(0.0F, selection.y)),
                            start, next.size());
                        next.replace(start, end - start, value);
                        text.setText(next);
                        const auto caret = static_cast<float>(start + value.size());
                        text.setSelection({caret, caret});
                    }
                });
            return std::static_pointer_cast<void>(binding);
        };
    }
    if (context.editor_props)
        commons.warningImage = context.editor_props->images().load(
            StringView{"warning.png"});
    if (context.prop) {
        const auto warning_skin = context.findSkin(summary_.get());
        const bool live_warning_skin = !lib::editor_window_host_options().all_mode;
        if (warning_skin || live_warning_skin) {
            commons.warnings = [prop = context.prop,
                                initial_skin = warning_skin, context, live_warning_skin,
                                life = std::weak_ptr<int>(command_lifetime_),
                                summary = summary_,
                                editor_props = context.editor_props](
                                   const event::CWXPath& value) {
                // D版の summSkin と同様、警告の判定ごとに現在の通
                // 常スキンを解決する。 明示的に固定された解決処理は固定した
                // ままにし、ALL の既存のスナップショット方式も維持する。
                if (live_warning_skin && life.expired()) return ::cwxeditor::cwx::warning::WarningList{};
                const auto skin = live_warning_skin ? context.findSkin(summary.get()) : initial_skin;
                if (!skin || (live_warning_skin && life.expired()))
                    return ::cwxeditor::cwx::warning::WarningList{};
                const bool classic = summary && summary->legacy();
                const auto data_version = summary
                    ? summary->dataVersion()
                    : ::cwxeditor::cwx::versioninfo::LATEST_VERSION;
                const auto target_version = editor_props
                    ? String(editor_props->var().targetVersion.value)
                    : String{};
                return ::cwxeditor::cwx::warning::warnings(
                    *prop, *skin, summary.get(), value, classic,
                    data_version, target_version);
            };
        }
    }
    if (context.prop && context.editor_props) {
        commons.ignitionWarnings = [prop=context.prop, editor_props=context.editor_props,
                                    summary=summary_, battle=dynamic_cast<const area::Battle*>(owner_),
                                    scene_owner=(dynamic_cast<const area::Area*>(owner_) || dynamic_cast<const area::Battle*>(owner_))](const event::EventTree& tree,
                                        const eventview::FireCondition& fire) {
            lib::Array<String> result;
            if(!scene_owner) return result;
            const auto& msgs=prop->requiredMsgs();
            const bool version150=editor_props->targetVersion(summary.get(),"1.50");
            using Kind=eventview::FireKind;
            if(fire.kind==Kind::KeyCode) {
                const auto code=prop->sys().convFireKeyCode(fire.keyCode);
                result=::cwxeditor::cwx::warning::sjisWarnings(*prop,summary.get(),code,msgs.keyCode.value);
                if(code=="MatchingType=All" && !tree.keyCodes().empty() &&
                   prop->sys().convFireKeyCode(tree.keyCodes().front())==code)
                    result.push_back(msgs.searchErrorKeyCodeMatchingAll.value);
                const bool timing_owner=dynamic_cast<const area::EnemyCard*>(tree.owner()) ||
                    dynamic_cast<const area::PlayerCardEvents*>(tree.owner());
                if(timing_owner && fire.keyCode.kind==types::FKCKind::HasNot && !version150)
                    result.push_back(msgs.warningHasNotKeyCode.value);
            } else if(fire.kind==Kind::Escape) {
                if(battle && !battle->possibleToRunAway())
                    result.push_back(msgs.warningNoIgniteRunAway.value);
            } else if(fire.kind==Kind::EveryRound && !version150)
                result.push_back(msgs.warningEveryRound.value);
            else if(fire.kind==Kind::Round0 && !version150)
                result.push_back(msgs.warningRound0.value);
            else if(fire.kind==Kind::RoundEnd && !editor_props->isTargetVersion(summary.get(),"4"))
                result.push_back(msgs.warningRoundEnd.value);
            return result;
        };
    }
    // D版の EventTreeView と同様、ビューとの境界で変換する。移植版
    // の標準処理により、 MainWindow が従来のコールバックを渡さない場合
    // も変換できる。 渡されたコールバックはホストによる明示的な上書きとして扱う。
    commons.toScript = [this] { toScript(); };
    commons.toScriptOne = [this] { toScriptOne(); };
    commons.toScriptAll = [this] { toScriptAll(); };
    commons.startToPackage = [this] { startToPackage(); };
    commons.eventToPackage = [this] { eventToPackage(); };
    commons.addManyRounds = [this] { addManyRounds(); };
    // D版と同様、右側の EventTree メニューは選択された Content を対象にする。 左側の所有者へ
    // の移動は、別の EventView::selectConnectedResource() 操作として残す。
    commons.selectConnectedResource = [this] { selectConnectedResource(); };
    commons.selectConnectedContentResource =
        [this] { selectConnectedContentResource(); };
    commons.fireKeyCodes = [editor_props = context.editor_props,
                            summary = summary_]() {
        lib::Array<String> standard;
        if (editor_props)
            standard = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                editor_props->standardKeyCodes(
                    summary ? summary->type() : StringView{}));
        lib::Array<String> used;
        if (summary) {
            if (const auto counter = summary->useCounter()) {
                for (const auto& id :
                     counter->template keys<::cwxeditor::cwx::usecounter::KeyCodeId>())
                    if (std::find(standard.begin(), standard.end(), id.id) == standard.end())
                        used.push_back(id.id);
            }
        }
        // D版の chooser.createKeyCodeCombo と同様、使用済みの名前を辞書順に並べ、 useNamesAfterStandard
        // を反映してから、createKCCombo が先頭を選択する。
        std::sort(used.begin(), used.end());
        const bool standard_first = editor_props &&
            editor_props->var().useNamesAfterStandard.value;
        if (standard_first) {
            if (!standard.empty() && !used.empty()) standard.push_back({});
            standard.insert(standard.end(), used.begin(), used.end());
            return standard;
        }
        if (!used.empty()) used.push_back({});
        used.insert(used.end(), standard.begin(), standard.end());
        return used;
    };
    if (context_.prop) {
        commons.convertFireKeyCode = [this](const StringView keyCode,
                                             const types::FKCKind kind) {
            return context_.prop->sys().convFireKeyCode(keyCode, kind);
        };
    }
    commons.canStartToPackage = [this] { return canStartToPackage(); };
    commons.canEventToPackage = [this] { return canEventToPackage(); };
    commons.canAddManyRounds = [this] { return canAddManyRounds(); };
    commons.canSelectConnectedResource =
        [this] { return canSelectConnectedResource(); };
    commons.canSelectConnectedContentResource =
        [this] { return canSelectConnectedContentResource(); };
    commons.contentText = [resolver = context.contentText,
                           initializer = context_,
                           editor_props = context.editor_props,
                           services = context.services, summary = summary_,
                           viewLife = std::weak_ptr<int>(command_lifetime_)](
                              const event::Content& content) {
        if (resolver) return resolver(content, summary.get());
        if (viewLife.expired() || !editor_props) return String{};
        // D版の EventView と同様、現在の summSkin で詳細テキスト
        // を整形する。 ここでも同じ現在のスキンを解決し、素材を含む行でシナリオ内の
        // 素材と存在しないパスを区別する。 解決中に EventWindow が無効
        // 化される場合があるため、その後は借用した Props ポインタを使わない。
        const auto skin = initializer.findSkin(summary.get());
        if (viewLife.expired()) return String{};
        dutils::EditorContext editor_context;
        editor_context.prop = editor_props;
        editor_context.skin = skin.get();
        editor_context.summary = summary.get();
        editor_context.services = services;
        // D版の EventEditor と同様、右側の枠には dutils.contentText()
        // をそのまま表示する。 先頭を contentName()
        // に置き換えると、最初の空白より前の引数 （フラグ・ステップ名
        // 、移動先、条件など）が失われ、誤解を招く短い見出しになる。
        return dutils::contentText(editor_context, &content, summary.get());
    };
    if (context.prop) commons.branchLocationText = [prop = context.prop](bool battle,
            std::optional<std::uint64_t> id, std::optional<String> name) {
        const auto& msgs = prop->requiredMsgs();
        if (!id) return utils::tryFormat(battle ? msgs.branchBattle.value : msgs.branchArea.value, msgs.etc.value);
        if (!name) return utils::tryFormat(battle ? msgs.noBattle.value : msgs.noArea.value, std::to_string(*id));
        return utils::tryFormat(battle ? msgs.branchBattleWithId.value : msgs.branchAreaWithId.value,
            std::to_string(*id), *name);
    };
    commons.editScenarioEventTemplates =
        [this, editorProps = context.editor_props, source = std::weak_ptr<Summary>(summary_),
         owner = context.dialog_owner, services = context.services,
         editorCommons = context.editor_commons,
         commonLife = context.editor_commons ? context.editor_commons->lifetimeToken() : std::weak_ptr<const int>{},
         viewLife = std::weak_ptr<int>(command_lifetime_)] {
            const auto summary = source.lock();
            if (viewLife.expired() || !editorProps || !summary) return;
            if (editorCommons) {
                if (commonLife.expired()) return;
                if (editorCommons->summary() == summary) {
                    (void)editorCommons->editScEventTemplate(owner);
                    return;
                }
            }
            if (event_template_dialog_) {
                if (event_template_dialog_->dialog() &&
                    event_template_dialog_->dialog()->visible()) {
                    event_template_dialog_->active();
                    return;
                }
                event_template_dialog_.reset();
            }
            ::cwxeditor::cwx::editor::gui::dwt::smalldialogs::Callbacks callbacks;
            if (editorCommons) {
                callbacks.scenarioNameChanged = [editorCommons] {
                    editorCommons->refEventTemplates.call();
                };
                callbacks.subscribeScenarioName = [editorCommons](lib::Action handler) {
                    const auto token = editorCommons->refScenarioName.add(std::move(handler));
                    return lib::Action([editorCommons, token] {
                        editorCommons->refScenarioName.remove(token);
                    });
                };
                callbacks.subscribeScenario = [editorCommons](
                    std::function<void(SummaryPtr)> handler) {
                    const auto token = editorCommons->refScenario.add(std::move(handler));
                    return lib::Action([editorCommons, token] {
                        editorCommons->refScenario.remove(token);
                    });
                };
            }
            auto dialog = std::make_shared<
                ::cwxeditor::cwx::editor::gui::dwt::smalldialogs::EventTemplateDialog>(
                *editorProps, summary.get(), owner, summary->eventTemplates(),
                std::move(callbacks),
                services);
            event_template_dialog_ = dialog;
            (void)dialog->open();
        };
    if (context.editor_props) {
        const auto templatesText =
            context.editor_props->buildTool(types::MenuID::EvTemplates);
        if (!templatesText.empty()) commons.eventTemplatesText = templatesText;
        const auto editText =
            context.editor_props->buildMenu(types::MenuID::EvTemplatesOfScenario);
        if (!editText.empty()) commons.editScenarioEventTemplatesText = editText;
    }
    rich_eventview::Props properties;
    if (context.editor_props) {
        const auto& messages = context.editor_props->requiredMsgs();
        properties.msgs.flag = messages.flag.value;
        properties.msgs.step = messages.step.value;
        properties.msgs.variant = messages.variant.value;
        properties.msgs.statusFormat = messages.flagStatus.value;
        properties.msgs.statusSelectedFormat = messages.flagStatusSel.value;
        properties.msgs.enterTree = messages.enterTree.value;
        properties.msgs.selectTree = messages.selectTree.value;
        properties.msgs.deadTree = messages.deadTree.value;
        properties.msgs.victoryTree = messages.victoryTree.value;
        properties.msgs.packageTree = messages.packageTree.value;
        properties.msgs.playerCard = messages.playerCard.value;
        properties.msgs.useTree = messages.useTree.value;
        properties.msgs.escapeTree = messages.escapeTree.value;
        properties.msgs.loseTree = messages.loseTree.value;
        properties.msgs.everyRoundTree = messages.everyRoundTree.value;
        properties.msgs.roundEndTree = messages.roundEndTree.value;
        properties.msgs.round0Tree = messages.round0Tree.value;
        properties.msgs.keyCodeTree = messages.keyCodeTree.value;
        properties.msgs.roundTree = messages.roundTree.value;
        auto* const editor_props = context.editor_props;
        const auto catalog_lifetime = editor_props->lifetimeToken();
        const auto fallback = ::cwxeditor::cwx::editor::gui::dwt::flagspane::EventTreeMessages{
            properties.msgs.enterTree, properties.msgs.selectTree,
            properties.msgs.deadTree, properties.msgs.victoryTree,
            properties.msgs.packageTree, properties.msgs.playerCard,
            properties.msgs.useTree, properties.msgs.escapeTree,
            properties.msgs.loseTree, properties.msgs.everyRoundTree,
            properties.msgs.roundEndTree, properties.msgs.round0Tree,
            properties.msgs.keyCodeTree, properties.msgs.roundTree};
        properties.eventTreeMessagesProvider =
            [editor_props, catalog_lifetime, fallback] {
                if (catalog_lifetime.expired()) return fallback;
                const auto current = editor_props->msgs();
                if (!current) return fallback;
                return ::cwxeditor::cwx::editor::gui::dwt::flagspane::EventTreeMessages{
                    current->enterTree.value, current->selectTree.value,
                    current->deadTree.value, current->victoryTree.value,
                    current->packageTree.value, current->playerCard.value,
                    current->useTree.value, current->escapeTree.value,
                    current->loseTree.value, current->everyRoundTree.value,
                    current->roundEndTree.value, current->round0Tree.value,
                    current->keyCodeTree.value, current->roundTree.value};
            };
    }
        properties.etc.undoMaxMainView = configuredUndoMax(context);
    if (context.vars) {
        properties.etc.startIncrementalSearchWhenKeyDown = context.vars->startIncrementalSearchWhenKeyDown.value;
        const auto* vars = context.vars;
        const std::weak_ptr<int> life = command_lifetime_;
        properties.etc.startIncrementalSearchWhenKeyDownProvider = [vars, life] {
            return !life.expired() && vars && vars->startIncrementalSearchWhenKeyDown.value;
        };
        properties.etc.incrementalSearchBoxWidth = context.vars->incrementalSearchBoxWidth.value;
        properties.etc.incrementalSearchType = context.vars->incrementalSearchType.value;
        properties.etc.storeIncrementalSearchType =
            [vars = context.vars, life = std::weak_ptr<int>(command_lifetime_)](std::int32_t type) {
                if (!life.expired()) vars->incrementalSearchType.value = type;
            };
        properties.etc.undoMaxEtcProvider =
            [vars = context.vars, life = std::weak_ptr<int>(command_lifetime_)] {
            if (life.expired()) return std::size_t{0};
            return static_cast<std::size_t>(
                std::max<std::int32_t>(0, vars->undoMaxEtc.value));
        };
        properties.etc.undoMaxEtc = properties.etc.undoMaxEtcProvider();
        commons.standardSelections =
            ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                context.vars->standardSelections.value);
        properties.etc.editSelectionWithCombo =
            context.vars->editSelectionWithCombo.value;
        properties.etc.selectionWidth = static_cast<float>(
            std::max(1, context.vars->selectionWidth.value));
        properties.etc.couponWidth = static_cast<float>(
            std::max(1, context.vars->couponWidth.value));
        properties.etc.editTriggerQuick = context.vars->editTriggerType.value ==
            static_cast<std::int32_t>(structs::EditTrigger::Quick);
        copyContentsPaletteProperties(properties.etc, *context.vars);
        properties.etc.adjustContentName = context.vars->adjustContentName.value;
        properties.etc.showEventContentDescription =
            context.vars->showEventContentDescription.value;
        properties.etc.eventGraphDetailWidth = static_cast<float>(
            std::max<std::int32_t>(120, context.vars->detailAreaWidth.value));
        properties.etc.storeEventGraphDetailWidth =
            [vars = context.vars, life = std::weak_ptr<int>(command_lifetime_)](
                const std::int32_t value) {
                if (!life.expired()) vars->detailAreaWidth.value = value;
            };
        properties.etc.specifySelectedEventTree =
            context.vars->specifySelectedEventTree.value;
        properties.etc.drawContentTreeLine = context.vars->drawContentTreeLine.value;
        properties.etc.drawCountOfUseOfStart = context.vars->drawCountOfUseOfStart.value;
        properties.etc.showTerminalMark = context.vars->showTerminalMark.value;
        properties.etc.forceIndentBranchContent = context.vars->forceIndentBranchContent.value;
        properties.etc.showTargetStartLineNumber = context.vars->showTargetStartLineNumber.value;
        properties.etc.drawContentWarnings =
            context.vars->drawContentWarnings.value;
        properties.etc.drawContentIconOnDetail =
            context.vars->drawContentIconOnDetail.value;
        properties.etc.limitForNumberOfVariant =
            context.vars->limitForNumberOfVariant.value;
        properties.etc.branchRandomFailureFormat =
            context.vars->branchRandomFailureFormat.value;
        properties.etc.showInputGuide = context.vars->showInputGuide.value;
        properties.etc.useCurrentStartName = context.vars->useCurrentStartName.value;
        properties.etc.contentConversionGroups.assign(
            context.vars->contentConversionGroups.value.begin(),
            context.vars->contentConversionGroups.value.end());
        properties.etc.commentBoxDistance =
            context.vars->commentBoxDistance.value;
        properties.etc.warningImageWidth =
            context.vars->warningImageWidth.value;
        const auto warningColor = context.vars->warningImageColor.value;
        properties.etc.warningImageColor = {
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, warningColor.r)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, warningColor.g)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, warningColor.b)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, warningColor.a))};
        properties.etc.toolTipWidth = context.vars->toolTipWidth.value;
        properties.etc.showEventToolBar = context.vars->showEventToolBar.value;
        properties.etc.localVariablesSashL = context.vars->localVariablesSashL.value;
        properties.etc.localVariablesSashR = context.vars->localVariablesSashR.value;
        properties.etc.storeLocalVariablesSash = [vars = context.vars](
            const std::int32_t left, const std::int32_t right) {
            if (!vars) return;
            vars->localVariablesSashL.value = left;
            vars->localVariablesSashR.value = right;
        };
        if (context.prop) {
            properties.etc.inputGuideText = context.prop->sys().evtChildOK({});
            // コンストラクターで summary 引数を summary_ に移動済み
            // 。 単独エディタの初期案内文には、現在のメンバーから取得した値を使う。
            const auto* guide_summary = lib::editor_window_host_options().all_mode
                ? summary.get() : summary_.get();
            if (context.skinResolver && guide_summary) {
                if (const auto skin = context.skinResolver(
                        guide_summary, guide_summary->type(), guide_summary->skinName())) {
                    const auto value = skin->evtChildOK();
                    if (!value.empty()) properties.etc.inputGuideText = value;
                }
            }
        }
    }
    properties.var.etc.undoMaxMainView = properties.etc.undoMaxMainView;
    properties.synchronize();
    if (!operationalHistory) {
        operationalHistory = std::make_shared<OperationalHistoryState>();
        operationalHistory->manager = std::make_shared<rich_eventview::UndoManager>(configuredUndoMax(context));
        owned_rich_undo_ = operationalHistory;
    }
    rich_undo_ = operationalHistory;
    ::cwxeditor::cwx::editor::gui::dwt::chooser::ChooserProperties selection_properties;
    selection_properties.editor_props = context_.editor_props;
    selection_properties.editor_commons = context_.editor_commons;
    selection_properties.previewSkin = [context = context_, summary = summary_,
        life = std::weak_ptr<int>(command_lifetime_)] {
        if (life.expired()) return ::cwxeditor::cwx::skin::Skin::Ptr{};
        return context.findSkin(summary.get());
    };
    commons.standardSelectionsProvider = [selection_properties,
        life = std::weak_ptr<int>(command_lifetime_)] {
        if (life.expired()) return lib::Array<String>{};
        return ::cwxeditor::cwx::editor::gui::dwt::chooser::selectionStandards(
            selection_properties);
    };
    commons.selectionChangesBinding = [publisher = context_.editor_commons](lib::Action refresh) {
        return ::cwxeditor::cwx::editor::gui::dwt::chooser::watchSelectionChanges(
            publisher, std::move(refresh));
    };
    rich_view_ = std::make_unique<rich_eventview::EventView>(
        std::move(commons), std::move(properties), owner, *operationalHistory->manager, readOnly);
    rich_view_->setBeforeFireEdit([this, life = std::weak_ptr<int>(command_lifetime_)] {
        return !life.expired() && finishScriptEdits(
            *this, rich_undo_.lock(), context_.editor_commons,
            commons::Commons::canonicalEventOwner(owner_), life);
    });
    rich_view_->setCancelSameOwnerEditors([this, life = std::weak_ptr<int>(command_lifetime_)] {
        return !life.expired() && cancelScriptEdits(*this, rich_undo_.lock(), life);
    });
    rich_view_->setHistoryReplayCoordinator(
        [this, life = std::weak_ptr<int>(command_lifetime_)](lib::Action replay) {
            if (life.expired()) return;
            runHistoryReplayCommand(std::move(replay));
        });
    rich_view_->cardPreviewResolvers(
        [context = context_, summary = summary_, life = std::weak_ptr<int>(command_lifetime_)] {
            if (life.expired()) return skin::Skin::Ptr{};
            return context.findSkin(summary.get());
        },
        [notifications, notifications_life, owner = &owner,
         life = std::weak_ptr<int>(command_lifetime_)] {
            return !life.expired() && notifications && !notifications_life.expired() &&
                notifications->battlePreviewDebugMode(owner);
        });
    // D版の deleteEvent と同様、削除するツリーのモードレスのプロパティ編集を強制的にキャンセルする。
    rich_view_->modelChanged.add([this, life=std::weak_ptr<int>(command_lifetime_)] {
        if (life.expired()) return;
        refreshCommentTitles(rich_view_.get(), left_comment_dialogs_, context_.prop, summary_);
        std::vector<const event::EventTree*> stale;
        for (const auto& [key, dialog] : tree_dialogs_)
            if (dialog && !dialog->pendingCreation() && !dialog->eventTreePtr()->owner()) stale.push_back(key);
        for (const auto* key : stale) {
            const auto found=tree_dialogs_.find(key);
            if (found==tree_dialogs_.end()) continue;
            const auto dialog=found->second;
            tree_dialogs_.erase(found);
            dialog->close();
            if (life.expired()) return;
        }
    });
    rich_view_->setEditAction([this] { edit(); });
    rich_view_->setAutoOpenContentAction([this](const auto& content,
                                                const auto& parent) {
        return openContentDialog(content, parent, true);
    });
    rich_view_->setScriptTemplateResolver([this](const StringView source,
                                                  const bool tryInsert,
                                                  const bool createEvent) {
        if (script_var_dialog_) {
            script_var_dialog_->active();
            return true;
        }
        if (!context_.prop || !context_.editor_props || !summary_ || !rich_view_ ||
            source.empty()) return false;

        String scriptText(source);
        script::CompileOption option;
        option.linkId = (context_.vars && context_.vars->linkCard.value) ||
                        !summary_->legacy();
        script::CWXScript compiler(context_.prop, summary_.get(), scriptText);
        lib::Array<String> variables;
        try {
            variables = compiler.eatEmptyVars(scriptText, option);
        } catch (const script::CWXScriptException& error) {
            if (context_.scriptErrorReporter)
                context_.scriptErrorReporter(error, source, option);
            else if (context_.errorReporter)
                context_.errorReporter(scripterrordialog::ScriptErrorDialog::formatError(
                    error, context_.prop, source, option));
            return true;
        }
        // テンプレートに未解決の値がなければ、既存の同期処理を維持
        // する。 コンパイルと挿入は一つの取り消し履歴にまとめる。
        if (variables.empty()) return false;

        SkinPtr skin;
        if (context_.skinResolver)
            skin = context_.skinResolver(summary_.get(), summary_->type(),
                                         summary_->skinName());
        smalldialogs::Callbacks callbacks;
        callbacks.services = context_.services;
        // D版の EventTreeView と同様、現在の EventTree の useCounter
        // を ScriptVarSetDialog に渡し、 カードのローカル変数を利用できるようにする。
        auto variable_counter = summary_->useCounter();
        if (const auto displayed_tree = rich_view_->displayedEventTree()) {
            if (const auto tree_counter = displayed_tree->useCounter())
                variable_counter = tree_counter;
        }
        auto dialog = std::make_shared<smalldialogs::ScriptVarSetDialog>(
            *context_.editor_props, summary_.get(), std::move(variable_counter),
            context_.dialog_owner, variables, scriptText, String(source), option,
            std::move(callbacks), context_.services, std::move(skin));
        const std::weak_ptr<smalldialogs::ScriptVarSetDialog> weak = dialog;
        const auto delivered = std::make_shared<bool>(false);
        dialog->appliedEvent.push_back([this, weak, delivered, tryInsert, createEvent] {
            if (*delivered || !rich_view_) return;
            const auto sourceDialog = weak.lock();
            if (!sourceDialog) return;
            std::vector<event::ContentPtr> contents;
            for (const auto& compiled : sourceDialog->contents()) {
                if (auto value = contentinitializer::ContentInitialValueEditor::fromScript(
                        compiled, context_.prop))
                    contents.push_back(std::move(value));
            }
            if (contents.empty()) return;
            *delivered = rich_view_->pasteCompiledTemplateContents(
                std::move(contents), tryInsert, createEvent);
        });
        dialog->closeEvent.push_back([this, weak] {
            const auto closing = weak.lock();
            if (closing && script_var_dialog_ == closing) script_var_dialog_.reset();
        });
        script_var_dialog_ = std::move(dialog);
        (void)script_var_dialog_->open();
        return true;
    });
    rich_view_->setCreateEventAction([this] { createEvent(); });
    rich_view_->setWriteCommentAction([this] { writeComment(); });
    (void)rich_view_->contentToolStateChanged.add(
        [this](const rich_eventview::ContentToolState& state) {
            if (context_.vars) {
                context_.vars->contentsAutoOpen = state.auto_open;
                context_.vars->contentsInsertFirst = state.insert_first;
                context_.vars->contentsPutMode = state.put_mode;
            }
            if (!context_.editor_commons) return;
            const auto mode = state.put_mode == 0 ? types::MenuID::PutQuick
                            : state.put_mode == 2 ? types::MenuID::PutContinue
                                                  : types::MenuID::PutSelect;
            context_.editor_commons->selContentTool.call(
                this, state.arrow, state.type, mode, state.auto_open,
                state.insert_first);
        });
    if (context_.editor_commons) {
        auto* const editor_commons = context_.editor_commons;
        const auto connection = editor_commons->selContentTool.addSender(
            [this](void* sender, const bool arrow, const types::CType type,
                   const types::MenuID mode, const bool auto_open,
                   const bool insert_first) {
                if (sender == this || !rich_view_) return;
                const auto put_mode = mode == types::MenuID::PutQuick ? 0
                                    : mode == types::MenuID::PutContinue ? 2 : 1;
                rich_view_->applyContentToolState(
                    {arrow, type, put_mode, auto_open, insert_first});
            });
        content_tool_disconnect_ = [editor_commons, connection, life=editor_commons->lifetimeToken()] {
            if (!life.expired()) editor_commons->selContentTool.remove(connection);
        };
    }
    // D版と同様、ユーザー設定はシナリオデータや取り消し履歴から独立して扱う。
    if (context_.vars) rich_view_->applyDisplayPreferences(
        context_.vars->showEventTreeDetail.value,
        context_.vars->showEventTreeLineNumber.value,
        context_.vars->eventTreeSlope.value,
        configuredEventTreeSlopeMaximum(context_.vars));
    const std::weak_ptr<int> settingsLife = command_lifetime_;
    if (!lib::editor_window_host_options().all_mode) {
        // D版の EventTreeView::refresh と同様、表示する EventTree
        // が変わったときだけ、 右ペインのコンテント編集・コメントを閉じる。左側のダイアログは残す。
        (void)rich_view_->selectionChanged.add(
            [this, settingsLife,
             observed = std::make_shared<std::weak_ptr<event::EventTree>>(
                 rich_view_->displayedEventTree())]() {
                if (settingsLife.expired() || !rich_view_) return;
                const auto current = rich_view_->displayedEventTree();
                if (observed->lock() == current) return;
                *observed = current;
                if (closing_active_dialog_) return;
                std::vector<contentinitializer::EventDialogPtr> editors;
                editors.reserve(content_dialogs_.size());
                for (const auto& [key, dialog] : content_dialogs_) {
                    (void)key;
                    if (dialog) editors.push_back(dialog);
                }
                closing_active_dialog_ = true;
                struct ResetClosing {
                    bool* value;
                    std::weak_ptr<int> life;
                    ~ResetClosing() { if (!life.expired()) *value = false; }
                } reset_closing{&closing_active_dialog_, settingsLife};
                for (const auto& dialog : editors) {
                    dialog->forceCancel();
                    if (settingsLife.expired()) return;
                }
                closeContentCommentDialogs();
                if (settingsLife.expired()) return;
            });
    }
    // 選択やモデルの更新が終わってから StatusChanged を通知
    // する。 外部ホストのコールバックが、このビューを閉じる場合がある。
    (void)rich_view_->statusChanged.add([this, settingsLife] {
        if (settingsLife.expired() || !rich_view_) return;
        const auto publish = callbacks_.statusChanged;
        const auto text = rich_view_->statusLine();
        if (publish) publish(text);
    });
    (void)rich_view_->contentsOrderChanged.add(
        [this, settingsLife](const std::vector<std::int32_t> order) {
            if (!settingsLife.expired() && context_.vars) context_.vars->contentsOrder = order;
        });
    (void)rich_view_->detailSettingChanged.add([this, settingsLife](const bool value) {
        if (!settingsLife.expired() && context_.vars) context_.vars->showEventTreeDetail = value;
    });
    (void)rich_view_->lineNumberSettingChanged.add([this, settingsLife](const bool value) {
        if (!settingsLife.expired() && context_.vars) context_.vars->showEventTreeLineNumber = value;
    });
    (void)rich_view_->slopeSettingChanged.add([this, settingsLife](const int value) {
        if (settingsLife.expired()) return;
        if (context_.vars) context_.vars->eventTreeSlope = value;
        if (context_.editor_commons) context_.editor_commons->refEventTreeSlope.call(this, value);
    });
    if (auto* shared = context_.editor_commons) {
        const auto styleConnection = shared->refEventEditorStyle.add([this, settingsLife] {
            if (!settingsLife.expired() && rich_view_)
                rich_view_->eventEditorStyleChanged();
        });
        event_editor_style_disconnect_ = [shared, styleConnection,
            life = shared->lifetimeToken()] {
                if (!life.expired()) shared->refEventEditorStyle.remove(styleConnection);
            };
        const auto connection = shared->refEventTreeSlope.addSender(
            [this, settingsLife](void* sender, const int value) {
                if (!settingsLife.expired() && sender != this && rich_view_)
                    rich_view_->applyEventTreeSlope(value);
            });
        event_slope_disconnect_ = [shared, connection, life = shared->lifetimeToken()] {
            if (!life.expired()) shared->refEventTreeSlope.remove(connection);
        };
        const auto previewConnection = shared->refPreviewValues.add([this, settingsLife] {
            if (!settingsLife.expired() && rich_view_) rich_view_->refresh();
        });
        preview_values_disconnect_ = [shared, previewConnection, life = shared->lifetimeToken()] {
            if (!life.expired()) shared->refPreviewValues.remove(previewConnection);
        };
        const auto bindVariablePreview = [this, shared](auto& signal, auto callback) {
            const auto token = signal.add(std::move(callback));
            scene_notifications_disconnect_.push_back(
                [borrowed = &signal, token, life = shared->lifetimeToken()] {
                    if (!life.expired()) borrowed->remove(token);
                });
        };
        const auto refreshVariablePreview = [this, settingsLife](const auto&...) {
            if (!settingsLife.expired() && rich_view_)
                rich_view_->commons().refSpecialPreview.call();
        };
        const auto refreshResourcePath = [this, settingsLife](const auto&...) {
            if (settingsLife.expired() || !rich_view_) return;
            rich_view_->commons().refSpecialPreview.call();
            if (!settingsLife.expired() && rich_view_)
                rich_view_->refreshStatusLine();
        };
        bindVariablePreview(shared->refPreviewValues, refreshVariablePreview);
        bindVariablePreview(shared->refPath, refreshResourcePath);
        bindVariablePreview(shared->refPaths, refreshResourcePath);
        bindVariablePreview(shared->delPaths, refreshResourcePath);
        bindVariablePreview(shared->replPath, refreshResourcePath);
        if (!lib::editor_window_host_options().all_mode) {
            // D版と同様、シナリオのスキン変更時は、開いているイベント行も更新する。
            bindVariablePreview(shared->refSkin, [this, settingsLife] {
                if (settingsLife.expired() || !rich_view_ || !context_.prop) return;
                auto guide = context_.prop->sys().evtChildOK({});
                const auto skin = summary_
                    ? context_.findSkin(summary_.get(), summary_->type(), summary_->skinName())
                    : context_.findSkin(nullptr);
                // ホストが提供する解決処理によって、このビューが同期的に無効化される場合がある。
                if (settingsLife.expired() || !rich_view_) return;
                if (skin) {
                    const auto value = skin->evtChildOK();
                    if (!value.empty()) guide = value;
                }
                rich_view_->props().etc.inputGuideText = std::move(guide);
                rich_view_->refresh();
            });
        }
        // D版の refreshCard と同様、参照モデルや対象バージョンがこのビュー
        // の外で変わったときは、 グラフの警告と行のテキストを両方とも更新対象にする。
        const auto refreshGraphWarnings = [this, settingsLife](const auto&...) {
            if (!settingsLife.expired() && rich_view_)
                rich_view_->refreshExternalState();
        };
        const auto refreshVariableWarnings = [this, settingsLife,
                                               refreshGraphWarnings](const auto&... args) {
            refreshGraphWarnings(args...);
            if (!settingsLife.expired() && rich_view_)
                rich_view_->commons().refSpecialPreview.call();
        };
        bindVariablePreview(shared->refFlagAndStep, refreshVariableWarnings);
        bindVariablePreview(shared->delFlagAndStep, refreshVariableWarnings);
        bindVariablePreview(shared->refDataVersion, refreshVariableWarnings);
        bindVariablePreview(shared->refCast, refreshGraphWarnings);
        bindVariablePreview(shared->delCast, refreshGraphWarnings);
        bindVariablePreview(shared->refSkill, refreshGraphWarnings);
        bindVariablePreview(shared->delSkill, refreshGraphWarnings);
        bindVariablePreview(shared->refItem, refreshGraphWarnings);
        bindVariablePreview(shared->delItem, refreshGraphWarnings);
        bindVariablePreview(shared->refBeast, refreshGraphWarnings);
        bindVariablePreview(shared->delBeast, refreshGraphWarnings);
        bindVariablePreview(shared->refInfo, refreshGraphWarnings);
        bindVariablePreview(shared->delInfo, refreshGraphWarnings);
        bindVariablePreview(shared->refArea, refreshGraphWarnings);
        bindVariablePreview(shared->delArea, refreshGraphWarnings);
        bindVariablePreview(shared->refBattle, refreshGraphWarnings);
        bindVariablePreview(shared->delBattle, refreshGraphWarnings);
        bindVariablePreview(shared->refPackage, refreshGraphWarnings);
        bindVariablePreview(shared->delPackage, refreshGraphWarnings);
        // D版と同様、置換通知は書き込み可能なシナリオのビューだけに接続する。
        // モデルだけでなく、表示中の所有者・イベント・キーコードの行も更新する。
        if (!read_only_) {
            const auto refreshReplacement = [this, settingsLife] {
                if (!settingsLife.expired()) refresh();
            };
            const auto textConnection = shared->replText.add(refreshReplacement);
            const auto idConnection = shared->replID.add(refreshReplacement);
            replacement_disconnect_ = [shared, textConnection, idConnection,
                                        life = shared->lifetimeToken()] {
                if (life.expired()) return;
                shared->replText.remove(textConnection);
                shared->replID.remove(idConnection);
            };
        }

    }
    if (!read_only_ && context_.editor_commons &&
        dynamic_cast<area::AbstractArea*>(owner_)) {
        auto* shared = context_.editor_commons;
        const auto bind = [this, shared](auto& signal, auto callback) {
            const auto token = signal.add(std::move(callback));
            scene_notifications_disconnect_.push_back(
                [borrowed = &signal, token, life = shared->lifetimeToken()] {
                    if (!life.expired()) borrowed->remove(token);
                });
        };
        const auto refreshPath = [this, settingsLife](const String& value, bool deleting) {
            if (settingsLife.expired() || !owner_ || path::cpparent(value) != owner_->cwxPath(true)) return;
            const auto retained = deleting && summary_ ? summary_->findCWXPath(value) : std::shared_ptr<path::CWXPath>{};
            refreshSceneOwners(dynamic_cast<event::EventTreeOwner*>(retained.get()));
        };
        bind(shared->addMenuCard, [refreshPath](String value) { refreshPath(value, false); });
        bind(shared->refMenuCard, [refreshPath](String value) { refreshPath(value, false); });
        bind(shared->delMenuCard, [refreshPath](String value) { refreshPath(value, true); });
        const auto refreshOrder = [this, settingsLife](String value, std::vector<int>, int) {
            if (!settingsLife.expired() && owner_ &&
                (value == owner_->cwxPath(true) || path::cpparent(value) == owner_->cwxPath(true))) refreshSceneOwners();
        };
        bind(shared->upMenuCard, refreshOrder); bind(shared->downMenuCard, refreshOrder);
        bind(shared->refMenuCardIndices, [this, settingsLife](const auto& scene, const auto&) {
            if (!settingsLife.expired() && scene.get() == dynamic_cast<area::AbstractArea*>(owner_)) refreshSceneOwners();
        });
        if (dynamic_cast<area::Battle*>(owner_)) {
            const auto refreshCast = [this, settingsLife](const auto& card) {
                if (settingsLife.expired() || !card) return;
                const auto* battle = dynamic_cast<area::Battle*>(owner_);
                if (battle && std::any_of(battle->cards().begin(), battle->cards().end(),
                    [&card](const auto& enemy) { return enemy && enemy->id() == card->id(); })) refreshSceneOwners();
            };
            bind(shared->refCast, refreshCast); bind(shared->delCast, refreshCast);
        }
    }
    (void)rich_view_->createControl(widget_);
    (void)rich_view_->initial();
    refresh();
}

EventView::~EventView() { retireHostBindings(); }

void EventView::retireHostBindings() {
    if (host_retired_) return;
    host_retired_ = true;
    // SWT の破棄処理と同様、借用した Commons が有効なうちに接続を解除
    // する。 実際の編集対象の所有者は、ペインとは独立して保持される場合がある。
    command_lifetime_.reset();
    auto sceneDisconnects = std::move(scene_notifications_disconnect_);
    scene_notifications_disconnect_.clear();
    for (const auto& disconnect : sceneDisconnects) if (disconnect) disconnect();
    const auto disconnectReplacement = std::move(replacement_disconnect_);
    replacement_disconnect_ = {};
    if (disconnectReplacement) disconnectReplacement();
    const auto disconnectPreview = std::move(preview_values_disconnect_);
    preview_values_disconnect_ = {};
    if (disconnectPreview) disconnectPreview();
    const auto disconnectStyle = std::move(event_editor_style_disconnect_);
    event_editor_style_disconnect_ = {};
    if (disconnectStyle) disconnectStyle();
    const auto disconnectSlope = std::move(event_slope_disconnect_);
    event_slope_disconnect_ = {};
    if (disconnectSlope) disconnectSlope();
    const auto disconnect = std::move(content_tool_disconnect_);
    content_tool_disconnect_ = {};
    if (disconnect) disconnect();
    auto left_comments = std::move(left_comment_dialogs_);
    for (const auto& [key, dialog] : left_comments) if (dialog) dialog->forceCancel();
    closeActiveDialog();
    auto templateDialog = std::move(event_template_dialog_);
    if (templateDialog) templateDialog->forceCancel();
    auto scriptVarDialog = std::move(script_var_dialog_);
    if (scriptVarDialog) scriptVarDialog->forceCancel();
    // D版と同様、一つのウィンドウが閉じてもコマンドは存続する。表示部分をここで無効
    // 化し、 記録済みの操作には必要に応じてこのラッパーとモデルのアダプターを保持さ
    // せる。 ラッパーから管理側への参照は弱参照だけなので、循環参照にはならない。
    if (rich_view_) rich_view_->retireHistoryPresentation();
    retained_history_view_ = std::move(rich_view_);
    if (owned_rich_undo_) owned_rich_undo_->manager->reset();
    owned_rich_undo_.reset();
    undo_ = nullptr;
    callbacks_ = {};
}

std::shared_ptr<lib::Panel> EventView::allCompactToolbar() {
    return rich_view_ ? rich_view_->allCompactToolbar()
                      : std::shared_ptr<lib::Panel>{};
}

void EventView::prepareContentsPalette() {
    if (rich_view_ && context_.vars)
        copyContentsPaletteProperties(rich_view_->props().etc, *context_.vars);
}

void EventView::refreshSceneOwners(event::EventTreeOwner* deleting) {
    if (host_retired_ || !rich_view_) return;
    const std::weak_ptr<int> life = command_lifetime_;
    if (deleting) {
        std::vector<const event::EventTree*> closing;
        for (const auto& [key, dialog] : tree_dialogs_)
            if (dialog && &dialog->eventTreeOwner() == deleting) closing.push_back(key);
        for (const auto* key : closing) {
            const auto found = tree_dialogs_.find(key);
            if (found == tree_dialogs_.end()) continue;
            const auto dialog = found->second;
            tree_dialogs_.erase(found);
            dialog->close();
            if (life.expired()) return;
        }
        std::vector<std::shared_ptr<comment::CommentDialog>> comments;
        for (const auto& [key, dialog] : left_comment_dialogs_) {
            bool matches = key == static_cast<path::CWXPath*>(deleting);
            for (const auto& tree : deleting->trees()) if (tree && key == tree.get()) matches = true;
            if (matches && dialog) comments.push_back(dialog);
        }
        std::vector<contentinitializer::EventDialogPtr> editors;
        for (const auto& [key, dialog] : content_dialogs_) {
            const auto content = dialog ? (dialog->event() ? dialog->event() : dialog->parentContent()) : event::ContentPtr{};
            const auto tree = content ? content->tree() : event::EventTreePtr{};
            if (tree && tree->owner() == deleting) editors.push_back(dialog);
        }
        std::vector<std::pair<String,
            std::shared_ptr<eventdialog::ContentCommentDialog>>> contentComments;
        for (const auto& [key, dialog] : content_comment_dialogs_) {
            const auto tree = dialog ? dialog->eventTree() : event::EventTreePtr{};
            if (tree && tree->owner() == deleting)
                contentComments.emplace_back(key, dialog);
        }
        const auto eventTreeComment = event_tree_comment_dialog_;
        const auto eventTree = eventTreeComment
            ? eventTreeComment->eventTree() : event::EventTreePtr{};
        for (const auto& dialog : comments) { dialog->forceCancel(); if (life.expired()) return; }
        for (const auto& dialog : editors) { dialog->forceCancel(); if (life.expired()) return; }
        for (const auto& [key, dialog] : contentComments) {
            const auto found = content_comment_dialogs_.find(key);
            if (found != content_comment_dialogs_.end() && found->second == dialog)
                content_comment_dialogs_.erase(found);
            if (comment_dialog_ == dialog) comment_dialog_.reset();
            if (dialog) dialog->forceCancel();
            if (life.expired()) return;
        }
        if (eventTree && eventTree->owner() == deleting) {
            event_tree_comment_dialog_.reset();
            if (comment_dialog_ == eventTreeComment) comment_dialog_.reset();
            eventTreeComment->forceCancel();
            if (life.expired()) return;
        }
    }
    rich_view_->refresh(deleting);
    if (life.expired()) return;
    refreshCommentTitles(rich_view_.get(), left_comment_dialogs_, context_.prop, summary_);
}

void EventView::refresh() {
    const std::weak_ptr<int> life = command_lifetime_;
    std::vector<const event::EventTree*> stale;
    for (const auto& [key, dialog] : tree_dialogs_)
        if (dialog && !dialog->pendingCreation() && !dialog->eventTreePtr()->owner()) stale.push_back(key);
    for (const auto* key : stale) {
        const auto found = tree_dialogs_.find(key);
        if (found == tree_dialogs_.end()) continue;
        const auto dialog = found->second;
        tree_dialogs_.erase(found);
        dialog->close();
        if (life.expired()) return;
    }
    tree_view_.refresh();
    if (life.expired()) return;
    if (rich_view_) rich_view_->refresh();
    if (life.expired()) return;
    refreshCommentTitles(rich_view_.get(), left_comment_dialogs_, context_.prop, summary_);
    const auto callback = callbacks_.refresh;
    if (callback) callback();
}

void EventView::refreshTitle() {
    if (callbacks_.refreshTitle) callbacks_.refreshTitle();
}

void EventView::ownerTitle(String value) {
    const std::weak_ptr<int> life = command_lifetime_;
    if (rich_view_) rich_view_->ownerTitle(std::move(value));
    if (life.expired()) return;
    refreshCommentTitles(rich_view_.get(), left_comment_dialogs_, context_.prop, summary_);
}

void EventView::openToolWindow() {
    if (callbacks_.openToolWindow) callbacks_.openToolWindow();
    else if (rich_view_) rich_view_->openToolWindow();
}

void EventView::openDuplicate() {
    // ウィンドウの複製には通常のホスト生成処理が必要で、モデルの複製とは
    // 別の操作になる。 その画面操作を提供しないホストでは無効にする。
    const auto open = callbacks_.openDuplicate;
    if (open) open();
}

void EventView::movingWindow() {
    if (callbacks_.movingWindow) callbacks_.movingWindow();
}

void EventView::moveWindow() {
    if (callbacks_.moveWindow) callbacks_.moveWindow();
}

void EventView::undo() {
    if (host_retired_ || read_only_) return;
    if (!callbacks_.undo && rich_view_) {
        // ツールバーとグラフメニューのコマンドを、同じ詳細ビューの処理経路へ渡す。
        rich_view_->undo();
        return;
    }
    if (callbacks_.undo) {
        runHistoryReplayCommand(callbacks_.undo);
        return;
    }
    if (undo_) runHistoryReplayCommand([this] { if (undo_) (void)undo_->undo(); });
}

void EventView::redo() {
    if (host_retired_ || read_only_) return;
    if (!callbacks_.redo && rich_view_) {
        rich_view_->redo();
        return;
    }
    if (callbacks_.redo) {
        runHistoryReplayCommand(callbacks_.redo);
        return;
    }
    if (undo_) runHistoryReplayCommand([this] { if (undo_) (void)undo_->redo(); });
}

void EventView::runHistoryReplayCommand(lib::Action replay) {
    if (host_retired_ || read_only_ || !replay) return;
    // キャンセルでこの EventWindow が無効化されても、共有ガードを保持する。
    const auto history = rich_undo_.lock();
    HistoryReplayCommandScope replayScope(history);
    if (!replayScope) return;
    const auto life = std::weak_ptr<int>(command_lifetime_);
    // C++版のモードレスダイアログを閉じる処理は、ツールバー、コンテキストメニュー、
    // MainWindow、独自コールバック、旧管理方式の履歴操作に共通の経路で行う。
    auto dialogs = std::move(tree_dialogs_);
    tree_dialogs_.clear();
    for (const auto& [key, dialog] : dialogs) {
        (void)key; if (dialog) dialog->close();
        if (life.expired()) return;
    }
    if (!cancelScriptEdits(*this, history, life) || life.expired()) return;
    replay();
}

bool EventView::canUndo() const noexcept {
    if (host_retired_ || read_only_) return false;
    if (callbacks_.undo) return undo_ && undo_->canUndo();
    return rich_view_ ? rich_view_->canUndo() : undo_ && undo_->canUndo();
}

bool EventView::canRedo() const noexcept {
    if (host_retired_ || read_only_) return false;
    if (callbacks_.redo) return undo_ && undo_->canRedo();
    return rich_view_ ? rich_view_->canRedo() : undo_ && undo_->canRedo();
}

void EventView::setUndoLimit(const std::size_t value) {
    if (host_retired_) return;
    if (undo_) undo_->max(value);
    if (const auto history = rich_undo_.lock()) history->manager->max(value);
    if (rich_view_) rich_view_->refUndoMax();
}

std::size_t EventView::undoLimit() const noexcept {
    const auto history = rich_undo_.lock();
    return history ? history->manager->max() : 0U;
}

void EventView::up() {
    if (callbacks_.up) callbacks_.up();
    else if (rich_view_) (void)rich_view_->up();
    else tree_view_.up();
    tree_view_.refresh();
}

void EventView::down() {
    if (callbacks_.down) callbacks_.down();
    else if (rich_view_) (void)rich_view_->down();
    else tree_view_.down();
    tree_view_.refresh();
}

bool EventView::canUp() const noexcept {
    return rich_view_ ? rich_view_->canUp() : !read_only_ && tree_view_.canUp();
}

bool EventView::canDown() const noexcept {
    return rich_view_ ? rich_view_->canDown() : !read_only_ && tree_view_.canDown();
}

void EventView::writeComment() {
    if (callbacks_.writeComment) { callbacks_.writeComment(); return; }
    if (!canWriteComment() || !rich_view_) return;
    if (!rich_view_->commentUsesRightPane()) {
        const auto tree = rich_view_->selectionEventTree();
        auto* owner = tree ? nullptr : rich_view_->selectionParent();
        path::CWXPath* model = tree ? static_cast<path::CWXPath*>(tree.get()) : owner;
        const path::CWXPath* key = model;
        if (!key) return;
        if (const auto it = left_comment_dialogs_.find(key); it != left_comment_dialogs_.end()) {
            it->second->active(); return;
        }
        const auto life = std::weak_ptr<int>(command_lifetime_);
        auto adapter = std::make_shared<comment::Commons>();
        adapter->services = context_.services;
        if (context_.editor_props) comment::bindEditorImages(adapter->prop, *context_.editor_props);
        adapter->prop.bindDialogSize = [properties=context_.editor_props, life](const std::shared_ptr<lib::Window>& window) -> std::function<void()> {
            return !properties || life.expired() ? std::function<void()>{} : properties->bindDialogSize(window, properties->flex().commentDlg);
        };
        auto dialog = std::make_shared<comment::CommentDialog>(*adapter,
            context_.dialog_owner, tree ? tree->comment() : owner->commentForEvents());
        // D版の writeCommentL と同様、モードレスエディタには結び付いたモデルの
        // 名前を使う。 表示行には発火条件が含まれる場合があるため、ツリーのモデル名を使う。
        String comment_name = tree ? tree->name() : String{};
        if (!tree) {
            if (const auto rows = rich_view_->tree()) for (const auto& row : rows->nodes) {
                const auto* info = rich_view_->nodeInfo(row.id);
                if (info && info->kind == rich_eventview::NodeInfo::Kind::Owner &&
                    info->owner == owner) { comment_name = row.text; break; }
            }
        }
        if (!tree) comment_name = ownerCommentName(owner, comment_name, summary_);
        if (context_.prop) dialog->title = utils::tryFormat(
            context_.prop->requiredMsgs().dlgTitCommentWith.value, comment_name);
        // D版のGCと同様、後の選択変更とは独立して選択モデルを保持する。 通常の Summary
        // は所有して保持する。借用したAPIの所有者については、 既存の EventWindow
        // の所有者寿命の条件と、ビューの弱参照ガードを維持する。
        const auto model_owner = summary_ ? summary_->findCWXPath(model->cwxPath(true)) : std::shared_ptr<path::CWXPath>{};
        dialog->applied = [this, life, adapter, tree, owner, model_owner](const StringView value) {
            if (life.expired() || !rich_view_) return;
            (void)rich_view_->setComment(String(value), owner, tree);
            if (life.expired()) return;
            tree_view_.refresh();
            const auto refresh = callbacks_.refresh;
            if (refresh) refresh();
        };
        dialog->closed = [this, life, key] {
            if (!life.expired()) left_comment_dialogs_.erase(key);
        };
        left_comment_dialogs_[key] = dialog;
        (void)dialog->open();
        return;
    }
    const auto content = rich_view_->displayedContent();
    const auto tree = rich_view_->displayedEventTree();
    if (!content && !tree) return;
    const auto life = std::weak_ptr<int>(command_lifetime_);
    if (content) {
        // 閉じるコールバックが finishClose() の実行中にラッパー
        // を破棄しないよう、 閉じたダイアログは次のコマンドまでキー付きマップ
        // に残す。 ここは安全なコマンドの境界なので、閉じた項目を削除する。
        for (auto it = content_comment_dialogs_.begin();
             it != content_comment_dialogs_.end();) {
            if (!it->second || !it->second->isOpen()) {
                if (comment_dialog_ == it->second) comment_dialog_.reset();
                it = content_comment_dialogs_.erase(it);
            } else {
                ++it;
            }
        }
        const auto found = content_comment_dialogs_.find(content->eventId());
        if (found != content_comment_dialogs_.end()) {
            comment_dialog_ = found->second;
            comment_dialog_->active();
            return;
        }
        auto dialog = std::make_shared<eventdialog::ContentCommentDialog>(
            context_, widget_, content->parent(), content);
        dialog->commitComment = [this, life, content](const StringView saved) {
            if (life.expired() || !rich_view_) return false;
            if (content->comment() == saved) return true;
            return rich_view_->writeContentComment(content, String(saved));
        };
        dialog->appliedEvent.push_back([this, life] {
            if (life.expired()) return;
            if (rich_view_) rich_view_->refresh();
            if (life.expired()) return;
            tree_view_.refresh();
            const auto refresh = callbacks_.refresh;
            if (refresh) refresh();
        });
        content_comment_dialogs_.insert_or_assign(content->eventId(), dialog);
        comment_dialog_ = dialog;
        dialog->open();
        return;
    }
    if (event_tree_comment_dialog_ && event_tree_comment_dialog_->isOpen()) {
        comment_dialog_ = event_tree_comment_dialog_;
        comment_dialog_->active();
        return;
    }
    auto dialog = std::make_shared<eventdialog::ContentCommentDialog>(
        context_, widget_, tree);
    event_tree_comment_dialog_ = dialog;
    comment_dialog_ = dialog;
    dialog->appliedEvent.push_back([this, life] {
        if (life.expired()) return;
        if (rich_view_) rich_view_->refresh();
        if (life.expired()) return;
        tree_view_.refresh();
        const auto refresh = callbacks_.refresh;
        if (refresh) refresh();
    });
    dialog->open();
}

void EventView::toScript() {
    if (host_retired_) return;
    const std::weak_ptr<int> life = command_lifetime_;
    if (callbacks_.toScript) {
        const auto action = callbacks_.toScript;
        if (finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
                commons::Commons::canonicalEventOwner(owner_), life)) action();
        return;
    }
    if (!rich_view_) return;
    const auto content = scriptSelection(*rich_view_);
    if (content && finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
            commons::Commons::canonicalEventOwner(owner_), life))
        (void)copyScript({content});
}

void EventView::toScriptOne() {
    if (host_retired_) return;
    const std::weak_ptr<int> life = command_lifetime_;
    if (callbacks_.toScriptOne) {
        const auto action = callbacks_.toScriptOne;
        if (finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
                commons::Commons::canonicalEventOwner(owner_), life)) action();
        return;
    }
    if (!rich_view_) return;
    const auto content = scriptSelection(*rich_view_);
    if (content && finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
            commons::Commons::canonicalEventOwner(owner_), life))
        (void)copyScript({content}, true);
}

void EventView::toScriptAll() {
    if (host_retired_) return;
    const std::weak_ptr<int> life = command_lifetime_;
    if (callbacks_.toScriptAll) {
        const auto action = callbacks_.toScriptAll;
        if (finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
                commons::Commons::canonicalEventOwner(owner_), life)) action();
        return;
    }
    if (!rich_view_ || !rich_view_->displayedEventTree()) return;
    if (!finishScriptEdits(*this, rich_undo_.lock(), context_.editor_commons,
            commons::Commons::canonicalEventOwner(owner_), life)) return;
    const auto tree = rich_view_->displayedEventTree();
    if (tree) {
        const auto starts = tree->starts();
        (void)copyScript(lib::Array<event::ContentPtr>(starts.begin(), starts.end()));
    }
}

bool EventView::copyScript(const lib::Array<event::ContentPtr>& contents,
                           const bool shallow) {
    if (!context_.prop || contents.empty()) return false;
    script::Contents converted;
    converted.reserve(contents.size());
    for (const auto& content : contents)
        if (content) converted.push_back(
            contentinitializer::ContentInitialValueEditor::toScript(*content, shallow));
    if (converted.empty()) return false;
    String evtChildOK;
    if (summary_) {
        if (const auto skin = context_.findSkin(summary_.get(), summary_->type(),
                                                summary_->skinName()))
            evtChildOK = skin->evtChildOK();
    }
    if (evtChildOK.empty()) evtChildOK = context_.prop->sys().evtChildOK({});
    const script::CWXScript compiler(context_.prop, summary_.get());
    auto text = compiler.toScriptText(
        converted, evtChildOK, summary_ && summary_->legacy(), "\t");
    if (text.empty()) return false;
    // D版の EventTreeView と同様、論理的な改行を明示してコピーする
    // 。 OS固有の改行への変換は、バックエンドのクリップボードとの境界で行う。
    text.push_back('\n');
    try {
        context_.clipboard.setContents(lib::ClipboardData{text, {}, {}});
    } catch (...) {
        // クリップボード操作が失敗しても、モデルや履歴を追加で変更しない。
        // スクリプト操作は、ここに入る前に未確定のインライン編集を確定する。
        return false;
    }
    if (context_.refreshToolBar) context_.refreshToolBar();
    return true;
}

void EventView::startToPackage() {
    if (callbacks_.startToPackage) {
        callbacks_.startToPackage();
        return;
    }
    if (!canStartToPackage() || !rich_view_ || !context_.editor_commons) return;
    const auto selected = rich_view_->selectionContent();
    const auto tree = rich_view_->displayedEventTree();
    const auto start = selected ? selected->parentStart() : event::ContentPtr{};
    if (!tree || !start) return;

    const auto life = std::weak_ptr<int>(command_lifetime_);
    auto* const commons = context_.editor_commons;
    const auto name = start->name();
    if (!rich_view_->convertStartToPackage(start, [commons, start, name] {
            return commons->createPackage(start, name, false);
        }) || life.expired()) return;
    refresh();
    if (context_.refreshToolBar) context_.refreshToolBar();
}

void EventView::eventToPackage() {
    if (callbacks_.eventToPackage) {
        callbacks_.eventToPackage();
        return;
    }
    if (!canEventToPackage() || !rich_view_ || !context_.editor_commons) return;
    const auto tree = rich_view_->selectionEventTree();
    auto* const owner = tree ? tree->owner() : nullptr;
    if (!owner) return;
    const auto oldName = tree->name();
    String ownerText;
    // カード側の上書きや翻訳を含め、実際の所有者行のラベルを読む。
    for (const auto& row : rich_view_->tree()->nodes) {
        const auto* info = rich_view_->nodeInfo(row.id);
        if (info && info->kind == rich_eventview::NodeInfo::Kind::Owner &&
            info->owner == owner) { ownerText = row.text; break; }
    }
    const auto life = std::weak_ptr<int>(command_lifetime_);
    const auto package_id = context_.editor_commons->createPackage(
        tree, ownerText + "-" + oldName, false);
    if (life.expired() || package_id == 0U) return;
    // 生成中に同期的にパッケージペインが選択される場合がある。 取得
    // 済みの転送元が元の所有者に属している場合だけ、処理を続ける。
    if (tree->owner() != owner ||
        std::find(owner->trees().begin(), owner->trees().end(), tree) == owner->trees().end()) return;
    auto start = std::make_shared<event::Content>(types::CType::Start, oldName);
    auto link = std::make_shared<event::Content>(types::CType::LinkPackage);
    link->packages(package_id);
    start->add(link);
    if (!rich_view_->replaceEventContents(tree, {start})) return;
    refresh();
    if (context_.refreshToolBar) context_.refreshToolBar();
}

void EventView::addManyRounds() {
    if (callbacks_.addManyRounds) {
        callbacks_.addManyRounds();
        return;
    }
    if (!canAddManyRounds() || !rich_view_) return;
    roundview::RoundViewConfig config;
    // D版の EventView::addManyRounds と同様、共有プロパテ
    // ィを渡し、 範囲選択で保存済みの roundMax 設定を使えるようにする。
    config.roundMax = context_.vars ? context_.vars->roundMax.value : 999U;
    config.services = context_.services;
    config.resources.manyRoundsText = "ラウンドを範囲追加";
    config.resources.roundSeparator = "～";
    config.resources.rangeHint = "%u～%u";
    auto* parent = context_.dialog_owner.get();
    roundview::ManyRoundsDialog dialog(std::move(config), context_.dialog_owner, parent);
    if (dialog.open()) (void)rich_view_->addManyRounds(dialog.rounds());
}

void EventView::selectConnectedResource() {
    if (callbacks_.selectConnectedResource) {
        callbacks_.selectConnectedResource();
        return;
    }
    if (!canSelectConnectedResource() || !rich_view_ ||
        !context_.editor_commons)
        return;
    auto* const selectedOwner = selectedConnectedOwner(rich_view_.get());
    if (!selectedOwner) return;
    const auto owner = selectedOwner->retainForCallback();
    if (!owner) return;
    // D版と同様、右側の EventTree グラフが別のコンテント選択を保持してい
    // ても、 このコマンドの対象は左側の Cards で選択した所有者から決める。
    auto value = path::cpaddattr(owner->cwxPath(true), "shallow");
    value = path::cpaddattr(value, "only");
    context_.editor_commons->openCWXPath(std::move(value), false);
}

void EventView::wrapTree() {
    if (callbacks_.wrapTree) callbacks_.wrapTree();
    else if (rich_view_) (void)rich_view_->wrapTree();
}

void EventView::findStartUsers() {
    if (callbacks_.findStartUsers) callbacks_.findStartUsers();
    else if (rich_view_) (void)rich_view_->findStartUsers();
}

namespace {

eventtreedialog::OwnerCapabilities eventTreeCapabilities(
    const event::EventTreeOwner& owner) {
    eventtreedialog::OwnerCapabilities result;
    result.canHasFireEnter = owner.canHasFireEnter();
    result.canHasFireLose = owner.canHasFireLose();
    result.canHasFireEscape = owner.canHasFireEscape();
    result.canHasFireEveryRound = owner.canHasFireEveryRound();
    result.canHasFireRoundEnd = owner.canHasFireRoundEnd();
    result.canHasFireRound0 = owner.canHasFireRound0();
    result.canHasFireRound = owner.canHasFireRound();
    result.canHasFireKeyCode = owner.canHasFireKeyCode();
    if (dynamic_cast<const area::Battle*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::Battle;
    } else if (dynamic_cast<const area::Area*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::Area;
    } else if (dynamic_cast<const area::Package*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::Package;
    } else if (dynamic_cast<const area::MenuCard*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::MenuCard;
    } else if (dynamic_cast<const area::EnemyCard*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::EnemyCard;
        result.withKeyCodeIgnitionType = true;
    } else if (dynamic_cast<const area::PlayerCardEvents*>(&owner)) {
        result.kind = eventtreedialog::OwnerKind::PlayerCardEvents;
        result.withKeyCodeIgnitionType = true;
    }
    return result;
}

}  // namespace

void EventView::createEvent() {
    std::erase_if(tree_dialogs_, [](const auto& item) { return !item.second || !item.second->dialog()->visible(); });
    if (read_only_ || !rich_view_ || !owner_ || !context_.editor_props ||
        !rich_view_->canCreateEvent()) return;
    auto* selected_owner = rich_view_->selectionParent();
    if (!selected_owner) return;
    if (dynamic_cast<area::Package*>(selected_owner)) {
        // D版と同様、パッケージには設定可能な発火条件がないため、
        // イベントを直ちに作成する。 新規イベント操作で、キャンセ
        // ル可能な EventTreeDialog は開かない。
        auto value = std::make_shared<event::EventTree>(
            context_.editor_props->requiredMsgs().packageTree.value);
        (void)rich_view_->appendTree(std::move(value),
                                    static_cast<std::size_t>(-1), selected_owner);
        return;
    }
    // D版と同様、ダイアログで利用できる発火条件と挿入先の所有者を、選択中の親から決
    // める。 親は MenuCard/EnemyCard の部分ツリーの場合もある。
    const auto selected_owner_lifetime = [&]() -> std::shared_ptr<event::EventTreeOwner> {
        if (auto* value = dynamic_cast<area::AbstractArea*>(selected_owner)) return value->weak_from_this().lock();
        if (auto* value = dynamic_cast<area::AbstractSpCard*>(selected_owner)) return value->weak_from_this().lock();
        if (auto* value = dynamic_cast<area::PlayerCardEvents*>(selected_owner)) return value->weak_from_this().lock();
        if (auto* value = dynamic_cast<event::AbstractEventTreeOwner*>(selected_owner)) return value->weak_from_this().lock();
        return {};
    }();
    const std::weak_ptr<int> life = command_lifetime_;
    auto value = std::make_shared<event::EventTree>(String{});
    auto dialog = std::make_shared<eventtreedialog::EventTreeDialog>(
        *context_.editor_props, context_.dialog_owner, summary_.get(), *selected_owner,
        *value, true, false, eventTreeCapabilities(*selected_owner), context_.services,
        context_.editor_commons);
    // apply() は、通知中にダイアログが破棄されても処理できるようにコールバック
    // を複製する。 その複製間と、適用ボタンの繰り返し操作の間で、挿入状態を共有する。
    const auto inserted = std::make_shared<bool>(false);
    dialog->applyTransaction([this, life, value, selected_owner, selected_owner_lifetime,
                              summary=summary_, inserted](const std::function<bool()>& apply) {
        if (life.expired() || !rich_view_) return false;
        if (!*inserted) {
            if (!apply() || life.expired()) return false;
            *inserted = static_cast<bool>(rich_view_->appendTree(value, static_cast<std::size_t>(-1), selected_owner));
            if (*inserted && !life.expired()) refresh();
            return *inserted && !life.expired();
        }
        if (value->owner() != selected_owner) return false;
        const bool edited=rich_view_->editEventTree(apply);
        if (!life.expired() && edited) {
            tree_view_.refresh();
            if (life.expired()) return edited;
            const auto notify=callbacks_.refresh;
            if (notify) notify();
        }
        return edited && !life.expired();
    });
    tree_dialogs_[value.get()] = dialog;
    (void)dialog->open();
}

bool EventView::openContentDialog(const event::ContentPtr& content,
                                  const event::ContentPtr& parent,
                                  const bool pendingCreate) {
    if (read_only_ || !rich_view_ || !content ||
        !contentinitializer::hasDialog(content->type(), static_cast<bool>(summary_)))
        return false;
    if (const auto found = content_dialogs_.find(content.get());
        found != content_dialogs_.end()) {
        active_dialog_ = found->second;
        if (active_dialog_) active_dialog_->active();
        return static_cast<bool>(active_dialog_);
    }

    lib::editor_action_log("EventView edit dialog-create-begin");
    // 作成途中のコンテントは未接続の場合があるため、ツリーのローカル変数の範囲を維持する。
    auto edit_tree = content->tree();
    if (!edit_tree && parent) edit_tree = parent->tree();
    if (!edit_tree) edit_tree = rich_view_->displayedEventTree();
    if (!contentDialogResourcesAvailable(content->type(), summary_, edit_tree)) return false;
    auto edit_counter = edit_tree ? edit_tree->useCounter() : content->useCounter();
    // 単独版と ALL の EventView は、どちらも所有する MainWindow の現
    // 在のスキン解決処理を使う。 初期値専用のエディタでは、独自の forceSkin を渡す。
    auto dialog = contentinitializer::createEventDialog(
        context_, summary_, SkinPtr{}, edit_counter,
        widget_, content, parent, false);
    // D版の create() と同様、イベントなしとして扱わず、初期化済みの Content
    // を渡す。 その値を保持したうえで、作成を確定まで保留するトランザクションの扱いを維持する。
    if (dialog) dialog->setCreateMode(pendingCreate);
    active_dialog_ = dialog;
    lib::editor_action_log(
        "EventView edit dialog-create-end created=" +
        std::to_string(dialog ? 1 : 0));
    if (!dialog) return false;

    if (pendingCreate) {
        if (!rich_view_->hasPendingContentCreate(content)) {
            if (active_dialog_ == dialog) active_dialog_.reset();
            return false;
        }
    } else if (!rich_view_->beginExternalContentEdit(content)) {
        if (active_dialog_ == dialog) active_dialog_.reset();
        return false;
    }
    content_dialogs_.insert_or_assign(content.get(), dialog);

    const auto child_names_lifetime = std::weak_ptr<int>(command_lifetime_);
    dialog->childNamesApplied = [this, content, child_names_lifetime](const auto& changes) {
        if (child_names_lifetime.expired() || !rich_view_) return;
        rich_view_->recordExternalChildNames(content, changes);
    };
    dialog->appliedEvent.push_back([this, content, life = child_names_lifetime] {
        if (life.expired() || !rich_view_) return;
        const bool committed = rich_view_->hasPendingContentCreate(content)
            ? rich_view_->commitPendingContentCreate(content)
            : rich_view_->commitExternalContentEdit(content);
        if (life.expired() || !committed) return;
        if (context_.editor_commons && summary_ && !summary_->scenarioPath().empty()) {
            const auto detail = content->detail();
            if (detail.use(types::CArg::Coupon) || detail.use(types::CArg::CouponNames) ||
                detail.use(types::CArg::Coupons) || detail.use(types::CArg::Expression))
                context_.editor_commons->refCoupons.call();
            if (life.expired()) return;
            if (detail.use(types::CArg::Gossip) || detail.use(types::CArg::Expression))
                context_.editor_commons->refGossips.call();
            if (life.expired()) return;
            if (detail.use(types::CArg::KeyCode) || detail.use(types::CArg::KeyCodes))
                context_.editor_commons->refKeyCodes.call();
            if (life.expired()) return;
            if (detail.use(types::CArg::CompleteStamp))
                context_.editor_commons->refCompleteStamps.call();
            if (life.expired()) return;
            if (detail.use(types::CArg::CellName))
                context_.editor_commons->refCellNames.call();
            if (life.expired()) return;
        }
        tree_view_.refresh();
        if (callbacks_.refresh) callbacks_.refresh();
    });
    dialog->closeEvent.push_back(
        [this, content, weakDialog = std::weak_ptr(dialog)] {
        const auto closedDialog = weakDialog.lock();
        if (rich_view_) {
            if (rich_view_->hasPendingContentCreate(content))
                (void)rich_view_->cancelPendingContentCreate(content);
            else
                rich_view_->endExternalContentEdit(content);
        }
        content_dialogs_.erase(content.get());
        if (active_dialog_ == closedDialog) {
            active_dialog_.reset();
            if (!content_dialogs_.empty()) active_dialog_ = content_dialogs_.begin()->second;
        }
        if (!closing_active_dialog_) refresh();
    });
    lib::editor_action_log("EventView edit dialog-open-begin");
    dialog->open();
    lib::editor_action_log("EventView edit dialog-open-end");
    return true;
}

void EventView::edit() {
    lib::editor_action_log("EventView edit begin");
    if (read_only_) return;
    if (callbacks_.edit) callbacks_.edit();
    else if (rich_view_) {
        const std::weak_ptr<int> life(command_lifetime_);
        auto* const rich = rich_view_.get();
        // D版の EventTreeView.edit() と同様、モードレスダイアログを開く・前
        // 面に出す前に、関連ビューの編集を確定する。 Commons への問い合わせは、EventWindow
        // ごとに独立した取り消し管理を持つ パッケージの関連ビューも対象にする。
        if (!finishScriptEdits(*this, rich_undo_.lock(),
                                       context_.editor_commons,
                                       commons::Commons::canonicalEventOwner(owner_), life) ||
            life.expired() || rich_view_.get() != rich) return;
        // フォーカスの問い合わせはホストのコールバックを呼ぶ。ビューはファサードが単独で所有
        // するため、 再利用する前に、ファサードの寿命と子ビューが同じものかを両方確認する。
        const auto content = rich->activeSelectionContent();
        if (life.expired() || rich_view_.get() != rich) return;
        lib::editor_action_log(
            "EventView edit selection=" +
            std::to_string(content ? static_cast<std::int32_t>(content->type()) : -1));
        if (!content) {
            const auto tree = rich->selectionEventTree();
            if (life.expired()) return;
            auto* const selected_owner = tree ? tree->owner() : nullptr;
            if (tree && selected_owner && context_.editor_props) {
                if (const auto found = tree_dialogs_.find(tree.get());
                    found != tree_dialogs_.end() && found->second->dialog()->visible()) {
                    // D版ではプロパティ編集の呼び出しごとに EventTreeDialog
                    // を新規生成する。 C++版では、一つの EventTree
                    // を二つのモードレス編集画面が競合して編集しないよう、
                    // 同じツリーの表示中ダイアログを意図的に再利用する。 ダイ
                    // アログの内容と、適用・履歴の処理経路はD版と共通に保つ。
                    if (context_.services.gui) context_.services.gui->restore_window(found->second->dialog());
                    return;
                }
                std::erase_if(tree_dialogs_, [](const auto& item) { return !item.second->dialog()->visible(); });
                auto dialog = std::make_shared<eventtreedialog::EventTreeDialog>(
                    *context_.editor_props, context_.dialog_owner, summary_.get(),
                    *selected_owner, *tree, false, read_only_,
                    eventTreeCapabilities(*selected_owner), context_.services,
                    context_.editor_commons);
                const auto life = std::weak_ptr<int>(command_lifetime_);
                dialog->applyTransaction([this, life, tree, selected_owner, summary=summary_](
                                           const std::function<bool()>& apply) {
                    if (life.expired() || !rich_view_ || tree->owner() != selected_owner) return false;
                    const bool changed = rich_view_->editEventTree(apply);
                    if (changed && !life.expired()) {
                        tree_view_.refresh();
                        if (life.expired()) return false;
                        const auto notify=callbacks_.refresh;
                        if (notify) notify();
                    }
                    return changed && !life.expired();
                });
                tree_dialogs_[tree.get()] = dialog;
                (void)dialog->open();
                return;
            }
        }
        if (life.expired()) return;
        if (content && openContentDialog(content, content->parent(), false)) return;
        if (life.expired()) return;
        if (!rich->editSelection() && !life.expired()) rich->editEnter();
    }
}

bool EventView::debugOpenContent(const std::size_t visibleIndex) {
    if (!rich_view_ || !rich_view_->selectVisibleContent(visibleIndex)) return false;
    edit();
    return static_cast<bool>(active_dialog_) || rich_view_->editing();
}

bool EventView::debugPlaceContent(const types::CType type, const bool autoOpen,
                                  const std::size_t visibleIndex) {
    if (!rich_view_ || !rich_view_->selectVisibleContent(visibleIndex)) return false;
    auto state = rich_view_->contentToolState();
    state.arrow = false;
    state.type = type;
    state.auto_open = autoOpen;
    rich_view_->applyContentToolState(state);
    return rich_view_->placeSelectedContentTool();
}

bool EventView::debugPasteScript(const StringView script, const bool tryInsert) {
    return rich_view_ && rich_view_->pasteScript(script, tryInsert);
}

void EventView::closeContentCommentDialogs() {
    // forceCancel() が同期的に閉じる通知を出す前に、ローカルで強参照を保持する。
    // D版では、一つの EventTree に複数の Content ダイアログを保持できる。
    auto contentComments = std::move(content_comment_dialogs_);
    content_comment_dialogs_.clear();
    auto eventTreeComment = std::move(event_tree_comment_dialog_);
    event_tree_comment_dialog_.reset();
    auto latestComment = std::move(comment_dialog_);
    comment_dialog_.reset();

    std::vector<std::shared_ptr<eventdialog::ContentCommentDialog>> dialogs;
    dialogs.reserve(contentComments.size() + 2U);
    for (auto& [key, dialog] : contentComments) {
        (void)key;
        if (dialog && std::find(dialogs.begin(), dialogs.end(), dialog) == dialogs.end())
            dialogs.push_back(std::move(dialog));
    }
    if (eventTreeComment &&
        std::find(dialogs.begin(), dialogs.end(), eventTreeComment) == dialogs.end())
        dialogs.push_back(std::move(eventTreeComment));
    if (latestComment &&
        std::find(dialogs.begin(), dialogs.end(), latestComment) == dialogs.end())
        dialogs.push_back(std::move(latestComment));
    for (const auto& dialog : dialogs) dialog->forceCancel();
}

void EventView::closeActiveDialog() {
    if (closing_active_dialog_) return;
    // D版と同様、所有者の破棄を一つの終了処理として扱う。C++版のモー
    // ドレスダイアログの 同期的な終了コールバックが、他のダイアログを処
    // 理する前に ホストの無効化処理へ再入しても、この制約を維持する。
    closing_active_dialog_ = true;
    const auto closing_lifetime = std::weak_ptr<int>(command_lifetime_);
    struct ResetClosing {
        bool* value;
        std::weak_ptr<int> lifetime;
        ~ResetClosing() {
            if (!lifetime.expired()) *value = false;
        }
    } reset_closing{&closing_active_dialog_, closing_lifetime};

    auto tree_dialogs = std::move(tree_dialogs_);
    tree_dialogs_.clear();
    for (const auto& [key, dialog] : tree_dialogs) {
        (void)key;
        if (dialog) dialog->close();
    }
    closeContentCommentDialogs();
    if (content_dialogs_.empty()) {
        active_dialog_.reset();
        return;
    }

    // forceCancel() がネイティブの同期的な終了コールバックを完了するまで、ダイ
    // アログを保持する。 特に、EventDialog::finishClose() の実行
    // 中に closeEvent が最後の shared_ptr を破棄しないようにする。
    std::vector<contentinitializer::EventDialogPtr> dialogs;
    dialogs.reserve(content_dialogs_.size());
    for (const auto& [key, dialog] : content_dialogs_) {
        (void)key;
        if (dialog) dialogs.push_back(dialog);
    }
    for (const auto& dialog : dialogs) dialog->forceCancel();
    content_dialogs_.clear();
    active_dialog_.reset();
}

void EventView::cutOne() {
    if (callbacks_.cutOne) callbacks_.cutOne();
    else if (rich_view_) (void)rich_view_->cut1Content();
}

void EventView::copyOne() {
    if (callbacks_.copyOne) callbacks_.copyOne();
    else if (rich_view_) (void)rich_view_->copy1Content();
}

void EventView::deleteOne() {
    if (callbacks_.deleteOne) callbacks_.deleteOne();
    else if (rich_view_) (void)rich_view_->del1Content();
    else tree_view_.removeSelection();
}

void EventView::pasteInsert() {
    if (callbacks_.pasteInsert) callbacks_.pasteInsert();
    else if (rich_view_) (void)rich_view_->pasteInsert();
}

void EventView::pasteInsert(const lib::CommandGesture gesture) {
    if (callbacks_.pasteInsertWithGesture)
        callbacks_.pasteInsertWithGesture(gesture);
    else if (callbacks_.pasteInsert)
        callbacks_.pasteInsert();
    else if (rich_view_)
        (void)rich_view_->pasteInsert(gesture);
    else
        pasteInsert();
}

void EventView::swapToParent() {
    if (callbacks_.swapToParent) callbacks_.swapToParent();
    else if (rich_view_) (void)rich_view_->swapToParent();
}

void EventView::swapToChild() {
    if (callbacks_.swapToChild) callbacks_.swapToChild();
    else if (rich_view_) (void)rich_view_->swapToChild();
}

void EventView::selectCurrentEvent() {
    if (callbacks_.selectCurrentEvent) callbacks_.selectCurrentEvent();
    else if (rich_view_) rich_view_->selectCurrentEvent();
    else tree_view_.selectCurrentEvent();
}

void EventView::cut() {
    if (callbacks_.cut) callbacks_.cut();
    else if (rich_view_) (void)rich_view_->cut();
    else {
        copy();
        deleteOne();
    }
}

void EventView::copy() {
    if (callbacks_.copy) callbacks_.copy();
    else if (rich_view_) (void)rich_view_->copy();
    clipboard_available_ = true;
}

void EventView::paste() {
    if (callbacks_.paste) callbacks_.paste();
    else if (rich_view_) (void)rich_view_->paste();
    else pasteInsert();
}

void EventView::del() {
    if (callbacks_.del) callbacks_.del();
    else if (rich_view_) {
        if (!rich_view_->canDoD()) return;
        if (rich_view_->rightPaneActive()) (void)rich_view_->deleteContent();
        else if (const auto fire = rich_view_->selectionFire())
            (void)rich_view_->removeFire(*fire);
        else (void)rich_view_->deleteEvent();
    }
    else deleteOne();
}

void EventView::clone() {
    if (callbacks_.clone) callbacks_.clone();
    else if (rich_view_) (void)rich_view_->clone();
    else tree_view_.cloneSelection();
}

bool EventView::canWriteComment() const {
    if (read_only_) return false;
    if (callbacks_.canWriteComment) return callbacks_.canWriteComment();
    if (!rich_view_) return tree_view_.hasSelection();
    return rich_view_->canWriteComment();
}

bool EventView::canToScript() const {
    return callbacks_.toScript
               ? (callbacks_.canToScript ? callbacks_.canToScript()
                                         : rich_view_ && static_cast<bool>(
                                               scriptSelection(*rich_view_)))
               : rich_view_ && static_cast<bool>(scriptSelection(*rich_view_));
}

bool EventView::canToScriptOne() const {
    return callbacks_.toScriptOne
               ? (callbacks_.canToScript ? callbacks_.canToScript()
                                         : rich_view_ && static_cast<bool>(
                                               scriptSelection(*rich_view_)))
               : rich_view_ && static_cast<bool>(scriptSelection(*rich_view_));
}

bool EventView::canToScriptAll() const {
    return callbacks_.toScriptAll
               ? (callbacks_.canToScriptAll ? callbacks_.canToScriptAll()
                                            : rich_view_ && static_cast<bool>(
                                                  rich_view_->displayedEventTree()))
               : rich_view_ && static_cast<bool>(rich_view_->displayedEventTree());
}

bool EventView::canWrapTree() const {
    return rich_view_ && rich_view_->canWrapTree();
}

bool EventView::canStartToPackage() const {
    return !read_only_ &&
           (callbacks_.canStartToPackage ? callbacks_.canStartToPackage()
                                         : context_.editor_commons && rich_view_ &&
                                               rich_view_->selectionContent() &&
                                               rich_view_->selectionContent()->parentStart());
}

bool EventView::canEventToPackage() const {
    return !read_only_ &&
           (callbacks_.canEventToPackage ? callbacks_.canEventToPackage()
                                         : context_.editor_commons && rich_view_ &&
                                               rich_view_->selectionEventTree());
}

bool EventView::canAddManyRounds() const {
    return !read_only_ &&
           (callbacks_.canAddManyRounds ? callbacks_.canAddManyRounds()
                                        : rich_view_ &&
                                              dynamic_cast<area::Battle*>(owner_) &&
                                              rich_view_->selectionParent() == owner_ &&
                                              rich_view_->selectionEventTree());
}

bool EventView::canSelectConnectedResource() const {
    if (callbacks_.canSelectConnectedResource)
        return callbacks_.canSelectConnectedResource();
    if (!rich_view_ || !context_.editor_commons) return false;
    // D版と同様、所有者への移動は左側の Cards の選択から行う。 所有者を
    // 編集しないため、読み取り専用の EventWindow でも利用できる。
    return selectedConnectedOwner(rich_view_.get()) != nullptr;
}

bool EventView::canSelectConnectedContentResource() const {
    if (host_retired_ || read_only_ || !rich_view_ || !summary_ ||
        !context_.editor_commons || !context_.editor_props)
        return false;
    const auto content = rich_view_->selectionContent();
    if (!content) return false;
    if (content->connectedResource(summary_.get())) return true;
    const auto file = content->connectedFile();
    if (summary_->hasMaterial(file, context_.editor_props->var().ignorePaths.value))
        return true;
    return !content->connectedCoupons().empty() ||
           !content->connectedGossips().empty() ||
           !content->connectedCompleteStamps().empty() ||
           !content->connectedKeyCodes().empty() ||
           !content->connectedCellNames().empty() ||
           !content->connectedCardGroups().empty();
}

void EventView::selectConnectedContentResource() {
    if (!canSelectConnectedContentResource()) return;
    const auto content = rich_view_->selectionContent();
    const auto summary = summary_;
    auto* const commons = context_.editor_commons;
    const auto commons_lifetime = commons->lifetimeToken();
    const std::weak_ptr<int> view_lifetime = command_lifetime_;

    // D版の右側の操作と同様、選択 Content の CWXPath、素材、関
    // 連する各ID種別の順に優先する。 左側の所有者への移動は、上の EventView::selectConnectedResource()
    // に残す。
    if (const auto target = content->connectedResource(summary.get())) {
        auto path_value = path::cpaddattr(target->cwxPath(true), "shallow");
        path_value = path::cpaddattr(path_value, "only");
        (void)commons->openCWXPath(std::move(path_value), false);
        return;
    }
    const auto file = content->connectedFile();
    const auto ignore_paths = context_.editor_props->var().ignorePaths.value;
    if (summary->hasMaterial(file, ignore_paths)) {
        (void)commons->openFilePath(file, false, true);
        return;
    }

    const auto coupons = content->connectedCoupons();
    const auto gossips = content->connectedGossips();
    const auto complete_stamps = content->connectedCompleteStamps();
    const auto key_codes = content->connectedKeyCodes();
    const auto cell_names = content->connectedCellNames();
    const auto card_groups = content->connectedCardGroups();
    const auto select_names = [&](const auto& ids) {
        if (!ids.empty() && !view_lifetime.expired() && !commons_lifetime.expired())
            commons->selectIDName(ids, false);
    };
    select_names(coupons);
    select_names(gossips);
    select_names(complete_stamps);
    select_names(key_codes);
    select_names(cell_names);
    select_names(card_groups);
}

bool EventView::canFindStartUsers() const {
    return callbacks_.canFindStartUsers ? callbacks_.canFindStartUsers()
                                        : rich_view_ ? rich_view_->canFindStartUsers()
                                                     : tree_view_.hasSelection();
}

bool EventView::canEdit() const {
    return !read_only_ &&
           (callbacks_.canEdit ? callbacks_.canEdit()
                               : rich_view_ ? rich_view_->canEdit()
                                            : tree_view_.hasSelection());
}

bool EventView::canCutOne() const {
    return !read_only_ &&
           (callbacks_.canCutOne ? callbacks_.canCutOne()
                                 : rich_view_ ? rich_view_->canCut1Content()
                                              : tree_view_.hasSelection());
}

bool EventView::canCopyOne() const {
    return callbacks_.canCopyOne ? callbacks_.canCopyOne()
                                  : rich_view_ ? rich_view_->canCopy1Content()
                                               : tree_view_.hasSelection();
}

bool EventView::canDeleteOne() const {
    return !read_only_ &&
           (callbacks_.canDeleteOne ? callbacks_.canDeleteOne()
                                    : rich_view_ ? rich_view_->canDel1Content()
                                                 : tree_view_.hasSelection());
}

bool EventView::canPasteInsert() const {
    return !read_only_ &&
           (callbacks_.canPasteInsert ? callbacks_.canPasteInsert()
                                      : rich_view_ ? rich_view_->canPasteInsert()
                                                   : clipboard_available_);
}

bool EventView::canSwapToParent() const {
    return !read_only_ &&
           (callbacks_.canSwapToParent ? callbacks_.canSwapToParent()
                                       : rich_view_ ? rich_view_->canSwapToParent()
                                                    : tree_view_.canUp());
}

bool EventView::canSwapToChild() const {
    return !read_only_ &&
           (callbacks_.canSwapToChild ? callbacks_.canSwapToChild()
                                      : rich_view_ ? rich_view_->canSwapToChild()
                                                   : tree_view_.canDown());
}

bool EventView::canSelectCurrentEvent() const {
    return callbacks_.canSelectCurrentEvent
               ? callbacks_.canSelectCurrentEvent()
               : rich_view_ && rich_view_->canSelectCurrentEvent();
}

bool EventView::canDoTCPD() const {
    return !read_only_;
}

bool EventView::canDoT() const {
    return rich_view_ && !callbacks_.canCutOne ? rich_view_->canDoT() : canCutOne();
}
bool EventView::canDoC() const {
    return rich_view_ && !callbacks_.canCopyOne ? rich_view_->canDoC() : canCopyOne();
}
bool EventView::canDoP() const {
    return !read_only_ &&
           (callbacks_.canPasteInsert ? callbacks_.canPasteInsert()
                                      : rich_view_ ? rich_view_->canDoP()
                                                   : clipboard_available_);
}
bool EventView::canDoD() const {
    return rich_view_ && !callbacks_.canDeleteOne ? rich_view_->canDoD() : canDeleteOne();
}
bool EventView::canDoClone() const {
    return rich_view_ ? rich_view_->canDoClone()
                      : !read_only_ && tree_view_.hasSelection();
}

bool EventView::openCWXPath(StringView value, bool windowActivate) {
    if (host_retired_) return false;
    bool opened = false;
    if (callbacks_.openPath) opened = callbacks_.openPath(value, windowActivate);
    else if (!lib::editor_window_host_options().all_mode && owner_ && rich_view_ &&
             std::dynamic_pointer_cast<background::BgImage>(owner_->findCWXPath(value))) {
        // D版と同様、素材検索のパスでは、明示的な opendialog 指定がなくても
        // 所有する ChangeBgImage の編集画面を開き、内部のセルを選択する。
        const auto leaf = path::cplast(value);
        const auto parent_path = path::cpparent(value);
        const auto content = std::dynamic_pointer_cast<event::Content>(
            owner_->findCWXPath(parent_path));
        const auto cell = owner_->findCWXPath(value);
        if (read_only_ || path::cpcategory(leaf) != "background" || !content ||
            content->type() != event::CType::ChangeBgImage ||
            content->findCWXPath(leaf) != cell) return false;
        const std::weak_ptr<int> life = command_lifetime_;
        if (!rich_view_->openCWXPath(path::cpaddattr(parent_path, "opendialog"),
                                    windowActivate) || life.expired()) return false;
        if (host_retired_) return false;
        const auto dialog = std::dynamic_pointer_cast<eventdialog::BgImagesDialog>(active_dialog_);
        if (!dialog || dialog->event() != content || !dialog->isOpen()) return false;
        opened = dialog->openCWXPath(leaf, windowActivate);
        if (life.expired()) return false;
    }
    else if (owner_ && rich_view_ &&
             std::dynamic_pointer_cast<card::BeastCard>(owner_->findCWXPath(value))) {
        // D版と同様、内部の効果モーションを指すパスでは、所有する Effect の編集画面を開き、 残りの
        // Motion/Beast 接尾辞を EffectDialog から MotionView に渡す。
        const auto parent_motion_path = path::cpparent(value);
        const auto content_path = path::cpbody(path::cpparent(parent_motion_path));
        const auto motion_path = path::cprel(value, content_path);
        const auto target = owner_->findCWXPath(value);
        const auto content = std::dynamic_pointer_cast<event::Content>(
            owner_->findCWXPath(content_path));
        if (!target || !content ||
            content->type() != event::CType::Effect || motion_path.empty() ||
            path::cpcategory(path::cptop(motion_path)) != "motion" ||
            content->findCWXPath(motion_path) != target) return false;

        const std::weak_ptr<int> life = command_lifetime_;
        if (read_only_) {
            // D版の EventTreeView::edit と同様、読み取り専用ビューで
            // は null を返す。 パスによる移動で Effect コンテントは選択する
            // が、新しいダイアログを開いたり MotionView へ移動したりはしない。
            opened = rich_view_->openCWXPath(content_path, windowActivate);
            if (life.expired()) return false;
        } else {
            if (!rich_view_->openCWXPath(path::cpaddattr(content_path, "opendialog"),
                                        windowActivate) || life.expired()) return false;
            if (host_retired_) return false;
            const auto dialog = std::dynamic_pointer_cast<eventdialog::EffectDialog>(
                active_dialog_);
            if (!dialog || dialog->event() != content || !dialog->isOpen()) return false;
            opened = dialog->openCWXPath(motion_path, windowActivate);
            if (life.expired()) return false;
        }
    }
    else if (rich_view_) opened = rich_view_->openCWXPath(value, windowActivate);
    else if (owner_) opened = static_cast<bool>(owner_->findCWXPath(value));
    if (opened) opened_paths_.emplace_back(value);
    return opened;
}

EventWindow::EventWindowCallbacks EventWindow::defaultCallbacks() {
    return {};
}

EventWindow::EventWindow(Context context, SummaryPtr summary,
                         EventOwnerHandle owner, Parent parent,
                         UndoManager* undoManager, bool readOnly,
                         EventWindowCallbacks callbacks,
                         OperationalHistory operationalHistory)
    : context_(std::move(context)),
      summary_(std::move(summary)),
      owner_(std::move(owner)),
      undo_(undoManager),
      read_only_(readOnly),
      ref_undo_(undoManager == nullptr),
      callbacks_(std::move(callbacks)) {
    if (owner_.empty()) {
        owner_ = EventOwnerHandle::from(
            std::make_shared<event::AbstractEventTreeOwner>());
    }
    context_.referenceArea = dynamic_cast<area::AbstractArea*>(owner_.resource().get());
    window_ = std::make_shared<lib::Panel>();
    window_->set_layout(std::make_shared<lib::FillLayout>());
    lib::LayoutItem windowItem;
    windowItem.minimum = {240.0F, 180.0F};
    windowItem.grow = 1.0F;
    windowItem.horizontal = lib::Align::Stretch;
    windowItem.vertical = lib::Align::Stretch;
    window_->set_layout_item(windowItem);
    if (parent) parent->add(window_);

    if (operationalHistory && operationalHistory->core) {
        undo_ = operationalHistory->core.get();
        ref_undo_ = true;
    }
    if (!undo_) {
        owned_undo_ = std::make_unique<UndoManager>(configuredUndoMax(context_));
        undo_ = owned_undo_.get();
    }
    refUndoMax();

    operational_history_ = operationalHistory ? std::move(operationalHistory)
        : std::make_shared<OperationalHistoryState>();
    if (!operational_history_->manager)
        operational_history_->manager = std::make_shared<rich_eventview::UndoManager>(configuredUndoMax(context_));
    event_view_ = std::make_shared<EventView>(
        context_, *owner_.get(), summary_, window_, *undo_, read_only_, callbacks_.view,
        operational_history_);
    event_view_->retainHistoryOwner(owner_.owner());
    operational_history_->views.push_back(event_view_);
    if (auto* rich = event_view_->richView()) {
        rich->historyModelOwner(owner_.owner());
        // makeRichCommons は、弱参照を使うグローバルな Commons の削除通知処理を登録する。
        rich->historyModelDeleting(rich->commons().deletingContent);
        if (auto* shared=context_.editor_commons) {
            // D版の CardPane は ref/delEventTree を監視し
            // 、カードのイベント印を更新する。 C++版のカードイベントは EventTreeOwner
            // アダプターを使うため、 保持している実際のカードを標準のカード
            // 変更通知経路へ渡す。 モデル単位の履歴再適用でも、元のイベント画
            // 面が閉じている場合に 存続するカード一覧を更新する必要がある。
            const auto refreshCard = [shared, life=shared->lifetimeToken(),
                    summary=std::weak_ptr<Summary>(summary_),
                    owner=std::weak_ptr<card::Card>(
                        std::dynamic_pointer_cast<card::Card>(owner_.resource()))] {
                const auto source = summary.lock();
                const auto value = owner.lock();
                if (life.expired() || !source || shared->summary()!=source || !value) return;
                if (const auto skill = std::dynamic_pointer_cast<card::SkillCard>(value))
                    shared->refSkill.call(skill);
                else if (const auto item = std::dynamic_pointer_cast<card::ItemCard>(value))
                    shared->refItem.call(item);
                else if (const auto beast = std::dynamic_pointer_cast<card::BeastCard>(value))
                    shared->refBeast.call(beast);
            };
            rich->modelChanged.add([origin=std::weak_ptr<EventView>(event_view_), refreshCard] {
                const auto view = origin.lock();
                if (view && !view->hostRetired()) refreshCard();
            });
            rich->historyModelNotify([shared,life=shared->lifetimeToken(),
                                     summary=std::weak_ptr<Summary>(summary_), refreshCard] {
                const auto source=summary.lock();
                if (life.expired() || !source || shared->summary()!=source) return;
                refreshCard();
                if (life.expired() || shared->summary()!=source) return;
                shared->refUseCount.call();
                if (!life.expired() && shared->summary()==source) shared->refreshToolBar();
            });
        }
        rich->historyOrigin([weak = std::weak_ptr<EventView>(event_view_)]() -> std::shared_ptr<void> {
            return weak.lock();
        });
        if (!lib::editor_window_host_options().all_mode &&
            (owner_.kind() == OwnerKind::Area || owner_.kind() == OwnerKind::Battle)) {
            const auto history = std::weak_ptr<OperationalHistoryState>(operational_history_);
            const auto gui = std::weak_ptr<lib::GuiBackend>(context_.services.gui);
            const auto origin = std::weak_ptr<EventView>(event_view_);
            const auto readHistorySelection = [history, gui, origin](const bool requireOrigin)
                -> std::optional<rich_eventview::EventView::SelectionSnapshot> {
                struct LivePeer {
                    std::shared_ptr<EventView> wrapper;
                    rich_eventview::EventView* rich{};
                };
                const auto shared = history.lock();
                const auto source = origin.lock();
                if (!shared) return std::nullopt;
                if (requireOrigin && (!source || source->hostRetired())) return std::nullopt;
                auto* const originRich = source && !source->hostRetired() ? source->richView() : nullptr;
                if (requireOrigin && (!originRich || source->hostRetired())) return std::nullopt;

                // 関連ビューの選択を読む間は、各ラッパーの寿命を保持する。 フォー
                // カスは確定済みの通知キャッシュから取得し、ホストは呼び出さない。
                std::vector<LivePeer> peers;
                for (const auto& weak : shared->views) {
                    auto wrapper = weak.lock();
                    if (!wrapper || wrapper->hostRetired()) continue;
                    auto* peer = wrapper->richView();
                    if (!peer || wrapper->hostRetired() || wrapper->richView() != peer) continue;
                    peers.push_back({std::move(wrapper), peer});
                }
                if (peers.empty()) return std::nullopt;
                const auto first = peers.front(); // D版と同様、該当するものがなければ views[0] を使う。

                // FocusObserver は、確定した共通のフォーカス変更によって更新される
                // 。 ウィジェットの弱参照のロックはコールバックを呼ばないローカルな読み取りで済
                // む。 変更後のスナップショット取得経路から、GUIの仮想取得関数を呼ばない。
                const auto callerGui = gui.lock();
                const auto focused = callerGui && shared->focus_gui.lock() == callerGui
                    ? shared->focused_widget.lock() : lib::WidgetPtr{};
                if (requireOrigin && (source->hostRetired() || source->richView() != originRich))
                    return std::nullopt;
                if (focused) {
                    for (const auto& peer : peers) {
                        if (peer.wrapper->hostRetired() ||
                            peer.wrapper->richView() != peer.rich) continue;
                        if (!lib::contains_widget(peer.wrapper->widget(), focused.get())) continue;
                        if (peer.wrapper->hostRetired() ||
                            peer.wrapper->richView() != peer.rich) return std::nullopt;
                        return peer.rich->selectionSnapshot(false);
                    }
                }
                if (first.wrapper->hostRetired() ||
                    first.wrapper->richView() != first.rich) return std::nullopt;
                return first.rich->selectionSnapshot(false);
            };
            rich->historySelectionSource([readHistorySelection] { return readHistorySelection(true); });
            // 共有履歴の再適用でも元の画面の存続を要求せず、 D版と同様
            // に、その時点で存続しているビューから選択状態を取得する。
            rich->historyReplaySelectionSource([readHistorySelection] { return readHistorySelection(false); });
            rich->historySelectionRestorer([history](
                const rich_eventview::EventView::SelectionSnapshot& selection) {
                const auto shared = history.lock();
                if (!shared) return;
                const auto peers = shared->views;
                // D版の EVUndo と同様、存続するすべてのビューで記録済みのパスを復元する。
                // フォーカスの選別はスナップショット取得時だけに行い、再適用時には行わない。 restoreHistorySelectionForReplay
                // はホストのコールバックを呼ばずに モデルに対する
                // ローカルな選択を変更し、表示の更新はその後で行う。
                for (const auto& weak : peers) {
                    auto wrapper = weak.lock();
                    if (!wrapper || wrapper->hostRetired()) continue;
                    auto* peer = wrapper->richView();
                    if (!peer || wrapper->hostRetired() || wrapper->richView() != peer) continue;
                    peer->restoreHistorySelectionForReplay(selection);
                }
            });
        }
        if (owner_.kind() != OwnerKind::Unknown) {
            // D版の EventTreeView.views() と同様、別のイベン
            // トツリーを表示するペインも含め、 同じ所有者の全ビューを更新する。左側
            // 一覧への貼り付けは通知前に新しいツリーを選ぶため、 ここで選択ツリーに
            // よって絞ると、他の画面の移動用の行が古いまま残る。 MainWindow
            // の関連ビューは Commons から取得する。パネルを介さず直接使
            // 用し、 履歴を共有するビューについては、履歴側の一覧を代わりに使う。
            const auto common = context_.editor_commons;
            const auto commonLife = common ? common->lifetimeToken()
                                           : std::weak_ptr<const int>{};
            const auto ownerResource = owner_.resource();
            rich->modelChanged.add([
                weak = std::weak_ptr<OperationalHistoryState>(operational_history_),
                origin = std::weak_ptr<EventView>(event_view_), common, commonLife,
                ownerResource] {
                const auto source = origin.lock();
                auto* const sourceRich = source ? source->richView() : nullptr;
                if (!source || !sourceRich || source->hostRetired()) return;
                const auto sourceTreeView = sourceRich->eventTreeControl();
                std::vector<commons::PanelPtr> refreshed;
                if (common && !commonLife.expired()) {
                    const auto peers = common->eventViewsFrom(ownerResource, false);
                    // selectedTree のコールバックは、どちらのホストも同期的に無効化する可能性がある。
                    if (commonLife.expired() || source->hostRetired() ||
                        source->richView() != sourceRich) return;
                    for (const auto& peer : peers) {
                        if (!peer) continue;
                        const auto panel = commons::getTopLevelPanel(peer);
                        if (!panel) continue;
                        const auto peerTreeView = panel->viewProjection().treeView;
                        if (peerTreeView == sourceTreeView) continue;
                        if (commonLife.expired() || source->hostRetired() ||
                            source->richView() != sourceRich) return;
                        const auto refresh = panel->viewProjection().refreshEventTreeView;
                        if (!refresh) continue;
                        if (commonLife.expired() || source->hostRetired() ||
                            source->richView() != sourceRich) return;
                        refresh();
                        if (commonLife.expired() || source->hostRetired() ||
                            source->richView() != sourceRich) return;
                        refreshed.push_back(peerTreeView);
                    }
                }
                const auto history = weak.lock();
                if (!history) return;
                const auto peers = history->views;
                for (const auto& peer : peers) {
                    if (source->hostRetired()) return;
                    const auto view = peer.lock();
                    if (!view || view == source || view->hostRetired()) continue;
                    auto* const peerRich = view->richView();
                    if (!peerRich || view->hostRetired() || view->richView() != peerRich) continue;
                    const auto peerTreeView = peerRich->eventTreeControl();
                    if (std::find(refreshed.begin(), refreshed.end(), peerTreeView) != refreshed.end())
                        continue;
                    view->refresh();
                }
            });
        }
        rich->historyReplayed([weak = std::weak_ptr<OperationalHistoryState>(operational_history_)] {
            if (const auto history = weak.lock()) {
                const auto peers = history->views;
                for (const auto& peer : peers)
                    if (const auto view = peer.lock(); view && !view->hostRetired()) view->refresh();
            }
        });
    }
    event_view_->ownerTitle(owner_.name());
    event_view_->setClipboardAvailable(callbacks_.clipboardAvailable);
    // EventView は詳細エディタの構築後に初期更新を完了する。
    // ここで繰り返すと、初回表示前に両方のツリーを再構築してしまう。
    installCommands();
    if (!read_only_ && context_.editor_commons) {
        auto* shared = context_.editor_commons;
        const auto token = shared->replText.add(
            [this, weak = std::weak_ptr<EventView>(event_view_)] {
                const auto view = weak.lock();
                if (view && !view->hostRetired()) refreshTitle();
            });
        replacement_title_disconnect_ = [shared, token, life = shared->lifetimeToken()] {
            if (!life.expired()) shared->replText.remove(token);
        };
    }
    refreshTitle();
    if (!lib::editor_window_host_options().all_mode &&
        (owner_.kind() == OwnerKind::Area || owner_.kind() == OwnerKind::Battle)) {
        if (auto* const rich = event_view_ && !event_view_->hostRetired()
                                   ? event_view_->richView() : nullptr) {
            observeOperationalHistoryFocus(operational_history_, context_.services.gui,
                                           event_view_, rich);
        }
    }
}

EventWindow::EventWindow(
    props::CProps& prop, variables::FlexEtcProps& vars, SummaryPtr summary,
    EventOwnerHandle owner, Parent parent, UndoManager* undoManager,
    bool readOnly, EventWindowCallbacks callbacks,
    OperationalHistory operationalHistory)
    : EventWindow(Context(prop, vars), std::move(summary), std::move(owner),
                  std::move(parent), undoManager, readOnly,
                  std::move(callbacks), std::move(operationalHistory)) {}

EventWindow::EventWindow(
    Context context, SummaryPtr summary,
    const lib::Owner<event::EventTreeOwner>& owner, Parent parent,
    UndoManager* undoManager, bool readOnly, EventWindowCallbacks callbacks,
    OperationalHistory operationalHistory)
    : EventWindow(std::move(context), std::move(summary),
                  EventOwnerHandle::from(owner), std::move(parent),
                  undoManager, readOnly, std::move(callbacks),
                  std::move(operationalHistory)) {}

EventWindow::~EventWindow() { retireHostBindings(); }

void EventWindow::retireHostBindings() {
    const auto disconnect = std::move(replacement_title_disconnect_);
    replacement_title_disconnect_ = {};
    if (disconnect) disconnect();
    if (event_view_) event_view_->retireHostBindings();
    const auto history = std::exchange(operational_history_, {});
    if (!history) return;
    std::erase_if(history->views, [this](const auto& weak) {
        const auto peer = weak.lock();
        return !peer || peer == event_view_ || peer->hostRetired();
    });
    if (history->views.empty()) clearOperationalHistoryFocus(*history);
    // 最後のホストが閉じると、D版に対応する取り消し操作の画面は残らない。
    // 借用したアプリ側の依存先が有効なうちに記録を破棄する。 診断用の呼
    // び出し元が空の管理オブジェクトを保持する場合も同様。 ただし、最後の
    // イベント画面が閉じてもシーンのペインが共通コアを所有している場合があ
    // る。 このペインの所有権を解放しても、他のペインの履歴は消さない。
    if (history->views.empty() && !history->core) history->manager->reset();
}

void EventWindow::refUndoMax() {
    const auto maximum = configuredUndoMax(context_);
    if (ref_undo_ && undo_) undo_->max(maximum);
    if (event_view_) event_view_->setUndoLimit(maximum);
}

bool EventWindow::openScene(bool duplicate) {
    if (owner_.kind() != OwnerKind::Area &&
        owner_.kind() != OwnerKind::Battle) {
        return false;
    }
    if (!callbacks_.openScene) return false;
    callbacks_.openScene(owner_, summary_, duplicate);
    return true;
}

void EventWindow::openDuplicate() {
    if (event_view_) event_view_->openDuplicate();
}

void EventWindow::ownerClosed(const path::CWXPath* resource) {
    if (!matches(resource)) return;
    if (callbacks_.closeWindow) callbacks_.closeWindow();
    if (callbacks_.ownerClosed) callbacks_.ownerClosed(owner_);
}

void EventWindow::ownerRefreshed(const path::CWXPath* resource) {
    if (!matches(resource)) return;
    refreshTitle();
    if (callbacks_.ownerRefreshed) callbacks_.ownerRefreshed(owner_);
}

bool EventWindow::matches(const path::CWXPath* resource) const noexcept {
    if (!resource) return false;
    if (owner_.resource().get() == resource) return true;
    return owner_.get() == resource;
}

void EventWindow::deleteOwner(const event::EventTreeOwner* owner) {
    if (!owner) return;
    ownerClosed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::deleteOwnerArea(const area::Area* owner) {
    if (!owner) return;
    ownerClosed(static_cast<const path::CWXPath*>(
        static_cast<const event::EventTreeOwner*>(owner)));
}

void EventWindow::deleteOwnerBattle(const area::Battle* owner) {
    if (!owner) return;
    ownerClosed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::deleteOwnerPackage(const area::Package* owner) {
    if (!owner) return;
    ownerClosed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::deleteOwnerSkillCard(
    const path::CWXPath*, const card::SkillCard* card) {
    if (!card) return;
    ownerClosed(static_cast<const path::CWXPath*>(card));
}

void EventWindow::deleteOwnerItemCard(
    const path::CWXPath*, const card::ItemCard* card) {
    if (!card) return;
    ownerClosed(static_cast<const path::CWXPath*>(card));
}

void EventWindow::deleteOwnerBeastCard(
    const path::CWXPath*, const card::BeastCard* card) {
    if (!card) return;
    ownerClosed(static_cast<const path::CWXPath*>(card));
}

void EventWindow::refOwner(const event::EventTreeOwner* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::refOwnerArea(const area::Area* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(
        static_cast<const event::EventTreeOwner*>(owner)));
}

void EventWindow::refOwnerBattle(const area::Battle* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::refOwnerPackage(const area::Package* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::refOwnerSkillCard(const card::SkillCard* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::refOwnerItemCard(const card::ItemCard* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::refOwnerBeastCard(const card::BeastCard* owner) {
    if (!owner) return;
    ownerRefreshed(static_cast<const path::CWXPath*>(owner));
}

void EventWindow::closeAdds(const Summary* summary) {
    if (!summary || summary_.get() != summary) return;
    if (callbacks_.closeWindow) callbacks_.closeWindow();
    if (callbacks_.summaryClosed) callbacks_.summaryClosed(summary_);
}

ResourceId EventWindow::image() const {
    return callbacks_.image ? callbacks_.image(owner_.kind()) : ResourceId{};
}

SkinPtr EventWindow::summarySkin() const {
    return context_.findSkin(summary_.get());
}

String EventWindow::title() const {
    if (callbacks_.title) return callbacks_.title(owner_);

    const auto label = ownerLabel(context_.prop, owner_.kind());
    // D版と同様、タイトル更新時に現在のカードを読む。 開いた時点の表示
    // スナップショットによって、番号や名前の変更が隠れないようにする。
    const auto liveCard = std::dynamic_pointer_cast<card::Card>(owner_.resource());
    const auto liveArea = std::dynamic_pointer_cast<area::AbstractArea>(owner_.resource());
    const auto id = liveCard ? numberString(liveCard->id())
        : liveArea ? numberString(liveArea->id())
        : owner_.id().empty() && owner_.resource()
            ? owner_.resource()->cwxPath(true) : owner_.id();
    const auto name = liveCard ? liveCard->name() : liveArea ? liveArea->name() : owner_.name();
    if (context_.prop) {
        return utils::tryFormat(context_.prop->requiredMsgs().viewNameEventTab.value,
                                label, id, name);
    }
    return label + "." + id + (name.empty() ? String{} : " " + name);
}

void EventWindow::refreshTitle() {
    const auto view = event_view_;
    const auto resource = owner_.resource();
    const auto liveCard = std::dynamic_pointer_cast<card::Card>(resource);
    const auto liveArea = std::dynamic_pointer_cast<area::AbstractArea>(resource);
    const auto name = liveCard ? liveCard->name() : liveArea ? liveArea->name() : owner_.name();
    title_ = title();
    const auto updated = title_;
    const auto publish = callbacks_.titleChanged;
    if (view && !view->hostRetired()) view->ownerTitle(name);
    if (publish) publish(updated);
    // 公開されたネイティブのタイトル・所有者更新処理は、ホストを同期的に無効化する場合がある。
    if (view && !view->hostRetired()) view->refreshTitle();
}

void EventWindow::movingWindow() {
    if (event_view_) event_view_->movingWindow();
}

void EventWindow::moveWindow() {
    if (event_view_) event_view_->moveWindow();
}

void EventWindow::cut(CommandEvent& event) {
    if (!canDoT()) return;
    event_view_->cut();
    event.handled = true;
}

void EventWindow::copy(CommandEvent& event) {
    if (!canDoC()) return;
    event_view_->copy();
    event.handled = true;
}

void EventWindow::paste(CommandEvent& event) {
    if (!canDoP()) return;
    event_view_->paste();
    event.handled = true;
}

void EventWindow::del(CommandEvent& event) {
    if (!canDoD()) return;
    event_view_->del();
    event.handled = true;
}

void EventWindow::clone(CommandEvent& event) {
    if (!canDoClone()) return;
    event_view_->clone();
    event.handled = true;
}

void EventWindow::cut() {
    CommandEvent event;
    cut(event);
}

void EventWindow::copy() {
    CommandEvent event;
    copy(event);
}

void EventWindow::paste() {
    CommandEvent event;
    paste(event);
}

void EventWindow::del() {
    CommandEvent event;
    del(event);
}

void EventWindow::clone() {
    CommandEvent event;
    clone(event);
}

bool EventWindow::canDoTCPD() const {
    return event_view_ && event_view_->canDoTCPD();
}

bool EventWindow::canDoT() const {
    return event_view_ && event_view_->canDoT();
}

bool EventWindow::canDoC() const {
    return event_view_ && event_view_->canDoC();
}

bool EventWindow::canDoP() const {
    return event_view_ && event_view_->canDoP();
}

bool EventWindow::canDoD() const {
    return event_view_ && event_view_->canDoD();
}

bool EventWindow::canDoClone() const {
    return event_view_ && event_view_->canDoClone();
}

bool EventWindow::openCWXPath(StringView value, bool windowActivate) {
    return event_view_ && event_view_->openCWXPath(value, windowActivate);
}

lib::Array<String> EventView::openedCWXPath() const {
    if (host_retired_) return {};
    return rich_view_ ? rich_view_->openedCWXPath() : opened_paths_;
}

lib::Array<String> EventWindow::openedCWXPath() const {
    return event_view_ ? event_view_->openedCWXPath() : lib::Array<String>{};
}

void EventWindow::installCommands() {
    const auto sceneEnabled = [this]() {
        return owner_.kind() == OwnerKind::Area ||
               owner_.kind() == OwnerKind::Battle;
    };
    const auto noArgs = [](lib::Action action) { return action; };
    (void)noArgs;

    commands_[Command::EditScene] = {
        [this] { (void)openScene(false); }, sceneEnabled};
    commands_[Command::EditSceneDuplicate] = {
        [this] { (void)openScene(true); }, sceneEnabled};
    commands_[Command::EditEventDuplicate] = {
        [this] { openDuplicate(); },
        [this] { return event_view_ && event_view_->canOpenDuplicate(); }};
    commands_[Command::Undo] = {
        [this] { if (event_view_) event_view_->undo(); },
        [this] { return event_view_ && event_view_->canUndo(); }};
    commands_[Command::Redo] = {
        [this] { if (event_view_) event_view_->redo(); },
        [this] { return event_view_ && event_view_->canRedo(); }};
    commands_[Command::Up] = {
        [this] { if (event_view_) event_view_->up(); },
        [this] { return event_view_ && event_view_->canUp(); }};
    commands_[Command::Down] = {
        [this] { if (event_view_) event_view_->down(); },
        [this] { return event_view_ && event_view_->canDown(); }};
    commands_[Command::Comment] = {
        [this] { if (event_view_) event_view_->writeComment(); },
        [this] { return event_view_ && event_view_->canWriteComment(); }};
    commands_[Command::ToScript] = {
        [this] { if (event_view_) event_view_->toScript(); },
        [this] { return event_view_ && event_view_->canToScript(); }};
    commands_[Command::ToScriptOne] = {
        [this] { if (event_view_) event_view_->toScriptOne(); },
        [this] { return event_view_ && event_view_->canToScriptOne(); }};
    commands_[Command::ToScriptAll] = {
        [this] { if (event_view_) event_view_->toScriptAll(); },
        [this] { return event_view_ && event_view_->canToScriptAll(); }};
    commands_[Command::EventToPackage] = {
        [this] { if (event_view_) event_view_->eventToPackage(); },
        [this] { return event_view_ && event_view_->canEventToPackage(); }};
    commands_[Command::StartToPackage] = {
        [this] { if (event_view_) event_view_->startToPackage(); },
        [this] { return event_view_ && event_view_->canStartToPackage(); }};
    commands_[Command::WrapTree] = {
        [this] { if (event_view_) event_view_->wrapTree(); },
        [this] { return event_view_ && event_view_->canWrapTree(); }};
    commands_[Command::SelectConnectedResource] = {
        [this] { if (event_view_) event_view_->selectConnectedResource(); },
        [this] { return event_view_ && event_view_->canSelectConnectedResource(); }};
    commands_[Command::FindId] = {
        [this] { if (event_view_) event_view_->findStartUsers(); },
        [this] { return event_view_ && event_view_->canFindStartUsers(); }};
    commands_[Command::EditProperty] = {
        [this] { if (event_view_) event_view_->edit(); },
        [this] { return event_view_ && event_view_->canEdit(); }};
    commands_[Command::CutOne] = {
        [this] { if (event_view_) event_view_->cutOne(); },
        [this] { return event_view_ && event_view_->canCutOne(); }};
    commands_[Command::CopyOne] = {
        [this] { if (event_view_) event_view_->copyOne(); },
        [this] { return event_view_ && event_view_->canCopyOne(); }};
    commands_[Command::DeleteOne] = {
        [this] { if (event_view_) event_view_->deleteOne(); },
        [this] { return event_view_ && event_view_->canDeleteOne(); }};
    commands_[Command::PasteInsert] = {
        [this] { if (event_view_) event_view_->pasteInsert(); },
        [this](const lib::CommandGesture gesture) {
            if (event_view_) event_view_->pasteInsert(gesture);
        },
        [this] { return event_view_ && event_view_->canPasteInsert(); }};
    commands_[Command::SwapToParent] = {
        [this] { if (event_view_) event_view_->swapToParent(); },
        [this] { return event_view_ && event_view_->canSwapToParent(); }};
    commands_[Command::SwapToChild] = {
        [this] { if (event_view_) event_view_->swapToChild(); },
        [this] { return event_view_ && event_view_->canSwapToChild(); }};
    commands_[Command::SelectCurrentEvent] = {
        [this] { if (event_view_) event_view_->selectCurrentEvent(); },
        [this] { return event_view_ && event_view_->canSelectCurrentEvent(); }};
}

bool EventWindow::invoke(Command command) {
    return invokeImpl(command, nullptr);
}

bool EventWindow::invoke(Command command, const lib::CommandGesture gesture) {
    return invokeImpl(command, &gesture);
}

bool EventWindow::invokeImpl(
    const Command command, const lib::CommandGesture* const gesture) {
    const auto found = commands_.find(command);
    if (found == commands_.end()) return false;
    if (found->second.enabled && !found->second.enabled()) return false;
    if (gesture && found->second.invoke_with_gesture)
        found->second.invoke_with_gesture(*gesture);
    else if (found->second.invoke)
        found->second.invoke();
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventwindow
